#include "MCGrenadeSkillAction.h"
#include "MCActiveSkillAimPreview.h"
#include "MCActiveSkillImpact.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Net/UnrealNetwork.h"

AMCGrenadeSkillAction::AMCGrenadeSkillAction()
{
    Body=CreateDefaultSubobject<USphereComponent>(TEXT("GrenadeBody"));SetRootComponent(Body);SceneRoot->SetupAttachment(Body);
    Body->InitSphereRadius(22.f);Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetCollisionObjectType(ECC_WorldDynamic);Body->SetCollisionResponseToAllChannels(ECR_Block);
    Body->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);Body->SetGenerateOverlapEvents(false);
    Movement=CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("GrenadeMovement"));
    Movement->SetUpdatedComponent(Body);Movement->bAutoActivate=false;
    Movement->bShouldBounce=true;Movement->Bounciness=.38f;Movement->Friction=.45f;
    Movement->ProjectileGravityScale=1.f;Movement->bRotationFollowsVelocity=true;Movement->bForceSubStepping=true;
    Movement->MaxSimulationTimeStep=1.f/60.f;Movement->MaxSimulationIterations=8;
    PropAsset=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/Props/SM_Buster_Grenade.SM_Buster_Grenade"));
    HeldMount=FTransform(FQuat::Identity,FVector(20.93208,66.03961,-17.62887));
    bHasRightGrip=true;
    RightGripTransform=FTransform(FQuat(.551196337,-.136977211,-.820045054,.070327856).GetNormalized(),FVector(-22.397028,-12.428428,2.525267));
    StartVFX=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Grenade_Start.NS_Grenade_Start"));
    LoopVFX=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Grenade_Loop.NS_Grenade_Loop"));
    ImpactVFX=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Grenade_Impact.NS_Grenade_Impact"));
    WarningVFX=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Grenade_Warning.NS_Grenade_Warning"));
}

UMCGrenadeSkillPerkEffect::UMCGrenadeSkillPerkEffect() {ActionClass=AMCGrenadeSkillAction::StaticClass();}

FVector AMCGrenadeSkillAction::HeldOffset() const
{
    // Source mesh was centered on ReleaseCenter before import. Keeping the
    // same center for the hand, physics and throw avoids a release-frame jump.
    return HeldMount.GetLocation()+FVector(FMath::Clamp(Param(TEXT("HeldX"),50.f),0.f,120.f)-50.f,
        FMath::Clamp(Param(TEXT("HeldY"),44.f),-80.f,80.f)-44.f,
        FMath::Clamp(Param(TEXT("HeldZ"),25.f),-40.f,100.f)-25.f);
}

float AMCGrenadeSkillAction::FuseSeconds() const {return FMath::Clamp(Param(TEXT("FuseSeconds"),3.f),.4f,30.f);}
float AMCGrenadeSkillAction::DropLeadSeconds() const
{
    return FMath::Clamp(Param(TEXT("OverholdDropLeadSeconds"),.15f),0.f,FuseSeconds()*.5f);
}

float AMCGrenadeSkillAction::ThrowRadius() const
{
    // The sphere's authored size is applied on the server at commit. Predict
    // the same size on clients from the replicated action tuning.
    return HasAuthority() ? Body->GetScaledSphereRadius()
        : FMath::Clamp(Param(TEXT("CollisionRadius"),22.f),2.f,35.f)*Body->GetShapeScale();
}

bool AMCGrenadeSkillAction::ResolveThrow(FVector& Origin,FVector& Velocity,FHitResult* Block) const
{
    FVector Direction;
    if(!MCActiveSkillImpact::ResolveLaunch(this,HeldOffset(),ThrowRadius(),Param(TEXT("AimRange"),6000.f),Origin,Direction,Block))
        return false;
    const float Speed=FMath::Clamp(Param(TEXT("ThrowSpeed"),1100.f),100.f,3500.f);
    const float Lift=FMath::Clamp(Param(TEXT("ThrowLift"),120.f),0.f,600.f);
    Velocity=(Direction*Speed+FVector(0,0,Lift)).GetClampedToMaxSize(4000.f);
    return true;
}

