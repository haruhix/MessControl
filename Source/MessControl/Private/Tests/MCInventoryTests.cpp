#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "MCToothStatusComponent.h"
#include "MCGripComponent.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCFoodActor.h"
#include "MCMouthSurface.h"
#include "MCThroat.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "MCToothAnimInstance.h"
#include "Engine/SkeletalMesh.h"

namespace {
struct FInventoryWorld {
    UWorld* W; AMCGameState* GS; AMCToothCharacter* H;
    FInventoryWorld() {
        W=UWorld::CreateWorld(EWorldType::Game,false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
        W->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL; URL.AddOption(TEXT("game=/Script/MessControl.MCGameMode"));
        W->SetGameMode(URL); W->InitializeActorsForPlay(URL); W->BeginPlay();
        GS=W->GetGameState<AMCGameState>(); GS->Phase=EMCShiftPhase::Working; GS->bDevManualEvents=true;
        for(TActorIterator<AMCThroat> It(W);It;++It) { It->SetActorEnableCollision(false); It->SetActorTickEnabled(false); }
        H=W->SpawnActor<AMCToothCharacter>(FVector(-600,0,98),FRotator::ZeroRotator);
        H->GetCharacterMovement()->DisableMovement();
    }
    ~FInventoryWorld() { W->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(W); W->DestroyWorld(false); }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCInventoryRouting,"MessControl.Inventory.SelectionAndMaterial",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCInventoryRouting::RunTest(const FString&) {
    FInventoryWorld T; auto* I=T.H->Inventory.Get();
    auto* Hard=T.W->SpawnActor<AMCFoodActor>(FVector(-480,0,60),FRotator::ZeroRotator); Hard->FoodData.Resistance=EMCFoodResistance::Hard;
    auto* Soft=T.W->SpawnActor<AMCFoodActor>(FVector(-480,150,60),FRotator::ZeroRotator); Soft->FoodData.Resistance=EMCFoodResistance::Soft;
    TestTrue(TEXT("Default slot is the cleaning tool"),T.H->HasBrush());
    T.H->bBrushing=true; I->ServerSelect(EMCToolSlot::Pickaxe);
    TestFalse(TEXT("Changing slot cancels the cleaning contact"),T.H->bBrushing || T.H->HasBrush());
    TestTrue(TEXT("Pickaxe accepts hard food"),I->CanBreak(Hard)); TestFalse(TEXT("Pickaxe rejects soft food"),I->CanBreak(Soft));
    I->ServerSelect(EMCToolSlot::Knife); TestTrue(TEXT("Knife accepts soft food"),I->CanBreak(Soft)); TestFalse(TEXT("Knife rejects hard food"),I->CanBreak(Hard));
    I->ServerSelect(static_cast<EMCToolSlot>(99)); TestEqual(TEXT("Invalid slot ignored"),I->Selected,EMCToolSlot::Knife);
    T.H->SwingBrush(); I->ServerSelect(EMCToolSlot::Spray); TestEqual(TEXT("Cannot swap the damage tool during a swing"),I->Selected,EMCToolSlot::Knife);
    TestTrue(TEXT("Pickaxe lifts overhead then swings down"),UMCInventoryComponent::SwingAngle(EMCToolSlot::Pickaxe,.30f)>100 && UMCInventoryComponent::SwingAngle(EMCToolSlot::Pickaxe,.44f)<-100);
    I->UnlockWaterJet(); TestTrue(TEXT("Upgrade is retained in slot one"),I->bWaterJetUnlocked);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCInvisiblePickaxe,"MessControl.Inventory.HiddenPickaxePreservesHandContacts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCInvisiblePickaxe::RunTest(const FString&) {
    FInventoryWorld T; auto* I=T.H->Inventory.Get(); I->ServerSelect(EMCToolSlot::Pickaxe); I->TickComponent(1.f/60,LEVELTICK_All,nullptr);
    UStaticMeshComponent* Tool=nullptr;TArray<UStaticMeshComponent*> Parts;T.H->GetComponents(Parts);
    for(auto* Part:Parts) if(Part->GetFName()==TEXT("InventoryTool")) Tool=Part;
    if(!TestNotNull(TEXT("The selected pickaxe has a real presentation mesh"),Tool) || !TestNotNull(TEXT("Pickaxe mesh is loaded"),Tool->GetStaticMesh().Get())) return false;
    auto* Floor=T.W->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Box);Box->SetBoxExtent(FVector(500,500,10));Box->SetCollisionProfileName(TEXT("BlockAll"));Box->RegisterComponent();
    Floor->SetActorLocation(T.H->GetActorLocation()-FVector(0,0,100));
    FTransform Wrist=T.H->GetMesh()->GetSocketTransform(T.H->RigBone(TEXT("hand_r")));
    const auto Bounds=Tool->GetStaticMesh()->GetBounds();const FTransform Presented=Tool->GetRelativeTransform()*T.H->BrushPivot->GetRelativeTransform()*Wrist;
    double Highest=-MAX_flt;
    for(int32 Corner=0;Corner<8;++Corner) Highest=FMath::Max(Highest,Presented.TransformPosition(Bounds.Origin+FVector(Corner&1?Bounds.BoxExtent.X:-Bounds.BoxExtent.X,Corner&2?Bounds.BoxExtent.Y:-Bounds.BoxExtent.Y,Corner&4?Bounds.BoxExtent.Z:-Bounds.BoxExtent.Z)).Z);
    Wrist.AddToTranslation(FVector(0,0,Floor->GetActorLocation().Z+10-Highest-20));
    TestTrue(TEXT("An exposed pickaxe corrects a wrist pose whose mesh penetrates the floor"),I->ShouldPresentTool() && !I->ConstrainPickaxeGrip(Wrist).IsNearlyZero());
    auto CheckHidden=[&](const TCHAR* Context) {
        TestFalse(*FString::Printf(TEXT("%s hides the selected pickaxe"),Context),I->ShouldPresentTool());
        TestTrue(*FString::Printf(TEXT("%s leaves the existing hand contact unchanged despite the same floor penetration"),Context),I->ConstrainPickaxeGrip(Wrist).IsNearlyZero());
    };
    // The movement mode changes before its cosmetic animation alpha arrives.
    T.H->AnimationSwim=0;T.H->GetCharacterMovement()->SetMovementMode(MOVE_Swimming);CheckHidden(TEXT("Swimming before the animation blend"));
    T.H->GetCharacterMovement()->DisableMovement();
    auto* Food=T.W->SpawnActor<AMCFoodActor>(T.H->GetActorLocation()+FVector(80,0,0),FRotator::ZeroRotator);
    T.H->HeldFood=Food;CheckHidden(TEXT("Carrying food"));T.H->HeldFood=nullptr;
    T.H->Grip->Frame.Food=Food;CheckHidden(TEXT("A reaching grip before its blend"));T.H->Grip->Frame.Food=nullptr;
    T.H->AnimationOrderFlight=1;CheckHidden(TEXT("Flight to the uvula"));T.H->AnimationOrderFlight=0;
    TestTrue(TEXT("Dropping the object and leaving traversal restores pickaxe presentation"),I->ShouldPresentTool() && !I->ConstrainPickaxeGrip(Wrist).IsNearlyZero());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSprayProtection,"MessControl.Inventory.HoldSprayAndPreserveProgress",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSprayProtection::RunTest(const FString&) {
    FInventoryWorld T; auto* I=T.H->Inventory.Get(); I->ServerSelect(EMCToolSlot::Spray);
    auto* Patch=T.W->SpawnActor<AMCMouthSurface>(FVector(-470,0,25),FRotator::ZeroRotator); Patch->bUlcer=true;
    Patch->HealSeconds=7;
    auto Tick=[&](int32 Count) { for(int32 N=0;N<Count;++N) { ++GFrameCounter; I->TickComponent(.1f,LEVELTICK_All,nullptr); } };
    T.H->ServerSetPrimary(true); Tick(20);
    TestTrue(TEXT("Two seconds of held spray heals two sevenths"),FMath::IsNearlyEqual(Patch->Healing,2.f/7,.0001f));
    TestTrue(TEXT("Treatment numbs the ulcer"),Patch->IsNumb());
    const float Saved=Patch->Healing,HP=T.GS->MouthHealth;
    for(int32 N=0;N<30;++N) I->ServerSpray();
    TestEqual(TEXT("Repeated requests cannot manufacture treatment time"),Patch->Healing,Saved);
    I->TickComponent(.1f,LEVELTICK_All,nullptr);
    TestEqual(TEXT("Duplicate ticks in one frame cannot accelerate treatment"),Patch->Healing,Saved);
    auto* Anim=Cast<UMCToothAnimInstance>(T.H->GetMesh()->GetAnimInstance());
    if(!TestNotNull(TEXT("Spray uses the production procedural animation instance"),Anim)) return false;
    const int32 Hand=T.H->GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton().FindBoneIndex(T.H->RigBone(TEXT("hand_r")));
    if(!TestTrue(TEXT("The real character rig has a right hand"),Hand!=INDEX_NONE)) return false;
    Anim->bRecordMotion=true;
    auto CaptureHand=[&]() {
        ++GFrameCounter;Anim->UpdateAnimation(.1f,false);
        return Anim->DiagnosticPose.IsValidIndex(Hand)?Anim->DiagnosticPose[Hand].GetLocation():FVector::ZeroVector;
    };
    // First build a live spray aim. Then mimic an old HealingTarget still on a
    // client when its movement/grip has already claimed the same right hand.
    for(int32 N=0;N<10;++N) CaptureHand();
    if(!TestTrue(TEXT("Production animation evaluates a real hand pose"),Anim->DiagnosticPose.IsValidIndex(Hand))) return false;
    const auto& Ref=T.H->GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();
    const int32 EyeL=Ref.FindBoneIndex(T.H->RigBone(TEXT("eye_l"))),EyeR=Ref.FindBoneIndex(T.H->RigBone(TEXT("eye_r")));
    if(!TestTrue(TEXT("The player rig has both eyes"),EyeL!=INDEX_NONE && EyeR!=INDEX_NONE)) return false;
    auto WorldHand=[&]() {return T.H->GetMesh()->GetComponentTransform().TransformPosition(Anim->DiagnosticPose[Hand].GetLocation());};
    auto CheckFacePose=[&](const TCHAR* Context) {
        const FVector Face=T.H->GetMesh()->GetComponentTransform().TransformPosition((Anim->DiagnosticPose[EyeL].GetLocation()+Anim->DiagnosticPose[EyeR].GetLocation())*.5);
        const FVector Delta=WorldHand()-Face;
        TestTrue(*FString::Printf(TEXT("%s keeps the hand at face height"),Context),FMath::Abs(Delta.Z)<8);
        TestTrue(*FString::Printf(TEXT("%s extends the hand in front of the face"),Context),FVector::DotProduct(Delta,T.H->GetActorForwardVector())>30);
    };
    CheckFacePose(TEXT("Targeted spray"));
    auto CheckHiddenSpray=[&](const TCHAR* Context) {
        I->HealingTarget=Patch;const FVector StaleTargetHand=CaptureHand();
        I->HealingTarget=nullptr;const FVector NoTargetHand=CaptureHand();
        TestTrue(*FString::Printf(TEXT("%s preserves the actual procedural hand pose despite a stale spray target"),Context),StaleTargetHand.Equals(NoTargetHand,.01f));
        I->HealingTarget=Patch;Tick(10);
        TestNull(*FString::Printf(TEXT("%s clears the authoritative treatment target"),Context),I->HealingTarget.Get());
        TestEqual(*FString::Printf(TEXT("%s keeps the existing ulcer progress"),Context),Patch->Healing,Saved);
        TestEqual(*FString::Printf(TEXT("%s keeps the selected spray slot"),Context),I->Selected,EMCToolSlot::Spray);
        TestTrue(*FString::Printf(TEXT("%s keeps held input for a later resume"),Context),T.H->IsPrimaryHeld());
    };
    T.H->GetCharacterMovement()->SetMovementMode(MOVE_Swimming);T.H->AnimationSwim=1;
    CheckHiddenSpray(TEXT("Swimming with a spray selected"));
    T.H->GetCharacterMovement()->SetMovementMode(MOVE_Custom,1);T.H->AnimationSwim=0;T.H->AnimationClimb=1;
    CheckHiddenSpray(TEXT("Climbing with a spray selected"));
    T.H->GetCharacterMovement()->DisableMovement();T.H->AnimationClimb=0;
    auto* Held=T.W->SpawnActor<AMCFoodActor>(T.H->GetActorLocation()+FVector(80,0,0),FRotator::ZeroRotator);
    // Use a known solid mesh: the prototype food now has mesh-shaped collision
    // and its bounds alone no longer guarantee an obstacle across this ray.
    FMCFoodRow Obstacle;Obstacle.WholeMeshes.Add(TSoftObjectPtr<UStaticMesh>(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"))));
    FRandomStream ObstacleRandom(41);Held->ConfigureItem(TEXT("SprayTestObstacle"),Obstacle,ObstacleRandom);
    Held->Body->SetSimulatePhysics(false);
    T.H->HeldFood=Held;CheckHiddenSpray(TEXT("Holding food with a spray selected"));
    Held->Release(T.H);
    TestNull(TEXT("Releasing food clears the actual carried object"),T.H->HeldFood.Get());
    TestTrue(TEXT("Leaving traversal and releasing food restores spray presentation"),I->ShouldPresentTool());
    Anim->bRecordMotion=false;I->ServerSpray();
    // A dropped object remains a real visibility obstacle. The fixture must
    // move it out of the treatment ray before expecting treatment to resume.
    TestNull(TEXT("Released food in front of the ulcer still blocks treatment"),I->HealingTarget.Get());
    TestEqual(TEXT("A released visibility obstacle cannot add treatment time"),Patch->Healing,Saved);
    Anim->bRecordMotion=true;
    for(int32 N=0;N<10;++N) CaptureHand();
    CheckFacePose(TEXT("Held spray without a target"));
    const FVector HeldPalm=WorldHand();
    T.H->ServerSetPrimary(false);
    for(int32 N=0;N<10;++N) CaptureHand();
    TestTrue(TEXT("Releasing untargeted spray lowers the hand"),HeldPalm.Z-WorldHand().Z>15);
    Anim->bRecordMotion=false;T.H->ServerSetPrimary(true);
    Held->SetActorLocation(T.H->GetActorLocation()+T.H->GetActorRightVector()*300,false,nullptr,ETeleportType::TeleportPhysics);
    I->ServerSpray();
    TestEqual(TEXT("Leaving traversal and moving dropped food aside reacquires the same ulcer"),I->HealingTarget.Get(),Patch);
    TestEqual(TEXT("Reacquiring a treatment target does not manufacture healing time"),Patch->Healing,Saved);
    Patch->Disturb(); Patch->Tick(.5f); TestEqual(TEXT("Protected lesion does no mouth damage"),T.GS->MouthHealth,HP);
    T.H->ServerSetPrimary(false); Tick(10); Patch->NumbUntil=-1; Patch->Disturb(); Patch->Tick(.5f);
    TestEqual(TEXT("Release, contact and idle time preserve progress"),Patch->Healing,Saved);
    TestTrue(TEXT("Damage returns while untreated"),T.GS->MouthHealth<HP);
    T.H->ServerSetPrimary(true); Patch->SetActorLocation(FVector(-1700,0,25)); Tick(10);
    TestEqual(TEXT("Distant ulcer cannot be treated"),Patch->Healing,Saved);
    Patch->SetActorLocation(FVector(-720,0,25)); Tick(10);
    TestEqual(TEXT("Ulcer behind player cannot be treated"),Patch->Healing,Saved);
    Patch->SetActorLocation(FVector(-470,0,25)); Tick(49);
    TestFalse(TEXT("Less than seven seconds is incomplete"),Patch->IsHealed());
    Tick(1); TestTrue(TEXT("Resumed treatment completes after seven effective seconds"),Patch->IsHealed());
    Tick(10); TestEqual(TEXT("Completed treatment stays at one"),Patch->Healing,1.f);
    return true;
}
#endif
