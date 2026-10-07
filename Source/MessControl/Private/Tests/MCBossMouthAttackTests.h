#pragma once
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCBossCharacter.h"
#include "MCBossClot.h"
#include "MCBossMouthAttackComponent.h"
#include "MCMouthSurface.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBossMouthSalvo,"MessControl.Boss.MouthSalvo",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBossMouthSalvo::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
    World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
    auto* Boss=World->SpawnActor<AMCBossCharacter>(FVector(0,0,2000),FRotator::ZeroRotator);
    auto* Player=World->SpawnActor<AMCToothCharacter>(FVector(250,0,2000),FRotator::ZeroRotator);
    auto* Controller=World->SpawnActor<APlayerController>();Controller->Possess(Player);
    Boss->GetCharacterMovement()->DisableMovement();Player->GetCharacterMovement()->DisableMovement();Player->Status->Initialize(100);
    Boss->Runtime.State=EMCBossState::Attacking;Boss->Runtime.Target=Player;Boss->Runtime.AttackForward=FVector::ForwardVector;
    Boss->Runtime.AttackSerial=1;
    FMCBossAttackDefinition Attack;Attack.bMouthClotAttack=true;Attack.ClotCount=1;Attack.Damage=24;Attack.ClotDamage=4;
    Attack.Range=850;Attack.HalfAngleDegrees=75;Attack.ActiveSeconds=2.2f;
    Boss->ExecuteAttack(Attack,Player);
    TestEqual(TEXT("Starting a mouth attack does not cause the former radial hit"),Player->Status->State.Health,100.f);
    AMCBossClot* First=nullptr;for(TActorIterator<AMCBossClot> It(World);It;++It) {First=*It;break;}
    for(int32 I=0;I<65;++I) World->Tick(LEVELTICK_All,1.f/60.f);
    // Synchronous World::Tick calls share one engine frame. Advance the actor explicitly
    // to the world's final clock, also exercising a long frame gap and curved sweep substeps.
    if(First && !First->bImpacted) First->Tick(1.f/60.f);
    TestEqual(TEXT("A real swept ballistic clot hits once even across a long frame gap"),Player->Status->State.Health,96.f);
    FHitResult PlayerHit(Player,Player->GetCapsuleComponent(),Player->GetActorLocation(),FVector::UpVector);
    for(int32 I=0;I<20;++I) Boss->MouthAttack->ResolveClotHit(1,PlayerHit);
    TestEqual(TEXT("Multiple collisions cannot exceed 24 damage per player per salvo"),Player->Status->State.Health,76.f);

    auto* Tongue=World->SpawnActor<AMCTongue>();Tongue->SetActorTickEnabled(false);
    TArray<FVector> Vertices={FVector(-1500,-1500,1500),FVector(1500,-1500,1500),FVector(1500,1500,1500),FVector(-1500,1500,1500)};
    TArray<int32> Indices={0,1,2,0,2,3};TArray<FVector> Normals;Normals.Init(FVector::UpVector,4);
    TArray<FVector2D> UV;UV.Init(FVector2D::ZeroVector,4);TArray<FColor> Colors;Colors.Init(FColor::White,4);
    TArray<FProcMeshTangent> Tangents;Tangents.Init(FProcMeshTangent(FVector::ForwardVector,false),4);
    Tongue->Surface->CreateMeshSection(0,Vertices,Indices,Normals,UV,Colors,Tangents,true);
    for(int32 I=0;I<8;++I) {
        FHitResult Hit;TestTrue(TEXT("The ulcer test uses the tongue's real triangle collision"),Tongue->SurfacePoint(FVector(-900+I*230,300,1510),Hit));
        Boss->MouthAttack->ResolveClotHit(1,Hit);
    }
    TestEqual(TEXT("Many tongue landings create at most two active boss ulcers"),Boss->MouthAttack->GetActiveUlcerCount(),2);
    ++Boss->Runtime.AttackSerial;Boss->MouthAttack->StartAttack(Attack);
    FHitResult More;Tongue->SurfacePoint(FVector(600,-300,1510),More);Boss->MouthAttack->ResolveClotHit(2,More);
    TestEqual(TEXT("The cap persists between salvos"),Boss->MouthAttack->GetActiveUlcerCount(),2);
    for(TActorIterator<AMCMouthSurface> It(World);It;++It) if(It->GetOwner()==Boss) {It->Healing=1;break;}
    Boss->MouthAttack->ResolveClotHit(2,More);
    TestEqual(TEXT("Healing a boss ulcer frees one slot"),Boss->MouthAttack->GetActiveUlcerCount(),2);
    const float Health=Player->Status->State.Health;
    Boss->DeactivateBoss();Boss->MouthAttack->ResolveClotHit(2,PlayerHit);
    TestEqual(TEXT("Deactivation invalidates stale hits"),Player->Status->State.Health,Health);
    int32 Flying=0;for(TActorIterator<AMCBossClot> It(World);It;++It) if(!It->IsActorBeingDestroyed()) ++Flying;
    TestEqual(TEXT("Deactivation removes airborne clots"),Flying,0);
    Boss->ResetForRun();TestEqual(TEXT("Encounter reset clears only its own ulcers"),Boss->MouthAttack->GetActiveUlcerCount(),0);
    World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return true;
}
#endif
