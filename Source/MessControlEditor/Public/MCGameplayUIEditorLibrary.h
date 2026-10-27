#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCGameplayUIEditorLibrary.generated.h"

class UMCGameplayUITheme;
class UMCGameplayUIWidget;
class UWidgetBlueprint;
class UFont;
class UFontFace;
class UWorld;
class AMCThroatVortex;
class AMCFoodActor;
class AMCTongue;
class AMCNutRainEvent;
class AMCNutBoss;
class UMCMainMenuWidget;
class APlayerController;
class AMCPlayerController;

/** Explicit editor authoring for the approved UI; existing artist trees are preserved unless a narrow repair is requested. */
UCLASS()
class MESSCONTROLEDITOR_API UMCGameplayUIEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Save only the specified live PIE world's game window, including HUD, to an E: PNG. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Gameplay UI|Editor")
    static bool CapturePIEGameWindow(UWorld* World, const FString& OutputFile);

    /** Owned PIE UI fixture: invokes the same controller handler bound to Escape/Menu, without the Editor StopPIE shortcut. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Gameplay UI|Editor")
    static bool TogglePIEPauseMenu(UWorld* World);

    /** Read-only live PIE view state for QA. This editor-only API never exposes gameplay mutators or changes property visibility. */
    UFUNCTION(BlueprintPure, Category="Mess Control|Gameplay UI|Editor")
    static FString InspectPlayerView(APlayerController* PlayerController);

    /** Read-only QA telemetry for this live PIE world's vortex; never changes gameplay or property visibility. */
    UFUNCTION(BlueprintPure, Category="Mess Control|Gameplay UI|Editor")
    static bool ReadPIEThroatVortex(UWorld* World, AMCThroatVortex* Vortex,
        TArray<AMCFoodActor*>& OutSources, double& OutStartedAt, float& OutDuration);

    /** Owned standalone-PIE summon fixture; native configuration/entrance, no health or phase overrides. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Gameplay UI|Editor")
    static AMCNutBoss* SpawnPIEMageForSummon(UWorld* World, AMCTongue* Tongue,
        AMCNutRainEvent* Event, FVector NearFloor, int32 Seed=137);

    /** Explicit standalone-PIE initial-stage fixture for recording coffee -> interval -> food.
     * Requires -MCHubTransientProfile and a ready living local player. Reuses the
     * current native run configuration; never claims a played/completed nut fight.
     */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Gameplay UI|Editor")
    static bool StartPostNutRelaxPreview(AMCPlayerController* PlayerController);

    /** Idempotent entry point after Live Coding; refreshes open gameplay widget Details. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Gameplay UI|Editor")
    static void EnsureGameplayUIPreviewRegistered();

    UFUNCTION(BlueprintCallable, Category="Mess Control|Gameplay UI|Editor")
    static void OpenGameplayUITheme(UMCGameplayUITheme* Theme = nullptr);

    /** Open editor-only controls for the actual MainMenu role and its scene overlay; never starts PIE. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Main Menu Preview|Editor")
    static void OpenMainMenuPreview();

    UFUNCTION(BlueprintCallable, Category="Mess Control|Main Menu Preview|Editor")
    static void CloseMainMenuPreview();

    UFUNCTION(BlueprintCallable, Category="Mess Control|Main Menu Preview|Editor")
    static void RefreshMainMenuPreview();

    /** Changes only the transient editor sample, preserving authored defaults and the player's culture. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Main Menu Preview|Editor")
    static void SetMainMenuPreviewState(FName State, const FString& Culture=TEXT("ru"));

    UFUNCTION(BlueprintPure, Category="Mess Control|Main Menu Preview|Editor")
    static UMCMainMenuWidget* GetMainMenuPreviewWidget();

    UFUNCTION(BlueprintPure, Category="Mess Control|Main Menu Preview|Editor")
    static bool IsMainMenuPreviewActive();

    /** Saves a new editable tree and parameter-preserving material snapshots; existing assets are never replaced. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Gameplay UI|Editor")
    static UWidgetBlueprint* CreateEditableGameplayWidget(const FString& AssetPath, TSubclassOf<UMCGameplayUIWidget> ParentClass);

    /** One-time migration of the task-owned wardrobe RGB tree to the editable color palette. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Gameplay UI|Editor")
    static bool RefreshHubWardrobePalettePresentation(UWidgetBlueprint* Blueprint);

    /** Migrate only the known empty shop grid, preserving the saved surrounding Designer tree. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Gameplay UI|Editor")
    static bool RefreshHubShopCatalogPresentation(UWidgetBlueprint* Blueprint);

    /** Append only missing named HUD controls, preserving the saved Designer layout and graph. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Gameplay UI|Editor")
    static bool AddMissingGameplayHUDPresentation(UWidgetBlueprint* Blueprint);

    /** Repair only bare-material brushes from today's WBP_Hub migration; preserves artist instances/layout. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Gameplay UI|Editor")
    static bool RepairEditableHubMaterialSnapshots(UWidgetBlueprint* Blueprint);

    /** Today's generated WBP_MainMenu only: replace Settings children, retaining its page/slot and all other pages. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Gameplay UI|Editor")
    static bool RefreshMainMenuSettingsPresentation(UWidgetBlueprint* Blueprint);

    /** Append only the Voice tab/section; retain all saved menu pages, controls and artist material instances. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Gameplay UI|Editor")
    static bool AddMainMenuVoicePresentation(UWidgetBlueprint* Blueprint);

    /** Replace only native menu CreateWidget class pins in the existing 3D front end. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Gameplay UI|Editor")
    static bool UseAuthoredMainMenu(UWidgetBlueprint* Host, TSubclassOf<UMCMainMenuWidget> MenuClass);

    /** Explicit-culture formatter checks; never changes editor culture, creates actors or saves assets. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Gameplay UI|Editor")
    static bool ValidateGameplayUILocalization(TArray<FString>& OutReport);

    /** Draw the actual editable Slate tree to PNG on E: without PIE or gameplay actors. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Gameplay UI|Editor")
    static bool RenderGameplayUIPreview(TSubclassOf<UMCGameplayUIWidget> WidgetClass, const FString& OutputFile,
        FIntPoint DrawSize, const FString& State, const FString& Culture, float Time=0.f, float RenderScale=1.f);

    /** Adds the task-owned CJK sub-typeface; preserves default Latin/Cyrillic and other sub-typefaces. Does not save. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Gameplay UI|Editor")
    static bool ConfigureBossNameCJKFallback(UFont* NameFont, UFontFace* FallbackFace);
};
