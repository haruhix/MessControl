#include "MCToothMovementComponent.h"
#include "MCCoffeeFlood.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"

UMCToothMovementComponent::UMCToothMovementComponent()
{
    MaxSwimSpeed=320; GetNavAgentPropertiesRef().bCanSwim=true;
}
AMCCoffeeFlood* UMCToothMovementComponent::DeepWaterAt(FVector P,bool Continuing) const
{
    if (!CharacterOwner || P.ContainsNaN()) return nullptr;
    const float Half=CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    for (TActorIterator<AMCCoffeeFlood> It(GetWorld());It;++It)
    {
        if (!It->Contains(P)) continue;
        const float Surface=It->SurfaceHeightAt(P);
        if (Surface-(P.Z-Half)<Half*(Continuing?.45f:.95f)) continue;
        FHitResult Floor; FCollisionQueryParams Params(SCENE_QUERY_STAT(MCSwimDepth),false,CharacterOwner);
        if (GetWorld()->LineTraceSingleByChannel(Floor,P,P-FVector(0,0,Half+1000),ECC_WorldStatic,Params)
            && Surface-Floor.ImpactPoint.Z<Half*(Continuing?1.f:1.2f)) continue;
        return *It;
    }
    return nullptr;
}
void UMCToothMovementComponent::UpdateCharacterStateBeforeMovement(float Dt)
{
    Super::UpdateCharacterStateBeforeMovement(Dt);
    const auto* Hero=Cast<AMCToothCharacter>(CharacterOwner);
    if (!Hero || !Hero->ToothPhysics || !Hero->ToothPhysics->CanAct()) return;
    if (DeepWaterAt(UpdatedComponent->GetComponentLocation(),IsSwimming()))
    {
        if (!IsSwimming()) SetMovementMode(MOVE_Swimming);
    }
    else if (IsSwimming()) SetMovementMode(MOVE_Falling);
}
void UMCToothMovementComponent::PhysSwimming(float Dt,int32 Iterations)
{
    if (Dt<MIN_TICK_TIME || !HasValidData()) return;
    auto* Hero=Cast<AMCToothCharacter>(CharacterOwner); if (!Hero) return;
    float Remaining=Dt;
    while (Remaining>=MIN_TICK_TIME && Iterations<MaxSimulationIterations)
    {
        ++Iterations; const float Step=FMath::Min(Remaining,.033f); Remaining-=Step;
        const FVector Before=UpdatedComponent->GetComponentLocation();
        auto* Water=DeepWaterAt(Before,true);
        if (!Water) { SetMovementMode(MOVE_Falling); StartNewPhysics(Remaining+Step,Iterations); return; }
        if (Hero->ClingTooth) { Velocity=FVector::ZeroVector; return; }
        const FVector Input=FVector(Acceleration.X,Acceleration.Y,0).GetClampedToMaxSize(GetMaxAcceleration())/FMath::Max(1.f,GetMaxAcceleration());
        // The old ragdoll paddle could not overcome the drain. A deliberate stroke
        // is stronger, while an idle swimmer still drifts towards the throat.
        const FVector Drive=Input*Water->Paddle*Water->WaterSettings.SwimStrokeMultiplier+Water->FlowAtPosition(Before,Hero);
        const FVector A=Water->WaterSettings.FloatAcceleration(Water->SurfaceHeightAt(Before),Before,Velocity,Drive,0,Water->WaterSettings.SwimFloatDepth);
        Velocity+=A*Step;
        const FVector Horizontal=FVector(Velocity.X,Velocity.Y,0).GetClampedToMaxSize(MaxSwimSpeed);
        Velocity=FVector(Horizontal.X,Horizontal.Y,FMath::Clamp(Velocity.Z,-220.,260.));
        FHitResult Hit;
        SafeMoveUpdatedComponent(Velocity*Step,UpdatedComponent->GetComponentQuat(),true,Hit);
        if (Hit.IsValidBlockingHit())
        {
            HandleImpact(Hit,Step,Velocity*Step);
            if (!Hero->ToothPhysics->CanAct()) return;
            SlideAlongSurface(Velocity*Step,1-Hit.Time,Hit.Normal,Hit,true);
            Velocity=(UpdatedComponent->GetComponentLocation()-Before)/Step;
        }
    }
}
