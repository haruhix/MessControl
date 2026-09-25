#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "MCDayPlan.generated.h"
class UMCCoffeeProfile;

UENUM(BlueprintType)
enum class EMCDayStep : uint8 { BrushLesson, DiscardBrushes, BreakfastRain, BreakfastCleanup, CoffeeWaves, CoffeeCleanup, StuckFood, Complete };

USTRUCT(BlueprintType)
struct FMCFoodRow : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Label;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<TSoftObjectPtr<UStaticMesh>> WholeMeshes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<TSoftObjectPtr<UStaticMesh>> FragmentMeshes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float SelectionWeight=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Health=75;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Mass=9;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float SpoilSeconds=35;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Fragments=3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector HalfExtent=FVector(45,35,35);
    void Sanitize();
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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ulcers") float UlcerHealSeconds=15;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ulcers") float UlcerDamagePerSecond=.35f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ulcers") float UlcerDisturbDamage=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arena") FVector ArenaHalfSize=FVector(1050,740,220);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arena") FVector ArenaCenter=FVector::ZeroVector;
    void Sanitize();
};
