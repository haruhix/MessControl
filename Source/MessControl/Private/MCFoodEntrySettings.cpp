#include "MCFoodEntrySettings.h"

void FMCFoodEntrySettings::Sanitize()
{
    const auto Safe=[](float Value,float Default,float Min,float Max) {
        return FMath::IsFinite(Value)?FMath::Clamp(Value,Min,Max):Default;
    };
    FlightSeconds=Safe(FlightSeconds,1.35f,.75f,3.f);
    EntryHeight=Safe(EntryHeight,250.f,150.f,900.f);
    OutsideDistance=Safe(OutsideDistance,250.f,0.f,600.f);
    PushSpeed=Safe(PushSpeed,140.f,0.f,180.f);
}

bool FMCFoodEntrySettings::BuildTrajectory(const FBox& TongueBounds,FVector LandingCenter,float GravityZ,FVector& Start,FVector& Velocity) const
{
    Start=Velocity=FVector::ZeroVector;
    if(!TongueBounds.IsValid || TongueBounds.Min.ContainsNaN() || TongueBounds.Max.ContainsNaN()
        || TongueBounds.GetSize().X<=KINDA_SMALL_NUMBER || LandingCenter.ContainsNaN()
        || !FMath::IsFinite(GravityZ) || GravityZ>=-KINDA_SMALL_NUMBER) return false;
    FMCFoodEntrySettings Limits=*this;Limits.Sanitize();
    // The shared spawn bands measure depth from +X (throat) toward -X (open mouth).
    Start=FVector(TongueBounds.Min.X-Limits.OutsideDistance,TongueBounds.GetCenter().Y,FMath::Max(LandingCenter.Z,TongueBounds.Max.Z)+Limits.EntryHeight);
    const FVector Gravity(0,0,GravityZ);
    Velocity=(LandingCenter-Start-Gravity*(.5f*Limits.FlightSeconds*Limits.FlightSeconds))/Limits.FlightSeconds;
    return !Start.ContainsNaN() && !Velocity.ContainsNaN();
}