bool AMCGrenadeSkillAction::BuildAimPreview(FMCActiveSkillAimPreview& Out) const
{
    Out={};
    const auto* Caster=Hero();
    if(Phase!=EMCGrenadeSkillPhase::Cooking || !IsCommitted() || IsFinished()
        || !Caster || !Caster->CanWork() || !Caster->ToothPhysics || !Caster->ToothPhysics->CanAct()) return false;

    const double Remaining=FuseEndsAt-Now();
    if(Remaining<=0.) return false;
    Out.FuseSeconds=float(Remaining);
    const float Lead=DropLeadSeconds();
    const float Warning=FMath::Clamp(Param(TEXT("WarningSeconds"),.65f),0.f,FuseSeconds());
    Out.bFuseWarning=Remaining<=FMath::Max(Lead,Warning);
    // Replication can leave the grenade in Cooking briefly after the server's
    // forced release time. Keep the warning and retire the throw prediction.
    if(Remaining<=Lead) return true;

    FVector Origin,Velocity;
    FHitResult Block;
    if(!ResolveThrow(Origin,Velocity,&Block)) {
        Out.State=EMCActiveSkillAimState::Blocked;
        if(Block.bBlockingHit) {
            Out.bHasContact=true;
            Out.ContactPoint=Block.ImpactPoint;
            Out.ContactNormal=Block.ImpactNormal;
        }
        return true;
    }

    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCGrenadeAimPreview),false,this);
    FCollisionResponseParams Responses;
    Body->InitSweepCollisionParams(Query,Responses);
    Query.AddIgnoredActor(Caster);
    const auto Sphere=FCollisionShape::MakeSphere(ThrowRadius());
    const FVector Gravity(0,0,Movement->GetGravityZ());
    const int32 Steps=FMath::Clamp(FMath::CeilToInt32(Remaining*30.),1,120);
    const int32 DrawStride=FMath::DivideAndRoundUp(Steps,28);
    Out.Path.Reserve(30);
    Out.Path.Add(Origin);
    FVector Previous=Origin;
    for(int32 Step=1;Step<=Steps;++Step) {
        const double Time=Remaining*Step/Steps;
        const FVector Point=Origin+Velocity*Time+Gravity*(.5*Time*Time);
        FHitResult Hit;
        if(GetWorld()->SweepSingleByChannel(Hit,Previous,Point,FQuat::Identity,Body->GetCollisionObjectType(),Sphere,Query,Responses)) {
            Out.Path.Add(Hit.Location);
            Out.bHasContact=true;
            Out.ContactPoint=Hit.ImpactPoint;
            Out.ContactNormal=Hit.ImpactNormal;
            Out.State=EMCActiveSkillAimState::Contact;
            return true;
        }
        if(Step%DrawStride==0 || Step==Steps) Out.Path.Add(Point);
        Previous=Point;
    }
    return true;
}

bool AMCGrenadeSkillAction::GetPresentationHandTarget(bool bLeft,FTransform& WorldPose) const
{
    return Phase==EMCGrenadeSkillPhase::Cooking && Super::GetPresentationHandTarget(bLeft,WorldPose);
}

bool AMCGrenadeSkillAction::CanStart() const
{
    FVector Origin,Direction;
    return Super::CanStart() && FMath::Clamp(Param(TEXT("MaxLifetime"),120.f),1.f,600.f)>=FuseSeconds()+.5f
        && MCActiveSkillImpact::ResolveLaunch(this,HeldOffset(),FMath::Clamp(Param(TEXT("CollisionRadius"),22.f),2.f,35.f),
            Param(TEXT("AimRange"),6000.f),Origin,Direction);
}

