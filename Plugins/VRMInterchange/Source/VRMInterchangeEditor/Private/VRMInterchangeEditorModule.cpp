// Minimal module entry point for the editor module.
#include "Modules/ModuleManager.h"

class FVRMInterchangeEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        // Keep empty for now. Later phases can register the post-import pipeline here.
        // Example (when you have a pipeline registrar):
        // IInterchangeEditorModule::Get().GetPostImportPipelineRegistry().RegisterFactory(...);
    }

    virtual void ShutdownModule() override
    {
        // Unregister anything you registered in StartupModule.
    }
};

IMPLEMENT_MODULE(FVRMInterchangeEditorModule, VRMInterchangeEditor)