#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCPerkVFXCompositionPlayer.h"
#include "MCVFXNativePresentationSettings.h"
#include "MCEventSFX.h"
#include "MCVFXPresentationComponent.generated.h"

class UMCVFXPresentationLibrary;
class UMCEventSFXReadiness;
class AMCEventSFXPlayer;

/** Immutable GUID projection of the one authored event-group model. */
USTRUCT()
struct FMCVFXEventGroupPolicy
{
    GENERATED_BODY()
    UPROPERTY() TArray<FGuid> SharedIDs;
    UPROPERTY() TArray<FGuid> DisabledIDs;
    UPROPERTY() TArray<FGuid> MutedSFXIDs;
};

UENUM(BlueprintType)
enum class EMCVFXPresentationResult : uint8 { UseDefault, Suppressed, Played, Rejected };

/** Accepted cosmetic receipt carried by the existing replicated gameplay actor/component. */
USTRUCT()
struct FMCVFXPresentationReceipt
{
    GENERATED_BODY()
    UPROPERTY() uint32 Serial=0;
    UPROPERTY() FName SourceID;
    UPROPERTY() FName Event;
    UPROPERTY() FMCPerkVFXSlot Slot;
    /** Legacy receipts play VFX. Audio-only receipts never replay native visuals. */
    UPROPERTY() bool bPlayVFX=true;
    UPROPERTY() bool bPlaySFX=false;
    UPROPERTY() FMCEventSFXSlot SFXSlot;
    UPROPERTY() FMCVFXEventGroupPolicy GroupPolicy;
    UPROPERTY() FTransform Frame=FTransform::Identity;
    UPROPERTY() double StartedAt=0;
    UPROPERTY() FGuid GameplayEpoch;
    UPROPERTY() FVector Direction=FVector::ZeroVector;
    UPROPERTY() float Radius=-1;
    UPROPERTY() float Fill=-1;
    UPROPERTY() float Intensity=-1;
    UPROPERTY() float Duration=-1;
    UPROPERTY() FLinearColor Color=FLinearColor::White;
    UPROPERTY() bool bHasColor=false;
    UPROPERTY() FVector SourcePosition=FVector::ZeroVector;
    UPROPERTY() FVector TargetPosition=FVector::ZeroVector;
    UPROPERTY() bool bHasEndpoints=false;
    UPROPERTY() bool bHasSeed=false;
    UPROPERTY() uint32 Seed=0;
    UPROPERTY() FGuid EpisodeID;
    UPROPERTY() bool bHasVisualSubject=false;
    UPROPERTY() TObjectPtr<AActor> VisualSubjectActor;
    UPROPERTY() FName VisualSubjectComponent;
    UPROPERTY() TArray<FMCEchoToolPart> CapturedMeshes;
    UPROPERTY() bool bCapturedMeshOverhead=false;
    UPROPERTY() double CapturedContactAt=0;
    UPROPERTY() FMCVFXNativeInputs NativeInputs;
};

USTRUCT()
struct FMCVFXPresentationCancel
{
    GENERATED_BODY()
    UPROPERTY() uint32 ThroughSerial=0;
    UPROPERTY() bool bImmediate=false;
    /** None = all sources. Invalid EpisodeID = the entire named source. */
    UPROPERTY() FName SourceID;
    UPROPERTY() FGuid EpisodeID;
};

USTRUCT()
struct FMCVFXPreparedSource
{
    GENERATED_BODY()
    UPROPERTY() TArray<FName> AllowedEvents;
    UPROPERTY() TMap<FName,FMCPreparedPerkVFXComposition> Events;
    UPROPERTY() TMap<FName,FMCPreparedEventSFX> AudioEvents;
    UPROPERTY() TMap<FName,FMCVFXEventGroupPolicy> GroupPolicies;
    UPROPERTY() FMCVFXPreparedNativePresentation Native;
};

USTRUCT()
struct FMCVFXPresentationPlayerHandle
{
    GENERATED_BODY()
    UPROPERTY() TWeakObjectPtr<AMCPerkVFXCompositionPlayer> Player;
    UPROPERTY() TWeakObjectPtr<AMCEventSFXPlayer> AudioPlayer;
    UPROPERTY() FName SourceID;
    UPROPERTY() FGuid EpisodeID;
    UPROPERTY() FTransform Offset=FTransform::Identity;
};

/** Peer-local native frame owner. No presentation transform is sent every tick. */
USTRUCT()
struct FMCVFXPresentationTickSource
{
    GENERATED_BODY()
    UPROPERTY() FName SourceID;
    UPROPERTY() FGuid EpisodeID;
    UPROPERTY() TWeakObjectPtr<AActor> Actor;
};

