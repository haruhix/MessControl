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
#include "MCDayDirector.h"
#include "MCDayPlan.h"
#include "MCMouthSurface.h"
#include "MCCoffeeFlood.h"
#include "MCCoffeeProfile.h"
#include "Kismet/GameplayStatics.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "MCToothPhysicsComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Materials/MaterialInstanceDynamic.h"
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
            Mode->bUseDayOnePlan=false; // These regression cases cover the retained sandbox events.
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCArtistRigTest,"MessControl.Physics.ArtistRigIntegration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCArtistRigTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth; auto* Hero=Mouth.Worker();
    const auto* Profile=Hero->Appearance.Get();
    if (!TestNotNull(TEXT("Player appearance profile"),Profile) || !TestNotNull(TEXT("Artist skeletal mesh"),Profile->SkeletalMesh.Get())) return false;
    TestEqual(TEXT("Actual player uses artist mesh"),Hero->GetMesh()->GetSkeletalMeshAsset(),Profile->SkeletalMesh.Get());
    TestEqual(TEXT("Body mapped to pelvis"),Hero->RigBone(TEXT("body")),FName("root_x"));
    TestTrue(TEXT("Imported +Y faces character +X"),Profile->MeshTransform.TransformVectorNoScale(FVector::RightVector).Equals(FVector::ForwardVector,.001));
    const auto& Ref=Profile->SkeletalMesh->GetRefSkeleton();
    for (const FName BoneRole:{FName("body"),FName("arm_l"),FName("arm_r"),FName("hand_r"),FName("leg_l"),FName("leg_r"),FName("foot_l"),FName("foot_r")})
        TestTrue(*FString::Printf(TEXT("Mapped %s exists"),*BoneRole.ToString()),Ref.FindBoneIndex(Hero->RigBone(BoneRole))!=INDEX_NONE);
    auto* Asset=Hero->GetMesh()->GetPhysicsAsset();
    if (!TestNotNull(TEXT("Gameplay physics override"),Asset)) return false;
    TestEqual(TEXT("Thirteen physical bodies, no face bodies"),Asset->SkeletalBodySetups.Num(),13);
    TestEqual(TEXT("Twelve constrained joints"),Asset->ConstraintSetup.Num(),12);
    for (const UPhysicsConstraintTemplate* Joint:Asset->ConstraintSetup)
    {
        const auto& C=Joint->DefaultInstance;
        TestEqual(TEXT("Joint twist limited"),C.GetAngularTwistMotion(),ACM_Limited);
        TestEqual(TEXT("No joint translation"),C.GetLinearXMotion(),LCM_Locked);
        TestFalse(TEXT("Hard angular stops"),bool(C.ProfileInstance.ConeLimit.bSoftConstraint));
        TestTrue(TEXT("Both joint bones exist"),Ref.FindBoneIndex(C.ConstraintBone1)!=INDEX_NONE && Ref.FindBoneIndex(C.ConstraintBone2)!=INDEX_NONE);
    }
    TestEqual(TEXT("Brush attached to actual hand"),Hero->BrushPivot->GetAttachSocketName(),Hero->RigBone(TEXT("hand_r")));
    Hero->Status->ApplyCoffee(); Hero->Status->Damage(25); Hero->Tick(.016f);
    auto* Material=Cast<UMaterialInstanceDynamic>(Hero->GetMesh()->GetMaterial(0));
    if (TestNotNull(TEXT("Player status material"),Material))
    {
        TestEqual(TEXT("Coffee reaches textured material"),Material->K2_GetScalarParameterValue(TEXT("Coffee")),1.f);
        TestEqual(TEXT("Damage reaches textured material"),Material->K2_GetScalarParameterValue(TEXT("Damage")),.25f);
    }
    Hero->GetCharacterMovement()->DisableMovement();
    for (int32 I=0;I<7;++I) { ++GFrameCounter; Mouth.World->Tick(LEVELTICK_All,.1f); }
    Hero->ToothPhysics->ApplyHit(FVector(450,0,250),Hero->GetActorLocation());
    TestEqual(TEXT("Artist rig enters ragdoll"),Hero->ToothPhysics->GetBodyState(),EMCBodyState::Ragdoll);
    TestTrue(TEXT("Artist core actually simulates"),Hero->GetMesh()->IsSimulatingPhysics(Hero->RigBone(TEXT("body"))));
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
    A->bHandling=false; A->GetCharacterMovement()->Velocity=FVector::ZeroVector;
    // Advance past the spawn recovery grace period. Synthetic contact uses the real hit delegate.
    for (int32 I=0;I<8;++I) { ++GFrameCounter; Mouth.World->Tick(LEVELTICK_All,.1f); }
    auto* ImpactFood=Mouth.World->SpawnActor<AMCFoodActor>(FVector(0,0,350),FRotator::ZeroRotator);
    ImpactFood->Body->SetPhysicsLinearVelocity(FVector(0,0,-700)); ImpactFood->Tick(.016f);
    FHitResult Hit; Hit.ImpactPoint=A->GetActorLocation(); Hit.ImpactNormal=FVector::UpVector;
    ImpactFood->Body->OnComponentHit.Broadcast(ImpactFood->Body,A,A->GetCapsuleComponent(),FVector(0,0,9000),Hit);
    const float Health=A->Status->State.Health;
    TestTrue(TEXT("Food impact damages player"),Health<100);
    TestEqual(TEXT("Food impact causes ragdoll"),A->ToothPhysics->GetBodyState(),EMCBodyState::Ragdoll);
    ImpactFood->Body->OnComponentHit.Broadcast(ImpactFood->Body,A,A->GetCapsuleComponent(),FVector(0,0,9000),Hit);
    TestEqual(TEXT("Duplicate physics callbacks do not deal damage twice"),A->Status->State.Health,Health);
    TestNotNull(TEXT("Food tuning asset exists"),LoadObject<UMCFoodProfile>(nullptr,TEXT("/Game/Data/DA_FoodPhysics.DA_FoodPhysics")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodApproachTest,"MessControl.Gameplay.RestingFoodDoesNotLaunchPlayers",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodApproachTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth; auto* Hero=Mouth.Worker();
    Hero->SetActorLocation(FVector(-100,0,100)); Hero->SetActorRotation(FRotator::ZeroRotator);
    // Exit the spawn grace period so an accidental knockdown cannot be masked by invulnerability.
    for (int32 I=0;I<8;++I) { ++GFrameCounter; Mouth.World->Tick(LEVELTICK_All,.1f); }
    auto* Food=Mouth.World->SpawnActor<AMCFoodActor>(FVector(0,0,100),FRotator::ZeroRotator);
    Food->Phase=EMCFoodPhase::Free;
    FHitResult Hit; Hit.ImpactPoint=Hero->GetActorLocation(); Hit.ImpactNormal=FVector::ForwardVector;
    auto Contact=[&](FVector FoodVelocity,FVector HeroVelocity)
    {
        Hero->GetCharacterMovement()->Velocity=HeroVelocity;
        Food->Body->SetPhysicsLinearVelocity(FoodVelocity); Food->Tick(.016f);
        // Even a large solver impulse must not substitute for incoming food velocity.
        Food->Body->OnComponentHit.Broadcast(Food->Body,Hero,Hero->GetCapsuleComponent(),FVector(9000,0,0),Hit);
    };
    Contact(FVector::ZeroVector,FVector(440,0,0));
    TestEqual(TEXT("Running into resting food does not deal damage"),Hero->Status->State.Health,100.f);
    TestTrue(TEXT("Running into resting food leaves the player standing"),Hero->ToothPhysics->CanAct());
    Contact(FVector(500,0,0),FVector::ZeroVector);
    TestEqual(TEXT("Food moving away does not launch the player"),Food->ConfirmedImpacts,0);
    Contact(FVector(0,500,0),FVector::ZeroVector);
    TestEqual(TEXT("Tangential motion does not count as an incoming strike"),Food->ConfirmedImpacts,0);
    Contact(FVector(-20,0,0),FVector(440,0,0));
    TestEqual(TEXT("Player speed cannot amplify a slow food nudge into a strike"),Food->ConfirmedImpacts,0);
    Contact(FVector(-500,0,0),FVector(-700,0,0));
    TestEqual(TEXT("Separating bodies do not produce a strike"),Food->ConfirmedImpacts,0);
    Hero->bHandling=true;
    TestTrue(TEXT("Approached food remains grabbable"),Food->TryGrab(Hero));
    Contact(FVector(-500,0,0),FVector::ZeroVector);
    TestEqual(TEXT("Carried food does not strike its own holder"),Food->ConfirmedImpacts,0);
    Food->Release(Hero);
    Contact(FVector(-600,0,0),FVector::ZeroVector);
    TestEqual(TEXT("Genuinely incoming free food still strikes once"),Food->ConfirmedImpacts,1);
    TestTrue(TEXT("Real strike damages and knocks down"),Hero->Status->State.Health<100 && Hero->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDayOneSequenceTest,"MessControl.DayOne.SequenceAndNoGlobalDeadline",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDayOneSequenceTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth; Mouth.Mode->bUseDayOnePlan=true; Mouth.NextPhase();
    auto* D=Mouth.Mode->DayDirector.Get(); if (!TestNotNull(TEXT("Day director starts"),D)) return false;
    TestEqual(TEXT("First event is lesson"),Mouth.State->StepIndex,0);
    TestEqual(TEXT("Lesson has no event deadline"),Mouth.State->PhaseEndsAt,0.);
    Mouth.State->DayStartedAt-=500; D->Tick(.1f);
    TestEqual(TEXT("240 seconds is a reference, never a hard cutoff"),Mouth.State->StepIndex,0);
    TestTrue(TEXT("Teeth and tissue are both dirty"),D->CountDirt()>8);
    for (TActorIterator<AActor> It(Mouth.World);It;++It) if (auto* S=It->FindComponentByClass<UMCToothStatusComponent>()) while (S->NeedsCare(true)) S->CareContact(true);
    D->Tick(.1f); TestEqual(TEXT("Cleaning advances to brush disposal in same day"),Mouth.State->StepIndex,1);
    TestEqual(TEXT("Event progress never increments Day"),Mouth.State->Day,1);
    for (TActorIterator<AMCFoodActor> It(Mouth.World);It;++It) if (It->bBrushTool) It->Dispose();
    D->Tick(.1f); D->Tick(.1f); TestEqual(TEXT("Breakfast starts after disposal"),Mouth.State->StepIndex,2);
    TestTrue(TEXT("Menu food actually spawns"),D->CountFood(2)>0);
    D->Next(); const float Before=Mouth.State->MouthHealth; D->Next(true);
    TestEqual(TEXT("Failed event advances inside day"),Mouth.State->Day,1);
    TestTrue(TEXT("Failure costs configured mouth health"),Mouth.State->MouthHealth<Before && Mouth.State->FailedEvents==1);
    TestTrue(TEXT("Coffee starts a real volume"),D->Flood && D->Flood->IsActive());
    D->Next(); TestFalse(TEXT("Coffee ends before cleaning"),D->Flood->IsActive());
    D->Next(); TestTrue(TEXT("Stuck food exists at arena teeth"),D->CountFood(3)>0);
    for (TActorIterator<AMCFoodActor> It(Mouth.World);It;++It) if (It->Batch==3)
    { TestNotNull(TEXT("Stuck anchor is an arena tooth"),Cast<AMCArenaTooth>(It->StuckTooth)); TestEqual(TEXT("Food remains stuck until pulled"),It->Phase,EMCFoodPhase::Stuck); }
    D->Next(); TestTrue(TEXT("Stops at day one, does not invent the foam party"),Mouth.State->bDayOneComplete);
    Mouth.Mode->RestartShift(); TestNull(TEXT("Restart removes director"),Mouth.Mode->DayDirector.Get());
    TestFalse(TEXT("Restart clears brush inventory rules"),Mouth.State->bPhysicalBrushes);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBrushRoutingTest,"MessControl.DayOne.PhysicalBrushAndCorrectDisposal",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBrushRoutingTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth; Mouth.State->bPhysicalBrushes=true; auto* Hero=Mouth.Worker();
    Hero->SetActorLocation(FVector(-80,0,95)); Hero->SetActorRotation(FRotator::ZeroRotator);
    TestFalse(TEXT("No starting brush in inventory"),Hero->HasBrush());
    const FTransform T(FVector(0,0,95)); auto* Brush=Mouth.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),T);
    Brush->ConfigureBrush(); UGameplayStatics::FinishSpawningActor(Brush,T);
    TestTrue(TEXT("Physical brush can be picked up"),Brush->TryGrab(Hero));
    TestTrue(TEXT("Inventory enables brushing"),Hero->HasBrush() && Brush->Phase==EMCFoodPhase::Equipped);
    TestFalse(TEXT("Equipped brushes cannot be stolen"),Brush->TryGrab(Hero));
    Hero->ServerThrowItem(); TestFalse(TEXT("Throw removes equipped brush"),Hero->HasBrush());
    TestEqual(TEXT("Thrown brush returns to physics"),Brush->Phase,EMCFoodPhase::Free);
    auto* Throat=Mouth.World->SpawnActor<AMCFoodDisposal>(FVector(500,0,100),FRotator::ZeroRotator);
    Brush->SetActorLocation(Throat->GetActorLocation()); Throat->Tick(.1f);
    TestFalse(TEXT("Throat refuses brushes"),Brush->IsDisposed());
    auto* Bin=Mouth.World->SpawnActor<AMCFoodDisposal>(FVector(-500,0,100),FRotator::ZeroRotator); Bin->bBrushBin=true;
    Brush->SetActorLocation(Bin->GetActorLocation()); Bin->Tick(.1f);
    TestTrue(TEXT("Overboard bin accepts brushes"),Brush->IsDisposed());
    auto* Food=Mouth.World->SpawnActor<AMCFoodActor>(Bin->GetActorLocation(),FRotator::ZeroRotator); Food->Phase=EMCFoodPhase::Free; Bin->Tick(.1f);
    TestFalse(TEXT("Brush bin never completes food removal"),Food->IsDisposed());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBreakfastMenuTest,"MessControl.DayOne.MenuFragmentsMassAndSpoilage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBreakfastMenuTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth; FRandomStream Random(17);
    auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
    if (!TestNotNull(TEXT("Editable menu table exists"),Table)) return false;
    const auto* Row=Table->FindRow<FMCFoodRow>(TEXT("Broccoli"),TEXT("Test")); if (!TestNotNull(TEXT("Broccoli row exists"),Row)) return false;
    TestEqual(TEXT("Whole variants A B C"),Row->WholeMeshes.Num(),3); TestEqual(TEXT("Fragment variants A B C"),Row->FragmentMeshes.Num(),3);
    auto* Food=Mouth.World->SpawnActor<AMCFoodActor>(FVector(0,0,160),FRotator::ZeroRotator); Food->ConfigureItem(TEXT("Broccoli"),*Row,Random);
    Food->Batch=22; Food->SpoilAt=35;
    const float Heavy=Food->DragSpeed(); Food->Settings.Mass=3; TestTrue(TEXT("Lighter food can be dragged faster"),Food->DragSpeed()>Heavy); Food->Settings.Mass=Row->Mass;
    Food->HitFood(Row->Health,FVector::ForwardVector);
    TestTrue(TEXT("Whole food is replaced"),Food->IsDisposed()); int32 Count=0; float Mass=0;
    for (TActorIterator<AMCFoodActor> It(Mouth.World);It;++It) if (It->bFragment && !It->IsDisposed())
    {
        ++Count; Mass+=It->Settings.Mass; TestEqual(TEXT("Pieces keep the objective batch"),It->Batch,22);
        TestEqual(TEXT("Breaking does not reset expiry"),It->SpoilAt,35.);
        TestNotNull(TEXT("Fragment mesh loaded"),It->ItemMesh.Get());
        It->HitFood(10000,FVector::ForwardVector); TestFalse(TEXT("Hitting fragments does not delete cleanup work"),It->IsDisposed());
    }
    TestEqual(TEXT("Configured number of pieces"),Count,Row->Fragments); TestTrue(TEXT("Fragment mass conserves whole mass"),FMath::IsNearlyEqual(Mass,Row->Mass));
    auto* Floor=Mouth.World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Floor); Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(1000,1000,10)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-10));
    auto* Rot=Mouth.World->SpawnActor<AMCFoodActor>(FVector(300,300,80),FRotator::ZeroRotator); Rot->ConfigureItem(TEXT("Egg"),*Row,Random); Rot->SpoilAt=.001;
    ++GFrameCounter; Mouth.World->Tick(LEVELTICK_All,.05f); Rot->Tick(.1f);
    TestTrue(TEXT("Expired food becomes infected"),Rot->bSpoiled);
    int32 Ulcers=0; for (TActorIterator<AMCMouthSurface> It(Mouth.World);It;++It) if (It->bUlcer) ++Ulcers;
    TestTrue(TEXT("Spoilage leaves a lesion on the actual floor"),Ulcers>0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCUlcerAndFloodTest,"MessControl.DayOne.UlcerProtectionAndCoffeeControl",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCUlcerAndFloodTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth; Mouth.State->Phase=EMCShiftPhase::Working;
    auto* Patch=Mouth.World->SpawnActor<AMCMouthSurface>(FVector(300,300,5),FRotator::ZeroRotator); Patch->bUlcer=true; Patch->HealSeconds=2;
    const float Before=Mouth.State->MouthHealth; Patch->Tick(.5f);
    TestTrue(TEXT("Untouched ulcer heals automatically while hurting mouth"),Patch->Healing>0 && Mouth.State->MouthHealth<Before);
    auto* Hero=Mouth.Worker(); Hero->SetActorLocation(FVector(300,300,95)); Patch->Tick(.1f);
    TestEqual(TEXT("Walking over ulcer resets healing"),Patch->Healing,0.f);
    Hero->SetActorLocation(FVector(0,0,95)); Patch->Tick(2.1f); TestTrue(TEXT("Protected ulcer closes itself"),Patch->IsActorBeingDestroyed());
    auto* Plan=NewObject<UMCDayPlan>(); auto* Flood=Mouth.World->SpawnActor<AMCCoffeeFlood>(); Flood->Start(Plan);
    Flood->StartedAt=Mouth.World->GetTimeSeconds()-3.8; Flood->Tick(.05f);
    TestTrue(TEXT("Rising coffee reaches the hero"),Hero->bInCoffee && Flood->Contains(Hero->GetActorLocation()));
    Hero->ServerPaddle(FVector2D(100,100)); TestTrue(TEXT("Server clamps swimming input"),Hero->PaddleInput.Size()<=1.001);
    Flood->Stop(); TestFalse(TEXT("Draining releases water control state"),Hero->bInCoffee);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDevEventsTest,"MessControl.Development.EventSandbox",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDevEventsTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth;
    auto* Floor=Mouth.World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(3000,2000,10)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-10));
    Mouth.World->SpawnActor<APlayerStart>(FVector(-500,0,100),FRotator::ZeroRotator);
    auto* PC=Mouth.World->SpawnActor<APlayerController>(); PC->Possess(Mouth.Worker());
    const auto* Plan=Mouth.Mode->FirstDayPlan.LoadSynchronous();
    if (!TestNotNull(TEXT("Day plan"),Plan)) return false;
    auto Index=[&](EMCDayStep Step) { return Plan->Steps.IndexOfByPredicate([Step](const FMCDayStepSettings& S){return S.Step==Step;}); };
    TestFalse(TEXT("Unassigned controller cannot use panel"),Mouth.Mode->CanUseDevPanel(PC));
    PC->SetAsLocalPlayerController(); // Same host designation used by GameMode when spawning a local player.
    TestTrue(TEXT("Standalone host can use panel"),Mouth.Mode->CanUseDevPanel(PC));
    Mouth.Mode->ExecuteDevAction(nullptr,EMCDevAction::StartStep,0);
    TestNull(TEXT("No requester cannot reset world"),Mouth.Mode->DayDirector.Get());
    Mouth.Mode->ExecuteDevAction(PC,EMCDevAction::StartStep,Index(EMCDayStep::BreakfastCleanup));
    auto* Director=Mouth.Mode->DayDirector.Get();
    if (!TestNotNull(TEXT("Cleanup selected directly"),Director)) return false;
    TestTrue(TEXT("Manual mode is visible to game state"),Mouth.State->bDevManualEvents);
    TestEqual(TEXT("Direct cleanup includes its meal"),Director->CountFood(2),Plan->BreakfastCount);
    TestEqual(TEXT("Manual event has no deadline"),Mouth.State->PhaseEndsAt,0.);
    for (TActorIterator<AMCFoodActor> It(Mouth.World);It;++It) It->Dispose();
    Director->Tick(.1f);
    TestEqual(TEXT("Finishing all work does not jump away from test"),Mouth.State->StepIndex,Index(EMCDayStep::BreakfastCleanup));
    Mouth.Mode->ExecuteDevAction(PC,EMCDevAction::StartStep,999);
    TestEqual(TEXT("Invalid step preserves current sandbox"),Mouth.Mode->DayDirector.Get(),Director);
    Mouth.Mode->ExecuteDevAction(PC,EMCDevAction::StartStep,Index(EMCDayStep::CoffeeWaves));
    TestTrue(TEXT("Coffee selection uses real flood"),Mouth.Mode->DayDirector->Flood->IsActive());
    Mouth.Mode->ExecuteDevAction(PC,EMCDevAction::CoffeeDirt);
    TestTrue(TEXT("Dirt action includes teeth and tissue"),Mouth.Mode->DayDirector->CountDirt()>8);
    Mouth.Mode->ExecuteDevAction(PC,EMCDevAction::StopCoffee);
    TestFalse(TEXT("Drain stops real flood"),Mouth.Mode->DayDirector->Flood->IsActive());
    Mouth.Mode->ExecuteDevAction(PC,EMCDevAction::Infection);
    for (int32 I=0;I<8;++I) { ++GFrameCounter; Mouth.World->Tick(LEVELTICK_All,.05f); }
    AMCMouthSurface* Ulcer=nullptr;
    for (TActorIterator<AMCMouthSurface> It(Mouth.World);It;++It) if (It->bUlcer) Ulcer=*It;
    TestNotNull(TEXT("Infection button produces actual food spoilage lesion"),Ulcer);
    if (Ulcer)
    {
        const float HP=Mouth.State->MouthHealth; Ulcer->Tick(.1f);
        TestTrue(TEXT("Manual mode keeps ulcer damage active"),Mouth.State->MouthHealth<HP);
    }
    Mouth.Mode->ExecuteDevAction(PC,EMCDevAction::StartStep,Index(EMCDayStep::StuckFood));
    TestEqual(TEXT("Stuck food has configured count"),Mouth.Mode->DayDirector->CountFood(3),Plan->StuckCount);
    int32 Ulcers=0; for (TActorIterator<AMCMouthSurface> It(Mouth.World);It;++It) if (It->bUlcer) ++Ulcers;
    TestEqual(TEXT("Clean step start removes previous ulcers"),Ulcers,0);
    TestEqual(TEXT("Clean step restores concrete reserve"),Mouth.State->AvailableArenaTeeth(),8);
    Mouth.Mode->ExecuteDevAction(PC,EMCDevAction::KillSelf);
    auto* Dead=Cast<AMCToothCharacter>(PC->GetPawn());
    if (TestNotNull(TEXT("Host pawn"),Dead))
    {
        TestFalse(TEXT("Death button kills host"),Dead->Status->IsAlive());
        Dead->RespawnAt=-1; Mouth.Mode->ProcessRespawns();
        TestEqual(TEXT("Dev death uses real reserve payment"),Mouth.State->AvailableArenaTeeth(),7);
    }
    Mouth.Mode->ExecuteDevAction(PC,EMCDevAction::RestartDay);
    TestFalse(TEXT("Normal restart leaves manual mode"),Mouth.State->bDevManualEvents);
    TestEqual(TEXT("Normal restart restores reserve"),Mouth.State->AvailableArenaTeeth(),8);
    Mouth.NextPhase();
    TestEqual(TEXT("Normal day starts with lesson"),Mouth.State->StepIndex,0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCoffeeWaterTest,"MessControl.Coffee.SurfaceAndRagdollSwimming",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCoffeeWaterTest::RunTest(const FString& Parameters)
{
    const auto* Profile=LoadObject<UMCCoffeeProfile>(nullptr,TEXT("/Game/Data/DA_CoffeeWater.DA_CoffeeWater"));
    if (!TestNotNull(TEXT("Artist-editable water profile exists"),Profile)) return false;
    TestNotNull(TEXT("Subdivided water mesh exists"),Profile->SurfaceMesh.LoadSynchronous());
    TestNotNull(TEXT("Coffee material instance exists"),Profile->SurfaceMaterial.LoadSynchronous());
    FMCCoffeeWaterSettings Water=Profile->Settings; Water.Sanitize();
    for (int32 I=0;I<24;++I)
        TestTrue(TEXT("Physical ripples stay within visual amplitude"),FMath::Abs(Water.Ripple(FVector(I*73,-I*39,0),I*.37f))<=Water.RippleHeight+.001f);
    const FVector Neutral=Water.FloatAcceleration(160,FVector(0,0,160-Water.FloatDepth),FVector::ZeroVector,FVector::ZeroVector,980,Water.FloatDepth);
    TestTrue(TEXT("At draft depth buoyancy balances gravity"),FMath::IsNearlyEqual(Neutral.Z,980.f));
    TestTrue(TEXT("Fast rising bodies receive less lift"),Water.FloatAcceleration(160,FVector(0,0,100),FVector(0,0,300),FVector::ZeroVector,980,Water.FloatDepth).Z<Neutral.Z);
    TestTrue(TEXT("Extreme depth and speed have bounded force"),Water.FloatAcceleration(160,FVector(0,0,-10000),FVector(1e6,1e6,-1e6),FVector(1e5),980,20).Size()<=2400.01);
    Water.RippleLength=std::numeric_limits<float>::quiet_NaN(); Water.WaterDrag=-3; Water.Sanitize();
    TestTrue(TEXT("Invalid tuning sanitizes"),Water.RippleLength>=100 && Water.WaterDrag>=.5f);

    FTestMouth Mouth; Mouth.State->Phase=EMCShiftPhase::Working;
    Mouth.Mode->DayDirector=Mouth.World->SpawnActor<AMCDayDirector>();
    auto* Floor=Mouth.World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(2000,1500,10)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-10));
    auto* A=Mouth.Worker(); auto* B=Mouth.Worker();
    A->SetActorLocation(FVector(-250,0,100)); B->SetActorLocation(FVector(250,0,100));
    auto Tick=[&](){++GFrameCounter; Mouth.World->Tick(LEVELTICK_All,1.f/60);};
    for (int32 I=0;I<45;++I) Tick();
    A->ToothPhysics->ApplyHit(FVector(0,0,450),A->GetActorLocation());
    B->ToothPhysics->ApplyHit(FVector(0,0,450),B->GetActorLocation());
    auto* Plan=NewObject<UMCDayPlan>(); Plan->FlowAcceleration=0; Plan->FloodHeight=160; Plan->WaveCount=1;
    auto* Flood=Mouth.World->SpawnActor<AMCCoffeeFlood>(); Flood->Start(Plan);
    for (int32 I=0;I<180;++I)
    {
        // Hold a wave crest to isolate buoyancy/control from the rising/falling event envelope.
        Flood->StartedAt=Mouth.World->GetTimeSeconds()-3.8;
        A->ServerPaddle(FVector2D(0,-1)); B->ServerPaddle(FVector2D(0,1)); Tick();
    }
    const FVector PA=A->ToothPhysics->PhysicalLocation(),PB=B->ToothPhysics->PhysicalLocation();
    TestTrue(TEXT("Left paddle moves real ragdoll left"),PA.Y<-50);
    TestTrue(TEXT("Right paddle moves real ragdoll right"),PB.Y>50);
    TestTrue(TEXT("Both ragdolls stay at water surface"),FMath::Abs(PA.Z-Flood->SurfaceHeightAt(PA))<65 && FMath::Abs(PB.Z-Flood->SurfaceHeightAt(PB))<65);
    TestTrue(TEXT("Swimming keeps bodies in ragdoll"),A->bInCoffee && B->bInCoffee && A->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll && B->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll);
    TestFalse(TEXT("Cannot stand up in water"),A->ToothPhysics->TryRecover());
    Flood->Stop(); TestFalse(TEXT("Draining clears water state"),A->bInCoffee || B->bInCoffee);
    UE_LOG(LogTemp,Display,TEXT("MC_WATER_TEST left=%s right=%s"),*PA.ToString(),*PB.ToString());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCoffeePourDrainTest,"MessControl.Coffee.PourAndDrain",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCoffeePourDrainTest::RunTest(const FString& Parameters)
{
    FMCCoffeeWaterSettings Water; Water.Sanitize();
    TestTrue(TEXT("Default cycle is 4 second pour then 2 second drain"),Water.FillSeconds==4 && Water.DrainSeconds==2 && Water.Cycles==1);
    TestTrue(TEXT("Water rises continuously"),Water.FillAmount(1)<Water.FillAmount(2) && Water.FillAmount(2)<Water.FillAmount(3.9f));
    TestTrue(TEXT("Drain begins at 4 seconds"),Water.Phase(3.99f)==EMCCoffeePhase::Filling && Water.Phase(4)==EMCCoffeePhase::Draining);
    TestTrue(TEXT("Drain lowers water and finishes at 6 seconds"),Water.FillAmount(4.1f)>Water.FillAmount(5) && Water.FillAmount(5)>Water.FillAmount(5.9f) && Water.Phase(6)==EMCCoffeePhase::Inactive);
    TestEqual(TEXT("Jet stops before drain"),Water.JetAmount(4.1f),0.f);
    const FVector P(-400,400,95);
    TestTrue(TEXT("Fill pushes away from impact"),FVector::DotProduct(Water.FlowAt(P,2,320),(P-Water.Inlet).GetSafeNormal2D())>0);
    TestTrue(TEXT("Drain pulls towards throat"),FVector::DotProduct(Water.FlowAt(P,4.7f,320),(Water.DrainPoint-P).GetSafeNormal2D())>0);
    TestTrue(TEXT("No residual flow after drain"),Water.FlowAt(P,6.1f,320).IsNearlyZero());
    Water.Cycles=2; TestTrue(TEXT("Second cycle restarts fill"),Water.Phase(6.5f)==EMCCoffeePhase::Filling && Water.Phase(12)==EMCCoffeePhase::Inactive);
    Water.FillSeconds=-1; Water.DrainSeconds=std::numeric_limits<float>::quiet_NaN(); Water.FrontWidth=0; Water.Sanitize();
    TestTrue(TEXT("Invalid timing and widths sanitize"),Water.FillSeconds>=1 && Water.DrainSeconds==2 && Water.FrontWidth>=40);

    FTestMouth Mouth; Mouth.State->Phase=EMCShiftPhase::Working;
    Mouth.Mode->DayDirector=Mouth.World->SpawnActor<AMCDayDirector>();
    auto* Plan=NewObject<UMCDayPlan>(); auto* Flood=Mouth.World->SpawnActor<AMCCoffeeFlood>(); Flood->Start(Plan);
    TestEqual(TEXT("Event duration comes from water profile"),Flood->Seconds,6.f);
    TestNotNull(TEXT("Pour mesh ready"),Flood->Jet->GetStaticMesh().Get());
    TestNotNull(TEXT("Impact crown ready"),Flood->Crown->GetStaticMesh().Get());
    TestNotNull(TEXT("Throat outflow mesh ready"),Flood->DrainRibbon->GetStaticMesh().Get());
    Flood->StartedAt=Mouth.World->GetTimeSeconds()-2;
    const FVector Point=Flood->WaterSettings.Inlet+FVector(-300,400,-500);
    const FVector OpenFlow=Flood->FlowAtPosition(Point);
    TestTrue(TEXT("Open flow is nonzero"),!OpenFlow.IsNearlyZero());
    auto* Obstacle=Mouth.World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Obstacle);
    Obstacle->SetRootComponent(Box); Box->SetBoxExtent(FVector(50,50,150)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent();
    Obstacle->SetActorLocation(FVector((Flood->WaterSettings.Inlet.X+Point.X)*.5f,(Flood->WaterSettings.Inlet.Y+Point.Y)*.5f,Point.Z));
    TestTrue(TEXT("Solid obstruction shields from flow"),Flood->FlowAtPosition(Point).IsNearlyZero());
    Obstacle->Destroy();
    Flood->StartedAt=Mouth.World->GetTimeSeconds()-4.7; Flood->Tick(.016f);
    TestTrue(TEXT("Real actor enters drain and hides jet"),Flood->GetPhase()==EMCCoffeePhase::Draining && !Flood->Jet->IsVisible() && Flood->DrainRibbon->IsVisible());
    Flood->Stop();
    TestTrue(TEXT("Cancellation removes all pour visuals and forces"),!Flood->Surface->IsVisible() && !Flood->Jet->IsVisible() && !Flood->Crown->IsVisible() && !Flood->DrainRibbon->IsVisible() && !Flood->Drops->IsVisible() && Flood->FlowAtPosition(P).IsNearlyZero());
    return true;
}
#endif
