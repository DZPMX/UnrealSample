#pragma once

#include "CoreMinimal.h"
#include "AnimGraphNode_Base.h"
#include "ElenaRig/AnimNode_ElenaRig.h"
#include "AnimGraphNode_ElenaRig.generated.h"

UCLASS()
class UNREALSAMPLEEDITOR_API UAnimGraphNode_ElenaRig : public UAnimGraphNode_Base
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category=Settings)
    FAnimNode_ElenaRig Node;

    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
    virtual FText GetTooltipText() const override;
    virtual FString GetNodeCategory() const override;
};
