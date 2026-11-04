#include "MCUIPresentationSubsystem.h"

#include "MCUIPresentationPanel.h"
#include "MCUnifiedUIWidget.h"
#include "MCFishingRewardWidget.h"
#include "MCTutorialWidget.h"
#include "MCEmoteWidget.h"
#include "MCWorldLabelWidget.h"
#include "MCWorldUIShapes.h"
#include "MCUIDataLibrary.h"
#include "MCUlcerProgressWidget.h"
#include "MCFreezeBarWidget.h"
#include "MCPlayerNameComponent.h"
#include "MCDeliveryZoneCaptionWidget.h"
#include "MCHubInteractionPromptWidget.h"
#include "Engine/DataTable.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"

#include "MCUnifiedTutorialWidget.h"
#include "MCHubWardrobeWidget.h"
#include "MCHubShopWidget.h"
#include "MCHubWidget.h"
#include "MCPerkChoiceWidget.h"
#include "MCGameplayMenuWidget.h"
#include "Engine/World.h"
#include "MCGameplayHUD.h"
#include "MCScoreboardWidget.h"
#include "MCGameplayUIFactory.h"
#include "MCGameplayUITheme.h"
#include "MCMainMenuPlayerController.h"
#include "MCPlayerController.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "UObject/UObjectIterator.h"


namespace MCWorldUIWarmupPrivate
{
    const UMCGameplayUIWidget* ResidentSourceDefaults(const UMCGameplayUITheme* CurrentTheme,FName Role,UClass* NativeFallback)
    {
        if(!CurrentTheme) return nullptr;
        UClass* SourceClass=NativeFallback;
        if(const auto* Entry=CurrentTheme->WidgetClasses.Find(Role);Entry && !Entry->IsNull()) SourceClass=Entry->Get();
        if(!SourceClass || !SourceClass->IsChildOf(NativeFallback)
            || SourceClass->HasAnyClassFlags(CLASS_Abstract|CLASS_Deprecated|CLASS_NewerVersionExists)) return nullptr;
        // Loading-barrier only: create resident UI class defaults, never a
        // live source widget or gameplay actor. Native fallback CDOs may be cold.
        return Cast<UMCGameplayUIWidget>(SourceClass->GetDefaultObject());
    }
    UDataTable* ResidentSourceCopyTable(const UMCGameplayUIWidget* Defaults,const UMCGameplayUITheme* CurrentTheme)
    {
        if(!Defaults) return nullptr;
        if(IsValid(Defaults->TranslationTable.Get())) return Defaults->TranslationTable.Get();
        const auto* SourceTheme=IsValid(Defaults->Theme.Get())?Defaults->Theme.Get():CurrentTheme;
        return SourceTheme?SourceTheme->LocalizationTable.Get():nullptr;
    }
    bool PrimeKnownWorldCaptions(const UMCGameplayUITheme* NewTheme,const UMCGameplayUITheme* CurrentTheme,float FontScale)
    {
        if(!NewTheme || !CurrentTheme) return false;
        const FString Culture=FInternationalization::Get().GetCurrentLanguage()->GetName();
        struct FKnownCopy {FName Key;FText Fallback;};
        struct FSourceCopy {FName Role;UClass* NativeClass;int32 FontSize;TArray<FKnownCopy> Entries;};
        const FSourceCopy Sources[]={
            {TEXT("UlcerProgress"),UMCUlcerProgressWidget::StaticClass(),16,
                {{TEXT("UI.World.ProgressPercent"),NSLOCTEXT("MCUlcerProgress","Percent","{Value}%")},{TEXT("UI.World.PullKey"),NSLOCTEXT("MCUlcerProgress","PullKey","E")},{TEXT("UI.World.PickaxeKey"),NSLOCTEXT("MCUlcerProgress","PickaxeKey","2")},{TEXT("UI.World.SprayKey"),NSLOCTEXT("MCUlcerProgress","SprayKey","4")}}},
            {TEXT("FreezeBar"),UMCFreezeBarWidget::StaticClass(),16,
                {{TEXT("UI.Status.FreezeValue"),NSLOCTEXT("MCFreezeBar","Frozen","Заморозка {Percent}")},{TEXT("UI.Status.FreezeWarmingValue"),NSLOCTEXT("MCFreezeBar","Warming","Заморозка {Percent} · Отогрев")}}},
            {TEXT("PlayerName"),UMCPlayerNameWidget::StaticClass(),18,
                {{TEXT("UI.PlayerAlarm.Help"),NSLOCTEXT("MCPlayerName","Help","+ НУЖНА ПОМОЩЬ")},{TEXT("UI.PlayerAlarm.Attention"),NSLOCTEXT("MCPlayerName","Attention","! ВНИМАНИЕ")}}},
            {TEXT("DeliveryCaption"),UMCDeliveryZoneCaptionWidget::StaticClass(),18,
                {{TEXT("UI.Delivery.EntryTitle"),NSLOCTEXT("MCDeliveryZones","EntryTitle","ВХОД")},{TEXT("UI.Delivery.ExitTitle"),NSLOCTEXT("MCDeliveryZones","ExitTitle","ВЫХОД")}}},
            {TEXT("DeliveryCaption"),UMCDeliveryZoneCaptionWidget::StaticClass(),16,
                {{TEXT("UI.Delivery.SortingError"),NSLOCTEXT("MCDeliveryZones","SortingError","ОШИБКА СОРТИРОВКИ")},{TEXT("UI.Delivery.EntryDescription"),NSLOCTEXT("MCDeliveryZones","EntryDescription","СВЕЖАЯ ЕДА")},{TEXT("UI.Delivery.ExitDescription"),NSLOCTEXT("MCDeliveryZones","ExitDescription","МУСОР · ИСПОРЧЕННОЕ · ЩЁТКИ")}}}
        };
        for(const auto& Source:Sources) {
            const auto* Defaults=ResidentSourceDefaults(CurrentTheme,Source.Role,Source.NativeClass);
            UDataTable* CopyTable=ResidentSourceCopyTable(Defaults,CurrentTheme);
            const auto* SourceTheme=Defaults && IsValid(Defaults->Theme.Get())?Defaults->Theme.Get():CurrentTheme;
            if(!Defaults || (!CopyTable && (Defaults->TranslationTable || (SourceTheme && !SourceTheme->LocalizationTable.IsNull())))) return false;
            TArray<FText> Corpus;
            for(const auto& Entry:Source.Entries) {
                // Mirror actual source key+native FText identity/fallback; missing copy rows do not add a new loading failure.
                Corpus.Add(UMCUIDataLibrary::ResolveText(CopyTable,Entry.Key,Entry.Fallback,Culture));
            }
            for(int32 Digit=0;Digit<10;++Digit) Corpus.Add(UMCUIDataLibrary::NumberForCulture(Digit,Culture));
            // New World styles all use MakeFont(size,true), independently of the Current source font family.
            if(!MCWorldUIShapes::PrimeText(NewTheme->MakeFont(Source.FontSize,true),Corpus,FontScale)) return false;
        }
        // These actual New variants contain fixed UI glyphs rather than additional table rows.
        if(!MCWorldUIShapes::PrimeText(NewTheme->MakeFont(31,true),{FText::AsCultureInvariant(TEXT("\u2191"))},FontScale)
            || !MCWorldUIShapes::PrimeText(NewTheme->MakeFont(24,true),{FText::AsCultureInvariant(TEXT("\u00AB\u00BB"))},FontScale)
            || !MCWorldUIShapes::PrimeText(NewTheme->MakeFont(19,true),{},FontScale)) return false;
        const auto* Prompt=Cast<UMCHubInteractionPromptWidget>(ResidentSourceDefaults(CurrentTheme,TEXT("HubInteractionPrompt"),UMCHubInteractionPromptWidget::StaticClass()));
        return Prompt && MCWorldUIShapes::PrimeText(NewTheme->MakeFont(36,true),{Prompt->KeyLabel},FontScale);
    }
}

