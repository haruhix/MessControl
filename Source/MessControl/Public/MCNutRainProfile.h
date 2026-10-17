#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MCFoodEntrySettings.h"
#include "MCNutBossTypes.h"
#include "MCNutRainProfile.generated.h"

class UDataTable;

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCNutEnemySettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1", ClampMax="1000")) float MaxHealth=90;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="40", ClampMax="500", Units="cm/s")) float MoveSpeed=165;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="50", ClampMax="250", Units="cm")) float AttackRange=125;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", ClampMax="100")) float AttackDamage=12;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.25", ClampMax="3", Units="s")) float WindupSeconds=.55f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.5", ClampMax="8", Units="s")) float AttackCooldown=1.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="20", ClampMax="260", Units="cm")) float BodyRadius=58;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", ClampMax="40", Units="cm")) float HopHeight=14;
    void Sanitize();
};

/** Two complementary authored patterns followed by the knight and its wizard form. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCNutRainSettings
{
    GENERATED_BODY()
    FMCNutRainSettings();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Encounter") bool bBossEncounter=true;
    /** Width 0..1 is left to right; depth 0..1 is throat to front, inside the safe tongue footprint.
     * Every entry is visited once in array order. Only the complete A/B order is randomized. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Patterns", meta=(TitleProperty="X")) TArray<FVector2D> PatternA;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Patterns", meta=(TitleProperty="X")) TArray<FVector2D> PatternB;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Patterns", meta=(ClampMin="0.1", ClampMax="3", Units="s")) float PatternDropSeconds=.5f;
    /** Time from the last launch of the first group to the first launch of the second group. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Patterns", meta=(ClampMin="0.1", ClampMax="8", Units="s")) float PatternSeriesGapSeconds=.9f;
    // Legacy fields remain readable for old non-boss fixtures; boss opening uses Patterns.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Series", meta=(ClampMin="4", ClampMax="8")) int32 MinimumSeries=4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Series", meta=(ClampMin="4", ClampMax="8")) int32 MaximumSeries=8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Series", meta=(ClampMin="4", ClampMax="12")) int32 MinimumNutsPerSeries=6;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Series", meta=(ClampMin="4", ClampMax="12")) int32 MaximumNutsPerSeries=10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rain", meta=(ClampMin="1.5", ClampMax="3", Units="s")) float MinimumDropSeconds=1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rain", meta=(ClampMin="1.5", ClampMax="3", Units="s")) float MaximumDropSeconds=3.f;
    /** Kept for older packages; boss opening uses PatternSeriesGapSeconds. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Series", meta=(ClampMin="0", ClampMax="8", Units="s")) float SeriesRestSeconds=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance", meta=(ClampMin="150", ClampMax="220", Units="cm")) float LargeNutHeight=180;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rain", meta=(ClampMin="0", ClampMax="40")) float ImpactDamageLimit=18;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rain", meta=(ClampMin="80", ClampMax="480", Units="cm")) float ImpactRadius=300;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rain", meta=(ClampMin="0", ClampMax="500", Units="cm/s")) float ImpactPushSpeed=260;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Encounter") FMCNutBossSettings Boss;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rain", meta=(ClampMin="1", ClampMax="120")) int32 NutCount=30;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rain", meta=(ClampMin="0", ClampMax="30")) int32 NutsPerExtraPlayer=8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rain", meta=(ClampMin="3", ClampMax="60", Units="s", ToolTip="Legacy rain duration. Boss opening derives duration from its two patterns, launch spacing and final flight.")) float RainSeconds=22.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rain", meta=(ClampMin="0.5", ClampMax="6", Units="s", ToolTip="After the last launch, wait for its flight before awakening the newest surviving nuts.")) float SettleSeconds=2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rain") FMCFoodEntrySettings Entry;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rain", meta=(ClampMin="60", ClampMax="650", Units="cm")) float ClusterRadius=260;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rain", meta=(ClampMin="0.03", ClampMax="0.35")) float LandingShadowOpacity=.20f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemies", meta=(ClampMin="0", ClampMax="16")) int32 EnemyCount=4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemies", meta=(ClampMin="0", ClampMax="4")) int32 EnemiesPerExtraPlayer=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemies") FMCNutEnemySettings Enemy;
    void Sanitize();
    int32 RainCountForPlayers(int32 Players) const;
    int32 EnemyCountForPlayers(int32 Players) const;
};

UCLASS(BlueprintType)
class MESSCONTROL_API UMCNutRainProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UMCNutRainProfile();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nut Cataclysm") FMCNutRainSettings Settings;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nut Cataclysm") TSoftObjectPtr<UDataTable> Menu;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nut Cataclysm") FName NutRow=TEXT("Walnut");
};
