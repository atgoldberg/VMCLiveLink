#pragma once

#include "CoreMinimal.h"
#include "VRMSpringBonesTypes.h"

namespace VRM
{
    // Parse from a top-level JSON string (GLB chunk or .gltf text)
    VRMINTERCHANGE_API bool ParseSpringBonesFromJson(const FString& Json, FVRMSpringConfig& OutConfig, FString& OutError);

    // Convenience: read file (.vrm/.glb/.gltf), extract top-level JSON, parse
    VRMINTERCHANGE_API bool ParseSpringBonesFromFile(const FString& Filename, FVRMSpringConfig& OutConfig, FString& OutError);
}