namespace
{
    UMCTutorialWidget* MCOverlayIntegration_FindTutorial(APlayerController* Controller)
    {
        UMCTutorialWidget* Found=nullptr;
        for(TObjectIterator<UMCTutorialWidget> It;It;++It) {
            if(It->IsA<UMCUnifiedTutorialWidget>() || It->GetOwningPlayer()!=Controller || It->GetWorld()!=Controller->GetWorld() || !It->IsInViewport()) continue;
            if(Found) return nullptr; // Ambiguous owner evidence: keep the original source UI.
            Found=*It;
        }
        return Found;
    }
#if !UE_BUILD_SHIPPING
    class FMCUIPresentationInput final : public IInputProcessor
    {
    public:
        explicit FMCUIPresentationInput(UMCUIPresentationSubsystem* InOwner):Owner(InOwner) {}
        virtual void Tick(float,FSlateApplication&,TSharedRef<ICursor>) override {if(Owner.IsValid()) Owner->CheckOwner();}
        virtual bool HandleKeyDownEvent(FSlateApplication&,const FKeyEvent& Event) override
        {
            if(Event.GetKey()==EKeys::Escape) {
                if(bOwnsBackPress) return true;
                auto* Instance=Owner.IsValid()?Owner->GetGameInstance():nullptr;
                auto* Player=Instance?Instance->GetFirstLocalPlayerController():nullptr;
                if(Owner.IsValid() && Owner->IsPanelOpenFor(Player)) {
                    if(!Event.IsRepeat()) {bOwnsBackPress=true;Owner->ClosePanel();}
                    return true;
                }
            }
            if(Event.GetKey()!=EKeys::F3 || !Owner.IsValid()) return false;
            if(Owner->IsCapturingSettingsKey()) return false;
            if(Event.IsRepeat()) return bOwnsPress;
            bOwnsPress=Owner->HandleF3();return bOwnsPress;
        }
        virtual bool HandleKeyUpEvent(FSlateApplication&,const FKeyEvent& Event) override
        {
            if(Event.GetKey()==EKeys::Escape) {const bool Handled=bOwnsBackPress;bOwnsBackPress=false;return Handled;}
            if(Event.GetKey()!=EKeys::F3) return false;
            const bool Handled=bOwnsPress;bOwnsPress=false;return Handled;
        }
        virtual const TCHAR* GetDebugName() const override {return TEXT("MessControl UI presentation F3");}
    private:
        TWeakObjectPtr<UMCUIPresentationSubsystem> Owner;
        bool bOwnsPress=false,bOwnsBackPress=false;
    };
#endif
}

