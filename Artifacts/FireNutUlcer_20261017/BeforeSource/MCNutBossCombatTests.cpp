#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCNutBoss.h"
#include "MCArenaTooth.h"
#include "MCNutCombatEffect.h"
#include "MCNutSpellProjectile.h"
#include "MCNutKamikaze.h"
#include "MCFoodActor.h"
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
#include "Components/BoxComponent.h"
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
    TestTrue(TEXT("A guarded hit damages the boss by twenty percent"),FMath::IsNearlyEqual(T.Boss->ReceiveToolDamage(40,Hero),8.f));
    TestEqual(TEXT("The same hit damages the shield's own durability"),T.Boss->ShieldHealth,460.f);
    Hero->SetActorLocation(T.Boss->GetActorLocation()-FVector(180,0,-16));
    TestEqual(TEXT("A rear hit applies the whole tool damage"),T.Boss->ReceiveToolDamage(40,Hero),40.f);
    Hero->SetActorLocation(T.Boss->GetActorLocation()+FVector(180,0,16));
    for(const auto State:{EMCNutBossState::Telegraph,EMCNutBossState::Executing,EMCNutBossState::Recovery}) {
        T.Boss->State=State; T.Boss->Attack=EMCNutBossAttack::Melee;
        TestFalse(TEXT("The visible non-guard attack pose cannot protect the front"),T.Boss->IsShieldProtectingFrom(Hero->GetActorLocation()));
        TestEqual(TEXT("Attacking and recovery take full front damage"),T.Boss->ReceiveToolDamage(40,Hero),40.f);
    }
    T.Boss->State=EMCNutBossState::Idle; T.Boss->ShieldHealth=10;
    TestEqual(TEXT("Even a breaking guarded hit deals twenty percent to the boss"),T.Boss->ReceiveToolDamage(40,Hero),8.f);
    TestEqual(TEXT("The shield is now broken"),T.Boss->ShieldHealth,0.f);
    TestEqual(TEXT("The next front hit is no longer protected"),T.Boss->ReceiveToolDamage(40,Hero),40.f);
    const float Before=T.Boss->Health;
    TestEqual(TEXT("NaN damage is rejected"),T.Boss->ReceiveToolDamage(std::numeric_limits<float>::quiet_NaN(),Hero),0.f);
    Hero->Status->Damage(1000);
    TestEqual(TEXT("A defeated source cannot deal another hit"),T.Boss->ReceiveToolDamage(40,Hero),0.f);
    TestEqual(TEXT("Rejected hits preserve authoritative health"),T.Boss->Health,Before);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossRollTest,"MessControl.CoreLoop.NutBoss.RollDurationAndPredictableSteering",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutBossRollTest::RunTest(const FString&)
{
    MCNutBossTestsPrivate::FFixture T; T.AddBoss();
    auto* Hero=T.AddHero(FVector(150,0,130));
    if(!TestTrue(TEXT("The knight schedules a visible rolling windup"),T.WaitFor(EMCNutBossAttack::Roll,EMCNutBossState::Telegraph,25))) return false;
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
    TestFalse(TEXT("Rolling exposes the shield"),T.Boss->IsShieldProtectingFrom(T.Boss->GetActorLocation()+Forward*200));
    const float BeforeTurn=T.Boss->AttackForward.Rotation().Yaw;
    Hero->SetActorLocation(T.Boss->GetActorLocation()+FVector(0,500,16));
    T.Step(1.f/60);
    TestTrue(TEXT("Pursuit turns at the authored angular speed, without snapping"),
        FMath::Abs(FMath::FindDeltaAngleDegrees(BeforeTurn,T.Boss->AttackForward.Rotation().Yaw))<=T.Boss->BossSettings.RollTurnDegreesPerSecond/60.f+.01f);
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
    TestEqual(TEXT("The landing opens exactly a one-second damage window"),T.Boss->AttackEndAt-T.Boss->StateStartedAt,1.);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossComboTest,"MessControl.CoreLoop.NutBoss.ComboGroundHitAndTwoSecondRecovery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutBossComboTest::RunTest(const FString&)
{
    MCNutBossTestsPrivate::FFixture T; T.AddBoss(); T.Step(1.4f);
    T.Boss->NextJumpAt=T.Boss->NextRollAt=T.Boss->NextSpecialAt=10000;
    T.Boss->NextMeleeAt=0;
    auto* Hero=T.AddHero(T.Boss->GetActorLocation()+FVector(300,0,16));
    if(!TestTrue(TEXT("Close range starts the combo warning"),T.WaitFor(EMCNutBossAttack::Melee,EMCNutBossState::Telegraph,1))) return false;
    const FVector ExpectedSlam=T.Boss->LockedStart+T.Boss->AttackForward*T.Boss->BossSettings.MeleeRange*.55f;
    Hero->SetActorLocation(FVector(1300,900,130)); // Exercise the advertised dodge before any contact.
    const float Before=Hero->Status->State.Health;
    T.Step(T.Boss->BossSettings.MeleeWindup+1.05f);
    TestEqual(TEXT("The complete combo enters recovery"),T.Boss->State,EMCNutBossState::Recovery);
    TestEqual(TEXT("The combo opens exactly two seconds of recovery"),T.Boss->AttackEndAt-T.Boss->StateStartedAt,2.);
    TestEqual(TEXT("Leaving the locked combo footprint avoids all its hits"),Hero->Status->State.Health,Before);
    bool GroundImpact=false;
    for(TActorIterator<AMCNutCombatEffect> It(T.World);It;++It)
        if(It->SourceActor==T.Boss && It->Cue.Type==EMCNutCombatCue::SlamImpact)
            GroundImpact|=FVector::DistSquared(It->Cue.Target,ExpectedSlam)<1.f;
    TestTrue(TEXT("The last blow's ground VFX shares the actual damage centre"),GroundImpact);
    T.Step(1.8f);
    TestEqual(TEXT("The knight remains inactive throughout the damage window"),T.Boss->State,EMCNutBossState::Recovery);
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
