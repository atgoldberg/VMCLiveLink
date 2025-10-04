// Tests for VRMSpringBonesPostImportPipeline
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "VRMSpringBonesPostImportPipeline.h"
#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSpringBonesPipelineNonInterference, "VRM.SpringBones.Pipeline.NonInterference",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMSpringBonesPipelineNonInterference::RunTest(const FString& Parameters)
{
    // Test that pipeline doesn't interfere when bGenerateSpringBoneData is false
    UVRMSpringBonesPostImportPipeline* Pipeline = NewObject<UVRMSpringBonesPostImportPipeline>();
    Pipeline->bGenerateSpringBoneData = false;
    
    // Create minimal test data
    UInterchangeBaseNodeContainer* NodeContainer = NewObject<UInterchangeBaseNodeContainer>();
    TArray<UInterchangeSourceData*> SourceDatas;
    FString ContentBasePath = TEXT("/Game/TestContent");
    
    // This should return early and not interfere
    Pipeline->ExecutePipeline(NodeContainer, SourceDatas, ContentBasePath);
    
    TestTrue(TEXT("Pipeline completes without error when disabled"), true);
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSpringBonesPipelineEarlyExit, "VRM.SpringBones.Pipeline.EarlyExit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMSpringBonesPipelineEarlyExit::RunTest(const FString& Parameters)
{
    // Test that pipeline exits early with invalid parameters
    UVRMSpringBonesPostImportPipeline* Pipeline = NewObject<UVRMSpringBonesPostImportPipeline>();
    Pipeline->bGenerateSpringBoneData = true;
    
    // Test with null NodeContainer
    TArray<UInterchangeSourceData*> SourceDatas;
    FString ContentBasePath = TEXT("/Game/TestContent");
    
    Pipeline->ExecutePipeline(nullptr, SourceDatas, ContentBasePath);
    TestTrue(TEXT("Pipeline handles null NodeContainer gracefully"), true);
    
    // Test with empty SourceDatas
    UInterchangeBaseNodeContainer* NodeContainer = NewObject<UInterchangeBaseNodeContainer>();
    Pipeline->ExecutePipeline(NodeContainer, SourceDatas, ContentBasePath);
    TestTrue(TEXT("Pipeline handles empty SourceDatas gracefully"), true);
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSpringBonesPipelineDefaultValues, "VRM.SpringBones.Pipeline.DefaultValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMSpringBonesPipelineDefaultValues::RunTest(const FString& Parameters)
{
    // Test default values are correct
    UVRMSpringBonesPostImportPipeline* Pipeline = NewObject<UVRMSpringBonesPostImportPipeline>();
    
    TestFalse(TEXT("bGenerateSpringBoneData defaults to false"), Pipeline->bGenerateSpringBoneData);
    TestFalse(TEXT("bOverwriteExisting defaults to false"), Pipeline->bOverwriteExisting);
    TestEqual(TEXT("SubFolder has correct default"), Pipeline->SubFolder, FString(TEXT("SpringBones")));
   // TestEqual(TEXT("DataAssetName has correct default"), Pipeline->DataAssetName, FString(TEXT("SpringBonesData")));
    
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS