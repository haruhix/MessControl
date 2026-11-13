#include "MCGameplayUIPreview.h"

#include "MCGameplayUITheme.h"
#include "MCGameplayUIWidget.h"
#include "MCHubGrillPresentationComponent.h"
#include "MCMainMenuWidget.h"
#include "MCRaidLoadoutWidget.h"
#include "MCUnifiedUIWidget.h"
#include "MCUnifiedFrontendWidget.h"
#include "MCUnifiedTeamWidget.h"
#include "MCTutorialTypes.h"
#include "AssetRegistry/AssetData.h"
#include "Blueprint/WidgetTree.h"
#include "Containers/Ticker.h"
#include "CoreGlobals.h"
#include "Customizations/IBlueprintWidgetCustomizationExtender.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DetailsViewArgs.h"
#include "Editor.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "Textures/SlateIcon.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UMGEditorModule.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#include <type_traits>

#define LOCTEXT_NAMESPACE "MCGameplayUIPreview"

namespace
{
    const FName ThemeTabName(TEXT("MCGameplayUITheme"));

    struct FMainMenuPreviewChoice
    {
        const TCHAR* State;
        const TCHAR* Label;
        EMCMainMenuPage Page;
        int32 SettingsTab=0;
    };

    const FMainMenuPreviewChoice MainMenuPreviewChoices[]={
        {TEXT("Main"),TEXT("Main menu"),EMCMainMenuPage::Main},
        {TEXT("Join"),TEXT("Join friends"),EMCMainMenuPage::Join},
        {TEXT("Settings"),TEXT("Settings / Graphics"),EMCMainMenuPage::Settings,0},
        {TEXT("SettingsAudio"),TEXT("Settings / Audio"),EMCMainMenuPage::Settings,1},
        {TEXT("SettingsLanguage"),TEXT("Settings / Language"),EMCMainMenuPage::Settings,2},
        {TEXT("SettingsControls"),TEXT("Settings / Controls"),EMCMainMenuPage::Settings,3},
        {TEXT("SettingsVoice"),TEXT("Settings / Voice"),EMCMainMenuPage::Settings,4},
        {TEXT("Credits"),TEXT("Credits"),EMCMainMenuPage::Credits},
        {TEXT("Exit"),TEXT("Quit confirmation"),EMCMainMenuPage::Exit},
        {TEXT("Host"),TEXT("Create lobby"),EMCMainMenuPage::Host},
        {TEXT("Lobby"),TEXT("Lobby"),EMCMainMenuPage::Lobby},
        {TEXT("Leave"),TEXT("Leave confirmation"),EMCMainMenuPage::Leave},
        {TEXT("VideoConfirm"),TEXT("Display confirmation"),EMCMainMenuPage::VideoConfirm},
        {TEXT("Pause"),TEXT("Pause"),EMCMainMenuPage::Pause},
        {TEXT("Connection"),TEXT("Connection"),EMCMainMenuPage::Connection}
    };

    struct FMainMenuPreviewCulture {const TCHAR* Code;const TCHAR* Label;};
    const FMainMenuPreviewCulture MainMenuPreviewCultures[]={
        {TEXT(""),TEXT("Current editor language")},
        {TEXT("ru"),TEXT("Russian (ru)")},{TEXT("en"),TEXT("English (en)")},
        {TEXT("de"),TEXT("German (de)")},{TEXT("fr"),TEXT("French (fr)")},
        {TEXT("es"),TEXT("Spanish (es)")},{TEXT("pt-BR"),TEXT("Portuguese (pt-BR)")},
        {TEXT("zh-Hans"),TEXT("Chinese simplified (zh-Hans)")},
        {TEXT("zh-Hant"),TEXT("Chinese traditional (zh-Hant)")},
        {TEXT("ja"),TEXT("Japanese (ja)")},{TEXT("ko"),TEXT("Korean (ko)")}
    };

    template<typename T>
    struct TUnifiedOverlayPreviewChoice {T Value;FText Label;};

    // Exact fixture tokens consumed by the composed views; aliases with identical output are omitted.
    const TUnifiedOverlayPreviewChoice<FName> UnifiedHubModes[]={
        {NAME_None,LOCTEXT("UnifiedHubGathering","Gathering: host")},
        {TEXT("Guest"),LOCTEXT("UnifiedHubGuest","Gathering: guest")},
        {TEXT("Walking"),LOCTEXT("UnifiedHubWalking","Walking")},
        {TEXT("Ready"),LOCTEXT("UnifiedHubReady","Ready")},
        {TEXT("WaitingForTeam"),LOCTEXT("UnifiedHubWaiting","Waiting for team")},
        {TEXT("Selecting"),LOCTEXT("UnifiedHubSelecting","Location selection: host")},
        {TEXT("SelectingGuest"),LOCTEXT("UnifiedHubSelectingGuest","Location selection: guest")},
        {TEXT("RaidStock"),LOCTEXT("RaidStock","Raid kit: choose from stock")},
        {TEXT("RaidEmpty"),LOCTEXT("RaidEmpty","Raid kit: empty stock")},
        {TEXT("RaidFull"),LOCTEXT("RaidFull","Raid kit: full loadout")},
        {TEXT("RaidReady"),LOCTEXT("RaidReady","Raid kit: local player ready")},
        {TEXT("RaidGuest"),LOCTEXT("RaidGuest","Raid kit: guest")},
        {TEXT("RaidAllReady"),LOCTEXT("RaidAllReady","Raid kit: all players ready")},
        {TEXT("Travelling"),LOCTEXT("UnifiedHubTravelling","Travelling")},
        {TEXT("FishingControls"),LOCTEXT("UnifiedHubFishingControls","Fishing controls")},
        {TEXT("FishingWaiting"),LOCTEXT("UnifiedHubFishingWaiting","Fishing: waiting")},
        {TEXT("FishingHooked"),LOCTEXT("UnifiedHubFishingHooked","Fishing: hooked")},
        {TEXT("BasketballControls"),LOCTEXT("UnifiedHubBasketballControls","Basketball controls")},
        {TEXT("BasketballPickup"),LOCTEXT("UnifiedHubBasketballPickup","Basketball: pick up")},
        {TEXT("ControlsLongText"),LOCTEXT("UnifiedHubControlsLongText","Long control instructions")}
    };
    const TUnifiedOverlayPreviewChoice<FName> RaidLoadoutModes[]={
        {TEXT("Default"),LOCTEXT("RaidLoadoutDefault","Default loadout")},
        {TEXT("RaidStock"),LOCTEXT("RaidStock","Raid kit: choose from stock")},
        {TEXT("RaidEmpty"),LOCTEXT("RaidEmpty","Raid kit: empty stock")},
        {TEXT("RaidFull"),LOCTEXT("RaidFull","Raid kit: full loadout")},
        {TEXT("RaidReady"),LOCTEXT("RaidReady","Raid kit: local player ready")},
        {TEXT("RaidGuest"),LOCTEXT("RaidGuest","Raid kit: guest")},
        {TEXT("RaidAllReady"),LOCTEXT("RaidAllReady","Raid kit: all players ready")}
    };
    const TUnifiedOverlayPreviewChoice<FName> RaidPerkRowModes[]={
        {TEXT("Default"),LOCTEXT("RaidRowDefault","Equipped perk")},
        {TEXT("Stock"),LOCTEXT("RaidRowStock","Owned perk with quantity")},
        {TEXT("Add"),LOCTEXT("RaidRowAdd","Add perk")},
        {TEXT("Disabled"),LOCTEXT("RaidRowDisabled","Unavailable perk")}
    };

    const TUnifiedOverlayPreviewChoice<FName> GrillHologramModes[]={
        {TEXT("Idle"),LOCTEXT("GrillIdle","Idle reel")},
        {TEXT("Default"),LOCTEXT("GrillSpin","Spin")},
        {TEXT("NoPrize"),LOCTEXT("GrillNoPrize","No prize")},
        {TEXT("Duplicate"),LOCTEXT("GrillDuplicate","Duplicate")},
        {TEXT("Reset"),LOCTEXT("GrillReset","Reset to idle")},
        {TEXT("RareReset"),LOCTEXT("GrillRareReset","Big win reset")}
    };

