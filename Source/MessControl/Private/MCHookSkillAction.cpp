#include "MCHookSkillAction.h"
#include "MCActiveSkillAimPreview.h"
#include "MCBossCharacter.h"
#include "MCNutBoss.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCExpressionComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCHook,Log,All);

namespace
{
bool LiveBoss(const AActor* Target)
{
    if(!IsValid(Target) || Target->IsActorBeingDestroyed() || Target->ActorHasTag(TEXT("MC_VFXLabRuntime"))) return false;
    if(const auto* Boss=Cast<AMCNutBoss>(Target)) return Boss->CanReceiveToolHit();
    if(const auto* Boss=Cast<AMCBossCharacter>(Target))
        return Boss->CanReceiveWeaponHit() && Boss->Runtime.State!=EMCBossState::Dormant;
    return false;
}
float Ease(float A) { A=FMath::Clamp(A,0.f,1.f);return A*A*(3-2*A); }
FTransform Mix(const FTransform& A,const FTransform& B,float Alpha)
{ FTransform Result;Result.Blend(A,B,Ease(Alpha));return Result; }
const FVector HookShaft(-.065153115,.582679950,.810085889);
const FVector HookPlaneNormal(.736729221,-.519489879,.432851384);
const FVector HookTetherTail(1.6512997,1.4646528,-9.8951995);
const FVector HookWindupOffset(-10,8,2);

FVector HookGrip(FVector HeldOffset)
{
    // Old row defaults were an approximate mount above the measured artist wrist.
    return FVector(-1.464954,53.611076,-15.103604)+(HeldOffset-FVector(45,35,25));
}

float EffectiveHookRange(float Range) {return FMath::Clamp(Range,150.f,3000.f);}

float HookPreviewParameter(const TMap<FName,float>& Values,FName Key,float Fallback)
{
    const float* Value=Values.Find(Key);
    return Value && FMath::IsFinite(*Value) ? *Value : Fallback;
}

FTransform ArtistHookWrist()
{
    // Runtime FBX wrist axes independently checked against native UE ref pose.
    return FTransform(FQuat(.551196337,-.136977211,-.820045054,.070327856).GetNormalized(),
        FVector(-4.57078743,-3.71570659,3.31188440));
}
}
AMCHookSkillAction::AMCHookSkillAction()
{
    PrimaryActorTick.TickGroup=TG_PostPhysics;
    PropAsset=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/Props/SM_Buster_Hook.SM_Buster_Hook"));
    // Measured from the normalized source mesh's straight handle segment.
    // The asset shaft is NOT +X. Put it along the flight/tether axis and keep
    // its measured bend plane vertical (curl toward +Z), without random roll.
    PropTransform.SetRotation(FRotationMatrix::MakeFromXY(HookShaft,HookPlaneNormal).ToQuat().Inverse());
    StartVFX=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Hook_Start.NS_Hook_Start"));
    LoopVFX=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Hook_Loop.NS_Hook_Loop"));
    ImpactVFX=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Hook_Impact.NS_Hook_Impact"));
    EndVFX=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Hook_End.NS_Hook_End"));
}
UMCHookSkillEffect::UMCHookSkillEffect() {ActionClass=AMCHookSkillAction::StaticClass();}

