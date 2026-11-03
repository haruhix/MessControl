#pragma once

#include "MCUniversalUIWidget.h"
#include "MCMainMenuWidget.h"
#include "MCUnifiedUIWidget.generated.h"

class UMCGameplayHUD;
class UMCScoreboardWidget;
class UMCHubWardrobeWidget;
class UMCHubShopWidget;
class UWidgetSwitcher;

UENUM(BlueprintType)
enum class EMCUnifiedUIScreen : uint8
{
    Menu, Gameplay,
    Hub UMETA(Hidden), Wardrobe, Boosters, Tools UMETA(Hidden), Perks UMETA(Hidden),
    Settings, Team
};

/** One editable root for the PSD interface, including screen selection in Designer. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCUnifiedUIWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCUnifiedUIWidget();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Screen") EMCUnifiedUIScreen PreviewScreen=EMCUnifiedUIScreen::Menu;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Screen",meta=(ClampMin="0",ClampMax="4")) int32 PreviewSettingsTab=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Art") TSoftObjectPtr<UTexture2D> Logo;
    /** PSD tool artwork; perk icons remain owned by DT_Perks. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Art|Tools") TMap<FName,TSoftObjectPtr<UTexture2D>> ToolIconArt;
    UFUNCTION(BlueprintCallable, Category="Interface") void ShowScreen(EMCUnifiedUIScreen Screen);
    virtual void BuildPresentationTree() override;
    virtual void ApplyEditorPreview(float Time) override;
    virtual void ApplyTheme() override;
    void SetMenuBackend(UMCMainMenuWidget* Backend);
    void SetHUDBackend(UMCGameplayHUD* Backend);
    void SetTeamBackend(UMCScoreboardWidget* Backend);
    void SetWardrobeBackend(UMCHubWardrobeWidget* Backend);
    void SetShopBackend(UMCHubShopWidget* Backend);
    bool HasCommercePresentation() const { return WardrobeBackend.IsValid() || ShopBackend.IsValid(); }
    bool HasTeamPresentation() const { return TeamBackend.IsValid(); }
    EMCUnifiedUIScreen GetActiveScreen() const { return ActiveScreen; }
    bool HasMenuPresentation() const { return MenuBackend.IsValid(); }
    UWidget* GetDefaultFocusTarget() const;
protected:
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnKeyUp(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
private:
    UTexture2D* ResolveToolArt(FName Key) const;
    void EnsureGameplayControls();
    void EnsureAdditionalScreens();
    void RefreshAdaptiveLayout();
    void RefreshRuntimePresentation();
    TMap<FName,TWeakObjectPtr<UWidget>> HUDControls;
    TWeakObjectPtr<UMCMainMenuWidget> MenuBackend;
    TWeakObjectPtr<UMCGameplayHUD> HUDBackend;
    TWeakObjectPtr<UMCScoreboardWidget> TeamBackend;
    TWeakObjectPtr<UMCHubWardrobeWidget> WardrobeBackend;
    TWeakObjectPtr<UMCHubShopWidget> ShopBackend;
    float RefreshDelay=0.f;
    EMCUnifiedUIScreen ActiveScreen=EMCUnifiedUIScreen::Menu;
};

/** One existing action owner; this view never creates rooms or saves settings itself. */
UCLASS()
class MESSCONTROL_API UMCUnifiedMenuButton : public UMCUniversalUIWidget
{
    GENERATED_BODY()
public:
    UMCUnifiedMenuButton();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Action") EMCMainMenuAction Action=EMCMainMenuAction::CreateLobby;
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    void SetBackend(UMCMainMenuWidget* Menu);
private:
    UFUNCTION() void ForwardAction();
    TWeakObjectPtr<UMCMainMenuWidget> Backend;
};
