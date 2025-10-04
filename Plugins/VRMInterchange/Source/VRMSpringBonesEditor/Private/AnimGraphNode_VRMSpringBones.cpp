#include "AnimGraphNode_VRMSpringBones.h"
#include "Animation/AnimBlueprint.h"

#include "Kismet2/BlueprintEditorUtils.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "AnimGraphNode_VRMSpringBones"

FText UAnimGraphNode_VRMSpringBones::GetTooltipText() const
{
    return LOCTEXT("Tooltip", "Simulates VRM spring bones");
}

FText UAnimGraphNode_VRMSpringBones::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    return LOCTEXT("Title", "VRM Spring Bones");
}

#undef LOCTEXT_NAMESPACE
