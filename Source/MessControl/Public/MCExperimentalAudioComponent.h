#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCExperimentalAudioComponent.generated.h"

class USoundBase;
class UAudioComponent;
class UMCSoundPalette;

/** Manual MetaSound auditions; gameplay reads the owning character's SoundPalette. */
UCLASS(ClassGroup=(MessControl),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCExperimentalAudioComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCExperimentalAudioComponent();
    // Compatibility query: gameplay palette routing is always enabled, independent of the old CVar.
    static bool IsEnabled();
    // Compatibility entry point. Uses SoundPalette, including explicit silence, never the audition map.
    UFUNCTION(BlueprintCallable,Category="Audio|Experiments")
    bool Play(FName Event,FVector Location,float Intensity=.65f,float Speed=.5f);
    void ShowStatus();
    void BeginComparison(FName Event,UMCSoundPalette* Palette);
    // Used only by mc.Audio.Compare and mc.Audio.Status.
    UPROPERTY(EditAnywhere,Category="Audio|Experiments") TMap<FName,TSoftObjectPtr<USoundBase>> Sounds;
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    USoundBase* ResolveAuditionSound(FName Event,FString& FailureReason);
    void AdvanceComparison();
    void StopComparison();
    UPROPERTY(Transient) TMap<FName,TObjectPtr<USoundBase>> LoadedSounds;
    UPROPERTY(Transient) TMap<FName,FSoftObjectPath> LoadedPaths;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> ComparisonAudio;
    UPROPERTY(Transient) TObjectPtr<UMCSoundPalette> ComparisonPalette;
    FTimerHandle ComparisonTimer;
    FName ComparisonEvent;
    int32 ComparisonStep=0;
};
