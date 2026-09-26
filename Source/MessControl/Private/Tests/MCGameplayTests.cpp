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
#include "MCTongueProfile.h"
#include "MCTongue.h"
#include "MCGazeComponent.h"
#include "MCGripComponent.h"
#include "MCExpressionComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/StaticMesh.h"
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
#include "MCToothMovementComponent.h"
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
        AMCFoodActor* GripCube()
        {
            const FTransform T(FVector(0,0,100));
            auto* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),T);
            FMCFoodRow Row; Row.Mass=4; Row.HalfExtent=FVector(50); Row.SpoilSeconds=300;
            Row.WholeMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))));
            FRandomStream Random(1); Food->ConfigureItem(TEXT("GripFixture"),Row,Random);
            Food->FinishSpawning(T); Food->Body->SetEnableGravity(false); return Food;
        }
        void Step(float Seconds)
        { for (int32 I=0;I<FMath::CeilToInt(Seconds*60);++I) { ++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60); } }
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
    auto* Original=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Art/Meshes/Character/SM_Teeth_rig.SM_Teeth_rig"));
    if (!TestNotNull(TEXT("Artist source retained"),Original)) return false;
    const auto& OriginalRef=Original->GetRefSkeleton();
    TestEqual(TEXT("Face repair preserves bone count"),Ref.GetNum(),OriginalRef.GetNum());
    for (int32 I=0;I<OriginalRef.GetNum();++I)
    {
        const int32 J=Ref.FindBoneIndex(OriginalRef.GetBoneName(I));
        if (TestTrue(TEXT("Source bone survives facial repair"),J!=INDEX_NONE))
        {
            const auto& A=OriginalRef.GetRefBonePose()[I]; const auto& B=Ref.GetRefBonePose()[J];
            // FBX/Blender reconstructs the two stretch-arm joints within 0.46 mm / 0.11 degrees.
            // Bound physical drift explicitly instead of using one tolerance for position, scale and quaternion.
            TestTrue(*FString::Printf(TEXT("Ref pose compatible: %s"),*OriginalRef.GetBoneName(I).ToString()),
                A.GetLocation().Equals(B.GetLocation(),.05) && A.GetScale3D().Equals(B.GetScale3D(),.001)
                && A.GetRotation().AngularDistance(B.GetRotation())<FMath::DegreesToRadians(.15f));
            const int32 PA=OriginalRef.GetParentIndex(I),PB=Ref.GetParentIndex(J);
            TestEqual(TEXT("Bone hierarchy preserved"),PA<0?NAME_None:OriginalRef.GetBoneName(PA),PB<0?NAME_None:Ref.GetBoneName(PB));
        }
    }
    for (const FName Role:{FName("eye_l"),FName("eye_r"),FName("lid_top_l"),FName("lid_top_r"),FName("lid_bottom_l"),FName("lid_bottom_r")})
        TestTrue(TEXT("Facial mapping exists"),Ref.FindBoneIndex(Hero->RigBone(Role))!=INDEX_NONE);
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
    FTestMouth Mouth; auto* A=Mouth.Worker(); A->SetActorLocation(FVector(-86,0,110)); A->SetActorRotation(FRotator::ZeroRotator); A->bHandling=true;
    auto* Food=Mouth.GripCube();
    TestTrue(TEXT("Near food can be gripped"),Food->TryGrab(A));
    TestTrue(TEXT("Grip links both sides"),A->HeldFood==Food && Food->Holders.Num()==1);
    TestTrue(TEXT("Repeated grab is idempotent"),Food->TryGrab(A) && Food->Holders.Num()==1);
    A->SetActorLocation(FVector(-1000,0,100)); Food->Tick(.1f);
    TestTrue(TEXT("Overstretched grip releases"),!A->HeldFood && Food->Holders.IsEmpty());
    TestFalse(TEXT("Remote grab rejected"),Food->TryGrab(A));
    Mouth.Step(.3f); A->SetActorLocation(FVector(-86,0,110));
    TestTrue(TEXT("Regrip within arm reach"),Food->TryGrab(A)); Mouth.Step(.65f);
    TestTrue(TEXT("Hands contact before extraction"),A->Grip->IsReady());
    Food->Phase=EMCFoodPhase::Stuck; Food->PullDirection=FVector(-1,0,0);
    A->ConsumeMovementInputVector(); A->AddMovementInput(FVector(0,1,0),1,true);
    for (int32 I=0;I<10;++I) Food->Tick(.1f);
    TestEqual(TEXT("Wrong pull direction gives no extraction"),Food->PullProgress,0.f);
    A->ConsumeMovementInputVector(); A->AddMovementInput(FVector(-1,0,0),1,true);
    for (int32 I=0;I<31;++I) Food->Tick(.1f);
    TestEqual(TEXT("Directional pulling frees food"),Food->Phase,EMCFoodPhase::Free);
    A->ConsumeMovementInputVector();
    A->GetCharacterMovement()->Velocity=FVector(-55,0,0);
    TestTrue(TEXT("Passive motion does not add grip drive input"),A->Grip->InputDirection().IsNearlyZero());
    Food->Body->SetPhysicsLinearVelocity(FVector::ZeroVector); A->AddActorWorldOffset(FVector(10,0,0));
    TestTrue(TEXT("No input cannot motor food from a changed rest offset"),A->Grip->DriveForce().IsNearlyZero());
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
    Hero->SetActorLocation(FVector(-86,0,110)); Hero->SetActorRotation(FRotator::ZeroRotator);
    // Exit the spawn grace period so an accidental knockdown cannot be masked by invulnerability.
    for (int32 I=0;I<8;++I) { ++GFrameCounter; Mouth.World->Tick(LEVELTICK_All,.1f); }
    auto* Food=Mouth.GripCube();
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
    auto* Hero=Mouth.Worker(); Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Hero->SetActorLocation(FVector(300,300,65)); Patch->Tick(.1f);
    TestEqual(TEXT("Walking over ulcer resets healing"),Patch->Healing,0.f);
    Hero->GetCharacterMovement()->SetMovementMode(MOVE_Falling); Patch->Tick(.1f);
    TestTrue(TEXT("Passing over ulcer in the air does not count as stepping"),Patch->Healing>0);
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
    // Isolate unconscious buoyancy; the controlled-swimming test covers automatic recovery.
    A->ToothPhysics->Settings.RagdollSeconds=10; B->ToothPhysics->Settings.RagdollSeconds=10;
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
    TestTrue(TEXT("Unconscious bodies remain buoyant"),A->bInCoffee && B->bInCoffee && A->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll && B->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll);
    TestTrue(TEXT("Can recover into swimming while water is still present"),A->ToothPhysics->TryRecover());
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
    // The artist tongue slopes below the original flat arena. Its front is still in the wave.
    auto* LowHero=Mouth.Worker();
    // Let the normal 0.6 second spawn/recovery protection expire before the test impact.
    Flood->SetActorTickEnabled(false);
    for (int32 I=0;I<45;++I) { ++GFrameCounter; Mouth.World->Tick(LEVELTICK_All,1.f/60); }
    Flood->SetActorTickEnabled(true);
    LowHero->SetActorLocation(FVector(Flood->WaterSettings.Inlet.X-300,Flood->WaterSettings.Inlet.Y+400,-105));
    Flood->WaterSettings.DryHeight=-200;
    Flood->StartedAt=Mouth.World->GetTimeSeconds()-(.25f+500.f/Flood->WaterSettings.FrontSpeed);
    Flood->Tick(.016f);
    TestTrue(TEXT("Impact reaches players on the lower artist tongue"),LowHero->ToothPhysics->KnockdownCount>0);
    Flood->StartedAt=Mouth.World->GetTimeSeconds()-4.7; Flood->Tick(.016f);
    TestTrue(TEXT("Real actor enters drain and hides jet"),Flood->GetPhase()==EMCCoffeePhase::Draining && !Flood->Jet->IsVisible() && Flood->DrainRibbon->IsVisible());
    Flood->Stop();
    TestTrue(TEXT("Cancellation removes all pour visuals and forces"),!Flood->Surface->IsVisible() && !Flood->Jet->IsVisible() && !Flood->Crown->IsVisible() && !Flood->DrainRibbon->IsVisible() && !Flood->Drops->IsVisible() && Flood->FlowAtPosition(P).IsNearlyZero());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCTongueScheduleTest,"MessControl.Tongue.AutomaticJoltAndReset",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCTongueScheduleTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth;
    Mouth.Mode->SetActorTickEnabled(false);
    Mouth.Mode->bUseDayOnePlan=true;
    Mouth.State->Phase=EMCShiftPhase::Working;
    auto* Tongue=Mouth.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FTransform::Identity);
    Tongue->SourceMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/Arena/SM_TongueSurface.SM_TongueSurface"));
    Tongue->FinishSpawning(FTransform::Identity);
    Tongue->SetActorTickEnabled(false);
    if (!TestTrue(TEXT("Authored tongue available"),Tongue->CurrentVertices().Num()>0)) return false;
    auto Advance=[&](float Seconds)
    {
        for (int32 I=0;I<FMath::RoundToInt(Seconds*10);++I) { ++GFrameCounter; Mouth.World->Tick(LEVELTICK_All,.1f); }
        Tongue->Tick(.1f);
    };
    Advance(21);
    TestEqual(TEXT("Normal day has no jolt before minimum rest"),Tongue->Motion.Serial,0);
    Advance(12);
    TestEqual(TEXT("Normal day starts jolt by maximum rest"),Tongue->Motion.Serial,1);
    TestFalse(TEXT("A running jolt cannot be triggered twice"),Tongue->TriggerJolt());
    Mouth.State->bDevManualEvents=true;
    Advance(40);
    TestEqual(TEXT("Manual event mode suppresses automatic jolts"),Tongue->Motion.Serial,1);
    Mouth.State->bDevManualEvents=false;
    Mouth.State->bDayOneComplete=true;
    Advance(1);
    TestEqual(TEXT("Completed day suppresses automatic jolts"),Tongue->Motion.Serial,1);
    Mouth.State->bDayOneComplete=false;
    Advance(1);
    TestEqual(TEXT("Active normal day resumes automatic jolts"),Tongue->Motion.Serial,2);
    Tongue->ResetPain();
    TestEqual(TEXT("Restart removes current jolt"),Tongue->Motion.Serial,0);
    Advance(21);
    TestEqual(TEXT("Restart grants a fresh quiet interval"),Tongue->Motion.Serial,0);
    Tongue->Settings.bAutomaticJolts=false;
    Advance(12);
    TestEqual(TEXT("Data setting disables automatic jolts"),Tongue->Motion.Serial,0);
    TestTrue(TEXT("Explicit dev action still works when automatic is disabled"),Tongue->TriggerJolt());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGazeAttentionTest,"MessControl.Gaze.AttentionAndOcclusion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGazeAttentionTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth; Mouth.Mode->SetActorTickEnabled(false); Mouth.State->Phase=EMCShiftPhase::Working;
    auto* A=Mouth.Worker(); auto* B=Mouth.Worker();
    A->SetActorLocationAndRotation(FVector(0,0,200),FRotator::ZeroRotator); B->SetActorLocation(FVector(250,100,200));
    auto Scan=[&](){A->Gaze->TickComponent(.3f,LEVELTICK_All,nullptr);};
    Scan(); TestTrue(TEXT("Visible player draws attention"),A->Gaze->Target.Actor==B && A->Gaze->Target.Interest==EMCGazeInterest::Player);
    auto* Food=Mouth.World->SpawnActor<AMCFoodActor>(FVector(200,-100,250),FRotator::ZeroRotator);
    Scan(); TestTrue(TEXT("Falling food interrupts a social hold"),A->Gaze->Target.Actor==Food && A->Gaze->Target.Interest==EMCGazeInterest::Danger);
    Food->Dispose(); Scan(); TestTrue(TEXT("Disposed target releases attention"),A->Gaze->Target.Actor==B);
    auto* Wall=Mouth.World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Box); Box->SetBoxExtent(FVector(20,300,300)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent(); Wall->SetActorLocation(FVector(120,0,200));
    Scan(); TestTrue(TEXT("Wall blocks gaze"),A->Gaze->Target.Interest==EMCGazeInterest::None);
    Wall->Destroy(); B->SetActorLocation(FVector(-200,0,200)); Scan();
    TestTrue(TEXT("Does not select a player behind the head"),A->Gaze->Target.Interest==EMCGazeInterest::None);
    TestTrue(TEXT("Events may request a brief reaction to a world point"),A->Gaze->NoticePoint(FVector(200,100,230),1));
    TestTrue(TEXT("Point reaction is independent of actor lifetime"),!A->Gaze->Target.Actor && A->Gaze->Target.Interest==EMCGazeInterest::Danger);
    const int32 Serial=A->Gaze->Target.Serial;
    TestFalse(TEXT("Reject invalid event point"),A->Gaze->NoticePoint(FVector(std::numeric_limits<double>::quiet_NaN()),1));
    TestEqual(TEXT("Rejected request does not change attention"),A->Gaze->Target.Serial,Serial);
    FMCGazeSettings S; S.YawLimit=1000; S.BlinkMin=0; S.TurnSpeed=std::numeric_limits<float>::quiet_NaN(); S.Sanitize();
    TestTrue(TEXT("Face tuning stays bounded"),S.YawLimit<=45 && S.BlinkMin>=1 && FMath::IsFinite(S.TurnSpeed));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGripContactTest,"MessControl.Grip.ContactAndRelease",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGripContactTest::RunTest(const FString&)
{
    FTestMouth Mouth; Mouth.Mode->SetActorTickEnabled(false); Mouth.State->Phase=EMCShiftPhase::Working;
    auto* Floor=Mouth.World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(1000,1000,10)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-10));
    FTransform Transform(FVector(0,0,51));
    auto* Food=Mouth.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Transform);
    FMCFoodRow Row; Row.Mass=4; Row.HalfExtent=FVector(50); Row.WholeMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))));
    FRandomStream Random(1); Food->ConfigureItem(TEXT("GripCube"),Row,Random); Food->FinishSpawning(Transform);
    auto* Hero=Mouth.Worker(); Hero->SetActorLocation(FVector(-86,0,61)); Hero->SetActorRotation(FRotator::ZeroRotator);
    Hero->GetCharacterMovement()->bRunPhysicsWithNoController=true; Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    auto Step=[&](float Seconds){for (int32 I=0;I<FMath::CeilToInt(Seconds*60);++I) { ++GFrameCounter; Mouth.World->Tick(LEVELTICK_All,1.f/60); }};
    Step(.8f); Hero->bHandling=true;
    const bool Grabbed=Food->TryGrab(Hero);
    TestTrue(*FString::Printf(TEXT("Both hands can reach the actual cube surface: %s hero %s food %s"),*Hero->Grip->DebugFailure,*Hero->GetActorLocation().ToCompactString(),*Food->GetActorLocation().ToCompactString()),Grabbed);
    if (!Grabbed) return false;
    TestFalse(TEXT("No force before the hands arrive"),Hero->Grip->IsReady());
    Step(.65f);
    TestTrue(*FString::Printf(TEXT("Hands reach their fixed surface points; error %.2f"),Hero->Grip->ContactError()),Hero->Grip->IsReady() && Hero->Grip->ContactError()<=Hero->Grip->Settings.ContactTolerance);
    TestEqual(TEXT("Front grip uses both hands"),Hero->Grip->Frame.Pose,EMCGripPose::FrontPull);
    TestTrue(TEXT("Holding permits turning along movement"),Hero->GetCharacterMovement()->bOrientRotationToMovement);
    TestTrue(TEXT("Grip keeps more clearance from the food"),FVector::Dist2D(Hero->GetActorLocation(),Food->Visual->Bounds.GetBox().GetClosestPointTo(Hero->GetActorLocation()))>50);
    Hero->SetActorRotation(FRotator(0,90,0)); Step(.5f);
    TestEqual(TEXT("Turning away leaves the near hand holding"),Hero->Grip->Frame.Pose,EMCGripPose::LeftHand);
    const FVector Anchor=Hero->Grip->Frame.LeftPoint;
    Hero->SetActorRotation(FRotator::ZeroRotator);
    for (int32 I=0;I<30 && Hero->Grip->Frame.Pose==EMCGripPose::LeftHand;++I) Step(1.f/60);
    TestFalse(TEXT("Second hand must arrive before two-hand force"),Hero->Grip->IsReady());
    Step(.65f);
    TestTrue(TEXT("Turning toward food restores two hands"),Hero->Grip->Frame.Pose==EMCGripPose::FrontPull && Hero->Grip->IsReady());
    TestTrue(TEXT("Supporting hand keeps the original surface anchor"),FVector(Hero->Grip->Frame.LeftPoint).Equals(Anchor,.01f));
    Food->Release(Hero); Hero->bHandling=false; Step(.5f);
    TestTrue(TEXT("Release restores walking rotation and free arms"),Hero->GetCharacterMovement()->bOrientRotationToMovement && Hero->Grip->Blend()<.001f && !Hero->Grip->Frame.Food);
    Hero->SetActorLocation(FVector(-86,0,61)); Hero->bHandling=true;
    TestTrue(TEXT("Can grab again"),Food->TryGrab(Hero)); Step(.5f);
    Food->Dispose(); Step(.4f);
    TestTrue(TEXT("Disposal clears both gameplay and animation grip"),!Hero->HeldFood && !Hero->Grip->Frame.Food && Hero->Grip->Blend()<.001f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPrimaryCarryTest,"MessControl.Grip.PrimaryCarryAndCare",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPrimaryCarryTest::RunTest(const FString&)
{
    FTestMouth Mouth; Mouth.Mode->SetActorTickEnabled(false); Mouth.State->Phase=EMCShiftPhase::Working;
    auto* Floor=Mouth.World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(1000,1000,10)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-10));
    const FTransform T(FVector(0,0,26));
    auto* Food=Mouth.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),T);
    FMCFoodRow Row; Row.Mass=6; Row.HalfExtent=FVector(50); Row.SpoilSeconds=300;
    Row.FragmentMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))));
    FRandomStream Random(1); Food->ConfigureItem(TEXT("SmallFood"),Row,Random,true); Food->FinishSpawning(T);
    auto* Hero=Mouth.Worker(); Hero->SetActorLocation(FVector(-68,0,61));
    auto* Move=Hero->GetCharacterMovement(); Move->bRunPhysicsWithNoController=true; Move->SetMovementMode(MOVE_Walking);
    Mouth.Step(.5f); Hero->ServerSetPrimary(true);
    for (int32 I=0;I<16;++I) { ++GFrameCounter; Mouth.World->Tick(LEVELTICK_All,.1f); }
    TestTrue(*FString::Printf(TEXT("Primary action lifts a small item: %s"),*Hero->Grip->DebugFailure),Hero->HeldFood==Food && Food->Phase==EMCFoodPhase::Carried);
    TestTrue(*FString::Printf(TEXT("Carried food lifts clear of the floor: z=%.1f"),Food->GetActorLocation().Z),Food->GetActorLocation().Z>45);
    TestFalse(TEXT("Carried food does not simulate separately from its holder"),Food->Body->IsSimulatingPhysics());
    bool ContinuousCarry=true;
    const int32 CarrySerial=Hero->Grip->Frame.Serial;
    for (int32 I=0;I<120;++I)
    {
        Mouth.Step(1.f/30);
        ContinuousCarry&=Hero->HeldFood==Food && Food->Phase==EMCFoodPhase::Carried && Hero->Grip->Frame.Serial==CarrySerial;
    }
    TestTrue(TEXT("Lifted floor contacts stay reachable without dropping and regrabbing"),ContinuousCarry);
    for (int32 I=0;I<45;++I) { Hero->AddMovementInput(FVector::RightVector); Mouth.Step(1.f/60); }
    TestTrue(TEXT("Movement turns the carrying hero"),FVector::DotProduct(Hero->GetActorForwardVector(),FVector::RightVector)>.8f);
    TestTrue(TEXT("Food follows the turn and stays in the hands"),Hero->HeldFood==Food && FVector::Dist(Food->GetActorLocation(),Hero->Grip->CarryLocation())<40);
    Hero->ServerSetPrimary(false); Mouth.Step(.4f);
    TestTrue(TEXT("Releasing the same button drops food with physics"),!Hero->HeldFood && Food->Phase==EMCFoodPhase::Free && Food->Body->IsSimulatingPhysics());
    Food->Dispose(); Hero->bSelfCare=true; Hero->Status->ApplyCoffee(); Hero->Status->Damage(25);
    const int32 Coffee=Hero->Status->State.CoffeeLeft;
    Hero->ServerSetPrimary(true); Mouth.Step(3.5f); Hero->ServerSetPrimary(false);
    TestTrue(TEXT("The same held action cleans then repairs without another key"),Coffee>0 && Hero->Status->State.CoffeeLeft==0 && Hero->Status->State.Health==Hero->Status->State.MaxHealth);
    TestFalse(TEXT("Releasing primary stops both work modes"),Hero->bBrushing || Hero->bHandling);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCExpressionTest,"MessControl.Animation.EmotesReactionsAndSpeech",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCExpressionTest::RunTest(const FString&)
{
    FTestMouth Mouth; Mouth.Mode->SetActorTickEnabled(false); Mouth.State->Phase=EMCShiftPhase::Working;
    auto* Floor=Mouth.World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(1000,1000,10)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-10));
    auto* Hero=Mouth.Worker(); Hero->SetActorLocation(FVector(0,0,61));
    Hero->GetCharacterMovement()->bRunPhysicsWithNoController=true; Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Mouth.Step(1);
    auto* Face=Hero->Expression.Get();
    if (!TestNotNull(TEXT("Editable emote library is available"),Face->Library.Get())) return false;
    TestTrue(TEXT("Menu has two authored clips and facial choices"),Face->Library->Entries.Num()>=6);
    Face->ServerPlayEmote(TEXT("unknown")); TestTrue(TEXT("Unknown requests do not change state"),Face->State.Id.IsNone());
    Face->ServerPlayEmote(TEXT("hello")); Mouth.Step(.5f);
    TestTrue(TEXT("Authored greeting blends fully in"),Face->State.Id==TEXT("hello") && Face->BodyAlpha()>.99f);
    const auto& Ref=Hero->GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();
    TArray<FTransform> Pose=Ref.GetRefBonePose(); Face->BuildBodyPose(Pose,Ref);
    float Change=0; TArray<FTransform> CS; CS.SetNum(Pose.Num());
    for (int32 I=0;I<Pose.Num();++I)
    {
        CS[I]=Ref.GetParentIndex(I)>=0?Pose[I]*CS[Ref.GetParentIndex(I)]:Pose[I];
        TestFalse(TEXT("Imported transform is finite"),CS[I].ContainsNaN());
        TestTrue(*FString::Printf(TEXT("Clip retains gameplay scale: %s %.2f"),*Ref.GetBoneName(I).ToString(),CS[I].GetLocation().Size()),CS[I].GetLocation().Size()<400);
        Change=FMath::Max(Change,float(Pose[I].GetRotation().AngularDistance(Ref.GetRefBonePose()[I].GetRotation())));
    }
    TestTrue(TEXT("The clip changes the actual skeletal pose"),Change>.2f);
    const uint16 Serial=Face->State.Serial; Face->ServerPlayEmote(TEXT("highfive")); TestEqual(TEXT("Spam cannot replace active emote"),Face->State.Serial,Serial);
    Hero->Status->Damage(10); Mouth.Step(.1f);
    TestEqual(TEXT("Pain overrides selected expression"),Face->CurrentEmotion,EMCEmotion::Pain);
    TestTrue(TEXT("Pain starts emote blend-out"),Face->State.StoppedAt>=0);
    Mouth.Step(.4f); TestEqual(TEXT("Interrupted body animation fades out"),Face->BodyAlpha(),0.f);
    Mouth.Step(1); Hero->bHandling=true;
    Face->ServerPlayEmote(TEXT("highfive")); TestEqual(TEXT("Work blocks hand gestures"),Face->State.Serial,Serial);
    Face->ServerPlayEmote(TEXT("happy")); Mouth.Step(.3f);
    TestEqual(TEXT("Facial emote remains available while working"),Face->CurrentEmotion,EMCEmotion::Happy);
    Face->SetSpeechInput(.8f,MCViseme::Round); TestEqual(TEXT("Voice envelope is accepted"),Face->SpeechAmount(),.8f);
    Mouth.Step(.4f); TestEqual(TEXT("Missing speech updates return to silence"),Face->SpeechAmount(),0.f);
    Face->SetSpeechInput(std::numeric_limits<float>::quiet_NaN()); TestEqual(TEXT("Invalid speech data is harmless"),Face->SpeechAmount(),0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCMouthMorphTest,"MessControl.Animation.ConnectedMouthMorphs",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCMouthMorphTest::RunTest(const FString&)
{
    FTestMouth Mouth; Mouth.Mode->SetActorTickEnabled(false); Mouth.State->Phase=EMCShiftPhase::Working;
    auto* Floor=Mouth.World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(1000,1000,10)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-10));
    auto* Hero=Mouth.Worker(); Hero->SetActorLocation(FVector(0,0,61));
    Hero->GetCharacterMovement()->bRunPhysicsWithNoController=true; Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking); Mouth.Step(1.5f);
    auto* Face=Hero->Expression.Get(); auto* Mesh=Hero->GetMesh();
    for (FName Name:UMCExpressionComponent::MouthShapes())
        if (!TestNotNull(*FString::Printf(TEXT("Imported mouth shape %s"),*Name.ToString()),Mesh->GetSkeletalMeshAsset()->FindMorphTarget(Name))) return false;
    TestEqual(TEXT("Enamel, lips and cavity have their own material slots"),Mesh->GetNumMaterials(),3);
    TestTrue(TEXT("Appearance override preserves the lip material"),Mesh->GetMaterial(0)!=Mesh->GetMaterial(1));
    auto CheckWeights=[&]()
    {
        float Sum=0;
        for (FName Name:UMCExpressionComponent::MouthShapes())
        {
            const float W=Mesh->GetMorphTarget(Name);Sum+=W;
            TestTrue(TEXT("Every facial weight is finite and nonnegative"),FMath::IsFinite(W) && W>=0 && W<=1.00001f);
        }
        TestTrue(TEXT("Complete mouth poses never add beyond full deformation"),Sum<=1.00001f);
    };
    Face->ServerPlayEmote(TEXT("happy")); Mouth.Step(.3f);
    for (uint8 I=1;I<=uint8(MCViseme::D);++I)
    {
        const auto Viseme=MCViseme(I);
        for (int32 Frame=0;Frame<24;++Frame)
        {
            Face->SetSpeechInput(Viseme==MCViseme::Closed?0.f:1.f,Viseme); Mouth.Step(1.f/60); CheckWeights();
        }
        TestTrue(TEXT("Each viseme drives the imported mesh, including silent closed lips"),Mesh->GetMorphTarget(UMCExpressionComponent::VisemeShape(Viseme))>.98f);
    }
    Hero->Status->Damage(10);
    for (int32 Frame=0;Frame<10;++Frame) { Face->SetSpeechInput(1,MCViseme::Open); Mouth.Step(1.f/60); CheckWeights(); }
    TestTrue(TEXT("Pain takes priority over live speech"),Mesh->GetMorphTarget(TEXT("Mouth_Pain"))>.6f && Mesh->GetMorphTarget(TEXT("Mouth_A"))<.05f);
    Mouth.Step(1.6f); CheckWeights();
    float Sum=0;for (FName Name:UMCExpressionComponent::MouthShapes()) Sum+=Mesh->GetMorphTarget(Name);
    TestTrue(TEXT("Silence and completed reactions return the mouth to its base shape"),Sum<.01f);
    Face->SetSpeechInput(1,MCViseme(255)); Mouth.Step(.1f);
    TestTrue(TEXT("Unknown viseme values are harmless"),UMCExpressionComponent::VisemeShape(MCViseme(255)).IsNone());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPupilResponseTest,"MessControl.Gaze.PupilResponseAndRecovery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPupilResponseTest::RunTest(const FString&)
{
    FTestMouth Mouth; Mouth.Mode->SetActorTickEnabled(false); Mouth.State->Phase=EMCShiftPhase::Working;
    auto* Floor=Mouth.World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(1000,1000,10)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-10));
    auto* Hero=Mouth.Worker(); Hero->SetActorLocation(FVector(0,0,61));
    Hero->GetCharacterMovement()->bRunPhysicsWithNoController=true; Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking); Mouth.Step(1);
    auto* G=Hero->Gaze.Get(); auto* Mesh=Hero->GetMesh();
    for (FName Name:{FName(TEXT("Pupil_Dilate")),FName(TEXT("Pupil_Contract"))})
        if (!TestNotNull(TEXT("Both pupil morphs are present in the gameplay mesh"),Mesh->GetSkeletalMeshAsset()->FindMorphTarget(Name))) return false;
    TestTrue(TEXT("Calm pupil has its original size"),FMath::IsNearlyEqual(G->PupilScale,1.f,.01f));
    const FVector Point=Hero->GetActorLocation()+FVector(250,0,25);
    TestTrue(TEXT("Authority can announce nearby danger"),G->NoticePoint(Point,.65f));
    Mouth.Step(.05f); TestTrue(TEXT("Dilation begins smoothly"),G->PupilScale>1.05f && G->PupilScale<1.6f);
    Mouth.Step(.3f); TestTrue(TEXT("Danger enlarges the rendered pupil"),G->PupilScale>1.65f && Mesh->GetMorphTarget(TEXT("Pupil_Dilate"))>.8f);
    Mouth.Step(.55f); TestTrue(TEXT("Pupil recovers gradually after the threat expires"),G->PupilScale>1.1f && G->PupilScale<1.7f);
    Mouth.Step(2); TestTrue(TEXT("Pupil returns to rest"),FMath::Abs(G->PupilScale-1)<.015f);
    Hero->Status->Damage(10); Mouth.Step(.15f); TestTrue(TEXT("Pain produces a brief pupil reaction"),G->PupilScale>1.2f);
    Mouth.Step(2); Hero->Expression->ServerPlayEmote(TEXT("angry")); Mouth.Step(.4f);
    TestTrue(TEXT("Focused expression contracts the pupil"),G->PupilScale<.9f && Mesh->GetMorphTarget(TEXT("Pupil_Contract"))>.25f);
    G->NoticePoint(Point,1); Mouth.Step(.35f);
    TestTrue(TEXT("Danger takes priority over a selected expression"),G->PupilScale>1.65f);
    FMCGazeSettings S; S.PupilRest=100; S.PupilFocus=-100; S.PupilReactSpeed=std::numeric_limits<float>::quiet_NaN(); S.Sanitize();
    TestTrue(TEXT("Pupil tuning stays inside the authored morph range"),S.PupilRest<=1.8f && S.PupilFocus>=.6f && FMath::IsFinite(S.PupilReactSpeed));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSurfaceSwimTest,"MessControl.Coffee.ControlledSwimming",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSurfaceSwimTest::RunTest(const FString&)
{
    FTestMouth Mouth; Mouth.Mode->SetActorTickEnabled(false); Mouth.State->Phase=EMCShiftPhase::Working;
    auto* Floor=Mouth.World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Box);Box->SetBoxExtent(FVector(2000,2000,10));Box->SetCollisionProfileName(TEXT("BlockAll"));Box->RegisterComponent();Floor->SetActorLocation(FVector(0,0,-10));
    auto* Water=Mouth.World->SpawnActor<AMCCoffeeFlood>();Water->bActive=true;Water->Height=200;Water->Seconds=1000;Water->HalfSize=FVector(1900,1900,500);
    Water->WaterSettings=FMCCoffeeWaterSettings();Water->WaterSettings.FillSeconds=15;Water->WaterSettings.DrainSeconds=10;Water->WaterSettings.RippleHeight=0;Water->WaterSettings.DrainPoint=FVector(1500,0,0);
    auto* H=Mouth.Worker();H->SetActorLocation(FVector(-300,0,180));auto* Move=CastChecked<UMCToothMovementComponent>(H->GetCharacterMovement());Move->bRunPhysicsWithNoController=true;Move->SetMovementMode(MOVE_Falling);
    auto Step=[&](float Seconds,FVector Input=FVector::ZeroVector)
    {for(int32 I=0;I<FMath::CeilToInt(Seconds*60);++I){Water->StartedAt=Mouth.World->GetTimeSeconds()-15.3;H->AddMovementInput(Input);Mouth.Step(1.f/60);}};
    Step(1);
    TestTrue(TEXT("Deep water starts controlled swimming"),Move->IsSwimming() && H->ToothPhysics->CanAct());
    TestTrue(TEXT("Idle swimmer floats near the surface"),FMath::Abs(H->GetActorLocation().Z-(Water->SurfaceHeightAt(H->GetActorLocation())-Water->WaterSettings.SwimFloatDepth))<12);
    TestTrue(TEXT("Idle swimmer drifts towards the throat"),H->GetActorLocation().X>-270);
    const float Before=H->GetActorLocation().X;Step(2.5f,FVector(-1,0,0));
    TestTrue(*FString::Printf(TEXT("Paddling makes progress against the drain: %.1f cm"),Before-H->GetActorLocation().X),H->GetActorLocation().X<Before-50);
    TestTrue(TEXT("Swimming pose and effort are active"),H->AnimationSwim>.95f && H->AnimationSwimEffort>.8f);
    auto* Wall=Mouth.World->SpawnActor<AActor>();auto* Block=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(Block);Block->SetBoxExtent(FVector(10,500,400));Block->SetCollisionProfileName(TEXT("BlockAll"));Block->RegisterComponent();
    const float WallX=H->GetActorLocation().X-120;Wall->SetActorLocation(FVector(WallX,0,200));Step(1.3f,FVector(-1,0,0));
    TestTrue(TEXT("Swimming sweeps the capsule against obstacles"),H->GetActorLocation().X>WallX+30);Wall->Destroy();
    H->ToothPhysics->ApplyHit(FVector(0,0,450),H->GetActorLocation());
    TestTrue(TEXT("A stunned swimmer can recover without waiting for dry ground"),H->ToothPhysics->TryRecover());Step(1.2f);
    TestTrue(TEXT("Recovery restores swimming control"),H->ToothPhysics->CanAct() && Move->IsSwimming());
    Water->Stop();Mouth.Step(1.5f);
    TestTrue(TEXT("Draining water restores grounded movement"),Move->IsMovingOnGround() && H->AnimationSwim<.02f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGripTurnTest,"MessControl.Grip.PhysicalTurning",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGripTurnTest::RunTest(const FString&)
{
    for (int32 FPS:{5,10,30,60})
    {
    FTestMouth Mouth;Mouth.Mode->SetActorTickEnabled(false);Mouth.State->Phase=EMCShiftPhase::Working;
    auto* Floor=Mouth.World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Floor);Floor->SetRootComponent(Box);Box->SetBoxExtent(FVector(1000,1000,10));Box->SetCollisionProfileName(TEXT("BlockAll"));Box->RegisterComponent();Floor->SetActorLocation(FVector(0,0,-10));
    auto* Food=Mouth.GripCube();Food->Body->SetEnableGravity(true);Food->SetActorLocation(FVector(0,0,51));
    auto* H=Mouth.Worker();H->SetActorLocation(FVector(-86,0,61));H->SetActorRotation(FRotator::ZeroRotator);H->GetCharacterMovement()->bRunPhysicsWithNoController=true;H->GetCharacterMovement()->SetMovementMode(MOVE_Walking);Mouth.Step(.8f);H->bHandling=true;
    if(!TestTrue(TEXT("Acquire turning fixture"),Food->TryGrab(H)))return false;Mouth.Step(.65f);
    const float Start=Food->GetActorRotation().Yaw;float PeakTorque=0;
    for(int32 I=0;I<FPS*2;++I)
    {H->SetActorRotation(FRotator(0,FMath::Min(40.f,I*30.f/FPS),0));++GFrameCounter;Mouth.World->Tick(LEVELTICK_All,1.f/FPS);PeakTorque=FMath::Max(PeakTorque,float(H->Grip->DriveTorque().Size()));}
    const float Turn=FMath::FindDeltaAngleDegrees(Start,Food->GetActorRotation().Yaw);
    TestTrue(*FString::Printf(TEXT("Turning at %d FPS uses Chaos torque: %.1f degrees, torque %.0f, spin %.2f, mass %.1f"),FPS,Turn,PeakTorque,Food->Body->GetPhysicsAngularVelocityInRadians().Z,Food->Body->GetMass()),Turn>8 && Turn<80 && Food->Body->IsSimulatingPhysics());
    TestTrue(TEXT("Turning retains the grip"),H->HeldFood==Food && H->Grip->IsReady());
    TestTrue(TEXT("Turning force is bounded"),PeakTorque>1000 && PeakTorque<=H->Grip->Settings.TurnTorque+1);
    Food->Release(H);TestTrue(TEXT("Release stops the turning motor"),H->Grip->DriveTorque().IsNearlyZero());
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGripModesTest,"MessControl.Grip.AnglesAndBounds",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGripModesTest::RunTest(const FString&)
{
    FMCGripSettings S;
    TestEqual(TEXT("Facing and approaching pushes"),UMCGripComponent::SelectPose(EMCGripPose::FrontPull,0,100,S),EMCGripPose::Push);
    TestEqual(TEXT("Facing and retreating pulls"),UMCGripComponent::SelectPose(EMCGripPose::Push,0,-100,S),EMCGripPose::FrontPull);
    TestEqual(TEXT("Left side uses left hand"),UMCGripComponent::SelectPose(EMCGripPose::FrontPull,-90,0,S),EMCGripPose::LeftHand);
    TestEqual(TEXT("Right side uses right hand"),UMCGripComponent::SelectPose(EMCGripPose::FrontPull,90,0,S),EMCGripPose::RightHand);
    TestEqual(TEXT("Behind uses rear pull"),UMCGripComponent::SelectPose(EMCGripPose::FrontPull,175,100,S),EMCGripPose::RearPull);
    TestEqual(TEXT("Small angle noise does not change hand count"),UMCGripComponent::SelectPose(EMCGripPose::FrontPull,58,0,S),EMCGripPose::FrontPull);
    TestEqual(TEXT("Side grip keeps its mode until decisively front"),UMCGripComponent::SelectPose(EMCGripPose::RightHand,52,0,S),EMCGripPose::RightHand);
    S.MaxArmStretch=100; S.ReachSeconds=0; S.DriveForce=std::numeric_limits<float>::quiet_NaN(); S.Sanitize();
    TestTrue(TEXT("Tuning cannot create infinite arms or force"),S.MaxArmStretch<=1.15f && S.ReachSeconds>=.12f && FMath::IsFinite(S.DriveForce));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCTongueWeightTest,"MessControl.Tongue.WeightContactsAndRecovery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCTongueWeightTest::RunTest(const FString&)
{
    FTestMouth Mouth; Mouth.Mode->SetActorTickEnabled(false); Mouth.State->Phase=EMCShiftPhase::Working; Mouth.State->bDevManualEvents=true;
    auto* Tongue=Mouth.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FTransform::Identity);
    Tongue->SourceMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/Arena/SM_TongueSurface.SM_TongueSurface")); Tongue->FinishSpawning(FTransform::Identity);
    Tongue->Settings.IdleHeight=0; Tongue->PressureSettings.RecoverSeconds=.3f;
    const FVector Center=FBox(Tongue->CurrentVertices()).GetCenter(); FHitResult Hit;
    if (!TestTrue(TEXT("Test load has a real tongue floor"),Tongue->SurfacePoint(Center,Hit))) return false;
    const FVector Start=Hit.ImpactPoint+FVector(0,0,31);
    auto* Food=Mouth.World->SpawnActor<AMCFoodActor>(Start,FRotator::ZeroRotator);
    Food->Settings.Mass=4; Food->Body->SetMassOverrideInKg(NAME_None,4,true);
    auto Step=[&](float Seconds){for (int32 I=0;I<FMath::CeilToInt(Seconds*60);++I) { ++GFrameCounter; Mouth.World->Tick(LEVELTICK_All,1.f/60); }};
    auto FoodLoad=[&](){for (const auto& S:Tongue->PressureLoads()) if (S.Actor==Food) return S.Depth; return 0.f;};
    Step(2);
    const float Light=Tongue->IndentationAt(Food->GetActorLocation());
    TestTrue(*FString::Printf(TEXT("Supported 4 kg food makes a dent: %.3f"),Light),Light>.5f);
    Food->Settings.Mass=28; Food->Body->SetMassOverrideInKg(NAME_None,28,true); Step(2);
    const float Heavy=Tongue->IndentationAt(Food->GetActorLocation());
    TestTrue(*FString::Printf(TEXT("Same collider with more mass presses deeper: %.3f -> %.3f"),Light,Heavy),Heavy>Light*1.5f);
    TestTrue(TEXT("Depth has a finite cap"),Heavy<=Tongue->PressureSettings.MaxDepth);
    TestTrue(TEXT("One food object has one load"),Tongue->PressureLoads().Num()==1 && FMath::IsNearlyEqual(FoodLoad(),28*Tongue->PressureSettings.DepthPerKg,.1f));
    FHitResult CosmeticHit; Tongue->SurfacePoint(Start,CosmeticHit);
    TestTrue(TEXT("Cosmetic pressure never lowers the physics floor"),FMath::Abs(CosmeticHit.ImpactPoint.Z-Hit.ImpactPoint.Z)<.01);
    const auto* Section=Tongue->Surface->GetProcMeshSection(0);
    bool ShaderData=false;
    if (Section) for (const auto& Vertex:Section->ProcVertexBuffer) ShaderData|=Vertex.UV1.X>.5f;
    TestTrue(TEXT("Weight is uploaded as material WPO input"),ShaderData);
    const FVector OldPlace=Food->GetActorLocation();
    Food->Body->SetEnableGravity(false); Food->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Food->SetActorLocation(OldPlace+FVector(0,0,350),false,nullptr,ETeleportType::TeleportPhysics); Step(.2f);
    TestEqual(TEXT("Lifted food stops loading the floor"),FoodLoad(),0.f);
    TestTrue(TEXT("Vacated dent recovers gradually"),Tongue->IndentationAt(OldPlace)>0 && Tongue->IndentationAt(OldPlace)<Heavy);
    Step(2); TestTrue(TEXT("Surface recovers after lifting"),Tongue->IndentationAt(OldPlace)<.1f);
    Food->SetActorLocation(Start,false,nullptr,ETeleportType::TeleportPhysics); Food->Body->SetEnableGravity(true); Step(2);
    TestTrue(TEXT("Putting food down restores its load"),FoodLoad()>0);
    Food->Dispose(); Step(.2f); TestEqual(TEXT("Disposed item has no load"),FoodLoad(),0.f);
    Tongue->ResetPressure(); TestTrue(TEXT("Reset clears the stored indentation"),Tongue->IndentationAt(Start)<.001f && Tongue->PressureLoads().IsEmpty());
    // Input bounds guard against a malformed designer profile creating an unstable floor.
    FMCTonguePressureSettings S; S.MaxDepth=10000; S.MaxSources=500; S.PressSeconds=0; S.DepthPerKg=std::numeric_limits<float>::quiet_NaN(); S.Sanitize();
    TestTrue(TEXT("Pressure budget and response remain bounded"),S.MaxDepth<=35 && S.MaxSources<=32 && S.PressSeconds>=.06f && FMath::IsFinite(S.DepthPerKg));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCTonguePlayerWeightTest,"MessControl.Tongue.PlayerWeightAndLanding",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCTonguePlayerWeightTest::RunTest(const FString&)
{
    FTestMouth Mouth; Mouth.Mode->SetActorTickEnabled(false); Mouth.State->Phase=EMCShiftPhase::Working; Mouth.State->bDevManualEvents=true;
    auto* Tongue=Mouth.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FTransform::Identity);
    Tongue->SourceMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/Arena/SM_TongueSurface.SM_TongueSurface")); Tongue->FinishSpawning(FTransform::Identity);
    Tongue->Settings.IdleHeight=0;
    FHitResult Hit; if (!Tongue->SurfacePoint(FBox(Tongue->CurrentVertices()).GetCenter(),Hit)) return false;
    auto* Hero=Mouth.Worker(); Hero->GetCharacterMovement()->bRunPhysicsWithNoController=true;
    Hero->SetActorLocation(Hit.ImpactPoint+FVector(0,0,61)); Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    auto Step=[&](float Seconds){for (int32 I=0;I<FMath::CeilToInt(Seconds*60);++I) { ++GFrameCounter; Mouth.World->Tick(LEVELTICK_All,1.f/60); }};
    auto Find=[&]()->const FMCTongueLoad* {for (const auto& S:Tongue->PressureLoads()) if (S.Actor==Hero) return &S; return nullptr;};
    Step(2); const auto* Standing=Find();
    TestTrue(TEXT("Grounded character loads the tongue"),Standing && Standing->Kind==EMCTongueLoadKind::Player);
    if (!Standing) return false;
    const float RestLoad=Standing->Depth;
    Hero->LaunchCharacter(FVector(0,0,600),false,true); Step(.2f);
    TestNull(TEXT("Airborne character has no standing load"),Find());
    float Peak=0; for (int32 I=0;I<100;++I) { Step(1.f/60); if (const auto* S=Find()) Peak=FMath::Max(Peak,S->Depth); }
    TestTrue(*FString::Printf(TEXT("Landing briefly increases pressure: %.2f > %.2f"),Peak,RestLoad),Peak>RestLoad*1.25f);
    Step(1); Hero->ToothPhysics->ApplyHit(FVector(0,0,-350),Hero->GetActorLocation());
    bool Broad=false;
    for (int32 I=0;I<100;++I) { Step(1.f/60); if (const auto* S=Find()) Broad|=S->Kind==EMCTongueLoadKind::Ragdoll && S->RadiusX>Tongue->PressureSettings.PlayerRadius; }
    TestTrue(TEXT("Supported ragdoll uses a broader footprint"),Broad);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCTongueEventTest,"MessControl.Tongue.EventProfiles",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCTongueEventTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth; Mouth.Mode->SetActorTickEnabled(false); Mouth.State->Phase=EMCShiftPhase::Working; Mouth.State->bDevManualEvents=true;
    auto* Tongue=Mouth.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FTransform::Identity);
    Tongue->SourceMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/Arena/SM_TongueSurface.SM_TongueSurface")); Tongue->FinishSpawning(FTransform::Identity);
    Tongue->SetActorTickEnabled(false); Tongue->Settings.IdleHeight=0;
    const auto Rest=Tongue->CurrentVertices(); if (!TestTrue(TEXT("Tongue geometry available"),Rest.Num()>100)) return false;
    FBox Bounds(Rest); FVector Origin=Bounds.GetCenter();
    auto* Profile=NewObject<UMCTongueMotionProfile>(); Profile->Settings.Height=100; Profile->Settings.Radius=400;
    TestTrue(TEXT("Event can start from explicit origin"),Tongue->PlayMotion(Profile,Origin,FVector::ForwardVector,.5f));
    TestEqual(TEXT("Per-call strength is snapshotted"),Tongue->Motion.Settings.Height,50.f);
    Profile->Settings.Height=210;
    TestEqual(TEXT("Asset edits cannot change an active event"),Tongue->Motion.Settings.Height,50.f);
    TestFalse(TEXT("Active motion rejects overlapping event"),Tongue->PlayMotion(Profile,Origin,FVector::ForwardVector));
    auto Peak=[&]()
    {
        Tongue->Motion.StartedAt=Tongue->ServerTime()-Tongue->Motion.Settings.Anticipation-Tongue->Motion.Settings.Rise-.05;
        Tongue->Tick(.033f);
        FVector Center=FVector::ZeroVector; float Sum=0;
        for (int32 I=0;I<Rest.Num();++I) { const float D=FMath::Max(0.f,float(Tongue->CurrentVertices()[I].Z-Rest[I].Z)); Center+=Rest[I]*D; Sum+=D; }
        TestTrue(TEXT("Local event visibly deforms mesh"),Sum>10); return Center/FMath::Max(1.f,Sum);
    };
    const FVector First=Peak();
    Tongue->ResetPain(); Origin.Y+=300; Profile->Settings.Height=50;
    TestTrue(TEXT("Same profile can move its origin"),Tongue->PlayMotion(Profile,Origin,FVector::ForwardVector));
    const FVector Second=Peak(); TestTrue(TEXT("Deformation follows the moved origin"),Second.Y>First.Y+100);
    Tongue->ResetPain(); Profile->Settings.Shape=EMCTongueShape::DirectionalWave; Profile->Settings.Speed=0;
    TestTrue(TEXT("Directional event starts with sanitized parameters"),Tongue->PlayMotion(Profile,Origin,FVector(0,1,0)));
    TestTrue(TEXT("Direction is retained and speed is safe"),Tongue->Motion.Direction.Equals(FVector(0,1,0),.001f) && Tongue->Motion.Settings.Speed>=100);
    Tongue->ResetPain();
    Profile->Settings.Speed=700; Profile->Settings.Radius=1300; Profile->Settings.Width=180;
    Origin=Bounds.GetCenter()-FVector(0,300,0);
    Tongue->PlayMotion(Profile,Origin,FVector(0,1,0));
    auto Crest=[&](float Seconds)
    {
        Tongue->Motion.StartedAt=Tongue->ServerTime()-Tongue->Motion.Settings.Anticipation-Seconds;
        Tongue->Tick(.033f); FVector Center=FVector::ZeroVector; float Sum=0;
        for (int32 I=0;I<Rest.Num();++I) { const float D=FMath::Max(0.f,float(Tongue->CurrentVertices()[I].Z-Rest[I].Z)); Center+=Rest[I]*D; Sum+=D; }
        TestTrue(TEXT("Travelling front has visible geometry"),Sum>10); return Center/FMath::Max(1.f,Sum);
    };
    const FVector Early=Crest(.25f),Late=Crest(.75f);
    TestTrue(TEXT("Front travels in requested direction over time"),Late.Y>Early.Y+100);
    Tongue->ResetPain(); Tongue->PlayMotion(Profile,Origin,FVector(0,-1,0));
    TestTrue(TEXT("Reversing direction sends front the other way"),Crest(.25f).Y<Origin.Y);
    Tongue->ResetPain();
    TestFalse(TEXT("Invalid strength is rejected"),Tongue->PlayMotion(Profile,Origin,FVector::ForwardVector,std::numeric_limits<float>::quiet_NaN()));
    TestEqual(TEXT("Rejected event has no state"),Tongue->Motion.Serial,0);
    return true;
}
#endif
