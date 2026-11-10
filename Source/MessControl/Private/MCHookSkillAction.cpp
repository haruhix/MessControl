#include "MCHookSkillAction.h"
#include "MCActiveSkillAimPreview.h"
#include "MCBossCharacter.h"
#include "MCNutBoss.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothStatusComponent.h"
#include "MCExpressionComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
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
constexpr float HookCockPhase=.69f;
constexpr float HookWindupSeconds=.28f;
constexpr float HookRangeScale=3.f;
constexpr float HookFlightSpeedScale=2.f;
constexpr float HookPullSpeedScale=.5f;
constexpr float DefaultHookFlightSpeed=2200.f;
constexpr float DefaultHookStopDistance=120.f;
constexpr double HookAssistMinDot=.99756405; // 4 degrees from the reticle.

FVector HookGrip(FVector HeldOffset)
{
    // Old row defaults were an approximate mount above the measured artist wrist.
    return FVector(-1.464954,53.611076,-15.103604)+(HeldOffset-FVector(45,35,25));
}

float AuthoredHookRange(float Range) {return FMath::Clamp(Range,150.f,3000.f);}
float EffectiveHookRange(float Range) {return HookRangeScale*AuthoredHookRange(Range);}
float EffectiveHookFlightSpeed(float AuthoredSpeed)
{
    return FMath::Clamp(HookFlightSpeedScale*AuthoredSpeed,HookFlightSpeedScale*600.f,MCHookPullPrediction::MaxSpeed);
}
float EffectiveHookPullSpeed(float AuthoredSpeed) {return HookPullSpeedScale*EffectiveHookFlightSpeed(AuthoredSpeed);}
float HookStopDistance(float AuthoredDistance) {return FMath::Clamp(AuthoredDistance,60.f,300.f);}

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

FVector PreviewHeldOffset(const TMap<FName,float>& Parameters)
{
    return FVector(HookPreviewParameter(Parameters,TEXT("HeldX"),45.f),
        HookPreviewParameter(Parameters,TEXT("HeldY"),35.f),HookPreviewParameter(Parameters,TEXT("HeldZ"),25.f));
}

FTransform HookRightWrist(FVector HeldOffset,FVector Offset,FRotator Turn=FRotator::ZeroRotator)
{
    return FTransform((Turn.Quaternion()*ArtistHookWrist().GetRotation()).GetNormalized(),HookGrip(HeldOffset)+Offset);
}

FTransform HookReadyWrist(FVector HeldOffset)
{
    return HookRightWrist(HeldOffset,FVector(10,9,10));
}

FTransform HookCockedWrist(FVector HeldOffset)
{
    return HookRightWrist(HeldOffset,FVector(-34,10,35),FRotator(-24,-35,-20));
}

FTransform HookReleaseWrist(FVector HeldOffset)
{
    return HookRightWrist(HeldOffset,FVector(40,6,21),FRotator(-12,15,25));
}

FTransform HookPropAtWrist(const FTransform& Wrist)
{
    return ArtistHookWrist().Inverse()*Wrist;
}

FRotator HookAimFacing(const FRotator& ActorRotation,const FVector& Look)
{
    return Look.SizeSquared2D()>UE_KINDA_SMALL_NUMBER?FRotator(0,Look.Rotation().Yaw,0):ActorRotation;
}

FQuat HookPoseFacing(FRotator ActorRotation,const FRotator& AimRotation,float Weight)
{
    // The crown and wrists turn together. The runner keeps steering and
    // the torso has a bounded twist even when the reticle is behind it.
    const double Turn=FMath::FindDeltaAngleDegrees(ActorRotation.Yaw,AimRotation.Yaw);
    const double TwistWeight=1.-FMath::SmoothStep(135.,180.,FMath::Abs(Turn));
    ActorRotation.Yaw+=FMath::Clamp(Turn,-95.,95.)*Weight*TwistWeight;
    return ActorRotation.Quaternion();
}

