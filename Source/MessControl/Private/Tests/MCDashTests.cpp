#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCThroat.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

namespace
{
struct FDashWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Hero=nullptr;
    UMCToothMovementComponent* Move=nullptr;
    FDashWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        Box(FVector(0,0,10000),FVector(5000,5000,20));
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,10080),FRotator::ZeroRotator);
        Move=CastChecked<UMCToothMovementComponent>(Hero->GetCharacterMovement());
        auto* Controller=World->SpawnActor<APlayerController>();
        // No viewport/player is created in this headless test world. In 5.8 a
        // bare PC is considered remote when no net driver exists; designate the
        // fixture as local just as GameMode does for its standalone controller.
        Controller->SetAsLocalPlayerController();Controller->Possess(Hero);
        Step(.15f);
    }
    ~FDashWorld() { World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false); }
    void Box(FVector Location,FVector Extent)
    {
        auto* Actor=World->SpawnActor<AActor>();auto* Shape=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Shape);Shape->SetBoxExtent(Extent);Shape->SetCollisionProfileName(TEXT("BlockAll"));
        Shape->RegisterComponent();Actor->SetActorLocation(Location);
    }
    void Step(float Seconds,float Dt=1.f/60,bool Drive=false)
    {
        for(int32 I=0;I<FMath::CeilToInt(Seconds/Dt);++I) {
            if(Drive) Hero->AddMovementInput(FVector::ForwardVector);
            ++GFrameCounter;World->Tick(LEVELTICK_All,Dt);
        }
    }
    void Tap(float Dt=1.f/60) { Hero->SetSprintInputHeld(true);Step(Dt,Dt);Hero->SetSprintInputHeld(false);Step(Dt,Dt); }
    bool Ready() const { return Hero->IsLocallyControlled() && Hero->CanWork() && Move->IsMovingOnGround() && Move->CanDash(); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDashTapHold,"MessControl.Dash.PressHoldCooldownAndCancellation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDashTapHold::RunTest(const FString&)
{
    for(float Dt:{1.f/30,1.f/60,1.f/120}) {
        FDashWorld T;
        if(!TestTrue(TEXT("Fixture has a local actionable character standing on its collision floor"),T.Ready())) return false;
        const FVector Start=T.Hero->GetActorLocation();
        T.Hero->SetSprintInputHeld(true);T.Step(Dt,Dt);
        TestTrue(TEXT("Shift starts the predicted dash on the first movement frame while still held"),T.Hero->IsDashing());
        TestTrue(TEXT("Shift press immediately moves the capsule"),T.Hero->GetActorLocation().X-Start.X>5);
        T.Hero->SetSprintInputHeld(false);T.Step(Dt,Dt);
        TestTrue(TEXT("Dash has a measurable normalized animation phase"),T.Hero->GetDashProgress()>0 && T.Hero->GetDashProgress()<1);
        TestTrue(TEXT("Dash without direction uses the facing direction"),FVector::DotProduct(T.Hero->GetDashDirection(),FVector::ForwardVector)>.99);
        T.Step(.45f,Dt);
        const float Distance=T.Hero->GetActorLocation().X-Start.X;
        TestTrue(*FString::Printf(TEXT("Dash covers its short configured distance at %.0f FPS (%.1f cm)"),1/Dt,Distance),Distance>380 && Distance<510);
        TestFalse(TEXT("Dash exits after one dash pose"),T.Hero->IsDashing());
        T.Tap(Dt);TestFalse(TEXT("Repeated taps cannot bypass the cooldown"),T.Hero->IsDashing());
        T.Step(.8f,Dt);T.Tap(Dt);TestTrue(TEXT("Dash becomes available after cooldown"),T.Hero->IsDashing());
        T.Hero->CancelGameplayInput();TestFalse(TEXT("Cancel stops an active dash immediately"),T.Hero->IsDashing());
        T.Step(1.2f,Dt);T.Hero->SetSprintInputHeld(true);T.Step(.08f,Dt);
        T.Hero->CancelGameplayInput();T.Step(.05f,Dt);
        TestFalse(TEXT("Input cancellation stops the press dash without restarting it"),T.Hero->IsDashing());
        T.Hero->SetSprintInputHeld(true);T.Step(1.f,Dt,true);
        TestTrue(TEXT("Holding Shift transitions into sustained sprinting"),T.Move->bSprintActive && T.Hero->GetVelocity().Size2D()>500);
        T.Hero->SetSprintInputHeld(false);T.Step(Dt,Dt);
        TestFalse(TEXT("Releasing a long hold never triggers a dash"),T.Hero->IsDashing());
        TestFalse(TEXT("Releasing Shift clears sprint intent"),T.Move->WantsToSprint());
        T.Hero->SetSprintInputHeld(true);T.Hero->AddMovementInput(FVector::RightVector);T.Step(Dt,Dt);
        TestTrue(TEXT("Repressing Shift after sprint starts a new dash immediately"),T.Hero->IsDashing());
        TestTrue(TEXT("Dash uses this frame's direction even when input arrives after the press"),FVector::DotProduct(T.Hero->GetDashDirection(),FVector::RightVector)>.99);
        T.Hero->SetSprintInputHeld(false);
    }
    FDashWorld T;if(!TestTrue(TEXT("Press fixture starts with an available ground dash"),T.Ready())) return false;
    T.Hero->SetSprintInputHeld(true);T.Step(.25f,.25f);
    TestTrue(TEXT("A frame longer than the hold threshold still starts the dash on press"),T.Hero->IsDashing());
    T.Step(1.2f);
    TestFalse(TEXT("Holding past the cooldown does not automatically repeat the dash"),T.Hero->IsDashing());
    TestTrue(TEXT("The cooldown has elapsed before release"),T.Move->CanDash());
    T.Hero->SetSprintInputHeld(false);T.Step(1.f/60);
    TestFalse(TEXT("Release never starts a second dash after cooldown"),T.Hero->IsDashing());
    T.Hero->SetSprintInputHeld(true);T.Hero->CancelGameplayInput();T.Step(1.f/60);
    TestFalse(TEXT("Cancel before the next movement frame clears the queued dash"),T.Hero->IsDashing());
    T.Hero->SetSprintInputHeld(true);T.Hero->SetSprintInputHeld(false);T.Step(1.f/60);
    TestTrue(TEXT("Press and release within one frame still executes one dash"),T.Hero->IsDashing());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDashCollisionAndRestrictions,"MessControl.Dash.CollisionAndActionRestrictions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDashCollisionAndRestrictions::RunTest(const FString&)
{
    {
        FDashWorld T;if(!TestTrue(TEXT("Wall fixture starts with an available ground dash"),T.Ready())) return false;
        T.Box(FVector(180,0,10150),FVector(10,500,250));T.Tap();
        TestTrue(TEXT("Wall test actually executes a dash before testing its sweep"),T.Hero->IsDashing());T.Step(.5f);
        TestTrue(TEXT("A dash sweeps the capsule and cannot pass through a wall"),T.Hero->GetActorLocation().X<140);
        TestTrue(TEXT("Blocked dash still expires and keeps a finite pose"),!T.Hero->IsDashing() && !T.Hero->GetActorLocation().ContainsNaN());
    }
    for(int32 Case=0;Case<6;++Case) {
        FDashWorld T;
        if(!TestTrue(TEXT("Restrictions fixture permits dash before applying each blocker"),T.Ready())) return false;
        if(Case==0) T.Move->SetMovementMode(MOVE_Falling);
        if(Case==1) { T.Move->SetWantsClimb(true);T.Move->SetMovementMode(MOVE_Custom,1); }
        if(Case==2) T.Move->SetMovementMode(MOVE_Swimming);
        if(Case==3) T.Hero->bBrushing=true;
        if(Case==4) T.Hero->Status->Damage(1000);
        if(Case==5) T.Move->DisableMovement();
        TestFalse(TEXT("Air, climb, swim, work, death and disabled movement reject dash"),T.Move->CanDash());
    }
    FDashWorld T;if(!TestTrue(TEXT("Death fixture has an available dash"),T.Ready())) return false;
    T.Tap();TestTrue(TEXT("Death interrupts a dash that actually started"),T.Hero->IsDashing());T.Hero->Status->Damage(1000);T.Step(.05f);
    TestFalse(TEXT("Death interrupts dash instead of carrying the ragdoll"),T.Hero->IsDashing());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDashSuctionRootMotion,"MessControl.Dash.AmbientWindPreservesControlAndReplaySource",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDashSuctionRootMotion::RunTest(const FString&)
{
    FDashWorld T;
    if(!TestTrue(TEXT("Wind fixture has an active local CMC simulation"),T.Ready())) return false;
    auto* Throat=T.World->SpawnActor<AMCThroat>(FVector(6000,0,10000),FRotator::ZeroRotator);
    Throat->SetActorTickEnabled(false);Throat->ThroatPhase=EMCThroatPhase::Swallowing;
    Throat->SuctionState.Origin=FVector(2000,0,10080);Throat->SuctionState.StartedAt=T.World->GetTimeSeconds()-.5;
    Throat->SuctionState.Duration=5;Throat->SuctionState.InfluenceRadius=6000;Throat->SuctionState.MaxPullSpeed=85;
    const float Start=T.Hero->GetActorLocation().X;T.Step(.4f);
    TestTrue(TEXT("Ambient wind moves an idle player toward the throat after voluntary braking"),T.Hero->GetActorLocation().X-Start>4);
    TestTrue(TEXT("Ambient suction remains a gentle independent velocity"),T.Hero->GetVelocity().Size2D()>10 && T.Hero->GetVelocity().Size2D()<=86);
    T.Tap();TestTrue(TEXT("Ambient additive wind does not prevent dash"),T.Hero->IsDashing());
    TestTrue(TEXT("Dash override and ambient additive root motion coexist"),T.Move->GetRootMotionSource(TEXT("MCTapDash")).IsValid() && T.Move->GetRootMotionSource(TEXT("MCThroatAmbientSuction")).IsValid());
    T.Hero->CancelGameplayInput();T.Step(.5f);Throat->SuctionState.Duration=0;T.Step(.5f);
    TestTrue(TEXT("The end of a swallow removes the field without retained drift"),T.Hero->GetVelocity().Size2D()<1);
    FMCLocomotionRootMotionSource Local,Server;
    Local.InstanceName=Server.InstanceName=TEXT("WindReplay");Local.Duration=Server.Duration=-1;
    Local.Force=FVector(20,0,0);Server.Force=FVector(80,0,0);Server.SetTime(.25f);
    TestTrue(TEXT("Spatially changing fields retain a stable root motion identity"),Local.Matches(&Server));
    TestTrue(TEXT("A correction applies the authoritative ambient force snapshot"),Local.UpdateStateFrom(&Server) && Local.Force.Equals(Server.Force,.01) && FMath::IsNearlyEqual(Local.GetTime(),Server.GetTime()));
    TUniquePtr<FRootMotionSource> Clone(Local.Clone());
    TestTrue(TEXT("Saved moves clone the derived force source and its sample"),Clone->GetScriptStruct()==Local.GetScriptStruct() && static_cast<FMCLocomotionRootMotionSource*>(Clone.Get())->Force.Equals(Local.Force));
    auto Replay=MakeShared<FMCLocomotionRootMotionSource>(Local);
    Replay->InstanceName=TEXT("MCThroatAmbientSuction");Replay->Status.SetFlag(ERootMotionSourceStatusFlags::Prepared);
    Replay->RootMotionParams.Set(FTransform(Replay->Force*2));T.Move->ApplyRootMotionSource(Replay);
    const float ReplayTime=Replay->GetTime();const FVector SavedSample(25,10,0);
    T.Move->RestoreSuctionForMove(SavedSample);
    TestTrue(TEXT("Replay replaces already prepared wind while preserving catchup scale and source time"),Replay->RootMotionParams.GetRootMotionTransform().GetTranslation().Equals(SavedSample*2,.01) && Replay->GetTime()==ReplayTime);
    Local.Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
    Local.FinishVelocityParams.Mode=ERootMotionFinishVelocityMode::ClampVelocity;Local.FinishVelocityParams.ClampVelocity=340;
    TArray<uint8> Bytes;FMemoryWriter MemoryWriter(Bytes);FObjectAndNameAsStringProxyArchive Writer(MemoryWriter,false);
    bool Written=false;Local.NetSerialize(Writer,nullptr,Written);
    FMemoryReader MemoryReader(Bytes);FObjectAndNameAsStringProxyArchive Reader(MemoryReader,true);
    FMCLocomotionRootMotionSource Remote;bool Read=false;Remote.NetSerialize(Reader,nullptr,Read);
    TestTrue(TEXT("Replication preserves dash gravity and finish braking settings"),Written && Read && Remote.Settings.HasFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate) && Remote.FinishVelocityParams.Mode==ERootMotionFinishVelocityMode::ClampVelocity && Remote.FinishVelocityParams.ClampVelocity==340);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDashProxyPresentation,"MessControl.Dash.ProxyPresentationFreshReceiptAndStaleReset",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDashProxyPresentation::RunTest(const FString&)
{
    FDashWorld T;if(!TestTrue(TEXT("Proxy fixture starts with a valid actionable character"),T.Ready())) return false;
    T.Step(.6f);
    T.Hero->SetRole(ROLE_SimulatedProxy);
    auto Notify=[&]() {T.Move->ProcessEvent(T.Move->FindFunctionChecked(TEXT("OnRep_DashStartedAt")),nullptr);};
    auto ServerTime=[&]() {const auto* State=T.World->GetGameState();return State?State->GetServerWorldTimeSeconds():T.World->GetTimeSeconds();};
    TestFalse(TEXT("Missing replicated dash never starts a cosmetic dash pose"),T.Hero->IsDashing());
    T.Move->DashStartedAt=ServerTime()-2;Notify();
    TestFalse(TEXT("A late joiner cannot replay an expired dash"),T.Hero->IsDashing());
    T.Move->DashStartedAt=ServerTime()+2;Notify();
    TestFalse(TEXT("An invalid future timestamp cannot start presentation"),T.Hero->IsDashing());
    T.Move->DashStartedAt=ServerTime()-.3;Notify();
    TestTrue(TEXT("Fresh delayed receipt starts the complete dash pose from its beginning"),T.Hero->IsDashing() && T.Hero->GetDashProgress()<.01f);
    T.Step(.2f);const float Progress=T.Hero->GetDashProgress();
    TestTrue(TEXT("Proxy presentation preserves its readable middle despite the server dash already expiring"),T.Hero->IsDashing() && Progress>.35f && Progress<.65f);
    Notify();TestEqual(TEXT("Repeated replication of one timestamp does not restart the dash pose"),T.Hero->GetDashProgress(),Progress);
    T.Step(.25f);TestFalse(TEXT("Receipt presentation ends after one configured duration"),T.Hero->IsDashing());
    T.Move->DashStartedAt=ServerTime();Notify();TestTrue(TEXT("A new server dash starts one new visual"),T.Hero->IsDashing());
    T.Move->DashStartedAt=-100;Notify();TestFalse(TEXT("Explicit server cancellation clears presentation immediately"),T.Hero->IsDashing());
    T.Move->DashStartedAt=ServerTime();Notify();T.Move->DisableMovement();
    TestFalse(TEXT("Capture or disabled movement cancels the proxy visual"),T.Hero->IsDashing());
    T.Move->SetMovementMode(MOVE_Walking);Notify();
    TestFalse(TEXT("Returning to standing cannot resurrect the cancelled old visual"),T.Hero->IsDashing());
    T.Hero->SetRole(ROLE_Authority);
    return true;
}
#endif