/** Peer-local state reconstructed from an existing replicated owner; never sent per tick. */
USTRUCT()
struct FMCVFXLocalSourceState
{
    GENERATED_BODY()
    UPROPERTY() FName SourceID;
    UPROPERTY() FName Event;
    UPROPERTY() FMCVFXPresentationPlayerHandle Handle;
    UPROPERTY() TWeakObjectPtr<UActorComponent> TickPrerequisite;
    UPROPERTY() bool bHasTickPrerequisite=false;
    UPROPERTY() FGuid Epoch;
    /** Peer-local lane ownership; never part of a replicated receipt. */
    UPROPERTY() bool bDispatchAudio=true;
    UPROPERTY() double NativeStartedAt=0;
};

USTRUCT()
struct FMCPendingVFXPresentationReceipt
{
    GENERATED_BODY()
    UPROPERTY() FMCVFXPresentationReceipt Receipt;
    UPROPERTY() FMCPreparedPerkVFXComposition Prepared;
    UPROPERTY() FMCPreparedEventSFX PreparedAudio;
    UPROPERTY() bool bPrepared=false;
    UPROPERTY() bool bAudioPrepared=false;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FMCVFXPresentationEvent,FName,EMCVFXPresentationResult);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FMCVFXSourcePresentationEvent,FName,FName,EMCVFXPresentationResult);

