#include "AnimGraphNode_VRMSpringBone.h"
#include "Animation/AnimBlueprint.h"
#include "VRMInterchangeLog.h"

#define LOCTEXT_NAMESPACE "AnimGraphNode_VRMSpringBone"

FText UAnimGraphNode_VRMSpringBone::GetTooltipText() const
{
    return LOCTEXT("Tooltip", "Simulates VRM spring bones (placeholder pass-through in Iteration 1)");
}

FText UAnimGraphNode_VRMSpringBone::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    return LOCTEXT("Title", "VRM Spring Bones");
}

#undef LOCTEXT_NAMESPACE
