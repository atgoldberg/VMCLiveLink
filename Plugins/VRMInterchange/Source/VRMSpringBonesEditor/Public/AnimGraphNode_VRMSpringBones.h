// AnimGraphNode_VRMSpringBones.h
#pragma once
#include "CoreMinimal.h"
#include "AnimGraph/Classes/AnimGraphNode_SkeletalControlBase.h"
#include "AnimNode_VRMSpringBones.h"
#include "AnimGraphNode_VRMSpringBones.generated.h"

UCLASS()
class UAnimGraphNode_VRMSpringBones : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category=Settings)
	FAnimNode_VRMSpringBones Node;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override { return FText::FromString(TEXT("VRM Spring Bones")); }
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.25f, 0.6f, 1.f); }
protected:
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
};
