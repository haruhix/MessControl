#include "MCSpongeSkillAction.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Math/RotationMatrix.h"

void AMCSpongeSkillAction::UpdateSoapTrail()
{
    if(GetNetMode()==NM_DedicatedServer || !IsCommitted() || IsFinished() || !Hero()) return;
    FVector Ground=FVector::ZeroVector,Normal=FVector::UpVector;
    // Other players' simulated pawns need no local PlayerController to render
    // a cosmetic trail; the authority-only start gate is intentionally not reused.
    const bool Supported=Hero()->Status && Hero()->Status->IsAlive()
        && Hero()->ToothPhysics && Hero()->ToothPhysics->CanAct()
        && MCActiveSkillMotion::HasGround(Hero(),Ground,Normal);
    if(!Supported) {
        EndSoapTrail(false);return;
    }
    const double At=Now();
    if(bHasSoapPoint && At<=LastSoapAt) return;
    const bool Continuous=!bHasSoapPoint || MCActiveSkillMotion::ContinuousPath(
        LastSoapPoint,Ground,float(At-LastSoapAt),FMath::Clamp(Param(TEXT("RideSpeed"),620.f),100.f,1200.f));
    // A discontinuity starts a fresh emitter at the new support. Its previous
    // particles keep fading independently, with no interpolated bridge.
    if(!Continuous) EndSoapTrail(false);
    const FVector Direction=FVector::VectorPlaneProject(GetActorForwardVector(),Normal).GetSafeNormal();
    FVector Velocity=FVector::VectorPlaneProject(Hero()->GetVelocity(),Normal).GetClampedToMaxSize(1200.f);
    if(Direction.IsNearlyZero() || Velocity.ContainsNaN()) Velocity=FVector::ZeroVector;
    LastSoapPoint=Ground;LastSoapAt=At;bHasSoapPoint=true;
    const FVector Position=Ground+Normal*2.2f;
    const FRotator Rotation=FRotationMatrix::MakeFromXZ(Direction.IsNearlyZero()?FVector::ForwardVector:Direction,Normal).Rotator();
    if(!IsValid(SoapTrailComponent)) {
        if(Velocity.IsNearlyZero(1.f)) return;
        UNiagaraSystem* System=SoapTrailVFX.LoadSynchronous();
        if(!System) return; // Known visual is loaded through the reflected readiness roots.
        // At-location components are world owned, so actor destruction does not
        // destroy the particles that are still fading on the floor.
        SoapTrailComponent=UNiagaraFunctionLibrary::SpawnSystemAtLocation(this,System,Position,Rotation,
            FVector::OneVector,true,false,ENCPoolMethod::None);
        if(!IsValid(SoapTrailComponent)) return;
        SoapTrailComponent->SetAbsolute(true,true,true);
        SoapTrailComponent->SetTickBehavior(ENiagaraTickBehavior::UsePrereqs);
        SoapTrailComponent->AddTickPrerequisiteActor(this);
        // Standalone Niagara preview demonstrates motion; gameplay always
        // consumes this pawn's real support, position and velocity.
        SoapTrailComponent->SetVariableBool(TEXT("User.PreviewMotion"),false);
        const auto Tune=[this](FName Key,FName User,float Low,float High) {
            const float Value=Param(Key,-1.f);
            if(Value>=0) SoapTrailComponent->SetVariableFloat(User,FMath::Clamp(Value,Low,High));
        };
        // Absent row overrides retain the Niagara artist's exposed defaults.
        Tune(TEXT("TrailWidth"),TEXT("User.Width"),10.f,300.f);
        Tune(TEXT("TrailLifetime"),TEXT("User.Lifetime"),.2f,3.f);
        Tune(TEXT("TrailOpacity"),TEXT("User.Opacity"),0.f,1.f);
        Tune(TEXT("TrailSpawnSpacing"),TEXT("User.SpawnSpacing"),16.f,80.f);
        SoapTrailComponent->SetVariableVec3(TEXT("User.Direction"),Direction);
        SoapTrailComponent->SetVariableVec3(TEXT("User.SurfaceNormal"),Normal);
        SoapTrailComponent->SetVariableVec3(TEXT("User.Velocity"),Velocity);
        SoapTrailComponent->Activate(true);
    }
    SoapTrailComponent->SetWorldLocationAndRotation(Position,Rotation);
    SoapTrailComponent->SetVariableVec3(TEXT("User.Direction"),Direction);
    SoapTrailComponent->SetVariableVec3(TEXT("User.SurfaceNormal"),Normal);
    SoapTrailComponent->SetVariableVec3(TEXT("User.Velocity"),Velocity);
}

void AMCSpongeSkillAction::EndSoapTrail(bool bImmediate)
{
    if(IsValid(SoapTrailComponent)) {
        SoapTrailComponent->SetVariableVec3(TEXT("User.Velocity"),FVector::ZeroVector);
        SoapTrailComponent->RemoveTickPrerequisiteActor(this);
        if(bImmediate) SoapTrailComponent->DeactivateImmediate();
        else SoapTrailComponent->Deactivate();
    }
    SoapTrailComponent=nullptr;bHasSoapPoint=false;
}
