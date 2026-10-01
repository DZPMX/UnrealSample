#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "ElenaRigData.h"
#include "ElenaRigVM.h"
#include "AnimNode_ElenaRig.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct UNREALSAMPLE_API FAnimNode_ElenaRig : public FAnimNode_Base
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Links)
    FPoseLink InputPose;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PinShownByDefault))
    bool bEnabled = true;

    UPROPERTY(EditAnywhere, Category=Settings, meta=(NeverAsPin))
    TObjectPtr<UElenaRigData> RigData;

    virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
    virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
    virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
    virtual void Evaluate_AnyThread(FPoseContext& Output) override;
    virtual void GatherDebugData(FNodeDebugData& DebugData) override;

private:
    ElenaRig::Program Program;
    ElenaRig::VM Machine;
    std::vector<ElenaRig::SQT> Inputs;
    TArray<int32> DirectBones;
    TArray<int32> DerivedBones;
    TArray<FCompactPoseBoneIndex> BoneMap;
    TArray<int32> DerivedByCompact;
    TArray<FTransform> ComponentPose;
};

template<> struct TStructOpsTypeTraits<FAnimNode_ElenaRig> : TStructOpsTypeTraitsBase2<FAnimNode_ElenaRig>
{
    enum { WithCopy = true };
};
