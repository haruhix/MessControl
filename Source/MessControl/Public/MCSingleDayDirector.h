#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/DataAsset.h"
#include "MCSmallMintCandy.h"
#include "MCSingleDayDirector.generated.h"

class UMCDayPlan;
class UMCNutRainProfile;
class UMCGameDirectorProfile;
class AMCNutRainEvent;
class AMCIceEvent;
class AMCFogBrawlEvent;
class AMCCoffeeFlood;
class AMCGameDirector;
class AMCBossCharacter;
class AMCDayDirector;
class AMCCataclysmEvent;
class AMCRewardChest;
class AMCRewardDropZone;
struct FMCFoodPipelineLoad;

UENUM(BlueprintType)
enum class EMCSingleDayStage : uint8 { Training, FirstPerk, Nuts, Director, Boss, Complete, OpeningPause, FirstMeal, MealRest, Ice, FogBrawl, MealCoffeeWave, BetweenMealsPause, PostNutMeal, PostNutMint, Jellies, PostNutCoffee, PostNutCoffeePause };

UENUM(BlueprintType)
enum class EMCSingleDayKeyEventKind : uint8 { NutEncounter, IceEvent, FogBrawl, Jellies };

/** A saved position in the main sequence. Only authored event kinds can run. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCSingleDayKeyEvent
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sequence") FName EventId=TEXT("NutEncounter");
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sequence") EMCSingleDayKeyEventKind Kind=EMCSingleDayKeyEventKind::NutEncounter;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sequence") TArray<TSoftObjectPtr<UMCNutRainProfile>> NutRainVariants;
    UPROPERTY(BlueprintReadWrite,Category="Legacy") int32 CompletionExperience=0; // Serialized legacy compatibility; XP is disabled.
    /** Retained for compatibility with older authored profiles. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sequence",meta=(ClampMin="5",ClampMax="600",Units="s")) float DirectorSupportSeconds=120;
};

/** Extensible main sequence. The complete run target is an orientation, not a victory timer. */
UCLASS(BlueprintType)
class MESSCONTROL_API UMCSingleDayProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UMCSingleDayProfile();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sequence") TArray<FMCSingleDayKeyEvent> KeyEvents;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sequence",meta=(ClampMin="1",ClampMax="120",Units="min")) float RunTargetMinMinutes=25;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sequence",meta=(ClampMin="1",ClampMax="120",Units="min")) float RunTargetMaxMinutes=35;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening",meta=(ClampMin="0",ClampMax="30",Units="s")) float AfterTrainingPauseSeconds=5;
    /** The first entry sets the size of the first portion in each food pattern. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening",meta=(ClampMin="1",ClampMax="30")) TArray<int32> OpeningMealPortions={5};
    /** Retained for compatibility with older authored profiles. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening",meta=(ClampMin="0",ClampMax="30",Units="s")) float BetweenMealPauseMinSeconds=5.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening",meta=(ClampMin="0",ClampMax="30",Units="s")) float BetweenMealPauseMaxSeconds=10.f;
    /** Retained serialized prototype settings; the portion sequence does not read these three fields. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Legacy") bool bOpeningCoffeeWave=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Legacy",meta=(ClampMin="1",ClampMax="6",Units="s")) float OpeningCoffeeWarningSeconds=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Legacy",meta=(ClampMin="1",ClampMax="30")) int32 OpeningMealItems=12;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening",meta=(ClampMin=".5",ClampMax="5",Units="s")) float OpeningMealDropSeconds=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening",meta=(ClampMin="0",ClampMax="30",Units="s")) float BeforeNutsPauseSeconds=4;
    /** The first entry sets the size of the second portion, after coffee cleanup. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="After Nuts",meta=(ClampMin="1",ClampMax="30")) TArray<int32> AfterNutMealPortions={10};
    /** Retained for compatibility with older authored profiles. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="After Nuts",meta=(ClampMin="0",ClampMax="15",Units="s")) float AfterNutCoffeeToFoodSeconds=6.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="After Nuts",meta=(ClampMin=".5",ClampMax="5",Units="s")) float AfterNutMealDropSeconds=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="After Nuts") FMCSmallMintCandySettings SmallMint;
    /** Explicit compatibility for the old short prototype, never enabled by new fragment authoring. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Legacy") bool bLegacyTimedFinale=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequence") TArray<TSoftObjectPtr<UMCNutRainProfile>> NutRainVariants;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Legacy", meta=(ClampMin="5", ClampMax="300", Units="s")) float DirectorSeconds=30;
    UPROPERTY(BlueprintReadWrite,Category="Legacy") int32 NutEventExperience=0; // Serialized legacy compatibility; XP is disabled.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Legacy") TSoftClassPtr<AMCBossCharacter> FinalBossClass;
};

/** Each food portion, coffee cleanup and main event ends with a shared reward. */
UCLASS()
class MESSCONTROL_API AMCSingleDayDirector : public AActor
{
    GENERATED_BODY()
public:
    AMCSingleDayDirector();
    void Initialize(UMCDayPlan* Plan, UMCSingleDayProfile* Profile, UMCGameDirectorProfile* InterludeProfile);
    const UMCSingleDayProfile* GetRunProfile() const { return Settings; }
    const UMCGameDirectorProfile* GetSupportProfile() const { return DirectorProfile; }
    void BeginFirstPerk();
    int32 ChooseNutSeriesSize(int32 Minimum,int32 Maximum,int32 CompletedSeries) const;
    bool ShouldFinishNutRain(int32 CompletedSeries,int32 MinimumSeries,float RainAge,float TargetSeconds) const;
    bool IsLegacyTimedFinale() const { return Settings && Settings->bLegacyTimedFinale; }
    void Stop();
    /** Called by this director's authoritative shared reward only. Destroyed unclaimed chests retry. */
    void NotifyStageRewardFinished(AMCRewardChest* Chest,bool bRetry);
    bool IsKeyEventRewardPending() const;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Sequence") EMCSingleDayStage Stage=EMCSingleDayStage::Training;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Sequence") TObjectPtr<AMCNutRainEvent> NutEvent;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Sequence") TObjectPtr<AMCIceEvent> IceEvent;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Sequence") TObjectPtr<AMCFogBrawlEvent> FogEvent;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Sequence") TObjectPtr<AMCCataclysmEvent> JellyEvent;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Sequence") TObjectPtr<AMCSmallMintCandy> SmallMint;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Sequence") TObjectPtr<AMCBossCharacter> FinalBoss;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Sequence") int32 VariantIndex=0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Sequence") int32 KeyEventIndex=0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Sequence") bool bAuthoredFragmentComplete=false;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Sequence") double StageEndsAt=0;
    /** One-based current portion, zero before the opening Relax starts. */
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Opening") int32 MealPortionNumber=0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Opening") int32 MealPortionCount=0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Opening") int32 MealPortionTarget=0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Opening") int32 MealPortionSpawned=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Rewards") bool bStageRewardPending=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Rewards") TObjectPtr<AMCRewardChest> StageRewardChest;
    /** Reuse the authored booster chest, including its presentation components. */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Rewards") TSubclassOf<AMCRewardChest> StageRewardChestClass;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Rewards") FName StageRewardCompletionId;
