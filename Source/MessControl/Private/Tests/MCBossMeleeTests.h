#pragma once
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCBossCharacter.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBossMeleeContact,"MessControl.Boss.MeleeCapsuleContact",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBossMeleeContact::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
    World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
    auto* Boss=World->SpawnActor<AMCBossCharacter>(FVector(0,0,2000),FRotator::ZeroRotator);
    auto* Player=World->SpawnActor<AMCToothCharacter>(FVector(105,0,1950),FRotator::ZeroRotator);
    auto* Controller=World->SpawnActor<APlayerController>();
    Controller->Possess(Player);
    Boss->GetCharacterMovement()->DisableMovement();Player->GetCharacterMovement()->DisableMovement();
    Boss->BodyHitbox->SetCapsuleSize(100,120,false);
    Player->GetCapsuleComponent()->SetCapsuleSize(45,65,false);
    Player->Status->Initialize(100);
    FMCBossAttackDefinition Attack;Attack.Range=250;Attack.VerticalReach=160;Attack.HalfAngleDegrees=55;Attack.Damage=24;
    TestTrue(TEXT("The possessed player is a living gameplay target"),AMCBossCharacter::IsLivingPlayer(Player));
    for (const FVector Offset:{FVector(105,0,-50),FVector(0,105,-50),FVector(-105,0,-50),FVector(0,0,60)})
    {
        Player->SetActorLocation(Boss->GetActorLocation()+Offset,false,nullptr,ETeleportType::TeleportPhysics);
        TestTrue(TEXT("Direct torso contact hits even beside, behind or directly under the boss"),Boss->IsPlayerInAttack(Player,Attack,FVector::ForwardVector));
    }
    Player->SetActorLocation(FVector(290,0,2000));
    TestTrue(TEXT("The player's capsule edge is inside melee reach"),Boss->IsPlayerInAttack(Player,Attack,FVector::ForwardVector));
    for (const FVector Offset:{FVector(310,0,0),FVector(-250,0,0),FVector(100,0,240)})
    {
        Player->SetActorLocation(Boss->GetActorLocation()+Offset);
        TestFalse(TEXT("Range, distant rear and height still reject hits"),Boss->IsPlayerInAttack(Player,Attack,FVector::ForwardVector));
    }
    Player->SetActorLocation(FVector(105,0,1950));
    auto* Wall=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Box);Box->SetBoxExtent(FVector(5,200,300));Box->SetCollisionProfileName(TEXT("BlockAll"));
    Box->RegisterComponent();Wall->SetActorLocation(FVector(50,0,2000));
    TestFalse(TEXT("A wall still blocks damage at contact distance"),Boss->IsPlayerInAttack(Player,Attack,FVector::ForwardVector));
    Wall->Destroy();
    Boss->Runtime.State=EMCBossState::Attacking;Boss->Runtime.AttackForward=FVector::ForwardVector;
    Boss->ExecuteAttack(Attack,Player);
    TestEqual(TEXT("A real native impact damages the contacting player"),Player->Status->State.Health,76.f);
    World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    return true;
}
#endif
