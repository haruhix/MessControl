#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "MCVFXComposition.h"
#include "MCPerkVFXProfile.h"
#include "MCPerkPreviewRunner.h"
#include "MCVFXActionCatalog.h"

class UMCVFXRecipePreset;
class UNiagaraSystem;
class UTexture2D;
class SWidget;

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
    bool CanEditProfile() const;
    FString GetEditUnavailableReason() const;
    TArray<FName> GetActionStages() const;
    FText GetEventLabel(FName Stage) const;
    FText GetActionLabel() const;
    TArray<EMCPerkPreviewScenario> GetScenarios() const;
    FText GetScenarioLabel(EMCPerkPreviewScenario Scenario) const;
    FString GetStatus() const;
    uint64 GetRevision() const;
    bool IsDirty() const;
    int32 GetLayerCount(FName Stage) const;
    /** Read-only to views; edits must pass through SetLayer/Add/Remove for identity/Undo. */
    UMCVFXRecipePreset* GetRecipe(FName Stage) const;
    FMCPerkVFXSlot GetOriginalSlot(FName Stage) const;

    FGuid AddSystem(FName Stage,UNiagaraSystem* System,FString& OutError);
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

    /** Explicit new masters -> new generated NS -> new profile -> canonical DT assignment.
     * Failed export leaves drafts and the previously assigned game profile intact.
     * Newly created intermediate assets are retained and reported instead of deleted.
     */
    bool ApplyAll(FString& OutError,const FString& AssetRoot=TEXT("/Game/Gameplay/ActiveSkills/VFX/ActionCompositions"));
    FString GetSnapshotJson() const;

    virtual void PostUndo(bool bSuccess) override;
    virtual void PostRedo(bool bSuccess) override;

private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};
