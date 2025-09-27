#include "VRMSpringBoneData.h"
#if WITH_EDITOR
#include "UObject/UnrealType.h"
#endif

#if WITH_EDITOR
void UVRMSpringBoneData::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    if (!PropertyChangedEvent.Property && !PropertyChangedEvent.MemberProperty) { return; }

    const FName PropName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
    const FName MemberName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

    static const TSet<FName> TunableNames = { TEXT("Stiffness"), TEXT("Drag"), TEXT("GravityDir"), TEXT("GravityPower"), TEXT("HitRadius") };

    bool bBump = false;
    if (TunableNames.Contains(PropName) || TunableNames.Contains(MemberName))
    {
        bBump = true;
    }
    // Editing any element of Springs array (even struct swap) should bump
    if (MemberName == TEXT("Springs") || PropName == TEXT("Springs"))
    {
        bBump = true; 
    }

    if (bBump)
    {
        ++EditRevision;
        for (FVRMSpring& Spring : SpringConfig.Springs)
        {
            Spring.Stiffness = FMath::Clamp(Spring.Stiffness, 0.f, 1.f);
            Spring.Drag = FMath::Clamp(Spring.Drag, 0.f, 1.f);
            if (!Spring.GravityDir.IsNearlyZero())
            {
                Spring.GravityDir = Spring.GravityDir.GetSafeNormal();
            }
            Spring.GravityPower = FMath::Max(0.f, Spring.GravityPower);
            Spring.HitRadius = FMath::Max(0.f, Spring.HitRadius);
        }
    }
}
#endif
