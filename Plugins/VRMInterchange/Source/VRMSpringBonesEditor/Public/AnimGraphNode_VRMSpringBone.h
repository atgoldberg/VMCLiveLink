#pragma once

#include "CoreMinimal.h"
#include "AnimGraphNode_Base.h" // correct include path
#include "AnimNode_VRMSpringBone.h"
#include "AnimGraphNode_VRMSpringBone.generated.h"

UCLASS()
class VRMSPRINGBONESEDITOR_API UAnimGraphNode_VRMSpringBone : public UAnimGraphNode_Base
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category=Settings)
    FAnimNode_VRMSpringBone Node;

    virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.2f, 0.6f, 1.f); }
    virtual FText GetTooltipText() const override;
    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
    virtual FString GetNodeCategory() const override { return TEXT("VRM"); }
};
