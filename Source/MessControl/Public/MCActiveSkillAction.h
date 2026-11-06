#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "GameFramework/Actor.h"
#include "MCPerkVFXProfile.h"
#include "MCPerkVFXCompositionPlayer.h"
#include "MCEventSFX.h"
#include "MCActiveSkillAction.generated.h"

class AMCPlayerState;
class AMCToothCharacter;
class UMCPerkComponent;
class USceneComponent;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class UStaticMesh;
class UNiagaraSystem;
class UNiagaraComponent;
class AMCEventSFXPlayer;
class UMCEventSFXReadiness;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnMCActiveSkillVFXStage,FName);

UENUM(BlueprintType)
enum class EMCActiveSkillEndReason : uint8
{
    Completed, InputCanceled, Replaced, Removed, OwnerDied, PawnChanged, RunReset, WorldEnded, TimedOut
};

/** Immutable authored values; arrays replicate while native TMaps do not. */
USTRUCT()
struct MESSCONTROL_API FMCActiveSkillParameter
{
    GENERATED_BODY()
    UPROPERTY() FName Key;
    UPROPERTY() float Value=0;
};

/** Initial-only authored audio/group binding; contains no local readiness certificate. */
USTRUCT()
struct FMCActiveSkillSFXBinding
{
    GENERATED_BODY()
    UPROPERTY() FName Stage;
    UPROPERTY() FMCEventSFXSlot Slot;
    UPROPERTY() FMCEventPresentationGroupSet Groups;
};

USTRUCT()
struct FMCActiveSkillAudioCue
{
    GENERATED_BODY()
    UPROPERTY() FName Stage;
    UPROPERTY() FTransform Frame=FTransform::Identity;
    UPROPERTY() double EventStartedAt=0;
    UPROPERTY() FGuid GameplayEpoch;
    UPROPERTY() bool bLoop=false;
    UPROPERTY() bool bHasPrepared=false;
    UPROPERTY() FMCPreparedEventSFX Prepared;
};

/** One physical press. Release never re-reads the item currently occupying this slot. */
USTRUCT()
struct MESSCONTROL_API FMCActiveSkillInputHandle
{
    GENERATED_BODY()
    UPROPERTY() int32 SlotIndex=INDEX_NONE;
    UPROPERTY() FGuid SlotInstanceID;
    UPROPERTY() uint32 PressSequence=0;
    bool IsValid() const { return SlotIndex>=0 && SlotIndex<4 && SlotInstanceID.IsValid() && PressSequence!=0; }
    bool operator==(const FMCActiveSkillInputHandle& Other) const
    { return SlotIndex==Other.SlotIndex && SlotInstanceID==Other.SlotInstanceID && PressSequence==Other.PressSequence; }
    bool operator!=(const FMCActiveSkillInputHandle& Other) const { return !(*this==Other); }
};

/** Camera ray sampled when the player commits a shot or releases a throw. */
struct FMCActiveSkillInputView
{
    FVector Origin;
    FVector Direction;
    bool IsValidFor(const AMCToothCharacter& Caster) const;
};

