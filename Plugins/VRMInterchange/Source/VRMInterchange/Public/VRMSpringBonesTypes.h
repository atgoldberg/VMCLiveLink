#pragma once

#include "CoreMinimal.h"
#include "VRMSpringBonesTypes.generated.h"

UENUM(BlueprintType)
enum class EVRMSpringSpec : uint8
{
    None,
    VRM0,
    VRM1
};

USTRUCT(BlueprintType)
struct VRMINTERCHANGE_API FVRMSpringColliderSphere
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="VRM") FVector Offset = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, Category="VRM") float Radius = 0.f;
};

USTRUCT(BlueprintType)
struct VRMINTERCHANGE_API FVRMSpringColliderCapsule
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="VRM") FVector Offset = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, Category="VRM") float Radius = 0.f;
    UPROPERTY(VisibleAnywhere, Category="VRM") FVector TailOffset = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct VRMINTERCHANGE_API FVRMSpringCollider
{
    GENERATED_BODY()

    // Node index in glTF (if known)
    UPROPERTY(VisibleAnywhere, Category="VRM") int32 NodeIndex = INDEX_NONE;
    // Optional bone name resolved later
    UPROPERTY(VisibleAnywhere, Category="VRM") FName BoneName;

    // Shapes (VRM1 supports multiple shapes per collider)
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<FVRMSpringColliderSphere> Spheres;
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<FVRMSpringColliderCapsule> Capsules;
};

USTRUCT(BlueprintType)
struct VRMINTERCHANGE_API FVRMSpringColliderGroup
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="VRM") FString Name;
    // Indices into Colliders array
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<int32> ColliderIndices;
};

USTRUCT(BlueprintType)
struct VRMINTERCHANGE_API FVRMSpringJoint
{
    GENERATED_BODY()

    // Node index in glTF (if known)
    UPROPERTY(VisibleAnywhere, Category="VRM") int32 NodeIndex = INDEX_NONE;
    // Optional bone name resolved later
    UPROPERTY(VisibleAnywhere, Category="VRM") FName BoneName;

    // Per-joint params (normalized)
    UPROPERTY(VisibleAnywhere, Category="VRM") float HitRadius = 0.f;
};

USTRUCT(BlueprintType)
struct VRMINTERCHANGE_API FVRMSpring
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="VRM") FString Name;

    // Joints composing this spring (indices into Joints)
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<int32> JointIndices;

    // Groups referenced by this spring (indices into ColliderGroups)
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<int32> ColliderGroupIndices;

    // Optional center (node index if known)
    UPROPERTY(VisibleAnywhere, Category="VRM") int32 CenterNodeIndex = INDEX_NONE;
    UPROPERTY(VisibleAnywhere, Category="VRM") FName CenterBoneName;

    // Spring parameters
    UPROPERTY(VisibleAnywhere, Category="VRM") float Stiffness = 0.f;
    UPROPERTY(VisibleAnywhere, Category="VRM") float Drag = 0.f;
    UPROPERTY(VisibleAnywhere, Category="VRM") FVector GravityDir = FVector(0, 0, -1);
    UPROPERTY(VisibleAnywhere, Category="VRM") float GravityPower = 0.f;
    UPROPERTY(VisibleAnywhere, Category="VRM") float HitRadius = 0.f;
};

USTRUCT(BlueprintType)
struct VRMINTERCHANGE_API FVRMSpringConfig
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="VRM") EVRMSpringSpec Spec = EVRMSpringSpec::None;

    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<FVRMSpringCollider> Colliders;
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<FVRMSpringColliderGroup> ColliderGroups;
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<FVRMSpringJoint> Joints; // Mainly for VRM1
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<FVRMSpring> Springs;

    // Optional raw JSON copy for diagnostics
    UPROPERTY(VisibleAnywhere, Category="VRM") FString RawJson;

    bool IsValid() const
    {
        return Spec != EVRMSpringSpec::None && (Springs.Num() > 0 || ColliderGroups.Num() > 0 || Colliders.Num() > 0 || Joints.Num() > 0);
    }
};