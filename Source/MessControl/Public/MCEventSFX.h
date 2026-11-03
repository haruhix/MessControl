#pragma once

#include "CoreMinimal.h"
#include "MCEventSFX.generated.h"

class USoundBase;
class USoundAttenuation;
class USoundConcurrency;

/** Independent audio policy. It never determines whether native VFX run. */
UENUM(BlueprintType)
enum class EMCEventSFXMode : uint8
{
    UseDefault UMETA(DisplayName="Original audio"),
    Add UMETA(DisplayName="Original audio + layers"),
    Replace UMETA(DisplayName="Layers only"),
    Disabled UMETA(DisplayName="Audio disabled")
};

UENUM(BlueprintType)
enum class EMCEventSFXSpace : uint8
{
    NonSpatial2D,
    EventLocation,
    FollowEventFrame,
    SourceSocket
};

/** A real SoundBase source: Wave, Cue and MetaSoundSource all use this contract. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCEventSFXLayer
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SFX",meta=(IgnoreForMemberInitializationTest)) FGuid ID=FGuid::NewGuid();
    /** Same event-group GUID as VFX layers. Invalid means the implicit legacy Default group. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SFX") FGuid GroupID;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SFX") FName Name;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SFX") bool bEnabled=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SFX") TSoftObjectPtr<USoundBase> Sound;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing",meta=(ClampMin="0",ClampMax="60",Units="s")) float DelaySeconds=0.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Mix",meta=(ClampMin="0",ClampMax="4")) float Volume=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Mix",meta=(ClampMin="0.125",ClampMax="4")) float Pitch=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Placement") EMCEventSFXSpace Space=EMCEventSFXSpace::EventLocation;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Mix") bool bIsUISound=false;
    /** Local to the real accepted event frame; does not modify gameplay geometry. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Placement") FTransform Offset=FTransform::Identity;
    /** The source provider must resolve the actual component. Missing bindings are errors. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Placement") FName Socket;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Placement") TSoftObjectPtr<USoundAttenuation> Attenuation;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Mix") TSoftObjectPtr<USoundConcurrency> Concurrency;
    /** Replays finite sources. Source-authored seamless loops remain authored in the source. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing") bool bRepeatOnFinish=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing",meta=(ClampMin="0",ClampMax="10",Units="s")) float FadeInSeconds=0.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing",meta=(ClampMin="0",ClampMax="10",Units="s")) float FadeOutSeconds=.1f;
    /** Finite one-shots may finish on graceful episode end; reset/death destroys all voices. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing") bool bAllowNaturalTail=true;
};

/** Authored at Profile.SFXSlots[actual event]. An absent entry is UseDefault. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCEventSFXSlot
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SFX") EMCEventSFXMode Mode=EMCEventSFXMode::UseDefault;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SFX") TArray<FMCEventSFXLayer> Layers;
    /** Safety horizon from the accepted event, including delays and source loops. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing",meta=(ClampMin="0.01",ClampMax="3600",Units="s")) float MaxDurationSeconds=30.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing",meta=(ClampMin="0",ClampMax="10",Units="s")) float TailSeconds=2.f;
    bool UsesNativeAudio() const {return Mode==EMCEventSFXMode::UseDefault || Mode==EMCEventSFXMode::Add;}
    bool HasCustomLayers() const;
    bool Validate(FString& Error) const;
    void GetSoftResources(TArray<FSoftObjectPath>& Out) const;
};

/** Immutable resident snapshot. Creation/loading belongs before readiness, never to Play. */
USTRUCT()
struct MESSCONTROL_API FMCPreparedEventSFX
{
    GENERATED_BODY()
    UPROPERTY() FMCEventSFXSlot Slot;
    UPROPERTY() TArray<TObjectPtr<USoundBase>> Sounds;
    UPROPERTY() TArray<TObjectPtr<USoundAttenuation>> Attenuations;
    UPROPERTY() TArray<TObjectPtr<USoundConcurrency>> Concurrencies;
    /** Effective shared-group policy; no duplicate group model. Source/settings remain retained. */
    UPROPERTY() TArray<FGuid> DisabledGroupIDs;
    UPROPERTY() TArray<FGuid> MutedGroupIDs;
    /** Local asset/Precache/startup-byte certificate; not decoded-buffer/DSP readiness or network-authored data. */
    UPROPERTY() bool bPlaybackResourcesPrepared=false;
};

namespace MCEventSFX
{
    MESSCONTROL_API bool Prepare(const FMCEventSFXSlot& Slot,bool bAllowLoad,FMCPreparedEventSFX& Out,FString& Error,bool* bWaiting=nullptr);
    /** Reject suppression if the real original source has not been wired to an audio-only gate. */
    MESSCONTROL_API bool ValidateNativePolicy(const FMCEventSFXSlot& Slot,bool bCanSuppressNativeAudio,FString& Error);
    /** Uses GUID projections from the ONE shared event-group model. Invalid GUID is the implicit Default group. */
    MESSCONTROL_API bool CompileSharedGroupPolicy(const TArray<FGuid>& SharedGroupIDs,
        const TArray<FGuid>& DisabledGroupIDs,const TArray<FGuid>& MutedGroupIDs,FMCPreparedEventSFX& Prepared,FString& Error);
}
