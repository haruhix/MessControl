#pragma once

#include "CoreMinimal.h"

class UMCPerkVFXProfile;
class UMCVFXRecipePreset;
class UMCVFXComposition;
class UNiagaraSystem;
struct FMCPerkVFXSlot;

DECLARE_DELEGATE_OneParam(FMCVFXRecipeApplied,const FMCPerkVFXSlot&);

/** Second-level authoring: an arbitrary composition of elements, optionally opened for one perk event. */
namespace MCVFXRecipeComposer
{
    void Register();
    void Unregister();
    void Open();
    void OpenForSlot(FName PerkID,FName Stage,UMCPerkVFXProfile* Profile,FMCVFXRecipeApplied OnApply=FMCVFXRecipeApplied());
    UMCVFXRecipePreset* GetWorkingRecipe();
    bool LoadComposition(UMCVFXComposition* Source);
    bool AddSystem(UNiagaraSystem* System);
    UMCVFXRecipePreset* SaveWorkingRecipeAs(const FString& ObjectPath,FString& OutError);
    UNiagaraSystem* ExportWorkingRecipe(const FString& LongPackageName,FString& OutError);
    bool ApplyToContext(FString& OutError);
}
