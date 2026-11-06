#include "MCSpongeSkillAction.h"
#include "MCToothCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NiagaraComponent.h"
#include "Engine/World.h"
#include "Math/RotationMatrix.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCSpongeSkillReview,Log,All);

namespace
{
void SpongeReviewReject(const AMCSpongeSkillAction* Action,const TCHAR* Gate)
{
    if(FParse::Param(FCommandLine::Get(),TEXT("MCActiveSkillReview")))
        UE_LOG(LogMCSpongeSkillReview,Log,TEXT("MC_ACTIVE_SPONGE_START_REJECT gate=%s action=%s pawn=%s time=%.5f"),
            Gate,*GetPathNameSafe(Action),*GetPathNameSafe(Action->Hero()),Action->Now());
}

FVector SpongeRideFootprint(const AMCSpongeSkillAction* Action)
{
    return FVector(FMath::Clamp(Action->Param(TEXT("RideHalfLength"),80.f),20.f,180.f),
        FMath::Clamp(Action->Param(TEXT("RideHalfWidth"),42.f),20.f,150.f),12)
        *Action->PropTransform.GetScale3D();
}

void UpdateSpongeDeckPose(AMCSpongeSkillAction* Action,const FVector& Heading,float DeltaSeconds=0.f)
{
    const FVector Foot=MCActiveSkillMotion::FootPoint(Action->Hero());
    const double Height=FMath::Clamp(Action->Param(TEXT("DeckHeight"),45.2f),0.f,100.f)
        *Action->PropTransform.GetScale3D().Z;
    FVector Point,Normal;
    if(!MCActiveSkillMotion::HasGround(Action->Hero(),Point,Normal) || Normal.ContainsNaN() || Normal.Z<.1) {
        Point=Foot;
        Normal=FVector::UpVector;
    }

    // The floor sweep can hit different spots on the same plane. Sample its
    // height beneath the rider, then ease changes from the previous deck plane
    // evaluated at the new position so the board does not trail the pawn.
    double GroundZ=Foot.Z+FVector::DotProduct(Point-Foot,Normal)/Normal.Z;
    if(DeltaSeconds>0.f) {
        const FVector PreviousNormal=Action->GetActorUpVector();
        const FVector PreviousPoint=Action->GetActorLocation()-FVector::UpVector*(Height/PreviousNormal.Z);
        const double PreviousGroundZ=Foot.Z+FVector::DotProduct(PreviousPoint-Foot,PreviousNormal)/PreviousNormal.Z;
        const float Blend=1.f-FMath::Exp(-14.f*DeltaSeconds);
        // Keep the board close to the current support at real steps and ridges.
        GroundZ+=FMath::Clamp((PreviousGroundZ-GroundZ)*(1.f-Blend),-6.,6.);
        Normal=FMath::Lerp(PreviousNormal,Normal,Blend).GetSafeNormal();
    }
    const FVector Deck(Foot.X,Foot.Y,GroundZ+Height/Normal.Z);
    Action->SetActorLocationAndRotation(Deck,FRotationMatrix::MakeFromZX(Normal,Heading).ToQuat());
}
}

AMCSpongeSkillAction::AMCSpongeSkillAction()
{
    PrimaryActorTick.TickGroup=TG_PostPhysics;
    PropAsset=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/Props/SM_Buster_Sponge.SM_Buster_Sponge"));
    PropTransform.SetScale3D(FVector(1.5));
    LoopComponent->SetRelativeScale3D(PropTransform.GetScale3D());
    StartVFX=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Sponge_Start.NS_Sponge_Start"));
    LoopVFX=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Sponge_Loop.NS_Sponge_Loop"));
    EndVFX=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Sponge_End.NS_Sponge_End"));
    SoapTrailVFX=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Sponge_Trail.NS_Sponge_Trail"));
}
UMCSpongeSkillEffect::UMCSpongeSkillEffect() {ActionClass=AMCSpongeSkillAction::StaticClass();}

