#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCNutBoss.h"
#include "MCNutCombatEffect.h"
#include "MCNutSpellProjectile.h"
#include "MCNutRainEvent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCTongue.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include <limits>
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCNutBossTestsPrivate
{
struct FFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCTongue* Tongue=nullptr;
    AMCNutRainEvent* Event=nullptr;
    AMCNutBoss* Boss=nullptr;
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
        const FTransform Pose(FRotator::ZeroRotator,FVector::ZeroVector,FVector(32,24,.2));
        Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Tongue->SourceMesh=Cube; Tongue->bAutomaticYawns=false; Tongue->FinishSpawning(Pose); Tongue->SetActorTickEnabled(false);
        Event=World->SpawnActor<AMCNutRainEvent>();
    }
    ~FFixture()
    {
        World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    }
    void Step(float Seconds)
    {
        for(int32 Index=0;Index<FMath::CeilToInt(Seconds*60);++Index) {++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60);}
    }
    void AddBoss(EMCNutBossRole Role=EMCNutBossRole::Tank,int32 Players=1,int32 MaxCreeps=8)
    {
        FMCNutBossSettings Settings;
        Settings.WholeMesh=Settings.ShellMesh=Settings.KernelMesh=Cube;
        Settings.TankMoveSpeed=60; Settings.MageMoveSpeed=40; Settings.MaxLiveCreeps=MaxCreeps;
        const FTransform Pose(FVector(-600,0,114));
        Boss=World->SpawnActorDeferred<AMCNutBoss>(AMCNutBoss::StaticClass(),Pose,Event,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Boss->ConfigureEncounter(Tongue,Event,Role,Settings,Players,41);
        const FTransform FallingPose(Boss->GetActorRotation(),Boss->LockedStart);
        Boss->FinishSpawning(FallingPose); Event->RegisterEncounterEnemy(Boss);
    }
    AMCToothCharacter* AddHero(FVector Point)
    {
        auto* Hero=World->SpawnActor<AMCToothCharacter>(Point,FRotator::ZeroRotator);
        Hero->GetCharacterMovement()->DisableMovement(); return Hero;
    }
    bool WaitFor(EMCNutBossAttack Attack,EMCNutBossState State,float Limit=20)
    {
        for(int32 Index=0;Index<FMath::CeilToInt(Limit*20);++Index) {
            if(Boss->Attack==Attack && Boss->State==State) return true;
            Step(.05f);
        }
        return false;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossSettingsTest,"MessControl.CoreLoop.NutBoss.SafeSettingsAndPartyHealth",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutBossSettingsTest::RunTest(const FString&)
{
    FMCNutBossSettings Settings;
    TestEqual(TEXT("Solo tank health is authored independently"),Settings.HealthForPlayers(EMCNutBossRole::Tank,1),1400.f);
    TestEqual(TEXT("Solo mage health is authored independently"),Settings.HealthForPlayers(EMCNutBossRole::Mage,1),1100.f);
    TestTrue(TEXT("A party of four has the tuned tank health"),FMath::IsNearlyEqual(Settings.HealthForPlayers(EMCNutBossRole::Tank,4),4130.f,.01f));
    TestTrue(TEXT("A party of four has the tuned mage health"),FMath::IsNearlyEqual(Settings.HealthForPlayers(EMCNutBossRole::Mage,4),3245.f,.01f));
    Settings.TankHealth=Settings.ExtraPlayerHealth=Settings.ChargeSpeed=std::numeric_limits<float>::quiet_NaN();
    Settings.ShieldFrontDamageScale=0; Settings.ChargeDamage=1000; Settings.RainDrops=MAX_int32;
    Settings.RainHitGap=0; Settings.Sanitize();
    TestTrue(TEXT("Corrupt encounter health becomes bounded and finite"),FMath::IsFinite(Settings.HealthForPlayers(EMCNutBossRole::Tank,MAX_int32)));
    TestTrue(TEXT("Directional protection always permits some real damage"),Settings.ShieldFrontDamageScale>=.15f);
    TestTrue(TEXT("One attack cannot become a default-player one-shot"),Settings.ChargeDamage<=40);
    TestTrue(TEXT("Rain damage respects a reaction-sized per-player hit interval"),Settings.RainHitGap>=.6f);
    TestTrue(TEXT("Cosmetic and server rain work remains bounded"),Settings.RainDrops<=12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossShieldTest,"MessControl.CoreLoop.NutBoss.DropAndDirectionalShield",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutBossShieldTest::RunTest(const FString&)
{
    MCNutBossTestsPrivate::FFixture T; T.AddBoss();
    TestTrue(TEXT("The falling boss is alive for objective accounting"),T.Boss->IsEncounterAlive());
    TestFalse(TEXT("An airborne entrance is not a reachable weapon target"),T.Boss->CanReceiveToolHit());
    const float Initial=T.Boss->Health;
    TestEqual(TEXT("An entrance cannot accept a forged early tool contact"),T.Boss->ReceiveToolDamage(40,nullptr),0.f);
    T.Step(1.4f);
    TestTrue(TEXT("The completed entrance opens normal weapon contact"),T.Boss->CanReceiveToolHit());
    TestEqual(TEXT("Base enemy sanitization does not truncate boss health"),T.Boss->Health,Initial);
    T.Boss->SetActorTickEnabled(false);
    auto* Hero=T.AddHero(T.Boss->GetActorLocation()+FVector(180,0,16));
    TestTrue(TEXT("A source directly ahead meets the directional shield"),T.Boss->IsShieldProtectingFrom(Hero->GetActorLocation()));
    TestTrue(TEXT("A shield still takes a real fraction of the normal hit"),FMath::IsNearlyEqual(T.Boss->ReceiveToolDamage(40,Hero),14.f));
    Hero->SetActorLocation(T.Boss->GetActorLocation()-FVector(180,0,-16));
    TestEqual(TEXT("A rear hit applies the whole tool damage"),T.Boss->ReceiveToolDamage(40,Hero),40.f);
    const float Before=T.Boss->Health;
    TestEqual(TEXT("NaN damage is rejected"),T.Boss->ReceiveToolDamage(std::numeric_limits<float>::quiet_NaN(),Hero),0.f);
    Hero->Status->Damage(1000);
    TestEqual(TEXT("A defeated source cannot deal another hit"),T.Boss->ReceiveToolDamage(40,Hero),0.f);
    TestEqual(TEXT("Rejected hits preserve authoritative health"),T.Boss->Health,Before);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossChargeTest,"MessControl.CoreLoop.NutBoss.ChargeLocksTrajectoryBeforeExecution",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutBossChargeTest::RunTest(const FString&)
{
    MCNutBossTestsPrivate::FFixture T; T.AddBoss();
    auto* Hero=T.AddHero(FVector(150,0,130));
    if(!TestTrue(TEXT("The tank schedules a visible charge windup"),T.WaitFor(EMCNutBossAttack::Charge,EMCNutBossState::Telegraph))) return false;
    const FVector Start=T.Boss->LockedStart,End=T.Boss->LockedTarget,Forward=T.Boss->AttackForward;
    const float Before=Hero->Status->State.Health;
    Hero->SetActorLocation(FVector(150,650,130));
    T.Step(.5f);
    TestEqual(TEXT("The warning endpoint does not home after the target dodges"),T.Boss->LockedTarget,End);
    TestEqual(TEXT("The attack forward remains fixed through the warning"),T.Boss->AttackForward,Forward);
    TestEqual(TEXT("A warning cannot apply charge damage early"),Hero->Status->State.Health,Before);
    if(!TestTrue(TEXT("The charge enters execution"),T.WaitFor(EMCNutBossAttack::Charge,EMCNutBossState::Executing,3))) return false;
    TestFalse(TEXT("Charge execution exposes the shield"),T.Boss->IsShieldProtectingFrom(T.Boss->GetActorLocation()+Forward*200));
    T.Step(.4f);
    TestTrue(TEXT("The moving boss follows the original straight lane"),FMath::Abs(T.Boss->GetActorLocation().Y-Start.Y)<3);
    TestEqual(TEXT("A player who left the lane avoids its hit"),Hero->Status->State.Health,Before);
    FVector Escape;
    TestFalse(TEXT("An off-lane player is not reported inside the charge threat"),T.Boss->IsPlayerInThreat(Hero,Escape));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossJumpTest,"MessControl.CoreLoop.NutBoss.JumpAreaHitsOnceAndAllowsEscape",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutBossJumpTest::RunTest(const FString&)
{
    MCNutBossTestsPrivate::FFixture T; T.AddBoss(); T.Step(8.5f);
    auto* Inside=T.AddHero(FVector(-200,0,130));
    auto* Outside=T.AddHero(FVector(-200,800,130));
    if(!TestTrue(TEXT("A random live target starts a locked jump"),T.WaitFor(EMCNutBossAttack::Jump,EMCNutBossState::Telegraph,2))) return false;
    // Put one fixture worker at the advertised point and the other outside it.
    const FVector Center=T.Boss->LockedTarget;
    Inside->SetActorLocation(Center+FVector(0,0,16)); Outside->SetActorLocation(Center+FVector(0,650,16));
    const float InsideBefore=Inside->Status->State.Health,OutsideBefore=Outside->Status->State.Health;
    T.Step(1.1f);
    TestEqual(TEXT("The advertised jump has a full damage-free warning"),Inside->Status->State.Health,InsideBefore);
    T.Step(1.05f);
    TestTrue(TEXT("Landing applies one bounded area hit"),FMath::IsNearlyEqual(Inside->Status->State.Health,InsideBefore-T.Boss->BossSettings.JumpDamage));
    TestEqual(TEXT("Leaving the advertised radius avoids the landing hit"),Outside->Status->State.Health,OutsideBefore);
    const float After=Inside->Status->State.Health; T.Step(.5f);
    TestEqual(TEXT("Recovery does not repeat the landing damage"),Inside->Status->State.Health,After);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossSummonTest,"MessControl.CoreLoop.NutBoss.SummonsRemainAfterDefeatAndResetCleansChildren",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutBossSummonTest::RunTest(const FString&)
{
    MCNutBossTestsPrivate::FFixture T; T.AddBoss(EMCNutBossRole::Mage,1,1); T.Step(11.5f);
    T.AddHero(FVector(100,0,130));
    if(!TestTrue(TEXT("The mage advertises a summon before creating enemies"),T.WaitFor(EMCNutBossAttack::Summon,EMCNutBossState::Telegraph,2))) return false;
    T.Step(1.25f);
    AMCNutEnemy* Creep=nullptr; int32 Count=0;
    for(TActorIterator<AMCNutEnemy> It(T.World);It;++It) if(!Cast<AMCNutBoss>(*It) && It->IsEncounterAlive()) {Creep=*It;++Count;}
    if(!TestEqual(TEXT("Summoning obeys the live enemy cap"),Count,1)) return false;
    TestTrue(TEXT("Summoned creeps belong to the event for reset cleanup"),Creep->GetOwner()==T.Event);
    TestTrue(TEXT("One common knife hit leaves the weak creep alive"),Creep->ReceiveToolDamage(25,nullptr)>0 && Creep->IsEncounterAlive());
    TestTrue(TEXT("The second knife hit defeats the weak creep"),Creep->ReceiveToolDamage(25,nullptr)>0 && !Creep->IsEncounterAlive());
    // A separate live child must remain an objective after the summoner dies.
    FMCNutEnemySettings Weak=T.Event->Settings.Enemy; Weak.MaxHealth=40;
    auto* Remaining=T.World->SpawnActor<AMCNutEnemy>();
    Remaining->SetActorLocation(FVector(500,500,45)); Remaining->SetOwner(T.Event);
    Remaining->ConfigureEnemy(T.Tongue,T.Cube,FVector(.7),Weak); T.Event->RegisterEncounterEnemy(Remaining);
    T.Boss->ReceiveToolDamage(100000,nullptr);
    TestTrue(TEXT("Boss defeat leaves existing creeps to finish"),Remaining->IsEncounterAlive());
    int32 Attacks=0;
    for(TActorIterator<AMCNutCombatEffect> It(T.World);It;++It) if(It->SourceActor==T.Boss && !It->IsActorBeingDestroyed()) ++Attacks;
    for(TActorIterator<AMCNutSpellProjectile> It(T.World);It;++It) if(It->SourceActor==T.Boss && !It->IsActorBeingDestroyed()) ++Attacks;
    TestEqual(TEXT("Defeat cancels owned attack actors immediately"),Attacks,0);
    T.Event->Stop();
    TestTrue(TEXT("An explicit event reset destroys the surviving creep"),Remaining->IsActorBeingDestroyed());
    return true;
}
#endif
