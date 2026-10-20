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
    "MessControl.SingleDay.PostNutRelax.PreservesOpeningAndUsesFiveEightNine",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPostNutRelaxDefaultsTest::RunTest(const FString&)
{
    const auto* Profile=GetDefault<UMCSingleDayProfile>();
    TestTrue(TEXT("The existing opening contract remains 3/5/8"),Profile->OpeningMealPortions==TArray<int32>({3,5,8}));
    TestTrue(TEXT("Post-nut servings are whole ingredient portions 5/8/9"),Profile->AfterNutMealPortions==TArray<int32>({5,8,9}));
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
    FFixture F;F.CompleteNuts();
    if(!TestEqual(TEXT("The real nut-completion handoff starts its meal"),F.Loop->Stage,EMCSingleDayStage::PostNutMeal)) return false;
    TestNull(TEXT("Adaptive support cannot inject hazards into this explicit Relax"),F.Mode->GameDirector.Get());
    TSet<uint32> Issued;int32 Batch=0;
    for(int32 Portion=0;Portion<3;++Portion) {
        const int32 Target=F.Profile->AfterNutMealPortions[Portion];
        TestEqual(TEXT("The serving target follows its authored portion"),F.Loop->MealPortionTarget,Target);
        const int32 Before=Issued.Num();
        for(int32 Attempt=0;Attempt<Target+3 && F.Loop->MealPortionSpawned<Target;++Attempt) {
            F.Clock(F.World->GetTimeSeconds()+F.Profile->AfterNutMealDropSeconds+.02f);F.Loop->Tick(.1f);
            for(TActorIterator<AMCFoodActor> It(F.World);It;++It) if(It->GetOwner()==F.Loop && !It->IsActorBeingDestroyed()) {
                Issued.Add(It->GetUniqueID());Batch=It->Batch;
                // External chop/delivery mechanics have separate tests. Supply
                // their terminal removal to the actual food-pipeline census.
                It->Destroy();
            }
        }
        if(!TestEqual(TEXT("Every authored whole ingredient is actually issued"),F.Loop->MealPortionSpawned,Target)) return false;
        TestEqual(TEXT("Back-pressure does not lose ingredients above the six-whole limit"),Issued.Num()-Before,Target);
        TestEqual(TEXT("Finishing issuance does not invent delivery completion"),F.Loop->Stage,EMCSingleDayStage::PostNutMeal);
        if(Portion<2) {
            auto* Held=F.World->SpawnActor<AMCFoodActor>();Held->Batch=Batch;Held->bFragment=true;Held->Phase=EMCFoodPhase::Carried;
            F.Loop->Tick(.1f);
            TestEqual(TEXT("A carried piece blocks the next serving"),F.Loop->MealPortionNumber,Portion+1);
            Held->Destroy();F.Loop->Tick(.1f);
            TestEqual(TEXT("Delivering that piece releases only the next portion"),F.Loop->MealPortionNumber,Portion+2);
            TestTrue(TEXT("Intermediate portions preserve their real dirt until final cleanup"),F.DirtyFloor()>0);
        }
    }
    TestEqual(TEXT("All three native portions issue 22 distinct ingredients"),Issued.Num(),22);
    F.ClearFloor();
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
    F.Loop->Tick(.1f);
    if(!TestEqual(TEXT("Actual shared cleanup releases mint without a timer shortcut"),F.Loop->Stage,EMCSingleDayStage::PostNutMint)) return false;
    if(!TestNotNull(TEXT("The real small-mint actor was started"),F.Loop->SmallMint.Get())) return false;
    TestEqual(TEXT("Mint must land before it can pulse"),F.Loop->SmallMint->Stage,EMCSmallMintStage::Falling);
    for(TActorIterator<AMCCoffeeFlood> It(F.World);It;++It) TestFalse(TEXT("No coffee is inserted into this Relax"),It->IsActive());
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
