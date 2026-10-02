#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "GraceAnimationInstance.generated.h"

class UAnimSequence;
class UMaterialInstanceDynamic;

UCLASS(Blueprintable, BlueprintType)
class GRACEANIMATIONRUNTIME_API UGraceAnimationInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grace|Motion", meta=(ToolTip="Original Grace motion imported for this component's existing skeleton."))
    TObjectPtr<UAnimSequence> Motion;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grace|Playback", meta=(Units="s", ToolTip="Source playback time in seconds. Drives both the pose and original wrinkle sampler."))
    float SourceTime = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grace|Playback", meta=(ToolTip="Advance Source Time with UE delta time. Disable for a shared clock or manual scrubbing."))
    bool Play = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grace|Playback", meta=(ToolTip="UE playback adaptation: wrap Source Time at the imported sequence duration."))
    bool Loop = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grace|Wrinkles", meta=(ToolTip="Existing face material slots receiving the eight original four-component wrinkle weights."))
    TArray<int32> FaceMaterialSlots;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Grace|Wrinkles")
    TArray<float> EvaluatedWrinkleWeights;

    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    void ApplyWrinkleWeights(const TArray<float>& Values);

protected:
    // These raw pointer signatures are imposed by UAnimInstance. UE owns the proxy.
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
    UPROPERTY(Transient)
    TMap<int32, TObjectPtr<UMaterialInstanceDynamic>> FaceMaterialInstances;
};
