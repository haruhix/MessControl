#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCTaskActor.h"
#include "MCToothCharacter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MCToothPhysicsComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include <limits>

namespace
{
    struct FTestMouth
    {
        UWorld* World;
        UGameInstance* Instance;
        AMCGameMode* Mode;
        AMCGameState* State;
        FTestMouth()
        {
            World=UWorld::CreateWorld(EWorldType::Game,false);
            FWorldContext& Context=GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
            Instance=NewObject<UGameInstance>(GEngine); World->SetGameInstance(Instance);
            FURL URL; URL.AddOption(TEXT("game=/Script/MessControl.MCGameMode")); URL.AddOption(TEXT("Seed=41"));
            World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
            Mode=World->GetAuthGameMode<AMCGameMode>(); State=World->GetGameState<AMCGameState>();
        }
        ~FTestMouth() { World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
        void NextPhase() { State->PhaseEndsAt=State->GetServerWorldTimeSeconds()-1; Mode->Tick(0.01f); }
        TArray<AMCTaskActor*> Tasks() { TArray<AMCTaskActor*> Result; for (TActorIterator<AMCTaskActor> It(World);It;++It) if (It->Progress<1) Result.Add(*It); return Result; }
        AMCToothCharacter* Worker()
        {
            auto* Tooth=World->SpawnActor<AMCToothCharacter>(FVector(0,0,98),FRotator::ZeroRotator);
            Tooth->GetCharacterMovement()->DisableMovement(); return Tooth;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSevenDayTest,"MessControl.Gameplay.SevenDayVictory",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSevenDayTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth; if (!TestNotNull(TEXT("Authority game mode"),Mouth.Mode) || !TestNotNull(TEXT("Game state"),Mouth.State)) return false;
    AMCToothCharacter* Worker=Mouth.Worker(); UMCDayEvent* Previous=nullptr;
    for (int32 Day=1;Day<=7;++Day)
    {
        Mouth.NextPhase(); TestEqual(TEXT("Sequential day"),Mouth.State->Day,Day);
        TestTrue(TEXT("No immediate event repeat"),Previous!=Mouth.State->CurrentEvent); Previous=Mouth.State->CurrentEvent;
        TestTrue(TEXT("Tasks spawned"),Mouth.State->TasksLeft>0);
        for (AMCTaskActor* Task:Mouth.Tasks())
        {
            Worker->SetActorLocation(Task->GetActorLocation()+FVector(-60,0,58));
            for (int32 I=0;I<100 && Task->Progress<1;++I) Task->ApplyWork(Worker,Task->Kind==EMCTaskKind::Coffee,0.2f);
            TestEqual(TEXT("Task complete"),Task->Progress,1.f);
            TestFalse(TEXT("Cannot complete twice"),Task->ApplyWork(Worker,Task->Kind==EMCTaskKind::Coffee,0.2f));
        }
    }
    TestEqual(TEXT("Win after seven days"),Mouth.State->Phase,EMCShiftPhase::Won);
    TestEqual(TEXT("Healthy mouth"),Mouth.State->MouthHealth,100.f);
    Mouth.Mode->RestartShift(); TestEqual(TEXT("Restart resets day"),Mouth.State->Day,0); TestEqual(TEXT("Restart resets phase"),Mouth.State->Phase,EMCShiftPhase::Intermission);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCWorkValidationTest,"MessControl.Gameplay.WorkValidationAndCooperation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCWorkValidationTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth; Mouth.NextPhase(); auto Tasks=Mouth.Tasks(); if (!TestTrue(TEXT("Task exists"),!Tasks.IsEmpty())) return false;
    AMCTaskActor* Task=Tasks[0]; auto* A=Mouth.Worker(); auto* B=Mouth.Worker(); const bool bBrush=Task->Kind==EMCTaskKind::Coffee;
    A->SetActorLocation(Task->GetActorLocation()+FVector(1000,0,58));
    TestFalse(TEXT("Out of range rejected"),Task->ApplyWork(A,bBrush,0.1f));
    A->SetActorLocation(Task->GetActorLocation()+FVector(60,0,58)); B->SetActorLocation(Task->GetActorLocation()+FVector(-60,0,58));
    TestFalse(TEXT("Wrong tool rejected"),Task->ApplyWork(A,!bBrush,0.1f));
    TestTrue(TEXT("Valid tool accepted"),Task->ApplyWork(A,bBrush,0.1f)); const float Once=Task->Progress;
    Task->ApplyWork(B,bBrush,0.1f); TestTrue(TEXT("Two players add work"),FMath::IsNearlyEqual(Task->Progress,Once*2));
    const float Before=Task->Progress; Task->ApplyWork(A,bBrush,-5); TestEqual(TEXT("Negative time cannot undo progress"),Task->Progress,Before);
    Task->ApplyWork(A,bBrush,1000); TestTrue(TEXT("Oversized time is clamped"),Task->Progress<Before+0.5f);
    A->SetActorLocation(Task->GetActorLocation()+FVector(0,0,400)); TestFalse(TEXT("Wrong height rejected"),Task->ApplyWork(A,bBrush,0.1f));
    Mouth.State->Phase=EMCShiftPhase::Intermission; TestFalse(TEXT("Intermission rejects work"),Task->ApplyWork(B,bBrush,0.1f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCTimeoutTest,"MessControl.Gameplay.TimeoutAndLoss",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCTimeoutTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth; Mouth.NextPhase(); const float Expected=FMath::Max(0.f,100.f-Mouth.State->TasksLeft*Mouth.State->CurrentEvent->MissedTaskDamage);
    Mouth.NextPhase(); TestEqual(TEXT("Missed tasks damage health"),Mouth.State->MouthHealth,Expected);
    Mouth.State->MouthHealth=1; Mouth.NextPhase(); Mouth.NextPhase();
    TestEqual(TEXT("Zero health loses run"),Mouth.State->Phase,EMCShiftPhase::Lost);
    TestEqual(TEXT("Health clamped to zero"),Mouth.State->MouthHealth,0.f);
    TestTrue(TEXT("Unfinished actors removed"),Mouth.Tasks().IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCRigTest,"MessControl.Physics.RigAndTuning",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCRigTest::RunTest(const FString& Parameters)
{
    USkeletalMesh* Mesh=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Art/Rig/SK_ToothHero.SK_ToothHero"));
    if (!TestNotNull(TEXT("Imported skeletal tooth"),Mesh)) return false;
    TestTrue(TEXT("Body bone exists"),Mesh->GetRefSkeleton().FindBoneIndex(TEXT("body"))!=INDEX_NONE);
    TestTrue(TEXT("Brush hand exists"),Mesh->GetRefSkeleton().FindBoneIndex(TEXT("hand_r"))!=INDEX_NONE);
    TestNotNull(TEXT("Squash morph"),Mesh->FindMorphTarget(TEXT("Squash")));
    TestNotNull(TEXT("Stretch morph"),Mesh->FindMorphTarget(TEXT("Stretch")));
    for (const auto& Slot:Mesh->GetMaterials()) TestNotNull(*FString::Printf(TEXT("Saved material %s"),*Slot.MaterialSlotName.ToString()),Slot.MaterialInterface.Get());
    const auto* Audio=LoadObject<UMCSoundPalette>(nullptr,TEXT("/Game/Data/DA_MouthSounds.DA_MouthSounds"));
    if (TestNotNull(TEXT("Sound palette"),Audio))
        for (const FName Event:{FName("Hit"),FName("Whoosh"),FName("Fall"),FName("StandUp")}) TestTrue(*Event.ToString(),Audio->Events.Contains(Event) && Audio->Events[Event].Sounds.Num()>0);
    UPhysicsAsset* Physics=Mesh->GetPhysicsAsset();
    if (TestNotNull(TEXT("Authored Physics Asset"),Physics))
    {
        TestEqual(TEXT("Seven simulated body shapes"),Physics->SkeletalBodySetups.Num(),7);
        TestEqual(TEXT("Six constrained joints"),Physics->ConstraintSetup.Num(),6);
    }
    FMCPhysicsSettings P; P.Knockback=std::numeric_limits<float>::quiet_NaN(); P.Mass=-1; P.GetUpSeconds=500;
    P.Sanitize(); TestEqual(TEXT("NaN restored to default"),P.Knockback,650.f);
    TestEqual(TEXT("Positive minimum mass"),P.Mass,3.f); TestEqual(TEXT("Recovery time bounded"),P.GetUpSeconds,2.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHitTest,"MessControl.Physics.AuthorityAndActionGates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCHitTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth; Mouth.NextPhase(); auto* A=Mouth.Worker(); auto* B=Mouth.Worker();
    A->SetActorLocation(FVector(0,0,150)); B->SetActorLocation(FVector(125,0,150)); A->SetActorRotation(FRotator::ZeroRotator);
    for (int32 I=0;I<7;++I) { ++GFrameCounter; Mouth.World->Tick(LEVELTICK_All,0.1f); }
    A->SwingBrush(); A->SwingBrush();
    TestEqual(TEXT("Repeated RPC respects cooldown"),A->ValidatedSwingCount,1);
    for (int32 I=0;I<3;++I) { ++GFrameCounter; Mouth.World->Tick(LEVELTICK_All,0.1f); }
    TestEqual(TEXT("One forward target receives one hit"),A->ConfirmedHitCount,1);
    TestEqual(TEXT("Strong hit enters ragdoll"),B->ToothPhysics->GetBodyState(),EMCBodyState::Ragdoll);
    B->SwingBrush(); TestEqual(TEXT("Fallen tooth cannot attack"),B->ValidatedSwingCount,0);
    const auto Tasks=Mouth.Tasks(); if (Tasks.Num())
    {
        B->SetActorLocation(Tasks[0]->GetActorLocation()+FVector(0,0,58));
        TestFalse(TEXT("Fallen tooth cannot work"),Tasks[0]->ApplyWork(B,Tasks[0]->Kind==EMCTaskKind::Coffee,0.1f));
    }
    TestFalse(TEXT("Cannot get up without floor support"),B->ToothPhysics->TryRecover());
    return true;
}
#endif
