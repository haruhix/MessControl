#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCActiveSkillTestTypes.h"
#include "MCPlayerState.h"
#include "MCPlayerController.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"
#include "UObject/Package.h"

namespace
{
struct FActiveProtocolWorld
{
    UWorld* W=UWorld::CreateWorld(EWorldType::Game,false);
    UPackage* OwnedWorldPackage=W->GetOutermost();
    const FName OwnedWorldPackageName=OwnedWorldPackage->GetFName();
    AMCPlayerController* PC=nullptr;
    AMCPlayerState* PS=nullptr;
    AMCToothCharacter* H=nullptr;
    UMCPerkComponent* P=nullptr;
    FActiveProtocolWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
        W->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        W->SetGameMode(URL);W->InitializeActorsForPlay(URL);W->BeginPlay();
        auto* GS=W->SpawnActor<AMCGameState>();W->SetGameState(GS);GS->Phase=EMCShiftPhase::Working;GS->bLobbyWaiting=false;
        auto* Table=NewObject<UDataTable>(W);Table->RowStruct=FMCPerkDefinition::StaticStruct();
        FMCPerkDefinition One;One.Kind=EMCPerkKind::Consumable;One.InitialCharges=1;One.EffectClass=UMCActiveSkillProtocolProbeEffect::StaticClass();
        Table->AddRow(TEXT("One"),One);Table->AddRow(TEXT("Other"),One);
        FMCPerkDefinition Three=One;Three.InitialCharges=3;Table->AddRow(TEXT("Three"),Three);
        FMCPerkDefinition Timed=Three;Timed.CooldownSeconds=30;Table->AddRow(TEXT("Timed"),Timed);
        FMCPerkDefinition Repeat=One;Repeat.Kind=EMCPerkKind::Active;Repeat.CooldownSeconds=30;Repeat.Parameters.Add(TEXT("RepeatStops"),1.f);
        Table->AddRow(TEXT("Repeat"),Repeat);
        FMCPerkDefinition Reject=One;Reject.Parameters.Add(TEXT("Reject"),1.f);Table->AddRow(TEXT("Reject"),Reject);
        PC=W->SpawnActor<AMCPlayerController>();
        PS=W->SpawnActorDeferred<AMCPlayerState>(AMCPlayerState::StaticClass(),FTransform::Identity,PC);
        PS->Perks->PerkTable=Table;PS->FinishSpawning(FTransform::Identity);
        PC->SetPlayerState(PS);PS->SetOwner(PC);GS->AddPlayerState(PS);P=PS->Perks;
        H=W->SpawnActor<AMCToothCharacter>(FVector(0,0,10000),FRotator::ZeroRotator);
        PC->Possess(H);H->GetCharacterMovement()->DisableMovement();H->Status->Initialize(100);PC->bShowMouseCursor=false;
    }
    FMCActiveSkillInputHandle Handle(int32 Slot,uint32 Sequence) const
    {FMCActiveSkillInputHandle R;R.SlotIndex=Slot;R.SlotInstanceID=P->ActiveSkillSlots[Slot].InstanceID;R.PressSequence=Sequence;return R;}
    AMCActiveSkillProtocolProbe* Probe(const FMCActiveSkillInputHandle& Input) const {return Cast<AMCActiveSkillProtocolProbe>(P->FindRunningAction(Input));}
    ~FActiveProtocolWorld()
    {
        W->EndPlay(EEndPlayReason::Quit);
        GEngine->DestroyWorldContext(W);
        W->DestroyWorld(false);
        // CreateWorld made this package for this fixture. The destroyed test
        // world must not leave an orphan dirty map that blocks later solo PIE.
        if(IsValid(OwnedWorldPackage) && OwnedWorldPackage->GetFName()==OwnedWorldPackageName
            && OwnedWorldPackage->GetName().StartsWith(TEXT("/Temp/")))
            OwnedWorldPackage->SetDirtyFlag(false);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCActiveLastChargeInputTest,"MessControl.Skills.Protocol.LastChargeAndIdentity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCActiveLastChargeInputTest::RunTest(const FString&)
{
    FActiveProtocolWorld W;
    TestTrue(TEXT("Acquire one charge"),W.P->TryAcquirePerk(TEXT("One")));
    const auto Original=W.Handle(0,1);
    TestTrue(TEXT("Accept press"),W.P->BeginSkillSlot(Original));
    auto* A=W.Probe(Original);if(!TestNotNull(TEXT("Independent action exists"),A)) return false;
    TestTrue(TEXT("Registered before StartCommitted"),A->bRegisteredAtStart);
    TestTrue(TEXT("Last charge clears slot"),W.P->ActiveSkillSlots[0].PerkID.IsNone());
    TestEqual(TEXT("Start exactly once"),A->Starts,1);
    TestTrue(TEXT("Filling the empty depleted slot accepts a new item"),W.P->TryAcquirePerk(TEXT("Other")));
    TestEqual(TEXT("An empty-slot refill never cancels the old held cycle"),A->Cancels,0);
    TestEqual(TEXT("Old cycle remains independently alive until its original release"),A->Finishes,0);
    const auto NewPress=W.Handle(0,2);
    TestFalse(TEXT("A new occupant cannot acquire the still-held old presentation"),W.P->BeginSkillSlot(NewPress));
    TestEqual(TEXT("Blocked new occupant preserves its charge"),W.P->ActiveSkillSlots[0].Charges,1);
    TestTrue(TEXT("Original release reaches accepted cycle after depletion AND refill"),W.P->ReleaseSkillInput(Original));
    TestEqual(TEXT("Original cycle receives release"),A->Releases,1);
    TestNull(TEXT("Released consumable probe frees the hand but keeps its hazard actor"),W.H->GetActiveSkillPresentation());
    TestFalse(TEXT("Released original action remains alive independently of its slot"),A->IsFinished());
    TestFalse(TEXT("Duplicate Completed rejected"),W.P->ReleaseSkillInput(Original));
    TestFalse(TEXT("Canceled after Completed does not cancel cycle"),W.P->CancelSkillInput(Original));
    TestTrue(TEXT("New instance starts after the old hand is released"),W.P->BeginSkillSlot(NewPress));
    auto* B=W.Probe(NewPress);if(!TestNotNull(TEXT("New independent cycle exists"),B)) return false;
    TestFalse(TEXT("Old release cannot reach new slot occupant"),W.P->ReleaseSkillInput(Original));
    TestEqual(TEXT("New cycle has not received stale release"),B->Releases,0);
    TestTrue(TEXT("New press releases own cycle"),W.P->ReleaseSkillInput(NewPress));
    A->Finish();
    TestFalse(TEXT("Old-instance cleanup cannot unseal the replacement's Completed press"),W.P->ReleaseSkillInput(NewPress));
    TestFalse(TEXT("Old-instance cleanup cannot convert replacement Completed into Canceled"),W.P->CancelSkillInput(NewPress));
    TestEqual(TEXT("Replacement receives exactly one Completed after old-instance cleanup"),B->Releases,1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCActiveRepeatTest,"MessControl.Skills.Protocol.RepeatBeforeCooldown",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCActiveRepeatTest::RunTest(const FString&)
{
    FActiveProtocolWorld W;TestTrue(TEXT("Acquire reusable"),W.P->TryAcquirePerk(TEXT("Repeat")));
    const auto First=W.Handle(0,1);TestTrue(TEXT("Start reusable"),W.P->BeginSkillSlot(First));
    auto* A=W.Probe(First);if(!TestNotNull(TEXT("Action started"),A)) return false;
    TestFalse(TEXT("Same physical press cannot repeat"),W.P->BeginSkillSlot(First));
    TestTrue(TEXT("Release alone leaves ride active"),W.P->ReleaseSkillInput(First));
    TestFalse(TEXT("Ride remains live"),A->IsFinished());
    TestEqual(TEXT("Reusable release retains its presentation for repeat-to-stop"),W.H->GetActiveSkillPresentation(),static_cast<AMCActiveSkillAction*>(A));
    const auto Again=W.Handle(0,2);TestTrue(TEXT("Fresh press stops before cooldown gate"),W.P->BeginSkillSlot(Again));
    TestEqual(TEXT("Repeat finishes once"),A->Finishes,1);
    TestTrue(TEXT("Cooldown retained after early stop"),W.P->GetSkillCooldownRemaining(0)>29.f);
    TestFalse(TEXT("Fresh new use still respects cooldown"),W.P->BeginSkillSlot(W.Handle(0,3)));
    TestEqual(TEXT("Reusable keeps unlimited charge sentinel"),W.P->ActiveSkillSlots[0].Charges,-1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCActiveCancelTest,"MessControl.Skills.Protocol.CancelAndPreparationAtomicity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCActiveCancelTest::RunTest(const FString&)
{
    FActiveProtocolWorld W;TestTrue(TEXT("Acquire rejected probe"),W.P->TryAcquirePerk(TEXT("Reject")));
    const auto Rejected=W.Handle(0,1);TestFalse(TEXT("Pure gate rejects preparation"),W.P->BeginSkillSlot(Rejected));
    TestEqual(TEXT("Rejected preparation preserves charge"),W.P->ActiveSkillSlots[0].Charges,1);
    TestEqual(TEXT("Rejected preparation leaves no registered action"),W.P->GetActiveSkillActions().Num(),0);
    TestEqual(TEXT("Rejected preparation leaves no cooldown"),W.P->GetSkillCooldownRemaining(0),0.f);
    TestNull(TEXT("Rejected preparation claims no presentation"),W.H->GetActiveSkillPresentation());
    TestTrue(TEXT("Remove rejected slot"),W.P->RemovePerk(TEXT("Reject")));
    TestTrue(TEXT("Acquire cancel probe"),W.P->TryAcquirePerk(TEXT("One")));
    const auto Press=W.Handle(0,2);TestTrue(TEXT("Start cancel probe"),W.P->BeginSkillSlot(Press));
    auto* A=W.Probe(Press);if(!TestNotNull(TEXT("Cancel action exists"),A)) return false;
    TestTrue(TEXT("Canceled routes even after depletion"),W.P->CancelSkillInput(Press));
    TestEqual(TEXT("One cleanup"),A->Finishes,1);A->Finish();TestEqual(TEXT("Repeated Finish harmless"),A->Finishes,1);
    TestNull(TEXT("Cancel releases presentation lease"),W.H->GetActiveSkillPresentation());
    TestTrue(TEXT("Consumed charge not refunded"),W.P->ActiveSkillSlots[0].PerkID.IsNone());
    TestFalse(TEXT("Canceled release cannot start action again"),W.P->ReleaseSkillInput(Press));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCActiveConcurrentChargesTest,"MessControl.Skills.Protocol.ConcurrentChargesRouteExactPress",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCActiveConcurrentChargesTest::RunTest(const FString&)
{
    FActiveProtocolWorld W;if(!TestTrue(TEXT("Acquire three charges of one instance"),W.P->TryAcquirePerk(TEXT("Three")))) return false;
    const auto First=W.Handle(0,1);const auto Second=W.Handle(0,2);
    if(!TestTrue(TEXT("First charged action starts"),W.P->BeginSkillSlot(First))) return false;
    auto* A=W.Probe(First);if(!TestNotNull(TEXT("First hazard exists"),A)) return false;
    TestFalse(TEXT("A second press cannot bypass an outstanding first press"),W.P->BeginSkillSlot(Second));
    TestEqual(TEXT("Rejected overlap does not debit a second charge"),W.P->ActiveSkillSlots[0].Charges,2);
    TestTrue(TEXT("First press completes and releases presentation"),W.P->ReleaseSkillInput(First));
    if(!TestTrue(TEXT("Second charge can start while first released hazard is still alive"),W.P->BeginSkillSlot(Second))) return false;
    auto* B=W.Probe(Second);if(!TestNotNull(TEXT("Second hazard exists by exact press"),B)) return false;
    TestTrue(TEXT("Same instance now owns two distinct accepted actions"),A!=B && !A->IsFinished() && !B->IsFinished() && W.P->GetActiveSkillActions().Num()==2);
    TestEqual(TEXT("Two accepted uses debit exactly two charges"),W.P->ActiveSkillSlots[0].Charges,1);
    TestFalse(TEXT("An exact duplicate first press cannot debit again"),W.P->BeginSkillSlot(First));
    TestFalse(TEXT("Old Completed cannot complete a newer action from the same instance"),W.P->ReleaseSkillInput(First));
    TestEqual(TEXT("Second action still has no Completed input"),B->Releases,0);
    auto Wrong=Second;Wrong.SlotIndex=1;
    TestFalse(TEXT("Wrong-slot handle cannot release the same instance/sequence"),W.P->ReleaseSkillInput(Wrong));
    A->Finish();
    TestTrue(TEXT("Finishing first hazard leaves the second press routable"),W.P->ReleaseSkillInput(Second));
    TestEqual(TEXT("Second action receives exactly its own Completed input"),B->Releases,1);
    TestFalse(TEXT("Second Completed remains sealed after another action unregisters"),W.P->ReleaseSkillInput(Second));
    const auto Third=W.Handle(0,3);
    if(!TestTrue(TEXT("A third charge starts alongside second released hazard"),W.P->BeginSkillSlot(Third))) return false;
    auto* C=W.Probe(Third);if(!TestNotNull(TEXT("Third hazard exists"),C)) return false;
    TestTrue(TEXT("Third accepted use clears the last charge's slot"),W.P->ActiveSkillSlots[0].PerkID.IsNone());
    TestTrue(TEXT("Third action receives only its own Completed input"),W.P->ReleaseSkillInput(Third));
    C->Finish();
    TestFalse(TEXT("Unregistering another action cannot unseal second Completed"),W.P->ReleaseSkillInput(Second));
    TestFalse(TEXT("Unregistering another action cannot turn second Completed into Canceled"),W.P->CancelSkillInput(Second));
    TestEqual(TEXT("Second action receives no duplicate release after sibling cleanup"),B->Releases,1);
    // Refill after depletion with a fresh instance, then cancel only its own
    // accepted press while the preceding instance's hazard is still alive.
    TestTrue(TEXT("A depleted slot accepts a distinct cancel fixture"),W.P->TryAcquirePerk(TEXT("One")));
    const auto CancelPress=W.Handle(0,4);
    if(!TestTrue(TEXT("Cancel fixture starts alongside the older released hazard"),W.P->BeginSkillSlot(CancelPress))) return false;
    auto* D=W.Probe(CancelPress);if(!TestNotNull(TEXT("Cancel fixture has its own exact handle"),D)) return false;
    TestTrue(TEXT("Canceled routes to exactly the new press"),W.P->CancelSkillInput(CancelPress));
    TestEqual(TEXT("Only new press receives Canceled"),D->Cancels,1);
    TestEqual(TEXT("Older completed hazard is not canceled by another handle"),B->Cancels,0);
    TestFalse(TEXT("Old accepted hazard survives another press's cancellation"),B->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCActiveOccupiedReplacementTest,"MessControl.Skills.Protocol.OccupiedReplacementTargetsInstance",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCActiveOccupiedReplacementTest::RunTest(const FString&)
{
    FActiveProtocolWorld W;TestTrue(TEXT("Acquire original last charge"),W.P->TryAcquirePerk(TEXT("One")));
    const auto OldPress=W.Handle(0,1);if(!TestTrue(TEXT("Old instance commits"),W.P->BeginSkillSlot(OldPress))) return false;
    auto* Old=W.Probe(OldPress);if(!TestNotNull(TEXT("Old accepted action exists"),Old)) return false;
    TestTrue(TEXT("Empty slot receives a new multi-charge instance"),W.P->TryAcquirePerk(TEXT("Three")));
    TestTrue(TEXT("Old press releases through the now-refilled slot"),W.P->ReleaseSkillInput(OldPress));
    const auto CurrentPress=W.Handle(0,2);if(!TestTrue(TEXT("Current occupied instance commits"),W.P->BeginSkillSlot(CurrentPress))) return false;
    auto* Current=W.Probe(CurrentPress);if(!TestNotNull(TEXT("Current action exists"),Current)) return false;
    TestTrue(TEXT("Occupied replacement validates exact previous ID and instance"),
        W.P->TryAcquirePerk(TEXT("Other"),0,TEXT("Three"),CurrentPress.SlotInstanceID));
    TestEqual(TEXT("Replacement cancels its actual occupied instance once"),Current->Cancels,1);
    TestEqual(TEXT("Replacement closes its actual occupied action once"),Current->Finishes,1);
    TestTrue(TEXT("Occupied action records the explicit replacement reason"),Current->LastReason==EMCActiveSkillEndReason::Replaced);
    TestEqual(TEXT("An older accepted instance sharing the slot index is not canceled"),Old->Cancels,0);
    TestFalse(TEXT("Older released hazard remains alive independently"),Old->IsFinished());
    const auto NewPress=W.Handle(0,3);if(!TestTrue(TEXT("Replacement item starts after cleanup"),W.P->BeginSkillSlot(NewPress))) return false;
    auto* New=W.Probe(NewPress);if(!TestNotNull(TEXT("Replacement action exists"),New)) return false;
    TestFalse(TEXT("Replaced old handle cannot release the new occupant"),W.P->ReleaseSkillInput(CurrentPress));
    TestFalse(TEXT("Completed older instance cannot release the new occupant"),W.P->ReleaseSkillInput(OldPress));
    TestEqual(TEXT("New occupant has received no stale Completed"),New->Releases,0);
    TestTrue(TEXT("Replacement's own handle still completes normally"),W.P->ReleaseSkillInput(NewPress));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCActiveFinishedReplayTest,"MessControl.Skills.Protocol.FinishedPressReplayAndConsumableCooldown",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCActiveFinishedReplayTest::RunTest(const FString&)
{
    {
        FActiveProtocolWorld W;TestTrue(TEXT("Acquire replay fixture"),W.P->TryAcquirePerk(TEXT("Three")));
        const auto First=W.Handle(0,1);TestTrue(TEXT("First action starts"),W.P->BeginSkillSlot(First));
        auto* A=W.Probe(First);if(!TestNotNull(TEXT("Replay fixture action exists"),A)) return false;
        W.P->ReleaseSkillInput(First);A->Finish();
        TestTrue(TEXT("No live action remains to mask the replay test"),W.P->GetActiveSkillActions().IsEmpty());
        TestFalse(TEXT("Replaying a finished accepted press cannot spend another charge"),W.P->BeginSkillSlot(First));
        TestEqual(TEXT("Finished press replay preserves the remaining charges"),W.P->ActiveSkillSlots[0].Charges,2);
        TestTrue(TEXT("A genuinely new press still works on that instance"),W.P->BeginSkillSlot(W.Handle(0,2)));
    }
    {
        FActiveProtocolWorld W;TestTrue(TEXT("Acquire timed consumable"),W.P->TryAcquirePerk(TEXT("Timed")));
        const auto First=W.Handle(0,1);TestTrue(TEXT("Timed first action starts"),W.P->BeginSkillSlot(First));
        TestTrue(TEXT("Timed first action releases its presentation"),W.P->ReleaseSkillInput(First));
        TestFalse(TEXT("A released hazard does not bypass a consumable's new-use cooldown"),W.P->BeginSkillSlot(W.Handle(0,2)));
        TestEqual(TEXT("Cooldown rejection preserves remaining charges"),W.P->ActiveSkillSlots[0].Charges,2);
        TestTrue(TEXT("Authored cooldown remains active"),W.P->GetSkillCooldownRemaining(0)>29.f);
    }
    return true;
}
#endif
