#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCRewardChest.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCPerkComponent.h"
#include "MCInventoryComponent.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameModeBase.h"
#include "UObject/UnrealType.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace
{
struct FPerkPauseWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    FPerkPauseWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
    }
    ~FPerkPauseWorld()
    {
        World->EndPlay(EEndPlayReason::Quit);
        GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    }
    AMCPlayerController* Player(bool bHasChoice)
    {
        auto* PC=World->SpawnActor<AMCPlayerController>();
        auto* State=World->SpawnActor<AMCPlayerState>(); PC->SetPlayerState(State);
        auto* Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,10000),FRotator::ZeroRotator);
        PC->Possess(Hero); Hero->Status->Initialize(100);
        if (bHasChoice) {
            State->LevelUpOffer.OfferId=FGuid::NewGuid();
            State->LevelUpOffer.TeamLevel=2;
            State->LevelUpOffer.PerkIDs={TEXT("P01"),TEXT("P02"),TEXT("P03")};
        }
        return PC;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPerkChoiceSharedPauseTest,"MessControl.Rewards.ChoiceSharedPause",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPerkChoiceSharedPauseTest::RunTest(const FString&)
{
    FPerkPauseWorld W;
    auto* First=W.Player(true); auto* Second=W.Player(true); auto* NoChoice=W.Player(false);
    NoChoice->ServerSetPerkChoicePause(true);
    TestFalse(TEXT("A player without a reward cannot pause the shared world"),W.World->IsPaused());
    First->ServerSetPerkChoicePause(true);
    TestTrue(TEXT("A choice pauses gameplay"),W.World->IsPaused());
    TestEqual(TEXT("The server counts every living participant"),First->GetPerkChoiceTotalPlayers(),3);
    const double PausedAt=W.World->GetTimeSeconds();
    for (int32 Frame=0;Frame<120;++Frame) W.World->Tick(LEVELTICK_TimeOnly,1.f/60);
    TestEqual(TEXT("Gameplay time remains frozen while choosing"),W.World->GetTimeSeconds(),PausedAt);
    First->ServerSetPerkChoicePause(true); // Duplicate open notifications must not add another lease.
    First->GetPlayerState<AMCPlayerState>()->ResetLevelUpChoices();
    First->ServerSetPerkChoicePause(false);
    TestTrue(TEXT("A fast first choice cannot resume a teammate before their UI-open RPC"),W.World->IsPaused());
    TestTrue(TEXT("The resolved player waits for the remaining chooser"),First->IsWaitingForPerkTeam());
    TestEqual(TEXT("The waiting snapshot includes the resolved player and player without a choice"),First->GetPerkChoiceReadyPlayers(),2);
    Second->ServerSetPerkChoicePause(false);
    TestTrue(TEXT("A panel-close RPC cannot discard an unconsumed authoritative offer"),W.World->IsPaused());
    Second->GetPlayerState<AMCPlayerState>()->ResetLevelUpChoices();
    Second->ServerSetPerkChoicePause(false);
    TestFalse(TEXT("The last consumed choice resumes the world"),W.World->IsPaused());
    TestFalse(TEXT("The team waiting state ends with the last choice"),First->IsWaitingForPerkTeam());
    W.World->Tick(LEVELTICK_TimeOnly,1.f/60);
    TestTrue(TEXT("Gameplay time advances again after the last choice"),W.World->GetTimeSeconds()>PausedAt);

    for(auto* PC:{First,Second}) {
        auto& Offer=PC->GetPlayerState<AMCPlayerState>()->LevelUpOffer;
        Offer.OfferId=FGuid::NewGuid();Offer.TeamLevel=3;Offer.PerkIDs={TEXT("P01"),TEXT("P02"),TEXT("P03")};
    }
    First->ServerSetPerkChoicePause(true);
    First->Destroy();
    TestTrue(TEXT("Disconnecting the first chooser preserves the other chooser's pause"),W.World->IsPaused());
    TestTrue(TEXT("The pause owner moves to a surviving player"),W.World->GetWorldSettings()->GetPauserPlayerState()!=First->PlayerState);
    Second->Destroy();
    TestFalse(TEXT("Disconnecting the last chooser releases the pause"),W.World->IsPaused());

    auto* Chest=W.World->SpawnActor<AMCRewardChest>();
    auto* Hero=CastChecked<AMCToothCharacter>(NoChoice->GetPawn());
    Chest->OpeningPlayer=Hero; Hero->RewardInteraction=Chest;
    Chest->Stage=EMCRewardChestStage::Opening;
    auto& QueuedOffer=NoChoice->GetPlayerState<AMCPlayerState>()->LevelUpOffer;
    QueuedOffer.OfferId=FGuid::NewGuid();QueuedOffer.TeamLevel=4;QueuedOffer.PerkIDs={TEXT("P01"),TEXT("P02"),TEXT("P03")};
    NoChoice->ServerSetPerkChoicePause(true);
    TestFalse(TEXT("Even a queued level cannot pause the unfinished chest opening"),W.World->IsPaused());
    Chest->Stage=EMCRewardChestStage::Open;
    NoChoice->ServerSetPerkChoicePause(true);
    TestTrue(TEXT("An opened chest choice also pauses gameplay"),W.World->IsPaused());
    Chest->Stage=EMCRewardChestStage::Exhausted;Hero->RewardInteraction=nullptr;
    NoChoice->ServerSetPerkChoicePause(false);
    TestTrue(TEXT("A level queued behind the consumed chest keeps its own choice paused"),W.World->IsPaused());
    NoChoice->GetPlayerState<AMCPlayerState>()->ResetLevelUpChoices();NoChoice->ServerSetPerkChoicePause(false);
    TestFalse(TEXT("Consuming chest cards and queued levels resumes gameplay"),W.World->IsPaused());

    bool ExternalMayResume=false;
    auto* Mode=W.World->GetAuthGameMode();
    Mode->SetPause(NoChoice,FCanUnpause::CreateLambda([&ExternalMayResume](){return ExternalMayResume;}));
    NoChoice->RefreshPerkChoiceTeamState();
    TestTrue(TEXT("Polling without a perk lease cannot clear an unrelated pause"),W.World->IsPaused());
    QueuedOffer.OfferId=FGuid::NewGuid();QueuedOffer.TeamLevel=5;QueuedOffer.PerkIDs={TEXT("P01"),TEXT("P02"),TEXT("P03")};
    NoChoice->ServerSetPerkChoicePause(true);
    NoChoice->GetPlayerState<AMCPlayerState>()->ResetLevelUpChoices();NoChoice->ServerSetPerkChoicePause(false);
    TestTrue(TEXT("Completing perks preserves another owner's rejecting pause lease"),W.World->IsPaused());
    ExternalMayResume=true;Mode->ClearPause();
    TestFalse(TEXT("The unrelated pause resumes under its own condition"),W.World->IsPaused());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPerkChoiceKeepsFoodTest,"MessControl.Rewards.ChoicePreservesCarriedFood",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPerkChoiceKeepsFoodTest::RunTest(const FString&)
{
    FPerkPauseWorld W;
    auto* PC=W.Player(false); auto* Hero=CastChecked<AMCToothCharacter>(PC->GetPawn());
    Hero->GetCharacterMovement()->DisableMovement();
    auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    if(!TestNotNull(TEXT("The fragment fixture has a real engine mesh"),Mesh)) return false;
#if WITH_EDITOR
    FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
    Hero->FoodCollection->Toggle();
    TArray<AMCFoodActor*> Food;
    for(int32 I=0;I<2;++I) {
        const FTransform Transform(Hero->GetActorLocation()+FVector(100,I*40-20,-35));
        auto* Piece=W.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FMCFoodRow Row;Row.WholeMeshes={Mesh};Row.FragmentMeshes={Mesh};Row.Scale=Row.FragmentScale=FVector(.3,.3,.1);Row.SpoilSeconds=300;
        FRandomStream Random(7+I);Piece->ConfigureItem(TEXT("PerkCarryFixture"),Row,Random,true);Piece->FinishSpawning(Transform);
        Piece->Body->SetEnableGravity(false);
        if(!TestTrue(TEXT("Normal collection accepts the fragment before the offer"),Hero->FoodCollection->Collect(Piece))) return false;
        Food.Add(Piece);
    }
    auto* State=PC->GetPlayerState<AMCPlayerState>();
    auto* Table=NewObject<UDataTable>();Table->RowStruct=FMCPerkDefinition::StaticStruct();
    for(int32 I=0;I<3;++I) {
        FMCPerkDefinition Row;Row.MaxStacks=5;
        Table->AddRow(FName(*FString::Printf(TEXT("CarryPerk%d"),I)),Row);
    }
    State->Perks->PerkTable=Table;State->QueueChoicesThroughLevel(2);
    TestTrue(TEXT("A real earned offer gates gameplay actions"),State->LevelUpOffer.IsValid() && !Hero->CanWork());
    // UI input cancellation and component ticks can arrive before the pause RPC.
    // Exercise both paths instead of relying on the paused world skipping ticks.
    Hero->CancelGameplayInput();
    Hero->FoodCollection->TickComponent(1.f/60,LEVELTICK_All,nullptr);Hero->Tick(1.f/60);
    TestEqual(TEXT("Opening the cards retains the entire tower"),Hero->FoodCollection->Pieces.Num(),2);
    for(auto* Piece:Food) TestTrue(TEXT("Each original piece keeps its carrier and slot membership"),Piece->StackCarrier==Hero && Hero->FoodCollection->Contains(Piece));
    TestFalse(TEXT("Cancelled input cannot continue an attack"),Hero->IsPrimaryHeld() || Hero->bBrushing);
    auto* Teammate=W.Player(true);
    PC->ServerSetPerkChoicePause(true);
    if(!TestTrue(TEXT("The real personal offer can be selected"),State->TryChooseLevelUpPerk(State->LevelUpOffer.OfferId,0))) return false;
    PC->RefreshPerkChoiceTeamState();
    TestTrue(TEXT("A resolved chooser waits for the teammate while retaining the load"),PC->IsWaitingForPerkTeam() && Hero->ShouldPreserveFoodForPerkChoice());
    Hero->CancelGameplayInput();Hero->FoodCollection->TickComponent(1.f/60,LEVELTICK_All,nullptr);
    TestEqual(TEXT("The waiting modal retains every original piece"),Hero->FoodCollection->Pieces.Num(),2);
    Teammate->GetPlayerState<AMCPlayerState>()->ResetLevelUpChoices();PC->RefreshPerkChoiceTeamState();
    TestFalse(TEXT("Completing the choice resumes the solo world"),W.World->IsPaused());
    TestEqual(TEXT("Selecting the perk preserves the original tower"),Hero->FoodCollection->Pieces.Num(),2);
    TestTrue(TEXT("Normal Q can throw exactly one retained piece afterwards"),Hero->FoodCollection->ThrowOne(FVector(1,0,.3).GetSafeNormal()));
    TestEqual(TEXT("Q leaves the other original piece in the hand"),Hero->FoodCollection->Pieces.Num(),1);
    // Retention is specific to a valid reward interaction. Ordinary cancellation
    // must still release a load instead of leaving orphaned carriers.
    Hero->CancelGameplayInput();
    TestTrue(TEXT("Ordinary cancellation still drops food outside perk UI"),Hero->FoodCollection->Pieces.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPerkChoiceQueuedBarrierTest,"MessControl.Rewards.ChoiceQueueDeathAndSpectators",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPerkChoiceQueuedBarrierTest::RunTest(const FString&)
{
    FPerkPauseWorld W;
    auto* First=W.Player(false);auto* Second=W.Player(false);auto* Spectator=W.Player(true);
    Spectator->GetPlayerState<AMCPlayerState>()->SetIsSpectator(true);
    auto* Table=NewObject<UDataTable>();Table->RowStruct=FMCPerkDefinition::StaticStruct();
    for(int32 Index=0;Index<6;++Index) {
        FMCPerkDefinition Row;Row.Weight=1+Index;Row.MaxStacks=5;
        Table->AddRow(FName(*FString::Printf(TEXT("WaitFixture%d"),Index)),Row);
    }
    for(auto* PC:{First,Second}) {
        auto* PS=PC->GetPlayerState<AMCPlayerState>();PS->Perks->PerkTable=Table;PS->QueueChoicesThroughLevel(3);
    }
    First->ServerSetPerkChoicePause(true);
    TestEqual(TEXT("Spectators are not counted as waiting teammates"),First->GetPerkChoiceTotalPlayers(),2);
    auto* FirstState=First->GetPlayerState<AMCPlayerState>();
    if(!TestTrue(TEXT("A real queued personal offer is selectable"),FirstState->TryChooseLevelUpPerk(FirstState->LevelUpOffer.OfferId,0))) return false;
    First->RefreshPerkChoiceTeamState();
    TestTrue(TEXT("Remaining queued levels keep the shared pause"),W.World->IsPaused());
    TestFalse(TEXT("A player with another queued card is still choosing, not ready"),First->IsWaitingForPerkTeam());
    TestEqual(TEXT("Only one queue entry was consumed"),FirstState->PendingLevelChoices,1);
    if(!TestTrue(TEXT("The next queued offer can be consumed while paused"),FirstState->TryChooseLevelUpPerk(FirstState->LevelUpOffer.OfferId,0))) return false;
    First->RefreshPerkChoiceTeamState();
    TestTrue(TEXT("Finished queue waits for the other player's whole queue"),First->IsWaitingForPerkTeam());
    TestEqual(TEXT("Exactly one of two living players is ready"),First->GetPerkChoiceReadyPlayers(),1);
    CastChecked<AMCToothCharacter>(Second->GetPawn())->Status->Damage(1000);
    First->RefreshPerkChoiceTeamState();
    TestFalse(TEXT("A dead chooser cannot lock living teammates on pause"),W.World->IsPaused());
    TestFalse(TEXT("The survivor's waiting state closes when the dead chooser is excluded"),First->IsWaitingForPerkTeam());
    TestEqual(TEXT("Death keeps the earned queue available for respawn"),Second->GetPlayerState<AMCPlayerState>()->PendingLevelChoices,2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPerkChoiceDeferredRunTest,"MessControl.Rewards.ChoiceWaitPreservesDeferredSaw",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPerkChoiceDeferredRunTest::RunTest(const FString&)
{
    FPerkPauseWorld W;
    auto* Chooser=W.Player(true);auto* Running=W.Player(true);
    auto* Hero=CastChecked<AMCToothCharacter>(Running->GetPawn());
    // Exercise the same replicated ongoing-run flag used by the existing card
    // deferral, without starting a movement/weapon simulation in this UI test.
    Hero->Inventory->bChainsawRunning=true;
    Chooser->ServerSetPerkChoicePause(true);
    TestTrue(TEXT("The other player's choice can still pause the shared clock"),W.World->IsPaused());
    TestFalse(TEXT("A deferred saw runner must not receive the waiting modal that cancels input"),Running->IsWaitingForPerkTeam());
    TestTrue(TEXT("The pause barrier does not cancel the ongoing saw flag"),Hero->Inventory->bChainsawRunning);
    Chooser->GetPlayerState<AMCPlayerState>()->ResetLevelUpChoices();Chooser->RefreshPerkChoiceTeamState();
    TestFalse(TEXT("The current choice resumes without forcing the runner's deferred card"),W.World->IsPaused());
    TestTrue(TEXT("The deferred personal offer is preserved"),Running->GetPlayerState<AMCPlayerState>()->LevelUpOffer.IsValid());
    TestTrue(TEXT("The saw can continue after the teammate finishes"),Hero->Inventory->bChainsawRunning);
    Hero->Inventory->bChainsawRunning=false;Chooser->RefreshPerkChoiceTeamState();
    TestTrue(TEXT("Ending the run admits its queued personal choice normally"),W.World->IsPaused());
    Running->GetPlayerState<AMCPlayerState>()->ResetLevelUpChoices();Chooser->RefreshPerkChoiceTeamState();
    TestFalse(TEXT("Consuming the deferred choice releases its own lease"),W.World->IsPaused());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPerkChoiceWorldBindingTest,"MessControl.Rewards.ChoicePauseWorldBinding",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPerkChoiceWorldBindingTest::RunTest(const FString&)
{
    FPerkPauseWorld Current;FPerkPauseWorld Previous;
    auto* PC=Current.Player(true);
    auto* Binding=FindFProperty<FWeakObjectProperty>(AMCPlayerController::StaticClass(),TEXT("PerkChoiceWorld"));
    if(!TestNotNull(TEXT("Pause bookkeeping explicitly records its weak world binding"),Binding)) return false;
    PC->ServerSetPerkChoicePause(true);
    TestTrue(TEXT("The current world initially owns a real perk pause"),Current.World->IsPaused());
    PC->NotifyLoadedWorld(Current.World->GetFName(),true);
    PC->NotifyLoadedWorld(Current.World->GetFName(),true);
    Current.World->GetAuthGameMode()->ClearPause();
    TestTrue(TEXT("Repeated presentation resets in the same world preserve its active lease"),Current.World->IsPaused());

    // Model the retained-controller handoff without moving an actor between
    // UWorlds: the old GameMode lease is gone and the cache names the old world.
    // This is a native bookkeeping regression, not a full seamless/network test.
    Current.World->GetAuthGameMode()->ForceClearUnpauseDelegates(PC);
    TestFalse(TEXT("The old GameMode's removed delegate no longer pauses"),Current.World->IsPaused());
    PC->ClientPerkChoiceTeamState(0,0,false,false);
    Binding->SetObjectPropertyValue_InContainer(PC,Previous.World);
    PC->NotifyLoadedWorld(Current.World->GetFName(),true);
    PC->RefreshPerkChoiceTeamState();
    TestTrue(TEXT("A changed world binding reacquires the destination GameMode's lease"),Current.World->IsPaused());
    TestEqual(TEXT("A changed world also resends the fresh owner count snapshot"),PC->GetPerkChoiceTotalPlayers(),1);
    TestTrue(TEXT("The cache now points only to the current world"),Binding->GetObjectPropertyValue_InContainer(PC)==Current.World);
    PC->GetPlayerState<AMCPlayerState>()->ResetLevelUpChoices();PC->RefreshPerkChoiceTeamState();
    TestFalse(TEXT("The destination lease releases after the retained offer is consumed"),Current.World->IsPaused());
    return true;
}
#endif
