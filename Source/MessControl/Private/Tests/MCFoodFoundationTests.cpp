#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCGripComponent.h"
#include "MCToothCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodInteractionGenerationTest,"MessControl.Network.FoodFoundation.AcceptedGeneration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodInteractionGenerationTest::RunTest(const FString&)
{
    FMCFoodInteractionState Old;Old.LoadEpoch=FGuid::NewGuid();Old.Revision=8;Old.Phase=EMCFoodPhase::Free;
    auto Next=Old;Next.Carry.Location=FVector(17,3,2);
    TestTrue(TEXT("Pose may advance within one accepted generation"),Next.CanAdvanceFrom(Old,Old.LoadEpoch));
    Next.Phase=EMCFoodPhase::Disposed;
    TestFalse(TEXT("Same revision cannot replace interaction ownership"),Next.CanAdvanceFrom(Old,Old.LoadEpoch));
    Next.Revision=9;TestTrue(TEXT("New release generation replaces carry"),Next.CanAdvanceFrom(Old,Old.LoadEpoch));
    TestFalse(TEXT("Late interaction cannot resurrect after release"),Old.CanAdvanceFrom(Next,Next.LoadEpoch));
    const FGuid NewEpoch=FGuid::NewGuid();Next.LoadEpoch=NewEpoch;Next.Revision=1;
    TestTrue(TEXT("Published new epoch may restart revisions"),Next.CanAdvanceFrom(Old,NewEpoch));
    TestFalse(TEXT("Untrusted epoch cannot replace current owner"),Next.CanAdvanceFrom(Old,Old.LoadEpoch));
    Next.LoadEpoch=Old.LoadEpoch;Next.Revision=9;Next.bRequiresCarrier=true;
    TestFalse(TEXT("Unmapped carrier is deferred rather than accepted as a release"),Next.CanAdvanceFrom(Old,Old.LoadEpoch));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodStableHandlingTest,"MessControl.Network.FoodFoundation.StableCanonicalLoad",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodStableHandlingTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);auto* Package=World->GetOutermost();
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));World->SetGameMode(URL);
    World->InitializeActorsForPlay(URL);World->BeginPlay();
    auto* Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,10000),FRotator::ZeroRotator);
    Hero->CaptureNetworkCorrectionContext();
    auto* A=World->SpawnActor<AMCFoodActor>(FVector(300,0,10000),FRotator::ZeroRotator);
    auto* B=World->SpawnActor<AMCFoodActor>(FVector(600,0,10000),FRotator::ZeroRotator);
    A->Settings.Mass=3;B->Settings.Mass=5;A->StackCarrier=Hero;B->StackCarrier=Hero;
    Hero->FoodCollection->Pieces.Add(A);Hero->FoodCollection->Pieces.Add(B);
    Hero->Grip->Frame.Food=A;Hero->Grip->Secondary.Food=A;
    const auto Before=Hero->Grip->CaptureHandlingForMove();
    TestEqual(TEXT("Canonical stack includes both masses exactly once across duplicate slots"),Before.LoadMass,8.f);
    TestEqual(TEXT("Stack subset is measured once"),Before.StackMass,8.f);
    const FTransform Canonical=A->GetCanonicalModelTransform(),BodyPose=A->GetActorTransform(),HeroPose=Hero->GetActorTransform();
    A->PresentationRoot->SetRelativeLocation(FVector(220,-70,30));A->Visual->SetRelativeScale3D(FVector(2,.5,3));
    A->Body->SetPhysicsLinearVelocity(FVector(2500,-1500,800));
    const auto After=Hero->Grip->CaptureHandlingForMove();
    TestTrue(TEXT("Visual jitter and body velocity do not revise accepted load"),Before.Equals(After));
    TestTrue(TEXT("Decorative frame moves independently of canonical body/model"),!A->GetPresentedActorTransform().Equals(BodyPose)
        && A->GetActorTransform().Equals(BodyPose) && A->GetCanonicalModelTransform().Equals(Canonical));
    TestTrue(TEXT("Manipulated food cannot inject acceleration"),Hero->Grip->ReactionAcceleration().IsZero());
    const FVector V(200,-120,7);TestEqual(TEXT("Food oscillation cannot constrain voluntary velocity"),Hero->Grip->ConstrainGripVelocity(V,.016f),V);
    TestTrue(TEXT("Presentation writes leave avatar transform unchanged"),Hero->GetActorTransform().Equals(HeroPose));
    Hero->FoodCollection->Pieces.Reset();A->StackCarrier=nullptr;B->StackCarrier=nullptr;
    Hero->Grip->Frame=FMCGripFrame();Hero->Grip->Secondary=FMCGripFrame();
    const auto Released=Hero->Grip->CaptureHandlingForMove();
    TestEqual(TEXT("Release removes canonical load"),Released.LoadMass,0.f);
    TestTrue(TEXT("Release creates a new accepted handling revision"),Released.Revision>Before.Revision);
    World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    if(IsValid(Package) && Package->GetName().StartsWith(TEXT("/Temp/"))) Package->SetDirtyFlag(false);
    return true;
}
#endif
