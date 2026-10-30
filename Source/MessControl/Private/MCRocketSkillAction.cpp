#include "MCRocketSkillAction.h"
#include "MCActiveSkillImpact.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Net/UnrealNetwork.h"

AMCRocketSkillAction::AMCRocketSkillAction()
{
    Body=CreateDefaultSubobject<USphereComponent>(TEXT("RocketBody"));SetRootComponent(Body);SceneRoot->SetupAttachment(Body);
    Body->InitSphereRadius(9.f);Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetCollisionObjectType(ECC_WorldDynamic);Body->SetCollisionResponseToAllChannels(ECR_Block);
    Body->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);Body->SetGenerateOverlapEvents(false);
    Body->SetNotifyRigidBodyCollision(true);
    ProjectileMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileMesh"));ProjectileMesh->SetupAttachment(SceneRoot);
    ProjectileMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);ProjectileMesh->SetGenerateOverlapEvents(false);
    LauncherRoot=CreateDefaultSubobject<USceneComponent>(TEXT("HeldLauncherRoot"));LauncherRoot->SetupAttachment(SceneRoot);
    LauncherRoot->SetAbsolute(true,true,true);PropMesh->SetupAttachment(LauncherRoot);
    Movement=CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("RocketMovement"));
    Movement->SetUpdatedComponent(Body);Movement->bAutoActivate=false;Movement->ProjectileGravityScale=0.f;
    Movement->bShouldBounce=false;Movement->bRotationFollowsVelocity=true;Movement->bForceSubStepping=true;
    Movement->MaxSimulationTimeStep=1.f/60.f;Movement->MaxSimulationIterations=8;
    PropAsset=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/Props/SM_Buster_Rocket.SM_Buster_Rocket"));
    // Exact source root.x pose/rest registration to the native capsule frame.
    // Mount, wrists and muzzle all use this complete frame; no old muzzle-root offset.
    PropTransform=FTransform::Identity;
    LauncherMount=FTransform(FQuat(-.05629097,.02959843,-.18749064,.98020536).GetNormalized(),FVector(80.10883,10.22189,-28.47319));
    // Registered evaluated Buster mittens, mapped to the current runtime wrist
    // frame. The foregrip guide is LEFT; the rear trigger is RIGHT.
    bHasLeftGrip=true;bHasRightGrip=true;
    RightGripTransform=FTransform(FQuat(-.360346884,.405698925,.727635324,.419649065).GetNormalized(),FVector(-69.191429,3.740859,11.228007));
    LeftGripTransform=FTransform(FQuat(.488474160,-.373419315,.711356759,.340474069).GetNormalized(),FVector(-7.762317,-11.878653,-3.651455));
    StartVFX=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Rocket_Start.NS_Rocket_Start"));
    LoopVFX=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Rocket_Loop.NS_Rocket_Loop"));
    ImpactVFX=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Rocket_Impact.NS_Rocket_Impact"));
}

UMCRocketSkillPerkEffect::UMCRocketSkillPerkEffect() {ActionClass=AMCRocketSkillAction::StaticClass();}

