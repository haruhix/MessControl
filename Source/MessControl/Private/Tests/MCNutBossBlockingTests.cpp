#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCBossCharacter.h"
#include "MCNutBoss.h"
#include "MCNutRainEvent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCTongue.h"
#include "AIController.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCNutBossBlockingTestsPrivate
{
struct FFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCTongue* Tongue=nullptr;
    UStaticMesh* Cube=nullptr;
    AMCNutBoss* Boss=nullptr;
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
        Tongue->SourceMesh=Cube; Tongue->bAutomaticYawns=false;
        Tongue->FinishSpawning(Pose); Tongue->SetActorTickEnabled(false);
    }
    ~FFixture()
    {
        World->EndPlay(EEndPlayReason::Quit);
        GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    }
    void Frame() {++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60.f);}
    void Step(float Seconds) {for(int32 I=0;I<FMath::CeilToInt(Seconds*60);++I) Frame();}
    AMCNutBoss* AddBoss(EMCNutBossRole Role=EMCNutBossRole::Tank,bool Landed=true)
    {
        FMCNutBossSettings Settings;
        Settings.WholeMesh=Settings.ShellMesh=Settings.KernelMesh=Cube;
        const FTransform Pose(FVector(-600,0,Settings.BodyRadiusForRole(Role)+14));
        Boss=World->SpawnActorDeferred<AMCNutBoss>(AMCNutBoss::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Boss->ConfigureEncounter(Tongue,nullptr,Role,Settings,1,41);
        Boss->FinishSpawning(FTransform(Boss->GetActorRotation(),Boss->LockedStart));
        if(Landed) {Step(1.4f); Boss->SetActorTickEnabled(false);}
        return Boss;
    }
    AMCToothCharacter* AddHero(FVector Position,bool Walking=false)
    {
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Hero=World->SpawnActor<AMCToothCharacter>(Position,FRotator::ZeroRotator,Params);
        if(Walking) {
            World->SpawnActor<AAIController>()->Possess(Hero);
            Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        } else Hero->GetCharacterMovement()->DisableMovement();
        return Hero;
    }
    UBoxComponent* Wall(FVector Position)
    {
        auto* Actor=World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Box); Box->SetBoxExtent(FVector(10,600,500));
        Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent();
        Actor->SetActorLocation(Position); return Box;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossStandingBlocking,
    "MessControl.CoreLoop.NutBoss.Blocking.RealCharacterSweepsAndWalking",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutBossStandingBlocking::RunTest(const FString&)
{
    using namespace MCNutBossBlockingTestsPrivate;
    for(const auto Role:{EMCNutBossRole::Tank,EMCNutBossRole::Mage}) {
        FFixture F; auto* Boss=F.AddBoss(Role);
        auto* Hero=F.AddHero(FVector(-1200,0,72),true);
        const FVector Center=Boss->GetActorLocation();
        TestEqual(TEXT("The blocking body remains query-only"),Boss->Body->GetCollisionEnabled(),ECollisionEnabled::QueryOnly);
        TestEqual(TEXT("Physical limbs and food retain their dedicated contact handling"),Boss->Body->GetCollisionResponseToChannel(ECC_PhysicsBody),ECR_Ignore);
        TestEqual(TEXT("The camera is not forced into the character by a boss"),Boss->Body->GetCollisionResponseToChannel(ECC_Camera),ECR_Ignore);
        for(const FVector Direction:{FVector::ForwardVector,-FVector::ForwardVector,FVector::RightVector,-FVector::RightVector}) {
            Hero->GetCharacterMovement()->StopMovementImmediately();
            Hero->SetActorLocation(Center-Direction*500+FVector(0,0,72-Center.Z),false,nullptr,ETeleportType::TeleportPhysics);
            FHitResult Hit;
            Hero->GetCharacterMovement()->SafeMoveUpdatedComponent(Direction*1000,Hero->GetActorQuat(),true,Hit);
            TestTrue(TEXT("A real CMC capsule sweep is stopped by the boss from every side"),Hit.bBlockingHit && Hit.GetActor()==Boss);
            TestFalse(TEXT("The blocking contact is reached without initial penetration"),Hit.bStartPenetrating);
            TestTrue(TEXT("The worker cannot cross through the boss centre"),FVector::DotProduct(Hero->GetActorLocation()-Center,Direction)<-50.f);
        }
        Hero->GetCharacterMovement()->StopMovementImmediately();
        Hero->SetActorLocation(FVector(Center.X-500,Center.Y,72),false,nullptr,ETeleportType::TeleportPhysics);
        Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        const float StartX=Hero->GetActorLocation().X;
        for(int32 I=0;I<180;++I) {Hero->AddMovementInput(FVector::ForwardVector);F.Frame();}
        TestTrue(TEXT("Ordinary held walking input actually reaches the boss"),Hero->GetActorLocation().X>StartX+100.f);
        TestTrue(TEXT("Continued walking input cannot pass through the boss"),Hero->GetActorLocation().X<Center.X-50.f);
        TestTrue(TEXT("The worker is not stepped up onto the boss"),Hero->GetActorLocation().Z<Center.Z);
        TestTrue(TEXT("Walking against the boss does not damage the worker"),Hero->Status->IsAlive());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossAttackBlocking,
    "MessControl.CoreLoop.NutBoss.Blocking.RollAndChargeKeepPlayerDamageAndWalls",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutBossAttackBlocking::RunTest(const FString&)
{
    using namespace MCNutBossBlockingTestsPrivate;
    for(const auto Attack:{EMCNutBossAttack::Roll,EMCNutBossAttack::Charge}) {
        FFixture F; auto* Boss=F.AddBoss(); const FVector Start=Boss->GetActorLocation();
        auto* Hero=F.AddHero(Start+FVector(Boss->Settings.BodyRadius+40,0,-15));
        // EnterStanding grants the ordinary 0.6-second spawn/get-up immunity.
        // Exercise an encounter contact after that protection, without
        // disabling it or weakening the actual physical-knockback assertion.
        F.Step(.65f);
        TestEqual(TEXT("The worker is standing before the attack contact"),Hero->ToothPhysics->GetBodyState(),EMCBodyState::Standing);
        const int32 KnockdownsBefore=Hero->ToothPhysics->KnockdownCount;
        const FVector PhysicalBefore=Hero->ToothPhysics->PhysicalLocation();
        Boss->Target=Hero; Boss->Attack=Attack; Boss->State=EMCNutBossState::Executing;
        Boss->AttackForward=FVector::ForwardVector; Boss->LockedStart=Start;
        Boss->LockedTarget=Start+FVector(1000,0,0);
        Boss->ResolveAt=F.World->GetTimeSeconds(); Boss->AttackEndAt=Boss->ResolveAt+12;
        const float Before=Hero->Status->State.Health;
        Boss->SetActorTickEnabled(true); F.Step(.25f); Boss->SetActorTickEnabled(false);
        const float Damage=Attack==EMCNutBossAttack::Roll?Boss->BossSettings.RollDamage:Boss->BossSettings.ChargeDamage;
        TestEqual(TEXT("A solid player still receives exactly one ordinary attack contact"),Hero->Status->State.Health,Before-Damage);
        TestTrue(TEXT("The attack advances through its own player contact instead of treating it as a wall"),Boss->GetActorLocation().X>Start.X+100.f);
        TestTrue(TEXT("A player contact does not reflect the attack direction"),Boss->AttackForward.X>.95f);
        TestTrue(TEXT("The ordinary attack still invokes physical knockback"),Hero->ToothPhysics->KnockdownCount>KnockdownsBefore);
        TestEqual(TEXT("The real hit enters the ordinary ragdoll state"),Hero->ToothPhysics->GetBodyState(),EMCBodyState::Ragdoll);
        TestTrue(TEXT("The simulated body actually moves in the attack's push direction"),Hero->ToothPhysics->PhysicalLocation().X>PhysicalBefore.X+5.f);
        TestFalse(TEXT("The temporary attack move-ignore does not persist"),Boss->Body->GetMoveIgnoreActors().Contains(Hero));
        TestEqual(TEXT("Player movement still sees the boss after its attack sweep"),Boss->Body->GetCollisionResponseToChannel(ECC_Pawn),ECR_Block);
        // The same movement helper must keep scenery blocking. Place a real
        // world obstacle just ahead and move the target beyond it.
        const FVector BeforeWall=Boss->GetActorLocation();
        auto* Wall=F.Wall(BeforeWall+FVector(Boss->Settings.BodyRadius+30,0,0));
        auto* Aim=F.AddHero(BeforeWall+FVector(900,0,0)); Boss->Target=Aim;
        Boss->AttackForward=FVector::ForwardVector; Boss->LockedStart=BeforeWall;
        Boss->LockedTarget=BeforeWall+FVector(1000,0,0); Boss->ResolveAt=F.World->GetTimeSeconds();
        Boss->AttackEndAt=Boss->ResolveAt+12; Boss->State=EMCNutBossState::Executing;
        Boss->SetActorTickEnabled(true); F.Step(.15f); Boss->SetActorTickEnabled(false);
        TestTrue(TEXT("Attack movement still cannot cross a real world obstacle"),Boss->GetActorLocation().X<Wall->GetComponentLocation().X-Boss->Settings.BodyRadius+1.f);
        if(Attack==EMCNutBossAttack::Roll)
            TestTrue(TEXT("Real world contact still reflects a rolling attack"),Boss->AttackForward.X<0.f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossBlockingLifecycle,
    "MessControl.CoreLoop.NutBoss.Blocking.LandingTransformationAndDeath",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutBossBlockingLifecycle::RunTest(const FString&)
{
    using namespace MCNutBossBlockingTestsPrivate;
    FFixture F; auto* Boss=F.AddBoss(EMCNutBossRole::Tank,false);
    TestEqual(TEXT("The incoming entrance does not act as a descending player blocker"),Boss->Body->GetCollisionResponseToChannel(ECC_Pawn),ECR_Ignore);
    F.Step(1.4f); Boss->SetActorTickEnabled(false);
    TestEqual(TEXT("A landed boss blocks character movement"),Boss->Body->GetCollisionResponseToChannel(ECC_Pawn),ECR_Block);
    const FVector Start=Boss->GetActorLocation();
    auto* Hero=F.AddHero(Start+FVector(600,0,-15)); Boss->Target=Hero;
    Boss->Attack=EMCNutBossAttack::Jump; Boss->State=EMCNutBossState::Telegraph;
    Boss->LockedStart=Start; Boss->LockedTarget=Start+FVector(600,0,0);
    Boss->ResolveAt=F.World->GetTimeSeconds();
    Boss->SetActorTickEnabled(true); F.Frame();
    TestEqual(TEXT("Jump flight does not become a moving ceiling on character capsules"),Boss->Body->GetCollisionResponseToChannel(ECC_Pawn),ECR_Ignore);
    const float Before=Hero->Status->State.Health; F.Step(Boss->BossSettings.JumpFlightSeconds+.05f);
    Boss->SetActorTickEnabled(false);
    TestEqual(TEXT("The jump still resolves its normal landing damage"),Hero->Status->State.Health,Before-Boss->BossSettings.JumpDamage);
    TestEqual(TEXT("Landing restores ordinary player blocking"),Boss->Body->GetCollisionResponseToChannel(ECC_Pawn),ECR_Block);
    TestTrue(TEXT("Landing knockback removes the overlapping walking capsule through normal ragdoll handling"),Hero->ToothPhysics->KnockdownCount>0 && Hero->GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
    // Use the real lethal-hit transformation, including its temporary zero HP,
    // instead of setting a presentation state and assuming collision followed.
    auto* Event=F.World->SpawnActor<AMCNutRainEvent>(); Event->Stage=EMCNutRainStage::Enemies;
    Boss->EncounterOwner=Event; Boss->bHasMagePhase=true; Boss->ShieldHealth=0;
    Boss->ReceiveToolDamage(Boss->Health+1.f,nullptr);
    TestEqual(TEXT("A lethal knight hit starts the event's actual transformation"),Boss->State,EMCNutBossState::Transforming);
    TestEqual(TEXT("The knight is temporarily at zero HP during transformation"),Boss->Health,0.f);
    TestEqual(TEXT("Transformation presentation retains a solid boss body"),Boss->Body->GetCollisionResponseToChannel(ECC_Pawn),ECR_Block);
    Boss->SetActorTickEnabled(true); F.Step(AMCNutBoss::MagePhaseSeconds+.1f); Boss->SetActorTickEnabled(false);
    TestEqual(TEXT("The same actor reveals the real mage phase"),Boss->BossRole,EMCNutBossRole::Mage);
    TestEqual(TEXT("The revealed mage retains character blocking"),Boss->Body->GetCollisionResponseToChannel(ECC_Pawn),ECR_Block);
    Boss->ReceiveToolDamage(Boss->Health+1.f,nullptr);
    TestTrue(TEXT("The ordinary damage resolver defeats the mage"),Boss->bDefeated);
    TestEqual(TEXT("A defeated boss cannot leave an invisible obstacle"),Boss->Body->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGenericBossStandingBlocking,
    "MessControl.Boss.Blocking.RealCharacterCapsuleSweep",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGenericBossStandingBlocking::RunTest(const FString&)
{
    MCNutBossBlockingTestsPrivate::FFixture F;
    auto* Boss=F.World->SpawnActor<AMCBossCharacter>(FVector(0,0,120),FRotator::ZeroRotator);
    Boss->SetActorTickEnabled(false);
    auto* Hero=F.AddHero(FVector(-500,0,72)); FHitResult Hit;
    Hero->GetCharacterMovement()->SafeMoveUpdatedComponent(FVector(1000,0,0),Hero->GetActorQuat(),true,Hit);
    TestTrue(TEXT("The generic boss navigation capsule already blocks a real worker sweep"),Hit.bBlockingHit && Hit.GetActor()==Boss);
    TestTrue(TEXT("The generic boss cannot be crossed through its centre"),Hero->GetActorLocation().X<0);
    return true;
}
#endif
