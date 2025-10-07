#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "VMCLiveLinkRetargetActorFactory.generated.h"

UCLASS()
class UVMCLiveLinkRetargetActorFactory : public UFactory
{
	GENERATED_BODY()
public:
	UVMCLiveLinkRetargetActorFactory();

	// UFactory
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual uint32 GetMenuCategories() const override;
	virtual FText GetDisplayName() const override;
	virtual bool ShouldShowInNewMenu() const override;

public:
	// Path to the template Blueprint inside the plugin content.
	UPROPERTY(EditAnywhere, Category="VMC")
	FSoftObjectPath TemplateBlueprintPath;
};
