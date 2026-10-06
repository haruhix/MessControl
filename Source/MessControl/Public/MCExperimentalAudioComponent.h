#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCExperimentalAudioComponent.generated.h"

class USoundBase;
class UInitialActiveSoundParams;
class UAudioComponent;
class UMCSoundPalette;

/** Finite MetaSound experiments; missing assets leave the original palette available. */
UCLASS(ClassGroup=(MessControl),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCExperimentalAudioComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCExperimentalAudioComponent();
    static bool IsEnabled();
    // Every graph accepts normalized Intensity and Speed and reports OnFinished.
    UFUNCTION(BlueprintCallable,Category="Audio|Experiments")
    bool Play(FName Event,FVector Location,float Intensity=.65f,float Speed=.5f);
    void ShowStatus();
    void BeginComparison(FName Event,UMCSoundPalette* Palette);
    UPROPERTY(EditAnywhere,Category="Audio|Experiments") TMap<FName,TSoftObjectPtr<USoundBase>> Sounds;
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    USoundBase* ResolveSound(FName Event,FString& FailureReason);
    void AdvanceComparison();
    void StopComparison();
    UPROPERTY(Transient) TMap<FName,TObjectPtr<USoundBase>> LoadedSounds;
    UPROPERTY(Transient) TMap<FName,FSoftObjectPath> LoadedPaths;
    UPROPERTY(Transient) TObjectPtr<UInitialActiveSoundParams> InitialParameters;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> ComparisonAudio;
    UPROPERTY(Transient) TObjectPtr<UMCSoundPalette> ComparisonPalette;
    FTimerHandle ComparisonTimer;
    FName ComparisonEvent;
    int32 ComparisonStep=0;
};