void AMCGrenadeSkillAction::BeginPlay()
{
    Super::BeginPlay();Movement->Deactivate();Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AMCGrenadeSkillAction::StartCommitted()
{
    Super::StartCommitted();SetInstigator(Hero());
    Body->SetSphereRadius(FMath::Clamp(Param(TEXT("CollisionRadius"),22.f),2.f,35.f));
    ArmedAt=Now();FuseEndsAt=ArmedAt+FuseSeconds();
    DropAt=FuseEndsAt-DropLeadSeconds();Phase=EMCGrenadeSkillPhase::Cooking;
    FollowHand();OnRep_Phase();
    PlaySkillCue(TEXT("Start"),GetActorLocation(),GetActorForwardVector(),Body->GetScaledSphereRadius());
    StartLoop();ForceNetUpdate();
}

void AMCGrenadeSkillAction::FollowHand()
{
    if(!Hero()) return;
    FVector Origin,Direction;
    // Even a wall appearing during cooking can only clamp the held grenade to
    // its near side. A release never teleports this accepted hazard through it.
    MCActiveSkillImpact::ResolveLaunch(this,HeldOffset(),Body->GetScaledSphereRadius(),Param(TEXT("AimRange"),6000.f),Origin,Direction);
    const FQuat HeldRotation=(HeldMount*Hero()->GetActorTransform()).GetRotation().GetNormalized();
    if(!Origin.ContainsNaN()) SetActorLocationAndRotation(Origin,HeldRotation,false,nullptr,ETeleportType::TeleportPhysics);
}

void AMCGrenadeSkillAction::Launch(FVector Velocity,EMCGrenadeSkillPhase Next)
{
    if(!HasAuthority() || Phase!=EMCGrenadeSkillPhase::Cooking || Velocity.ContainsNaN()) return;
    Phase=Next;MarkPresentationReleased();PresentationRecoveryEndsAt=Now()+.28;
    Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    if(Hero()) Body->IgnoreActorWhenMoving(Hero(),true);
    Movement->SetUpdatedComponent(Body);Movement->Velocity=Velocity.GetClampedToMaxSize(4000.f);
    Movement->Activate(true);OnRep_Phase();ForceNetUpdate();
}

void AMCGrenadeSkillAction::ReleaseInput()
{
    if(!HasAuthority() || !IsCommitted() || IsFinished() || Phase!=EMCGrenadeSkillPhase::Cooking) return;
    if(!Hero() || !Hero()->CanWork() || !Hero()->ToothPhysics || !Hero()->ToothPhysics->CanAct() || Now()>=DropAt) {Drop();return;}
    FVector Origin,Velocity;
    if(!ResolveThrow(Origin,Velocity)) {
        SetActorLocation(Origin,false,nullptr,ETeleportType::TeleportPhysics);Drop();return;
    }
    SetActorLocation(Origin,false,nullptr,ETeleportType::TeleportPhysics);
    Launch(Velocity,EMCGrenadeSkillPhase::Thrown);
}

void AMCGrenadeSkillAction::Drop()
{
    if(Phase!=EMCGrenadeSkillPhase::Cooking) return;
    const FVector CarryVelocity=Hero()?Hero()->GetVelocity().GetClampedToMaxSize(500.f):FVector::ZeroVector;
    Launch(CarryVelocity+FVector(0,0,-45.f),EMCGrenadeSkillPhase::Dropped);
    PresentGroundWarning();
    bWarningPresented=true;
}

void AMCGrenadeSkillAction::PresentGroundWarning()
{
    // Warning is an authored horizontal danger ring. The attached Loop owns
    // the fuse spark; never float the ground ring at the cooking hand height.
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCGrenadeGroundWarning),false,this);
    if(Hero()) Query.AddIgnoredActor(Hero());
    FHitResult Ground;
    const FVector Start=GetActorLocation()+FVector(0,0,100.f);
    if(GetWorld()->LineTraceSingleByObjectType(Ground,Start,Start-FVector(0,0,5000.f),
        FCollisionObjectQueryParams(ECC_WorldStatic),Query) && Ground.ImpactNormal.Z>.2f)
        PlaySkillCue(TEXT("Warning"),Ground.ImpactPoint+FVector(0,0,2.f),FVector::UpVector,
            FMath::Clamp(Param(TEXT("BlastRadius"),300.f),20.f,2000.f));
}

