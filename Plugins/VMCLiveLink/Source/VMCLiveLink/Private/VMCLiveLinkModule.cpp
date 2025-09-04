#include "Modules/ModuleManager.h"
#include "VMCLog.h"

class FVMCLiveLinkModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        UE_LOG(LogVMCLiveLink, Log, TEXT("VMCLiveLink runtime module started"));
    }
    virtual void ShutdownModule() override
    {
        UE_LOG(LogVMCLiveLink, Log, TEXT("VMCLiveLink runtime module shutdown"));
    }
};

IMPLEMENT_MODULE(FVMCLiveLinkModule, VMCLiveLink)
