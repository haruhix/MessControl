#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "MCTaskCoinPresentationComponent.h"
#include "MCPlayerController.h"
#include "MCWorldPresentationComponent.h"
#include "MCVFXSourceContracts.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "UObject/UnrealType.h"

namespace MCTaskCoinComposerTests
{
/** Ownership-predicate fixture only. No viewport Init (audio/window startup),
 * GameInstance Init, BeginPlay, preparation or successful reward receipt. The
 * real object reference graph is bound explicitly; it is not a preview provider. */
struct FContext
{
    UWorld* World=nullptr;
    UGameInstance* Instance=nullptr;
    UGameViewportClient* Viewport=nullptr;
    ULocalPlayer* Local=nullptr;
    AMCPlayerController* Controller=nullptr;
    APawn* Pawn=nullptr;
    UMCTaskCoinPresentationComponent* Coins=nullptr;
    FContext()
    {
        World=UWorld::CreateWorld(EWorldType::Game,false);
        auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
        Instance=NewObject<UGameInstance>(GEngine);World->SetGameInstance(Instance);
        Controller=World->SpawnActor<AMCPlayerController>();Pawn=World->SpawnActor<APawn>();
        // This world intentionally never initializes actors/BeginPlay. Normally
        // AController::PostInitializeComponents registers this exact controller;
        // UPlayer::GetPlayerController(World) resolves through that world list.
        World->AddController(Controller);
        Controller->SpawnPlayerCameraManager();
        Controller->Possess(Pawn);Controller->SetViewTarget(Pawn);Controller->SetAsLocalPlayerController();
        Viewport=NewObject<UGameViewportClient>(GEngine);
        // Avoid UGameViewportClient::Init: it requests an audio device even in
        // a fixture with no actual game window. Only ownership is under test.
        FindFProperty<FObjectPropertyBase>(UGameViewportClient::StaticClass(),TEXT("World"))->SetObjectPropertyValue_InContainer(Viewport,World);
        FindFProperty<FObjectPropertyBase>(UGameViewportClient::StaticClass(),TEXT("GameInstance"))->SetObjectPropertyValue_InContainer(Viewport,Instance);
        Context.OwningGameInstance=Instance;Context.GameViewport=Viewport;Instance->OnWorldChanged(nullptr,World);
        Local=NewObject<ULocalPlayer>(GEngine);
        Instance->AddLocalPlayer(Local,PLATFORMUSERID_NONE);
        Local->ViewportClient=Viewport;Local->PlayerController=Controller;Controller->Player=Local;
        Coins=NewObject<UMCTaskCoinPresentationComponent>(Controller);
    }
    ~FContext()
    {
        // Detach before public player removal so it cannot destroy controllers
        // or clean up a live game's viewport/session. These are owned fixtures.
        Controller->Player=nullptr;Local->PlayerController=nullptr;
        Instance->RemoveLocalPlayer(Local);
        Instance->OnWorldChanged(World,nullptr);
        World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
};
int32 ActorCount(UWorld* World)
{int32 Count=0;for(TActorIterator<AActor> It(World);It;++It)++Count;return Count;}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCTaskCoinContextEligibilityTest,
    "MessControl.VFX.WorldPresentation.TaskCoinContextOwnership",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCTaskCoinContextEligibilityTest::RunTest(const FString&)
{
    using namespace MCTaskCoinComposerTests;
    for(const auto& Context:GEngine->GetWorldContexts())if(Context.WorldType==EWorldType::PIE) {
        AddError(TEXT("Run outside PIE; only a disposable ownership-predicate fixture is used."));return false;
    }
    FContext F;
    TestEqual(TEXT("The real world controller list resolves this exact local player"),F.Local->GetPlayerController(F.World),static_cast<APlayerController*>(F.Controller));
    TestTrue(TEXT("Real MC ownership predicate recognizes the explicit local object graph"),F.Controller->HasLocalPresentationOwner());
    TestTrue(TEXT("One exact local player/viewport/current pawn can enter the extra context gate"),F.Coins->HasSingleLocalViewportContext(F.Controller,F.Pawn));
    TestFalse(TEXT("Context eligibility never bypasses real resource preparation"),F.Coins->CanPresent(F.Controller,F.Pawn));
    const int32 Before=ActorCount(F.World);
    TestFalse(TEXT("No prepared receipt can be invented by context eligibility"),F.Coins->PublishComposerState());
    TestTrue(TEXT("Refused custom path keeps native art"),F.Coins->bNativeVisuals);
    TestNull(TEXT("Refused/private void path creates no source wrapper"),F.Controller->FindComponentByClass<UMCWorldPresentationComponent>());
    TestEqual(TEXT("Refused path spawns no presentation actor"),ActorCount(F.World),Before);

    auto* Second=NewObject<ULocalPlayer>(GEngine);
    F.Instance->AddLocalPlayer(Second,PLATFORMUSERID_NONE);Second->ViewportClient=F.Viewport;
    TestTrue(TEXT("Adding a second view does not change the original native owner gate"),F.Controller->HasLocalPresentationOwner());
    TestFalse(TEXT("Two local players always refuse custom coins, even in one viewport"),F.Coins->HasSingleLocalViewportContext(F.Controller,F.Pawn));
    F.Instance->RemoveLocalPlayer(Second);
    TestTrue(TEXT("Single ownership is eligible for a future receipt after second player leaves"),F.Coins->HasSingleLocalViewportContext(F.Controller,F.Pawn));

    F.Local->ViewportClient=nullptr;
    TestFalse(TEXT("Private/no-viewport context is refused"),F.Coins->HasSingleLocalViewportContext(F.Controller,F.Pawn));
    F.Local->ViewportClient=F.Viewport;
    auto* OtherPawn=F.World->SpawnActor<APawn>();
    F.Controller->SetViewTarget(OtherPawn);
    TestFalse(TEXT("A spectator/different view target cannot use current owner's custom receipt"),F.Coins->HasSingleLocalViewportContext(F.Controller,F.Pawn));
    F.Controller->SetViewTarget(F.Pawn);
    TestFalse(TEXT("A different pawn is not inferred as the actual owner"),F.Coins->HasSingleLocalViewportContext(F.Controller,OtherPawn));

    const auto* Contract=MCVFXSourceContracts::Find(TEXT("Reward.TaskCoins"));
    const auto* Original=Contract?Contract->OriginalAudio.Find(TEXT("Reward")):nullptr;
    TestTrue(TEXT("One actual original sound row exposes the independently gated source"),Original&&Original->bSuppressionWired
        &&Original->PaletteEvent.IsNone()&&Original->Resource==FSoftObjectPath(TEXT("/Game/Audio/Rewards/S_TaskCoins.S_TaskCoins")));
    TestFalse(TEXT("No suppression is inferred for another reward event"),MCVFXSourceContracts::CanSuppressOriginalSFX(TEXT("Reward.TaskCoins"),TEXT("Other")));
    TestTrue(TEXT("Refused path remains latched without a manufactured accepted receipt"),!F.Coins->bComposerReceiptAllowed&&F.Coins->ActiveCount==0);
    return !HasAnyErrors();
}

#endif
