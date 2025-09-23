#pragma once

#include "CoreMinimal.h"
#include "VRMSpringBonesTypes.generated.h"

UENUM()
enum class EVRMSpringSpec : uint8
{
    None,
    VRM0,
    VRM1
};

USTRUCT()
struct VRMINTERCHANGE_API FVRMSpringColliderSphere
{
    GENERATED_BODY()

    UPROPERTY() FVector Offset = FVector::ZeroVector;
    UPROPERTY() float Radius = 0.f;
};

USTRUCT()
struct VRMINTERCHANGE_API FVRMSpringColliderCapsule
{
    GENERATED_BODY()

    UPROPERTY() FVector Offset = FVector::ZeroVector;
    UPROPERTY() float Radius = 0.f;
    UPROPERTY() FVector TailOffset = FVector::ZeroVector;
};

USTRUCT()
struct VRMINTERCHANGE_API FVRMSpringCollider
{
    GENERATED_BODY()

    // Node index in glTF (if known)
    UPROPERTY() int32 NodeIndex = INDEX_NONE;
    // Optional bone name resolved later
    UPROPERTY() FName BoneName;

    // Shapes (VRM1 supports multiple shapes per collider)
    UPROPERTY() TArray<FVRMSpringColliderSphere> Spheres;
    UPROPERTY() TArray<FVRMSpringColliderCapsule> Capsules;
};

USTRUCT()
struct VRMINTERCHANGE_API FVRMSpringColliderGroup
{
    GENERATED_BODY()

    UPROPERTY() FString Name;
    // Indices into Colliders array
    UPROPERTY() TArray<int32> ColliderIndices;
};

USTRUCT()
struct VRMINTERCHANGE_API FVRMSpringJoint
{
    GENERATED_BODY()

    // Node index in glTF (if known)
    UPROPERTY() int32 NodeIndex = INDEX_NONE;
    // Optional bone name resolved later
    UPROPERTY() FName BoneName;

    // Per-joint params (normalized)
    UPROPERTY() float HitRadius = 0.f;
};

USTRUCT()
struct VRMINTERCHANGE_API FVRMSpring
{
    GENERATED_BODY()

    UPROPERTY() FString Name;

    // Joints composing this spring (indices into Joints)
    UPROPERTY() TArray<int32> JointIndices;

    // Groups referenced by this spring (indices into ColliderGroups)
    UPROPERTY() TArray<int32> ColliderGroupIndices;

    // Optional center (node index if known)
    UPROPERTY() int32 CenterNodeIndex = INDEX_NONE;
    UPROPERTY() FName CenterBoneName;

    // Spring parameters
    UPROPERTY() float Stiffness = 0.f;
    UPROPERTY() float Drag = 0.f;
    UPROPERTY() FVector GravityDir = FVector(0, 0, -1);
    UPROPERTY() float GravityPower = 0.f;
    UPROPERTY() float HitRadius = 0.f;
};

USTRUCT()
struct VRMINTERCHANGE_API FVRMSpringConfig
{
    GENERATED_BODY()

    UPROPERTY() EVRMSpringSpec Spec = EVRMSpringSpec::None;

    UPROPERTY() TArray<FVRMSpringCollider> Colliders;
    UPROPERTY() TArray<FVRMSpringColliderGroup> ColliderGroups;
    UPROPERTY() TArray<FVRMSpringJoint> Joints; // Mainly for VRM1
    UPROPERTY() TArray<FVRMSpring> Springs;

    // Optional raw JSON copy for diagnostics
    UPROPERTY() FString RawJson;

    bool IsValid() const
    {
        return Spec != EVRMSpringSpec::None && (Springs.Num() > 0 || ColliderGroups.Num() > 0 || Colliders.Num() > 0 || Joints.Num() > 0);
    }
};