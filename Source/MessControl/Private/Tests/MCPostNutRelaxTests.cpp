#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "MCArenaTooth.h"
#include "MCCoffeeFlood.h"
#include "MCDayPlan.h"
#include "MCFoodActor.h"
#include "MCGameDirectorProfile.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCHazardWave.h"
#include "MCInventoryComponent.h"
#include "MCMouthSurface.h"
#include "MCNutRainEvent.h"
#include "MCSingleDayDirector.h"
#include "MCRewardChest.h"
#include "MCSmallMintCandy.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCTutorialDirector.h"
#include "MCThroat.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCPostNutRelaxTestsPrivate
{
/** Rules fixture: the engine owns time/queries; unrelated actors stay still. */
struct FFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCGameMode* Mode=nullptr;
    AMCGameState* State=nullptr;
    AMCSingleDayDirector* Loop=nullptr;
    AMCTongue* Tongue=nullptr;
    AMCToothCharacter* Hero=nullptr;
    UMCDayPlan* Plan=nullptr;
    UMCSingleDayProfile* Profile=nullptr;
    UStaticMesh* Cube=nullptr;

    FFixture()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/MessControl.MCGameMode"));
        World->SetGameMode(URL);Mode=World->GetAuthGameMode<AMCGameMode>();
        Mode->bUseSingleDayLoop=true;
        World->InitializeActorsForPlay(URL);World->BeginPlay();
        State=World->GetGameState<AMCGameState>();Loop=State->SingleDayDirector;
        if(Mode->TutorialDirector) {Mode->TutorialDirector->Stop();Mode->TutorialDirector->Destroy();Mode->TutorialDirector=nullptr;}
        State->bTutorialActive=State->bLobbyWaiting=false;State->Phase=EMCShiftPhase::Working;
        for(TActorIterator<AActor> It(World);It;++It) Freeze(*It);
        for(TActorIterator<AMCArenaTooth> It(World);It;++It) {It->SetCoffee(0);It->ClearGrimeForBooster();}
        ClearFloor();

        auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
        Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
        if(Plane) FStaticMeshCompilingManager::Get().FinishCompilation({Plane});
        if(Cube) FStaticMeshCompilingManager::Get().FinishCompilation({Cube});
#endif
        const FTransform Floor(FRotator::ZeroRotator,FVector(0,0,-50),FVector(40,40,1));
        Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Floor);
        Tongue->SourceMesh=Plane;Tongue->bAutomaticYawns=false;Tongue->FinishSpawning(Floor);
        Tongue->SetActorTickEnabled(false);
        Hero=MakeHero(FVector(-500,0,0));

        Plan=NewObject<UMCDayPlan>(World);
        auto* Menu=NewObject<UDataTable>(World);Menu->RowStruct=FMCFoodRow::StaticStruct();
        FMCFoodRow Food;Food.Kind=EMCFoodKind::Food;Food.Health=10;Food.Fragments=1;
        Food.Resistance=EMCFoodResistance::Soft;Food.Collision.bAutoOptimize=false;
        Food.WholeMeshes={Cube};Food.FragmentMeshes={Cube};Menu->AddRow(TEXT("PostNutIngredient"),Food);Plan->Menu=Menu;
        Profile=NewObject<UMCSingleDayProfile>(World);
        Profile->KeyEvents.SetNum(2);
        Profile->KeyEvents[0].Kind=EMCSingleDayKeyEventKind::NutEncounter;
        Profile->KeyEvents[1].Kind=EMCSingleDayKeyEventKind::Jellies;
        for(auto& Slot:Profile->KeyEvents) Slot.CompletionExperience=0;
        auto* Support=NewObject<UMCGameDirectorProfile>(World);
        for(auto& Day:Support->Days) Day.InitialPatches=0;
        for(auto& Rule:Support->Events) Rule.Weight=0;
        Loop->Initialize(Plan,Profile,Support);Loop->SetActorTickEnabled(false);
    }
    ~FFixture()
    {
        World->EndPlay(EEndPlayReason::Quit);
        GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    }
    static void Freeze(AActor* Actor)
    {
        Actor->SetActorTickEnabled(false);Actor->SetActorEnableCollision(false);
        TInlineComponentArray<UActorComponent*> Components(Actor);
        for(auto* Component:Components) {
            Component->SetComponentTickEnabled(false);
            if(auto* Primitive=Cast<UPrimitiveComponent>(Component);Primitive && Primitive->IsSimulatingPhysics()) Primitive->SetSimulatePhysics(false);
        }
    }
    AMCToothCharacter* MakeHero(FVector Feet)
    {
        FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Player=World->SpawnActor<AMCToothCharacter>(AMCToothCharacter::StaticClass(),Feet+FVector(0,0,100),FRotator::ZeroRotator,Spawn);
        auto* Controller=World->SpawnActor<APlayerController>();Controller->Possess(Player);
        Player->SetActorTickEnabled(false);Player->GetCharacterMovement()->SetComponentTickEnabled(false);
        Player->GetMesh()->SetComponentTickEnabled(false);Player->ToothPhysics->SetComponentTickEnabled(false);
        Place(Player,Feet);return Player;
    }
    void Place(AMCToothCharacter* Player,FVector Feet,float AirborneClearance=0)
    {
        FHitResult Floor;if(Tongue->SurfacePoint(Feet,Floor)) Feet=Floor.ImpactPoint;
        Player->GetCharacterMovement()->StopMovementImmediately();
        Player->SetActorLocation(Feet+FVector(0,0,Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2+AirborneClearance),
            false,nullptr,ETeleportType::TeleportPhysics);
        Player->GetCharacterMovement()->SetMovementMode(AirborneClearance>0?MOVE_Falling:MOVE_Walking);
    }
    void Clock(double Time)
    {
        World->GetWorldSettings()->MaxUndilatedFrameTime=3600;
        ++GFrameCounter;World->Tick(LEVELTICK_TimeOnly,FMath::Max(.001f,float(Time-World->GetTimeSeconds())));
    }
    void Timers(float Seconds)
    {
        for(int32 Index=0,Frames=FMath::CeilToInt(Seconds*60);Index<Frames;++Index) {
            ++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60);
        }
    }
    void ClearFloor()
    {
        for(TActorIterator<AMCMouthSurface> It(World);It;++It)
            if(!It->bUlcer && !It->IsActorBeingDestroyed()) It->ClearLiquidForBooster();
    }
    int32 DirtyFloor() const
    {
        int32 Count=0;for(TActorIterator<AMCMouthSurface> It(World);It;++It)
            if(!It->IsActorBeingDestroyed() && !It->bUlcer && !It->IsClean()) ++Count;
        return Count;
    }
    void CompleteNuts()
    {
        // Combat completion is an external signal, as in the existing sequence
        // fixture. The real sequence tick owns every subsequent handoff.
        Loop->Stage=EMCSingleDayStage::Nuts;Loop->KeyEventIndex=0;
        Loop->NutEvent=World->SpawnActor<AMCNutRainEvent>();
        Loop->NutEvent->Stage=EMCNutRainStage::Complete;
        Loop->NutEvent->EnemiesLeft=Loop->NutEvent->BossesLeft=0;Loop->Tick(.1f);
        ResolveStageReward();
    }
    void ResolveStageReward()
    {
        if(!Loop->bStageRewardPending) return;
        // Feed the real director its terminal reward signal; effect/claim
        // correctness is covered independently by StageBooster reward tests.
        auto* Reward=Loop->StageRewardChest.Get();
        if(!IsValid(Reward)) {
            Reward=World->SpawnActor<AMCRewardChest>();Reward->SetOwner(Loop);
            Reward->InitializeTeamStageReward(FVector::ZeroVector,FVector(0,0,450),31,nullptr,nullptr);
        }
        Reward->ClaimedMask=7;Loop->StageRewardChest=Reward;
        Loop->NotifyStageRewardFinished(Reward,false);Loop->Tick(.1f);Reward->Destroy();
    }
    AMCHazardWave* LastWave(AMCSmallMintCandy* Mint) const
    {
        AMCHazardWave* Last=nullptr;
        for(TActorIterator<AMCHazardWave> It(World);It;++It)
            if(It->GetOwner()==Mint && !It->IsActorBeingDestroyed()) Last=*It;
        return Last;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPostNutRelaxDefaultsTest,
    "MessControl.SingleDay.PostNutRelax.FiveThenCoffeeIntervalAndTen",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPostNutRelaxDefaultsTest::RunTest(const FString&)
{
    const auto* Profile=GetDefault<UMCSingleDayProfile>();
    TestTrue(TEXT("The opening contains five whole items in one meal"),Profile->OpeningMealPortions==TArray<int32>({5}));
    TestTrue(TEXT("Post-nut serving contains ten whole items in one meal"),Profile->AfterNutMealPortions==TArray<int32>({10}));
    TestEqual(TEXT("The coffee-to-food interval defaults to six seconds"),Profile->AfterNutCoffeeToFoodSeconds,6.f);
    TestEqual(TEXT("Individual drops retain the existing one-second cadence"),Profile->AfterNutMealDropSeconds,1.f);
    TestEqual(TEXT("Appending coffee stages preserves the serialized Jellies value"),int32(EMCSingleDayStage::Jellies),15);
    if(!TestTrue(TEXT("The explicit jelly slot follows the nut encounter"),Profile->KeyEvents.Num()>=2)) return false;
    TestEqual(TEXT("First key event remains nuts"),Profile->KeyEvents[0].Kind,EMCSingleDayKeyEventKind::NutEncounter);
    TestEqual(TEXT("Jellies own the next event slot"),Profile->KeyEvents[1].Kind,EMCSingleDayKeyEventKind::Jellies);
    TestEqual(TEXT("Earliest first mint front is three seconds after landing"),Profile->SmallMint.FirstWaveMinSeconds,3.f);
    TestEqual(TEXT("Latest first mint front is five seconds after landing"),Profile->SmallMint.FirstWaveMaxSeconds,5.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPostNutRelaxFoodAndCleanupGateTest,
    "MessControl.SingleDay.PostNutRelax.ExactPortionsAndSharedCleanupGate",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPostNutRelaxFoodAndCleanupGateTest::RunTest(const FString&)
{
    using namespace MCPostNutRelaxTestsPrivate;
    FFixture F;
    auto* SavedCoffee=NewObject<UMCCoffeeProfile>(F.World);
    SavedCoffee->Settings.Cycles=4;F.Plan->CoffeeProfile=SavedCoffee;
    F.CompleteNuts();
    if(!TestEqual(TEXT("Nut completion starts its drink before any food"),F.Loop->Stage,EMCSingleDayStage::PostNutCoffee)) return false;
    TestNull(TEXT("Adaptive support cannot inject hazards into this explicit Relax"),F.Mode->GameDirector.Get());
    TestFalse(TEXT("The prior nut reward has finished before the drink"),F.Loop->bStageRewardPending);
    AMCCoffeeFlood* Coffee=nullptr;
    for(TActorIterator<AMCCoffeeFlood> It(F.World);It;++It)
        if(It->GetOwner()==F.Loop && !It->IsActorBeingDestroyed()) {Coffee=*It;break;}
    if(!TestNotNull(TEXT("The actual native coffee wave is running"),Coffee)) return false;
    TestTrue(TEXT("The drink uses the ordinary river wave"),Coffee->bRiverFlood && !Coffee->IsCataclysmStorm());
    TestEqual(TEXT("Only one physical coffee wave runs"),Coffee->Waves,1);
    TestEqual(TEXT("Its replicated settings contain one cycle"),Coffee->WaterSettings.Cycles,1);
    TestEqual(TEXT("The saved coffee profile is preserved"),SavedCoffee->Settings.Cycles,4);
    TestEqual(TEXT("Clients retain their existing coffee visual profile"),Coffee->Profile.Get(),SavedCoffee);
    TestEqual(TEXT("No meal starts during coffee"),F.Loop->MealPortionSpawned,0);
    F.Clock(F.World->GetTimeSeconds()+Coffee->Seconds+20);
    F.Loop->Tick(.1f);
    TestEqual(TEXT("The nominal deadline cannot bypass a still-active wave"),F.Loop->Stage,EMCSingleDayStage::PostNutCoffee);
    TestEqual(TEXT("The coffee-to-food interval has not started while water is active"),F.Loop->StageEndsAt,0.);
    Coffee->Tick(.1f);
    if(!TestFalse(TEXT("The coffee's actual update reaches its terminal state"),Coffee->IsActive())) return false;
    // Retain visible cleaning debt even if this test floor has no painted tile.
    auto* CoffeeDirt=F.World->SpawnActor<AMCMouthSurface>();
    CoffeeDirt->Batch=Coffee->ResidueBatch;CoffeeDirt->Status->ApplyCoffee(.4f);
    CoffeeDirt->SetActorTickEnabled(false);
    const double WaveEnd=F.State->GetServerWorldTimeSeconds();
    F.Loop->Tick(.1f);
    if(!TestEqual(TEXT("The real wave end starts its authored interval"),F.Loop->Stage,EMCSingleDayStage::PostNutCoffeePause)) return false;
    TestEqual(TEXT("The interval is exactly six seconds from observed full wave completion"),F.Loop->StageEndsAt-WaveEnd,6.);
    TestEqual(TEXT("No food appears at wave completion"),F.Loop->MealPortionSpawned,0);
    TestFalse(TEXT("There is no reward between the drink and food"),F.Loop->bStageRewardPending);
    F.Clock(F.Loop->StageEndsAt-.01);F.Loop->Tick(.1f);
    TestEqual(TEXT("Food cannot fall during the six-second post-wave interval"),F.Loop->MealPortionSpawned,0);
    F.Clock(F.Loop->StageEndsAt+.01);F.Loop->Tick(.1f);
    if(!TestEqual(TEXT("Interval expiry starts the ordinary food stage"),F.Loop->Stage,EMCSingleDayStage::PostNutMeal)) return false;
    TestEqual(TEXT("The first item is served on that expiry tick"),F.Loop->MealPortionSpawned,1);
    TestFalse(TEXT("Coffee residue is retained when food begins"),CoffeeDirt->IsClean());
    TestFalse(TEXT("Dirty residue does not add a reward or cleanliness gate before food"),F.Loop->bStageRewardPending);
    TestEqual(TEXT("There is one post-nut serving"),F.Loop->MealPortionCount,1);
    TestEqual(TEXT("That serving targets exactly ten whole ingredients"),F.Loop->MealPortionTarget,10);
    TArray<AMCFoodActor*> Issued;
    auto Capture=[&]() {
        for(TActorIterator<AMCFoodActor> It(F.World);It;++It)
            if(It->GetOwner()==F.Loop && !It->IsActorBeingDestroyed() && !Issued.Contains(*It)) {
                Issued.Add(*It);FFixture::Freeze(*It);
            }
    };
    Capture();
    F.Clock(F.World->GetTimeSeconds()+F.Profile->AfterNutMealDropSeconds-.02f);F.Loop->Tick(.1f);
    TestEqual(TEXT("The next ingredient still respects its existing individual drop interval"),F.Loop->MealPortionSpawned,1);
    for(int32 Attempt=0;Attempt<20 && F.Loop->MealPortionSpawned<10;++Attempt) {
        F.Clock(F.World->GetTimeSeconds()+F.Profile->AfterNutMealDropSeconds+.02f);
        F.Loop->Tick(.1f);Capture();
    }
    if(!TestEqual(TEXT("The real serving path issues all ten ingredients"),Issued.Num(),10)) return false;
    TestEqual(TEXT("A six-whole solo cap no longer splits this ten-item authored meal"),F.Loop->MealPortionSpawned,10);
    F.Clock(F.World->GetTimeSeconds()+60);F.Loop->Tick(.1f);Capture();
    TestEqual(TEXT("Time cannot issue an eleventh item"),Issued.Num(),10);
    TestFalse(TEXT("Time cannot reward undelivered food"),F.Loop->bStageRewardPending);
    for(auto* Food:Issued) Food->Destroy();
    F.Loop->Tick(.1f);
    TestFalse(TEXT("Finishing food still waits for actual coffee and food dirt"),F.Loop->bStageRewardPending);
    F.ClearFloor();
    // The real drink can stain the world's fallback teeth as well. Resolve
    // those before introducing the single controlled tooth debt below.
    for(TActorIterator<AMCArenaTooth> It(F.World);It;++It) if(It->IsAvailable() && It->Status) {
        const int32 Layers=It->Status->State.CoffeeLeft;
        for(int32 Layer=0;Layer<Layers;++Layer) It->Status->CareContact(true,nullptr);
    }
    auto* Fragment=F.World->SpawnActor<AMCFoodActor>();Fragment->Batch=991234;Fragment->bFragment=true;Fragment->Phase=EMCFoodPhase::Carried;
    auto* Dirt=F.World->SpawnActor<AMCMouthSurface>();Dirt->Batch=997777;Dirt->Status->ApplyCoffee(.4f);Dirt->SetActorTickEnabled(false);
    const FTransform ToothPose(FVector(1100,0,70));
    auto* Tooth=F.World->SpawnActorDeferred<AMCArenaTooth>(AMCArenaTooth::StaticClass(),ToothPose);
    FMCArenaToothSettings ToothSettings;ToothSettings.InitialCalculusEveryNthTooth=0;
    Tooth->SetAppearance(F.Cube,FVector(1));Tooth->Initialize(42,ToothSettings);Tooth->FinishSpawning(ToothPose);
    FFixture::Freeze(Tooth);Tooth->SetCoffee(1.f);F.State->ArenaTeeth.Add(Tooth);
    F.Loop->Tick(.1f);
    TestEqual(TEXT("Final gate includes unrelated carried food, floor dirt and available dirty teeth"),F.State->TasksLeft,3);
    TestNull(TEXT("Mint does not appear before shared cleanup"),F.Loop->SmallMint.Get());
    Fragment->Destroy();F.Loop->Tick(.1f);
    TestEqual(TEXT("Floor dirt still blocks mint after all food has gone"),F.Loop->Stage,EMCSingleDayStage::PostNutMeal);
    Dirt->ClearLiquidForBooster();F.Loop->Tick(.1f);
    TestEqual(TEXT("A dirty available tooth remains a real final task"),F.State->TasksLeft,1);
    const int32 Contacts=Tooth->Status->State.CoffeeLeft;
    for(int32 Index=0;Index<Contacts;++Index)
        if(!TestTrue(TEXT("Real tooth care clears its remaining layer"),Tooth->Status->CareContact(true,nullptr))) return false;
    auto* Throat=F.World->SpawnActor<AMCThroat>();FFixture::Freeze(Throat);
    Throat->QueuedFoodCount=1;Throat->ThroatPhase=EMCThroatPhase::Collecting;
    F.Loop->Tick(.1f);
    TestFalse(TEXT("Queued disposal work still prevents a completion reward"),F.Loop->bStageRewardPending);
    Throat->QueuedFoodCount=0;Throat->ThroatPhase=EMCThroatPhase::Recovering;
    F.Loop->Tick(.1f);
    TestFalse(TEXT("The last disposal recovery must actually finish"),F.Loop->bStageRewardPending);
    Throat->ThroatPhase=EMCThroatPhase::Collecting;
    F.Loop->Tick(.1f);
    TestTrue(TEXT("All real work opens one food-segment reward"),F.Loop->bStageRewardPending);
    TestEqual(TEXT("Coffee and ten foods share a single completion identity"),F.Loop->StageRewardCompletionId,FName(TEXT("PostNutPortion_1")));
    F.ResolveStageReward();
    if(!TestEqual(TEXT("The completed meal retains the authored small-mint stage"),F.Loop->Stage,EMCSingleDayStage::PostNutMint)) return false;
    if(!TestNotNull(TEXT("The real small-mint actor was started"),F.Loop->SmallMint.Get())) return false;
    TestEqual(TEXT("Mint must land before it can pulse"),F.Loop->SmallMint->Stage,EMCSmallMintStage::Falling);
    for(TActorIterator<AMCCoffeeFlood> It(F.World);It;++It)
        TestFalse(TEXT("The single opening drink has finished"),It->IsActive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSmallMintWaveContractTest,
    "MessControl.SingleDay.PostNutRelax.MintRepeatsJumpableHarmlessFrontsUntilBroken",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSmallMintWaveContractTest::RunTest(const FString&)
{
    using namespace MCPostNutRelaxTestsPrivate;
    FFixture F;
    auto* Mint=F.World->SpawnActor<AMCSmallMintCandy>();Mint->SetActorTickEnabled(false);
    FMCSmallMintCandySettings Config;
    if(!TestTrue(TEXT("A real mint can start on the queried tongue surface"),Mint->Start(F.Tongue,Config,9831))) return false;
    const double FallDeadline=Mint->StartedAt+Mint->Settings.FallSeconds;
    F.Clock(FallDeadline-.01);Mint->Tick(.001f);
    TestEqual(TEXT("No active attack exists during the fall"),Mint->WavesEmitted,0);
    F.Clock(FallDeadline+.01);Mint->Tick(.001f);
    if(!TestEqual(TEXT("The fall finishes through normal mint Tick"),Mint->Stage,EMCSmallMintStage::Active)) return false;
    TestTrue(TEXT("First pulse is scheduled 3–5 seconds after actual landing"),Mint->NextWaveAt-Mint->LandedAt>=2.999 && Mint->NextWaveAt-Mint->LandedAt<=5.001);
    const FVector Center=Mint->LandingPoint;
    F.Place(F.Hero,Center+FVector(-500,0,0));
    auto* Jumping=F.MakeHero(Center+FVector(500,0,0));F.Place(Jumping,Center+FVector(500,0,0),45);
    TestTrue(TEXT("The airborne capsule sole clears the actual ground-wave query"),Jumping->ClearsGroundWave(12));
    const float GroundHealth=F.Hero->Status->State.Health,AirHealth=Jumping->Status->State.Health;
    const FVector GroundPosition=F.Hero->GetActorLocation();
    const int32 GroundKnockdowns=F.Hero->ToothPhysics->KnockdownCount;
    F.Clock(Mint->NextWaveAt-.01);Mint->Tick(.001f);
    TestEqual(TEXT("The first front cannot be emitted early"),Mint->WavesEmitted,0);
    F.Clock(Mint->NextWaveAt+.01);Mint->Tick(.001f);
    auto* First=F.LastWave(Mint);
    if(!TestNotNull(TEXT("The real mint emits its first wave"),First)) return false;
    First->SetActorTickEnabled(false);
    const double Next=Mint->NextWaveAt;
    TestTrue(TEXT("Recovery begins after travel, rather than overlapping successive fronts"),
        Next-First->StartedAt>=Config.WaveTravelSeconds+Config.WaveRestMinSeconds-.02
        && Next-First->StartedAt<=Config.WaveTravelSeconds+Config.WaveRestMaxSeconds+.02);
    F.Clock(First->StartedAt+Config.WaveTravelSeconds*.3);First->Tick(.1f);
    TestTrue(TEXT("The swept front freezes grounded feet"),F.Hero->HasFrozenLegs());
    TestFalse(TEXT("The same swept front can be jumped"),Jumping->HasFrozenLegs());
    TestEqual(TEXT("Grounded hit preserves player health"),F.Hero->Status->State.Health,GroundHealth);
    TestEqual(TEXT("Airborne player health also remains unchanged"),Jumping->Status->State.Health,AirHealth);
    TestTrue(TEXT("Freeze does not launch or displace the grounded pawn"),F.Hero->GetActorLocation().Equals(GroundPosition,.01));
    TestTrue(TEXT("Freeze keeps tool use available"),F.Hero->CanWork());
    TestEqual(TEXT("No ragdoll knockdown is applied"),F.Hero->ToothPhysics->KnockdownCount,GroundKnockdowns);
    F.Place(Jumping,Center+FVector(500,0,0));First->Tick(.1f);
    TestFalse(TEXT("Landing behind a front already jumped does not retroactively freeze feet"),Jumping->HasFrozenLegs());

    F.Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
    const int32 RescueContacts=F.Hero->ConfirmedHitCount;
    // One real held-input edge must sustain self rescue through normal Hero
    // ticks and contact timers. No repeated click or private action resolver.
    F.Hero->SetActorTickEnabled(true);
    F.Hero->SetPrimaryInputHeld(true);
    for(int32 Frame=0;Frame<240 && F.Hero->HasFrozenLegs();++Frame) {
        F.Timers(1.f/60);
    }
    const bool Rescued=!F.Hero->HasFrozenLegs();
    if(!TestTrue(TEXT("One held pickaxe press frees the player's own feet through real ticks"),Rescued)) {
        F.Hero->SetPrimaryInputHeld(false);return false;
    }
    TestEqual(TEXT("Held self rescue clears the actual foot-ice health"),F.Hero->IceLegHealth,0.f);
    TestTrue(TEXT("Held rescue confirms multiple ordinary tool contacts"),F.Hero->ConfirmedHitCount-RescueContacts>=2);
    const int32 SwingsAfterRescue=F.Hero->ValidatedSwingCount;
    const int32 HitsAfterRescue=F.Hero->ConfirmedHitCount;
    // Keep holding through full recovery: ordinary combat must not become
    // automatic once the self-rescue target no longer exists.
    F.Timers(F.Hero->Inventory->SwingDuration()+1.f);
    TestEqual(TEXT("Holding after self rescue cannot start another combat swing"),F.Hero->ValidatedSwingCount,SwingsAfterRescue);
    TestEqual(TEXT("Holding after self rescue cannot add another contact"),F.Hero->ConfirmedHitCount,HitsAfterRescue);
    F.Hero->SetPrimaryInputHeld(false);F.Hero->SetActorTickEnabled(false);
    F.Clock(FMath::Max(Next+.01,double(F.World->GetTimeSeconds())+.01));Mint->Tick(.001f);
    TestEqual(TEXT("A living mint repeats rather than completing after one front"),Mint->WavesEmitted,2);
    TestFalse(TEXT("Emitting repeat fronts cannot end the mint encounter"),Mint->IsComplete());
    auto* Second=F.LastWave(Mint);if(!TestNotNull(TEXT("The second travelling front is a real actor"),Second)) return false;
    Second->SetActorTickEnabled(false);
    F.Place(F.Hero,Center+FVector(-120,0,0));F.Hero->SetActorRotation((Center-F.Hero->GetActorLocation()).Rotation());
    F.Clock(Second->StartedAt+Config.WaveTravelSeconds*.15);Second->Tick(.1f);
    TestTrue(TEXT("A later front can freeze a rescued worker again"),F.Hero->HasFrozenLegs());
    for(int32 Swing=0;Swing<40 && !Mint->IsComplete();++Swing) {
        F.Hero->SwingBrush();F.Timers(F.Hero->Inventory->SwingDuration()+.025f);
    }
    if(!TestTrue(TEXT("Normal pickaxe contacts must actually break the mint"),Mint->IsComplete())) return false;
    TestFalse(TEXT("Breaking the mint frees its remaining frozen workers"),F.Hero->HasFrozenLegs());
    const int32 CompletedWaveCount=Mint->WavesEmitted;
    F.Clock(F.World->GetTimeSeconds()+30);Mint->Tick(.1f);
    TestEqual(TEXT("A broken mint never emits another front"),Mint->WavesEmitted,CompletedWaveCount);
    TestNull(TEXT("Its owned fronts are removed on completion"),F.LastWave(Mint));
    TestEqual(TEXT("The encounter still did not damage the worker"),F.Hero->Status->State.Health,GroundHealth);
    return true;
}

#endif
