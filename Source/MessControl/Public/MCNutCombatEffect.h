#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCNutBossTypes.h"
#include "MCVFXNativePresentationSettings.h"
#include "MCNutVFXCatalog.h"
#include "MCVFXNativeInputs.h"
#include "MCNutCombatEffect.generated.h"

class AMCTongue;
class AMCNutBoss;
class UProceduralMeshComponent;
class UInstancedStaticMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMCVFXPresentationComponent;

/** One already accepted native cosmetic burst. Fixed frame, no new gameplay decision. */
struct FMCNutAcceptedBurst
{
    FName Event;
    FTransform Frame=FTransform::Identity;
    float StartedAtCueAge=0;
    bool bPresentationAccepted=false;
    bool bRejected=false;
};

UENUM(BlueprintType)
enum class EMCNutCombatCue : uint8
{
    ChargeTell, JumpTell, SlamImpact, FireCast, FireImpact, NutRain, SummonTell, EntranceImpact, RollTell,
    CastCharge, CastRelease, RitualCast, ShieldHit, Transform, DeathBurst,
    MeleeTell, MeleeSlash, TeleportTell, TeleportBurst, KnightHeadSplit
};

/** One server-clock cue. Rendering peers reconstruct bounded cosmetic geometry from these values. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCNutCombatCueState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) EMCNutBossRole Role=EMCNutBossRole::Tank;
    UPROPERTY(BlueprintReadOnly) EMCNutCombatCue Type=EMCNutCombatCue::ChargeTell;
    UPROPERTY(BlueprintReadOnly) FVector Origin=FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FVector Target=FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) double StartedAt=-1;
    UPROPERTY(BlueprintReadOnly) float WindupSeconds=1;
    UPROPERTY(BlueprintReadOnly) float ActiveSeconds=0;
    UPROPERTY(BlueprintReadOnly) float Radius=100;
    UPROPERTY(BlueprintReadOnly) float DetailRadius=90;
    UPROPERTY(BlueprintReadOnly) int32 Seed=0;
    UPROPERTY(BlueprintReadOnly) int32 DropCount=8;
    UPROPERTY(BlueprintReadOnly) float DropCadence=.5f;
    /** Replaces only accents. Native ground warning geometry is never hidden by a profile. */
    UPROPERTY(BlueprintReadOnly) bool bSuppressCosmeticAccents=false;
    UPROPERTY(BlueprintReadOnly) FGuid PresentationEpisodeID;
    UPROPERTY(BlueprintReadOnly) FName NativeEvent;
    /** Explicit originating mechanic, independent of reused ShieldHit/Impact render types. */
    UPROPERTY(BlueprintReadOnly) FName PresentationSource;
    UPROPERTY(BlueprintReadOnly) FName PresentationEvent;
    UPROPERTY(BlueprintReadOnly) FName AfterWindupEvent;
    UPROPERTY() FMCVFXNativePresentationRef NativeStyle;
    double EndsAt() const { return StartedAt+WindupSeconds+ActiveSeconds; }
};

