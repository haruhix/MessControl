#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCDataAssets.h"
#include "MCSceneAudioComponent.generated.h"

class UAudioComponent;
struct FStreamableHandle;

/** Local scene music/ambience: two layers, at most one current and one fading voice each. */
UCLASS(ClassGroup=(MessControl),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCSceneAudioComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCSceneAudioComponent();
    /** Missing scenes and empty layers fade to silence. Unchanged layers continue playing. */
    UFUNCTION(BlueprintCallable,BlueprintCosmetic,Category="Audio|Scenes")
    void SetScene(UMCSoundPalette* Palette,FName Scene);
    UFUNCTION(BlueprintCallable,BlueprintCosmetic,Category="Audio|Scenes")
    void RefreshScene();
    UFUNCTION(BlueprintCallable,BlueprintCosmetic,Category="Audio|Scenes")
    void StopScene(float FadeOutSeconds=.75f);
    /** Current and fading voices for inspection; pending loads create no voice. */
    UFUNCTION(BlueprintPure,Category="Audio|Scenes") TArray<UAudioComponent*> GetVoiceComponents() const;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Audio|Scenes") TObjectPtr<UMCSoundPalette> SoundPalette;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Audio|Scenes") FName SceneContext;
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    struct FLayerState
    {
        FMCSoundSceneLayer Desired;
        FMCSoundSceneLayer Active;
        TSharedPtr<FStreamableHandle> Load;
        FTimerHandle RetireTimer;
        uint64 Revision=0;
    };
    FLayerState Layers[2];
    // Current/retiring music, then current/retiring ambience. GC retains every live voice.
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> Voices;
    bool bShuttingDown=false;
    void ApplyScene(UMCSoundPalette* Palette,FName Scene,bool Force);
    void ApplyLayer(int32 Layer,const FMCSoundSceneLayer& Settings,bool Force);
    void StartLayer(int32 Layer,uint64 Revision);
    void RetireCurrent(int32 Layer,float FadeSeconds);
    void DestroyRetiring(int32 Layer);
    void ReleaseVoice(int32 Slot);
    void CancelLoad(int32 Layer);
    void HandleFinished(UAudioComponent* Voice);
};
