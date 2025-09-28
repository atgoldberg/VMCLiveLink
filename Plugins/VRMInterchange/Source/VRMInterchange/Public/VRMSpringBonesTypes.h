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
    // If true, this is an inside-sphere collider (prevents joints from leaving the sphere)
    UPROPERTY(VisibleAnywhere, Category="VRM") bool bInside = false;
};

USTRUCT(BlueprintType)
struct VRMINTERCHANGE_API FVRMSpringColliderCapsule
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="VRM") FVector Offset = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, Category="VRM") float Radius = 0.f;
    UPROPERTY(VisibleAnywhere, Category="VRM") FVector TailOffset = FVector::ZeroVector;
    // If true, this is an inside-capsule collider (prevents joints from leaving the capsule)
    UPROPERTY(VisibleAnywhere, Category="VRM") bool bInside = false;
};

USTRUCT(BlueprintType)
struct VRMINTERCHANGE_API FVRMSpringColliderPlane
{
    GENERATED_BODY()

    // Plane offset and normal in local space
    UPROPERTY(VisibleAnywhere, Category="VRM") FVector Offset = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, Category="VRM") FVector Normal = FVector(0,0,1);
};

USTRUCT(BlueprintType)
struct VRMINTERCHANGE_API FVRMSpringCollider
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="VRM") int32 NodeIndex = INDEX_NONE;
    UPROPERTY(VisibleAnywhere, Category="VRM") FName BoneName;
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<FVRMSpringColliderSphere> Spheres;
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<FVRMSpringColliderCapsule> Capsules;
    // Extended shapes: planes
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<FVRMSpringColliderPlane> Planes;
};

USTRUCT(BlueprintType)
struct VRMINTERCHANGE_API FVRMSpringColliderGroup
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="VRM") FString Name;
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<int32> ColliderIndices;
};

USTRUCT(BlueprintType)
struct VRMINTERCHANGE_API FVRMSpringJoint
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="VRM") int32 NodeIndex = INDEX_NONE;
    UPROPERTY(VisibleAnywhere, Category="VRM") FName BoneName;
    UPROPERTY(VisibleAnywhere, Category="VRM") float HitRadius = 0.f;
};

USTRUCT(BlueprintType)
struct VRMINTERCHANGE_API FVRMSpring
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="VRM") FString Name;
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<int32> JointIndices;
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<int32> ColliderGroupIndices;
    UPROPERTY(VisibleAnywhere, Category="VRM") int32 CenterNodeIndex = INDEX_NONE;
    UPROPERTY(VisibleAnywhere, Category="VRM") FName CenterBoneName;

    UPROPERTY(EditAnywhere, Category="VRM|Spring", meta=(ClampMin="0.0", ClampMax="1.0", ToolTip="Spring stiffness [0,1].")) float Stiffness = 0.f;
    UPROPERTY(EditAnywhere, Category="VRM|Spring", meta=(ClampMin="0.0", ClampMax="1.0", ToolTip="Spring drag/damping [0,1].")) float Drag = 0.f;
    UPROPERTY(EditAnywhere, Category="VRM|Spring", meta=(ToolTip="Gravity direction vector for this spring (will be normalized).")) FVector GravityDir = FVector(0, 0, -1);
    UPROPERTY(EditAnywhere, Category="VRM|Spring", meta=(ClampMin="0.0", ToolTip="Gravity power / magnitude applied along GravityDir.")) float GravityPower = 0.f;
    UPROPERTY(EditAnywhere, Category="VRM|Spring", meta=(ClampMin="0.0", ToolTip="Default collision hit radius for joints in this spring.")) float HitRadius = 0.f;
};

USTRUCT(BlueprintType)
struct VRMINTERCHANGE_API FVRMSpringConfig
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="VRM") EVRMSpringSpec Spec = EVRMSpringSpec::None;

    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<FVRMSpringCollider> Colliders;
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<FVRMSpringColliderGroup> ColliderGroups;
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<FVRMSpringJoint> Joints;
    // Make Springs editable but fixed size so users can tune params without re-authoring structure
    UPROPERTY(EditAnywhere, Category="VRM", meta=(EditFixedSize, ToolTip="Per-spring tunables editable; element count fixed to imported data.")) TArray<FVRMSpring> Springs;

    UPROPERTY(VisibleAnywhere, Category="VRM") FString RawJson;

    bool IsValid() const
    {
        return Spec != EVRMSpringSpec::None && (Springs.Num() > 0 || ColliderGroups.Num() > 0 || Colliders.Num() > 0 || Joints.Num() > 0);
    }
};