bool AMCSpongeSkillAction::CanStart() const
{
    if(!Super::CanStart()) {SpongeReviewReject(this,TEXT("base_context"));return false;}
    if(!MCActiveSkillMotion::IsEligible(Hero(),true)) {SpongeReviewReject(this,TEXT("eligibility"));return false;}
    if(!MCActiveSkillMotion::CanAcquire(Hero())) {SpongeReviewReject(this,TEXT("motion_lease"));return false;}
    const bool Clear=MCActiveSkillMotion::ClearRideFootprint(Hero(),this,Hero()->GetActorForwardVector(),SpongeRideFootprint(this),0,0);
    if(!Clear) SpongeReviewReject(this,TEXT("footprint"));
    return Clear;
}
void AMCSpongeSkillAction::StartCommitted()
{
    Super::StartCommitted();
    Heading=Hero()->GetActorForwardVector().GetSafeNormal2D();PreviousFoot=MCActiveSkillMotion::FootPoint(Hero());
    const float Duration=FMath::Clamp(Param(TEXT("RideSeconds"),8.f),.2f,30.f);
    const float Speed=FMath::Clamp(Param(TEXT("RideSpeed"),620.f),100.f,1200.f);
    EndsAt=Now()+Duration;LastSampleAt=Now();
    UpdateSpongeDeckPose(this,Heading);
    if(!Motion.Begin(Hero(),GetActionID(),Heading*Speed,false,Duration)) Finish(EMCActiveSkillEndReason::InputCanceled);
    else {UpdateRideVFX();PlaySkillCue(TEXT("Start"),PreviousFoot+FVector(0,0,3),Heading);StartLoop();UpdateRideVFX();UpdateSoapTrail();}
}
bool AMCSpongeSkillAction::RepeatInput()
{
    if(!IsCommitted() || IsFinished()) return false;
    Finish(EMCActiveSkillEndReason::Completed);return true;
}
void AMCSpongeSkillAction::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);if(IsFinished() || !IsCommitted() || !Hero()) return;
    UpdateRideVFX();
    if(!HasAuthority()) {UpdateSoapTrail();return;}
    if(Now()>=EndsAt) {Finish(EMCActiveSkillEndReason::Completed);return;}
    if(!MCActiveSkillMotion::IsEligible(Hero(),true) || !Motion.IsOwned()) {Finish(EMCActiveSkillEndReason::InputCanceled);return;}
    const float Seconds=float(Now()-LastSampleAt);
    const float Speed=FMath::Clamp(Param(TEXT("RideSpeed"),620.f),100.f,1200.f);
    const FVector Foot=MCActiveSkillMotion::FootPoint(Hero());
    if(Seconds>0 && !MCActiveSkillMotion::ContinuousPath(PreviousFoot,Foot,Seconds,Speed)) {
        FVector Ground,Normal;const auto* Move=Hero()->GetCharacterMovement();
        const FVector Travel=Foot-PreviousFoot;
        const float NativeTravel=Speed*Seconds;
        const float WalkableZ=FMath::Clamp(Move->GetWalkableFloorZ(),.1f,1.f);
        const float MaxSlope=FMath::Sqrt(FMath::Max(0.f,1.f-WalkableZ*WalkableZ))/WalkableZ;
        // CMC keeps horizontal speed on a ramp, adding its real support Z.
        // A bounded hitch may continue the ride, but never paint its unseen
        // interval or connect the old world-space particles across that gap.
        const bool BoundedHitch=FMath::IsFinite(Seconds) && Seconds>.25f && Seconds<=1.f
            && !PreviousFoot.ContainsNaN() && !Foot.ContainsNaN() && FMath::IsFinite(Move->MaxStepHeight)
            && MCActiveSkillMotion::HasGround(Hero(),Ground,Normal)
            && Travel.Size2D()<=NativeTravel*1.1f+5.f
            && FMath::Abs(Travel.Z)<=NativeTravel*MaxSlope+FMath::Max(0.f,Move->MaxStepHeight)+5.f;
        if(!BoundedHitch) {Finish(EMCActiveSkillEndReason::InputCanceled);return;}
        EndSoapTrail(false);
        if(FParse::Param(FCommandLine::Get(),TEXT("MCActiveSkillReview")))
            UE_LOG(LogMCSpongeSkillReview,Log,TEXT("MC_ACTIVE_SPONGE_HITCH_CONTINUE action=%s seconds=%.5f travel=%s native_travel=%.5f"),
                *GetPathName(),Seconds,*Travel.ToString(),NativeTravel);
    } else {
        float HalfWidth=FMath::Clamp(float(SpongeRideFootprint(this).Y),5.f,150.f);
        if(const UStaticMesh* Mesh=PropMesh?PropMesh->GetStaticMesh():nullptr) {
            const FBox Bounds=Mesh->GetBoundingBox().TransformBy(PropMesh->GetRelativeTransform());
            HalfWidth=FMath::Clamp(float(Bounds.GetExtent().Y),5.f,150.f);
        }
        // A legacy tuning row can request a wider radius than the visible deck.
        const float CleanRadius=FMath::Clamp(Param(TEXT("CleanRadius"),HalfWidth),5.f,HalfWidth);
        MCActiveSkillMotion::CleanSweep(Hero(),this,PreviousFoot,Foot,Seconds,CleanRadius,
            FMath::Clamp(Param(TEXT("CleanRate"),1.4f),.01f,20.f),Speed,Param(TEXT("CleanOnContact"),1.f)>=.5f);
    }
    PreviousFoot=Foot;LastSampleAt=Now();
    const FVector PreviousHeading=Heading;
    if(Now()>=SteeringLockedUntil)
        Heading=MCActiveSkillMotion::Steering(Hero(),Heading,FMath::Clamp(Param(TEXT("TurnRate"),220.f),20.f,720.f),DeltaSeconds);
    const FVector Half=SpongeRideFootprint(this);
    const float Ahead=FMath::Min(DeltaSeconds,.05f);
    FHitResult Blocker;
    if(!MCActiveSkillMotion::ClearRideFootprint(Hero(),this,Heading,Half,Ahead,Speed,&Blocker)) {
        FVector Reflected=Heading,WallNormal=FVector::ZeroVector;
        int32 Reflections=0;bool Clear=false;float RemainingCorrection=45.f;
        // A corner can require both real wall normals before its outgoing
        // corridor clears. Never bounce from the walkable support itself.
        for(int32 Attempt=0;Attempt<3;++Attempt) {
            FVector Next;
            if(!MCActiveSkillMotion::ReflectRideDirection(Hero(),Blocker,Reflected,Next)) break;
            // PostPhysics can observe a hitch after the narrow capsule has
            // advanced farther than this wide prop's short lookahead. Recover
            // only the real wall overlap, with one shared correction budget.
            if(Blocker.bStartPenetrating
                && !MCActiveSkillMotion::RecoverSpongeWallOverlap(Hero(),this,Blocker,RemainingCorrection)) break;
            WallNormal=Blocker.ImpactNormal.GetSafeNormal2D();Reflected=Next;++Reflections;
            if(MCActiveSkillMotion::ClearRideFootprint(Hero(),this,Reflected,Half,Ahead,Speed,&Blocker)) {Clear=true;break;}
        }
        if(Clear) {
            Heading=Reflected;BounceCount+=Reflections;LastBounceNormal=WallNormal;
            SteeringLockedUntil=Now()+FMath::Clamp(Param(TEXT("BounceSteeringLock"),.2f),0.f,.5f);
            ForceNetUpdate();
        } else if(MCActiveSkillMotion::ClearRideFootprint(Hero(),this,PreviousHeading,Half,Ahead,Speed)) {
            // An attempted turn can rotate the wide nose into a wall even
            // though the existing tangent route is clear. Keep that route.
            Heading=PreviousHeading;
        } else {Finish(EMCActiveSkillEndReason::Completed);return;}
        // Do not count collision recovery as next tick's cleaning travel.
        if(RemainingCorrection<45.f) PreviousFoot=MCActiveSkillMotion::FootPoint(Hero());
    }
    Motion.Update(Heading*Speed);
    UpdateSpongeDeckPose(this,Heading,DeltaSeconds);
    UpdateRideVFX();
    UpdateSoapTrail();
}
void AMCSpongeSkillAction::UpdateRideVFX()
{
    if(!Hero() || !LoopComponent) return;
    FVector Contact=MCActiveSkillMotion::FootPoint(Hero());
    if(const UStaticMesh* Mesh=PropMesh?PropMesh->GetStaticMesh():nullptr) {
        const FBox Bounds=Mesh->GetBoundingBox();
        const FVector Center=Bounds.GetCenter();
        Contact=PropMesh->GetComponentTransform().TransformPosition(FVector(Center.X,Center.Y,Bounds.Min.Z));
    }
    LoopComponent->SetWorldLocation(Contact+GetActorUpVector()*(2.7*PropTransform.GetScale3D().Z));
    UpdateLoopVFX(GetActorForwardVector().GetSafeNormal2D());
}
void AMCSpongeSkillAction::OnFinished(EMCActiveSkillEndReason Reason)
{
    // A replica also receives WorldEnded on ordinary actor removal. Graceful
    // deactivation keeps its existing ground particles alive until their age ends.
    EndSoapTrail(GetWorld() && GetWorld()->bIsTearingDown);
    Motion.End();
    if(HasAuthority() && Reason!=EMCActiveSkillEndReason::WorldEnded)
        PlaySkillCue(TEXT("End"),Hero()?MCActiveSkillMotion::FootPoint(Hero())+FVector(0,0,3):GetActorLocation(),Heading);
    Super::OnFinished(Reason);
}
