#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "MCMainMenuGameMode.h"
#include "MCMainMenuPlayerController.h"
#include "MCMainMenuWidget.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCDayDirector.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"

namespace MCMainMenuTestsPrivate
{
    struct FMenuWorld
    {
        UWorld* World;
        FMenuWorld()
        {
            World=UWorld::CreateWorld(EWorldType::Game,false);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->SetGameInstance(NewObject<UGameInstance>(GEngine));
            FURL URL; URL.AddOption(TEXT("game=/Script/MessControl.MCMainMenuGameMode"));
            World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
        }
        ~FMenuWorld()
        {
            GEngine->GetWorldContextFromWorldChecked(World).TravelURL.Reset();
            World->EndPlay(EEndPlayReason::Quit);
            GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
        }
        UMCMainMenuWidget* NewMenu()
        {
            // A local context without a viewport exercises the production UObject widget tree
            // without replacing the editor's real player or changing saved display settings.
            auto* PC=World->SpawnActor<APlayerController>();
            // ULocalPlayer has Within=Engine, even when its context uses a different transient world.
            auto* Player=NewObject<ULocalPlayer>(GEngine);
            PC->Player=Player; Player->PlayerController=PC; PC->SetAsLocalPlayerController();
            return CreateWidget<UMCMainMenuWidget>(PC,UMCMainMenuWidget::StaticClass());
        }
    };

