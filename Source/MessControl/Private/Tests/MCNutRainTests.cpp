#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodActor.h"
#include "MCNutEnemy.h"
#include "MCNutBoss.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "MCNutRainEvent.h"
#include "MCNutLandingShadow.h"
#include "MCNutRainProfile.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutRainLimits,"MessControl.CoreLoop.NutRain.SafeCountsAndCadence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutRainLimits::RunTest(const FString&)
{
    FMCNutRainSettings Settings;
    TestEqual(TEXT("Directed rain includes final flight inside twenty to twenty-five seconds"),Settings.RainSeconds,22.5f);
    TestEqual(TEXT("Directed rain derives its count from duration and individual drop spacing"),Settings.RainCountForPlayers(4),10);
    Settings.RainSeconds=40; Settings.MinimumDropSeconds=1.5f; Settings.MaximumDropSeconds=2.5f; Settings.SeriesRestSeconds=4;
    Settings.MinimumNutsPerSeries=2; Settings.MaximumNutsPerSeries=4; Settings.Sanitize();
    TestEqual(TEXT("Saved forty-second profile is migrated into the approved duration"),Settings.RainSeconds,25.f);
    TestTrue(TEXT("Individual drops retain one-and-a-half to three second reaction time"),Settings.MinimumDropSeconds>=1.5f && Settings.MaximumDropSeconds<=3.f);
    TestEqual(TEXT("Saved multi-second pauses are removed for continuous rain"),Settings.SeriesRestSeconds,0.f);
    Settings.bBossEncounter=false;
    TestEqual(TEXT("Solo prototype launches 30 nuts"),Settings.RainCountForPlayers(1),30);
    TestEqual(TEXT("Solo prototype can awaken four surviving nuts"),Settings.EnemyCountForPlayers(1),4);
    TestEqual(TEXT("Cooperation can scale the rain"),Settings.RainCountForPlayers(4),54);
    TestEqual(TEXT("Cooperation can scale the hostile aftermath"),Settings.EnemyCountForPlayers(4),7);
    Settings.NutCount=MAX_int32; Settings.NutsPerExtraPlayer=MAX_int32;
    Settings.EnemyCount=MAX_int32; Settings.EnemiesPerExtraPlayer=MAX_int32;
    Settings.RainSeconds=Settings.SettleSeconds=Settings.Entry.FlightSeconds=std::numeric_limits<float>::quiet_NaN();
    Settings.Enemy.AttackDamage=Settings.Enemy.WindupSeconds=std::numeric_limits<float>::infinity();
    Settings.Sanitize();
    TestEqual(TEXT("Rain physics budget is bounded for corrupt player counts"),Settings.RainCountForPlayers(MAX_int32),120);
    TestEqual(TEXT("Hostile actor budget is bounded"),Settings.EnemyCountForPlayers(MAX_int32),16);
    TestTrue(TEXT("Awakening waits for the final launch to land"),Settings.SettleSeconds>=Settings.Entry.FlightSeconds+.25f);
    TestTrue(TEXT("Corrupt rain duration becomes finite"),FMath::IsFinite(Settings.RainSeconds) && Settings.RainSeconds>=3);
    TestTrue(TEXT("Enemies retain a readable finite telegraph"),FMath::IsFinite(Settings.Enemy.WindupSeconds) && Settings.Enemy.WindupSeconds>=.25f);
    return true;
}

