#include "MCFoodActor.h"
#include "MCGameState.h"
#include "MCNutBoss.h"
#include "MCNutBossImpact.h"
#include "MCNutEnemy.h"
#include "MCNutRainEvent.h"
#include "MCReactionVFX.h"
#include "MCToothCharacter.h"
#include "MCVFXPresentationComponent.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"

void MCNutBossImpact::Fracture(AMCFoodActor& Nut,UMCVFXPresentationComponent* Presentation,FName Event,
    FVector Point,FVector Normal,float WholeScatterSpeed)
{
    if(!Nut.HasAuthority() || Nut.IsDisposed() || Nut.IsActorBeingDestroyed()) return;
    FMCPerkVFXGameplayValues Values;
    Values.Direction=Normal;
    Values.Radius=FMath::Clamp(float(Nut.Body->GetScaledBoxExtent().GetMax()),8.f,90.f);
    Values.Duration=.55f;
    Values.Intensity=1.f;
    // Spend the nut before presentation callbacks, without delivery credit.
    // The whole bait nut uses its real authored pieces. A thrown shell piece
    // is already a fragment and is spent outright.
    if(WholeScatterSpeed<=0 || !Nut.FractureKnightNut(Normal,WholeScatterSpeed,false)) Nut.Dispose();
    if(IsValid(Presentation)) Presentation->PlayEvent(Event,FTransform(Point),Values);
}

bool AMCFoodActor::FractureKnightNut(FVector Direction,float Speed,bool bPlayFoodBreak)
{
    if(!HasAuthority() || IsActorBeingDestroyed() || !IsNutKineticsActive() || !Body->IsSimulatingPhysics()
        || IsCutTransactionPending() || IsCutTransactionStaged() || Direction.ContainsNaN()
        || !FMath::IsFinite(Speed) || Speed<=0) return false;
    bool bHasAuthoredFragments=false;
    for(const auto& Ref:FoodData.FragmentMeshes) if(!Ref.IsNull()) {
        if(!Ref.Get()) return false;
        bHasAuthoredFragments=true;
    }
    if(!bHasAuthoredFragments) return false;

    const FVector Center=Body->GetCenterOfMass();
    const FVector Inherited=Body->GetPhysicsLinearVelocity().GetClampedToMaxSize(300.f)*.4;
    TArray<AMCFoodActor*> Pieces;
    if(!SpawnFragments(Direction,&Pieces)) return false;
    const FVector Forward=Direction.GetSafeNormal2D(SMALL_NUMBER,GetActorForwardVector());
    const FVector Side=FVector::CrossProduct(FVector::UpVector,Forward);
    const float Scatter=FMath::Clamp(Speed,240.f,680.f);
    for(int32 Index=0;Index<Pieces.Num();++Index) {
        auto* Piece=Pieces[Index];
        if(!IsValid(Piece) || !Piece->Body->IsSimulatingPhysics()) continue;
        const float Fan=Pieces.Num()>1?2.f*Index/(Pieces.Num()-1)-1.f:0.f;
        const FVector Radial=(Piece->Body->GetCenterOfMass()-Center).GetSafeNormal2D();
        const FVector Toss=(Forward*.7+Side*Fan*.9+Radial*.45).GetSafeNormal2D(SMALL_NUMBER,Forward);
        Piece->Body->SetPhysicsLinearVelocity(Inherited+Toss*Scatter+FVector::UpVector*(95.f+Scatter*.19f+FMath::Abs(Fan)*45.f));
        Piece->Body->SetPhysicsAngularVelocityInRadians(Side*3.+FVector::UpVector*Fan*4.);
        Piece->Body->WakeAllRigidBodies();
        Piece->ForceNetUpdate();
    }
    const FVector BurstOrigin=Visual->Bounds.Origin;
    const float BurstRadius=FMath::Clamp(float(Visual->Bounds.SphereRadius),35.f,150.f);
    Dispose();
    if(bPlayFoodBreak)
        AMCReactionVFX::SpawnForPresentation(GetWorld(),TEXT("Food.Break"),TEXT("BreakArt"),BurstOrigin,
            EMCReactionEffect::FoodBreak,.65f,BurstRadius,Forward,0,this);
    return true;
}

bool AMCFoodActor::HasFreshnessTimer() const
{
    // Encounter ownership replicates; custom nut rows must also hide timers on clients.
    return FoodData.Kind==EMCFoodKind::Food && ItemName!=TEXT("Nut") && ItemName!=TEXT("Walnut")
        && !Cast<AMCNutRainEvent>(GetOwner()) && !ActorHasTag(TEXT("MCNutRain"));
}

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
    const bool bBossContact=Enemy->IsA<AMCNutBoss>();
    // The ordinary resolver owns the shield, resistance and damage feedback.
    Enemy->ReceiveToolDamage(Damage,Thrower);
    ++ConfirmedImpacts;
    const FVector Normal=Hit.ImpactNormal.GetSafeNormal();
    if(bBossContact) {
        const auto* State=GetWorld()->GetGameState<AMCGameState>();
        MCNutBossImpact::Fracture(*this,State?State->NutThrowPresentation.Get():nullptr,
            TEXT("BossImpact"),Hit.ImpactPoint,Normal);
        return;
    }
    SetActorLocation(Hit.Location+Normal*2,false,nullptr,ETeleportType::TeleportPhysics);
    Body->SetPhysicsLinearVelocity((Velocity-2*FVector::DotProduct(Velocity,Normal)*Normal)*.2f+FVector(0,0,45));
    Body->SetPhysicsAngularVelocityInRadians(FVector(0,2,1));
    Body->WakeAllRigidBodies();ForceNetUpdate();
}
