#include "VMCLiveLinkRetargetActorFactory.h"
#include "VMCLiveLinkEditorModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/Blueprint.h"
#include "UObject/Package.h"
#include "Modules/ModuleManager.h"

UVMCLiveLinkRetargetActorFactory::UVMCLiveLinkRetargetActorFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;

	// We are creating a Blueprint asset by duplicating a template
	SupportedClass = UBlueprint::StaticClass();

	// Default template path (adjust to your actual asset).
	// The asset must reside under your plugin's Content and be cooked/visible to the editor.
	TemplateBlueprintPath = FSoftObjectPath(TEXT("/VMCLiveLink/LiveLink/BP_VMC_Retarget_Actor.BP_VMC_Retarget_Actor"));
}

UObject* UVMCLiveLinkRetargetActorFactory::FactoryCreateNew(
	UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* /*Context*/, FFeedbackContext* /*Warn*/)
{
	// Load template
	UObject* TemplateObj = TemplateBlueprintPath.TryLoad();
	UBlueprint* TemplateBP = Cast<UBlueprint>(TemplateObj);
	if (!TemplateBP)
	{
		UE_LOG(LogTemp, Warning, TEXT("[VMCLiveLink] Template Blueprint not found at '%s'."), *TemplateBlueprintPath.ToString());
		return nullptr;
	}

	// Duplicate into the selected folder/package with the requested name
	UObject* Duplicated = StaticDuplicateObject(TemplateBP, InParent, Name);
	if (!Duplicated)
	{
		UE_LOG(LogTemp, Warning, TEXT("[VMCLiveLink] Failed to duplicate template Blueprint."));
		return nullptr;
	}

	// Register and compile new Blueprint, then mark dirty
	FAssetRegistryModule::AssetCreated(Duplicated);
	if (UBlueprint* NewBP = Cast<UBlueprint>(Duplicated))
	{
		FKismetEditorUtilities::CompileBlueprint(NewBP);
	}
	Duplicated->MarkPackageDirty();

	return Duplicated;
}

uint32 UVMCLiveLinkRetargetActorFactory::GetMenuCategories() const
{
	// Put this factory under the VMC LiveLink asset category registered in the module
	return VMCLiveLinkEditor::GetAssetCategoryBit();
}

FText UVMCLiveLinkRetargetActorFactory::GetDisplayName() const
{
	return NSLOCTEXT("VMCLiveLink", "VMCLiveLinkRetargetActorFactory", "VMC Retarget Actor");
}

bool UVMCLiveLinkRetargetActorFactory::ShouldShowInNewMenu() const
{
	return true;
}