namespace
{
struct FNutRainWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCTongue* Tongue=nullptr;
    AMCNutRainEvent* Event=nullptr;
    // A transient soft asset path cannot reload its table after collection.
    // The fixture owns both objects across Event::Stop and a second Start.
    TStrongObjectPtr<UMCNutRainProfile> Profile;
    TStrongObjectPtr<UDataTable> Menu;
    FNutRainWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
        UStaticMesh* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
        FStaticMeshCompilingManager::Get().FinishCompilation({Cube});
#endif
        const FTransform Pose(FRotator::ZeroRotator,FVector::ZeroVector,FVector(24,16,.2));
        Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Tongue->SourceMesh=Cube; Tongue->bAutomaticYawns=false; Tongue->FinishSpawning(Pose); Tongue->SetActorTickEnabled(false);
        Profile.Reset(NewObject<UMCNutRainProfile>(World)); Profile->Settings.NutCount=6;
        Profile->Settings.bBossEncounter=false;
        Profile->Settings.NutsPerExtraPlayer=Profile->Settings.EnemiesPerExtraPlayer=0;
        Profile->Settings.EnemyCount=2; Profile->Settings.RainSeconds=3; Profile->Settings.SettleSeconds=1.5f;
        Profile->Settings.Entry.FlightSeconds=.75f; Profile->Settings.Entry.EntryHeight=150;
        Menu.Reset(NewObject<UDataTable>(World)); Menu->RowStruct=FMCFoodRow::StaticStruct();
        FMCFoodRow Row; Row.WholeMeshes={Cube}; Row.FragmentMeshes={Cube}; Row.Resistance=EMCFoodResistance::Hard;
        Row.Scale=FVector(.5); Row.FragmentScale=FVector(.25); Row.SpoilSeconds=600; Row.Mass=7;
        Menu->AddRow(TEXT("Walnut"),Row); Profile->Menu=Menu.Get();
        Event=World->SpawnActor<AMCNutRainEvent>();
    }
    ~FNutRainWorld()
    { World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
    void Step(float Seconds)
    { for(int32 I=0;I<FMath::CeilToInt(Seconds*60);++I) { ++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60); } }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutRainLifecycle,"MessControl.CoreLoop.NutRain.RainThenHostilesAndReset",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutRainLifecycle::RunTest(const FString&)
{
    FNutRainWorld T; T.Event->Start(nullptr,T.Profile.Get());
    TestEqual(TEXT("The event begins with rain"),T.Event->Stage,EMCNutRainStage::Rainfall);
    TestEqual(TEXT("Only the first nut launches immediately"),T.Event->NutsSpawned,1);
    TestEqual(TEXT("Rain does not immediately create enemies"),T.Event->EnemiesLeft,0);
    TestTrue(TEXT("Launched nut follows the existing ballistic mouth-entry transport"),T.Event->Nuts[0]->IsMouthEntryActive());
    T.Step(3.1f);
    TestEqual(TEXT("Rain obeys the configured drop budget"),T.Event->NutsSpawned,6);
    TestEqual(TEXT("The final flight settles before hostile awakening"),T.Event->Stage,EMCNutRainStage::Settling);
    T.Step(1.7f);
    TestFalse(TEXT("Valid food collision and tongue do not fail the event"),T.Event->bFailed);
    if(!TestEqual(TEXT("Newest surviving nuts become hostile"),T.Event->EnemiesLeft,2)) return false;
    TestEqual(TEXT("Rain time alone cannot complete the hostile aftermath"),T.Event->Stage,EMCNutRainStage::Enemies);
    T.Step(3);
    TestFalse(TEXT("Hostiles require defeat, even after the rain deadline"),T.Event->IsComplete());
    auto* First=T.Event->Enemies[0].Get();
    TestEqual(TEXT("Invalid damage cannot remove a hostile"),First->ReceiveToolDamage(std::numeric_limits<float>::quiet_NaN(),nullptr),0.f);
    First->ReceiveToolDamage(1000,nullptr); T.Step(.1f);
    TestEqual(TEXT("One defeated nut leaves one hostile"),T.Event->EnemiesLeft,1);
    TestFalse(TEXT("The remaining hostile still blocks event completion"),T.Event->IsComplete());
    T.Event->Enemies[1]->ReceiveToolDamage(1000,nullptr); T.Step(.1f);
    TestTrue(TEXT("Defeating every awakened nut completes the event"),T.Event->IsComplete());
    const int32 Batch=T.Event->Nuts[0]->Batch;
    T.Event->Stop();
    int32 Remaining=0;
    for(TActorIterator<AMCFoodActor> It(T.World);It;++It) if(It->Batch==Batch) ++Remaining;
    TestEqual(TEXT("Explicit reset removes rain leftovers"),Remaining,0);
    TestEqual(TEXT("Reset restores the idle stage"),T.Event->Stage,EMCNutRainStage::Idle);
    TestEqual(TEXT("Reset removes enemy handles"),T.Event->Enemies.Num(),0);
    CollectGarbage(RF_NoFlags);
    TestEqual(TEXT("The fixture retains its transient menu across reset and collection"),T.Profile->Menu.Get(),T.Menu.Get());
    T.Event->Start(nullptr,T.Profile.Get());
    TestEqual(TEXT("A new event starts a fresh count"),T.Event->NutsSpawned,1);
    TestFalse(TEXT("A restarted storm cannot inherit completion"),T.Event->IsComplete());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutEnemyTargeting,"MessControl.CoreLoop.NutRain.LiveTargetsAndInterruptibleBites",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutEnemyTargeting::RunTest(const FString&)
{
    FNutRainWorld T; T.Event->Start(nullptr,T.Profile.Get()); T.Step(4.8f);
    if(!TestTrue(TEXT("The fixture has a hostile nut"),!T.Event->Enemies.IsEmpty())) return false;
    AMCNutEnemy* Enemy=T.Event->Enemies[0];
    for(AMCNutEnemy* Other:T.Event->Enemies) if(Other!=Enemy) Other->SetActorTickEnabled(false);
    auto* Hero=T.World->SpawnActor<AMCToothCharacter>(Enemy->GetActorLocation()+FVector(90,0,65),FRotator::ZeroRotator);
    Hero->GetCharacterMovement()->DisableMovement();
    T.Step(.5f);
    TestTrue(TEXT("A nut finds the live player"),Enemy->Target==Hero);
    const float Before=Hero->Status->State.Health;
    TestTrue(TEXT("A close player starts a visible bite windup"),Enemy->AttackStartedAt>-99);
    TestTrue(TEXT("A weapon strike registers against the enemy"),Enemy->ReceiveToolDamage(5,Hero)>0);
    T.Step(.6f);
    TestEqual(TEXT("Interrupting a bite prevents its scheduled damage"),Hero->Status->State.Health,Before);
    Hero->Status->Damage(1000); T.Step(.6f);
    TestNull(TEXT("A dead player is removed from the nut's target"),Enemy->Target.Get());
    TestTrue(TEXT("A defeated player cannot deal another tool hit"),Enemy->ReceiveToolDamage(5,Hero)==0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutDirectedEncounter,"MessControl.CoreLoop.NutRain.DirectedSeriesAndGuaranteedBossGate",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutDirectedEncounter::RunTest(const FString&)
{
    FNutRainWorld T;
    T.Profile->Settings.bBossEncounter=true;
    T.Profile->Settings.RainSeconds=22.5f;
    T.Profile->Settings.MinimumNutsPerSeries=T.Profile->Settings.MaximumNutsPerSeries=2;
    auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    T.Profile->Settings.Boss.WholeMesh=Cube; T.Profile->Settings.Boss.ShellMesh=Cube; T.Profile->Settings.Boss.KernelMesh=Cube;
    T.Event->Start(nullptr,T.Profile.Get());
    const int32 Batch=T.Event->Nuts[0]->Batch;
    TestTrue(TEXT("Rain walnuts have the configured player-sized height"),FMath::IsNearlyEqual(float(T.Event->Nuts[0]->Visual->Bounds.BoxExtent.Z*2),180.f,1.f));
    TestTrue(TEXT("Director's storm lasts between twenty and twenty-five seconds"),T.Event->Settings.RainSeconds>=20 && T.Event->Settings.RainSeconds<=25);
    TestTrue(TEXT("The rain schedule continuously covers the storm with individual launches"),T.Event->PlannedLaunchTimes.Num()>=8);
    TestTrue(TEXT("The last scheduled launch reserves its flight inside the storm"),
        FMath::IsNearlyEqual(T.Event->PlannedLaunchTimes.Last()+T.Event->Settings.Entry.FlightSeconds,T.Event->Settings.RainSeconds,.01f));
    for(int32 Index=0;Index<T.Event->PlannedLandings.Num();++Index)
        TestTrue(TEXT("Boss cataclysm starts directly above its advertised landing"),
            FVector::DistSquared2D(T.Event->LaunchLocations[Index],T.Event->PlannedLandings[Index])<1.f);
    T.Step(19);
    TestEqual(TEXT("The minimum twenty-second dodge phase cannot start bosses early"),T.Event->Stage,EMCNutRainStage::Rainfall);
    for(AMCFoodActor* Food:T.Event->Nuts) if(IsValid(Food) && !Food->IsDisposed()) Food->Dispose();
    T.Step(7);
    TestFalse(TEXT("Directed encounter starts successfully"),T.Event->bFailed);
    TestTrue(TEXT("Several individual drops precede bosses"),T.Event->CompletedSeries>=4);
    TestEqual(TEXT("Removing every existing walnut cannot cancel the two bosses"),T.Event->Bosses.Num(),2);
    TestEqual(TEXT("Falling bosses remain part of the alive encounter"),T.Event->BossesLeft,2);
    TestFalse(TEXT("An untouched encounter cannot complete on a deadline"),T.Event->IsComplete());
    const auto& Times=T.Event->PlannedLaunchTimes;
    for(int32 I=1;I<Times.Num();++I) {
        const float Gap=Times[I]-Times[I-1];
        TestTrue(TEXT("Every individual drop follows the approved one-and-a-half to three second interval"),Gap>=1.499f && Gap<=3.001f);
    }
    const int32 SpellBefore=T.Event->NutsSpawned;
    TestTrue(TEXT("Wizard rain targeting outside the playable interest area falls back to a real reachable nut"),
        T.Event->SpawnSpellNut(FVector(100000,100000,0),12345));
    TestEqual(TEXT("Fallback spell rain registers exactly one real food actor"),T.Event->NutsSpawned,SpellBefore+1);
    if(T.Event->NutsSpawned>SpellBefore) {
        const FVector Landing=T.Event->PlannedLandings.Last();
        FHitResult ValidFloor;
        TestTrue(TEXT("Fallback nut's actual telegraphed footprint stays reachable and clear of delivery lanes"),
            T.Tongue->GameplaySpawnFootprint(Landing,T.Event->Settings.ImpactRadius,ValidFloor));
        TestTrue(TEXT("Fallback spell nut uses the real mouth-entry flight"),T.Event->Nuts.Last()->IsMouthEntryActive());
        TestTrue(TEXT("Fallback spell nut has an independently owned visible warning actor"),
            !T.Event->LandingShadows.IsEmpty() && IsValid(T.Event->LandingShadows.Last()));
    }
    // A native obstacle/food contact can leave the enum Falling after entry has
    // ended. Exercise the actual food body/floor support, not an enum-only mock.
    FHitResult Support;
    if(!TestTrue(TEXT("Fixture has a supported tongue point for a resting whole nut"),T.Tongue->SurfacePoint(FVector::ZeroVector,Support))) return false;
    const FTransform RestPose(Support.ImpactPoint+FVector(0,0,30));
    auto* RestingNut=T.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),RestPose,T.Event,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    FRandomStream FixtureRandom(17);
    RestingNut->ConfigureItem(TEXT("Walnut"),*T.Menu->FindRow<FMCFoodRow>(TEXT("Walnut"),TEXT("resting rain nut fixture")),FixtureRandom);
    RestingNut->Tags.AddUnique(TEXT("MCNutRain")); RestingNut->Batch=Batch; RestingNut->FinishSpawning(RestPose);
    RestingNut->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
    RestingNut->Phase=EMCFoodPhase::Falling; T.Event->Nuts.Add(RestingNut);
    TArray<AMCFoodActor*> Available;
    T.Event->GetAvailableRainNuts(Available);
    TestTrue(TEXT("Resting supported Falling nut remains eligible for the wizard's summon"),Available.Contains(RestingNut));
    RestingNut->SetActorLocation(Support.ImpactPoint+FVector(0,0,800),false,nullptr,ETeleportType::TeleportPhysics);
    T.Event->GetAvailableRainNuts(Available);
    TestFalse(TEXT("An actual airborne Falling nut cannot be summoned"),Available.Contains(RestingNut));
    RestingNut->SetActorLocation(Support.ImpactPoint-FVector(0,0,120),false,nullptr,ETeleportType::TeleportPhysics);
    T.Event->GetAvailableRainNuts(Available);
    TestFalse(TEXT("A resting nut below the tongue is not mistaken for ground support"),Available.Contains(RestingNut));
    RestingNut->SetActorLocation(RestPose.GetLocation(),false,nullptr,ETeleportType::TeleportPhysics);
    RestingNut->BeginMouthEntry(FVector(0,0,-50),0,RestPose.GetLocation());
    T.Event->GetAvailableRainNuts(Available);
    TestFalse(TEXT("Active mouth entry remains ineligible even near the supporting floor"),Available.Contains(RestingNut));
    auto* SpareCreep=T.World->SpawnActor<AMCNutEnemy>();
    T.Event->RegisterEncounterEnemy(SpareCreep);
    TestTrue(TEXT("A newly registered ordinary enemy is initially alive"),SpareCreep->IsEncounterAlive());
    for(AMCNutBoss* Boss:T.Event->Bosses) if(IsValid(Boss)) Boss->ReceiveToolDamage(100000,nullptr);
    T.Step(.1f);
    TestTrue(TEXT("Defeating both bosses unlocks the encounter without a leftover summon gate"),T.Event->IsComplete());
    TestTrue(TEXT("Remaining summons are removed when both bosses are defeated"),!IsValid(SpareCreep) || SpareCreep->IsActorBeingDestroyed());
    T.Event->Stop();
    int32 Left=0; for(TActorIterator<AMCFoodActor> It(T.World);It;++It) if(It->Batch==Batch) ++Left;
    TestEqual(TEXT("Encounter reset also removes cracked walnut fragments"),Left,0);
    TestEqual(TEXT("Encounter reset releases boss handles"),T.Event->Bosses.Num(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutRainGroundSpacing,"MessControl.CoreLoop.NutRain.GroundSpacingAndExplicitSpellFallback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutRainGroundSpacing::RunTest(const FString&)
{
    FNutRainWorld T;
    T.Profile->Settings.bBossEncounter=true;
    auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    T.Profile->Settings.Boss.WholeMesh=Cube; T.Profile->Settings.Boss.ShellMesh=Cube; T.Profile->Settings.Boss.KernelMesh=Cube;
    T.Event->Start(nullptr,T.Profile.Get());
    if(!TestTrue(TEXT("Opening rain has a real whole nut"),!T.Event->Nuts.IsEmpty())) return false;
    auto* First=T.Event->Nuts[0].Get();
    // This placement-only fixture puts the first native food on its advertised
    // floor. The actor's normal Tick ends entry when its phase becomes Free.
    // No spawn/clearance helper is bypassed for any subsequent nut.
    const auto RestOnFloor=[&](AMCFoodActor* Food,FVector Point) {
        FHitResult Floor;
        if(!IsValid(Food) || !T.Tongue->SurfacePoint(Point,Floor)) return false;
        Food->SetActorLocation(Floor.ImpactPoint+FVector(0,0,Food->Body->GetScaledBoxExtent().Z+5),false,nullptr,ETeleportType::TeleportPhysics);
        Food->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
        Food->Phase=EMCFoodPhase::Free; Food->Tick(.01f);
        return !Food->IsMouthEntryActive() && !Food->IsDisposed();
    };
    if(!TestTrue(TEXT("First whole nut is resting and no longer in mouth entry"),RestOnFloor(First,T.Event->PlannedLandings[0]))) return false;
    T.Step(3.1f);
    if(!TestTrue(TEXT("Normal opening scheduler launches the next individual nut"),T.Event->Nuts.Num()>=2)) return false;
    auto* Second=T.Event->Nuts[1].Get();
    const auto Clearance=[&](const AMCFoodActor* A,const AMCFoodActor* B) {
        return FMath::Max(T.Event->Settings.LargeNutHeight*1.15f,
            float(A->Body->GetScaledBoxExtent().Size2D()+B->Body->GetScaledBoxExtent().Size2D())+24.f);
    };
    TestTrue(TEXT("Opening placement reserves the resting whole nut's physical footprint"),
        FVector::Dist2D(T.Event->PlannedLandings[1],First->GetActorLocation())>=Clearance(First,Second)-.1f);
    if(!TestTrue(TEXT("Second occupied footprint is also a resting whole nut"),RestOnFloor(Second,T.Event->PlannedLandings[1]))) return false;
    for(int32 Index=2;Index<T.Event->Nuts.Num();++Index) if(IsValid(T.Event->Nuts[Index])) T.Event->Nuts[Index]->Dispose();
    // The dedicated placement fixture skips boss creation and disables the
    // event tick so its synthetic Enemies stage cannot trigger a victory gate.
    T.Event->SetActorTickEnabled(false); T.Event->Stage=EMCNutRainStage::Enemies;
    const int32 Before=T.Event->NutsSpawned;
    if(!TestTrue(TEXT("Explicit wizard candidate on a ground nut falls back to another reachable landing"),
        T.Event->SpawnSpellNut(First->GetActorLocation(),77123))) return false;
    TestEqual(TEXT("Explicit fallback still creates exactly one real food actor"),T.Event->NutsSpawned,Before+1);
    auto* Incoming=T.Event->Nuts.Last().Get();
    const FVector Landing=T.Event->PlannedLandings.Last();
    TestTrue(TEXT("Explicit spell fallback clears the first occupied ground footprint"),
        FVector::Dist2D(Landing,First->GetActorLocation())>=Clearance(First,Incoming)-.1f);
    TestTrue(TEXT("Explicit spell fallback also clears the second occupied ground footprint"),
        FVector::Dist2D(Landing,Second->GetActorLocation())>=Clearance(Second,Incoming)-.1f);
    FHitResult Floor;
    TestTrue(TEXT("Spaced spell nut still lands in a reachable gameplay footprint"),
        T.Tongue->GameplaySpawnFootprint(Landing,T.Event->Settings.ImpactRadius,Floor));
    TestTrue(TEXT("Ground reservations preserve both existing whole nuts for later summons"),!First->IsDisposed() && !Second->IsDisposed());
    TestTrue(TEXT("Spaced spell food still uses real entry and an owned landing warning"),
        Incoming->IsMouthEntryActive() && IsValid(T.Event->LandingShadows.Last()));
    return true;
}
#endif