void AMCGrenadeSkillAction::Detonate()
{
    if(!HasAuthority() || !IsCommitted() || IsFinished() || DetonationCount!=0) return;
    // Seal before callbacks: damage may kill the source and reenter Cancel.
    DetonationCount=1;Phase=EMCGrenadeSkillPhase::Detonated;FinishAt=Now()+.35;
    Movement->StopMovementImmediately();Movement->Deactivate();Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    StopLoop();ReleasePresentation();OnRep_Phase();
    FMCActiveSkillBlastSettings Blast;
    Blast.Radius=Param(TEXT("BlastRadius"),300.f);Blast.FoodDamage=Param(TEXT("FoodDamage"),100.f);
    Blast.EnemyDamage=Param(TEXT("EnemyDamage"),60.f);Blast.PlayerDamage=Param(TEXT("PlayerDamage"),25.f);
    Blast.PushSpeed=Param(TEXT("PushSpeed"),500.f); // Grenade deliberately has no arena-tooth policy.
    PlaySkillCue(TEXT("Impact"),GetActorLocation(),FVector::UpVector,Blast.Radius);
    MCActiveSkillImpact::ApplyRadial(this,SourceState(),GetActorLocation(),Blast);ForceNetUpdate();
}

void AMCGrenadeSkillAction::Cancel(EMCActiveSkillEndReason Reason)
{
    if(!HasAuthority() || IsFinished()) return;
    if(!IsCommitted() || Reason==EMCActiveSkillEndReason::RunReset || Reason==EMCActiveSkillEndReason::WorldEnded) {Super::Cancel(Reason);return;}
    if(Reason==EMCActiveSkillEndReason::TimedOut) {Drop();Detonate();return;}
    // Loss/depletion/replacement/death releases the hand, not the accepted fuse.
    Drop();ReleasePresentation();
}

void AMCGrenadeSkillAction::Tick(float Dt)
{
    Super::Tick(Dt);
    if(!HasAuthority() || !IsCommitted() || IsFinished()) return;
    if(PresentationRecoveryEndsAt>=0 && Now()>=PresentationRecoveryEndsAt) {ReleasePresentation();PresentationRecoveryEndsAt=-1;}
    if(Phase==EMCGrenadeSkillPhase::Detonated) {if(Now()>=FinishAt) Finish();return;}
    if(Phase==EMCGrenadeSkillPhase::Cooking) {
        // A living knocked-down owner no longer controls a hand/capsule launch
        // point. Release the accepted hazard without resetting its fuse.
        if(!Hero() || !Hero()->CanWork() || !Hero()->ToothPhysics || !Hero()->ToothPhysics->CanAct()) Drop();
        else FollowHand();
        if(!bWarningPresented && Now()>=FuseEndsAt-FMath::Clamp(Param(TEXT("WarningSeconds"),.65f),0.f,FuseSeconds())) {
            bWarningPresented=true;PresentGroundWarning();
        }
        if(Now()>=DropAt) Drop();
    }
    if(FuseEndsAt>0 && Now()>=FuseEndsAt) {Drop();Detonate();}
}

void AMCGrenadeSkillAction::OnRep_Phase()
{
    PropMesh->SetVisibility(Phase!=EMCGrenadeSkillPhase::Prepared && Phase!=EMCGrenadeSkillPhase::Detonated,true);
    if(!HasAuthority()) {Movement->Deactivate();Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);}
}

void AMCGrenadeSkillAction::OnFinished(EMCActiveSkillEndReason Reason)
{
    Movement->Deactivate();Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);Super::OnFinished(Reason);
}

void AMCGrenadeSkillAction::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCGrenadeSkillAction,Phase);DOREPLIFETIME(AMCGrenadeSkillAction,ArmedAt);
    DOREPLIFETIME(AMCGrenadeSkillAction,FuseEndsAt);DOREPLIFETIME(AMCGrenadeSkillAction,DetonationCount);
}
