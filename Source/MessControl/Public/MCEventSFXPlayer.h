#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCEventSFX.h"
#include "MCEventSFXPlayer.generated.h"

class UAudioComponent;
class AMCGameState;

USTRUCT()
struct MESSCONTROL_API FMCEventSFXContext
{
    GENERATED_BODY()
    UPROPERTY() FName SourceID;
    UPROPERTY() FName Event;
    UPROPERTY() FGuid EpisodeID;
    UPROPERTY() FGuid GameplayEpoch;
    UPROPERTY() float Intensity=.65f;
    UPROPERTY() float Speed=.5f;
    /** Local native source only; rebuilt from the receipt consumer on each peer, never replicated. */
    UPROPERTY() TWeakObjectPtr<AActor> ParameterOwner;
};

/** Local inspection/control only; never part of a saved profile or pack digest. */
struct MESSCONTROL_API FMCEventSFXPreviewFilter
{
    bool bOutputMuted=false;
    float OutputGain=1.f;
    TArray<FGuid> SoloGroupIDs;
    TArray<FGuid> MutedGroupIDs;
};

USTRUCT()
struct FMCEventSFXVoiceState
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<UAudioComponent> Component;
    bool bStarted=false,bFinished=false,bRetiring=false,bAwaitingRepeat=false;
    bool bSilentClock=false,bNaturalTail=false;
    float SourceDuration=0.f,NextBoundaryAge=0.f,RetireAtAge=0.f;
};

/** One accepted SourceID/Event/Episode's local audio. No gameplay or replication decisions. */
UCLASS(NotPlaceable,Transient)
class MESSCONTROL_API AMCEventSFXPlayer : public AActor
{
    GENERATED_BODY()
public:
    AMCEventSFXPlayer();
    /** Enforces world budgets. Owner is WorldSettings so world-space tails survive source destruction. */
    static AMCEventSFXPlayer* SpawnPrepared(UWorld* World,const FMCPreparedEventSFX& Prepared,
        const FTransform& Frame,USceneComponent* SocketSource,const FMCEventSFXContext& Context,
        FName OwnershipTag,FString& Error,float InitialElapsedSeconds=0.f,const FMCEventSFXPreviewFilter* InitialFilter=nullptr);
    bool StartPrepared(const FMCPreparedEventSFX& Prepared,const FTransform& Frame,USceneComponent* SocketSource,
        const FMCEventSFXContext& Context,FString& Error,float InitialElapsedSeconds=0.f,const FMCEventSFXPreviewFilter* InitialFilter=nullptr);
    void Advance(float DeltaSeconds);
    void UpdateEventFrame(const FTransform& Frame);
    /** Optional local native anchor. The active owner supplies actor tick ordering; no transport or socket substitution. */
    bool BindEventFrameSource(USceneComponent* Source);
    void UpdateInputs(float Intensity,float Speed);
    void SetPreviewFilter(const FMCEventSFXPreviewFilter& Filter);
    void Stop(bool bImmediate=true);
    void StopGroup(FGuid GroupID,bool bImmediate=true);
    static void StopWorldPlayers(UWorld* World,FName OwnershipTag);
    bool IsRunning() const {return bRunning;}
    float GetPlaybackTime() const {return Age;}
    int32 GetPendingLayerCount() const;
    TArray<UAudioComponent*> GetVoiceComponents() const;
    int32 GetStartedLayerCount() const;
    int32 GetSkippedLateLayerCount() const {return SkippedLateLayers;}
    const FMCEventSFXContext& GetContext() const {return EventContext;}
    const FString& GetOutputStatus() const {return OutputStatus;}
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    friend class FMCEventSFXOutputTransition;
    bool IsLayerEnabled(int32 Index) const;
    bool HasUsableOutput() const;
    void StartLayer(int32 Index);
    void PlayVoice(int32 Index,float SourceStartSeconds);
    void PlayVoiceWithOutputState(int32 Index,float SourceStartSeconds,bool bOutputAvailable);
    void ApplyGainAndInputs(int32 Index);
    void FinishVoice(UAudioComponent* Component);
    void ReleaseVoice(int32 Index);
    void CancelLayer(int32 Index,bool bImmediate);
    void ClearOwnedVoices();
    float EffectiveGain(int32 Index) const;
    UPROPERTY(Transient) FMCPreparedEventSFX Data;
    UPROPERTY(Transient) TArray<FMCEventSFXVoiceState> Voices;
    UPROPERTY(Transient) FMCEventSFXContext EventContext;
    UPROPERTY(Transient) TWeakObjectPtr<USceneComponent> Subject;
    UPROPERTY(Transient) TWeakObjectPtr<USceneComponent> FrameSource;
    UPROPERTY(Transient) TWeakObjectPtr<AMCGameState> EpochState;
    FMCEventSFXPreviewFilter PreviewFilter;
    FTransform EventFrame=FTransform::Identity;
    float Age=0.f,InitialReceiptAge=0.f;
    bool bRunning=false,bStopping=false,bStartedOnce=false;
    int32 SkippedLateLayers=0,PlaybackRequests=0;
    FString OutputStatus;
};
