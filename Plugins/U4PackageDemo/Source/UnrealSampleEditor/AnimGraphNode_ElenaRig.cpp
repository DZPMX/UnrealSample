#include "AnimGraphNode_ElenaRig.h"

FText UAnimGraphNode_ElenaRig::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    return NSLOCTEXT("ElenaRig", "Title", "Elena Source Rig (Realtime)");
}

FText UAnimGraphNode_ElenaRig::GetTooltipText() const
{
    return NSLOCTEXT("ElenaRig", "Tooltip", "Evaluate Elena's original auxiliary constraints from the current input pose. Disable to retain the input animation unchanged.");
}

FString UAnimGraphNode_ElenaRig::GetNodeCategory() const
{
    return TEXT("Animation|Elena");
}
