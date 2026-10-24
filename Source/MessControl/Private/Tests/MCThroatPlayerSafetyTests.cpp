#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCThroat.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PhysicsEngine/BodyInstance.h"

namespace MCThroatPlayerSafetyTests
{
struct FFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCThroat* Throat=nullptr;
    FVector Inlet,Inward,Right;
    FFixture()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        // A non-axis-aligned artist throat exercises the real aperture profile,
        // without a tongue/floor masking the deliberately isolated flight.
        const FTransform Pose(FRotator(0,37,0),FVector(6000,0,5000));
        Throat=World->SpawnActorDeferred<AMCThroat>(AMCThroat::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Throat->bManualIntake=true;Throat->ManualAuthoredExpansion=.75f;
        Throat->AuthoredMouth->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/FromBlender4/SK_Exit.SK_Exit")));
        Throat->FinishSpawning(Pose);Throat->SetActorTickEnabled(false);
        Inlet=Throat->VacuumInlet();
        Inward=Throat->AuthoredMouth->GetComponentTransform().TransformVectorNoScale(FVector(0,.9747185974,.2234360218)).GetSafeNormal();
        Right=Throat->AuthoredMouth->GetComponentTransform().TransformVectorNoScale(FVector::ForwardVector).GetSafeNormal();
    }
    ~FFixture()
    {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    void Step(float Seconds)
    {for(int32 I=0;I<FMath::RoundToInt(Seconds*120);++I) {++GFrameCounter;World->Tick(LEVELTICK_All,1.f/120);}}
    AMCToothCharacter* Player(FVector Point)
    {
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Hero=World->SpawnActor<AMCToothCharacter>(Point,FRotator::ZeroRotator,Params);
        Hero->GetCharacterMovement()->DisableMovement();Hero->SetActorTickEnabled(false);
        return Hero;
    }
    AMCToothCharacter* FallenPlayer(FVector Point,FVector Velocity)
    {
        auto* Hero=Player(Point);
        // Fresh standing bodies deliberately reject knockdown for 0.6 s. Let
        // that real spawn grace expire rather than bypassing ApplyHit's gate.
        Step(.7f);
        // Use the actual hit/ragdoll transition and real Chaos bodies. Keep only
        // capsule recentering paused so the throat must read the physical body.
        Hero->ToothPhysics->ApplyHit(Velocity,Point);
        Hero->ToothPhysics->SetComponentTickEnabled(false);
        auto* Mesh=Hero->GetMesh();
        Mesh->SetEnableGravity(false);
        Mesh->AddWorldOffset(Point-Hero->ToothPhysics->PhysicalLocation(),false,nullptr,ETeleportType::TeleportPhysics);
        if(auto* Body=Mesh->GetBodyInstance(Hero->RigBone(TEXT("body")))) { Body->LinearDamping=0; Body->UpdateDampingProperties(); }
        Mesh->SetAllPhysicsLinearVelocity(Velocity);Mesh->SetAllPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
        return Hero;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCThroatPhysicalPlayerSweepTest,"MessControl.Throat.PlayerSafety.PhysicalRagdollCannotSkipAperture",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCThroatPhysicalPlayerSweepTest::RunTest(const FString&)
{
    using namespace MCThroatPlayerSafetyTests;
    FFixture F;
    if(!TestNotNull(TEXT("Artist throat mesh is available"),F.Throat->AuthoredMouth->GetSkeletalMeshAsset())) return false;
    auto* Hero=F.FallenPlayer(F.Inlet-F.Inward*50,F.Inward*1400);
    if(!TestTrue(TEXT("The hit created a real simulated ragdoll"),Hero->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll
        && Hero->GetMesh()->IsSimulatingPhysics(Hero->RigBone(TEXT("body"))))) return false;
    F.Throat->Tick(1.f/120);
    if(!TestTrue(TEXT("The player starts alive in front of the opening"),Hero->Status->IsAlive())) return false;
    const FVector CapsuleBefore=Hero->GetActorLocation();
    F.Step(.475f);
    const FVector BodyAfter=Hero->ToothPhysics->PhysicalLocation();
    const double Depth=FVector::DotProduct(BodyAfter-F.Inlet,F.Inward);
    AddInfo(FString::Printf(TEXT("Actual physical flight: depth=%.1f cm, capsule drift=%.1f cm"),Depth,FVector::Dist(CapsuleBefore,Hero->GetActorLocation())));
    if(!TestTrue(TEXT("Real physics moved the body beyond the old 500 cm point-check volume"),Depth>500)) return false;
    TestTrue(TEXT("The capsule still holds its earlier position"),Hero->GetActorLocation().Equals(CapsuleBefore,.1f));
    F.Throat->Tick(1.f/120);
    TestFalse(TEXT("Swept physical entry uses ordinary player death instead of leaving a live ragdoll behind the throat"),Hero->Status->IsAlive());
    auto* Current=F.FallenPlayer(F.Inlet-F.Inward*200,F.Inward*500);
    Current->GetMesh()->AddWorldOffset(F.Inward*230,false,nullptr,ETeleportType::TeleportPhysics);
    F.Throat->Tick(1.f/120);
    TestFalse(TEXT("A detached body currently inside is detected before its capsule catches up"),Current->Status->IsAlive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCThroatPlayerBoundaryTest,"MessControl.Throat.PlayerSafety.FrontStripTissueAndTeleportRemainSafe",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCThroatPlayerBoundaryTest::RunTest(const FString&)
{
    using namespace MCThroatPlayerSafetyTests;
    FFixture F;
    auto* Front=F.Player(F.Inlet-F.Inward*100);
    auto* Side=F.Player(F.Inlet-F.Inward*100+F.Right*1200);
    auto* Teleported=F.Player(F.Inlet-F.Inward*300);
    F.Throat->Tick(1.f/120);
    TestTrue(TEXT("The usable front strip is not lethal"),Front->Status->IsAlive());
    // A passage outside the artist opening never becomes a broad lethal box.
    Side->SetActorLocation(F.Inlet+F.Inward*70+F.Right*1200,false,nullptr,ETeleportType::TeleportPhysics);
    // A distant reset must not be interpreted as physical travel through the hole.
    Teleported->SetActorLocation(F.Inlet+F.Inward*700,false,nullptr,ETeleportType::TeleportPhysics);
    F.Throat->Tick(1.f/120);
    TestTrue(TEXT("Side tissue outside the opening does not count as entering the throat"),Side->Status->IsAlive());
    TestTrue(TEXT("A discontinuous teleport across the opening is not treated as a swept flight"),Teleported->Status->IsAlive());
    TestTrue(TEXT("Checking other players preserves the front-strip player"),Front->Status->IsAlive());
    // Current presence inside the opening remains lethal, even without history.
    auto* Inside=F.Player(F.Inlet+F.Inward*30);
    F.Throat->Tick(1.f/120);
    TestFalse(TEXT("A player actually inside the aperture still dies immediately"),Inside->Status->IsAlive());
    return true;
}
#endif
