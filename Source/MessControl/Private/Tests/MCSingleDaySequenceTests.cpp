#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCGameDirector.h"
#include "MCGameDirectorProfile.h"
#include "MCSingleDayDirector.h"
#include "MCRewardChest.h"
#include "MCTutorialDirector.h"
#include "MCNutRainEvent.h"
#include "MCIceEvent.h"
#include "MCFogBrawlEvent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCFoodActor.h"
#include "MCFoodDirectorHooks.h"
#include "MCMouthSurface.h"
#include "MCCoffeeFlood.h"
#include "MCTongue.h"
#include "MCArenaTooth.h"
#include "MCDayPlan.h"
#include "MCColdCola.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCSingleDaySequenceTestsPrivate
{
struct FSequenceWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCGameMode* Mode=nullptr;
    AMCGameState* State=nullptr;
    AMCSingleDayDirector* Loop=nullptr;
    UMCDayPlan* Plan=nullptr;
    UMCGameDirectorProfile* Support=nullptr;
    UMCSingleDayProfile* Sequence=nullptr;
    FSequenceWorld(int32 SavedPrototypeSlots=0)
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/MessControl.MCGameMode"));
        World->SetGameMode(URL);Mode=World->GetAuthGameMode<AMCGameMode>();
        Mode->bUseSingleDayLoop=true;
        World->InitializeActorsForPlay(URL);World->BeginPlay();
        State=World->GetGameState<AMCGameState>();Loop=State->SingleDayDirector;
        if (Mode->TutorialDirector) {Mode->TutorialDirector->Stop();Mode->TutorialDirector->Destroy();Mode->TutorialDirector=nullptr;}
        State->bTutorialActive=State->bLobbyWaiting=false;State->Phase=EMCShiftPhase::Working;
        Plan=NewObject<UMCDayPlan>(World);
        Support=NewObject<UMCGameDirectorProfile>(World);
        Support->InitialCleaningSeconds=1.5f;
        for (auto& Day:Support->Days) {Day.InitialPatches=0;Day.MinimumDifficulty=.55f;Day.MaximumDifficulty=1.1f;}
        // Lifecycle clock cases must not create unrelated hazards or meals.
        for (auto& Rule:Support->Events) Rule.Weight=0;
        Sequence=NewObject<UMCSingleDayProfile>(World);
        // Use three slots to exercise repeated Patterns and prototype expansion.
        Sequence->KeyEvents.RemoveAll([](const FMCSingleDayKeyEvent& Slot) {return Slot.Kind==EMCSingleDayKeyEventKind::Jellies;});
        if(SavedPrototypeSlots==1 || SavedPrototypeSlots==2) {
            Sequence->KeyEvents.SetNum(SavedPrototypeSlots);
            Sequence->KeyEvents[0].EventId=TEXT("AuthoredNut");
            Sequence->KeyEvents[0].DirectorSupportSeconds=9;
            if(SavedPrototypeSlots==2) {
                Sequence->KeyEvents[1].EventId=TEXT("AuthoredIce");
                Sequence->KeyEvents[1].DirectorSupportSeconds=17;
            }
        }
        for(auto& Slot:Sequence->KeyEvents) Slot.CompletionExperience=0;
        Loop->Initialize(Plan,Sequence,Support);
        FreezeActors();
    }
    ~FSequenceWorld() {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    void FreezeActors()
    {
        for (TActorIterator<AActor> It(World);It;++It) {
            It->SetActorTickEnabled(false);It->SetActorEnableCollision(false);
            TArray<UActorComponent*> Components;It->GetComponents(Components);
            for (auto* Component:Components) {
                Component->SetComponentTickEnabled(false);
                if (auto* Primitive=Cast<UPrimitiveComponent>(Component);Primitive && Primitive->IsSimulatingPhysics()) Primitive->SetSimulatePhysics(false);
            }
            if (auto* Status=It->FindComponentByClass<UMCToothStatusComponent>()) {
                Status->State.CoffeeLeft=Status->State.RepairLeft=0;
                Status->State.Health=Status->State.MaxHealth;
            }
        }
    }
    void CompleteNutSlot()
    {
        // Event combat has its own tests. This fixture supplies its terminal signal
        // to the real sequence tick, rather than calling the private handoff.
        Loop->Stage=EMCSingleDayStage::Nuts;
        Loop->NutEvent=World->SpawnActor<AMCNutRainEvent>();
        Loop->NutEvent->Stage=EMCNutRainStage::Complete;
        Loop->NutEvent->EnemiesLeft=Loop->NutEvent->BossesLeft=0;
        Loop->Tick(.1f);
    }
    void CompleteIceSlot()
    {
        Loop->KeyEventIndex=1;
        Loop->Stage=EMCSingleDayStage::Ice;
        Loop->IceEvent=World->SpawnActor<AMCIceEvent>();
        Loop->IceEvent->Stage=EMCIceEventStage::Complete;
        Loop->IceEvent->CandyHealth=0;
        Loop->Tick(.1f);
    }
    void CompleteFogSlot()
    {
        Loop->KeyEventIndex=2;
        Loop->Stage=EMCSingleDayStage::FogBrawl;
        Loop->FogEvent=World->SpawnActor<AMCFogBrawlEvent>();
        Loop->FogEvent->Stage=EMCFogBrawlStage::Complete;
        Loop->FogEvent->StrikesResolved=Loop->FogEvent->TotalStrikes;
        Loop->Tick(.1f);
    }
    void ResolveStageReward()
    {
        if(!Loop->bStageRewardPending) return;
        // Lifecycle-only fixture: provide the terminal claim signal, just like
        // the terminal event signals above. Real opening, immediate effects and
        // duplicate claims are exercised by StageBooster reward tests.
        auto* Reward=Loop->StageRewardChest.Get();
        if(!IsValid(Reward)) {
            Reward=World->SpawnActor<AMCRewardChest>();Reward->SetOwner(Loop);
            Reward->InitializeTeamStageReward(FVector::ZeroVector,FVector(0,0,450),31,nullptr,nullptr);
        }
        Reward->ClaimedMask=7;Loop->StageRewardChest=Reward;
        Loop->NotifyStageRewardFinished(Reward,false);Loop->Tick(.1f);
        Reward->Destroy();
    }
    void RestoreTongueCollision()
    {
        // Dispatch calls the real event Start. Its floor samples need collision,
        // while pawn, hazard and scheduler ticks remain frozen in this fixture.
        for(TActorIterator<AMCTongue> It(World);It;++It) It->SetActorEnableCollision(true);
    }
    TArray<AMCFoodActor*> PatternFood() const
    {
        TArray<AMCFoodActor*> Food;
        for(TActorIterator<AMCFoodActor> It(World);It;++It)
            if(!It->IsActorBeingDestroyed() && It->GetOwner()==Loop && !It->IsDisposed()) Food.Add(*It);
        return Food;
    }
    AMCCoffeeFlood* PatternWave() const
    {
        for(TActorIterator<AMCCoffeeFlood> It(World);It;++It)
            if(!It->IsActorBeingDestroyed() && It->GetOwner()==Loop) return *It;
        return nullptr;
    }
    void ClearMealDirt()
    {
        for(TActorIterator<AMCMouthSurface> It(World);It;++It)
            if(!It->bUlcer) It->ClearLiquidForBooster();
        for(TActorIterator<AMCArenaTooth> It(World);It;++It) if(It->IsAvailable() && It->Status) {
            const int32 Layers=It->Status->State.CoffeeLeft;
            for(int32 Layer=0;Layer<Layers;++Layer) It->Status->CareContact(true,nullptr);
        }
    }
    void FinishPortion()
    {
        const int32 Target=Loop->MealPortionTarget;
        for(int32 Attempt=0;Attempt<Target*2 && Loop->Stage==EMCSingleDayStage::FirstMeal;++Attempt) {
            Loop->Tick(.1f);
            for(auto* Food:PatternFood()) Food->Destroy();
            ClearMealDirt();
            AdvanceClockTo(float(World->GetTimeSeconds())+5.01f);
        }
    }
    void FinishPattern()
    {
        RestoreTongueCollision();
        FinishPortion();
        if(auto* Wave=PatternWave()) {
            AdvanceClockTo(float(World->GetTimeSeconds())+Wave->Seconds+2.f);
            Wave->Tick(.1f);
            Loop->Tick(.1f);
        }
        ClearMealDirt();Loop->Tick(.1f);
        FinishPortion();
    }
    void AddFogTarget()
    {
        const FTransform Placement(FRotator::ZeroRotator,FVector(220,0,28));
        auto* Target=World->SpawnActorDeferred<AMCArenaTooth>(AMCArenaTooth::StaticClass(),Placement);
        FMCArenaToothSettings Defaults;Defaults.InitialCalculusEveryNthTooth=0;
        Target->Initialize(1,Defaults);Target->FinishSpawning(Placement);
        FreezeActors();
    }
    void AdvanceClockTo(float Seconds)
    {
        // TimeOnly advances the engine clock without executing pawn/hazard ticks
        // or stepping physics. Raising this fixture's clamp avoids thousands of
        // artificial frames while testing the removed 30s/day/run deadlines.
        World->GetWorldSettings()->MaxUndilatedFrameTime=3600.f;
        ++GFrameCounter;World->Tick(LEVELTICK_TimeOnly,FMath::Max(.01f,Seconds-float(World->GetTimeSeconds())));
    }
    void AddWorkerAndFoodGeometry(float TongueScale=10.f,bool SolidFood=false)
    {
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
#if WITH_EDITOR
        if (Mesh) FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
        const FTransform Floor(FRotator::ZeroRotator,FVector(0,0,-50),FVector(TongueScale,TongueScale,1));
        auto* Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Floor);
        Tongue->SourceMesh=Mesh;Tongue->bAutomaticYawns=false;Tongue->FinishSpawning(Floor);
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Hero=World->SpawnActor<AMCToothCharacter>(Mode->DefaultPawnClass,FVector(0,0,150),FRotator::ZeroRotator,Params);
        auto* Controller=World->SpawnActor<APlayerController>();Controller->Possess(Hero);
        auto* Menu=NewObject<UDataTable>(World);Menu->RowStruct=FMCFoodRow::StaticStruct();
        auto* FoodMesh=SolidFood?LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")):Mesh;
#if WITH_EDITOR
        if(FoodMesh) FStaticMeshCompilingManager::Get().FinishCompilation({FoodMesh});
#endif
        FMCFoodRow Food;Food.Health=10;Food.Fragments=1;Food.Resistance=EMCFoodResistance::Soft;
        Food.WholeMeshes={FoodMesh};Food.FragmentMeshes={FoodMesh};
        Menu->AddRow(TEXT("SequenceFood"),Food);Plan->Menu=Menu;
        auto* Cola=NewObject<UMCColdColaProfile>(World);Cola->ColdSeconds=35;Cola->ThawSeconds=5;Plan->ColdColaProfile=Cola;
        FreezeActors();
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDayOpeningMealGateTest,"MessControl.SingleDay.Sequence.OpeningMealExplainsItsActualRemainingWork",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDayOpeningMealGateTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F;F.AddWorkerAndFoodGeometry(30.f,true);
    F.Sequence->OpeningMealPortions={1};
    F.Sequence->AfterTrainingPauseSeconds=0;
    F.Loop->Initialize(F.Plan,F.Sequence,F.Support);
    F.RestoreTongueCollision();F.Loop->BeginFirstPerk();
    F.Loop->Tick(.1f);F.Loop->Tick(.1f);F.Loop->Tick(.1f);
    if(!TestEqual(TEXT("The real sequence reaches its opening meal"),F.Loop->Stage,EMCSingleDayStage::FirstMeal)) return false;
    AMCFoodActor* Whole=nullptr;
    for(TActorIterator<AMCFoodActor> It(F.World);It;++It) if(It->GetOwner()==F.Loop) {Whole=*It;break;}
    if(!TestNotNull(TEXT("The real serving path issued its ordinary ingredient"),Whole)) return false;
    const int32 Batch=Whole->Batch;
    TestEqual(TEXT("Serving also commits a real cleanup task"),MCCountDirectedFoodDirt(F.World,Batch),1);
    TestTrue(TEXT("The replicated instruction tells players to deliver fragments"),F.State->DirectorState.Instruction.Contains(TEXT("Q")));
    Whole->Destroy();
    auto* Fragment=F.World->SpawnActor<AMCFoodActor>();
    Fragment->Batch=Batch;Fragment->bFragment=true;Fragment->Phase=EMCFoodPhase::Carried;
    for(TActorIterator<AMCMouthSurface> It(F.World);It;++It) if(It->Batch==Batch) It->ClearLiquidForBooster();
    F.Loop->Tick(.1f);
    TestEqual(TEXT("A clean floor cannot skip a carried fragment"),F.Loop->Stage,EMCSingleDayStage::FirstMeal);
    TestEqual(TEXT("A carried fragment remains one real task"),F.State->TasksLeft,1);
    TestTrue(TEXT("Clients can see the outstanding carried piece"),F.State->DirectorState.Instruction.Contains(TEXT("фрагменты 1 (в руках 1)")));
    Fragment->Destroy();
    auto* Residue=F.World->SpawnActor<AMCMouthSurface>();
    Residue->Batch=Batch;Residue->Tags.Add(FName(TEXT("MCFoodServedDirt")));Residue->Status->ApplyCoffee(.5f);
    F.Loop->Tick(.1f);
    TestEqual(TEXT("Real remaining residue also keeps the meal open"),F.Loop->Stage,EMCSingleDayStage::FirstMeal);
    TestTrue(TEXT("The instruction names the remaining stain"),F.State->DirectorState.Instruction.Contains(TEXT("пятна 1")));
    Residue->ClearLiquidForBooster();
    AMCToothCharacter* Hero=nullptr;for(TActorIterator<AMCToothCharacter> It(F.World);It;++It) {Hero=*It;break;}
    if(!TestNotNull(TEXT("The fixture has a real player status"),Hero)) return false;
    Hero->Status->ApplyCoffee(1.f);
    F.Loop->Tick(.1f);
    TestFalse(TEXT("Resolving a portion cannot request a key-event chest"),F.Loop->bStageRewardPending);
    TestEqual(TEXT("Resolving the batch advances despite unrelated global player dirt"),F.Loop->Stage,EMCSingleDayStage::MealCoffeeWave);
    TestTrue(TEXT("The unrelated player stain is preserved"),Hero->Status->State.CoffeeLeft>0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDayOpeningMealArenaTeethTest,"MessControl.SingleDay.Sequence.OpeningMealWaitsForAvailableDirtyArenaTeeth",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDayOpeningMealArenaTeethTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F;F.AddWorkerAndFoodGeometry(30.f,true);
    F.Sequence->OpeningMealPortions={1};
    F.Sequence->AfterTrainingPauseSeconds=0;
    F.Loop->Initialize(F.Plan,F.Sequence,F.Support);
    F.RestoreTongueCollision();F.Loop->BeginFirstPerk();
    F.Loop->Tick(.1f);F.Loop->Tick(.1f);F.Loop->Tick(.1f);
    if(!TestEqual(TEXT("The actual serving sequence reaches FirstMeal"),F.Loop->Stage,EMCSingleDayStage::FirstMeal)) return false;
    AMCFoodActor* Served=nullptr;
    for(TActorIterator<AMCFoodActor> It(F.World);It;++It) if(It->GetOwner()==F.Loop) {Served=*It;break;}
    if(!TestNotNull(TEXT("The ordinary meal was really issued"),Served)) return false;
    const int32 Batch=Served->Batch;
    Served->Destroy();
    for(TActorIterator<AMCMouthSurface> It(F.World);It;++It) if(It->Batch==Batch) It->ClearLiquidForBooster();
    auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
    if(Mesh) FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
    if(!TestNotNull(TEXT("Arena tooth fixture mesh is available"),Mesh)) return false;
    auto AddDirtyTooth=[&](int32 Id) {
        const FTransform Pose(FVector(900,Id*100,100));
        auto* Tooth=F.World->SpawnActorDeferred<AMCArenaTooth>(AMCArenaTooth::StaticClass(),Pose);
        FMCArenaToothSettings Settings;Settings.InitialCalculusEveryNthTooth=0;
        Tooth->SetAppearance(Mesh,FVector(1));Tooth->Initialize(Id,Settings);Tooth->FinishSpawning(Pose);
        Tooth->SetActorTickEnabled(false);Tooth->SetActorEnableCollision(false);
        F.State->ArenaTeeth.Add(Tooth);Tooth->SetCoffee(1.f);
        return Tooth;
    };
    auto* Available=AddDirtyTooth(12);
    auto* Lost=AddDirtyTooth(13);
    auto* Consumed=AddDirtyTooth(14);
    if(!TestTrue(TEXT("A destroyed tooth enters the ordinary unavailable state"),Lost->ReceiveArenaHit(1000,FVector::ForwardVector))) return false;
    if(!TestTrue(TEXT("A respawn-consumed tooth is unavailable despite retained care data"),Consumed->ConsumeForRespawn())) return false;
    F.Loop->Tick(.1f);
    TestEqual(TEXT("Clean food and floor cannot bypass a dirty available arena tooth"),F.Loop->Stage,EMCSingleDayStage::FirstMeal);
    TestEqual(TEXT("Only the one available dirty tooth remains as work"),F.State->TasksLeft,1);
    TestTrue(TEXT("The replicated instruction explains the remaining dirty tooth"),F.State->DirectorState.Instruction.Contains(TEXT("грязные зубы 1")));
    TestTrue(TEXT("The Director journal explains the same tooth gate"),F.State->DirectorState.LastDecision.Contains(TEXT("грязные зубы 1")));
    TestTrue(TEXT("The consumed tooth still has dirt, so availability must exclude it"),Consumed->Status->State.CoffeeLeft>0);
    const int32 Contacts=Available->Status->State.CoffeeLeft;
    for(int32 Contact=0;Contact<Contacts;++Contact)
        if(!TestTrue(TEXT("The normal care API cleans a real tooth layer"),Available->Status->CareContact(true,nullptr))) return false;
    TestFalse(TEXT("The remaining tooth now needs no brushing"),Available->Status->NeedsCare(true));
    F.Loop->Tick(.1f);
    TestFalse(TEXT("Cleaning the available tooth does not request a chest"),F.Loop->bStageRewardPending);
    TestEqual(TEXT("Cleaning that actual available tooth releases the meal-to-coffee barrier"),F.Loop->Stage,EMCSingleDayStage::MealCoffeeWave);
    TestTrue(TEXT("The successful handoff preserves dirt on the unavailable consumed tooth"),Consumed->Status->State.CoffeeLeft>0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDayOpeningRelaxTest,"MessControl.SingleDay.Sequence.PatternWaitsForWaveAndCleanupBeforeSecondMeal",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDayOpeningRelaxTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F;F.AddWorkerAndFoodGeometry(30.f,true);
    F.Sequence->OpeningMealPortions={1,8};
    F.Sequence->AfterTrainingPauseSeconds=0;
    F.Sequence->OpeningMealDropSeconds=F.Sequence->AfterNutMealDropSeconds=.5f;
    auto* SavedCoffee=NewObject<UMCCoffeeProfile>(F.World);
    SavedCoffee->Settings.Cycles=4;F.Plan->CoffeeProfile=SavedCoffee;
    F.Loop->Initialize(F.Plan,F.Sequence,F.Support);
    F.RestoreTongueCollision();F.Loop->BeginFirstPerk();
    F.Loop->Tick(.1f);F.Loop->Tick(.1f);
    if(!TestEqual(TEXT("The sequence begins with food"),F.Loop->Stage,EMCSingleDayStage::FirstMeal)) return false;
    TestEqual(TEXT("The Pattern publishes two portions"),F.Loop->MealPortionCount,2);
    TestNull(TEXT("No coffee appears during the first portion"),F.PatternWave());
    const auto FirstFoods=F.PatternFood();
    if(!TestEqual(TEXT("The first portion uses the first authored count"),FirstFoods.Num(),1)) return false;
    const int32 Batch=FirstFoods[0]->Batch;
    FirstFoods[0]->Destroy();
    F.ClearMealDirt();F.Loop->Tick(.1f);
    TestFalse(TEXT("The first cleanup cannot start a chest"),F.Loop->bStageRewardPending);
    if(!TestEqual(TEXT("The first cleanup starts the coffee wave"),F.Loop->Stage,EMCSingleDayStage::MealCoffeeWave)) return false;
    auto* Wave=F.PatternWave();
    if(!TestNotNull(TEXT("The sequence starts an actual owned coffee actor"),Wave)) return false;
    TestTrue(TEXT("The owned drink is an ordinary river wave"),Wave->bRiverFlood && !Wave->IsCataclysmStorm());
    TestEqual(TEXT("The real wave has one cycle"),Wave->Waves,1);
    TestEqual(TEXT("The replicated water settings contain one cycle"),Wave->WaterSettings.Cycles,1);
    TestEqual(TEXT("The authored coffee profile is unchanged"),SavedCoffee->Settings.Cycles,4);
    TestTrue(TEXT("Client visuals retain the authored profile"),Wave->Profile==SavedCoffee);
    TestEqual(TEXT("The coffee wave is part of the same Pattern"),Wave->ResidueBatch,Batch);

    F.AdvanceClockTo(float(F.World->GetTimeSeconds())+Wave->Seconds+2.f);F.Loop->Tick(.1f);
    TestEqual(TEXT("Elapsed time alone cannot finish an active wave"),F.Loop->Stage,EMCSingleDayStage::MealCoffeeWave);
    TestTrue(TEXT("No second meal appears during the active wave"),F.PatternFood().IsEmpty());
    Wave->Tick(.1f);
    if(!TestFalse(TEXT("The actual wave update reaches its terminal state"),Wave->IsActive())) return false;
    // Keep one deterministic cleanup task even if this synthetic floor has no
    // valid sampled residue point for the wave profile.
    auto* Residue=F.World->SpawnActor<AMCMouthSurface>();
    Residue->Batch=Batch;Residue->Tags.Add(TEXT("MCFoodServedDirt"));Residue->Status->ApplyCoffee(.5f);
    F.Loop->Tick(.1f);
    TestEqual(TEXT("The completed wave starts coffee cleanup"),F.Loop->Stage,EMCSingleDayStage::BetweenMealsPause);
    TestEqual(TEXT("Coffee cleanup has no deadline"),F.Loop->StageEndsAt,0.);
    TestNull(TEXT("The finished wave actor is removed"),F.PatternWave());
    F.AdvanceClockTo(float(F.World->GetTimeSeconds())+60.f);F.Loop->Tick(.1f);
    TestEqual(TEXT("Coffee residue still blocks progression after time passes"),F.Loop->Stage,EMCSingleDayStage::BetweenMealsPause);
    TestTrue(TEXT("The dirty arena has received no second portion"),F.PatternFood().IsEmpty());
    TestFalse(TEXT("Coffee cleanup cannot request a chest"),F.Loop->bStageRewardPending);
    F.ClearMealDirt();F.Loop->Tick(.1f);
    if(!TestEqual(TEXT("Clearing coffee residue starts the second meal"),F.Loop->Stage,EMCSingleDayStage::FirstMeal)) return false;
    TestEqual(TEXT("The second portion has the correct one-based number"),F.Loop->MealPortionNumber,2);
    if(!TestEqual(TEXT("The second portion serves the native ten-item target"),F.Loop->MealPortionTarget,10)) return false;

    int32 Issued=0;
    for(int32 Attempt=0;Attempt<20 && Issued<10;++Attempt) {
        F.AdvanceClockTo(float(F.World->GetTimeSeconds())+1.01f);F.Loop->Tick(.1f);
        for(auto* Food:F.PatternFood()) {++Issued;Food->Destroy();}
    }
    TestEqual(TEXT("The real serving path issues exactly ten team ingredients"),Issued,10);
    F.Loop->Tick(.1f);
    TestEqual(TEXT("Actual food residue blocks the key-event handoff"),F.Loop->Stage,EMCSingleDayStage::FirstMeal);
    TestNull(TEXT("The key event does not overlap the second meal"),F.Loop->NutEvent.Get());
    TestNull(TEXT("The second portion does not start another coffee wave"),F.PatternWave());
    TestFalse(TEXT("The second portion does not request a chest"),F.Loop->bStageRewardPending);

    F.ClearMealDirt();
    auto* OutsideBatch=F.World->SpawnActor<AMCMouthSurface>();
    OutsideBatch->Batch=Batch+100;OutsideBatch->Status->ApplyCoffee(.5f);
    F.Loop->Tick(.1f);
    TestEqual(TEXT("The final gate includes real floor dirt outside the current batch"),F.Loop->Stage,EMCSingleDayStage::FirstMeal);
    OutsideBatch->ClearLiquidForBooster();F.Loop->Tick(.1f);
    TestEqual(TEXT("A clean second meal starts the current key event immediately"),F.Loop->Stage,EMCSingleDayStage::Nuts);
    TestNotNull(TEXT("The sequence dispatched the real nut event"),F.Loop->NutEvent.Get());
    TestFalse(TEXT("There is no chest before the key event completes"),F.Loop->bStageRewardPending);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDayOpeningFiveTest,"MessControl.SingleDay.Sequence.OpeningPortionServesExactlyFiveBeforeCoffee",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDayOpeningFiveTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F;F.AddWorkerAndFoodGeometry(30.f,true);
    F.Sequence->AfterTrainingPauseSeconds=0;
    F.Loop->Initialize(F.Plan,F.Sequence,F.Support);
    F.RestoreTongueCollision();F.Loop->BeginFirstPerk();F.Loop->Tick(.1f);
    TArray<AMCFoodActor*> Served;
    for(int32 Index=0;Index<5;++Index) {
        F.Loop->Tick(.1f);
        for(auto* Food:F.PatternFood()) if(!Served.Contains(Food)) {
            Served.Add(Food);Food->SetActorTickEnabled(false);Food->SetActorEnableCollision(false);
        }
        F.AdvanceClockTo(float(F.World->GetTimeSeconds())+F.Sequence->OpeningMealDropSeconds+.01f);
    }
    if(!TestEqual(TEXT("The native first portion issues exactly five whole items"),Served.Num(),5)) return false;
    const int32 Batch=Served[0]->Batch;
    F.Loop->Tick(.1f);
    TestEqual(TEXT("No timer can finish a portion with live food"),F.Loop->Stage,EMCSingleDayStage::FirstMeal);
    TestNull(TEXT("No wave overlaps live food"),F.PatternWave());
    for(auto* Food:Served) Food->Destroy();
    F.Loop->Tick(.1f);
    TestEqual(TEXT("Disposal alone cannot skip floor cleanup"),F.Loop->Stage,EMCSingleDayStage::FirstMeal);
    F.ClearMealDirt();F.Loop->Tick(.1f);
    TestFalse(TEXT("The clean first portion creates no reward"),F.Loop->bStageRewardPending);
    TestEqual(TEXT("Cleaning the five-item portion starts coffee"),F.Loop->Stage,EMCSingleDayStage::MealCoffeeWave);
    auto* Wave=F.PatternWave();
    if(!TestNotNull(TEXT("The first cleanup started an owned wave"),Wave)) return false;
    auto* OwnedFood=F.World->SpawnActor<AMCFoodActor>();OwnedFood->Batch=Batch;
    auto* OwnedDirt=F.World->SpawnActor<AMCMouthSurface>();OwnedDirt->Batch=Batch;OwnedDirt->Tags.Add(TEXT("MCFoodServedDirt"));
    auto* UnrelatedFood=F.World->SpawnActor<AMCFoodActor>();UnrelatedFood->Batch=Batch+1;
    auto* UnrelatedDirt=F.World->SpawnActor<AMCMouthSurface>();UnrelatedDirt->Batch=Batch+1;UnrelatedDirt->Tags.Add(TEXT("MCFoodServedDirt"));
    F.Loop->Stop();
    TestTrue(TEXT("Stopping the Pattern destroys its wave"),Wave->IsActorBeingDestroyed());
    TestTrue(TEXT("Stopping the Pattern destroys its remaining food"),OwnedFood->IsActorBeingDestroyed());
    TestTrue(TEXT("Stopping the Pattern destroys its remaining stains"),OwnedDirt->IsActorBeingDestroyed());
    TestFalse(TEXT("Stopping the Pattern preserves unrelated food"),UnrelatedFood->IsActorBeingDestroyed());
    TestFalse(TEXT("Stopping the Pattern preserves unrelated stains"),UnrelatedDirt->IsActorBeingDestroyed());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDayFinalRewardTest,"MessControl.SingleDay.Sequence.FinalFogRewardFinishesDayOne",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDayFinalRewardTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F;
    TestFalse(TEXT("The profile does not use the timed legacy finale"),F.Loop->IsLegacyTimedFinale());
    F.CompleteFogSlot();
    TestTrue(TEXT("The last key event opens its shared reward"),F.Loop->bStageRewardPending);
    TestEqual(TEXT("The last key event has its own reward identity"),F.Loop->StageRewardCompletionId,FName(TEXT("KeyEvent_2_FogBrawl")));
    TestNull(TEXT("Completed fog is removed before its reward"),F.Loop->FogEvent.Get());
    TestEqual(TEXT("The run continues while the last choice is outstanding"),F.State->Phase,EMCShiftPhase::Working);
    TestFalse(TEXT("An unresolved final chest cannot finish the authored run"),F.Loop->bAuthoredFragmentComplete);
    F.ResolveStageReward();
    TestFalse(TEXT("Claiming the reward releases the reward barrier"),F.Loop->bStageRewardPending);
    TestEqual(TEXT("The last shared reward completes the sequence"),F.Loop->Stage,EMCSingleDayStage::Complete);
    TestTrue(TEXT("The final claimed reward marks the authored run complete"),F.Loop->bAuthoredFragmentComplete);
    TestEqual(TEXT("The completed day is a win"),F.State->Phase,EMCShiftPhase::Won);
    TestNull(TEXT("Day completion does not start adaptive support"),F.Mode->GameDirector.Get());
    TestNull(TEXT("Day completion does not start the legacy boss"),F.Loop->FinalBoss.Get());
    TestEqual(TEXT("Day completion publishes no countdown"),F.State->PhaseEndsAt,0.);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDayNoDeadlineTest,"MessControl.SingleDay.Sequence.SupportSurvivesOldFinaleDayAndRunTargets",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDayNoDeadlineTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F;
    auto* Director=F.World->SpawnActor<AMCGameDirector>();F.Mode->GameDirector=Director;
    Director->InitializeRun(F.Plan,F.Support);Director->BeginSupport();F.FreezeActors();
    for(const float Seconds:{31.f,361.f,2101.f}) {
        F.AdvanceClockTo(Seconds);
        TestTrue(TEXT("The fixture advanced actual server world time beyond the target"),F.State->GetServerWorldTimeSeconds()>=Seconds-.01f);
        Director->Tick(.25f);
        TestEqual(TEXT("Old finale and day targets cannot end untimed support"),F.State->Phase,EMCShiftPhase::Working);
        TestTrue(TEXT("Support remains managed after every old deadline"),Director->IsManagingEvents());
        TestEqual(TEXT("No expired clock is published"),F.State->DirectorState.DayEndAt,0.);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDayIceDispatchTest,"MessControl.SingleDay.Sequence.NutRewardThenPatternDispatchesIce",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDayIceDispatchTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F;F.AddWorkerAndFoodGeometry(30.f,true);
    F.CompleteNutSlot();
    TestTrue(TEXT("The nut completion requests one key-event reward"),F.Loop->bStageRewardPending);
    TestEqual(TEXT("Nut completion uses its slot identity"),F.Loop->StageRewardCompletionId,FName(TEXT("KeyEvent_0_NutEncounter")));
    TestNull(TEXT("The completed nut actor is removed before the chest"),F.Loop->NutEvent.Get());
    F.AdvanceClockTo(float(F.World->GetTimeSeconds())+180.f);F.Loop->Tick(.1f);
    TestEqual(TEXT("Time cannot advance the slot before the reward is claimed"),F.Loop->KeyEventIndex,0);
    TestTrue(TEXT("No meal arrives before the reward is claimed"),F.PatternFood().IsEmpty());
    TestNull(TEXT("No ice actor exists before the following Pattern"),F.Loop->IceEvent.Get());
    F.ResolveStageReward();
    TestEqual(TEXT("Claiming the reward advances one slot"),F.Loop->KeyEventIndex,1);
    TestEqual(TEXT("The next slot begins with the first portion"),F.Loop->Stage,EMCSingleDayStage::FirstMeal);
    TestFalse(TEXT("Completing nuts leaves the next slot outstanding"),F.Loop->bAuthoredFragmentComplete);
    TestNull(TEXT("No adaptive support is started between key events"),F.Mode->GameDirector.Get());
    F.FinishPattern();
    TestEqual(TEXT("The completed Pattern dispatches the second key-event kind"),F.Loop->Stage,EMCSingleDayStage::Ice);
    TestEqual(TEXT("Dispatch preserves the same slot index"),F.Loop->KeyEventIndex,1);
    if(!TestNotNull(TEXT("Ice dispatch creates the real event"),F.Loop->IceEvent.Get())) return false;
    TestFalse(TEXT("Ice starts with the fixture's valid tongue"),F.Loop->IceEvent->bFailed);
    TestTrue(TEXT("The HUD publishes ice completion progress"),F.State->TasksTotal>0 && F.State->TasksLeft>0);
    F.Loop->Stop();
    TestNull(TEXT("Stopping the sequence removes the ice actor"),F.Loop->IceEvent.Get());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDaySavedNutCompatibilityTest,"MessControl.SingleDay.Sequence.SavedNutOnlyProfileAppendsIceAndFogWithoutChangingSavedSettings",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDaySavedNutCompatibilityTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F(1);
    const auto& RuntimeSlots=F.Loop->GetRunProfile()->KeyEvents;
    if(!TestEqual(TEXT("The runtime sequence expands to three slots"),RuntimeSlots.Num(),3)) return false;
    TestEqual(TEXT("The runtime slot preserves the authored identity"),RuntimeSlots[0].EventId,FName(TEXT("AuthoredNut")));
    TestEqual(TEXT("The runtime copy preserves the authored support setting"),RuntimeSlots[0].DirectorSupportSeconds,9.f);
    TestEqual(TEXT("The first added key event is Ice"),RuntimeSlots[1].Kind,EMCSingleDayKeyEventKind::IceEvent);
    TestEqual(TEXT("The second added key event is Fog"),RuntimeSlots[2].Kind,EMCSingleDayKeyEventKind::FogBrawl);
    TestEqual(TEXT("Compatibility leaves the saved profile at one slot"),F.Sequence->KeyEvents.Num(),1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDayFogDispatchTest,"MessControl.SingleDay.Sequence.IceRewardThenPatternDispatchesFog",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDayFogDispatchTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F;F.AddWorkerAndFoodGeometry(30.f,true);F.AddFogTarget();
    F.CompleteIceSlot();
    TestFalse(TEXT("Completing ice leaves the fog slot outstanding"),F.Loop->bAuthoredFragmentComplete);
    TestNull(TEXT("Ice is removed before its reward so the arena can thaw"),F.Loop->IceEvent.Get());
    TestTrue(TEXT("Ice requests one shared key-event reward"),F.Loop->bStageRewardPending);
    TestEqual(TEXT("Ice uses its own slot identity"),F.Loop->StageRewardCompletionId,FName(TEXT("KeyEvent_1_IceEvent")));
    F.ResolveStageReward();
    TestEqual(TEXT("The reward advances to the fog slot's first meal"),F.Loop->Stage,EMCSingleDayStage::FirstMeal);
    TestNull(TEXT("Fog does not overlap the Pattern"),F.Loop->FogEvent.Get());
    F.FinishPattern();
    TestEqual(TEXT("The completed Pattern dispatches fog"),F.Loop->Stage,EMCSingleDayStage::FogBrawl);
    TestEqual(TEXT("Fog is exactly the third key-event slot"),F.Loop->KeyEventIndex,2);
    if(!TestNotNull(TEXT("Dispatch creates the real fog event"),F.Loop->FogEvent.Get())) return false;
    TestFalse(TEXT("Fog starts with the fixture tongue and controlled worker"),F.Loop->FogEvent->bFailed);
    TestNull(TEXT("The sequence has no ordinary support actor"),F.Mode->GameDirector.Get());
    TestEqual(TEXT("The fog HUD starts with all strikes outstanding"),F.State->TasksLeft,F.Loop->FogEvent->TotalStrikes);
    F.AdvanceClockTo(float(F.Loop->FogEvent->StageEndsAt)+.2f);F.Loop->FogEvent->Tick(.1f);
    TestEqual(TEXT("Smoke-in starts the separate all-teeth selection"),F.Loop->FogEvent->Stage,EMCFogBrawlStage::Selection);
    TestNull(TEXT("The selection does not announce a target prematurely"),F.Loop->FogEvent->ActiveTarget.Get());
    F.AdvanceClockTo(float(F.Loop->FogEvent->StageEndsAt)+.2f);F.Loop->FogEvent->Tick(.1f);
    TestEqual(TEXT("The actual fog actor enters its first warning"),F.Loop->FogEvent->Stage,EMCFogBrawlStage::Warning);
    if(!TestNotNull(TEXT("The warning selects an existing reachable reserve tooth"),F.Loop->FogEvent->ActiveTarget.Get())) return false;
    F.Loop->FogEvent->StrikesResolved=2;F.Loop->Tick(.1f);
    TestEqual(TEXT("The HUD counts unresolved strikes"),F.State->TasksLeft,F.Loop->FogEvent->TotalStrikes-2);
    TestEqual(TEXT("The HUD retains the total strike count"),F.State->TasksTotal,F.Loop->FogEvent->TotalStrikes);
    TestTrue(TEXT("Resolving strikes advances the published completion progress"),F.State->DirectorState.CompletionProgress>0);
    auto* Fog=F.Loop->FogEvent.Get();F.Loop->Stop();
    TestNull(TEXT("Stopping the sequence removes fog"),F.Loop->FogEvent.Get());
    TestTrue(TEXT("Stopping destroys the actual fog actor"),Fog->IsActorBeingDestroyed());
    F.AdvanceClockTo(float(F.World->GetTimeSeconds())+600.f);F.Loop->Tick(.1f);
    TestNull(TEXT("A stopped sequence cannot start another fog event"),F.Loop->FogEvent.Get());
    TestNull(TEXT("A stopped sequence cannot start ordinary support"),F.Mode->GameDirector.Get());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDaySavedIceCompatibilityTest,"MessControl.SingleDay.Sequence.SavedNutIceProfileAppendsFogWithoutChangingSavedSettings",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDaySavedIceCompatibilityTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F(2);
    const auto& RuntimeSlots=F.Loop->GetRunProfile()->KeyEvents;
    if(!TestEqual(TEXT("The runtime sequence gains a third slot"),RuntimeSlots.Num(),3)) return false;
    TestEqual(TEXT("Runtime compatibility preserves the authored ice identity"),RuntimeSlots[1].EventId,FName(TEXT("AuthoredIce")));
    TestEqual(TEXT("Runtime compatibility preserves the authored support setting"),RuntimeSlots[1].DirectorSupportSeconds,17.f);
    TestEqual(TEXT("The added key event is fog"),RuntimeSlots[2].Kind,EMCSingleDayKeyEventKind::FogBrawl);
    TestEqual(TEXT("Compatibility leaves the saved profile at two slots"),F.Sequence->KeyEvents.Num(),2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDayFailedFogTest,"MessControl.SingleDay.Sequence.FailedFogSetupTerminatesAndCleansEvent",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDayFailedFogTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F;
    F.Loop->KeyEventIndex=2;F.Loop->Stage=EMCSingleDayStage::FogBrawl;
    F.Loop->FogEvent=F.World->SpawnActor<AMCFogBrawlEvent>();
    F.Loop->FogEvent->Stage=EMCFogBrawlStage::Complete;F.Loop->FogEvent->bFailed=true;
    AddExpectedError(TEXT("MC_SINGLE_DAY FAILED:"),EAutomationExpectedErrorFlags::Contains,1);
    F.Loop->Tick(.1f);
    TestEqual(TEXT("Fog failure takes precedence over a terminal event signal"),F.State->Phase,EMCShiftPhase::Lost);
    TestNull(TEXT("Failure removes the fog actor"),F.Loop->FogEvent.Get());
    TestNull(TEXT("Failure cannot start ordinary support"),F.Mode->GameDirector.Get());
    TestFalse(TEXT("Failure never marks the authored fragment complete"),F.Loop->bAuthoredFragmentComplete);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDayFailedIceTest,"MessControl.SingleDay.Sequence.FailedIceSetupTerminatesAndCleansEvent",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDayFailedIceTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F;
    F.Loop->KeyEventIndex=1;F.Loop->Stage=EMCSingleDayStage::Ice;
    F.Loop->IceEvent=F.World->SpawnActor<AMCIceEvent>();
    F.Loop->IceEvent->Stage=EMCIceEventStage::Complete;F.Loop->IceEvent->bFailed=true;
    AddExpectedError(TEXT("MC_SINGLE_DAY FAILED:"),EAutomationExpectedErrorFlags::Contains,1);
    F.Loop->Tick(.1f);
    TestEqual(TEXT("A failed ice setup ends the run instead of advancing"),F.State->Phase,EMCShiftPhase::Lost);
    TestNull(TEXT("Failure cleans up the ice actor"),F.Loop->IceEvent.Get());
    TestNull(TEXT("Failure cannot start ordinary support"),F.Mode->GameDirector.Get());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDaySupportPoolTest,"MessControl.SingleDay.Sequence.SupportAdmitsFoodColaAndRepairWithoutDayOrDeadlineGates",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDaySupportPoolTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F;F.AddWorkerAndFoodGeometry();
    for (auto& Day:F.Support->Days) {
        Day.PressureLimit=3;Day.TargetPressureMin=1;Day.TargetPressureMax=2;
        Day.FoodWorkPerPlayer=120;Day.MaxWholeFood=4;Day.MaxFragments=24;
    }
    for (auto& Rule:F.Support->Events) {
        if (Rule.Kind==EMCGameDirectorEvent::Food || Rule.Kind==EMCGameDirectorEvent::ColdCola || Rule.Kind==EMCGameDirectorEvent::LooseTooth) {
            Rule.Weight=1;Rule.FirstDay=7;Rule.MinimumDifficulty=0;
        }
    }
    auto* Director=F.World->SpawnActor<AMCGameDirector>();F.Mode->GameDirector=Director;
    Director->InitializeRun(F.Plan,F.Support);Director->BeginSupport();F.FreezeActors();F.AdvanceClockTo(2);
    TestEqual(TEXT("This is really day one, below the authored event gates"),F.State->Day,1);
    TestEqual(TEXT("The support snapshot has no deadline"),Director->Observe().DayEndAt,0.);
    const auto Seen=Director->Observe();
    if (!TestTrue(TEXT("The candidate fixture has a living available worker and food surface"),Seen.AvailablePlayers>0 && !Seen.bUrgent && !Seen.bGlobalMovement)) return false;
    const auto Candidates=Director->EvaluateCandidates(Seen,F.State->GetServerWorldTimeSeconds());
    for (const auto Kind:{EMCGameDirectorEvent::Food,EMCGameDirectorEvent::ColdCola,EMCGameDirectorEvent::LooseTooth}) {
        const auto* Candidate=Candidates.FindByPredicate([Kind](const auto& Value){return Value.Kind==Kind;});
        if (!TestNotNull(TEXT("Every ordinary event remains in the runtime candidate pool"),Candidate)) return false;
        TestTrue(FString::Printf(TEXT("Ordinary kind %d can be chosen on day one: %s"),int32(Kind),*Candidate->BlockReason),
            Candidate->BlockReason.IsEmpty() && Candidate->EffectiveWeight>0 && Candidate->Probability>0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSingleDaySharedHistoryTest,"MessControl.SingleDay.Sequence.SharedHistoryIsBoundedAndSurvivesKeyEventPublish",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSingleDaySharedHistoryTest::RunTest(const FString&)
{
    MCSingleDaySequenceTestsPrivate::FSequenceWorld F;
    F.State->DirectorDecisionLog.Reset();
    for (int32 Index=0;Index<140;++Index) F.State->RecordDirectorDecision(FString::Printf(TEXT("SequenceHistory%03d"),Index));
    if (!TestEqual(TEXT("Run history keeps exactly its last 128 decisions"),F.State->DirectorDecisionLog.Num(),128)) return false;
    TestEqual(TEXT("Bounded history evicts only the oldest decisions"),F.State->DirectorDecisionLog[0],FString(TEXT("SequenceHistory012")));
    const auto Before=F.State->DirectorDecisionLog;
    F.Loop->Stage=EMCSingleDayStage::Nuts;F.Loop->NutEvent=F.World->SpawnActor<AMCNutRainEvent>();
    F.Loop->NutEvent->SetActorTickEnabled(false);F.Loop->NutEvent->Stage=EMCNutRainStage::Enemies;F.Loop->NutEvent->EnemiesLeft=3;
    F.Loop->Tick(.1f);
    TestTrue(TEXT("Replacing the key-event HUD card preserves every retained decision"),F.State->DirectorDecisionLog==Before);
    TestTrue(TEXT("The current HUD snapshot still exposes shared history"),F.State->DirectorState.DecisionLog==Before);
    TestEqual(TEXT("The latest decision is not replaced by transient key-event text"),F.State->DirectorState.LastDecision,Before.Last());
    F.State->RecordDirectorDecision(TEXT("AfterKeyEvent"));
    TestEqual(TEXT("Later scheduler writes keep the same bound"),F.State->DirectorDecisionLog.Num(),128);
    TestEqual(TEXT("Later decisions append to the shared history"),F.State->DirectorDecisionLog.Last(),FString(TEXT("AfterKeyEvent")));
    return true;
}
#endif
