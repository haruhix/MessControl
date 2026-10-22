#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "MCHubWidget.h"
#include "MCGameplayMenuWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHubGuidancePresentationTest,"MessControl.Hub.ReadyGuidanceHasEditableIdempotentControls",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCHubGuidancePresentationTest::RunTest(const FString&)
{
    TStrongObjectPtr<UMCHubWidget> Widget(NewObject<UMCHubWidget>(GetTransientPackage()));
    Widget->SetDesignerFlags(EWidgetDesignFlags::Designing);Widget->Initialize();Widget->BuildPresentationTree();
    if(!TestNotNull(TEXT("Hub has an editable tree"),Widget->WidgetTree.Get())) return false;
    UWidget* Root=Widget->WidgetTree->RootWidget;
    TestNotNull(TEXT("Ready-zone waypoint is a Designer-editable border"),Widget->WidgetTree->FindWidget<UBorder>(TEXT("HubReadyWaypoint")));
    TestNotNull(TEXT("Ready-zone direction arrow is an editable text control"),Widget->WidgetTree->FindWidget<UTextBlock>(TEXT("HubReadyWaypointArrow")));
    TestNotNull(TEXT("Ready-zone distance is an editable localized text control"),Widget->WidgetTree->FindWidget<UTextBlock>(TEXT("HubReadyWaypointLabel")));
    TestNotNull(TEXT("Hub exposes the Steam invitation action"),Widget->WidgetTree->FindWidget<UMCHubActionButton>(TEXT("HubInviteFriends")));
    TestNotNull(TEXT("Invitation keyboard guidance remains editable"),Widget->WidgetTree->FindWidget<UTextBlock>(TEXT("HubInviteHint")));
    TArray<UWidget*> Before;Widget->WidgetTree->GetAllWidgets(Before);
    Widget->BuildPresentationTree();Widget->ApplyEditorPreview(.5f);
    TestEqual(TEXT("Refreshing guidance keeps the original presentation root"),Widget->WidgetTree->RootWidget.Get(),Root);
    TArray<UWidget*> After;Widget->WidgetTree->GetAllWidgets(After);
    TestEqual(TEXT("Refreshing guidance never appends duplicate controls"),After.Num(),Before.Num());
    const auto* Waypoint=Widget->WidgetTree->FindWidget<UBorder>(TEXT("HubReadyWaypoint"));
    TestTrue(TEXT("Designer never invents a world-position marker"),Waypoint && Waypoint->GetVisibility()==ESlateVisibility::Collapsed);
    TestTrue(TEXT("Waypoint follows the shared palette instead of fixed artist colors"),Widget->StyleBindings.ContainsByPredicate(
        [](const FMCUIStyleBinding& Binding){return Binding.WidgetName==TEXT("HubReadyWaypoint") && Binding.Color==EMCUIColorToken::Marine;}));
    TStrongObjectPtr<UMCGameplayMenuWidget> Pause(NewObject<UMCGameplayMenuWidget>(GetTransientPackage()));
    Pause->SetDesignerFlags(EWidgetDesignFlags::Designing);Pause->Initialize();Pause->BuildPresentationTree();Pause->ApplyEditorPreview(0.f);
    auto* Invite=Pause->WidgetTree->FindWidget<UMCGameplayMenuActionButton>(TEXT("MenuPauseInvite"));
    if(TestNotNull(TEXT("The actual gameplay Pause menu has its own editable Hub invitation action"),Invite)) {
        TestEqual(TEXT("Pause uses the existing Steam invitation action"),Invite->MenuAction,EMCGameplayMenuAction::Invite);
        TestTrue(TEXT("Steam preview exposes the pause invitation"),Invite->IsVisible());
        Pause->bPreviewSteamAvailable=false;Pause->ApplyEditorPreview(0.f);
        TestEqual(TEXT("Offline pause preview hides an unavailable invitation"),Invite->GetVisibility(),ESlateVisibility::Collapsed);
    }
    return true;
}
#endif
