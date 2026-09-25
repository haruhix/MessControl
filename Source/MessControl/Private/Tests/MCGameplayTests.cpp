#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCTaskActor.h"
#include "MCToothCharacter.h"
#include "MCArenaTooth.h"
#include "MCArenaToothSocket.h"
#include "MCToothStatusComponent.h"
#include "MCFoodActor.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
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
        void CompleteDay()
        {
            for (TActorIterator<AActor> It(World);It;++It)
                if (auto* Status=It->FindComponentByClass<UMCToothStatusComponent>())
                    for (int32 I=0;I<100;++I) { Status->CareContact(true); Status->CareContact(false); }
            for (TActorIterator<AMCFoodActor> It(World);It;++It) It->Dispose();
            Mode->UpdateObjectives();
        }
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
        Mouth.CompleteDay();
    }
    TestEqual(TEXT("Win after seven days"),Mouth.State->Phase,EMCShiftPhase::Won);
    TestEqual(TEXT("Healthy mouth"),Mouth.State->MouthHealth,100.f);
    Mouth.Mode->RestartShift(); TestEqual(TEXT("Restart resets day"),Mouth.State->Day,0); TestEqual(TEXT("Restart resets phase"),Mouth.State->Phase,EMCShiftPhase::Intermission);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCWorkValidationTest,"MessControl.Gameplay.WorkValidationAndCooperation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCWorkValidationTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth; auto* A=Mouth.Worker(); auto* B=Mouth.Worker(); auto* Tooth=Mouth.State->ArenaTeeth[0].Get();
    Tooth->SetCoffee(1); const FVector Near=Tooth->GetActorLocation()+FVector(0,Tooth->Body->Bounds.BoxExtent.Y+75,0);
    A->SetActorLocation(Near); A->SetActorRotation(FRotator(0,-90,0)); A->bHandling=true;
    for (int32 I=0;I<10;++I) A->AdvanceCare(.1f);
    TestEqual(TEXT("Care tool cannot remove coffee"),Tooth->Status->State.CoffeeLeft,4);
    A->bHandling=false; A->bBrushing=true;
    for (int32 I=0;I<4;++I) A->AdvanceCare(.1f);
    TestEqual(TEXT("0.4 seconds is not a full contact"),Tooth->Status->State.CoffeeLeft,4);
    A->AdvanceCare(.1f); TestEqual(TEXT("0.5 seconds removes exactly one layer"),Tooth->Status->State.CoffeeLeft,3);
    for (int32 I=0;I<3;++I) A->AdvanceCare(.1f);
    A->SetActorLocation(Near+FVector(0,500,0)); A->AdvanceCare(.1f);
    TestEqual(TEXT("Leaving contact resets partial work"),A->ContactProgress,0.f);
    A->SetActorLocation(Near); A->AdvanceCare(.1f); A->AdvanceCare(.1f);
    TestEqual(TEXT("Interrupted work cannot complete early"),Tooth->Status->State.CoffeeLeft,3);
    Tooth->SetCoffee(1); A->ResetContact(); B->SetActorLocation(Near+FVector(60,0,0)); B->SetActorRotation(FRotator(0,-90,0)); B->bBrushing=true;
    for (int32 I=0;I<10;++I) { A->AdvanceCare(.1f); B->AdvanceCare(.1f); }
    TestEqual(TEXT("Two workers clear four contacts in one second"),Tooth->Status->State.CoffeeLeft,0);
    TestFalse(TEXT("No over-cleaning"),Tooth->Status->CareContact(true));
    Tooth->SetCoffee(1); A->ResetContact(); A->AdvanceCare(-1); A->AdvanceCare(std::numeric_limits<float>::quiet_NaN());
    TestEqual(TEXT("Invalid delta cannot add work"),A->ContactProgress,0.f);
    A->AdvanceCare(1000); TestEqual(TEXT("Oversized delta is clamped to one tick"),Tooth->Status->State.CoffeeLeft,4);
    A->SetActorRotation(FRotator(0,90,0)); A->AdvanceCare(.1f); TestEqual(TEXT("Facing away resets contact"),A->ContactProgress,0.f);
    A->SetActorRotation(FRotator(0,-90,0)); Mouth.State->Phase=EMCShiftPhase::Lost;
    TestFalse(TEXT("Finished run rejects work"),A->CanContact(Tooth));
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
    TestTrue(TEXT("No legacy placeholder tasks spawned"),Mouth.Tasks().IsEmpty());
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
    auto* PC=Mouth.World->SpawnActor<APlayerController>(); Mouth.World->SpawnActor<APlayerController>();
    TestEqual(TEXT("Two player controllers occupy the session"),Mouth.Mode->GetNumPlayers(),2);
    Mouth.Mode->PreLogin(TEXT(""),TEXT("127.0.0.1"),FUniqueNetIdRepl(),Error);
    TestTrue(TEXT("Configured two-player session rejects third player"),!Error.IsEmpty());

    auto* Worker=Mouth.Worker();
    PC->Possess(Worker);
    for (int32 Day=1;Day<=2;++Day)
    {
        Mouth.NextPhase(); Mouth.State->MouthHealth=39.5f;
        Mouth.CompleteDay();
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCArenaToothTest,"MessControl.Gameplay.ArenaToothLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCArenaToothTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth;
    TestEqual(TEXT("Eight real teeth, separate from players"),Mouth.State->ArenaTeeth.Num(),8);
    TestEqual(TEXT("Eight available"),Mouth.State->AvailableArenaTeeth(),8);
    TSet<int32> Ids;
    for (AMCArenaTooth* Tooth:Mouth.State->ArenaTeeth) Ids.Add(Tooth->State.ToothId);
    TestEqual(TEXT("Unique IDs"),Ids.Num(),8);
    AMCArenaTooth* Tooth=Mouth.State->ArenaTeeth[2];
    Tooth->SetCoffee(1);
    TestEqual(TEXT("Coffee does not silently deal damage"),Tooth->State.Health,100.f);
    TestFalse(TEXT("Negative damage rejected"),Tooth->ReceiveArenaHit(-25,FVector::RightVector));
    TestFalse(TEXT("NaN damage rejected"),Tooth->ReceiveArenaHit(std::numeric_limits<float>::quiet_NaN(),FVector::RightVector));
    Tooth->ReceiveArenaHit(75,FVector::RightVector);
    TestTrue(TEXT("Low health becomes loose"),Tooth->IsLoose());
    Mouth.NextPhase(); Mouth.NextPhase(); // Starting and timing out a day must not rebuild the teeth.
    TestTrue(TEXT("Same physical tooth across days"),Mouth.State->ArenaTeeth[2]==Tooth);
    TestEqual(TEXT("Damage persists across days"),Tooth->State.Health,25.f);
    TestEqual(TEXT("Coffee persists across days"),Tooth->State.Coffee,1.f);
    const float MouthHealth=Mouth.State->MouthHealth;
    Tooth->ReceiveArenaHit(100,FVector::RightVector);
    TestEqual(TEXT("Only this tooth is lost"),Mouth.State->AvailableArenaTeeth(),7);
    TestEqual(TEXT("Tooth damage is separate from mouth health"),Mouth.State->MouthHealth,MouthHealth);
    TestFalse(TEXT("Lost tooth cannot take damage twice"),Tooth->ReceiveArenaHit(25,FVector::RightVector));
    TestEqual(TEXT("Lost health clamped"),Tooth->State.Health,0.f);
    TestEqual(TEXT("No double spending"),Mouth.State->AvailableArenaTeeth(),7);
    Mouth.Mode->RestartShift();
    TestEqual(TEXT("Restart restores real teeth"),Mouth.State->AvailableArenaTeeth(),8);
    int32 Actors=0; for (TActorIterator<AMCArenaTooth> It(Mouth.World);It;++It) ++Actors;
    TestEqual(TEXT("Restart does not duplicate actors"),Actors,8);
    TestNotNull(TEXT("Editable profile exists"),LoadObject<UMCArenaToothProfile>(nullptr,TEXT("/Game/Data/DA_ArenaTooth.DA_ArenaTooth")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCArenaBrushTest,"MessControl.Gameplay.ArenaBrushHit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCArenaBrushTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth; auto* Worker=Mouth.Worker(); AMCArenaTooth* Tooth=Mouth.State->ArenaTeeth[0];
    Worker->SetActorLocation(Tooth->GetActorLocation()+FVector(0,125,0)); Worker->SetActorRotation(FRotator(0,-90,0));
    Worker->SwingBrush(); Worker->SwingBrush();
    for (int32 I=0;I<3;++I) { ++GFrameCounter; Mouth.World->Tick(LEVELTICK_All,0.1f); }
    TestEqual(TEXT("One validated swing deals one hit"),Tooth->State.Health,75.f);
    TestEqual(TEXT("One confirmed arena hit"),Worker->ConfirmedHitCount,1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCArenaPlacementTest,"MessControl.Gameplay.ArenaAuthoredPlacement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCArenaPlacementTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth;
    auto* Left=Mouth.World->SpawnActor<AMCArenaToothSocket>(FVector(-320,-815,0),FRotator(0,10,3));
    auto* Right=Mouth.World->SpawnActor<AMCArenaToothSocket>(FVector(420,815,0),FRotator::ZeroRotator);
    Left->ToothId=7; Left->SetActorScale3D(FVector(2,2,3));
    Right->ToothId=2; Right->SetActorScale3D(FVector(3,2,2));
    Mouth.Mode->RestartShift();
    TestEqual(TEXT("Only authored teeth, no extra inner row"),Mouth.State->ArenaTeeth.Num(),2);
    TestEqual(TEXT("Order follows authored identity"),Mouth.State->ArenaTeeth[0]->State.ToothId,2);
    AMCArenaTooth* Tooth=Mouth.State->ArenaTeeth[1];
    TestEqual(TEXT("Authored ID retained"),Tooth->State.ToothId,7);
    TestTrue(TEXT("Original mesh pivot, rotation and scale retained"),Tooth->Visual->GetComponentTransform().Equals(Left->Preview->GetComponentTransform(),0.01f));
    TestTrue(TEXT("Editor preview hidden in gameplay"),Left->Preview->bHiddenInGame);
    Tooth->ReceiveArenaHit(100,FVector(0,1,0));
    TestEqual(TEXT("Losing a row tooth removes a concrete reserve"),Mouth.State->AvailableArenaTeeth(),1);
    Mouth.Mode->RestartShift();
    TestEqual(TEXT("Restart keeps authored layout"),Mouth.State->ArenaTeeth.Num(),2);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCareStatusTest,"MessControl.Gameplay.SharedStatusAndCare",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCareStatusTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth; auto* Hero=Mouth.Worker(); auto* S=Hero->Status.Get();
    S->ApplyCoffee(); S->Damage(75);
    TestTrue(TEXT("Players share coffee, damage and loose statuses"),S->IsLoose() && S->State.CoffeeLeft==4 && S->State.Health==25);
    Hero->bSelfCare=true; Hero->bHandling=true;
    for (int32 I=0;I<20;++I) Hero->AdvanceCare(.1f);
    TestEqual(TEXT("Four care contacts restore health"),S->State.Health,100.f);
    TestFalse(TEXT("Care secures the tooth"),S->IsLoose());
    TestEqual(TEXT("Treatment leaves coffee for the brush"),S->State.CoffeeLeft,4);
    Hero->bHandling=false; Hero->bBrushing=true;
    for (int32 I=0;I<20;++I) Hero->AdvanceCare(.1f);
    TestEqual(TEXT("Solo self brushing works"),S->State.CoffeeLeft,0);
    S->Settings.CoffeeContacts=6; S->Settings.ContactSeconds=.2f; S->ApplyCoffee();
    for (int32 I=0;I<12;++I) Hero->AdvanceCare(.1f);
    TestEqual(TEXT("Data controls both timing and number of contacts"),S->State.CoffeeLeft,0);
    S->Damage(100); TestFalse(TEXT("Death cannot be treated as ordinary damage"),S->CareContact(false));
    TestFalse(TEXT("Dead players cannot act or get up"),Hero->ToothPhysics->CanAct() || Hero->ToothPhysics->TryRecover());
    TestNotNull(TEXT("Care asset exists"),LoadObject<UMCToothCareProfile>(nullptr,TEXT("/Game/Data/DA_ToothCare.DA_ToothCare")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCRespawnTest,"MessControl.Gameplay.ConcreteReserveRespawn",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCRespawnTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth; auto* A=Mouth.Worker(); auto* B=Mouth.Worker();
    auto* PC1=Mouth.World->SpawnActor<APlayerController>(); auto* PC2=Mouth.World->SpawnActor<APlayerController>(); PC1->Possess(A); PC2->Possess(B);
    Mouth.State->ArenaTeeth[0]->ReceiveArenaHit(100,FVector::ForwardVector);
    auto* Source=Mouth.State->ArenaTeeth[1].Get(); Source->SetCoffee(1); Source->ReceiveArenaHit(25,FVector::ForwardVector);
    A->Status->Damage(100); B->Status->Damage(100); A->RespawnAt=-1; B->RespawnAt=-1;
    Mouth.Mode->ProcessRespawns();
    auto* NewA=Cast<AMCToothCharacter>(PC1->GetPawn()); auto* NewB=Cast<AMCToothCharacter>(PC2->GetPawn());
    TestTrue(TEXT("Both controllers receive new heroes"),NewA!=A && NewB!=B && NewA && NewB);
    if (NewA==A || NewB==B || !NewA || !NewB) return false;
    TestEqual(TEXT("Destroyed reserve is skipped"),NewA->RespawnSourceId,2);
    TestEqual(TEXT("Second death consumes another tooth"),NewB->RespawnSourceId,3);
    TestEqual(TEXT("Seven reserves minus two respawns"),Mouth.State->AvailableArenaTeeth(),5);
    TestTrue(TEXT("Spent tooth leaves a visible and physical gap"),Source->State.bConsumed && Source->IsHidden() && Source->Body->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
    TestEqual(TEXT("Inherited damage"),NewA->Status->State.Health,75.f);
    TestEqual(TEXT("Inherited coffee"),NewA->Status->State.CoffeeLeft,4);
    TestFalse(TEXT("Spent tooth cannot be consumed again"),Source->ConsumeForRespawn());
    Mouth.Mode->ProcessRespawns(); TestEqual(TEXT("Queue does not charge twice"),Mouth.State->AvailableArenaTeeth(),5);
    for (AMCArenaTooth* Tooth:Mouth.State->ArenaTeeth) if (Tooth->IsAvailable()) Tooth->ConsumeForRespawn();
    Mouth.Mode->Tick(.01f); TestTrue(TEXT("Zero reserves with living players is playable"),Mouth.State->Phase!=EMCShiftPhase::Lost);
    NewA->Status->Damage(100); NewB->Status->Damage(100); Mouth.Mode->Tick(.01f);
    TestEqual(TEXT("No players and no reserves loses"),Mouth.State->Phase,EMCShiftPhase::Lost);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodInteractionTest,"MessControl.Gameplay.FoodGripPullAndImpact",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodInteractionTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth; auto* A=Mouth.Worker(); A->SetActorLocation(FVector(-100,0,100)); A->SetActorRotation(FRotator::ZeroRotator); A->bHandling=true;
    auto* Food=Mouth.World->SpawnActor<AMCFoodActor>(FVector(0,0,100),FRotator::ZeroRotator);
    TestTrue(TEXT("Near food can be gripped"),Food->TryGrab(A));
    TestTrue(TEXT("Grip links both sides"),A->HeldFood==Food && Food->Holders.Num()==1);
    TestTrue(TEXT("Repeated grab is idempotent"),Food->TryGrab(A) && Food->Holders.Num()==1);
    A->SetActorLocation(FVector(-1000,0,100)); Food->Tick(.1f);
    TestTrue(TEXT("Overstretched grip releases"),!A->HeldFood && Food->Holders.IsEmpty());
    TestFalse(TEXT("Remote grab rejected"),Food->TryGrab(A));
    A->SetActorLocation(FVector(-100,0,100)); Food->TryGrab(A);
    Food->Phase=EMCFoodPhase::Stuck; Food->PullDirection=FVector(-1,0,0);
    A->GetCharacterMovement()->Velocity=FVector(0,55,0);
    for (int32 I=0;I<10;++I) Food->Tick(.1f);
    TestEqual(TEXT("Wrong pull direction gives no extraction"),Food->PullProgress,0.f);
    A->GetCharacterMovement()->Velocity=FVector(-55,0,0);
    for (int32 I=0;I<31;++I) Food->Tick(.1f);
    TestEqual(TEXT("Directional pulling frees food"),Food->Phase,EMCFoodPhase::Free);
    Food->Dispose(); TestTrue(TEXT("Disposal releases grip and hides food"),!A->HeldFood && Food->IsDisposed() && Food->IsHidden());
    TestFalse(TEXT("Disposed food cannot be grabbed again"),Food->TryGrab(A));
    auto* ImpactFood=Mouth.World->SpawnActor<AMCFoodActor>(FVector(0,0,350),FRotator::ZeroRotator);
    // Advance past the spawn recovery grace period. Synthetic contact uses the real hit delegate.
    for (int32 I=0;I<8;++I) { ++GFrameCounter; Mouth.World->Tick(LEVELTICK_All,.1f); }
    FHitResult Hit; Hit.ImpactPoint=A->GetActorLocation();
    ImpactFood->Body->OnComponentHit.Broadcast(ImpactFood->Body,A,A->GetCapsuleComponent(),FVector(0,0,9000),Hit);
    const float Health=A->Status->State.Health;
    TestTrue(TEXT("Food impact damages player"),Health<100);
    TestEqual(TEXT("Food impact causes ragdoll"),A->ToothPhysics->GetBodyState(),EMCBodyState::Ragdoll);
    ImpactFood->Body->OnComponentHit.Broadcast(ImpactFood->Body,A,A->GetCapsuleComponent(),FVector(0,0,9000),Hit);
    TestEqual(TEXT("Duplicate physics callbacks do not deal damage twice"),A->Status->State.Health,Health);
    TestNotNull(TEXT("Food tuning asset exists"),LoadObject<UMCFoodProfile>(nullptr,TEXT("/Game/Data/DA_FoodPhysics.DA_FoodPhysics")));
    return true;
}
#endif