namespace
{
bool TraceHookFlight(const AMCToothCharacter& Caster,const AActor* Action,FVector From,FVector To,FHitResult& Hit)
{
    const UWorld* World=Caster.GetWorld();
    if(!World) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCActiveHookFlight),false,&Caster);
    if(Action) Query.AddIgnoredActor(Action);
    bool Found=World->LineTraceSingleByChannel(Hit,From,To,ECC_Visibility,Query);
    double Nearest=Found?Hit.Distance:FVector::Distance(From,To)+1.;
    // Actual component traces include Visibility-ignoring pawns and boss torsos,
    // clipped by the nearest world blocker. Nothing is hooked through a wall.
    auto Consider=[&](AActor* Actor,UPrimitiveComponent* Shape) {
        if(Actor==&Caster || !Shape || !Shape->IsQueryCollisionEnabled()) return;
        FHitResult ShapeHit;
        if(Shape->LineTraceComponent(ShapeHit,From,To,Query) && ShapeHit.Distance<=Nearest+.1) {
            Hit=ShapeHit;Nearest=ShapeHit.Distance;Found=true;
        }
    };
    for(TActorIterator<AMCToothCharacter> It(World);It;++It)
        Consider(*It,It->GetCapsuleComponent()->IsQueryCollisionEnabled()?static_cast<UPrimitiveComponent*>(It->GetCapsuleComponent()):static_cast<UPrimitiveComponent*>(It->GetMesh()));
    for(TActorIterator<AMCBossCharacter> It(World);It;++It) Consider(*It,It->BodyHitbox);
    for(TActorIterator<AMCNutBoss> It(World);It;++It) Consider(*It,It->Body);
    return Found;
}

FVector HookAimPoint(const AMCToothCharacter& Caster,const AActor* Action,FVector View,FVector Look,float Range)
{
    const FVector End=View+Look*Range;
    FHitResult Hit;
    return TraceHookFlight(Caster,Action,View,End,Hit) ? FVector(Hit.ImpactPoint) : End;
}

FVector HookLaunchDirection(const AMCToothCharacter& Caster,FVector Target,FVector Origin)
{
    const FVector Direction=(Target-Origin).GetSafeNormal();
    return Direction.IsNearlyZero() ? Caster.GetActorForwardVector() : Direction;
}

bool ReadHookPull(const AMCToothCharacter& Caster,const AActor* Action,const FHitResult& Hit,float PullSpeed,
    bool bPreview,bool& bPullCaster,FVector& Goal,FVector& Velocity)
{
    const AActor* Target=Hit.GetActor();
    if(!IsValid(Target) || Target==&Caster || Target->GetWorld()!=Caster.GetWorld() || Target->ActorHasTag(TEXT("MC_VFXLabRuntime"))) return false;
    const AMCToothCharacter* Moved=nullptr;
    bool bRemoteAllyPreview=false;
    if(const auto* Ally=Cast<AMCToothCharacter>(Target)) {
        const auto* Move=Ally->GetCharacterMovement();
        if(!Move || Move->MovementMode==MOVE_Flying || IsValid(Ally->GetActiveSkillPresentation())) return false;
        // Other players' controllers and floor state are not present on this client.
        bRemoteAllyPreview=bPreview && !Caster.HasAuthority() && !Ally->GetController();
        if(bRemoteAllyPreview) {
            if(!Ally->CanWork() || Ally->HasFrozenLegs() || Move->MovementMode==MOVE_None) return false;
        } else if(!MCActiveSkillMotion::IsEligible(Ally,false)) return false;
        Moved=Ally;
    } else if(LiveBoss(Target)) Moved=&Caster;
    if(!MCActiveSkillMotion::CanAcquire(Moved)) return false;

    bPullCaster=Moved==&Caster;
    Goal=bPullCaster ? FVector(Hit.ImpactPoint) : Caster.GetActorLocation();
    Velocity=(Goal-Moved->GetActorLocation()).GetSafeNormal()*FMath::Clamp(PullSpeed,100.f,1800.f);
    return bRemoteAllyPreview || MCActiveSkillMotion::ClearPullStep(Moved,Action,bPullCaster ? Target : &Caster,Velocity,.033f);
}
}

bool AMCHookSkillAction::TraceFlight(FVector From,FVector To,FHitResult& Hit) const
{
    const auto* Caster=Hero();
    return Caster && TraceHookFlight(*Caster,this,From,To,Hit);
}

