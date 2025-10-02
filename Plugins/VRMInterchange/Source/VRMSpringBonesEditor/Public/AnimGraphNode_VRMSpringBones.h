// AnimGraphNode_VRMSpringBones.h
#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "VRMInterchangeLog.h"
#include "AnimNode_VRMSpringBones.h"
#include "AnimGraphNode_VRMSpringBones.generated.h"

UCLASS()
class UAnimGraphNode_VRMSpringBones : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category = Settings)
    FAnimNode_VRMSpringBones Node;

    virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0, 0.6f, 1.f); }
    virtual FText GetTooltipText() const override;
    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
    virtual FString GetNodeCategory() const override { return TEXT("VRM"); }
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
};