private:
#if WITH_EDITOR
    friend class UMCGameplayUIEditorLibrary;
#endif
    friend class FMCStageRewardPortionGateTest;
    friend class FMCObjectiveCoinPortionAndCoffeeTest;
    bool BeginStageReward(FName CompletionId);
    void TickStageReward();
    void TrySpawnStageReward();
    void ResetStageRewards();
    void BeginOpeningPause();
    void BeginFirstMeal();
    void BeginMealPortion(int32 PortionIndex);
    void TickFirstMeal();
    void BeginMealCoffeeWave();
    void TickMealCoffeeWave();
    void TickCoffeeCleanup();
    void ReportMealGate(const FMCFoodPipelineLoad& Food,int32 Dirt,int32 DirtyTeeth,int32 Pending,double Now);
    FName ChooseOpeningFood();
    void BeginKeyEvent();
    void BeginNuts();
    void BeginIce();
    void BeginFogBrawl();
    void BeginJellies();
    void TickJellies();
    void FinishKeyEvent();
    void ClearKeyEvent(bool bCompleted);
    void CompleteRun();
    void BeginBoss();
    void Fail(const FString& Reason);
    void Publish(const FString& Title, const FString& Instruction, int32 Left=0, int32 Total=0);
    void Record(const FString& Decision);
    UPROPERTY() TObjectPtr<UMCDayPlan> Mechanics;
    UPROPERTY() TObjectPtr<UMCSingleDayProfile> Settings;
    UPROPERTY() TObjectPtr<UMCGameDirectorProfile> DirectorProfile;
    UPROPERTY() TObjectPtr<AMCGameDirector> Interlude;
    UPROPERTY() TObjectPtr<AMCDayDirector> MealServices;
    UPROPERTY() TObjectPtr<AMCCoffeeFlood> OpeningCoffee;
    double InterludeEndsAt=0;
    double RunStartedAt=0;
    double NextMealDropAt=0;
    FString LastMealGateState;
    double NextMealDiagnosticAt=0;
    int32 FirstMealBatch=0,MealAttempts=0;
    FRandomStream MealRandom;
    UPROPERTY() TObjectPtr<AMCRewardDropZone> StageRewardZone;
    TSet<FName> CompletedStageRewards;
    double NextStageRewardAttemptAt=0;
    int32 StageRewardPlacementAttempts=0;
    bool bStopped=false;
};
