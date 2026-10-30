#include "MCSpongeSkillAction.h"
#include "MCToothCharacter.h"
#include "NiagaraComponent.h"
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
}

AMCSpongeSkillAction::AMCSpongeSkillAction()
{
    PrimaryActorTick.TickGroup=TG_PostPhysics;
    PropAsset=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/Props/SM_Buster_Sponge.SM_Buster_Sponge"));
    StartVFX=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Sponge_Start.NS_Sponge_Start"));
    LoopVFX=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Sponge_Loop.NS_Sponge_Loop"));
    EndVFX=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Sponge_End.NS_Sponge_End"));
}
UMCSpongeSkillEffect::UMCSpongeSkillEffect() {ActionClass=AMCSpongeSkillAction::StaticClass();}

bool AMCSpongeSkillAction::CanStart() const
{
    if(!Super::CanStart()) {SpongeReviewReject(this,TEXT("base_context"));return false;}
    if(!MCActiveSkillMotion::IsEligible(Hero(),true)) {SpongeReviewReject(this,TEXT("eligibility"));return false;}
    if(!MCActiveSkillMotion::CanAcquire(Hero())) {SpongeReviewReject(this,TEXT("motion_lease"));return false;}
    const bool Clear=MCActiveSkillMotion::ClearRideFootprint(Hero(),this,Hero()->GetActorForwardVector(),
        FVector(FMath::Clamp(Param(TEXT("RideHalfLength"),80.f),20.f,180.f),FMath::Clamp(Param(TEXT("RideHalfWidth"),42.f),20.f,150.f),12),0,0);
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
    const FTransform Pose=MCActiveSkillMotion::RidePose(Hero(),Heading,FMath::Clamp(Param(TEXT("DeckHeight"),45.2f),0.f,100.f));
    SetActorLocationAndRotation(Pose.GetLocation(),Pose.GetRotation());
    if(!Motion.Begin(Hero(),GetActionID(),Heading*Speed,false,Duration)) Finish(EMCActiveSkillEndReason::InputCanceled);
    else {UpdateRideVFX();PlaySkillCue(TEXT("Start"),PreviousFoot+FVector(0,0,3),Heading);StartLoop();UpdateRideVFX();}
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
    if(!HasAuthority()) return;
    if(Now()>=EndsAt) {Finish(EMCActiveSkillEndReason::Completed);return;}
    if(!MCActiveSkillMotion::IsEligible(Hero(),true) || !Motion.IsOwned()) {Finish(EMCActiveSkillEndReason::InputCanceled);return;}
    const float Seconds=float(Now()-LastSampleAt);
    const float Speed=FMath::Clamp(Param(TEXT("RideSpeed"),620.f),100.f,1200.f);
    const FVector Foot=MCActiveSkillMotion::FootPoint(Hero());
    if(Seconds>0 && !MCActiveSkillMotion::ContinuousPath(PreviousFoot,Foot,Seconds,Speed)) {Finish(EMCActiveSkillEndReason::InputCanceled);return;}
    MCActiveSkillMotion::CleanSweep(Hero(),this,PreviousFoot,Foot,Seconds,FMath::Clamp(Param(TEXT("CleanRadius"),45.f),5.f,150.f),
        FMath::Clamp(Param(TEXT("CleanRate"),1.4f),.01f,20.f),Speed);
    PreviousFoot=Foot;LastSampleAt=Now();
    Heading=MCActiveSkillMotion::Steering(Hero(),Heading,FMath::Clamp(Param(TEXT("TurnRate"),220.f),20.f,720.f),DeltaSeconds);
    if(!MCActiveSkillMotion::ClearRideFootprint(Hero(),this,Heading,
        FVector(FMath::Clamp(Param(TEXT("RideHalfLength"),80.f),20.f,180.f),FMath::Clamp(Param(TEXT("RideHalfWidth"),42.f),20.f,150.f),12),FMath::Min(DeltaSeconds,.05f),Speed)) {
        Finish(EMCActiveSkillEndReason::Completed);return;
    }
    Motion.Update(Heading*Speed);
    const FTransform Pose=MCActiveSkillMotion::RidePose(Hero(),Heading,FMath::Clamp(Param(TEXT("DeckHeight"),45.2f),0.f,100.f));
    SetActorLocationAndRotation(Pose.GetLocation(),Pose.GetRotation());
    UpdateRideVFX();
}
void AMCSpongeSkillAction::UpdateRideVFX()
{
    if(!Hero() || !LoopComponent) return;
    LoopComponent->SetWorldLocation(MCActiveSkillMotion::FootPoint(Hero())+FVector(0,0,3));
    UpdateLoopVFX(GetActorForwardVector().GetSafeNormal2D());
}
void AMCSpongeSkillAction::OnFinished(EMCActiveSkillEndReason Reason)
{
    Motion.End();
    if(HasAuthority() && Reason!=EMCActiveSkillEndReason::WorldEnded)
        PlaySkillCue(TEXT("End"),Hero()?MCActiveSkillMotion::FootPoint(Hero())+FVector(0,0,3):GetActorLocation(),Heading);
    Super::OnFinished(Reason);
}
