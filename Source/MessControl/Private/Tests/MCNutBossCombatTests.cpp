#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCNutBoss.h"
#include "MCNutBossAnimInstance.h"
#include "MCArenaTooth.h"
#include "MCNutCombatEffect.h"
#include "MCCombatFeedbackComponent.h"
#include "MCNutSpellProjectile.h"
#include "MCNutKamikaze.h"
#include "MCFoodActor.h"
#include "MCNutRainEvent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCTongue.h"
#include "MCMouthSurface.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Animation/AnimSequence.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "ProceduralMeshComponent.h"
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
    TestEqual(TEXT("The default shield break opens a five-second damage window"),Settings.TankStunSeconds,5.f);
    TestTrue(TEXT("One attack cannot become a default-player one-shot"),Settings.ChargeDamage<=40);
    TestTrue(TEXT("Rain damage respects a reaction-sized per-player hit interval"),Settings.RainHitGap>=.6f);
    TestTrue(TEXT("Cosmetic and server rain work remains bounded"),Settings.RainDrops<=12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossShieldTest,"MessControl.CoreLoop.NutBoss.DropAndShieldFirstHealth",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
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
    TestTrue(TEXT("An intact shield protects against a frontal source"),T.Boss->IsShieldProtectingFrom(Hero->GetActorLocation()));
    TestEqual(TEXT("A shielded hit cannot damage boss HP"),T.Boss->ReceiveToolDamage(40,Hero),0.f);
    TestEqual(TEXT("The same hit damages the shield's own durability"),T.Boss->ShieldHealth,460.f);
    TestEqual(TEXT("Shield-only contact still produces accepted combat feedback"),T.Boss->Feedback->LastHit.ShieldDamage,40.f);
    TestEqual(TEXT("Shield feedback reports no boss health damage"),T.Boss->Feedback->LastHit.HealthDamage,0.f);
    Hero->SetActorLocation(T.Boss->GetActorLocation()-FVector(180,0,-16));
    TestTrue(TEXT("The shield also protects against a rear source"),T.Boss->IsShieldProtectingFrom(Hero->GetActorLocation()));
    TestEqual(TEXT("A rear hit cannot bypass shield durability"),T.Boss->ReceiveToolDamage(40,Hero),0.f);
    TestEqual(TEXT("A rear hit removes the same shield durability"),T.Boss->ShieldHealth,420.f);
    for(const float Side:{-1.f,1.f}) {
        Hero->SetActorLocation(T.Boss->GetActorLocation()+FVector(0,Side*180,16));
        TestTrue(TEXT("Either side is protected by the intact shield"),T.Boss->IsShieldProtectingFrom(Hero->GetActorLocation()));
        TestEqual(TEXT("Side hits cannot bypass the shield"),T.Boss->ReceiveToolDamage(40,Hero),0.f);
    }
    Hero->SetActorLocation(T.Boss->GetActorLocation()+FVector(180,0,16));
    for(const auto State:{EMCNutBossState::Telegraph,EMCNutBossState::Executing,EMCNutBossState::Recovery}) {
        T.Boss->State=State; T.Boss->Attack=EMCNutBossAttack::Melee;
        TestTrue(TEXT("Attack and recovery poses retain shield protection"),T.Boss->IsShieldProtectingFrom(Hero->GetActorLocation()));
        TestEqual(TEXT("Attacking and recovery cannot bypass shield durability"),T.Boss->ReceiveToolDamage(40,Hero),0.f);
    }
    const float BeforeAreaShield=T.Boss->ShieldHealth;
    TestEqual(TEXT("Source-less area damage also belongs to the shield"),T.Boss->ReceiveToolDamage(40,nullptr),0.f);
    TestEqual(TEXT("Area damage removes ordinary shield durability"),T.Boss->ShieldHealth,BeforeAreaShield-40.f);
    TestEqual(TEXT("No protected direction or combat pose removed HP"),T.Boss->Health,Initial);
    T.Boss->State=EMCNutBossState::Idle; T.Boss->ShieldHealth=10;
    TestEqual(TEXT("The breaking hit discards excess damage instead of spilling into HP"),T.Boss->ReceiveToolDamage(40,Hero),0.f);
    TestEqual(TEXT("The shield is now broken"),T.Boss->ShieldHealth,0.f);
    TestEqual(TEXT("Breaking the shield preserves the knight's full HP"),T.Boss->Health,Initial);
    TestEqual(TEXT("Shield break enters the existing stun state"),T.Boss->State,EMCNutBossState::Stunned);
    TestEqual(TEXT("The interrupted attack is cleared"),T.Boss->Attack,EMCNutBossAttack::None);
    TestEqual(TEXT("The damage window lasts five seconds"),float(T.Boss->AttackEndAt-T.Boss->StateStartedAt),5.f);
    const double StunEndsAt=T.Boss->AttackEndAt;
    TestEqual(TEXT("The next front hit is no longer protected"),T.Boss->ReceiveToolDamage(40,Hero),40.f);
    TestEqual(TEXT("Hits during stun do not prolong the five-second window"),T.Boss->AttackEndAt,StunEndsAt);
    const float Before=T.Boss->Health;
    TestEqual(TEXT("NaN damage is rejected"),T.Boss->ReceiveToolDamage(std::numeric_limits<float>::quiet_NaN(),Hero),0.f);
    Hero->Status->Damage(1000);
    TestEqual(TEXT("A defeated source cannot deal another hit"),T.Boss->ReceiveToolDamage(40,Hero),0.f);
    TestEqual(TEXT("Rejected hits preserve authoritative health"),T.Boss->Health,Before);
    T.Boss->SetActorTickEnabled(true); T.Step(4.9f);
    TestEqual(TEXT("The knight remains stunned until the window ends"),T.Boss->State,EMCNutBossState::Stunned);
    T.Step(.2f);
    TestTrue(TEXT("The knight leaves stun after five seconds"),T.Boss->State!=EMCNutBossState::Stunned);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossTeamCounterTest,"MessControl.CoreLoop.NutBoss.TeamPressureCounterAndShieldPriority",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutBossTeamCounterTest::RunTest(const FString&)
{
    MCNutBossTestsPrivate::FFixture T;T.AddBoss();T.Step(1.4f);
    T.Boss->SetActorTickEnabled(false);
    T.Boss->BossSettings.TankCounterHitsMin=T.Boss->BossSettings.TankCounterHitsMax=10;
    T.Boss->ShieldHealth=10000;
    auto* Left=T.AddHero(T.Boss->GetActorLocation()+FVector(260,0,0));
    auto* Right=T.AddHero(T.Boss->GetActorLocation()+FVector(-260,0,0));
    auto* Far=T.AddHero(T.Boss->GetActorLocation()+FVector(1000,0,0));
    for(int32 I=0;I<9;++I) T.Boss->ReceiveToolDamage(1,I%2?Right:Left);
    TestEqual(TEXT("Team contacts share one pressure chain"),T.Boss->GetTankPressureHits(),9);
    TestFalse(TEXT("Nine contacts do not trigger an early counter"),T.Boss->IsCounterShoving());
    T.Boss->ReceiveToolDamage(0,Left);T.Boss->ReceiveToolDamage(1,nullptr);
    TestEqual(TEXT("Rejected hits and source-less damage are not extra team contacts"),T.Boss->GetTankPressureHits(),9);
    T.Boss->ReceiveToolDamage(1,Right);
    TestEqual(TEXT("The tenth team contact starts the counter warning"),T.Boss->Attack,EMCNutBossAttack::CounterShove);
    TestEqual(TEXT("A fresh counter clears the team chain"),T.Boss->GetTankPressureHits(),0);
    const double Resolve=T.Boss->ResolveAt,End=T.Boss->AttackEndAt;
    const float Shield=T.Boss->ShieldHealth;
    for(int32 I=0;I<15;++I) T.Boss->ReceiveToolDamage(1,I%2?Right:Left);
    TestEqual(TEXT("The committed counter still accepts ordinary shield damage"),T.Boss->ShieldHealth,Shield-15.f);
    TestEqual(TEXT("Hits cannot restart the counter warning"),T.Boss->ResolveAt,Resolve);
    TestEqual(TEXT("Hits cannot extend the counter phase"),T.Boss->AttackEndAt,End);
    TestEqual(TEXT("Hits during a counter cannot prime a second counter"),T.Boss->GetTankPressureHits(),0);
    const float NearHealth=Left->Status->State.Health,FarHealth=Far->Status->State.Health;
    T.Boss->SetActorTickEnabled(true);T.Step(.48f);
    TestEqual(TEXT("The counter deals one small close-range hit"),Left->Status->State.Health,NearHealth-T.Boss->BossSettings.TankCounterDamage);
    TestEqual(TEXT("The counter does not hit distant players"),Far->Status->State.Health,FarHealth);
    const float AfterPush=Left->Status->State.Health;T.Step(.15f);
    TestEqual(TEXT("A counter does not damage a worker every frame"),Left->Status->State.Health,AfterPush);
    // Breaking the shield overrides even a committed counter and guarantees
    // the full vulnerable interval, regardless of continued team pressure.
    T.Boss->SetActorTickEnabled(false);T.Boss->ShieldHealth=1;
    T.Boss->ReceiveToolDamage(2,Far);
    TestEqual(TEXT("Shield break interrupts the counter for its guaranteed stun"),T.Boss->State,EMCNutBossState::Stunned);
    TestEqual(TEXT("Shield break clears the counter action"),T.Boss->Attack,EMCNutBossAttack::None);
    TestEqual(TEXT("The shield window is never shortened by a counter"),float(T.Boss->AttackEndAt-T.Boss->StateStartedAt),5.f);
    const double StunEnd=T.Boss->AttackEndAt;
    for(int32 I=0;I<15;++I) T.Boss->ReceiveToolDamage(1,Far);
    TestEqual(TEXT("Stun-window hits do not build anti-stun pressure"),T.Boss->GetTankPressureHits(),0);
    TestEqual(TEXT("Stun-window hits do not restart its clock"),T.Boss->AttackEndAt,StunEnd);
    T.Boss->SetActorTickEnabled(true);T.Step(4.9f);
    TestEqual(TEXT("The knight grants the whole five-second window"),T.Boss->State,EMCNutBossState::Stunned);
    T.Step(.2f);TestFalse(TEXT("The counter does not queue from damage during stun"),T.Boss->IsCounterShoving());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossPressureGapTest,"MessControl.CoreLoop.NutBoss.PressureGapResetsTeamChain",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutBossPressureGapTest::RunTest(const FString&)
{
    MCNutBossTestsPrivate::FFixture T;T.AddBoss();T.Step(1.4f);T.Boss->SetActorTickEnabled(false);
    auto* Hero=T.AddHero(T.Boss->GetActorLocation()+FVector(260,0,0));
    for(int32 I=0;I<9;++I) T.Boss->ReceiveToolDamage(1,Hero);
    T.Step(T.Boss->BossSettings.TankCounterHitGap+.1f);
    T.Boss->ReceiveToolDamage(1,Hero);
    TestEqual(TEXT("A quiet gap starts a new chain rather than banking old hits"),T.Boss->GetTankPressureHits(),1);
    TestFalse(TEXT("An interrupted chain does not trigger the counter"),T.Boss->IsCounterShoving());
    T.Boss->BossSettings.TankCounterHitsMin=T.Boss->BossSettings.TankCounterHitsMax=10;
    // Begin a fresh deterministic chain after the previous one has expired.
    T.Step(T.Boss->BossSettings.TankCounterHitGap+.1f);
    Hero->SetActorLocation(T.Boss->GetActorLocation()+FVector(1000,0,0));
    for(int32 I=0;I<10;++I) T.Boss->ReceiveToolDamage(1,Hero);
    TestTrue(TEXT("A complete fresh chain triggers one counter"),T.Boss->IsCounterShoving());
    T.Boss->SetActorTickEnabled(true);T.Step(1.3f);T.Boss->SetActorTickEnabled(false);
    TestFalse(TEXT("The short counter has finished its recovery"),T.Boss->IsCounterShoving());
    for(int32 I=0;I<15;++I) T.Boss->ReceiveToolDamage(1,Hero);
    TestEqual(TEXT("The post-counter cooldown cannot bank another team chain"),T.Boss->GetTankPressureHits(),0);
    TestFalse(TEXT("The post-counter cooldown prevents repeated immediate pushes"),T.Boss->IsCounterShoving());
    FMCNutBossSettings Settings;Settings.TankCounterHitsMin=0;Settings.TankCounterHitsMax=MAX_int32;
    Settings.TankCounterDamage=1000;Settings.TankCounterWindup=0;Settings.Sanitize();
    TestEqual(TEXT("Pressure thresholds stay within the promised range"),Settings.TankCounterHitsMin,10);
    TestEqual(TEXT("Pressure thresholds cannot grow beyond twelve"),Settings.TankCounterHitsMax,12);
    TestTrue(TEXT("The close counter remains a small hit with a visible warning"),Settings.TankCounterDamage<=12 && Settings.TankCounterWindup>=.3f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossRollTest,"MessControl.CoreLoop.NutBoss.RollDurationAndPredictableSteering",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutBossRollTest::RunTest(const FString&)
{
    MCNutBossTestsPrivate::FFixture T; T.AddBoss();
    auto* Hero=T.AddHero(FVector(150,0,130));
    if(!TestTrue(TEXT("The knight schedules a visible rolling windup"),T.WaitFor(EMCNutBossAttack::Roll,EMCNutBossState::Telegraph,25))) return false;
    bool MatchingRollTell=false;
    for(TActorIterator<AMCNutCombatEffect> It(T.World);It;++It)
        if(It->SourceActor==T.Boss && It->Cue.Type==EMCNutCombatCue::RollTell)
            MatchingRollTell|=FMath::IsNearlyEqual(It->Cue.Radius,T.Boss->BossSettings.RollDamageRadius(T.Boss->Settings.BodyRadius));
    TestTrue(TEXT("The visible roll warning uses the same enlarged radius as damage"),MatchingRollTell);
    const FVector End=T.Boss->LockedTarget,Forward=T.Boss->AttackForward;
    const float Before=Hero->Status->State.Health;
    Hero->SetActorLocation(FVector(150,650,130));
    T.Step(.5f);
    TestEqual(TEXT("The warning endpoint does not home after the target dodges"),T.Boss->LockedTarget,End);
    TestEqual(TEXT("The attack forward remains fixed through the warning"),T.Boss->AttackForward,Forward);
    TestEqual(TEXT("A warning cannot apply rolling damage early"),Hero->Status->State.Health,Before);
    if(!TestTrue(TEXT("The roll enters execution"),T.WaitFor(EMCNutBossAttack::Roll,EMCNutBossState::Executing,3))) return false;
    const double Duration=T.Boss->AttackEndAt-T.Boss->StateStartedAt;
    TestTrue(TEXT("The roll lasts twelve to twenty seconds"),Duration>=12 && Duration<=20);
    TestTrue(TEXT("Rolling cannot bypass an intact shield"),T.Boss->IsShieldProtectingFrom(T.Boss->GetActorLocation()+Forward*200));
    const float BeforeTurn=T.Boss->AttackForward.Rotation().Yaw;
    Hero->SetActorLocation(T.Boss->GetActorLocation()+FVector(0,500,16));
    T.Step(1.f/60);
    TestTrue(TEXT("Pursuit turns at the authored angular speed, without snapping"),
        FMath::Abs(FMath::FindDeltaAngleDegrees(BeforeTurn,T.Boss->AttackForward.Rotation().Yaw))<=T.Boss->BossSettings.RollTurnDegreesPerSecond/60.f+.01f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossRollPressureTest,"MessControl.CoreLoop.NutBoss.RollSpeedAndExpandedDamageRadius",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutBossRollPressureTest::RunTest(const FString&)
{
    MCNutBossTestsPrivate::FFixture T;T.AddBoss();T.Step(1.4f);
    const float BodyRadius=T.Boss->Body->GetScaledSphereRadius();
    const float OldDamageRadius=T.Boss->Settings.BodyRadius+15.f;
    const float Radius=T.Boss->BossSettings.RollDamageRadius(T.Boss->Settings.BodyRadius);
    TestTrue(TEXT("The roll's radius increases by twenty percent, not its area"),FMath::IsNearlyEqual(Radius,OldDamageRadius*1.2f));
    TestEqual(TEXT("The native roll speed is thirty percent above the previous 520 cm/s"),T.Boss->BossSettings.RollSpeed,676.f);
    const FVector Start=T.Boss->GetActorLocation();
    auto* Aim=T.AddHero(Start+FVector(1000,0,16));
    auto* Inside=T.AddHero(Start+FVector(5,700,16));
    auto* Outside=T.AddHero(Start+FVector(5,-700,16));
    const float HeroRadius=Inside->GetCapsuleComponent()->GetScaledCapsuleRadius();
    Inside->SetActorLocation(Start+FVector(5,(OldDamageRadius+Radius)*.5f+HeroRadius,16));
    Outside->SetActorLocation(Start+FVector(5,-Radius-HeroRadius-12.f,16));
    const float BeforeInside=Inside->Status->State.Health,BeforeOutside=Outside->Status->State.Health;
    T.Boss->Target=Aim;
    T.Boss->Attack=EMCNutBossAttack::Roll;T.Boss->State=EMCNutBossState::Executing;
    T.Boss->AttackForward=FVector::ForwardVector;T.Boss->AttackEndAt=T.World->GetTimeSeconds()+12;
    T.Step(1.f/60.f);
    TestTrue(TEXT("Actual roll movement uses the increased speed"),FMath::IsNearlyEqual(
        float(FVector::Dist2D(Start,T.Boss->GetActorLocation())),T.Boss->BossSettings.RollSpeed/60.f,.15f));
    TestEqual(TEXT("A player in the newly added annulus takes the actual rolling hit"),Inside->Status->State.Health,BeforeInside-T.Boss->BossSettings.RollDamage);
    TestEqual(TEXT("A player beyond the enlarged radius takes no rolling damage"),Outside->Status->State.Health,BeforeOutside);
    TestEqual(TEXT("The physical body is not enlarged to implement this attack"),T.Boss->Body->GetScaledSphereRadius(),BodyRadius);
    TestEqual(TEXT("The authored turn rate remains unchanged"),T.Boss->BossSettings.RollTurnDegreesPerSecond,50.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossRollToothTest,"MessControl.CoreLoop.NutBoss.RollCollisionDamagesArenaTeeth",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutBossRollToothTest::RunTest(const FString&)
{
    MCNutBossTestsPrivate::FFixture T; T.AddBoss(); T.Step(1.4f);
    auto* Hero=T.AddHero(FVector(900,0,130));
    auto AddTooth=[&](int32 Id,float Y) {
        const FTransform Pose(FVector(0,Y,145));
        auto* Tooth=T.World->SpawnActorDeferred<AMCArenaTooth>(AMCArenaTooth::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FMCArenaToothSettings Settings; Settings.InitialCalculusEveryNthTooth=0;
        Tooth->SetAppearance(T.Cube,FVector(2,3,3)); Tooth->Initialize(Id,Settings); Tooth->FinishSpawning(Pose);
        return Tooth;
    };
    auto* Tooth=AddTooth(1,0); auto* Other=AddTooth(2,650);
    auto AimAt=[&](AMCArenaTooth* Target) {
        const FVector Center=Target->GetActorLocation();
        T.Boss->SetActorLocation(FVector(-Target->Body->GetScaledBoxExtent().X-T.Boss->Settings.BodyRadius-5,Center.Y,T.Boss->Settings.BodyRadius+14));
        T.Boss->AttackForward=FVector::ForwardVector;
        Hero->SetActorLocation(FVector(900,Center.Y,130)); T.Boss->Target=Hero;
    };
    T.Boss->Attack=EMCNutBossAttack::Roll; T.Boss->State=EMCNutBossState::Telegraph;
    T.Boss->ResolveAt=T.World->GetTimeSeconds()+.5;
    AimAt(Tooth); T.Step(.3f);
    TestEqual(TEXT("The rolling warning cannot damage a nearby tooth"),Tooth->State.Health,100.f);
    T.Step(.35f);
    if(!TestEqual(TEXT("A real blocking roll sweep damages the struck tooth once"),Tooth->State.Health,100-T.Boss->BossSettings.RollDamage)) return false;
    TestEqual(TEXT("The damage enters the ordinary chipped stage"),Tooth->DamageStage(),EMCArenaToothDamageStage::Chipped);
    TestEqual(TEXT("Teeth outside the collision path keep their health"),Other->State.Health,100.f);
    const float AfterFirst=Tooth->State.Health;
    AimAt(Tooth); T.Step(.05f);
    AimAt(Tooth); T.Step(.05f);
    TestEqual(TEXT("Repeated collision substeps cannot drain HP every frame"),Tooth->State.Health,AfterFirst);
    AimAt(Other); T.Step(.05f);
    TestEqual(TEXT("Another tooth has an independent contact cooldown"),Other->State.Health,100-T.Boss->BossSettings.RollDamage);
    T.Boss->SetActorTickEnabled(false); T.Step(T.Boss->BossSettings.RollHitGap+.05f); T.Boss->SetActorTickEnabled(true);
    AimAt(Tooth); T.Step(.05f);
    TestEqual(TEXT("A later roll collision can damage the same tooth again"),Tooth->State.Health,AfterFirst-T.Boss->BossSettings.RollDamage);
    const float BeforeRecovery=Tooth->State.Health;
    T.Boss->State=EMCNutBossState::Recovery; T.Boss->AttackEndAt=T.World->GetTimeSeconds()+3;
    AimAt(Tooth); T.Step(.2f);
    TestEqual(TEXT("Recovery contact cannot apply rolling damage"),Tooth->State.Health,BeforeRecovery);
    Tooth->ReceiveArenaHit(Tooth->State.Health-1,FVector::ForwardVector);
    T.Boss->SetActorTickEnabled(false); T.Step(T.Boss->BossSettings.RollHitGap+.05f); T.Boss->SetActorTickEnabled(true);
    T.Boss->State=EMCNutBossState::Executing; T.Boss->AttackEndAt=T.World->GetTimeSeconds()+3;
    AimAt(Tooth); T.Step(.05f);
    TestEqual(TEXT("A lethal roll collision uses normal tooth loss"),Tooth->DamageStage(),EMCArenaToothDamageStage::Lost);
    TestFalse(TEXT("A tooth knocked out by the boss is no longer a respawn reserve"),Tooth->IsAvailable());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutWizardPressureTest,"MessControl.CoreLoop.NutBoss.WizardPressureWaitsForFullCastAndTurnsSmoothly",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutWizardPressureTest::RunTest(const FString&)
{
    MCNutBossTestsPrivate::FFixture T;T.AddBoss(EMCNutBossRole::Mage);T.Step(1.4f);
    T.Event->Stage=EMCNutRainStage::Enemies;T.Event->Settings.bBossEncounter=true;
    T.Event->Bosses.AddUnique(T.Boss);
    T.Event->SetActorTickEnabled(true);
    auto* Hero=T.AddHero(T.Boss->GetActorLocation()+FVector(-800,0,16));T.Boss->Target=Hero;
    T.Boss->NextMeleeAt=T.Boss->NextFireballAt=T.Boss->NextRainAt=T.Boss->NextSummonAt=T.Boss->NextTeleportAt=T.Boss->NextSpecialAt=10000;
    const float OldYaw=T.Boss->GetActorRotation().Yaw;T.Step(.05f);
    const float Turn=FMath::Abs(FMath::FindDeltaAngleDegrees(OldYaw,T.Boss->GetActorRotation().Yaw));
    TestTrue(TEXT("A target behind the wizard causes a bounded turn over time"),Turn>0 && Turn<=T.Boss->BossSettings.MageTurnDegreesPerSecond*.05f+.1f);
    Hero->SetActorLocation(FVector(800,0,130));T.Boss->NextFireballAt=T.Boss->NextSpecialAt=0;T.Boss->NextTeleportAt=0;
    if(!TestTrue(TEXT("A scheduled fire-nut first enters its native teleport"),T.WaitFor(EMCNutBossAttack::Teleport,EMCNutBossState::Telegraph,2))) return false;
    if(!TestTrue(TEXT("Native AI starts fire-nut after the teleport"),T.WaitFor(EMCNutBossAttack::Fireball,EMCNutBossState::Telegraph,6))) return false;
    // Re-arm the second evasive blink only for this bounded pressure fixture.
    T.Boss->NextTeleportAt=0;
    const double Resolve=T.Boss->ResolveAt;
    T.Boss->ReceiveToolDamage(5,Hero);T.Step(.35f);T.Boss->ReceiveToolDamage(5,Hero);T.Step(.35f);T.Boss->ReceiveToolDamage(5,Hero);
    TestEqual(TEXT("Sustained damage does not restart or interrupt the cast"),T.Boss->ResolveAt,Resolve);
    TestEqual(TEXT("The interrupted hit reaction waits behind the committed cast"),T.Boss->Attack,EMCNutBossAttack::Fireball);
    if(!TestTrue(TEXT("Fire-nut resolves into its authored exit"),T.WaitFor(EMCNutBossAttack::Fireball,EMCNutBossState::Recovery,2))) return false;
    const float Exit=T.Boss->BossSettings.MageCastAnimationSeconds*(1-T.Boss->BossSettings.MageCastReleaseFraction);
    TestTrue(TEXT("Recovery includes every remaining source frame and a separate blend"),T.Boss->AttackEndAt-T.Boss->StateStartedAt>=Exit+T.Boss->BossSettings.AnimationBlendSeconds-.001);
    T.Step(Exit-.05f);TestEqual(TEXT("The late source frames cannot be cut short by teleport"),T.Boss->Attack,EMCNutBossAttack::Fireball);
    if(!TestTrue(TEXT("The real encounter wizard later teleports under sustained damage"),T.WaitFor(EMCNutBossAttack::Teleport,EMCNutBossState::Telegraph,7))) return false;
    const FVector Before=T.Boss->GetActorLocation();T.Step(T.Boss->BossSettings.MageTeleportWindup-.1f);
    TestTrue(TEXT("The full teleport windup holds the advertised departure point"),T.Boss->GetActorLocation().Equals(Before,.1f));
    T.Step(.2f);TestTrue(TEXT("Teleport now reaches the increased minimum distance"),
        FVector::Dist2D(Before,T.Boss->GetActorLocation())>=T.Boss->BossSettings.MageTeleportMinDistance-.1f);
    TestEqual(TEXT("Arrival retains the authored teleport recovery"),T.Boss->State,EMCNutBossState::Recovery);
    const double Arrival=T.Boss->StateStartedAt;
    T.Boss->NextRainAt=T.Boss->NextSummonAt=T.Boss->NextSpecialAt=0;
    if(!TestTrue(TEXT("The next attack after a completed teleport is fire-nut even when other spells are ready"),
        T.WaitFor(EMCNutBossAttack::Fireball,EMCNutBossState::Telegraph,4))) return false;
    TestTrue(TEXT("Fire-nut waits until the complete teleport exit and blend have played"),
        T.Boss->StateStartedAt-Arrival>=T.Boss->BossSettings.MageTeleportAnimationSeconds*(1-T.Boss->BossSettings.MageCastReleaseFraction)+T.Boss->BossSettings.AnimationBlendSeconds-.05);
    T.Boss->ReceiveToolDamage(100000,Hero);
    T.Step(3.2f);
    TestTrue(TEXT("The final boss remains visible through its full death performance"),IsValid(T.Boss));
    TestEqual(TEXT("Victory cleanup waits for the death clip instead of destroying it early"),T.Event->Stage,EMCNutRainStage::Enemies);
    T.Step(.5f);
    TestEqual(TEXT("The encounter completes after the finished death pose and fade"),T.Event->Stage,EMCNutRainStage::Complete);
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
    if(!TestEqual(TEXT("Landing retains the native jump recovery"),T.Boss->State,EMCNutBossState::Recovery)) return false;
    const auto& Settings=T.Boss->BossSettings;
    auto* JumpClip=Settings.TankJumpAnimation.LoadSynchronous();
    if(!TestNotNull(TEXT("The recovery retains an authored jump clip"),JumpClip)) return false;
    TestEqual(TEXT("Authority jump seconds match the authored source clip"),Settings.TankJumpAnimationSeconds,JumpClip->GetPlayLength(),.001f);
    // Landing no longer truncates the animation to the prototype's one-second
    // window: every remaining source frame and one blend interval must play.
    const double ExpectedRecovery=JumpClip->GetPlayLength()*(1-Settings.TankJumpImpactFraction)+Settings.TankAnimationBlendSeconds;
    TestEqual(TEXT("Jump recovery includes exactly the remaining authored frames and blend"),
        T.Boss->AttackEndAt-T.Boss->StateStartedAt,ExpectedRecovery,.001);
    FMCNutBossAnimationSnapshot Snapshot;
    T.Boss->BuildAnimationSnapshot(T.Boss->TankModel.Get(),Snapshot);
    const auto* JumpSample=Snapshot.Samples.FindByPredicate([JumpClip](const auto& Sample){return Sample.Clip==JumpClip;});
    if(!TestNotNull(TEXT("The actual recovery snapshot plays the same jump clip"),JumpSample)) return false;
    const float ExpectedClipTime=FMath::Min(JumpClip->GetPlayLength(),
        JumpClip->GetPlayLength()*Settings.TankJumpImpactFraction+float(T.World->GetTimeSeconds()-T.Boss->StateStartedAt));
    TestEqual(TEXT("The recovery advances continuously from the authored landing frame"),JumpSample->Seconds,ExpectedClipTime,.001f);
    T.Step(FMath::Max(0.f,float(T.Boss->AttackEndAt-T.World->GetTimeSeconds())-.1f));
    TestEqual(TEXT("The remaining source frames cannot be interrupted by the next attack"),T.Boss->State,EMCNutBossState::Recovery);
    TestEqual(TEXT("The complete recovery never repeats the landing damage"),Inside->Status->State.Health,After);
    T.Step(.15f);
    TestFalse(TEXT("The jump releases recovery after its authored exit and blend"),T.Boss->State==EMCNutBossState::Recovery);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossComboTest,"MessControl.CoreLoop.NutBoss.ComboGroundHitAndTwoSecondRecovery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutBossComboTest::RunTest(const FString&)
{
    MCNutBossTestsPrivate::FFixture T; T.AddBoss(); T.Step(1.4f);
    T.Boss->NextJumpAt=T.Boss->NextRollAt=T.Boss->NextSpecialAt=10000;
    T.Boss->NextMeleeAt=0;
    auto* Hero=T.AddHero(T.Boss->GetActorLocation()+FVector(300,0,16));
    const float Before=Hero->Status->State.Health;
    // The native sequence is now Attack1, Attack2, Combo. The old fixture
    // treated its first single swing as the entire three-contact combo.
    for(uint8 Variant=0;Variant<3;++Variant) {
        Hero->SetActorLocation(T.Boss->GetActorLocation()+T.Boss->GetActorForwardVector()*300+FVector(0,0,16));
        if(!TestTrue(TEXT("Close range starts the next native melee warning"),
            T.WaitFor(EMCNutBossAttack::Melee,EMCNutBossState::Telegraph,8))) return false;
        if(!TestEqual(TEXT("The native melee sequence reaches both swings then the combo"),T.Boss->TankMeleeVariant,Variant)) return false;
        if(Variant==2) break;
        Hero->SetActorLocation(FVector(1300,900,130));
        if(!TestTrue(TEXT("The preceding authored single swing reaches recovery"),
            T.WaitFor(EMCNutBossAttack::Melee,EMCNutBossState::Recovery,3))) return false;
        if(!TestTrue(TEXT("The preceding single swing completes before the next warning"),
            T.WaitFor(EMCNutBossAttack::None,EMCNutBossState::Idle,5))) return false;
    }
    const FVector ExpectedSlam=T.Boss->LockedStart+T.Boss->AttackForward*T.Boss->BossSettings.MeleeRange*.55f;
    Hero->SetActorLocation(FVector(1300,900,130)); // Exercise the advertised dodge before any contact.
    if(!TestTrue(TEXT("All three authored contacts complete before combo recovery"),
        T.WaitFor(EMCNutBossAttack::Melee,EMCNutBossState::Recovery,4))) return false;
    TestEqual(TEXT("The complete combo enters recovery"),T.Boss->State,EMCNutBossState::Recovery);
    const auto& Settings=T.Boss->BossSettings;
    auto* ComboClip=Settings.TankComboAnimation.LoadSynchronous();
    if(!TestNotNull(TEXT("The combo recovery retains its authored source clip"),ComboClip)) return false;
    TestEqual(TEXT("Authority combo seconds match the authored source clip"),Settings.TankComboAnimationSeconds,ComboClip->GetPlayLength(),.001f);
    // Execution retains .1 seconds after the final contact; recovery owns the
    // rest of this exact clip plus its blend, replacing the old fixed two seconds.
    const double ExpectedRecovery=FMath::Max(.01f,ComboClip->GetPlayLength()*(1-float(Settings.TankComboContactFractions.Z))-.1f)
        +Settings.TankAnimationBlendSeconds;
    TestEqual(TEXT("The combo recovery exactly matches its source exit and blend"),
        T.Boss->AttackEndAt-T.Boss->StateStartedAt,ExpectedRecovery,.001);
    FMCNutBossAnimationSnapshot Snapshot;
    T.Boss->BuildAnimationSnapshot(T.Boss->TankModel.Get(),Snapshot);
    TestNotNull(TEXT("The actual recovery snapshot retains the combo clip"),
        Snapshot.Samples.FindByPredicate([ComboClip](const auto& Sample){return Sample.Clip==ComboClip;}));
    TestEqual(TEXT("Leaving the locked combo footprint avoids all its hits"),Hero->Status->State.Health,Before);
    bool GroundImpact=false;
    for(TActorIterator<AMCNutCombatEffect> It(T.World);It;++It)
        if(It->SourceActor==T.Boss && It->Cue.Type==EMCNutCombatCue::SlamImpact)
            GroundImpact|=FVector::DistSquared(It->Cue.Target,ExpectedSlam)<1.f;
    TestTrue(TEXT("The last blow's ground VFX shares the actual damage centre"),GroundImpact);
    T.Step(FMath::Max(0.f,float(T.Boss->AttackEndAt-T.World->GetTimeSeconds())-.1f));
    TestEqual(TEXT("The knight remains inactive throughout the damage window"),T.Boss->State,EMCNutBossState::Recovery);
    TestEqual(TEXT("A dodging worker stays unharmed through every swing and the complete combo"),Hero->Status->State.Health,Before);
    T.Step(.15f);
    TestFalse(TEXT("The combo releases recovery after the source exit and blend"),T.Boss->State==EMCNutBossState::Recovery);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossSummonTest,"MessControl.CoreLoop.NutBoss.SummonsRemainAfterDefeatAndResetCleansChildren",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutBossSummonTest::RunTest(const FString&)
{
    MCNutBossTestsPrivate::FFixture T; T.AddBoss(EMCNutBossRole::Mage,1,1); T.Step(11.5f);
    for(int32 Index=0;Index<3;++Index) {
        auto* Nut=T.World->SpawnActor<AMCFoodActor>(FVector(-200+Index*180,350,100),FRotator::ZeroRotator);
        FMCFoodRow Row; Row.WholeMeshes={T.Cube}; Row.FragmentMeshes={T.Cube}; Row.Resistance=EMCFoodResistance::Hard;
        FRandomStream Random(Index+1); Nut->ConfigureItem(TEXT("Walnut"),Row,Random);
        Nut->Tags.AddUnique(TEXT("MCNutRain")); Nut->SetOwner(T.Event); Nut->Phase=EMCFoodPhase::Free;
        T.Event->Nuts.Add(Nut);
    }
    T.AddHero(FVector(100,0,130));
    if(!TestTrue(TEXT("The mage advertises a summon before creating enemies"),T.WaitFor(EMCNutBossAttack::Summon,EMCNutBossState::Telegraph,2))) return false;
    T.Step(1.25f);
    AMCNutEnemy* Creep=nullptr; int32 Count=0;
    for(TActorIterator<AMCNutEnemy> It(T.World);It;++It) if(!Cast<AMCNutBoss>(*It) && It->IsEncounterAlive()) {Creep=*It;++Count;}
    if(!TestEqual(TEXT("Every available ground nut becomes a summon, regardless of old live cap"),Count,3)) return false;
    TestTrue(TEXT("The summoned nut has a running fuse from spawn"),Cast<AMCNutKamikaze>(Creep) && Cast<AMCNutKamikaze>(Creep)->GetFuseSecondsRemaining()>0);
    TestTrue(TEXT("Summoned creeps belong to the event for reset cleanup"),Creep->GetOwner()==T.Event);
    TestTrue(TEXT("One common knife hit leaves the weak creep alive"),Creep->ReceiveToolDamage(25,nullptr)>0 && Creep->IsEncounterAlive());
    TestTrue(TEXT("The second knife hit defeats the weak creep"),Creep->ReceiveToolDamage(25,nullptr)>0 && !Creep->IsEncounterAlive());
    // A separate live child survives one boss; killing both resolves the encounter.
    FMCNutEnemySettings Weak=T.Event->Settings.Enemy; Weak.MaxHealth=40;
    auto* Remaining=T.World->SpawnActor<AMCNutEnemy>();
    Remaining->SetActorLocation(FVector(500,500,45)); Remaining->SetOwner(T.Event);
    Remaining->ConfigureEnemy(T.Tongue,T.Cube,FVector(.7),Weak); T.Event->RegisterEncounterEnemy(Remaining);
    T.Boss->ReceiveToolDamage(100000,nullptr);
    TestTrue(TEXT("Boss defeat leaves existing creeps to finish"),Remaining->IsEncounterAlive());
    int32 Attacks=0;
    for(TActorIterator<AMCNutCombatEffect> It(T.World);It;++It)
        if(It->SourceActor==T.Boss && !It->IsActorBeingDestroyed() && It->Cue.Type!=EMCNutCombatCue::DeathBurst) ++Attacks;
    for(TActorIterator<AMCNutSpellProjectile> It(T.World);It;++It) if(It->SourceActor==T.Boss && !It->IsActorBeingDestroyed()) ++Attacks;
    TestEqual(TEXT("Defeat cancels owned attacks while retaining its cosmetic death burst"),Attacks,0);
    T.Event->Stop();
    TestTrue(TEXT("An explicit event reset destroys the surviving creep"),Remaining->IsActorBeingDestroyed());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutFireAimTest,"MessControl.CoreLoop.NutBoss.FireNutTracksUntilReleaseWithoutGroundMarker",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutFireAimTest::RunTest(const FString&)
{
    MCNutBossTestsPrivate::FFixture T;T.AddBoss(EMCNutBossRole::Mage);T.Step(1.4f);
    auto* Hero=T.AddHero(FVector(800,0,130));T.Boss->Target=Hero;
    T.Boss->NextMeleeAt=T.Boss->NextRainAt=T.Boss->NextSummonAt=10000;
    T.Boss->NextFireballAt=T.Boss->NextTeleportAt=T.Boss->NextSpecialAt=0;
    if(!TestTrue(TEXT("The real blink schedules its subsequent fire-nut cast"),T.WaitFor(EMCNutBossAttack::Fireball,EMCNutBossState::Telegraph,7))) return false;
    const FVector Initial=T.Boss->LockedTarget;
    Hero->SetActorLocation(Hero->GetActorLocation()+FVector(0,300,0));T.Step(.2f);
    TestTrue(TEXT("Moving during the cast changes the wizard's aim"),FVector::Dist(T.Boss->LockedTarget,Initial)>200);
    TestTrue(TEXT("The wizard aims at the current player position"),FVector::Dist(T.Boss->LockedTarget,Hero->GetActorLocation())<1);
    bool Charge=false,DestinationCue=false;
    for(TActorIterator<AMCNutCombatEffect> It(T.World);It;++It) if(It->SourceActor==T.Boss) {
        DestinationCue|=It->Cue.Type==EMCNutCombatCue::FireCast;
        if(It->Cue.Type==EMCNutCombatCue::CastCharge) {
            Charge=true;
            TestFalse(TEXT("The hand charge draws no floor destination marker"),It->Warning->IsVisible() || It->DropWarnings->IsVisible() || It->DropShadows->IsVisible());
        }
    }
    TestTrue(TEXT("The visible charge in the hand remains"),Charge);
    TestFalse(TEXT("Native fire-nut no longer creates a destination telegraph"),DestinationCue);
    // Compatibility: an old/lab FireCast cue must not restore the old marker.
    auto* Legacy=AMCNutCombatEffect::Spawn(T.Boss,T.Tongue,EMCNutCombatCue::FireCast,T.Boss->GetCastOrigin(),FVector(20000,0,500),90,1,.2f);
    if(!TestNotNull(TEXT("A legacy charge can be previewed without a target floor"),Legacy)) return false;
    // PreviewAtAge intentionally skips game worlds; let the actual runtime
    // presentation tick before inspecting component visibility.
    T.Step(.05f);
    TestFalse(TEXT("A legacy FireCast never reveals its target on the floor"),Legacy->Warning->IsVisible());
    TestTrue(TEXT("The charge remains visible even without a target floor"),Legacy->Flames->IsVisible());
    Legacy->Cue.Target=Hero->GetActorLocation();T.Step(.05f);
    TestFalse(TEXT("A valid target floor cannot restore the old destination marker"),Legacy->Warning->IsVisible());
    Hero->SetActorLocation(Hero->GetActorLocation()+FVector(0,200,0));
    AMCNutSpellProjectile* Shot=nullptr;
    for(int32 I=0;I<35 && !Shot;++I) {
        T.Step(.05f);
        for(TActorIterator<AMCNutSpellProjectile> It(T.World);It;++It) if(It->SourceActor==T.Boss) {Shot=*It;break;}
    }
    if(!TestNotNull(TEXT("The completed cast releases a real projectile"),Shot)) return false;
    TestTrue(TEXT("The release uses the player's latest position"),FVector::Dist(Shot->Flight.Target,Hero->GetActorLocation())<1);
    TestTrue(TEXT("The projectile exceeds the old 1800 cm/s cap"),FMath::IsNearlyEqual(float(FVector::Dist(Shot->Flight.Start,Shot->Flight.Target)/Shot->Flight.Seconds),2800.f,.1f));
    const FVector Aim=Shot->Flight.Target;const float Health=Hero->Status->State.Health;
    Hero->SetActorLocation(Hero->GetActorLocation()+FVector(0,600,0));T.Step(.2f);
    TestTrue(TEXT("A post-release dodge never bends the existing projectile"),IsValid(Shot) && Shot->Flight.Target.Equals(Aim,.01f));
    TestEqual(TEXT("A real post-release sidestep avoids this projectile's damage"),Hero->Status->State.Health,Health);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutFireUlcerTest,"MessControl.CoreLoop.NutBoss.FireNutTongueImpactCreatesPainUlcer",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutFireUlcerTest::RunTest(const FString&)
{
    MCNutBossTestsPrivate::FFixture T;
    const auto Shoot=[&](FVector Start,FVector Target) {
        auto* Shot=AMCNutSpellProjectile::Spawn(T.Event,T.Tongue,Start,Target,1800,18,65);
        if(Shot) {Shot->SetOwner(T.Event);T.Event->TrackEncounterActor(Shot);}
        return Shot;
    };
    const auto CountUlcers=[&]() {
        int32 Count=0;for(TActorIterator<AMCMouthSurface> It(T.World);It;++It) if(It->bUlcer && !It->IsHealed()) ++Count;
        return Count;
    };
    FHitResult Floor;
    if(!TestTrue(TEXT("The projectile fixture has real tongue collision"),T.Tongue->SurfacePoint(FVector::ZeroVector,Floor))) return false;
    auto* Shot=Shoot(Floor.ImpactPoint+FVector(0,0,500),Floor.ImpactPoint-FVector(0,0,100));
    if(!TestNotNull(TEXT("A real fire-nut is launched toward the tongue"),Shot)) return false;
    TestEqual(TEXT("The faster projectile retains its requested travel speed"),float(FVector::Dist(Shot->Flight.Start,Shot->Flight.Target)/Shot->Flight.Seconds),1800.f,.1f);
    T.Step(.5f);
    TestTrue(TEXT("The swept tongue collision resolves before an airborne endpoint"),Shot->Impact.bImpacted);
    if(!TestEqual(TEXT("The first tongue hit creates one pain ulcer"),CountUlcers(),1)) return false;
    AMCMouthSurface* Ulcer=nullptr;
    for(TActorIterator<AMCMouthSurface> It(T.World);It;++It) if(It->ActorHasTag(TEXT("MC_NutFireUlcer"))) Ulcer=*It;
    if(!TestNotNull(TEXT("The new ulcer is identified as fire-nut damage"),Ulcer)) return false;
    TestTrue(TEXT("The wound is placed on the actual tongue surface"),FVector::Dist(Ulcer->GetActorLocation(),Floor.ImpactPoint)<12);
    TestTrue(TEXT("The wound uses the ordinary treatable pain-wave mechanics"),!Ulcer->bTreatmentBlocked && Ulcer->Healing==0 && Ulcer->PulseDamage>0 && Ulcer->PulseInterval>0);
    TestEqual(TEXT("The encounter owns its new wound for reset cleanup"),Ulcer->GetOwner(),static_cast<AActor*>(T.Event));
    T.Step(.1f);
    TestEqual(TEXT("Repeated impact ticks cannot create extra ulcers"),CountUlcers(),1);
    // Expiring in the air must not invent a collision by projecting onto tissue.
    Shoot(FVector(-1000,0,400),FVector(-700,0,400));T.Step(.3f);
    TestEqual(TEXT("An airborne endpoint creates no wound below the shot"),CountUlcers(),1);
    // The shared spawner can return an existing wound. Keep its ownership and
    // lifecycle even if that wound had no previous owner.
    const FVector OtherPoint=Floor.ImpactPoint+FVector(700,0,0);
    auto* Existing=AMCMouthSurface::SpawnDamageUlcer(T.World,OtherPoint);
    if(!TestNotNull(TEXT("An unrelated pre-existing ulcer is available"),Existing)) return false;
    Existing->Area->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Shoot(OtherPoint+FVector(0,0,500),OtherPoint-FVector(0,0,100));T.Step(.5f);
    TestEqual(TEXT("A hit near an existing ulcer preserves deduplication"),CountUlcers(),2);
    TestNull(TEXT("The projectile never adopts a pre-existing unowned ulcer"),Existing->GetOwner());
    T.Event->Stop();
    TestTrue(TEXT("Stopping the event removes its fire-nut ulcer"),!IsValid(Ulcer) || Ulcer->IsActorBeingDestroyed());
    TestTrue(TEXT("Stopping the event preserves the unrelated ulcer"),IsValid(Existing) && !Existing->IsActorBeingDestroyed());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossBoundaryRainTest,"MessControl.CoreLoop.NutBoss.EdgeTargetStillCastsRealRain",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutBossBoundaryRainTest::RunTest(const FString&)
{
    MCNutBossTestsPrivate::FFixture T;
    auto* Profile=NewObject<UMCNutRainProfile>(T.World);
    Profile->Settings.Boss.WholeMesh=T.Cube;
    Profile->Settings.Boss.ShellMesh=T.Cube;
    Profile->Settings.Boss.KernelMesh=T.Cube;
    T.Event->Start(nullptr,Profile);
    T.Event->SetActorTickEnabled(false);
    T.AddBoss(EMCNutBossRole::Mage); T.Step(1.4f);
    T.Event->Stage=EMCNutRainStage::Enemies;
    T.Event->Settings.bBossEncounter=true;
    T.Boss->NextMeleeAt=T.Boss->NextFireballAt=T.Boss->NextSummonAt=T.Boss->NextTeleportAt=10000;
    T.Boss->NextSpecialAt=T.Boss->NextRainAt=0;
    T.AddHero(FVector(1540,1120,130));
    if(!TestTrue(TEXT("A target at the tongue edge still receives an actual rain windup"),
        T.WaitFor(EMCNutBossAttack::NutRain,EMCNutBossState::Telegraph,3))) return false;
    const int32 Before=T.Event->NutsSpawned;
    T.Step(T.Boss->BossSettings.RainWindup+1.2f);
    TestTrue(TEXT("The edge-target cast releases real persistent nuts"),T.Event->NutsSpawned>Before);
    for(const AMCFoodActor* Nut:T.Event->Nuts) if(IsValid(Nut) && !Nut->IsDisposed()) {
        FHitResult Floor;
        TestTrue(TEXT("Every relocated drop has a reachable gameplay footprint"),
            T.Tongue->GameplaySpawnFootprint(Nut->WorkPosition(),T.Event->Settings.ImpactRadius,Floor));
    }
    return true;
}
#endif
