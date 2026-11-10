#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCAcceptedExternalMotion.h"
#include "MCToothMovementComponent.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"
#include <limits>

namespace MCAcceptedMotionTests
{
FMCAcceptedMotionSnapshot Snapshot(EMCAcceptedMotionPolicy Kind)
{
    FMCAcceptedMotionSnapshot R;R.OperationID=FGuid(1,2,3,4);R.Epoch=FGuid(5,6,7,8);R.Revision=11;R.bActive=true;R.Policy=Kind;
    R.PreviousMode=MOVE_Walking;R.PreviousCustomMode=3;R.ServerSourceID=17;R.Duration=2;R.StartMoveTime=12.25f;R.SourceTime=.375f;
    R.Velocity=FVector(850,0,0);R.Goal=FVector(300,40,100);R.Speed=850;R.StopDistance=120;R.TurnRate=130;R.SteeringLockedUntil=.5f;return R;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCAcceptedMotionWire,
    "MessControl.Networking.Foundation.ExternalMotionWireAndTerminalIdentity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCAcceptedMotionWire::RunTest(const FString&)
{
    for(auto Kind:{EMCAcceptedMotionPolicy::ConstantVelocity,EMCAcceptedMotionPolicy::SteeredVelocity,EMCAcceptedMotionPolicy::BoundedGoal})
    for(bool Active:{true,false}) {
        auto Sent=MCAcceptedMotionTests::Snapshot(Kind);Sent.bActive=Active;
        if(Kind==EMCAcceptedMotionPolicy::BoundedGoal) {Sent.Speed=2200;Sent.CurveDuration=.75f;}
        FBitWriter Writer(4096,true);TestTrue(TEXT("The accepted provider snapshot serializes into packed network bits"),Sent.Serialize(Writer));
        uint32 Sentinel=0x91ab28c7;Writer<<Sentinel;
        FBitReader Reader(Writer.GetData(),Writer.GetNumBits());FMCAcceptedMotionSnapshot Read;
        TestTrue(TEXT("Each provider and terminal decode with bounded state"),Read.Serialize(Reader));
        uint32 NextField=0;Reader<<NextField;
        TestTrue(TEXT("Optional provider payload never shifts the next response field"),!Reader.IsError() && NextField==Sentinel && Reader.GetPosBits()==Writer.GetNumBits());
        TestTrue(TEXT("Load, operation and monotonic revision survive the same codec"),Read.OperationID==Sent.OperationID && Read.Epoch==Sent.Epoch && Read.Revision==Sent.Revision && Read.bActive==Active);
        if(!Active) {TestTrue(TEXT("Terminal retains identity without an obsolete motion source/goal/time"),Read.Goal.IsZero() && Read.Velocity.IsZero() && Read.SourceTime==0 && Read.CurveDuration==0);continue;}
        TestTrue(TEXT("Native source identity and ACK-relative clock survive"),Read.Policy==Kind && Read.ServerSourceID==17 && Read.SourceTime==Sent.SourceTime && Read.StartMoveTime==Sent.StartMoveTime);
        TestTrue(TEXT("Only the selected solver parameters are transported"),Kind==EMCAcceptedMotionPolicy::BoundedGoal?Read.Goal.Equals(Sent.Goal):Read.Velocity.Equals(Sent.Velocity));
        if(Kind==EMCAcceptedMotionPolicy::BoundedGoal) {
            TestEqual(TEXT("Hook pull mean speed survives the accepted motion snapshot"),Read.Speed,Sent.Speed);
            TestEqual(TEXT("Hook pull curve timing survives the accepted motion snapshot"),Read.CurveDuration,Sent.CurveDuration);
            TestEqual(TEXT("Hook prediction receives the curve duration from the accepted motion snapshot"),Read.AsHookReceipt().CurveDuration,Sent.CurveDuration);
        }
        if(Kind==EMCAcceptedMotionPolicy::SteeredVelocity) TestTrue(TEXT("Steering state includes its simulation-time bounce lock"),Read.TurnRate==Sent.TurnRate && Read.SteeringLockedUntil==Sent.SteeringLockedUntil);
    }
    auto Invalid=MCAcceptedMotionTests::Snapshot(EMCAcceptedMotionPolicy::SteeredVelocity);
    Invalid.TurnRate=std::numeric_limits<float>::infinity();TestFalse(TEXT("A wire policy cannot import nonfinite steering"),Invalid.IsWellFormed());
    Invalid=MCAcceptedMotionTests::Snapshot(EMCAcceptedMotionPolicy::ConstantVelocity);Invalid.Epoch.Invalidate();
    TestFalse(TEXT("An external operation requires a load identity"),Invalid.IsWellFormed());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCAcceptedMotionHistory,
    "MessControl.Networking.Foundation.ExternalMotionHistoryOwnsExactEpochAndSource",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCAcceptedMotionHistory::RunTest(const FString&)
{
    for(auto Kind:{EMCAcceptedMotionPolicy::ConstantVelocity,EMCAcceptedMotionPolicy::SteeredVelocity,EMCAcceptedMotionPolicy::BoundedGoal}) {
        auto R=MCAcceptedMotionTests::Snapshot(Kind);FRootMotionSourceGroup History;
        auto Wind=MakeShared<FMCLocomotionRootMotionSource>();Wind->InstanceName=TEXT("FoundationAmbientWind");Wind->Duration=-1;Wind->LocalID=8;
        auto OtherEpoch=R;OtherEpoch.Epoch=FGuid(9,10,11,12);
        auto Foreign=MCAcceptedExternalMotion::MakeSource(OtherEpoch);Foreign->LocalID=9;
        History.RootMotionSources.Add(Wind);History.RootMotionSources.Add(Foreign);
        MCAcceptedExternalMotion::ReplaceHistory(History,R,23,true);
        TestTrue(TEXT("A corrected source uses one component-issued local ID without touching ambient or another epoch"),History.RootMotionSources.Num()==3
            && History.RootMotionSources.Last()->LocalID==23 && History.RootMotionSources.Contains(Wind) && History.RootMotionSources.Contains(Foreign));
        MCAcceptedExternalMotion::ReplaceHistory(History,R,23,true);
        TestTrue(TEXT("A repeated accepted snapshot replaces once without pre-advancing replay time"),History.RootMotionSources.Num()==3
            && History.RootMotionSources.Last()->GetTime()==R.SourceTime);
        History.PendingAddRootMotionSources.Add(MCAcceptedExternalMotion::MakeSource(R));
        auto Terminal=R;Terminal.bActive=false;++Terminal.Revision;Terminal.Policy=EMCAcceptedMotionPolicy::ConstantVelocity;
        MCAcceptedExternalMotion::ReplaceHistory(History,Terminal,0,false);
        TestTrue(TEXT("A terminal decoded without policy payload purges both active/pending exact history"),History.RootMotionSources.Num()==2
            && History.PendingAddRootMotionSources.IsEmpty() && History.RootMotionSources.Contains(Wind) && History.RootMotionSources.Contains(Foreign));
        auto Source=MCAcceptedExternalMotion::MakeSource(R);
        TestTrue(TEXT("Existing reflected root-motion source names remain compatible"),Source->GetScriptStruct()==(Kind==EMCAcceptedMotionPolicy::BoundedGoal?FMCHookPullRootMotionSource::StaticStruct():FMCLocomotionRootMotionSource::StaticStruct()));
    }
    return true;
}
#endif
