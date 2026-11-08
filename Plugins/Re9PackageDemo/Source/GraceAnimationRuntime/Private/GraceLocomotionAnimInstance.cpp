#include "GraceLocomotionAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "GraceRunStopMatching.h"

namespace
{
    const FName LocomotionMachine(TEXT("Locomotion"));
    const FName RunState(TEXT("Run"));
    const FName RunStartState(TEXT("RunStart"));
    const FName RunStopState(TEXT("RunStop"));
    const FName RunTurnState(TEXT("RunTurn"));
    const FName RootBone(TEXT("root"));
    const FName LeftFootBone(TEXT("L_Leg_Foot"));
    const FName RightFootBone(TEXT("R_Leg_Foot"));

    constexpr float SourceFramesPerSecond = 60.0f;
    constexpr float BackTurnThresholdDegrees = 135.0f;
    constexpr float BackTurnInputThreshold = 0.800000011920929f;
    constexpr float StopCancelFrame = 40.0f;
    constexpr float TurnCancelFrame = 30.0f;
    constexpr float TurnFastCancelFrame = 65.0f;
    constexpr float BackTurnEndOffsetFrames = 35.0f;
    constexpr float SideTurnEndOffsetFrames = 30.0f;
    constexpr float SideCancelEndOffsetFrames = 20.0f;
    constexpr float SideTurnThresholdDegrees = 40.0f;
    constexpr float SideTurnIgnorePreviousDegrees = 95.0f;
    constexpr float SideCancelThresholdDegrees = 10.0f;
    constexpr float ForwardInputThresholdDegrees = 20.0f;
    constexpr float FastCancelDirectionChangeDegrees = 20.0f;

    FVector3f ToSourceModelPosition(const FTransform& Root, const FTransform& Foot)
    {
        // Current importer: root scale 100 supplies the centimetre-to-metre
        // conversion. Root-local Y is reflected; source axes are X/Y/Z (up Y).
        const FVector Local = Root.InverseTransformPosition(Foot.GetTranslation());
        return FVector3f(static_cast<float>(Local.X), -static_cast<float>(Local.Y), static_cast<float>(Local.Z));
    }

    FVector2f FootVelocity(const FVector3f& Current, const FVector3f& Previous, float DeltaFrames)
    {
        // Source query emits zero velocity for a nonpositive query time step.
        if (DeltaFrames <= 0.0f)
        {
            return FVector2f::ZeroVector;
        }
        const FVector3f Difference = Current - Previous;
        return FVector2f((Difference.X / DeltaFrames) * SourceFramesPerSecond,
            (Difference.Z / DeltaFrames) * SourceFramesPerSecond);
    }
}

void UGraceLocomotionAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
    LeftFootPosition = FVector3f::ZeroVector;
    RightFootPosition = FVector3f::ZeroVector;
    LeftFootVelocityXZ = FVector2f::ZeroVector;
    RightFootVelocityXZ = FVector2f::ZeroVector;
    SampleElapsedSeconds = 0.0f;
    HasFootSample = false;
    RunStopRequested = false;
    RunTurnRequested = false;
    RunTurnFinished = false;
    CanInterruptRunStop = true;
    CanInterruptRunTurn = true;
    AcceptedTurnHeadingDegrees = 0.0f;
    TurnStartActorYawDegrees = 0.0f;
    UseAnimatedTurnYaw = false;
    AnimatedTurnYawDegrees = 0.0f;
    SelectedRunStop = nullptr;
    SelectedRunTurn = nullptr;
    IsRunTurnMirrored = false;
    RunTurnStartSeconds = 0.0f;
    WasPositiveSourceInputAngle = false;
    HasReturnedToForwardInput = false;
    CurrentRunTurn = ERunTurn::RightBack;
}

void UGraceLocomotionAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    SampleElapsedSeconds += DeltaSeconds;
}

void UGraceLocomotionAnimInstance::NativePostEvaluateAnimation()
{
    Super::NativePostEvaluateAnimation();
    if (TObjectPtr<USkeletalMeshComponent> OwningMesh = GetSkelMeshComponent())
    {
        USkeletalMeshComponent& Mesh = *OwningMesh;
        // PostEvaluate runs before the engine flips the read/write pose buffers.
        // The ordinary component-space getter would read the previous pose here.
        // This AnimBP's current contract is the verified 796-bone Grace skeleton.
        const TArray<FTransform>& Pose = Mesh.GetEditableComponentSpaceTransforms();
        const FTransform& Root = Pose[Mesh.GetBoneIndex(RootBone)];
        const FVector3f NextLeft = ToSourceModelPosition(Root, Pose[Mesh.GetBoneIndex(LeftFootBone)]);
        const FVector3f NextRight = ToSourceModelPosition(Root, Pose[Mesh.GetBoneIndex(RightFootBone)]);
        const float DeltaFrames = SampleElapsedSeconds * SourceFramesPerSecond;
        // Initialization has no previous evaluated pose. Zero initial velocity is
        // an explicit UE lifecycle adapter, not an assertion about RE startup.
        LeftFootVelocityXZ = HasFootSample ? FootVelocity(NextLeft, LeftFootPosition, DeltaFrames) : FVector2f::ZeroVector;
        RightFootVelocityXZ = HasFootSample ? FootVelocity(NextRight, RightFootPosition, DeltaFrames) : FVector2f::ZeroVector;
        LeftFootPosition = NextLeft;
        RightFootPosition = NextRight;
        HasFootSample = true;
        SampleElapsedSeconds = 0.0f;
    }
}