FVector AMCRocketSkillAction::MeshLocalMuzzle() const
{
    // Legacy MuzzleX/Y/Z were pawn offsets for the old pivot. They cannot move
    // the projectile independently of the visible barrel. New tuning is mesh-local.
    return FVector(Param(TEXT("MuzzleLocalX"),float(LauncherMuzzleLocal.X)),Param(TEXT("MuzzleLocalY"),float(LauncherMuzzleLocal.Y)),
        Param(TEXT("MuzzleLocalZ"),float(LauncherMuzzleLocal.Z)));
}
FTransform AMCRocketSkillAction::ReadyLauncherPose(FVector Direction) const
{
    if(!Hero()) return FTransform::Identity;
    FTransform Pose=PropTransform*LauncherMount*Hero()->GetActorTransform();
    const FVector Forward=Pose.TransformVectorNoScale(LauncherForwardLocal.GetSafeNormal(SMALL_NUMBER,FVector::ForwardVector)).GetSafeNormal();
    const FQuat Correction=FQuat::FindBetweenNormals(Forward,Direction.GetSafeNormal(SMALL_NUMBER,Hero()->GetActorForwardVector()));
    Pose.SetRotation((Correction*Pose.GetRotation()).GetNormalized());
    Pose.SetScale3D(FVector::OneVector);
    return Pose;
}
FVector AMCRocketSkillAction::GetLauncherMuzzlePoint() const
{return PropMesh?PropMesh->GetComponentTransform().TransformPosition(MeshLocalMuzzle()):GetActorLocation();}
FVector AMCRocketSkillAction::GetLauncherForward() const
{return PropMesh?PropMesh->GetComponentTransform().TransformVectorNoScale(LauncherForwardLocal).GetSafeNormal():GetActorForwardVector();}
float AMCRocketSkillAction::FlightSeconds() const {return FMath::Clamp(Param(TEXT("FlightSeconds"),6.f),.25f,15.f);}
float AMCRocketSkillAction::WindupSeconds() const {return FMath::Clamp(Param(TEXT("FireWindupSeconds"),.15f),0.f,.6f);}
bool AMCRocketSkillAction::IsLauncherHeld() const
{
    return IsCommitted() && !IsFinished() && Hero() && Hero()->GetActiveSkillPresentation()==this
        && (Phase==EMCRocketSkillPhase::Windup || (GetPresentationReleaseAge()>=0
            && GetPresentationReleaseAge()<FMath::Clamp(LauncherRecoverySeconds,.15f,1.2f)));
}
void AMCRocketSkillAction::UpdateHeldLauncher()
{
    const bool Held=IsLauncherHeld();PropMesh->SetVisibility(Held,true);
    if(!Held || !LauncherRoot || !Hero()) return;
    FVector Direction=GetActorForwardVector();FTransform Pose=ReadyLauncherPose(Direction);
    if(Phase==EMCRocketSkillPhase::Windup) {
        FVector Muzzle;ReadLaunch(Muzzle,Direction,nullptr,&Pose);
        if(bHasDrawStart && WindupSeconds()>UE_SMALL_NUMBER) {
            const FTransform From=InitialDrawPropActorLocal*Hero()->GetActorTransform();
            FTransform Draw;Draw.Blend(From,Pose,FMath::SmoothStep(0.f,1.f,GetPresentationAge()/WindupSeconds()));Pose=Draw;
        }
    }
    const float Age=GetPresentationReleaseAge();
    const float Kick=Age>=0?FMath::Exp(-Age*11.f)*FMath::Sin(FMath::Clamp(Age/.11f,0.f,1.f)*HALF_PI):0.f;
    Pose.AddToTranslation(-Direction*(Kick*9.f)+FVector(0,0,Kick*3.f));
    const FVector RecoilAxis=Pose.TransformVectorNoScale(FVector::RightVector).GetSafeNormal();
    Pose.SetRotation((FQuat(RecoilAxis,FMath::DegreesToRadians(Kick*5.f))*Pose.GetRotation()).GetNormalized());
    LauncherRoot->SetWorldTransform(PropTransform.Inverse()*Pose);
}

bool AMCRocketSkillAction::ReadLaunch(FVector& Origin,FVector& Direction,FHitResult* Block,FTransform* PropWorld) const
{
    Origin=GetActorLocation();Direction=GetActorForwardVector();
    if(!GetWorld() || !Hero()) return false;
    FVector View,Look;if(!GetAim(View,Look)) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCRocketActualMuzzle),false,this);Query.AddIgnoredActor(Hero());
    const float Radius=FMath::Clamp(Param(TEXT("ProjectileRadius"),9.f),2.f,40.f);
    Direction=Look;FTransform Pose=ReadyLauncherPose(Look);Origin=Pose.TransformPosition(MeshLocalMuzzle());
    if(PropWorld) *PropWorld=Pose;
    const auto ClearMuzzle=[&]() {
        if(Origin.ContainsNaN() || Pose.ContainsNaN()) return false;
        const FVector Start=Hero()->GetActorLocation()+FVector(0,0,Origin.Z-Hero()->GetActorLocation().Z);
        FHitResult Wall;
        if(GetWorld()->SweepSingleByChannel(Wall,Start,Origin,FQuat::Identity,ECC_Visibility,
            FCollisionShape::MakeSphere(Radius),Query)) {
            if(Block) *Block=Wall;
            Origin=Wall.Location+Wall.ImpactNormal*2.f;return false;
        }
        return true;
    };
    // Test the real forward-facing barrel BEFORE convergence can rotate its
    // muzzle behind a nearby wall. CanStart must reject without spending charge.
    if(!ClearMuzzle()) return false;
    const float Range=FMath::Clamp(Param(TEXT("AimRange"),6000.f),100.f,20000.f);
    FHitResult Aim;
    const FVector Target=GetWorld()->LineTraceSingleByChannel(Aim,View,View+Look*Range,ECC_Visibility,Query)?Aim.ImpactPoint:View+Look*Range;
    const auto TargetIsAhead=[&]() {
        if(FVector::DotProduct(Target-Origin,Look)>Radius) return true;
        if(Aim.bBlockingHit) {
            if(Block) *Block=Aim;
            Origin=Aim.ImpactPoint+Aim.ImpactNormal*(Radius+2.f);
        }
        return false;
    };
    if(!TargetIsAhead()) return false;
    // Camera target and muzzle are coupled because the barrel rotates about its
    // actual held origin. Bounded fixed-point alignment keeps authored roll.
    for(int32 I=0;I<5;++I) {
        Pose=ReadyLauncherPose(Direction);Origin=Pose.TransformPosition(MeshLocalMuzzle());
        if(PropWorld) *PropWorld=Pose;
        if(!ClearMuzzle() || !TargetIsAhead()) return false;
        Direction=(Target-Origin).GetSafeNormal(SMALL_NUMBER,Look);
        if(Direction.ContainsNaN() || FVector::DotProduct(Direction,Look)<=0.f) return false;
    }
    Pose=ReadyLauncherPose(Direction);Origin=Pose.TransformPosition(MeshLocalMuzzle());
    Direction=Pose.TransformVectorNoScale(LauncherForwardLocal).GetSafeNormal(SMALL_NUMBER,Look);
    if(PropWorld) *PropWorld=Pose;
    if(Origin.ContainsNaN() || Direction.ContainsNaN() || Pose.ContainsNaN()) return false;
    return ClearMuzzle() && TargetIsAhead() && !Direction.IsNearlyZero() && FVector::DotProduct(Direction,Look)>0.f;
}

