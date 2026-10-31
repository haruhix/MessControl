#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"

class AMCVFXCompositionActor;
class ASkeletalMeshActor;
class FAdvancedPreviewScene;
class FMCVFXComposerViewportClient;
class UMCVFXComposition;
class UNiagaraSystem;
class UMaterialInterface;
class UWorld;

/** Isolated cosmetic preview. It never starts PIE or changes the source Niagara systems. */
class SMCVFXComposerViewport final : public SEditorViewport
{
public:
    SLATE_BEGIN_ARGS(SMCVFXComposerViewport) {} SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SMCVFXComposerViewport() override;

    /** Prepare only. Play starts the prepared snapshot; changing a layer does not mutate it mid-frame. */
    void SetComposition(UMCVFXComposition* InComposition);
    void Play();
    void Pause();
    /** Reset the time and preview subject, then play again. */
    void Reset();
    /** Replay with small fixed steps, then pause. GPU/history-dependent effects are approximate. */
    void Seek(float Seconds);
    void SetMotionEnabled(bool bEnabled);
    void SetGroundVisible(bool bVisible);
    void SetBoundsVisible(bool bVisible);
    void Focus();
    void SetViewPreset(int32 Preset);

    float GetPlaybackTime() const;
    bool IsPlaying() const;
    FString GetDiagnostics() const;
    AMCVFXCompositionActor* GetPreviewActor() const;
    UWorld* GetPreviewWorld() const;

protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
    friend class FMCVFXComposerViewportClient;
    void AdvancePreview(float DeltaSeconds);
    void ResetSubject();
    void UpdateSubject(float DeltaSeconds);
    bool CheckPreviewReadiness();

    TSharedPtr<FAdvancedPreviewScene> PreviewScene;
    TSharedPtr<FMCVFXComposerViewportClient> ComposerViewportClient;
    TWeakObjectPtr<AMCVFXCompositionActor> PreviewActor;
    TWeakObjectPtr<ASkeletalMeshActor> SubjectActor;
    TWeakObjectPtr<UMCVFXComposition> Composition;
    TArray<TWeakObjectPtr<UNiagaraSystem>> ReadinessSystems;
    TArray<TWeakObjectPtr<UMaterialInterface>> ReadinessMaterials;
    FVector SubjectBaseLocation=FVector::ZeroVector;
    FQuat SubjectBaseRotation=FQuat::Identity;
    FString PreparationError;
    FString SubjectWarning;
    FString PlaybackNote;
    FString ReadinessMessage;
    float MotionTime=0;
    bool bPrepared=false;
    bool bHasStarted=false;
    bool bPaused=false;
    bool bMotionEnabled=false;
    bool bPendingPlay=false;
};
