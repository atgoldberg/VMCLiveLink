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

// Called once after ParseSpringBonesFromJson(... OutNodeParent, OutNodeChildren ...)
void UVRMSpringBoneData::BuildResolvedChildren()
{
    const FVRMSpringConfig& Cfg = SpringConfig;
    ResolvedChildNodeIndexPerJoint.SetNum(Cfg.Joints.Num());
    TSet<int32> SpringJointNodes; SpringJointNodes.Reserve(Cfg.Joints.Num());
    for (const auto& J : Cfg.Joints) { SpringJointNodes.Add(J.NodeIndex); }

    for (const FVRMSpring& S : Cfg.Springs)
    {
        // walk chain in order
        for (int32 i = 0; i < S.JointIndices.Num(); ++i)
        {
            const int32 JointIdx = S.JointIndices[i];
            const int32 ThisNode = Cfg.Joints[JointIdx].NodeIndex;

            int32 ChosenChild = INDEX_NONE;

            // Preferred: the next joint in the chain, if it is an actual child
            if (i + 1 < S.JointIndices.Num())
            {
                const int32 NextJointIdx = S.JointIndices[i + 1];
                const int32 NextNode = Cfg.Joints[NextJointIdx].NodeIndex;

                if (const FVRMNodeChildren* Kids = NodeChildren.Find(ThisNode))
                {
                    if (Kids->Children.Contains(NextNode))
                    {
                        ChosenChild = NextNode;
                    }
                }
            }

            // Fallback: pick the first child that is *also* in this spring (handles forks)
            if (ChosenChild == INDEX_NONE)
            {
                if (const FVRMNodeChildren* Kids = NodeChildren.Find(ThisNode))
                {
                    for (int32 K : Kids->Children)
                    {
                        if (SpringJointNodes.Contains(K)) { ChosenChild = K; break; }
                    }
                }
            }

            ResolvedChildNodeIndexPerJoint[JointIdx] = ChosenChild; // can be INDEX_NONE (=> 7cm pseudo tail)
        }
    }
}

#endif
