// Copyright (c) 2024 Lifelike & Believable Animation Design, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VRMSpringBonesTypes.h"

namespace VRM
{
    /**
     * Validation and diagnostic utilities for VRM spring bone configurations
     */
    
    VRMINTERCHANGE_API struct FVRMValidationResult
    {
        bool bIsValid = false;
        TArray<FString> Warnings;
        TArray<FString> Errors;
        TArray<FString> Info;
        
        void AddWarning(const FString& Message) { Warnings.Add(Message); }
        void AddError(const FString& Message) { Errors.Add(Message); bIsValid = false; }
        void AddInfo(const FString& Message) { Info.Add(Message); }
        
        bool HasIssues() const { return Warnings.Num() > 0 || Errors.Num() > 0; }
        
        FString GetSummary() const
        {
            FString Summary = FString::Printf(TEXT("VRM Validation: %s"), bIsValid ? TEXT("VALID") : TEXT("INVALID"));
            if (Errors.Num() > 0) Summary += FString::Printf(TEXT(" (%d errors)"), Errors.Num());
            if (Warnings.Num() > 0) Summary += FString::Printf(TEXT(" (%d warnings)"), Warnings.Num());
            return Summary;
        }
    };
    
    /**
     * Validate a VRM spring bone configuration for common issues
     */
    VRMINTERCHANGE_API FVRMValidationResult ValidateSpringConfig(const FVRMSpringConfig& Config);
    
    /**
     * Generate a diagnostic report for a VRM spring bone configuration
     */
    VRMINTERCHANGE_API FString GenerateDiagnosticReport(const FVRMSpringConfig& Config);
    
    /**
     * Check if a VRM file contains spring bone data without full parsing
     */
    VRMINTERCHANGE_API bool HasSpringBoneData(const FString& Filename);
}