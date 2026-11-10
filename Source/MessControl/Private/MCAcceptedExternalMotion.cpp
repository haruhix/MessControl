#include "MCAcceptedExternalMotion.h"
#include "MCToothMovementComponent.h"

bool FMCAcceptedMotionSnapshot::IsWellFormed() const
{
    if(!Revision || !OperationID.IsValid() || !Epoch.IsValid()) return false;
    if(!bActive) return true;
    if(uint8(Policy)>uint8(EMCAcceptedMotionPolicy::BoundedGoal) || PreviousMode>=MOVE_MAX
        || ServerSourceID==uint16(ERootMotionSourceID::Invalid) || !FMath::IsFinite(StartMoveTime)
        || !FMath::IsFinite(SourceTime) || SourceTime<0 || !FMath::IsFinite(Duration)
        || (Duration!=-1 && (Duration<=0 || Duration>600)) || (Duration>=0 && SourceTime>Duration+.25f)) return false;
    if(Policy==EMCAcceptedMotionPolicy::BoundedGoal)
        return !Goal.ContainsNaN() && FMath::IsFinite(Speed) && Speed>=MCHookPullPrediction::MinSpeed && Speed<=MCHookPullPrediction::MaxSpeed
            && FMath::IsFinite(CurveDuration) && CurveDuration>=0
            && FMath::IsFinite(StopDistance) && StopDistance>=60 && StopDistance<=300 && Duration>=.1f && Duration<=8;
    if(Velocity.ContainsNaN() || Velocity.SizeSquared()>FMath::Square(2000.01f)) return false;
    return Policy!=EMCAcceptedMotionPolicy::SteeredVelocity || (FMath::IsFinite(Speed) && Speed>=100 && Speed<=2000
        && FMath::IsFinite(TurnRate) && TurnRate>=0 && TurnRate<=720
        && FMath::IsFinite(SteeringLockedUntil) && SteeringLockedUntil>=0);
}
bool FMCAcceptedMotionSnapshot::Serialize(FArchive& Ar)
{
    bool Present=Revision!=0;Ar.SerializeBits(&Present,1);
    if(!Present) {if(Ar.IsLoading()) *this={};return !Ar.IsError();}
    if(Ar.IsLoading()) *this={};
    Ar<<OperationID<<Epoch<<Revision;Ar.SerializeBits(&bActive,1);
    // Terminal identity survives packet loss without sending obsolete geometry.
    if(bActive) {
        uint8 Kind=uint8(Policy);Ar.SerializeBits(&Kind,2);if(Ar.IsLoading()) Policy=EMCAcceptedMotionPolicy(Kind);
        Ar.SerializeBits(&bFreeFlight,1);
        Ar<<PreviousMode<<PreviousCustomMode<<ServerSourceID<<Duration<<StartMoveTime<<SourceTime;
        if(Policy==EMCAcceptedMotionPolicy::BoundedGoal) Ar<<Goal<<Speed<<StopDistance<<CurveDuration;
        else {
            Ar<<Velocity;
            if(Policy==EMCAcceptedMotionPolicy::SteeredVelocity) Ar<<Speed<<TurnRate<<SteeringLockedUntil;
        }
    }
    return !Ar.IsError() && IsWellFormed();
}
FMCHookPullReceipt FMCAcceptedMotionSnapshot::AsHookReceipt() const
{
    FMCHookPullReceipt R;R.ActionID=OperationID;R.Epoch=Epoch;R.Revision=Revision;R.bActive=bActive;R.bFreeFlight=bFreeFlight;
    R.PreviousMode=PreviousMode;R.PreviousCustomMode=PreviousCustomMode;R.Goal=Goal;R.Speed=Speed;R.StopDistance=StopDistance;
    R.CurveDuration=CurveDuration;R.Duration=Duration;R.StartMoveTime=StartMoveTime;R.SourceTime=SourceTime;R.ServerSourceID=ServerSourceID;return R;
}
FName MCAcceptedExternalMotion::SourceName(const FMCAcceptedMotionSnapshot& R)
{
    if(R.Policy==EMCAcceptedMotionPolicy::BoundedGoal) return MCHookPullPrediction::SourceName(R.OperationID);
    // The existing velocity RMS has no epoch field. Its native matching name
    // carries the complete accepted key without changing its reflected layout.
    return FName(*(TEXT("MCActiveSkillMotion_")+R.OperationID.ToString(EGuidFormats::Digits)+TEXT("_")+R.Epoch.ToString(EGuidFormats::Digits)));
}
TSharedPtr<FRootMotionSource> MCAcceptedExternalMotion::MakeSource(const FMCAcceptedMotionSnapshot& R)
{
    if(R.Policy==EMCAcceptedMotionPolicy::BoundedGoal) return MCHookPullPrediction::MakeSource(R.AsHookReceipt());
    auto S=MakeShared<FMCLocomotionRootMotionSource>();
    S->InstanceName=SourceName(R);S->Priority=450;S->AccumulateMode=ERootMotionAccumulateMode::Override;
    S->Force=R.Velocity;S->Duration=R.Duration;S->StartTime=R.StartMoveTime;S->SetTime(R.SourceTime);
    if(!R.bFreeFlight) S->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
    return S;
}
bool MCAcceptedExternalMotion::IsExternalFamily(const TSharedPtr<FRootMotionSource>& S)
{
    return S && (S->GetScriptStruct()==FMCHookPullRootMotionSource::StaticStruct() || S->GetScriptStruct()==FMCLocomotionRootMotionSource::StaticStruct())
        && S->InstanceName.ToString().StartsWith(TEXT("MCActiveSkillMotion_"));
}
bool MCAcceptedExternalMotion::MatchesSource(const TSharedPtr<FRootMotionSource>& S,const FMCAcceptedMotionSnapshot& R)
{
    if(!IsExternalFamily(S)) return false;
    if(R.bActive && ((R.Policy==EMCAcceptedMotionPolicy::BoundedGoal)!=(S->GetScriptStruct()==FMCHookPullRootMotionSource::StaticStruct()))) return false;
    if(S->GetScriptStruct()==FMCHookPullRootMotionSource::StaticStruct()) {
        const auto* Hook=static_cast<const FMCHookPullRootMotionSource*>(S.Get());return S->InstanceName==MCHookPullPrediction::SourceName(R.OperationID)
            && Hook->ActionID==R.OperationID && Hook->Epoch==R.Epoch;
    }
    auto Velocity=R;Velocity.Policy=EMCAcceptedMotionPolicy::ConstantVelocity;
    return S->InstanceName==SourceName(Velocity);
}
bool MCAcceptedExternalMotion::IsRunning(const TSharedPtr<FRootMotionSource>& S)
{
    return S && !S->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval) && !S->Status.HasFlag(ERootMotionSourceStatusFlags::Finished)
        && (S->Duration<0 || S->GetTime()<S->Duration);
}
void MCAcceptedExternalMotion::ReplaceHistory(FRootMotionSourceGroup& G,const FMCAcceptedMotionSnapshot& R,uint16 LocalID,bool Add)
{
    auto Remove=[&](const TSharedPtr<FRootMotionSource>& S){return MatchesSource(S,R);};
    G.RootMotionSources.RemoveAll(Remove);G.PendingAddRootMotionSources.RemoveAll(Remove);
    if(Add) {auto S=MakeSource(R);S->LocalID=LocalID;G.RootMotionSources.Add(S);}
}
void MCAcceptedExternalMotion::RemoveUnstampedSources(FRootMotionSourceGroup& G)
{G.RootMotionSources.RemoveAll(IsExternalFamily);G.PendingAddRootMotionSources.RemoveAll(IsExternalFamily);}
