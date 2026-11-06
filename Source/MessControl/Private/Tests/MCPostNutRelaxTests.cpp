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
        Food.WholeMeshes={Cube};Food.FragmentMeshes={Cube};Menu->AddRow(TEXT("MealIngredient"),Food);Plan->Menu=Menu;
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
    void ClearTeeth()
    {
        for(TActorIterator<AMCArenaTooth> It(World);It;++It) {
            if(It->IsAvailable() && It->Status) {
                const int32 Layers=It->Status->State.CoffeeLeft;
                for(int32 Layer=0;Layer<Layers;++Layer) It->Status->CareContact(true,nullptr);
            }
        }
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSequentialCoffeeCleanupTest,
    "MessControl.SingleDay.Sequence.TwoMealsWaitForActualCoffeeAndCleanup",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSequentialCoffeeCleanupTest::RunTest(const FString&)
{
    using namespace MCPostNutRelaxTestsPrivate;
    FFixture F;
    auto* SavedCoffee=NewObject<UMCCoffeeProfile>(F.World);
    SavedCoffee->Settings.Cycles=4;
    F.Plan->CoffeeProfile=SavedCoffee;

    F.Loop->BeginFirstPerk();
    if(!TestEqual(TEXT("Ready starts the opening pause"),F.Loop->Stage,EMCSingleDayStage::OpeningPause)) return false;
    F.Clock(F.Loop->StageEndsAt+.01);
    F.Loop->Tick(.1f);
    if(!TestEqual(TEXT("Opening pause starts the first meal"),F.Loop->Stage,EMCSingleDayStage::FirstMeal)) return false;
    TestEqual(TEXT("The sequence serves two meals"),F.Loop->MealPortionCount,2);
    TestEqual(TEXT("Opening meal targets five whole items"),F.Loop->MealPortionTarget,5);

    // Dispose of each ingredient to leave the next handoff to the director.
    const auto ServeMeal=[&](int32 Target) -> AMCFoodActor* {
        for(int32 Item=0;Item<Target;++Item) {
            if(F.Loop->MealPortionSpawned<=Item) {
                const float DropInterval=FMath::Max(F.Profile->OpeningMealDropSeconds,F.Profile->AfterNutMealDropSeconds);
                F.Clock(F.World->GetTimeSeconds()+DropInterval+.02);
                F.Loop->Tick(.1f);
            }
            if(!TestEqual(TEXT("Native meal service issues the next whole item"),F.Loop->MealPortionSpawned,Item+1)) return nullptr;
            for(TActorIterator<AMCFoodActor> It(F.World);It;++It) {
                if(It->GetOwner()!=F.Loop || It->IsActorBeingDestroyed() || It->bFragment) continue;
                if(Item+1==Target) {FFixture::Freeze(*It);return *It;}
                It->Destroy();
                break;
            }
        }
        return nullptr;
    };

    auto* FirstMealEnd=ServeMeal(5);
    if(!TestNotNull(TEXT("The first meal spawned all five items"),FirstMealEnd)) return false;
    F.Loop->Tick(.1f);
    TestEqual(TEXT("Outstanding first-meal food blocks the wave"),F.Loop->Stage,EMCSingleDayStage::FirstMeal);
    FirstMealEnd->Destroy();
    F.ClearFloor();
    F.ClearTeeth();
    F.Loop->Tick(.1f);
    if(!TestEqual(TEXT("First meal completion starts the actual coffee wave"),F.Loop->Stage,EMCSingleDayStage::MealCoffeeWave)) return false;
    TestFalse(TEXT("The first meal does not open a reward"),F.Loop->bStageRewardPending);
    TestNull(TEXT("No adaptive director runs during the fixed sequence"),F.Mode->GameDirector.Get());

    AMCCoffeeFlood* Coffee=nullptr;
    for(TActorIterator<AMCCoffeeFlood> It(F.World);It;++It)
        if(It->GetOwner()==F.Loop && !It->IsActorBeingDestroyed()) {Coffee=*It;break;}
    if(!TestNotNull(TEXT("The actual native coffee wave is running"),Coffee)) return false;
    TestTrue(TEXT("The drink uses the ordinary river wave"),Coffee->bRiverFlood && !Coffee->IsCataclysmStorm());
    TestEqual(TEXT("Exactly one physical wave runs"),Coffee->Waves,1);
    TestEqual(TEXT("The original coffee profile keeps its authored cycles"),SavedCoffee->Settings.Cycles,4);
    F.Clock(F.World->GetTimeSeconds()+Coffee->Seconds+20);
    F.Loop->Tick(.1f);
    TestEqual(TEXT("Elapsed time cannot bypass a still-active wave"),F.Loop->Stage,EMCSingleDayStage::MealCoffeeWave);
    Coffee->Tick(.1f);
    if(!TestFalse(TEXT("The wave reaches its terminal state through its own tick"),Coffee->IsActive())) return false;

    auto* Dirt=F.World->SpawnActor<AMCMouthSurface>();
    Dirt->Batch=Coffee->ResidueBatch;
    Dirt->Status->ApplyCoffee(.4f);
    Dirt->SetActorTickEnabled(false);
    F.Loop->Tick(.1f);
    if(!TestEqual(TEXT("The real wave end opens a cleanup stage"),F.Loop->Stage,EMCSingleDayStage::BetweenMealsPause)) return false;
    TestEqual(TEXT("Coffee cleanup has no timer"),F.Loop->StageEndsAt,0.);
    F.Clock(F.World->GetTimeSeconds()+60);
    F.Loop->Tick(.1f);
    TestEqual(TEXT("Coffee dirt still blocks the second meal after a minute"),F.Loop->Stage,EMCSingleDayStage::BetweenMealsPause);
    TestFalse(TEXT("Coffee completion does not open a reward"),F.Loop->bStageRewardPending);
    F.ClearFloor();
    F.ClearTeeth();
    F.Loop->Tick(.1f);
    if(!TestEqual(TEXT("Actual coffee cleanup starts the second meal"),F.Loop->Stage,EMCSingleDayStage::FirstMeal)) return false;
    TestEqual(TEXT("The second meal is the second serving of the sequence"),F.Loop->MealPortionNumber,2);
    TestEqual(TEXT("The second meal targets ten whole items"),F.Loop->MealPortionTarget,10);

    auto* SecondMealEnd=ServeMeal(10);
    if(!TestNotNull(TEXT("The second meal spawned all ten items"),SecondMealEnd)) return false;
    F.Clock(F.World->GetTimeSeconds()+60);
    F.Loop->Tick(.1f);
    TestEqual(TEXT("Waiting cannot serve an eleventh item"),F.Loop->MealPortionSpawned,10);
    TestEqual(TEXT("The last whole item still blocks the key event"),F.Loop->Stage,EMCSingleDayStage::FirstMeal);
    SecondMealEnd->Destroy();
    F.ClearFloor();
    F.ClearTeeth();

    auto* Throat=F.World->SpawnActor<AMCThroat>();
    FFixture::Freeze(Throat);
    Throat->QueuedFoodCount=1;
    Throat->ThroatPhase=EMCThroatPhase::Collecting;
    F.Loop->Tick(.1f);
    TestEqual(TEXT("Queued disposal still blocks the key event"),F.Loop->Stage,EMCSingleDayStage::FirstMeal);
    Throat->QueuedFoodCount=0;
    Throat->ThroatPhase=EMCThroatPhase::Recovering;
    F.Loop->Tick(.1f);
    TestEqual(TEXT("Disposal recovery must actually finish"),F.Loop->Stage,EMCSingleDayStage::FirstMeal);
    Throat->ThroatPhase=EMCThroatPhase::Collecting;
    auto* MealDirt=F.World->SpawnActor<AMCMouthSurface>();
    MealDirt->Status->ApplyCoffee(.4f);
    F.Loop->Tick(.1f);
    TestEqual(TEXT("The second meal's remaining dirt still blocks the key event"),F.Loop->Stage,EMCSingleDayStage::FirstMeal);
    MealDirt->ClearLiquidForBooster();
    F.Loop->Tick(.1f);
    TestEqual(TEXT("All cleared work starts the first key event"),F.Loop->Stage,EMCSingleDayStage::Nuts);
    TestFalse(TEXT("The second meal does not open a reward"),F.Loop->bStageRewardPending);
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
