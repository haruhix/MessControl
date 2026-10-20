#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCCombatFeedbackComponent.h"
#include "MCBossCharacter.h"
#include "MCCataclysmJelly.h"
#include "MCGameState.h"
#include "MCInventoryComponent.h"
#include "MCNutBoss.h"
#include "MCNutKamikaze.h"
#include "MCPerkComponent.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCTongue.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCEnemyUpgradeTests
{
enum class EEnemy { Knight, Mage, Nut, Kamikaze, JellyLarge, JellyMedium, JellySmall };

struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Hero=nullptr;
    AMCTongue* Tongue=nullptr;
    UStaticMesh* Cube=nullptr;

    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* State=World->SpawnActor<AMCGameState>();
        State->Phase=EMCShiftPhase::Working;State->bLobbyWaiting=false;World->SetGameState(State);
        Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
        FStaticMeshCompilingManager::Get().FinishCompilation({Cube});
#endif
        const FTransform Floor(FRotator::ZeroRotator,FVector(0,0,10000),FVector(32,24,.2));
        Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Floor,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Tongue->SourceMesh=Cube;Tongue->bAutomaticYawns=false;Tongue->FinishSpawning(Floor);Tongue->SetActorTickEnabled(false);
        auto* Controller=World->SpawnActor<AMCPlayerController>();
        auto* Player=World->SpawnActor<AMCPlayerState>();Controller->SetPlayerState(Player);
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,10100),FRotator::ZeroRotator);
        Controller->Possess(Hero);Hero->Status->Initialize(1000);
        // Hold the contact arrangement stationary while the ordinary inventory
        // tick, primary input, tool ownership and pulse cooldown remain active.
        Hero->SetActorTickEnabled(false);
        Hero->GetCharacterMovement()->SetComponentTickEnabled(false);
        Hero->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
        Step(.2f);
    }
    ~FWorld()
    {
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    }
    void Step(float Seconds,float Dt=1.f/60)
    {
        for(int32 Frame=0;Frame<FMath::CeilToInt(Seconds/Dt);++Frame) {++GFrameCounter;World->Tick(LEVELTICK_All,Dt);}
    }
    bool StartSaw()
    {
        auto* Perks=Hero->GetPlayerState<AMCPlayerState>()->Perks.Get();
        if(!Perks->ServerGrantReward(TEXT("Tool_Chainsaw_Rare"))) return false;
        Hero->Inventory->ServerSelect(EMCToolSlot::Knife);
        Hero->ServerSetPrimary(true);
        return Hero->Inventory->IsChainsawRunning();
    }
    AMCNutEnemy* Enemy(EEnemy Kind,FVector Offset=FVector::ZeroVector)
    {
        if(Offset.IsNearlyZero()) Offset=FVector(Kind==EEnemy::Knight || Kind==EEnemy::Mage?350:200,0,0);
        const FTransform Pose(FRotator::ZeroRotator,Hero->GetActorLocation()+Offset);
        AMCNutEnemy* Target=nullptr;
        if(Kind==EEnemy::Knight || Kind==EEnemy::Mage) {
            auto* Boss=World->SpawnActorDeferred<AMCNutBoss>(AMCNutBoss::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            FMCNutBossSettings Settings;Settings.WholeMesh=Settings.ShellMesh=Settings.KernelMesh=Cube;
            Settings.TankHealth=Settings.MageHealth=1000;
            Boss->ConfigureEncounter(Tongue,nullptr,Kind==EEnemy::Knight?EMCNutBossRole::Tank:EMCNutBossRole::Mage,Settings,1,41);
            Boss->State=EMCNutBossState::Recovery;Boss->Attack=EMCNutBossAttack::None;
            Boss->FinishSpawning(Pose);
            Boss->SetActorLocation(Pose.GetLocation(),false,nullptr,ETeleportType::TeleportPhysics);
            Target=Boss;
        } else if(Kind==EEnemy::JellyLarge || Kind==EEnemy::JellyMedium || Kind==EEnemy::JellySmall) {
            auto* Jelly=World->SpawnActor<AMCCataclysmJelly>(Pose.GetLocation(),FRotator::ZeroRotator);
            const auto Size=Kind==EEnemy::JellyLarge?EMCCataclysmJellySize::Large:Kind==EEnemy::JellyMedium?EMCCataclysmJellySize::Medium:EMCCataclysmJellySize::Small;
            Jelly->ConfigureJelly(Tongue,1000,Size);Target=Jelly;
        } else if(Kind==EEnemy::Kamikaze) {
            auto* Summon=World->SpawnActor<AMCNutKamikaze>(Pose.GetLocation(),FRotator::ZeroRotator);
            FMCNutEnemySettings Settings;Settings.MaxHealth=1000;
            Summon->ConfigureKamikaze(Tongue,Cube,FVector::OneVector,Settings,30);Target=Summon;
        } else {
            Target=World->SpawnActor<AMCNutEnemy>(Pose.GetLocation(),FRotator::ZeroRotator);
            FMCNutEnemySettings Settings;Settings.MaxHealth=1000;Target->ConfigureEnemy(Tongue,Cube,FVector::OneVector,Settings);
        }
        Target->SetActorTickEnabled(false);
        return Target;
    }
    AActor* Wall()
    {
        auto* Actor=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Box);Box->SetBoxExtent(FVector(5,300,300));
        Box->SetCollisionProfileName(TEXT("BlockAll"));Box->RegisterComponent();
        Actor->SetActorLocation(Hero->GetActorLocation()+FVector(60,0,0));
        return Actor;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCChainsawNutFamilies,"MessControl.Inventory.EnemyUpgrades.ChainsawAllNutFamilies",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCChainsawNutFamilies::RunTest(const FString&)
{
    using namespace MCEnemyUpgradeTests;
    for(const auto Kind:{EEnemy::Knight,EEnemy::Mage,EEnemy::Nut,EEnemy::Kamikaze,EEnemy::JellyLarge,EEnemy::JellyMedium,EEnemy::JellySmall}) {
        FWorld Fixture;auto* Target=Fixture.Enemy(Kind);auto* Knight=Cast<AMCNutBoss>(Target);
        const bool Shielded=Kind==EEnemy::Knight;
        const float Health=Target->Health,Shield=Shielded?Knight->ShieldHealth:0.f;
        if(!TestTrue(TEXT("Real reward and primary input start the selected chainsaw"),Fixture.StartSaw())) continue;
        Fixture.Step(.04f);
        const FString Label=FString::Printf(TEXT("Enemy family %d"),int32(Kind));
        TestTrue(*(Label+TEXT(" receives a real held-tool contact")),Shielded?Knight->ShieldHealth<Shield:Target->Health<Health);
        if(Shielded) TestEqual(TEXT("The knight's intact shield still protects HP"),Target->Health,Health);
        TestEqual(*(Label+TEXT(" confirms one accepted enemy hit")),Fixture.Hero->ConfirmedHitCount,1);
        TestEqual(*(Label+TEXT(" presents one accepted damage event")),Target->Feedback->LastHit.Serial,1);
        const float AfterHealth=Target->Health,AfterShield=Shielded?Knight->ShieldHealth:0.f;
        Fixture.Step(.2f);
        TestEqual(TEXT("Repeated server pulses cannot bypass one-target cooldown"),Target->Health,AfterHealth);
        if(Shielded) TestEqual(TEXT("Shield-only contacts consume the same cooldown"),Knight->ShieldHealth,AfterShield);
        TestEqual(TEXT("Cooldown samples do not duplicate the success event"),Fixture.Hero->ConfirmedHitCount,1);
        Fixture.Step(.6f);
        TestEqual(TEXT("Continuous saw contact resumes after the target cooldown"),Fixture.Hero->ConfirmedHitCount,2);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCChainsawShieldFirst,"MessControl.Inventory.EnemyUpgrades.ChainsawShieldBreakHasNoHPSpill",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCChainsawShieldFirst::RunTest(const FString&)
{
    using namespace MCEnemyUpgradeTests;
    FWorld Fixture;auto* Knight=CastChecked<AMCNutBoss>(Fixture.Enemy(EEnemy::Knight));
    Knight->ShieldHealth=10;const float Health=Knight->Health;
    if(!TestTrue(TEXT("Normal primary input starts the owned saw"),Fixture.StartSaw())) return false;
    Fixture.Step(.04f);
    TestEqual(TEXT("The first real saw pulse breaks the shield"),Knight->ShieldHealth,0.f);
    TestEqual(TEXT("Excess shield damage does not reach HP"),Knight->Health,Health);
    TestEqual(TEXT("The shield-breaking hit is confirmed exactly once"),Fixture.Hero->ConfirmedHitCount,1);
    TestEqual(TEXT("Shield break enters the stun damage window"),Knight->State,EMCNutBossState::Stunned);
    Fixture.Step(.2f);
    TestEqual(TEXT("The breaking pulse cannot immediately hit exposed HP again"),Knight->Health,Health);
    Fixture.Step(.6f);
    TestTrue(TEXT("A later independent saw pulse damages exposed knight HP"),Knight->Health<Health);
    TestEqual(TEXT("Two accepted pulses produce exactly two success events"),Fixture.Hero->ConfirmedHitCount,2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCChainsawContactRejections,"MessControl.Inventory.EnemyUpgrades.ChainsawRejectsWallsReachAndInactiveTargets",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCChainsawContactRejections::RunTest(const FString&)
{
    using namespace MCEnemyUpgradeTests;
    for(int32 Case=0;Case<6;++Case) {
        FWorld Fixture;const FVector Offset=Case==1?FVector(-350,0,0):Case==2?FVector(600,0,0):Case==3?FVector(350,0,400):FVector(350,0,0);
        auto* Knight=CastChecked<AMCNutBoss>(Fixture.Enemy(EEnemy::Knight,Offset));
        if(Case==0) Fixture.Wall();
        if(Case==4) Knight->State=EMCNutBossState::Falling;
        if(Case==5) Knight->State=EMCNutBossState::Transforming;
        const float Health=Knight->Health,Shield=Knight->ShieldHealth;
        if(!TestTrue(TEXT("A rejected target does not prevent starting the real saw"),Fixture.StartSaw())) continue;
        Fixture.Step(.3f);
        TestEqual(TEXT("Walls, rear arc, distance, height and invulnerable phases reject HP damage"),Knight->Health,Health);
        TestEqual(TEXT("Rejected blade contacts do not consume shield durability"),Knight->ShieldHealth,Shield);
        TestEqual(TEXT("Rejected contacts have no successful hit feedback"),Fixture.Hero->ConfirmedHitCount,0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCChainsawBossBlockingIsNotWallCrash,"MessControl.Inventory.EnemyUpgrades.ChainsawBossBlockingDoesNotCauseWallCrash",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCChainsawBossBlockingIsNotWallCrash::RunTest(const FString&)
{
    using namespace MCEnemyUpgradeTests;
    for(int32 Case=0;Case<4;++Case) {
        FWorld Fixture;AActor* Obstacle=nullptr;
        if(Case<2) Obstacle=Fixture.Enemy(Case==0?EEnemy::Knight:EEnemy::Mage);
        else if(Case==2) {
            auto* Boss=Fixture.World->SpawnActor<AMCBossCharacter>(Fixture.Hero->GetActorLocation()+FVector(350,0,0),FRotator::ZeroRotator);
            Boss->SetActorTickEnabled(false);Boss->GetCharacterMovement()->DisableMovement();Obstacle=Boss;
        } else Obstacle=Fixture.Wall();
        // EnterStanding grants the freshly spawned pawn .6 seconds of physical
        // recovery protection. Let that normal gate expire before starting the
        // saw, so the wall control exercises its actual knockdown as well.
        Fixture.Step(.5f);
        if(!TestTrue(TEXT("The real held chainsaw is running before collision"),Fixture.StartSaw())) continue;
        const float Health=Fixture.Hero->Status->State.Health;
        const int32 Knockdowns=Fixture.Hero->ToothPhysics->KnockdownCount;
        auto* Movement=Fixture.Hero->GetCharacterMovement();
        Movement->Velocity=FVector(350,0,0);
        FHitResult Hit;
        // This is the actual movement sweep and its registered OnBodyHit path,
        // rather than a fabricated hit passed directly to the inventory handler.
        Movement->SafeMoveUpdatedComponent(FVector(700,0,0),Fixture.Hero->GetActorQuat(),true,Hit);
        TestTrue(TEXT("The ordinary capsule sweep really blocks against the selected obstacle"),Hit.bBlockingHit && Hit.GetActor()==Obstacle);
        TestFalse(TEXT("The sweep reaches a surface without starting in penetration"),Hit.bStartPenetrating);
        TestTrue(TEXT("Solid geometry prevents the chainsaw user crossing its centre"),Fixture.Hero->GetActorLocation().X<Obstacle->GetActorLocation().X);
        if(Case<3) {
            TestEqual(TEXT("Contact with an enemy body does not apply scenery crash damage"),Fixture.Hero->Status->State.Health,Health);
            TestEqual(TEXT("A blocked saw user is not knocked down by its own enemy contact"),Fixture.Hero->ToothPhysics->GetBodyState(),EMCBodyState::Standing);
            TestEqual(TEXT("Solid enemy contact does not enter the physical knockdown path"),Fixture.Hero->ToothPhysics->KnockdownCount,Knockdowns);
            TestTrue(TEXT("Solid enemy contact leaves the chainsaw running"),Fixture.Hero->Inventory->IsChainsawRunning());
            Fixture.Step(.04f);
            TestTrue(TEXT("The ordinary tool tick can still hit the blocking enemy"),Fixture.Hero->ConfirmedHitCount>0);
        } else {
            TestEqual(TEXT("The same real movement callback retains scenery crash damage"),Fixture.Hero->Status->State.Health,Health-30.f);
            TestEqual(TEXT("A real wall contact still knocks down the chainsaw user"),Fixture.Hero->ToothPhysics->GetBodyState(),EMCBodyState::Ragdoll);
            TestEqual(TEXT("The real wall callback enters the ordinary knockdown path once"),Fixture.Hero->ToothPhysics->KnockdownCount,Knockdowns+1);
            TestFalse(TEXT("A real wall crash switches the chainsaw off"),Fixture.Hero->Inventory->IsChainsawRunning());
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCChainsawInsideContactArc,"MessControl.Inventory.EnemyUpgrades.ChainsawInsideContactsKeepRearArcRejected",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCChainsawInsideContactArc::RunTest(const FString&)
{
    using namespace MCEnemyUpgradeTests;
    for(int32 Kind=0;Kind<3;++Kind) for(const float Offset:{0.f,20.f,-20.f}) {
        FWorld Fixture;AMCNutEnemy* Nut=nullptr;AMCBossCharacter* Boss=nullptr;
        if(Kind<2) Nut=Fixture.Enemy(Kind==0?EEnemy::Nut:EEnemy::JellySmall);
        else {
            Boss=Fixture.World->SpawnActor<AMCBossCharacter>(Fixture.Hero->GetActorLocation()+FVector(350,0,0),FRotator::ZeroRotator);
            Boss->SetActorTickEnabled(false);Boss->GetCharacterMovement()->DisableMovement();
        }
        AActor* Target=Nut?static_cast<AActor*>(Nut):static_cast<AActor*>(Boss);
        // Includes a coincident centre and a source already inside the body.
        // The rear offset remains a real rear approach even though its nearest
        // surface point equals the source and therefore has no useful direction.
        Target->SetActorLocation(Fixture.Hero->GetActorLocation()+FVector(Offset,0,0),false,nullptr,ETeleportType::TeleportPhysics);
        const float Health=Nut?Nut->Health:Boss->Runtime.Health;
        if(!TestTrue(TEXT("Ordinary primary input starts the owned saw inside the target volume"),Fixture.StartSaw())) continue;
        Fixture.Step(.04f);
        const bool ExpectedHit=Offset>=0;
        const float AfterHealth=Nut?Nut->Health:Boss->Runtime.Health;
        TestEqual(TEXT("Coincident and front inside contacts hit, while a real rear approach still misses"),AfterHealth<Health,ExpectedHit);
        TestEqual(TEXT("An accepted inside contact has one success event and a rear contact has none"),Fixture.Hero->ConfirmedHitCount,ExpectedHit?1:0);
    }
    return true;
}
#endif
