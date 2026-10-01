#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCGameMode.h"
#include "MCColdCola.h"
#include "MCInventoryComponent.h"
#include "MCGameState.h"
#include "MCTaskActor.h"
#include "MCToothCharacter.h"
#include "MCArenaTooth.h"
#include "MCSurfaceWipe.h"
#include "MCCoffeeWipe.h"
#include "MCArenaToothSocket.h"
#include "MCToothStatusComponent.h"
#include "MCFoodActor.h"
#include "MCThroat.h"
#include "MCVomitBurst.h"
#include "MCHazardWave.h"
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
#include "MCMotionRecorder.h"
#include "Animation/AnimSequence.h"
#include "Engine/StaticMesh.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"

#endif
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
#include "MCLocomotionCycle.h"
#include "MCLocomotionSurface.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsControlComponent.h"
#include <limits>

namespace
{
    UStaticMesh* ReadyCareTestMesh(const FString& Path)
    {
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,*Path);
#if WITH_EDITOR
        // A synchronous automation test does not yield to the editor's async mesh
        // compiler. Wait for collision data, as normal world startup does.
        if (Mesh) FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
        return Mesh;
    }
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
            // Movement/flow unit fixtures provide their own geometry. The game's new
            // solid throat must not become an unrelated wall across those test tracks.
            for(TActorIterator<AMCThroat> It(World);It;++It) { It->SetActorEnableCollision(false); It->SetActorTickEnabled(false); }
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCThroatCycleTest,"MessControl.Throat.ClosedUntilWeightedLanding",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCThroatCycleTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth;
    auto* Throat=Mouth.World->SpawnActor<AMCThroat>(FVector(0,0,200),FRotator::ZeroRotator);
    auto* Food=Mouth.GripCube(); Food->SetActorLocation(Throat->GetActorTransform().TransformPosition(Throat->ZoneCenter+FVector(0,0,55)));
    Food->Body->SetSimulatePhysics(false); Food->Phase=EMCFoodPhase::Free;
    auto* Brush=Mouth.GripCube(); Brush->bBrushTool=true; Brush->Body->SetSimulatePhysics(false); Brush->SetActorLocation(Food->GetActorLocation()+FVector(0,800,0));
    Throat->Tick(.1f);
    TestTrue(TEXT("Food is staged without disappearing"),Throat->FoodInZone==1 && !Food->IsDisposed());
    TestEqual(TEXT("Default throat is closed"),Throat->OpenAmount(),0.f);
    auto* Hero=Mouth.Worker(); FMovementBaseInterfaceData Base(Throat->UvulaLanding.Get()); Hero->SetBase(&Base);
    FHitResult Side(Throat,Throat->UvulaLanding,FVector::ZeroVector,FVector::ForwardVector);
    Throat->NotifyUvulaLanding(Hero,Side,400); Throat->Tick(.4f);
    TestEqual(TEXT("Side contact cannot order a swallow"),Throat->ThroatPhase,EMCThroatPhase::Collecting);
    FHitResult Top(Throat,Throat->UvulaLanding,FVector::ZeroVector,FVector::UpVector);
    Throat->NotifyUvulaLanding(Hero,Top,400); Throat->Tick(.3f);
    TestEqual(TEXT("Brief landing gives time for the uvula to sag"),Throat->ThroatPhase,EMCThroatPhase::Collecting);
    Throat->Tick(.2f);
    TestEqual(TEXT("Player weight begins anticipation"),Throat->ThroatPhase,EMCThroatPhase::Anticipation);
    TestEqual(TEXT("Anticipation stays shut"),Throat->OpenAmount(),0.f);
    Hero->SetActorLocation(Throat->GetActorLocation()+FVector(-900,0,70));
    Throat->PhaseStartedAt-=Throat->AnticipationSeconds+0.01; Throat->Tick(.01f);
    TestEqual(TEXT("Food captured once"),Food->Phase,EMCFoodPhase::Swallowing);
    TestFalse(TEXT("Captured food cannot be grabbed"),Food->TryGrab(Hero));
    TestFalse(TEXT("Captured food cannot fragment"),Food->HitFood(10000,FVector::ForwardVector));
    TestFalse(TEXT("A brush outside this gulp is not swallowed"),Brush->IsDisposed());
    Throat->PhaseStartedAt-=Throat->SwallowSeconds+0.01; Throat->Tick(.01f);
    TestTrue(TEXT("Meal disposed only after the gulp"),Food->IsDisposed() && Throat->FoodSwallowed==1);
    Throat->PhaseStartedAt-=Throat->RecoverySeconds+0.01; Throat->Tick(.01f);
    TestEqual(TEXT("Returns to closed collecting state"),Throat->ThroatPhase,EMCThroatPhase::Collecting);
    Throat->Tick(1); TestEqual(TEXT("Standing on the button does not loop"),Throat->SwallowCount,1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCThroatEligibilityTest,"MessControl.Throat.BoundsHeldFoodAndReset",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCThroatEligibilityTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth; auto* Throat=Mouth.World->SpawnActor<AMCThroat>(FVector(0,0,200),FRotator(0,65,0));
    auto* Food=Mouth.GripCube(); Food->Phase=EMCFoodPhase::Free;
    auto Place=[&](FVector Offset){Food->SetActorLocation(Throat->GetActorTransform().TransformPosition(Throat->ZoneCenter+Offset));};
    Place(FVector(0,0,60)); TestTrue(TEXT("Rotated zone accepts food"),Throat->ContainsFood(Food));
    Place(FVector(Throat->ZoneRadius+10,0,60)); TestFalse(TEXT("Outside circular zone rejected"),Throat->ContainsFood(Food));
    Place(FVector(0,0,Throat->ZoneHeight+10)); TestFalse(TEXT("Food high above zone rejected"),Throat->ContainsFood(Food));
    Place(FVector(0,0,60)); auto* Hero=Mouth.Worker(); Food->Holders.Add(Hero);
    TestFalse(TEXT("Held food remains with its player"),Throat->ContainsFood(Food)); Food->Holders.Empty();
    Food->Phase=EMCFoodPhase::Stuck; TestFalse(TEXT("Stuck food rejected"),Throat->ContainsFood(Food));
    Food->Phase=EMCFoodPhase::Free; Throat->ThroatPhase=EMCThroatPhase::Anticipation;
    Throat->PhaseStartedAt=Mouth.World->GetTimeSeconds()-Throat->AnticipationSeconds-.01; Throat->Tick(.01f);
    TestEqual(TEXT("Reset fixture has a meal still in flight"),Food->Phase,EMCFoodPhase::Swallowing);
    Throat->ResetSwallow(); TestEqual(TEXT("Reset closes the throat"),Throat->ThroatPhase,EMCThroatPhase::Collecting);
    TestEqual(TEXT("Reset releases an unfinished meal alive"),Food->Phase,EMCFoodPhase::Free);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCThroatRiskTest,"MessControl.Throat.OrderJumpEscapeAndSpasm",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCThroatRiskTest::RunTest(const FString&)
{
    FTestMouth Mouth;
    auto* Throat=Mouth.World->SpawnActor<AMCThroat>(FVector(5000,0,200),FRotator(0,35,0));
    auto* Food=Mouth.GripCube(); Food->Body->SetSimulatePhysics(false); Food->Phase=EMCFoodPhase::Free;
    const FVector Center=Throat->GetActorTransform().TransformPosition(Throat->ZoneCenter);
    Food->SetActorLocation(Center+FVector(0,0,45));
    const FVector TipAxis=Throat->Uvula->GetComponentTransform().TransformPosition(FVector(0,0,-50));
    TestTrue(TEXT("Old central landing intersects the visible uvula"),Throat->UvulaBodyClearance(TipAxis,40,62)<0);
    const FVector FrontLanding=Throat->UvulaLanding->GetComponentLocation()+FVector(0,0,16+58+3);
    TestTrue(TEXT("New front landing clears the visible stalk and bulb"),Throat->UvulaBodyClearance(FrontLanding,40,62)>2);
    auto* Hero=Mouth.Worker(); Hero->SetActorLocation(Center+FVector(0,0,60)); Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    TestTrue(TEXT("Space offer appears for a grounded player with staged food"),Throat->CanOrderJump(Hero));
    Hero->SetActorLocation(Center+Throat->GetActorForwardVector()*210+FVector(0,0,60));
    TestFalse(TEXT("An approach through the visible uvula is not offered"),Throat->CanOrderJump(Hero));
    Hero->SetActorLocation(Center-Throat->GetActorForwardVector()*(Throat->ZoneRadius+50)+FVector(0,0,60));
    TestFalse(TEXT("Remote player cannot order a jump"),Throat->LaunchToUvula(Hero));
    Hero->SetActorLocation(Center+FVector(0,0,60));
    TestTrue(TEXT("Server begins preparation for the raised uvula"),Throat->LaunchToUvula(Hero));
    TestFalse(TEXT("Preparation does not launch immediately"),Hero->bOrderJumpLaunched);
    TestFalse(TEXT("Repeated requests during the same jump are rejected"),Throat->LaunchToUvula(Hero));
    Food->SetActorLocation(Center+FVector(0,2000,45)); Throat->Tick(.1f);
    TestNull(TEXT("Removing the meal cancels the pending jump"),Hero->OrderJumpTarget.Get());
    Food->SetActorLocation(Center+FVector(0,0,45));
    TestTrue(TEXT("The next valid order can prepare again"),Throat->LaunchToUvula(Hero));
    Hero->OrderJumpStartedAt-=AMCToothCharacter::OrderPrepareSeconds+.01f; Throat->Tick(.01f);
    TestTrue(TEXT("Takeoff follows the preparation"),Hero->bOrderJumpLaunched);
    Hero->ClearOrderJump(); Hero->GetCharacterMovement()->StopMovementImmediately();
    Throat->ThroatPhase=EMCThroatPhase::Anticipation; Throat->PhaseStartedAt=Mouth.World->GetTimeSeconds();
    Throat->Tick(.1f); TestNull(TEXT("Warning gives time to run away"),Hero->SwallowedBy.Get());
    Throat->PhaseStartedAt-=Throat->AnticipationSeconds+.01; Throat->Tick(.01f);
    TestTrue(TEXT("Player left in circle is captured"),Hero->SwallowedBy==Throat && !Hero->CanWork());
    Throat->PhaseStartedAt-=Throat->SwallowSeconds*.8f; Throat->Tick(.01f);
    TestEqual(TEXT("A tooth triggers a spasm"),Throat->ThroatPhase,EMCThroatPhase::Spasm);
    TestFalse(TEXT("Failed order is not credited"),Food->IsDisposed());
    Throat->PhaseStartedAt-=Throat->SpasmSeconds+.01; Throat->Tick(.01f);
    TestTrue(TEXT("Tooth exits the throat alive with an outward ballistic impulse"),!Hero->SwallowedBy && Hero->Status->IsAlive() &&
        FVector::DotProduct(Hero->GetCharacterMovement()->PendingLaunchVelocity,Throat->GetActorForwardVector())<-900 && Hero->GetCharacterMovement()->PendingLaunchVelocity.Z>400);
    TestEqual(TEXT("Expulsion has its own visible phase"),Throat->ThroatPhase,EMCThroatPhase::Vomiting);
    TestTrue(TEXT("Food returns to play"),Food->Phase==EMCFoodPhase::Free && Throat->FoodSwallowed==0 && Throat->SpasmCount==1);
    Food->Body->SetSimulatePhysics(false); Food->SetActorLocation(Center+FVector(0,0,45)); Hero->SetActorLocation(Center+FVector(0,0,60));
    Throat->ThroatPhase=EMCThroatPhase::Anticipation; Throat->PhaseStartedAt=Mouth.World->GetTimeSeconds()-Throat->AnticipationSeconds-.01; Throat->Tick(.01f);
    Throat->ResetSwallow(); TestTrue(TEXT("Restart releases both captured food and player"),!Hero->SwallowedBy && Food->Phase==EMCFoodPhase::Free && Hero->CanWork());
    return true;
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
    FTestMouth Mouth; auto* A=Mouth.Worker(); auto* B=Mouth.Worker(); auto* Tooth=Mouth.Worker(); Tooth->SetActorLocation(FVector(1000,0,150));
    Tooth->Status->ApplyCoffee(1); const FVector Near=Tooth->GetActorLocation()+FVector(0,Tooth->GetCapsuleComponent()->Bounds.BoxExtent.Y+75,0);
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
    Tooth->Status->ApplyCoffee(1); A->ResetContact(); B->SetActorLocation(Near+FVector(60,0,0)); B->SetActorRotation(FRotator(0,-90,0)); B->bBrushing=true;
    for (int32 I=0;I<10;++I) { A->AdvanceCare(.1f); B->AdvanceCare(.1f); }
    TestEqual(TEXT("Two workers clear four contacts in one second"),Tooth->Status->State.CoffeeLeft,0);
    TestFalse(TEXT("No over-cleaning"),Tooth->Status->CareContact(true));
    Tooth->Status->ApplyCoffee(1); A->ResetContact(); A->AdvanceCare(-1); A->AdvanceCare(std::numeric_limits<float>::quiet_NaN());
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
    // Match CharacterMovement: teleport the capsule while preserving the mesh world pose,
    // then apply the separate mesh correction without a teleport flag.
    const FVector BodyBefore=Hero->ToothPhysics->PhysicalLocation();
    const FVector MeshBefore=Hero->GetMesh()->GetComponentLocation();
    const FVector Correction(600,0,0);
    {
        const FScopedPreventAttachedComponentMove PreventMeshMove(Hero->GetMesh());
        Hero->SetActorLocation(Hero->GetActorLocation()+Correction,false,nullptr,ETeleportType::TeleportPhysics);
    }
    Hero->GetMesh()->SetWorldLocation(MeshBefore+Correction,false,nullptr,ETeleportType::None);
    ++GFrameCounter; Mouth.World->Tick(LEVELTICK_All,1.f/60);
    const FVector BodyAfter=Hero->ToothPhysics->PhysicalLocation();
    TestTrue(*FString::Printf(TEXT("Large capsule correction also moves the simulated body: before %s, after %s"),*BodyBefore.ToString(),*BodyAfter.ToString()),
        FVector::Dist2D(BodyAfter,BodyBefore+Correction)<20);
    Hero->ToothPhysics->ApplyHit(FVector(450,0,250),Hero->GetActorLocation());
    TestEqual(TEXT("Artist rig enters ragdoll"),Hero->ToothPhysics->GetBodyState(),EMCBodyState::Ragdoll);
    TestTrue(TEXT("Artist core actually simulates"),Hero->GetMesh()->IsSimulatingPhysics(Hero->RigBone(TEXT("body"))));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCActiveRagdollTest,"MessControl.Physics.ActiveRagdollComparisonAndContactGates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCActiveRagdollTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth; auto* H=Mouth.Worker(); auto* Physics=H->ToothPhysics.Get(); auto* Mesh=H->GetMesh();
    auto* Motors=H->FindComponentByClass<UPhysicsControlComponent>();
    if(!TestNotNull(TEXT("Physics controls"),Motors)) return false;
    auto Weight=[&](FName Role) { const auto* B=Mesh->GetBodyInstance(H->RigBone(Role)); return B?B->PhysicsBlendWeight:-1.f; };
    Mouth.Step(.8f);
    TestEqual(TEXT("Experiment defaults off"),Physics->GetActiveRagdollMode(),EMCActiveRagdollMode::Off);
    TestTrue(TEXT("Original leg presentation retained"),FMath::IsNearlyEqual(Weight(TEXT("foot_l")),.05f,.005f));
    TestFalse(TEXT("Unknown mode rejected"),Physics->SetActiveRagdollMode(static_cast<EMCActiveRagdollMode>(255)));
    TestTrue(TEXT("Soft comparison enabled"),Physics->SetActiveRagdollMode(EMCActiveRagdollMode::Soft));
    Mouth.Step(1.f);
    TestTrue(TEXT("Reference root stays stable while limbs simulate"),!Mesh->IsSimulatingPhysics(H->RigBone(TEXT("body"))) && Weight(TEXT("body"))==0);
    for(FName Role:{FName("hand_l"),FName("hand_r"),FName("foot_l"),FName("foot_r")})
        TestTrue(*FString::Printf(TEXT("Fully physical free %s"),*Role.ToString()),Weight(Role)>.99f);
    FPhysicsControlData Soft; Motors->GetControlData(Motors->GetControlNamesInSet(TEXT("arm_l"))[0],Soft);
    TestTrue(TEXT("Pure animation targets and finite motor torque"),!Soft.bUseSkeletalAnimation && Soft.MaxTorque>0);
    const auto& Ref=Mesh->GetSkeletalMeshAsset()->GetRefSkeleton(); TArray<FTransform> Pose=Ref.GetRefBonePose();
    const int32 Arm=Ref.FindBoneIndex(H->RigBone(TEXT("arm_l"))),Root=Ref.FindBoneIndex(H->RigBone(TEXT("body")));
    Pose[Arm].ConcatenateRotation(FRotator(8,0,0).Quaternion());
    Physics->SubmitAnimationTargets(Pose,Ref,1.f/60);
    FPhysicsControlTarget Target; Motors->GetControlTarget(Motors->GetControlNamesInSet(TEXT("arm_l"))[0],Target);
    TArray<FTransform> CS; CS.SetNum(Pose.Num());
    for(int32 I=0;I<Pose.Num();++I) CS[I]=Ref.GetParentIndex(I)<0?Pose[I]:Pose[I]*CS[Ref.GetParentIndex(I)];
    const FTransform Goal=CS[Arm].GetRelativeTransform(CS[Root]);
    TestTrue(TEXT("Arm motor follows the unblended authored pose"),Target.TargetOrientation.Quaternion().AngularDistance(Goal.GetRotation())<.001);
    const FTransform MeshBefore=Mesh->GetRelativeTransform();
    Mesh->SetRelativeLocation(MeshBefore.GetLocation()+FVector(-120,35,10));
    Physics->SubmitAnimationTargets(Pose,Ref,0);
    Motors->GetControlTarget(Motors->GetControlNamesInSet(TEXT("arm_l"))[0],Target);
    TestTrue(TEXT("Network mesh smoothing cannot displace the parent-space muscle goal"),Target.TargetPosition.Equals(Goal.GetLocation(),.001));
    Mesh->SetRelativeTransform(MeshBefore);
    for(int32 I=0;I<90;++I) Physics->SetGripArms(false,true);
    TestTrue(TEXT("Precision contact returns torso and working hand to IK"),Weight(TEXT("body"))<.001f && Weight(TEXT("hand_r"))<.001f);
    TestFalse(TEXT("Held hand muscle is released"),Motors->GetControlEnabled(Motors->GetControlNamesInSet(TEXT("arm_r"))[0]));
    Mouth.Step(1.f); // Real grip update releases the synthetic contact and settles the limb.
    TestTrue(TEXT("Released physical hand returns smoothly under stable root"),Weight(TEXT("body"))==0 && Weight(TEXT("hand_r"))>.99f);
    Physics->SetActiveRagdollMode(EMCActiveRagdollMode::Firm);
    FPhysicsControlData Firm; Motors->GetControlData(Motors->GetControlNamesInSet(TEXT("arm_l"))[0],Firm);
    TestTrue(TEXT("Firm comparison has stronger muscles"),Firm.AngularStrength>Soft.AngularStrength);
    Physics->ApplyHit(FVector(350,0,250),H->GetActorLocation());
    TestEqual(TEXT("Strong hit still falls"),Physics->GetBodyState(),EMCBodyState::Ragdoll);
    Physics->SetActiveRagdollMode(EMCActiveRagdollMode::Soft);
    TestFalse(TEXT("Changing comparison cannot wake a fallen balance motor"),Motors->GetControlEnabled(Motors->GetControlNamesInSet(TEXT("Balance"))[0]));
    TestTrue(TEXT("Changing comparison cannot freeze a fallen body"),Mesh->IsSimulatingPhysics(H->RigBone(TEXT("body"))));
    Physics->SetThroatCaptured(true);
    TestFalse(TEXT("Gulp still owns the complete physical body"),Mesh->IsSimulatingPhysics(H->RigBone(TEXT("body"))));
    Physics->SetThroatCaptured(false); Physics->SetActiveRagdollMode(EMCActiveRagdollMode::Off); Mouth.Step(1.f);
    TestTrue(TEXT("Returning to the original mode preserves the mesh attachment"),Mesh->GetAttachParent()==H->GetCapsuleComponent());
    TestTrue(TEXT("Original presentation restored without restart"),FMath::IsNearlyEqual(Weight(TEXT("body")),.25f,.002f) && FMath::IsNearlyEqual(Weight(TEXT("foot_l")),.05f,.002f));
    Motors->GetControlData(Motors->GetControlNamesInSet(TEXT("arm_l"))[0],Firm);
    TestTrue(TEXT("Original animation cache and strength restored"),Firm.bUseSkeletalAnimation && Firm.AngularStrength==Physics->Settings.MuscleStrength);
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
    D->Next(); TestTrue(TEXT("Cold cola is a real next event"),D->ColdCola && D->ColdCola->bActive);
    TestTrue(TEXT("Cola starts its own drink"),D->ColdCola && D->ColdCola->Drink && D->ColdCola->Drink->IsActive());
    D->Next(); TestNull(TEXT("Leaving cold cola clears its actor"),D->ColdCola.Get());
    TestTrue(TEXT("Stuck food exists at arena teeth"),D->CountFood(3)>0);
    for (TActorIterator<AMCFoodActor> It(Mouth.World);It;++It) if (It->Batch==3)
    { TestNotNull(TEXT("Stuck anchor is an arena tooth"),Cast<AMCArenaTooth>(It->StuckTooth)); TestEqual(TEXT("Food remains stuck until pulled"),It->Phase,EMCFoodPhase::Stuck); }
    D->Next(); TestTrue(TEXT("Stops at day one, does not invent the foam party"),Mouth.State->bDayOneComplete);
    Mouth.Mode->RestartShift(); TestNull(TEXT("Restart removes director"),Mouth.Mode->DayDirector.Get());
    TestFalse(TEXT("Restart clears brush inventory rules"),Mouth.State->bPhysicalBrushes);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCLiquidSpawnFloorTest,"MessControl.Liquid.SpawnIgnoresDynamicObstacles",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCLiquidSpawnFloorTest::RunTest(const FString&)
{
    FTestMouth Mouth; Mouth.Mode->SetActorTickEnabled(false);
    const FTransform FloorTransform(FQuat::Identity,FVector::ZeroVector,FVector(30,30,1));
    auto* Tongue=Mouth.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FloorTransform);
    Tongue->SourceMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
    Tongue->FinishSpawning(FloorTransform);
    auto* Obstacle=Mouth.World->SpawnActor<AActor>();
    auto* Box=NewObject<UBoxComponent>(Obstacle); Obstacle->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(2000,2000,20)); Box->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    Box->RegisterComponent(); Obstacle->SetActorLocation(FVector(0,0,150));
    FHitResult Hit;
    TestTrue(TEXT("Dynamic obstacle intercepts the old spawn query"),
        Mouth.World->LineTraceSingleByChannel(Hit,FVector(-700,-340,350),FVector(-700,-340,-250),ECC_WorldStatic) && Hit.GetActor()==Obstacle);
    auto* Director=Mouth.World->SpawnActor<AMCDayDirector>();
    auto* Plan=LoadObject<UMCDayPlan>(nullptr,TEXT("/Game/Data/DA_Day01.DA_Day01"));
    if (!TestNotNull(TEXT("Day one plan loaded"),Plan)) return false;
    Director->Start(Plan,0,true);
    int32 Count=0;
    for (TActorIterator<AMCMouthSurface> It(Mouth.World);It;++It) if (!It->bUlcer)
    {
        ++Count;
        TestTrue(TEXT("Puddle spawns on the tongue beneath the obstacle"),FMath::IsNearlyEqual(It->GetActorLocation().Z,5.,.05));
    }
    TestEqual(TEXT("All planned puddles spawned"),Count,Plan->SurfacePatches);
    Tongue->SetActorLocation(FVector(0,0,-40));
    for (TActorIterator<AMCMouthSurface> It(Mouth.World);It;++It) if (!It->bUlcer)
    {
        It->Tick(1.f/60);
        TestTrue(TEXT("Puddle keeps its binding when the tongue moves"),FMath::IsNearlyEqual(It->GetActorLocation().Z,-35.,.05));
    }
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
    TestTrue(TEXT("Whole variants configured"),!Row->WholeMeshes.IsEmpty()); TestTrue(TEXT("Fragments configured"),!Row->FragmentMeshes.IsEmpty());
    for (const auto& Mesh:Row->WholeMeshes) if (!Mesh.IsNull()) ReadyCareTestMesh(Mesh.ToSoftObjectPath().ToString());
    for (const auto& Mesh:Row->FragmentMeshes) if (!Mesh.IsNull()) ReadyCareTestMesh(Mesh.ToSoftObjectPath().ToString());
    FMCFoodRow ScaledRow=*Row; ScaledRow.Scale*=FVector(.5,.75,1.25); ScaledRow.FragmentScale=FVector(.27,.41,.63);
    auto* Food=Mouth.World->SpawnActor<AMCFoodActor>(FVector(0,0,160),FRotator::ZeroRotator); Food->ConfigureItem(TEXT("Broccoli"),ScaledRow,Random);
    TestTrue(TEXT("Menu scale reaches the rendered food"),Food->Visual->GetRelativeScale3D().Equals(ScaledRow.Scale));
    TestTrue(TEXT("Scaled collision fits the visible food"),Food->Body->GetUnscaledBoxExtent().Equals((Food->ItemMesh->GetBounds().BoxExtent*ScaledRow.Scale).ComponentMax(FVector(3)),.01));
    TestTrue(TEXT("Grip queries inherit the visible scale"),Food->GripSurface->GetComponentScale().Equals(Food->Visual->GetComponentScale()));
    Food->Batch=22; Food->SpoilAt=35;
    const float Heavy=Food->DragSpeed(); Food->Settings.Mass=3; TestTrue(TEXT("Lighter food can be dragged faster"),Food->DragSpeed()>Heavy); Food->Settings.Mass=Row->Mass;
    Food->HitFood(Row->Health,FVector::ForwardVector);
    TestTrue(TEXT("Whole food is replaced"),Food->IsDisposed()); int32 Count=0; float Mass=0;
    for (TActorIterator<AMCFoodActor> It(Mouth.World);It;++It) if (It->bFragment && !It->IsDisposed())
    {
        ++Count; Mass+=It->Settings.Mass; TestEqual(TEXT("Pieces keep the objective batch"),It->Batch,22);
        TestEqual(TEXT("New pieces share the attended parent's deadline"),It->SpoilAt,Food->SpoilAt);
        TestNotNull(TEXT("Fragment mesh loaded"),It->ItemMesh.Get());
        TestTrue(TEXT("Fragment scale is independent of whole food scale"),It->Visual->GetRelativeScale3D().Equals(ScaledRow.FragmentScale));
        if (It->ItemMesh) TestTrue(TEXT("Fragment collision fits its independent scale"),It->Body->GetUnscaledBoxExtent().Equals((It->ItemMesh->GetBounds().BoxExtent*ScaledRow.FragmentScale).ComponentMax(FVector(3)),.01));
        It->HitFood(10000,FVector::ForwardVector); TestFalse(TEXT("Hitting fragments does not delete cleanup work"),It->IsDisposed());
    }
    TestEqual(TEXT("Configured number of pieces"),Count,Row->Fragments); TestTrue(TEXT("Fragment mass conserves whole mass"),FMath::IsNearlyEqual(Mass,Row->Mass));
    auto* Floor=Mouth.World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Floor); Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(1000,1000,10)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-10));
    auto* Rot=Mouth.World->SpawnActor<AMCFoodActor>(FVector(300,300,80),FRotator::ZeroRotator); Rot->ConfigureItem(TEXT("Egg"),*Row,Random); Rot->SpoilAt=.001;
    ++GFrameCounter; Mouth.World->Tick(LEVELTICK_All,.05f); Rot->Tick(.1f);
    TestFalse(TEXT("Generic floor cannot absorb food"),Rot->bSpoiled);
    int32 Ulcers=0; for (TActorIterator<AMCMouthSurface> It(Mouth.World);It;++It) if (It->bUlcer) ++Ulcers;
    TestEqual(TEXT("Ulcers only emerge on the tongue"),Ulcers,0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodAbsorptionTest,"MessControl.DayOne.UnattendedFoodBecomesTreatmentTask",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodAbsorptionTest::RunTest(const FString&)
{
    FTestMouth Mouth; Mouth.Mode->SetActorTickEnabled(false); Mouth.State->Phase=EMCShiftPhase::Working; Mouth.State->bDevManualEvents=true;
    const FTransform FloorTransform(FQuat::Identity,FVector::ZeroVector,FVector(30,30,1));
    auto* Tongue=Mouth.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FloorTransform);
    Tongue->SourceMesh=ReadyCareTestMesh(TEXT("/Engine/BasicShapes/Plane.Plane")); Tongue->FinishSpawning(FloorTransform);
    ++GFrameCounter; Mouth.World->Tick(LEVELTICK_All,.1f);
    const auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
    const auto* Row=Table?Table->FindRow<FMCFoodRow>(TEXT("Egg"),TEXT("Absorption test")):nullptr;
    if(!TestNotNull(TEXT("Food row exists"),Row)) return false;
    for(const auto& Mesh:Row->WholeMeshes) if(!Mesh.IsNull()) ReadyCareTestMesh(Mesh.ToSoftObjectPath().ToString());
    FRandomStream Random(41); auto* Food=Mouth.World->SpawnActor<AMCFoodActor>(); Food->ConfigureItem(TEXT("Egg"),*Row,Random);
    Food->Phase=EMCFoodPhase::Free; Food->Body->SetSimulatePhysics(false); Food->Batch=22;
    FHitResult Floor; if(!TestTrue(TEXT("Real tongue supports food"),Tongue->SurfacePoint(FVector::ZeroVector,Floor))) return false;
    Food->SetActorLocation(Floor.ImpactPoint+FVector(0,0,Food->Body->Bounds.BoxExtent.Z+2));
    Food->SpoilAt=10; Food->UpdateAbsorption(.1f); TestEqual(TEXT("Fresh food remains free"),Food->Phase,EMCFoodPhase::Free);
    Food->SpoilAt=.001; Food->UpdateAbsorption(.1f); TestEqual(TEXT("Neglected food starts sinking"),Food->Phase,EMCFoodPhase::Absorbing);
    const float StartZ=Food->GetActorLocation().Z;
    Food->AbsorbStartedAt-=Food->FoodData.AbsorbSeconds*.5; Food->UpdateAbsorption(.1f);
    TestTrue(TEXT("Food visibly sinks before becoming a lesion"),Food->GetActorLocation().Z<StartZ && !Food->IsDisposed());
    Food->AttendFood(); TestEqual(TEXT("Attention rescues the sinking food"),Food->Phase,EMCFoodPhase::Free);
    TestTrue(TEXT("Attention resets the unattended deadline"),Food->SpoilAt>0 && Food->GetActorLocation().Z>Floor.ImpactPoint.Z);
    Food->Body->SetSimulatePhysics(false); Food->SpoilAt=.001; Food->UpdateAbsorption(.1f);
    Food->AbsorbStartedAt-=Food->FoodData.AbsorbSeconds+.01; Food->UpdateAbsorption(.1f);
    auto* Patch=Food->AbsorbedUlcer.Get(); if(!TestNotNull(TEXT("Fully absorbed food creates an ulcer"),Patch)) return false;
    TestTrue(TEXT("The old food is hidden and cannot be eaten"),Food->bAbsorbed && Food->IsDisposed());
    TestTrue(TEXT("Ulcer keeps the food's objective batch"),Patch->bUlcer && Patch->Batch==22);
    auto* Director=Mouth.World->SpawnActor<AMCDayDirector>();
    TestEqual(TEXT("Absorption does not count as successful cleanup"),Director->CountFood(22),1);
    Patch->Tick(10); TestEqual(TEXT("Ulcer requires spray, with no automatic healing"),Patch->Healing,0.f);
    Patch->Healing=1; TestEqual(TEXT("Curing the ulcer resolves the meal's work"),Director->CountFood(22),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCUlcerAndFloodTest,"MessControl.DayOne.UlcerProtectionAndCoffeeControl",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCUlcerAndFloodTest::RunTest(const FString& Parameters)
{
    FTestMouth Mouth; Mouth.State->Phase=EMCShiftPhase::Working;
    auto* Patch=Mouth.World->SpawnActor<AMCMouthSurface>(FVector(300,300,5),FRotator::ZeroRotator); Patch->bUlcer=true; Patch->HealSeconds=7;
    const float Before=Mouth.State->MouthHealth; Patch->Tick(.5f);
    TestTrue(TEXT("Untreated ulcer hurts mouth without healing"),Patch->Healing==0 && Mouth.State->MouthHealth<Before);
    auto* Hero=Mouth.Worker(); Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Hero->SetActorLocation(FVector(300,300,65)); Patch->Tick(.1f);
    Patch->Healing=.25f; Patch->Disturb(); TestEqual(TEXT("Walking over ulcer preserves treatment progress"),Patch->Healing,.25f);
    Hero->GetCharacterMovement()->SetMovementMode(MOVE_Falling); Patch->Tick(.1f);
    TestFalse(TEXT("Passing over ulcer in the air does not count as stepping"),Patch->bDisturbed);
    Hero->SetActorLocation(FVector(0,0,95)); Patch->Tick(20); TestEqual(TEXT("Idle time never completes treatment"),Patch->Healing,.25f);
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
    const FTransform TongueTransform(FQuat::Identity,FVector::ZeroVector,FVector(30,30,1));
    auto* Tongue=Mouth.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),TongueTransform);
    Tongue->SourceMesh=ReadyCareTestMesh(TEXT("/Engine/BasicShapes/Plane.Plane")); Tongue->FinishSpawning(TongueTransform);
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
    Mouth.Mode->ExecuteDevAction(PC,EMCDevAction::SwimCoffee);
    auto* SwimWater=Mouth.Mode->DayDirector->Flood.Get();
    TestEqual(TEXT("F3 swimming holds a full flood for ten minutes"),SwimWater->WaterSettings.HoldSeconds,600.f);
    SwimWater->StartedAt=Mouth.World->GetTimeSeconds()-300; SwimWater->Tick(.1f);
    TestTrue(TEXT("The middle of the long test stays full without a jet or drain"),SwimWater->IsActive() && SwimWater->GetPhase()==EMCCoffeePhase::Holding
        && !SwimWater->Jet->IsVisible() && !SwimWater->DrainRibbon->IsVisible());
    Mouth.Mode->ExecuteDevAction(PC,EMCDevAction::CoffeeDirt);
    TestTrue(TEXT("Dirt action includes teeth and tissue"),Mouth.Mode->DayDirector->CountDirt()>8);
    Mouth.Mode->ExecuteDevAction(PC,EMCDevAction::StopCoffee);
    TestFalse(TEXT("Drain stops real flood"),Mouth.Mode->DayDirector->Flood->IsActive());
    Mouth.Mode->ExecuteDevAction(PC,EMCDevAction::Infection);
    for (int32 I=0;I<160;++I) { ++GFrameCounter; Mouth.World->Tick(LEVELTICK_All,.05f); }
    AMCMouthSurface* Ulcer=nullptr;
    for (TActorIterator<AMCMouthSurface> It(Mouth.World);It;++It) if (It->bUlcer) Ulcer=*It;
    TestNotNull(TEXT("Infection button produces a lesion through unattended absorption"),Ulcer);
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
    Water.Cycles=1; Water.HoldSeconds=600;
    TestTrue(TEXT("Long test holds a full level after filling"),Water.Phase(4)==EMCCoffeePhase::Holding && Water.Phase(603.9f)==EMCCoffeePhase::Holding && Water.FillAmount(300)==1);
    TestTrue(TEXT("Holding has no pour or drain force"),Water.JetAmount(300)==0 && Water.DrainAmount(300)==0 && Water.FlowAt(P,300,320).IsNearlyZero());
    TestTrue(TEXT("Ten minute hold drains normally then ends"),Water.Phase(604)==EMCCoffeePhase::Draining && FMath::IsNearlyEqual(Water.FillAmount(605),.5f) && Water.Phase(606)==EMCCoffeePhase::Inactive);
    Water.HoldSeconds=0;
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
    // Let spawn protection expire, then verify the artist's lower floor receives
    // the coffee stain while the new front-wave behavior preserves control.
    Flood->SetActorTickEnabled(false);
    for (int32 I=0;I<45;++I) { ++GFrameCounter; Mouth.World->Tick(LEVELTICK_All,1.f/60); }
    Flood->SetActorTickEnabled(true);
    LowHero->SetActorLocation(FVector(Flood->WaterSettings.Inlet.X-300,Flood->WaterSettings.Inlet.Y+400,-105));
    Flood->WaterSettings.DryHeight=-200;
    Flood->StartedAt=Mouth.World->GetTimeSeconds()-(.25f+500.f/Flood->WaterSettings.FrontSpeed);
    Flood->Tick(.016f);
    TestTrue(TEXT("Coffee reaches players on the lower artist tongue"),LowHero->Status->State.CoffeeLeft>0);
    TestTrue(TEXT("Coffee front preserves player control"),LowHero->ToothPhysics->CanAct() && LowHero->ToothPhysics->KnockdownCount==0);
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
    Advance(100);
    TestEqual(TEXT("Saved default keeps normal gameplay free of random jolts"),Tongue->Motion.Serial,0);
    TestFalse(TEXT("Native default disables random jolts"),FMCTongueSettings().bAutomaticJolts);
    TestTrue(TEXT("Explicit jolt still starts"),Tongue->TriggerJolt());
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
    TestEqual(TEXT("Returning to normal play does not trigger a random jolt"),Tongue->Motion.Serial,1);
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
    TestTrue(TEXT("Grip does not project the capsule into the food"),FVector::Dist2D(Hero->GetActorLocation(),Food->Visual->Bounds.GetBox().GetClosestPointTo(Hero->GetActorLocation()))>=Hero->GetCapsuleComponent()->GetScaledCapsuleRadius()-1);
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
    TestTrue(TEXT("Carried food keeps Chaos collision and momentum"),Food->Body->IsSimulatingPhysics());
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
    {
        // Character's closed M/B/P is Basis; the importer drops numerical noise.
        if (Name==TEXT("Mouth_MBP") && Mesh->GetSkeletalMeshAsset()->FindMorphTarget(TEXT("Eyes_Blink"))) continue;
        if (!TestNotNull(*FString::Printf(TEXT("Imported mouth shape %s"),*Name.ToString()),Mesh->GetSkeletalMeshAsset()->FindMorphTarget(Name))) return false;
    }
    TestEqual(TEXT("Character and bag have their own material slots"),Mesh->GetNumMaterials(),2);
    TestTrue(TEXT("Appearance override preserves the bag material"),Mesh->GetMaterial(0)!=Mesh->GetMaterial(1));
    if (!TestNotNull(TEXT("Artist blink is imported"),Mesh->GetSkeletalMeshAsset()->FindMorphTarget(TEXT("Eyes_Blink")))) return false;
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
    Face->ApplyMorphBlink(1);
    TestEqual(TEXT("A closed blink is applied through the artist morph"),Mesh->GetMorphTarget(TEXT("Eyes_Blink")),1.f);
    TestTrue(TEXT("Blink replaces the emotional eyelid deformation"),FMath::IsNearlyEqual(Mesh->GetMorphTarget(TEXT("BlinkCancel_Mouth_Smile")),Mesh->GetMorphTarget(TEXT("Mouth_Smile")),.001f));
    for (uint8 I=1;I<=uint8(MCViseme::D);++I)
    {
        const auto Viseme=MCViseme(I);
        for (int32 Frame=0;Frame<24;++Frame)
        {
            Face->SetSpeechInput(Viseme==MCViseme::Closed?0.f:1.f,Viseme); Mouth.Step(1.f/60); CheckWeights();
        }
        if (Viseme==MCViseme::Closed && !Mesh->GetSkeletalMeshAsset()->FindMorphTarget(TEXT("Mouth_MBP")))
        {
            float Sum=0; for (FName Name:UMCExpressionComponent::MouthShapes()) Sum+=Mesh->GetMorphTarget(Name);
            TestTrue(TEXT("Silent closed lips suppress other poses and return to the artist's neutral mouth"),Sum<.01f);
        }
        else TestTrue(TEXT("Each viseme drives the imported mesh"),Mesh->GetMorphTarget(UMCExpressionComponent::VisemeShape(Viseme))>.98f);
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
    Mouth.Step(.05f); TestTrue(TEXT("Contraction begins smoothly"),G->PupilScale>.7f && G->PupilScale<.98f);
    Mouth.Step(.3f); TestTrue(TEXT("Danger contracts the rendered pupil"),G->PupilScale<.7f && Mesh->GetMorphTarget(TEXT("Pupil_Contract"))>.75f);
    TestEqual(TEXT("Danger does not also dilate the pupil"),Mesh->GetMorphTarget(TEXT("Pupil_Dilate")),0.f);
    Mouth.Step(.55f); TestTrue(TEXT("Pupil recovers gradually after the threat expires"),G->PupilScale>.68f && G->PupilScale<.95f);
    Mouth.Step(2); TestTrue(TEXT("Pupil returns to rest"),FMath::Abs(G->PupilScale-1)<.015f);
    Hero->Expression->ServerPlayEmote(TEXT("happy")); Mouth.Step(.4f);
    TestTrue(TEXT("A positive emotion enlarges the pupil"),G->PupilScale>1.5f && Mesh->GetMorphTarget(TEXT("Pupil_Dilate"))>.6f);
    TestEqual(TEXT("Positive emotion does not also contract the pupil"),Mesh->GetMorphTarget(TEXT("Pupil_Contract")),0.f);
    Hero->Status->Damage(10); Mouth.Step(.25f); TestTrue(TEXT("Pain contracts the pupil even during a positive emotion"),G->PupilScale<.95f);
    Mouth.Step(2); Hero->NotifyTaskFeedback(true); Mouth.Step(.4f);
    TestTrue(TEXT("A successful gameplay task enlarges the pupil"),G->PupilScale>1.5f);
    G->NoticePoint(Point,1); Mouth.Step(.35f);
    TestTrue(TEXT("Danger takes priority over positive task feedback"),G->PupilScale<.7f);
    Mouth.Step(3); Hero->Expression->ServerPlayEmote(TEXT("surprise")); Mouth.Step(.4f);
    TestTrue(TEXT("Surprise contracts the pupil"),G->PupilScale<.75f);
    Mouth.Step(3); Hero->Expression->ServerPlayEmote(TEXT("artist_shock")); Mouth.Step(.4f);
    TestTrue(TEXT("The authored shock animation contracts the pupil"),G->PupilScale<.8f);
    Mouth.Step(4); Hero->Expression->ServerPlayEmote(TEXT("angry")); Mouth.Step(.4f);
    TestTrue(TEXT("Focused expression contracts the pupil"),G->PupilScale<.9f && Mesh->GetMorphTarget(TEXT("Pupil_Contract"))>.25f);
    G->NoticePoint(Point,1); Mouth.Step(.35f);
    TestTrue(TEXT("Danger takes priority over a selected expression"),G->PupilScale<.7f);
    FMCGazeSettings S; S.PupilRest=100; S.PupilFocus=-100; S.PupilDanger=100; S.PupilPain=100; S.PupilPositive=-100; S.PupilReactSpeed=std::numeric_limits<float>::quiet_NaN(); S.Sanitize();
    TestTrue(TEXT("Pupil tuning preserves contraction and dilation directions"),S.PupilRest<=1.8f && S.PupilFocus>=.6f && S.PupilDanger<=S.PupilRest && S.PupilPain<=S.PupilRest && S.PupilPositive>=S.PupilRest && FMath::IsFinite(S.PupilReactSpeed));
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
    auto CheckContact=[&]()
    {
        FHitResult Floor; if (!TestTrue(TEXT("Deformed floor remains queryable"),Tongue->SurfacePoint(Start,Floor))) return;
        const float Depth=Tongue->IndentationAt(Start);
        TestTrue(TEXT("Collision lowers by the full pressure depth"),FMath::Abs(Hit.ImpactPoint.Z-Floor.ImpactPoint.Z-Depth)<.05f);
        const auto* Section=Tongue->Surface->GetProcMeshSection(0);
        if (!Section || !TestTrue(TEXT("Contact identifies a rendered triangle"),Floor.FaceIndex>=0 && Floor.FaceIndex*3+2<Section->ProcIndexBuffer.Num())) return;
        FVector Center=FVector::ZeroVector;
        for(int32 J=0;J<3;++J) Center+=Section->ProcVertexBuffer[Section->ProcIndexBuffer[Floor.FaceIndex*3+J]].Position/3;
        FHitResult Triangle; TestTrue(TEXT("Physics follows the rendered pressure triangle"),Tongue->SurfacePoint(Center,Triangle) && FMath::Abs(Triangle.ImpactPoint.Z-Center.Z)<.05f);
    };
    CheckContact();
    const float BeforeTuning=Tongue->IndentationAt(Start);
    Tongue->PressureSettings.MaxDepth=20; Tongue->PressureSettings.DepthPerKg=1.2f;
    Tongue->PressureSettings.FoodMargin=100; Tongue->PressureSettings.PressSeconds=.3f;
    Tongue->PressureSettings.TrailHoldSeconds=.35f;
    Step(2);
    TestTrue(TEXT("Changing pressure tuning deepens the same physical surface"),Tongue->IndentationAt(Start)>BeforeTuning*1.5f);
    CheckContact();
    const FVector OldPlace=Food->GetActorLocation();
    const float HeldDepth=Tongue->IndentationAt(OldPlace);
    bool PressureMask=false,RimMask=false;
    for (const auto& Vertex:Tongue->Surface->GetProcMeshSection(0)->ProcVertexBuffer)
    { PressureMask|=Vertex.Color.G>0; RimMask|=Vertex.Color.B>0; }
    TestTrue(TEXT("The shared pressure field supplies depth and rim material masks"),PressureMask && RimMask);
    Food->Body->SetEnableGravity(false); Food->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Food->SetActorLocation(OldPlace+FVector(0,0,350),false,nullptr,ETeleportType::TeleportPhysics); Step(.2f);
    TestEqual(TEXT("Lifted food stops loading the floor"),FoodLoad(),0.f);
    TestTrue(TEXT("Trail hold keeps the vacated physical imprint before recovery"),FMath::Abs(Tongue->IndentationAt(OldPlace)-HeldDepth)<.05f);
    TestTrue(TEXT("Vacated dent recovers gradually"),Tongue->IndentationAt(OldPlace)>0 && Tongue->IndentationAt(OldPlace)<Tongue->PressureSettings.MaxDepth);
    CheckContact();
    // Recovery must use elapsed time even with very slow frames (4 Hz).
    for (int32 I=0;I<8;++I) { ++GFrameCounter; Mouth.World->Tick(LEVELTICK_All,.25f); }
    TestTrue(TEXT("Surface recovers after lifting even at 4 Hz"),Tongue->IndentationAt(OldPlace)<.1f);
    CheckContact();
    Food->SetActorLocation(Start,false,nullptr,ETeleportType::TeleportPhysics); Food->Body->SetEnableGravity(true); Step(2);
    TestTrue(TEXT("Putting food down restores its load"),FoodLoad()>0);
    Food->Dispose(); Step(.2f); TestEqual(TEXT("Disposed item has no load"),FoodLoad(),0.f);
    Tongue->ResetPressure(); TestTrue(TEXT("Reset clears the stored indentation"),Tongue->IndentationAt(Start)<.001f && Tongue->PressureLoads().IsEmpty());
    // Input bounds guard against a malformed designer profile creating an unstable floor.
    FMCTonguePressureSettings S; S.MaxDepth=10000; S.MaxSources=500; S.PressSeconds=0; S.DepthPerKg=std::numeric_limits<float>::quiet_NaN(); S.Sanitize();
    TestTrue(TEXT("Pressure budget and response remain bounded"),S.MaxDepth<=35 && S.MaxSources<=32 && S.PressSeconds>=.06f && FMath::IsFinite(S.DepthPerKg));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCTongueDesignerTest,"MessControl.Tongue.DesignerPresets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCTongueDesignerTest::RunTest(const FString&)
{
    FTestMouth Mouth; Mouth.Mode->SetActorTickEnabled(false);
    auto* Tongue=Mouth.World->SpawnActor<AMCTongue>();
    Tongue->Profile=NewObject<UMCTongueProfile>(); Tongue->Profile->Pressure.MaxDepth=11;
    auto* Preset=NewObject<UMCTonguePressurePreset>(); Preset->Settings.MaxDepth=17;
    Preset->Settings.FalloffPower=2.3f; Preset->Settings.PlayerDepthScale=.7f;
    Preset->Settings.TrailHoldSeconds=.4f; Preset->Settings.LandingSeconds=.5f;
    Preset->SurfaceMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
    const auto Motion=Tongue->Motion; const float Idle=Tongue->Settings.IdleHeight;
    Tongue->ApplyPressurePreset(Preset);
    TestEqual(TEXT("Preset supplies physical depth"),Tongue->PressureSettings.MaxDepth,17.f);
    TestEqual(TEXT("Preset supplies footprint shape"),Tongue->PressureSettings.FalloffPower,2.3f);
    TestEqual(TEXT("Preset changes the presentation material"),Tongue->Surface->GetMaterial(0),Preset->SurfaceMaterial.Get());
    TestTrue(TEXT("Pressure presets preserve independent event motion"),Tongue->Motion.Serial==Motion.Serial && Tongue->Settings.IdleHeight==Idle);
    Preset->Settings.MaxDepth=14; Tongue->ApplyPressurePreset(Preset);
    TestEqual(TEXT("The same preset can be reapplied after designer edits"),Tongue->PressureSettings.MaxDepth,14.f);
    Tongue->Profile->DefaultPressurePreset=Preset; Tongue->ReloadPressureProfile();
    TestEqual(TEXT("Main profile respects its default preset"),Tongue->PressureSettings.MaxDepth,14.f);
    Tongue->Profile->DefaultPressurePreset=nullptr; Tongue->ReloadPressureProfile();
    TestEqual(TEXT("Inline settings are preserved and can be restored"),Tongue->PressureSettings.MaxDepth,11.f);
    TestNull(TEXT("Restoring inline settings clears the temporary preset"),Tongue->ActivePressurePreset.Get());
    FMCTonguePressureSettings S; S.FalloffPower=-20; S.TrailHoldSeconds=100; S.LandingSeconds=0; S.PlayerDepthScale=std::numeric_limits<float>::quiet_NaN(); S.Sanitize();
    TestTrue(TEXT("Designer controls remain finite and bounded"),S.FalloffPower>=2 && S.TrailHoldSeconds<=1 && S.LandingSeconds>=.08f && FMath::IsFinite(S.PlayerDepthScale));
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
    FHitResult Pressed; Tongue->SurfacePoint(Hero->GetActorLocation(),Pressed);
    TestTrue(TEXT("Player stands inside its physical dent"),Hit.ImpactPoint.Z-Pressed.ImpactPoint.Z>.5f && Hero->GetCharacterMovement()->IsMovingOnGround());
    const float Sole=Hero->GetActorLocation().Z-Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    TestTrue(TEXT("Capsule stays on the deformed floor"),Sole-Pressed.ImpactPoint.Z>=-.5f && Sole-Pressed.ImpactPoint.Z<4);
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodArtBoundsTest,"MessControl.Grip.ArtistMeshContacts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodArtBoundsTest::RunTest(const FString&)
{
    FTestMouth Mouth; Mouth.Mode->SetActorTickEnabled(false); Mouth.State->Phase=EMCShiftPhase::Working;
    const auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
    if (!TestNotNull(TEXT("Current artist menu"),Table)) return false;
    for (const auto& Entry:Table->GetRowMap()) for (bool Fragment:{false,true})
    {
        const auto& Menu=*reinterpret_cast<const FMCFoodRow*>(Entry.Value);
        const auto& Choices=Fragment?Menu.FragmentMeshes:Menu.WholeMeshes;
        int32 Valid=0;
        for (const auto& Choice:Choices)
        {
        if (Choice.IsNull()) continue;
        const FString Path=Choice.ToSoftObjectPath().ToString(),Name=Choice.GetAssetName();
        auto* Mesh=ReadyCareTestMesh(Path);
        if (!TestNotNull(*Name,Mesh)) continue;
        ++Valid;
        const FTransform T(FVector(0,0,100));
        auto* Food=Mouth.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),T);
        FMCFoodRow Row=Menu; Row.WholeMeshes.Reset(); Row.Mass=12; Row.SpoilSeconds=300;
        Row.WholeMeshes.Add(Mesh); Row.FragmentMeshes=Row.WholeMeshes;
        FRandomStream Random(1); Food->ConfigureItem(*Name,Row,Random,Fragment); Food->FinishSpawning(T);
        Food->Body->SetEnableGravity(false); Mouth.Step(.1f);
        TestTrue(*(Name+TEXT(" visible and physical centres agree")),Food->Visual->Bounds.Origin.Equals(Food->Body->Bounds.Origin,.1));
        const FVector GameplayScale=Food->FoodData.Kind==EMCFoodKind::Spicy?(Food->bFragment?Food->FoodData.FragmentScale:Food->FoodData.Scale):Food->Visual->GetRelativeScale3D();
        TestTrue(*(Name+TEXT(" collider fits its configured variant independently of cosmetic pepper pulses")),Food->Body->GetUnscaledBoxExtent().Equals((Mesh->GetBounds().BoxExtent*GameplayScale).ComponentMax(FVector(3)),.1));
        FHitResult Hit;
        const FVector Origin=Food->Visual->Bounds.Origin+FVector(-Food->Visual->Bounds.BoxExtent.X-100,0,0);
        const bool Found=Food->FindGripSurface(Origin,Hit);
        if (!Found) AddInfo(FString::Printf(TEXT("%s query=%d physics=%d origin=%s visual=%s queryOrigin=%s extent=%s"),*Name,
            Food->GripSurface->IsRegistered(),Food->GripSurface->IsPhysicsStateCreated(),*Origin.ToCompactString(),
            *Food->Visual->Bounds.Origin.ToCompactString(),*Food->GripSurface->Bounds.Origin.ToCompactString(),*Food->Visual->Bounds.BoxExtent.ToCompactString()));
        TestTrue(*(Name+TEXT(" imported surface can be gripped")),Found);
        Food->Destroy();
        }
        TestTrue(*FString::Printf(TEXT("%s has usable %s meshes"),*Entry.Key.ToString(),Fragment?TEXT("fragment"):TEXT("whole")),Valid>0);
    }
    return true;
}

#include "MCBrushContactComponent.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCareVolumeTest,"MessControl.Care.LocalToothCleaning",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCareVolumeTest::RunTest(const FString&)
{
    FTestMouth Mouth; Mouth.Mode->SetActorTickEnabled(false); Mouth.State->Phase=EMCShiftPhase::Working;
    const FTransform T(FVector(0,0,150));
    auto* Tooth=Mouth.World->SpawnActorDeferred<AMCArenaTooth>(AMCArenaTooth::StaticClass(),T);
    Tooth->SetAppearance(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")),FVector(2));
    Tooth->FinishSpawning(T); Tooth->SetCoffee(1);
    auto* H=Mouth.Worker(); H->SetActorLocation(FVector(-140,30,125)); H->SetActorRotation(FRotator::ZeroRotator); H->bBrushing=true;
    for (int32 I=0;I<4;++I) H->AdvanceCare(.1f);
    int32 Front=0,Back=0;
    for (int32 Y=0;Y<16;++Y) for (int32 Z=0;Z<16;++Z)
    { Front+=255-Tooth->GrimeMask[FMCSurfaceWipe::Index(0,Y,Z)]; Back+=255-Tooth->GrimeMask[FMCSurfaceWipe::Index(15,Y,Z)]; }
    TestTrue(TEXT("Brush leaves a local clean trail"),Front>500);
    TestEqual(TEXT("Opposite face stays dirty"),Back,0);
    TestTrue(TEXT("Partial stroke leaves other visible dirt"),Tooth->Status->State.CoffeeLeft>0);
    const auto Before=Tooth->GrimeMask;
    H->SetActorRotation(FRotator(0,180,0));
    TestFalse(TEXT("Retained stain cannot be cleaned behind the player's back"),Tooth->BrushGrime(H,.1f));
    TestFalse(TEXT("Headless contact also requires facing the stain"),H->BrushContact->IsTouchingSurface());
    TestTrue(TEXT("Turning away preserves the mask"),Before==Tooth->GrimeMask);
    H->SetActorRotation(FRotator::ZeroRotator);
    H->bBrushing=false; TestFalse(TEXT("Wrong tool cannot erase the mask"),Tooth->BrushGrime(H,.1f));
    H->bBrushing=true; H->SetActorLocation(FVector(-800,0,150));
    TestFalse(TEXT("Remote brush cannot erase the mask"),Tooth->BrushGrime(H,.1f));
    TestTrue(TEXT("Rejected strokes keep the persistent mask"),Before==Tooth->GrimeMask);
    auto* B=Mouth.Worker(); B->SetActorLocation(FVector(175,0,150)); B->SetActorRotation(FRotator(0,180,0)); B->bBrushing=true;
    TestFalse(TEXT("Clean opposite face cannot advance cleaning"),Tooth->BrushGrime(B,.1f));
    TestTrue(TEXT("Rejected clean-face contact preserves the first trail"),Before==Tooth->GrimeMask);
    Tooth->SetCoffee(1);
    TestTrue(TEXT("New spill restores every mask cell"),!Tooth->GrimeMask.ContainsByPredicate([](uint8 V){return V!=255;}));
    H->SetActorLocation(FVector(-345,30,125));
    TestFalse(TEXT("Assisted brushing does not expand ordinary interaction reach"),H->CanContact(Tooth));
    TestFalse(TEXT("Floating hand does not stretch to a distant stain"),Tooth->BrushGrime(H,.1f));
    H->SetActorLocation(FVector(-170,30,125));
    TestTrue(TEXT("Brush can reach the stain without pressing into the crown"),Tooth->BrushGrime(H,.1f));
    const float HandTravel=H->BrushContact->MaxHandTravel;
    H->BrushContact->MaxHandTravel=1.f;
    TestFalse(TEXT("Floating hand respects its independent short travel limit"),Tooth->BrushGrime(H,.1f));
    H->BrushContact->MaxHandTravel=HandTravel;
    // A nominal wrist goal inside surrounding geometry is not a reachable
    // contact. Reject it instead of presenting forever without wiping.
    auto* Obstacle=Mouth.World->SpawnActor<AActor>();
    auto* Wall=NewObject<UBoxComponent>(Obstacle); Obstacle->SetRootComponent(Wall);
    Wall->SetBoxExtent(FVector(220)); Wall->SetCollisionProfileName(TEXT("BlockAll")); Wall->RegisterComponent();
    Obstacle->SetActorLocation(FVector(-120,0,150));
    const auto BlockedMask=Tooth->GrimeMask;
    TestFalse(TEXT("Brush rejects every blocked hand orientation"),Tooth->BrushGrime(H,.1f));
    TestTrue(TEXT("Blocked physical contact preserves the cleaning mask"),BlockedMask==Tooth->GrimeMask);
    Obstacle->Destroy();
    // A volume stroke must stay local across atlas slice boundaries.
    TArray<uint8> Mask; FMCSurfaceWipe::Reset(Mask);
    TestTrue(TEXT("Stroke crosses a slice boundary"),FMCSurfaceWipe::Stroke(Mask,FVector(.2,.3,.19),FVector(.2,.3,.22),FVector(200),24,.1f));
    TestEqual(TEXT("Unrelated atlas tile stays dirty"),Mask[FMCSurfaceWipe::Index(14,14,15)],uint8(255));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCExposedCoatingTest,"MessControl.Care.ExposedToothCoating",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCExposedCoatingTest::RunTest(const FString&)
{
    FTestMouth M; M.Mode->SetActorTickEnabled(false); M.State->Phase=EMCShiftPhase::Working;
    const FTransform FloorTransform(FQuat::Identity,FVector(0,0,140),FVector(8));
    auto* Tongue=M.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FloorTransform);
    Tongue->SourceMesh=ReadyCareTestMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
    Tongue->Settings.IdleHeight=0; Tongue->FinishSpawning(FloorTransform);
    Tongue->SetActorTickEnabled(false);
    FVector Rest;
    TestTrue(TEXT("Rest support reads the saved tongue geometry"),Tongue->RestSurfacePoint(FVector(-100,0,0),Rest));
    TestTrue(TEXT("Rest support follows the tongue transform"),FMath::IsNearlyEqual(Rest.Z,140.,.1));
    const FTransform ToothTransform(FVector(0,0,150));
    auto* Tooth=M.World->SpawnActorDeferred<AMCArenaTooth>(AMCArenaTooth::StaticClass(),ToothTransform);
    Tooth->SetAppearance(ReadyCareTestMesh(TEXT("/Engine/BasicShapes/Cube.Cube")),FVector(2));
    Tooth->FinishSpawning(ToothTransform); Tooth->SetCoffee(1);
    auto* H=M.Worker(); H->SetActorLocation(FVector(-155,0,205)); H->SetActorRotation(FRotator::ZeroRotator); H->bBrushing=true;
    H->GetCharacterMovement()->bRunPhysicsWithNoController=true;
    H->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    float Lowest=MAX_flt; int32 Contacts=0;
    for(int32 I=0;I<240 && Tooth->Status->NeedsCare(true);++I) {
        FVector Point,Normal;
        if(Tooth->FindDirtyContact(H,Point,Normal)) { Lowest=FMath::Min(Lowest,float(Point.Z)); ++Contacts; }
        H->AdvanceCare(.1f); M.Step(.1f);
    }
    TestTrue(TEXT("Exposed coating retains brushable dirt"),Contacts>0);
    TestTrue(TEXT("Buried lower enamel cannot become a dirty target"),Lowest>=162);
    AddInfo(FString::Printf(TEXT("exposed coating contacts=%d left=%.4f low=%.1f health=%.1f"),Contacts,Tooth->RemainingGrime(),Lowest,H->Status->State.Health));
    TestFalse(TEXT("Exposed coating can be completely cleaned"),Tooth->Status->NeedsCare(true));
    TestTrue(TEXT("No hidden samples keep the cleaning task open"),Tooth->RemainingGrime()<.025f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPhysicalObjectGripTest,"MessControl.Grip.SizeBasedOverheadPhysicalHands",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPhysicalObjectGripTest::RunTest(const FString&)
{
    FTestMouth M; M.Mode->SetActorTickEnabled(false); M.State->Phase=EMCShiftPhase::Working;
    auto* Floor=M.World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(1500,1500,10)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-10));
    const FTransform T(FVector(0,0,26)); auto* Food=M.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),T);
    FMCFoodRow Row; Row.Mass=6; Row.SpoilSeconds=300;
    Row.FragmentMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))));
    FRandomStream R(1); Food->ConfigureItem(TEXT("PhysicalLift"),Row,R,true); Food->FinishSpawning(T);
    auto* H=M.Worker(); H->SetActorLocation(FVector(-68,0,61));
    H->GetCharacterMovement()->bRunPhysicsWithNoController=true; H->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    M.Step(.5f);
    Food->Settings.Mass=24; Food->Body->SetMassOverrideInKg(NAME_None,24);
    TestTrue(TEXT("Small heavy item still selects lifting"),H->Grip->CanCarry(Food));
    Food->SetActorRotation(FRotator(25,43,65),ETeleportType::TeleportPhysics);
    TestTrue(TEXT("Rotating the same small item retains the lift selection"),H->Grip->CanCarry(Food));
    Food->SetActorRotation(FRotator::ZeroRotator,ETeleportType::TeleportPhysics);
    Food->SetActorScale3D(FVector(3));
    TestFalse(TEXT("The same mesh enlarged on the map selects drag"),H->Grip->CanCarry(Food));
    Food->Settings.Mass=.5f;
    TestFalse(TEXT("Making a large object light cannot select lifting"),H->Grip->CanCarry(Food));
    Food->SetActorScale3D(FVector(1)); Food->Settings.Mass=24;
    H->bHandling=true;
    if(!TestTrue(TEXT("Small heavy item acquires a surface grip"),Food->TryGrab(H))) return false;
    TestTrue(TEXT("Reaching does not lift before contact"),H->Grip->DriveForce(Food).IsNearlyZero() && Food->Phase!=EMCFoodPhase::Carried);
    for(int32 I=0;I<90 && Food->Phase!=EMCFoodPhase::Carried;++I) M.Step(1.f/60);
    TestEqual(TEXT("Physical contact accepts the load"),Food->Phase,EMCFoodPhase::Carried);
    TestTrue(TEXT("Lift begins at the existing object position"),H->Grip->LiftAlpha(Food)<.05f && FVector::Dist(H->Grip->CarryLocation(Food),Food->GetActorLocation())<8);
    M.Step(2);
    const bool Left=H->Grip->Frame.Pose==EMCGripPose::LeftHand;
    auto* Mesh=H->GetMesh(); auto* Hand=Mesh->GetBodyInstance(H->RigBone(Left?TEXT("hand_l"):TEXT("hand_r")));
    TestTrue(TEXT("Held hand is fully simulated and visible"),H->ToothPhysics->IsPhysicalObjectGrip(Left) && Hand && Hand->IsInstanceSimulatingPhysics() && Hand->PhysicsBlendWeight>.99f);
    TestTrue(*FString::Printf(TEXT("Held physical palm stays on the surface: %.2f cm"),H->Grip->ContactError()),H->Grip->ContactError()<8);
    const float Crown=Mesh->GetSkeletalMeshAsset()->GetImportedBounds().GetBox().TransformBy(Mesh->GetComponentTransform()).Max.Z+3;
    TestTrue(*FString::Printf(TEXT("Object bottom clears the actual character mesh: bottom %.1f, crown %.1f"),Food->Body->Bounds.GetBox().Min.Z,Crown),
        Food->Body->Bounds.GetBox().Min.Z>Crown);
    auto* Motors=H->FindComponentByClass<UPhysicsControlComponent>();
    TestTrue(TEXT("A finite palm motor drives contact"),Motors && Motors->GetControlEnabled(Motors->GetControlNamesInSet(TEXT("GripPalms"))[Left?0:1]));
    for(const auto Mode:{EMCActiveRagdollMode::Soft,EMCActiveRagdollMode::Firm,EMCActiveRagdollMode::Off}) {
        H->ToothPhysics->SetActiveRagdollMode(Mode); M.Step(.3f);
        TestTrue(TEXT("Changing free-locomotion muscles preserves the overhead contact"),
            H->Grip->Holds(Food) && H->Grip->ContactError()<8 && H->ToothPhysics->GetBodyState()==EMCBodyState::Standing);
    }
    Food->Release(H); H->bHandling=false; M.Step(.9f);
    TestEqual(TEXT("Dropping an overhead load does not knock down its carrier"),H->ToothPhysics->GetBodyState(),EMCBodyState::Standing);
    const auto* Joint=Mesh->FindConstraintInstance(H->RigBone(Left?TEXT("arm_l"):TEXT("arm_r")));
    TestTrue(TEXT("Release restores original joint limits"),Joint && Joint->GetLinearXMotion()==LCM_Locked && Joint->GetAngularSwing1Motion()==ACM_Limited);
    TestTrue(TEXT("Release disables palm servos and restores locomotion root"),!Motors->GetControlEnabled(Motors->GetControlNamesInSet(TEXT("GripPalms"))[Left?0:1]) && Mesh->IsSimulatingPhysics(H->RigBone(TEXT("body"))));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCThroatCameraTest,"MessControl.Throat.CameraWaitsForSpit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCThroatCameraTest::RunTest(const FString&)
{
    FTestMouth Mouth; Mouth.Mode->SetActorTickEnabled(false); Mouth.State->Phase=EMCShiftPhase::Working;
    auto* PC=Mouth.World->SpawnActor<APlayerController>(); PC->SetAsLocalPlayerController();
    auto* Hero=Mouth.Worker(); PC->Possess(Hero); Hero->SetActorLocation(FVector(-200,0,100));
    Hero->UpdateMouthCamera(.1f); Hero->CameraBoom->TickComponent(.1f,LEVELTICK_All,nullptr);
    const FVector Eye=Hero->Camera->GetComponentLocation(),Focus=Hero->MouthCameraFocus;
    const FRotator Rotation=Hero->Camera->GetComponentRotation();
    auto* Throat=Mouth.World->SpawnActor<AMCThroat>();
    Hero->SetThroatCapture(Throat);
    Hero->SetActorLocation(FVector(1400,0,-450));
    Hero->UpdateMouthCamera(.1f); Hero->CameraBoom->TickComponent(.1f,LEVELTICK_All,nullptr);
    TestTrue(TEXT("Camera stays in the mouth while the player travels down the throat"),Hero->Camera->GetComponentLocation().Equals(Eye,.1f));
    TestTrue(TEXT("Camera aim and focus also stay fixed"),Hero->Camera->GetComponentRotation().Equals(Rotation,.1f) && Hero->MouthCameraFocus==Focus);
    auto* Water=Mouth.World->SpawnActor<AMCCoffeeFlood>(); Water->bActive=true; Water->Seconds=1000; Water->WaterSettings.HoldSeconds=600; Water->StartedAt=Mouth.World->GetTimeSeconds()-100;
    Hero->GetCharacterMovement()->TickComponent(.1f,LEVELTICK_All,nullptr); Hero->Tick(.1f); Water->Tick(.1f);
    TestTrue(TEXT("Captured player stays alive with movement suspended even in coffee"),Hero->Status->IsAlive() && Hero->GetCharacterMovement()->MovementMode==MOVE_None && !Hero->bInCoffee);
    const FVector Exit(-600,0,150),Impulse(-570,0,420);
    Hero->ClientThroatExit(Exit,Impulse);
    Hero->UpdateMouthCamera(.01f); Hero->CameraBoom->TickComponent(.01f,LEVELTICK_All,nullptr);
    TestTrue(TEXT("Ejection releases capture and camera"),!Hero->SwallowedBy && !Hero->bMouthCameraHeld && Hero->CanWork());
    TestTrue(TEXT("Camera resumes with a smooth first step"),FVector::Dist(Hero->MouthCameraEye,Eye)>0 && FVector::Dist(Hero->MouthCameraEye,Eye)<70);
    // A repeated null replication must not cancel the reliable launch.
    Hero->GetCharacterMovement()->Velocity=Impulse; Hero->SetThroatCapture(nullptr);
    TestTrue(TEXT("A repeated release preserves the ejection velocity"),Hero->GetVelocity().Equals(Impulse,.1f));
    Mouth.Step(.7f);
    Hero->ToothPhysics->ApplyHit(FVector(0,0,600),Hero->GetActorLocation());
    TestEqual(TEXT("Second capture starts with a knocked-down player"),Hero->ToothPhysics->GetBodyState(),EMCBodyState::Ragdoll);
    Hero->SetThroatCapture(Throat);
    TestTrue(TEXT("A knocked-down player also becomes kinematic in the gulp"),Hero->ToothPhysics->CanAct() && !Hero->GetMesh()->IsSimulatingPhysics(Hero->RigBone(TEXT("body"))));
    Hero->ClientThroatExit(Exit,Impulse);
    TestTrue(TEXT("Ejected player restores collision"),Hero->GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::QueryAndPhysics);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCJumpHeightTest,"MessControl.Locomotion.RaisedJumpHeight",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCJumpHeightTest::RunTest(const FString&)
{
    FTestMouth Mouth; auto* Hero=Mouth.Worker();
    UClass* Blueprint=LoadClass<AMCToothCharacter>(nullptr,TEXT("/Game/Blueprints/BP_ToothCharacter.BP_ToothCharacter_C"));
    if(!TestNotNull(TEXT("Authored player Blueprint exists"),Blueprint)) return false;
    auto* Authored=Mouth.World->SpawnActor<AMCToothCharacter>(Blueprint,FVector(5000,0,100),FRotator::ZeroRotator);
    for(auto* H:{Hero,Authored}) {
        auto* Move=H->GetCharacterMovement();
        const float OldHeight=500.f*500.f/(2*FMath::Abs(Move->GetGravityZ()));
        TestTrue(TEXT("Native and Blueprint players jump 28 percent higher"),FMath::IsNearlyEqual(Move->GetMaxJumpHeight()/OldHeight,1.28f,.001f));
        Move->SetMovementMode(MOVE_Walking); H->Jump();
        TestTrue(TEXT("Actual jump accepts the new takeoff velocity"),Move->DoJump(false,1.f/60) && FMath::IsNearlyEqual(Move->Velocity.Z,565.685f,.01f));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHeavyFoodDragTest,"MessControl.Grip.HeavyArtistFoodMoves",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCHeavyFoodDragTest::RunTest(const FString&)
{
    const auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
    if (!TestNotNull(TEXT("Current artist menu"),Table)) return false;
    for (const TCHAR* Name:{TEXT("Broccoli"),TEXT("Carrot")})
    {
        FTestMouth Mouth; Mouth.Mode->SetActorTickEnabled(false); Mouth.State->Phase=EMCShiftPhase::Working;
        auto* Floor=Mouth.World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Floor);
        Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(1500,1500,10)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-10));
        const auto* Menu=Table->FindRow<FMCFoodRow>(Name,TEXT("Heavy grip"));
        if (!TestNotNull(Name,Menu) || Menu->WholeMeshes.IsEmpty()) continue;
        const FString Path=Menu->WholeMeshes[0].ToSoftObjectPath().ToString();
        auto* Mesh=ReadyCareTestMesh(Path);
        if (!TestNotNull(Name,Mesh)) continue;
        const FTransform T(FVector(0,0,80));
        auto* Food=Mouth.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),T);
        FMCFoodRow Row=*Menu; Row.WholeMeshes.Reset(); Row.Mass=12; Row.SpoilSeconds=300; Row.WholeMeshes.Add(Mesh);
        FRandomStream Random(1); Food->ConfigureItem(Name,Row,Random); Food->FinishSpawning(T);
        auto* H=Mouth.Worker(); H->SetActorLocation(FVector(-Food->Body->GetUnscaledBoxExtent().X-40,0,61)); H->SetActorRotation(FRotator::ZeroRotator);
        H->GetCharacterMovement()->bRunPhysicsWithNoController=true; H->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        Mouth.Step(1);
        // Walk up to the food as a player would; the capsule determines the closest legal approach.
        for (int32 I=0;I<30;++I) { H->AddMovementInput(FVector(1,0,0),.25f); Mouth.Step(1.f/60); }
        H->GetCharacterMovement()->StopMovementImmediately(); H->bHandling=true;
        if (!TestTrue(*FString::Printf(TEXT("Grip real 12 kg %s: %s"),Name,*H->Grip->DebugFailure),Food->TryGrab(H))) continue;
        Mouth.Step(.8f); const FVector Start=Food->GetActorLocation(); float Contact=0;
        for (int32 I=0;I<180;++I) { H->AddMovementInput(FVector(-1,0,0),.6f); Mouth.Step(1.f/60); if (H->Grip->IsReady()) Contact+=1.f/60; }
        const float Travel=Start.X-Food->GetActorLocation().X;
        TestTrue(*FString::Printf(TEXT("%s moves under sustained effort: %.1f cm"),Name,Travel),Travel>25 && Travel<700);
        TestTrue(*FString::Printf(TEXT("%s retains physical contact: %.2fs"),Name,Contact),Contact>2.3f && H->HeldFood==Food);
        TestTrue(TEXT("Heavy food remains a simulated body"),Food->Body->IsSimulatingPhysics() && Food->Phase==EMCFoodPhase::Free);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDualHandTest,"MessControl.Grip.TwoSmallObjects",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDualHandTest::RunTest(const FString&)
{
    for (float FPS:{10.f,30.f,60.f})
    {
        FTestMouth M; M.Mode->SetActorTickEnabled(false); M.State->Phase=EMCShiftPhase::Working;
        auto* Floor=M.World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Floor);
        Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(1500,1500,10)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-10));
        auto Spawn=[&](float Y)
        {
            const FTransform T(FVector(0,Y,26)); auto* F=M.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),T);
            FMCFoodRow R; R.Mass=6; R.SpoilSeconds=300; R.FragmentMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))));
            FRandomStream Random(4); F->ConfigureItem(TEXT("SmallFixture"),R,Random,true); F->FinishSpawning(T); return F;
        };
        auto* A=Spawn(-32); auto* B=Spawn(32); auto* H=M.Worker(); H->SetActorLocation(FVector(-64,0,61));
        auto* Move=H->GetCharacterMovement(); Move->bRunPhysicsWithNoController=true; Move->SetMovementMode(MOVE_Walking);
        M.Step(.5); H->ServerSetPrimary(true);
        for (int32 N=0;N<int32(FPS*3);++N) { ++GFrameCounter; M.World->Tick(LEVELTICK_All,1/FPS); }
        TestTrue(*FString::Printf(TEXT("One held button acquires both small objects at %.0f FPS: %s"),FPS,*H->Grip->DebugFailure),H->Grip->Holds(A) && H->Grip->Holds(B));
        TestTrue(TEXT("Each object has its own hand"),H->Grip->Frame.Pose!=H->Grip->Secondary.Pose && !H->Grip->HasFreeHand());
        TestTrue(TEXT("Both remain physical and lifted"),A->Body->IsSimulatingPhysics() && B->Body->IsSimulatingPhysics() && A->GetActorLocation().Z>40 && B->GetActorLocation().Z>40);
        TestTrue(*FString::Printf(TEXT("Both palms follow current physics contacts at %.0f FPS (error %.2f cm)"),FPS,H->Grip->ContactError()),H->Grip->ContactError()<8);
        auto* First=H->Grip->Frame.Food.Get(); auto* Second=H->Grip->Secondary.Food.Get();
        if (First && Second)
        {
            First->Dispose(); M.Step(.2f);
            TestTrue(TEXT("Disposing one item preserves the other hand and holder"),H->HeldFood==Second && H->Grip->Holds(Second) && Second->Holders.Contains(H));
            H->ToothPhysics->ApplyHit(FVector(0,0,450),H->GetActorLocation()); M.Step(.1f);
            TestTrue(TEXT("Knockdown clears all hand state"),!H->HeldFood && !H->Grip->Frame.Food && !H->Grip->Secondary.Food && Second->Holders.IsEmpty());
        }
        H->ServerSetPrimary(false);
        TestFalse(TEXT("Release clears secondary"),bool(H->Grip->Secondary.Food));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlayerGripTest,"MessControl.Grip.PlayerPullAndThrow",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlayerGripTest::RunTest(const FString&)
{
    FTestMouth M; M.Mode->SetActorTickEnabled(false); M.State->Phase=EMCShiftPhase::Working;
    auto* Floor=M.World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(1000,1000,10)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-10));
    auto* H=M.Worker(); auto* P=M.Worker(); H->SetActorLocation(FVector(-90,0,61)); P->SetActorLocation(FVector(0,0,61));
    for (auto* C:{H,P}) { C->GetCharacterMovement()->bRunPhysicsWithNoController=true; C->GetCharacterMovement()->SetMovementMode(MOVE_Walking); }
    M.Step(1); H->bHandling=true;
    TestFalse(TEXT("Cannot grab self"),H->Grip->BeginPlayerGrip(H));
    TestTrue(TEXT("Nearby player is grabbable"),H->Grip->BeginPlayerGrip(P));
    M.Step(.5); const FVector Start=P->GetActorLocation();
    for (int32 N=0;N<90;++N) { H->AddMovementInput(FVector(-1,0,0),.4f); M.Step(1.f/60); }
    TestTrue(TEXT("Pulling moves the other player with bounded force"),FVector::Dist2D(Start,P->GetActorLocation())>10 && !P->GetActorLocation().ContainsNaN());
    H->Grip->ThrowPlayer(); M.Step(.1f);
    TestTrue(TEXT("Throw releases grip and activates ragdoll"),!H->Grip->GrabbedPlayer && P->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGripPoseContinuityTest,"MessControl.Animation.GripPoseContinuity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGripPoseContinuityTest::RunTest(const FString&)
{
    for (const int32 Rate:{30,60,120,0}) // 0 alternates a slow frame with three short frames.
    {
    FTestMouth M; M.Mode->SetActorTickEnabled(false); M.State->Phase=EMCShiftPhase::Working;
    auto* Floor=M.World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(1500,1500,10)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-10));
    auto Spawn=[&](float Y)
    {
        const FTransform T(FVector(0,Y,26)); auto* F=M.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),T);
        FMCFoodRow Row; Row.Mass=6; Row.SpoilSeconds=300; Row.FragmentMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))));
        FRandomStream R(4); F->ConfigureItem(TEXT("PoseFixture"),Row,R,true); F->FinishSpawning(T); return F;
    };
    auto* A=Spawn(-32); auto* B=Spawn(32); auto* H=M.Worker(); H->SetActorLocation(FVector(-64,0,61));
    H->GetCharacterMovement()->bRunPhysicsWithNoController=true; H->GetCharacterMovement()->SetMovementMode(MOVE_Walking); M.Step(1);
    auto* Recorder=NewObject<UMCMotionRecorder>(H); Recorder->RegisterComponent(); Recorder->Start(60,FString::Printf(TEXT("grip_continuity_%d"),Rate));
    auto Sample=[&](const TCHAR* Stage,FVector Direction=FVector::ZeroVector)
    {
        Recorder->Stage=Stage;
        TArray<FTransform> Previous; float Step=0,Angle=0,HandStep=0;
        for (int32 N=0;N<120;++N)
        {
            if (!Direction.IsNearlyZero()) H->AddMovementInput(Direction,.35f);
            const float Dt=Rate?1.f/Rate:(N%4==0?.05f:1.f/90);
            ++GFrameCounter; M.World->Tick(LEVELTICK_All,Dt); int32 I=0;
            for (const FName Role:{FName("knee_l"),FName("foot_l"),FName("knee_r"),FName("foot_r"),FName("hand_l"),FName("hand_r")})
            {
                const FTransform Bone=H->GetMesh()->GetSocketTransform(H->RigBone(Role)).GetRelativeTransform(H->GetActorTransform());
                if (Previous.IsValidIndex(I))
                {
                    const float Distance=float(FVector::Distance(Bone.GetLocation(),Previous[I].GetLocation()))/(Dt*60);
                    if(I<4) { Step=FMath::Max(Step,Distance); Angle=FMath::Max(Angle,float(FMath::RadiansToDegrees(Bone.GetRotation().AngularDistance(Previous[I].GetRotation())))/(Dt*60)); }
                    else HandStep=FMath::Max(HandStep,Distance);
                    Previous[I]=Bone;
                }
                else Previous.Add(Bone);
                ++I;
            }
        }
        AddInfo(FString::Printf(TEXT("%s (%d FPS): max leg step %.3f cm, rotation %.3f degrees per 1/60 s"),Stage,Rate,Step,Angle));
        TestTrue(*FString::Printf(TEXT("%s (%d FPS): legs stay continuous through grip transitions"),Stage,Rate),Step<4 && Angle<20);
        AddInfo(FString::Printf(TEXT("%s (%d FPS): max hand step %.3f cm/60Hz"),Stage,Rate,HandStep));
        TestTrue(*FString::Printf(TEXT("%s (%d FPS): hands stay continuous through grip transitions"),Stage,Rate),HandStep<10);
    };
    Sample(TEXT("Idle")); H->bHandling=true;
    TestTrue(TEXT("First item acquired"),A->TryGrab(H)); Sample(TEXT("One item"));
    TestTrue(TEXT("Second item acquired"),B->TryGrab(H)); Sample(TEXT("Two items"));
    Sample(TEXT("Walking with two items"),FVector(-1,0,0));
    H->DropFood(); H->bHandling=false; Sample(TEXT("Released"));
    Recorder->Stop();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCMovementTransitionsTest,"MessControl.Animation.SoloMovementTransitions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCMovementTransitionsTest::RunTest(const FString&)
{
    for(const int32 Rate:{30,60,120,0})
    {
        FTestMouth M; M.Mode->SetActorTickEnabled(false); M.State->Phase=EMCShiftPhase::Working;
        auto* Floor=M.World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Floor);
        Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(12000,12000,10)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-10));
        auto* H=M.Worker(); H->SetActorLocation(FVector(0,0,61));
        auto* Move=CastChecked<UMCToothMovementComponent>(H->GetCharacterMovement());
        Move->bRunPhysicsWithNoController=true; Move->SetMovementMode(MOVE_Walking); M.Step(1);
        auto* Recorder=NewObject<UMCMotionRecorder>(H); Recorder->RegisterComponent(); Recorder->Start(90,FString::Printf(TEXT("solo_transitions_%d"),Rate));
        TMap<FName,FTransform> Previous,PreviousWorld; int32 Frame=0; bool Air=false,Ground=false;
        EMCBodyState PreviousState=EMCBodyState::Standing;
        auto Sample=[&](const TCHAR* Stage,float Seconds,FVector Direction=FVector::ZeroVector,float Limit=12.f)
        {
            Recorder->Stage=Stage; float Time=0,MaxStep=0; bool Finite=true,Bounded=true;
            while(Time<Seconds)
            {
                const float Dt=Rate?1.f/Rate:(Frame++%4==0?.05f:1.f/90);
                if(!Direction.IsNearlyZero()) H->AddMovementInput(Direction);
                ++GFrameCounter; M.World->Tick(LEVELTICK_All,Dt); Time+=Dt;
                Air|=Move->IsFalling(); Ground|=Move->IsMovingOnGround();
                Finite&=!H->GetActorLocation().ContainsNaN() && !H->GetVelocity().ContainsNaN();
                const auto State=H->ToothPhysics->GetBodyState();
                for(const FName Role:{FName("body"),FName("hand_l"),FName("hand_r"),FName("foot_l"),FName("foot_r")})
                {
                    const FTransform WorldBone=H->GetMesh()->GetSocketTransform(H->RigBone(Role));
                    const FTransform Bone=WorldBone.GetRelativeTransform(H->GetActorTransform());
                    Finite&=!Bone.ContainsNaN(); Bounded&=Bone.GetLocation().Size()<240;
                    // The capsule is recentered on the body for ragdoll/recovery.
                    // World-space bones must remain continuous across that change.
                    const bool Physical=State!=EMCBodyState::Standing || PreviousState!=EMCBodyState::Standing;
                    if(const auto* Old=(Physical?PreviousWorld:Previous).Find(Role))
                        MaxStep=FMath::Max(MaxStep,float(FVector::Dist((Physical?WorldBone:Bone).GetLocation(),Old->GetLocation()))/(Dt*60));
                    Previous.Add(Role,Bone); PreviousWorld.Add(Role,WorldBone);
                }
                PreviousState=State;
            }
            AddInfo(FString::Printf(TEXT("%s (%d FPS): max step %.3f cm/60Hz, bounded=%d finite=%d"),Stage,Rate,MaxStep,Bounded,Finite));
            TestTrue(*FString::Printf(TEXT("%s (%d FPS): finite bounded continuous limbs"),Stage,Rate),Finite && Bounded && MaxStep<Limit);
        };
        Sample(TEXT("idle"),1,FVector::ZeroVector,1.5f);
        Sample(TEXT("walk"),2,FVector(1,0,0));
        Move->SetSprinting(true); Sample(TEXT("run"),2,FVector(1,0,0));
        Sample(TEXT("turn90"),2,FVector(0,1,0));
        Sample(TEXT("reverse"),2,FVector(0,-1,0));
        Move->SetSprinting(false); Sample(TEXT("stop"),1);
        H->Jump(); Sample(TEXT("jump"),.35f,FVector(1,0,0),12);
        H->StopJumping(); Sample(TEXT("land"),2,FVector::ZeroVector,12);
        TestTrue(TEXT("Jump leaves the ground and lands"),Air && Ground && Move->IsMovingOnGround());
        auto* Patch=M.World->SpawnActor<AMCLocomotionSurface>(); Patch->HalfExtent=FVector(12000,12000,40); Patch->RefreshBounds();
        Patch->Surface=EMCGroundSurface::Sticky; Sample(TEXT("sticky"),2,FVector(1,0,0));
        TestEqual(TEXT("Sticky case exercised"),Move->GroundSurface,EMCGroundSurface::Sticky);
        Patch->Surface=EMCGroundSurface::Slippery; Sample(TEXT("slip"),2,FVector(1,0,0));
        TestEqual(TEXT("Slippery case exercised"),Move->GroundSurface,EMCGroundSurface::Slippery);
        Sample(TEXT("coast"),2); Patch->Surface=EMCGroundSurface::Normal; Sample(TEXT("dry_stop"),1);
        H->Expression->ServerPlayEmote(TEXT("hello2")); Sample(TEXT("emote"),4,FVector::ZeroVector,12);
        Sample(TEXT("emote_release"),2);
        H->ToothPhysics->ApplyHit(FVector(360,0,450),H->GetActorLocation());
        Sample(TEXT("ragdoll_recovery"),9,FVector::ZeroVector,15);
        TestTrue(TEXT("Hit falls and recovers in solo simulation"),H->ToothPhysics->KnockdownCount>0 && H->ToothPhysics->RecoveryCount>0 && H->ToothPhysics->GetBodyState()==EMCBodyState::Standing);
        Sample(TEXT("recovered_idle"),1,FVector::ZeroVector,1.5f);
        Sample(TEXT("walk_after_recovery"),2,FVector(1,0,0));
        Sample(TEXT("stop_before_obstacles"),1);
        auto Obstacle=[&](FVector Offset,FVector Extent,float Pitch=0.f)
        {
            auto* Actor=M.World->SpawnActor<AActor>(); auto* Shape=NewObject<UBoxComponent>(Actor);
            Actor->SetRootComponent(Shape); Shape->SetBoxExtent(Extent); Shape->SetCollisionProfileName(TEXT("BlockAll")); Shape->RegisterComponent();
            const FVector Origin=H->GetActorLocation(); Actor->SetActorLocationAndRotation(FVector(Origin.X,Origin.Y,0)+Offset,FRotator(Pitch,0,0)); return Actor;
        };
        auto* Ramp=Obstacle(FVector(350,0,68),FVector(250,200,10),15);
        Sample(TEXT("ramp_and_edge"),3,FVector(1,0,0)); Ramp->Destroy();
        Sample(TEXT("settle_after_ramp"),1);
        auto* Step=Obstacle(FVector(180,0,12),FVector(85,200,12));
        Sample(TEXT("step_up_down"),1.5f,FVector(1,0,0)); Step->Destroy();
        Sample(TEXT("settle_after_step"),1);
        auto* Wall=Obstacle(FVector(160,0,150),FVector(20,250,150));
        const FVector BeforeWall=H->GetActorLocation(); Sample(TEXT("blocked_by_wall"),2,FVector(1,0,0));
        TestTrue(TEXT("Wall blocks capsule without tunnelling"),H->GetActorLocation().X-BeforeWall.X<160 && H->GetVelocity().Size2D()<10);
        Sample(TEXT("slide_along_wall"),1,FVector(1,1,0).GetSafeNormal()); Wall->Destroy();
        Recorder->Stop();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCArtistClipTest,"MessControl.Animation.Teeth3Layers",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCArtistClipTest::RunTest(const FString&)
{
    auto* Profile=LoadObject<UMCAnimationProfile>(nullptr,TEXT("/Game/Data/DA_ToothAnimation.DA_ToothAnimation"));
    if (!TestNotNull(TEXT("Animation profile"),Profile)) return false;
    for (auto* Clip:{Profile->GrabLeft.Get(),Profile->GrabRight.Get(),Profile->Push.Get(),Profile->Tired.Get()})
        if (TestNotNull(TEXT("Artist work clip assigned"),Clip)) TestTrue(TEXT("Clip contains a playable pose transition"),Clip->GetPlayLength()>.3f && Clip->GetSkeleton()!=nullptr);
    auto* Lib=LoadObject<UMCEmoteLibrary>(nullptr,TEXT("/Game/Data/DA_Emotes.DA_Emotes"));
    for (const FName Id:{FName("dance1"),FName("dance2"),FName("hello2"),FName("highfive2")})
        TestTrue(*FString::Printf(TEXT("Emote %s is playable"),*Id.ToString()),Lib && Lib->Entries.ContainsByPredicate([&](const FMCEmoteEntry& E){return E.Id==Id && E.Animation && E.Animation->GetPlayLength()>1;}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCLocomotionGroundTest,"MessControl.Locomotion.SprintSurfacesAndMomentum",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCLocomotionGroundTest::RunTest(const FString&)
{
    FTestMouth M; M.Mode->SetActorTickEnabled(false); M.State->Phase=EMCShiftPhase::Working;
    auto* Floor=M.World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(10000,10000,10)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-10));
    auto* Hero=M.Worker(); Hero->SetActorLocation(FVector(0,0,61));
    auto* Move=CastChecked<UMCToothMovementComponent>(Hero->GetCharacterMovement());
    Move->bRunPhysicsWithNoController=true; Move->SetMovementMode(MOVE_Walking); M.Step(.5f);
    auto Drive=[&](float Seconds)
    { for (int32 I=0;I<int32(Seconds*60);++I) { Hero->AddMovementInput(FVector::ForwardVector); M.Step(1.f/60); } };
    Drive(1.5f); const float Walk=Hero->GetVelocity().Size2D();
    Move->SetSprinting(true); Drive(1.5f); const float Run=Hero->GetVelocity().Size2D();
    TestTrue(TEXT("Shift produces a separate faster gait"),Run>Walk*1.4f && Hero->AnimationRun>.95f);
    const auto Saved=Move->GetPredictionData_Client_Character()->AllocateNewMove();
    Saved->SetMoveFor(Hero,.016f,FVector(100,0,0),*Move->GetPredictionData_Client_Character());
    Move->SetSprinting(false); Move->UpdateFromCompressedFlags(Saved->GetCompressedFlags());
    TestTrue(TEXT("Sprint survives saved move transport"),Move->WantsToSprint());
    Move->SetSprinting(false); Saved->PrepMoveFor(Hero); TestTrue(TEXT("Sprint survives correction replay"),Move->WantsToSprint());
    auto* Zone=M.World->SpawnActor<AMCLocomotionSurface>(); Zone->HalfExtent=FVector(9000,9000,30); Zone->RefreshBounds();
    Zone->Surface=EMCGroundSurface::Sticky; Drive(1.5f);
    TestEqual(TEXT("Sticky floor detected under the sole"),Move->GroundSurface,EMCGroundSurface::Sticky);
    TestTrue(TEXT("Sticky floor resists sprint and lengthens support"),Hero->GetVelocity().Size2D()<Run*.7f && Hero->AnimationStance>.7f);
    Zone->Surface=EMCGroundSurface::Normal; Drive(1.5f); const FVector DryStart=Hero->GetActorLocation(); M.Step(.8f);
    const float DryStop=FVector::Dist2D(DryStart,Hero->GetActorLocation());
    Zone->Surface=EMCGroundSurface::Slippery; Drive(3.f); const FVector WetStart=Hero->GetActorLocation(); M.Step(.8f);
    const float WetStop=FVector::Dist2D(WetStart,Hero->GetActorLocation());
    TestTrue(TEXT("Slippery floor preserves momentum after release"),WetStop>DryStop*1.5f);
    TestTrue(TEXT("Sliding is not a full-speed walking cycle"),Hero->AnimationSlip>.9f && Hero->AnimationSpeed<.3f);
    Hero->SetActorLocation(FVector(0,0,500)); Move->SetMovementMode(MOVE_Falling); Move->RefreshGroundSurface();
    TestEqual(TEXT("Airborne player does not inherit a patch below"),Move->GroundSurface,EMCGroundSurface::Normal);
    Hero->CancelGameplayInput(); TestFalse(TEXT("Menus cancel held sprint"),Move->WantsToSprint());
    AddInfo(FString::Printf(TEXT("walk %.1f run %.1f dry stop %.1f slippery stop %.1f"),Walk,Run,DryStop,WetStop));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCLocomotionCycleTest,"MessControl.Locomotion.FootCycleContinuity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCLocomotionCycleTest::RunTest(const FString&)
{
    for (float Stance:{.48f,.62f,.76f})
    {
        auto Before=FMCLocomotionCycle::Sample(Stance-.0001f,Stance),After=FMCLocomotionCycle::Sample(Stance+.0001f,Stance);
        TestTrue(TEXT("Toe-off stays continuous for every surface"),FMath::Abs(Before.Sweep-After.Sweep)<.002f && After.Lift<.002f);
        Before=FMCLocomotionCycle::Sample(.9999f,Stance); After=FMCLocomotionCycle::Sample(.0001f,Stance);
        TestTrue(TEXT("Landing does not pop the leg"),FMath::Abs(Before.Sweep-After.Sweep)<.002f && Before.Lift<.002f);
        TestTrue(TEXT("Swing clears the floor"),FMCLocomotionCycle::Sample((1+Stance)*.5f,Stance).Lift>.99f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodEmptyVariantsTest,"MessControl.Grip.EmptyMenuSlots",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodEmptyVariantsTest::RunTest(const FString&)
{
    FTestMouth M; M.Mode->SetActorTickEnabled(false);
    auto* Mesh=ReadyCareTestMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    FMCFoodRow Row; Row.WholeMeshes={TSoftObjectPtr<UStaticMesh>(),TSoftObjectPtr<UStaticMesh>(Mesh),TSoftObjectPtr<UStaticMesh>()};
    auto* Food=M.GripCube(); FRandomStream Random(7);
    for (int32 I=0;I<24;++I)
    { Food->ConfigureItem(TEXT("SparseMenu"),Row,Random); TestEqual(TEXT("Empty artist slots never select an invisible variant"),Food->ItemMesh.Get(),Mesh); }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCWetFootingTest,"MessControl.Locomotion.CleanedPuddleFooting",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCWetFootingTest::RunTest(const FString&)
{
    FTestMouth M; M.Mode->SetActorTickEnabled(false);
    auto* Patch=M.World->SpawnActor<AMCMouthSurface>(FVector(0,0,5),FRotator::ZeroRotator);
    Patch->Status->Initialize(100); Patch->Status->ApplyCoffee(1); Patch->LiquidHalfSize=100; Patch->ResetLiquid();
    TestTrue(TEXT("Wet interior changes footing"),Patch->AffectsFooting(FVector::ZeroVector));
    TestFalse(TEXT("Clear corners of the quad do not affect footing"),Patch->AffectsFooting(FVector(99,99,0)));
    TestFalse(TEXT("Passing above the puddle is not ground contact"),Patch->AffectsFooting(FVector(0,0,80)));
    for (int32 I=0;I<20;++I) FMCCoffeeWipe::Stroke(Patch->WipeMask,FVector2D(.5),FVector2D(.5),.2f,.1f);
    TestFalse(TEXT("A locally wiped path regains footing before the whole stain is clean"),Patch->AffectsFooting(FVector::ZeroVector));
    TestTrue(TEXT("The remaining stain still exists"),!Patch->IsClean() && FMCCoffeeWipe::Remaining(Patch->WipeMask)>.5f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCAuthoredThroatMorphTest,"MessControl.Animation.AuthoredThroatApertureAndVomit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCAuthoredThroatMorphTest::RunTest(const FString&)
{
    FTestMouth M; M.Mode->SetActorTickEnabled(false);
    auto* Throat=M.World->SpawnActor<AMCThroat>(FVector(5000,0,200),FRotator::ZeroRotator);
    auto* Mesh=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/FromBlender2/SK_Exit.SK_Exit"));
    if(!TestNotNull(TEXT("Imported artist throat"),Mesh)) return false;
    TestNotNull(TEXT("Open morph exists"),Mesh->FindMorphTarget(TEXT("Open")));
    TestNotNull(TEXT("vomit morph exists"),Mesh->FindMorphTarget(TEXT("vomit")));
    Throat->AuthoredMouth->SetSkeletalMesh(Mesh); Throat->Tick(.01f);
    TestTrue(TEXT("Artist's closing target is active at rest"),Throat->AuthoredMouth->GetMorphTarget(TEXT("Open"))>.95f);
    Throat->ThroatPhase=EMCThroatPhase::Swallowing; Throat->PhaseStartedAt=M.World->GetTimeSeconds()-.4;
    Throat->Tick(.01f);
    TestTrue(TEXT("Swallow releases the closing target to reveal the aperture"),Throat->AuthoredMouth->GetMorphTarget(TEXT("Open"))<.03f);
    Throat->ThroatPhase=EMCThroatPhase::Spasm; Throat->PhaseStartedAt=M.World->GetTimeSeconds()-.4;
    Throat->Tick(.01f); TestTrue(TEXT("Spasm drives the imported vomit pose"),Throat->AuthoredMouth->GetMorphTarget(TEXT("vomit"))>.5f);
    Throat->ThroatPhase=EMCThroatPhase::Vomiting; Throat->PhaseStartedAt=M.World->GetTimeSeconds()-.3;
    Throat->Tick(.01f); TestTrue(TEXT("Expulsion holds the mouth open and contracts the vomit pose"),Throat->OpenAmount()>.9f && Throat->AuthoredMouth->GetMorphTarget(TEXT("vomit"))>.8f);
    Throat->PhaseStartedAt=M.World->GetTimeSeconds()-Throat->VomitSeconds+.01;
    const float BeforeRecovery=Throat->OpenAmount();
    Throat->ThroatPhase=EMCThroatPhase::Recovering; Throat->PhaseStartedAt=M.World->GetTimeSeconds();
    TestTrue(TEXT("Expulsion flows into closing without closing and reopening"),BeforeRecovery>.9f && FMath::Abs(BeforeRecovery-Throat->OpenAmount())<.1f);
    return true;
}
// A rejected gulp is atomic even when a frame passes the entire swallow interval.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVomitMealTest,"MessControl.Hazards.VomitReturnsOnlyCurrentGulp",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVomitMealTest::RunTest(const FString&)
{
    FTestMouth M; M.Mode->SetActorTickEnabled(false);
    auto* Throat=M.World->SpawnActor<AMCThroat>(FVector(5000,0,200),FRotator::ZeroRotator);
    const FVector Center=Throat->GetActorTransform().TransformPosition(Throat->ZoneCenter);
    auto* Good=M.GripCube(); auto* Bad=M.GripCube(); auto* Outside=M.GripCube();
    for(auto* F:{Good,Bad,Outside}) { F->Phase=EMCFoodPhase::Free; F->Body->SetSimulatePhysics(false); F->Batch=77; }
    for(bool Spoiled:{false,true}) {
        Bad->bSpoiled=Spoiled; Bad->FoodData.Kind=Spoiled?EMCFoodKind::Food:EMCFoodKind::ForeignObject;
        Good->SetActorLocation(Center+FVector(0,-65,50)); Bad->SetActorLocation(Center+FVector(0,65,50));
        Outside->SetActorLocation(Center+FVector(0,1000,50));
        Throat->ThroatPhase=EMCThroatPhase::Anticipation; Throat->PhaseStartedAt=M.World->GetTimeSeconds()-Throat->AnticipationSeconds-.01;
        Throat->Tick(.01f);
        TestTrue(TEXT("Both pieces enter the same gulp"),Good->Phase==EMCFoodPhase::Swallowing && Bad->Phase==EMCFoodPhase::Swallowing);
        Throat->PhaseStartedAt-=Throat->SwallowSeconds+1; Throat->Tick(.01f);
        TestEqual(TEXT("Late frame still rejects before committing any piece"),Throat->ThroatPhase,EMCThroatPhase::Spasm);
        TestTrue(TEXT("Healthy part of rejected meal remains alive"),!Good->IsDisposed() && Throat->FoodSwallowed==0);
        Throat->PhaseStartedAt-=Throat->SpasmSeconds+.01; Throat->Tick(.01f);
        TestTrue(TEXT("Whole current meal returns"),Good->Phase==EMCFoodPhase::Free && Bad->Phase==EMCFoodPhase::Free);
        TestTrue(TEXT("An item from the same batch outside the gulp stays put"),Outside->Phase==EMCFoodPhase::Free && Outside->GetActorLocation().Equals(Center+FVector(0,1000,50)));
        TestEqual(TEXT("Returned item keeps batch identity"),Good->Batch,77);
        Good->Body->SetSimulatePhysics(false); Bad->Body->SetSimulatePhysics(false); Throat->ResetSwallow();
    }
    TestEqual(TEXT("Exactly one vomit per rejected gulp"),Throat->VomitCount,2);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVomitFlightTest,"MessControl.Hazards.VomitFlightImpactPersistenceAndWipe",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVomitFlightTest::RunTest(const FString&)
{
    FTestMouth M; M.Mode->SetActorTickEnabled(false); M.State->bPhysicalBrushes=false;
    auto* Tongue=M.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FTransform::Identity);
    Tongue->SourceMesh=ReadyCareTestMesh(TEXT("/Game/Gameplay/Arena/SM_TongueSurface.SM_TongueSurface"));
    Tongue->FinishSpawning(FTransform::Identity); Tongue->SetActorTickEnabled(false);
    Tongue->Settings.IdleHeight=0; Tongue->PressureSettings.bEnabled=false; Tongue->Tick(.033f);
    FHitResult Floor; const FVector Center=FBox(Tongue->CurrentVertices()).GetCenter();
    if(!TestTrue(TEXT("Actual tongue provides a floor"),Tongue->SurfacePoint(Center,Floor))) return false;
    auto* Throat=M.World->SpawnActor<AMCThroat>(FVector(Center.X+650,Center.Y,Floor.ImpactPoint.Z),FRotator::ZeroRotator);
    Throat->SetActorTickEnabled(false);
    auto* Burst=M.World->SpawnActor<AMCVomitBurst>(); Burst->Configure(Throat);
    TestEqual(TEXT("Three portions plan real surface hits"),Burst->Portions.Num(),3);
    auto Puddles=[&]() { TArray<AMCMouthSurface*> Result; for(TActorIterator<AMCMouthSurface> It(M.World);It;++It) if(It->Batch==Burst->Batch) Result.Add(*It); return Result; };
    TestTrue(TEXT("No dirt appears before impact"),Puddles().IsEmpty());
    M.Step(.3f); TestTrue(TEXT("Portions spend time airborne"),Burst->LandedPortions()==0 && Burst->AirborneInstances>0 && Puddles().IsEmpty());
    M.Step(2.1f);
    auto Patches=Puddles();
    if(!TestEqual(TEXT("Swept flights create three actual tongue impacts"),Burst->LandedPortions(),3) || !TestEqual(TEXT("Each impact creates exactly one patch"),Patches.Num(),3)) return false;
    for(auto* Patch:Patches) {
        TestTrue(TEXT("Impact belongs to the tongue and starts a spread animation"),Patch->GetTongue()==Tongue && Patch->LiquidBornAt>0 && !Patch->IsClean());
        FHitResult Hit; Tongue->SurfacePoint(Patch->GetActorLocation(),Hit);
        TestTrue(TEXT("Dirty patch stays on the impact surface"),FVector::Dist(Hit.ImpactPoint,Patch->GetActorLocation())<7);
    }
    M.Step(4);
    TestFalse(TEXT("Temporary projectile actor expires"),IsValid(Burst));
    for(auto* Patch:Patches) TestTrue(TEXT("Dirt outlives the airborne effect"),IsValid(Patch) && !Patch->IsClean());
    auto* Hero=M.Worker(); auto* Patch=Patches[0];
    Hero->SetActorLocation(Patch->GetActorLocation()+FVector(-65,0,75)); Hero->SetActorRotation(FRotator::ZeroRotator); Hero->bBrushing=true;
    for(int32 I=0;I<5;++I) Hero->AdvanceCare(.1f);
    TestTrue(TEXT("Real brush contacts erase a local track in vomit"),Patch->RemainingLiquid()<.99f);
    for(int32 I=0;I<200 && !Patch->IsClean();++I) Hero->AdvanceCare(.1f);
    TestTrue(TEXT("Vomit can be completely cleaned through the existing brush mechanic"),Patch->IsClean() && Patch->RemainingLiquid()<.025f);
    TestTrue(TEXT("Cleaning one impact preserves the other two"),!Patches[1]->IsClean() && !Patches[2]->IsClean());
    auto* Cancelled=M.World->SpawnActor<AMCVomitBurst>(); Cancelled->Configure(Throat); Cancelled->Batch=54321; Cancelled->Destroy(); M.Step(2.5f);
    int32 CancelledDirt=0; for(TActorIterator<AMCMouthSurface> It(M.World);It;++It) if(It->Batch==54321) ++CancelledDirt;
    TestEqual(TEXT("Cancelling an airborne burst cannot create delayed dirt"),CancelledDirt,0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSpicyFuseTest,"MessControl.Hazards.SpicyFusePauseResumeAndRound",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSpicyFuseTest::RunTest(const FString&)
{
    FTestMouth M; M.Mode->SetActorTickEnabled(false);
    auto* Throat=M.World->SpawnActor<AMCThroat>(FVector(5000,0,200),FRotator::ZeroRotator);
    auto* Pepper=M.GripCube(); Pepper->Body->SetSimulatePhysics(false); Pepper->SetActorTickEnabled(false);
    Pepper->FoodData.Kind=EMCFoodKind::Spicy; Pepper->Phase=EMCFoodPhase::Free;
    Pepper->SetActorLocation(Throat->GetActorTransform().TransformPosition(Throat->ZoneCenter+FVector(0,0,50)));
    M.State->Day=1; Pepper->ArmSpicy(); TestEqual(TEXT("First round gives eight seconds"),Pepper->FuseRemaining(),8.f);
    Pepper->FuseEndsAt-=3; const float Remaining=Pepper->FuseRemaining();
    Throat->ThroatPhase=EMCThroatPhase::Anticipation; Throat->PhaseStartedAt=M.World->GetTimeSeconds(); Throat->Tick(.01f);
    TestTrue(TEXT("Uvula anticipation immediately pauses fuse"),Pepper->bFusePaused);
    M.Step(.2f); TestEqual(TEXT("Paused time is retained"),Pepper->FuseRemaining(),Remaining);
    Pepper->SetActorLocation(Pepper->GetActorLocation()+FVector(0,1000,0)); Throat->Tick(.01f);
    TestFalse(TEXT("Leaving collecting circle resumes the fuse"),Pepper->bFusePaused);
    TestTrue(TEXT("Leaving cannot refresh the countdown"),FMath::IsNearlyEqual(Pepper->FuseRemaining(),Remaining,.02f));
    const float FirstRadius=Pepper->DetonationRadius();
    const FVector CollisionExtent=Pepper->Body->GetUnscaledBoxExtent();
    Pepper->FuseEndsAt=M.State->GetServerWorldTimeSeconds()+Pepper->FoodData.FuseSeconds-.236f;
    Pepper->Tick(.01f);const float Expanded=Pepper->Visual->GetRelativeScale3D().X;
    auto* PulseMaterial=Cast<UMaterialInstanceDynamic>(Pepper->Visual->GetMaterial(0));
    const float Bright=PulseMaterial?PulseMaterial->K2_GetScalarParameterValue(TEXT("Flash")):0;
    Pepper->FuseEndsAt=M.State->GetServerWorldTimeSeconds()+Pepper->FoodData.FuseSeconds-.646f;
    Pepper->Tick(.01f);const float Rest=Pepper->Visual->GetRelativeScale3D().X;
    TestTrue(TEXT("Cartoon beat expands mesh and changes shader in the same phase"),Expanded>Rest*1.12f && Bright>.98f && PulseMaterial && PulseMaterial->K2_GetScalarParameterValue(TEXT("Flash"))<.02f);
    TestTrue(TEXT("Cosmetic pulse leaves collision dimensions unchanged"),Pepper->Body->GetUnscaledBoxExtent().Equals(CollisionExtent));
    TestTrue(TEXT("Pulsing mesh retains its center"),Pepper->Visual->GetRelativeTransform().TransformPosition(Pepper->ItemMesh->GetBounds().Origin).IsNearlyZero());
    auto* Later=M.GripCube(); Later->Body->SetSimulatePhysics(false); Later->FoodData.Kind=EMCFoodKind::Spicy;
    M.State->Day=7; Later->ArmSpicy(); TestEqual(TEXT("Later round clamps to six seconds"),Later->FuseRemaining(),6.f);
    TestTrue(TEXT("Later round has wider pulse"),Later->DetonationRadius()>FirstRadius);
    Later->PauseFuse(Throat); Later->Detonate(); TestFalse(TEXT("Paused pepper cannot detonate"),Later->IsDisposed());
    Throat->ResetSwallow(); TestFalse(TEXT("Reset releases owned fuses"),Later->bFusePaused);
    Later->Detonate(); TestTrue(TEXT("Unpaused detonation consumes pepper"),Later->IsDisposed());
    int32 Waves=0; for(TActorIterator<AMCHazardWave> It(M.World);It;++It) if(It->bSpicy) ++Waves;
    TestEqual(TEXT("Exactly one spicy wave"),Waves,1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCWaveDodgeTest,"MessControl.Hazards.JumpClearsSweptWave",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCWaveDodgeTest::RunTest(const FString&)
{
    FTestMouth M; M.Mode->SetActorTickEnabled(false);
    auto* Low=M.Worker(); auto* High=M.Worker();
    Low->SetActorLocation(FVector(5000,100,Low->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    High->SetActorLocation(FVector(5000,-100,High->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+90));
    auto* Wave=M.World->SpawnActor<AMCHazardWave>(FVector(5000,0,0),FRotator::ZeroRotator);
    Wave->MaxRadius=300; Wave->WarningSeconds=.1f; Wave->Damage=12; Wave->TravelSeconds=1;
    const float LowHP=Low->Status->State.Health,HighHP=High->Status->State.Health;
    // A large timestep still sweeps across the annulus rather than skipping it.
    Wave->StartedAt=M.World->GetTimeSeconds()-.75; Wave->Tick(.75f);
    TestTrue(TEXT("Grounded player is hit"),Low->Status->State.Health<LowHP);
    TestEqual(TEXT("Airborne player's feet clear the wave"),High->Status->State.Health,HighHP);
    High->SetActorLocation(FVector(5000,-100,High->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())); Wave->Tick(.01f);
    TestEqual(TEXT("Landing behind a crossed wave is safe"),High->Status->State.Health,HighHP);
    TestEqual(TEXT("A wave damages a player only once"),Wave->HitCount,1);
    TestTrue(TEXT("Moving through an expanding wave is swept"),AMCHazardWave::Crosses(250,10,20,200,20));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCUlcerSurfaceWaveTest,"MessControl.Hazards.UlcerWaveDeformsLocalSurface",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCUlcerSurfaceWaveTest::RunTest(const FString&)
{
    FTestMouth M; M.Mode->SetActorTickEnabled(false); M.State->Phase=EMCShiftPhase::Working; M.State->bDevManualEvents=true;
    auto* Tongue=M.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FTransform::Identity);
    Tongue->SourceMesh=ReadyCareTestMesh(TEXT("/Game/Gameplay/Arena/SM_TongueSurface.SM_TongueSurface"));
    Tongue->FinishSpawning(FTransform::Identity); Tongue->SetActorTickEnabled(false);
    Tongue->Settings.IdleHeight=0; Tongue->PressureSettings.bEnabled=false; Tongue->Tick(.033f);
    const auto Rest=Tongue->CurrentVertices();
    FHitResult Floor;
    if(!TestTrue(TEXT("Tongue fixture has a surface"),Tongue->SurfacePoint(FBox(Rest).GetCenter(),Floor))) return false;
    auto SpawnUlcer=[&](FVector Point)
    {
        FHitResult Hit; if(!Tongue->SurfacePoint(Point,Hit)) return static_cast<AMCMouthSurface*>(nullptr);
        const FTransform T(Hit.ImpactPoint+FVector(0,0,5));
        auto* Ulcer=M.World->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),T);
        Ulcer->bUlcer=true; Ulcer->bRandomizeLiquidSize=false; Ulcer->FinishSpawning(T); Ulcer->SetActorTickEnabled(false);
        return Ulcer;
    };
    auto SpawnWave=[&](AMCMouthSurface* Ulcer)
    {
        const FTransform T(Ulcer->GetActorLocation());
        auto* Wave=M.World->SpawnActorDeferred<AMCHazardWave>(AMCHazardWave::StaticClass(),T);
        Wave->Source=Ulcer; Wave->MaxRadius=260; Wave->FinishSpawning(T); Wave->SetActorTickEnabled(false); return Wave;
    };
    auto* Ulcer=SpawnUlcer(Floor.ImpactPoint);
    if(!TestNotNull(TEXT("Ulcer binds to the tongue"),Ulcer) || !TestEqual(TEXT("Ulcer uses the actual tongue"),Ulcer->GetTongue(),Tongue)) return false;
    auto* Wave=SpawnWave(Ulcer);
    auto Crest=[&](float Age)
    {
        Wave->StartedAt=Tongue->ServerTime()-Wave->WarningSeconds-Age;
        Tongue->Tick(.033f);
        float Sum=0,WeightedRadius=0,Outside=0,Peak=0; int32 PeakIndex=INDEX_NONE;
        const auto* Section=Tongue->Surface->GetProcMeshSection(0);
        bool Red=false,OutsideRed=false;
        for(int32 I=0;I<Rest.Num();++I)
        {
            const float D=Tongue->CurrentVertices()[I].Z-Rest[I].Z;
            const float Radius=FVector::Dist2D(Rest[I],Ulcer->GetActorLocation());
            if(Radius>Wave->MaxRadius) { Outside=FMath::Max(Outside,FMath::Abs(D)); OutsideRed|=Section->ProcVertexBuffer[I].Color.R!=0; }
            if(D>0) { Sum+=D; WeightedRadius+=Radius*D; }
            if(D>Peak && Section->ProcVertexBuffer[I].Normal.Z>.3) { Peak=D; PeakIndex=I; }
            Red|=Section->ProcVertexBuffer[I].Color.R!=0;
        }
        TestTrue(TEXT("The wave visibly lifts tissue"),Peak>1);
        TestTrue(TEXT("The existing 260 cm radius is preserved"),Outside<.001f);
        TestTrue(TEXT("Local crest uses the existing pain redness"),Red);
        TestFalse(TEXT("Pain redness stays inside the 260 cm pulse radius"),OutsideRed);
        if(PeakIndex!=INDEX_NONE)
        {
            const FVector P=Tongue->CurrentVertices()[PeakIndex]; FHitResult Hit;
            TestTrue(TEXT("Collision follows the visible crest"),Tongue->SurfacePoint(P,Hit) && FMath::Abs(Hit.ImpactPoint.Z-P.Z)<1);
        }
        return WeightedRadius/FMath::Max(1.f,Sum);
    };
    const float Early=Crest(.25f),Late=Crest(.65f);
    TestTrue(TEXT("The local front expands over time"),Late>Early+40);
    // Existing large motions keep their own slot and cannot suppress local ulcer pulses.
    auto* Profile=NewObject<UMCTongueMotionProfile>(); Profile->Settings.Height=0; Profile->Settings.Redness=0;
    Profile->Settings.Push=0; Profile->Settings.Lift=0;
    TestTrue(TEXT("A larger event can coexist"),Tongue->PlayMotion(Profile,Floor.ImpactPoint,FVector::ForwardVector));
    Crest(.45f); TestEqual(TEXT("Local pulses leave the larger motion untouched"),Tongue->Motion.Serial,1);
    Wave->Tick(.01f); TestFalse(TEXT("The ulcer's pink ring is replaced"),Wave->Ring->IsVisible());
    const float SavedHeight=Tongue->Settings.WaveHeight; Tongue->Settings.WaveHeight=SavedHeight+20;
    FMCTongueMotionState Pulse; Wave->SurfaceMotion(Tongue,Tongue->ServerTime(),Pulse);
    TestEqual(TEXT("An active pulse keeps its snapshotted height"),Pulse.Settings.Height,SavedHeight);
    TestEqual(TEXT("Local pulse retains the original pain color intensity"),Pulse.Settings.Redness,1.f);
    auto* OtherUlcer=SpawnUlcer(Floor.ImpactPoint+FVector(350,0,0));
    if(!TestNotNull(TEXT("Second ulcer has a surface"),OtherUlcer)) return false;
    auto* OtherWave=SpawnWave(OtherUlcer); OtherWave->StartedAt=Wave->StartedAt;
    Tongue->Tick(.033f);
    float OtherCrest=0;
    for(int32 I=0;I<Rest.Num();++I)
        if(FVector::Dist2D(Rest[I],Ulcer->GetActorLocation())>Wave->MaxRadius)
            OtherCrest=FMath::Max(OtherCrest,float(Tongue->CurrentVertices()[I].Z-Rest[I].Z));
    TestTrue(TEXT("Another ulcer independently deforms its own region"),OtherCrest>1);
    TestTrue(TEXT("Treatment can suppress the ulcer"),Ulcer->ApplyAnesthetic(3));
    TestTrue(TEXT("The second ulcer can also be treated"),OtherUlcer->ApplyAnesthetic(3));
    Tongue->Tick(.033f);
    float Remaining=0; for(int32 I=0;I<Rest.Num();++I) Remaining=FMath::Max(Remaining,float(FVector::Distance(Rest[I],Tongue->CurrentVertices()[I])));
    TestTrue(TEXT("Treatment stops the surface wave immediately"),Remaining<.001f);
    bool RemainingRed=false; for(const auto& V:Tongue->Surface->GetProcMeshSection(0)->ProcVertexBuffer) RemainingRed|=V.Color.R!=0;
    TestFalse(TEXT("Treatment also stops the local pain redness"),RemainingRed);
    Wave->Tick(.01f); TestTrue(TEXT("Treatment also removes the damage wave"),Wave->IsActorBeingDestroyed());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCClimbTest,"MessControl.Locomotion.ClimbHangReleaseAndPredictedJump",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCClimbTest::RunTest(const FString&)
{
    FTestMouth M; M.Mode->SetActorTickEnabled(false);
    auto* Wall=M.World->SpawnActor<AActor>(FVector(5100,0,350),FRotator::ZeroRotator);
    auto* Box=NewObject<UBoxComponent>(Wall); Wall->SetRootComponent(Box); Box->SetBoxExtent(FVector(20,300,350));
    Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent(); Box->SetWorldLocation(FVector(5100,0,350));
    auto* Hero=M.Worker(); Hero->SetActorLocation(FVector(5000,0,150));
    auto* Move=CastChecked<UMCToothMovementComponent>(Hero->GetCharacterMovement()); Move->bRunPhysicsWithNoController=true; Move->SetMovementMode(MOVE_Falling);
    Move->SetWantsClimb(true); Move->UpdateCharacterStateBeforeMovement(.01f);
    TestTrue(TEXT("Reachable solid wall starts climbing"),Move->IsClimbing());
    const float StartZ=Hero->GetActorLocation().Z;
    for(int32 I=0;I<24;++I) { Hero->AddMovementInput(FVector::UpVector); M.Step(1.f/60); }
    TestTrue(TEXT("Up input climbs the wall"),Hero->GetActorLocation().Z>StartZ+25);
    const FVector Hanging=Hero->GetActorLocation();
    M.Step(1.2f);
    TestTrue(TEXT("No stamina loss or gravity while hanging"),Move->IsClimbing() && FMath::Abs(Hero->GetActorLocation().Z-Hanging.Z)<3);
    Hero->Jump(); M.Step(1.f/60);
    TestTrue(TEXT("Normal predicted jump input launches away from wall"),Move->IsFalling() && Move->Velocity.X<-100 && Move->Velocity.Z>200);
    Hero->StopJumping(); Move->UpdateFromCompressedFlags(FSavedMove_Character::FLAG_Custom_1);
    TestTrue(TEXT("Network flag restores climb intent"),Move->WantsClimb());
    Move->UpdateFromCompressedFlags(0); TestFalse(TEXT("Network flag releases intent"),Move->WantsClimb());
    // An ordinary narrow ledge can be mounted with full capsule sweeps.
    M.Step(.6f);
    Hero->SetActorLocation(FVector(5000,0,610)); Move->SetMovementMode(MOVE_Falling); Move->SetWantsClimb(true);
    bool Mantled=false;
    for(int32 I=0;I<70 && !Mantled;++I) {
        Hero->AddMovementInput(FVector::UpVector); M.Step(1.f/60);
        Mantled=Move->IsMovingOnGround() && Hero->GetActorLocation().Z>740;
    }
    TestTrue(TEXT("Climbing over the lip ends on the top surface"),Mantled);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCIceToolTest,"MessControl.ColdCola.ToolRangeAndHealth",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCIceToolTest::RunTest(const FString&)
{
    FTestMouth M; M.Mode->SetActorTickEnabled(false); M.State->Phase=EMCShiftPhase::Working;
    auto* Hero=M.Worker(); Hero->GetCharacterMovement()->DisableMovement();
    Hero->SetActorLocationAndRotation(FVector(5000,0,100),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
    M.Step(.7f);
    const FTransform T(FVector(5100,0,100));
    auto* Ice=M.World->SpawnActorDeferred<AMCIceBlock>(AMCIceBlock::StaticClass(),T);
    Ice->Health=Ice->MaxHealth=120; Ice->Size=FVector(95,65,110); Ice->FinishSpawning(T); Ice->Body->SetSimulatePhysics(false);
    Hero->Inventory->ServerSelect(EMCToolSlot::Knife);
    TestFalse(TEXT("Knife cannot break ice"),Ice->HitWithPickaxe(Hero,40));
    Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
    TestFalse(TEXT("Invalid damage is rejected"),Ice->HitWithPickaxe(Hero,std::numeric_limits<float>::quiet_NaN()));
    Hero->SetActorRotation(FRotator(0,180,0));
    TestFalse(TEXT("An ice block behind the player cannot be hit"),Ice->HitWithPickaxe(Hero,40));
    Hero->SetActorLocationAndRotation(FVector(4700,0,100),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
    TestFalse(TEXT("A distant ice block cannot be hit"),Ice->HitWithPickaxe(Hero,40));
    Hero->SetActorLocation(FVector(5000,0,100),false,nullptr,ETeleportType::TeleportPhysics);
    TestTrue(TEXT("Facing nearby pickaxe hit succeeds"),Ice->HitWithPickaxe(Hero,40)); Ice->Body->SetSimulatePhysics(false);
    TestEqual(TEXT("Configured ice health survives the first hit"),Ice->Health,80.f);
    TestTrue(TEXT("Finishing hit succeeds"),Ice->HitWithPickaxe(Hero,80));
    TestTrue(TEXT("Broken ice disables its solid obstacle"),Ice->bBroken && Ice->Body->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
    TestFalse(TEXT("A broken block cannot be counted twice"),Ice->HitWithPickaxe(Hero,40));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCRestartHazardsTest,"MessControl.Run.RestartClearsLiveHazards",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCRestartHazardsTest::RunTest(const FString&)
{
    FTestMouth M;M.Mode->SetActorTickEnabled(false);
    auto* Cola=M.World->SpawnActor<AMCColdColaEvent>();
    auto* Plan=LoadObject<UMCDayPlan>(nullptr,TEXT("/Game/Data/DA_Day01.DA_Day01"));
    if(!TestNotNull(TEXT("Authored day plan"),Plan)) return false;
    Cola->Start(Plan);
    auto* Floor=Cola->SlipperyFloor.Get();auto* Drink=Cola->Drink.Get();
    auto* Ice=M.World->SpawnActor<AMCIceBlock>();
    auto* Ulcer=M.World->SpawnActor<AMCMouthSurface>();Ulcer->bUlcer=true;
    auto* Wave=M.World->SpawnActor<AMCHazardWave>();Wave->WarningSeconds=30;
    auto* Pepper=M.GripCube();Pepper->FoodData.Kind=EMCFoodKind::Spicy;Pepper->ArmSpicy();
    M.State->Day=4;M.Mode->RestartShift();
    TestTrue(TEXT("Restart removes slippery volume and flood"),Floor->IsActorBeingDestroyed() && Drink->IsActorBeingDestroyed());
    TestTrue(TEXT("Restart removes ice, ulcers, live waves and armed pepper"),Ice->IsActorBeingDestroyed() && Ulcer->IsActorBeingDestroyed() && Wave->IsActorBeingDestroyed() && Pepper->IsActorBeingDestroyed());
    TestFalse(TEXT("Restart clears old cold event"),Cola->bActive);
    TestEqual(TEXT("Restart resets day"),M.State->Day,0);
    M.NextPhase();TestEqual(TEXT("Fresh day starts after reset"),M.State->Day,1);
    TestTrue(TEXT("Fresh event has its own timer"),M.State->PhaseEndsAt>M.State->GetServerWorldTimeSeconds());
    return true;
}

#endif