bool UMCUIPresentationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
    return false;
#else
    return !IsRunningCommandlet() && !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
#endif
}

void UMCUIPresentationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    PresentationHandle=UMCGameplayUIFactory::OnPresentationChanged().AddUObject(this,&ThisClass::RefreshInterface);
#if !UE_BUILD_SHIPPING
    if(FSlateApplication::IsInitialized())
    {
        InputProcessor=MakeShared<FMCUIPresentationInput>(this);
        FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor,0);
    }
#endif
}

void UMCUIPresentationSubsystem::Deinitialize()
{
    UMCGameplayUIFactory::OnPresentationChanged().Remove(PresentationHandle);PresentationHandle.Reset();
    if(InputProcessor && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
    InputProcessor.Reset();ClosePanel(false);RestoreLegacy();
    if(Interface) Interface->RemoveFromParent();Interface=nullptr;PreparedWorldLabelSlate.Reset();PreparedWorldLabelView=nullptr;InterfaceOwner.Reset();
    Super::Deinitialize();
}

bool UMCUIPresentationSubsystem::IsPanelOpenFor(const APlayerController* Controller) const
{
    return Controller && Panel && Panel->IsInViewport() && PanelOwner.Get()==Controller;
}

void UMCUIPresentationSubsystem::CheckOwner()
{
    RefreshInterface();
    if(!Panel) return;
    if(!PanelOwner.IsValid() || PanelOwner->GetWorld()!=GetWorld() || PanelOwner->GetGameInstance()!=GetGameInstance() || !Panel->IsInViewport())
    {ClosePanel(false);return;}
    if(const auto* GamePC=Cast<AMCPlayerController>(PanelOwner.Get());GamePC && !GamePC->CanOpenUIPresentationTools()) ClosePanel();
}

bool UMCUIPresentationSubsystem::IsCapturingSettingsKey() const
{
    auto* GI=GetGameInstance();auto* PC=GI?GI->GetFirstLocalPlayerController():nullptr;
    if(!PC) return false;
    for(TObjectIterator<UMCGameplayMenuWidget> It;It;++It)
        if(It->GetOwningPlayer()==PC && It->GetWorld()==GetWorld() && It->IsInViewport() && It->IsVisible() && It->IsPresentationKeyCaptureActive()) return true;
    for(TObjectIterator<UMCMainMenuWidget> It;It;++It)
        if(It->GetOwningPlayer()==PC && It->GetWorld()==GetWorld()
            && It->GetCurrentMenuPage()==EMCMainMenuPage::Settings && It->GetActiveSettingsTab()==4
            && It->IsSettingsVoiceRebinding()) return true;
    return false;
}

bool UMCUIPresentationSubsystem::HandleF3()
{
#if UE_BUILD_SHIPPING
    return false;
#else
    auto* GI=GetGameInstance();
    auto* Viewport=GI?GI->GetGameViewportClient():nullptr;
    auto ViewportWidget=Viewport?Viewport->GetGameViewportWidget():nullptr;
    // Embedded PIE must not capture an editor shortcut while Details/Designer has focus.
    if(!ViewportWidget || !ViewportWidget->HasAnyUserFocusOrFocusedDescendants()) return false;
    auto* PC=GI->GetFirstLocalPlayerController();
    if(!IsValid(PC) || PC->GetWorld()!=GetWorld() || !PC->IsLocalController()) return false;
    if(Panel) {ClosePanel();return true;}
    if(auto* GamePC=Cast<AMCPlayerController>(PC))
    {
        if(!GamePC->CanOpenUIPresentationTools()) return true;
        if(GamePC->CanUseRegularF3Panel()) {GamePC->ToggleDevPanel();return true;}
    }
    else if(auto* MenuPC=Cast<AMCMainMenuPlayerController>(PC))
    {
        if(!MenuPC->CanOpenUIPresentationTools()) return true;
    }
    else return false;
    Panel=CreateWidget<UMCUIPresentationPanelWidget>(PC,UMCUIPresentationPanelWidget::StaticClass());
    if(!Panel) return false;
    // Existing front-end/modals use the global viewport layer. A player-layer
    // overlay would paint and hit-test underneath those screens regardless of its ZOrder.
    PanelOwner=PC;Panel->AddToViewport(30000);
    if(auto* GamePC=Cast<AMCPlayerController>(PC)) GamePC->UpdateInputMode();
    else ApplyPanelInputMode(PC);
    return true;
#endif
}

bool UMCUIPresentationSubsystem::ApplyPanelInputMode(APlayerController* PC)
{
    if(!IsPanelOpenFor(PC))
    {
        if(Interface && Interface->IsVisible() && InterfaceOwner.Get()==PC
            && UMCGameplayUIFactory::GetPresentationVersion()==EMCUIVersion::New)
        {
            auto* OverlayPC=Cast<AMCPlayerController>(PC);
            const bool Emotes=OverlayPC && OverlayPC->CanOpenUIPresentationTools() && Interface->GetActiveScreen()==EMCUnifiedUIScreen::Emotes && Interface->HasEmotePresentation();
            if(Emotes) {
                // Same source GameAndUI semantics, including a real held-T release. No source Begin/Confirm/Close calls.
                Interface->SetEmoteFocusLease(true);
                if(auto* Hero=Cast<AMCToothCharacter>(PC->GetPawn())) Hero->CancelGameplayInput();
                PC->ResetIgnoreMoveInput();PC->SetIgnoreMoveInput(true);PC->ResetIgnoreLookInput();PC->SetIgnoreLookInput(true);
                auto* Focus=Interface->GetDefaultFocusTarget();
                FInputModeGameAndUI Mode;Mode.SetWidgetToFocus(Focus?Focus->TakeWidget():Interface->TakeWidget());
                Mode.SetHideCursorDuringCapture(false);Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);PC->SetInputMode(Mode);PC->SetShowMouseCursor(true);return true;
            }
            const bool TutorialMenu=OverlayPC && OverlayPC->CanOpenUIPresentationTools() && Interface->RequiresTutorialMenuInput();
            const bool Menu=Interface->HasMenuPresentation();
            auto* GamePC=Cast<AMCPlayerController>(PC);
            const bool Team=Interface->HasTeamPresentation() && GamePC && GamePC->CanPresentUnifiedTeam();
            const bool Commerce=Interface->HasCommercePresentation() && GamePC && GamePC->CanOpenUIPresentationTools()
                && (GamePC->IsWardrobeOpen() || GamePC->IsHubShopOpen());
            const bool Reward=Interface->HasRewardPresentation() && GamePC && GamePC->CanPresentUnifiedReward();
            const bool GameplayMenu=Interface->HasGameplayMenuPresentation() && GamePC && GamePC->GetActiveGameplayMenuPresentation();
            const bool HubMenu=Interface->HasHubPresentation() && Interface->RequiresHubMenuInput() && GamePC && GamePC->CanPresentUnifiedHub();
            if((Menu && (Cast<AMCMainMenuPlayerController>(PC) || (GamePC && GamePC->CanOpenUIPresentationTools()))) || Team || Commerce || Reward || GameplayMenu || HubMenu || TutorialMenu) {
                if(auto* Hero=Cast<AMCToothCharacter>(PC->GetPawn())) Hero->CancelGameplayInput();
                if(GamePC) {
                    PC->ResetIgnoreMoveInput();PC->SetIgnoreMoveInput(true);
                    PC->ResetIgnoreLookInput();PC->SetIgnoreLookInput(true);
                }
                auto* Focus=Interface->GetDefaultFocusTarget();
                if(Team) {
                    FInputModeGameAndUI Mode;Mode.SetWidgetToFocus(Focus?Focus->TakeWidget():Interface->TakeWidget());
                    Mode.SetHideCursorDuringCapture(false);Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);PC->SetInputMode(Mode);
                } else {
                    FInputModeUIOnly Mode;Mode.SetWidgetToFocus(Focus?Focus->TakeWidget():Interface->TakeWidget());
                    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);PC->SetInputMode(Mode);
                }
                PC->SetShowMouseCursor(true);return true;
            }
        }
        return false;
    }
    if(auto* Hero=Cast<AMCToothCharacter>(PC->GetPawn())) Hero->CancelGameplayInput();
    PC->FlushPressedKeys();PC->SetShowMouseCursor(true);
    FInputModeUIOnly Mode;Mode.SetWidgetToFocus(Panel->TakeWidget());
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);PC->SetInputMode(Mode);
    return true;
}

