#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class UVRMSpringBoneData;

class VRMINTERCHANGEEDITOR_API FVRMInterchangeEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

#if WITH_EDITOR
	static void NotifySpringDataCreated(UVRMSpringBoneData* Asset);
	static void NotifySpringDataSaved(UVRMSpringBoneData* Asset);
#endif
};
