#pragma once

#include "CoreMinimal.h"

// Simple editor-only tombstone manager to remember deleted VRM spring imports (by source hash) so
// auto/implicit reimport will not recreate user-deleted assets on next editor restart.
// Lightweight newline-separated text file.

class FVRMDeletedImportManager
{
public:
    static FVRMDeletedImportManager& Get()
    { 
        static FVRMDeletedImportManager Instance; 
        Instance.EnsureLoaded(); 
        return Instance; 
    }

    bool Contains(const FString& SourceHash) const 
    { 
        return DeletedSourceHashes.Contains(SourceHash); 
    }

    void Add(const FString& SourceHash)
    {
        if (SourceHash.IsEmpty()) return;
        if (!DeletedSourceHashes.Contains(SourceHash))
        {
            DeletedSourceHashes.Add(SourceHash);
            Save();
        }
    }

    void Remove(const FString& SourceHash)
    { 
        if (DeletedSourceHashes.Contains(SourceHash)) 
        { 
            DeletedSourceHashes.Remove(SourceHash); 
            Save(); 
        } 
    }

private:
    TSet<FString> DeletedSourceHashes; 
    bool bLoaded=false;

    FString GetStoreFilename() const 
    { 
        return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VRMInterchange"), TEXT("DeletedImports.txt")); 
    }

    void EnsureLoaded()
    {
        if (bLoaded) return; 
        bLoaded=true; 
        const FString StoreFile=GetStoreFilename(); 
        if(!FPaths::FileExists(StoreFile)) 
            return; 
        
        TArray<FString> Lines; 
        if(FFileHelper::LoadFileToStringArray(Lines,*StoreFile)) 
            for(const FString& L:Lines)
            { 
                FString T=L.TrimStartAndEnd(); 
                if(!T.IsEmpty()) 
                    DeletedSourceHashes.Add(T);
            } 
    }

    void Save()
    { 
        IFileManager::Get().MakeDirectory(*FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VRMInterchange")), true); 
        FString Out; 
        for(const FString& H:DeletedSourceHashes)
        { 
            Out+=H+TEXT("\n"); 
        } 
        FFileHelper::SaveStringToFile(Out,*GetStoreFilename(),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); 
    }
};
