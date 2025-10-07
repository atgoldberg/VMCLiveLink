// Copyright (c) 2025 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.

#include "VRMSpringBonesValidation.h"
#include "VRMInterchangeLog.h"
#include "Misc/FileHelper.h"

namespace VRM
{
    FVRMValidationResult ValidateSpringConfig(const FVRMSpringConfig& Config)
    {
        FVRMValidationResult Result;
        Result.bIsValid = true; // Start optimistic
        
        // Basic configuration checks
        if (Config.Spec == EVRMSpringSpec::None)
        {
            Result.AddError(TEXT("No VRM specification detected"));
            return Result;
        }
        
        Result.AddInfo(FString::Printf(TEXT("VRM Specification: %s"), 
            Config.Spec == EVRMSpringSpec::VRM0 ? TEXT("0.x") : 
            Config.Spec == EVRMSpringSpec::VRM1 ? TEXT("1.0") : TEXT("Unknown")));
        
        // Check if we have any spring data
        if (Config.Springs.Num() == 0 && Config.Colliders.Num() == 0 && Config.Joints.Num() == 0)
        {
            Result.AddWarning(TEXT("No spring bone data found (springs, colliders, or joints)"));
        }
        
        // Validate springs
        for (int32 SpringIdx = 0; SpringIdx < Config.Springs.Num(); SpringIdx++)
        {
            const FVRMSpring& Spring = Config.Springs[SpringIdx];
            
            if (Spring.Name.IsEmpty())
            {
                Result.AddWarning(FString::Printf(TEXT("Spring %d has no name"), SpringIdx));
            }
            
            if (Spring.JointIndices.Num() == 0)
            {
                Result.AddWarning(FString::Printf(TEXT("Spring '%s' has no joints"), *Spring.Name));
            }
            
            // Validate parameter ranges
            if (Spring.Stiffness < 0.0f || Spring.Stiffness > 1.0f)
            {
                Result.AddWarning(FString::Printf(TEXT("Spring '%s' stiffness (%.3f) outside normal range [0,1]"), *Spring.Name, Spring.Stiffness));
            }
            
            if (Spring.Drag < 0.0f || Spring.Drag > 1.0f)
            {
                Result.AddWarning(FString::Printf(TEXT("Spring '%s' drag (%.3f) outside normal range [0,1]"), *Spring.Name, Spring.Drag));
            }
            
            // Check joint references
            for (int32 JointIdx : Spring.JointIndices)
            {
                if (JointIdx < 0 || JointIdx >= Config.Joints.Num())
                {
                    Result.AddError(FString::Printf(TEXT("Spring '%s' references invalid joint index %d (max: %d)"), 
                        *Spring.Name, JointIdx, Config.Joints.Num() - 1));
                }
            }
            
            // Check collider group references
            for (int32 GroupIdx : Spring.ColliderGroupIndices)
            {
                if (GroupIdx < 0 || GroupIdx >= Config.ColliderGroups.Num())
                {
                    Result.AddError(FString::Printf(TEXT("Spring '%s' references invalid collider group %d (max: %d)"), 
                        *Spring.Name, GroupIdx, Config.ColliderGroups.Num() - 1));
                }
            }
        }
        
        // Validate collider groups
        for (int32 GroupIdx = 0; GroupIdx < Config.ColliderGroups.Num(); GroupIdx++)
        {
            const FVRMSpringColliderGroup& Group = Config.ColliderGroups[GroupIdx];
            
            for (int32 ColliderIdx : Group.ColliderIndices)
            {
                if (ColliderIdx < 0 || ColliderIdx >= Config.Colliders.Num())
                {
                    Result.AddError(FString::Printf(TEXT("Collider group '%s' references invalid collider %d (max: %d)"), 
                        *Group.Name, ColliderIdx, Config.Colliders.Num() - 1));
                }
            }
        }
        
        // Validate colliders
        for (int32 ColliderIdx = 0; ColliderIdx < Config.Colliders.Num(); ColliderIdx++)
        {
            const FVRMSpringCollider& Collider = Config.Colliders[ColliderIdx];
            
            bool bHasShape = (Collider.Spheres.Num() > 0) || (Collider.Capsules.Num() > 0);
            if (!bHasShape)
            {
                Result.AddWarning(FString::Printf(TEXT("Collider %d has no shapes (spheres or capsules)"), ColliderIdx));
            }
            
            // Validate sphere shapes
            for (const FVRMSpringColliderSphere& Sphere : Collider.Spheres)
            {
                if (Sphere.Radius <= 0.0f)
                {
                    Result.AddWarning(FString::Printf(TEXT("Collider %d has sphere with invalid radius %.3f"), 
                        ColliderIdx, Sphere.Radius));
                }
            }
            
            // Validate capsule shapes
            for (const FVRMSpringColliderCapsule& Capsule : Collider.Capsules)
            {
                if (Capsule.Radius <= 0.0f)
                {
                    Result.AddWarning(FString::Printf(TEXT("Collider %d has capsule with invalid radius %.3f"), 
                        ColliderIdx, Capsule.Radius));
                }
            }
        }
        
        // Add summary info
        if (Result.bIsValid)
        {
            Result.AddInfo(FString::Printf(TEXT("Configuration valid: %d springs, %d colliders, %d joints"), 
                Config.Springs.Num(), Config.Colliders.Num(), Config.Joints.Num()));
        }
        
        return Result;
    }
    
