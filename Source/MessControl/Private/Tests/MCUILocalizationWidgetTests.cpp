#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "MCGameplayUIWidget.h"
#include "MCBoosterWidget.h"
#include "MCPerkChoiceWidget.h"
#include "MCPerkCardWidget.h"
#include "MCUIDataTypes.h"
#include "Blueprint/WidgetTree.h"
#include "Components/RichTextBlock.h"
#include "Components/HorizontalBox.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/DataTable.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

namespace MCUIWidgetLocalizationTests
{
    UDataTable* MakeTable()
    {
        auto* Table=NewObject<UDataTable>();
        Table->RowStruct=FMCUILocalizationRow::StaticStruct();
        return Table;
    }

    FMCUILocalizationRow Copy(const TCHAR* Russian,const TCHAR* English)
    {
        FMCUILocalizationRow Row;
        Row.RU=FText::FromString(Russian);
        Row.EN=FText::FromString(English);
        return Row;
    }

    UMCGameplayUIWidget* MakeWidget()
    {
        auto* Widget=NewObject<UMCGameplayUIWidget>();
        Widget->WidgetTree=NewObject<UWidgetTree>(Widget);
        Widget->WidgetTree->RootWidget=Widget->WidgetTree->ConstructWidget<UVerticalBox>();
        return Widget;
    }

