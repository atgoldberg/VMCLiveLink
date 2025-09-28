#pragma once

#include "CoreMinimal.h"
#include "VRMSpringBonesTypes.h"

namespace VRM
{
    // Parse from a top-level JSON string (GLB chunk or .gltf text)
    VRMINTERCHANGE_API bool ParseSpringBonesFromJson(const FString& Json, FVRMSpringConfig& OutConfig, FString& OutError);

    // Convenience: read file (.vrm/.glb/.gltf), extract top-level JSON, parse
    VRMINTERCHANGE_API bool ParseSpringBonesFromFile(const FString& Filename, FVRMSpringConfig& OutConfig, FString& OutError);

    // New overloads that also produce a node index -> bone name map (glTF node names)
    VRMINTERCHANGE_API bool ParseSpringBonesFromJson(const FString& Json, FVRMSpringConfig& OutConfig, TMap<int32, FName>& OutNodeMap, FString& OutError);
    VRMINTERCHANGE_API bool ParseSpringBonesFromFile(const FString& Filename, FVRMSpringConfig& OutConfig, TMap<int32, FName>& OutNodeMap, FString& OutError);
}