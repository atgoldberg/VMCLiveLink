// RemapSync.cpp
#include "RemapSync.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformCrt.h"

static FRWLock GRemapLock;
static TMap<uint64, FRemapMaps> GRemapByKey;

static uint64 MakeKey(const FGuid& G, const FName& N)
{
	return HashCombine(GetTypeHash(G), GetTypeHash(N));
}

void VMCLiveLink_UpdateRemapMaps(const FLiveLinkSubjectKey& Key,
	const TMap<FName, FName>& BoneMap,
	const TMap<FName, FName>& CurveMap)
{
	const uint64 K = MakeKey(Key.Source, Key.SubjectName.Name);
	FWriteScopeLock W(GRemapLock);
	FRemapMaps& M = GRemapByKey.FindOrAdd(K);
	M.BoneMap = BoneMap;
	M.CurveMap = CurveMap;
}

bool VMCLiveLink_GetRemapMaps(const FGuid& SourceGuid, const FName& SubjectName, FRemapMaps& Out)
{
	const uint64 K = MakeKey(SourceGuid, SubjectName);
	FReadScopeLock R(GRemapLock);
	if (const FRemapMaps* Found = GRemapByKey.Find(K))
	{
		Out = *Found;
		return true;
	}
	return false;
}
