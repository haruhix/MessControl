#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "MCFoodStackSettings.h"
#include "MCDayPlan.generated.h"
class UMCCoffeeProfile;
class UMCColdColaProfile;

UENUM(BlueprintType)
enum class EMCDayStep : uint8 { BrushLesson, DiscardBrushes, BreakfastRain, BreakfastCleanup, CoffeeWaves, CoffeeCleanup, StuckFood, Complete, ColdCola };

UENUM(BlueprintType)
enum class EMCFoodResistance : uint8 { Automatic, Soft, Hard };

UENUM(BlueprintType)
enum class EMCFoodKind : uint8 { Food, ForeignObject, Spicy };

USTRUCT(BlueprintType)
struct FMCFoodRow : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Label;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<TSoftObjectPtr<UStaticMesh>> WholeMeshes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<TSoftObjectPtr<UStaticMesh>> FragmentMeshes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.01",UIMin="0.1",UIMax="3",ToolTip="Per-axis scale of whole food mesh and collision. Fragments have their own independent FragmentScale.")) FVector Scale=FVector::OneVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance", meta=(ClampMin="0.01",ToolTip="Independent per-axis scale of fragment mesh and collision. Does not multiply the whole food Scale. Mass is configured separately.")) FVector FragmentScale=FVector(.5);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float SelectionWeight=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Health=75;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EMCFoodResistance Resistance=EMCFoodResistance::Automatic;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hazard") EMCFoodKind Kind=EMCFoodKind::Food;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hazard",meta=(ClampMin="6",ClampMax="8")) float FuseSeconds=8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hazard",meta=(ClampMin="50",ClampMax="600")) float FirstPulseRadius=180;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hazard",meta=(ClampMin="0",ClampMax="200")) float RadiusPerRound=90;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hazard",meta=(ClampMin="0",ClampMax="100")) float PulseDamage=18;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Mass=9;
    UPROPERTY(EditAnywhere, BlueprintReadWrite,Category="Ulcer",meta=(ToolTip="Freshness in seconds from spawn. Spoiled food never creates ulcers.")) float SpoilSeconds=180;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ulcer", meta=(ClampMin="0.5",ClampMax="10")) float AbsorbSeconds=2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Fragments=3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector HalfExtent=FVector(45,35,35);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stack") FMCFoodStackSettings Stack;
    void Sanitize();
#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

USTRUCT(BlueprintType)
struct FMCDayStepSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EMCDayStep Step=EMCDayStep::BrushLesson;
    // Zero means no event deadline, including the brush lesson.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Seconds=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Title;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Instruction;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float FailureDamage=8;
};

/** One day contains an ordered list of events; their timers never increment Day. */
UCLASS(BlueprintType)
class MESSCONTROL_API UMCDayPlan : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UMCDayPlan();
    // Design reference only, never used to force a transition.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timeline") float TargetDaySeconds=240;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timeline") TArray<FMCDayStepSettings> Steps;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakfast") TSoftObjectPtr<UDataTable> Menu;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakfast") int32 BreakfastCount=6;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stuck food") int32 StuckCount=4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cleaning") int32 SurfacePatches=10;
    // Retained only for old assets. Repetitions now live in Coffee Profile -> Settings -> Cycles.
    UPROPERTY() int32 WaveCount=4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Coffee") float FloodHeight=155;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Coffee") float FlowAcceleration=320;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Coffee") float PaddleAcceleration=400;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Coffee") float AnchorReach=160;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Coffee") TSoftObjectPtr<UMCCoffeeProfile> CoffeeProfile;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cold cola") TSoftObjectPtr<UMCColdColaProfile> ColdColaProfile;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ulcers",meta=(ClampMin="6",ClampMax="8")) float UlcerHealSeconds=7;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ulcers") float UlcerDamagePerSecond=.35f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ulcers") float UlcerDisturbDamage=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ulcers",meta=(ClampMin="1",ClampMax="15")) float UlcerPulseInterval=3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arena") FVector ArenaHalfSize=FVector(1050,740,220);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arena") FVector ArenaCenter=FVector::ZeroVector;
    void Sanitize();
};