/** Prepared without gameplay side effects; committed actions outlive their depleted slot/effect. */
UCLASS(Abstract, Blueprintable)
class MESSCONTROL_API AMCActiveSkillAction : public AActor
{
    GENERATED_BODY()
public:
    AMCActiveSkillAction();
    virtual bool CanStart() const;
    /** Override must call Super first. Cannot reject after the cost has committed. */
    virtual void StartCommitted();
    virtual void ReleaseInput();
    /** True consumes a fresh repeat press before the new-use cooldown gate. */
    virtual bool RepeatInput();
    /** A launched hazard may retain its fuse on input loss/death. Reset/world teardown must finish. */
    virtual void Cancel(EMCActiveSkillEndReason Reason);
    void Finish(EMCActiveSkillEndReason Reason=EMCActiveSkillEndReason::Completed);
    UFUNCTION(BlueprintPure,Category="Skill") AMCToothCharacter* Hero() const;
    UFUNCTION(BlueprintPure,Category="Skill") AMCPlayerState* SourceState() const;
    UMCPerkComponent* Perks() const;
    UFUNCTION(BlueprintPure,Category="Skill|Tuning") float Param(FName Key,float Fallback) const;
    double Now() const;
    bool GetAim(FVector& Origin,FVector& Direction) const;
    static bool ReadAim(const AMCToothCharacter& Caster,FVector& Origin,FVector& Direction);
    UFUNCTION(BlueprintPure,Category="Skill") FGuid GetActionID() const { return ActionID; }
    UFUNCTION(BlueprintPure,Category="Skill") int32 GetSlotIndex() const { return InputHandle.SlotIndex; }
    UFUNCTION(BlueprintPure,Category="Skill") FGuid GetSlotInstanceID() const { return InputHandle.SlotInstanceID; }
    uint32 GetPressSequence() const { return InputHandle.PressSequence; }
    /** Blueprint/Python cannot expose uint32; this preserves its full positive range for observation. */
    UFUNCTION(BlueprintPure,Category="Skill|Review") int64 GetPressSequenceForReview() const { return int64(InputHandle.PressSequence); }
    UFUNCTION(BlueprintPure,Category="Skill") FName GetPerkID() const { return PerkID; }
    const FMCActiveSkillInputHandle& GetInputHandle() const { return InputHandle; }
    UFUNCTION(BlueprintPure,Category="Skill") bool IsCommitted() const { return bCommitted; }
    UFUNCTION(BlueprintPure,Category="Skill") bool IsFinished() const { return bFinished; }
    UFUNCTION(BlueprintPure,Category="Skill|Presentation") float GetPresentationAge() const;
    /** -1 until the held action fires/releases. Independent of projectile lifetime. */
    UFUNCTION(BlueprintPure,Category="Skill|Presentation") virtual float GetPresentationReleaseAge() const;
    virtual bool GetPresentationHandTarget(bool bLeft,FTransform& WorldPose) const;
    void ClaimPresentation();
    void ReleasePresentation();
    /** Negative Radius retains the artist's asset default. Values are applied before activation. */
    void PlaySkillCue(FName Stage,FVector Location,FVector Direction=FVector::UpVector,float Radius=-1.f);
    void StartLoop();
    void StopLoop();
    void UpdateLoopVFX(FVector Direction,float Fill=-1.f);
    /** Before commit only. Copies settings and retains resolved systems; never mutates the profile/CDO. */
    bool PrepareVFXProfile(UMCPerkVFXProfile* Profile,FString& OutError);
    bool PrepareActiveAudio(UMCPerkVFXProfile* Profile,FString& OutError);
    void GetPreparedAudioResources(TArray<UObject*>& OutResources) const;
    void GetPreparedAudioSlots(TArray<FMCPreparedEventSFX>& OutSlots) const;
    bool CertifyPreparedAudio(const UMCEventSFXReadiness* Readiness,FString& OutError);
    bool IsPreparedAudioReady() const;
    static void StopWorldAudio(UWorld* World);
    const FString& GetLastAudioError() const {return LastAudioError;}
    UFUNCTION(BlueprintPure,Category="Skill|VFX") UNiagaraSystem* GetPreparedVFXSystem(FName Stage) const;
    /** Includes every independent composition source; readiness callers must not inspect only the legacy single System. */
    void GetPreparedVFXSystems(FName Stage,TArray<UNiagaraSystem*>& OutSystems) const;
    UFUNCTION(BlueprintPure,Category="Skill|VFX") UMCVFXComposition* GetPreparedVFXComposition(FName Stage) const;
    /** Native observation of real action events; cosmetic disabling does not remove the event. */
    FOnMCActiveSkillVFXStage OnVFXStage;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Skill") TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Skill") TObjectPtr<UStaticMeshComponent> PropMesh;
    /** Reflected CDO roots are followed by the loading manifest through EffectClass -> ActionClass. */
    UPROPERTY(EditDefaultsOnly,Category="Skill|Presentation") TSoftObjectPtr<UStaticMesh> PropAsset;
    UPROPERTY(EditDefaultsOnly,Category="Skill|Presentation") FTransform PropTransform=FTransform::Identity;
    /** Prop-local skeletal wrist frames, including rotation. No hand at an unspecified anchor. */
    UPROPERTY(EditDefaultsOnly,Category="Skill|Presentation|Grip") bool bHasLeftGrip=false;
    UPROPERTY(EditDefaultsOnly,Category="Skill|Presentation|Grip") bool bHasRightGrip=false;
    UPROPERTY(EditDefaultsOnly,Category="Skill|Presentation|Grip") FTransform LeftGripTransform=FTransform::Identity;
    UPROPERTY(EditDefaultsOnly,Category="Skill|Presentation|Grip") FTransform RightGripTransform=FTransform::Identity;
    UPROPERTY(EditDefaultsOnly,Category="Skill|VFX") TSoftObjectPtr<UNiagaraSystem> StartVFX;
    UPROPERTY(EditDefaultsOnly,Category="Skill|VFX") TSoftObjectPtr<UNiagaraSystem> LoopVFX;
    UPROPERTY(EditDefaultsOnly,Category="Skill|VFX") TSoftObjectPtr<UNiagaraSystem> ImpactVFX;
    UPROPERTY(EditDefaultsOnly,Category="Skill|VFX") TSoftObjectPtr<UNiagaraSystem> EndVFX;
    UPROPERTY(EditDefaultsOnly,Category="Skill|VFX") TSoftObjectPtr<UNiagaraSystem> WarningVFX;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Skill|VFX") TObjectPtr<UNiagaraComponent> LoopComponent;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    /** Exactly once, including external destruction/world teardown; release only this action's leases. */
    virtual void OnFinished(EMCActiveSkillEndReason Reason);
    void MarkPresentationReleased();
    bool HasCommittedInputView() const {return CommittedInputView.IsSet();}
    FTransform ResolvePresentationGrip(bool bLeft,const TMap<FName,float>* PreviewParameters=nullptr) const;

