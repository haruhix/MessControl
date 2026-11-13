#include "MCDeathSpiritComponent.h"
#include "MCPlayerController.h"
#include "MCToothCharacter.h"
#include "MCTongue.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
constexpr float SpiritSpeed=440.f;
constexpr double SteeringPacketInterval=.05;
constexpr double SteeringTimeout=.22;
}

void UMCDeathSpiritComponent::SetSteeringAxis(bool bRight,float Value)
{
    if(!IsSpiritActive() || IsPulling() || !FMath::IsFinite(Value)) return;
    (bRight?LocalSteering.Y:LocalSteering.X)=FMath::Clamp(Value,-1.f,1.f);
}

void UMCDeathSpiritComponent::ServerSetSteering_Implementation(FVector_NetQuantizeNormal Direction)
{
    const auto* Hero=Cast<AMCToothCharacter>(GetOwner());
    if(!Hero || !Hero->GetController() || !IsSpiritActive() || IsPulling() || Direction.ContainsNaN()) return;
    RemoteSteering=FVector(Direction.X,Direction.Y,0.).GetClampedToMaxSize(1.);
    RemoteSteeringExpiresAt=Clock()+SteeringTimeout;
}

void UMCDeathSpiritComponent::UpdateSteering(float DeltaSeconds)
{
    const auto* Hero=Cast<AMCToothCharacter>(GetOwner());
    const auto* PC=Hero?Cast<AMCPlayerController>(Hero->GetController()):nullptr;
    if(!PC || !IsSpiritActive() || IsPulling()) {
        LocalSteering=FVector2D::ZeroVector;
        RemoteSteering=FVector::ZeroVector;
        RemoteSteeringExpiresAt=0.;
        bSentSteering=false;
        return;
    }

    const double Now=Clock();
    if(!PC->IsLocalController()) {
        if(Hero->HasAuthority() && Now<RemoteSteeringExpiresAt) AdvanceSteering(RemoteSteering,DeltaSeconds);
        return;
    }

    if(PC->IsMoveInputIgnored() || PC->GetSpectatorTarget()) LocalSteering=FVector2D::ZeroVector;
    const FRotator View=PC->PlayerCameraManager?PC->PlayerCameraManager->GetCameraRotation():PC->GetControlRotation();
    const FVector Input=FVector(LocalSteering.X,LocalSteering.Y,0.).GetClampedToMaxSize(1.);
    const FVector Direction=FRotator(0.,View.Yaw,0.).RotateVector(Input);
    if(Hero->HasAuthority()) {
        AdvanceSteering(Direction,DeltaSeconds);
        return;
    }

    const bool bMoving=!Direction.IsNearlyZero();
    if((bMoving || bSentSteering) && Now>=NextSteeringPacketAt) {
        ServerSetSteering(FVector_NetQuantizeNormal(Direction));
        NextSteeringPacketAt=Now+SteeringPacketInterval;
        bSentSteering=bMoving;
    }
}

void UMCDeathSpiritComponent::AdvanceSteering(FVector Direction,float DeltaSeconds)
{
    if(Direction.IsNearlyZero() || DeltaSeconds<=0.f) return;
    const auto* Hero=CastChecked<AMCToothCharacter>(GetOwner());
    const float Step=FMath::Min(DeltaSeconds,.05f);
    const FVector Previous=State.Origin;
    FVector Candidate=Previous+Direction*(SpiritSpeed*Step);

    FHitResult Floor;
    AMCTongue* Tongue=nullptr;
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) {
        if(It->SurfacePoint(Candidate,Floor) && Floor.ImpactNormal.Z>.4f) {Tongue=*It;break;}
    }
    if(!Tongue) return;

    const double TargetZ=Floor.ImpactPoint.Z+Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    Candidate.Z=FMath::Lerp(Previous.Z,TargetZ,FMath::Clamp(7.f*Step,0.f,1.f));
    const double Now=Clock();
    const FVector Hover=HoverOffset(Now);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MCDeathSpiritSteering),false,GetOwner());
    Params.AddIgnoredActor(Tongue);
    FHitResult Wall;
    if(GetWorld()->SweepSingleByObjectType(Wall,Previous+Hover,Candidate+Hover,FQuat::Identity,
        FCollisionObjectQueryParams(ECC_WorldStatic),FCollisionShape::MakeSphere(25.f),Params)
        && (!Wall.bStartPenetrating || FVector::DotProduct(Candidate-Previous,Wall.Normal)<=0.)) return;

    State.Origin=Candidate;
    State.FacingYaw=FMath::FixedTurn(State.FacingYaw,float(Direction.Rotation().Yaw),780.f*Step);
    if(Now>=NextSteeringReplicationAt) {
        NextSteeringReplicationAt=Now+SteeringPacketInterval;
        GetOwner()->ForceNetUpdate();
    }
}