FTransform HookBodyPose(FRotator Rotation,FVector Translation,double Height)
{
    const double Width=1./FMath::Sqrt(Height);
    return FTransform(Rotation,Translation,FVector(Width,Width,Height));
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
UPrimitiveComponent* HookPlayerShape(const AMCToothCharacter& Player)
{
    auto* Capsule=Player.GetCapsuleComponent();
    return Capsule->IsQueryCollisionEnabled()?static_cast<UPrimitiveComponent*>(Capsule):Player.GetMesh();
}

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
        if(Shape->LineTraceComponent(ShapeHit,From,To,Query) && ShapeHit.Distance<Nearest) {
            Hit=ShapeHit;Nearest=ShapeHit.Distance;Found=true;
        }
    };
    for(TActorIterator<AMCToothCharacter> It(World);It;++It)
        Consider(*It,HookPlayerShape(**It));
    for(TActorIterator<AMCBossCharacter> It(World);It;++It) Consider(*It,It->BodyHitbox);
    for(TActorIterator<AMCNutBoss> It(World);It;++It) Consider(*It,It->Body);
    return Found;
}

FVector HookAimPoint(const AMCToothCharacter& Caster,const AActor* Action,FVector View,FVector Look,FVector ReleaseOrigin,float Range)
{
    // The shoulder camera is behind and to the side of the release hand.
    // Clip its reticle ray to the forward half of the hand's actual reach.
    const FVector ToRelease=ReleaseOrigin-View;
    const double ReleaseDepth=FVector::DotProduct(ToRelease,Look);
    const double ReachSquared=FMath::Square(double(Range))-(ToRelease-Look*ReleaseDepth).SizeSquared();
    const double StartDepth=FMath::Max(0.,ReleaseDepth+1.);
    const double EndDepth=ReleaseDepth+FMath::Sqrt(FMath::Max(0.,ReachSquared));
    if(EndDepth<=StartDepth) return ReleaseOrigin+Look*Range;
    const FVector Start=View+Look*StartDepth;
    const FVector End=View+Look*EndDepth;
    FHitResult Hit;
    return TraceHookFlight(Caster,Action,Start,End,Hit) ? FVector(Hit.ImpactPoint) : End;
}

FVector HookLaunchDirection(FVector Target,FVector Origin,FVector Look)
{
    const FVector ToTarget=Target-Origin;
    // A near obstacle can intercept the camera ray behind the release hand.
    // The cast still starts at that hand and tests obstacles along its flight.
    return FVector::DotProduct(ToTarget,Look)>UE_SMALL_NUMBER
        ? ToTarget.GetSafeNormal(SMALL_NUMBER,Look) : Look;
}

bool ReadHookPull(const AMCToothCharacter& Caster,const AActor* Action,const FHitResult& Hit,float PullSpeed,float StopDistance,
    bool bPreview,bool& bPullCaster,FVector& Goal)
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
    constexpr float ProbeSeconds=.033f;
    const FVector Delta=Goal-Moved->GetActorLocation();
    const FVector Velocity=Delta.GetSafeNormal()*FMath::Min(PullSpeed*MCHookPullPrediction::PeakSpeedScale,
        FMath::Max(0.f,float(Delta.Size())-StopDistance)/ProbeSeconds);
    return bRemoteAllyPreview || MCActiveSkillMotion::ClearPullStep(Moved,Action,bPullCaster ? Target : &Caster,Velocity,ProbeSeconds);
}

