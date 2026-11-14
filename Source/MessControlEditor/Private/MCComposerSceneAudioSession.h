#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "MCDataAssets.h"
#include "UObject/StrongObjectPtr.h"

/** One shared working copy for the scene-audio views in Composer. Gameplay keeps
 * reading the canonical palette until the artist explicitly applies this draft. */
class FMCComposerSceneAudioSession final : public FEditorUndoClient
{
public:
    static TSharedRef<FMCComposerSceneAudioSession> Shared();

    /** Public for isolated editor fixtures; production panels use Shared(). */
    FMCComposerSceneAudioSession();
    virtual ~FMCComposerSceneAudioSession() override;

    bool Open(UMCSoundPalette* Palette, FString& Error);
    UMCSoundPalette* GetSource() const { return Source.Get(); }
    UMCSoundPalette* GetDraft() const { return Draft.Get(); }
    const FMCSoundScene* GetScene(FName Scene) const;
    TArray<FName> GetSceneNames() const;
    bool SetScene(FName Scene, const FMCSoundScene& Settings, FString& Error);
    bool RemoveScene(FName Scene, FString& Error);

    bool HasChanges() const;
    bool HasExternalChanges() const;
    /** Saves only the existing canonical palette, only when its guard is current. */
    bool Apply(FString& Error);
    /** Discards this draft. A changed disk file still requires asset reload/reopen. */
    bool Revert(FString& Error);
    uint64 GetRevision() const { return Revision; }
    FString GetStatus() const;

    virtual void PostUndo(bool bSuccess) override;
    virtual void PostRedo(bool bSuccess) override;

private:
    FMCComposerSceneAudioSession(const FMCComposerSceneAudioSession&) = delete;
    FMCComposerSceneAudioSession& operator=(const FMCComposerSceneAudioSession&) = delete;

    bool Fail(FString& Error, const FString& Message);
    void RegisterUndo();
    void CaptureBaseline(bool bCaptureDisk);
    UMCSoundPalette* MakeDraft(UMCSoundPalette* Palette) const;
    FString ReadDiskValues(bool bForceContentHash = false) const;

    TStrongObjectPtr<UMCSoundPalette> Source;
    TStrongObjectPtr<UMCSoundPalette> Draft;
    FString SourceValuesAtOpen;
    FString SourceEventsAtOpen;
    FString DiskAtOpen;
    FString DraftValuesAtBaseline;
    mutable FString CachedDiskStat;
    mutable FString CachedDiskValues;
    FString Status;
    uint64 Revision = 0;
    bool bRegisteredForUndo = false;
    bool bApplying = false;
    bool bDiskConflictLatched = false;
};
