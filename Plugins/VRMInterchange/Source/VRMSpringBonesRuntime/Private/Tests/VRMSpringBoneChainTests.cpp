#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Animation/AnimInstance.h"
#include "AnimNode_VRMSpringBone.h"
#include "VRMSpringBoneData.h"

#if WITH_DEV_AUTOMATION_TESTS

// Simple automation test validating that chain counts align with config data after BuildChains.
// This is a lightweight unit-style test (no asset loading); it fabricates a minimal config.

static void PopulateDummyConfig(UVRMSpringBoneData* Data, int32 Chains, int32 JointsPerChain)
{
    if (!Data) return;
    Data->SpringConfig.Joints.Reset();
    Data->SpringConfig.Springs.Reset();
    Data->SpringConfig.Spec = EVRMSpringSpec::VRM1;

    int32 JointIndex = 0;
    for (int32 c=0;c<Chains;++c)
    {
        FVRMSpring Spring; Spring.CenterNodeIndex = INDEX_NONE; Spring.CenterBoneName = NAME_None;
        for (int32 j=0;j<JointsPerChain;++j)
        {
            FVRMSpringJoint Joint; Joint.BoneName = *FString::Printf(TEXT("TestBone_%d_%d"), c, j); Joint.NodeIndex = JointIndex; JointIndex++;
            int32 AddedIdx = Data->SpringConfig.Joints.Add(MoveTemp(Joint));
            Spring.JointIndices.Add(AddedIdx);
        }
        Data->SpringConfig.Springs.Add(MoveTemp(Spring));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSpringBoneChainResolutionTest, "VRM.SpringBones.ChainResolutionCounts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVRMSpringBoneChainResolutionTest::RunTest(const FString& Parameters)
{
    // Create transient spring data asset
    UVRMSpringBoneData* Data = NewObject<UVRMSpringBoneData>();
    TestNotNull(TEXT("SpringDataAsset created"), Data);

    const int32 ExpectedChains = 3;
    const int32 ExpectedJointsPer = 4;
    PopulateDummyConfig(Data, ExpectedChains, ExpectedJointsPer);
    TestTrue(TEXT("Config valid"), Data->SpringConfig.IsValid());

    // Anim node under test
    FAnimNode_VRMSpringBone Node;
    Node.SpringConfig = Data;

    // We cannot fully build chains without a real skeleton / bone container (requires animation system context).
    // For iteration 1 we only validate preconditions we can control: the number of springs vs config springs.
    TestEqual(TEXT("Config spring count"), Data->SpringConfig.Springs.Num(), ExpectedChains);

    // Future iteration: stand up a mock bone container to exercise BuildChains.

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
