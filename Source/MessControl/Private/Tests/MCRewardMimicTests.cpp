#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCRewardChest.h"
#include "MCRewardDropZone.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothMovementComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCGripComponent.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCPerkComponent.h"
#include "MCGameState.h"
#include "MCOrbitSpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace
{
    struct FMimicWorld
    {
        UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
        UDataTable* Table=nullptr;
        AMCRewardDropZone* Zone=nullptr;
        FMimicWorld()
        {
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->SetGameInstance(NewObject<UGameInstance>(GEngine));
            FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
            World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
            auto* State=World->SpawnActor<AMCGameState>(); State->Phase=EMCShiftPhase::Working;
            World->SetGameState(State);
            auto* Floor=Box(FVector(0,0,9980),FVector(10000,10000,20));
            Zone=World->SpawnActor<AMCRewardDropZone>(FVector(0,0,10000),FRotator::ZeroRotator);
            Zone->AllowedLandingSurface=Floor; Zone->Area->SetBoxExtent(FVector(9000,9000,1000));
            Table=NewObject<UDataTable>(); Table->RowStruct=FMCPerkDefinition::StaticStruct();
            for(int32 I=0;I<8;++I) {
                FMCPerkDefinition Row; Row.DisplayName=FText::FromString(FString::Printf(TEXT("Mimic fixture %d"),I));
                Row.Polarity=I<4?EMCPerkPolarity::Positive:EMCPerkPolarity::Negative;
                Row.Weight=1+I; Row.MaxStacks=5;
                Table->AddRow(FName(*FString::Printf(TEXT("MimicFixture%d"),I)),Row);
            }
        }
        ~FMimicWorld()
        { World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
        AActor* Box(FVector Location,FVector Extent)
        {
            auto* Actor=World->SpawnActor<AActor>(); auto* Shape=NewObject<UBoxComponent>(Actor);
            Actor->SetRootComponent(Shape); Shape->SetBoxExtent(Extent); Shape->SetCollisionProfileName(TEXT("BlockAll"));
            // Drop zones intentionally reject hidden support. Shape components
            // default to HiddenInGame, unlike the real visible tongue surface.
            Shape->SetHiddenInGame(false);
            Shape->RegisterComponent(); Actor->SetActorLocation(Location); return Actor;
        }
        void Step(float Seconds)
        { for(int32 I=0;I<FMath::CeilToInt(Seconds*60);++I) { ++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60); } }
        AMCRewardChest* Chest(float Chance,int32 Seed=6183,FVector XY=FVector::ZeroVector,bool Land=true,bool Effects=false)
        {
            XY.Z=10000; const FTransform Pose(XY);
            auto* Chest=World->SpawnActorDeferred<AMCRewardChest>(AMCRewardChest::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            Chest->MimicChance=Chance; Chest->bPlacedReward=true; Chest->PlacedDropZone=Zone; Chest->PerkTable=Table;
            Chest->TelegraphSeconds=.1f; Chest->FallSeconds=.1f; Chest->OpeningSeconds=.1f; Chest->LockpickingSeconds=.1f;
            Chest->InitializeReward(XY,XY,Seed,Table,EMCRewardSelectionPolicy::ChooseOne,Zone);
            if(Effects) for(const FName Name:{FName(TEXT("RewardGlowRays")),FName(TEXT("RewardFloorHalo"))}) {
                auto* Effect=NewObject<UStaticMeshComponent>(Chest,Name);
                Chest->AddInstanceComponent(Effect); Effect->SetupAttachment(Chest->Scene);
                Effect->SetVisibility(Name==TEXT("RewardGlowRays")); Effect->SetHiddenInGame(false); Effect->RegisterComponent();
            }
            Chest->FinishSpawning(Pose);
#if WITH_EDITOR
            FStaticMeshCompilingManager::Get().FinishCompilation({Chest->Body->GetStaticMesh(),Chest->Lid->GetStaticMesh()});
#endif
            if(Land) Step(.6f);
            return Chest;
        }
        FVector Front(AMCRewardChest* Chest,float Side=0) const
        {
            const FTransform Contact=Chest->GetLockpickContact();
            FVector P=Contact.GetLocation()+Contact.GetUnitAxis(EAxis::X)*120+Contact.GetUnitAxis(EAxis::Y)*Side;
            P.Z=10061; return P;
        }
        AMCToothCharacter* Worker(FVector P)
        {
            auto* PC=World->SpawnActor<AMCPlayerController>();
            const FTransform StatePose;
            auto* PS=World->SpawnActorDeferred<AMCPlayerState>(AMCPlayerState::StaticClass(),StatePose,PC);
            PS->Perks->PerkTable=Table; PS->FinishSpawning(StatePose); PC->SetPlayerState(PS);
            auto* Hero=World->SpawnActor<AMCToothCharacter>(P,FRotator(0,180,0)); PC->Possess(Hero);
            Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking); return Hero;
        }
        void Move(AMCToothCharacter* Hero,FVector P)
        {
            Hero->SetActorLocation(P,false,nullptr,ETeleportType::TeleportPhysics);
            Hero->GetCharacterMovement()->StopMovementImmediately();
        }
        bool Open(FAutomationTestBase& Test,AMCRewardChest* Chest,AMCToothCharacter* Hero,const TCHAR* Label)
        {
            const bool Opened=Chest->BeginLockpicking(Hero);
            if(!Opened) {
                const auto* PS=Hero->GetPlayerState<AMCPlayerState>();
                const auto* State=World->GetGameState<AMCGameState>();
                const FTransform Contact=Chest->GetLockpickContact();
                const FVector Delta=Hero->GetActorLocation()-Contact.GetLocation();
                FCollisionQueryParams Query(SCENE_QUERY_STAT(MCMimicFixtureFacts),true,Chest); Query.AddIgnoredActor(Hero);
                FHitResult Sight,Floor;
                const bool Blocked=World->LineTraceSingleByChannel(Sight,Hero->GetActorLocation()+FVector(0,0,35),Chest->Solid->Bounds.Origin,ECC_Visibility,Query);
                const bool HasFloor=World->LineTraceSingleByChannel(Floor,Chest->LandingPoint+FVector(0,0,30),Chest->LandingPoint-FVector(0,0,30),ECC_Visibility,Query);
                Test.AddInfo(FString::Printf(TEXT("Mimic fixture guards: stage=%s authority=%d PS=%s pawnMatches=%d alive=%d canWork=%d physicsCanAct=%d lobby=%d phase=%d zone=%s meshes=%d/%d hero=%s chest=%s contact=%s distance=%.2f frontDot=%.3f frontDistance=%.2f reach=%d LOS=%s floor=%s registered=%d visible=%d accepted=%d"),
                    *StaticEnum<EMCRewardChestStage>()->GetNameStringByValue(int64(Chest->Stage)),Chest->HasAuthority(),*GetNameSafe(PS),PS && PS->GetPawn()==Hero,Hero->Status->IsAlive(),Hero->CanWork(),Hero->ToothPhysics->CanAct(),State && State->bLobbyWaiting,State?int32(State->Phase):-1,*GetNameSafe(Chest->PlacedDropZone),Chest->Body->GetStaticMesh()!=nullptr,Chest->Lid->GetStaticMesh()!=nullptr,
                    *Hero->GetActorLocation().ToString(),*Chest->GetActorLocation().ToString(),*Contact.GetLocation().ToString(),FVector::Dist(Hero->GetActorLocation(),Chest->GetActorLocation()),FVector::DotProduct(Delta.GetSafeNormal2D(),Contact.GetUnitAxis(EAxis::X).GetSafeNormal2D()),FVector::DotProduct(Delta,Contact.GetUnitAxis(EAxis::X).GetSafeNormal2D()),Chest->CanReachLockpick(Hero),*GetNameSafe(Blocked?Sight.GetActor():nullptr),*GetNameSafe(HasFloor?Floor.GetActor():nullptr),Floor.GetComponent() && Floor.GetComponent()->IsRegistered(),Floor.GetComponent() && Floor.GetComponent()->IsVisible(),HasFloor && Zone->AcceptsFloor(Floor)));
                Query.AddIgnoredActor(Floor.GetActor()); Query.bTraceComplex=false;
                FCollisionObjectQueryParams Objects;
                Objects.AddObjectTypesToQuery(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
                Objects.AddObjectTypesToQuery(ECC_Pawn); Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
                TArray<FOverlapResult> Overlaps; const FVector Extent=Chest->GetPlacementHalfExtent();
                World->OverlapMultiByObjectType(Overlaps,Chest->LandingPoint+FVector(0,0,Extent.Z),FQuat::Identity,Objects,FCollisionShape::MakeBox(Extent),Query);
                for(const FOverlapResult& Overlap:Overlaps) Test.AddInfo(FString::Printf(TEXT("Mimic fixture landing overlap: %s/%s"),*GetNameSafe(Overlap.GetActor()),*GetNameSafe(Overlap.GetComponent())));
            }
            return Test.TestTrue(Label,Opened);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCMimicRoll,"MessControl.Rewards.Mimic.ChanceAndSeed",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCMimicRoll::RunTest(const FString&)
{
    FMimicWorld W;
    auto* Chest=W.Chest(0,1,FVector::ZeroVector,false);
    int32 Mimics=0;
    for(int32 Seed=0;Seed<32;++Seed) {
        Chest->MimicChance=0; Chest->InitializeReward(FVector::ZeroVector,FVector::ZeroVector,Seed,W.Table,EMCRewardSelectionPolicy::ChooseOne,W.Zone);
        TestFalse(TEXT("Chance zero always produces a regular reward"),Chest->bMimic);
        Chest->MimicChance=1; Chest->InitializeReward(FVector::ZeroVector,FVector::ZeroVector,Seed,W.Table,EMCRewardSelectionPolicy::ChooseOne,W.Zone);
        TestTrue(TEXT("Chance one always produces a mimic"),Chest->bMimic);
        Chest->MimicChance=.35f; Chest->InitializeReward(FVector::ZeroVector,FVector::ZeroVector,Seed,W.Table,EMCRewardSelectionPolicy::ChooseOne,W.Zone);
        const bool First=Chest->bMimic; Mimics+=First;
        Chest->InitializeReward(FVector::ZeroVector,FVector::ZeroVector,Seed,W.Table,EMCRewardSelectionPolicy::ChooseOne,W.Zone);
        TestEqual(TEXT("A repeated chest seed reproduces the mimic roll"),Chest->bMimic,First);
    }
    TestTrue(TEXT("Intermediate chance produces both outcomes over the fixed seed sample"),Mimics>0 && Mimics<32);
    Chest->ForceMimicForTest(false); Chest->MimicChance=1;
    Chest->InitializeReward(FVector::ZeroVector,FVector::ZeroVector,12,W.Table,EMCRewardSelectionPolicy::ChooseOne,W.Zone);
    TestFalse(TEXT("The F3 fixture can force an ordinary reward independently of chance"),Chest->bMimic);
    Chest->ForceMimicForTest();
    TestTrue(TEXT("The F3 fixture can force a mimic"),Chest->bMimic);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCMimicRegular,"MessControl.Rewards.Mimic.RegularRewardRegression",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCMimicRegular::RunTest(const FString&)
{
    FMimicWorld W;
    auto* A=W.Chest(0,113); auto* B=W.Chest(1,113,FVector(1200,0,0)); B->ForceMimicForTest(false);
    auto* Opener=W.Worker(W.Front(A)); auto* Other=W.Worker(W.Front(B));
    TestEqual(TEXT("The real touchdown path lands the regular chest"),A->Stage,EMCRewardChestStage::Landed);
    if(!W.Open(*this,A,Opener,TEXT("The ordinary reward still starts lockpicking"))) return false;
    if(!W.Open(*this,B,Other,TEXT("A forced regular reward follows the same opening path"))) return false;
    TestTrue(TEXT("Mimic selection does not consume or alter the original seeded loot draw"),A->LootIDs==B->LootIDs && A->Polarity==B->Polarity);
    W.Step(.65f);
    TestEqual(TEXT("A regular chest still reaches card selection"),A->Stage,EMCRewardChestStage::Open);
    TestEqual(TEXT("A regular chest still offers three cards"),A->LootIDs.Num(),3);
    TestFalse(TEXT("Regular opening never captures its opener"),Opener->IsMimicCaptured());
    TestFalse(TEXT("Another player cannot select this opener's card"),A->TryChooseCard(Other,0));
    const FName Card=A->LootIDs[0]; const int32 Before=Opener->GetPlayerState<AMCPlayerState>()->Perks->GetStacks(Card);
    TestTrue(TEXT("The opener can collect the original reward card"),A->TryChooseCard(Opener,0));
    TestEqual(TEXT("Card selection grants exactly one stack"),Opener->GetPlayerState<AMCPlayerState>()->Perks->GetStacks(Card),Before+1);
    TestFalse(TEXT("A selected reward cannot grant again"),A->TryChooseCard(Opener,0));
    TestTrue(TEXT("Selection releases the ordinary interaction"),!Opener->RewardInteraction && Opener->CanWork());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCMimicCapture,"MessControl.Rewards.Mimic.CaptureAndActionGuards",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCMimicCapture::RunTest(const FString&)
{
    FMimicWorld W; auto* Chest=W.Chest(1);
    auto* Opener=W.Worker(W.Front(Chest)); auto* Teammate=W.Worker(W.Front(Chest,140));
    const FVector ActorScale=Opener->GetActorScale3D();
    if(!W.Open(*this,Chest,Opener,TEXT("A mimic uses the real lockpicking entry point"))) return false;
    W.Step(.45f);
    TestEqual(TEXT("Opening begins the swallowing animation"),Chest->Stage,EMCRewardChestStage::MimicSwallowing);
    TestTrue(TEXT("The chest and opener reference the same captive"),Chest->GetCapturedPlayer()==Opener && Opener->MimicCaptor==Chest);
    TestTrue(TEXT("A captured player remains alive"),Opener->Status->IsAlive());
    TestFalse(TEXT("A captured player cannot act"),Opener->CanWork());
    TestFalse(TEXT("A second player cannot open an occupied mimic"),Chest->BeginLockpicking(Teammate));
    TestFalse(TEXT("A captive cannot extract a reward card"),Chest->TryChooseCard(Opener,0));
    TestTrue(TEXT("Captured movement and capsule collision are disabled"),Opener->GetCharacterMovement()->MovementMode==MOVE_None && Opener->GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
    Opener->SetPrimaryInputHeld(true); Opener->SetHandleInputHeld(true); Opener->SetBraceInputHeld(true); Opener->SetJumpInputHeld(true);
    TestFalse(TEXT("Captured input cannot start a grip or primary action"),Opener->Grip->Brace.bHeld || Opener->IsPrimaryHeld() || Opener->bHandling || Opener->bBrushing);
    W.Step(.95f);
    TestEqual(TEXT("The gulp completes into occupied state"),Chest->Stage,EMCRewardChestStage::MimicOccupied);
    TestTrue(TEXT("Only the mesh disappears while swallowed"),!Opener->GetMesh()->IsVisible() && Opener->GetActorScale3D().Equals(ActorScale));
    TestFalse(TEXT("The captive cannot rescue itself"),Chest->CanRescue(Opener));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCMimicRescue,"MessControl.Rewards.Mimic.TeammateHeldRescueAndDistance",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCMimicRescue::RunTest(const FString&)
{
    FMimicWorld W; auto* Chest=W.Chest(1);
    auto* Captive=W.Worker(W.Front(Chest)); auto* Helper=W.Worker(W.Front(Chest,140));
    if(!W.Open(*this,Chest,Captive,TEXT("The opener can trigger the mimic"))) return false;
    W.Step(1.4f);
    if(!TestTrue(TEXT("A living reachable teammate can rescue the captive"),Chest->CanRescue(Helper))) return false;
    auto* Unowned=W.World->SpawnActor<AMCToothCharacter>(W.Front(Chest,-140),FRotator::ZeroRotator);
    TestFalse(TEXT("An unowned pawn cannot impersonate a teammate rescuer"),Chest->CanRescue(Unowned)); Unowned->Destroy();
    W.Step(.3f);
    TestTrue(TEXT("Nearby teammates do not release the captive without held input"),Captive->IsMimicCaptured() && Chest->RescueProgress()==0);
    Helper->SetHandleInputHeld(true); W.Step(.8f);
    TestTrue(TEXT("Held E uses the validated teammate rescue path"),Chest->RescuePlayer==Helper && Helper->MimicRescueTarget==Chest && Chest->RescueProgress()>.1f && Chest->RescueProgress()<1);
    Helper->SetHandleInputHeld(false); W.Step(.1f);
    TestTrue(TEXT("Releasing E cancels progress without releasing the captive"),!Chest->RescuePlayer && !Helper->MimicRescueTarget && Chest->RescueProgress()==0 && Captive->IsMimicCaptured());
    Helper->SetHandleInputHeld(true); W.Step(.3f); W.Move(Helper,W.Front(Chest,1000)); W.Step(.2f);
    TestTrue(TEXT("Leaving rescue reach cancels the held interaction"),!Chest->RescuePlayer && Chest->RescueProgress()==0 && Captive->IsMimicCaptured());
    Helper->SetHandleInputHeld(false); W.Move(Helper,W.Front(Chest,140)); Helper->SetHandleInputHeld(true);
    W.Step(Chest->RescueSeconds+.3f); Helper->SetHandleInputHeld(false);
    TestTrue(TEXT("A complete teammate hold releases the captive"),!Captive->IsMimicCaptured() && !Chest->GetCapturedPlayer() && !Helper->MimicRescueTarget);
    TestTrue(TEXT("Cooperative rescue restores movement, collision and work"),Captive->CanWork() && Captive->GetCharacterMovement()->MovementMode!=MOVE_None && Captive->GetCapsuleComponent()->GetCollisionEnabled()!=ECollisionEnabled::NoCollision && Captive->GetMesh()->IsVisible());
    TestEqual(TEXT("The rescued mimic cannot immediately swallow a second player"),Chest->Stage,EMCRewardChestStage::Exhausted);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCMimicCleanup,"MessControl.Rewards.Mimic.ResetAndDestroyRestoreCaptive",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCMimicCleanup::RunTest(const FString&)
{
    FMimicWorld W; auto* Chest=W.Chest(1); auto* Hero=W.Worker(W.Front(Chest));
    const FTransform Mesh=Hero->GetMesh()->GetRelativeTransform(); const FVector Scale=Hero->GetActorScale3D();
    const auto Collision=Hero->GetCapsuleComponent()->GetCollisionEnabled();
    if(!W.Open(*this,Chest,Hero,TEXT("The placed mimic can be opened"))) return false;
    W.Step(1.4f); Chest->ResetPlacedReward(); W.Step(.05f);
    TestTrue(TEXT("Reset releases the placed mimic's captive"),!Hero->MimicCaptor && !Chest->GetCapturedPlayer() && Hero->CanWork());
    TestTrue(TEXT("Reset restores the original mesh transform and capsule collision"),Hero->GetMesh()->GetRelativeTransform().Equals(Mesh,.001f) && Hero->GetActorScale3D().Equals(Scale) && Hero->GetCapsuleComponent()->GetCollisionEnabled()==Collision);
    TestEqual(TEXT("Reset returns the placed reward to landed state"),Chest->Stage,EMCRewardChestStage::Landed);
    W.Move(Hero,W.Front(Chest));
    if(!W.Open(*this,Chest,Hero,TEXT("The reset mimic can be opened again"))) return false;
    W.Step(1.4f); Chest->Destroy(); W.Step(.05f);
    TestTrue(TEXT("Destroying the chest restores its captive without a stale actor reference"),!Hero->MimicCaptor && Hero->CanWork() && Hero->GetMesh()->IsVisible());
    TestTrue(TEXT("Destruction restores movement, collision and mesh scale"),Hero->GetCharacterMovement()->MovementMode!=MOVE_None && Hero->GetCapsuleComponent()->GetCollisionEnabled()==Collision && Hero->GetMesh()->GetRelativeTransform().Equals(Mesh,.001f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCMimicRunCleanup,"MessControl.Rewards.Mimic.RunEndReleasesCaptive",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCMimicRunCleanup::RunTest(const FString&)
{
    FMimicWorld W; auto* Chest=W.Chest(1); auto* Hero=W.Worker(W.Front(Chest));
    if(!W.Open(*this,Chest,Hero,TEXT("The active run allows mimic capture"))) return false;
    W.Step(1.4f);
    if(!TestTrue(TEXT("The player is held during the active run"),Hero->IsMimicCaptured())) return false;
    W.World->GetGameState<AMCGameState>()->Phase=EMCShiftPhase::Won; W.Step(.1f);
    TestTrue(TEXT("Winning releases the captive before teammates lose the ability to rescue"),!Hero->MimicCaptor && !Chest->GetCapturedPlayer() && Hero->GetMesh()->IsVisible() && Hero->GetCapsuleComponent()->GetCollisionEnabled()!=ECollisionEnabled::NoCollision);
    W.World->GetGameState<AMCGameState>()->Phase=EMCShiftPhase::Working; W.Step(.1f);
    TestTrue(TEXT("The next active state has normal movement and actions"),Hero->CanWork() && Hero->GetCharacterMovement()->MovementMode!=MOVE_None);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCMimicCamera,"MessControl.Rewards.Mimic.CameraCollisionAndRestore",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCMimicCamera::RunTest(const FString&)
{
    FMimicWorld W; auto* Chest=W.Chest(1); auto* Hero=W.Worker(W.Front(Chest));
    // A tall real collision volume makes a camera ray through this chest
    // observably blocked, independently of the render mesh or NullRHI.
    Chest->Solid->SetBoxExtent(FVector(90,150,300)); Chest->Solid->SetRelativeLocation(FVector(0,0,300));
    Chest->Solid->SetCollisionResponseToChannel(ECC_Camera,ECR_Block);
    Hero->bCameraWallReveal=false; Hero->bManualCameraOrbit=true;
    Hero->CameraOrbitYaw=0; Hero->CameraOrbitPitch=-20; Hero->CameraOrbitDistance=900;
    const FVector BoomScale=Hero->CameraBoom->GetComponentScale();
    if(!W.Open(*this,Chest,Hero,TEXT("The camera fixture enters through real mimic lockpicking"))) return false;
    W.Step(1.4f);
    auto* PC=CastChecked<AMCPlayerController>(Hero->GetController());
    // Designate the already spawned controller as the local viewer after the
    // opening RPCs: this test has no ULocalPlayer or widget viewport.
    PC->SetAsLocalPlayerController(); PC->SetViewTarget(Hero); PC->SetActorTickEnabled(false);
    W.Step(1.f);
    auto* Arm=CastChecked<UMCOrbitSpringArmComponent>(Hero->CameraBoom);
    const FVector Focus=Hero->GetCameraFocusLocation();
    TestTrue(TEXT("The swallowed player's camera follows the chest instead of the gulped pawn"),Focus.Equals(Chest->GetMimicCaptureLocation()+FVector(0,0,20),.1f));
    TestTrue(TEXT("Only the captive's chest is ignored by the orbit camera"),Arm->GetIgnoredViewActor()==Chest);
    TestTrue(TEXT("The camera keeps the player's orbit distance and boom scale"),FMath::IsNearlyEqual(Hero->CameraOrbitDistance,900.f,.1f) && Arm->GetComponentScale().Equals(BoomScale));
    TestTrue(TEXT("The real camera sphere sweep can leave the captor's collision volume"),FVector::Dist(Focus,Hero->Camera->GetComponentLocation())>750);
    auto* Wall=W.Box(Focus-Arm->GetComponentRotation().Vector()*400,FVector(40)); W.Step(.2f);
    TestTrue(TEXT("Ignoring the mimic still retracts before other world geometry"),FVector::Dist(Hero->GetCameraFocusLocation(),Hero->Camera->GetComponentLocation())<500);
    Wall->Destroy(); W.Step(1.f);
    TestTrue(TEXT("Removing the other obstacle restores the occupied orbit distance"),FVector::Dist(Hero->GetCameraFocusLocation(),Hero->Camera->GetComponentLocation())>750);
    Chest->ResetPlacedReward(); W.Step(.5f);
    TestTrue(TEXT("Release restores the ordinary pawn camera focus"),!Hero->MimicCaptor && Hero->GetCameraFocusLocation().Equals(Hero->GetActorLocation()+FVector(0,0,30),.1f));
    TestNull(TEXT("Release removes the temporary chest collision exception"),Arm->GetIgnoredViewActor());
    TestTrue(TEXT("Normal camera collision again blocks the retained chest volume"),Arm->IsCollisionFixApplied());
    TestTrue(TEXT("Release preserves orbit controls and boom scale"),FMath::IsNearlyEqual(Hero->CameraOrbitDistance,900.f,.1f) && Arm->GetComponentScale().Equals(BoomScale));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCMimicPresentation,"MessControl.Rewards.Mimic.RevealedEffectsAndReset",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCMimicPresentation::RunTest(const FString&)
{
    FMimicWorld W; auto* Chest=W.Chest(0,6183,FVector::ZeroVector,true,true);
    auto* Rays=FindObject<UStaticMeshComponent>(Chest,TEXT("RewardGlowRays"));
    auto* Halo=FindObject<UStaticMeshComponent>(Chest,TEXT("RewardFloorHalo"));
    if(!TestNotNull(TEXT("The fixture includes an authored visible effect"),Rays)
        || !TestNotNull(TEXT("The fixture includes an authored hidden effect"),Halo)) return false;
    TestTrue(TEXT("Ordinary rewards keep their authored glow switches"),Rays->GetVisibleFlag() && !Halo->GetVisibleFlag());
    Chest->ForceMimicForTest();
    TestTrue(TEXT("A hidden mimic preserves the ordinary reward disguise"),Rays->GetVisibleFlag() && !Halo->GetVisibleFlag());
    auto* Mouth=Cast<UMaterialInstanceDynamic>(Chest->MimicMouth->GetMaterial(0));
    if(TestNotNull(TEXT("The primitive mouth has its own opaque material instance"),Mouth)) {
        TestEqual(TEXT("The mouth does not reuse the masked character face shader"),Mouth->GetBlendMode(),BLEND_Opaque);
        const FLinearColor Color=Mouth->K2_GetVectorParameterValue(TEXT("Color"));
        TestTrue(TEXT("The mouth interior has a dark color"),Color.R<.03f && Color.G<.01f && Color.B<.01f);
    }
    auto* Hero=W.Worker(W.Front(Chest)); Chest->OpeningSeconds=.7f;
    if(!W.Open(*this,Chest,Hero,TEXT("The presentation fixture starts through native lockpicking"))) return false;
    W.Step(.2f);
    TestEqual(TEXT("The first reveal occurs during opening"),Chest->Stage,EMCRewardChestStage::Opening);
    TestTrue(TEXT("Revealed teeth and mouth replace reward rays"),!Rays->GetVisibleFlag() && !Halo->GetVisibleFlag() && Chest->MimicMouth->GetVisibleFlag() && Chest->MimicLowerTeeth->GetVisibleFlag());
    W.Move(Hero,W.Front(Chest,1000)); W.Step(.3f);
    TestEqual(TEXT("Leaving before the gulp cancels the native opening"),Chest->Stage,EMCRewardChestStage::Landed);
    TestTrue(TEXT("Cancellation restores authored visible and hidden effects"),Rays->GetVisibleFlag() && !Halo->GetVisibleFlag() && !Chest->MimicMouth->GetVisibleFlag());
    W.Move(Hero,W.Front(Chest)); Chest->OpeningSeconds=.1f;
    if(!W.Open(*this,Chest,Hero,TEXT("The cancelled mimic can be opened again"))) return false;
    W.Step(1.4f);
    TestEqual(TEXT("The presentation fixture holds a real captive"),Chest->Stage,EMCRewardChestStage::MimicOccupied);
    TestTrue(TEXT("Occupied mimics retain suppression after multiple native ticks"),!Rays->GetVisibleFlag() && !Halo->GetVisibleFlag() && Chest->MimicMouth->GetVisibleFlag());
    Chest->ResetPlacedReward(); W.Step(.05f);
    TestTrue(TEXT("Reset releases the captive and restores each authored glow switch"),!Hero->IsMimicCaptured() && Rays->GetVisibleFlag() && !Halo->GetVisibleFlag() && !Chest->MimicMouth->GetVisibleFlag());
    Chest->ForceMimicForTest(false);
    TestTrue(TEXT("Forcing an ordinary reward preserves the original effect switches"),!Chest->bMimic && Rays->GetVisibleFlag() && !Halo->GetVisibleFlag() && !Chest->MimicMouth->GetVisibleFlag());
    return true;
}
#endif
