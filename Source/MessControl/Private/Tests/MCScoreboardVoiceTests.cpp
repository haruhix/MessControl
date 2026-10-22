#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "MCScoreboardWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Misc/AutomationTest.h"
#include "OnlineSubsystemTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCScoreboardVoiceSavedTreeTest,
    "MessControl.UI.Scoreboard.VoiceAppendPreservesArtistTree",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCScoreboardVoiceSavedTreeTest::RunTest(const FString&)
{
    auto* Widget=NewObject<UMCScoreboardWidget>();Widget->SetDesignerFlags(EWidgetDesignFlags::Designing);
    Widget->WidgetTree=NewObject<UWidgetTree>(Widget);
    auto* Root=Widget->WidgetTree->ConstructWidget<UVerticalBox>();Widget->WidgetTree->RootWidget=Root;
    Root->SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* HeaderFrame=Widget->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("ScoreColumnsFrame"));
    Root->AddChild(HeaderFrame);
    auto* Header=Widget->WidgetTree->ConstructWidget<UHorizontalBox>();HeaderFrame->SetContent(Header);
    auto* ArtistLabel=Widget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("ArtistHeading"));
    ArtistLabel->SetText(FText::AsCultureInvariant(TEXT("KEEP THIS ARTIST LABEL")));Header->AddChild(ArtistLabel);
    auto* Players=Widget->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("ScorePlayers"));Root->AddChild(Players);
    auto* Row=Widget->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("ScorePlayer1Row"));Players->AddChild(Row);
    Row->SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* Cells=Widget->WidgetTree->ConstructWidget<UHorizontalBox>();Row->SetContent(Cells);
    auto* Name=Widget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("ScorePlayer1Name"));
    Cells->AddChild(Name);
    Widget->BuildPresentationTree();
    TestEqual(TEXT("The saved artist root is retained"),Widget->WidgetTree->RootWidget.Get(),static_cast<UWidget*>(Root));
    TestEqual(TEXT("The saved player row is retained"),Widget->WidgetTree->FindWidget(TEXT("ScorePlayer1Row")),static_cast<UWidget*>(Row));
    TestEqual(TEXT("The original header text is not replaced"),ArtistLabel->GetText().ToString(),TEXT("KEEP THIS ARTIST LABEL"));
    TestEqual(TEXT("Root permits child hit testing"),Root->GetVisibility(),ESlateVisibility::SelfHitTestInvisible);
    TestEqual(TEXT("Saved row permits child hit testing"),Row->GetVisibility(),ESlateVisibility::SelfHitTestInvisible);
    auto* Mute=Cast<UMCScoreboardMuteButton>(Widget->WidgetTree->FindWidget(TEXT("ScorePlayer1Mute")));
    TestNotNull(TEXT("Existing row receives a real editable mute button"),Mute);
    TestNotNull(TEXT("Existing header receives the voice column"),Widget->WidgetTree->FindWidget(TEXT("ScoreVoiceHeader")));
    const int32 Children=Cells->GetChildrenCount(),Headers=Header->GetChildrenCount();
    Widget->BuildPresentationTree();Widget->ApplyEditorPreview(0.f);Widget->BuildPresentationTree();
    TestEqual(TEXT("Repeated authoring refresh never appends a duplicate column"),Cells->GetChildrenCount(),Children);
    TestEqual(TEXT("Repeated authoring refresh never appends a duplicate heading"),Header->GetChildrenCount(),Headers);
    TestEqual(TEXT("Button survives refresh by identity"),Widget->WidgetTree->FindWidget(TEXT("ScorePlayer1Mute")),static_cast<UWidget*>(Mute));
    TestTrue(TEXT("Designer preview has an enabled human example"),Widget->PlayerEntries[1].bVoiceControlEnabled);
    TestFalse(TEXT("Designer preview has a disabled bot example"),Widget->PlayerEntries[3].bVoiceControlEnabled);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCScoreboardLegacyHeaderVoiceTest,
    "MessControl.UI.Scoreboard.LegacyHeaderWithoutFrameReceivesVoiceColumn",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCScoreboardLegacyHeaderVoiceTest::RunTest(const FString&)
{
    auto* Widget=NewObject<UMCScoreboardWidget>();Widget->SetDesignerFlags(EWidgetDesignFlags::Designing);
    Widget->WidgetTree=NewObject<UWidgetTree>(Widget);
    auto* Root=Widget->WidgetTree->ConstructWidget<UVerticalBox>();Widget->WidgetTree->RootWidget=Root;
    // Match saved WBP_Scoreboard_v10: the header is a horizontal row directly
    // in its column, with ping nested in a size box, and no newer frame name.
    auto* Header=Widget->WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("ArtistLegacyHeader"));
    Root->AddChild(Header);
    auto* Player=Widget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("ScorePlayerHeader"));
    Player->SetText(FText::AsCultureInvariant(TEXT("ARTIST PLAYER COLUMN")));Header->AddChild(Player);
    auto* PingWidth=Widget->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ArtistPingWidth"));
    PingWidth->SetWidthOverride(123.f);
    auto* Ping=Widget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("ScorePingHeader"));
    PingWidth->SetContent(Ping);Header->AddChild(PingWidth);
    auto* Players=Widget->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("ScorePlayers"));
    Root->AddChild(Players);
    Widget->BuildPresentationTree();
    TestNull(TEXT("Migration does not invent or replace the legacy header frame"),Widget->WidgetTree->FindWidget(TEXT("ScoreColumnsFrame")));
    TestEqual(TEXT("Legacy root is preserved"),Widget->WidgetTree->RootWidget.Get(),static_cast<UWidget*>(Root));
    TestEqual(TEXT("Legacy player heading keeps its original copy during append"),Player->GetText().ToString(),FString(TEXT("ARTIST PLAYER COLUMN")));
    TestEqual(TEXT("Legacy ping wrapper keeps its authored width"),PingWidth->GetWidthOverride(),123.f);
    const auto* Voice=Widget->WidgetTree->FindWidget<UTextBlock>(TEXT("ScoreVoiceHeader"));
    if(TestNotNull(TEXT("Legacy header receives the persisted voice heading leaf"),Voice)) {
        TestNotNull(TEXT("Voice heading has a stable editable width wrapper"),Widget->WidgetTree->FindWidget<USizeBox>(TEXT("ScoreVoiceColumn")));
        TestEqual(TEXT("Voice column is appended to the original header row"),Voice->GetParent()->GetParent(),static_cast<UPanelWidget*>(Header));
    }
    TestEqual(TEXT("Exactly one voice column is appended"),Header->GetChildrenCount(),3);
    Widget->BuildPresentationTree();
    TestEqual(TEXT("Legacy header append is idempotent"),Header->GetChildrenCount(),3);
    TestNotNull(TEXT("Legacy rows also receive real mute controls"),Widget->WidgetTree->FindWidget<UMCScoreboardMuteButton>(TEXT("ScorePlayer1Mute")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCScoreboardVoiceIdentityTest,
    "MessControl.UI.Scoreboard.MutePressKeepsParticipantAcrossReorder",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCScoreboardVoiceIdentityTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    auto* Alice=World->SpawnActor<APlayerState>();auto* Bob=World->SpawnActor<APlayerState>();
    auto* Bot=World->SpawnActor<APlayerState>();Bot->SetIsABot(true);
    const FUniqueNetIdRef AliceId=FUniqueNetIdString::Create(TEXT("voice-alice"),TEXT("STEAM"));
    const FUniqueNetIdRef BobId=FUniqueNetIdString::Create(TEXT("voice-bob"),TEXT("STEAM"));
    Alice->SetUniqueId(FUniqueNetIdRepl(AliceId));
    Bob->SetUniqueId(FUniqueNetIdRepl(BobId));
    auto* Button=NewObject<UMCScoreboardMuteButton>();Button->Configure(nullptr,Alice);
    TestEqual(TEXT("An idle row targets its participant"),Button->GetActionTarget(),Alice);
    Button->OnPressed.Broadcast();Button->Configure(nullptr,Bob);
    TestEqual(TEXT("A pointer press cannot mute a different participant after score sorting"),Button->GetActionTarget(),Alice);
    Button->OnClicked.Broadcast();
    TestEqual(TEXT("After completing the click, subsequent action uses the newly sorted row"),Button->GetActionTarget(),Bob);
    Button->OnPressed.Broadcast();Button->Configure(nullptr,nullptr);
    TestEqual(TEXT("A disappearing row is not retargeted while pressed"),Button->GetActionTarget(),Bob);
    TestTrue(TEXT("A remote human with online identity can be muted"),UMCScoreboardWidget::CanControlPlayerVoice(Alice,false));
    TestFalse(TEXT("Bots never show an active voice action"),UMCScoreboardWidget::CanControlPlayerVoice(Bot,true));
    TestFalse(TEXT("A remote player without identity cannot overwrite a shared empty preference"),UMCScoreboardWidget::CanControlPlayerVoice(Bot,false));
    auto* Pending=World->SpawnActor<APlayerState>();
    TestFalse(TEXT("A pending remote identity cannot be muted"),UMCScoreboardWidget::CanControlPlayerVoice(Pending,false));
    TestTrue(TEXT("The local player can self mute before online identity is assigned"),UMCScoreboardWidget::CanControlPlayerVoice(Pending,true));
    World->DestroyWorld(false);
    return true;
}
#endif
