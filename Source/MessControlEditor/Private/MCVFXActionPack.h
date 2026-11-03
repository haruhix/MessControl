#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MCVFXActionLayout.h"
#include "MCVFXActionPack.generated.h"

class UMCPerkVFXProfile;
class UMCVFXRecipePreset;

/** Content hashes include the package and present bulk/exports sidecars, never machine paths. */
USTRUCT()
struct FMCVFXActionPackDependencyFingerprint
{
    GENERATED_BODY()
    UPROPERTY() FName PackageName;
    UPROPERTY() FString ContentDigest;
};

/** Immutable saved-version manifest. All actual presentation settings remain in existing assets. */
UCLASS()
class MESSCONTROLEDITOR_API UMCVFXActionPack : public UDataAsset
{
    GENERATED_BODY()
public:
    static constexpr int32 CurrentSchemaVersion=1;
    UPROPERTY(VisibleAnywhere,Category="Pack",AssetRegistrySearchable) int32 SchemaVersion=CurrentSchemaVersion;
    UPROPERTY(VisibleAnywhere,Category="Pack",AssetRegistrySearchable) FGuid PackID;
    UPROPERTY(VisibleAnywhere,Category="Pack",AssetRegistrySearchable) FName SourceID;
    UPROPERTY(VisibleAnywhere,Category="Pack") TArray<FName> EventIDs;
    UPROPERTY(VisibleAnywhere,Category="Pack",AssetRegistrySearchable) int32 ProviderSchemaVersion=1;
    /** Zero means that the saved profile has no explicit native override. */
    UPROPERTY(VisibleAnywhere,Category="Pack",AssetRegistrySearchable) int32 NativeSchemaVersion=0;
    UPROPERTY(VisibleAnywhere,Category="Pack",AssetRegistrySearchable) FText Label;
    UPROPERTY(VisibleAnywhere,Category="Pack",AssetRegistrySearchable) FString VersionLabel;
    UPROPERTY(VisibleAnywhere,Category="Pack") FDateTime CreatedUTC;
    UPROPERTY(VisibleAnywhere,Category="Sources") TSoftObjectPtr<UMCPerkVFXProfile> RuntimeProfile;
    UPROPERTY(VisibleAnywhere,Category="Sources") TMap<FName,TSoftObjectPtr<UMCVFXRecipePreset>> RecipeMasters;
    UPROPERTY(VisibleAnywhere,Category="Layout") FMCVFXActionLayout Layout;
    UPROPERTY(VisibleAnywhere,Category="Dependencies") TArray<FMCVFXActionPackDependencyFingerprint> Dependencies;

    virtual bool IsEditorOnly() const override { return true; }
    virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;
    /** Shape and portable reference syntax only; no loads, writes or canonical access. */
    bool ValidateStructure(FString& Error) const;
};
