// In VRMInterchangeEditorModule.h
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FVRMInterchangeEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};