void UMCUIPresentationSubsystem::RestoreLegacy()
{
    if(Interface) Interface->SetEmoteFocusLease(false);
    bInterfaceTutorialMenuInput=false;
    if(auto* HUD=ActiveHUD.Get()) HUD->SetUnifiedViewActive(false);ActiveHUD.Reset();
    if(auto* Hub=ActiveHub.Get();Hub && Hub->GetWorld()==GetWorld() && GetWorld() && !GetWorld()->bIsTearingDown) Hub->SetUnifiedViewActive(false);
    ActiveHub.Reset();bInterfaceHubMenuInput=false;
    for(const auto& Entry:LegacyOpacity) if(auto* Widget=Entry.Key.Get()) Widget->SetRenderOpacity(Entry.Value);
    for(const auto& Entry:LegacyVisibility) if(auto* Widget=Entry.Key.Get()) Widget->SetVisibility(Entry.Value);
    LegacyOpacity.Reset();LegacyVisibility.Reset();
}

bool UMCUIPresentationSubsystem::PrepareWorldLabels(APlayerController* PC)
{
    if(!IsValid(PC) || PC->GetWorld()!=GetWorld()) return false;
    auto* NewTheme=UMCGameplayUIFactory::GetThemeForVersion(EMCUIVersion::New);
    if(!UMCWorldLabelWidget::AreThemeResourcesResident(NewTheme)) return false;
    const auto* Role=NewTheme->WidgetClasses.Find(TEXT("WorldLabel"));
    UClass* ResidentClass=Role && !Role->IsNull()?Role->Get():UMCWorldLabelWidget::StaticClass();
    if(!ResidentClass || !ResidentClass->IsChildOf(UMCWorldLabelWidget::StaticClass())) return false;
    if(PreparedWorldLabelView && (PreparedWorldLabelView->GetOwningPlayer()!=PC || PreparedWorldLabelView->GetClass()!=ResidentClass)) {
        PreparedWorldLabelSlate.Reset();PreparedWorldLabelView=nullptr;
    }
    if(!PreparedWorldLabelView) {
        PreparedWorldLabelView=CreateWidget<UMCWorldLabelWidget>(PC,ResidentClass);
        if(!PreparedWorldLabelView) return false;
        PreparedWorldLabelView->Theme=NewTheme;
        // No viewport entry or actor is needed. Retain the actual shared Slate
        // tree/font variants before even a future dynamic grenade is permitted.
        PreparedWorldLabelSlate=PreparedWorldLabelView->TakeWidget();
    }
    if(!PreparedWorldLabelSlate.IsValid() || !PreparedWorldLabelView->PreparePresentation()) return false;
    // Exact source-role copy, actual New font variants, no gameplay owner or hidden scene is created.
    const float FontScale=FMath::Max(.001f,PreparedWorldLabelView->GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale());
    return MCWorldUIWarmupPrivate::PrimeKnownWorldCaptions(NewTheme,UMCGameplayUIFactory::GetMasterTheme(),FontScale);
}

