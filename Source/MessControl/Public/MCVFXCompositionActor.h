#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCVFXCompositionActor.generated.h"

class UMCVFXComposition;
class UNiagaraComponent;
class UNiagaraSystem;
class USceneComponent;

/** Cosmetic only. Preparation is explicit; Play never synchronously loads known effects on first interaction. */
UCLASS(BlueprintType)
class MESSCONTROL_API AMCVFXCompositionActor : public AActor
{
    GENERATED_BODY()
public:
    AMCVFXCompositionActor();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VFX Composition") TObjectPtr<UMCVFXComposition> Composition;

    /** Call during loading/preparation. Does not promise that outstanding editor shaders or packaged PSOs are ready. */
    UFUNCTION(BlueprintCallable, Category="VFX Composition") bool PrepareComposition(UMCVFXComposition* InComposition,FString& OutError);
    UFUNCTION(BlueprintCallable, Category="VFX Composition") bool PlayPrepared(USceneComponent* Subject=nullptr,bool bHonorSolo=false);
    UFUNCTION(BlueprintCallable, Category="VFX Composition") void PauseComposition(bool bPaused);
    UFUNCTION(BlueprintCallable, Category="VFX Composition") void StopComposition(bool bImmediate=true);
    UFUNCTION(BlueprintPure, Category="VFX Composition") float GetPlaybackTime() const {return PlaybackTime;}
    UFUNCTION(BlueprintPure, Category="VFX Composition") bool IsPlaying() const {return bRunning && !bPaused;}
    UFUNCTION(BlueprintPure, Category="VFX Composition") bool IsRunning() const {return bRunning;}
    UFUNCTION(BlueprintPure, Category="VFX Composition") FString GetLastError() const {return LastError;}

    void SetExternallyTicked(bool bValue) {bExternallyTicked=bValue;}
    void AdvanceComposition(float DeltaSeconds);
    int32 GetLiveLayerCount() const;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual bool ShouldTickIfViewportsOnly() const override {return true;}

private:
    void StartLayer(int32 Index);
    UPROPERTY(Transient) TObjectPtr<UMCVFXComposition> Prepared;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraSystem>> PreparedSystems;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> Components;
    UPROPERTY(Transient) TWeakObjectPtr<USceneComponent> SubjectComponent;
    TArray<bool> Started,Stopped;
    float PlaybackTime=0;
    bool bRunning=false,bPaused=false,bExternallyTicked=false,bHonorSolo=false,bHasSolo=false,bEmissionStopped=false;
    FString LastError;
};
