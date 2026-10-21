#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "MCGameplayUIWidget.h"
#include "MCHubWidget.h"
#include "MCLoadingWidget.h"
#include "MCMainMenuWidget.h"
#include "MCUIDataTypes.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

namespace MCDesignerPresentationTests
{
    struct FPreviewWorld
    {
        UWorld* World=UWorld::CreateWorld(EWorldType::GamePreview,false);
        FPreviewWorld() {GEngine->CreateNewWorldContext(EWorldType::GamePreview).SetCurrentWorld(World);}
        ~FPreviewWorld() {GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    };

    template<class T>
    TStrongObjectPtr<T> Seed(UWorld* World)
    {
        TStrongObjectPtr<T> Widget(NewObject<T>(World));
        Widget->WidgetTree=NewObject<UWidgetTree>(Widget.Get(),TEXT("WidgetTree"));
        Widget->SetDesignerFlags(EWidgetDesignFlags::Designing);
        Widget->BuildPresentationTree();
        return Widget;
    }

    template<class T>
    TStrongObjectPtr<T> RoundTrip(T* Source,UWorld* World)
    {
        TStrongObjectPtr<T> Widget(NewObject<T>(World));
        Widget->SetDesignerFlags(EWidgetDesignFlags::Designing);
        Widget->WidgetTree=DuplicateObject<UWidgetTree>(Source->WidgetTree,Widget.Get(),TEXT("WidgetTree"));
        Widget->StyleBindings=Source->StyleBindings;
        Widget->TextBindings=Source->TextBindings;
        // The authoring utility removes callbacks to its temporary source;
        // the real instance must bind its own actions exactly once.
        Widget->WidgetTree->ForEachWidget([Source](UWidget* Child)
        {
            if(auto* Button=Cast<UButton>(Child)) Button->OnClicked.RemoveAll(Source);
        });
        return Widget;
    }

    int32 CountWidgets(UWidgetTree* Tree)
    {
        TArray<UWidget*> Widgets;Tree->GetAllWidgets(Widgets);return Widgets.Num();
    }

    UBorder* AddArtistDecoration(UMCGameplayUIWidget* Widget,FName CanvasName)
    {
        auto* Canvas=Widget->WidgetTree->FindWidget<UCanvasPanel>(CanvasName);
        if(!Canvas) return nullptr;
        auto* Decoration=Widget->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("ArtistDecoration"));
        auto* Slot=Canvas->AddChildToCanvas(Decoration);
        Slot->SetPosition(FVector2D(123,81));Slot->SetSize(FVector2D(713,37));
        return Decoration;
    }