bool UMCUIPresentationSubsystem::PrepareInterface(APlayerController* PC)
{
    if(!IsValid(PC) || !PC->IsLocalController() || PC->GetWorld()!=GetWorld()) return false;
    if(InterfaceOwner.Get()!=PC)
    {
        RestoreLegacy();if(Interface) Interface->RemoveFromParent();Interface=nullptr;PreparedWorldLabelSlate.Reset();PreparedWorldLabelView=nullptr;InterfaceOwner.Reset();
    }
    auto PrepareOverlays=[this,PC]() {
        auto* GamePC=Cast<AMCPlayerController>(PC);
        if(GamePC && !GamePC->EmoteWidget) {
            // Precreate exactly the original controller-owned source in the loading path.
            // Its original NativeConstruct performs the normal UI refresh; no held selection/action is begun.
            GamePC->EmoteWidget=CreateWidget<UMCEmoteWidget>(GamePC,UMCGameplayUIFactory::ResolveClass(TEXT("Emotes"),UMCEmoteWidget::StaticClass()));
            if(GamePC->EmoteWidget) {GamePC->EmoteWidget->AddToViewport(15);GamePC->EmoteWidget->SetVisibility(ESlateVisibility::Collapsed);GamePC->EmoteWidget->TakeWidget();}
        }
        return Interface && Interface->PrepareOverlayPresentation(GamePC?MCOverlayIntegration_FindTutorial(PC):nullptr,GamePC?GamePC->EmoteWidget.Get():nullptr);
    };
    if(Interface) return PrepareWorldLabels(PC) && PrepareOverlays() && Interface->PrepareHUDExtrasPresentation() && Interface->PrepareEncounterPresentation();
    const auto* Theme=UMCGameplayUIFactory::GetThemeForVersion(EMCUIVersion::New);
    auto* Class=UMCGameplayUIFactory::ResolveClassForTheme(TEXT("InterfaceRoot"),UMCUnifiedUIWidget::StaticClass(),Theme);
    Interface=CreateWidget<UMCUnifiedUIWidget>(PC,Class);if(!Interface) return false;
    InterfaceOwner=PC;
    // TakeWidget constructs every switcher page, including retained glyph MIDs.
    // The view remains collapsed until readiness and a supported screen agree.
    InterfaceZOrder=5;Interface->TakeWidget();Interface->SetVisibility(ESlateVisibility::Collapsed);Interface->AddToViewport(InterfaceZOrder);
    return PrepareWorldLabels(PC) && PrepareOverlays() && Interface->PrepareHUDExtrasPresentation() && Interface->PrepareEncounterPresentation();
}

bool UMCUIPresentationSubsystem::PrepareFishingCatch(APlayerController* PC,UMCFishingRewardWidget* Source)
{
    return Source && PrepareInterface(PC) && Interface->PrepareCatchPresentation(Source);
}

