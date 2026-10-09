#include "MCPlayerCameraComponent.h"
#include "MCToothCharacter.h"
#include "Engine/World.h"

namespace {
    constexpr float ImpactDuration=.28f;
    constexpr float GroundImpactDuration=.7f;
    float ImpactEnvelope(double Age,float Duration=ImpactDuration) {
        const float Remaining=1.f-FMath::Clamp(float(Age)/Duration,0.f,1.f);
        return Remaining*Remaining;
    }
}

UMCPlayerCameraComponent::UMCPlayerCameraComponent()
{
    PostProcessBlendWeight=1.f;
    PostProcessSettings.bOverride_DepthOfFieldEnabled=true;
    PostProcessSettings.DepthOfFieldEnabled=true;
    PostProcessSettings.bOverride_DepthOfFieldFstop=true;
    PostProcessSettings.DepthOfFieldFstop=1.8f;
    PostProcessSettings.bOverride_DepthOfFieldSensorWidth=true;
    PostProcessSettings.DepthOfFieldSensorWidth=36.f;
    PostProcessSettings.bOverride_DepthOfFieldFocalDistance=true;
    PostProcessSettings.DepthOfFieldFocalDistance=900.f;
}

void UMCPlayerCameraComponent::AddImpact(float ImpactSpeed, const FVector& WorldDirection)
{
    if(!GetWorld() || !FMath::IsFinite(ImpactSpeed) || ImpactSpeed<=120.f || WorldDirection.ContainsNaN()) return;
    const double Now=GetWorld()->GetTimeSeconds();
    const float Speed=FMath::Min(ImpactSpeed,1000.f);
    // Relative kinetic energy per unit mass; cap large hits and overlapping contacts.
    const float Strength=(Speed*Speed-120.f*120.f)/(1000.f*1000.f-120.f*120.f);
    const float Existing=ImpactStrength*ImpactEnvelope(Now-ImpactStartedAt);
    if(Now-ImpactStartedAt<.1 && Strength<=Existing) return;
    ImpactStrength=FMath::Max(Existing,Strength);
    ImpactDirection=WorldDirection.GetSafeNormal();
    ImpactStartedAt=Now;
}

void UMCPlayerCameraComponent::AddGroundImpact(float Strength,const FVector& Source)
{
    if(!GetWorld() || !FMath::IsFinite(Strength) || Strength<=0.f || Source.ContainsNaN()) return;
    const double Now=GetWorld()->GetTimeSeconds();
    const float Existing=GroundImpactStrength*ImpactEnvelope(Now-GroundImpactStartedAt,GroundImpactDuration);
    Strength=FMath::Clamp(Strength,0.f,1.f);
    if(Now-GroundImpactStartedAt<.08 && Strength<=Existing) return;
    GroundImpactStrength=FMath::Max(Existing,Strength);
    GroundImpactSource=Source;GroundImpactStartedAt=Now;
}

void UMCPlayerCameraComponent::GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView)
{
    Super::GetCameraView(DeltaTime,DesiredView);
    const double Age=GetWorld()?GetWorld()->GetTimeSeconds()-ImpactStartedAt:ImpactDuration;
    if(ImpactStrength>0.f && Age>=0. && Age<ImpactDuration) {
        const float Amount=ImpactStrength*ImpactEnvelope(Age)*FMath::Clamp(ImpactShakeScale,0.f,2.f);
        const float Wave=FMath::Cos(float(Age)*2.f*PI*17.f);
        const float CrossWave=FMath::Sin(float(Age)*2.f*PI*13.f);
        const FVector Direction=DesiredView.Rotation.UnrotateVector(ImpactDirection);
        const float Side=Direction.Y<0.f?-1.f:1.f;
        // Angular feedback only; keep the eye position, lens and orbit input unchanged.
        DesiredView.Rotation+=FRotator(-.55f*Wave,.35f*CrossWave*Side,.16f*Wave*Side)*Amount;
    }
    const double GroundAge=GetWorld()?GetWorld()->GetTimeSeconds()-GroundImpactStartedAt:GroundImpactDuration;
    if(GroundImpactStrength>0.f && GroundAge>=0. && GroundAge<GroundImpactDuration) {
        const float Amount=GroundImpactStrength*ImpactEnvelope(GroundAge,GroundImpactDuration)*FMath::Clamp(ImpactShakeScale,0.f,2.f);
        const float Wave=FMath::Cos(float(GroundAge)*2.f*PI*7.f);
        const float CrossWave=FMath::Sin(float(GroundAge)*2.f*PI*9.f);
        const FVector Direction=DesiredView.Rotation.UnrotateVector(GroundImpactSource-GetComponentLocation());
        const float Side=Direction.Y<0.f?-1.f:1.f;
        DesiredView.Rotation+=FRotator(-.9f*Wave,.6f*CrossWave*Side,.35f*Wave*Side)*Amount;
    }
    const auto* Hero=Cast<AMCToothCharacter>(GetOwner());
    if (!Hero || !bAutoFocusPlayer || PostProcessBlendWeight<=0.f || !PostProcessSettings.bOverride_DepthOfFieldFocalDistance) return;
    const FVector Focus=Hero->GetCameraFocusLocation();
    // Use the final view after the spring arm's collision correction, and focus
    // on the avatar's depth plane even when the follow camera frames it off-center.
    const float Distance=FMath::Max(10.f,float(FVector::DotProduct(Focus-DesiredView.Location,DesiredView.Rotation.Vector())));
    PostProcessSettings.DepthOfFieldFocalDistance=Distance;
    DesiredView.PostProcessSettings.DepthOfFieldFocalDistance=Distance;
}
