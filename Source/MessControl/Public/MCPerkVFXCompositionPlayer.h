#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCPerkVFXProfile.h"
#include "MCPerkVFXCompositionPlayer.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;

/** Prepared once before commit. Strong references retain all known visuals; playback never loads assets. */
USTRUCT()
struct MESSCONTROL_API FMCPreparedPerkVFXComposition
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<UMCVFXComposition> Snapshot;
    UPROPERTY() TArray<TObjectPtr<UNiagaraSystem>> Systems;
    UPROPERTY() FMCPerkVFXSlot Outer;
};

/** Native action values. Negative values leave the artist's own Niagara default intact. */
struct MESSCONTROL_API FMCPerkVFXGameplayValues
{
    FVector Direction=FVector::ZeroVector;
    float Radius=-1.f,Fill=-1.f,Intensity=-1.f,Duration=-1.f;
    FLinearColor Color=FLinearColor::White;
    bool bHasColor=false;
};

/** Independent local cosmetics. Existing action replication dispatches events; particles are never replicated. */
UCLASS(Transient,NotBlueprintable)
class MESSCONTROL_API AMCPerkVFXCompositionPlayer final:public AActor
{
    GENERATED_BODY()
public:
    AMCPerkVFXCompositionPlayer();
    /** Prepared sources and typed overrides are validated before the action commits. No synchronous load here. */
    bool StartPrepared(const FMCPreparedPerkVFXComposition& Prepared,const FTransform& Frame,
        USceneComponent* Subject,UNiagaraComponent* LoopAnchor,bool bHostLoop,const FMCPerkVFXGameplayValues& Values,FString& Error);
    void UpdateGameplayValues(const FMCPerkVFXGameplayValues& Values);
    void StopEmission(bool bImmediate=false);
    UFUNCTION(BlueprintPure,Category="Perk VFX") bool IsRunning() const {return bRunning;}
    UFUNCTION(BlueprintPure,Category="Perk VFX") int32 GetLiveLayerCount() const;
    UFUNCTION(BlueprintPure,Category="Perk VFX") int32 GetPendingLayerCount() const;
    UFUNCTION(BlueprintPure,Category="Perk VFX") float GetPlaybackTime() const {return Age;}
    UFUNCTION(BlueprintPure,Category="Perk VFX") bool IsStopping() const {return bStopping;}
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    void StartLayer(int32 Index);
    void ApplyValues(int32 Index);
    void Advance(float DeltaSeconds);
    UPROPERTY(Transient) FMCPreparedPerkVFXComposition Data;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> Components;
    UPROPERTY(Transient) TWeakObjectPtr<USceneComponent> SubjectComponent;
    UPROPERTY(Transient) TWeakObjectPtr<UNiagaraComponent> NativeLoopAnchor;
    TArray<bool> Started,Stopped;
    FMCPerkVFXGameplayValues Gameplay;
    FTransform LastAnchor=FTransform::Identity;
    float Age=0,StopAge=0;
    bool bRunning=false,bStopping=false,bHostDrivenLoop=false;
};