    void NestCaption(UWidgetTree* Tree,UButton* Button)
    {
        if(!Button || !Button->GetContent()) return;
        auto* Caption=Button->GetContent();Button->RemoveChild(Caption);
        auto* Wrapper=Tree->ConstructWidget<UBorder>();
        Wrapper->SetContent(Caption);Button->SetContent(Wrapper);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCLoadingDesignerRoundTrip,
    "MessControl.UI.Designer.LoadingPreservesAuthoredTree",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FMCLoadingDesignerRoundTrip::RunTest(const FString&)
{
    using namespace MCDesignerPresentationTests;
    FPreviewWorld Fixture;
    auto Source=Seed<UMCLoadingWidget>(Fixture.World);
    auto Widget=RoundTrip(Source.Get(),Fixture.World);
    auto* Decoration=AddArtistDecoration(Widget.Get(),TEXT("LoadingRoot"));
    auto* Title=Widget->WidgetTree->FindWidget<UTextBlock>(TEXT("LoadingTitle"));
    auto* Retry=Widget->WidgetTree->FindWidget<UButton>(TEXT("RetryLoading"));
    auto* Leave=Widget->WidgetTree->FindWidget<UButton>(TEXT("LeaveLoading"));
    if(!TestNotNull(TEXT("Loading title is editable"),Title)
        || !TestNotNull(TEXT("Retry remains a real button"),Retry)
        || !TestNotNull(TEXT("Leave remains a real button"),Leave)
        || !TestNotNull(TEXT("Artist can add decoration"),Decoration)) return false;
    NestCaption(Widget->WidgetTree,Retry);NestCaption(Widget->WidgetTree,Leave);
    FSlateFontInfo Font=Title->GetFont();Font.Size=37;Title->SetFont(Font);
    const auto* OriginalRoot=Widget->WidgetTree->RootWidget.Get();
    const int32 OriginalChildren=CountWidgets(Widget->WidgetTree);
    const int32 OriginalActors=Fixture.World->GetActorCount();
    for(FName State:{FName(TEXT("Default")),FName(TEXT("Waiting")),FName(TEXT("Failed")),FName(TEXT("Default"))})
    {
        Widget->PreviewState=State;
        Widget->BuildPresentationTree();Widget->BuildPresentationTree();
        Widget->ApplyEditorPreview(1.f);Widget->RefreshPresentation();
    }
    TestTrue(TEXT("Repeated loading previews retain the authored root"),Widget->WidgetTree->RootWidget.Get()==OriginalRoot);
    TestEqual(TEXT("Loading does not regenerate or add controls"),CountWidgets(Widget->WidgetTree),OriginalChildren);
    TestTrue(TEXT("Custom decoration survives all loading states"),Widget->WidgetTree->FindWidget(TEXT("ArtistDecoration"))==Decoration);
    const auto* Slot=CastChecked<UCanvasPanelSlot>(Decoration->Slot);
    TestTrue(TEXT("Artist position survives loading states"),Slot->GetPosition().Equals(FVector2D(123,81)));
    TestTrue(TEXT("Artist size survives loading states"),Slot->GetSize().Equals(FVector2D(713,37)));
    TestEqual(TEXT("Loading preserves the authored font size"),Title->GetFont().Size,37.f);
    TestTrue(TEXT("Retry callback targets the new loading instance"),Retry->OnClicked.GetAllObjects().Contains(Widget.Get()));
    TestTrue(TEXT("Leave callback targets the new loading instance"),Leave->OnClicked.GetAllObjects().Contains(Widget.Get()));
    TestEqual(TEXT("Retry is bound once after repeated builds"),Retry->OnClicked.GetAllObjects().Num(),1);
    TestEqual(TEXT("Leave is bound once after repeated builds"),Leave->OnClicked.GetAllObjects().Num(),1);
    TestEqual(TEXT("Loading preview never creates gameplay actors"),Fixture.World->GetActorCount(),OriginalActors);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHubDesignerRoundTrip,
    "MessControl.UI.Designer.HubPreservesAuthoredTree",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FMCHubDesignerRoundTrip::RunTest(const FString&)
{
    using namespace MCDesignerPresentationTests;
    FPreviewWorld Fixture;
    auto Source=Seed<UMCHubWidget>(Fixture.World);
    auto Widget=RoundTrip(Source.Get(),Fixture.World);
    // An artist may opt out of the starter theme bindings and own fonts/colors.
    Widget->StyleBindings.Reset();
    auto* Decoration=AddArtistDecoration(Widget.Get(),TEXT("HubCanvas"));
    auto* Title=Widget->WidgetTree->FindWidget<UTextBlock>(TEXT("HubReadinessTitle"));
    auto* Open=Widget->WidgetTree->FindWidget<UMCHubActionButton>(TEXT("HubOpenSelection"));
    auto* Launch=Widget->WidgetTree->FindWidget<UMCHubActionButton>(TEXT("HubLaunch"));
    if(!TestNotNull(TEXT("Hub title is editable"),Title)
        || !TestNotNull(TEXT("Ready action remains a real button"),Open)
        || !TestNotNull(TEXT("Launch action remains a real button"),Launch)
        || !TestNotNull(TEXT("Hub accepts custom decoration"),Decoration)) return false;
    NestCaption(Widget->WidgetTree,Open);NestCaption(Widget->WidgetTree,Launch);
    FSlateFontInfo Font=Title->GetFont();Font.Size=39;Title->SetFont(Font);
    FButtonStyle Style=Open->GetStyle();Style.SetNormalPadding(FMargin(31,7,19,11));Open->SetStyle(Style);
    const auto* OriginalRoot=Widget->WidgetTree->RootWidget.Get();
    const int32 OriginalChildren=CountWidgets(Widget->WidgetTree);
    const int32 OriginalActors=Fixture.World->GetActorCount();
    for(FName State:{FName(TEXT("Default")),FName(TEXT("Selecting")),FName(TEXT("Travelling")),FName(TEXT("Default"))})
    {
        Widget->PreviewState=State;
        Widget->BuildPresentationTree();Widget->BuildPresentationTree();
        Widget->ApplyEditorPreview(1.f);Widget->RefreshFromState();
    }
    TestTrue(TEXT("Hub previews retain the authored root"),Widget->WidgetTree->RootWidget.Get()==OriginalRoot);
    TestEqual(TEXT("Hub does not replace or append controls"),CountWidgets(Widget->WidgetTree),OriginalChildren);
    TestTrue(TEXT("Hub preserves the existing action button"),Widget->WidgetTree->FindWidget(TEXT("HubOpenSelection"))==Open);
    const auto* Slot=CastChecked<UCanvasPanelSlot>(Decoration->Slot);
    TestTrue(TEXT("Hub preserves artist position"),Slot->GetPosition().Equals(FVector2D(123,81)));
    TestTrue(TEXT("Hub preserves artist size"),Slot->GetSize().Equals(FVector2D(713,37)));
    TestEqual(TEXT("Hub preserves artist font when bindings are removed"),Title->GetFont().Size,39.f);
    TestTrue(TEXT("Hub does not overwrite artist button padding"),Open->GetStyle().NormalPadding==FMargin(31,7,19,11));
    TestEqual(TEXT("Hub actions are not rebound twice"),Open->OnClicked.GetAllObjects().Num(),1);
    TestEqual(TEXT("Designer preview never spawns mouth-stage actors"),Fixture.World->GetActorCount(),OriginalActors);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCMainMenuDesignerRoundTrip,
    "MessControl.UI.Designer.MainMenuPreservesAuthoredTree",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FMCMainMenuDesignerRoundTrip::RunTest(const FString&)
{
    using namespace MCDesignerPresentationTests;
    FPreviewWorld Fixture;
    auto Source=Seed<UMCMainMenuWidget>(Fixture.World);
    auto Widget=RoundTrip(Source.Get(),Fixture.World);
    auto* Pages=Widget->WidgetTree->FindWidget<UWidgetSwitcher>(TEXT("MenuPages"));
    auto* Main=Widget->WidgetTree->FindWidget<UVerticalBox>(TEXT("MenuMain_Page"));
    auto* Backdrop=Widget->WidgetTree->FindWidget<UBorder>(TEXT("MenuBackdrop"));
    auto* Title=Widget->WidgetTree->FindWidget<UTextBlock>(TEXT("MenuTitle"));
    auto* Settings=Widget->WidgetTree->FindWidget<UMCMainMenuActionButton>(TEXT("MenuMain_Action3"));
    if(!TestNotNull(TEXT("Menu keeps its saved page switcher"),Pages)
        || !TestNotNull(TEXT("Menu main page remains editable"),Main)
        || !TestNotNull(TEXT("Menu backdrop remains editable"),Backdrop)
        || !TestNotNull(TEXT("Menu title remains editable"),Title)
        || !TestNotNull(TEXT("Menu action remains a real button"),Settings)) return false;
    TestEqual(TEXT("All eleven pages are saved for Designer editing"),Pages->GetChildrenCount(),11);
    auto* Sections=Widget->WidgetTree->FindWidget<UWidgetSwitcher>(TEXT("MenuSettings_Sections"));
    if(!TestNotNull(TEXT("Settings sections are saved for Designer editing"),Sections)) return false;
    TestEqual(TEXT("Graphics, audio, language and controls are persistent sections"),Sections->GetChildrenCount(),4);
    auto* Resolution=Widget->WidgetTree->FindWidget<UComboBoxString>(TEXT("MenuSettings_ResolutionPicker"));
    auto* Language=Widget->WidgetTree->FindWidget<UComboBoxString>(TEXT("MenuSettings_LanguagePicker"));
    if(!TestNotNull(TEXT("Duplicated resolution picker exists"),Resolution)
        || !TestNotNull(TEXT("Duplicated language picker exists"),Language)) return false;
    TestEqual(TEXT("Resolution defaults are initialized exactly once during tree duplication"),Resolution->GetOptionCount(),1);
    TestFalse(TEXT("Resolution selection survives tree duplication"),Resolution->GetSelectedOption().IsEmpty());
    TestEqual(TEXT("Native language defaults are initialized exactly once during tree duplication"),Language->GetOptionCount(),10);
    TestFalse(TEXT("Native language selection survives tree duplication"),Language->GetSelectedOption().IsEmpty());
    const TCHAR* SliderNames[]={TEXT("RenderScaleSlider"),TEXT("MasterSlider"),TEXT("MusicSlider"),TEXT("AmbienceSlider"),TEXT("SFXSlider"),TEXT("MouseSlider"),TEXT("GamepadSlider")};
    for(const TCHAR* Name:SliderNames)
    {
        const FName FullName(*(FString(TEXT("MenuSettings_"))+Name));
        auto* Slider=Widget->WidgetTree->FindWidget<USlider>(FullName);
        TestNotNull(*FString::Printf(TEXT("%s remains an editable stock slider"),Name),Slider);
        TestTrue(*FString::Printf(TEXT("%s inherits the shared slider theme"),Name),Widget->StyleBindings.ContainsByPredicate([FullName](const auto& Entry){return Entry.WidgetName==FullName && Entry.Target==EMCUIStyleTarget::Slider;}));
    }
    auto* Decoration=Widget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("ArtistCaption"));
    Decoration->SetText(FText::AsCultureInvariant(TEXT("Artist-owned caption")));
    auto* Slot=Main->AddChildToVerticalBox(Decoration);Slot->SetPadding(FMargin(7,13,17,23));
    NestCaption(Widget->WidgetTree,Settings);
    Backdrop->SetPadding(FMargin(19,29,31,43));
    FSlateFontInfo Font=Title->GetFont();Font.Size=41;Title->SetFont(Font);
    const FLinearColor ArtistColor(.71f,.23f,.43f,1.f);Title->SetColorAndOpacity(FSlateColor(ArtistColor));
    FButtonStyle Style=Settings->GetStyle();Style.SetNormalPadding(FMargin(9,17,23,31));Settings->SetStyle(Style);
    Settings->SetRenderTranslation(FVector2D(11,19));
    const auto* OriginalRoot=Widget->WidgetTree->RootWidget.Get();
    const int32 OriginalChildren=CountWidgets(Widget->WidgetTree);
    const int32 OriginalActors=Fixture.World->GetActorCount();
    const TCHAR* States[]={TEXT("Main"),TEXT("Host"),TEXT("Join"),TEXT("Settings"),TEXT("Credits"),TEXT("Exit"),
        TEXT("Lobby"),TEXT("Leave"),TEXT("VideoConfirm"),TEXT("Pause"),TEXT("Connection"),TEXT("Main")};
    for(const TCHAR* State:States)
    {
        Widget->PreviewState=FName(State);
        Widget->BuildPresentationTree();Widget->BuildPresentationTree();Widget->ApplyEditorPreview(1.f);
        const FName PageName(*FString::Printf(TEXT("Menu%s_Page"),State));
        TestTrue(*FString::Printf(TEXT("Preview selects saved %s page"),State),Widget->GetActiveMenuPageRoot()==Widget->WidgetTree->FindWidget(PageName));
    }
    const TCHAR* TabStates[]={TEXT("Settings"),TEXT("SettingsAudio"),TEXT("SettingsLanguage"),TEXT("SettingsControls")};
    for(int32 Tab=0;Tab<4;++Tab)
    {
        Widget->PreviewState=FName(TabStates[Tab]);Widget->ApplyEditorPreview(1.f);
        TestEqual(*FString::Printf(TEXT("Designer can preview settings section %d"),Tab),Sections->GetActiveWidgetIndex(),Tab);
    }
    TestTrue(TEXT("Menu previews preserve the authored root"),Widget->WidgetTree->RootWidget.Get()==OriginalRoot);
    TestEqual(TEXT("Menu previews never regenerate controls"),CountWidgets(Widget->WidgetTree),OriginalChildren);
    TestEqual(TEXT("Artist-owned text is not replaced"),Decoration->GetText().ToString(),FString(TEXT("Artist-owned caption")));
    TestTrue(TEXT("Menu preserves artist child geometry"),Slot->GetPadding()==FMargin(7,13,17,23));
    TestTrue(TEXT("Menu preserves artist backdrop geometry"),Backdrop->GetPadding()==FMargin(19,29,31,43));
    TestEqual(TEXT("Menu preserves artist font"),Title->GetFont().Size,41.f);
    TestTrue(TEXT("Menu preserves artist text color"),Title->GetColorAndOpacity().GetSpecifiedColor().Equals(ArtistColor));
    TestTrue(TEXT("Menu preserves artist button style"),Settings->GetStyle().NormalPadding==FMargin(9,17,23,31));
    TestTrue(TEXT("Menu preserves artist render transform"),Settings->GetRenderTransform().Translation.Equals(FVector2D(11,19)));
    TestEqual(TEXT("Menu Designer never creates gameplay actors"),Fixture.World->GetActorCount(),OriginalActors);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCMainMenuLocalizationKeepsEdits,
    "MessControl.UI.Designer.MainMenuLocalizationPreservesPendingEdits",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FMCMainMenuLocalizationKeepsEdits::RunTest(const FString&)
{
    using namespace MCDesignerPresentationTests;
    FPreviewWorld Fixture;
    Fixture.World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    auto* PC=Fixture.World->SpawnActor<APlayerController>();
    if(!TestNotNull(TEXT("Isolated local controller exists"),PC)) return false;
    auto* Player=NewObject<ULocalPlayer>(GEngine);
    PC->Player=Player;Player->PlayerController=PC;PC->SetAsLocalPlayerController();
    TStrongObjectPtr<UMCMainMenuWidget> Widget(CreateWidget<UMCMainMenuWidget>(PC,UMCMainMenuWidget::StaticClass()));
    if(!TestNotNull(TEXT("Production menu initializes"),Widget.Get())) return false;
    auto MakeTable=[](const FString& Prefix)
    {
        auto* Table=NewObject<UDataTable>();Table->RowStruct=FMCUILocalizationRow::StaticStruct();
        const TCHAR* Keys[]={TEXT("UI.MainMenu.Option.Fullscreen"),TEXT("UI.MainMenu.Option.Borderless"),TEXT("UI.MainMenu.Option.Windowed"),
            TEXT("UI.MainMenu.Option.Quality.Low"),TEXT("UI.MainMenu.Option.Quality.Medium"),TEXT("UI.MainMenu.Option.Quality.High"),
            TEXT("UI.MainMenu.Option.Quality.Epic"),TEXT("UI.MainMenu.Option.Quality.Cinematic"),TEXT("UI.MainMenu.Option.QualityCustom"),TEXT("UI.MainMenu.HostAddress")};
        for(const TCHAR* Key:Keys)
        {
            FMCUILocalizationRow Row;Row.RU=FText::AsCultureInvariant(Prefix+Key);Row.EN=Row.RU;Table->AddRow(FName(Key),Row);
        }
        return Table;
    };
    TStrongObjectPtr<UDataTable> TableA(MakeTable(TEXT("A:"))),TableB(MakeTable(TEXT("B:")));
    Widget->SetTranslationTable(TableA.Get());
    Widget->HandleAction(EMCMainMenuAction::Settings);
    auto* Resolution=Widget->WidgetTree->FindWidget<UComboBoxString>(TEXT("MenuSettings_ResolutionPicker"));
    auto* Mode=Widget->WidgetTree->FindWidget<UComboBoxString>(TEXT("MenuSettings_WindowModePicker"));
    auto* Quality=Widget->WidgetTree->FindWidget<UComboBoxString>(TEXT("MenuSettings_QualityPicker"));
    auto* VSync=Widget->WidgetTree->FindWidget<UCheckBox>(TEXT("MenuSettings_VSyncCheck"));
    if(!TestNotNull(TEXT("Resolution picker exists"),Resolution) || !TestNotNull(TEXT("Mode picker exists"),Mode)
        || !TestNotNull(TEXT("Quality picker exists"),Quality) || !TestNotNull(TEXT("VSync exists"),VSync)) return false;
    Resolution->SetSelectedIndex(Resolution->GetOptionCount()-1);Mode->SetSelectedIndex(2);Quality->SetSelectedIndex(1);
    VSync->SetIsChecked(!VSync->IsChecked());
    auto* Music=Widget->WidgetTree->FindWidget<USlider>(TEXT("MenuSettings_MusicSlider"));
    auto* Language=Widget->WidgetTree->FindWidget<UComboBoxString>(TEXT("MenuSettings_LanguagePicker"));
    if(!TestNotNull(TEXT("Music remains an editable slider"),Music) || !TestNotNull(TEXT("Language picker exists"),Language)) return false;
    Music->SetValue(.23f);Language->SetSelectedIndex(5);
    Widget->HandleAction(EMCMainMenuAction::SettingsAudio);
    const FString PendingResolution=Resolution->GetSelectedOption();const bool PendingVSync=VSync->IsChecked();
    Widget->SetTranslationTable(TableB.Get());
    TestEqual(TEXT("Resolution selection survives live translation refresh"),Resolution->GetSelectedOption(),PendingResolution);
    TestEqual(TEXT("Pending window mode survives live translation refresh"),Mode->GetSelectedIndex(),2);
    TestEqual(TEXT("Pending quality survives live translation refresh"),Quality->GetSelectedIndex(),1);
    TestEqual(TEXT("Pending VSync survives live translation refresh"),VSync->IsChecked(),PendingVSync);
    TestEqual(TEXT("Pending music value survives tab and localization changes"),Music->GetValue(),.23f);
    TestEqual(TEXT("Pending language survives localization refresh"),Language->GetSelectedIndex(),5);
    TestEqual(TEXT("Selected mode uses the replacement table immediately"),Mode->GetSelectedOption(),FString(TEXT("B:UI.MainMenu.Option.Windowed")));
    TestEqual(TEXT("Selected quality uses the replacement table immediately"),Quality->GetSelectedOption(),FString(TEXT("B:UI.MainMenu.Option.Quality.Medium")));
    if(Quality->GetOptionCount()==5) Quality->AddOption(TEXT("Pending custom quality"));
    Quality->SetSelectedIndex(5);Widget->SetTranslationTable(TableA.Get());
    TestEqual(TEXT("Custom quality selection survives live translation refresh"),Quality->GetSelectedIndex(),5);
    TestEqual(TEXT("Custom quality copy is translated"),Quality->GetSelectedOption(),FString(TEXT("A:UI.MainMenu.Option.QualityCustom")));
    Widget->HandleAction(EMCMainMenuAction::Back);Widget->HandleAction(EMCMainMenuAction::JoinLobby);
    auto* Address=Widget->WidgetTree->FindWidget<UEditableTextBox>(TEXT("MenuJoin_AddressBox"));
    if(!TestNotNull(TEXT("Join address exists"),Address)) return false;
    Address->SetText(FText::AsCultureInvariant(TEXT("coop-host.local:17777")));Widget->SetTranslationTable(TableB.Get());
    TestEqual(TEXT("Typed IP/DNS endpoint survives live translation refresh"),Address->GetText().ToString(),FString(TEXT("coop-host.local:17777")));
    TestEqual(TEXT("Address hint uses the replacement table immediately"),Address->GetHintText().ToString(),FString(TEXT("B:UI.MainMenu.HostAddress")));
    return true;
}

#endif
