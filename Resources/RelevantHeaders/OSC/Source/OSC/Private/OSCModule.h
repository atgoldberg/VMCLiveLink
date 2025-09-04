// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"


namespace UE::OSC
{
	class FModule : public IModuleInterface
	{
	public:
		virtual void StartupModule() override;
	};
} // namespace UE::OSC
