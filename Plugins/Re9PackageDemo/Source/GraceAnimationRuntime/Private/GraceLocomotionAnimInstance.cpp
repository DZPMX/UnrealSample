#include "GraceLocomotionAnimInstance.h"

#include "Animation/AnimNode_StateMachine.h"
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
    constexpr float TurnEndOffsetFrames = 35.0f;
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

float UGraceLocomotionAnimInstance::GetLocomotionStateWeight(FName StateName)
{
    // UE exposes this non-owning borrow as a raw pointer. Validate at that API
    // boundary and use a reference locally; no borrowed pointer is retained.
    if (const FAnimNode_StateMachine* EngineMachine = GetStateMachineInstanceFromName(LocomotionMachine))
    {
        const FAnimNode_StateMachine& Machine = *EngineMachine;
        return GetInstanceStateWeight(GetStateMachineIndex(LocomotionMachine), Machine.GetStateIndex(StateName));
    }
    // Before graph initialization, UE has no state weight to consume.
    return 0.0f;
}

void UGraceLocomotionAnimInstance::SelectRunStop()
{
    const Grace::Locomotion::FRunStopQuery Query
    {
        LeftFootPosition, RightFootPosition, LeftFootVelocityXZ, RightFootVelocityXZ
    };
    const Grace::Locomotion::FRunStopSelection Selection = Grace::Locomotion::MatchRunStop(Query);
    SelectedRunStop = Selection.Foot == Grace::Locomotion::ERunStopFoot::Left ? LeftRunStop : RightRunStop;
    RunStopStartSeconds = Selection.StartFrame / SourceFramesPerSecond;
}

bool UGraceLocomotionAnimInstance::UpdateRunControl(bool IsMoving, bool IsRunning, bool IsCrouching,
    float MoveForward, float MoveRight, float ControlYawDegrees, float ActorYawDegrees)
{
    const int32 MachineIndex = GetStateMachineIndex(LocomotionMachine);
    const FName State = GetCurrentStateName(MachineIndex);
    const bool IsStopping = State == RunStopState;
    const bool IsTurning = State == RunTurnState;
    const float StopWeight = GetLocomotionStateWeight(RunStopState);
    const float TurnWeight = GetLocomotionStateWeight(RunTurnState);
    const float StopFrame = IsStopping ? GetStateAnimationTime(RunStopState) * SourceFramesPerSecond : 0.0f;
    const float TurnFrame = IsTurning ? GetStateAnimationTime(RunTurnState) * SourceFramesPerSecond : 0.0f;
    const float TravelYaw = ControlYawDegrees + FMath::RadiansToDegrees(FMath::Atan2(MoveRight, MoveForward));
    const float RelativeYaw = FMath::FindDeltaAngleDegrees(ActorYawDegrees, TravelYaw);
    const float InputIntensity = FVector2f(MoveForward, MoveRight).Size();

    // These gates intentionally use event onset until state exit. They do not
    // emulate the unresolved native CLIP cross-layer lifetime/reset semantics.
    CanInterruptRunStop = !IsStopping || StopFrame >= StopCancelFrame;
    CanInterruptRunTurn = !IsTurning || TurnFrame >= TurnCancelFrame;

    // The same dynamic player can still contribute while its state fades out.
    // Reconsider stop entry only after that previous contribution has ended, so
    // a new choice cannot replace the clip underneath an active outgoing blend.
    const bool WantsStop = StopWeight <= 0.0f && !IsMoving && !IsCrouching
        && (State == RunState || State == RunStartState || (IsTurning && CanInterruptRunTurn));
    if (WantsStop && !RunStopRequested)
    {
        SelectRunStop();
    }
    RunStopRequested = WantsStop;

    // Keep one selected sequence stable for its complete UE blend lifetime.
    // A further 180-degree request is reconsidered after the outgoing turn ends.
    const bool WantsTurn = State == RunState && TurnWeight <= 0.0f && IsMoving && IsRunning && !IsCrouching
        && InputIntensity >= BackTurnInputThreshold && FMath::Abs(RelativeYaw) >= BackTurnThresholdDegrees;
    if (WantsTurn && !RunTurnRequested)
    {
        // Verified imported root motion: 5400 advances positive UE yaw; 5405
        // advances negative yaw. Mesh yaw -90 does not change that yaw sign.
        SelectedRunTurn = RelativeYaw >= 0.0f ? RightRunTurn : LeftRunTurn;
        AcceptedTurnHeadingDegrees = TravelYaw;
        TurnStartActorYawDegrees = ActorYawDegrees;
    }
    RunTurnRequested = WantsTurn;

    const bool ReachedOriginalEnd = IsTurning
        && GetStateAnimationRemainingTime(RunTurnState) <= TurnEndOffsetFrames / SourceFramesPerSecond;
    // UE adapter for a further direction change: the source enables FastCancel
    // at frame 65 and has a 20-degree setting, but its correction accumulator is
    // not replicated. Compare against the accepted world-space turn intent.
    const bool ChangedTurnIntent = IsTurning && IsMoving && TurnFrame >= TurnFastCancelFrame
        && FMath::Abs(FMath::FindDeltaAngleDegrees(AcceptedTurnHeadingDegrees, TravelYaw)) >= FastCancelDirectionChangeDegrees;
    RunTurnFinished = ReachedOriginalEnd || ChangedTurnIntent;

    UseAnimatedTurnYaw = IsTurning || TurnWeight > 0.0f;
    if (UseAnimatedTurnYaw)
    {
        // UE adapter: anchor this frame at the authored cumulative root yaw of
        // the last evaluated pose. CharacterMovement then applies the next
        // extracted delta once. This corrects lost entry-blend rotation without
        // adding the full 180 degrees on top of already consumed root motion.
        const FTransform RootMotion = SelectedRunTurn->ExtractRootMotionFromRange(
            0.0, GetStateAnimationTime(RunTurnState), FAnimExtractContext());
        AnimatedTurnYawDegrees = FRotator::NormalizeAxis(TurnStartActorYawDegrees + RootMotion.Rotator().Yaw);
    }

    return !RunTurnRequested && !UseAnimatedTurnYaw
        && (!IsStopping || CanInterruptRunStop);
}