    FString GenerateDiagnosticReport(const FVRMSpringConfig& Config)
    {
        FVRMValidationResult Validation = ValidateSpringConfig(Config);
        
        FString Report;
        Report += TEXT("=== VRM Spring Bone Diagnostic Report ===\n");
        Report += FString::Printf(TEXT("Status: %s\n"), Validation.bIsValid ? TEXT("VALID") : TEXT("INVALID"));
        Report += TEXT("\n");
        
        // Basic stats
        Report += TEXT("Configuration Summary:\n");
        Report += FString::Printf(TEXT("  Specification: %s\n"), 
            Config.Spec == EVRMSpringSpec::VRM0 ? TEXT("VRM 0.x") : 
            Config.Spec == EVRMSpringSpec::VRM1 ? TEXT("VRM 1.0") : TEXT("None"));
        Report += FString::Printf(TEXT("  Springs: %d\n"), Config.Springs.Num());
        Report += FString::Printf(TEXT("  Colliders: %d\n"), Config.Colliders.Num());
        Report += FString::Printf(TEXT("  Collider Groups: %d\n"), Config.ColliderGroups.Num());
        Report += FString::Printf(TEXT("  Joints: %d\n"), Config.Joints.Num());
        Report += TEXT("\n");
        
        // Errors
        if (Validation.Errors.Num() > 0)
        {
            Report += FString::Printf(TEXT("Errors (%d):\n"), Validation.Errors.Num());
            for (const FString& Error : Validation.Errors)
            {
                Report += FString::Printf(TEXT("  ERROR: %s\n"), *Error);
            }
            Report += TEXT("\n");
        }
        
        // Warnings
        if (Validation.Warnings.Num() > 0)
        {
            Report += FString::Printf(TEXT("Warnings (%d):\n"), Validation.Warnings.Num());
            for (const FString& Warning : Validation.Warnings)
            {
                Report += FString::Printf(TEXT("  WARN: %s\n"), *Warning);
            }
            Report += TEXT("\n");
        }
        
        // Info
        if (Validation.Info.Num() > 0)
        {
            Report += TEXT("Details:\n");
            for (const FString& Info : Validation.Info)
            {
                Report += FString::Printf(TEXT("  INFO: %s\n"), *Info);
            }
        }
        
        return Report;
    }
    
    bool HasSpringBoneData(const FString& Filename)
    {
        FString Content;
        if (!FFileHelper::LoadFileToString(Content, *Filename))
        {
            return false;
        }
        
        // Simple string search for VRM extensions
        return Content.Contains(TEXT("VRMC_springBone")) || 
               Content.Contains(TEXT("secondaryAnimation"));
    }
}