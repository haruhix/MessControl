#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCNutKamikaze.h"
#include "MCNutCombatEffect.h"
#include "MCNutRainEvent.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCNutKamikazeTestsPrivate
{
struct FFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCTongue* Tongue=nullptr;
    AMCNutRainEvent* Event=nullptr;
    UStaticMesh* Cube=nullptr;
    FFixture()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
        Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
        FStaticMeshCompilingManager::Get().FinishCompilation({Cube});
#endif
        const FTransform Pose(FRotator::ZeroRotator,FVector::ZeroVector,FVector(24,16,.2));
        Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Tongue->SourceMesh=Cube; Tongue->bAutomaticYawns=false;
        Tongue->FinishSpawning(Pose); Tongue->SetActorTickEnabled(false);
        Event=World->SpawnActor<AMCNutRainEvent>();
    }
    ~FFixture()
    {
        World->EndPlay(EEndPlayReason::Quit);
        GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    }
    void Step(float Seconds)
    {
        for(int32 Index=0;Index<FMath::CeilToInt(Seconds*60);++Index) {
            ++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60);
        }
    }
    AMCNutKamikaze* AddNut(float FuseSeconds=5.f,FVector Point=FVector(0,0,72))
    {
        FMCNutEnemySettings Settings;
        Settings.MaxHealth=40; Settings.MoveSpeed=40;
        const FTransform Pose(Point);
        auto* Nut=World->SpawnActorDeferred<AMCNutKamikaze>(AMCNutKamikaze::StaticClass(),Pose,Event,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Nut->ConfigureKamikaze(Tongue,Cube,FVector(.6),Settings,FuseSeconds,230,20,650);
        Nut->FinishSpawning(Pose);
        Event->RegisterEncounterEnemy(Nut);
        return Nut;
    }
    AMCToothCharacter* AddHero(FVector Point)
    {
        auto* Hero=World->SpawnActor<AMCToothCharacter>(Point,FRotator::ZeroRotator);
        Hero->GetCharacterMovement()->DisableMovement();
        Hero->SetActorTickEnabled(false); Hero->ToothPhysics->SetComponentTickEnabled(false);
        return Hero;
    }
    int32 ExplosionCount() const
    {
        int32 Count=0;
        for(TActorIterator<AMCNutCombatEffect> It(World);It;++It)
            if(It->Cue.Type==EMCNutCombatCue::FireImpact) ++Count;
        return Count;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutKamikazeFuseExpiry,
    "MessControl.CoreLoop.NutKamikaze.SpawnFuseExpiresWithoutContact",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutKamikazeFuseExpiry::RunTest(const FString&)
{
    MCNutKamikazeTestsPrivate::FFixture T;
    auto* Unconfigured=T.World->SpawnActor<AMCNutKamikaze>();
    T.Step(.2f);
    TestFalse(TEXT("BeginPlay alone cannot start an unconfigured fuse"),Unconfigured->bArmed);
    TestFalse(TEXT("An unconfigured summoned actor never detonates"),Unconfigured->bExploded);
    Unconfigured->Destroy();
    auto* Nut=T.AddNut(1.f);
    TestTrue(TEXT("The configured spawn immediately arms the fuse"),Nut->bArmed);
    TestNull(TEXT("This fuse has no player contact or target"),Nut->Target.Get());
    T.Step(.45f);
    TestTrue(TEXT("A fuse counts down before any player arrives"),Nut->GetFuseSecondsRemaining()<.65f && Nut->GetFuseSecondsRemaining()>.4f);
    TestTrue(TEXT("The nut remains killable during its countdown"),Nut->CanReceiveToolHit());
    T.Step(.65f);
    TestTrue(TEXT("The spawn timer detonates even without a target"),Nut->bExploded);
    TestFalse(TEXT("Explosion consumes the fuse and the live enemy"),Nut->bArmed || Nut->IsEncounterAlive());
    TestEqual(TEXT("Expiry produces one visible burst"),T.ExplosionCount(),1);
    T.Step(.2f);
    TestEqual(TEXT("Defeated ticks cannot repeat a blast"),T.ExplosionCount(),1);
    T.Event->Stop(); T.Step(.1f);
    TestEqual(TEXT("Event reset removes a pending summoned explosion cue immediately"),T.ExplosionCount(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutKamikazeKilledFuse,
    "MessControl.CoreLoop.NutKamikaze.KillCancelsBlastAndDamage",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutKamikazeKilledFuse::RunTest(const FString&)
{
    MCNutKamikazeTestsPrivate::FFixture T;
    auto* Hero=T.AddHero(FVector(100,0,110));
    auto* Nut=T.AddNut(1.f);
    const float Before=Hero->Status->State.Health;
    T.Step(.2f);
    TestTrue(TEXT("An armed summon can be killed with the ordinary weapon damage resolver"),Nut->ReceiveToolDamage(1000,Hero)>0);
    TestTrue(TEXT("A fatal hit immediately defeats the summon"),Nut->bDefeated);
    TestFalse(TEXT("A fatal hit immediately disarms the fuse"),Nut->bArmed);
    T.Step(1.f);
    TestFalse(TEXT("The cancelled countdown does not later detonate"),Nut->bExploded);
    TestEqual(TEXT("Killing the summon produces no damaging explosion cue"),T.ExplosionCount(),0);
    TestEqual(TEXT("A nearby player receives no damage from a neutralized summon"),Hero->Status->State.Health,Before);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutKamikazeNearestPursuit,
    "MessControl.CoreLoop.NutKamikaze.NearestLivePursuitAndTimedDamage",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutKamikazeNearestPursuit::RunTest(const FString&)
{
    MCNutKamikazeTestsPrivate::FFixture T;
    auto* Nearest=T.AddHero(FVector(200,0,110));
    auto* Farther=T.AddHero(FVector(-850,0,110));
    auto* Nut=T.AddNut(5.f);
    const FVector SpawnPoint=Nut->GetActorLocation();
    T.Step(.25f);
    TestEqual(TEXT("The summon selects the nearest living player"),Nut->Target.Get(),Nearest);
    TestTrue(TEXT("The armed summon runs towards that player before expiry"),Nut->GetActorLocation().X>SpawnPoint.X+1.f);
    Nut->ReceiveToolDamage(5,Farther); T.Step(.25f);
    TestEqual(TEXT("A distant attacker cannot replace nearest-player pursuit"),Nut->Target.Get(),Nearest);
    Nearest->Status->Damage(10000); T.Step(.25f);
    TestEqual(TEXT("A dead nearest player is replaced by the next living player"),Nut->Target.Get(),Farther);
    TestTrue(TEXT("Retargeting preserves the original running fuse"),Nut->GetFuseSecondsRemaining()<4.5f);
    Nut->ReceiveToolDamage(10000,nullptr);

    auto* BlastHero=T.AddHero(FVector(700,0,110));
    auto* BlastNut=T.AddNut(1.f,FVector(600,0,72));
    const float Before=BlastHero->Status->State.Health;
    T.Step(.5f);
    TestEqual(TEXT("Proximity never detonates early and there is no ordinary bite attack"),BlastHero->Status->State.Health,Before);
    T.Step(.65f);
    TestTrue(TEXT("A nearby summoned nut eventually detonates by time"),BlastNut->bExploded);
    TestEqual(TEXT("The timer applies its authored explosion damage once"),BlastHero->Status->State.Health,Before-20.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutKamikazeSummonGroup,
    "MessControl.CoreLoop.NutKamikaze.SummonGroupStaggersActualBlastsAndSkipsKilledSlots",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutKamikazeSummonGroup::RunTest(const FString&)
{
    {
        MCNutKamikazeTestsPrivate::FFixture T;
        auto* First=T.AddNut(1.f,FVector(-300,0,72));
        auto* Second=T.AddNut(1.f);
        auto* Third=T.AddNut(1.f,FVector(300,0,72));
        const double OriginalFirstDeadline=First->FuseExplodesAt;
        AMCNutKamikaze::StaggerSummonedGroup({First,Second,Third});
        TestEqual(TEXT("The first summon retains its already running fuse"),First->FuseExplodesAt,OriginalFirstDeadline);
        TestTrue(TEXT("Each summon has its own warning deadline"),
            Second->FuseExplodesAt-First->FuseExplodesAt>=.749 && Third->FuseExplodesAt-Second->FuseExplodesAt>=.749);
        T.Step(1.1f);
        TestTrue(TEXT("The first summon explodes first"),First->bExploded);
        TestFalse(TEXT("The remaining summons do not explode with the first"),Second->bExploded || Third->bExploded);
        const double FirstBlast=First->HitAt;
        const double SecondDeadline=Second->FuseExplodesAt;
        T.Step(.3f);
        TestEqual(TEXT("Ordinary ticks do not keep extending the next fuse"),Second->FuseExplodesAt,SecondDeadline);
        T.Step(.5f);
        TestTrue(TEXT("The second summon follows the stable group order"),Second->bExploded);
        TestFalse(TEXT("The third summon still has a running timer"),Third->bExploded);
        const double SecondBlast=Second->HitAt;
        TestTrue(TEXT("The first two actual blasts are separated by at least .75 seconds"),SecondBlast-FirstBlast>=.749);
        T.Step(.8f);
        TestTrue(TEXT("The final summon eventually detonates"),Third->bExploded);
        TestTrue(TEXT("The final actual blast retains the same minimum spacing"),Third->HitAt-SecondBlast>=.749);
    }
    {
        MCNutKamikazeTestsPrivate::FFixture T;
        auto* First=T.AddNut(1.f,FVector(-450,0,72));
        auto* Killed=T.AddNut(1.f,FVector(-150,0,72));
        auto* Third=T.AddNut(1.f,FVector(150,0,72));
        auto* Fourth=T.AddNut(1.f,FVector(450,0,72));
        AMCNutKamikaze::StaggerSummonedGroup({First,Killed,Third,Fourth});
        TestTrue(TEXT("A grouped summon remains killable before its warning expires"),Killed->ReceiveToolDamage(1000,nullptr)>0);
        TestFalse(TEXT("The killed slot is disarmed without an explosion"),Killed->bArmed || Killed->bExploded);
        // Simulate one delayed AI dispatch: the world clock advances beyond
        // every deadline while these actor ticks cannot run.
        for(auto* Nut:{First,Third,Fourth}) Nut->SetActorTickEnabled(false);
        T.Step(4.f);
        for(auto* Nut:{First,Third,Fourth}) Nut->SetActorTickEnabled(true);
        T.Step(1.f/60);
        TestTrue(TEXT("The overdue leader detonates when dispatch resumes"),First->bExploded);
        TestFalse(TEXT("One delayed dispatch cannot produce a same-frame chain explosion"),Third->bExploded || Fourth->bExploded);
        const double FirstBlast=First->HitAt;
        TestTrue(TEXT("The next live warning is rebased after the actual delayed blast"),Third->FuseExplodesAt>=FirstBlast+.749);
        const double ThirdDeadline=Third->FuseExplodesAt;
        T.Step(.4f);
        TestEqual(TEXT("Waiting after a hitch does not perpetually rebase a fuse"),Third->FuseExplodesAt,ThirdDeadline);
        TestFalse(TEXT("The next live summon survives until its own deadline"),Third->bExploded);
        T.Step(.4f);
        TestTrue(TEXT("An early-killed queue slot cannot deadlock later summons"),Third->bExploded);
        TestFalse(TEXT("The final overdue summon does not share the next blast"),Fourth->bExploded);
        const double ThirdBlast=Third->HitAt;
        TestTrue(TEXT("Skipping a dead slot preserves actual blast spacing"),ThirdBlast-FirstBlast>=.749);
        T.Step(.8f);
        TestTrue(TEXT("The final overdue summon still completes its timer"),Fourth->bExploded);
        TestTrue(TEXT("The final delayed blast is also separated"),Fourth->HitAt-ThirdBlast>=.749);
    }
    return true;
}
#endif
