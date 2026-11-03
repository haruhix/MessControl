#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "MCVFXComposition.h"
#include "MCPerkVFXProfile.h"
#include "MCPerkPreviewRunner.h"
#include "MCVFXActionCatalog.h"
#include "MCVFXNativeSources.h"
#include "MCComposerSFXTypes.h"
#include "MCVFXActionLayout.h"

class UMCVFXRecipePreset;
class UNiagaraSystem;
class UTexture2D;
class SWidget;
class USoundBase;
class UMCVFXNativePresentationSettings;
class UMCVFXActionPack;
struct FMCVFXNativePartSettings;

enum class EMCComposerLayerLane : uint8 { VFX, SFX };
enum class EMCComposerGroupRemoval : uint8 { UngroupChildren, DeleteChildren };

/** Per-window action draft. It never uses the free composer's global WorkingRecipe. */
class FMCVFXActionSession final : public FEditorUndoClient
{
public:
    FMCVFXActionSession();
    virtual ~FMCVFXActionSession() override;
    static TArray<FMCVFXActionCatalogueEntry> GetCatalogue();
    static TArray<FName> GetStages();
    static FText GetStageLabel(FName Stage);

    /** Owns its context directly; no other editor window has to be open. */
    bool Initialize(FName ActionID,FString& OutError);
    void Clear();
    FName GetActionID() const;
    bool IsContextCurrent() const;
    bool CanPreview() const;
    FString GetPreviewUnavailableReason() const;
    bool CanEditProfile() const;
    FString GetEditUnavailableReason() const;
    TArray<FName> GetActionStages() const;
    FText GetEventLabel(FName Stage) const;
    FText GetActionLabel() const;
    FSoftObjectPath GetCanonicalOwner() const;
    TArray<EMCPerkPreviewScenario> GetScenarios() const;
    FText GetScenarioLabel(EMCPerkPreviewScenario Scenario) const;
    FString GetStatus() const;
    uint64 GetRevision() const;
    uint64 GetPlaybackRevision() const;
    uint64 GetLayoutRevision() const;
    FGuid GetDraftToken() const;
    /** Runtime values and editor layout have independent baselines. */
    bool IsRuntimeDirty() const;
    bool IsMasterLayoutDirty() const;
    bool IsDirty() const;
    const FMCEventPresentationGroupSet* GetEventGroups(FName Event) const;
    FGuid CreateEventGroup(FGuid Token,FName Event,FName Name,FString& OutError);
    bool RenameEventGroup(FGuid Token,FName Event,FGuid Group,FName Name,FString& OutError);
    bool SetEventGroupEnabled(FGuid Token,FName Event,FGuid Group,bool bEnabled,FString& OutError);
    bool SetEventGroupAudioMuted(FGuid Token,FName Event,FGuid Group,bool bMuted,FString& OutError);
    FGuid DuplicateEventGroup(FGuid Token,FName Event,FGuid Group,FString& OutError);
    bool AssignLayerToGroup(FGuid Token,FName Event,EMCComposerLayerLane Lane,FGuid Layer,FGuid Group,FString& OutError);
    bool RemoveEventGroup(FGuid Token,FName Event,FGuid Group,EMCComposerGroupRemoval Policy,FString& OutError);
    /** Preview-only filter. It restarts no mechanic until the ordinary Play command. */
    bool SetPreviewSoloGroups(FGuid Token,FName Event,const TArray<FGuid>& Groups,FString& OutError);
    TArray<FGuid> GetPreviewSoloGroups(FName Event) const;
    void ClearPreviewSolo();
    FMCVFXActionLayout GetLayoutSnapshot() const;
    bool SetLayoutSnapshot(FGuid Token,const FMCVFXActionLayout& Layout,FString& OutError);
    bool GetActionHubPosition(FVector2D& OutPosition) const;
    bool SetActionHubPosition(FGuid Token,FVector2D Position,FString& OutError);
    bool GetEventCardPosition(FName Event,FVector2D& OutPosition) const;
    bool SetEventCardPosition(FGuid Token,FName Event,FVector2D Position,FString& OutError);
    bool GetEventGroupLayout(FName Event,FGuid Group,FMCComposerGroupLayout& OutLayout) const;
    bool SetEventGroupLayout(FGuid Token,FName Event,FGuid Group,const FMCComposerGroupLayout& Layout,FString& OutError);
    bool GetLayerGraphPosition(FName Event,EMCComposerLayerLane Lane,FGuid Layer,FVector2D& OutPosition) const;
    bool SetLayerGraphPosition(FGuid Token,FName Event,EMCComposerLayerLane Lane,FGuid Layer,FVector2D Position,FString& OutError);
    /** Successful master/pack saves checkpoint layout only, never the canonical runtime baseline. */
    void MarkMasterLayoutSaved();
    int32 GetLayerCount(FName Stage) const;
    /** Read-only to views; edits must pass through SetLayer/Add/Remove for identity/Undo. */
    UMCVFXRecipePreset* GetRecipe(FName Stage) const;
    FMCPerkVFXSlot GetOriginalSlot(FName Stage) const;
    TArray<FMCVFXNativeSourcePart> GetNativeSources(FName Stage) const;
    EMCPerkVFXSlotMode GetPreviewSlotMode(FName Stage) const;
    /** Working native artwork, never the shared saved source. Null means no renderer contract. */
    const UMCVFXNativePresentationSettings* GetNativeDraft() const;
    FGuid GetNativeDraftToken() const;
    bool HasNativeOverride() const;
    bool SetNativePart(FGuid Token,bool bPersistent,FName Event,FName PartID,const FMCVFXNativePartSettings& Part,FString& OutError);
    bool ResetNativePart(FGuid Token,bool bPersistent,FName Event,FName PartID,FString& OutError);
    bool SetNativeKeepWithComposition(FGuid Token,FName Event,bool bKeep,FString& OutError);
    const FMCEventSFXSlot* GetSFXSlot(FName Event) const;
    FGuid GetSFXDraftToken() const;
    FMCComposerSFXCapabilities GetSFXCapability(FName Event) const;
    FGuid AddSFXLayer(FGuid Token,FName Event,USoundBase* Sound,FGuid GroupID,FString& OutError);
    bool SetSFXLayer(FGuid Token,FName Event,FGuid LayerID,const FMCEventSFXLayer& Layer,FString& OutError);
    bool RemoveSFXLayer(FGuid Token,FName Event,FGuid LayerID,FString& OutError);
    FGuid DuplicateSFXLayer(FGuid Token,FName Event,FGuid LayerID,FString& OutError);
    bool SetSFXMode(FGuid Token,FName Event,EMCEventSFXMode Mode,FString& OutError);
    bool AuditionSFX(FName Event,USoundBase* Sound,FString& OutError,EMCPerkPreviewScenario Scenario=EMCPerkPreviewScenario::Target);
    bool SetPreviewAudioEnabled(bool bEnabled,FString& OutError);
    bool IsPreviewAudioEnabled() const;
    FString GetPreviewAudioStatus() const;

