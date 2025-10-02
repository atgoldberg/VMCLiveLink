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
    // Add a richer overload that also returns Parent/Children graph
    VRMINTERCHANGE_API bool ParseSpringBonesFromJson(const FString& Json,FVRMSpringConfig& OutConfig,TMap<int32, FName>& OutNodeMap,TMap<int32, int32>& OutNodeParent,TMap<int32, TArray<int32>>& OutNodeChildren,FString& OutError);
    VRMINTERCHANGE_API bool ParseSpringBonesFromFile(const FString& Filename,FVRMSpringConfig& OutConfig,TMap<int32, FName>& OutNodeMap,TMap<int32, int32>& OutNodeParent,TMap<int32, FVRMNodeChildren>& OutNodeChildren,FString& OutError);
}
