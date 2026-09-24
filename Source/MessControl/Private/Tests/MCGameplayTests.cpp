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
#include "GameFramework/PlayerController.h"
#include "MCToothPhysicsComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCRunRulesTest,"MessControl.Gameplay.RunRules",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCRunRulesTest::RunTest(const FString& Parameters)
{
    const auto* DefaultRules=LoadObject<UMCRunRules>(nullptr,TEXT("/Game/Data/DA_RunRules.DA_RunRules"));
    if (!TestNotNull(TEXT("Saved run rules asset exists"),DefaultRules)) return false;
    TestEqual(TEXT("Default mouth health"),DefaultRules->Settings.MaxMouthHealth,100.f);
    TestEqual(TEXT("Default days"),DefaultRules->Settings.DaysToSurvive,7);
    TestEqual(TEXT("Default players"),DefaultRules->Settings.MaxPlayers,4);
    TestEqual(TEXT("Planned arena teeth exclude starting players"),DefaultRules->Settings.InitialArenaTeeth,8);

    FTestMouth Mouth;
    auto* Custom=NewObject<UMCRunRules>(Mouth.Mode);
    Custom->Settings.MaxMouthHealth=40; Custom->Settings.DaysToSurvive=2;
    Custom->Settings.MaxPlayers=2; Custom->Settings.InitialArenaTeeth=3;
    Mouth.Mode->RunRulesProfile=Custom;
    TestEqual(TEXT("Changing profile does not alter active run"),Mouth.State->MouthHealth,100.f);
    Mouth.Mode->RestartShift();
    TestEqual(TEXT("Restart applies custom health"),Mouth.State->MouthHealth,40.f);
    TestEqual(TEXT("Run snapshot has custom player limit"),Mouth.State->RunSettings.MaxPlayers,2);
    TestEqual(TEXT("Run snapshot keeps planned arena count"),Mouth.State->RunSettings.InitialArenaTeeth,3);
    Custom->Settings.MaxMouthHealth=80; Custom->Settings.DaysToSurvive=5;

    // Exercise admission, rather than just comparing a copied setting.
    FString Error;
    Mouth.Mode->PreLogin(TEXT(""),TEXT("127.0.0.1"),FUniqueNetIdRepl(),Error);
    TestTrue(TEXT("Empty two-player session accepts a connection"),Error.IsEmpty());
    Mouth.World->SpawnActor<APlayerController>(); Mouth.World->SpawnActor<APlayerController>();
    TestEqual(TEXT("Two player controllers occupy the session"),Mouth.Mode->GetNumPlayers(),2);
    Mouth.Mode->PreLogin(TEXT(""),TEXT("127.0.0.1"),FUniqueNetIdRepl(),Error);
    TestTrue(TEXT("Configured two-player session rejects third player"),!Error.IsEmpty());

    auto* Worker=Mouth.Worker();
    for (int32 Day=1;Day<=2;++Day)
    {
        Mouth.NextPhase(); Mouth.State->MouthHealth=39.5f;
        for (AMCTaskActor* Task:Mouth.Tasks())
        {
            Worker->SetActorLocation(Task->GetActorLocation()+FVector(-60,0,58));
            for (int32 I=0;I<100 && Task->Progress<1;++I) Task->ApplyWork(Worker,Task->Kind==EMCTaskKind::Coffee,0.2f);
        }
        TestEqual(TEXT("Healing respects active snapshot, not edited asset"),Mouth.State->MouthHealth,40.f);
        TestEqual(TEXT("Victory follows custom two-day rule"),Mouth.State->Phase,Day==2?EMCShiftPhase::Won:EMCShiftPhase::Intermission);
    }
    Mouth.Mode->RestartShift();
    TestEqual(TEXT("Next run picks up profile changes"),Mouth.State->MouthHealth,80.f);
    TestEqual(TEXT("Next run picks up day changes"),Mouth.State->RunSettings.DaysToSurvive,5);
    Mouth.Mode->RunRulesProfile.Reset(); Mouth.Mode->RestartShift();
    TestEqual(TEXT("Missing optional profile falls back to defaults"),Mouth.State->MouthHealth,100.f);

    FMCRunSettings Invalid; Invalid.MaxMouthHealth=std::numeric_limits<float>::quiet_NaN();
    Invalid.DaysToSurvive=0; Invalid.MaxPlayers=99; Invalid.InitialArenaTeeth=-5; Invalid.Sanitize();
    TestEqual(TEXT("Finite default health"),Invalid.MaxMouthHealth,100.f);
    TestEqual(TEXT("At least one day"),Invalid.DaysToSurvive,1);
    TestEqual(TEXT("At most four players supported"),Invalid.MaxPlayers,4);
    TestEqual(TEXT("Arena count cannot be negative"),Invalid.InitialArenaTeeth,0);
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
        for (const UPhysicsConstraintTemplate* Joint:Physics->ConstraintSetup)
        {
            const auto& C=Joint->DefaultInstance;
            const FString Name=C.JointName.ToString();
            TestEqual(*FString::Printf(TEXT("%s saved swing 1 limit"),*Name),C.GetAngularSwing1Limit(),65.f);
            TestEqual(*FString::Printf(TEXT("%s saved swing 2 limit"),*Name),C.GetAngularSwing2Limit(),60.f);
            TestEqual(*FString::Printf(TEXT("%s saved twist limit"),*Name),C.GetAngularTwistLimit(),50.f);
            TestEqual(*FString::Printf(TEXT("%s swing constrained"),*Name),C.GetAngularSwing1Motion(),ACM_Limited);
            TestEqual(*FString::Printf(TEXT("%s swing 2 constrained"),*Name),C.GetAngularSwing2Motion(),ACM_Limited);
            TestEqual(*FString::Printf(TEXT("%s twist constrained"),*Name),C.GetAngularTwistMotion(),ACM_Limited);
            TestEqual(*FString::Printf(TEXT("%s linear X locked"),*Name),C.GetLinearXMotion(),LCM_Locked);
            TestEqual(*FString::Printf(TEXT("%s linear Y locked"),*Name),C.GetLinearYMotion(),LCM_Locked);
            TestEqual(*FString::Printf(TEXT("%s linear Z locked"),*Name),C.GetLinearZMotion(),LCM_Locked);
            TestFalse(*FString::Printf(TEXT("%s hard swing stops"),*Name),bool(C.ProfileInstance.ConeLimit.bSoftConstraint));
            TestFalse(*FString::Printf(TEXT("%s hard twist stops"),*Name),bool(C.ProfileInstance.TwistLimit.bSoftConstraint));
            TestEqual(*FString::Printf(TEXT("%s no duplicate target clamp"),*Name),C.ProfileInstance.AngularDrive.LimitViolationResponse,EAngularDriveLimitViolationResponse::None);
        }
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
