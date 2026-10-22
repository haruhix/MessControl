#include "MCGameplayUIPreview.h"

#include "MCGameplayUITheme.h"
#include "MCGameplayUIWidget.h"
#include "MCMainMenuWidget.h"
#include "AssetRegistry/AssetData.h"
#include "Blueprint/WidgetTree.h"
#include "Containers/Ticker.h"
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
        // Session-only state: never written to the Blueprint CDO or saved tree.
        FName MenuState=TEXT("Main");
        FString MenuCulture;
        bool bHasMenuSelection=false;
    };

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
                    SNew(STextBlock).Text(LOCTEXT("PreviewHelp","Designer preview only. Uses fixture data; no PIE or gameplay changes."))
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
            if (!Blueprint || !Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(UMCMainMenuWidget::StaticClass())
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
                    CaptureMenuSelection(Session,Widget);
                    RestoreMenuSelection(Session,Widget);
                    if (Widget && Session.bHasMenuSelection)
                    {
                        Widget->ApplyEditorPreview(0.f);
                        MCGameplayUIPreview::SynchronizeFixtureTree(Widget);
                        Widget->InvalidateLayoutAndVolatility();
                    }
                }
                if (!Session.bPlaying || !Widget) continue;
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