float UGraceLocomotionAnimInstance::GetStateAnimationTime(FName StateName)
{
    return GetInstanceAssetPlayerTime(GetInstanceAssetPlayerIndex(LocomotionMachine, StateName));
}

float UGraceLocomotionAnimInstance::GetStateAnimationRemainingTime(FName StateName)
{
    return GetInstanceAssetPlayerTimeFromEnd(GetInstanceAssetPlayerIndex(LocomotionMachine, StateName));
}

void UGraceLocomotionAnimInstance::SelectRunStop()
{
    const Grace::Locomotion::FRunPoseQuery Query
    {
        LeftFootPosition, RightFootPosition, LeftFootVelocityXZ, RightFootVelocityXZ
    };
    const Grace::Locomotion::FRunStopSelection Selection = Grace::Locomotion::MatchRunStop(Query);
    SelectedRunStop = Selection.Foot == Grace::Locomotion::ERunStopFoot::Left ? LeftRunStop : RightRunStop;
    RunStopStartSeconds = Selection.StartFrame / SourceFramesPerSecond;
}

void UGraceLocomotionAnimInstance::SelectRunTurn(ERunTurn Turn, float TravelYaw, float ActorYaw)
{
    CurrentRunTurn = Turn;
    RunTurnStartSeconds = 0.0f;
    IsRunTurnMirrored = Turn == ERunTurn::RightSide || Turn == ERunTurn::CancelLeftSide;
    switch (Turn)
    {
    case ERunTurn::LeftBack:
        SelectedRunTurn = LeftRunTurn;
        break;
    case ERunTurn::RightBack:
        SelectedRunTurn = RightRunTurn;
        break;
    case ERunTurn::LeftSide:
    case ERunTurn::RightSide:
        SelectedRunTurn = RunSideTurn;
        break;
    case ERunTurn::CancelLeftSide:
    case ERunTurn::CancelRightSide:
        SelectedRunTurn = RunSideTurnCancel;
        RunTurnStartSeconds = Grace::Locomotion::MatchRunSideTurnCancel(
            {LeftFootPosition, RightFootPosition, LeftFootVelocityXZ, RightFootVelocityXZ},
            IsRunTurnMirrored) / SourceFramesPerSecond;
        break;
    }
    AcceptedTurnHeadingDegrees = TravelYaw;
    TurnStartActorYawDegrees = ActorYaw;
    // Original onChangedState(Turn) clears the return-to-forward latch.
    HasReturnedToForwardInput = false;
    RunTurnRequested = true;
}

