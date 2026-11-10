#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCVFXLabBossStation.generated.h"

class AMCTongue;
class AMCNutBoss;
class AMCNutRainEvent;
class AMCBossCharacter;
class AMCToothCharacter;
class AMCPlaytestBotController;
class UMCNutRainProfile;
class UMCBossProfile;
class UTextRenderComponent;
class AMCNutKamikaze;
class AMCFoodActor;
class USceneComponent;
class UStaticMesh;
class UMCVFXPresentationComponent;

UENUM(BlueprintType)
enum class EMCVFXLabBossKind : uint8
{
    TankMelee, TankCharge, TankJump, TankRoll, TankEntrance, TankShieldHit, TankShieldBreakOverflow, TankDeath,
    MageMelee, MageFireball, MageSummon, MageNutRain, MageTeleport, MageHurt, MageDeath, MageEntrance,
    ZombiePunchLeft, ZombiePunchRight, ZombieKick, ZombieHurt, ZombieDeath, ZombieRoar,
    Phase3PunchRight, Phase3AreaAttack,
    // Append only: saved laboratory actors retain the original enum values.
    TankCounterShove, TankHeadSplit, KamikazeFuse, KamikazeBlast, RainEntry, TankChain
};

/** One isolated, repeatable real boss ability. Counters describe observed callbacks, not visual approval. */
UCLASS(Blueprintable)
class MESSCONTROL_API AMCVFXLabBossStation : public AActor
{
    GENERATED_BODY()
public:
    AMCVFXLabBossStation();
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VFX Lab") TObjectPtr<AMCTongue> Floor;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VFX Lab") EMCVFXLabBossKind Kind=EMCVFXLabBossKind::TankRoll;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VFX Lab") bool bAutoRun=true;
    /** Manual A/B owner keeps the real fixtures between strikes. Ordinary lab cycles are unchanged. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VFX Lab|Comparison") bool bComparisonMode=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VFX Lab|Comparison") bool bLegacyFeedback=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VFX Lab|Comparison") int32 ComparisonSeed=137;
    /** Pause after a complete ability, retaining the impact/ulcer for inspection. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VFX Lab",meta=(ClampMin="0.5",Units="s")) float CycleSeconds=4;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VFX Lab",meta=(ClampMin="0.1",Units="s")) float StartDelaySeconds=.5f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VFX Lab|Assets") TSoftObjectPtr<UMCNutRainProfile> NutProfile;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VFX Lab|Assets") TSoftClassPtr<AMCToothCharacter> SubjectClass;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VFX Lab|Assets") TSoftClassPtr<AMCBossCharacter> ZombieClass;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VFX Lab|Assets") TSoftClassPtr<AMCBossCharacter> Phase3Class;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VFX Lab|Assets") TSoftObjectPtr<UMCBossProfile> ZombieProfile;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VFX Lab|Assets") TSoftObjectPtr<UMCBossProfile> Phase3Profile;
    UFUNCTION(BlueprintCallable,Category="VFX Lab") void Reset();
    UFUNCTION(BlueprintCallable,Category="VFX Lab") void SetRunning(bool bEnabled);
    AMCToothCharacter* GetComparisonSubject() const;
    AActor* GetComparisonEnemy() const;
    UMCVFXPresentationComponent* GetComparisonPresentation() const;
    USceneComponent* GetComparisonVisualSubject() const;
    bool IsComparisonReady() const;
    bool PrepareComparison(EMCVFXLabBossKind Selected);
    /** Isolated editor-world fixture; borrows the existing pawn and its identity without possession or cleanup ownership. */
    bool PrepareComparisonUsingSubject(EMCVFXLabBossKind Selected,AMCToothCharacter* ExistingSubject);
    bool RunComparisonAction();
    float ApplyComparisonOutgoing(float Damage);
    float ApplyComparisonIncoming(float Damage);
    FString GetComparisonStatusText() const;
    void ResumeComparisonBot();
    UPROPERTY(Replicated,VisibleAnywhere,BlueprintReadOnly,Category="VFX Lab|Observed") bool bRunning=false;
    UPROPERTY(Replicated,VisibleAnywhere,BlueprintReadOnly,Category="VFX Lab|Observed") int32 Cycles=0;
    UPROPERTY(Replicated,VisibleAnywhere,BlueprintReadOnly,Category="VFX Lab|Observed") int32 Passed=0;
    UPROPERTY(Replicated,VisibleAnywhere,BlueprintReadOnly,Category="VFX Lab|Observed") int32 Failed=0;
    UPROPERTY(Replicated,VisibleAnywhere,BlueprintReadOnly,Category="VFX Lab|Observed") int32 Telegraphs=0;
    UPROPERTY(Replicated,VisibleAnywhere,BlueprintReadOnly,Category="VFX Lab|Observed") int32 Executions=0;
    UPROPERTY(Replicated,VisibleAnywhere,BlueprintReadOnly,Category="VFX Lab|Observed") int32 Recoveries=0;
    UPROPERTY(Replicated,VisibleAnywhere,BlueprintReadOnly,Category="VFX Lab|Observed") int32 Impacts=0;
    UPROPERTY(ReplicatedUsing=OnRep_Status,VisibleAnywhere,BlueprintReadOnly,Category="VFX Lab|Observed") FString Status=TEXT("Waiting for PIE");
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="VFX Lab") TObjectPtr<UTextRenderComponent> Label;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    bool BeginCycle(AMCToothCharacter* BorrowedSubject=nullptr);
    bool PrepareComparisonInternal(EMCVFXLabBossKind Selected,AMCToothCharacter* BorrowedSubject);
    bool LaunchAction();
    void Observe();
    void FinishCycle(bool bPassed,const FString& Reason);
    void Cleanup();
    void GatherOwnedActors();
    void Track(AActor* Actor);
    bool Owns(const AActor* Actor) const;
    bool IsNutKind() const;
    bool IsMageKind() const;
    bool IsCombatKind() const;
    bool IsEntranceKind() const;
    bool IsDeathKind() const;
    bool IsKamikazeKind() const;
    FString KindName() const;
    UFUNCTION() void OnRep_Status();
    void UpdateLabel();
    UPROPERTY(Transient) TObjectPtr<AMCNutBoss> NutBoss;
    UPROPERTY(Transient) TObjectPtr<AMCBossCharacter> Boss;
    UPROPERTY(Transient) TObjectPtr<AMCNutRainEvent> NutOwner;
    UPROPERTY(Transient) TObjectPtr<AMCNutKamikaze> Kamikaze;
    UPROPERTY(Transient) TObjectPtr<AMCFoodActor> EntryNut;
    UPROPERTY(Transient) TObjectPtr<UStaticMesh> PreparedCreepMesh;
    UPROPERTY(VisibleAnywhere,Category="VFX Lab") TObjectPtr<UMCVFXPresentationComponent> EntryPresentation;
    UPROPERTY(Transient) TObjectPtr<AMCToothCharacter> Subject;
    UPROPERTY(Transient) TObjectPtr<AMCPlaytestBotController> SubjectController;
    bool bBorrowedSubject=false;
    TArray<TWeakObjectPtr<AActor>> OwnedActors;
    TSet<TWeakObjectPtr<AActor>> ObservedActors;
    TSet<uint8> SeenCues;
    FVector BossStart=FVector::ZeroVector;
    double CycleStartedAt=0,ActionStartedAt=0,NextCycleAt=0;
    float SubjectHealthBefore=0,BossHealthBefore=0,ShieldBefore=0,AppliedHealthDamage=0,MaximumTravel=0;
    float ComparisonOutgoingHealth=0,ComparisonOutgoingShield=0,ComparisonIncomingHealth=0;
    int32 LastTelegraphs=0,LastExecutions=0,LastRecoveries=0;
    int32 CycleTelegraphs=0,CycleExecutions=0,CycleRecoveries=0,GeneratedCreeps=0;
    uint8 LastBossState=255;
    bool bCycleOpen=false,bActionStarted=false,bSawEntrance=false,bSawDeath=false,bSawHurt=false;
    bool bSawWind=false,bSawClot=false,bSawClotImpact=false,bSawUlcer=false,bSawFireImpact=false;
    double NextCounterContactAt=0;
    int32 CounterContacts=0;
    bool bSawCounter=false;
};
