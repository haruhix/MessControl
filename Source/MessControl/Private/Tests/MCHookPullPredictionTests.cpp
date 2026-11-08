#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCHookPullPrediction.h"
#include "MCActiveSkillMotion.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include <limits>

namespace MCHookPullPredictionTests
{
FMCHookPullReceipt Receipt()
{
    FMCHookPullReceipt R;
    R.ActionID=FGuid(1,2,3,4);R.Epoch=FGuid(5,6,7,8);R.Revision=11;
    R.bActive=true;R.bFreeFlight=false;R.PreviousMode=MOVE_Walking;R.PreviousCustomMode=3;
    R.Goal=FVector(200,30,400);R.Speed=850;R.StopDistance=120;R.Duration=2.5f;
    R.StartMoveTime=12.25f;R.SourceTime=.375f;R.ServerSourceID=17;
    return R;
}
bool IsExactHook(const TSharedPtr<FRootMotionSource>& S,const FMCHookPullReceipt& R)
{
    if(!MCHookPullPrediction::IsHook(S)) return false;
    const auto* H=static_cast<const FMCHookPullRootMotionSource*>(S.Get());
    return H->ActionID==R.ActionID && H->Epoch==R.Epoch;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHookPullReceiptSerialization,
    "MessControl.ActiveSkills.HookPrediction.ReceiptRoundTripAndValidation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCHookPullReceiptSerialization::RunTest(const FString&)
{
    using namespace MCHookPullPredictionTests;
    for(bool Active:{true,false})
    {
        auto Sent=Receipt();Sent.bActive=Active;Sent.bFreeFlight=Active;
        TArray<uint8> Bytes;FMemoryWriter Writer(Bytes);
        TestTrue(TEXT("A complete accepted or stopped receipt serializes"),Sent.Serialize(Writer));
        FMCHookPullReceipt Read;FMemoryReader Reader(Bytes);
        TestTrue(TEXT("The receipt decodes with bounds intact"),Read.Serialize(Reader));
        TestTrue(TEXT("Action, epoch and revision survive the wire"),Read.ActionID==Sent.ActionID && Read.Epoch==Sent.Epoch && Read.Revision==Sent.Revision);
        TestEqual(TEXT("Active or stopped state survives the wire"),Read.bActive,Sent.bActive);
        if(!Active) {
            TestTrue(TEXT("Stopped receipts retain only the cleanup identity"),Read.Goal.IsZero() && Read.SourceTime==0 && !Read.bFreeFlight);
            continue;
        }
        TestTrue(TEXT("Movement ownership flags and previous mode survive the wire"),Read.bFreeFlight==Sent.bFreeFlight
            && Read.PreviousMode==Sent.PreviousMode && Read.PreviousCustomMode==Sent.PreviousCustomMode);
        TestTrue(TEXT("Accepted geometry and speed survive the wire"),Read.Goal.Equals(Sent.Goal) && Read.Speed==Sent.Speed && Read.StopDistance==Sent.StopDistance);
        TestTrue(TEXT("Acknowledged source time is distinct from its start timestamp"),Read.SourceTime==Sent.SourceTime && Read.StartMoveTime==Sent.StartMoveTime
            && Read.Duration==Sent.Duration && Read.ServerSourceID==Sent.ServerSourceID);
    }
    FMCHookPullReceipt Empty;TArray<uint8> EmptyBytes;FMemoryWriter EmptyWriter(EmptyBytes);
    TestTrue(TEXT("An absent receipt serializes"),Empty.Serialize(EmptyWriter));
    auto Reused=Receipt();FMemoryReader EmptyReader(EmptyBytes);
    TestTrue(TEXT("An absent receipt clears a reused decode container"),Reused.Serialize(EmptyReader) && Reused.Revision==0 && !Reused.bActive && !Reused.ActionID.IsValid());

    auto Invalid=Receipt();Invalid.Epoch.Invalidate();
    TestFalse(TEXT("Accepted movement requires a valid epoch"),Invalid.IsWellFormed());
    Invalid=Receipt();Invalid.ServerSourceID=uint16(ERootMotionSourceID::Invalid);
    TestFalse(TEXT("An active receipt requires a server source identity"),Invalid.IsWellFormed());
    Invalid.bActive=false;TestTrue(TEXT("A stopped receipt does not require a live source ID"),Invalid.IsWellFormed());
    Invalid=Receipt();Invalid.Speed=std::numeric_limits<float>::infinity();
    TestFalse(TEXT("Nonfinite speed is rejected"),Invalid.IsWellFormed());
    TArray<uint8> InvalidBytes;FMemoryWriter InvalidWriter(InvalidBytes);Invalid.Serialize(InvalidWriter);
    FMCHookPullReceipt InvalidRead;FMemoryReader InvalidReader(InvalidBytes);
    TestFalse(TEXT("Invalid wire data is rejected while loading too"),InvalidRead.Serialize(InvalidReader));
    Invalid=Receipt();Invalid.Goal.X=std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("A nonfinite goal is rejected"),Invalid.IsWellFormed());
    Invalid=Receipt();Invalid.SourceTime=Invalid.Duration+.251f;
    TestFalse(TEXT("A source cannot import arbitrary time beyond its duration"),Invalid.IsWellFormed());
    Invalid=Receipt();Invalid.PreviousMode=MOVE_MAX;
    TestFalse(TEXT("An invalid restore mode is rejected"),Invalid.IsWellFormed());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHookPullSourceSerialization,
    "MessControl.ActiveSkills.HookPrediction.SourceRoundTripAndStableIdentity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCHookPullSourceSerialization::RunTest(const FString&)
{
    auto R=MCHookPullPredictionTests::Receipt();
    auto Local=MCHookPullPrediction::MakeSource(R);
    auto Changed=MCHookPullPrediction::MakeSource(R);
    ++Changed->Revision;Changed->Goal+=FVector(30,10,0);Changed->Speed=900;Changed->SetTime(.5f);
    TestTrue(TEXT("An updated accepted goal retains the native matching identity"),Local->Matches(Changed.Get()));
    TestFalse(TEXT("A changed accepted goal/time still requires reconciliation"),Local->MatchesAndHasSameState(Changed.Get()));
    auto OtherEpoch=MCHookPullPrediction::MakeSource(R);OtherEpoch->Epoch=FGuid(9,10,11,12);
    TestFalse(TEXT("A previous possession epoch cannot match a new pull"),Local->Matches(OtherEpoch.Get()));
    auto OtherAction=MCHookPullPrediction::MakeSource(R);OtherAction->ActionID=FGuid(13,14,15,16);
    TestFalse(TEXT("A replacement action cannot match the old source"),Local->Matches(OtherAction.Get()));
    Local->LocalID=23;Changed->LocalID=81;
    TestTrue(TEXT("A matching correction imports accepted state"),Local->UpdateStateFrom(Changed.Get()));
    TestTrue(TEXT("Import preserves the registered local ID and updates goal and time"),Local->LocalID==23 && Local->Goal.Equals(Changed->Goal)
        && Local->Revision==Changed->Revision && Local->GetTime()==Changed->GetTime());
    Local->Status.SetFlag(ERootMotionSourceStatusFlags::MarkedForRemoval);
    Local->UpdateStateFrom(Changed.Get());
    TestTrue(TEXT("Native state import never revives a locally removed pull"),Local->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval));
    Local->Status.UnSetFlag(ERootMotionSourceStatusFlags::MarkedForRemoval);

    TArray<uint8> Bytes;FMemoryWriter MemoryWriter(Bytes);FObjectAndNameAsStringProxyArchive Writer(MemoryWriter,false);
    bool Written=false;TestTrue(TEXT("The derived source serializes"),Local->NetSerialize(Writer,nullptr,Written) && Written);
    FMemoryReader MemoryReader(Bytes);FObjectAndNameAsStringProxyArchive Reader(MemoryReader,true);
    FMCHookPullRootMotionSource Read;bool ReadSuccess=false;
    TestTrue(TEXT("The derived source decodes"),Read.NetSerialize(Reader,nullptr,ReadSuccess) && ReadSuccess);
    TestTrue(TEXT("The decoded source matches the accepted action and state"),Read.MatchesAndHasSameState(Local.Get()));
    TestTrue(TEXT("Planar ownership settings survive source replication"),Read.Settings.HasFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate));
    TUniquePtr<FRootMotionSource> Cloned(Read.Clone());
    TestTrue(TEXT("Saved-move cloning retains the derived source and accepted context"),Cloned->GetScriptStruct()==FMCHookPullRootMotionSource::StaticStruct()
        && Cloned->MatchesAndHasSameState(&Read));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHookPullCatchupClamp,
    "MessControl.ActiveSkills.HookPrediction.CatchupCannotCrossAcceptedStopRadius",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCHookPullCatchupClamp::RunTest(const FString&)
{
    // PrepareRootMotion reads only the character location; use the immutable
    // engine defaults instead of loading a map or starting gameplay.
    const auto* Character=GetDefault<ACharacter>();
    const auto* Move=Character->GetCharacterMovement();
    if(!TestNotNull(TEXT("The engine character default has its movement component"),Move)) return false;
    auto R=MCHookPullPredictionTests::Receipt();R.Goal=Character->GetActorLocation()+FVector(200,0,0);R.SourceTime=0;
    constexpr float SimulationTime=.2f,MovementTickTime=.016f;
    auto Catchup=MCHookPullPrediction::MakeSource(R);
    Catchup->PrepareRootMotion(SimulationTime,MovementTickTime,*Character,*Move);
    const FVector Displacement=Catchup->RootMotionParams.GetRootMotionTransform().GetTranslation()*MovementTickTime;
    TestTrue(TEXT("A long replay/catchup step stops at the planar accepted radius"),Displacement.Equals(FVector(80,0,0),.01));
    TestEqual(TEXT("Source time advances by simulation time exactly once"),Catchup->GetTime(),SimulationTime,.0001f);

    auto Normal=MCHookPullPrediction::MakeSource(R);
    Normal->PrepareRootMotion(MovementTickTime,MovementTickTime,*Character,*Move);
    const FVector NormalDisplacement=Normal->RootMotionParams.GetRootMotionTransform().GetTranslation()*MovementTickTime;
    TestTrue(TEXT("An ordinary move keeps the accepted speed cap"),NormalDisplacement.Equals(FVector(R.Speed*MovementTickTime,0,0),.01));
    R.Goal=Character->GetActorLocation()+FVector(200,0,40);auto Sloped=MCHookPullPrediction::MakeSource(R);
    Sloped->PrepareRootMotion(SimulationTime,MovementTickTime,*Character,*Move);
    const FVector SlopedDisplacement=Sloped->RootMotionParams.GetRootMotionTransform().GetTranslation()*MovementTickTime;
    TestTrue(TEXT("A grounded pull stops on the authority's three-dimensional sphere"),FMath::IsNearlyEqual(
        (R.Goal-Character->GetActorLocation()-SlopedDisplacement).Size(),double(R.StopDistance),.01));
    R.Goal=Character->GetActorLocation()+FVector(200,0,400);
    R.bFreeFlight=true;auto FreeFlight=MCHookPullPrediction::MakeSource(R);
    FreeFlight->PrepareRootMotion(.5f,MovementTickTime,*Character,*Move);
    const FVector FlightDisplacement=FreeFlight->RootMotionParams.GetRootMotionTransform().GetTranslation()*MovementTickTime;
    const FVector Delta=R.Goal-Character->GetActorLocation();
    TestTrue(TEXT("Free-flight catchup clamps in three dimensions"),FlightDisplacement.Z>0 && FMath::IsNearlyEqual(FlightDisplacement.Size(),Delta.Size()-R.StopDistance,.01));
    R.Goal=Character->GetActorLocation()+FVector(80,0,0);auto Inside=MCHookPullPrediction::MakeSource(R);
    Inside->PrepareRootMotion(SimulationTime,MovementTickTime,*Character,*Move);
    TestTrue(TEXT("Already inside the accepted radius produces no pull or reverse force"),Inside->RootMotionParams.GetRootMotionTransform().GetTranslation().IsNearlyZero());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHookPullHistoryImport,
    "MessControl.ActiveSkills.HookPrediction.HistoryUsesRegisteredIdentityAndExactCleanup",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCHookPullHistoryImport::RunTest(const FString&)
{
    auto R=MCHookPullPredictionTests::Receipt();
    FRootMotionSourceGroup Live;auto LiveHook=MCHookPullPrediction::MakeSource(R);
    const uint16 RegisteredID=Live.ApplyRootMotionSource(LiveHook);
    TestTrue(TEXT("The native source group issues a valid local ID"),RegisteredID!=uint16(ERootMotionSourceID::Invalid));
    FRootMotionSourceGroup History;
    FRootMotionSourceGroup IdentifierIssuer;
    auto Unrelated=MakeShared<FRootMotionSource_ConstantForce>();Unrelated->InstanceName=TEXT("UnrelatedImpulse");IdentifierIssuer.ApplyRootMotionSource(Unrelated);
    auto Pending=MakeShared<FRootMotionSource_ConstantForce>();Pending->InstanceName=TEXT("PendingAmbient");IdentifierIssuer.ApplyRootMotionSource(Pending);
    auto Old=MCHookPullPrediction::MakeSource(R);Old->LocalID=RegisteredID;
    auto Duplicate=MCHookPullPrediction::MakeSource(R);Duplicate->LocalID=RegisteredID;
    auto OtherEpoch=MCHookPullPrediction::MakeSource(R);OtherEpoch->Epoch=FGuid(90,91,92,93);IdentifierIssuer.ApplyRootMotionSource(OtherEpoch);
    History.RootMotionSources={Unrelated,Old,OtherEpoch};History.PendingAddRootMotionSources={Pending,Duplicate};
    MCHookPullPrediction::ReplaceHistory(History,R,RegisteredID,true);
    TestTrue(TEXT("History import keeps unrelated active and pending sources"),History.RootMotionSources.Contains(Unrelated) && History.PendingAddRootMotionSources.Contains(Pending));
    TestTrue(TEXT("An epoch-specific import preserves another epoch"),History.RootMotionSources.Contains(OtherEpoch));
    TestEqual(TEXT("Only one exact accepted Hook is active in the imported history"),History.RootMotionSources.FilterByPredicate([&](const auto& S){return MCHookPullPredictionTests::IsExactHook(S,R);}).Num(),1);
    TestFalse(TEXT("No duplicate exact Hook remains pending"),History.PendingAddRootMotionSources.ContainsByPredicate([&](const auto& S){return MCHookPullPredictionTests::IsExactHook(S,R);}));
    const auto* Imported=History.RootMotionSources.FindByPredicate([&](const auto& S){return MCHookPullPredictionTests::IsExactHook(S,R);});
    if(!TestTrue(TEXT("The imported acknowledged source exists"),Imported!=nullptr)) return false;
    TestEqual(TEXT("History reuses the native component/group-issued ID"),(*Imported)->LocalID,RegisteredID);
    TestEqual(TEXT("History is seeded at acknowledged end time"),(*Imported)->GetTime(),R.SourceTime);
    LiveHook->SetTime(0);LiveHook->Status.SetFlag(ERootMotionSourceStatusFlags::Prepared);
    Live.UpdateStateFrom(History);
    TestTrue(TEXT("Native reconciliation promotes the matching pending source to active"),Live.RootMotionSources.Contains(LiveHook)
        && !Live.PendingAddRootMotionSources.Contains(LiveHook));
    TestTrue(TEXT("Native reconciliation imports ack time and requires fresh preparation"),LiveHook->GetTime()==R.SourceTime
        && !LiveHook->Status.HasFlag(ERootMotionSourceStatusFlags::Prepared));
    const auto* Character=GetDefault<ACharacter>();
    if(!TestNotNull(TEXT("The replay probe has an engine movement component"),Character->GetCharacterMovement())) return false;
    Live.PrepareRootMotion(.016f,*Character,*Character->GetCharacterMovement());
    TestEqual(TEXT("Native replay advances the acknowledged source by one saved move, not twice"),LiveHook->GetTime(),R.SourceTime+.016f,.0001f);
    MCHookPullPrediction::ReplaceHistory(History,R,RegisteredID,false);
    TestFalse(TEXT("Stop removes the exact action/epoch from replay history"),History.RootMotionSources.ContainsByPredicate([&](const auto& S){return MCHookPullPredictionTests::IsExactHook(S,R);})
        || History.PendingAddRootMotionSources.ContainsByPredicate([&](const auto& S){return MCHookPullPredictionTests::IsExactHook(S,R);}));
    TestTrue(TEXT("Stop leaves unrelated motion and another epoch intact"),History.RootMotionSources.Contains(Unrelated)
        && History.RootMotionSources.Contains(OtherEpoch) && History.PendingAddRootMotionSources.Contains(Pending));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHookPullBufferedMoves,
    "MessControl.ActiveSkills.HookPrediction.BufferedMovesUseAcceptedSimulationBudget",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCHookPullBufferedMoves::RunTest(const FString&)
{
    const auto* Character=GetDefault<ACharacter>();
    const auto* Move=Character->GetCharacterMovement();
    if(!TestNotNull(TEXT("The buffered-move probe has native CMC"),Move)) return false;
    auto R=MCHookPullPredictionTests::Receipt();R.Goal=Character->GetActorLocation()+FVector(1000,0,0);R.SourceTime=0;
    auto Source=MCHookPullPrediction::MakeSource(R);
    const float Before=Source->GetTime();
    constexpr float BufferedSeconds=.0565f,ActorFrameSeconds=.0167f;
    Source->PrepareRootMotion(BufferedSeconds,ActorFrameSeconds,*Character,*Move);
    const float SimulationDelta=Source->GetTime()-Before;
    const FVector From=Character->GetActorLocation();
    const FVector To=From+Source->RootMotionParams.GetRootMotionTransform().GetTranslation()*ActorFrameSeconds;
    TestEqual(TEXT("Buffered native RMS legitimately advances about 48cm"),float(FVector::Dist(From,To)),48.025f,.001f);
    TestFalse(TEXT("The old actor-frame-only budget rejects this legitimate displacement"),
        MCActiveSkillMotion::ContinuousPath(From,To,ActorFrameSeconds,R.Speed));
    const float TrustedSeconds=FMath::Max(ActorFrameSeconds,SimulationDelta);
    TestTrue(TEXT("The exact accepted source-time budget admits buffered CMC movement"),
        MCActiveSkillMotion::ContinuousPath(From,To,TrustedSeconds,R.Speed));
    TestFalse(TEXT("The same trusted budget still rejects an unrelated 300cm teleport"),
        MCActiveSkillMotion::ContinuousPath(From,From+FVector(300,0,0),TrustedSeconds,R.Speed));
    TestFalse(TEXT("The original quarter-second continuity cap remains intact"),
        MCActiveSkillMotion::ContinuousPath(From,To,.251f,R.Speed));
    return true;
}
#endif
