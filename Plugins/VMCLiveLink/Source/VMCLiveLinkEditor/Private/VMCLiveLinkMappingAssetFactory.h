#pragma once
#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "VMCLiveLinkMappingAssetFactory.generated.h"

UCLASS()
class UVMCLiveLinkMappingAssetFactory : public UFactory
{
	GENERATED_BODY()
public:
	UVMCLiveLinkMappingAssetFactory();

	// UFactory
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual uint32 GetMenuCategories() const override;
	virtual FText GetDisplayName() const override;
};