/** Prepared event presentation without an active-skill dependency. Never applies gameplay effects. */
UCLASS(ClassGroup=(Presentation),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCVFXPresentationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCVFXPresentationComponent();
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") FName SourceID;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TArray<FName> Events;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UMCVFXPresentationLibrary> Library;
    /** Explicit preparation may load assets; runtime cue playback never does. */
    bool Prepare(FString& Error,bool bAllowLoad=true);
    /** Local working-copy override for the owned editor scenario, before the real gameplay event. */
    bool PrepareProfileOverride(UMCPerkVFXProfile* Profile,FString& Error);
    bool PrepareSourceProfile(FName ID,const TArray<FName>& AllowedEvents,UMCPerkVFXProfile* Profile,FString& Error,bool bAllowLoad=true);
    bool PrepareScopedOverride(FName ID,const TArray<FName>& AllowedEvents,UMCPerkVFXProfile* Profile,FString& Error);
    bool IsSourcePrepared(FName ID) const {return PreparedSources.Contains(ID);}
    bool IsSourceReadyForPlayback(FName ID) const;
    /** Immutable no-load handoff to a not-yet-committed actor in this world. Never copies receipt serials. */
    bool CopyPreparedSourceTo(FName ID,UMCVFXPresentationComponent* Target,FString& Error) const;
    const FMCPreparedPerkVFXComposition* GetPreparedSourceEvent(FName ID,FName Event) const {return FindPreparedEvent(ID,Event);}
    bool GetPreparedEventMode(FName ID,FName Event,EMCPerkVFXSlotMode& OutMode) const;
    bool GetPreparedAudioMode(FName ID,FName Event,EMCEventSFXMode& OutMode) const;
    /** Native animated cue/projectile must finish its frame before matching local players tick. */
    bool BindSourceEpisodeTickSource(FName ID,FGuid EpisodeID,AActor* TickSource,FString& Error);
    TArray<FName> GetPreparedSourceIDs() const;
    void ClearScopedOverride(FName ID);
    /** bDispatchVFX=false only when an existing replicated state actor owns that visual lane. SFX and acceptance stay independent. */
    EMCVFXPresentationResult PlaySourceEvent(FName ID,FName Event,const FTransform& Frame,const FMCPerkVFXGameplayValues& Values=FMCPerkVFXGameplayValues(),bool bDispatchVFX=true);
    /** Existing peer-local accepted boundary: independent prepared one-shot, no RPC or receipt serial ledger.
     * LocalStateAgeSeconds may carry the actual replicated boundary age. Audio errors stay independent of the VFX result. */
    EMCVFXPresentationResult PlayLocalSourceEvent(FName ID,FName Event,const FTransform& Frame,const FMCPerkVFXGameplayValues& Values=FMCPerkVFXGameplayValues(),bool bDispatchVFX=true);
    /** No load, RNG, gameplay, or RPC. Default/Disabled modes are successful no-ops; query mode for native gates.
     * Audio may belong to a separate accepted receipt; disabling this lane stops only its state-owned audio. */
    bool UpdateLocalSourceState(FName ID,FName Event,bool bActive,const FTransform& Frame,
        const FMCPerkVFXGameplayValues& Values,UActorComponent* TickPrerequisite,FString& Error,bool bDispatchAudio=true);
    void StopLocalSourceStates(FName ID=NAME_None);
    int32 GetLocalSourceStateCount() const {return LocalSourceStates.Num();}
    void StopSource(FName ID,bool bImmediate=true);
    void StopSourceEpisode(FName ID,FGuid EpisodeID,bool bImmediate=true);
    bool UpdateSourceEpisode(FName ID,FGuid EpisodeID,const FTransform& Frame,const FMCPerkVFXGameplayValues& Values);
    void ClearProfileOverride();
    void GetPreparedResources(TArray<UNiagaraSystem*>& OutSystems) const;
    const FMCVFXPreparedNativePresentation* GetPreparedNativeSource(FName ID) const;
    void GetPreparedNativeResources(TArray<UObject*>& OutObjects) const;
    void GetPreparedAudioResources(TArray<UObject*>& OutObjects) const;
    void GetPreparedAudioSlots(TArray<FMCPreparedEventSFX>& OutSlots) const;
    /** Before readiness only: trusts the local resource barrier, never replicated certificate bits. */
    bool CertifyPreparedAudio(const UMCEventSFXReadiness* Readiness,FString& Error);
    /** Provider-supplied native socket owner; no mesh/component inference from names. */
    bool BindSourceAudioSocket(FName ID,USceneComponent* Component,FString& Error);
    /** Authority accepted-event boundary. UseDefault means the caller must retain its existing native cosmetic. */
    EMCVFXPresentationResult PlayEvent(FName Event,const FTransform& Frame,const FMCPerkVFXGameplayValues& Values=FMCPerkVFXGameplayValues(),bool bDispatchVFX=true);
    /** Stops accepted transient events; persistent native states have an independent explicit lifetime. */
    void StopPresentation(bool bImmediate=false);
    /** Death-boundary cleanup: only this accepted local source's authored
     * finite audio tails retire gracefully; persistent states/other sources stop. */
    void StopPresentationWithLocalSourceAudioTails(FName LocalSourceID);
    void RetireLocalSourceAudioEvent(FName LocalSourceID,FName Event);
    /** Drop one immutable preparation snapshot only. No cancel, scoped override
     * removal, audio replay or accepted-player mutation. Explicit owner boundary. */
    void ForgetPreparedSourceSnapshot(FName ID);
    /** Explicit run reset only. Stops component-owned event players, including tails whose original actor is gone. */
    static void StopWorldPresentations(UWorld* World);
    bool IsPresentationRunning() const;
    bool IsSourceEpisodeRunning(FName ID,FGuid EpisodeID) const;
    /** Native artwork may yield only to an active visual layer, never audio or a delayed reservation. */
    bool IsSourceEpisodeVisualActive(FName ID,FGuid EpisodeID) const;
    bool IsPrepared() const {return bPrepared;}
    int32 GetPendingReceiptCount() const {return PendingReceipts.Num();}
    uint32 GetHighestReceivedSerial() const {return ReceivedSerial;}
    uint32 GetCancelledThroughSerial() const {return CancelledThroughSerial;}
    const FString& GetLastError() const {return LastError;}
    const FString& GetLastAudioError() const {return LastAudioError;}
    /** Native event observed after the actual gameplay boundary; never a manufactured preview timeline. */
    FMCVFXPresentationEvent OnEventAccepted;
    FMCVFXSourcePresentationEvent OnSourceEventAccepted;
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
private:
    friend class FMCVFXOrderedDeliveryTest;
    friend class FMCVFXCancellationWatermarkTest;
    friend class FMCVFXBoundedDeliveryTest;
    friend class FMCVFXEpochSupersessionTest;
    friend class FMCVFXEpisodeIsolationTest;
    friend class FMCVFXAudioIndependentChannels;
    friend class FMCVFXAudioOrderedCancellation;
    friend class FMCVFXSharedGroupsPreparation;
    friend class FMCCombatEchoCapturedPayload;
    friend class FMCCombatBilliardsVisualSubject;
    friend class FMCCharacterMovementLocalAudio;
    bool PrepareProfile(UMCPerkVFXProfile* Profile,bool bAllowLoad,FString& Error);
    bool BuildPreparedSource(FName ID,const TArray<FName>& AllowedEvents,UMCPerkVFXProfile* Profile,bool bAllowLoad,FMCVFXPreparedSource& Out,FString& Error);
    const FMCPreparedPerkVFXComposition* FindPreparedEvent(FName ID,FName Event) const;
    bool PrepareSlot(const FMCPerkVFXSlot& Slot,bool bAllowLoad,FMCPreparedPerkVFXComposition& Out,FString& Error,bool* bWaiting=nullptr,
        const FMCVFXEventGroupPolicy* Groups=nullptr);
    bool PrepareAudioSlot(FName ID,FName Event,const FMCEventSFXSlot& Slot,const FMCVFXEventGroupPolicy& Groups,bool bAllowLoad,
        FMCPreparedEventSFX& Out,FString& Error,bool* bWaiting=nullptr);
    bool ValidatePreparedEventInput(const FMCPreparedPerkVFXComposition* Prepared,const FTransform& Frame,
        const FMCPerkVFXGameplayValues& Values,bool bDispatchVFX,FString& Error) const;
    FMCVFXPresentationReceipt MakePreparedEventReceipt(FName ID,FName Event,const FTransform& Frame,
        const FMCPerkVFXGameplayValues& Values,const FMCVFXPreparedSource& Source,
        const FMCPreparedPerkVFXComposition& Prepared,bool bDispatchVFX) const;
    bool PresentReceipt(const FMCVFXPresentationReceipt& Receipt,const FMCPreparedPerkVFXComposition* Prepared,FString& Error,
        const FMCPreparedEventSFX* PreparedAudio=nullptr,FMCVFXPresentationPlayerHandle* LocalHandle=nullptr);
    bool ReceiveReceipt(const FMCVFXPresentationReceipt& Receipt,const FMCPreparedPerkVFXComposition* Prepared,FString& Error,bool bMayWait,
        const FMCPreparedEventSFX* PreparedAudio=nullptr);
    void PresentAudio(const FMCVFXPresentationReceipt& Receipt,const FMCPreparedEventSFX* PreparedAudio,
        FMCVFXPresentationPlayerHandle& Handle,float Elapsed);
    bool CertifyAudioSlot(FMCPreparedEventSFX& Slot,FString& Error) const;
    void ProcessPendingReceipts();
    void ReceiveCancel(const FMCVFXPresentationCancel& Cancel);
    void ObserveRunEpoch();
    uint32 GetCancellationFloor(const FMCVFXPresentationReceipt& Receipt) const;
    void PruneScopedCancels();
    void StopScope(FName ID,FGuid EpisodeID,bool bImmediate,FName LocalAudioTailSource=NAME_None);
    UFUNCTION(NetMulticast,Reliable) void MulticastReceipt(const FMCVFXPresentationReceipt& Receipt);
    UFUNCTION(NetMulticast,Reliable) void MulticastCancel(const FMCVFXPresentationCancel& Cancel);
    UFUNCTION() void OnRep_Receipt();
    UFUNCTION() void OnRep_Cancel();
    UFUNCTION() void OnRep_ScopedCancels();
    /** Initial-only snapshots serve late joiners. Live ordered delivery uses only the reliable RPC path. */
    UPROPERTY(ReplicatedUsing=OnRep_Receipt) FMCVFXPresentationReceipt LastReceipt;
    UPROPERTY(ReplicatedUsing=OnRep_Cancel) FMCVFXPresentationCancel LastCancel;
    UPROPERTY(ReplicatedUsing=OnRep_ScopedCancels) TArray<FMCVFXPresentationCancel> ScopedCancels;
    UPROPERTY(Transient) TMap<FName,FMCVFXPreparedSource> PreparedSources;
    /** Immutable overrides survive canonical grant/pawn re-preparation during editor scenarios. */
    UPROPERTY(Transient) TMap<FName,FMCVFXPreparedSource> ScopedOverrides;
    UPROPERTY(Transient) TObjectPtr<UMCVFXPresentationLibrary> RetainedLibrary;
    UPROPERTY(Transient) TObjectPtr<UMCPerkVFXProfile> RetainedProfile;
    UPROPERTY(Transient) TMap<uint32,FMCVFXPresentationPlayerHandle> Players;
    UPROPERTY(Transient) TArray<FMCVFXPresentationTickSource> EpisodeTickSources;
    UPROPERTY(Transient) TArray<FMCVFXLocalSourceState> LocalSourceStates;
    /** Same player handles/epoch lifecycle, deliberately separate from remote serial keys. */
    UPROPERTY(Transient) TArray<FMCVFXLocalSourceState> LocalEventPlayers;
    UPROPERTY(Transient) TArray<FMCPendingVFXPresentationReceipt> PendingReceipts;
    uint32 ReceivedSerial=0,IssuedSerial=0,CancelledThroughSerial=0;
    FGuid ObservedEpoch;
    TArray<FGuid> RetiredEpochs;
    UPROPERTY(Transient) FMCVFXPresentationReceipt LatestReceivedReceipt;
    UPROPERTY(Transient) TObjectPtr<UMCEventSFXReadiness> LocalAudioReadiness;
    UPROPERTY(Transient) TMap<FName,TWeakObjectPtr<USceneComponent>> AudioSocketSources;
    bool bPrepared=false,bHasOverride=false;
    FString LastError;
    FString LastAudioError;
};
