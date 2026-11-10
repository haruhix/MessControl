#include "MCHookPullPrediction.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

bool FMCHookPullReceipt::IsWellFormed() const
{
    if(!Revision || !ActionID.IsValid() || !Epoch.IsValid()) return false;
    if(!bActive) return true;
    return !Goal.ContainsNaN() && FMath::IsFinite(Speed) && Speed>=MCHookPullPrediction::MinSpeed && Speed<=MCHookPullPrediction::MaxSpeed
        && FMath::IsFinite(StopDistance) && StopDistance>=60 && StopDistance<=300
        && FMath::IsFinite(CurveDuration) && CurveDuration>=0
        && FMath::IsFinite(Duration) && Duration>=.1f && Duration<=8
        && FMath::IsFinite(StartMoveTime) && FMath::IsFinite(SourceTime) && SourceTime>=0 && SourceTime<=Duration+.25f
        && PreviousMode<MOVE_MAX && ServerSourceID!=uint16(ERootMotionSourceID::Invalid);
}
bool FMCHookPullReceipt::Serialize(FArchive& Ar)
{
    bool Present=Revision!=0;Ar.SerializeBits(&Present,1);
    if(!Present) {if(Ar.IsLoading()) *this={};return !Ar.IsError();}
    if(Ar.IsLoading()) *this={};
    Ar<<ActionID<<Epoch<<Revision;Ar.SerializeBits(&bActive,1);
    // A terminal receipt is a retained identity tombstone. Its old geometry
    // is irrelevant to cleanup and need not occupy every subsequent ACK.
    if(bActive) {
        Ar.SerializeBits(&bFreeFlight,1);
        Ar<<PreviousMode<<PreviousCustomMode<<Goal<<Speed<<StopDistance<<CurveDuration<<Duration<<StartMoveTime<<SourceTime<<ServerSourceID;
    }
    return !Ar.IsError() && IsWellFormed();
}
FRootMotionSource* FMCHookPullRootMotionSource::Clone() const {return new FMCHookPullRootMotionSource(*this);}
UScriptStruct* FMCHookPullRootMotionSource::GetScriptStruct() const {return StaticStruct();}
bool FMCHookPullRootMotionSource::Matches(const FRootMotionSource* Other) const
{
    return FRootMotionSource::Matches(Other) && ActionID==static_cast<const FMCHookPullRootMotionSource*>(Other)->ActionID
        && Epoch==static_cast<const FMCHookPullRootMotionSource*>(Other)->Epoch;
}
bool FMCHookPullRootMotionSource::MatchesAndHasSameState(const FRootMotionSource* Other) const
{
    if(!FRootMotionSource::MatchesAndHasSameState(Other)) return false;
    const auto* O=static_cast<const FMCHookPullRootMotionSource*>(Other);
    return Revision==O->Revision && Goal.Equals(O->Goal,.01f) && Speed==O->Speed && StopDistance==O->StopDistance
        && CurveDuration==O->CurveDuration;
}
bool FMCHookPullRootMotionSource::UpdateStateFrom(const FRootMotionSource* Other,bool Catchup)
{
    if(!Matches(Other) || !FRootMotionSource_ConstantForce::UpdateStateFrom(Other,Catchup)) return false;
    const auto* O=static_cast<const FMCHookPullRootMotionSource*>(Other);
    Revision=O->Revision;Goal=O->Goal;Speed=O->Speed;StopDistance=O->StopDistance;CurveDuration=O->CurveDuration;Force=O->Force;
    Settings=O->Settings;FinishVelocityParams=O->FinishVelocityParams;return true;
}
double MCHookPullPrediction::TravelProgress(double Alpha)
{
    const double A=FMath::Clamp(Alpha,0.,1.);
    // Keep the end slope for a moving goal that extends the planned pull.
    return LinearTravelWeight*FMath::Max(0.,Alpha)+(1.-LinearTravelWeight)*A*A*(3.-2.*A);
}
FVector MCHookPullPrediction::RemainingTravel(FVector Delta,float StopDistance,bool bFreeFlight)
{
    float Radius=StopDistance;
    if(!bFreeFlight) {
        // The authority ends against a 3D sphere even for grounded pulls.
        // Its planar cross-section shrinks when the accepted goal is higher.
        Radius=FMath::Sqrt(FMath::Max(0.f,StopDistance*StopDistance-float(Delta.Z*Delta.Z)));
        Delta.Z=0;
    }
    return Delta.GetSafeNormal()*FMath::Max(0.,Delta.Size()-Radius);
}
void FMCHookPullRootMotionSource::PrepareRootMotion(float SimulationTime,float MovementTickTime,
    const ACharacter& Character,const UCharacterMovementComponent& Movement)
{
    const FVector Remaining=MCHookPullPrediction::RemainingTravel(Goal-Character.GetActorLocation(),StopDistance,
        !Settings.HasFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate));
    double StepDistance=Speed*SimulationTime;
    if(CurveDuration>0) {
        const double StartAlpha=double(GetTime())/CurveDuration;
        const double EndAlpha=StartAlpha+double(SimulationTime)/CurveDuration;
        StepDistance=Speed*double(CurveDuration)*(MCHookPullPrediction::TravelProgress(EndAlpha)-MCHookPullPrediction::TravelProgress(StartAlpha));
    }
    // Use simulation time so catch-up/replay cannot step beyond the accepted
    // stop radius. The base applies SimulationTime/MovementTickTime once.
    Force=Remaining.GetClampedToMaxSize(FMath::Max(0.,StepDistance))/FMath::Max(.0001f,SimulationTime);
    FRootMotionSource_ConstantForce::PrepareRootMotion(SimulationTime,MovementTickTime,Character,Movement);
}
bool FMCHookPullRootMotionSource::NetSerialize(FArchive& Ar,UPackageMap* Map,bool& Success)
{
    if(!FRootMotionSource_ConstantForce::NetSerialize(Ar,Map,Success)) return false;
    Ar<<ActionID<<Epoch<<Revision<<Goal<<Speed<<StopDistance<<CurveDuration;
    Ar<<Settings.Flags;
    Success=Success && !Ar.IsError() && ActionID.IsValid() && Epoch.IsValid() && Revision!=0 && !Goal.ContainsNaN()
        && FMath::IsFinite(Speed) && Speed>=MCHookPullPrediction::MinSpeed && Speed<=MCHookPullPrediction::MaxSpeed
        && FMath::IsFinite(StopDistance) && StopDistance>=60 && StopDistance<=300
        && FMath::IsFinite(CurveDuration) && CurveDuration>=0;
    return Success;
}
FName MCHookPullPrediction::SourceName(const FGuid& ID)
{return FName(*(TEXT("MCActiveSkillMotion_")+ID.ToString(EGuidFormats::Digits)));}
TSharedPtr<FMCHookPullRootMotionSource> MCHookPullPrediction::MakeSource(const FMCHookPullReceipt& R)
{
    auto S=MakeShared<FMCHookPullRootMotionSource>();
    S->ActionID=R.ActionID;S->Epoch=R.Epoch;S->Revision=R.Revision;S->Goal=R.Goal;S->Speed=R.Speed;S->StopDistance=R.StopDistance;
    S->CurveDuration=R.CurveDuration;
    S->InstanceName=SourceName(R.ActionID);S->Priority=450;S->AccumulateMode=ERootMotionAccumulateMode::Override;
    S->Duration=R.Duration;S->StartTime=R.StartMoveTime;S->SetTime(R.SourceTime);
    if(!R.bFreeFlight) S->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
    return S;
}
bool MCHookPullPrediction::IsHook(const TSharedPtr<FRootMotionSource>& S)
{return S.IsValid() && S->GetScriptStruct()==FMCHookPullRootMotionSource::StaticStruct();}
void MCHookPullPrediction::ReplaceHistory(FRootMotionSourceGroup& G,const FMCHookPullReceipt& R,uint16 LocalID,bool Add)
{
    auto Remove=[&](const TSharedPtr<FRootMotionSource>& S) {
        return IsHook(S) && static_cast<const FMCHookPullRootMotionSource*>(S.Get())->ActionID==R.ActionID
            && static_cast<const FMCHookPullRootMotionSource*>(S.Get())->Epoch==R.Epoch;
    };
    G.RootMotionSources.RemoveAll(Remove);G.PendingAddRootMotionSources.RemoveAll(Remove);
    if(Add) {auto S=MakeSource(R);S->LocalID=LocalID;G.RootMotionSources.Add(S);}
}