bool AMCHookSkillAction::BuildAimPreview(const AMCToothCharacter& Caster,const TMap<FName,float>& PreviewParameters,FMCActiveSkillAimPreview& Out) const
{
    Out=FMCActiveSkillAimPreview();
    FVector View,Look;
    if(!Caster.GetWorld() || !ReadAim(Caster,View,Look)) return false;

    const AActor* Action=IsTemplate() ? nullptr : this;
    const float Range=EffectiveHookRange(HookPreviewParameter(PreviewParameters,TEXT("HookRange"),1400.f));
    const FVector Target=HookAimPoint(Caster,Action,View,Look,Range);
    const FVector HeldOffset(HookPreviewParameter(PreviewParameters,TEXT("HeldX"),45.f),
        HookPreviewParameter(PreviewParameters,TEXT("HeldY"),35.f),HookPreviewParameter(PreviewParameters,TEXT("HeldZ"),25.f));
    // Match the release wrist at the end of windup, assuming the current caster pose.
    const FTransform CasterPose(Caster.GetActorRotation(),Caster.GetActorLocation());
    const FVector Origin=CasterPose.TransformPosition(HookGrip(HeldOffset)+HookWindupOffset);
    const FVector End=Origin+HookLaunchDirection(Caster,Target,Origin)*Range;
    FHitResult Hit;
    const bool bContact=TraceHookFlight(Caster,Action,Origin,End,Hit);
    Out.Path={Origin,bContact ? FVector(Hit.ImpactPoint) : End};
    if(bContact) {
        Out.bHasContact=true;
        Out.ContactPoint=Hit.ImpactPoint;
        Out.ContactNormal=Hit.ImpactNormal;
        bool bPullCaster=false;
        FVector Goal,Velocity;
        const float PullSpeed=HookPreviewParameter(PreviewParameters,TEXT("PullSpeed"),850.f);
        if(ReadHookPull(Caster,Action,Hit,PullSpeed,true,bPullCaster,Goal,Velocity))
            Out.State=bPullCaster ? EMCActiveSkillAimState::Boss : EMCActiveSkillAimState::Ally;
        else Out.State=EMCActiveSkillAimState::Blocked;
    }
    return true;
}
bool AMCHookSkillAction::CanStart() const
{
    FVector Origin,Direction;
    return Super::CanStart() && MCActiveSkillMotion::IsEligible(Hero(),false)
        && Hero()->GetCharacterMovement()->MovementMode!=MOVE_Flying && GetAim(Origin,Direction);
}
float AMCHookSkillAction::GetPhaseAlpha() const
{ return PhaseDuration>UE_SMALL_NUMBER?FMath::Clamp(float((Now()-PhaseStartedAt)/PhaseDuration),0.f,1.f):0.f; }

