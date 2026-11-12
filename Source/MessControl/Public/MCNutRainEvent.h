#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCDayPlan.h"
#include "MCNutRainProfile.h"
#include "MCNutRainPattern.h"
#include "MCNutRainEvent.generated.h"

class AMCFoodActor;
class AMCNutEnemy;
class AMCTongue;
class AMCNutLandingShadow;
class UMCVFXPresentationComponent;
class AMCNutBoss;
class AMCToothCharacter;

UENUM(BlueprintType)
enum class EMCNutRainStage : uint8 { Idle, Rainfall, Settling, Enemies, Complete };

/** Rain leads to one knight, then its wizard phase; ground debris never gates the fight. */
UCLASS(Blueprintable)
class MESSCONTROL_API AMCNutRainEvent : public AActor
{
    GENERATED_BODY()
public:
    AMCNutRainEvent();
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    void Start(UMCDayPlan* Plan,UMCNutRainProfile* Profile=nullptr);
    bool IsComplete() const { return Stage==EMCNutRainStage::Complete && !bFailed; }
    /** Explicit cancellation/reset cleans this event's nuts and all their fragments. */
    void Stop();
    void TrackEncounterActor(AActor* Actor);
    void RegisterEncounterEnemy(AMCNutEnemy* Enemy);
    /** Wizard rain uses the same real food, warning and cleanup ownership as the opening storm.
     * An indexed drop replays the opening A/B zones; unindexed callers retain free placement. */
    bool SpawnSpellNut(FVector Candidate,int32 Seed,int32 PatternIndex=INDEX_NONE,UMCVFXPresentationComponent* PreviewSource=nullptr);
    /** Available whole ground nuts only. The caller disposes one after its summon was successfully created. */
    void GetAvailableRainNuts(TArray<AMCFoodActor*>& Out) const;
    /** Capture the current real nuts and fix their left/right order for this cast. */
    int32 BeginWizardGravityWaves(FVector Origin,FVector Right,UMCVFXPresentationComponent* PreviewSource=nullptr);
    int32 StartWizardGravityWave(int32 WaveIndex,float LiftSeconds,float HoldSeconds,float Height);
    int32 DropWizardGravityWave(int32 WaveIndex);
    bool HasWizardGravityDropsInFlight() const;
    void CancelWizardGravityWaves();
    /** Lift the event's existing whole nuts into the sphere above the requested floor point. */
    int32 BeginWizardNutSphere(FVector Center,float GatherSeconds,float Height);
    bool FireWizardNutSphereShot(FVector Target,FVector& OutOrigin,FVector& OutTarget);
    void CancelWizardNutSphere();
    bool HasWizardNutSphereAmmo() const;
    bool HasWizardNutSphereShotsInFlight() const;
    FVector GetWizardNutSphereLocation() const { return WizardNutSphereLocation; }
    /** Overlapping walnuts can advertise separate contacts but hit a worker once per hand. */
    bool ClaimWizardGravityWaveHit(const AMCNutLandingShadow* Warning,AMCToothCharacter* Hero);
    /** Restricted disposable Composer fixture; does not start a shift or touch a live arena. */
    bool PrepareIsolatedSpellContext(AMCTongue* Surface,UMCNutRainProfile* Profile);
    /** One real opening-rain drop, restricted to the disposable editor Game world. */
    bool SpawnIsolatedEntryNut(FVector Candidate,int32 Seed);
    bool LaunchIsolatedEntryNut();
    UPROPERTY(Replicated, BlueprintReadOnly) EMCNutRainStage Stage=EMCNutRainStage::Idle;
    UPROPERTY(Replicated, BlueprintReadOnly) double StageStartedAt=0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 NutsSpawned=0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 EnemiesLeft=0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 RainTargetCount=0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 EnemyTargetCount=0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 SeriesIndex=0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 CompletedSeries=0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 SeriesDropsLeft=0;
    /** 0 = dark/reference A starts, 1 = magenta/reference B starts. */
    UPROPERTY(Replicated, BlueprintReadOnly) int32 FirstPattern=0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 BossesLeft=0;
    UPROPERTY(Replicated, BlueprintReadOnly) double NextDropAt=0;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bFailed=false;
    UPROPERTY(Replicated, BlueprintReadOnly) FMCNutRainSettings Settings;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly) TArray<TObjectPtr<AMCFoodActor>> Nuts;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly) TArray<TObjectPtr<AMCNutEnemy>> Enemies;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly) TArray<TObjectPtr<AMCNutBoss>> Bosses;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly) TArray<TObjectPtr<AMCNutLandingShadow>> LandingShadows;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly) TArray<float> PlannedLaunchTimes;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly) TArray<float> ActualLaunchTimes;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly) TArray<int32> PlannedPatternIndices;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly) TArray<int32> PlannedZoneIndices;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly) TArray<FVector2D> PlannedZoneCoordinates;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly) TArray<FVector> PlannedLandings;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly) TArray<FVector> LaunchLocations;
