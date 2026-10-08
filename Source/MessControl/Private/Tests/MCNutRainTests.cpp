#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodActor.h"
#include "MCNutEnemy.h"
#include "Components/StaticMeshComponent.h"
#include "MCNutRainEvent.h"
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
    TestEqual(TEXT("Directed rain stays at least forty seconds"),Settings.RainSeconds,40.f);
    TestEqual(TEXT("Directed rain has a bounded eight by four launch ceiling"),Settings.RainCountForPlayers(4),32);
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
    T.Profile->Settings.RainSeconds=40;
    T.Profile->Settings.MinimumNutsPerSeries=T.Profile->Settings.MaximumNutsPerSeries=2;
    auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    T.Profile->Settings.Boss.WholeMesh=Cube; T.Profile->Settings.Boss.ShellMesh=Cube; T.Profile->Settings.Boss.KernelMesh=Cube;
    T.Event->Start(nullptr,T.Profile.Get());
    const int32 Batch=T.Event->Nuts[0]->Batch;
    TestTrue(TEXT("Rain walnuts have the configured player-sized height"),FMath::IsNearlyEqual(float(T.Event->Nuts[0]->Visual->Bounds.BoxExtent.Z*2),180.f,1.f));
    T.Step(39);
    TestEqual(TEXT("Forty-second dodge phase cannot start bosses early"),T.Event->Stage,EMCNutRainStage::Rainfall);
    for(AMCFoodActor* Food:T.Event->Nuts) if(IsValid(Food) && !Food->IsDisposed()) Food->Dispose();
    T.Step(12);
    TestFalse(TEXT("Directed encounter starts successfully"),T.Event->bFailed);
    TestTrue(TEXT("At least four complete series precede bosses"),T.Event->CompletedSeries>=4);
    TestEqual(TEXT("Removing every existing walnut cannot cancel the two bosses"),T.Event->Bosses.Num(),2);
    TestEqual(TEXT("Falling bosses remain part of the alive encounter"),T.Event->BossesLeft,2);
    TestFalse(TEXT("An untouched encounter cannot complete on a deadline"),T.Event->IsComplete());
    const auto& Times=T.Event->PlannedLaunchTimes;
    for(int32 I=1;I<Times.Num();++I) {
        const float Gap=Times[I]-Times[I-1];
        if(I%2==1) TestTrue(TEXT("Nuts inside a series have reaction time"),Gap>=1.49f && Gap<=2.57f);
        else TestTrue(TEXT("Complete series have a four-second breather"),Gap>=3.99f && Gap<=4.07f);
    }
    for(AMCNutEnemy* Enemy:T.Event->Enemies) if(IsValid(Enemy)) Enemy->ReceiveToolDamage(100000,nullptr);
    T.Step(.1f);
    TestTrue(TEXT("Defeating both bosses and every creep unlocks the encounter"),T.Event->IsComplete());
    T.Event->Stop();
    int32 Left=0; for(TActorIterator<AMCFoodActor> It(T.World);It;++It) if(It->Batch==Batch) ++Left;
    TestEqual(TEXT("Encounter reset also removes cracked walnut fragments"),Left,0);
    TestEqual(TEXT("Encounter reset releases boss handles"),T.Event->Bosses.Num(),0);
    return true;
}
#endif
