#pragma once

#include "CoreMinimal.h"
#include "MCVFXActionPack.h"
#include "MCPerkVFXProfile.h"
#include "MCVFXRecipePreset.h"
#include "UObject/StrongObjectPtr.h"

/** Consumes artifacts already saved by the Session writer. This helper saves only the final manifest. */
struct FMCVFXActionPackSaveRequest
{
    FString ManifestPackagePath;
    FName SourceID;
    TArray<FName> EventIDs;
    int32 ProviderSchemaVersion=1;
    int32 NativeSchemaVersion=0;
    FText Label;
    FString VersionLabel;
    UMCPerkVFXProfile* RuntimeProfile=nullptr;
    TMap<FName,UMCVFXRecipePreset*> RecipeMasters;
    FMCVFXActionLayout Layout;
    /** Optional captured source baseline, checked before any new manifest is created. */
    TArray<FMCVFXActionPackDependencyFingerprint> ExpectedDependencies;
};

struct FMCVFXActionPackSaveResult
{
    TStrongObjectPtr<UMCVFXActionPack> Pack;
    /** Includes an unsaved intermediate manifest on failure. Nothing is deleted. */
    TArray<FSoftObjectPath> CreatedAssets;
    bool bSaved=false;
};

/** Retained saved objects only. Session must clone them before transactional editing. */
struct FMCVFXActionPackLoaded
{
    TStrongObjectPtr<UMCVFXActionPack> Pack;
    TStrongObjectPtr<UMCPerkVFXProfile> RuntimeProfile;
    TMap<FName,TStrongObjectPtr<UMCVFXRecipePreset>> RecipeMasters;
    FMCVFXActionLayout Layout;
    TArray<FString> DependencyWarnings;
};

namespace MCVFXActionPacks
{
    bool Validate(const UMCVFXActionPack* Pack,FString& Error,bool bRequireSavedManifest=true);
    bool CaptureDependencies(UMCPerkVFXProfile* Profile,const TMap<FName,UMCVFXRecipePreset*>& Masters,
        TArray<FMCVFXActionPackDependencyFingerprint>& Out,FString& Error,FName SourceID=NAME_None);
    /** Changed/missing/dirty resources are warnings, not silently copied historical snapshots. */
    bool CheckDependencies(const UMCVFXActionPack* Pack,TArray<FString>& OutWarnings,FString& Error);
    bool SaveNew(const FMCVFXActionPackSaveRequest& Request,FMCVFXActionPackSaveResult& Out,FString& Error);
    /** No canonical assignment. Failure leaves Out unchanged. Changed dependencies require explicit opt-in. */
    bool Load(const UMCVFXActionPack* Pack,FName ExpectedSource,const TArray<FName>& ExpectedEvents,
        int32 ExpectedProviderSchema,int32 ExpectedNativeSchema,FMCVFXActionPackLoaded& Out,FString& Error,
        bool bAllowChangedDependencies=false);
}
