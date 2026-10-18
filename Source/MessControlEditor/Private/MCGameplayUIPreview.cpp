#include "MCGameplayUIPreview.h"

#include "MCGameplayUITheme.h"
#include "MCGameplayUIWidget.h"
#include "AssetRegistry/AssetData.h"
#include "Blueprint/WidgetTree.h"
#include "Containers/Ticker.h"
#include "Customizations/IBlueprintWidgetCustomizationExtender.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DetailsViewArgs.h"
#include "Editor.h"
#include "Framework/Docking/TabManager.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UMGEditorModule.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
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

    bool IsGameplayEditor(const TSharedPtr<FWidgetBlueprintEditor>& Editor)
    {
        const auto* Blueprint=Editor?Editor->GetWidgetBlueprintObj():nullptr;
        return Blueprint && Blueprint->GetOutermost()->GetName().StartsWith(TEXT("/Game/Gameplay/UI/"))
            && Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(UMCGameplayUIWidget::StaticClass());
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
    };

    class FGameplayPreviewExtender final : public IBlueprintWidgetCustomizationExtender
    {
    public:
        void Start()
        {
            Ticker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this,&FGameplayPreviewExtender::Tick));
            PropertyHandle=FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this,&FGameplayPreviewExtender::OnPropertyChanged);
            UndoHandle=FEditorDelegates::PostUndoRedo.AddRaw(this,&FGameplayPreviewExtender::OnUndoRedo);
        }

        void StopAll()
        {
            FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
            FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyHandle);
            FEditorDelegates::PostUndoRedo.Remove(UndoHandle);
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