private:
    friend class UMCActiveSkillPerkEffect;
    friend class UMCPerkComponent;
    friend class FMCActiveAudioEventClockTest;
    friend class FMCActiveAudioLoopSnapshotTest;
    void InitializeAction(UMCPerkComponent* OwnerPerks,AMCToothCharacter* Pawn,FName ID,
        const FMCActiveSkillInputHandle& Handle,const TMap<FName,float>& Values);
    UFUNCTION() void OnRep_Committed();
    UFUNCTION() void OnRep_Presentation();
    UFUNCTION() void OnRep_Loop();
    UFUNCTION(NetMulticast,Reliable) void MulticastSkillCue(FName Stage,FVector_NetQuantize Location,FVector_NetQuantizeNormal Direction,float Radius,
        double EventStartedAt,uint32 CueSerial,FGuid CueEpoch,bool bStopAudio=false,bool bImmediate=false);
    void ApplyPresentation();
    void ApplyPresentationLease();
    void ReleaseAppliedPresentationLease();
    void RefreshPresentationTickPrerequisite(USkeletalMeshComponent* Mesh);
    void ReleasePresentationTickPrerequisite();
    void ApplyVFXParameters(UNiagaraComponent* Component,FVector Direction,float Radius,float Fill=-1.f) const;
    bool PrepareVFXBindings(const TArray<FMCPerkVFXBinding>& Bindings,FString& OutError,bool bAllowLoad=true,
        const TArray<FMCActiveSkillSFXBinding>* GroupBindings=nullptr);
    bool BuildActiveAudioBindings(UMCPerkVFXProfile* Profile,TArray<FMCActiveSkillSFXBinding>& OutBindings,
        TMap<FName,FMCPreparedEventSFX>& OutPrepared,FString& Error) const;
    bool PrepareActiveAudioBindings(const TArray<FMCActiveSkillSFXBinding>& Bindings,bool bAllowLoad,
        TMap<FName,FMCPreparedEventSFX>& OutPrepared,FString& Error) const;
    bool CertifyAudioSlot(FMCPreparedEventSFX& Slot,FString& Error) const;
    void QueueActiveAudio(FName Stage,const FTransform& Frame,double EventStartedAt,FGuid Epoch,bool bLoop=false);
    void ProcessPendingAudio();
    void UpdateLoopAudio();
    void StopActiveAudio(bool bImmediate,bool bKeepTerminalTails=false);
    UFUNCTION() void OnRep_AudioBindings();
    void PrepareDefaultVFX();
    const FMCPerkVFXSlot* PreparedVFXSlot(FName Stage) const;
    void ApplyVFXSlotOverrides(UNiagaraComponent* Component,FName Stage) const;
    FMCPerkVFXGameplayValues CompositionGameplayValues(FVector Direction,float Radius,float Fill=-1.f) const;
    AMCPerkVFXCompositionPlayer* PlayPreparedComposition(FName Stage,const FTransform& Frame,bool bLoop,
        const FMCPerkVFXGameplayValues& Values);
    void StopCompositionPlayers(bool bImmediate);
    UFUNCTION() void OnRep_VFXBindings();
    UPROPERTY(ReplicatedUsing=OnRep_VFXBindings) TArray<FMCPerkVFXBinding> VFXBindings;
    UPROPERTY(ReplicatedUsing=OnRep_AudioBindings) TArray<FMCActiveSkillSFXBinding> AudioBindings;
    UPROPERTY(Transient) TMap<FName,FMCPreparedEventSFX> PreparedAudio;
    UPROPERTY(Transient) TArray<TWeakObjectPtr<AMCEventSFXPlayer>> AudioPlayers;
    UPROPERTY(Transient) TWeakObjectPtr<AMCEventSFXPlayer> LoopAudioPlayer;
    UPROPERTY(Transient) TArray<FMCActiveSkillAudioCue> PendingAudio;
    UPROPERTY(Transient) TObjectPtr<UMCEventSFXReadiness> LocalAudioReadiness;
    UPROPERTY(Transient) TMap<FName,TObjectPtr<UNiagaraSystem>> PreparedVFXSystems;
    UPROPERTY(Transient) TMap<FName,FMCPreparedPerkVFXComposition> PreparedVFXCompositions;
    UPROPERTY(Transient) TArray<TWeakObjectPtr<AMCPerkVFXCompositionPlayer>> CompositionPlayers;
    UPROPERTY(Transient) TWeakObjectPtr<AMCPerkVFXCompositionPlayer> LoopCompositionPlayer;
    FMCPerkVFXGameplayValues LoopCompositionValues;
    UPROPERTY(Transient) TArray<FMCPerkVFXBinding> PreparedVFXBindings;
    FTransform DefaultLoopTransform=FTransform::Identity;
    bool bCapturedDefaultLoopTransform=false;
    bool bVFXPrepared=false;
    bool bLoopEventRunning=false;
    bool bAudioPrepared=false;
    double LastLoopAudioStamp=-1;
    uint32 IssuedCueSerial=0,ReceivedCueSerial=0;
    FString LastAudioError;
    bool bAudioCancelled=false;
    UPROPERTY(Replicated) TObjectPtr<AMCPlayerState> SourcePlayerState;
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) TObjectPtr<AMCToothCharacter> SourcePawn;
    UPROPERTY(Replicated) FName PerkID;
    UPROPERTY(Replicated) FGuid ActionID;
    UPROPERTY(Replicated) FMCActiveSkillInputHandle InputHandle;
    UPROPERTY() TMap<FName,float> Parameters;
    UPROPERTY(Replicated) TArray<FMCActiveSkillParameter> InitialParameters;
    UPROPERTY(ReplicatedUsing=OnRep_Committed) bool bCommitted=false;
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) bool bWantsPresentation=false;
    UPROPERTY(ReplicatedUsing=OnRep_Loop) bool bLoopRunning=false;
    UPROPERTY(ReplicatedUsing=OnRep_Loop) double LoopStartedAt=-1;
    UPROPERTY(Replicated) FGuid GameplayEpoch;
    TWeakObjectPtr<AMCToothCharacter> AppliedPresentationPawn;
    TWeakObjectPtr<USkeletalMeshComponent> AppliedPresentationTickMesh;
    bool bOwnsPresentationTickPrerequisite=false;
    bool bReportedPresentationTickCycle=false;
    UPROPERTY(Replicated) double StartedAt=0;
    UPROPERTY(Replicated) double PresentationReleasedAt=-1;
    bool bInitialized=false;
    TOptional<FMCActiveSkillInputView> CommittedInputView;
    bool bFinished=false;
};