/** Presentation only: this actor has no collision, damage or objective contribution. */
UCLASS()
class MESSCONTROL_API AMCNutCombatEffect : public AActor
{
    GENERATED_BODY()
public:
    AMCNutCombatEffect();
    static AMCNutCombatEffect* Spawn(AActor* SourceActor,AMCTongue* Tongue,EMCNutCombatCue Type,
        FVector Origin,FVector Target,float Radius,float WindupSeconds,float ActiveSeconds,int32 Seed=0,
        float DetailRadius=90,int32 DropCount=8,float DropCadence=.5f,
        bool bSuppressCosmeticAccents=false,FGuid PresentationEpisodeID=FGuid(),
        const FMCVFXPreparedNativePresentation* NativePresentation=nullptr,FName NativeEvent=NAME_None,
        const FMCNutCuePresentation& Presentation=FMCNutCuePresentation());
    static AMCNutCombatEffect* SpawnPresented(const FMCNutCuePresentation& Context,AActor* SourceActor,AMCTongue* Tongue,EMCNutCombatCue Type,
        FVector Origin,FVector Target,float Radius,float WindupSeconds,float ActiveSeconds,int32 Seed=0,
        float DetailRadius=90,int32 DropCount=8,float DropCadence=.5f)
    {return Spawn(SourceActor,Tongue,Type,Origin,Target,Radius,WindupSeconds,ActiveSeconds,Seed,DetailRadius,DropCount,DropCadence,false,FGuid(),nullptr,NAME_None,Context);}
    static FVector GetNutRainDropPoint(FVector Center,float Radius,int32 Seed,int32 Index);
    /** Peer-local surface cracks follow the shield bone, without modifying artist materials. */
    static void UpdateShieldDamageVisual(AMCNutBoss* Boss);
    static FVector GetShieldContactOrigin(const AMCNutBoss* Boss);
    /** Evaluated authored shield normal, facing away from the boss body. X is the contact normal. */
    static FTransform GetShieldContactFrame(const AMCNutBoss* Boss);
    static constexpr int32 MaxRainDrops=24;
    static constexpr float DropFlightSeconds=.8f;
    UFUNCTION(BlueprintCallable,Category="Nut Combat") void Cancel();
    /** Editor-only asset creation; existing Niagara artist edits are preserved. */
    UFUNCTION(BlueprintCallable,Category="MessControl|Editor") static bool AuthorNiagaraAssets();
    /** Cosmetic preview for transient editor actors; creates no gameplay actors or damage. */
    UFUNCTION(BlueprintCallable,Category="MessControl|Editor") void PreviewAtAge(float Age);
    /** Contextual preview observes real accepted burst sites, not synthetic timers. */
    void GetAcceptedBurstEvents(TArray<FName>& Out) const;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(Replicated,BlueprintReadOnly) FMCNutCombatCueState Cue;
    UPROPERTY(Replicated,BlueprintReadOnly) TObjectPtr<AMCTongue> Tongue;
    UPROPERTY(Replicated,BlueprintReadOnly) TObjectPtr<AActor> SourceActor;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Warning;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> DropWarnings;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> DropShadows;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Flames;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> RainNuts;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> Debris;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Magic;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Storm;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UNiagaraComponent> SustainedParticles;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Presentation") TObjectPtr<UMCVFXPresentationComponent> Presentation;
    /** Reflected defaults keep these owned assets reachable by cooking. */
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UMaterialInterface> TelegraphMaterial;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UMaterialInterface> ShadowMaterial;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UMaterialInterface> FlameMaterial;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UMaterialInterface> MagicMaterial;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UMaterialInterface> ShieldFractureMaterial;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UMaterialInterface> StormMaterial;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UNiagaraSystem> DustImpactSystem;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UNiagaraSystem> FireImpactSystem;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UNiagaraSystem> ArcaneBurstSystem;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UNiagaraSystem> CastChargeSystem;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UNiagaraSystem> MotionDustSystem;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UNiagaraSystem> StormMotesSystem;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|VFX") TSoftObjectPtr<UNiagaraSystem> ShieldHitSystem;
private:
    const FMCVFXNativePartSettings* NativePart(FName ID) const {return NativeArt.FindPart(Cue.NativeEvent,ID);}
    UPROPERTY(Transient) FMCVFXPreparedNativePresentation NativeArt;
    double Now() const;
    void InitializeMaterials();
    void Present(float Age);
    /** Native replicated cue clock/anchors drive this local presentation; particles decide no gameplay. */
    bool UpdateComposedPresentation(float Age);
    void SuppressNativeDecorations();
    void PresentTankImpact(float Age);
    void PresentKnightHeadSplit(float Age);
    void Burst(FVector Point,bool bFire,float Scale=1);
    void PlayBurst(UNiagaraSystem* System,FVector Point,FVector Direction,float Scale=1,bool bKeepWithComposition=false);
    void PresentBurstEvent(FName Event,UNiagaraSystem* System,FVector Point,FVector Direction,float Scale,float StartedAtCueAge=0);
    void UpdateAcceptedBursts(float Age);
    void PresentMagic(float Age,FVector Floor,FVector Normal,FVector Right,FVector Side,float Fade);
    UFUNCTION() void FinishedBurst(UNiagaraComponent* Component);
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> WarningMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SmallWarningMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ShadowMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FireMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MagicMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> StormMID;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> Bursts;
    uint32 ImpactMask=0;
    int32 BuiltDropCount=0;
    bool bBurstPresented=false;
    bool bSustainedStarted=false;
    bool bReleasePresented=false;
    bool bComposedDecorations=false;
    FString LastNativeInputError;
    FMCVFXNativeInputs LastCueNativeInputs;
    TArray<FMCNutAcceptedBurst> AcceptedBursts;
    float LastCueAge=0;
};