    TConstArrayView<TUnifiedOverlayPreviewChoice<FName>> WidgetScenariosForClass(const UClass* Class)
    {
        if(Class && Class->IsChildOf(UMCRaidLoadoutWidget::StaticClass())) return MakeArrayView(RaidLoadoutModes);
        if(Class && Class->IsChildOf(UMCRaidPerkRowWidget::StaticClass())) return MakeArrayView(RaidPerkRowModes);
        if(Class && Class->IsChildOf(UMCHubGrillHologramWidget::StaticClass())) return MakeArrayView(GrillHologramModes);
        return {};
    }
    const TUnifiedOverlayPreviewChoice<FName> UnifiedTeamModes[]={
        {NAME_None,LOCTEXT("UnifiedTeamArena","Arena scoreboard")},
        {TEXT("HubLocal"),LOCTEXT("UnifiedTeamHubLocal","Hub: local player")},
        {TEXT("HubHost"),LOCTEXT("UnifiedTeamHubHost","Hub: room host")},
        {TEXT("HubGuest"),LOCTEXT("UnifiedTeamHubGuest","Hub: room guest")}
    };
    const TUnifiedOverlayPreviewChoice<FName> UnifiedRewardModes[]={
        {NAME_None,LOCTEXT("UnifiedRewardChoice","Reward choice")},
        {TEXT("AutoPicking"),LOCTEXT("UnifiedRewardAutoPicking","Automatic selection")},
        {TEXT("Replacement"),LOCTEXT("UnifiedRewardReplacement","Replace a skill")},
        {TEXT("Contract"),LOCTEXT("UnifiedRewardContract","Contract confirmation")},
        {TEXT("WaitingForTeam"),LOCTEXT("UnifiedRewardWaiting","Waiting for team")},
        {TEXT("Pending"),LOCTEXT("UnifiedRewardPending","Selection pending")}
    };
    const TUnifiedOverlayPreviewChoice<FName> UnifiedGameplayMenuModes[]={
        {NAME_None,LOCTEXT("UnifiedGameplayMenuDefault","Widget default page")},
        {TEXT("Pause"),LOCTEXT("UnifiedGameplayMenuPause","Pause")},
        {TEXT("Settings"),LOCTEXT("UnifiedGameplayMenuSettings","Graphics settings")},
        {TEXT("Audio"),LOCTEXT("UnifiedGameplayMenuAudio","Audio and microphone")},
        {TEXT("Connection"),LOCTEXT("UnifiedGameplayMenuConnection","Connection")},
        {TEXT("Credits"),LOCTEXT("UnifiedGameplayMenuCredits","Credits")},
        {TEXT("Leave"),LOCTEXT("UnifiedGameplayMenuLeave","Leave confirmation")},
        {TEXT("Exit"),LOCTEXT("UnifiedGameplayMenuExit","Exit confirmation")},
        {TEXT("VideoConfirm"),LOCTEXT("UnifiedGameplayMenuVideo","Display confirmation")}
    };
    const TUnifiedOverlayPreviewChoice<FName> UnifiedCatchModes[]={
        {NAME_None,LOCTEXT("UnifiedCatchDefault","Widget default rarity")},
        {TEXT("Common"),LOCTEXT("UnifiedCatchCommon","Common catch")},
        {TEXT("Rare"),LOCTEXT("UnifiedCatchRare","Rare catch")},
        {TEXT("Epic"),LOCTEXT("UnifiedCatchEpic","Epic catch")},
        {TEXT("Legendary"),LOCTEXT("UnifiedCatchLegendary","Legendary catch")},
        {TEXT("WalletFull"),LOCTEXT("UnifiedCatchWalletFull","Wallet full")},
        {TEXT("Merged"),LOCTEXT("UnifiedCatchMerged","Merged reward")}
    };
    const TUnifiedOverlayPreviewChoice<FName> UnifiedHUDExtrasModes[]={
        {NAME_None,LOCTEXT("UnifiedHUDExtrasDefault","Default HUD")},
        {TEXT("Death"),LOCTEXT("UnifiedHUDExtrasDeath","Death")},
        {TEXT("Respawn"),LOCTEXT("UnifiedHUDExtrasRespawn","Respawn countdown")},
        {TEXT("Spectator"),LOCTEXT("UnifiedHUDExtrasSpectator","Spectator")},
        {TEXT("ResultsWin"),LOCTEXT("UnifiedHUDExtrasWin","Victory results")},
        {TEXT("ResultsLose"),LOCTEXT("UnifiedHUDExtrasLose","Defeat results")},
        {TEXT("Swimming"),LOCTEXT("UnifiedHUDExtrasSwimming","Swimming")},
        {TEXT("Drowning"),LOCTEXT("UnifiedHUDExtrasDrowning","Drowning")},
        {TEXT("Booster"),LOCTEXT("UnifiedHUDExtrasBooster","Booster")},
        {TEXT("Passive"),LOCTEXT("UnifiedHUDExtrasPassive","Passives")},
        {TEXT("PassiveOverflow"),LOCTEXT("UnifiedHUDExtrasPassiveOverflow","Passives: row scrolling")},
        {TEXT("Cataclysm"),LOCTEXT("UnifiedHUDExtrasCataclysm","Cataclysm instructions")},
        {TEXT("CataclysmSwim"),LOCTEXT("UnifiedHUDExtrasCataclysmSwim","Cataclysm and swimming")},
        {TEXT("CataclysmDrowning"),LOCTEXT("UnifiedHUDExtrasCataclysmDrowning","Cataclysm and drowning")},
        {TEXT("LongText"),LOCTEXT("UnifiedHUDExtrasLongText","Combined long text")},
        {TEXT("FoodReticle"),LOCTEXT("UnifiedHUDExtrasFoodReticle","Food reticle")},
        {TEXT("FoodReticleFade"),LOCTEXT("UnifiedHUDExtrasFoodReticleFade","Food reticle: fading")},
        {TEXT("SprayReticle"),LOCTEXT("UnifiedHUDExtrasSprayReticle","Spray reticle")},
        {TEXT("PressureReticle"),LOCTEXT("UnifiedHUDExtrasPressureReticle","Pressure spray reticle")},
        {TEXT("AntisepticReload"),LOCTEXT("UnifiedHUDExtrasAntisepticReload","Antiseptic reload")},
        {TEXT("FogNavigation"),LOCTEXT("UnifiedHUDExtrasFogNavigation","Fog navigation")},
        {TEXT("ReticleCombined"),LOCTEXT("UnifiedHUDExtrasReticleCombined","Combined reticle and navigation")}
    };
    const TUnifiedOverlayPreviewChoice<FName> UnifiedLoadingModes[]={
        {NAME_None,LOCTEXT("UnifiedLoadingAssets","Loading assets")},
        {TEXT("Graphics"),LOCTEXT("UnifiedLoadingGraphics","Preparing graphics")},
        {TEXT("Streaming"),LOCTEXT("UnifiedLoadingStreaming","Streaming textures and meshes")},
        {TEXT("Connecting"),LOCTEXT("UnifiedLoadingConnecting","Connecting")},
        {TEXT("Waiting"),LOCTEXT("UnifiedLoadingWaiting","Waiting for players")},
        {TEXT("Failed"),LOCTEXT("UnifiedLoadingFailed","Failed: retry and back")}
    };
    const TUnifiedOverlayPreviewChoice<FName> UnifiedBossModes[]={
        {NAME_None,LOCTEXT("UnifiedBossDefault","Default boss stack")},
        {TEXT("Letterbox"),LOCTEXT("UnifiedBossLetterbox","Cinematic letterbox")},
        {TEXT("ZeroBosses"),LOCTEXT("UnifiedBossZero","No bosses")},
        {TEXT("ShieldedLeader"),LOCTEXT("UnifiedBossShielded","Shielded leader")},
        {TEXT("ZeroHealth"),LOCTEXT("UnifiedBossZeroHealth","Zero health")},
        {TEXT("FullHealth"),LOCTEXT("UnifiedBossFullHealth","Full health")},
        {TEXT("BrokenShield"),LOCTEXT("UnifiedBossBrokenShield","Broken shield")}
    };
    const TUnifiedOverlayPreviewChoice<int32> UnifiedFrontendPages[]={
        {int32(EMCUnifiedFrontendPage::Join),LOCTEXT("UnifiedFrontendJoin","Join friends")},
        {int32(EMCUnifiedFrontendPage::Connection),LOCTEXT("UnifiedFrontendConnection","Connection")},
        {int32(EMCUnifiedFrontendPage::Lobby),LOCTEXT("UnifiedFrontendLobby","Lobby")},
        {int32(EMCUnifiedFrontendPage::Leave),LOCTEXT("UnifiedFrontendLeave","Leave confirmation")},
        {int32(EMCUnifiedFrontendPage::Exit),LOCTEXT("UnifiedFrontendExit","Exit confirmation")}
    };
    const TUnifiedOverlayPreviewChoice<EMCGameplayUIPreviewScenario> UnifiedScenarios[]={
        {EMCGameplayUIPreviewScenario::Default,LOCTEXT("UnifiedScenarioDefault","Default")},
        {EMCGameplayUIPreviewScenario::LongText,LOCTEXT("UnifiedScenarioLongText","Long text")},
        {EMCGameplayUIPreviewScenario::LowResources,LOCTEXT("UnifiedScenarioLowResources","Low health")},
        {EMCGameplayUIPreviewScenario::Empty,LOCTEXT("UnifiedScenarioEmpty","Empty")},
        {EMCGameplayUIPreviewScenario::OneBoss,LOCTEXT("UnifiedScenarioOneBoss","One boss")},
        {EMCGameplayUIPreviewScenario::TwoBosses,LOCTEXT("UnifiedScenarioTwoBosses","Two bosses")},
        {EMCGameplayUIPreviewScenario::ThreeBosses,LOCTEXT("UnifiedScenarioThreeBosses","Three bosses")},
        {EMCGameplayUIPreviewScenario::Pending,LOCTEXT("UnifiedScenarioPending","Pending")},
        {EMCGameplayUIPreviewScenario::Disabled,LOCTEXT("UnifiedScenarioDisabled","Disabled")},
        {EMCGameplayUIPreviewScenario::Selected,LOCTEXT("UnifiedScenarioSelected","Location selection")},
        {EMCGameplayUIPreviewScenario::Victory,LOCTEXT("UnifiedScenarioVictory","Tutorial complete")},
        {EMCGameplayUIPreviewScenario::Waiting,LOCTEXT("UnifiedScenarioWaiting","Waiting")}
    };

    const TUnifiedOverlayPreviewChoice<FName> UnifiedTutorialModes[]={
        {NAME_None,LOCTEXT("UnifiedTutorialSelectedLesson","Selected lesson")},
        {TEXT("FullComplete"),LOCTEXT("UnifiedTutorialFullComplete","Full tutorial complete")},
        {TEXT("ShortComplete"),LOCTEXT("UnifiedTutorialShortComplete","Short tutorial complete")},
        {TEXT("Ready"),LOCTEXT("UnifiedTutorialReady","Ready")},
        {TEXT("Empty"),LOCTEXT("UnifiedTutorialEmpty","Empty")},
        {TEXT("LongText"),LOCTEXT("UnifiedTutorialLongText","Long text")}
    };
    // Only the 17 stages with authored source copy; TrashSort/Finished have no fixture.
    const TUnifiedOverlayPreviewChoice<EMCTutorialStage> UnifiedTutorialLessons[]={
        {EMCTutorialStage::Loading,LOCTEXT("UnifiedTutorialLoading","Loading")},
        {EMCTutorialStage::Intro,LOCTEXT("UnifiedTutorialIntro","Introduction")},
        {EMCTutorialStage::ToothpickPull,LOCTEXT("UnifiedTutorialToothpickPull","Toothpick: pull")},
        {EMCTutorialStage::ToothpickBreak,LOCTEXT("UnifiedTutorialToothpickBreak","Toothpick: break")},
        {EMCTutorialStage::ToothpickHeal,LOCTEXT("UnifiedTutorialToothpickHeal","Toothpick: heal")},
        {EMCTutorialStage::BrushTooth,LOCTEXT("UnifiedTutorialBrushTooth","Brush tooth")},
        {EMCTutorialStage::FoodCut,LOCTEXT("UnifiedTutorialFoodCut","Cut food")},
        {EMCTutorialStage::FreshSort,LOCTEXT("UnifiedTutorialFreshSort","Sort fresh food")},
        {EMCTutorialStage::SpoiledSort,LOCTEXT("UnifiedTutorialSpoiledSort","Sort spoiled food")},
        {EMCTutorialStage::BreakfastRain,LOCTEXT("UnifiedTutorialBreakfastRain","Breakfast: rain")},
        {EMCTutorialStage::BreakfastCleanup,LOCTEXT("UnifiedTutorialBreakfastCleanup","Breakfast: cleanup")},
        {EMCTutorialStage::CoffeeWarning,LOCTEXT("UnifiedTutorialCoffeeWarning","Coffee: warning")},
        {EMCTutorialStage::CoffeeWaves,LOCTEXT("UnifiedTutorialCoffeeWaves","Coffee: waves")},
        {EMCTutorialStage::CoffeeCleanup,LOCTEXT("UnifiedTutorialCoffeeCleanup","Coffee: cleanup")},
        {EMCTutorialStage::Calculus,LOCTEXT("UnifiedTutorialCalculus","Calculus")},
        {EMCTutorialStage::FoamParty,LOCTEXT("UnifiedTutorialFoamParty","Foam party")},
        {EMCTutorialStage::Complete,LOCTEXT("UnifiedTutorialComplete","Complete")}
    };
    const TUnifiedOverlayPreviewChoice<FName> UnifiedEmoteModes[]={
        {NAME_None,LOCTEXT("UnifiedEmoteSelectedCategory","Selected category")},
        {TEXT("Held"),LOCTEXT("UnifiedEmoteHeld","Held selection")},
        {TEXT("HeldNoSelection"),LOCTEXT("UnifiedEmoteHeldNoSelection","Held without selection")},
        {TEXT("Disabled"),LOCTEXT("UnifiedEmoteDisabled","Actions unavailable")},
        {TEXT("Empty"),LOCTEXT("UnifiedEmoteEmpty","Empty")},
        {TEXT("LongText"),LOCTEXT("UnifiedEmoteLongText","Long text")}
    };
    const TUnifiedOverlayPreviewChoice<int32> UnifiedEmoteCategories[]={
        {0,LOCTEXT("UnifiedEmoteFaces","Faces")},
        {1,LOCTEXT("UnifiedEmoteGestures","Gestures")},
        {2,LOCTEXT("UnifiedEmoteSignals","Signals")}
    };

    bool IsMainMenuEditor(const TSharedPtr<FWidgetBlueprintEditor>& Editor)
    {
        const auto* Blueprint=Editor?Editor->GetWidgetBlueprintObj():nullptr;
        return Blueprint && Blueprint->ParentClass
            && Blueprint->ParentClass->IsChildOf(UMCMainMenuWidget::StaticClass());
    }

