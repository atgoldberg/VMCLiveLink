// Minimal module entry point for the editor module.
#include "Modules/ModuleManager.h"
#include "VRMSpringBonesPostImportPipeline.h"
#include "InterchangeManager.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Class.h"
#include "InterchangePipelineBase.h"

class FVRMInterchangeEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        // Ensure the pipeline class is linked into the module binary
        UVRMSpringBonesPostImportPipeline::StaticClass();

        UE_LOG(LogTemp, Log, TEXT("VRMInterchangeEditor module started"));

#if WITH_EDITOR
        // Enumerate all loaded UClasses derived from UInterchangePipelineBase
        const UClass* PipelineBase = UInterchangePipelineBase::StaticClass();
        int32 Found = 0;
        for (TObjectIterator<UClass> It; It; ++It)
        {
            UClass* C = *It;
            if (!C->HasAnyClassFlags(CLASS_Deprecated) && C->IsChildOf(PipelineBase))
            {
                ++Found;
                const bool bIsTarget = (C == UVRMSpringBonesPostImportPipeline::StaticClass());
                UE_LOG(LogTemp, Log, TEXT("Interchange Pipeline subclass found: %s%s"), *C->GetName(), bIsTarget ? TEXT("  <-- target") : TEXT(""));
            }
        }
        UE_LOG(LogTemp, Log, TEXT("Interchange Pipeline subclasses enumerated: %d"), Found);
#endif
    }

    virtual void ShutdownModule() override
    {
        UE_LOG(LogTemp, Log, TEXT("VRMInterchangeEditor module shutdown"));
    }
};

IMPLEMENT_MODULE(FVRMInterchangeEditorModule, VRMInterchangeEditor)