bool UGraceLocomotionAnimInstance::UpdateRunControl(bool IsMoving, bool IsRunning, bool IsCrouching,
    float MoveForward, float MoveRight, float ControlYawDegrees, float ActorYawDegrees)
{
    const FName State = GetCurrentStateName(GetStateMachineIndex(LocomotionMachine));
    const bool IsStopping = State == RunStopState;
    const bool IsTurning = State == RunTurnState;
    const bool IsBackTurn = CurrentRunTurn == ERunTurn::LeftBack || CurrentRunTurn == ERunTurn::RightBack;
    const bool IsSideTurn = CurrentRunTurn == ERunTurn::LeftSide || CurrentRunTurn == ERunTurn::RightSide;
    const float StopFrame = IsStopping ? GetStateAnimationTime(RunStopState) * SourceFramesPerSecond : 0.0f;
    const float TurnSeconds = IsTurning ? GetStateAnimationTime(RunTurnState) : 0.0f;
    const float TurnFrame = TurnSeconds * SourceFramesPerSecond;
    const float TravelYaw = ControlYawDegrees + FMath::RadiansToDegrees(FMath::Atan2(MoveRight, MoveForward));
    const float RelativeYaw = FMath::FindDeltaAngleDegrees(ActorYawDegrees, TravelYaw);
    const float AbsoluteYaw = FMath::Abs(RelativeYaw);
    const float InputIntensity = FVector2f(MoveForward, MoveRight).Size();
    // Source positive turns left; imported UE positive root yaw turns right.
    const bool PositiveSourceAngle = -RelativeYaw >= 0.0f;
    const bool InputCrossedForward = PositiveSourceAngle != WasPositiveSourceInputAngle;
    const bool HasRunIntent = IsMoving && IsRunning && !IsCrouching;
    const float EndOffset = IsBackTurn ? BackTurnEndOffsetFrames
        : (IsSideTurn ? SideTurnEndOffsetFrames : SideCancelEndOffsetFrames);
    const bool ReachedOriginalEnd = IsTurning
        && GetStateAnimationRemainingTime(RunTurnState) <= EndOffset / SourceFramesPerSecond;

    // Approved event-lifetime adapter: event onset until logical state exit.
    // Side-turn stop/walk interruption is a UE adapter; it is separate from the
    // original RightAngleAgain and direction-crossing cancellation gates below.
    CanInterruptRunStop = !IsStopping || StopFrame >= StopCancelFrame;
    CanInterruptRunTurn = !IsTurning || !IsBackTurn || TurnFrame >= TurnCancelFrame;
    const bool WantsStop = !IsMoving && !IsCrouching
        && (State == RunState || State == RunStartState || (IsTurning && CanInterruptRunTurn));
    if (WantsStop && !RunStopRequested)
    {
        SelectRunStop();
    }
    RunStopRequested = WantsStop;
    RunTurnRequested = false;

    // Evaluate the current input before any procedural rotation. A faded pose
    // must never postpone a new request or have its animation swapped underneath
    // it: the graph uses UE inertialization for all entries/exits of these states.
    if (HasRunIntent && InputIntensity >= BackTurnInputThreshold)
    {
        const bool CanChooseBack = State == RunState
            || (IsTurning && (!IsBackTurn || ReachedOriginalEnd));
        if (CanChooseBack && AbsoluteYaw >= BackTurnThresholdDegrees)
        {
            SelectRunTurn(RelativeYaw >= 0.0f ? ERunTurn::RightBack : ERunTurn::LeftBack,
                TravelYaw, ActorYawDegrees);
        }
        else
        {
            const float AgainFrame = IsBackTurn ? 35.0f : (IsSideTurn ? 38.0f : 40.0f);
            const bool CanChooseSide = State == RunState || (IsTurning && TurnFrame >= AgainFrame);
            const bool SideInputEligible = InputCrossedForward
                || (HasReturnedToForwardInput && AbsoluteYaw >= SideTurnIgnorePreviousDegrees);
            const ERunTurn Side = RelativeYaw >= 0.0f ? ERunTurn::RightSide : ERunTurn::LeftSide;
            const bool SameSideMotion = IsTurning && IsSideTurn && CurrentRunTurn == Side;
            if (CanChooseSide && SideInputEligible && !SameSideMotion && AbsoluteYaw >= SideTurnThresholdDegrees)
            {
                SelectRunTurn(Side, TravelYaw, ActorYawDegrees);
            }
            else if (IsTurning && IsSideTurn && InputCrossedForward && AbsoluteYaw >= SideCancelThresholdDegrees)
            {
                if (CurrentRunTurn == ERunTurn::LeftSide && RelativeYaw > 0.0f)
                {
                    SelectRunTurn(ERunTurn::CancelLeftSide, TravelYaw, ActorYawDegrees);
                }
                else if (CurrentRunTurn == ERunTurn::RightSide && RelativeYaw < 0.0f)
                {
                    SelectRunTurn(ERunTurn::CancelRightSide, TravelYaw, ActorYawDegrees);
                }
            }
        }
    }

    const float FastCancelFrame = IsSideTurn ? 60.0f : TurnFastCancelFrame;
    // Approved UE substitute for the unresolved original correction accumulator.
    const bool ChangedTurnIntent = IsTurning && IsMoving && TurnFrame >= FastCancelFrame
        && FMath::Abs(FMath::FindDeltaAngleDegrees(AcceptedTurnHeadingDegrees, TravelYaw)) >= FastCancelDirectionChangeDegrees;
    RunTurnFinished = !RunTurnRequested && (ReachedOriginalEnd || ChangedTurnIntent);

    UseAnimatedTurnYaw = IsTurning || RunTurnRequested;
    if (RunTurnRequested)
    {
        // The old player's time belongs to the old clip. A replacement starts at
        // the actual actor heading, not old time applied to the new root curve.
        AnimatedTurnYawDegrees = ActorYawDegrees;
    }
    else if (IsTurning)
    {
        const FTransform RootMotion = SelectedRunTurn->ExtractRootMotionFromRange(
            RunTurnStartSeconds, TurnSeconds, FAnimExtractContext());
        const float RootYaw = IsRunTurnMirrored ? -RootMotion.Rotator().Yaw : RootMotion.Rotator().Yaw;
        AnimatedTurnYawDegrees = FRotator::NormalizeAxis(TurnStartActorYawDegrees + RootYaw);
    }

    // Original after-update remembers input sign and latches return within 20deg.
    // UE input adapter retains this history across neutral input instead of
    // inventing a world heading from atan2(0,0).
    if (IsMoving && (State == RunState || State == RunStartState || IsTurning))
    {
        WasPositiveSourceInputAngle = PositiveSourceAngle;
        HasReturnedToForwardInput |= AbsoluteYaw <= ForwardInputThresholdDegrees;
    }
    return !UseAnimatedTurnYaw && (!IsStopping || CanInterruptRunStop);
}
