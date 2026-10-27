#include "MCFoodActor.h"
#include "MCNutEnemy.h"
#include "MCNutRainEvent.h"
#include "MCToothCharacter.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"

bool AMCFoodActor::IsNutEncounterFragment() const
{
    const auto* Encounter=Cast<AMCNutRainEvent>(GetOwner());
    // Ownership survives cutting and replicates; actor Tags alone are not
    // replicated and cannot define the local E/Q interaction mode.
    return bFragment && !bBrushTool && FoodData.Kind==EMCFoodKind::Food
        && IsValid(Encounter) && Encounter->Settings.bBossEncounter;
}

void AMCFoodActor::UpdateNutFragmentThrowImpact()
{
    // Only an intentional Q launch arms a nut piece. Loose, cut and carried
    // fragments retain their existing harmless contact with players and enemies.
    if(!HasAuthority() || !bManualThrowInFlight || !IsNutEncounterFragment()) return;
    const auto* Encounter=CastChecked<AMCNutRainEvent>(GetOwner());
    if(Encounter->IsActorBeingDestroyed() || Encounter->bFailed || Encounter->Stage!=EMCNutRainStage::Enemies) {
        EndManualThrowFlight();return;
    }
    auto* Thrower=ManualThrower.Get();
    if(!IsValid(Thrower) || Thrower->GetWorld()!=GetWorld() || IsDisposed() || StackCarrier
        || !Holders.IsEmpty() || (Phase!=EMCFoodPhase::Free && Phase!=EMCFoodPhase::Falling) || !Body->IsSimulatingPhysics()) {
        EndManualThrowFlight();return;
    }
    const FVector End=GetActorLocation(),Start=NutThrowSweepStart;
    NutThrowSweepStart=End;
    if(Start.ContainsNaN() || End.ContainsNaN() || Start.Equals(End,.01)) return;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCNutFragmentThrow),false,this);
    // The retained fragment collision policy lets pieces pass through teammates.
    // Their visibility colliders must not prematurely disarm this enemy contact.
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) Query.AddIgnoredActor(*It);
    FHitResult Hit;
    // The enemy body is query-only. Sweep the travelled segment instead of
    // relying on Chaos contacts or testing the endpoint at a high throw speed.
    const float Radius=FMath::Clamp(float(Body->GetScaledBoxExtent().GetMin()),4.f,24.f);
    if(!GetWorld()->SweepSingleByChannel(Hit,Start,End,FQuat::Identity,ECC_Visibility,
        FCollisionShape::MakeSphere(Radius),Query)) return;
    auto* Enemy=Cast<AMCNutEnemy>(Hit.GetActor());
    if(!Enemy || Enemy->GetOwner()!=Encounter || !Enemy->CanReceiveToolHit()) {
        // Scenery and enemies outside this live encounter also occlude the shot.
        EndManualThrowFlight();return;
    }
    const FVector Velocity=PrePhysicsVelocity.IsNearlyZero()?Body->GetPhysicsLinearVelocity():PrePhysicsVelocity;
    const float Speed=FVector::DotProduct(Velocity-Enemy->GetVelocity(),-Hit.ImpactNormal.GetSafeNormal());
    EndManualThrowFlight(); // Consume the launch before damage/feedback can re-enter.
    if(!FMath::IsFinite(Speed) || Speed<Settings.ImpactSpeed) return;
    const float Damage=FMath::Min(Settings.MaxDamage,FMath::Clamp(Speed*Settings.Mass*.025f,4.f,18.f));
    // The ordinary virtual resolver retains boss shield, resistance and feedback
    // behavior. This never delivers/disposes food or awards a second food task.
    Enemy->ReceiveToolDamage(Damage,Thrower);
    ++ConfirmedImpacts;
    const FVector Normal=Hit.ImpactNormal.GetSafeNormal();
    SetActorLocation(Hit.Location+Normal*2,false,nullptr,ETeleportType::TeleportPhysics);
    Body->SetPhysicsLinearVelocity((Velocity-2*FVector::DotProduct(Velocity,Normal)*Normal)*.2f+FVector(0,0,45));
    Body->SetPhysicsAngularVelocityInRadians(FVector(0,2,1));
    Body->WakeAllRigidBodies();ForceNetUpdate();
}
