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
        TestTrue(TEXT("A press dash charges stamina once on its first frame"),FMath::IsNearlyEqual(T.Hero->GetStamina(),T.Hero->GetMaxStamina()-T.Move->DashStaminaCost,.01f));
        TestTrue(TEXT("Shift press immediately moves the capsule"),T.Hero->GetActorLocation().X-Start.X>5);
        T.Hero->SetSprintInputHeld(false);T.Step(Dt,Dt);
        TestTrue(TEXT("Dash has a measurable normalized animation phase"),T.Hero->GetDashProgress()>0 && T.Hero->GetDashProgress()<1);
        TestTrue(TEXT("Dash without direction uses the facing direction"),FVector::DotProduct(T.Hero->GetDashDirection(),FVector::ForwardVector)>.99);
        T.Step(.45f,Dt);
        const float Distance=T.Hero->GetActorLocation().X-Start.X;
        TestTrue(*FString::Printf(TEXT("Dash covers its short configured distance at %.0f FPS (%.1f cm)"),1/Dt,Distance),Distance>380 && Distance<510);
        TestFalse(TEXT("Dash exits after one dash pose"),T.Hero->IsDashing());
        T.Tap(Dt);TestTrue(TEXT("The second independent charge permits a second dash"),T.Hero->IsDashing());
        TestEqual(TEXT("Exactly two charges are spent"),T.Move->GetDashCharges(),0);
        T.Step(.45f,Dt);T.Tap(Dt);TestFalse(TEXT("A third tap cannot bypass two recovering charges"),T.Hero->IsDashing());
        T.Step(2.2f,Dt);T.Tap(Dt);TestTrue(TEXT("Dash becomes available when the first charge recovers"),T.Hero->IsDashing());
        T.Hero->CancelGameplayInput();TestFalse(TEXT("Cancel stops an active dash immediately"),T.Hero->IsDashing());
        T.Step(3.1f,Dt);T.Hero->SetSprintInputHeld(true);T.Step(.08f,Dt);
        T.Hero->CancelGameplayInput();T.Step(.05f,Dt);
        TestFalse(TEXT("Input cancellation stops the press dash without restarting it"),T.Hero->IsDashing());
        const float BeforeSprint=T.Hero->GetStamina();
        T.Hero->SetSprintInputHeld(true);T.Step(1.f,Dt,true);
        TestTrue(TEXT("Holding Shift transitions into sustained sprinting"),T.Move->bSprintActive && T.Hero->GetVelocity().Size2D()>500);
        TestTrue(TEXT("Moving while holding Shift drains stamina"),T.Hero->GetStamina()<BeforeSprint-8.f);
        T.Hero->SetSprintInputHeld(false);T.Step(Dt,Dt);
        TestFalse(TEXT("Releasing a long hold never triggers a dash"),T.Hero->IsDashing());
        TestFalse(TEXT("Releasing Shift clears sprint intent"),T.Move->WantsToSprint());
        T.Step(2.1f,Dt);
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
    TestTrue(TEXT("The second charge remains ready while the first recovers"),T.Move->CanDash());
    T.Hero->SetSprintInputHeld(false);T.Step(1.f/60);
    TestFalse(TEXT("Release never starts a second dash after cooldown"),T.Hero->IsDashing());
    T.Hero->SetSprintInputHeld(true);T.Hero->CancelGameplayInput();T.Step(1.f/60);
    TestFalse(TEXT("Cancel before the next movement frame clears the queued dash"),T.Hero->IsDashing());
    T.Hero->SetSprintInputHeld(true);T.Hero->SetSprintInputHeld(false);T.Step(1.f/60);
    TestTrue(TEXT("Press and release within one frame still executes one dash"),T.Hero->IsDashing());
    T.Hero->CancelGameplayInput();
    FMCStaminaPredictionState Empty;Empty.Value=0;Empty.RecoveryDelay=T.Move->StaminaRecoveryDelay;Empty.Exhausted=true;
    T.Move->RestoreStaminaPrediction(Empty);
    TestFalse(TEXT("Exhausted stamina rejects a new dash"),T.Move->CanDash());
    T.Hero->SetSprintInputHeld(true);T.Step(.3f,1.f/60,true);
    TestTrue(TEXT("Holding Shift while exhausted uses ordinary walking"),!T.Move->bSprintActive && T.Hero->GetVelocity().Size2D()<=T.Move->WalkSpeed+1);
    T.Step(1.6f,1.f/60,true);
    TestTrue(TEXT("Stamina recovers through the delay and resumes held sprint without another press"),T.Hero->GetStamina()>0 && T.Move->bSprintActive);
    T.Hero->SetSprintInputHeld(false);T.Step(6.f);
    TestTrue(TEXT("Released sprint recovers to the configured maximum"),FMath::IsNearlyEqual(T.Hero->GetStamina(),T.Hero->GetMaxStamina(),.01f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDashIndependentCharges,"MessControl.Dash.TwoChargesRecoverIndependently",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDashIndependentCharges::RunTest(const FString&)
{
    for(float Dt:{1.f/30,1.f/60,1.f/120}) {
        FDashWorld T;
        if(!TestTrue(TEXT("Independent-charge fixture begins on its real floor"),T.Ready())) return false;
        TestEqual(TEXT("A fresh body has exactly two dash charges"),T.Move->GetDashCharges(),2);
        TestEqual(TEXT("Default recharge is three seconds before perk modifiers"),T.Move->EffectiveDashCooldown(),3.f);
        T.Tap(Dt);
        TestEqual(TEXT("First real press spends one charge"),T.Move->GetDashCharges(),1);
        TestTrue(TEXT("First recharge starts at the press rather than the end of motion"),T.Move->GetDashChargeCooldownRemaining(0)>2.9f);
        T.Step(.65f,Dt);
        const float FirstBefore=T.Move->GetDashChargeCooldownRemaining(0);
        const float StaminaBefore=T.Hero->GetStamina();
        T.Tap(Dt);
        TestTrue(TEXT("Second press uses the remaining charge"),T.Hero->IsDashing() && T.Move->GetDashCharges()==0);
        TestTrue(TEXT("Second press leaves the older timer advancing without restart"),FMath::IsNearlyEqual(T.Move->GetDashChargeCooldownRemaining(0),FirstBefore-2*Dt,.015f));
        TestTrue(TEXT("The second charge has its own fresh three-second timer"),FMath::IsNearlyEqual(T.Move->GetDashChargeCooldownRemaining(1),3.f-Dt,.015f));
        TestTrue(TEXT("Each accepted dash still spends stamina exactly once"),FMath::IsNearlyEqual(T.Hero->GetStamina(),StaminaBefore-T.Move->DashStaminaCost,.01f));
        T.Step(.45f,Dt);
        const float OldTimer=T.Move->GetDashChargeCooldownRemaining(0),NewTimer=T.Move->GetDashChargeCooldownRemaining(1);
        T.Tap(Dt);
        TestFalse(TEXT("A third real press is rejected"),T.Hero->IsDashing());
        TestTrue(TEXT("Rejected press neither restarts nor consumes either recharge"),FMath::IsNearlyEqual(T.Move->GetDashChargeCooldownRemaining(0),OldTimer-2*Dt,.015f)
            && FMath::IsNearlyEqual(T.Move->GetDashChargeCooldownRemaining(1),NewTimer-2*Dt,.015f));
        const float UntilFirst=T.Move->GetDashChargeCooldownRemaining(0);
        T.Step(UntilFirst+.02f,Dt);
        TestEqual(TEXT("Only the earlier charge is restored at its own deadline"),T.Move->GetDashCharges(),1);
        TestTrue(TEXT("Later charge still has the gap between the two presses to recover"),T.Move->GetDashChargeCooldownRemaining(1)>.55f);
        T.Step(T.Move->GetDashChargeCooldownRemaining(1)+.02f,Dt);
        TestEqual(TEXT("Both charges recover independently without further input"),T.Move->GetDashCharges(),2);
    }
    FDashWorld T;
    if(!TestTrue(TEXT("Cancellation fixture begins actionable"),T.Ready())) return false;
    T.Tap();T.Hero->CancelGameplayInput();
    TestEqual(TEXT("Cancelling a dash does not refund its spent charge"),T.Move->GetDashCharges(),1);
    const float Running=T.Move->GetDashChargeCooldownRemaining(0);
    T.Move->DashCooldown=1.5f;
    T.Step(.5f);
    TestTrue(TEXT("A tuning/perk change does not retime an already spent charge"),FMath::IsNearlyEqual(T.Move->GetDashChargeCooldownRemaining(0),Running-.5f,.02f));
    T.Tap();
    TestTrue(TEXT("A later dash captures its own newly adjusted duration"),FMath::IsNearlyEqual(T.Move->GetDashChargeCooldownRemaining(1),1.5f-1.f/60,.02f));
    T.Hero->CancelGameplayInput();T.Move->DisableMovement();T.Step(.5f);
    TestTrue(TEXT("Disabling locomotion continues both recharge clocks"),T.Move->GetDashChargeCooldownRemaining(0)<Running-1.f
        && T.Move->GetDashChargeCooldownRemaining(1)<1.1f);
    T.Hero->Status->Damage(1000);T.Step(.05f);
    TestFalse(TEXT("A dead body cannot use its remaining charges"),T.Move->CanDash());
    auto* Replacement=T.World->SpawnActor<AMCToothCharacter>(FVector(-1000,0,10080),FRotator::ZeroRotator);
    auto* ReplacementMove=CastChecked<UMCToothMovementComponent>(Replacement->GetCharacterMovement());
    T.Hero->GetController()->Possess(Replacement);
    TestEqual(TEXT("Replacement begins with two charges rather than inheriting corpse timers"),ReplacementMove->GetDashCharges(),2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDashResourceReconciliation,"MessControl.Dash.ChargeSnapshotsSavedMovesAndAcknowledgements",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDashResourceReconciliation::RunTest(const FString&)
{
    FDashWorld T;
    if(!TestTrue(TEXT("Resource-ledger fixture begins on its actual floor"),T.Ready())) return false;
    auto* Prediction=T.Move->GetPredictionData_Client_Character();
    Prediction->SavedMoves.Reset();Prediction->LastAckedMove.Reset();Prediction->PendingMove.Reset();Prediction->bUpdatePosition=false;
    auto Record=[&](float TimeStamp,bool Dash) {
        if(Dash) T.Move->RequestDash();
        FSavedMovePtr Saved=Prediction->AllocateNewMove();
        Saved->SetMoveFor(T.Hero,.1f,FVector::ZeroVector,*Prediction);Saved->TimeStamp=TimeStamp;
        T.Move->PerformMovement(.1f);
        Saved->PostUpdate(T.Hero,FSavedMove_Character::PostUpdate_Record);
        Prediction->SavedMoves.Add(Saved);
        return Saved;
    };
    const FSavedMovePtr Acked=Record(1.f,false);
    const FSavedMovePtr Spent=Record(1.1f,true);
    Record(1.2f,false);
    TestEqual(TEXT("The predicted spend uses one charge before its acknowledgement"),T.Move->GetDashCharges(),1);
    FMCStaminaMoveResponse Written;
    Written.ClientAdjustment.bAckGoodMove=true;Written.ClientAdjustment.TimeStamp=1.f;
    Written.Stamina=T.Move->GetStaminaPrediction();Written.Stamina.Value=T.Hero->GetMaxStamina();Written.Stamina.RecoveryDelay=0.f;
    // The authoritative timestamp includes an earlier use unknown to this owner.
    Written.Dash.Remaining[0]=2.7f;Written.Dash.Remaining[1]=0.f;
    TArray<uint8> Bytes;FMemoryWriter Writer(Bytes);
    TestTrue(TEXT("A good movement acknowledgement serializes both charge clocks"),Written.Serialize(*T.Move,Writer,nullptr));
    FMCStaminaMoveResponse Read;FMemoryReader Reader(Bytes);
    TestTrue(TEXT("Charge clocks deserialize with the acknowledged movement timestamp"),Read.Serialize(*T.Move,Reader,nullptr));
    TestEqual(TEXT("First authoritative clock survives serialization"),Read.Dash.Remaining[0],2.7f);
    TestEqual(TEXT("Second authoritative clock remains separately ready"),Read.Dash.Remaining[1],0.f);
    const FVector Before=T.Hero->GetActorLocation();
    T.Move->ClientHandleMoveResponse(Read);
    TestTrue(TEXT("Good ACK restores the server state then replays newer movement time and one spend"),FMath::IsNearlyEqual(T.Move->GetDashChargeCooldownRemaining(0),2.5f,.001f)
        && FMath::IsNearlyEqual(T.Move->GetDashChargeCooldownRemaining(1),2.9f,.001f));
    TestTrue(TEXT("Resource-only acknowledgement does not move the capsule"),T.Hero->GetActorLocation().Equals(Before,.001));
    TestTrue(TEXT("The correct saved move was acknowledged"),Prediction->LastAckedMove==Acked && Prediction->SavedMoves.Num()==2);
    // Prep/replay is the same saved-move path used by CMC corrections; it must
    // retain the updated server baseline rather than the old one-charge snapshot.
    Spent->PrepMoveFor(T.Hero);
    TestTrue(TEXT("Saved move prep retains the corrected two-clock baseline"),FMath::IsNearlyEqual(T.Move->GetDashChargeCooldownRemaining(0),2.7f,.001f)
        && T.Move->GetDashChargeCooldownRemaining(1)==0.f);
    T.Hero->bClientUpdating=true;
    T.Move->PerformMovement(.1f);
    TestTrue(TEXT("Restored root motion charges the replayed dash exactly once"),FMath::IsNearlyEqual(T.Move->GetDashChargeCooldownRemaining(0),2.6f,.001f)
        && FMath::IsNearlyEqual(T.Move->GetDashChargeCooldownRemaining(1),3.f,.001f));
    T.Move->DashCooldown=1.5f;
    Spent->PrepMoveFor(T.Hero);T.Move->PerformMovement(.1f);
    TestTrue(TEXT("Repeated correction replay preserves the original per-use duration"),FMath::IsNearlyEqual(T.Move->GetDashChargeCooldownRemaining(0),2.6f,.001f)
        && FMath::IsNearlyEqual(T.Move->GetDashChargeCooldownRemaining(1),3.f,.001f));
    for(bool ChargesDenied:{true,false}) {
        Spent->PrepMoveFor(T.Hero);
        if(ChargesDenied) {
            FMCDashPredictionState Authoritative;Authoritative.Remaining[0]=2.f;Authoritative.Remaining[1]=2.5f;
            T.Move->RestoreDashPrediction(Authoritative);
        } else {
            FMCStaminaPredictionState Exhausted;Exhausted.Value=0.f;Exhausted.Exhausted=true;Exhausted.RecoveryDelay=.65f;
            T.Move->RestoreStaminaPrediction(Exhausted);
        }
        T.Move->PerformMovement(.1f);
        TestFalse(TEXT("Corrected charge or stamina denial removes the restored predicted dash source"),T.Move->IsDashing());
        TestFalse(TEXT("Rejected correction replay does not spend stamina again"),T.Move->LastMoveChargedDash());
    }
    T.Hero->bClientUpdating=false;
    T.Move->CancelDash();
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
