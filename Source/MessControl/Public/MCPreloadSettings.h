#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MCPreloadSettings.generated.h"

class UTexture2D;

USTRUCT()
struct FMCMapPreloadAssets
{
    GENERATED_BODY()
    /** Short map name, without PIE prefixes, e.g. L_Mouth. */
    UPROPERTY(EditAnywhere, Category="Loading") FName Map;
    /** Additional visual assets or DA/DT roots. Maps and streamed audio are excluded. */
    UPROPERTY(EditAnywhere, Category="Loading") TArray<FSoftObjectPath> Assets;
};

/** Preparation budgets are independent of the user's persistent quality settings. */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Resource preparation"))
class MESSCONTROL_API UMCPreloadSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    virtual FName GetCategoryName() const override { return TEXT("Game"); }
    /** Shared with blocking-travel Slate. Import as a resident UI texture; text stays in widgets. */
    UPROPERTY(EditAnywhere, Config, Category="Loading screen")
    TSoftObjectPtr<UTexture2D> LoadingScreenArt = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Gameplay/UI/Loading/T_LoadingScreen_WashNSmash.T_LoadingScreen_WashNSmash")));
    UPROPERTY(EditAnywhere, Config, Category="Assets") TArray<FSoftObjectPath> CommonAssets;
    UPROPERTY(EditAnywhere, Config, Category="Assets") TArray<FMCMapPreloadAssets> Maps;
    UPROPERTY(EditAnywhere, Config, Category="Budgets", meta=(ClampMin="15", ClampMax="600", Units="s")) float TimeoutSeconds=120.f;
    UPROPERTY(EditAnywhere, Config, Category="Budgets", meta=(ClampMin="256", ClampMax="4096")) int32 TargetTextureSize=1024;
    UPROPERTY(EditAnywhere, Config, Category="Budgets", meta=(ClampMin="16", ClampMax="1024")) int32 TextureBudgetMB=256;
    UPROPERTY(EditAnywhere, Config, Category="Budgets", meta=(ClampMin="16", ClampMax="512")) int32 MeshBudgetMB=64;
};
