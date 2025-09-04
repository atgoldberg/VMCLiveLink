// Copyright Epic Games, Inc. All Rights Reserved.

#include "OSCModule.h"

#include "OSCLog.h"

DEFINE_LOG_CATEGORY(LogOSC);


namespace UE::OSC
{
	void FModule::StartupModule()
	{
		if (!FModuleManager::Get().LoadModule(TEXT("Networking")))
		{
			UE_LOG(LogOSC, Error, TEXT("Required module 'Networking' failed to load. OSC service disabled."));
		}
	}
} // namespace UE::OSC

IMPLEMENT_MODULE(UE::OSC::FModule, OSC)
