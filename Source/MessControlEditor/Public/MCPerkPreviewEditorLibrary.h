#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCPerkPreviewEditorLibrary.generated.h"

class UMCPerkVFXProfile;
class UMCVFXComposition;
class UNiagaraSystem;
class UNiagaraScript;
class UNiagaraComponent;

/** Inspection/control of the visible editor tool, not a second hidden gameplay fixture. */
UCLASS()
class MESSCONTROLEDITOR_API UMCPerkPreviewEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Typed UE-version-safe authoring; no opaque registered-type indices or implicit saves. */
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Authoring")
    static bool SetMeshRendererUserBinding(UNiagaraSystem* System,FName EmitterName,int32 RendererIndex,int32 MeshIndex,FName UserName,FString& OutError);
    /** Request the current graph once after authoring. True means accepted, not compiled; poll the normal compile/stack gates separately. No save or editor window. */
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Authoring")
    static bool RequestNiagaraSystemCompile(UNiagaraSystem* System,FString& OutError);
    /** Declare an exact gameplay-owned Float/Vector/Position array with a system-owned data interface. Idempotent, never saves or replaces an existing parameter. */
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Authoring")
    static bool AddNativeArrayUserParameter(UNiagaraSystem* System,FName UserName,FName ElementType,FString& OutError);
    /** At most 16 typed input edits on one explicitly owned project draft, one edit context/transaction. JSON is the installed toolset's normal inputItemRef/inputData shape. No save/compile; failure may leave an applied prefix, reported with actual readbacks. */
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Authoring")
    static FString SetNiagaraStackInputBatch(UNiagaraSystem* System,const FString& OperationsJson,FName OwnerMetadataKey,const FString& ExpectedOwnerValue);
    /** Read-only JSON of the requested editor version (or exposed version), actual graph nodes, typed pins and links. No load, edit, compile, save or window. */
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Authoring")
    static FString DumpNiagaraScriptGraph(UNiagaraScript* Script,FString& OutError);
    /** Read-only selected handle-version scripts, native handler/stage counts and renderer/template classes, including disabled emitters. No load/compile/mutation; counts do not certify audio absence. */
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Authoring")
    static FString GetNiagaraAudioInspection(UNiagaraSystem* System,FString& OutError);
    /** Bounded read-only component override-store JSON. False bool, missing, null objects and unsupported values are distinct; retained capture is checked only against its real player. DI references do not certify live array/VM contents. */
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Inspection")
    static FString GetNiagaraComponentParameterInspection(const UNiagaraComponent* Component,const TArray<FName>& UserNames,FString& OutError);
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
    /** Generic canonical writer; source identity may be a perk or a registered non-perk presentation. */
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Preview") static FString AssignSavedProfileToSource(FName SourceID,UMCPerkVFXProfile* Profile);
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Preview") static FString GetPerkPreviewSnapshotJson();
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Composer") static void OpenVFXComposer();
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Composer") static void OpenVFXElements();
    /** One read-only catalogue snapshot for grouped review. No workspace, scenarios, source edits or NS loads. Canonical profile inspection is optional and does not certify coverage or runtime behavior. */
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Inspection") static FString GetVFXComposerCatalogueJson(bool bIncludeCanonical=false);
    UFUNCTION(BlueprintPure,Category="MessControl|VFX Composer") static FString GetActionWorkspaceSnapshotJson();
    /** Select/reopen the real shared workspace; never discards an unapplied draft. */
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Composer") static bool SelectVFXComposerAction(FName ActionID,FString& OutError);
    /** Same public-workspace Play/Stop controls used by the buttons, for bounded MCP review. No saves. */
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Composer") static bool PlayVFXComposerAction(const FString& Scenario,bool bLoop,FString& OutError);
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Composer") static void StopVFXComposerAction();
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Composer") static bool SetVFXComposerPreviewView(int32 Preset);
    /** Bounded native review helper; does not resize the user's main Editor window. */
    UFUNCTION(BlueprintCallable,Category="MessControl|VFX Composer") static bool ResizeVFXComposerForReview(FVector2D ClientSize);
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
