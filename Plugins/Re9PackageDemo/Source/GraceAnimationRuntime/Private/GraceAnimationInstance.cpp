#include "GraceAnimationInstance.h"
#include "GraceWrinkleMotion.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
    struct FGraceAnimationProxy final : FAnimInstanceProxy
    {
        explicit FGraceAnimationProxy(UGraceAnimationInstance& Instance)
            : FAnimInstanceProxy(&Instance)
        {
            WrinkleWeights.Init(0.0f, 32);
        }

        TWeakObjectPtr<UAnimSequence> Motion;
        TWeakObjectPtr<UGraceWrinkleMotion> WrinkleMotion;
        float Time = 0.0f;
        TArray<float> WrinkleWeights;

        virtual void PreUpdate(UAnimInstance* InInstance, float DeltaSeconds) override
        {
            FAnimInstanceProxy::PreUpdate(InInstance, DeltaSeconds);
            const TObjectPtr<UGraceAnimationInstance> Instance = Cast<UGraceAnimationInstance>(InInstance);
            if (IsValid(Instance))
            {
                Motion = Instance->Motion;
                WrinkleMotion.Reset();
                if (IsValid(Instance->Motion))
                {
                    WrinkleMotion = Cast<UGraceWrinkleMotion>(Instance->Motion->GetAssetUserDataOfClass(UGraceWrinkleMotion::StaticClass()));
                }
                Time = Instance->SourceTime;
            }
        }

        virtual bool Evaluate(FPoseContext& Output) override
        {
            Output.ResetToRefPose();
            const TObjectPtr<UAnimSequence> Sequence = Motion.Get();
            if (IsValid(Sequence))
            {
                FAnimationPoseData PoseData(Output);
                Sequence->GetAnimationPose(PoseData, FAnimExtractContext(double(Time), false));
            }
            const TObjectPtr<UGraceWrinkleMotion> Source = WrinkleMotion.Get();
            // Approved standalone playback policy: absent motion/tracks clear all masks.
            WrinkleWeights.Init(0.0f, 32);
            if (IsValid(Sequence) && IsValid(Source))
            {
                WrinkleWeights = Source->SampleAtSeconds(Time);
            }
            return true;
        }

        virtual void PreEvaluateAnimation(UAnimInstance* InInstance) override
        {
            FAnimInstanceProxy::PreEvaluateAnimation(InInstance);
            const TObjectPtr<UGraceAnimationInstance> Instance = Cast<UGraceAnimationInstance>(InInstance);
            if (IsValid(Instance))
            {
                Motion = Instance->Motion;
                WrinkleMotion.Reset();
                if (IsValid(Instance->Motion))
                {
                    WrinkleMotion = Cast<UGraceWrinkleMotion>(Instance->Motion->GetAssetUserDataOfClass(UGraceWrinkleMotion::StaticClass()));
                }
                Time = Instance->SourceTime;
            }
        }

        virtual void PostEvaluate(UAnimInstance* InInstance) override
        {
            FAnimInstanceProxy::PostEvaluate(InInstance);
            const TObjectPtr<UGraceAnimationInstance> Instance = Cast<UGraceAnimationInstance>(InInstance);
            if (IsValid(Instance))
            {
                Instance->ApplyWrinkleWeights(WrinkleWeights);
            }
        }
    };
}

FAnimInstanceProxy* UGraceAnimationInstance::CreateAnimInstanceProxy()
{
    // Ownership transfers directly to UAnimInstance's existing proxy lifecycle.
    return new FGraceAnimationProxy(*this);
}

void UGraceAnimationInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
#if WITH_EDITOR
    const TObjectPtr<USkeletalMeshComponent> Component = GetSkelMeshComponent();
    if (IsValid(Component))
    {
        const TObjectPtr<UWorld> World = Component->GetWorld();
        if (IsValid(World) && World->WorldType == EWorldType::Editor)
        {
            // UE does not serialize this preview flag; restore it when the level/ABP initializes.
            Component->SetUpdateAnimationInEditor(true);
        }
    }
#endif
}

void UGraceAnimationInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    if (Play && IsValid(Motion))
    {
        SourceTime += DeltaSeconds;
        const float Duration = Motion->GetPlayLength();
        if (Loop)
        {
            SourceTime = FMath::Fmod(SourceTime, Duration);
        }
        else
        {
            SourceTime = FMath::Min(SourceTime, Duration);
        }
    }
}

void UGraceAnimationInstance::ApplyWrinkleWeights(const TArray<float>& Values)
{
    EvaluatedWrinkleWeights = Values;
    const TObjectPtr<USkeletalMeshComponent> Component = GetSkelMeshComponent();
    if (IsValid(Component))
    {
        for (const int32 Slot : FaceMaterialSlots)
        {
            TObjectPtr<UMaterialInstanceDynamic> Material = FaceMaterialInstances.FindRef(Slot);
            if (!IsValid(Material) || Material != Component->GetMaterial(Slot))
            {
                Material = Component->CreateDynamicMaterialInstance(Slot);
                FaceMaterialInstances.Add(Slot, Material);
            }
            if (IsValid(Material))
            {
                for (int32 Group = 0; Group < 8; ++Group)
                {
                    FLinearColor Packed;
                    for (int32 Channel = 0; Channel < 4; ++Channel)
                    {
                        const float Value = Values[Group * 4 + Channel];
                        // Source float4 submission clamps values at the material boundary.
                        const float UpperLimited = Value < 1.0f ? Value : 1.0f;
                        Packed.Component(Channel) = Value < 0.0f ? 0.0f : UpperLimited;
                    }
                    const FName Parameter(*FString::Printf(TEXT("WrinkleWeights%02dTo%02d"), Group * 4 + 1, Group * 4 + 4));
                    Material->SetVectorParameterValue(Parameter, Packed);
                }
            }
        }
    }
}
