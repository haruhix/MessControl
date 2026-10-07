#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCFoodCollectionComponent.h"
#include "MCTongue.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace
{
struct FYawnWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Hero=nullptr;
    AMCTongue* Tongue=nullptr;
    UMCToothMovementComponent* Move=nullptr;
    APlayerController* Controller=nullptr;
    FYawnWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* Floor=World->SpawnActor<AActor>();auto* Shape=NewObject<UBoxComponent>(Floor);
        Floor->SetRootComponent(Shape);Shape->SetBoxExtent(FVector(5000,5000,20));
        Shape->SetCollisionProfileName(TEXT("BlockAll"));Shape->RegisterComponent();Floor->SetActorLocation(FVector(0,0,10000));
        Tongue=World->SpawnActor<AMCTongue>(FVector(15000,0,10000),FRotator::ZeroRotator);
        Tongue->bAutomaticYawns=false;Tongue->SetActorTickEnabled(false);Tongue->SetActorEnableCollision(false);
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,10080),FRotator::ZeroRotator);
        Move=CastChecked<UMCToothMovementComponent>(Hero->GetCharacterMovement());
        Controller=World->SpawnActor<APlayerController>();Controller->SetAsLocalPlayerController();Controller->Possess(Hero);
        Step(.15f);
    }
    ~FYawnWorld() {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    void Step(float Seconds,float Dt=1.f/60,FVector Input=FVector::ZeroVector)
    {
        for(int32 I=0;I<FMath::CeilToInt(Seconds/Dt);++I) {
            if(!Input.IsNearlyZero()) Hero->AddMovementInput(Input);
            ++GFrameCounter;World->Tick(LEVELTICK_All,Dt);
        }
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCYawnWindControl,"MessControl.Yawn.AirCurrentPreservesInputAndExpires",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCYawnWindControl::RunTest(const FString&)
{
    for(float Dt:{1.f/30,1.f/60,1.f/120}) {
        FYawnWorld T;
        if(!TestTrue(TEXT("Fixture has a local actionable character on its collision floor"),T.Hero->CanWork() && T.Move->IsMovingOnGround())) return false;
        T.Hero->bBrushing=true;T.Hero->FoodCollection->Toggle();
        T.Move->Velocity=FVector(17,9,0);const FVector BeforeVelocity=T.Move->Velocity;
        T.Hero->BeginYawn(T.Tongue,3);
        TestTrue(TEXT("Onset keeps the player's work, collection and current velocity"),T.Hero->CanWork() && T.Hero->bBrushing && T.Hero->FoodCollection->bCollecting && T.Move->Velocity.Equals(BeforeVelocity,.01));
        TestTrue(TEXT("Yawn never freezes movement, ignores input or plants an automatic anchor"),T.Move->IsMovingOnGround() && !T.Controller->IsMoveInputIgnored() && T.Hero->YawnAnchor.IsNearlyZero());
        TestTrue(TEXT("A fresh inhalation begins at zero wind strength"),T.Hero->YawnWindVelocity().IsNearlyZero());
        T.Hero->bBrushing=false;T.Hero->FoodCollection->Stop();T.Move->StopMovementImmediately();
        T.Step(.7f,Dt);const FVector Wind=T.Hero->YawnWindVelocity(),Direction=Wind.GetSafeNormal();
        TestTrue(TEXT("After onset the yawn has a finite horizontal current"),Wind.Size2D()>200 && !Wind.ContainsNaN() && FMath::IsNearlyZero(Wind.Z));
        const FVector IdleStart=T.Hero->GetActorLocation();T.Step(.45f,Dt);
        const float IdleDistance=FVector::DotProduct(T.Hero->GetActorLocation()-IdleStart,Direction);
        TestTrue(*FString::Printf(TEXT("Idle character drifts downwind at %.0f FPS (%.1f cm)"),1/Dt,IdleDistance),IdleDistance>40);
        T.Move->SetSprinting(true);const FVector RunStart=T.Hero->GetActorLocation();T.Step(.9f,Dt,-Direction);
        const float RunDistance=FVector::DotProduct(T.Hero->GetActorLocation()-RunStart,Direction);
        TestTrue(*FString::Printf(TEXT("Opposing sprint wins ground against the same current at %.0f FPS (%.1f cm)"),1/Dt,RunDistance),T.Move->bSprintActive && RunDistance<-40);
        TestTrue(TEXT("Movement intent still follows the player's opposing input"),FVector::DotProduct(T.Move->Intent(),-Direction)>.9);
        T.Move->SetSprinting(false);T.Step(1.4f,Dt);
        TestTrue(TEXT("Countdown ends the wind, animation and source reference"),!T.Hero->IsYawning() && T.Hero->YawnWindVelocity().IsNearlyZero() && T.Hero->YawnPoseAlpha()==0 && !T.Hero->YawnTongue);
        TestTrue(TEXT("Expired current leaves no root motion source or idle drift"),!T.Move->GetRootMotionSource(TEXT("MCThroatAmbientSuction")).IsValid() && T.Hero->GetVelocity().Size2D()<1);
        TestTrue(TEXT("Normal control survives the full yawn lifecycle"),T.Hero->CanWork() && T.Move->IsMovingOnGround() && !T.Controller->IsMoveInputIgnored());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCYawnWindMoveSnapshot,"MessControl.Yawn.WindUsesMovementSnapshotAndServerDeadline",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCYawnWindMoveSnapshot::RunTest(const FString&)
{
    FYawnWorld T;T.Hero->BeginYawn(T.Tongue,3);T.Step(.7f);
    const FVector Sample=T.Move->CaptureSuctionForMove();
    TestTrue(TEXT("Predicted movement captures the active yawn in its existing suction sample"),Sample.Equals(T.Hero->YawnWindVelocity(),.01) && Sample.Size2D()>200);
    T.Hero->YawnEndsAt=T.World->GetTimeSeconds()-.01;
    TestTrue(TEXT("A stale event expires by server time before its stopping update arrives"),T.Hero->YawnWindVelocity().IsNearlyZero() && T.Hero->YawnPoseAlpha()==0 && T.Move->CaptureSuctionForMove().IsNearlyZero());
    // A correction replays an old move's force even after the live event ends.
    T.Move->RestoreSuctionForMove(Sample);T.Move->PerformMovement(1.f/60);
    auto Source=T.Move->GetRootMotionSource(TEXT("MCThroatAmbientSuction"));
    TestTrue(TEXT("Replay restores the saved current rather than resampling the expired event"),Source.IsValid() && static_cast<FMCLocomotionRootMotionSource*>(Source.Get())->Force.Equals(Sample,.01));
    T.Hero->UpdateYawn(0);T.Step(.4f);
    TestTrue(TEXT("The next live movement removes the replayed source and clears the old pose"),!T.Move->GetRootMotionSource(TEXT("MCThroatAmbientSuction")).IsValid() && T.Hero->YawnPoseAlpha()==0 && !T.Hero->YawnTongue);
    return true;
}
#endif
