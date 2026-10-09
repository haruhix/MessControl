#include "MCGroundImpactSubsystem.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

void UMCGroundImpactSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if(GetWorld()->GetNetMode()!=NM_Client)
        SpawnHandle=GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this,&UMCGroundImpactSubsystem::QueueActor));
}

void UMCGroundImpactSubsystem::Deinitialize()
{
    if(SpawnHandle.IsValid()) GetWorld()->RemoveOnActorSpawnedHandler(SpawnHandle);
    for(auto& Pair:Bodies) if(auto* Body=Pair.Key.Get()) {
        Body->OnComponentHit.RemoveDynamic(this,&UMCGroundImpactSubsystem::OnBodyHit);
        if(!Pair.Value.bOriginallyNotify) Body->SetNotifyRigidBodyCollision(false);
    }
    Bodies.Reset();PendingActors.Reset();PendingImpacts.Reset();
    Super::Deinitialize();
}

void UMCGroundImpactSubsystem::QueueActor(AActor* Actor)
{
    if(GetWorld()->GetNetMode()!=NM_Client && Actor && !Cast<APawn>(Actor)) PendingActors.Add(Actor);
}

void UMCGroundImpactSubsystem::RegisterActor(AActor* Actor)
{
    if(!IsValid(Actor) || Cast<APawn>(Actor)) return;
    TInlineComponentArray<UPrimitiveComponent*> Components;Actor->GetComponents(Components);
    for(auto* Body:Components) {
        if(!Body->IsSimulatingPhysics() || Body->GetMass()<5.f || Body->Bounds.SphereRadius<45.f || Bodies.Contains(Body)) continue;
        FBodyState State;State.bOriginallyNotify=Body->BodyInstance.bNotifyRigidBodyCollision;
        Bodies.Add(Body,State);
        Body->SetNotifyRigidBodyCollision(true);
        Body->OnComponentHit.AddUniqueDynamic(this,&UMCGroundImpactSubsystem::OnBodyHit);
    }
}

void UMCGroundImpactSubsystem::OnBodyHit(UPrimitiveComponent* Body,AActor* OtherActor,UPrimitiveComponent* OtherBody,FVector NormalImpulse,const FHitResult& Hit)
{
    if(GetWorld()->GetNetMode()==NM_Client || !IsValid(Body) || !IsValid(OtherBody) || !Hit.bBlockingHit
        || Hit.ImpactNormal.Z<.55f || Cast<APawn>(OtherActor) || OtherBody->IsSimulatingPhysics()
        || NormalImpulse.ContainsNaN() || Hit.ImpactPoint.ContainsNaN()) return;
    auto* State=Bodies.Find(Body);if(!State) return;
    const double Now=GetWorld()->GetTimeSeconds();
    if(Now-State->LastImpactAt<.35) return;
    const float Mass=Body->GetMass();
    if(!FMath::IsFinite(Mass) || Mass<5.f || Body->Bounds.SphereRadius<45.f) return;
    // Chaos has already resolved velocity here. The normal impulse retains the
    // incoming speed; floor support/friction impulses stay below the landing gate.
    const float Speed=float(FVector::DotProduct(NormalImpulse,Hit.ImpactNormal))/Mass;
    if(!FMath::IsFinite(Speed) || Speed<180.f) return;
    const float MetresPerSecond=FMath::Min(Speed,2500.f)*.01f;
    const float Energy=.5f*FMath::Min(Mass,5000.f)*MetresPerSecond*MetresPerSecond;
    const float Strength=FMath::Clamp(FMath::Sqrt(Energy/900.f),0.f,1.f);
    State->LastImpactAt=Now;
    const FImpact Impact{Hit.ImpactPoint,Strength,FMath::Lerp(1000.f,2600.f,Strength)};
    if(PendingImpacts.Num()<32) PendingImpacts.Add(Impact);
    else {
        int32 Weakest=0;for(int32 I=1;I<PendingImpacts.Num();++I)
            if(PendingImpacts[I].Strength<PendingImpacts[Weakest].Strength) Weakest=I;
        if(Strength>PendingImpacts[Weakest].Strength) PendingImpacts[Weakest]=Impact;
    }
}

void UMCGroundImpactSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    UWorld* World=GetWorld();if(World->GetNetMode()==NM_Client || !World->HasBegunPlay()) return;
    for(const auto& Actor:PendingActors) RegisterActor(Actor.Get());PendingActors.Reset();
    DiscoveryRemaining-=DeltaTime;
    if(DiscoveryRemaining<=0.f) {
        DiscoveryRemaining=1.f;
        // Also find streamed props and components that enable simulation after spawning.
        for(TActorIterator<AActor> It(World);It;++It) RegisterActor(*It);
        for(auto It=Bodies.CreateIterator();It;++It) if(!It.Key().IsValid()) It.RemoveCurrent();
    }
    const double Now=World->GetTimeSeconds();
    if(PendingImpacts.IsEmpty() || Now-LastDispatchAt<.08) return;
    LastDispatchAt=Now;
    for(TActorIterator<AMCToothCharacter> It(World);It;++It) {
        auto* Hero=*It;if(!Cast<APlayerController>(Hero->GetController()) || !Hero->Status->IsAlive()) continue;
        float BestStrength=0.f;FVector Source=FVector::ZeroVector;
        for(const auto& Impact:PendingImpacts) {
            const float Falloff=FMath::Clamp(1.f-float(FVector::Dist(Hero->GetActorLocation(),Impact.Point))/Impact.Radius,0.f,1.f);
            const float Strength=Impact.Strength*Falloff;
            if(Strength>BestStrength) {BestStrength=Strength;Source=Impact.Point;}
        }
        if(BestStrength>.025f) Hero->NotifyGroundImpact(BestStrength,Source);
    }
    PendingImpacts.Reset();
}
