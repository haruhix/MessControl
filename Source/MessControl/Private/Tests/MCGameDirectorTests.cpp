#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCGameDirector.h"
#include "MCGameDirectorProfile.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCDayPlan.h"
#include "MCDayDirector.h"
#include "MCFoodActor.h"
#include "MCFoodDirectorHooks.h"
#include "MCCoffeeFlood.h"
#include "MCColdCola.h"
#include "MCMouthSurface.h"
#include "MCFirePatch.h"
#include "MCThroat.h"
#include "MCTongue.h"
#include "MCToothStatusComponent.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothMovementComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/UnrealType.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif
#include <limits>

namespace
{
UMCGameDirectorProfile* EmptyDirectorProfile(UObject* Outer)
{
    auto* Profile=NewObject<UMCGameDirectorProfile>(Outer);
    for(auto& Day:Profile->Days)
    {
        Day.InitialPatches=0;
        Day.DaySeconds=60; Day.EventGap=1; Day.RestSeconds=0; Day.FinalCleanupSeconds=10;
    }
    for(auto& Rule:Profile->Events) Rule.Weight=0;
    Profile->InitialCleaningSeconds=1;
    Profile->DecisionInterval=.25f;
    return Profile;
}

void EnableEvent(UMCGameDirectorProfile* Profile,EMCGameDirectorEvent Kind,float Weight=1000,float Cooldown=0,int32 Cap=0)
{
    for(auto& Rule:Profile->Events) if(Rule.Kind==Kind)
    {
        Rule.Weight=Weight;Rule.CooldownSeconds=Cooldown;Rule.MaxPerDay=Cap;
        Rule.FirstDay=1;Rule.MinimumDifficulty=0;return;
    }
    FMCGameDirectorEventRule Rule;Rule.Kind=Kind;Rule.Weight=Weight;Rule.CooldownSeconds=Cooldown;
    Rule.MaxPerDay=Cap;Profile->Events.Add(Rule);
}

bool FiniteTelemetry(const FMCGameDirectorState& Seen)
{
    const float Values[]={Seen.Pressure,Seen.WorkSeconds,Seen.TargetPressure,Seen.Difficulty,Seen.Throughput,
        Seen.TeamHealth,Seen.TeamStamina,Seen.Stress,Seen.DayProgress,Seen.CompletionProgress,Seen.WaitWeight,Seen.WaitProbability};
    for(const float Value:Values) if(!FMath::IsFinite(Value) || Value<0) return false;
    return Seen.TeamHealth<=1 && Seen.TeamStamina<=1 && Seen.DayProgress<=1 && Seen.CompletionProgress<=1;
}

const FMCGameDirectorCandidate* FindCandidate(const TArray<FMCGameDirectorCandidate>& Candidates,EMCGameDirectorEvent Kind)
{
    return Candidates.FindByPredicate([Kind](const FMCGameDirectorCandidate& Candidate) {return Candidate.Kind==Kind;});
}

UMCGameDirectorProfile* RuntimeChoiceProfile(UObject* Outer)
{
    auto* Profile=EmptyDirectorProfile(Outer);
    for(auto& Day:Profile->Days)
    {
        Day.DaySeconds=120;Day.BuildSeconds=120;Day.CoffeePatches=1;Day.PressureLimit=3;
        Day.TargetPressureMin=Day.TargetPressureMax=.7f;
        Day.MinimumDifficulty=Day.MaximumDifficulty=1;
    }
    return Profile;
}

struct FDirectorWorld
{
    UWorld* World=nullptr;
    AMCGameMode* Mode=nullptr;
    AMCGameState* State=nullptr;
    AMCGameDirector* Director=nullptr;
    AMCTongue* Tongue=nullptr;
    AMCThroat* Throat=nullptr;
    AMCToothCharacter* Hero=nullptr;
    FDirectorWorld()
    {
        World=UWorld::CreateWorld(EWorldType::Game,false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/MessControl.MCGameMode"));URL.AddOption(TEXT("Seed=6001"));
        World->SetGameMode(URL);
        Mode=World->GetAuthGameMode<AMCGameMode>();
        // Keep setup independent of both legacy/default production startup paths.
        Mode->bUseAdaptiveDirector=false; Mode->bUseDayOnePlan=false;
        World->InitializeActorsForPlay(URL);World->BeginPlay();
        Mode->SetActorTickEnabled(false);State=World->GetGameState<AMCGameState>();
        for(TActorIterator<AMCThroat> It(World);It;++It)
        {
            Throat=*It;Throat->SetActorEnableCollision(false);Throat->SetActorTickEnabled(false);
        }
        auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
#if WITH_EDITOR
        if(Plane) FStaticMeshCompilingManager::Get().FinishCompilation({Plane});
#endif
        const FTransform Floor(FRotator::ZeroRotator,FVector(0,0,-50),FVector(30,30,1));
        Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Floor);
        Tongue->SourceMesh=Plane;Tongue->bAutomaticYawns=false;Tongue->FinishSpawning(Floor);
        Tongue->SetActorTickEnabled(false);
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,100),FRotator::ZeroRotator);
        auto* Controller=World->SpawnActor<APlayerController>();Controller->SetAsLocalPlayerController();Controller->Possess(Hero);
        Hero->SetActorTickEnabled(false);Hero->GetCharacterMovement()->SetComponentTickEnabled(false);
        // This inert worker supplies availability, not animation/rig physics.
        Hero->GetMesh()->SetComponentTickEnabled(false);Hero->ToothPhysics->SetComponentTickEnabled(false);
        Director=World->SpawnActor<AMCGameDirector>();Mode->GameDirector=Director;
    }
    ~FDirectorWorld()
    {
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    }
    void Start(UMCGameDirectorProfile* Profile=nullptr,float FixtureDaySeconds=0,UMCDayPlan* SourcePlan=nullptr)
    {
        if(!Profile) Profile=EmptyDirectorProfile(World);
        auto* Plan=SourcePlan?SourcePlan:NewObject<UMCDayPlan>(World);
        Director->InitializeRun(Plan,Profile);
        // Only lifecycle fixtures shorten the copied active settings. Production
        // duration validation remains the separate six-to-eight-minute test.
        if(FixtureDaySeconds>0)
            for(auto& Day:Director->Settings->Days)
            {Day.DaySeconds=FixtureDaySeconds;Day.FinalCleanupSeconds=0;Day.InitialPatches=0;}
        Director->BeginDay(1);
    }
    void Clean()
    {
        for(TActorIterator<AActor> It(World);It;++It)
            if(auto* Status=It->FindComponentByClass<UMCToothStatusComponent>())
                for(int32 I=0;I<150;++I) {Status->CareContact(true);Status->CareContact(false);}
        // Fixture cleanup is deliberate: liquid wiping/tool accuracy has its own
        // gameplay tests. These cases isolate scheduler lifecycle and food debt.
        for(TActorIterator<AMCMouthSurface> It(World);It;++It)
            if(!It->bUlcer) It->Destroy();
    }
    void Step(float Seconds,float Dt=1.f/30)
    {
        for(int32 I=0;I<FMath::CeilToInt(Seconds/Dt);++I) {++GFrameCounter;World->Tick(LEVELTICK_All,Dt);}
    }
    AMCFoodActor* Food(bool Fragment=true,EMCFoodKind Kind=EMCFoodKind::Food,int32 Batch=6001)
    {
        auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
        if(Cube) FStaticMeshCompilingManager::Get().FinishCompilation({Cube});
#endif
        FMCFoodRow Row;Row.Kind=Kind;Row.WholeMeshes={Cube};Row.FragmentMeshes={Cube};
        Row.Scale=Row.FragmentScale=FVector(.25);Row.Fragments=3;Row.SpoilSeconds=600;
        const FTransform Transform(FVector(0,0,500));
        auto* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Transform,
            nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FRandomStream Random(17);Food->ConfigureItem(TEXT("DirectorFixture"),Row,Random,Fragment);
        Food->Batch=Batch;Food->FinishSpawning(Transform);Food->Body->SetSimulatePhysics(false);
        Food->Phase=EMCFoodPhase::Free;return Food;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameDirectorProfileTest,"MessControl.Director.SevenDaysAndFiniteProfileBounds",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameDirectorProfileTest::RunTest(const FString&)
{
    auto* Profile=NewObject<UMCGameDirectorProfile>();
    TestEqual(TEXT("Production progression contains seven profiles"),Profile->Days.Num(),7);
    float Previous=0;
    for(const auto& Day:Profile->Days)
    {
        TestTrue(TEXT("Production day lasts six to eight minutes"),Day.DaySeconds>=360 && Day.DaySeconds<=480);
        TestTrue(TEXT("Day duration does not decrease"),Day.DaySeconds>=Previous);Previous=Day.DaySeconds;
    }
    Profile->Days.SetNum(1);Profile->Days[0].DaySeconds=std::numeric_limits<float>::quiet_NaN();
    Profile->Days[0].InitialPatches=-2;Profile->Days[0].MaxFoodBatch=200;
    Profile->Days[0].PressureLimit=std::numeric_limits<float>::infinity();
    Profile->Days[0].TargetPressureMin=std::numeric_limits<float>::quiet_NaN();
    Profile->Days[0].MaximumDifficulty=std::numeric_limits<float>::infinity();
    Profile->Events[0].Weight=std::numeric_limits<float>::quiet_NaN();
    Profile->Events[0].CooldownSeconds=std::numeric_limits<float>::infinity();
    Profile->DecisionInterval=std::numeric_limits<float>::quiet_NaN();
    Profile->AdaptationSeconds=std::numeric_limits<float>::infinity();
    Profile->TeamScaling=std::numeric_limits<float>::quiet_NaN();Profile->Sanitize();
    TestEqual(TEXT("Incomplete designer profile restores all seven days"),Profile->Days.Num(),7);
    TestTrue(TEXT("Invalid time/pressure/team values become finite"),FMath::IsFinite(Profile->Days[0].DaySeconds)
        && FMath::IsFinite(Profile->Days[0].PressureLimit) && FMath::IsFinite(Profile->TeamScaling)
        && FMath::IsFinite(Profile->Days[0].TargetPressureMin) && FMath::IsFinite(Profile->Days[0].MaximumDifficulty)
        && FMath::IsFinite(Profile->Events[0].Weight) && FMath::IsFinite(Profile->Events[0].CooldownSeconds)
        && FMath::IsFinite(Profile->DecisionInterval) && FMath::IsFinite(Profile->AdaptationSeconds));
    TestEqual(TEXT("Initial work cannot be negative"),Profile->Days[0].InitialPatches,0);
    TestEqual(TEXT("A single food pulse stays bounded"),Profile->Days[0].MaxFoodBatch,4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameDirectorRunSnapshotTest,"MessControl.Director.RunSnapshotsSettingsAndRuntimeObservation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameDirectorRunSnapshotTest::RunTest(const FString&)
{
    FDirectorWorld T;auto* Source=EmptyDirectorProfile(T.World);
    auto* SavedPlan=T.Mode->FirstDayPlan.LoadSynchronous();
    if(!TestNotNull(TEXT("The mechanics fixture uses the saved first-day asset"),SavedPlan)) return false;
    T.Start(Source,0,SavedPlan);
    TestTrue(TEXT("Active run owns a profile copy"),T.Director->Settings && T.Director->Settings!=Source);
    TestTrue(TEXT("GameState publishes the saved plan asset instead of the private mechanics snapshot"),
        T.State->DayPlan==SavedPlan && T.State->DayPlan!=T.Director->Mechanics);
    TestTrue(TEXT("The replicated plan has a networking-stable asset path"),T.State->DayPlan
        && T.State->DayPlan->IsAsset() && T.State->DayPlan->IsFullNameStableForNetworking());
    Source->Days[0].TargetPressureMax=2;
    EnableEvent(Source,EMCGameDirectorEvent::Food);
    TestTrue(TEXT("Editing source cannot retune the active run"),T.Director->Settings->Days[0].TargetPressureMax<2);
    for(const auto& Rule:T.Director->Settings->Events)
        TestEqual(TEXT("Editing source cannot enable an event in the active copy"),Rule.Weight,0.f);
    T.Clean();
    TestEqual(TEXT("An empty runtime profile has no unselected food debt"),T.Director->PendingFoodCount(),0);
    TestFalse(TEXT("Potential future events cannot block an already clean arena"),T.Director->HasOutstandingWork());
    const auto Seen=T.Director->Observe();
    TestTrue(TEXT("The diagnostic snapshot exposes finite runtime telemetry"),FiniteTelemetry(Seen));
    TestEqual(TEXT("Food counters start at zero before a runtime selection"),Seen.PlannedFood,0);
    T.Step(.5f);
    TestTrue(TEXT("Monitor enables only the managed run"),T.State->DirectorState.bEnabled);
    TestTrue(TEXT("Recent decisions remain bounded"),T.State->DirectorState.DecisionLog.Num()<=12);
    float Probability=T.State->DirectorState.WaitProbability;
    for(const auto& Candidate:T.State->DirectorState.Candidates)
    {
        TestTrue(TEXT("Candidate diagnostic values stay finite and nonnegative"),FMath::IsFinite(Candidate.BaseWeight)
            && FMath::IsFinite(Candidate.EffectiveWeight) && FMath::IsFinite(Candidate.Probability)
            && FMath::IsFinite(Candidate.ForecastPressure) && Candidate.Probability>=0);
        Probability+=Candidate.Probability;
    }
    TestTrue(TEXT("Candidates plus explicit waiting form one probability distribution"),FMath::IsNearlyEqual(Probability,1.f,.001f));
    TestEqual(TEXT("Disabled runtime events preserve a zero effective choice weight"),T.State->DirectorState.WaitProbability,1.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameDirectorOneFoodTest,"MessControl.Director.OneFragmentAndQueuedMealBlockCompletion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameDirectorOneFoodTest::RunTest(const FString&)
{
    FDirectorWorld T;T.Start();T.Clean();auto* Food=T.Food();
    auto Seen=T.Director->Observe();
    TestEqual(TEXT("Exactly one ordinary fragment remains visible to Director"),Seen.Fragments,1);
    TestTrue(TEXT("Count one is unfinished work, not a tolerated residue"),T.Director->HasOutstandingWork());
    if(!TestNotNull(TEXT("Native throat exists"),T.Throat)) return false;
    Food->SetActorLocation(T.Throat->GetActorTransform().TransformPosition(T.Throat->ZoneCenter));
    if(!TestTrue(TEXT("Food enters the real gathering queue"),T.Throat->AcceptDelivery(Food))) return false;
    Seen=T.Director->Observe();
    TestTrue(TEXT("Reserved food is still mandatory before Dispose"),Seen.ThroatQueued>0 && !Food->IsDisposed() && T.Director->HasOutstandingWork());
    T.Throat->ResetSwallow();Food->Dispose();
    TestEqual(TEXT("Disposal removes the last fragment from observation"),T.Director->Observe().Fragments,0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameDirectorPepperIntakeTest,"MessControl.Director.AcceptedPepperCanFinishRealSwallow",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameDirectorPepperIntakeTest::RunTest(const FString&)
{
    FDirectorWorld T;T.Start();T.Clean();
    if(!TestNotNull(TEXT("Native throat exists"),T.Throat)) return false;
    auto* Pepper=T.Food(false,EMCFoodKind::Spicy);Pepper->ArmSpicy();
    Pepper->SetActorLocation(T.Throat->GetActorTransform().TransformPosition(T.Throat->ZoneCenter));
    if(!TestTrue(TEXT("Live pepper can be accepted as a real meal"),T.Throat->AcceptDelivery(Pepper))) return false;
    TestTrue(TEXT("Accepted pepper remains alive with its fuse paused"),Pepper->bFusePaused && !Pepper->IsDisposed());
    TestTrue(TEXT("Urgency cannot deadlock the swallow that resolves it"),T.Director->CanStartSwallow());
    T.Throat->SetActorTickEnabled(true);T.Step(7.5f);
    TestTrue(TEXT("The production throat consumes accepted pepper through its full lifecycle"),Pepper->IsDisposed());
    TestTrue(TEXT("No cancelled intake is left in the gathering queue"),T.Throat->QueuedFoodCount==0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameDirectorExistingGlobalTest,"MessControl.Director.ExistingYawnReservesGlobalChannel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameDirectorExistingGlobalTest::RunTest(const FString&)
{
    FDirectorWorld T;
    if(!TestTrue(TEXT("Unmanaged fixture starts a real air event"),T.Tongue->StartYawn(3))) return false;
    T.Start();T.Clean();
    TestTrue(TEXT("Director observes an already committed global movement"),T.Director->Observe().bGlobalMovement);
    TestFalse(TEXT("Swallow cannot overlap that air event"),T.Director->CanStartSwallow());
    T.Tongue->ResetYawn();
    TestTrue(TEXT("Ending the global event frees swallow channel"),T.Director->CanStartSwallow());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameDirectorSnapshotNetFlagTest,"MessControl.Director.MonitorStateIsReplicatedAsOneProperty",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameDirectorSnapshotNetFlagTest::RunTest(const FString&)
{
    const auto* Property=FindFProperty<FStructProperty>(AMCGameState::StaticClass(),TEXT("DirectorState"));
    if(!TestNotNull(TEXT("GameState exposes the Director snapshot"),Property)) return false;
    TestTrue(TEXT("Snapshot is registered as replicated state"),Property->HasAnyPropertyFlags(CPF_Net));
    TestEqual(TEXT("HUD/log state has the declared shared struct"),Property->Struct.Get(),FMCGameDirectorState::StaticStruct());
    // Actual server -> remote client transfer is checked by MCGameDirectorSmoke.
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameDirectorAutomaticCampaignTest,"MessControl.Director.AutomaticSevenDayLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameDirectorAutomaticCampaignTest::RunTest(const FString&)
{
    FDirectorWorld T;auto* Profile=EmptyDirectorProfile(T.World);Profile->IntermissionSeconds=1;
    T.Start(Profile,.75f);T.Clean();T.Mode->bUseAdaptiveDirector=true;T.Mode->SetActorTickEnabled(true);
    TestFalse(TEXT("A managed run does not use the legacy day-one completion latch"),T.State->bDayOneComplete);
    T.Step(.25f);
    TestEqual(TEXT("Cleaning an empty fixture does not shorten the authored day clock"),T.State->Day,1);
    uint32 DaysSeen=1;
    for(int32 I=0;I<250 && T.State->Phase!=EMCShiftPhase::Won && T.State->Phase!=EMCShiftPhase::Lost;++I)
    {
        T.Clean();T.Step(.1f);
        if(T.State->Day>=1 && T.State->Day<=7) DaysSeen|=1u<<(T.State->Day-1);
    }
    TestEqual(TEXT("Real GameMode intermission ticks visit all seven days without manual BeginDay calls"),DaysSeen,127u);
    TestEqual(TEXT("An empty controlled seven-day run ends on day seven"),T.State->Day,7);
    TestEqual(TEXT("The seventh completed deadline ends the shift as Won"),T.State->Phase,EMCShiftPhase::Won);
    TestEqual(TEXT("Completed fixture days do not accumulate failed events"),T.State->FailedEvents,0);
    TestFalse(TEXT("No legacy one-day halt prevents progression"),T.State->bDayOneComplete);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameDirectorDeadlineDebtTest,"MessControl.Director.DeadlineDoesNotPenalizeUnselectedEvents",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameDirectorDeadlineDebtTest::RunTest(const FString&)
{
    FDirectorWorld T;auto* Profile=EmptyDirectorProfile(T.World);EnableEvent(Profile,EMCGameDirectorEvent::Food);
    Profile->IntermissionSeconds=1;
    T.Start(Profile,.5f);T.Clean();T.Mode->bUseAdaptiveDirector=true;T.Mode->SetActorTickEnabled(true);
    // This deliberately short test clock expires during initial cleaning, before
    // any enabled candidate may be selected. Eligibility is not mandatory debt.
    T.Step(.75f);
    TestEqual(TEXT("A clean deadline does not penalize an event never selected"),T.State->FailedEvents,0);
    TestEqual(TEXT("Deadline cannot manufacture an unissued food ticket"),T.Director->PendingFoodCount(),0);
    TestEqual(TEXT("Deadline has not forged a food launch"),T.Director->Observe().SpawnedFood,0);
    TestEqual(TEXT("Deadline has not forged a delivery"),T.Director->Observe().FinishedFood,0);
    TestFalse(TEXT("Unselected event rules are not outstanding work"),T.Director->HasOutstandingWork());
    T.Step(1.1f);
    TestEqual(TEXT("Normal GameMode transition starts day two"),T.State->Day,2);
    TestEqual(TEXT("The next day has no inherited hypothetical food debt"),T.Director->PendingFoodCount(),0);
    TestEqual(TEXT("Unselected opportunities never increment selected food counters"),T.Director->Observe().PlannedFood,0);
    auto* ActualFood=T.Food();T.Step(.75f);
    TestEqual(TEXT("The following deadline still penalizes real undelivered work"),T.State->FailedEvents,1);
    TestTrue(TEXT("A deadline retains the actual last fragment and its unfinished work"),!ActualFood->IsDisposed()
        && T.Director->Observe().Fragments==1 && T.Director->HasOutstandingWork());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameDirectorReinitializeTest,"MessControl.Director.ReinitializeRetiresOldServices",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameDirectorReinitializeTest::RunTest(const FString&)
{
    FDirectorWorld T;auto* Profile=EmptyDirectorProfile(T.World);EnableEvent(Profile,EMCGameDirectorEvent::Yawn);T.Start(Profile);T.Clean();
    for(int32 I=0;I<100 && !T.State->DirectorState.bNextReserved;++I) T.Step(.1f);
    if(!TestTrue(TEXT("Old run has a selected warning before reinitialization"),T.State->DirectorState.bNextReserved)) return false;
    T.Director->Stop();T.Start();
    int32 Services=0;
    for(TActorIterator<AMCDayDirector> It(T.World);It;++It) if(!It->IsActorBeingDestroyed()) ++Services;
    TestEqual(TEXT("Repeated run initialization owns exactly one live event service"),Services,1);
    TestTrue(TEXT("Reinitialized run manages events again"),T.Director->IsManagingEvents());
    TestEqual(TEXT("New run does not inherit a retired reservation"),T.Director->Observe().QueuedEvents,0);
    TestEqual(TEXT("New run has no hypothetical future food debt"),T.Director->PendingFoodCount(),0);
    TestEqual(TEXT("New monitor does not inherit old planned food"),T.Director->Observe().PlannedFood,0);
    T.Director->Stop();
    TestFalse(TEXT("Stopping a run disables its replicated monitor"),T.State->DirectorState.bEnabled);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameDirectorWarningIntakeTest,"MessControl.Director.IntakeDuringWarningCommitsWithoutDeadlock",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameDirectorWarningIntakeTest::RunTest(const FString&)
{
    FDirectorWorld T;auto* Profile=EmptyDirectorProfile(T.World);EnableEvent(Profile,EMCGameDirectorEvent::Yawn,1000,600,1);T.Start(Profile);T.Clean();
    T.Director->Settings->WarningSeconds=5;
    bool Reserved=false;
    for(int32 I=0;I<60 && !Reserved;++I)
    {
        T.Clean();T.Step(.25f);
        Reserved=!T.Director->CanStartSwallow() && !T.Tongue->IsYawnActive();
    }
    if(!TestTrue(TEXT("A runtime-selected global event reserves its warning window before committing"),Reserved)) return false;
    auto* Food=T.Food();
    Food->SetActorLocation(T.Throat->GetActorTransform().TransformPosition(T.Throat->ZoneCenter));
    if(!TestTrue(TEXT("The reservation still permits player-controlled delivery"),T.Throat->AcceptDelivery(Food))) return false;
    T.Throat->SetActorTickEnabled(true);T.Step(.3f);
    TestTrue(TEXT("Committed intake releases the uncommitted global reservation"),T.Director->CanStartSwallow());
    TestEqual(TEXT("Cancelled reservation is not retained as future event debt"),T.Director->Observe().QueuedEvents,0);
    for(int32 I=0;I<85 && !Food->IsDisposed();++I)
    {
        T.Step(.1f);
        TestFalse(TEXT("Deferred yawn never overlaps the committed meal"),T.Tongue->IsYawnActive());
    }
    TestTrue(TEXT("The actual throat reaches Dispose instead of mutually waiting forever"),Food->IsDisposed());
    TestEqual(TEXT("One completed meal empties the gathering queue"),T.Throat->QueuedFoodCount,0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameDirectorProducerGrantTest,"MessControl.Director.GlobalProducersRequireDirectorGrant",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameDirectorProducerGrantTest::RunTest(const FString&)
{
    FDirectorWorld T;T.Start();T.Clean();
    TestFalse(TEXT("Legacy/random yawn cannot start outside the common Director grant"),T.Tongue->StartYawn(3));
    auto* Flood=T.World->SpawnActor<AMCCoffeeFlood>();Flood->Start(T.Director->Mechanics);
    TestFalse(TEXT("A direct coffee river request cannot bypass admission"),Flood->IsActive());
    auto* Cola=T.World->SpawnActor<AMCColdColaEvent>();Cola->Start(T.Director->Mechanics);
    TestTrue(TEXT("A direct cold-cola request cannot bypass admission"),Cola->IsComplete());
    TestFalse(TEXT("Rejected global producers do not create a phantom global channel"),T.Director->Observe().bGlobalMovement);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameDirectorAftermathTest,"MessControl.Director.DisposedPepperAftermathRemainsWork",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameDirectorAftermathTest::RunTest(const FString&)
{
    FDirectorWorld T;T.Start();T.Clean();
    auto* Pepper=T.Food(false,EMCFoodKind::Spicy);Pepper->FoodData.FuseSeconds=1;
    Pepper->SetActorLocation(FVector(0,0,10));Pepper->ArmSpicy();T.Step(1.1f);Pepper->Detonate();
    TestTrue(TEXT("An expired pepper retires its ingredient actor"),Pepper->IsDisposed());
    const auto Seen=T.Director->Observe();
    TestTrue(TEXT("The actual explosion leaves fire or lesion work"),Seen.Fires>0 || Seen.Ulcers>0);
    TestTrue(TEXT("Disposal cannot complete an unresolved pepper aftermath"),T.Director->HasOutstandingWork() && !Pepper->IsHazardResolved());
    TestTrue(TEXT("The real lesion commits its tongue-pain motion"),T.Tongue->IsMotionActive());
    TestFalse(TEXT("Swallow correctly waits for that committed global motion"),T.Director->CanStartSwallow());
    T.Hero->SetActorLocation(FVector(1000,1000,100),false,nullptr,ETeleportType::TeleportPhysics);
    const auto Motion=T.Tongue->Motion;
    const double MotionEnds=Motion.StartedAt+Motion.Settings.Duration()+Motion.Settings.RestAfter;
    T.Step(float(FMath::Max(0.,MotionEnds-T.State->GetServerWorldTimeSeconds())+.3));
    const auto AfterMotion=T.Director->Observe();
    TestFalse(TEXT("The actual pain timeline expires without resetting the consequence"),T.Tongue->IsMotionActive());
    TestTrue(TEXT("Fire or lesion stays mandatory and urgent after the motion expires"),AfterMotion.bUrgent
        && (AfterMotion.Fires>0 || AfterMotion.Ulcers>0) && T.Director->HasOutstandingWork() && !Pepper->IsHazardResolved());
    TestTrue(TEXT("Urgent treatment alone permits a future player-directed swallow"),T.Director->CanStartSwallow());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameDirectorReproducibleTest,"MessControl.Director.RuntimeSelectionsReproduceFromSameSeed",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameDirectorReproducibleTest::RunTest(const FString&)
{
    auto Run=[]()
    {
        FDirectorWorld T;auto* Profile=RuntimeChoiceProfile(T.World);
        EnableEvent(Profile,EMCGameDirectorEvent::Coffee);EnableEvent(Profile,EMCGameDirectorEvent::Yawn);
        T.State->RunSeed=6419;T.Start(Profile);T.Clean();
        TArray<EMCGameDirectorEvent> Sequence;
        // Resolve actual fixture consequences between decisions. Capture the
        // producers' starts, not a second copy of the selection algorithm.
        for(int32 I=0;I<200 && Sequence.Num()<6;++I)
        {
            T.Clean();T.Tongue->ResetYawn();T.Step(.25f);
            const auto Seen=T.Director->Observe();
            if(T.Tongue->IsYawnActive()) Sequence.Add(EMCGameDirectorEvent::Yawn);
            else if(Seen.CleaningTasks>0) Sequence.Add(EMCGameDirectorEvent::Coffee);
        }
        return Sequence;
    };
    const auto First=Run();const auto Second=Run();
    TestTrue(TEXT("Controlled run exercises several real producer starts"),First.Num()>=4);
    TestTrue(TEXT("Same seed, state and timestep reproduce the runtime event sequence"),First==Second);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameDirectorCandidateStateTest,"MessControl.Director.RuntimeCandidatesRespondToObservedWorld",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameDirectorCandidateStateTest::RunTest(const FString&)
{
    FDirectorWorld T;auto* Profile=RuntimeChoiceProfile(T.World);
    EnableEvent(Profile,EMCGameDirectorEvent::Coffee);EnableEvent(Profile,EMCGameDirectorEvent::Yawn);
    T.Start(Profile);T.Clean();T.Step(.25f);
    // Freeze selection while observing real world changes. Evaluation itself
    // remains the production read-only policy, with no test-side scoring.
    T.Director->SetActorTickEnabled(false);
    T.Tongue->ResetYawn();T.Clean();
    const auto Healthy=T.Director->Observe();
    const auto Ready=T.Director->EvaluateCandidates(Healthy,T.State->GetServerWorldTimeSeconds()+10);
    const auto* Coffee=FindCandidate(Ready,EMCGameDirectorEvent::Coffee);
    if(!TestNotNull(TEXT("Enabled coffee remains an explicit candidate"),Coffee)) return false;
    TestTrue(TEXT("A healthy drained world gives coffee a nonzero choice weight"),Coffee->EffectiveWeight>0);
    T.Hero->Status->State.Health=T.Hero->Status->State.MaxHealth*.3f;
    const auto Hurt=T.Director->Observe();
    const auto HurtCandidates=T.Director->EvaluateCandidates(Hurt,T.State->GetServerWorldTimeSeconds()+10);
    const auto* HurtCoffee=FindCandidate(HurtCandidates,EMCGameDirectorEvent::Coffee);
    if(!TestNotNull(TEXT("The same event keeps a diagnostic under low team health"),HurtCoffee)) return false;
    TestTrue(TEXT("Observed health changes effective event weight before any new choice"),Hurt.Stress>Healthy.Stress
        && HurtCoffee->EffectiveWeight<Coffee->EffectiveWeight);
    T.Hero->Status->State.Health=T.Hero->Status->State.MaxHealth;
    // Whole ingredients retain enough destruction work to cross the actual
    // pressure target; forty one-hit fragments no longer constitute overload.
    for(int32 I=0;I<40;++I) T.Food(false,EMCFoodKind::Food,7000+I);
    const auto Overloaded=T.Director->Observe();
    const auto Blocked=T.Director->EvaluateCandidates(Overloaded,T.State->GetServerWorldTimeSeconds()+10);
    const auto* BlockedCoffee=FindCandidate(Blocked,EMCGameDirectorEvent::Coffee);
    if(!TestNotNull(TEXT("Overload exposes its candidate diagnostic"),BlockedCoffee)) return false;
    TestTrue(TEXT("Real outstanding food increases observed work and pressure"),Overloaded.WorkSeconds>Healthy.WorkSeconds && Overloaded.Pressure>Healthy.Pressure);
    TestTrue(TEXT("The overload fixture reaches the director's actual target"),Overloaded.Pressure>=Overloaded.TargetPressure);
    TestTrue(TEXT("Overload suppresses new coffee and explains the decision"),BlockedCoffee->EffectiveWeight==0
        && BlockedCoffee->Probability==0 && !BlockedCoffee->BlockReason.IsEmpty());
    T.Hero->Status->State.Health=0;
    const auto NoWorker=T.Director->Observe();
    const auto NoWorkerCandidates=T.Director->EvaluateCandidates(NoWorker,T.State->GetServerWorldTimeSeconds()+10);
    for(const auto& Candidate:NoWorkerCandidates)
        TestEqual(TEXT("No available living worker cannot receive a new event"),Candidate.Probability,0.f);
    TestEqual(TEXT("Evaluation does not reserve food merely because rules exist"),T.Director->PendingFoodCount(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameDirectorCooldownTest,"MessControl.Director.RuntimeCooldownAndDiversityAffectChoices",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameDirectorCooldownTest::RunTest(const FString&)
{
    FDirectorWorld T;auto* Profile=RuntimeChoiceProfile(T.World);
    EnableEvent(Profile,EMCGameDirectorEvent::Yawn,1000,30);
    T.Start(Profile);T.Clean();
    const auto Baseline=T.Director->Observe();
    const auto Before=T.Director->EvaluateCandidates(Baseline,T.State->GetServerWorldTimeSeconds()+10);
    const auto* BeforeYawn=FindCandidate(Before,EMCGameDirectorEvent::Yawn);
    if(!TestNotNull(TEXT("The enabled air event has an initial score"),BeforeYawn)) return false;
    const float BeforeWeight=BeforeYawn->EffectiveWeight;
    for(int32 I=0;I<150 && !T.Tongue->IsYawnActive();++I) {T.Clean();T.Step(.1f);}
    if(!TestTrue(TEXT("Production selection eventually starts its eligible air event"),T.Tongue->IsYawnActive())) return false;
    const double Started=T.State->GetServerWorldTimeSeconds();
    T.Tongue->ResetYawn();T.Step(.3f);
    const auto Cooling=T.Director->EvaluateCandidates(Baseline,Started+1);
    const auto* CoolingYawn=FindCandidate(Cooling,EMCGameDirectorEvent::Yawn);
    if(!TestNotNull(TEXT("Cooling event stays visible for diagnostics"),CoolingYawn)) return false;
    TestTrue(TEXT("A started event cannot repeat during its cooldown"),CoolingYawn->Probability==0
        && CoolingYawn->EffectiveWeight==0 && !CoolingYawn->BlockReason.IsEmpty());
    const auto After=T.Director->EvaluateCandidates(Baseline,Started+31);
    const auto* RecentYawn=FindCandidate(After,EMCGameDirectorEvent::Yawn);
    if(!TestNotNull(TEXT("Elapsed cooldown restores the candidate"),RecentYawn)) return false;
    TestTrue(TEXT("Recent-kind diversity reduces a repeat under the same supplied observation"),RecentYawn->EffectiveWeight>0
        && RecentYawn->EffectiveWeight<BeforeWeight);
    TestEqual(TEXT("Resolved air event leaves no hypothetical food debt"),T.Director->PendingFoodCount(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameDirectorReliefTest,"MessControl.Director.AdaptationRelievesStrugglingTeam",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameDirectorReliefTest::RunTest(const FString&)
{
    FDirectorWorld T;auto* Profile=EmptyDirectorProfile(T.World);
    for(auto& Day:Profile->Days)
    {
        Day.DaySeconds=120;Day.BuildSeconds=120;
        Day.MinimumDifficulty=.3f;Day.MaximumDifficulty=1.4f;
        Day.TargetPressureMin=.15f;Day.TargetPressureMax=.8f;
    }
    Profile->AdaptationSeconds=20;T.Start(Profile);T.Clean();T.Step(20);
    const auto Healthy=T.Director->Observe();
    T.Hero->Status->State.Health=T.Hero->Status->State.MaxHealth*.1f;
    auto* Movement=Cast<UMCToothMovementComponent>(T.Hero->GetCharacterMovement());
    if(!TestNotNull(TEXT("Native hero supplies production stamina telemetry"),Movement)) return false;
    FMCStaminaPredictionState Stamina;Stamina.Value=0;Stamina.Exhausted=true;Movement->RestoreStaminaPrediction(Stamina);
    for(int32 I=0;I<20;++I) T.Food(true,EMCFoodKind::Food,8000+I);
    const auto BeforeTick=T.Director->Observe();
    T.Step(.3f);const auto First=T.Director->Observe();
    T.Step(15);const auto Relieved=T.Director->Observe();
    TestTrue(TEXT("Low health and exhausted stamina are observed from real hero state"),Relieved.TeamHealth<.2f && Relieved.TeamStamina<.1f);
    TestTrue(TEXT("Telemetry stays finite through overload and relief"),FiniteTelemetry(BeforeTick) && FiniteTelemetry(First) && FiniteTelemetry(Relieved));
    TestTrue(TEXT("Adaptation lowers difficulty and pressure target for a struggling team"),Relieved.Difficulty<Healthy.Difficulty
        && Relieved.TargetPressure<Healthy.TargetPressure);
    TestTrue(TEXT("Relief is gradual rather than an instantaneous full jump"),First.Difficulty>Relieved.Difficulty);
    TestTrue(TEXT("Difficulty and target respect authored lower bounds"),Relieved.Difficulty>=T.Director->DaySettings.MinimumDifficulty
        && Relieved.TargetPressure>=T.Director->DaySettings.TargetPressureMin);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameDirectorFoodAdmissionTest,"MessControl.Director.FoodForecastBoundsActualAdmission",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameDirectorFoodAdmissionTest::RunTest(const FString&)
{
    FDirectorWorld T;auto* Profile=RuntimeChoiceProfile(T.World);
    EnableEvent(Profile,EMCGameDirectorEvent::Food,1000,3600,1);
    for(auto& Day:Profile->Days) Day.MaxFoodBatch=1;
    auto* SavedPlan=T.Mode->FirstDayPlan.LoadSynchronous();
    if(!TestNotNull(TEXT("Food admission uses the real saved menu/mechanics asset"),SavedPlan)) return false;
    T.Start(Profile,0,SavedPlan);T.Clean();
    const auto Before=T.Director->Observe();
    const auto Candidates=T.Director->EvaluateCandidates(Before,T.State->GetServerWorldTimeSeconds()+10);
    const auto* Candidate=FindCandidate(Candidates,EMCGameDirectorEvent::Food);
    if(!TestNotNull(TEXT("Runtime food exposes its transport-inclusive forecast"),Candidate)) return false;
    if(!TestTrue(TEXT("A legal affordable menu row is available"),Candidate->EffectiveWeight>0)) return false;
    const float Forecast=Candidate->ForecastPressure;
    for(int32 I=0;I<100 && T.Director->Observe().SpawnedFood==0;++I) {T.Clean();T.Step(.1f);}
    const auto Actual=T.Director->Observe();
    TestEqual(TEXT("Exactly one affordable runtime meal actually launches"),Actual.SpawnedFood,1);
    TestTrue(TEXT("Entry flight already exposes its real landing/transport work"),Actual.WorkSeconds>0 && Actual.WholeFood==1);
    TestTrue(TEXT("Actual admitted pressure stays inside its conservative forecast and hard budget"),
        Actual.Pressure<=Forecast+.001f && Actual.Pressure<=T.Director->DaySettings.PressureLimit+.001f);
    TestEqual(TEXT("The successful spawn, not candidate evaluation, commits its ledger"),Actual.PlannedFood,Actual.SpawnedFood);
    TestTrue(TEXT("Launched food stays mandatory until real delivery"),T.Director->HasOutstandingWork());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameDirectorColaPayloadTest,"MessControl.Director.SelectedColaLimitsIceWithoutEditingSavedProfile",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameDirectorColaPayloadTest::RunTest(const FString&)
{
    FDirectorWorld T;auto* Profile=RuntimeChoiceProfile(T.World);
    EnableEvent(Profile,EMCGameDirectorEvent::ColdCola,1000,3600,1);
    for(auto& Day:Profile->Days)
    {Day.PressureLimit=.55f;Day.TargetPressureMin=.4f;Day.TargetPressureMax=.5f;}
    auto* SavedPlan=T.Mode->FirstDayPlan.LoadSynchronous();
    if(!TestNotNull(TEXT("Cola fixture uses the saved production plan"),SavedPlan)) return false;
    auto* SavedProfile=SavedPlan->ColdColaProfile.LoadSynchronous();
    if(!TestNotNull(TEXT("The saved production cola profile is available"),SavedProfile)) return false;
    const int32 OriginalIce=SavedProfile->IceCount;
    if(!TestTrue(TEXT("Saved profile has more ice than the admitted one-block payload"),SavedProfile->IsAsset() && OriginalIce>1)) return false;
    T.Start(Profile,0,SavedPlan);T.Clean();
    AMCColdColaEvent* Event=nullptr;
    for(int32 I=0;I<150 && !Event;++I)
    {
        T.Step(.1f);
        for(TActorIterator<AMCColdColaEvent> It(T.World);It;++It) if(It->bActive) {Event=*It;break;}
    }
    if(!TestNotNull(TEXT("Runtime selection starts the real admitted cold-cola event"),Event)) return false;
    T.Step(6);
    int32 ActualIce=0;
    for(TActorIterator<AMCIceBlock> It(T.World);It;++It) if(!It->IsActorBeingDestroyed()) ++ActualIce;
    TestEqual(TEXT("Selected budget override spawns exactly one actual ice actor"),ActualIce,1);
    TestEqual(TEXT("The event owns only its one selected ice block"),Event->IceLeft(),1);
    TestTrue(TEXT("Runtime event retains the networking-stable saved profile"),Event->Profile==SavedProfile && Event->Profile->IsAsset());
    TestEqual(TEXT("Payload scaling cannot mutate the saved authored ice count"),SavedProfile->IceCount,OriginalIce);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameDirectorDifficultyScaleTest,"MessControl.Director.TripleDifficultyScalesSevenActiveDaysWithoutChangingAssets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameDirectorDifficultyScaleTest::RunTest(const FString&)
{
    FDirectorWorld T;auto* Saved=T.Mode->DirectorProfile.LoadSynchronous();
    if(!TestNotNull(TEXT("Scaling fixture reads the saved Director profile"),Saved)) return false;
    const auto SavedDays=Saved->Days;const float SavedMultiplier=Saved->DifficultyMultiplier;
    auto* Source=DuplicateObject<UMCGameDirectorProfile>(Saved,T.World);
    Source->DifficultyMultiplier=3;
    for(auto& Rule:Source->Events) Rule.Weight=0;
    for(auto& Day:Source->Days) Day.InitialPatches=0;
    Source->Sanitize();const auto RawDays=Source->Days;
    T.Start(Source);T.Clean();
    for(int32 I=0;I<7;++I)
    {
        T.Clean();T.Director->BeginDay(I+1);const auto& Active=T.Director->DaySettings;const auto& Raw=RawDays[I];
        TestTrue(TEXT("Active difficulty and pressure budgets use the requested triple scale"),
            FMath::IsNearlyEqual(Active.MinimumDifficulty,Raw.MinimumDifficulty*3)
            && FMath::IsNearlyEqual(Active.MaximumDifficulty,Raw.MaximumDifficulty*3)
            && FMath::IsNearlyEqual(Active.TargetPressureMin,Raw.TargetPressureMin*3)
            && FMath::IsNearlyEqual(Active.TargetPressureMax,Raw.TargetPressureMax*3)
            && FMath::IsNearlyEqual(Active.PressureLimit,Raw.PressureLimit*3)
            && FMath::IsNearlyEqual(Active.FoodWorkPerPlayer,Raw.FoodWorkPerPlayer*3));
        TestEqual(TEXT("Whole-food capacity scales upward with the existing safety cap"),Active.MaxWholeFood,FMath::Min(12,Raw.MaxWholeFood*3));
        TestEqual(TEXT("Fragment capacity scales upward with the existing safety cap"),Active.MaxFragments,FMath::Min(80,Raw.MaxFragments*3));
        TestTrue(TEXT("Scaling preserves each day's clock, cadence and authored event payload"),
            Active.DaySeconds==Raw.DaySeconds && Active.EventGap==Raw.EventGap && Active.FoodInterval==Raw.FoodInterval
            && Active.RestSeconds==Raw.RestSeconds && Active.FinalCleanupSeconds==Raw.FinalCleanupSeconds
            && Active.CoffeePatches==Raw.CoffeePatches && Active.MaxFoodBatch==Raw.MaxFoodBatch);
        T.Step(2);T.Hero->Status->State.Health=T.Hero->Status->State.MaxHealth*.1f;T.Step(3);
        const auto Seen=T.Director->Observe();
        TestTrue(TEXT("Real low-health adaptation remains finite inside scaled bounds on every day"),FiniteTelemetry(Seen)
            && Seen.Difficulty>=Active.MinimumDifficulty-.001f && Seen.Difficulty<=Active.MaximumDifficulty+.001f
            && Seen.TargetPressure>=Active.TargetPressureMin-.001f && Seen.TargetPressure<=Active.TargetPressureMax+.001f);
        TestTrue(TEXT("BeginDay never writes scaled values back into the run's raw authored days"),
            FMCGameDirectorDaySettings::StaticStruct()->CompareScriptStruct(&T.Director->Settings->Days[I],&Raw,0));
    }
    TestEqual(TEXT("Scaling does not edit the source asset multiplier"),Saved->DifficultyMultiplier,SavedMultiplier);
    TestEqual(TEXT("Scaling does not add or drop source asset days"),Saved->Days.Num(),SavedDays.Num());
    for(int32 I=0;I<SavedDays.Num();++I)
        TestTrue(TEXT("The saved authored day remains unchanged"),FMCGameDirectorDaySettings::StaticStruct()->CompareScriptStruct(&Saved->Days[I],&SavedDays[I],0));
    for(int32 I=0;I<RawDays.Num();++I)
        TestTrue(TEXT("The supplied transient profile also retains raw day values"),FMCGameDirectorDaySettings::StaticStruct()->CompareScriptStruct(&Source->Days[I],&RawDays[I],0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGameDirectorCoffeeStarvationTest,"MessControl.Director.OrdinaryFoodDoesNotStarveCoffeeWhileSpecialGatesRemain",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGameDirectorCoffeeStarvationTest::RunTest(const FString&)
{
    FDirectorWorld T;auto* Profile=RuntimeChoiceProfile(T.World);
    for(auto& Day:Profile->Days) {Day.EventGap=12;Day.MaxFoodBatch=1;}
    EnableEvent(Profile,EMCGameDirectorEvent::Food,1000,3600,1);
    auto* SavedPlan=T.Mode->FirstDayPlan.LoadSynchronous();
    if(!TestNotNull(TEXT("The cadence regression launches real food from the saved menu"),SavedPlan)) return false;
    T.Start(Profile,0,SavedPlan);T.Clean();
    for(int32 I=0;I<100 && T.Director->Observe().SpawnedFood==0;++I) {T.Clean();T.Step(.1f);}
    if(!TestEqual(TEXT("A normal runtime food item starts before testing the coffee window"),T.Director->Observe().SpawnedFood,1)) return false;
    T.Director->SetActorTickEnabled(false);
    // Complete only this diagnostic meal. Keeping its transport work would
    // legitimately block coffee by pressure and obscure the cadence regression.
    for(TActorIterator<AMCFoodActor> It(T.World);It;++It)
        if(!It->bBrushTool && !It->IsDisposed()) It->Dispose();
    T.Clean();EnableEvent(T.Director->Settings,EMCGameDirectorEvent::Coffee,1000,30);
    const double FoodFinished=T.State->GetServerWorldTimeSeconds();
    const auto AfterFood=T.Director->EvaluateCandidates(T.Director->Observe(),FoodFinished);
    const auto* ImmediateCoffee=FindCandidate(AfterFood,EMCGameDirectorEvent::Coffee);
    if(!TestNotNull(TEXT("Coffee is evaluated immediately after ordinary food"),ImmediateCoffee)) return false;
    TestTrue(TEXT("Ordinary food does not restart the special-event gap and starve coffee"),ImmediateCoffee->EffectiveWeight>0
        && ImmediateCoffee->Probability>0 && ImmediateCoffee->BlockReason.IsEmpty());
    T.Director->SetActorTickEnabled(true);
    bool CoffeeStarted=false;
    for(int32 I=0;I<100 && !CoffeeStarted;++I) {T.Step(.1f);CoffeeStarted=T.Director->Observe().CleaningTasks>0;}
    if(!TestTrue(TEXT("The real coffee producer actually launches in the open window"),CoffeeStarted)) return false;
    TestTrue(TEXT("Coffee starts before the old food-based twelve-second gap would expire"),T.State->GetServerWorldTimeSeconds()-FoodFinished<12);
    T.Director->SetActorTickEnabled(false);T.Clean();EnableEvent(T.Director->Settings,EMCGameDirectorEvent::Yawn,1000,0);
    const double CoffeeFinished=T.State->GetServerWorldTimeSeconds();
    const auto Cooling=T.Director->EvaluateCandidates(T.Director->Observe(),CoffeeFinished);
    const auto* Yawn=FindCandidate(Cooling,EMCGameDirectorEvent::Yawn);const auto* Coffee=FindCandidate(Cooling,EMCGameDirectorEvent::Coffee);
    if(!TestNotNull(TEXT("The next special event has a cadence diagnostic"),Yawn)
        || !TestNotNull(TEXT("Coffee has its own cooldown diagnostic"),Coffee)) return false;
    // Coffee cleaning is an ordinary producer in the current scheduler. Its own
    // cooldown still applies, while it does not reset the movement-event gap.
    TestTrue(TEXT("Coffee cleaning does not restart the movement-event gap"),Yawn->EffectiveWeight>0
        && Yawn->Probability>0 && Yawn->BlockReason.IsEmpty());
    TestTrue(TEXT("Coffee keeps its individual cooldown after a real launch"),Coffee->EffectiveWeight==0
        && Coffee->BlockReason.Contains(TEXT("Cooldown")));
    const auto Restored=T.Director->EvaluateCandidates(T.Director->Observe(),CoffeeFinished+31);
    const auto* Ready=FindCandidate(Restored,EMCGameDirectorEvent::Coffee);
    if(!TestNotNull(TEXT("Coffee remains an explicit option after its cooldown"),Ready)) return false;
    TestTrue(TEXT("Elapsed special gap and cooldown reopen coffee"),Ready->EffectiveWeight>0 && Ready->Probability>0);
    for(int32 I=0;I<40;++I) T.Food(false,EMCFoodKind::Food,9200+I);
    const auto OverloadedWorld=T.Director->Observe();
    TestTrue(TEXT("The admission fixture exceeds the actual pressure target"),OverloadedWorld.Pressure>=OverloadedWorld.TargetPressure);
    const auto Overloaded=T.Director->EvaluateCandidates(OverloadedWorld,CoffeeFinished+31);
    const auto* Blocked=FindCandidate(Overloaded,EMCGameDirectorEvent::Coffee);
    if(!TestNotNull(TEXT("Overloaded coffee remains visible in diagnostics"),Blocked)) return false;
    TestTrue(TEXT("Removing food starvation never bypasses overload admission"),Blocked->EffectiveWeight==0
        && Blocked->Probability==0 && !Blocked->BlockReason.IsEmpty());
    return true;
}
#endif
