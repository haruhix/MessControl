#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCPerkPreviewEditorLibrary.generated.h"

class UMCPerkVFXProfile;
class UMCVFXComposition;
class UNiagaraSystem;

/** Inspection/control of the visible editor tool, not a second hidden gameplay fixture. */
UCLASS()
class MESSCONTROLEDITOR_API UMCPerkPreviewEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Preview") static void OpenPerkPreview();
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Preview") static bool RunRocketScenario(FName Scenario);
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Preview") static bool RunActionScenario(FName ActionID,FName Scenario);
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Preview") static void ResetPerkPreview();
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Preview") static void PausePerkPreview(bool bPaused);
    UFUNCTION(BlueprintPure,Category="MessControl|VFX Preview") static UMCPerkVFXProfile* GetWorkingVFXProfile();
    /** Shared writer for the native Save dialog and MCP authoring; creates a new asset only. */
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Preview") static UMCPerkVFXProfile* SaveWorkingProfileAs(const FString& ObjectPath,FString& OutError);
    /** Explicit authoring command. Saves only the canonical table, refuses dirty source packages. */
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Preview") static FString AssignSavedProfileToRocket(UMCPerkVFXProfile* Profile);
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Preview") static FString AssignSavedProfileToPerk(FName PerkID,UMCPerkVFXProfile* Profile);
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Preview") static FString GetPerkPreviewSnapshotJson();
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Composer") static void OpenVFXComposer();
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Composer") static void OpenVFXElements();
    UFUNCTION(BlueprintPure,Category="MessControl|VFX Composer") static FString GetActionWorkspaceSnapshotJson();
    /** Same transactional event import as the visible workspace; never assigns a runtime profile. */
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Composer") static bool ImportActionEventRecipe(FName ActionID,FName Stage,UMCVFXComposition* Source);
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Composer") static bool OpenComposerForRocketStage(FName Stage);
    UFUNCTION(BlueprintPure,Category="MessControl|VFX Composer") static UMCVFXComposition* GetWorkingVFXRecipe();
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Composer") static bool ComposerAddSystem(UNiagaraSystem* System);
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Composer") static bool ComposerLoadRecipe(UMCVFXComposition* Source);
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Composer") static UMCVFXComposition* SaveWorkingVFXRecipeAs(const FString& ObjectPath,FString& OutError);
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Composer") static UNiagaraSystem* ExportWorkingVFXRecipe(const FString& PackageName,FString& OutError);
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Composer") static bool ComposerApplyToContext(FString& OutError);
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Composer") static FString GetVFXComposerSnapshotJson();
};
