#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCGripComponent.h"

void AMCToothCharacter::UpdateLocomotion(float Dt)
{
    const auto* Move=CastChecked<UMCToothMovementComponent>(GetCharacterMovement());
    const bool Ground=Move->IsMovingOnGround() && ToothPhysics->CanAct();
    const float Blend=1-FMath::Exp(-10*Dt);
    AnimationClimb=FMath::Lerp(AnimationClimb,Move->IsClimbing()?1.f:0.f,Blend);
    if(Move->IsClimbing()) AnimationClimbPhase=FMath::Fmod(AnimationClimbPhase+Dt*GetVelocity().Size()*.04f,2*PI);
    AnimationAir=FMath::Lerp(AnimationAir,Move->IsFalling() && ToothPhysics->CanAct()?1.f:0.f,Blend);
    AnimationLanding=FMath::Lerp(AnimationLanding,LandingImpulse,1-FMath::Exp(-28.f*Dt));
    AnimationRun=FMath::Lerp(AnimationRun,Ground && Move->bSprintActive?1.f:0.f,Blend);
    AnimationSticky=FMath::Lerp(AnimationSticky,Ground && Move->GroundSurface==EMCGroundSurface::Sticky?1.f:0.f,Blend);
    AnimationSlip=FMath::Lerp(AnimationSlip,Ground && Move->GroundSurface==EMCGroundSurface::Slippery?1.f:0.f,Blend);
    const FVector Velocity=GetVelocity(); const float Speed=Velocity.Size2D();
    const FVector Input=Move->Intent();
    const float Load=Grip->LoadMass();
    const float Resistance=Move->HasHeavyGrip()?FMath::Clamp(1-Speed/FMath::Max(40.f,Move->GetMaxSpeed()),0.f,1.f):0;
    const float Effort=Ground && !Input.IsNearlyZero()?FMath::Clamp(Load/32.f+Resistance*.5f+AnimationSticky*.35f,0.f,1.f):0;
    AnimationEffort=FMath::Lerp(AnimationEffort,Effort,Blend);
    // Feet stop paddling when momentum carries a passive player across a slippery floor.
    float Drive=Speed*(1-AnimationSlip*(Input.IsNearlyZero()?.92f:.35f));
    if (Ground && Move->HasHeavyGrip() && !Input.IsNearlyZero()) Drive=FMath::Max(Drive,35.f*AnimationEffort);
    const float PreviewTime=FMath::Fmod(GetWorld()->GetTimeSeconds(),8.f);
    const float Target=bPreviewAnimation?(PreviewTime<3?.8f:0):Ground?FMath::Clamp(Drive/FMath::Max(1.f,Move->WalkSpeed),0.f,1.25f):0;
    AnimationSpeed=FMath::Lerp(AnimationSpeed,Target,Blend);
    const FVector Direction=Speed>10?Velocity.GetSafeNormal2D():Input;
    const FVector Local=GetActorTransform().InverseTransformVectorNoScale(Direction);
    if (!Local.IsNearlyZero()) AnimationDirection=FMath::Lerp(AnimationDirection,Local,Blend);
    const FVector Accel=(Velocity-PreviousLocomotionVelocity)/FMath::Max(.008f,Dt); PreviousLocomotionVelocity=Velocity;
    const FVector LocalAccel=GetActorTransform().InverseTransformVectorNoScale(Accel).GetClampedToMaxSize(1400);
    AnimationInertia=FMath::Lerp(AnimationInertia,Ground?LocalAccel/1400.f:FVector::ZeroVector,1-FMath::Exp(-7*Dt));
    AnimationStance=FMath::Lerp(.62f,.48f,AnimationRun);
    AnimationStance=FMath::Lerp(AnimationStance,.76f,AnimationSticky);
    // Keep phase continuous across run/surface/load changes. Short effort steps also work at a blocked load.
    const float Cycles=FMath::Clamp(Drive/115.f,0.f,4.5f)*(1-.18f*AnimationSticky);
    if (Ground || bPreviewAnimation) Gait=FMath::Fmod(Gait+Dt*(bPreviewAnimation?Target*2.8f:Cycles)*2*PI*AnimationSettings.Tempo,2*PI);
}