private:
    void Enter(EMCNutRainStage Next);
    void ConfigureNutRow();
    bool SpawnNut(TOptional<FVector> Candidate=TOptional<FVector>(),TOptional<FVector2D> PatternZone=TOptional<FVector2D>(),
        FName OriginPresentationSource=TEXT("Event.NutRain.Entry"),UMCVFXPresentationComponent* PreviewSource=nullptr);
    void AwakenLastNuts();
    void BeginSeries();
    void UpdatePatternProgress();
    void SpawnBossEncounter();
    void Record(const FString& Decision);
    bool ChooseLanding(float Margin,float NutBodyRadius,FHitResult& Floor);
    bool ResolvePatternLanding(FVector2D Zone,float Margin,FHitResult& Floor) const;
    bool AreRainDropsResolved() const;
    bool FindWizardGravityFloor(const AMCFoodActor* Food,FHitResult& Floor,bool bRequireGrounded) const;
    struct FWizardGravityNut {
        TWeakObjectPtr<AMCFoodActor> Food;
        TWeakObjectPtr<AMCNutLandingShadow> Warning;
        int32 WaveIndex=INDEX_NONE;
        bool bLiftAttempted=false,bDropAttempted=false;
    };
    TArray<FWizardGravityNut> WizardGravityNuts;
    TSet<TWeakObjectPtr<AMCToothCharacter>> WizardGravityWaveHits[2];
    TWeakObjectPtr<UMCVFXPresentationComponent> WizardGravityPreview;
    struct FWizardNutSphereNut {
        TWeakObjectPtr<AMCFoodActor> Food;
        FVector Origin=FVector::ZeroVector,LiftPoint=FVector::ZeroVector,GatherEnd=FVector::ZeroVector;
        FVector ShotTarget=FVector::ZeroVector;
        double ShotStartedAt=0.;
        float ShotSeconds=0.f;
    };
    void TickWizardNutSphere(double Now);
    void ReleaseWizardNutSphereNut(const FWizardNutSphereNut& Nut,bool bRestoreOrigin);
    TArray<FWizardNutSphereNut> WizardNutSphereNuts;
    FVector WizardNutSphereLocation=FVector::ZeroVector;
    double WizardNutSphereGatherAt=0.,WizardNutSphereReadyAt=0.;
    bool bWizardNutSphereTickOnly=false;
    UPROPERTY() TObjectPtr<UMCNutRainProfile> ActiveProfile;
    UPROPERTY() TObjectPtr<AMCTongue> Tongue;
    UPROPERTY() FMCFoodRow NutRow;
    FName NutRowName=TEXT("Walnut");
    FRandomStream Random;
    FVector ClusterCenter=FVector::ZeroVector;
    int32 ClusterDropsLeft=0;
    int32 DropsAttempted=0,FoodBatch=0;
    int32 PartyPlayers=1;
    bool bDeferIsolatedEntryPresentation=false;
    bool bHasIsolatedEntryLaunch=false;
    FVector IsolatedEntryVelocity=FVector::ZeroVector,IsolatedEntryLanding=FVector::ZeroVector;
    double RainStartedAt=0;
    double LastPatternLaunchAt=0;
    TArray<FMCNutRainPatternStep> PatternSteps;
    UPROPERTY() TArray<TObjectPtr<AActor>> EncounterActors;
};