bool AMCRocketSkillAction::CanStart() const
{
    FVector Origin,Direction;
    return Super::CanStart() && FMath::Clamp(Param(TEXT("MaxLifetime"),120.f),1.f,600.f)>=FlightSeconds()+WindupSeconds()+.5f
        && ReadLaunch(Origin,Direction);
}

void AMCRocketSkillAction::BeginPlay()
{
    Super::BeginPlay();Movement->Deactivate();Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->OnComponentHit.AddDynamic(this,&AMCRocketSkillAction::OnImpact);
    Movement->OnProjectileStop.AddDynamic(this,&AMCRocketSkillAction::OnProjectileStopped);
    if(!ProjectileAsset.IsNull()) ProjectileMesh->SetStaticMesh(ProjectileAsset.LoadSynchronous());
    ProjectileMesh->SetRelativeTransform(ProjectileTransform);OnRep_Phase();
}

void AMCRocketSkillAction::StartCommitted()
{
    if(Hero() && Hero()->GetMesh()) {
        const FTransform Wrist=Hero()->GetMesh()->GetSocketTransform(Hero()->RigBone(TEXT("hand_r")));
        FTransform Draw=RightGripTransform.Inverse()*Wrist;Draw.SetScale3D(FVector::OneVector);
        InitialDrawPropActorLocal=Draw.GetRelativeTransform(Hero()->GetActorTransform());bHasDrawStart=true;
    }
    Super::StartCommitted();SetInstigator(Hero());
    Body->SetSphereRadius(FMath::Clamp(Param(TEXT("ProjectileRadius"),9.f),2.f,40.f));
    Phase=EMCRocketSkillPhase::Windup;LaunchAt=Now()+WindupSeconds();
    FHitResult Block;
    if(!ReadLaunch(PreparedOrigin,PreparedDirection,&Block)) {
        // Preparation was clear; a post-commit obstruction consumes the accepted
        // shot on its near side instead of creating a rocket behind the wall.
        SetActorLocation(PreparedOrigin,false,nullptr,ETeleportType::TeleportPhysics);
        Detonate(PreparedOrigin,Block.bBlockingHit?Block.ImpactNormal:-PreparedDirection);return;
    }
    SetActorLocationAndRotation(PreparedOrigin,PreparedDirection.Rotation(),false,nullptr,ETeleportType::TeleportPhysics);
    UpdateHeldLauncher();OnRep_Phase();ForceNetUpdate();
    if(WindupSeconds()<=0.f) Launch(true);
}

void AMCRocketSkillAction::Launch(bool bRefreshAim)
{
    if(!HasAuthority() || Phase!=EMCRocketSkillPhase::Windup || IsFinished()) return;
    FVector Origin=PreparedOrigin,Direction=PreparedDirection;FHitResult Block;
    if(bRefreshAim && Hero() && !ReadLaunch(Origin,Direction,&Block)) {
        SetActorLocation(Origin,false,nullptr,ETeleportType::TeleportPhysics);
        Detonate(Origin,Block.bBlockingHit?Block.ImpactNormal:-Direction);return;
    }
    SetActorLocationAndRotation(Origin,Direction.Rotation(),false,nullptr,ETeleportType::TeleportPhysics);
    Phase=EMCRocketSkillPhase::Flying;FlightEndsAt=Now()+FlightSeconds();MarkPresentationReleased();
    Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    if(Hero()) Body->IgnoreActorWhenMoving(Hero(),true);
    Movement->SetUpdatedComponent(Body);
    Movement->Velocity=Direction*FMath::Clamp(Param(TEXT("ProjectileSpeed"),2200.f),200.f,6000.f);
    Movement->Activate(true);OnRep_Phase();
    PlaySkillCue(TEXT("Start"),Origin,Direction,FMath::Clamp(Param(TEXT("MuzzleVFXRadius"),65.f),10.f,300.f));StartLoop();ForceNetUpdate();
}

