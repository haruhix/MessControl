#pragma once

#include "CoreMinimal.h"

class FMCPerkPreviewRunner;
class UMCPerkVFXProfile;
class UNiagaraSystem;
class SWidget;
struct FMCPerkVFXSlot;
enum class EMCPerkPreviewScenario : uint8;

/** Project editor tool. The visible panel and diagnostic bridge share the same runner. */
namespace MCPerkActionPreviewPanel
{
    void Register();
    void Unregister();
    void Open();
    /** Creates only a shared context/runner; never opens a legacy panel or tab. */
    bool EnsureContext(FName PerkID,FString& OutError);
    FName GetContextPerkID();
    TSharedPtr<FMCPerkPreviewRunner> GetActiveRunner();
    UMCPerkVFXProfile* GetWorkingProfile();
    /** Changes only the visible working copy; never writes the table or saved profile. */
    bool SetWorkingSlot(FName Stage,const FMCPerkVFXSlot& Slot,FString& OutError);
    UNiagaraSystem* ResolveWorkingStageSystem(FName Stage);
    /** Audition a slot snapshot without modifying the visible working profile. */
    bool AuditionWorkingSlot(FName Stage,const FMCPerkVFXSlot& Slot,FString& OutError);
    /** Optional local camera callback belongs only to this returned viewport. */
    TSharedRef<SWidget> MakeSharedActionViewport(TFunction<void(int32)>* OutSetView=nullptr);
    bool RunScenario(EMCPerkPreviewScenario Scenario);
    void ResetPreview();
}