FTransform AMCHookSkillAction::HandLocal(bool Left) const
{
    const FTransform& From=Left?PhaseLeftHand:PhaseRightHand;
    const FTransform& Rest=Left?RestLeftHand:RestRightHand;
    const float A=GetPhaseAlpha();
    FTransform Goal=Rest;
    const FVector Grip=HookGrip(FVector(Param(TEXT("HeldX"),45),Param(TEXT("HeldY"),35),Param(TEXT("HeldZ"),25)));
    const FQuat GripRotation=Left?Rest.GetRotation():ArtistHookWrist().GetRotation();
    if(CastPhase==EMCHookCastPhase::Inactive) return Rest;
    if(CastPhase==EMCHookCastPhase::Windup) {
        Goal=FTransform(FRotator(-8,Left?5:-15,Left?-5:-12),Left?FVector(5,-57,-17):Grip+HookWindupOffset);
        Goal.SetRotation((Goal.GetRotation()*GripRotation).GetNormalized());
        return Mix(From,Goal,A);
    }
    if(CastPhase==EMCHookCastPhase::Outbound) {
        // The prop leaves the hand. Only the mitten follows this short throw arc.
        const float Age=FMath::Max(0.f,float(Now()-ReleasedAt));
        FTransform Extended(FRotator(Left?-4:12,Left?0:8,Left?-6:18),Left?FVector(15,-55,-10):Grip+FVector(28,-2,12));
        Extended.SetRotation((Extended.GetRotation()*GripRotation).GetNormalized());
        if(Age<.10f) return Mix(From,Extended,Age/.10f);
        Goal=FTransform(FRotator(-3,0,Left?-5:8),Left?FVector(10,-53,-16):Grip+FVector(14,0,5));
        Goal.SetRotation((Goal.GetRotation()*GripRotation).GetNormalized());
        return Mix(Extended,Goal,(Age-.10f)/.18f);
    }
    if(CastPhase==EMCHookCastPhase::Pulling) {
        const float Work=FMath::Sin(float(Now()-PhaseStartedAt)*12.f)*2.f;
        Goal=FTransform(FRotator(-8,Left?-7:8,Left?-8:10),Left?FVector(12,-48,-17):Grip+FVector(8+Work,0,2));
        Goal.SetRotation((Goal.GetRotation()*GripRotation).GetNormalized());
        return Mix(From,Goal,FMath::Min(1.f,float(Now()-PhaseStartedAt)/.12f));
    }
    if(CastPhase==EMCHookCastPhase::Returning) {
        Goal=FTransform(FRotator(0,Left?-5:3,Left?-5:6),Left?FVector(4,-52,-18):Grip+FVector(3,0,0));
        Goal.SetRotation((Goal.GetRotation()*GripRotation).GetNormalized());
        return Mix(From,Goal,A);
    }
    return Mix(From,Rest,A);
}
FTransform AMCHookSkillAction::GetCasterHandFrame(bool Left) const
{ return Hero()?HandLocal(Left)*FTransform(CastFacing,Hero()->GetActorLocation()):FTransform::Identity; }
FTransform AMCHookSkillAction::GetCastBodyOffset() const
{
    FTransform Goal=FTransform::Identity;
    const float A=GetPhaseAlpha();float Blend=A;
    if(CastPhase==EMCHookCastPhase::Windup) Goal=FTransform(FRotator(-5,-12,-4),FVector(-3,0,-3));
    else if(CastPhase==EMCHookCastPhase::Outbound) {
        const float Age=FMath::Max(0.f,float(Now()-ReleasedAt));
        const FTransform Throw(FRotator(9,9,3),FVector(3,0,-2));
        if(Age<.10f) return Mix(PhaseBody,Throw,Age/.10f);
        return Mix(Throw,FTransform::Identity,(Age-.10f)/.20f);
    } else if(CastPhase==EMCHookCastPhase::Pulling) {
        Goal=FTransform(FRotator(-8,-4,-2),FVector(-3,0,-3));Blend=FMath::Min(1.f,float(Now()-PhaseStartedAt)/.12f);
    } else if(CastPhase==EMCHookCastPhase::Returning) Goal=FTransform(FRotator(-3,0,-1),FVector(-1,0,-1));
    return Mix(PhaseBody,Goal,Blend);
}
void AMCHookSkillAction::SetCastPhase(EMCHookCastPhase Phase,float Duration)
{
    const FTransform Right=HandLocal(false),Left=HandLocal(true),Body=GetCastBodyOffset();
    PhaseVisualRotation=PropMesh?PropMesh->GetComponentQuat():GetActorQuat();
    PhaseRightHand=Right;PhaseLeftHand=Left;PhaseBody=Body;
    CastPhase=Phase;PhaseStartedAt=Now();PhaseDuration=Duration;ForceNetUpdate();
}
void AMCHookSkillAction::StartCommitted()
{
    Super::StartCommitted();
    // Camera orbit after a throw may reveal the face, but cannot retarget the
    // accepted cast or drag the glove around the body.
    CastFacing=Hero()->GetActorRotation();
    auto ReadHand=[&](bool Left) {
        const FName Bone=Hero()->RigBone(Left?TEXT("hand_l"):TEXT("hand_r"));
        const auto* Mesh=Hero()->GetMesh();
        FTransform Pose=Mesh->DoesSocketExist(Bone)?Mesh->GetSocketTransform(Bone).GetRelativeTransform(Hero()->GetActorTransform())
            :FTransform(FVector(35,Left?-35:35,15));
        Pose.SetScale3D(FVector::OneVector);return Pose;
    };
    RestRightHand=ReadHand(false);RestLeftHand=ReadHand(true);
    FVector Origin,Direction;GetAim(Origin,Direction);
    const float Range=EffectiveHookRange(Param(TEXT("HookRange"),1400.f));
    AimPoint=HookAimPoint(*Hero(),this,Origin,Direction,Range);
    SetCastPhase(EMCHookCastPhase::Windup,FMath::Clamp(Param(TEXT("WindupSeconds"),.16f),.08f,.4f));
    UpdatePresentation();
}
void AMCHookSkillAction::Launch()
{
    FlightOrigin=GetTetherStart();FlightDirection=HookLaunchDirection(*Hero(),AimPoint,FlightOrigin);
    ReleasedAt=Now();ImpactPoint=FlightOrigin;
    const float Range=EffectiveHookRange(Param(TEXT("HookRange"),1400.f));
    const float Speed=FMath::Clamp(Param(TEXT("FlightSpeed"),2200.f),600.f,5000.f);
    SetCastPhase(EMCHookCastPhase::Outbound,Range/Speed);
    SetActorLocationAndRotation(FlightOrigin,FlightDirection.Rotation());
    PlaySkillCue(TEXT("Start"),FlightOrigin,FlightDirection);StartLoop();
}
FVector AMCHookSkillAction::GetTetherStart() const { return GetCasterHandFrame().GetLocation(); }
FVector AMCHookSkillAction::GetHookHeadLocation() const
{
    if(CastPhase==EMCHookCastPhase::Outbound) {
        const double Distance=FMath::Clamp(double(Now()-ReleasedAt)*FMath::Clamp(Param(TEXT("FlightSpeed"),2200.f),600.f,5000.f),0.,double(EffectiveHookRange(Param(TEXT("HookRange"),1400.f))));
        return FlightOrigin+FlightDirection*Distance;
    }
    if(CastPhase==EMCHookCastPhase::Pulling && IsValid(HookTarget)) return HookTarget->GetActorTransform().TransformPosition(TargetLocalContact);
    if(CastPhase==EMCHookCastPhase::Returning) return FMath::Lerp(ImpactPoint,GetTetherStart(),double(Ease(GetPhaseAlpha())));
    return GetTetherStart();
}
FVector AMCHookSkillAction::GetTetherEnd() const { return GetHookHeadLocation(); }
FVector AMCHookSkillAction::GetTetherAttachmentLocation() const
{ return PropMesh?PropMesh->GetComponentTransform().TransformPosition(HookTetherTail):GetHookHeadLocation(); }
FVector AMCHookSkillAction::GetHookShaftWorldDirection() const
{ return PropMesh?PropMesh->GetComponentTransform().TransformVectorNoScale(HookShaft).GetSafeNormal():FlightDirection; }
FVector AMCHookSkillAction::GetHookCurlWorldDirection() const
{ return PropMesh?PropMesh->GetComponentTransform().TransformVectorNoScale(FVector::CrossProduct(HookShaft,HookPlaneNormal)).GetSafeNormal():FVector::UpVector; }
bool AMCHookSkillAction::BeginContact(const FHitResult& Hit)
{
    auto* Caster=Hero();
    bool bPullCaster=false;
    FVector Goal,Velocity;
    if(!Caster || !ReadHookPull(*Caster,this,Hit,Param(TEXT("PullSpeed"),850.f),false,bPullCaster,Goal,Velocity)) return false;
    AActor* Target=Hit.GetActor();
    auto* Moved=bPullCaster ? Caster : Cast<AMCToothCharacter>(Target);
    const float Seconds=FMath::Clamp(Param(TEXT("PullSeconds"),2.5f),.1f,8.f);
    const auto* Move=Moved->GetCharacterMovement();
    const bool Grounded=Move->IsMovingOnGround() && FMath::Abs(Goal.Z-Moved->GetActorLocation().Z)<=Move->MaxStepHeight;
    if(!Motion.Begin(Moved,GetActionID(),Velocity,!Grounded,Seconds)) return false;
    HookTarget=Target;PulledPawn=Moved;TargetLocalContact=Target->GetActorTransform().InverseTransformPosition(Hit.ImpactPoint);
    bContactedTarget=true;ImpactPoint=Hit.ImpactPoint;PreviousMovedPosition=Moved->GetActorLocation();LastSampleAt=Now();EndsAt=Now()+Seconds;
    SetCastPhase(EMCHookCastPhase::Pulling,Seconds);
    PlaySkillCue(TEXT("Impact"),Hit.ImpactPoint,-FlightDirection);
    if(Hero()->Expression) Hero()->Expression->PlayHookReaction(GetActionID(),EMCHookFaceReaction::Success);
    if(Moved!=Hero() && Moved->Expression) Moved->Expression->PlayHookReaction(GetActionID(),EMCHookFaceReaction::Caught);
    return true;
}
void AMCHookSkillAction::BeginReturn(EMCActiveSkillEndReason Reason)
{
    // A pulled ally may have moved since the previous prop tick. Start the
    // return at its current contact, while an outbound world hit uses the
    // swept blocking point rather than the analytically extrapolated head.
    ImpactPoint=CastPhase==EMCHookCastPhase::Pulling && IsValid(HookTarget)?GetHookHeadLocation():GetActorLocation();
    Motion.End();ReturnReason=Reason;
    if(!bContactedTarget && Hero()->Expression) Hero()->Expression->PlayHookReaction(GetActionID(),EMCHookFaceReaction::Miss);
    const float Seconds=FMath::Clamp(float(FVector::Distance(ImpactPoint,GetTetherStart())/3200.),.18f,.45f);
    SetCastPhase(EMCHookCastPhase::Returning,Seconds);
}
void AMCHookSkillAction::StopPull(FName Gate,const FHitResult* Hit)
{
    PullStopReason=Gate;
    UE_LOG(LogMCHook,Log,TEXT("MC_HOOK_PULL_STOP action=%s gate=%s phase=%d pawn=%s actor=%s component=%s penetrating=%d depth=%.4f normal=%s impact=%s location=%s"),
        *GetActionID().ToString(),*Gate.ToString(),int32(CastPhase),*GetPathNameSafe(PulledPawn),
        Hit?*GetPathNameSafe(Hit->GetActor()):TEXT("None"),Hit?*GetPathNameSafe(Hit->GetComponent()):TEXT("None"),
        Hit?int32(Hit->bStartPenetrating):0,Hit?Hit->PenetrationDepth:0.f,Hit?*Hit->ImpactNormal.ToString():TEXT("None"),
        Hit?*Hit->ImpactPoint.ToString():TEXT("None"),Hit?*Hit->Location.ToString():TEXT("None"));
    BeginReturn(EMCActiveSkillEndReason::InputCanceled);
}
bool AMCHookSkillAction::TargetStillValid() const
{
    if(!IsValid(HookTarget) || HookTarget->GetWorld()!=GetWorld() || HookTarget->ActorHasTag(TEXT("MC_VFXLabRuntime")) || !IsValid(PulledPawn)
        || PulledPawn->GetWorld()!=GetWorld() || !MCActiveSkillMotion::IsEligible(Hero(),false)
        || !MCActiveSkillMotion::IsEligible(PulledPawn,false)) return false;
    if(HookTarget==PulledPawn) return HookTarget!=Hero() && !IsValid(PulledPawn->GetActiveSkillPresentation());
    return PulledPawn==Hero() && LiveBoss(HookTarget);
}
void AMCHookSkillAction::UpdatePresentation()
{
    const FVector Start=GetTetherStart(),End=GetTetherEnd();
    const FQuat HandIntent=(HandLocal(false).GetRotation()*RestRightHand.GetRotation().Inverse()).GetNormalized();
    const FVector Direction=CastPhase==EMCHookCastPhase::Windup || CastPhase==EMCHookCastPhase::Recovery
        ?CastFacing.Quaternion().RotateVector(HandIntent.GetForwardVector()):CastPhase==EMCHookCastPhase::Outbound
        ?FlightDirection:(End-Start).GetSafeNormal();
    SetActorLocationAndRotation(End,Direction.IsNearlyZero()?FlightDirection.Rotation():Direction.Rotation());
    if(PropMesh) {
        const FTransform Grip=ArtistHookWrist();
        const FTransform Wrist=GetCasterHandFrame();
        const FTransform SourceUpright(CastFacing.Quaternion(),Wrist.GetLocation()-CastFacing.Quaternion().RotateVector(Grip.GetLocation()));
        FTransform Visual=PropTransform*GetActorTransform();
        if(CastPhase==EMCHookCastPhase::Windup) {
            // Original asset axes are correct for the artist's upright HELD
            // hook. Only the airborne/tension head needs the flight correction.
            const FTransform Contact=Grip.Inverse()*Wrist;
            Visual.Blend(SourceUpright,Contact,Ease(GetPhaseAlpha()));
        } else if(CastPhase==EMCHookCastPhase::Outbound) {
            Visual.SetRotation(FQuat::Slerp(PhaseVisualRotation,Visual.GetRotation(),
                Ease(float(Now()-ReleasedAt)/.08f)).GetNormalized());
        } else if(CastPhase==EMCHookCastPhase::Recovery) {
            Visual.Blend(FTransform(PhaseVisualRotation,End),SourceUpright,Ease(GetPhaseAlpha()));
        }
        PropMesh->SetWorldTransform(Visual);
    }
    if(LoopComponent) {
        LoopComponent->SetVariablePosition(TEXT("User.TetherStart"),Start);
        LoopComponent->SetVariablePosition(TEXT("User.TetherEnd"),GetTetherAttachmentLocation());
    }
}
void AMCHookSkillAction::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);if(IsFinished() || !IsCommitted() || !Hero()) return;
    if(!HasAuthority()) {UpdatePresentation();return;}
    if(!MCActiveSkillMotion::IsEligible(Hero(),false)) {Finish(EMCActiveSkillEndReason::InputCanceled);return;}
    if(CastPhase==EMCHookCastPhase::Windup) {
        if(GetPhaseAlpha()>=1) Launch();
    } else if(CastPhase==EMCHookCastPhase::Outbound) {
        const FVector Next=GetHookHeadLocation();FHitResult Contact;
        if(TraceFlight(GetActorLocation(),Next,Contact)) {
            SetActorLocation(Contact.ImpactPoint);
            if(!BeginContact(Contact)) BeginReturn(EMCActiveSkillEndReason::Completed);
        } else if(GetPhaseAlpha()>=1) {SetActorLocation(Next);BeginReturn(EMCActiveSkillEndReason::Completed);}
    } else if(CastPhase==EMCHookCastPhase::Pulling) {
        const FVector Start=GetTetherStart(),End=GetTetherEnd();
        if(!TargetStillValid()) {StopPull(TEXT("TargetUnavailable"));UpdatePresentation();return;}
        if(!Motion.IsOwned()) {StopPull(TEXT("MotionLost"));UpdatePresentation();return;}
        if(FVector::DistSquared(Start,End)>FMath::Square(FMath::Clamp(Param(TEXT("BreakRange"),1800.f),150.f,4000.f))) {StopPull(TEXT("BreakRange"));UpdatePresentation();return;}
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MCActiveHookTether),false,Hero());Query.AddIgnoredActor(this);Query.AddIgnoredActor(HookTarget);
        FHitResult Wall;
        if(GetWorld()->LineTraceSingleByChannel(Wall,Start,End,ECC_Visibility,Query)) {StopPull(TEXT("TetherBlocked"),&Wall);UpdatePresentation();return;}
        const float Seconds=float(Now()-LastSampleAt),Speed=FMath::Clamp(Param(TEXT("PullSpeed"),850.f),100.f,1800.f);
        if(Seconds>0 && !MCActiveSkillMotion::ContinuousPath(PreviousMovedPosition,PulledPawn->GetActorLocation(),Seconds,Speed)) {StopPull(TEXT("DiscontinuousMotion"));UpdatePresentation();return;}
        PreviousMovedPosition=PulledPawn->GetActorLocation();LastSampleAt=Now();
        const FVector Goal=PulledPawn==Hero()?End:Hero()->GetActorLocation(),Delta=Goal-PulledPawn->GetActorLocation();
        const float Stop=FMath::Clamp(Param(TEXT("StopDistance"),120.f),60.f,300.f);
        if(Delta.Size()<=Stop || Now()>=EndsAt) {
            PullStopReason=Now()>=EndsAt?TEXT("PullDeadline"):TEXT("ReachedStopDistance");
            BeginReturn(Now()>=EndsAt?EMCActiveSkillEndReason::TimedOut:EMCActiveSkillEndReason::Completed);
        }
        else {
            const FVector Velocity=Delta.GetSafeNormal()*FMath::Min(Speed,float((Delta.Size()-Stop)/FMath::Max(.01f,DeltaSeconds)));
            FHitResult Blocker;
            if(!MCActiveSkillMotion::ClearPullStep(PulledPawn,this,PulledPawn==Hero()?HookTarget.Get():Hero(),Velocity,DeltaSeconds,&Blocker)) {
                StopPull(TEXT("PullStepBlocked"),&Blocker);UpdatePresentation();return;
            }
            if(!Motion.Update(Velocity)) {StopPull(TEXT("MotionUpdateLost"));UpdatePresentation();return;}
        }
    } else if(CastPhase==EMCHookCastPhase::Returning && GetPhaseAlpha()>=1) {
        StopLoop();PlaySkillCue(TEXT("End"),GetTetherStart(),FVector::UpVector);
        SetCastPhase(EMCHookCastPhase::Recovery,.18f);
    } else if(CastPhase==EMCHookCastPhase::Recovery && GetPhaseAlpha()>=1) {Finish(ReturnReason);return;}
    UpdatePresentation();
}
void AMCHookSkillAction::OnFinished(EMCActiveSkillEndReason Reason)
{
    Motion.End();
    const bool Returned=CastPhase==EMCHookCastPhase::Recovery && GetPhaseAlpha()>=1
        && (Reason==EMCActiveSkillEndReason::InputCanceled || Reason==EMCActiveSkillEndReason::Completed || Reason==EMCActiveSkillEndReason::TimedOut);
    if(HasAuthority() && !Returned && Reason!=EMCActiveSkillEndReason::Completed && Reason!=EMCActiveSkillEndReason::TimedOut) {
        if(Hero() && Hero()->Expression) Hero()->Expression->StopHookReaction(GetActionID());
        if(IsValid(PulledPawn) && PulledPawn!=Hero() && PulledPawn->Expression) PulledPawn->Expression->StopHookReaction(GetActionID());
    }
    Super::OnFinished(Reason);
}
void AMCHookSkillAction::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCHookSkillAction,HookTarget);DOREPLIFETIME(AMCHookSkillAction,PulledPawn);DOREPLIFETIME(AMCHookSkillAction,TargetLocalContact);
    DOREPLIFETIME(AMCHookSkillAction,CastPhase);DOREPLIFETIME(AMCHookSkillAction,PhaseStartedAt);DOREPLIFETIME(AMCHookSkillAction,PhaseDuration);
    DOREPLIFETIME(AMCHookSkillAction,ReleasedAt);DOREPLIFETIME(AMCHookSkillAction,FlightOrigin);DOREPLIFETIME(AMCHookSkillAction,FlightDirection);
    DOREPLIFETIME(AMCHookSkillAction,AimPoint);DOREPLIFETIME(AMCHookSkillAction,CastFacing);DOREPLIFETIME(AMCHookSkillAction,ImpactPoint);DOREPLIFETIME(AMCHookSkillAction,bContactedTarget);
    DOREPLIFETIME(AMCHookSkillAction,PhaseRightHand);DOREPLIFETIME(AMCHookSkillAction,PhaseLeftHand);DOREPLIFETIME(AMCHookSkillAction,PhaseBody);
    DOREPLIFETIME(AMCHookSkillAction,RestRightHand);DOREPLIFETIME(AMCHookSkillAction,RestLeftHand);
    DOREPLIFETIME(AMCHookSkillAction,PhaseVisualRotation);
    DOREPLIFETIME(AMCHookSkillAction,PullStopReason);
}