    bool IsGameplayEditor(const TSharedPtr<FWidgetBlueprintEditor>& Editor)
    {
        const auto* Blueprint=Editor?Editor->GetWidgetBlueprintObj():nullptr;
        if (!Blueprint || !Blueprint->ParentClass
            || !Blueprint->ParentClass->IsChildOf(UMCGameplayUIWidget::StaticClass())) return false;
        const FString Package=Blueprint->GetOutermost()->GetName();
        return Package.StartsWith(TEXT("/Game/Gameplay/UI/"))
            || (Package.StartsWith(TEXT("/Game/UI/")) && IsMainMenuEditor(Editor));
    }

    UMCGameplayUIWidget* GetGameplayPreview(const TSharedPtr<FWidgetBlueprintEditor>& Editor)
    {
        auto* Widget=IsGameplayEditor(Editor)?Cast<UMCGameplayUIWidget>(Editor->GetPreview()):nullptr;
        return Widget && Widget->IsDesignTime() && !Widget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)?Widget:nullptr;
    }

    /** Owns only a transient Designer widget. The native DetailsView owns theme transactions/undo. */
    class SGameplayUIThemePanel final : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SGameplayUIThemePanel) {} SLATE_END_ARGS()

        void Construct(const FArguments&)
        {
            FDetailsViewArgs Args;
            Args.bUpdatesFromSelection=false;
            Args.bLockable=false;
            Args.bAllowSearch=true;
            Args.NameAreaSettings=FDetailsViewArgs::HideNameArea;
            Details=FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(Args);
            Details->OnFinishedChangingProperties().AddSP(this,&SGameplayUIThemePanel::OnPaletteChanged);
            ChildSlot
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(12)
                [
                    SNew(STextBlock).Text(LOCTEXT("ThemeHelp","Shared gameplay palette. Color fields include RGBA values and the native color picker. Changes support Undo; save the theme asset to keep them."))
                    .AutoWrapText(true)
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(12,0,12,12)
                [
                    SNew(SObjectPropertyEntryBox)
                    .AllowedClass(UMCGameplayUITheme::StaticClass())
                    .AllowClear(false)
                    .ObjectPath(this,&SGameplayUIThemePanel::GetThemePath)
                    .OnObjectChanged(this,&SGameplayUIThemePanel::OnThemePicked)
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(12,0,12,12)
                [
                    SNew(SButton).Text(LOCTEXT("SavePalette","Save shared theme"))
                    .IsEnabled_Lambda([this]{return SelectedTheme.IsValid();})
                    .OnClicked_Lambda([this]
                    {
                        if (GEditor && SelectedTheme.IsValid())
                            if (auto* Assets=GEditor->GetEditorSubsystem<UEditorAssetSubsystem>())
                                Assets->SaveLoadedAsset(SelectedTheme.Get(),false);
                        return FReply::Handled();
                    })
                ]
                + SVerticalBox::Slot().FillHeight(1)
                [
                    SNew(SSplitter)
                    + SSplitter::Slot().Value(.45f).MinSize(260)
                    [Details.ToSharedRef()]
                    + SSplitter::Slot().Value(.55f).MinSize(300)
                    [
                        SNew(SBorder).Padding(16)
                        [SAssignNew(PreviewHost,SBox)]
                    ]
                ]
            ];
            SetTheme(nullptr);
        }

        void SetTheme(UMCGameplayUITheme* Theme)
        {
            if (!Theme) Theme=GetDefault<UMCGameplayUIWidget>()->GetResolvedTheme();
            // The native fallback is useful for drawing, but is not a persistent palette asset to edit.
            if (Theme && Theme->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)) Theme=nullptr;
            SelectedTheme.Reset(Theme);
            Details->SetObject(Theme);
            RebuildPreview();
        }

        void RefreshPreview()
        {
            if (Preview.IsValid())
            {
                Preview->Theme=SelectedTheme.Get();
                Preview->ApplyTheme();
                Preview->ApplyEditorPreview(Preview->PreviewTime);
                MCGameplayUIPreview::SynchronizeFixtureTree(Preview.Get());
                Preview->InvalidateLayoutAndVolatility();
            }
        }

        UMCGameplayUITheme* GetTheme() const {return SelectedTheme.Get();}

    private:
        FString GetThemePath() const {return SelectedTheme.IsValid()?SelectedTheme->GetPathName():FString();}
        void OnThemePicked(const FAssetData& Asset) {SetTheme(Cast<UMCGameplayUITheme>(Asset.GetAsset()));}

        void OnPaletteChanged(const FPropertyChangedEvent&)
        {
            if (SelectedTheme.IsValid()) SelectedTheme->NotifyThemeChanged();
            RefreshPreview();
        }

        void RebuildPreview()
        {
            PreviewHost->SetContent(SNew(STextBlock).Text(LOCTEXT("ThemePreviewMissing","Create /Game/Gameplay/UI/WBP_UIThemePreview to show the editable theme sample here.")));
            Preview.Reset();
            auto* Class=LoadClass<UMCGameplayUIWidget>(nullptr,TEXT("/Game/Gameplay/UI/WBP_UIThemePreview.WBP_UIThemePreview_C"));
            if (!Class) return;
            auto* Widget=NewObject<UMCGameplayUIWidget>(GetTransientPackage(),Class,NAME_None,RF_Transient);
            Widget->SetDesignerFlags(EWidgetDesignFlags::Designing | EWidgetDesignFlags::ExecutePreConstruct);
            Widget->Theme=SelectedTheme.Get();
            Widget->Initialize();
            Preview.Reset(Widget);
            PreviewHost->SetContent(SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[Widget->TakeWidget()]);
            Widget->ApplyTheme();
            Widget->ApplyEditorPreview(0.f);
            MCGameplayUIPreview::SynchronizeFixtureTree(Widget);
        }

        TSharedPtr<IDetailsView> Details;
        TSharedPtr<SBox> PreviewHost;
        TStrongObjectPtr<UMCGameplayUITheme> SelectedTheme;
        TStrongObjectPtr<UMCGameplayUIWidget> Preview;
    };

    struct FPreviewSession
    {
        TWeakPtr<FWidgetBlueprintEditor> Editor;
        TWeakObjectPtr<UMCGameplayUIWidget> Preview;
        FDelegateHandle RebuildHandle;
        float Time=0.f;
        bool bPlaying=false;
        uint8 GeometrySettleFrames=0;
        uint64 SettleQueuedFrame=0;
        // Session-only state: never written to the Blueprint CDO or saved tree.
        FName MenuState=TEXT("Main");
        FString MenuCulture;
        bool bHasMenuSelection=false;
        FName WidgetScenario;
        bool bHasWidgetScenario=false;
        bool bHasPresentationSelection=false;
        EMCUIVersion PresentationVersion=EMCUIVersion::New;
        bool bHasUnifiedSelection=false;
        EMCUnifiedUIScreen UnifiedScreen=EMCUnifiedUIScreen::Menu;
        int32 UnifiedSettingsTab=0;
        FName UnifiedHubMode;
        FName UnifiedTeamMode;
        FName UnifiedRewardMode;
        FName UnifiedGameplayMenuMode;
        FName UnifiedCatchMode;
        FName UnifiedHUDExtrasMode;
        FName UnifiedLoadingMode;
        FName UnifiedBossMode;
        FName UnifiedAlertMode;
        FName UnifiedTutorialMode;
        EMCTutorialStage UnifiedTutorialLesson=EMCTutorialStage::BrushTooth;
        FName UnifiedEmoteMode;
        int32 UnifiedEmoteCategory=0;
        int32 UnifiedFrontendPage=0;
        FString UnifiedCulture;
        EMCGameplayUIPreviewScenario UnifiedScenario=EMCGameplayUIPreviewScenario::Default;
    };

    void QueueGeometrySettle(FPreviewSession& Session)
    {
        Session.GeometrySettleFrames=Session.bPlaying?0:3;
        Session.SettleQueuedFrame=GFrameCounter;
    }

    bool SupportsUnifiedScenario(const FPreviewSession& Session,EMCGameplayUIPreviewScenario Scenario)
    {
        if(Scenario==EMCGameplayUIPreviewScenario::Default) return true;
        const bool Long=Scenario==EMCGameplayUIPreviewScenario::LongText;
        const bool Empty=Scenario==EMCGameplayUIPreviewScenario::Empty;
        const bool Pending=Scenario==EMCGameplayUIPreviewScenario::Pending;
        const bool Waiting=Scenario==EMCGameplayUIPreviewScenario::Waiting;
        const bool Disabled=Scenario==EMCGameplayUIPreviewScenario::Disabled;
        const bool BossCount=Scenario==EMCGameplayUIPreviewScenario::OneBoss || Scenario==EMCGameplayUIPreviewScenario::TwoBosses || Scenario==EMCGameplayUIPreviewScenario::ThreeBosses;
        switch(Session.UnifiedScreen)
        {
        case EMCUnifiedUIScreen::Hub:return Long || Waiting || Scenario==EMCGameplayUIPreviewScenario::Selected;
        case EMCUnifiedUIScreen::Reward:return Long || Waiting || Pending;
        case EMCUnifiedUIScreen::Team:return Long || Empty;
        case EMCUnifiedUIScreen::Wardrobe:return Disabled;
        case EMCUnifiedUIScreen::Frontend:
            // Confirmation pages have no long/empty/pending fixture; use only their real disabled state.
            return Disabled
                || (Long && (Session.UnifiedFrontendPage==int32(EMCUnifiedFrontendPage::Join) || Session.UnifiedFrontendPage==int32(EMCUnifiedFrontendPage::Lobby)))
                || (Empty && (Session.UnifiedFrontendPage==int32(EMCUnifiedFrontendPage::Join) || Session.UnifiedFrontendPage==int32(EMCUnifiedFrontendPage::Connection)))
                || (Pending && Session.UnifiedFrontendPage>=int32(EMCUnifiedFrontendPage::Join) && Session.UnifiedFrontendPage<=int32(EMCUnifiedFrontendPage::Lobby));
        case EMCUnifiedUIScreen::GameplayMenu:return Long || (Pending && Session.UnifiedGameplayMenuMode==TEXT("Connection"));
        case EMCUnifiedUIScreen::Catch:
        case EMCUnifiedUIScreen::HUDExtras:
        case EMCUnifiedUIScreen::Emotes:return Long;
        case EMCUnifiedUIScreen::Loading:return Waiting;
        case EMCUnifiedUIScreen::Boss:return Long || Empty || BossCount || Scenario==EMCGameplayUIPreviewScenario::LowResources;
        case EMCUnifiedUIScreen::Alert:return Long || Empty || BossCount;
        case EMCUnifiedUIScreen::Tutorial:return Long || Empty || Scenario==EMCGameplayUIPreviewScenario::Victory;
        default:return false; // Main menu, base HUD, Settings and Boosters have no scenario fixture.
        }
    }

    bool HasUnifiedScenarioChoices(const FPreviewSession& Session)
    {
        for(const auto& Choice:UnifiedScenarios)
            if(Choice.Value!=EMCGameplayUIPreviewScenario::Default && SupportsUnifiedScenario(Session,Choice.Value)) return true;
        return false;
    }

    void NormalizeUnifiedScenario(FPreviewSession& Session)
    {
        if(!SupportsUnifiedScenario(Session,Session.UnifiedScenario)) Session.UnifiedScenario=EMCGameplayUIPreviewScenario::Default;
    }

    class FGameplayPreviewExtender final : public IBlueprintWidgetCustomizationExtender
    {
    public:
        void Start()
        {
            Ticker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this,&FGameplayPreviewExtender::Tick));
            PropertyHandle=FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this,&FGameplayPreviewExtender::OnPropertyChanged);
            UndoHandle=FEditorDelegates::PostUndoRedo.AddRaw(this,&FGameplayPreviewExtender::OnUndoRedo);
            ObserveAssetEditors();
        }

        void StopAll()
        {
            FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
            FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyHandle);
            FEditorDelegates::PostUndoRedo.Remove(UndoHandle);
            if (auto* Assets=ObservedAssetEditors.Get()) Assets->OnAssetOpenedInEditor().Remove(AssetOpenedHandle);
            ObservedAssetEditors.Reset();
            for (auto& Session:Sessions)
                if (auto Editor=Session.Editor.Pin()) Editor->OnWidgetPreviewReady.Remove(Session.RebuildHandle);
            Sessions.Reset();
            if (auto Tab=ThemeTab.Pin()) Tab->RequestCloseTab();
            ThemePanel.Reset();
        }

        virtual void CustomizeDetails(IDetailLayoutBuilder& Layout,const TArrayView<UWidget*>,const TSharedRef<FWidgetBlueprintEditor>& Editor) override
        {
            if (!IsGameplayEditor(Editor)) return;
            SessionFor(Editor);
            const TWeakPtr<FWidgetBlueprintEditor> WeakEditor=Editor;
            const TWeakPtr<FGameplayPreviewExtender> WeakSelf=StaticCastSharedRef<FGameplayPreviewExtender>(AsShared());
            auto& Category=Layout.EditCategory(TEXT("Gameplay UI Preview"),LOCTEXT("PreviewCategory","Gameplay UI Preview"),ECategoryPriority::Important);
            Category.AddCustomRow(LOCTEXT("PreviewSearch","Preview Play Stop Reset Theme"))
            .WholeRowContent()
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0,4)
                [
                    SNew(STextBlock).Text(LOCTEXT("PreviewHelp","Designer preview only. Set Preview Culture / Scenario; use Designer Screen Size and DPI to check anchors and wrapping. No PIE or gameplay changes."))
                    .AutoWrapText(true)
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().Padding(0,0,4,0)
                    [MakeButton(LOCTEXT("Preview","Preview"),WeakSelf,WeakEditor,0)]
                    + SHorizontalBox::Slot().AutoWidth().Padding(0,0,4,0)
                    [MakeButton(LOCTEXT("Play","Play"),WeakSelf,WeakEditor,1)]
                    + SHorizontalBox::Slot().AutoWidth().Padding(0,0,4,0)
                    [MakeButton(LOCTEXT("Stop","Stop"),WeakSelf,WeakEditor,2)]
                    + SHorizontalBox::Slot().AutoWidth()
                    [MakeButton(LOCTEXT("Reset","Reset"),WeakSelf,WeakEditor,3)]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)
                [
                    SNew(SButton).Text(LOCTEXT("ThemeButton","Gameplay UI Theme…"))
                    .OnClicked_Lambda([WeakEditor]
                    {
                        auto* Widget=GetGameplayPreview(WeakEditor.Pin());
                        MCGameplayUIPreview::OpenThemeEditor(Widget?Widget->GetResolvedTheme():nullptr);
                        return FReply::Handled();
                    })
                ]
            ];
            Category.AddCustomRow(LOCTEXT("ProfilePreviewSearch","Current New UI profile preview"))
            .WholeRowContent()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0,4,6,4)
                [SNew(SButton).Text(LOCTEXT("PreviewCurrentUI","Current UI"))
                    .OnClicked_Lambda([WeakSelf,WeakEditor]{if(auto Self=WeakSelf.Pin()) Self->SetPreviewVersion(WeakEditor.Pin(),EMCUIVersion::Current);return FReply::Handled();})]
                + SHorizontalBox::Slot().AutoWidth().Padding(0,4)
                [SNew(SButton).Text(LOCTEXT("PreviewNewUI","New UI"))
                    .OnClicked_Lambda([WeakSelf,WeakEditor]{if(auto Self=WeakSelf.Pin()) Self->SetPreviewVersion(WeakEditor.Pin(),EMCUIVersion::New);return FReply::Handled();})]
            ];
            if (IsMainMenuEditor(Editor))
            {
                Category.AddCustomRow(LOCTEXT("MainMenuPreviewSearch","Main menu page language screen size DPI"))
                .WholeRowContent()
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,2)
                    [SNew(STextBlock).Text(LOCTEXT("MainMenuPage","Menu page (preview only)"))]
                    + SVerticalBox::Slot().AutoHeight()
                    [MakeMenuSelector(WeakSelf,WeakEditor,false)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,2)
                    [SNew(STextBlock).Text(LOCTEXT("MainMenuCulture","Language (preview only)"))]
                    + SVerticalBox::Slot().AutoHeight()
                    [MakeMenuSelector(WeakSelf,WeakEditor,true)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)
                    [
                        SNew(STextBlock).Text(LOCTEXT("MainMenuPreviewHelp",
                            "Edit the real widgets in Hierarchy. Use Designer Screen Size and DPI for different displays. Page and language choices stay in this editor session; they do not change the game's settings."))
                        .AutoWrapText(true)
                    ]
                ];
            }
            const auto* Blueprint=Editor->GetWidgetBlueprintObj();
            const auto Scenarios=WidgetScenariosForClass(Blueprint?Blueprint->ParentClass.Get():nullptr);
            if(!Scenarios.IsEmpty())
            {
                Category.AddCustomRow(LOCTEXT("WidgetScenarioSearch","Widget preview scenario"))
                .WholeRowContent()
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,2)
                    [SNew(STextBlock).Text(LOCTEXT("WidgetScenarioLabel","Scenario (preview only)"))]
                    + SVerticalBox::Slot().AutoHeight()[MakeWidgetScenarioSelector(WeakSelf,WeakEditor,Scenarios)]
                ];
            }
            if(Blueprint && Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(UMCUnifiedUIWidget::StaticClass()))
            {
                Category.AddCustomRow(LOCTEXT("UnifiedScreenSearch","Interface screen language PSD Hub Reward Frontend GameplayMenu Catch HUD Loading Boss Alert Settings Tutorial Emotes mode page scenario"))
                .WholeRowContent()
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,2)
                    [SNew(STextBlock).Text(LOCTEXT("UnifiedScreen","Interface screen (preview only)"))]
                    + SVerticalBox::Slot().AutoHeight()[MakeUnifiedSelector(WeakSelf,WeakEditor,false)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,2)
                    [SNew(STextBlock).Text(LOCTEXT("UnifiedLanguage","Language (preview only)"))]
                    + SVerticalBox::Slot().AutoHeight()[MakeUnifiedSelector(WeakSelf,WeakEditor,true)]
                    + SVerticalBox::Slot().AutoHeight()
                    [MakeUnifiedModeSection(WeakSelf,WeakEditor,EMCUnifiedUIScreen::Hub,LOCTEXT("UnifiedHubMode","Hub mode (preview only)"),&FPreviewSession::UnifiedHubMode,UnifiedHubModes)]
                    + SVerticalBox::Slot().AutoHeight()
                    [MakeUnifiedModeSection(WeakSelf,WeakEditor,EMCUnifiedUIScreen::Team,LOCTEXT("UnifiedTeamMode","Team mode (preview only)"),&FPreviewSession::UnifiedTeamMode,UnifiedTeamModes)]
                    + SVerticalBox::Slot().AutoHeight()
                    [MakeUnifiedModeSection(WeakSelf,WeakEditor,EMCUnifiedUIScreen::Reward,LOCTEXT("UnifiedRewardMode","Reward mode (preview only)"),&FPreviewSession::UnifiedRewardMode,UnifiedRewardModes)]
                    + SVerticalBox::Slot().AutoHeight()
                    [MakeUnifiedModeSection(WeakSelf,WeakEditor,EMCUnifiedUIScreen::Frontend,LOCTEXT("UnifiedFrontendPage","Frontend page (preview only)"),&FPreviewSession::UnifiedFrontendPage,UnifiedFrontendPages)]
                    + SVerticalBox::Slot().AutoHeight()
                    [MakeUnifiedModeSection(WeakSelf,WeakEditor,EMCUnifiedUIScreen::GameplayMenu,LOCTEXT("UnifiedGameplayMenuMode","Gameplay menu page (preview only)"),&FPreviewSession::UnifiedGameplayMenuMode,UnifiedGameplayMenuModes)]
                    + SVerticalBox::Slot().AutoHeight()
                    [MakeUnifiedModeSection(WeakSelf,WeakEditor,EMCUnifiedUIScreen::Catch,LOCTEXT("UnifiedCatchMode","Catch mode (preview only)"),&FPreviewSession::UnifiedCatchMode,UnifiedCatchModes)]
                    + SVerticalBox::Slot().AutoHeight()
                    [MakeUnifiedModeSection(WeakSelf,WeakEditor,EMCUnifiedUIScreen::HUDExtras,LOCTEXT("UnifiedHUDExtrasMode","HUD overlay mode (preview only)"),&FPreviewSession::UnifiedHUDExtrasMode,UnifiedHUDExtrasModes)]
                    + SVerticalBox::Slot().AutoHeight()
                    [MakeUnifiedModeSection(WeakSelf,WeakEditor,EMCUnifiedUIScreen::Loading,LOCTEXT("UnifiedLoadingMode","Loading mode (preview only)"),&FPreviewSession::UnifiedLoadingMode,UnifiedLoadingModes)]
                    + SVerticalBox::Slot().AutoHeight()
                    [MakeUnifiedModeSection(WeakSelf,WeakEditor,EMCUnifiedUIScreen::Boss,LOCTEXT("UnifiedBossMode","Boss mode (preview only)"),&FPreviewSession::UnifiedBossMode,UnifiedBossModes)]
                    + SVerticalBox::Slot().AutoHeight()[MakeUnifiedScenarioSection(WeakSelf,WeakEditor)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,2)
                    [SNew(STextBlock).Text(LOCTEXT("UnifiedSettingsTab","Settings tab (preview only)"))
                        .Visibility_Lambda([WeakSelf,WeakEditor]
                        {
                            if(auto Self=WeakSelf.Pin()) if(const auto* Session=Self->FindSession(WeakEditor.Pin()))
                                return Session->UnifiedScreen==EMCUnifiedUIScreen::Settings?EVisibility::Visible:EVisibility::Collapsed;
                            return EVisibility::Collapsed;
                        })]
                    + SVerticalBox::Slot().AutoHeight()[MakeUnifiedSettingsTabSelector(WeakSelf,WeakEditor)]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SVerticalBox).Visibility_Lambda([WeakSelf,WeakEditor]
                        {
                            if(auto Self=WeakSelf.Pin()) if(const auto* Session=Self->FindSession(WeakEditor.Pin()))
                                return Session->UnifiedScreen==EMCUnifiedUIScreen::Tutorial?EVisibility::Visible:EVisibility::Collapsed;
                            return EVisibility::Collapsed;
                        })
                        + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,2)
                        [SNew(STextBlock).Text(LOCTEXT("UnifiedTutorialMode","Tutorial mode (preview only)"))]
                        + SVerticalBox::Slot().AutoHeight()
                        [MakeUnifiedOverlaySelector(WeakSelf,WeakEditor,&FPreviewSession::UnifiedTutorialMode,UnifiedTutorialModes)]
                        + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,2)
                        [SNew(STextBlock).Text(LOCTEXT("UnifiedTutorialLesson","Tutorial lesson (preview only)"))]
                        + SVerticalBox::Slot().AutoHeight()
                        [MakeUnifiedOverlaySelector(WeakSelf,WeakEditor,&FPreviewSession::UnifiedTutorialLesson,UnifiedTutorialLessons)]
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SVerticalBox).Visibility_Lambda([WeakSelf,WeakEditor]
                        {
                            if(auto Self=WeakSelf.Pin()) if(const auto* Session=Self->FindSession(WeakEditor.Pin()))
                                return Session->UnifiedScreen==EMCUnifiedUIScreen::Emotes?EVisibility::Visible:EVisibility::Collapsed;
                            return EVisibility::Collapsed;
                        })
                        + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,2)
                        [SNew(STextBlock).Text(LOCTEXT("UnifiedEmoteMode","Emote mode (preview only)"))]
                        + SVerticalBox::Slot().AutoHeight()
                        [MakeUnifiedOverlaySelector(WeakSelf,WeakEditor,&FPreviewSession::UnifiedEmoteMode,UnifiedEmoteModes)]
                        + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,2)
                        [SNew(STextBlock).Text(LOCTEXT("UnifiedEmoteCategory","Emote category (preview only)"))]
                        + SVerticalBox::Slot().AutoHeight()
                        [MakeUnifiedOverlaySelector(WeakSelf,WeakEditor,&FPreviewSession::UnifiedEmoteCategory,UnifiedEmoteCategories)]
                    ]
                ];
            }
        }

        TSharedRef<SDockTab> SpawnThemeTab(const FSpawnTabArgs&)
        {
            TSharedRef<SDockTab> Tab=SNew(SDockTab).TabRole(ETabRole::NomadTab)
                [SAssignNew(ThemePanel,SGameplayUIThemePanel)];
            ThemeTab=Tab;
            if (RequestedTheme.IsValid()) ThemePanel->SetTheme(RequestedTheme.Get());
            return Tab;
        }

        void OpenTheme(UMCGameplayUITheme* Theme)
        {
            RequestedTheme=Theme;
            FGlobalTabmanager::Get()->TryInvokeTab(ThemeTabName);
            if (ThemePanel.IsValid()) ThemePanel->SetTheme(Theme);
        }

    private:
        void SetPreviewVersion(const TSharedPtr<FWidgetBlueprintEditor>& Editor,EMCUIVersion Version)
        {
            if(!GetGameplayPreview(Editor)) return;
            auto& Session=SessionFor(Editor.ToSharedRef());
            Session.bHasPresentationSelection=true;Session.PresentationVersion=Version;
            Execute(Editor,0);
        }

        void ObserveAssetEditors()
        {
            if (ObservedAssetEditors.IsValid() || !GEditor) return;
            auto* Assets=GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
            if (!Assets) return;
            ObservedAssetEditors=Assets;
            AssetOpenedHandle=Assets->OnAssetOpenedInEditor().AddRaw(this,&FGameplayPreviewExtender::OnAssetOpened);
            // Live Coding can register us after the menu Designer is already open.
            for (UObject* Asset:Assets->GetAllEditedAssets())
                OnAssetOpened(Asset,Assets->FindEditorForAsset(Asset,false));
        }

        void OnAssetOpened(UObject* Asset,IAssetEditorInstance* Instance)
        {
            const auto* Blueprint=Cast<UWidgetBlueprint>(Asset);
            if (!Blueprint || !Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(UMCGameplayUIWidget::StaticClass())
                || !Instance || Instance->GetEditorName()!=FName(TEXT("WidgetBlueprintEditor"))) return;
            // UE's WidgetBlueprintEditor toolkit owns this concrete editor. Register
            // before Details has a selection so its first ready tree is synchronized.
            auto Editor=StaticCastSharedRef<FWidgetBlueprintEditor>(static_cast<FWidgetBlueprintEditor*>(Instance)->AsShared());
            if (!IsGameplayEditor(Editor)) return;
            auto& Session=SessionFor(Editor);
            if (auto* Widget=GetGameplayPreview(Editor))
            {
                Session.Preview=Widget;
                CaptureMenuSelection(Session,Widget);
                RestoreMenuSelection(Session,Widget);
                Widget->ApplyEditorPreview(Session.Time);
                MCGameplayUIPreview::SynchronizeFixtureTree(Widget);
                Widget->InvalidateLayoutAndVolatility();
                QueueGeometrySettle(Session);
            }
        }

        static FName InitialMenuState(const UMCMainMenuWidget* Menu)
        {
            EMCMainMenuPage Page=Menu->PreviewPage;
            const FString State=Menu->PreviewState.ToString();
            const int64 StatePage=StaticEnum<EMCMainMenuPage>()->GetValueByNameString(State);
            if (State.StartsWith(TEXT("Settings"))) Page=EMCMainMenuPage::Settings;
            else if (StatePage>=0 && StatePage<=int64(EMCMainMenuPage::Connection)) Page=EMCMainMenuPage(StatePage);
            int32 Tab=FMath::Clamp(Menu->PreviewSettingsTab,0,4);
            if (State==TEXT("SettingsAudio")) Tab=1;
            else if (State==TEXT("SettingsLanguage")) Tab=2;
            else if (State==TEXT("SettingsControls")) Tab=3;
            else if (State==TEXT("SettingsVoice")) Tab=4;
            for (const auto& Choice:MainMenuPreviewChoices)
                if (Choice.Page==Page && (Page!=EMCMainMenuPage::Settings || Choice.SettingsTab==Tab)) return FName(Choice.State);
            return TEXT("Main");
        }

        static void CaptureMenuSelection(FPreviewSession& Session,UMCGameplayUIWidget* Widget)
        {
            if(Widget && !Session.bHasWidgetScenario && !WidgetScenariosForClass(Widget->GetClass()).IsEmpty())
            {
                Session.WidgetScenario=Widget->PreviewState;
                Session.bHasWidgetScenario=true;
            }
            if(const auto* Root=Cast<UMCUnifiedUIWidget>(Widget);Root && !Session.bHasUnifiedSelection)
            {
                Session.UnifiedScreen=Root->PreviewScreen;
                Session.UnifiedSettingsTab=FMath::Clamp(Root->PreviewSettingsTab,0,4);
                if(Root->WidgetTree) if(const auto* Team=Root->WidgetTree->FindWidget<UMCUnifiedTeamWidget>(TEXT("UnifiedTeam")))
                    if(Team->PreviewState==TEXT("HubLocal") || Team->PreviewState==TEXT("HubHost") || Team->PreviewState==TEXT("HubGuest"))
                        Session.UnifiedTeamMode=Team->PreviewState;
                Session.UnifiedHubMode=Root->PreviewHubMode;Session.UnifiedRewardMode=Root->PreviewRewardMode;Session.UnifiedGameplayMenuMode=Root->PreviewGameplayMenuMode;Session.UnifiedCatchMode=Root->PreviewCatchMode;Session.UnifiedHUDExtrasMode=Root->PreviewHUDExtrasMode;Session.UnifiedLoadingMode=Root->PreviewLoadingMode;Session.UnifiedBossMode=Root->PreviewBossMode;Session.UnifiedAlertMode=Root->PreviewAlertMode;Session.UnifiedFrontendPage=Root->PreviewFrontendPage;
                Session.UnifiedTutorialMode=Root->PreviewTutorialMode;
                Session.UnifiedTutorialLesson=Root->PreviewTutorialLesson;
                Session.UnifiedEmoteMode=Root->PreviewEmoteMode;
                Session.UnifiedEmoteCategory=FMath::Clamp(Root->PreviewEmoteCategory,0,2);
                Session.UnifiedCulture=Root->PreviewCulture;
                Session.UnifiedScenario=Root->PreviewScenario;
                NormalizeUnifiedScenario(Session);
                Session.bHasUnifiedSelection=true;
            }
            if (Session.bHasMenuSelection) return;
            if (const auto* Menu=Cast<UMCMainMenuWidget>(Widget))
            {
                Session.MenuState=InitialMenuState(Menu);
                Session.MenuCulture=Menu->PreviewCulture;
                Session.bHasMenuSelection=true;
            }
        }

        static void RestoreMenuSelection(const FPreviewSession& Session,UMCGameplayUIWidget* Widget)
        {
            if(Widget && Widget->IsDesignTime() && Session.bHasPresentationSelection)
            {Widget->bOverridePreviewUIVersion=true;Widget->PreviewUIVersion=Session.PresentationVersion;}
            if(Widget && Widget->IsDesignTime() && Session.bHasWidgetScenario
                && !Widget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
                Widget->PreviewState=Session.WidgetScenario;
            if(auto* Root=Cast<UMCUnifiedUIWidget>(Widget);Root && Root->IsDesignTime() && Session.bHasUnifiedSelection
                && !Root->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
            {
                Root->PreviewScreen=Session.UnifiedScreen;
                Root->PreviewSettingsTab=Session.UnifiedSettingsTab;
                Root->PreviewHubMode=Session.UnifiedHubMode;Root->PreviewRewardMode=Session.UnifiedRewardMode;Root->PreviewGameplayMenuMode=Session.UnifiedGameplayMenuMode;Root->PreviewCatchMode=Session.UnifiedCatchMode;Root->PreviewHUDExtrasMode=Session.UnifiedHUDExtrasMode;Root->PreviewLoadingMode=Session.UnifiedLoadingMode;Root->PreviewBossMode=Session.UnifiedBossMode;Root->PreviewAlertMode=Session.UnifiedAlertMode;Root->PreviewFrontendPage=Session.UnifiedFrontendPage;
                Root->PreviewTutorialMode=Session.UnifiedTutorialMode;
                Root->PreviewTutorialLesson=Session.UnifiedTutorialLesson;
                Root->PreviewEmoteMode=Session.UnifiedEmoteMode;
                Root->PreviewEmoteCategory=Session.UnifiedEmoteCategory;
                Root->PreviewCulture=Session.UnifiedCulture;
                Root->PreviewScenario=Session.UnifiedScenario;
                Root->PreviewState=FName(*StaticEnum<EMCUnifiedUIScreen>()->GetNameStringByValue(int64(Session.UnifiedScreen)));
                if(Root->WidgetTree) if(auto* Team=Root->WidgetTree->FindWidget<UMCUnifiedTeamWidget>(TEXT("UnifiedTeam")))
                    Team->PreviewState=Session.UnifiedTeamMode;
            }
            auto* Menu=Cast<UMCMainMenuWidget>(Widget);
            if (!Menu || !Session.bHasMenuSelection || !Menu->IsDesignTime()
                || Menu->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)) return;
            for (const auto& Choice:MainMenuPreviewChoices)
                if (Session.MenuState==FName(Choice.State))
                {
                    Menu->PreviewState=Session.MenuState;
                    Menu->PreviewPage=Choice.Page;
                    Menu->PreviewSettingsTab=Choice.SettingsTab;
                    Menu->PreviewCulture=Session.MenuCulture;
                    return;
                }
        }

        FPreviewSession* FindSession(const TSharedPtr<FWidgetBlueprintEditor>& Editor)
        {
            if (!Editor) return nullptr;
            for (auto& Session:Sessions) if (Session.Editor.Pin()==Editor) return &Session;
            return nullptr;
        }

        static TSharedRef<SWidget> MakeWidgetScenarioSelector(TWeakPtr<FGameplayPreviewExtender> WeakSelf,
            TWeakPtr<FWidgetBlueprintEditor> WeakEditor,TConstArrayView<TUnifiedOverlayPreviewChoice<FName>> Choices)
        {
            return SNew(SComboButton)
                .IsEnabled_Lambda([WeakEditor]{return GetGameplayPreview(WeakEditor.Pin())!=nullptr;})
                .OnGetMenuContent_Lambda([WeakSelf,WeakEditor,Choices]() -> TSharedRef<SWidget>
                {
                    FMenuBuilder Menu(true,nullptr);
                    for(const auto& Choice:Choices)
                    {
                        const FName State=Choice.Value;
                        Menu.AddMenuEntry(Choice.Label,LOCTEXT("WidgetScenarioPreviewOnly","Changes only the Designer fixture."),FSlateIcon(),
                            FUIAction(FExecuteAction::CreateLambda([WeakSelf,WeakEditor,State]
                            {
                                if(auto Self=WeakSelf.Pin()) if(auto Editor=WeakEditor.Pin())
                                {
                                    auto& Session=Self->SessionFor(Editor.ToSharedRef());
                                    Session.WidgetScenario=State;
                                    Session.bHasWidgetScenario=true;
                                    Self->Execute(Editor,0);
                                }
                            }),FCanExecuteAction(),FIsActionChecked::CreateLambda([WeakSelf,WeakEditor,State]
                            {
                                if(auto Self=WeakSelf.Pin()) if(const auto* Session=Self->FindSession(WeakEditor.Pin()))
                                    return Session->WidgetScenario==State;
                                return false;
                            })),NAME_None,EUserInterfaceActionType::RadioButton);
                    }
                    return Menu.MakeWidget();
                })
                .ButtonContent()[SNew(STextBlock).Text_Lambda([WeakSelf,WeakEditor,Choices]
                {
                    if(auto Self=WeakSelf.Pin()) if(const auto* Session=Self->FindSession(WeakEditor.Pin()))
                        for(const auto& Choice:Choices) if(Session->WidgetScenario==Choice.Value) return Choice.Label;
                    return LOCTEXT("WidgetScenarioDefault","Default preview");
                })];
        }

        FText MenuSelectionLabel(const TSharedPtr<FWidgetBlueprintEditor>& Editor,bool bCulture)
        {
            const auto* Session=FindSession(Editor);
            const auto* Menu=Cast<UMCMainMenuWidget>(GetGameplayPreview(Editor));
            if (bCulture)
            {
                const FString Culture=Session && Session->bHasMenuSelection?Session->MenuCulture:Menu?Menu->PreviewCulture:FString();
                for (const auto& Choice:MainMenuPreviewCultures)
                    if (Culture==Choice.Code) return FText::FromString(Choice.Label);
                return FText::FromString(Culture);
            }
            const FName State=Session && Session->bHasMenuSelection?Session->MenuState:Menu?InitialMenuState(Menu):FName(TEXT("Main"));
            for (const auto& Choice:MainMenuPreviewChoices)
                if (State==FName(Choice.State)) return FText::FromString(Choice.Label);
            return FText::FromName(State);
        }

        void SelectMenuPreview(const TSharedPtr<FWidgetBlueprintEditor>& Editor,FName State,const FString& Culture,bool bCulture)
        {
            auto* Menu=Cast<UMCMainMenuWidget>(GetGameplayPreview(Editor));
            if (!Menu) return;
            auto& Session=SessionFor(Editor.ToSharedRef());
            CaptureMenuSelection(Session,Menu);
            if (bCulture) Session.MenuCulture=Culture;
            else Session.MenuState=State;
            RestoreMenuSelection(Session,Menu);
            Menu->ApplyEditorPreview(Session.Time);
            MCGameplayUIPreview::SynchronizeFixtureTree(Menu);
            Menu->InvalidateLayoutAndVolatility();
            QueueGeometrySettle(Session);
        }

        static TSharedRef<SWidget> MakeMenuSelector(TWeakPtr<FGameplayPreviewExtender> WeakSelf,TWeakPtr<FWidgetBlueprintEditor> WeakEditor,bool bCulture)
        {
            return SNew(SComboButton)
                .IsEnabled_Lambda([WeakEditor]{return Cast<UMCMainMenuWidget>(GetGameplayPreview(WeakEditor.Pin()))!=nullptr;})
                .OnGetMenuContent_Lambda([WeakSelf,WeakEditor,bCulture]() -> TSharedRef<SWidget>
                {
                    FMenuBuilder Menu(true,nullptr);
                    auto AddChoice=[&](const FText& Label,FName State,const FString& Culture)
                    {
                        Menu.AddMenuEntry(Label,LOCTEXT("MainMenuChoiceHelp","Changes only this transient Designer preview."),FSlateIcon(),
                            FUIAction(FExecuteAction::CreateLambda([WeakSelf,WeakEditor,State,Culture,bCulture]
                            {
                                if (auto Self=WeakSelf.Pin()) Self->SelectMenuPreview(WeakEditor.Pin(),State,Culture,bCulture);
                            }),FCanExecuteAction(),FIsActionChecked::CreateLambda([WeakSelf,WeakEditor,State,Culture,bCulture]
                            {
                                if (auto Self=WeakSelf.Pin())
                                    if (const auto* Session=Self->FindSession(WeakEditor.Pin()))
                                        return Session->bHasMenuSelection && (bCulture?Session->MenuCulture==Culture:Session->MenuState==State);
                                return false;
                            })),NAME_None,EUserInterfaceActionType::RadioButton);
                    };
                    if (bCulture)
                        for (const auto& Choice:MainMenuPreviewCultures) AddChoice(FText::FromString(Choice.Label),NAME_None,Choice.Code);
                    else
                        for (const auto& Choice:MainMenuPreviewChoices) AddChoice(FText::FromString(Choice.Label),FName(Choice.State),FString());
                    return Menu.MakeWidget();
                })
                .ButtonContent()
                [
                    SNew(STextBlock).Text_Lambda([WeakSelf,WeakEditor,bCulture]
                    {
                        if (auto Self=WeakSelf.Pin()) return Self->MenuSelectionLabel(WeakEditor.Pin(),bCulture);
                        return FText::GetEmpty();
                    })
                ];
        }

        static TSharedRef<SWidget> MakeUnifiedSelector(TWeakPtr<FGameplayPreviewExtender> WeakSelf,TWeakPtr<FWidgetBlueprintEditor> WeakEditor,bool bCulture)
        {
            return SNew(SComboButton)
                .OnGetMenuContent_Lambda([WeakSelf,WeakEditor,bCulture]() -> TSharedRef<SWidget>
                {
                    FMenuBuilder Menu(true,nullptr);
                    auto Add=[&](const FText& Label,EMCUnifiedUIScreen Screen,FString Culture,bool bReady)
                    {
                        Menu.AddMenuEntry(Label,FText::GetEmpty(),FSlateIcon(),FUIAction(
                            FExecuteAction::CreateLambda([WeakSelf,WeakEditor,Screen,Culture,bCulture]
                            {
                                if(auto Self=WeakSelf.Pin()) if(auto Editor=WeakEditor.Pin()) {
                                    auto& Session=Self->SessionFor(Editor.ToSharedRef());Session.bHasUnifiedSelection=true;
                                    if(bCulture) Session.UnifiedCulture=Culture;else Session.UnifiedScreen=Screen;
                                    NormalizeUnifiedScenario(Session);
                                    Self->Execute(Editor,0);
                                }
                            }),FCanExecuteAction::CreateLambda([bReady]{return bReady;}),
                            FIsActionChecked::CreateLambda([WeakSelf,WeakEditor,Screen,Culture,bCulture,bReady]
                            {
                                if(bReady) if(auto Self=WeakSelf.Pin()) if(const auto* Session=Self->FindSession(WeakEditor.Pin()))
                                    return bCulture?Session->UnifiedCulture==Culture:Session->UnifiedScreen==Screen;
                                return false;
                            })),NAME_None,EUserInterfaceActionType::RadioButton);
                    };
                    if(bCulture) for(const auto& Choice:MainMenuPreviewCultures) Add(FText::FromString(Choice.Label),EMCUnifiedUIScreen::Menu,Choice.Code,true);
                    else {
                        Add(LOCTEXT("UnifiedMenu","Main menu"),EMCUnifiedUIScreen::Menu,FString(),true);
                        Add(LOCTEXT("UnifiedHUD","Gameplay HUD"),EMCUnifiedUIScreen::Gameplay,FString(),true);
                        Add(LOCTEXT("UnifiedSettings","Settings"),EMCUnifiedUIScreen::Settings,FString(),true);
                        Add(LOCTEXT("UnifiedTeam","Team"),EMCUnifiedUIScreen::Team,FString(),true);
                        Add(LOCTEXT("UnifiedWardrobe","Wardrobe"),EMCUnifiedUIScreen::Wardrobe,FString(),true);
                        Add(LOCTEXT("UnifiedShop","Boosters shop"),EMCUnifiedUIScreen::Boosters,FString(),true);
                        Add(LOCTEXT("UnifiedHub","Hub"),EMCUnifiedUIScreen::Hub,FString(),true);
                        Add(LOCTEXT("UnifiedReward","Reward"),EMCUnifiedUIScreen::Reward,FString(),true);
                        Add(LOCTEXT("UnifiedFrontend","Join / lobby / confirmations"),EMCUnifiedUIScreen::Frontend,FString(),true);
                        Add(LOCTEXT("UnifiedGameplayMenu","Pause / connection / audio"),EMCUnifiedUIScreen::GameplayMenu,FString(),true);
                        Menu.AddMenuSeparator();
                        Add(LOCTEXT("UnifiedCatch","Fishing catch"),EMCUnifiedUIScreen::Catch,FString(),true);
                        Add(LOCTEXT("UnifiedHUDExtras","HUD: results / swim / status"),EMCUnifiedUIScreen::HUDExtras,FString(),true);
                        Add(LOCTEXT("UnifiedLoading","Loading / waiting / retry"),EMCUnifiedUIScreen::Loading,FString(),true);
                        Add(LOCTEXT("UnifiedBoss","Boss health"),EMCUnifiedUIScreen::Boss,FString(),true);
                        Add(LOCTEXT("UnifiedAlert","Gameplay alert"),EMCUnifiedUIScreen::Alert,FString(),true);
                        Add(LOCTEXT("UnifiedTutorial","Tutorial"),EMCUnifiedUIScreen::Tutorial,FString(),true);
                        Add(LOCTEXT("UnifiedEmotes","Emotes"),EMCUnifiedUIScreen::Emotes,FString(),true);
                    }
                    return Menu.MakeWidget();
                })
                .ButtonContent()[SNew(STextBlock).Text_Lambda([WeakSelf,WeakEditor,bCulture]
                {
                    if(auto Self=WeakSelf.Pin()) if(const auto* Session=Self->FindSession(WeakEditor.Pin())) {
                        if(bCulture)
                        {
                            for(const auto& Choice:MainMenuPreviewCultures)
                                if(Session->UnifiedCulture==Choice.Code) return FText::FromString(Choice.Label);
                            return FText::FromString(Session->UnifiedCulture);
                        }
                        switch(Session->UnifiedScreen)
                        {
                        case EMCUnifiedUIScreen::Menu:return LOCTEXT("UnifiedMenu","Main menu");
                        case EMCUnifiedUIScreen::Gameplay:return LOCTEXT("UnifiedHUD","Gameplay HUD");
                        case EMCUnifiedUIScreen::Settings:return LOCTEXT("UnifiedSettings","Settings");
                        case EMCUnifiedUIScreen::Team:return LOCTEXT("UnifiedTeam","Team");
                        case EMCUnifiedUIScreen::Wardrobe:return LOCTEXT("UnifiedWardrobe","Wardrobe");
                        case EMCUnifiedUIScreen::Boosters:return LOCTEXT("UnifiedShop","Boosters shop");
                        case EMCUnifiedUIScreen::Hub:return LOCTEXT("UnifiedHub","Hub");
                        case EMCUnifiedUIScreen::Reward:return LOCTEXT("UnifiedReward","Reward");
                        case EMCUnifiedUIScreen::Catch:return LOCTEXT("UnifiedCatch","Fishing catch");
                        case EMCUnifiedUIScreen::HUDExtras:return LOCTEXT("UnifiedHUDExtras","HUD: results / swim / status");
                        case EMCUnifiedUIScreen::Loading:return LOCTEXT("UnifiedLoading","Loading / waiting / retry");
                        case EMCUnifiedUIScreen::Boss:return LOCTEXT("UnifiedBoss","Boss health");
                        case EMCUnifiedUIScreen::Alert:return LOCTEXT("UnifiedAlert","Gameplay alert");
                        case EMCUnifiedUIScreen::Tutorial:return LOCTEXT("UnifiedTutorial","Tutorial");
                        case EMCUnifiedUIScreen::Emotes:return LOCTEXT("UnifiedEmotes","Emotes");
                        case EMCUnifiedUIScreen::Frontend:return LOCTEXT("UnifiedFrontend","Join / lobby / confirmations");
                        case EMCUnifiedUIScreen::GameplayMenu:return LOCTEXT("UnifiedGameplayMenu","Pause / connection / audio");
                        default:return FText::FromString(StaticEnum<EMCUnifiedUIScreen>()->GetNameStringByValue(int64(Session->UnifiedScreen)));
                        }
                    }
                    return FText::GetEmpty();
                })];
        }

        static FText UnifiedSettingsTabLabel(int32 Tab)
        {
            switch(Tab)
            {
            case 1:return LOCTEXT("UnifiedSettingsAudio","Audio");
            case 2:return LOCTEXT("UnifiedSettingsLanguage","Language");
            case 3:return LOCTEXT("UnifiedSettingsControls","Controls");
            case 4:return LOCTEXT("UnifiedSettingsVoice","Voice");
            default:return LOCTEXT("UnifiedSettingsGraphics","Graphics");
            }
        }

        static TSharedRef<SWidget> MakeUnifiedSettingsTabSelector(TWeakPtr<FGameplayPreviewExtender> WeakSelf,TWeakPtr<FWidgetBlueprintEditor> WeakEditor)
        {
            return SNew(SComboButton)
                .Visibility_Lambda([WeakSelf,WeakEditor]
                {
                    if(auto Self=WeakSelf.Pin()) if(const auto* Session=Self->FindSession(WeakEditor.Pin()))
                        return Session->UnifiedScreen==EMCUnifiedUIScreen::Settings?EVisibility::Visible:EVisibility::Collapsed;
                    return EVisibility::Collapsed;
                })
                .OnGetMenuContent_Lambda([WeakSelf,WeakEditor]() -> TSharedRef<SWidget>
                {
                    FMenuBuilder Menu(true,nullptr);
                    for(int32 Tab=0;Tab<5;++Tab)
                        Menu.AddMenuEntry(UnifiedSettingsTabLabel(Tab),
                            LOCTEXT("UnifiedSettingsTabHelp","Changes only the transient Designer settings page; player settings stay unchanged."),
                            FSlateIcon(),FUIAction(
                                FExecuteAction::CreateLambda([WeakSelf,WeakEditor,Tab]
                                {
                                    if(auto Self=WeakSelf.Pin()) if(auto Editor=WeakEditor.Pin())
                                    {
                                        auto& Session=Self->SessionFor(Editor.ToSharedRef());
                                        Session.bHasUnifiedSelection=true;Session.UnifiedSettingsTab=Tab;
                                        Self->Execute(Editor,0);
                                    }
                                }),FCanExecuteAction(),FIsActionChecked::CreateLambda([WeakSelf,WeakEditor,Tab]
                                {
                                    if(auto Self=WeakSelf.Pin()) if(const auto* Session=Self->FindSession(WeakEditor.Pin()))
                                        return Session->UnifiedSettingsTab==Tab;
                                    return false;
                                })),NAME_None,EUserInterfaceActionType::RadioButton);
                    return Menu.MakeWidget();
                })
                .ButtonContent()[SNew(STextBlock).Text_Lambda([WeakSelf,WeakEditor]
                {
                    if(auto Self=WeakSelf.Pin()) if(const auto* Session=Self->FindSession(WeakEditor.Pin()))
                        return UnifiedSettingsTabLabel(Session->UnifiedSettingsTab);
                    return FText::GetEmpty();
                })];
        }

        template<typename T,SIZE_T Count>
        static TSharedRef<SWidget> MakeUnifiedOverlaySelector(TWeakPtr<FGameplayPreviewExtender> WeakSelf,TWeakPtr<FWidgetBlueprintEditor> WeakEditor,
            T FPreviewSession::* Field,const TUnifiedOverlayPreviewChoice<T> (&Choices)[Count])
        {
            // All choice arrays have static storage. Only the transient session field is changed.
            const auto* Entries=Choices;
            return SNew(SComboButton)
                .IsEnabled_Lambda([WeakEditor]{return Cast<UMCUnifiedUIWidget>(GetGameplayPreview(WeakEditor.Pin()))!=nullptr;})
                .OnGetMenuContent_Lambda([WeakSelf,WeakEditor,Field,Entries]() -> TSharedRef<SWidget>
                {
                    FMenuBuilder Menu(true,nullptr);
                    for(SIZE_T Index=0;Index<Count;++Index) {
                        const T Value=Entries[Index].Value;
                        Menu.AddMenuEntry(Entries[Index].Label,
                            LOCTEXT("UnifiedOverlayChoiceHelp","Changes only the transient Designer preview; game state and player settings stay unchanged."),
                            FSlateIcon(),FUIAction(FExecuteAction::CreateLambda([WeakSelf,WeakEditor,Field,Value]
                            {
                                if(auto Self=WeakSelf.Pin()) if(auto Editor=WeakEditor.Pin()) {
                                    auto& Session=Self->SessionFor(Editor.ToSharedRef());
                                    Session.bHasUnifiedSelection=true;Session.*Field=Value;NormalizeUnifiedScenario(Session);
                                    // Reload the authored tree when switching roster layouts so
                                    // the compact Hub metrics cannot carry into the arena fixture.
                                    if constexpr(std::is_same_v<T,FName>)
                                        if(Field==&FPreviewSession::UnifiedTeamMode) Editor->RefreshPreview();
                                    Self->Execute(Editor,0);
                                }
                            }),FCanExecuteAction(),FIsActionChecked::CreateLambda([WeakSelf,WeakEditor,Field,Value]
                            {
                                if(auto Self=WeakSelf.Pin()) if(const auto* Session=Self->FindSession(WeakEditor.Pin())) return Session->*Field==Value;
                                return false;
                            })),NAME_None,EUserInterfaceActionType::RadioButton);
                    }
                    return Menu.MakeWidget();
                })
                .ButtonContent()[SNew(STextBlock).Text_Lambda([WeakSelf,WeakEditor,Field,Entries]
                {
                    if(auto Self=WeakSelf.Pin()) if(const auto* Session=Self->FindSession(WeakEditor.Pin()))
                        for(SIZE_T Index=0;Index<Count;++Index) if(Session->*Field==Entries[Index].Value) return Entries[Index].Label;
                    return LOCTEXT("UnifiedOverlayCustom","Custom preview");
                })];
        }

        template<typename T,SIZE_T Count>
        static TSharedRef<SWidget> MakeUnifiedModeSection(TWeakPtr<FGameplayPreviewExtender> WeakSelf,TWeakPtr<FWidgetBlueprintEditor> WeakEditor,
            EMCUnifiedUIScreen Screen,const FText& Label,T FPreviewSession::* Field,const TUnifiedOverlayPreviewChoice<T> (&Choices)[Count])
        {
            return SNew(SVerticalBox)
                .Visibility_Lambda([WeakSelf,WeakEditor,Screen]
                {
                    if(auto Self=WeakSelf.Pin()) if(const auto* Session=Self->FindSession(WeakEditor.Pin()))
                        return Session->UnifiedScreen==Screen?EVisibility::Visible:EVisibility::Collapsed;
                    return EVisibility::Collapsed;
                })
                + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,2)[SNew(STextBlock).Text(Label)]
                + SVerticalBox::Slot().AutoHeight()[MakeUnifiedOverlaySelector(WeakSelf,WeakEditor,Field,Choices)];
        }

        static TSharedRef<SWidget> MakeUnifiedScenarioSection(TWeakPtr<FGameplayPreviewExtender> WeakSelf,TWeakPtr<FWidgetBlueprintEditor> WeakEditor)
        {
            // Alert ignores its FName mode: only these real enum fixtures change that view.
            // Other screens receive only scenarios their current page actually consumes.
            return SNew(SVerticalBox)
                .Visibility_Lambda([WeakSelf,WeakEditor]
                {
                    if(auto Self=WeakSelf.Pin()) if(const auto* Session=Self->FindSession(WeakEditor.Pin()))
                        return HasUnifiedScenarioChoices(*Session)?EVisibility::Visible:EVisibility::Collapsed;
                    return EVisibility::Collapsed;
                })
                + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,2)
                [SNew(STextBlock).Text(LOCTEXT("UnifiedScenario","Scenario (preview only)"))]
                + SVerticalBox::Slot().AutoHeight()
                [SNew(SComboButton)
                    .IsEnabled_Lambda([WeakEditor]{return Cast<UMCUnifiedUIWidget>(GetGameplayPreview(WeakEditor.Pin()))!=nullptr;})
                    .OnGetMenuContent_Lambda([WeakSelf,WeakEditor]() -> TSharedRef<SWidget>
                    {
                        FMenuBuilder Menu(true,nullptr);
                        if(auto Self=WeakSelf.Pin()) if(const auto* Session=Self->FindSession(WeakEditor.Pin()))
                            for(const auto& Choice:UnifiedScenarios) {
                                if(!SupportsUnifiedScenario(*Session,Choice.Value)) continue;
                                const auto Value=Choice.Value;
                                Menu.AddMenuEntry(Choice.Label,
                                    LOCTEXT("UnifiedScenarioHelp","Uses this screen's existing Designer fixture; saved widget defaults and player state stay unchanged."),
                                    FSlateIcon(),FUIAction(FExecuteAction::CreateLambda([WeakSelf,WeakEditor,Value]
                                    {
                                        if(auto Current=WeakSelf.Pin()) if(auto Editor=WeakEditor.Pin()) {
                                            auto& Selected=Current->SessionFor(Editor.ToSharedRef());
                                            if(!SupportsUnifiedScenario(Selected,Value)) return;
                                            Selected.bHasUnifiedSelection=true;Selected.UnifiedScenario=Value;Current->Execute(Editor,0);
                                        }
                                    }),FCanExecuteAction(),FIsActionChecked::CreateLambda([WeakSelf,WeakEditor,Value]
                                    {
                                        if(auto Current=WeakSelf.Pin()) if(const auto* Selected=Current->FindSession(WeakEditor.Pin()))
                                            return Selected->UnifiedScenario==Value;
                                        return false;
                                    })),NAME_None,EUserInterfaceActionType::RadioButton);
                            }
                        return Menu.MakeWidget();
                    })
                    .ButtonContent()[SNew(STextBlock).Text_Lambda([WeakSelf,WeakEditor]
                    {
                        if(auto Self=WeakSelf.Pin()) if(const auto* Session=Self->FindSession(WeakEditor.Pin()))
                            for(const auto& Choice:UnifiedScenarios) if(Session->UnifiedScenario==Choice.Value) return Choice.Label;
                        return FText::GetEmpty();
                    })]
                ];
        }

        static TSharedRef<SWidget> MakeButton(const FText& Label,TWeakPtr<FGameplayPreviewExtender> WeakSelf,TWeakPtr<FWidgetBlueprintEditor> Editor,int32 Action)
        {
            return SNew(SButton).Text(Label)
                .IsEnabled_Lambda([Editor] {return GetGameplayPreview(Editor.Pin())!=nullptr;})
                .OnClicked_Lambda([WeakSelf,Editor,Action]
                {
                    if (auto Self=WeakSelf.Pin()) Self->Execute(Editor.Pin(),Action);
                    return FReply::Handled();
                });
        }

        FPreviewSession& SessionFor(const TSharedRef<FWidgetBlueprintEditor>& Editor)
        {
            for (auto& Session:Sessions) if (Session.Editor.Pin()==Editor) return Session;
            auto& Session=Sessions.AddDefaulted_GetRef();
            Session.Editor=Editor;
            Session.Preview=GetGameplayPreview(Editor);
            CaptureMenuSelection(Session,Session.Preview.Get());
            const TWeakPtr<FGameplayPreviewExtender> WeakSelf=StaticCastSharedRef<FGameplayPreviewExtender>(AsShared());
            const TWeakPtr<FWidgetBlueprintEditor> WeakEditor=Editor;
            Session.RebuildHandle=Editor->OnWidgetPreviewReady.AddLambda([WeakSelf,WeakEditor]
            {
                if (auto Self=WeakSelf.Pin())
                    if (auto CurrentEditor=WeakEditor.Pin())
                    {
                        auto& Current=Self->SessionFor(CurrentEditor.ToSharedRef());
                        Current.bPlaying=false;
                        Current.Time=0;
                        Current.Preview=GetGameplayPreview(CurrentEditor);
                        if (Current.Preview.IsValid())
                        {
                            CaptureMenuSelection(Current,Current.Preview.Get());
                            RestoreMenuSelection(Current,Current.Preview.Get());
                            Current.Preview->ApplyEditorPreview(0.f);
                            MCGameplayUIPreview::SynchronizeFixtureTree(Current.Preview.Get());
                            QueueGeometrySettle(Current);
                        }
                    }
            });
            return Session;
        }

        void Execute(const TSharedPtr<FWidgetBlueprintEditor>& Editor,int32 Action)
        {
            auto* Widget=GetGameplayPreview(Editor);
            if (!Widget) return;
            auto& Session=SessionFor(Editor.ToSharedRef());
            Session.Preview=Widget;
            CaptureMenuSelection(Session,Widget);
            RestoreMenuSelection(Session,Widget);
            Session.bPlaying=Action==1;
            QueueGeometrySettle(Session);
            if (Action==2) return;
            Session.Time=Action==0?FMath::Max(0.f,Widget->PreviewTime):0.f;
            if (Action==3)
            {
                Widget->ResetEditorPreview();
                Widget->PreviewTime=0.f;
                Widget->ApplyEditorPreview(0.f);
            }
            else
            {
                // Explicit Preview reloads authored table content; Play can keep
                // its per-frame fixture cache until the next explicit refresh.
                if (Action==0) Widget->ResetEditorPreview();
                Widget->PreviewTime=Session.Time;
                Widget->ApplyEditorPreview(Session.Time);
            }
            Widget->InvalidateLayoutAndVolatility();
            MCGameplayUIPreview::SynchronizeFixtureTree(Widget);
        }

        bool Tick(float DeltaTime)
        {
            // The module may initialize before GEditor/subsystems are available.
            ObserveAssetEditors();
            for (int32 Index=Sessions.Num()-1;Index>=0;--Index)
            {
                auto& Session=Sessions[Index];
                auto Editor=Session.Editor.Pin();
                if (!Editor || !IsGameplayEditor(Editor))
                {
                    if (Editor) Editor->OnWidgetPreviewReady.Remove(Session.RebuildHandle);
                    Sessions.RemoveAt(Index);
                    continue;
                }
                auto* Widget=GetGameplayPreview(Editor);
                if (Widget!=Session.Preview.Get())
                {
                    Session.bPlaying=false;
                    Session.Time=0.f;
                    Session.Preview=Widget;
                    QueueGeometrySettle(Session);
                    CaptureMenuSelection(Session,Widget);
                    RestoreMenuSelection(Session,Widget);
                    if (Widget && (Session.bHasMenuSelection || Session.bHasUnifiedSelection || Session.bHasWidgetScenario))
                    {
                        Widget->ApplyEditorPreview(0.f);
                        MCGameplayUIPreview::SynchronizeFixtureTree(Widget);
                        Widget->InvalidateLayoutAndVolatility();
                    }
                }
                if (!Widget) continue;
                if (!Session.bPlaying) {
                    if(Session.GeometrySettleFrames>0 && GFrameCounter>Session.SettleQueuedFrame) {
                        --Session.GeometrySettleFrames;
                        Session.SettleQueuedFrame=GFrameCounter;
                        // Reflow the frozen fixture once per frame, without restarting it or advancing time.
                        RestoreMenuSelection(Session,Widget);
                        Widget->PreviewTime=Session.Time;
                        Widget->ApplyEditorPreview(Session.Time);
                        MCGameplayUIPreview::SynchronizeFixtureTree(Widget);
                        Widget->InvalidateLayoutAndVolatility();
                    }
                    continue;
                }
                Session.GeometrySettleFrames=0;
                Session.Time=FMath::Fmod(Session.Time+FMath::Clamp(DeltaTime,0.f,.1f),FMath::Max(.1f,Widget->PreviewDuration));
                Widget->PreviewTime=Session.Time;
                Widget->ApplyEditorPreview(Session.Time);
                MCGameplayUIPreview::SynchronizeFixtureTree(Widget);
                Widget->InvalidateLayoutAndVolatility();
            }
            return true;
        }

        void RefreshTheme(UMCGameplayUITheme* Theme)
        {
            if (!Theme || bApplyingTheme) return;
            TGuardValue<bool> Guard(bApplyingTheme,true);
            Theme->NotifyThemeChanged();
            for (auto& Session:Sessions)
                if (auto* Widget=GetGameplayPreview(Session.Editor.Pin());Widget && Widget->GetResolvedTheme()==Theme)
                {
                    Widget->ApplyTheme();
                    Widget->ApplyEditorPreview(Session.Time);
                    MCGameplayUIPreview::SynchronizeFixtureTree(Widget);
                    Widget->InvalidateLayoutAndVolatility();
                    QueueGeometrySettle(Session);
                }
            if (ThemePanel.IsValid() && ThemePanel->GetTheme()==Theme) ThemePanel->RefreshPreview();
        }

        void OnPropertyChanged(UObject* Object,FPropertyChangedEvent&)
        {
            if (auto* Theme=Cast<UMCGameplayUITheme>(Object)) RefreshTheme(Theme);
        }

        void OnUndoRedo()
        {
            TSet<UMCGameplayUITheme*> Themes;
            for (auto& Session:Sessions)
                if (auto* Widget=GetGameplayPreview(Session.Editor.Pin())) Themes.Add(Widget->GetResolvedTheme());
            if (ThemePanel.IsValid()) Themes.Add(ThemePanel->GetTheme());
            for (auto* Theme:Themes) RefreshTheme(Theme);
        }

        TArray<FPreviewSession> Sessions;
        FTSTicker::FDelegateHandle Ticker;
        FDelegateHandle PropertyHandle,UndoHandle;
        FDelegateHandle AssetOpenedHandle;
        TWeakObjectPtr<UAssetEditorSubsystem> ObservedAssetEditors;
        TSharedPtr<SGameplayUIThemePanel> ThemePanel;
        TWeakPtr<SDockTab> ThemeTab;
        TWeakObjectPtr<UMCGameplayUITheme> RequestedTheme;
        bool bApplyingTheme=false;
    };

    TSharedPtr<FGameplayPreviewExtender> PreviewExtender;
}