void AMCRocketSkillAction::OnImpact(UPrimitiveComponent*,AActor*,UPrimitiveComponent*,FVector,const FHitResult& Hit)
{
    if(HasAuthority() && Phase==EMCRocketSkillPhase::Flying)
        Detonate(Hit.bBlockingHit?FVector(Hit.ImpactPoint):GetActorLocation(),Hit.ImpactNormal);
}

void AMCRocketSkillAction::OnProjectileStopped(const FHitResult& Hit)
{
    if(HasAuthority() && Phase==EMCRocketSkillPhase::Flying)
        Detonate(Hit.bBlockingHit?FVector(Hit.ImpactPoint):GetActorLocation(),Hit.ImpactNormal);
}

void AMCRocketSkillAction::Detonate(FVector Point,FVector Normal)
{
    if(!HasAuthority() || !IsCommitted() || IsFinished() || DetonationCount!=0 || Point.ContainsNaN()) return;
    DetonationCount=1;Phase=EMCRocketSkillPhase::Detonated;MarkPresentationReleased();
    FinishAt=Now()+FMath::Max(.35f,FMath::Clamp(LauncherRecoverySeconds,.15f,1.2f)-GetPresentationReleaseAge());
    Movement->StopMovementImmediately();Movement->Deactivate();Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    StopLoop();OnRep_Phase();
    Normal=Normal.GetSafeNormal(SMALL_NUMBER,FVector::UpVector);
    FMCActiveSkillBlastSettings Blast;
    Blast.Radius=Param(TEXT("BlastRadius"),400.f);Blast.FoodDamage=Param(TEXT("FoodDamage"),140.f);
    Blast.EnemyDamage=Param(TEXT("EnemyDamage"),100.f);Blast.PlayerDamage=Param(TEXT("PlayerDamage"),40.f);
    Blast.PushSpeed=Param(TEXT("PushSpeed"),650.f);Blast.ArenaDamage=Param(TEXT("ArenaDamage"),120.f);
    Blast.bDamageArenaTeeth=true;
    PlaySkillCue(TEXT("Impact"),Point,Normal,Blast.Radius);
    MCActiveSkillImpact::ApplyRadial(this,SourceState(),Point+Normal*2.f,Blast);ForceNetUpdate();
}

void AMCRocketSkillAction::Cancel(EMCActiveSkillEndReason Reason)
{
    if(!HasAuthority() || IsFinished()) return;
    if(!IsCommitted() || Reason==EMCActiveSkillEndReason::RunReset || Reason==EMCActiveSkillEndReason::WorldEnded) {Super::Cancel(Reason);return;}
    if(Phase==EMCRocketSkillPhase::Windup) Launch(false);
    if(Reason==EMCActiveSkillEndReason::TimedOut) Detonate(GetActorLocation(),-GetActorForwardVector());
    ReleasePresentation(); // An accepted projectile survives ordinary input/pawn/slot loss.
    UpdateHeldLauncher();
}

void AMCRocketSkillAction::Tick(float Dt)
{
    Super::Tick(Dt);
    if(!IsCommitted() || IsFinished()) return;
    UpdateHeldLauncher();
    if(!HasAuthority()) return;
    if(GetPresentationReleaseAge()>=FMath::Clamp(LauncherRecoverySeconds,.15f,1.2f)) ReleasePresentation();
    if(Phase==EMCRocketSkillPhase::Windup && Now()>=LaunchAt) Launch(true);
    else if(Phase==EMCRocketSkillPhase::Flying && Now()>=FlightEndsAt) Detonate(GetActorLocation(),-GetActorForwardVector());
    else if(Phase==EMCRocketSkillPhase::Detonated && Now()>=FinishAt) Finish();
}

void AMCRocketSkillAction::OnRep_Phase()
{
    UpdateHeldLauncher();
    ProjectileMesh->SetVisibility(Phase==EMCRocketSkillPhase::Flying,true);
    if(!HasAuthority()) {Movement->Deactivate();Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);}
}

void AMCRocketSkillAction::OnFinished(EMCActiveSkillEndReason Reason)
{
    Movement->Deactivate();Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);Super::OnFinished(Reason);
}

void AMCRocketSkillAction::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCRocketSkillAction,Phase);DOREPLIFETIME(AMCRocketSkillAction,FlightEndsAt);DOREPLIFETIME(AMCRocketSkillAction,DetonationCount);
    DOREPLIFETIME(AMCRocketSkillAction,InitialDrawPropActorLocal);DOREPLIFETIME(AMCRocketSkillAction,bHasDrawStart);
}
