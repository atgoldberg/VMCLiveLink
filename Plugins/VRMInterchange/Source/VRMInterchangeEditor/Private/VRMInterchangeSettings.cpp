#include "VRMInterchangeSettings.h"


UVRMInterchangeSettings::UVRMInterchangeSettings()
{
	// helps where it appears in the Settings tree (optional)
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("VRM Interchange");
	// Sensible defaults
	bGenerateSpringBoneData = false;
	bGeneratePostProcessAnimBP = false;
	bAssignPostProcessABP = false;
	bOverwriteExistingSpringAssets = false;
}