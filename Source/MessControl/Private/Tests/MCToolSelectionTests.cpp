#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCInventoryComponent.h"
#include "MCToolScrollInput.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCFishingRodComponent.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCGameState.h"
#include "MCHubGameState.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace MCToolSelectionTests
{
struct FWorld
{
    UWorld* World=nullptr;
    AMCPlayerController* Controller=nullptr;
    AMCToothCharacter* Hero=nullptr;
    explicit FWorld(bool bHub=false)
    {
        if(bHub) {
            const FString PackageName=TEXT("/Temp/MCToolSelection_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT("_Hub");
            World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("Hub"),CreatePackage(*PackageName));
        } else World=UWorld::CreateWorld(EWorldType::Game,false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        if(bHub) {
            auto* State=World->SpawnActor<AMCHubGameState>();World->SetGameState(State);State->Phase=EMCHubPhase::Gathering;
        } else {
            auto* State=World->SpawnActor<AMCGameState>();World->SetGameState(State);
            State->Phase=EMCShiftPhase::Working;State->bLobbyWaiting=false;
        }
        Controller=World->SpawnActor<AMCPlayerController>();
        // These isolated native cases exercise input/authority, not the local
        // viewport's readiness coordinator (there is no game viewport here).
        Controller->SetActorTickEnabled(false);
        Controller->SetPlayer(NewObject<ULocalPlayer>(GEngine));
        Controller->SetPlayerState(World->SpawnActor<AMCPlayerState>());
        Hero=SpawnTooth(FVector(0,5000,10000));Controller->Possess(Hero);
        // Possession restarts CharacterMovement. This contact fixture has no
        // floor; hold both actors at the same height after that real restart.
        Hero->GetCharacterMovement()->DisableMovement();
    }
    ~FWorld()
    {
        World->EndPlay(EEndPlayReason::Quit);
        GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    }
    AMCToothCharacter* SpawnTooth(FVector Position)
    {
        auto* Tooth=World->SpawnActor<AMCToothCharacter>(Position,FRotator::ZeroRotator);
        Tooth->Status->Initialize(100);Tooth->GetCharacterMovement()->DisableMovement();return Tooth;
    }
    void Step(float Seconds)
    {
        for(int32 I=0;I<FMath::CeilToInt(Seconds*60);++I)
        {++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60);}
    }
};

FMCToolSelectionReceipt Receipt(EMCToolSlot Slot,uint32 RequestId,uint32 Revision)
{
    FMCToolSelectionReceipt Result;Result.Slot=Slot;Result.RequestId=RequestId;Result.Revision=Revision;return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCToolSelectionInterruptTest,
    "MessControl.Input.Tools.InstantSelectionPreservesAttackDeadline",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCToolSelectionInterruptTest::RunTest(const FString&)
{
    MCToolSelectionTests::FWorld W;auto* I=W.Hero->Inventory.Get();MCToolScrollInput::FCursor Cursor;
    if(!TestTrue(TEXT("Real local pawn and inventory BeginPlay completed"),W.Hero->IsLocallyControlled() && I->HasBegunPlay())) return false;
    auto* Target=W.SpawnTooth(W.Hero->GetActorLocation()+FVector(130,0,0));Target->FreezeLegs(100.f);
    if(!TestTrue(TEXT("The forward target has real pickaxe-breakable frozen legs"),Target->HasFrozenLegs())) return false;
    const float Ice=Target->IceLegHealth;
    I->ServerSelect(EMCToolSlot::Knife);W.Hero->SwingBrush();
    const int32 Swings=W.Hero->ValidatedSwingCount;
    if(!TestEqual(TEXT("The original knife swing was accepted"),Swings,1)) return false;
    const float OriginalDuration=I->SwingDuration();
    W.Hero->SwingBrush(); // A second press queues a continuation on the old tool.
    TestTrue(TEXT("Wheel can interrupt immediately during the swing"),MCToolScrollInput::Handle(*W.Hero,1.f,Cursor));
    TestEqual(TEXT("Knife immediately advances to spray"),I->Selected,EMCToolSlot::Spray);
    TestTrue(TEXT("Direct selection can immediately reverse during that same cooldown"),I->RequestSelect(EMCToolSlot::Pickaxe));
    TestEqual(TEXT("The new tool is already selected before a world tick"),I->Selected,EMCToolSlot::Pickaxe);
    TestTrue(TEXT("A cancelled swing no longer drives the replacement tool's pose"),W.Hero->GetToolSwingElapsed()>50.f);
    W.Hero->SwingBrush();
    TestEqual(TEXT("Swapping cannot buy an earlier attack"),W.Hero->ValidatedSwingCount,Swings);
    W.Step(OriginalDuration+.10f);
    TestEqual(TEXT("The old queued combo never restarts after swapping"),W.Hero->ValidatedSwingCount,Swings);
    TestEqual(TEXT("The old delayed knife contact never turns into a new pickaxe hit"),Target->IceLegHealth,Ice);
    auto LogContactFixture=[&](const TCHAR* Phase) {
        const FVector Offset=Target->GetActorLocation()-W.Hero->GetActorLocation();
        AddInfo(FString::Printf(TEXT("%s t=%.3f hero=%s target=%s distance=%.2f forward_dot=%.3f reach=%.2f hero_work=%d target_alive=%d target_frozen=%d hero_body=%d target_body=%d ice=%.2f swing_age=%.3f accepted_swings=%d confirmed_hits=%d"),
            Phase,W.World->GetTimeSeconds(),*W.Hero->GetActorLocation().ToCompactString(),*Target->GetActorLocation().ToCompactString(),
            Offset.Size(),FVector::DotProduct(W.Hero->GetActorForwardVector(),Offset.GetSafeNormal2D()),I->MeleeReach(),
            W.Hero->CanWork(),Target->Status->IsAlive(),Target->HasFrozenLegs(),int32(W.Hero->ToothPhysics->GetBodyState()),
            int32(Target->ToothPhysics->GetBodyState()),Target->IceLegHealth,W.Hero->GetToolSwingElapsed(),W.Hero->ValidatedSwingCount,W.Hero->ConfirmedHitCount));
    };
    LogContactFixture(TEXT("Before final native pickaxe swing"));
    W.Hero->SwingBrush();
    TestEqual(TEXT("The selected pickaxe attacks normally after the original deadline"),W.Hero->ValidatedSwingCount,Swings+1);
    W.Step(I->SwingContactTime()+.10f);
    LogContactFixture(TEXT("After final native pickaxe contact window"));
    TestTrue(TEXT("The same target is reachable by an actual new pickaxe contact"),Target->IceLegHealth<Ice);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCToolSelectionReceiptTest,
    "MessControl.Input.Tools.PredictionReceiptsRejectStaleABA",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCToolSelectionReceiptTest::RunTest(const FString&)
{
    using namespace MCToolSelectionTests;
    FMCToolSelectionPrediction P;EMCToolSlot Visible=EMCToolSlot::Brush;
    TestTrue(TEXT("Initial receipt establishes authority"),P.Receive(Receipt(Visible,0,1),Visible));
    const uint32 First=P.Request(EMCToolSlot::Knife);Visible=EMCToolSlot::Knife;
    const uint32 Second=P.Request(EMCToolSlot::Brush);Visible=EMCToolSlot::Brush;
    TestTrue(TEXT("Older request's newer receipt is consumed"),P.Receive(Receipt(EMCToolSlot::Knife,First,2),Visible));
    TestEqual(TEXT("First ACK cannot undo newer A -> B -> A intent"),Visible,EMCToolSlot::Brush);
    TestEqual(TEXT("Latest owner request remains pending"),P.PendingRequestId,Second);
    P.AcknowledgeOrdered(First);
    TestEqual(TEXT("An older reliable ACK cannot release a newer swing fence"),P.OutstandingSwingFence,Second);
    TestTrue(TEXT("Same-slot authority state still acknowledges by revision"),P.Receive(Receipt(EMCToolSlot::Brush,Second,3),Visible));
    TestEqual(TEXT("Latest ACK retires prediction"),P.PendingRequestId,uint32(0));
    TestEqual(TEXT("A property receipt does not release reliable RPC ordering"),P.OutstandingSwingFence,Second);
    P.AcknowledgeOrdered(Second);
    TestEqual(TEXT("The matching reliable ACK releases the swing fence"),P.OutstandingSwingFence,uint32(0));
    TestFalse(TEXT("Late packet with an older revision cannot revive knife"),P.Receive(Receipt(EMCToolSlot::Knife,First,2),Visible));
    TestEqual(TEXT("Visible slot remains the accepted latest selection"),Visible,EMCToolSlot::Brush);
    const uint32 Rejected=P.Request(EMCToolSlot::Spray);Visible=EMCToolSlot::Spray;
    TestTrue(TEXT("Rejection receipt can acknowledge unchanged authority"),P.Receive(Receipt(EMCToolSlot::Brush,Rejected,4),Visible));
    TestEqual(TEXT("Rejected prediction rolls back immediately"),Visible,EMCToolSlot::Brush);
    P.Request(EMCToolSlot::Knife);Visible=EMCToolSlot::Knife;P.Cancel(Visible);
    TestEqual(TEXT("Death or menu cancellation restores last authority slot"),Visible,EMCToolSlot::Brush);
    TestEqual(TEXT("Cancellation removes pending presentation"),P.PendingRequestId,uint32(0));
    TestTrue(TEXT("Local cancellation keeps the outstanding RPC fence"),P.OutstandingSwingFence!=0);
    TestFalse(TEXT("Invalid slot receipts are never applied"),P.Receive(Receipt(EMCToolSlot(99),0,5),Visible));
    TestEqual(TEXT("Malformed receipt did not consume a valid revision"),P.LastRevision,uint32(4));
    const uint32 Coalesced=P.Request(EMCToolSlot::Spray);Visible=EMCToolSlot::Spray;
    TestTrue(TEXT("A coalesced authority override still acknowledges the processed request"),P.Receive(Receipt(EMCToolSlot::Pickaxe,Coalesced,6),Visible));
    TestEqual(TEXT("Coalesced override cannot strand an old owner intent"),P.PendingRequestId,uint32(0));
    TestEqual(TEXT("Latest authority override is presented"),Visible,EMCToolSlot::Pickaxe);
    P.NextRequestId=MAX_uint32;
    TestEqual(TEXT("Request ID wrap skips reserved legacy ID zero"),P.Request(EMCToolSlot::Knife),uint32(1));
    TestTrue(TEXT("Revision wrap compares in serial-number order"),FMCToolSelectionPrediction::IsNewer(1,MAX_uint32));
    TestFalse(TEXT("A stale pre-wrap revision is older"),FMCToolSelectionPrediction::IsNewer(MAX_uint32,1));
    FMCToolSelectionPrediction Wrap;Wrap.NextRequestId=MAX_uint32-1;Wrap.Request(EMCToolSlot::Spray);Visible=EMCToolSlot::Spray;
    Wrap.Receive(Receipt(EMCToolSlot::Brush,0,1),Visible);Wrap.AcknowledgeOrdered(0);
    TestEqual(TEXT("Reserved legacy ACK zero cannot retire a near-wrap request"),Wrap.PendingRequestId,MAX_uint32);
    TestEqual(TEXT("Reserved legacy ACK zero cannot release a near-wrap swing fence"),Wrap.OutstandingSwingFence,MAX_uint32);
    FMCToolSelectionPrediction NewOwner;Visible=EMCToolSlot::Brush;
    NewOwner.Receive(Receipt(EMCToolSlot::Knife,5,20),Visible);
    TestEqual(TEXT("Reownership seeds the next request from the server watermark"),NewOwner.Request(EMCToolSlot::Spray),uint32(6));
    FMCToolSelectionPrediction EarlyOwner;EarlyOwner.Request(EMCToolSlot::Spray);Visible=EMCToolSlot::Spray;
    EarlyOwner.Receive(Receipt(EMCToolSlot::Knife,5,20),Visible);
    TestEqual(TEXT("A higher ACK finishes an older pre-initial-receipt intent"),EarlyOwner.PendingRequestId,uint32(0));
    TestEqual(TEXT("That newer authority state restores the real selected tool"),Visible,EMCToolSlot::Knife);
    EarlyOwner.AcknowledgeOrdered(5);
    TestEqual(TEXT("A higher reliable ACK also releases the superseded fence"),EarlyOwner.OutstandingSwingFence,uint32(0));
    TestEqual(TEXT("The next retry is already above the server watermark"),EarlyOwner.Request(EMCToolSlot::Brush),uint32(6));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCToolSelectionLocalGuardsTest,
    "MessControl.Input.Tools.DirectSelectionKeepsMenuDeathAndRodGuards",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCToolSelectionLocalGuardsTest::RunTest(const FString&)
{
    MCToolSelectionTests::FWorld W(true);auto* I=W.Hero->Inventory.Get();
    W.Hero->FishingRod->bEquipped=true;
    W.Controller->bShowMouseCursor=true;
    TestFalse(TEXT("Direct digit selection cannot leak out of a visible menu"),I->RequestSelect(EMCToolSlot::Knife));
    TestTrue(TEXT("A menu-rejected request does not stow a fishing rod"),W.Hero->FishingRod->bEquipped);
    W.Controller->bShowMouseCursor=false;W.Controller->SetIgnoreLookInput(true);
    TestFalse(TEXT("Loading look barrier also blocks direct selection"),I->RequestSelect(EMCToolSlot::Knife));
    W.Controller->SetIgnoreLookInput(false);W.Hero->SetArenaArrivalHeld(true);
    TestFalse(TEXT("Arrival hold prevents direct selection"),I->RequestSelect(EMCToolSlot::Knife));
    W.Hero->SetArenaArrivalHeld(false);W.Hero->Status->State.Health=0;
    TestFalse(TEXT("A dead player cannot select tools"),I->RequestSelect(EMCToolSlot::Knife));
    TestTrue(TEXT("A death-rejected request also leaves rod recovery to authority"),W.Hero->FishingRod->bEquipped);
    TestEqual(TEXT("Rejected inputs kept the previous selected tool"),I->Selected,EMCToolSlot::Brush);
    W.Hero->Status->Initialize(100);
    TestTrue(TEXT("Live, unblocked direct selection resumes immediately"),I->RequestSelect(EMCToolSlot::Knife));
    TestEqual(TEXT("The accepted request selects its tool"),I->Selected,EMCToolSlot::Knife);
    TestFalse(TEXT("Only an accepted authority selection stows the rod"),W.Hero->FishingRod->bEquipped);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCToolSelectionLateSwingTest,
    "MessControl.Input.Tools.LateSwingCannotReviveCancelledSelection",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCToolSelectionLateSwingTest::RunTest(const FString&)
{
    using namespace MCToolSelectionTests;
    FWorld W;auto* I=W.Hero->Inventory.Get();
    const ENetRole OriginalRole=W.Hero->GetLocalRole();
    // This role fixture invokes real native receipt/multicast handlers. It is
    // a local ordering regression, not a substitute for a live network run.
    W.Hero->SetRole(ROLE_AutonomousProxy);I->Selected=EMCToolSlot::Knife;
    EMCToolSlot Visible=I->Selected;I->SelectionPrediction.Receive(Receipt(Visible,0,1),Visible);
    const uint32 First=I->SelectionPrediction.Request(EMCToolSlot::Pickaxe);
    I->Selected=EMCToolSlot::Pickaxe;I->CancelPreviousSwingForSelection();
    const double OldAt=W.World->GetTimeSeconds();
    auto LateKnife=[&]() {W.Hero->MulticastSwing_Implementation(OldAt,nullptr,FVector::ZeroVector,FVector::UpVector,uint8(EMCToolSlot::Knife)|(1<<4));};
    LateKnife();
    TestTrue(TEXT("An old multicast cannot revive the cancelled tool pose"),W.Hero->GetToolSwingElapsed()>50.f);
    I->SelectionReceipt=Receipt(EMCToolSlot::Pickaxe,First,2);I->OnRep_SelectionReceipt();
    TestTrue(TEXT("Property-first delivery keeps the reliable swing fence"),I->IsSelectionPresentationFenced());
    I->SelectionPrediction.Cancel(Visible);I->Selected=Visible;LateKnife();
    TestTrue(TEXT("Menu/death presentation cancel cannot open a late-RPC window"),W.Hero->GetToolSwingElapsed()>50.f);
    I->ClientConfirmSelection_Implementation(I->SelectionReceipt);
    TestFalse(TEXT("Matching reliable ACK finally retires the fence"),I->IsSelectionPresentationFenced());
    const double NextAt=OldAt+.1;
    W.Hero->MulticastSwing_Implementation(NextAt,nullptr,FVector::ZeroVector,FVector::UpVector,uint8(EMCToolSlot::Pickaxe)|(1<<4));
    TestEqual(TEXT("A real next swing after the ordered ACK still plays"),W.Hero->SwingStartedAt,NextAt);

    const uint32 Middle=I->SelectionPrediction.Request(EMCToolSlot::Brush);
    I->Selected=EMCToolSlot::Brush;I->CancelPreviousSwingForSelection();
    const uint32 Last=I->SelectionPrediction.Request(EMCToolSlot::Knife);
    I->Selected=EMCToolSlot::Knife;I->CancelPreviousSwingForSelection();
    LateKnife();
    TestTrue(TEXT("Same-tool A -> B -> A also rejects the original late swing"),W.Hero->GetToolSwingElapsed()>50.f);
    I->ClientConfirmSelection_Implementation(Receipt(EMCToolSlot::Brush,Middle,3));LateKnife();
    TestTrue(TEXT("The middle reliable ACK cannot release the final request's fence"),I->IsSelectionPresentationFenced() && W.Hero->GetToolSwingElapsed()>50.f);
    I->SelectionReceipt=Receipt(EMCToolSlot::Knife,Last,4);I->OnRep_SelectionReceipt();LateKnife();
    TestTrue(TEXT("Final same-slot property ACK cannot resurrect an old same-tool swing"),W.Hero->GetToolSwingElapsed()>50.f);
    I->ClientConfirmSelection_Implementation(I->SelectionReceipt);
    const double FinalAt=NextAt+.1;
    W.Hero->MulticastSwing_Implementation(FinalAt,nullptr,FVector::ZeroVector,FVector::UpVector,uint8(EMCToolSlot::Knife)|(1<<4));
    TestEqual(TEXT("The following same-tool swing remains visible after the final reliable ACK"),W.Hero->SwingStartedAt,FinalAt);

    I->SelectionPrediction.Request(EMCToolSlot::Spray);
    W.Hero->SetRole(OriginalRole);W.Controller->UnPossess();W.Hero->SetRole(ROLE_SimulatedProxy);
    I->Selected=EMCToolSlot::Pickaxe; // A newer observer replication already arrived.
    I->TickComponent(0,LEVELTICK_All,nullptr);
    TestEqual(TEXT("Losing ownership never rolls a replicated observer slot back to stale owner cache"),I->Selected,EMCToolSlot::Pickaxe);
    TestEqual(TEXT("Ownership loss forgets the local visual request"),I->SelectionPrediction.PendingRequestId,uint32(0));
    TestEqual(TEXT("Ownership loss also forgets this former owner's fence"),I->SelectionPrediction.OutstandingSwingFence,uint32(0));
    I->ClientConfirmSelection_Implementation(Receipt(EMCToolSlot::Brush,Last+1,5));
    TestEqual(TEXT("A former owner's late reliable receipt cannot overwrite observer replication"),I->Selected,EMCToolSlot::Pickaxe);
    TestTrue(TEXT("A non-local receipt is retained for deferred initial/returning owner reconciliation"),I->bDeferredSelectionReceipt);
    // Possession is a real authority operation even though the receipt fixture
    // intentionally simulates a client role on the same local pawn.
    W.Hero->SetRole(OriginalRole);W.Controller->Possess(W.Hero);W.Hero->SetRole(ROLE_AutonomousProxy);
    I->TickComponent(0,LEVELTICK_All,nullptr);
    TestEqual(TEXT("Deferred receipt becomes visible once the pawn actually becomes local"),I->Selected,EMCToolSlot::Brush);
    TestFalse(TEXT("Deferred owner reconciliation is consumed once"),I->bDeferredSelectionReceipt);
    I->ClientConfirmSelection_Implementation(Receipt(EMCToolSlot::Knife,Last+1,6));
    TestEqual(TEXT("The next current owner snapshot continues to apply normally"),I->Selected,EMCToolSlot::Knife);
    W.Hero->SetRole(OriginalRole);
    return true;
}
#endif
