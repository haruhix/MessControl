#pragma once

#include "CoreMinimal.h"

class UMCPerkVFXProfile;

/** Canonical persisted presentation source, independent of preview actor lifetime. */
struct FMCVFXCanonicalProfileState
{
    FName SourceID;
    FSoftObjectPath OwnerAsset;
    FName OwnerKey;
    TSoftObjectPtr<UMCPerkVFXProfile> Profile;
    FString OwnerDigest;
    FString ProfileDigest;
    /** Captures the loaded profile's values as well as its on-disk identity. */
    FString ProfileValueDigest;
    FName CanonicalRowID; // None for a non-perk library source.
    bool bOwnerKeyPresent=false;
    TArray<FName> AllowedEvents;
};

/** A failed save is not an implicit successful in-memory assignment. */
struct FMCVFXProfileCommitResult
{
    bool bSaved=false;
    bool bChangedInMemory=false;
    bool bRestoredPreviousAssignment=false;
    TSoftObjectPtr<UMCPerkVFXProfile> PreviousProfile;
    FString Message;
};

namespace MCVFXProfileBindings
{
    /** Metadata registration only; no package loads or writes at static init. */
    void RegisterLibrarySource(FName SourceID,const FSoftObjectPath& LibraryAsset,FName OwnerKey,
        const TArray<FName>& AllowedEvents);
    /** DT-backed adapters retain their canonical row; registered library sources need no row. */
    bool ReadCanonical(FName SourceID,FMCVFXCanonicalProfileState& OutState,FString& OutError);
    bool ValidateUnchanged(const FMCVFXCanonicalProfileState& State,FString& OutError);
    /** Shared persisted-resource gate for generic and legacy authoring writers. */
    bool ValidateSavedProfileResources(const UMCPerkVFXProfile* Profile,const TArray<FName>& AllowedEvents,FString& OutError);
    /** All new profile/resources must already be clean saved runtime assets. Writes the owner last. */
    bool AssignSavedProfile(UMCPerkVFXProfile* SavedProfile,const FMCVFXCanonicalProfileState& Expected,
        FMCVFXProfileCommitResult& OutResult,FString& OutError);
}
