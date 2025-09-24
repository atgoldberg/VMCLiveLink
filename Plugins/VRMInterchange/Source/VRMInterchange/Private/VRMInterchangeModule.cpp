#pragma once
#include "Modules/ModuleManager.h"
#include "InterchangeManager.h"
#include "Misc/CoreDelegates.h"
#include "Engine.h"
#include "VRMTranslator.h"

class FVRMInterchangeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Register after engine init; call immediately if GEngine is already valid
		PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FVRMInterchangeModule::OnPostEngineInit);
		if (GEngine)
		{
			OnPostEngineInit();
		}
	}

	virtual void ShutdownModule() override
	{
		if (PostEngineInitHandle.IsValid())
		{
			FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
			PostEngineInitHandle.Reset();
		}
		// No UnregisterTranslator in UE 5.6; manager cleans up internally
	}

private:
	void OnPostEngineInit()
	{
		UInterchangeManager& Manager = UInterchangeManager::GetInterchangeManager();
		Manager.RegisterTranslator(UVRMTranslator::StaticClass());
	}

private:
	FDelegateHandle PostEngineInitHandle;
};

IMPLEMENT_MODULE(FVRMInterchangeModule, VRMInterchange)