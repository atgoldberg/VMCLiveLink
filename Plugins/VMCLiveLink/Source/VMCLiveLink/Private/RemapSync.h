// RemapSync.h
#pragma once
#include "CoreMinimal.h"
#include "LiveLinkTypes.h"

// Per-subject remap tables
struct FRemapMaps
{
	TMap<FName, FName> BoneMap;
	TMap<FName, FName> CurveMap;
};

// Update from the remapper (by subject key)
void VMCLiveLink_UpdateRemapMaps(const FLiveLinkSubjectKey& Key,
	const TMap<FName, FName>& BoneMap,
	const TMap<FName, FName>& CurveMap);

// Query from the source (by guid + subject name)
bool VMCLiveLink_GetRemapMaps(const FGuid& SourceGuid, const FName& SubjectName, FRemapMaps& Out);
