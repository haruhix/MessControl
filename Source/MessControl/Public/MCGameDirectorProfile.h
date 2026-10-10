#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MCGameDirectorTypes.h"
#include "MCGameDirectorProfile.generated.h"

/** Runtime candidate rule. Weight zero disables a kind; no daily quota is owed. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCGameDirectorEventRule
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) EMCGameDirectorEvent Kind=EMCGameDirectorEvent::Food;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1000")) float Weight=5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="3600",Units="s")) float CooldownSeconds=8;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",ClampMax="7")) int32 FirstDay=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="3")) float MinimumDifficulty=0;
    /** Optional safety cap, not a target. Zero means unlimited. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1000")) int32 MaxPerDay=0;
    void Sanitize();
};

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCGameDirectorDaySettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="60",ClampMax="1200")) float DaySeconds=360;
    /** Legacy serialized data only. Runtime selection never consumes these counts. */
    UPROPERTY(meta=(DeprecatedProperty,DeprecationMessage="Use runtime event rules; this count is ignored.")) int32 FoodCount=7;
    UPROPERTY(meta=(DeprecatedProperty,DeprecationMessage="Use runtime event rules; this count is ignored.")) int32 CoffeeEvents=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",ClampMax="20")) int32 CoffeePatches=3;
    UPROPERTY(meta=(DeprecatedProperty,DeprecationMessage="Use runtime event rules; this count is ignored.")) int32 CoffeeFloods=0;
    UPROPERTY(meta=(DeprecatedProperty,DeprecationMessage="Use runtime event rules; this count is ignored.")) int32 Colas=1;
    UPROPERTY(meta=(DeprecatedProperty,DeprecationMessage="Use runtime event rules; this count is ignored.")) int32 Yawns=1;
    UPROPERTY(meta=(DeprecatedProperty,DeprecationMessage="Use runtime event rules; this count is ignored.")) int32 Peppers=0;
    UPROPERTY(meta=(DeprecatedProperty,DeprecationMessage="Use runtime event rules; this count is ignored.")) int32 StuckFood=1;
    UPROPERTY(meta=(DeprecatedProperty,DeprecationMessage="Use runtime event rules; this count is ignored.")) int32 RepairEvents=0;
    UPROPERTY(meta=(DeprecatedProperty,DeprecationMessage="Use runtime event rules; this count is ignored.")) int32 Rewards=2;
    UPROPERTY(meta=(DeprecatedProperty,DeprecationMessage="Use runtime event rules; this count is ignored.")) int32 Bosses=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="3")) float TargetPressureMin=.18f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="3")) float TargetPressureMax=.48f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="3")) float MinimumDifficulty=.5f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="3")) float MaximumDifficulty=.85f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="20")) int32 InitialPatches=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",ClampMax="8")) int32 MaxFoodBatch=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",ClampMax="12")) int32 MaxWholeFood=3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="4",ClampMax="80")) int32 MaxFragments=18;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",ClampMax="30")) float FoodInterval=6;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="5",ClampMax="120")) float FoodWorkPerPlayer=30;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.2",ClampMax="3")) float PressureLimit=.9f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",ClampMax="60")) float EventGap=12;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="30")) float RestSeconds=12;
    UPROPERTY(meta=(DeprecatedProperty,DeprecationMessage="Runtime pacing follows observed pressure instead of a fixed build duration.")) float BuildSeconds=35;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="10",ClampMax="180")) float FinalCleanupSeconds=60;
    void Sanitize();
};

/** Designer-owned runtime rules, pacing bounds and estimates. Copied at run start. */
UCLASS(BlueprintType)
class MESSCONTROL_API UMCGameDirectorProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UMCGameDirectorProfile();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Director") TArray<FMCGameDirectorDaySettings> Days;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Director") TArray<FMCGameDirectorEventRule> Events;
    /** Scales every day's difficulty, pressure budget and food queue capacity at run time. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Director",meta=(ClampMin="0.25",ClampMax="10")) float DifficultyMultiplier=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Director",meta=(ClampMin="0.25",ClampMax="30",Units="s")) float DecisionInterval=3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Director",meta=(ClampMin="1",ClampMax="120",Units="s")) float AdaptationSeconds=20;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Director",meta=(ClampMin="1",ClampMax="30")) float InitialCleaningSeconds=8;
    UPROPERTY(meta=(DeprecatedProperty,DeprecationMessage="Runtime capacity uses the actual number of available players.")) float TeamScaling=.35f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Director",meta=(ClampMin="1",ClampMax="5")) float WarningSeconds=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Director",meta=(ClampMin="1",ClampMax="30")) float IntermissionSeconds=7;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Director",meta=(ClampMin="0",ClampMax="10")) float MissedTaskDamage=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Estimates",meta=(ClampMin="1",ClampMax="30")) float CleaningWorkerSeconds=8;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Estimates",meta=(ClampMin="1",ClampMax="60")) float RepairWorkerSeconds=10;
    FMCGameDirectorDaySettings GetScaledDaySettings(int32 DayIndex) const;
    void Sanitize();
};