void MCGameplayUIPreview::SynchronizeFixtureTree(UMCGameplayUIWidget* Widget)
{
    if(!Widget || !Widget->IsDesignTime() || Widget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)) return;
    // Designer normally forces every wrapper visible/enabled, including collapsed
    // fallbacks and absent boss rows. Override only transient Slate attributes;
    // keep design flags, authored defaults and gameplay initialization untouched.
    auto Sync=[](UWidget* Item)
    {
        if(auto Slate=Item->GetCachedWidget())
        {
            // The public getters prefer cached Slate state, which Designer has
            // forced visible. Read the authored fields instead of feeding that
            // forced value back into the wrapper.
            static const auto* Visibility=FindFProperty<FEnumProperty>(UWidget::StaticClass(),TEXT("Visibility"));
            static const auto* Enabled=FindFProperty<FBoolProperty>(UWidget::StaticClass(),TEXT("bIsEnabled"));
            static const auto* Opacity=FindFProperty<FFloatProperty>(UWidget::StaticClass(),TEXT("RenderOpacity"));
            if(Visibility) Slate->SetVisibility(UWidget::ConvertSerializedVisibilityToRuntime(static_cast<ESlateVisibility>(
                Visibility->GetUnderlyingProperty()->GetSignedIntPropertyValue(Visibility->ContainerPtrToValuePtr<void>(Item)))));
            if(Enabled) Slate->SetEnabled(Enabled->GetPropertyValue_InContainer(Item));
            if(Opacity) Slate->SetRenderOpacity(Opacity->GetPropertyValue_InContainer(Item));
        }
    };
    Sync(Widget);
    if(Widget->WidgetTree) Widget->WidgetTree->ForEachWidgetAndDescendants(Sync);
    Widget->RefreshPresentationGeometry();
}