bool ResolveHookShot(const AMCToothCharacter& Caster,const AActor* Action,FVector View,FVector Look,FVector Origin,
    float Range,float PullSpeed,float StopDistance,bool bPreview,FVector& Direction,FHitResult& Contact)
{
    Direction=HookLaunchDirection(HookAimPoint(Caster,Action,View,Look,Origin,Range),Origin,Look);
    const bool bDirectContact=TraceHookFlight(Caster,Action,Origin,Origin+Direction*Range,Contact);
    const AActor* DirectActor=Contact.GetActor();
    if(Cast<AMCToothCharacter>(DirectActor) || LiveBoss(DirectActor)) return bDirectContact;

    const double BlockerDepth=bDirectContact?FVector::DotProduct(Contact.ImpactPoint-View,Look):TNumericLimits<double>::Max();
    double BestDot=HookAssistMinDot;
    bool bAssisted=false;
    for(TActorIterator<AMCToothCharacter> It(Caster.GetWorld());It;++It) {
        const auto* Ally=*It;
        if(Ally==&Caster || Ally->IsActorBeingDestroyed() || (!Ally->GetPlayerState() && !Cast<APlayerController>(Ally->GetController()))) continue;
        const auto* Shape=HookPlayerShape(*Ally);
        if(!Shape || !Shape->IsQueryCollisionEnabled()) continue;
        const FVector Center=Shape->Bounds.Origin;
        const FVector FromView=Center-View,FromOrigin=Center-Origin;
        const double Alignment=FVector::DotProduct(FromView.GetSafeNormal(),Look);
        if(Alignment<=BestDot || FVector::DotProduct(FromOrigin,Look)<=0
            || FVector::DotProduct(FromView,Look)>=BlockerDepth
            || FromOrigin.SizeSquared()>FMath::Square(Range+Shape->Bounds.SphereRadius)) continue;

        FHitResult Seen,AssistedContact;
        const FVector AssistedDirection=FromOrigin.GetSafeNormal();
        if(!TraceHookFlight(Caster,Action,View,Center,Seen) || Seen.GetActor()!=Ally
            || !TraceHookFlight(Caster,Action,Origin,Origin+AssistedDirection*Range,AssistedContact)
            || AssistedContact.GetActor()!=Ally) continue;
        bool bPullCaster=false;
        FVector Goal;
        if(!ReadHookPull(Caster,Action,AssistedContact,PullSpeed,StopDistance,bPreview,bPullCaster,Goal)) continue;

        BestDot=Alignment;
        Direction=AssistedDirection;
        Contact=AssistedContact;
        bAssisted=true;
    }
    return bAssisted || bDirectContact;
}
}

bool AMCHookSkillAction::TraceFlight(FVector From,FVector To,FHitResult& Hit) const
{
    const auto* Caster=Hero();
    return Caster && TraceHookFlight(*Caster,this,From,To,Hit);
}

FVector AMCHookSkillAction::ReadHeldOffset() const
{
    return FVector(Param(TEXT("HeldX"),45.f),Param(TEXT("HeldY"),35.f),Param(TEXT("HeldZ"),25.f));
}

void AMCHookSkillAction::BuildReadyPose(const TMap<FName,float>& PreviewParameters,FTransform& OutRightHandLocal,FTransform& OutPropLocal) const
{
    OutRightHandLocal=HookReadyWrist(PreviewHeldOffset(PreviewParameters));
    OutPropLocal=HookPropAtWrist(OutRightHandLocal);
}

