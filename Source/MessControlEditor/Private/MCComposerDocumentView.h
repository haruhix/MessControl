#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "UObject/StrongObjectPtr.h"

class FMCVFXActionSession;
class FMCPerkPreviewRunner;
class UMCPerkVFXProfile;
class UMCSoundPalette;
class USoundBase;
class UNiagaraSystem;
class SWidget;
class SBox;
enum class EMCPerkPreviewScenario : uint8;

/** Per-window navigation and playback lease; authored data belongs to the shared document. */
class FMCComposerDocumentView final : public TSharedFromThis<FMCComposerDocumentView>
{
public:
    FMCComposerDocumentView();
    ~FMCComposerDocumentView();
    bool Select(FName ActionID,FString& OutError);
    TSharedPtr<FMCVFXActionSession> GetDocument() const { return Document; }
    FGuid GetViewID() const { return ViewID; }
    bool Play(EMCPerkPreviewScenario Scenario,FString& OutError);
    bool AuditionSound(FName Event,USoundBase* Sound,EMCPerkPreviewScenario Scenario,FString& OutError);
    bool AuditionSystem(FName Event,UNiagaraSystem* System,EMCPerkPreviewScenario Scenario,FString& OutError);
    void Stop();
    void Pause(bool bPaused);
    bool OwnsPreview() const;
    TSharedPtr<FMCPerkPreviewRunner> GetRunner() const;
    TSharedRef<SWidget> MakePreviewWidget();
    void SetPreviewView(int32 Preset);
    bool SetPreviewAudioEnabled(bool bEnabled,FString& OutError);
    bool IsPreviewAudioEnabled() const;
    void SetUseDocumentPreviewMix(bool bUse) { bUseDocumentPreviewMix=bUse; }
    FString GetPreviewAudioStatus() const;
    bool SetSceneAudio(UMCSoundPalette* Palette,FName Scene,float Music,float Ambience,FString& OutError);
    void ClearSceneAudio();
    FString GetSnapshotJson() const;

private:
    TSharedPtr<FMCVFXActionSession> Document;
    FGuid ViewID;
    uint64 PreviewEpoch=0;
    bool bAudioEnabled=false;
    bool bUseDocumentPreviewMix=false;
    TStrongObjectPtr<UMCSoundPalette> ScenePalette;
    FName Scene;
    float MusicGain=1.f,AmbienceGain=1.f;
    int32 CameraPreset=1;
    TFunction<void(int32)> SetCamera;
    TArray<TWeakPtr<SBox>> PreviewHosts;
    bool Start(FName Event,USoundBase* Sound,UNiagaraSystem* System,EMCPerkPreviewScenario Scenario,FString& OutError);
    void RefreshPreviewHosts();
};
