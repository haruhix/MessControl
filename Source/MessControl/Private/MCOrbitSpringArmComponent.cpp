#include "MCOrbitSpringArmComponent.h"

FVector UMCOrbitSpringArmComponent::BlendLocations(const FVector& Desired,const FVector& Hit,bool bBlocked,float Dt)
{
    const FVector Origin=GetComponentLocation()+TargetOffset;
    const FVector Safe=bBlocked?Hit:Desired;
    const float Limit=FVector::Distance(Origin,Safe);
    if(SafeArmLength<0 || FVector::DistSquared(Origin,LastOrigin)>FMath::Square(1400.f)) SafeArmLength=Limit;
    // Smooth only outward travel. A newly closer hit always wins this frame,
    // so the filtered camera stays on the validated part of the sphere sweep.
    SafeArmLength=FMath::Min(Limit,FMath::Lerp(SafeArmLength,Limit,1-FMath::Exp(-12.f*FMath::Max(0.f,Dt))));
    LastOrigin=Origin;
    return Origin+(Safe-Origin).GetSafeNormal()*SafeArmLength;
}
