#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "GraceLocomotionAnimInstance.generated.h"

class UAnimSequence;

// Keeps the generated AnimGraph and its normal UE proxy. This class only supplies
// the ordinary GeneralM stop, side-turn and back-turn requests for SK_Grace_Default.
UCLASS(Blueprintable)
class GRACEANIMATIONRUNTIME_API UGraceLocomotionAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, Category="Grace|Run Source", meta=(ToolTip="GeneralM bank 0, motion 5500: left-foot run end, full Grace skeleton."))
    TObjectPtr<UAnimSequence> LeftRunStop;

    UPROPERTY(EditDefaultsOnly, Category="Grace|Run Source", meta=(ToolTip="GeneralM bank 0, motion 5505: right-foot run end, full Grace skeleton."))
    TObjectPtr<UAnimSequence> RightRunStop;

    UPROPERTY(EditDefaultsOnly, Category="Grace|Run Source", meta=(ToolTip="GeneralM bank 0, motion 5405: left 180-degree root-motion turn."))
    TObjectPtr<UAnimSequence> LeftRunTurn;

    UPROPERTY(EditDefaultsOnly, Category="Grace|Run Source", meta=(ToolTip="GeneralM bank 0, motion 5400: right 180-degree root-motion turn."))
    TObjectPtr<UAnimSequence> RightRunTurn;

    UPROPERTY(EditDefaultsOnly, Category="Grace|Run Source", meta=(ToolTip="GeneralM motion 5355. Motion 5350 uses this same source with the MOT-list Mirror flag."))
    TObjectPtr<UAnimSequence> RunSideTurn;

    UPROPERTY(EditDefaultsOnly, Category="Grace|Run Source", meta=(ToolTip="GeneralM motion 5375, cancelling a right side-turn. Motion 5370 uses this source mirrored to cancel a left side-turn."))
    TObjectPtr<UAnimSequence> RunSideTurnCancel;

    UPROPERTY(Transient, BlueprintReadOnly, Category="Grace|Run Selection")
    TObjectPtr<UAnimSequence> SelectedRunStop;

    UPROPERTY(Transient, BlueprintReadOnly, Category="Grace|Run Selection")
    TObjectPtr<UAnimSequence> SelectedRunTurn;

    UPROPERTY(Transient, BlueprintReadOnly, Category="Grace|Run Selection", meta=(ToolTip="Original MOT-list Mirror flag. Consumed by the UE Mirror node for bones and root motion."))
    bool IsRunTurnMirrored = false;

    UPROPERTY(Transient, BlueprintReadOnly, Category="Grace|Run Selection", meta=(Units="s", ToolTip="Back/side turns start at frame zero. Side-turn cancellation selects one of the original frame 1/5/9 pose-query fragments."))
    float RunTurnStartSeconds = 0.0f;

    UPROPERTY(Transient, BlueprintReadOnly, Category="Grace|Run Selection", meta=(Units="s", ToolTip="Original eligible stop fragment starts at source frame 1, at 60 Hz."))
    float RunStopStartSeconds = 1.0f / 60.0f;

    UPROPERTY(Transient, BlueprintReadOnly, Category="Grace|Run Requests")
    bool RunStopRequested = false;

    UPROPERTY(Transient, BlueprintReadOnly, Category="Grace|Run Requests")
    bool RunTurnRequested = false;

    UPROPERTY(Transient, BlueprintReadOnly, Category="Grace|Run Requests", meta=(ToolTip="UE adapter: run-end cancellation opens at source frame 40 and resets on state exit."))
    bool CanInterruptRunStop = true;

    UPROPERTY(Transient, BlueprintReadOnly, Category="Grace|Run Requests", meta=(ToolTip="UE adapter: ordinary back-turn stop/walk cancellation opens at frame 30. Side-turn stop/walk cancellation is allowed; turn-to-turn selection has separate source gates."))
    bool CanInterruptRunTurn = true;

    UPROPERTY(Transient, BlueprintReadOnly, Category="Grace|Run Requests")
    bool RunTurnFinished = false;

    UPROPERTY(Transient, BlueprintReadOnly, Category="Grace|Run Rotation", meta=(ToolTip="Authored root yaw belongs to the current turn only. Inertial pose decay never retains yaw ownership. A replacement turn anchors at the actual current heading."))
    bool UseAnimatedTurnYaw = false;

    UPROPERTY(Transient, BlueprintReadOnly, Category="Grace|Run Rotation", meta=(Units="deg", ToolTip="UE world yaw at the last evaluated source turn time, before this frame's root-motion delta. Applied by the Character, not by this animation instance."))
    float AnimatedTurnYawDegrees = 0.0f;

    // Call on the game thread from the Character before procedural yaw is applied.
    // Select a new action before procedural yaw can consume its direction error.
    // The return value permits the existing walk/run yaw adapter.
    UFUNCTION(BlueprintCallable, Category="Grace|Run Control", meta=(ToolTip="Update ordinary run stop/turn requests before rotating Grace. Input axes are camera-relative; both yaw inputs are UE world degrees. Returns whether procedural actor yaw may run this frame."))
    bool UpdateRunControl(bool IsMoving, bool IsRunning, bool IsCrouching,
        float MoveForward, float MoveRight, float ControlYawDegrees, float ActorYawDegrees);

    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    virtual void NativePostEvaluateAnimation() override;

private:
    enum class ERunTurn : uint8
    {
        LeftBack, RightBack, LeftSide, RightSide, CancelLeftSide, CancelRightSide
    };

    float GetStateAnimationTime(FName StateName);
    float GetStateAnimationRemainingTime(FName StateName);
    void SelectRunStop();
    void SelectRunTurn(ERunTurn Turn, float TravelYaw, float ActorYaw);

    FVector3f LeftFootPosition = FVector3f::ZeroVector;
    FVector3f RightFootPosition = FVector3f::ZeroVector;
    FVector2f LeftFootVelocityXZ = FVector2f::ZeroVector;
    FVector2f RightFootVelocityXZ = FVector2f::ZeroVector;
    float SampleElapsedSeconds = 0.0f;
    float AcceptedTurnHeadingDegrees = 0.0f;
    float TurnStartActorYawDegrees = 0.0f;
    bool HasFootSample = false;
    bool WasPositiveSourceInputAngle = false;
    bool HasReturnedToForwardInput = false;
    ERunTurn CurrentRunTurn = ERunTurn::RightBack;
};
