#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCThroat.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

namespace MCRecoverySafetyTestsPrivate
{
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Hero=nullptr;
    AMCGameMode* Mode=nullptr;
    AMCGameState* State=nullptr;
    float FloorZ=0;
    FWorld(bool ReserveFlow=false)
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL; URL.AddOption(ReserveFlow?TEXT("game=/Script/MessControl.MCGameMode"):TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);
        Mode=World->GetAuthGameMode<AMCGameMode>();
        if(Mode) { Mode->bUseDayOnePlan=false; Mode->bUseAdaptiveDirector=false; Mode->bUseSingleDayLoop=false; }
        World->InitializeActorsForPlay(URL); World->BeginPlay();
        if(Mode) {
            Mode->SetActorTickEnabled(false);
            State=World->GetGameState<AMCGameState>(); State->bDevManualEvents=true;
            State->Phase=EMCShiftPhase::Working; State->PhaseEndsAt=0;
            for(TActorIterator<AMCThroat> It(World);It;++It) { It->SetActorTickEnabled(false); It->SetActorEnableCollision(false); }
        }
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(10000,0,2000),FRotator::ZeroRotator);
        Hero->GetCharacterMovement()->DisableMovement();
        FloorZ=2000-Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        Box(FVector(10000,0,FloorZ-10),FVector(2000,2000,10));
        Hero->ToothPhysics->Settings.RagdollSeconds=6.f;
        Step(.8f);
    }
    ~FWorld()
    { World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
    void Step(float Seconds)
    { for(int32 I=0;I<FMath::CeilToInt(Seconds*60);++I) { ++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60); } }
    AActor* Box(FVector Location,FVector Extent,bool Overhang=false)
    {
        auto* Actor=World->SpawnActor<AActor>(); auto* Shape=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Shape); Shape->SetBoxExtent(Extent);
        Shape->SetCollisionProfileName(TEXT("BlockAll"));
        // A physical overhead obstruction, rather than an extra floor: the
        // standing capsule and ragdoll collide, while floor rays reach the floor.
        if(Overhang) Shape->SetCollisionResponseToChannel(ECC_WorldStatic,ECR_Ignore);
        Shape->RegisterComponent(); Actor->SetActorLocation(Location); return Actor;
    }
    AActor* TrapFallenBody()
    {
        Hero->ToothPhysics->ApplyHit(FVector(250,0,0),Hero->GetActorLocation());
        Step(.8f); // Give the actual simulated body time to lie down first.
        auto* Roof=Box(FVector(10000,0,FloorZ+590),FVector(1800,1800,500),true);
        Hero->ToothPhysics->Settings.RagdollSeconds=.5f;
        return Roof;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBlockedRecoveryReserveSafety,
    "MessControl.Physics.Recovery.BlockedBodyUsesOrdinaryReserveDeath",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBlockedRecoveryReserveSafety::RunTest(const FString&)
{
    using namespace MCRecoverySafetyTestsPrivate;
    FWorld F(true); auto* PC=F.World->SpawnActor<APlayerController>(); PC->Possess(F.Hero);
    const int32 Reserves=F.State->AvailableArenaTeeth();
    if(!TestTrue(TEXT("The normal arena has a replacement reserve"),Reserves>0)) return false;
    F.TrapFallenBody();
    TestFalse(TEXT("The standing capsule genuinely cannot fit the physical enclosure"),F.Hero->ToothPhysics->TryRecover());
    F.Step(2.f);
    TestTrue(TEXT("Blocked get-up allows a retry window instead of immediately killing the player"),F.Hero->Status->IsAlive());
    for(int32 I=0;I<24 && F.Hero->Status->IsAlive();++I) {
        F.Hero->ToothPhysics->ApplyHit(FVector(5,0,0),F.Hero->ToothPhysics->PhysicalLocation());
        F.Step(.3f);
    }
    if(!TestFalse(TEXT("Repeated contacts cannot leave an unsupported living ragdoll forever"),F.Hero->Status->IsAlive())) return false;
    TestTrue(TEXT("The body stayed above the ordinary out-of-mouth kill height"),F.Hero->ToothPhysics->PhysicalLocation().Z>0);
    TestTrue(TEXT("Ordinary death schedules the controller's normal respawn delay"),F.Hero->RespawnAt>F.State->GetServerWorldTimeSeconds());
    TestEqual(TEXT("The retry fallback never pretends to find a standing pose"),F.Hero->ToothPhysics->RecoveryCount,0);
    F.Mode->ProcessRespawns();
    TestTrue(TEXT("The controller retains its dead body during the normal wait"),PC->GetPawn()==F.Hero);
    F.Hero->RespawnAt=-1; F.Mode->ProcessRespawns();
    auto* Replacement=Cast<AMCToothCharacter>(PC->GetPawn());
    if(!TestTrue(TEXT("Normal reserve processing replaces the trapped body"),Replacement && Replacement!=F.Hero)) return false;
    TestEqual(TEXT("Exactly one real reserve is consumed"),F.State->AvailableArenaTeeth(),Reserves-1);
    TestEqual(TEXT("The replacement is healthy"),Replacement->Status->State.Health,Replacement->Status->State.MaxHealth);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCRecoveryBlockClearsSafely,
    "MessControl.Physics.Recovery.TemporaryObstructionPreservesPlayerAndResetsDeadline",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCRecoveryBlockClearsSafely::RunTest(const FString&)
{
    using namespace MCRecoverySafetyTestsPrivate;
    FWorld F; const float Health=F.Hero->Status->State.Health;
    for(int32 Attempt=0;Attempt<2;++Attempt) {
        F.Hero->ToothPhysics->Settings.RagdollSeconds=6.f;
        auto* Roof=F.TrapFallenBody();
        if(!TestFalse(TEXT("A real temporary obstruction prevents unsafe get-up"),F.Hero->ToothPhysics->TryRecover())) return false;
        F.Step(2.f);
        TestTrue(TEXT("Each independent knockdown receives its own recovery window"),F.Hero->Status->IsAlive());
        Roof->Destroy(); F.Step(3.f);
        TestTrue(TEXT("Removing the obstruction restores normal control"),F.Hero->ToothPhysics->CanAct());
        TestEqual(TEXT("Legitimate recovery preserves health"),F.Hero->Status->State.Health,Health);
        TestTrue(TEXT("Recovery restores the upright capsule"),F.Hero->GetActorUpVector().Z>.99f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCThroatOwnsCapturedBody,
    "MessControl.Physics.Recovery.ThroatCaptureRejectsExternalKnockdown",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCThroatOwnsCapturedBody::RunTest(const FString&)
{
    using namespace MCRecoverySafetyTestsPrivate;
    FWorld F; auto* Throat=F.World->SpawnActor<AMCThroat>(FVector(100000,0,2000),FRotator::ZeroRotator);
    Throat->SetActorTickEnabled(false); Throat->SetActorEnableCollision(false);
    F.Hero->SetThroatCapture(Throat); F.Step(.8f); // Outlast ordinary recovery invulnerability.
    const int32 Knockdowns=F.Hero->ToothPhysics->KnockdownCount;
    F.Hero->ToothPhysics->ApplyHit(FVector(900,0,300),F.Hero->GetActorLocation());
    TestEqual(TEXT("A boss impulse cannot re-enter ragdoll during the gulp"),F.Hero->ToothPhysics->KnockdownCount,Knockdowns);
    TestEqual(TEXT("The gulp retains its controlled body state"),F.Hero->ToothPhysics->GetBodyState(),EMCBodyState::Standing);
    TestFalse(TEXT("A captured player cannot start get-up"),F.Hero->ToothPhysics->TryRecover());
    TestFalse(TEXT("The gulp retains ownership of skeletal simulation"),F.Hero->GetMesh()->IsSimulatingPhysics(F.Hero->RigBone(TEXT("body"))));
    TestEqual(TEXT("The mesh stays attached to the capture capsule"),F.Hero->GetMesh()->GetAttachParent(),static_cast<USceneComponent*>(F.Hero->GetCapsuleComponent()));
    F.Hero->SetThroatCapture(nullptr); F.Step(.8f);
    F.Hero->ToothPhysics->ApplyHit(FVector(900,0,300),F.Hero->GetActorLocation());
    TestTrue(TEXT("Normal impacts work again after release"),F.Hero->ToothPhysics->KnockdownCount>Knockdowns);
    return true;
}
#endif