void MCGameplayUIPreview::EnsureRegistered()
{
    if (IsRunningCommandlet() || !GIsEditor) return;
    if (!PreviewExtender)
    {
        PreviewExtender=MakeShared<FGameplayPreviewExtender>();
        auto& UMG=FModuleManager::LoadModuleChecked<IUMGEditorModule>(TEXT("UMGEditor"));
        UMG.AddWidgetCustomizationExtender(PreviewExtender.ToSharedRef());
        PreviewExtender->Start();
        const TWeakPtr<FGameplayPreviewExtender> Weak=PreviewExtender;
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(ThemeTabName,FOnSpawnTab::CreateLambda([Weak](const FSpawnTabArgs& Args)->TSharedRef<SDockTab>
        {
            if (auto Extender=Weak.Pin()) return Extender->SpawnThemeTab(Args);
            return SNew(SDockTab);
        })).SetDisplayName(LOCTEXT("ThemeTab","Gameplay UI Theme"))
            .SetTooltipText(LOCTEXT("ThemeTabTooltip","Edit the shared gameplay UI palette and its Designer preview."));
    }
    FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).NotifyCustomizationModuleChanged();
}

void MCGameplayUIPreview::Unregister()
{
    if (!PreviewExtender) return;
    PreviewExtender->StopAll();
    if (auto* UMG=FModuleManager::GetModulePtr<IUMGEditorModule>(TEXT("UMGEditor")))
        UMG->RemoveWidgetCustomizationExtender(PreviewExtender.ToSharedRef());
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ThemeTabName);
    PreviewExtender.Reset();
}

void MCGameplayUIPreview::OpenThemeEditor(UMCGameplayUITheme* Theme)
{
    EnsureRegistered();
    if (PreviewExtender) PreviewExtender->OpenTheme(Theme);
}

#undef LOCTEXT_NAMESPACE