    bool HasText(UMCMainMenuWidget* Menu, const FString& Expected)
    {
        bool bFound=false;
        Menu->WidgetTree->ForEachWidget([&](UWidget* Widget)
        {
            if (const auto* Text=Cast<UTextBlock>(Widget)) bFound|=Text->GetText().ToString()==Expected;
        });
        return bFound;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCMainMenuIsolated,
    "MessControl.Menu.IsolatedFromGameplay",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCMainMenuIsolated::RunTest(const FString&)
{
    MCMainMenuTestsPrivate::FMenuWorld Test;
    const auto* Mode=Test.World->GetAuthGameMode<AMCMainMenuGameMode>();
    if (!TestNotNull(TEXT("Menu world uses its own game mode"),Mode)) return false;
    TestNull(TEXT("Menu mode does not inherit the gameplay mode"),Test.World->GetAuthGameMode<AMCGameMode>());
    TestNull(TEXT("Menu cannot spawn a gameplay pawn"),Mode->DefaultPawnClass.Get());
    TestTrue(TEXT("Menu owns a separate controller"),Mode->PlayerControllerClass==AMCMainMenuPlayerController::StaticClass());
    for (int32 Tick=0;Tick<120;++Tick) Test.World->Tick(LEVELTICK_All,1.f/60);
    TestFalse(TEXT("Menu startup never creates a pawn"),static_cast<bool>(TActorIterator<APawn>(Test.World)));
    TestFalse(TEXT("Menu startup never creates a day director"),static_cast<bool>(TActorIterator<AMCDayDirector>(Test.World)));
    TestNull(TEXT("Menu does not start or replicate the gameplay run state"),Test.World->GetGameState<AMCGameState>());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCMainMenuJoinAddress,
    "MessControl.Menu.JoinAddressBecomesNetworkTravel",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCMainMenuJoinAddress::RunTest(const FString&)
{
    const struct { const TCHAR* Input; const TCHAR* Host; int32 Port; } Cases[]=
    {
        {TEXT(" localhost "),TEXT("localhost"),7777},
        {TEXT("coop-pc:17777"),TEXT("coop-pc"),17777},
        {TEXT("192.168.1.10"),TEXT("192.168.1.10"),7777},
        {TEXT("coop-pc.local:17777"),TEXT("coop-pc.local"),17777},
        {TEXT("127.0.0.1:1"),TEXT("127.0.0.1"),1},
        {TEXT("127.0.0.1:65535"),TEXT("127.0.0.1"),65535}
    };
    for (const auto& Item:Cases)
    {
        FString Address;
        if (!TestTrue(*FString::Printf(TEXT("Accept endpoint %s"),Item.Input),UMCMainMenuWidget::NormalizeJoinAddress(Item.Input,Address))) continue;
        // Check the engine's parser, so a valid-looking bare hostname cannot silently become a map load.
        FURL URL(nullptr,*Address,TRAVEL_Absolute);
        TestTrue(TEXT("Unreal accepts the normalized endpoint"),URL.Valid!=0);
        TestEqual(TEXT("Explicit Unreal protocol avoids bare-hostname protocol parsing"),URL.Protocol,FString(TEXT("unreal")));
        TestEqual(TEXT("Unreal routes the value as a host"),URL.Host,FString(Item.Host));
        TestEqual(TEXT("Unreal uses the requested/default port"),URL.Port,Item.Port);
        TestEqual(TEXT("Direct joins never inject travel options"),URL.Op.Num(),0);
    }
    for (const TCHAR* Bad:{TEXT(""),TEXT(" "),TEXT("192.168.0.999"),TEXT("127.0.0.1:0"),TEXT("127.0.0.1:65536"),
        TEXT("127.0.0.1:abc"),TEXT("127.0.0.1:"),TEXT("host name"),TEXT("-host"),TEXT("host..local"),
        TEXT("/Game/Maps/L_Mouth"),TEXT("127.0.0.1?MCTutorial=1"),TEXT("unreal://127.0.0.1"),TEXT("host:7777:8888")})
    {
        FString Address=TEXT("old-address");
        TestFalse(*FString::Printf(TEXT("Reject invalid endpoint %s"),Bad),UMCMainMenuWidget::NormalizeJoinAddress(Bad,Address));
        TestTrue(TEXT("Invalid input does not leave a usable previous endpoint"),Address.IsEmpty());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCMainMenuNavigation,
    "MessControl.Menu.WidgetNavigationAndTravel",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCMainMenuNavigation::RunTest(const FString&)
{
    using namespace MCMainMenuTestsPrivate;
    FMenuWorld Test;
    UMCMainMenuWidget* Menu=Test.NewMenu();
    if (!TestNotNull(TEXT("Production native menu initializes"),Menu) || !TestNotNull(TEXT("Native menu builds its widget tree"),Menu->WidgetTree->RootWidget.Get())) return false;
    for (const TCHAR* Label:{TEXT("Tutorial"),TEXT("Create Lobby"),TEXT("Join Lobby"),TEXT("Settings"),TEXT("Credits"),TEXT("Exit")})
        TestTrue(*FString::Printf(TEXT("Main menu exposes %s"),Label),HasText(Menu,Label));

    Menu->HandleAction(EMCMainMenuAction::Settings);
    TestTrue(TEXT("Settings exposes a real apply action"),HasText(Menu,TEXT("Apply & Save")));
    TestFalse(TEXT("Settings does not retain main menu actions"),HasText(Menu,TEXT("Tutorial")));
    Menu->HandleAction(EMCMainMenuAction::Back);
    TestTrue(TEXT("Settings Back returns to the main menu"),HasText(Menu,TEXT("Create Lobby")));
    Menu->HandleAction(EMCMainMenuAction::Credits);
    TestTrue(TEXT("Credits page opens"),HasText(Menu,TEXT("CREDITS")));
    Menu->HandleAction(EMCMainMenuAction::Back);
    TestTrue(TEXT("Credits Back returns to the main menu"),HasText(Menu,TEXT("Tutorial")));

    FWorldContext& Context=GEngine->GetWorldContextFromWorldChecked(Test.World);
    Menu->HandleAction(EMCMainMenuAction::Tutorial);
    FURL Tutorial(nullptr,*Context.TravelURL,TRAVEL_Absolute);
    TestEqual(TEXT("Tutorial launches the existing arena"),Tutorial.Map,FString(TEXT("/Game/Maps/L_Mouth")));
    TestTrue(TEXT("Tutorial explicitly opts in"),Tutorial.HasOption(TEXT("MCTutorial=1")));
    TestFalse(TEXT("Tutorial launch remains standalone"),Tutorial.HasOption(TEXT("listen")));
    Context.TravelURL.Reset();
    Menu->HandleAction(EMCMainMenuAction::Host);
    FURL Lobby(nullptr,*Context.TravelURL,TRAVEL_Absolute);
    TestTrue(TEXT("Local host waits for Start instead of advancing the gameplay loop"),Lobby.HasOption(TEXT("MCLobby=1")));
    TestTrue(TEXT("Local host accepts friends"),Lobby.HasOption(TEXT("listen")));
    TestFalse(TEXT("Normal lobby never enables the tutorial implicitly"),Lobby.HasOption(TEXT("MCTutorial=")));
    Context.TravelURL.Reset();

    Menu->ShowPause();
    TestTrue(TEXT("In-game menu exposes Resume"),HasText(Menu,TEXT("Resume")));
    TestFalse(TEXT("In-game menu cannot launch a second tutorial"),HasText(Menu,TEXT("Tutorial")));
    Menu->HandleAction(EMCMainMenuAction::Settings); Menu->HandleAction(EMCMainMenuAction::Back);
    TestTrue(TEXT("Settings Back preserves the in-game context"),HasText(Menu,TEXT("Resume")));
    Menu->HandleAction(EMCMainMenuAction::Credits); Menu->HandleAction(EMCMainMenuAction::Back);
    TestTrue(TEXT("Credits Back preserves the in-game context"),HasText(Menu,TEXT("Return to Main Menu")));
    return true;
}

#endif
