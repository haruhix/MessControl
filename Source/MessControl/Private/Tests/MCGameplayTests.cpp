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
#endif
