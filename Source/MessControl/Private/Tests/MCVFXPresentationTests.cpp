#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXPresentationLibrary.h"
#include "MCPreloadManifest.h"
#include "MCGameState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MCVFXTestWorld.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"
#include "UObject/Class.h"

namespace MCEventPresentationTests
{
struct FWorld
{
    UWorld* World=MCVFXTestWorld::Create(EWorldType::EditorPreview,false);
    AActor* Owner=nullptr;
    UMCVFXPresentationComponent* Component=nullptr;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
        Owner=World->SpawnActor<AActor>();Component=NewObject<UMCVFXPresentationComponent>(Owner);
        Component->SourceID=TEXT("EditorOnly.NutEnemyDeath");Component->Events={TEXT("Death")};
        Owner->AddInstanceComponent(Component);Component->RegisterComponent();
    }
    ~FWorld(){if(World){GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXSemanticEvents,
    "MessControl.VFX.Presentation.ProviderEventContract",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXSemanticEvents::RunTest(const FString&)
{
    auto* Profile=NewObject<UMCPerkVFXProfile>();FMCPerkVFXSlot Disabled;Disabled.Mode=EMCPerkVFXSlotMode::Disabled;
    Profile->Slots.Add(TEXT("Death"),Disabled);FString Error;
    TestTrue(TEXT("Declared Death event needs no fabricated action phases"),Profile->ValidateForEvents({TEXT("Death")},Error));
    TestFalse(TEXT("Legacy action validation still rejects Death"),Profile->Validate(Error));
    TestFalse(TEXT("Another provider cannot accept Death"),Profile->ValidateForEvents({TEXT("CleanComplete")},Error));
    TestFalse(TEXT("Duplicate provider event IDs are rejected"),Profile->ValidateForEvents({TEXT("Death"),TEXT("Death")},Error));
    Profile->Slots.Reset();Profile->Slots.Add(TEXT("Impact"),Disabled);
    TestTrue(TEXT("Legacy action Impact still validates"),Profile->Validate(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXPreparedResources,
    "MessControl.VFX.Presentation.IndependentPreparedResources",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXPreparedResources::RunTest(const FString&)
{
    using namespace MCEventPresentationTests;FWorld Test;
    auto* Original=NewObject<UNiagaraSystem>();
    Original->GetExposedParameters().SetParameterValue(2.f,FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.Gain")),true);
    auto* Authored=NewObject<UMCVFXComposition>();FMCVFXCompositionLayer Layer;
    Layer.System=Original;Authored->Layers.Add(Layer);
    auto* Profile=NewObject<UMCPerkVFXProfile>();FMCPerkVFXSlot Slot;
    Slot.Mode=EMCPerkVFXSlotMode::Composition;Slot.Composition=Authored;Profile->Slots.Add(TEXT("Death"),Slot);FString Error;
    if(!TestTrue(TEXT("Generic component prepares without an active action"),Test.Component->PrepareProfileOverride(Profile,Error)))
    {AddError(Error);return false;}
    Authored->Layers[0].System=NewObject<UNiagaraSystem>();Profile->Slots[TEXT("Death")].Mode=EMCPerkVFXSlotMode::Disabled;
    TArray<UNiagaraSystem*> Systems;Test.Component->GetPreparedResources(Systems);
    TestTrue(TEXT("Accepted prepared resources retain the original source after author edits"),Systems.Num()==1 && Systems[0]==Original);
    TestEqual(TEXT("Preparation leaves original Niagara defaults intact"),Original->GetExposedParameters().GetParameterValue<float>(
        FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.Gain"))),2.f);
    auto* Invalid=NewObject<UMCPerkVFXProfile>();Invalid->Slots.Add(TEXT("Undeclared"),Slot);
    TestFalse(TEXT("Undeclared replacement fails atomically"),Test.Component->PrepareProfileOverride(Invalid,Error));
    Test.Component->GetPreparedResources(Systems);
    TestTrue(TEXT("Failed preparation preserves prior resource snapshot"),Systems.Num()==1 && Systems[0]==Original);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXLibraryManifest,
    "MessControl.VFX.Presentation.LibraryManifestWaves",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXLibraryManifest::RunTest(const FString&)
{
    auto* Library=NewObject<UMCVFXPresentationLibrary>();
    const FSoftObjectPath ProfilePath(TEXT("/Game/Gameplay/VFX/Profiles/DA_DeathProfile.DA_DeathProfile"));
    Library->Profiles.Add(TEXT("EditorOnly.NutEnemyDeath"),TSoftObjectPtr<UMCPerkVFXProfile>(ProfilePath));
    TArray<FSoftObjectPath> Paths;FMCPreloadManifest::CollectSoftReferences(Library,Paths);
    TestTrue(TEXT("Location library map is a reflected runtime profile root"),Paths.Contains(ProfilePath));
    auto* Profile=NewObject<UMCPerkVFXProfile>();
    const FSoftObjectPath RuntimePath(TEXT("/Game/Gameplay/VFX/Compositions/DA_DeathComposition.DA_DeathComposition"));
    FMCPerkVFXSlot Slot;Slot.Mode=EMCPerkVFXSlotMode::Composition;Slot.Composition=RuntimePath;
    Profile->Slots.Add(TEXT("Death"),Slot);Paths.Reset();FMCPreloadManifest::CollectSoftReferences(Profile,Paths);
    TestTrue(TEXT("Semantic event reaches runtime composition with no active-stage special case"),Paths.Contains(RuntimePath));
    TestEqual(TEXT("Mouth root is the explicitly authored location library"),UMCVFXPresentationLibrary::MouthLibraryPath().ToString(),
        FString(TEXT("/Game/Gameplay/VFX/Profiles/DA_MouthPresentation.DA_MouthPresentation")));
    TestNull(TEXT("Pure manifest traversal did not load the fixture"),ProfilePath.ResolveObject());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXLateReceipt,
    "MessControl.VFX.Presentation.LateReceiptSchedule",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXLateReceipt::RunTest(const FString&)
{
    using namespace MCEventPresentationTests;FWorld Test;FString Error;
    auto* Recipe=NewObject<UMCVFXComposition>();FMCVFXCompositionLayer Layer;
    Layer.System=NewObject<UNiagaraSystem>();Layer.StartSeconds=2.f;Layer.EmissionSeconds=.25f;Recipe->Layers.Add(Layer);
    FMCPreparedPerkVFXComposition Data;Data.Snapshot=Recipe;Data.Systems.Add(Layer.System.Get());
    auto* Player=Test.World->SpawnActor<AMCPerkVFXCompositionPlayer>();
    TestTrue(TEXT("Late receipt uses elapsed server timeline"),Player->StartPrepared(Data,FTransform::Identity,nullptr,nullptr,false,FMCPerkVFXGameplayValues(),Error,1.f));
    TestEqual(TEXT("Timeline begins at server elapsed time"),Player->GetPlaybackTime(),1.f);
    TestEqual(TEXT("Not-yet-due layer remains pending"),Player->GetPendingLayerCount(),1);
    Player->StopEmission(true);
    auto* Expired=Test.World->SpawnActor<AMCPerkVFXCompositionPlayer>();
    TestTrue(TEXT("Expired explicit layer is consumed without replay"),Expired->StartPrepared(Data,FTransform::Identity,nullptr,nullptr,false,FMCPerkVFXGameplayValues(),Error,3.f));
    TestEqual(TEXT("Expired layer has no pending start"),Expired->GetPendingLayerCount(),0);
    TestEqual(TEXT("Expired layer has no active renderer"),Expired->GetLiveLayerCount(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXExplicitWorldReset,
    "MessControl.VFX.Presentation.ExplicitResetOwnsOnlyEventPlayers",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXExplicitWorldReset::RunTest(const FString&)
{
    using namespace MCEventPresentationTests;FWorld Test;FString Error;
    auto* Recipe=NewObject<UMCVFXComposition>();Recipe->DurationSeconds=10.f;
    FMCVFXCompositionLayer Layer;Layer.System=NewObject<UNiagaraSystem>();Layer.StartSeconds=5.f;Recipe->Layers.Add(Layer);
    FMCPreparedPerkVFXComposition Data;Data.Snapshot=Recipe;Data.Systems.Add(Layer.System.Get());
    auto* AcceptedDeath=Test.World->SpawnActor<AMCPerkVFXCompositionPlayer>();
    auto* IndependentAction=Test.World->SpawnActor<AMCPerkVFXCompositionPlayer>();
    // Explicit provenance of the component's factory. The companion player has
    // the same class/timeline but belongs to an independent active action.
    AcceptedDeath->Tags.Add(TEXT("MC.VFXPresentationComponent.Player"));
    if(!TestTrue(TEXT("Accepted event has a real pending native schedule"),AcceptedDeath->StartPrepared(
        Data,FTransform::Identity,nullptr,nullptr,false,FMCPerkVFXGameplayValues(),Error))
        || !TestTrue(TEXT("Independent action has its own pending native schedule"),IndependentAction->StartPrepared(
            Data,FTransform::Identity,nullptr,nullptr,false,FMCPerkVFXGameplayValues(),Error)))return false;
    Test.Owner->Destroy();
    TestTrue(TEXT("Accepted world-space event survives destruction of its original owner"),AcceptedDeath->IsRunning());
    UMCVFXPresentationComponent::StopWorldPresentations(Test.World);
    TestFalse(TEXT("Explicit reset stops the event player even after its original actor lifetime"),AcceptedDeath->IsRunning());
    TestEqual(TEXT("Reset cancels every future event-layer start"),AcceptedDeath->GetPendingLayerCount(),0);
    TestTrue(TEXT("Reset does not stop independent action players of the same class"),IndependentAction->IsRunning());
    TestEqual(TEXT("Independent schedule remains intact"),IndependentAction->GetPendingLayerCount(),1);
    IndependentAction->StopEmission(true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXReplicatedEpochReset,
    "MessControl.VFX.Presentation.RunEpochCancelsOwnerlessTails",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXReplicatedEpochReset::RunTest(const FString&)
{
    using namespace MCEventPresentationTests;FWorld Test;FString Error;
    auto* State=Test.World->SpawnActor<AMCGameState>();Test.World->SetGameState(State);
    State->GameplayLoadId=FGuid::NewGuid();const FGuid AcceptedEpoch=State->GameplayLoadId;
    auto* Recipe=NewObject<UMCVFXComposition>();Recipe->DurationSeconds=10.f;
    FMCVFXCompositionLayer Layer;Layer.System=NewObject<UNiagaraSystem>();Layer.StartSeconds=5.f;Recipe->Layers.Add(Layer);
    FMCPreparedPerkVFXComposition Data;Data.Snapshot=Recipe;Data.Systems.Add(Layer.System.Get());
    auto* AcceptedDeath=Test.World->SpawnActor<AMCPerkVFXCompositionPlayer>();
    auto* IndependentAction=Test.World->SpawnActor<AMCPerkVFXCompositionPlayer>();
    TestFalse(TEXT("Mismatched run epoch cannot bind a stale/new event to this run"),AcceptedDeath->BindToPresentationEpoch(FGuid::NewGuid(),Error));
    if(!TestTrue(TEXT("Accepted event binds to the actual GameState run epoch"),AcceptedDeath->BindToPresentationEpoch(AcceptedEpoch,Error))
        || !TestTrue(TEXT("Bound event starts its native pending schedule"),AcceptedDeath->StartPrepared(
            Data,FTransform::Identity,nullptr,nullptr,false,FMCPerkVFXGameplayValues(),Error))
        || !TestTrue(TEXT("Ordinary active player remains independently scheduled"),IndependentAction->StartPrepared(
            Data,FTransform::Identity,nullptr,nullptr,false,FMCPerkVFXGameplayValues(),Error)))return false;
    Test.Owner->Destroy();AcceptedDeath->Tick(.1f);
    TestTrue(TEXT("Natural owner removal does not cancel a current-run tail"),AcceptedDeath->IsRunning());
    // The same replicated field changes on rendering clients; no GameMode or
    // extra multicast is needed there to invalidate the old local player.
    State->GameplayLoadId=FGuid::NewGuid();AcceptedDeath->Tick(.1f);
    TestFalse(TEXT("Changed run identity cancels an ownerless old-run event"),AcceptedDeath->IsRunning());
    TestEqual(TEXT("No delayed old-run layer can appear after reset"),AcceptedDeath->GetPendingLayerCount(),0);
    TestTrue(TEXT("The optional presentation epoch does not alter ordinary unbound active players"),IndependentAction->IsRunning());
    IndependentAction->StopEmission(true);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXOrderedDeliveryTest,
    "MessControl.VFX.Presentation.OrderedDeliveryDeduplicatesInitialSnapshot",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXOrderedDeliveryTest::RunTest(const FString&)
{
    using namespace MCEventPresentationTests;FWorld Test;
    auto* State=Test.World->SpawnActor<AMCGameState>();Test.World->SetGameState(State);
    State->GameplayLoadId=FGuid::NewGuid();const FGuid NextEpoch=FGuid::NewGuid();
    auto* Component=Test.Component;Component->Events={TEXT("Hit"),TEXT("Break")};
    int32 NativeEvents=0;Component->OnEventAccepted.AddLambda([&](FName,EMCVFXPresentationResult){++NativeEvents;});
    auto* NS=NewObject<UNiagaraSystem>();
    auto* Recipe=NewObject<UMCVFXComposition>();FMCVFXCompositionLayer Layer;Layer.System=NS;Recipe->Layers.Add(Layer);
    FMCVFXPresentationReceipt Hit;Hit.Serial=1;Hit.SourceID=Component->SourceID;Hit.Event=TEXT("Hit");
    Hit.Slot.Mode=EMCPerkVFXSlotMode::Composition;Hit.Slot.Composition=Recipe;Hit.GameplayEpoch=NextEpoch;
    auto Break=Hit;Break.Serial=2;Break.Event=TEXT("Break");
    // Exercise the very same receiver used by reliable delivery and the
    // initial-only late-join snapshot. No network session or visual is faked.
    Component->MulticastReceipt_Implementation(Hit);Component->MulticastReceipt_Implementation(Break);
    Component->LastReceipt=Break;Component->OnRep_Receipt();
    Component->MulticastReceipt_Implementation(Hit);
    Recipe->Layers[0].Transform.SetTranslation(FVector(900,0,0));
    TestEqual(TEXT("Two accepted custom events survive waiting for their replicated epoch"),Component->GetPendingReceiptCount(),2);
    TestEqual(TEXT("Late snapshot / duplicate reliable delivery do not add another copy"),Component->GetHighestReceivedSerial(),uint32(2));
    if(Component->PendingReceipts.Num()==2) {
        TestEqual(TEXT("First accepted event remains first"),Component->PendingReceipts[0].Receipt.Event,FName(TEXT("Hit")));
        TestEqual(TEXT("Break remains a separate ordered event"),Component->PendingReceipts[1].Receipt.Event,FName(TEXT("Break")));
        TestTrue(TEXT("Queued accepted data is an immutable native snapshot while waiting for its epoch"),
            Component->PendingReceipts[0].bPrepared
            && Component->PendingReceipts[0].Prepared.Snapshot!=Recipe
            && Component->PendingReceipts[0].Prepared.Snapshot->Layers[0].Transform.GetTranslation().IsNearlyZero());
    }
    Component->LastCancel.ThroughSerial=1;Component->LastCancel.bImmediate=true;Component->OnRep_Cancel();
    TestEqual(TEXT("Cancellation removes pending old Hit without deleting later Break"),Component->GetPendingReceiptCount(),1);
    if(!Component->PendingReceipts.IsEmpty())TestEqual(TEXT("Later Break is retained"),Component->PendingReceipts[0].Receipt.Serial,uint32(2));
    Component->MulticastReceipt_Implementation(Hit);
    TestEqual(TEXT("Canceled Hit cannot be resurrected by replay"),Component->GetPendingReceiptCount(),1);
    TestEqual(TEXT("Cosmetic receive/replay never manufactures another accepted gameplay event"),NativeEvents,0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXCancellationWatermarkTest,
    "MessControl.VFX.Presentation.CancelWatermarkPreservesLaterPlayers",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXCancellationWatermarkTest::RunTest(const FString&)
{
    using namespace MCEventPresentationTests;FWorld Test;FString Error;
    auto* Recipe=NewObject<UMCVFXComposition>();Recipe->DurationSeconds=10.f;
    FMCVFXCompositionLayer Layer;Layer.System=NewObject<UNiagaraSystem>();Layer.StartSeconds=5.f;Recipe->Layers.Add(Layer);
    FMCPreparedPerkVFXComposition Data;Data.Snapshot=Recipe;Data.Systems.Add(Layer.System.Get());
    auto* OldHit=Test.World->SpawnActor<AMCPerkVFXCompositionPlayer>();
    auto* LaterBreak=Test.World->SpawnActor<AMCPerkVFXCompositionPlayer>();
    if(!TestTrue(TEXT("Old receipt has a real pending player"),OldHit->StartPrepared(
        Data,FTransform::Identity,nullptr,nullptr,false,FMCPerkVFXGameplayValues(),Error))
        || !TestTrue(TEXT("Later receipt has an independent real player"),LaterBreak->StartPrepared(
            Data,FTransform::Identity,nullptr,nullptr,false,FMCPerkVFXGameplayValues(),Error)))return false;
    FMCVFXPresentationPlayerHandle OldHandle;OldHandle.Player=OldHit;OldHandle.SourceID=Test.Component->SourceID;
    auto LaterHandle=OldHandle;LaterHandle.Player=LaterBreak;
    Test.Component->Players.Add(1,OldHandle);Test.Component->Players.Add(2,LaterHandle);
    FMCVFXPresentationCancel Cancel;Cancel.ThroughSerial=1;Cancel.bImmediate=true;
    Test.Component->MulticastCancel_Implementation(Cancel);
    TestFalse(TEXT("Reliable cancellation stops the exact earlier player"),OldHit->IsRunning());
    TestEqual(TEXT("Canceled earlier player has no delayed layer left"),OldHit->GetPendingLayerCount(),0);
    TestTrue(TEXT("Late old cancellation cannot stop a later Break"),LaterBreak->IsRunning());
    Test.Component->LastCancel=Cancel;Test.Component->OnRep_Cancel();
    TestTrue(TEXT("Initial late-join cancel snapshot is idempotent for a later player"),LaterBreak->IsRunning());
    Cancel.ThroughSerial=2;Test.Component->MulticastCancel_Implementation(Cancel);
    TestFalse(TEXT("A later watermark can explicitly cancel that later receipt"),LaterBreak->IsRunning());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXBoundedDeliveryTest,
    "MessControl.VFX.Presentation.DeferredDeliveryIsBounded",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXBoundedDeliveryTest::RunTest(const FString&)
{
    using namespace MCEventPresentationTests;FWorld Test;FString Error;
    auto* State=Test.World->SpawnActor<AMCGameState>();Test.World->SetGameState(State);
    State->GameplayLoadId=FGuid::NewGuid();
    FMCVFXPresentationReceipt Receipt;Receipt.SourceID=Test.Component->SourceID;Receipt.Event=TEXT("Death");
    Receipt.Slot.Mode=EMCPerkVFXSlotMode::Replace;Receipt.Slot.System=NewObject<UNiagaraSystem>();Receipt.GameplayEpoch=FGuid::NewGuid();
    for(uint32 Serial=1;Serial<=32;++Serial) {
        Receipt.Serial=Serial;
        if(!Test.Component->ReceiveReceipt(Receipt,nullptr,Error,true)){AddError(Error);return false;}
    }
    Receipt.Serial=33;
    TestFalse(TEXT("Deferred epoch queue rejects receipt 33 instead of silently growing"),Test.Component->ReceiveReceipt(Receipt,nullptr,Error,true));
    TestEqual(TEXT("Explicit pending queue bound is retained"),Test.Component->GetPendingReceiptCount(),32);
    TestEqual(TEXT("Capacity rejection does not consume the receipt serial"),Test.Component->GetHighestReceivedSerial(),uint32(32));
    FMCVFXPresentationCancel Cancel;Cancel.ThroughSerial=16;Cancel.bImmediate=true;Test.Component->ReceiveCancel(Cancel);
    TestTrue(TEXT("Delivery can resume after explicit cancellation frees capacity"),Test.Component->ReceiveReceipt(Receipt,nullptr,Error,true));
    TestEqual(TEXT("Old canceled events plus one later event give a bounded queue"),Test.Component->GetPendingReceiptCount(),17);
    TestEqual(TEXT("Later event is appended after remaining earlier events"),Test.Component->PendingReceipts.Last().Receipt.Serial,uint32(33));
    const auto* Function=Test.Component->FindFunction(TEXT("MulticastReceipt"));
    TestTrue(TEXT("Live custom transport is reliable multicast"),Function && Function->HasAllFunctionFlags(FUNC_Net|FUNC_NetReliable|FUNC_NetMulticast));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXEpochSupersessionTest,
    "MessControl.VFX.Presentation.UnobservedOldEpochCannotBlockCurrent",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXEpochSupersessionTest::RunTest(const FString&)
{
    using namespace MCEventPresentationTests;FWorld Test;FString Error;
    auto* State=Test.World->SpawnActor<AMCGameState>();Test.World->SetGameState(State);
    State->GameplayLoadId=FGuid::NewGuid();const FGuid Current=State->GameplayLoadId;
    const FGuid UnobservedOld=FGuid::NewGuid(),Future=FGuid::NewGuid();
    FMCVFXPresentationReceipt Receipt;Receipt.SourceID=Test.Component->SourceID;Receipt.Event=TEXT("Death");
    Receipt.Slot.Mode=EMCPerkVFXSlotMode::Replace;
    Receipt.Slot.System=FSoftObjectPath(TEXT("/Game/Tests/VFXPractice/TransportFixture_Unloaded.TransportFixture_Unloaded"));
    Receipt.Serial=1;Receipt.GameplayEpoch=UnobservedOld;
    if(!Test.Component->ReceiveReceipt(Receipt,nullptr,Error,true)){AddError(Error);return false;}
    TestFalse(TEXT("Fixture really did not observe the older epoch"),Test.Component->RetiredEpochs.Contains(UnobservedOld));
    Receipt.Serial=2;Receipt.GameplayEpoch=Current;
    if(!Test.Component->ReceiveReceipt(Receipt,nullptr,Error,true)){AddError(Error);return false;}
    TestEqual(TEXT("A later current-run receipt supersedes the stale blocking prefix immediately"),Test.Component->GetPendingReceiptCount(),1);
    if(!Test.Component->PendingReceipts.IsEmpty())
        TestEqual(TEXT("Current event is the remaining head, waiting only for its real resource"),Test.Component->PendingReceipts[0].Receipt.Serial,uint32(2));
    Receipt.Serial=3;Receipt.GameplayEpoch=Future;
    if(!Test.Component->ReceiveReceipt(Receipt,nullptr,Error,true)){AddError(Error);return false;}
    TestEqual(TEXT("A genuinely later future-run receipt is preserved"),Test.Component->GetPendingReceiptCount(),2);
    FMCVFXPresentationCancel Cancel;Cancel.ThroughSerial=2;Cancel.bImmediate=true;Test.Component->ReceiveCancel(Cancel);
    TestEqual(TEXT("After old/current cancellation the future receipt still waits"),Test.Component->GetPendingReceiptCount(),1);
    if(!Test.Component->PendingReceipts.IsEmpty())
        TestEqual(TEXT("Future epoch is not silently discarded as stale"),Test.Component->PendingReceipts[0].Receipt.GameplayEpoch,Future);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXMultiSourceOverridesTest,
    "MessControl.VFX.Presentation.MultiSourceOverridesSurviveCanonicalPreparation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXMultiSourceOverridesTest::RunTest(const FString&)
{
    using namespace MCEventPresentationTests;FWorld Test;FString Error;
    auto* Component=Test.Component;Component->SourceID=NAME_None;Component->Events.Reset();Component->Library.Reset();
    const FName A(TEXT("B2_Shield")),B(TEXT("B2_ThunderGod"));
    auto* Override=NewObject<UMCPerkVFXProfile>();FMCPerkVFXSlot Disabled;Disabled.Mode=EMCPerkVFXSlotMode::Disabled;
    Override->Slots.Add(TEXT("Proc"),Disabled);
    if(!TestTrue(TEXT("Source-scoped working override prepares before acquisition"),Component->PrepareScopedOverride(A,{TEXT("Proc")},Override,Error))
        || !TestTrue(TEXT("Another source independently prepares its native default"),Component->PrepareSourceProfile(B,{TEXT("Link")},nullptr,Error,false))
        || !TestTrue(TEXT("Canonical grant preparation retains the already prepared working override"),Component->PrepareSourceProfile(A,{TEXT("Proc")},nullptr,Error,false)))
    {AddError(Error);return false;}
    TArray<FName> AcceptedSources;
    Component->OnSourceEventAccepted.AddLambda([&](FName ID,FName,EMCVFXPresentationResult){AcceptedSources.Add(ID);});
    TestTrue(TEXT("First source keeps its explicit override"),Component->PlaySourceEvent(A,TEXT("Proc"),FTransform::Identity)==EMCVFXPresentationResult::Suppressed);
    TestTrue(TEXT("Another source retains native default without inheriting that override"),Component->PlaySourceEvent(B,TEXT("Link"),FTransform::Identity)==EMCVFXPresentationResult::UseDefault);
    TestTrue(TEXT("Actual accepted callback identifies both sources"),AcceptedSources.Num()==2 && AcceptedSources[0]==A && AcceptedSources[1]==B);
    const auto IDs=Component->GetPreparedSourceIDs();
    TestTrue(TEXT("One component holds both independent source contracts"),IDs.Num()==2 && IDs.Contains(A) && IDs.Contains(B));
    Component->ClearScopedOverride(A);
    TestTrue(TEXT("Clearing one preview source does not invalidate another"),!Component->IsSourcePrepared(A) && Component->IsSourcePrepared(B));
    if(!Component->PrepareSourceProfile(A,{TEXT("Proc")},nullptr,Error,false)){AddError(Error);return false;}
    TestTrue(TEXT("After explicit scope clear canonical preparation resumes"),Component->PlaySourceEvent(A,TEXT("Proc"),FTransform::Identity)==EMCVFXPresentationResult::UseDefault);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXEpisodeIsolationTest,
    "MessControl.VFX.Presentation.SourceEpisodeCancellationIsIsolated",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXEpisodeIsolationTest::RunTest(const FString&)
{
    using namespace MCEventPresentationTests;FWorld Test;FString Error;
    auto* Component=Test.Component;Component->SourceID=NAME_None;Component->Events.Reset();Component->Library.Reset();
    const FName A(TEXT("B2_LastingFormula")),B(TEXT("B2_SecondWind"));
    if(!Component->PrepareSourceProfile(A,{TEXT("Coat")},nullptr,Error,false)
        || !Component->PrepareSourceProfile(B,{TEXT("Proc")},nullptr,Error,false)){AddError(Error);return false;}
    auto* State=Test.World->SpawnActor<AMCGameState>();Test.World->SetGameState(State);State->GameplayLoadId=FGuid::NewGuid();
    const FGuid WaitingEpoch=FGuid::NewGuid(),EpisodeA=FGuid::NewGuid(),EpisodeB=FGuid::NewGuid();
    FMCVFXPresentationReceipt Receipt;Receipt.Serial=1;Receipt.SourceID=A;Receipt.Event=TEXT("Coat");Receipt.EpisodeID=EpisodeA;
    Receipt.GameplayEpoch=WaitingEpoch;Receipt.Slot.Mode=EMCPerkVFXSlotMode::Replace;Receipt.Slot.System=NewObject<UNiagaraSystem>();
    if(!Component->ReceiveReceipt(Receipt,nullptr,Error,true)){AddError(Error);return false;}
    Receipt.Serial=2;Receipt.EpisodeID=EpisodeB;
    if(!Component->ReceiveReceipt(Receipt,nullptr,Error,true)){AddError(Error);return false;}
    Receipt.Serial=3;Receipt.SourceID=B;Receipt.Event=TEXT("Proc");Receipt.EpisodeID.Invalidate();
    if(!Component->ReceiveReceipt(Receipt,nullptr,Error,true)){AddError(Error);return false;}
    Component->StopSourceEpisode(A,EpisodeA,true);
    TestEqual(TEXT("Ending one coating leaves another coating and another source queued"),Component->GetPendingReceiptCount(),2);
    TestFalse(TEXT("Ended coating cannot remain pending"),Component->PendingReceipts.ContainsByPredicate([&](const auto& P){return P.Receipt.SourceID==A && P.Receipt.EpisodeID==EpisodeA;}));
    TestTrue(TEXT("Other coating episode is retained"),Component->PendingReceipts.ContainsByPredicate([&](const auto& P){return P.Receipt.SourceID==A && P.Receipt.EpisodeID==EpisodeB;}));
    Component->StopSource(A,true);
    TestEqual(TEXT("Source removal cancels all of its episodes, not another source"),Component->GetPendingReceiptCount(),1);
    TestTrue(TEXT("Independent source survives"),Component->PendingReceipts[0].Receipt.SourceID==B);
    return true;
}
#endif
