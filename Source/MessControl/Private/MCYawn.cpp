#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCFoodCollectionComponent.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCReactionVFX.h"
#include "MCThroat.h"
#include "MCFoodActor.h"
#include "MCGameState.h"
#include "MCGazeComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"

bool AMCToothCharacter::IsYawning() const
{
    const auto* GS=GetWorld()?GetWorld()->GetGameState():nullptr;
    return YawnEndsAt>(GS?GS->GetServerWorldTimeSeconds():GetWorld()?GetWorld()->GetTimeSeconds():0);
}
void AMCToothCharacter::BeginYawn(AMCTongue* Tongue,float Seconds)
{
    if(!HasAuthority() || !Tongue || !Status->IsAlive() || SwallowedBy || MimicCaptor) return;
    // The tongue owns the air event, rather than an automatic hand/foot anchor.
    // Keep input, held food and locomotion alive so the player can resist it.
    YawnTongue=Tongue;YawnAnchor=FVector::ZeroVector;
    YawnPullDirection=FVector::ForwardVector;float ClosestMouth=FLT_MAX;
    for(TActorIterator<AMCThroat> It(GetWorld());It;++It) {
        const FVector Inlet=It->VacuumInlet();
        const float Distance=FVector::DistSquared2D(Inlet,GetActorLocation());
        if(Distance<ClosestMouth) {ClosestMouth=Distance;YawnPullDirection=(Inlet-GetActorLocation()).GetSafeNormal2D();}
    }
    if(YawnPullDirection.IsNearlyZero()) YawnPullDirection=FVector::ForwardVector;
    YawnStartedAt=Tongue->ServerTime();YawnEndsAt=YawnStartedAt+FMath::Clamp(Seconds,1.f,8.f);OnRep_Yawn();ForceNetUpdate();
}
float AMCToothCharacter::YawnPoseAlpha() const
{
    if(!IsYawning()) return 0;
    const auto* GS=GetWorld()->GetGameState();const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    return FMath::SmoothStep(.08f,.6f,float(Now-YawnStartedAt))*FMath::SmoothStep(0.f,.65f,float(YawnEndsAt-Now));
}
FVector AMCToothCharacter::YawnWindVelocity() const
{
    if(!IsYawning() || !IsValid(YawnTongue) || !Status->IsAlive() || SwallowedBy || MimicCaptor) return FVector::ZeroVector;
    const float Speed=FMath::IsFinite(YawnTongue->YawnWindSpeed)?FMath::Clamp(YawnTongue->YawnWindSpeed,0.f,500.f):260.f;
    return YawnPullDirection.GetSafeNormal2D()*Speed*YawnPoseAlpha();
}
FVector AMCToothCharacter::YawnHandPoint(int32 Side) const
{
    // Retained for callers of the old pose helper; hands are no longer planted.
    return GetActorLocation()+GetActorRightVector()*(Side==0?-35:35);
}
void AMCToothCharacter::OnRep_Yawn()
{
    if(IsYawning() && HasAuthority() && Gaze) {
        FVector Focus=GetActorLocation()-YawnPullDirection*300;
        Focus.Z=GetMesh()->GetSocketLocation(RigBone(TEXT("eye_l"))).Z+8;
        Gaze->NoticePoint(Focus,.8f);
    }
}
void AMCToothCharacter::UpdateYawn(float Dt)
{
    if(!IsYawning() && YawnEndsAt!=0 && HasAuthority()) {
        YawnEndsAt=0;YawnTongue=nullptr;OnRep_Yawn();ForceNetUpdate();
    }
}
bool AMCTongue::StartYawn(float Seconds)
{
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    if(!HasAuthority() || IsYawnActive() || (GS && (GS->Phase==EMCShiftPhase::Won || GS->Phase==EMCShiftPhase::Lost))) return false;
    YawnStartedAt=ServerTime();YawnDuration=FMath::Clamp(Seconds,1.f,8.f);NextYawnAt=ServerTime()+65;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) It->BeginYawn(this,YawnDuration);
    // One mouth-owned vacuum explains the pull. The broad end reaches across
    // the tongue and all the trails converge into the actual gameplay aperture.
    AMCThroat* Mouth=nullptr;float Closest=FLT_MAX;
    for(TActorIterator<AMCThroat> It(GetWorld());It;++It) {
        const float Distance=FVector::DistSquared(It->GetActorLocation(),GetActorLocation());
        if(Distance<Closest) {Closest=Distance;Mouth=*It;}
    }
    if(Mouth) {
        const FVector Inlet=Mouth->VacuumInlet();
        FVector Source=Surface->Bounds.Origin;FHitResult Ground;
        if(SurfacePoint(Source,Ground)) Source=Ground.ImpactPoint;
        Source+=FVector(0,0,65);
        const FVector Axis=(Inlet-Source).GetSafeNormal();
        const float Length=FMath::Clamp(float(FVector::Distance(Inlet,Source))+200.f,300.f,2600.f);
        if(auto* Flow=AMCReactionVFX::Spawn(GetWorld(),Inlet,EMCReactionEffect::Yawn,YawnDuration,Mouth->ZoneRadius*.8f,Axis,Length)) {
            Flow->SetOwner(Mouth);
            Flow->AttachToActor(Mouth,FAttachmentTransformRules::KeepWorldTransform);
        }
    }
    ForceNetUpdate();return true;
}
bool AMCTongue::IsYawnActive() const {return YawnStartedAt>=0 && ServerTime()<YawnStartedAt+YawnDuration;}
void AMCTongue::ResetYawn()
{
    if(!HasAuthority()) return;YawnStartedAt=-100;NextYawnAt=ServerTime()+65;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) {It->YawnEndsAt=ServerTime()-.01;It->UpdateYawn(0);}
    ForceNetUpdate();
}