    FGuid AddSystem(FName Stage,UNiagaraSystem* System,FString& OutError);
    FGuid AddSystemToGroup(FGuid Token,FName Event,UNiagaraSystem* System,FGuid Group,FString& OutError);
    bool SetLayer(FName Stage,const FGuid& ID,const FMCVFXCompositionLayer& Layer,FString& OutError);
    bool RemoveLayer(FName Stage,const FGuid& ID,FString& OutError);
    FGuid DuplicateLayer(FName Stage,const FGuid& ID,FString& OutError);
    bool SetLayerEnabled(FName Stage,const FGuid& ID,bool bEnabled,FString& OutError);
    /** Plus creates a new empty compound inside this event; it does not add a gameplay action. */
    bool NewCompound(FName Stage,FString& OutError);
    bool ImportRecipe(FName Stage,const UMCVFXRecipePreset* Source,FString& OutError);

    /** Temporary extra gallery element over the stage's current draft, never committed. */
    bool AuditionNS(FName Stage,UNiagaraSystem* System,FString& OutError,EMCPerkPreviewScenario Scenario=EMCPerkPreviewScenario::Target);
    bool PlayFullAction(FString& OutError,EMCPerkPreviewScenario Scenario=EMCPerkPreviewScenario::Target);
    void Pause(bool bPaused);
    void Stop();
    TSharedRef<SWidget> MakePreviewWidget();
    void SetPreviewView(int32 Preset);

    /** Explicit new masters -> independent runtime compositions -> new profile -> canonical owner assignment.
     * Failed preparation leaves drafts and the previously assigned game profile intact.
     * Newly created intermediate assets are retained and reported instead of deleted.
     */
    bool ApplyAll(FString& OutError,const FString& AssetRoot=TEXT("/Game/Gameplay/ActiveSkills/VFX/ActionCompositions"));
    // Portable-pack integration is owned by the same Session writer.
    UMCVFXActionPack* SavePackNew(const FText& Label,FString& OutError,const FString& AssetRoot=TEXT("/Game/Gameplay/VFX/Packs"));
    bool LoadPack(const UMCVFXActionPack* Pack,FString& OutError,bool bDiscardDraft=false);
    bool ApplySavedPack(const UMCVFXActionPack* Pack,FString& OutError);
    FSoftObjectPath GetLoadedPackPath() const;
    bool HasUnsavedPackChanges() const;
    FString GetSnapshotJson() const;

    virtual void PostUndo(bool bSuccess) override;
    virtual void PostRedo(bool bSuccess) override;

private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};
