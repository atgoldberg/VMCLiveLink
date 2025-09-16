// copyright 2025 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg All rights reserved.
#include "Modules/ModuleManager.h"

class FVMCLiveLinkEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override {}
    virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FVMCLiveLinkEditorModule, VMCLiveLinkEditor)
