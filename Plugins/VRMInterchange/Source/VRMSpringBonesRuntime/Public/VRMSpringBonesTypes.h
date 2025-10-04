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
struct VRMSPRINGBONESRUNTIME_API FVRMSpringColliderSphere
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="VRM") FVector Offset = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, Category="VRM") float Radius = 0.f;
    UPROPERTY(VisibleAnywhere, Category="VRM") bool bInside = false;
};

USTRUCT(BlueprintType)
struct VRMSPRINGBONESRUNTIME_API FVRMSpringColliderCapsule
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="VRM") FVector Offset = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, Category="VRM") float Radius = 0.f;
    UPROPERTY(VisibleAnywhere, Category="VRM") FVector TailOffset = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, Category="VRM") bool bInside = false;
};

USTRUCT(BlueprintType)
struct VRMSPRINGBONESRUNTIME_API FVRMSpringColliderPlane
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="VRM") FVector Offset = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, Category="VRM") FVector Normal = FVector(0,0,1);
};

USTRUCT(BlueprintType)
struct VRMSPRINGBONESRUNTIME_API FVRMSpringCollider
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="VRM") int32 NodeIndex = INDEX_NONE;
    UPROPERTY(VisibleAnywhere, Category="VRM") FName BoneName;
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<FVRMSpringColliderSphere> Spheres;
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<FVRMSpringColliderCapsule> Capsules;
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<FVRMSpringColliderPlane> Planes;
};

USTRUCT(BlueprintType)
struct VRMSPRINGBONESRUNTIME_API FVRMSpringColliderGroup
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="VRM") FString Name;
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<int32> ColliderIndices;
};

USTRUCT(BlueprintType)
struct VRMSPRINGBONESRUNTIME_API FVRMSpringJoint
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="VRM") int32 NodeIndex = INDEX_NONE;
    UPROPERTY(VisibleAnywhere, Category="VRM") FName BoneName;
    UPROPERTY(VisibleAnywhere, Category="VRM") float HitRadius = 0.f;
};

USTRUCT(BlueprintType)
struct VRMSPRINGBONESRUNTIME_API FVRMSpring
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="VRM") FString Name;
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<int32> JointIndices;
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<int32> ColliderGroupIndices;
    UPROPERTY(VisibleAnywhere, Category="VRM") int32 CenterNodeIndex = INDEX_NONE;
    UPROPERTY(VisibleAnywhere, Category="VRM") FName CenterBoneName;

    UPROPERTY(EditAnywhere, Category="VRM|Spring", meta=(ClampMin="0.0", ClampMax="1.0")) float Stiffness = 0.f;
    UPROPERTY(EditAnywhere, Category="VRM|Spring", meta=(ClampMin="0.0", ClampMax="1.0")) float Drag = 0.f;
    UPROPERTY(EditAnywhere, Category="VRM|Spring") FVector GravityDir = FVector(0, 0, -1);
    UPROPERTY(EditAnywhere, Category="VRM|Spring", meta=(ClampMin="0.0")) float GravityPower = 0.f;
    UPROPERTY(EditAnywhere, Category="VRM|Spring", meta=(ClampMin="0.0")) float HitRadius = 0.f;
};

USTRUCT(BlueprintType)
struct VRMSPRINGBONESRUNTIME_API FVRMSpringConfig
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="VRM") EVRMSpringSpec Spec = EVRMSpringSpec::None;

    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<FVRMSpringCollider> Colliders;
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<FVRMSpringColliderGroup> ColliderGroups;
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<FVRMSpringJoint> Joints;
    UPROPERTY(EditAnywhere, Category="VRM", meta=(EditFixedSize)) TArray<FVRMSpring> Springs;

    UPROPERTY(VisibleAnywhere, Category="VRM") FString RawJson;

    bool IsValid() const
    {
        return Spec != EVRMSpringSpec::None && (Springs.Num() > 0 || ColliderGroups.Num() > 0 || Colliders.Num() > 0 || Joints.Num() > 0);
    }
};

USTRUCT(BlueprintType)
struct VRMSPRINGBONESRUNTIME_API FVRMNodeChildren
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "VRM|Hierarchy")
    TArray<int32> Children;
};