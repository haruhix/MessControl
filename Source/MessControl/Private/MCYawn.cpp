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
    CancelGameplayInput();DropFood();bBrushing=false;bHandling=false;ResetContact();
    YawnTongue=nullptr;
    FHitResult Hit;
    if(ToothPhysics->CanAct() && Tongue->SurfacePoint(GetActorLocation(),Hit)
        && FMath::Abs(GetActorLocation().Z-GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-Hit.ImpactPoint.Z)<95) {
        YawnTongue=Tongue;YawnAnchor=Tongue->GetActorTransform().InverseTransformPosition(Hit.ImpactPoint);
    }
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
FVector AMCToothCharacter::YawnHandPoint(int32 Side) const
{
    FVector P=YawnTongue?YawnTongue->GetActorTransform().TransformPosition(YawnAnchor):GetActorLocation()-FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    if(YawnTongue) {FHitResult Hit;if(YawnTongue->SurfacePoint(P,Hit)) P=Hit.ImpactPoint;}
    return P-YawnPullDirection*16+FVector::CrossProduct(FVector::UpVector,YawnPullDirection)*(Side==0?35:-35)+FVector(0,0,8);
}
void AMCToothCharacter::OnRep_Yawn()
{
    if(IsYawning()) {
        bBrushing=false;bHandling=false;GetCharacterMovement()->StopMovementImmediately();
        // Tiny dropped food must not pin an inhaled character. World geometry
        // still blocks the swept movement; food keeps its own physics bodies.
        for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if(!It->IsDisposed()) GetCapsuleComponent()->IgnoreActorWhenMoving(*It,true);
        if(YawnTongue) GetCharacterMovement()->SetMovementMode(MOVE_None);
    } else {
        if(GetCharacterMovement()->MovementMode==MOVE_None && !SwallowedBy && !MimicCaptor) GetCharacterMovement()->SetMovementMode(MOVE_Falling);
        for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if(It->StackCarrier!=this && !It->Holders.Contains(this)) GetCapsuleComponent()->IgnoreActorWhenMoving(*It,false);
    }
}
void AMCToothCharacter::UpdateYawn(float Dt)
{
    if(IsYawning()) {
        bBrushing=false;bHandling=false;ResetContact();
        if(YawnTongue && Status->IsAlive() && ToothPhysics->CanAct()) {
            GetCharacterMovement()->StopMovementImmediately();GetCharacterMovement()->SetMovementMode(MOVE_None);
            for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if(!It->IsDisposed()) GetCapsuleComponent()->IgnoreActorWhenMoving(*It,true);
            FHitResult Hit;
            const float Age=FMath::Max(0.,YawnTongue->ServerTime()-YawnStartedAt);
            const float Drag=72*FMath::SmoothStep(.12f,.85f,Age)+FMath::Sin(Age*7)*7*YawnPoseAlpha();
            const FVector Anchor=YawnTongue->GetActorTransform().TransformPosition(YawnAnchor)+YawnPullDirection*Drag;
            if(YawnTongue->SurfacePoint(Anchor,Hit)) {
                const FVector Goal=Hit.ImpactPoint+FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2);
                // Sweep keeps automatic grip from pulling a player through other geometry.
                SetActorLocation(Goal,true);
                // Face the planted hands while the inhalation drags the body and feet backwards.
                SetActorRotation(FMath::RInterpTo(GetActorRotation(),FRotator(0,(-YawnPullDirection).Rotation().Yaw,0),Dt,9.f));
                if(HasAuthority() && Gaze) {
                    // The planted mittens are too close and low for the eyes:
                    // aiming there hid both pupils behind the lower eyelids.
                    const FVector Eyes=(GetMesh()->GetSocketLocation(RigBone(TEXT("eye_l")))
                        +GetMesh()->GetSocketLocation(RigBone(TEXT("eye_r"))))*.5f;
                    FVector Focus=GetActorLocation()-YawnPullDirection*300;
                    Focus.Z=Eyes.Z+8;
                    Gaze->NoticePoint(Focus,.8f);
                }
            } else if(HasAuthority()) {YawnTongue=nullptr;GetCharacterMovement()->SetMovementMode(MOVE_Falling);ForceNetUpdate();}
        }
    } else if(YawnEndsAt!=0 && HasAuthority()) {YawnEndsAt=0;YawnTongue=nullptr;OnRep_Yawn();ForceNetUpdate();}
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
