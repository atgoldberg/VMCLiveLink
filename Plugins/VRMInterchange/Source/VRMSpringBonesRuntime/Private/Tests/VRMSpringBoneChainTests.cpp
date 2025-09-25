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

// Test: ComputeRestFromPositions should compute segment lengths and normalized directions from component-space positions
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSpringBoneRestFromPositionsTest, "VRM.SpringBones.RestFromPositions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVRMSpringBoneRestFromPositionsTest::RunTest(const FString& Parameters)
{
    FAnimNode_VRMSpringBone::FChainInfo Chain;

    // Simple straight chain along X: (0,0,0) -> (10,0,0) -> (20,0,0)
    Chain.RestComponentPositions = TArray<FVector>{ FVector::ZeroVector, FVector(10.f,0.f,0.f), FVector(20.f,0.f,0.f) };
    FAnimNode_VRMSpringBone::ComputeRestFromPositions(Chain);
    TestEqual(TEXT("Segments count"), Chain.SegmentRestLengths.Num(), 3);
    TestTrue(TEXT("Segment 1 length"), FMath::IsNearlyEqual(Chain.SegmentRestLengths[1], 10.f, KINDA_SMALL_NUMBER));
    TestTrue(TEXT("Segment 2 length"), FMath::IsNearlyEqual(Chain.SegmentRestLengths[2], 10.f, KINDA_SMALL_NUMBER));
    TestTrue(TEXT("Direction unit X"), Chain.RestDirections[1].Equals(FVector(1.f,0.f,0.f), KINDA_SMALL_NUMBER) && Chain.RestDirections[2].Equals(FVector(1.f,0.f,0.f), KINDA_SMALL_NUMBER));

    // Non-uniform positions: second segment has diagonal offset
    Chain.RestComponentPositions = TArray<FVector>{ FVector::ZeroVector, FVector(10.f,0.f,0.f), FVector(20.f,10.f,0.f) };
    FAnimNode_VRMSpringBone::ComputeRestFromPositions(Chain);
    const float ExpectedLen2 = FVector(20.f,10.f,0.f).Size() - FVector(10.f,0.f,0.f).Size();
    // Instead compute segment2 len directly
    const float Seg2 = (FVector(20.f,10.f,0.f) - FVector(10.f,0.f,0.f)).Size();
    TestTrue(TEXT("Segment 2 diagonal length"), FMath::IsNearlyEqual(Chain.SegmentRestLengths[2], Seg2, KINDA_SMALL_NUMBER));
    FVector Dir2 = Chain.RestDirections[2];
    FVector ExpectedDir2 = (FVector(20.f,10.f,0.f) - FVector(10.f,0.f,0.f)).GetSafeNormal();
    TestTrue(TEXT("Segment 2 direction normalized"), Dir2.Equals(ExpectedDir2, KINDA_SMALL_NUMBER));

    return true;
}

// Test: mirrored chain should produce directions with negative X when positions are mirrored
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSpringBoneMirroredRestDirectionsTest, "VRM.SpringBones.MirroredRestDirections", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVRMSpringBoneMirroredRestDirectionsTest::RunTest(const FString& Parameters)
{
    FAnimNode_VRMSpringBone::FChainInfo Chain;
    Chain.RestComponentPositions = TArray<FVector>{ FVector::ZeroVector, FVector(-10.f,0.f,0.f) };
    FAnimNode_VRMSpringBone::ComputeRestFromPositions(Chain);
    TestEqual(TEXT("Segments count"), Chain.SegmentRestLengths.Num(), 2);
    TestTrue(TEXT("Mirrored direction X"), Chain.RestDirections[1].Equals(FVector(-1.f,0.f,0.f), KINDA_SMALL_NUMBER));
    TestTrue(TEXT("Mirrored length"), FMath::IsNearlyEqual(Chain.SegmentRestLengths[1], 10.f, KINDA_SMALL_NUMBER));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