bool UMCUIPresentationSubsystem::GetUnifiedFishCounterTargets(const APlayerController* PC,UWidget*& Icon,UWidget*& Card) const
{
    Icon=nullptr;Card=nullptr;
    if(!Interface || InterfaceOwner.Get()!=PC || !Interface->IsVisible()
        || UMCGameplayUIFactory::GetPresentationVersion()!=EMCUIVersion::New) return false;
    return Interface->GetHubFishCounterTargets(Icon,Card);
}
void UMCUIPresentationSubsystem::RefreshInterface()
{
    auto* GI=GetGameInstance();auto* PC=GI?GI->GetFirstLocalPlayerController():nullptr;
    if(!IsValid(PC) || PC->GetWorld()!=GetWorld() || (InterfaceOwner.IsValid() && InterfaceOwner.Get()!=PC)) {
        RestoreLegacy();if(Interface) Interface->RemoveFromParent();Interface=nullptr;PreparedWorldLabelSlate.Reset();PreparedWorldLabelView=nullptr;InterfaceOwner.Reset();return;
    }
    auto* GamePC=Cast<AMCPlayerController>(PC);
    auto* MenuPC=Cast<AMCMainMenuPlayerController>(PC);
    UMCMainMenuWidget* Menu=nullptr;UMCGameplayHUD* HUD=nullptr;UMCScoreboardWidget* Team=nullptr;
    UMCHubWardrobeWidget* Wardrobe=nullptr;UMCHubShopWidget* Shop=nullptr;
    UMCHubWidget* Hub=nullptr;UMCHubWidget* HubChrome=nullptr;UMCGameplayHUD* HUDChrome=nullptr;UMCPerkChoiceWidget* Reward=nullptr;UMCGameplayMenuWidget* GameplayMenu=nullptr;
    UMCTutorialWidget* Tutorial=nullptr;UMCEmoteWidget* Emotes=nullptr;
    EMCUnifiedUIScreen Screen=EMCUnifiedUIScreen::Gameplay;
    if(UMCGameplayUIFactory::GetPresentationVersion()==EMCUIVersion::New) {
        if((MenuPC && MenuPC->CanOpenUIPresentationTools()) || (GamePC && GamePC->CanOpenUIPresentationTools()))
            for(TObjectIterator<UMCMainMenuWidget> It;It;++It) {
                if(It->GetOwningPlayer()!=PC || It->GetWorld()!=GetWorld() || !It->IsVisible()
                    || (!It->IsInViewport() && !It->GetParent())) continue;
                bool VisibleHost=true;
                for(auto* Parent=It->GetParent();Parent;Parent=Parent->GetParent()) if(!Parent->IsVisible()) {VisibleHost=false;break;}
                if(!VisibleHost) continue;
                const auto Page=It->GetCurrentMenuPage();
                if(MenuPC && Page==EMCMainMenuPage::Main) {Menu=*It;Screen=EMCUnifiedUIScreen::Menu;break;}
                if(Page==EMCMainMenuPage::Settings || Page==EMCMainMenuPage::VideoConfirm) {Menu=*It;Screen=EMCUnifiedUIScreen::Settings;break;}
                if(Page==EMCMainMenuPage::Join || Page==EMCMainMenuPage::Connection || Page==EMCMainMenuPage::Lobby
                    || Page==EMCMainMenuPage::Leave || Page==EMCMainMenuPage::Exit) {Menu=*It;Screen=EMCUnifiedUIScreen::Frontend;break;}
            }
        if(GamePC && GamePC->CanOpenUIPresentationTools()) {
            Tutorial=MCOverlayIntegration_FindTutorial(PC);
            if(Tutorial && (!Tutorial->IsVisible() || !Tutorial->GetUnifiedPresentationSnapshot().bVisible)) Tutorial=nullptr;
            auto* OriginalEmotes=GamePC->EmoteWidget.Get();
            if(OriginalEmotes && OriginalEmotes->GetWorld()==GetWorld() && OriginalEmotes->GetOwningPlayer()==PC
                && OriginalEmotes->IsInViewport() && OriginalEmotes->IsVisible()) Emotes=OriginalEmotes;
            if(GamePC->IsWardrobeOpen()) for(TObjectIterator<UMCHubWardrobeWidget> It;It;++It)
                if(It->GetOwningPlayer()==PC && It->GetWorld()==GetWorld() && It->IsInViewport() && It->IsVisible()) {Wardrobe=*It;break;}
            if(GamePC->IsHubShopOpen()) for(TObjectIterator<UMCHubShopWidget> It;It;++It)
                if(It->GetOwningPlayer()==PC && It->GetWorld()==GetWorld() && It->IsInViewport() && It->IsVisible()) {Shop=*It;break;}
            GameplayMenu=GamePC->GetActiveGameplayMenuPresentation();
            if(GamePC->CanPresentUnifiedReward()) Reward=GamePC->PerkChoiceWidget;
            for(TObjectIterator<UMCHubWidget> It;It;++It)
                if(It->GetOwningPlayer()==PC && It->GetWorld()==GetWorld() && It->IsInViewport() && It->IsVisible()) {
                    HubChrome=*It;if(GamePC->CanPresentUnifiedHub()) Hub=*It;break;
                }
            for(TObjectIterator<UMCGameplayHUD> It;It;++It)
                if(It->GetOwningPlayer()==PC && It->GetWorld()==GetWorld() && It->GetParent()) {HUDChrome=*It;break;}
        }
        if(GamePC && !Hub && !Menu && !GameplayMenu && !Reward && (GamePC->CanUseRegularF3Panel() || Tutorial))
            for(TObjectIterator<UMCGameplayHUD> It;It;++It)
                if(It->GetOwningPlayer()==PC && It->GetWorld()==GetWorld() && It->GetParent()) {HUD=*It;break;}
        if(GamePC && GamePC->CanPresentUnifiedTeam())
            for(TObjectIterator<UMCScoreboardWidget> It;It;++It)
                if(It->GetOwningPlayer()==PC && It->GetWorld()==GetWorld() && It->IsInViewport() && It->IsVisible()) {Team=*It;break;}
    }
    // Mirror only the original owner's visible state; its Route/TryActivate/Maintain path retains the private gameplay gate.
    if(Menu || GameplayMenu || Wardrobe || Shop || Reward || Team
        || (Tutorial && Tutorial->GetUnifiedPresentationSnapshot().bRequiresMenuInput) || (Hub && Hub->RequiresMenuInput())) Emotes=nullptr;
    // Bind/prepare before hiding either original tree. A first zero source geometry frame falls back to Current.
    if((Tutorial || Emotes) && !Interface) PrepareInterface(PC);
    if(Interface) {
        Interface->SetTutorialBackend(Tutorial);Interface->SetEmoteBackend(Emotes);
        if(Tutorial && !Interface->HasTutorialPresentation()) Tutorial=nullptr;
        if(Emotes && !Interface->HasEmotePresentation()) Emotes=nullptr;
    } else {Tutorial=nullptr;Emotes=nullptr;}
    const bool WasVisible=Interface && Interface->IsVisible(),HadTeam=Interface && Interface->HasTeamPresentation();
    const bool PreviousHubMenu=bInterfaceHubMenuInput;
    const bool PreviousTutorialMenu=bInterfaceTutorialMenuInput;
    const EMCUnifiedUIScreen PreviousScreen=Interface?Interface->GetActiveScreen():EMCUnifiedUIScreen::Gameplay;
    if(!Menu && !HUD && !Team && !Wardrobe && !Shop && !Hub && !Reward && !GameplayMenu && !Tutorial && !Emotes) {
        RestoreLegacy();
        if(Interface) {
            Interface->SetMenuBackend(nullptr);Interface->SetHUDBackend(nullptr);Interface->SetTeamBackend(nullptr);
            Interface->SetWardrobeBackend(nullptr);Interface->SetShopBackend(nullptr);Interface->SetHubBackend(nullptr);
            Interface->SetRewardBackend(nullptr);Interface->SetGameplayMenuBackend(nullptr);Interface->SetCatchBackend(nullptr);Interface->SetBossBackend(nullptr);Interface->SetAlertBackend(nullptr);Interface->SetTutorialBackend(nullptr);Interface->SetEmoteBackend(nullptr);Interface->SetVisibility(ESlateVisibility::Collapsed);
        }
        if(GamePC) GamePC->RefreshFishingCounterTarget();
        if(WasVisible && !IsPanelOpenFor(PC)) {if(GamePC) GamePC->UpdateInputMode();else if(MenuPC) MenuPC->RestorePresentationInput();}
        return;
    }
    if(!Interface && !PrepareInterface(PC)) return;
    if(Menu || GameplayMenu || Wardrobe || Shop || Reward || Emotes) {Hub=nullptr;HUD=nullptr;Team=nullptr;}
    // Tutorial teaches over the real HUD. Other modals occlude its New overlay without destroying its source.
    const bool TutorialVisible=Tutorial && !Menu && !GameplayMenu && !Wardrobe && !Shop && !Reward && !Emotes && !Team;
    TSet<UWidget*> Hide;
    auto HideRoot=[&](UUserWidget* View) {if(View && View->WidgetTree && View->WidgetTree->RootWidget) Hide.Add(View->WidgetTree->RootWidget);};
    HideRoot(Menu);HideRoot(Team);HideRoot(Wardrobe);HideRoot(Shop);HideRoot(Reward);HideRoot(GameplayMenu);
    if(Tutorial && Tutorial->WidgetTree) {
        auto* Root=Tutorial->WidgetTree->FindWidget(TEXT("TutorialRoot"));
        if(Root && Root!=Tutorial->WidgetTree->FindWidget(TEXT("TutorialCard"))) Hide.Add(Root);
    }
    HideRoot(Emotes);
    for(auto It=LegacyVisibility.CreateIterator();It;++It)
        if(!It.Key().IsValid() || !Hide.Contains(It.Key().Get())) {if(auto* Widget=It.Key().Get()) Widget->SetVisibility(It.Value());It.RemoveCurrent();}
    for(auto* Source:Hide) {
        if(!LegacyVisibility.Contains(Source)) LegacyVisibility.Add(Source,Source->GetVisibility());
        Source->SetVisibility(ESlateVisibility::Hidden);
    }
    // A modal replaces the visible root screen. Keep the legacy background
    // chrome suppressed until Current is selected, so it cannot reappear behind it.
    if(ActiveHUD.Get()!=HUDChrome) {if(auto* Previous=ActiveHUD.Get()) Previous->SetUnifiedViewActive(false);ActiveHUD=HUDChrome;}
    if(HUDChrome) HUDChrome->SetUnifiedViewActive(true);
    if(ActiveHub.Get()!=HubChrome) {
        if(auto* Previous=ActiveHub.Get();Previous && Previous->GetWorld()==GetWorld() && !GetWorld()->bIsTearingDown) Previous->SetUnifiedViewActive(false);
        ActiveHub=HubChrome;
    }
    if(HubChrome) HubChrome->SetUnifiedViewActive(true);
    if(!Menu) Screen=Hub?EMCUnifiedUIScreen::Hub:(HUD || TutorialVisible)?EMCUnifiedUIScreen::Gameplay:EMCUnifiedUIScreen::Team;
    if(Reward) Screen=EMCUnifiedUIScreen::Reward;
    if(GameplayMenu) Screen=EMCUnifiedUIScreen::GameplayMenu;
    if(Wardrobe) Screen=EMCUnifiedUIScreen::Wardrobe;else if(Shop) Screen=EMCUnifiedUIScreen::Boosters;
    if(Emotes) Screen=EMCUnifiedUIScreen::Emotes;
    const bool Commerce=Wardrobe || Shop;
    const bool HubMenu=Hub && Hub->RequiresMenuInput();
    const int32 DesiredZ=Commerce?95:GameplayMenu?85:Menu?75:Team?40:Reward?35:Emotes?31:Hub?26:TutorialVisible?21:5;
    if(InterfaceZOrder!=DesiredZ) {Interface->RemoveFromParent();InterfaceZOrder=DesiredZ;Interface->AddToViewport(InterfaceZOrder);}
    Interface->SetMenuBackend(Menu);Interface->SetHUDBackend(HUD);Interface->SetTeamBackend(Team);
    Interface->SetWardrobeBackend(Wardrobe);Interface->SetShopBackend(Shop);Interface->SetHubBackend(Hub);
    Interface->SetRewardBackend(Reward);Interface->SetGameplayMenuBackend(GameplayMenu);
    Interface->SetCatchBackend(GamePC && HubChrome?GamePC->GetFishingRewardPresentation():nullptr);
    Interface->SetBossBackend(GamePC?GamePC->BossHealthWidget.Get():nullptr);Interface->SetAlertBackend(HUDChrome?HUDChrome->GetGameplayAlertPresentation():nullptr);
    Interface->SetTutorialBackend(Tutorial);Interface->SetEmoteBackend(Emotes);Interface->ShowScreen(Screen);
    Interface->SetVisibility((Menu || Commerce || Reward || GameplayMenu || Emotes)?ESlateVisibility::Visible:(Team || Hub || TutorialVisible)?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::HitTestInvisible);
    bInterfaceHubMenuInput=HubMenu;
    bInterfaceTutorialMenuInput=TutorialVisible && Interface->RequiresTutorialMenuInput();
    if(!Emotes) Interface->SetEmoteFocusLease(false);
    if(GamePC) GamePC->RefreshFishingCounterTarget();
    // The controller's original resolver may have focused a retained source before this mirror was ready.
    // Transfer only that exact source focus; never steal unrelated app/panel or New button navigation focus.
    const bool OriginalOverlayFocused=(Emotes && (Emotes->HasUserFocus(PC) || Emotes->HasUserFocusedDescendants(PC)))
        || (bInterfaceTutorialMenuInput && Tutorial && (Tutorial->HasUserFocus(PC) || Tutorial->HasUserFocusedDescendants(PC)));
    if((!WasVisible || HadTeam!=(Team!=nullptr) || PreviousScreen!=Screen || PreviousHubMenu!=HubMenu || PreviousTutorialMenu!=bInterfaceTutorialMenuInput || OriginalOverlayFocused) && !IsPanelOpenFor(PC)) {
        if(Menu || Team || Commerce || Reward || GameplayMenu || HubMenu || Emotes || bInterfaceTutorialMenuInput) ApplyPanelInputMode(PC);
        else if(GamePC) GamePC->UpdateInputMode();
    }
}

void UMCUIPresentationSubsystem::ClosePanel(bool bRestoreInput)
{
    auto* PC=PanelOwner.Get();
    if(Panel) Panel->RemoveFromParent();
    Panel=nullptr;PanelOwner.Reset();
    if(!bRestoreInput || !IsValid(PC) || PC->GetWorld()!=GetWorld()) return;
    if(auto* GamePC=Cast<AMCPlayerController>(PC)) GamePC->UpdateInputMode();
    else if(auto* MenuPC=Cast<AMCMainMenuPlayerController>(PC)) MenuPC->RestorePresentationInput();
}