bool AMCHookSkillAction::BuildAimPreview(const AMCToothCharacter& Caster,const TMap<FName,float>& PreviewParameters,FMCActiveSkillAimPreview& Out) const
{
    Out=FMCActiveSkillAimPreview();
    FVector View,Look;
    if(!Caster.GetWorld() || !ReadAim(Caster,View,Look)) return false;

    const AActor* Action=IsTemplate() ? nullptr : this;
    const float Range=EffectiveHookRange(HookPreviewParameter(PreviewParameters,TEXT("HookRange"),1400.f));
    // Predict the same release grip the windup will reach from this heading.
    const FRotator ActorRotation=Caster.GetActorRotation();
    const FTransform CasterPose(HookPoseFacing(ActorRotation,HookAimFacing(ActorRotation,Look),1.f),Caster.GetActorLocation());
    const FTransform Release=HookPropAtWrist(HookReleaseWrist(PreviewHeldOffset(PreviewParameters)))*CasterPose;
    const FVector Origin=Release.GetLocation();
    const float PullSpeed=EffectiveHookPullSpeed(HookPreviewParameter(PreviewParameters,TEXT("FlightSpeed"),DefaultHookFlightSpeed));
    const float StopDistance=HookStopDistance(HookPreviewParameter(PreviewParameters,TEXT("StopDistance"),DefaultHookStopDistance));
    FVector Direction;
    FHitResult Hit;
    if(ResolveHookShot(Caster,Action,View,Look,Origin,Range,PullSpeed,StopDistance,true,Direction,Hit)) {
        Out.bHasContact=true;
        Out.ContactPoint=Hit.ImpactPoint;
        Out.ContactNormal=Hit.ImpactNormal;
        bool bPullCaster=false;
        FVector Goal;
        if(ReadHookPull(Caster,Action,Hit,PullSpeed,StopDistance,true,bPullCaster,Goal))
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
    const FVector HeldOffset=ReadHeldOffset();
    auto FreeHand=[&](FVector Position,FRotator Turn) {
        return FTransform((Turn.Quaternion()*Rest.GetRotation()).GetNormalized(),Position);
    };
    if(CastPhase==EMCHookCastPhase::Inactive) return Rest;
    if(CastPhase==EMCHookCastPhase::Windup) {
        const FTransform Cocked=Left ? FreeHand(FVector(15,-62,1),FRotator(-10,15,-13)) : HookCockedWrist(HeldOffset);
        const FTransform Release=Left ? FreeHand(FVector(-17,-59,-3),FRotator(7,-15,-9)) : HookReleaseWrist(HeldOffset);
        if(A<HookCockPhase) return Mix(From,Cocked,A/HookCockPhase);
        return Mix(Cocked,Release,(A-HookCockPhase)/(1.f-HookCockPhase));
    }
    if(CastPhase==EMCHookCastPhase::Outbound) {
        // Only the right mitten follows through. The left balances the cast.
        const float Age=FMath::Max(0.f,float(Now()-PhaseStartedAt));
        const FTransform FollowThrough=Left ? FreeHand(FVector(-23,-62,-8),FRotator(5,-19,-13))
            : HookRightWrist(HeldOffset,FVector(48,7,9),FRotator(8,24,28));
        const FTransform Settle=Left ? FreeHand(FVector(10,-54,-16),FRotator(-3,0,-5))
            : HookRightWrist(HeldOffset,FVector(19,9,8),FRotator(-3,0,8));
        if(Age<.065f) return Mix(From,FollowThrough,Age/.065f);
        return Mix(FollowThrough,Settle,(Age-.065f)/.23f);
    }
    if(CastPhase==EMCHookCastPhase::Pulling) {
        const float Work=FMath::Sin(float(Now()-PhaseStartedAt)*12.f)*2.f;
        const FTransform Goal=Left ? FreeHand(FVector(12,-52,-17),FRotator(-8,-7,-8))
            : HookRightWrist(HeldOffset,FVector(14+Work,9,6),FRotator(-8,8,10));
        return Mix(From,Goal,FMath::Min(1.f,float(Now()-PhaseStartedAt)/.12f));
    }
    if(CastPhase==EMCHookCastPhase::Returning) {
        const FTransform Catch=Left ? FreeHand(FVector(4,-54,-18),FRotator(0,-5,-5))
            : HookRightWrist(HeldOffset,FVector(16,12,9),FRotator(-4,8,8));
        return Mix(From,Catch,A);
    }
    return Mix(From,Left ? Rest : HookReadyWrist(HeldOffset),A);
}
FTransform AMCHookSkillAction::GetCasterHandFrame(bool Left) const
{ return Hero()?HandLocal(Left)*FTransform(GetCastPoseFacing(),Hero()->GetActorLocation()):FTransform::Identity; }
FQuat AMCHookSkillAction::GetCastPoseFacing() const
{
    if(!Hero()) return CastFacing.Quaternion();
    const float ReleaseAge=GetPresentationReleaseAge();
    const float Weight=CastPhase==EMCHookCastPhase::Windup?Ease(GetPhaseAlpha()/HookCockPhase)
        :ReleaseAge>=0?1.f-Ease(ReleaseAge/.24f):0.f;
    return HookPoseFacing(Hero()->GetActorRotation(),CastFacing,Weight);
}
FTransform AMCHookSkillAction::GetCastBodyOffset() const
{
    FTransform Goal=FTransform::Identity;
    const float A=GetPhaseAlpha();float Blend=A;
    if(CastPhase==EMCHookCastPhase::Windup) {
        const FTransform Cocked=HookBodyPose(FRotator(12,-24,-8),FVector(-9,4,-6),.83);
        const FTransform Release=HookBodyPose(FRotator(-17,11,6),FVector(9,-2,4),1.13);
        if(A<HookCockPhase) return Mix(PhaseBody,Cocked,A/HookCockPhase);
        return Mix(Cocked,Release,(A-HookCockPhase)/(1.f-HookCockPhase));
    }
    else if(CastPhase==EMCHookCastPhase::Outbound) {
        const float Age=FMath::Max(0.f,float(Now()-PhaseStartedAt));
        const FTransform Throw=HookBodyPose(FRotator(-21,15,8),FVector(11,-3,0),1.06);
        if(Age<.065f) return Mix(PhaseBody,Throw,Age/.065f);
        const FTransform Recoil=HookBodyPose(FRotator(4,-3,-2),FVector(-2,1,-2),.97);
        if(Age<.19f) return Mix(Throw,Recoil,(Age-.065f)/.125f);
        return Mix(Recoil,FTransform::Identity,(Age-.19f)/.13f);
    } else if(CastPhase==EMCHookCastPhase::Pulling) {
        Goal=FTransform(FRotator(-8,-4,-2),FVector(-3,0,-3));Blend=FMath::Min(1.f,float(Now()-PhaseStartedAt)/.12f);
    } else if(CastPhase==EMCHookCastPhase::Returning) Goal=FTransform(FRotator(-3,0,-1),FVector(-1,0,-1));
    return Mix(PhaseBody,Goal,Blend);
}
void AMCHookSkillAction::SetCastPhase(EMCHookCastPhase Phase,float Duration)
{
    const FTransform Right=HandLocal(false),Left=HandLocal(true),Body=GetCastBodyOffset();
    FTransform PreviousVisual=PropMesh?PropMesh->GetComponentTransform():GetActorTransform();
    if(Hero() && (CastPhase==EMCHookCastPhase::Windup || CastPhase==EMCHookCastPhase::Recovery || CastPhase==EMCHookCastPhase::Returning))
        PreviousVisual=Hero()->GetPresentationFrame().ToCanonicalWorld(PreviousVisual);
    else if(CastPhase==EMCHookCastPhase::Pulling && PulledPawn && HookTarget==PulledPawn)
        PreviousVisual=PulledPawn->GetPresentationFrame().ToCanonicalWorld(PreviousVisual);
    PhaseVisualRotation=PreviousVisual.GetRotation();
    PhaseRightHand=Right;PhaseLeftHand=Left;PhaseBody=Body;
    CastPhase=Phase;PhaseStartedAt=Now();PhaseDuration=Duration;ForceNetUpdate();
}
void AMCHookSkillAction::StartCommitted()
{
    Super::StartCommitted();
    // The accepted view is fixed. The presentation starts at the runner's
    // current pose and turns into that aim while locomotion keeps ticking.
    GetAim(CastView,CastLook);
    CastFacing=HookAimFacing(Hero()->GetActorRotation(),CastLook);
    auto ReadHand=[&](bool Left) {
        const FName Bone=Hero()->RigBone(Left?TEXT("hand_l"):TEXT("hand_r"));
        const auto* Mesh=Hero()->GetMesh();
        FTransform Pose=Mesh->DoesSocketExist(Bone)?Hero()->GetSimulationMeshSocketTransform(Bone).GetRelativeTransform(Hero()->GetActorTransform())
            :FTransform(FVector(35,Left?-35:35,15));
        Pose.SetScale3D(FVector::OneVector);return Pose;
    };
    RestRightHand=ReadHand(false);RestLeftHand=ReadHand(true);
    SetCastPhase(EMCHookCastPhase::Windup,FMath::Clamp(Param(TEXT("WindupSeconds"),HookWindupSeconds),.08f,.4f));
    // The aiming view already uses this exact pose. Direct native casts also
    // start with the prop safely at the right side instead of the tool wrist.
    PhaseRightHand=HookReadyWrist(ReadHeldOffset());
    UpdatePresentation();
}
void AMCHookSkillAction::Launch()
{
    const FTransform Release=HookPropAtWrist(GetCasterHandFrame());
    FlightOrigin=Release.GetLocation();
    const float Range=EffectiveHookRange(Param(TEXT("HookRange"),1400.f));
    const float Speed=EffectiveHookFlightSpeed(Param(TEXT("FlightSpeed"),DefaultHookFlightSpeed));
    const float StopDistance=HookStopDistance(Param(TEXT("StopDistance"),DefaultHookStopDistance));
    FHitResult PredictedContact;
    ResolveHookShot(*Hero(),this,CastView,CastLook,FlightOrigin,Range,Speed*HookPullSpeedScale,StopDistance,false,FlightDirection,PredictedContact);
    ImpactPoint=FlightOrigin;
    SetCastPhase(EMCHookCastPhase::Outbound,Range/Speed);
    MarkPresentationReleased();
    PhaseVisualRotation=Release.GetRotation();
    SetActorLocationAndRotation(FlightOrigin,FlightDirection.Rotation());
    PlaySkillCue(TEXT("Start"),FlightOrigin,FlightDirection);StartLoop();
}
FVector AMCHookSkillAction::GetTetherStart() const { return GetCasterHandFrame().GetLocation(); }
FVector AMCHookSkillAction::GetHookHeadLocation() const
{
    if(CastPhase==EMCHookCastPhase::Outbound) {
        const double Distance=EffectiveHookRange(Param(TEXT("HookRange"),1400.f))*MCHookPullPrediction::TravelProgress(GetPhaseAlpha());
        return FlightOrigin+FlightDirection*Distance;
    }
    if(CastPhase==EMCHookCastPhase::Pulling && IsValid(HookTarget)) return HookTarget->GetActorTransform().TransformPosition(TargetLocalContact);
    const FVector GripPivot=HookPropAtWrist(GetCasterHandFrame()).GetLocation();
    if(CastPhase==EMCHookCastPhase::Returning) return FMath::Lerp(ImpactPoint,GripPivot,double(Ease(GetPhaseAlpha())));
    return GripPivot;
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
    FVector Goal;
    const float Speed=EffectiveHookPullSpeed(Param(TEXT("FlightSpeed"),DefaultHookFlightSpeed));
    const float StopDistance=HookStopDistance(Param(TEXT("StopDistance"),DefaultHookStopDistance));
    if(!Caster || !ReadHookPull(*Caster,this,Hit,Speed,StopDistance,false,bPullCaster,Goal)) return false;
    AActor* Target=Hit.GetActor();
    auto* Moved=bPullCaster ? Caster : Cast<AMCToothCharacter>(Target);
    const float AuthoredReach=AuthoredHookRange(Param(TEXT("HookRange"),1400.f));
    const float ExtraDistance=FMath::Max(0.f,float(FVector::Distance(Goal,Moved->GetActorLocation()))-AuthoredReach);
    const float Seconds=FMath::Min(8.f,FMath::Clamp(Param(TEXT("PullSeconds"),2.5f),.1f,8.f)+ExtraDistance/Speed);
    const auto* Move=Moved->GetCharacterMovement();
    const bool Grounded=Move->IsMovingOnGround() && FMath::Abs(Goal.Z-Moved->GetActorLocation().Z)<=Move->MaxStepHeight;
    auto* Prediction=Cast<UMCToothMovementComponent>(Moved->GetCharacterMovement());
    if(!Prediction || !Prediction->BeginAcceptedHookPull(GetActionID(),Goal,Speed,StopDistance,Seconds,!Grounded)) return false;
    PullMovement=Prediction;
    if(!Prediction->SampleAcceptedHookPullTime(GetActionID(),PreviousPullSimulationTime)) {
        Prediction->EndAcceptedHookPull(GetActionID());PullMovement.Reset();return false;
    }
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
    if(auto* Move=PullMovement.Get()) Move->EndAcceptedHookPull(GetActionID());
    PullMovement.Reset();ReturnReason=Reason;
    if(!bContactedTarget && Hero()->Expression) Hero()->Expression->PlayHookReaction(GetActionID(),EMCHookFaceReaction::Miss);
    const float Seconds=FMath::Clamp(float(FVector::Distance(ImpactPoint,GetTetherStart())/3200.),.18f,HookRangeScale*.45f);
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
    const bool Held=CastPhase==EMCHookCastPhase::Windup || CastPhase==EMCHookCastPhase::Recovery;
    const FTransform Contact=HookPropAtWrist(GetCasterHandFrame());
    const FVector Direction=CastPhase==EMCHookCastPhase::Outbound?FlightDirection:(End-Start).GetSafeNormal();
    const FQuat Rotation=Held ? Contact.GetRotation() : (Direction.IsNearlyZero()?FlightDirection:Direction).ToOrientationQuat();
    SetActorLocationAndRotation(End,Rotation);
    if(PropMesh) {
        FTransform Visual=Held ? Contact : PropTransform*GetActorTransform();
        if(CastPhase==EMCHookCastPhase::Outbound) {
            Visual.SetRotation(FQuat::Slerp(PhaseVisualRotation,Visual.GetRotation(),
                Ease(float(Now()-PhaseStartedAt)/.08f)).GetNormalized());
        }
        if(Hero() && CastPhase==EMCHookCastPhase::Returning) {
            // A returning prop bridges a world/target contact and its caster's
            // hand. Each endpoint belongs to its own presented frame.
            FTransform Origin(PhaseVisualRotation,ImpactPoint);
            if(PulledPawn && HookTarget==PulledPawn) Origin=PulledPawn->GetPresentationFrame().FromCanonicalWorld(Origin);
            const FTransform Hand=Hero()->GetPresentationFrame().FromCanonicalWorld(Contact);
            const float A=Ease(GetPhaseAlpha());
            Visual.SetLocation(FMath::Lerp(Origin.GetLocation(),Hand.GetLocation(),A));
            Visual.SetRotation(FQuat::Slerp(Origin.GetRotation(),Hand.GetRotation(),A).GetNormalized());
        }
        else if(Hero() && Held) Visual=Hero()->GetPresentationFrame().FromCanonicalWorld(Visual);
        else if(CastPhase==EMCHookCastPhase::Pulling && PulledPawn && HookTarget==PulledPawn)
            Visual=PulledPawn->GetPresentationFrame().FromCanonicalWorld(Visual);
        PropMesh->SetWorldTransform(Visual);
    }
    if(LoopComponent) {
        LoopComponent->SetVariablePosition(TEXT("User.TetherStart"),Hero()?Hero()->GetPresentationFrame().FromCanonicalWorld(Start):Start);
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
        if(!PullMovement.IsValid() || !PullMovement->IsAcceptedHookPullOwned(GetActionID())) {StopPull(TEXT("MotionLost"));UpdatePresentation();return;}
        const float AuthoredReach=AuthoredHookRange(Param(TEXT("HookRange"),1400.f));
        const float BreakSlack=FMath::Max(100.f,FMath::Clamp(Param(TEXT("BreakRange"),1800.f),150.f,4000.f)-AuthoredReach);
        if(FVector::DistSquared(Start,End)>FMath::Square(HookRangeScale*AuthoredReach+BreakSlack)) {StopPull(TEXT("BreakRange"));UpdatePresentation();return;}
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MCActiveHookTether),false,Hero());Query.AddIgnoredActor(this);Query.AddIgnoredActor(HookTarget);
        FHitResult Wall;
        if(GetWorld()->LineTraceSingleByChannel(Wall,Start,End,ECC_Visibility,Query)) {StopPull(TEXT("TetherBlocked"),&Wall);UpdatePresentation();return;}
        const float Seconds=float(Now()-LastSampleAt),Speed=EffectiveHookPullSpeed(Param(TEXT("FlightSpeed"),DefaultHookFlightSpeed));
        float SimulationTime=0;
        if(!PullMovement->SampleAcceptedHookPullTime(GetActionID(),SimulationTime)) {StopPull(TEXT("MotionLost"));UpdatePresentation();return;}
        // Remote CMC may process several buffered input moves in one actor
        // frame. Budget their trusted source time, retaining the same .25s cap.
        const float SimulationDelta=SimulationTime-PreviousPullSimulationTime;
        const float SampleSeconds=FMath::Max(Seconds,SimulationDelta);
        const bool Continuous=SampleSeconds>0
            ?MCActiveSkillMotion::ContinuousPath(PreviousMovedPosition,PulledPawn->GetActorLocation(),SampleSeconds,Speed)
            :PreviousMovedPosition.Equals(PulledPawn->GetActorLocation(),UE_KINDA_SMALL_NUMBER);
        if(!FMath::IsFinite(Seconds) || Seconds<0 || !FMath::IsFinite(SimulationDelta) || SimulationDelta<0
            || SimulationDelta>.25f || !Continuous) {StopPull(TEXT("DiscontinuousMotion"));UpdatePresentation();return;}
        PreviousPullSimulationTime=SimulationTime;
        PreviousMovedPosition=PulledPawn->GetActorLocation();LastSampleAt=Now();
        const FVector Goal=PulledPawn==Hero()?End:Hero()->GetActorLocation(),Delta=Goal-PulledPawn->GetActorLocation();
        const float Stop=HookStopDistance(Param(TEXT("StopDistance"),DefaultHookStopDistance));
        // The bounded RMS stops at the radius without overshoot. Allow a
        // millimetre numeric tolerance instead of waiting for the deadline.
        if(Delta.Size()<=Stop+.1f || Now()>=EndsAt) {
            PullStopReason=Now()>=EndsAt?TEXT("PullDeadline"):TEXT("ReachedStopDistance");
            BeginReturn(Now()>=EndsAt?EMCActiveSkillEndReason::TimedOut:EMCActiveSkillEndReason::Completed);
        }
        else {
            const FVector Velocity=Delta.GetSafeNormal()*FMath::Min(Speed*MCHookPullPrediction::PeakSpeedScale,
                float((Delta.Size()-Stop)/FMath::Max(.01f,DeltaSeconds)));
            FHitResult Blocker;
            if(!MCActiveSkillMotion::ClearPullStep(PulledPawn,this,PulledPawn==Hero()?HookTarget.Get():Hero(),Velocity,DeltaSeconds,&Blocker)) {
                StopPull(TEXT("PullStepBlocked"),&Blocker);UpdatePresentation();return;
            }
            if(!PullMovement->UpdateAcceptedHookPull(GetActionID(),Goal)) {StopPull(TEXT("MotionUpdateLost"));UpdatePresentation();return;}
        }
    } else if(CastPhase==EMCHookCastPhase::Returning && GetPhaseAlpha()>=1) {
        StopLoop();PlaySkillCue(TEXT("End"),GetTetherStart(),FVector::UpVector);
        SetCastPhase(EMCHookCastPhase::Recovery,.18f);
    } else if(CastPhase==EMCHookCastPhase::Recovery && GetPhaseAlpha()>=1) {Finish(ReturnReason);return;}
    UpdatePresentation();
}
void AMCHookSkillAction::OnFinished(EMCActiveSkillEndReason Reason)
{
    if(auto* Move=PullMovement.Get()) Move->EndAcceptedHookPull(GetActionID());
    PullMovement.Reset();
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
    DOREPLIFETIME(AMCHookSkillAction,FlightOrigin);DOREPLIFETIME(AMCHookSkillAction,FlightDirection);
    DOREPLIFETIME(AMCHookSkillAction,CastFacing);DOREPLIFETIME(AMCHookSkillAction,ImpactPoint);DOREPLIFETIME(AMCHookSkillAction,bContactedTarget);
    DOREPLIFETIME(AMCHookSkillAction,PhaseRightHand);DOREPLIFETIME(AMCHookSkillAction,PhaseLeftHand);DOREPLIFETIME(AMCHookSkillAction,PhaseBody);
    DOREPLIFETIME(AMCHookSkillAction,RestRightHand);DOREPLIFETIME(AMCHookSkillAction,RestLeftHand);
    DOREPLIFETIME(AMCHookSkillAction,PhaseVisualRotation);
    DOREPLIFETIME(AMCHookSkillAction,PullStopReason);
}
