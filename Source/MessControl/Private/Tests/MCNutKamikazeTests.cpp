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
#endif
