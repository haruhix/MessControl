#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MCFoodEntrySettings.h"
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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="20", ClampMax="100", Units="cm")) float BodyRadius=42;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", ClampMax="40", Units="cm")) float HopHeight=14;
    void Sanitize();
};

/** Prototype counts are bounded independently from the number of surviving nuts. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCNutRainSettings
{
    GENERATED_BODY()
    FMCNutRainSettings();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rain", meta=(ClampMin="1", ClampMax="120")) int32 NutCount=30;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rain", meta=(ClampMin="0", ClampMax="30")) int32 NutsPerExtraPlayer=8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rain", meta=(ClampMin="3", ClampMax="60", Units="s")) float RainSeconds=12;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rain", meta=(ClampMin="0.5", ClampMax="6", Units="s", ToolTip="After the last launch, wait for its flight before awakening the newest surviving nuts.")) float SettleSeconds=2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rain") FMCFoodEntrySettings Entry;
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