    template<class T>
    T* AddLabel(UMCGameplayUIWidget* Widget,FName Name,const TCHAR* Original)
    {
        auto* Label=Widget->WidgetTree->ConstructWidget<T>(T::StaticClass(),Name);
        Label->SetText(FText::FromString(Original));
        CastChecked<UVerticalBox>(Widget->WidgetTree->RootWidget)->AddChild(Label);
        return Label;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCUIWidgetStaticLocalizationBindings,
    "MessControl.UI.Localization.WidgetBindingsAndLiveTableEdits",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FMCUIWidgetStaticLocalizationBindings::RunTest(const FString&)
{
    using namespace MCUIWidgetLocalizationTests;
    const FString GlobalLanguage=FInternationalization::Get().GetCurrentLanguage()->GetName();
    auto* Table=MakeTable();
    Table->AddRow(TEXT("UI.Test.Play"),Copy(TEXT("Играть"),TEXT("Play")));
    Table->AddRow(TEXT("UI.Test.Rich"),Copy(TEXT("<b>Продолжить</>"),TEXT("<b>Continue</>")));
    Table->AddRow(TEXT("UI.Test.Count"),Copy(TEXT("Счёт: {Count}"),TEXT("Score: {Count}")));
    auto* Widget=MakeWidget();
    Widget->SetDesignerFlags(EWidgetDesignFlags::Designing);
    Widget->PreviewCulture=TEXT("en");
    auto* Plain=AddLabel<UTextBlock>(Widget,TEXT("PlainLabel"),TEXT("Original plain"));
    auto* Rich=AddLabel<URichTextBlock>(Widget,TEXT("RichLabel"),TEXT("Original rich"));
    auto* Counter=AddLabel<UTextBlock>(Widget,TEXT("CounterLabel"),TEXT("Score: 12"));
    auto* Missing=AddLabel<UTextBlock>(Widget,TEXT("MissingLabel"),TEXT("Authored fallback"));
    Widget->TextBindings={{TEXT("PlainLabel"),TEXT("UI.Test.Play")},
        {TEXT("RichLabel"),TEXT("UI.Test.Rich")},{TEXT("CounterLabel"),TEXT("UI.Test.Count")},
        {TEXT("MissingLabel"),TEXT("UI.Test.Missing")},{TEXT("AbsentWidget"),TEXT("UI.Test.Play")}};
    Widget->SetTranslationTable(Table);
    TestEqual(TEXT("Plain label uses a stable table key"),Plain->GetText().ToString(),TEXT("Play"));
    TestEqual(TEXT("Rich text keeps its localized markup"),Rich->GetText().ToString(),TEXT("<b>Continue</>"));
    TestEqual(TEXT("Formatted counters retain their presenter value"),Counter->GetText().ToString(),TEXT("Score: 12"));
    TestEqual(TEXT("A missing key retains the authored label"),Missing->GetText().ToString(),TEXT("Authored fallback"));
    TestEqual(TEXT("Designer language does not change global language"),FInternationalization::Get().GetCurrentLanguage()->GetName(),GlobalLanguage);

    auto* Row=Table->FindRow<FMCUILocalizationRow>(TEXT("UI.Test.Play"),TEXT("Widget binding test"));
    Row->EN=FText::FromString(TEXT("Start now"));
    Table->HandleDataTableChanged(TEXT("UI.Test.Play"));
    TestEqual(TEXT("A table edit immediately refreshes its open widget"),Plain->GetText().ToString(),TEXT("Start now"));
    TestEqual(TEXT("A table edit still does not replace a live counter"),Counter->GetText().ToString(),TEXT("Score: 12"));

    auto* Replacement=MakeTable();
    Replacement->AddRow(TEXT("UI.Test.Play"),Copy(TEXT("Новая игра"),TEXT("New game")));
    Widget->SetTranslationTable(Replacement);
    TestEqual(TEXT("Replacing the table immediately changes the label"),Plain->GetText().ToString(),TEXT("New game"));
    TestEqual(TEXT("Missing row after replacement uses original copy, not a stale translation"),Rich->GetText().ToString(),TEXT("Original rich"));
    Table->HandleDataTableChanged(TEXT("UI.Test.Play"));
    TestEqual(TEXT("Editing the previous table cannot restore its old translation"),Plain->GetText().ToString(),TEXT("New game"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCUIWidgetLocalizationInheritance,
    "MessControl.UI.Localization.WidgetInheritanceAndRuntimeCulture",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FMCUIWidgetLocalizationInheritance::RunTest(const FString&)
{
    using namespace MCUIWidgetLocalizationTests;
    auto& Internationalization=FInternationalization::Get();
    const FString OriginalLanguage=Internationalization.GetCurrentLanguage()->GetName();
    ON_SCOPE_EXIT { Internationalization.SetCurrentLanguage(OriginalLanguage); };
    auto* Parent=MakeWidget();
    auto* ParentTable=MakeTable();
    ParentTable->AddRow(TEXT("UI.Test.Play"),Copy(TEXT("Играть"),TEXT("Play")));
    Parent->SetTranslationTable(ParentTable);
    auto* Child=NewObject<UMCGameplayUIWidget>(Parent->WidgetTree);
    TestEqual(TEXT("A nested widget inherits the screen's explicit table"),Child->GetResolvedTranslationTable(),ParentTable);
    auto* ChildTable=MakeTable();
    Child->SetTranslationTable(ChildTable);
    TestEqual(TEXT("A nested explicit table overrides its parent"),Child->GetResolvedTranslationTable(),ChildTable);

    auto* Label=AddLabel<UTextBlock>(Parent,TEXT("CultureLabel"),TEXT("Original"));
    Parent->TextBindings.Add(TEXT("CultureLabel"),TEXT("UI.Test.Play"));
    Internationalization.SetCurrentLanguage(TEXT("en"));
    Parent->RefreshLocalization();
    TestEqual(TEXT("Runtime uses the game's current language"),Label->GetText().ToString(),TEXT("Play"));
    Internationalization.SetCurrentLanguage(TEXT("ru"));
    TestEqual(TEXT("Runtime culture change refreshes an already constructed label"),Label->GetText().ToString(),TEXT("Играть"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCUIBoosterLocalizationState,
    "MessControl.UI.Localization.BoosterRefreshRetainsCurrentGameplayState",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FMCUIBoosterLocalizationState::RunTest(const FString&)
{
    using namespace MCUIWidgetLocalizationTests;
    auto& Internationalization=FInternationalization::Get();
    const FString OriginalLanguage=Internationalization.GetCurrentLanguage()->GetName();
    ON_SCOPE_EXIT { Internationalization.SetCurrentLanguage(OriginalLanguage); };
    Internationalization.SetCurrentLanguage(TEXT("en"));
    auto* Translations=MakeTable();
    Translations->AddRow(TEXT("Test.Booster.Name"),Copy(TEXT("Кофейный щит"),TEXT("Coffee shield")));
    Translations->AddRow(TEXT("Test.Booster.Description"),Copy(TEXT("Защита"),TEXT("Protection")));
    Translations->AddRow(TEXT("UI.Booster.Charges"),Copy(TEXT("Заряды: {Count}"),TEXT("Uses: {Count}")));
    auto* Boosters=NewObject<UDataTable>();Boosters->RowStruct=FMCUIBoosterRow::StaticStruct();
    FMCUIBoosterRow Row;Row.GameplayPerkID=TEXT("TestShield");Row.DisplayNameKey=TEXT("Test.Booster.Name");Row.DescriptionKey=TEXT("Test.Booster.Description");
    Boosters->AddRow(TEXT("CustomShieldRow"),Row);
    auto* Parent=MakeWidget();Parent->SetTranslationTable(Translations);
    auto* Widget=NewObject<UMCBoosterWidget>(Parent->WidgetTree);Widget->WidgetTree=NewObject<UWidgetTree>(Widget);
    CastChecked<UVerticalBox>(Parent->WidgetTree->RootWidget)->AddChild(Widget);
    Widget->BoosterTable=Boosters;Widget->RefreshLocalization();
    FMCUIBoosterViewData Data;Data.GameplayPerkID=TEXT("TestShield");Data.QueueCount=7;
    Data.bFuryActive=true;Data.bGrenadeArmed=true;Data.bActivationBlocked=true;
    Data.ActivationInput=FText::AsCultureInvariant(TEXT("K"));Widget->ApplyPresentation(Data);
    auto* Name=CastChecked<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("BoosterName")));
    auto* Count=CastChecked<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("BoosterQueueCount")));
    TestEqual(TEXT("Custom presentation row resolves through the screen's central table"),Name->GetText().ToString(),TEXT("Coffee shield"));
    auto* Changed=Translations->FindRow<FMCUILocalizationRow>(TEXT("Test.Booster.Name"),TEXT("Booster test"));
    Changed->EN=FText::FromString(TEXT("Renamed shield"));Translations->HandleDataTableChanged(TEXT("Test.Booster.Name"));
    TestEqual(TEXT("Editing central text refreshes the current booster"),Name->GetText().ToString(),TEXT("Renamed shield"));
    TestEqual(TEXT("Table editing keeps the real queue count"),Count->GetText().ToString(),TEXT("Uses: 7"));
    auto* Replacement=DuplicateObject<UDataTable>(Translations,GetTransientPackage());
    Replacement->FindRow<FMCUILocalizationRow>(TEXT("Test.Booster.Name"),TEXT("Nested override test"))->EN=FText::FromString(TEXT("Parent switched table"));
    Parent->SetTranslationTable(Replacement);
    TestEqual(TEXT("Parent table replacement immediately re-presents the nested booster's content"),Name->GetText().ToString(),TEXT("Parent switched table"));
    TestFalse(TEXT("Nested widget no longer observes the old table"),Translations->OnDataTableChanged().IsBoundToObject(Widget));
    TestTrue(TEXT("Nested widget observes the replacement table"),Replacement->OnDataTableChanged().IsBoundToObject(Widget));
    Replacement->FindRow<FMCUILocalizationRow>(TEXT("Test.Booster.Name"),TEXT("Nested edit test"))->EN=FText::FromString(TEXT("Nested live edit"));
    Replacement->HandleDataTableChanged(TEXT("Test.Booster.Name"));
    TestEqual(TEXT("Nested widget follows edits to the new table"),Name->GetText().ToString(),TEXT("Nested live edit"));
    Internationalization.SetCurrentLanguage(TEXT("ru"));
    TestEqual(TEXT("Culture refresh localizes the current booster"),Name->GetText().ToString(),TEXT("Кофейный щит"));
    TestEqual(TEXT("Culture refresh keeps the real queue count"),Count->GetText().ToString(),TEXT("Заряды: 7"));
    auto* Input=CastChecked<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("BoosterKey")));
    TestEqual(TEXT("Input selection is not replaced by a preview default"),Input->GetText().ToString(),TEXT("K"));
    TestEqual(TEXT("Active effect retains its visible presentation state"),Widget->WidgetTree->FindWidget(TEXT("BoosterFuryPanel"))->GetVisibility(),ESlateVisibility::HitTestInvisible);
    TestEqual(TEXT("Armed grenade retains its visible presentation state"),Widget->WidgetTree->FindWidget(TEXT("BoosterArmedPanel"))->GetVisibility(),ESlateVisibility::HitTestInvisible);
    TestFalse(TEXT("Blocked activation stays blocked"),Widget->WidgetTree->FindWidget(TEXT("BoosterKeycap"))->GetIsEnabled());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCUIChoiceLocalizationState,
    "MessControl.UI.Localization.ChoiceRefreshRetainsPendingAndWaitingState",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FMCUIChoiceLocalizationState::RunTest(const FString&)
{
    using namespace MCUIWidgetLocalizationTests;
    auto& Internationalization=FInternationalization::Get();
    const FString OriginalLanguage=Internationalization.GetCurrentLanguage()->GetName();
    ON_SCOPE_EXIT { Internationalization.SetCurrentLanguage(OriginalLanguage); };
    Internationalization.SetCurrentLanguage(TEXT("en"));
    auto* Legacy=MakeTable();
    Legacy->AddRow(TEXT("UI.Reward.WaitingTitle"),Copy(TEXT("Ждём"),TEXT("Waiting")));
    auto* Current=MakeTable();
    Current->AddRow(TEXT("UI.Reward.WaitingTitle"),Copy(TEXT("Ждём игроков"),TEXT("Waiting for players")));
    Current->AddRow(TEXT("UI.Reward.TeamReady"),Copy(TEXT("Готовы: {Ready}/{Total}"),TEXT("Ready: {Ready}/{Total}")));
    Current->AddRow(TEXT("UI.Reward.LevelTitle"),Copy(TEXT("УРОВЕНЬ {Level}"),TEXT("LEVEL {Level}")));
    Current->AddRow(TEXT("UI.Reward.SelectionPending"),Copy(TEXT("Подтверждаем"),TEXT("Confirming")));
    Current->AddRow(TEXT("Test.Card.Name"),Copy(TEXT("Перк"),TEXT("Perk")));
    Current->AddRow(TEXT("Test.Card.Description"),Copy(TEXT("Описание"),TEXT("Description")));
    auto* Cards=NewObject<UDataTable>();Cards->RowStruct=FMCUICardRow::StaticStruct();
    FMCUICardRow Row;Row.GameplayPerkID=TEXT("TestPerk");Row.DisplayNameKey=TEXT("Test.Card.Name");Row.DescriptionKey=TEXT("Test.Card.Description");
    Cards->AddRow(TEXT("CustomCardRow"),Row);
    auto* Widget=NewObject<UMCPerkChoiceWidget>();Widget->WidgetTree=NewObject<UWidgetTree>(Widget);
    Widget->LocalizationTable=Legacy;Widget->CardTable=Cards;
    Widget->ShowTeamWaiting(2,4);
    TestEqual(TEXT("Existing explicit LocalizationTable remains supported"),Widget->GetResolvedTranslationTable(),Legacy);
    Widget->SetTranslationTable(Current);
    TestEqual(TEXT("New screen override takes precedence"),Widget->GetResolvedTranslationTable(),Current);
    TestEqual(TEXT("The legacy override is not silently rewritten"),Widget->LocalizationTable.Get(),Legacy);
    TestTrue(TEXT("Changing tables preserves the waiting phase"),Widget->IsWaitingForTeam());
    auto* Hint=CastChecked<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("RewardHint")));
    TestEqual(TEXT("Waiting keeps actual ready/total values"),Hint->GetText().ToString(),TEXT("Ready: 2/4"));
    Widget->ShowLevelChoices({FName(TEXT("TestPerk"))},6,3);Widget->SetSelectionPending(true);
    auto* CardsPanel=CastChecked<UHorizontalBox>(Widget->WidgetTree->FindWidget(TEXT("RewardCards")));
    auto* Card=CastChecked<UMCPerkCardWidget>(CardsPanel->GetChildAt(0));
    auto* Name=CastChecked<UTextBlock>(Card->WidgetTree->FindWidget(TEXT("CardName")));
    auto* Scroll=CastChecked<UScrollBox>(Card->WidgetTree->FindWidget(TEXT("CardBodyScroll")));
    // Retain the Slate owner: discarding this ref immediately destructs a
    // worldless widget and leaves GetScrollOffset with no live scroll box.
    const TSharedRef<SWidget> CardSlate=Card->TakeWidget();
    Scroll->SetScrollOffset(83.f);
    TestEqual(TEXT("Scroll fixture has a queued Slate offset"),Scroll->GetScrollOffset(),83.f);
    TestEqual(TEXT("Custom card table uses the selected central catalogue"),Name->GetText().ToString(),TEXT("Perk"));
    auto* Changed=Current->FindRow<FMCUILocalizationRow>(TEXT("Test.Card.Name"),TEXT("Choice test"));
    Changed->EN=FText::FromString(TEXT("Renamed perk"));Current->HandleDataTableChanged(TEXT("Test.Card.Name"));
    TestTrue(TEXT("Re-localizing does not replace the current card control"),CardsPanel->GetChildAt(0)==Card);
    TestEqual(TEXT("The live card name changes"),Name->GetText().ToString(),TEXT("Renamed perk"));
    TestEqual(TEXT("Re-localizing the same offer preserves the queued scroll offset"),Scroll->GetScrollOffset(),83.f);
    TestEqual(TEXT("The live team level remains unchanged"),CastChecked<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("RewardTitle")))->GetText().ToString(),TEXT("LEVEL 6"));
    TestEqual(TEXT("Pending confirmation remains displayed"),Hint->GetText().ToString(),TEXT("Confirming"));
    TestFalse(TEXT("Pending card remains disabled"),Card->GetChoiceButton()->GetIsEnabled());
    TestTrue(TEXT("The same offer remains open"),Widget->IsShowingChoices());
    return true;
}

#endif
