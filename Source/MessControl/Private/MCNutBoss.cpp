#include "MCNutBoss.h"
#include "MCNutBossAnimInstance.h"

#include "MCNutCombatEffect.h"
#include "MCNutSpellProjectile.h"
#include "MCNutRainEvent.h"
#include "MCGameState.h"
#include "MCArenaTooth.h"
#include "MCReactionVFX.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

namespace
{
FVector HeightScale(const UStaticMesh* Mesh,float Height)
{
    return Mesh?FVector(Height/FMath::Max(1.,Mesh->GetBounds().BoxExtent.Z*2)):FVector::OneVector;
}
void SetCenteredMesh(UStaticMeshComponent* Component,UStaticMesh* Mesh,float Height,FVector Offset,FRotator Rotation)
{
    if(!Mesh) { Component->SetVisibility(false); return; }
    const FVector Scale=HeightScale(Mesh,Height);
    Component->SetStaticMesh(Mesh); Component->SetRelativeScale3D(Scale);
    Component->SetRelativeRotation(Rotation);
    Component->SetRelativeLocation(Offset-Rotation.RotateVector(Mesh->GetBounds().Origin*Scale));
}
}

AMCNutBoss::AMCNutBoss()
{
    PrimaryActorTick.TickInterval=1.f/30.f;
    Shield=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shield")); Shield->SetupAttachment(Body);
    Kernel=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Kernel")); Kernel->SetupAttachment(Body);
    OpenShell=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OpenShell")); OpenShell->SetupAttachment(Body);
    TankModel=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("TankModel")); TankModel->SetupAttachment(Body);
    TankModel->SetCollisionEnabled(ECollisionEnabled::NoCollision); TankModel->SetCanEverAffectNavigation(false);
    TankModel->SetVisibility(false);
    TankModel->bEnableUpdateRateOptimizations=false;
    MageModel=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("MageModel")); MageModel->SetupAttachment(Body);
    MageModel->SetCollisionEnabled(ECollisionEnabled::NoCollision); MageModel->SetCanEverAffectNavigation(false);
    MageModel->SetVisibility(false); MageModel->bEnableUpdateRateOptimizations=false;
    TankBall=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TankBall")); TankBall->SetupAttachment(Body);
    TankBall->SetCollisionEnabled(ECollisionEnabled::NoCollision); TankBall->SetCanEverAffectNavigation(false);
    TankBall->SetVisibility(false);
    for(auto* Component:{Shield.Get(),Kernel.Get(),OpenShell.Get()}) {
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision); Component->SetCanEverAffectNavigation(false);
    }
    Label->SetWorldSize(24);
}

double AMCNutBoss::Now() const
{
    const auto* Game=GetWorld()?GetWorld()->GetGameState():nullptr;
    return Game?Game->GetServerWorldTimeSeconds():GetWorld()?GetWorld()->GetTimeSeconds():0;
}

void AMCNutBoss::ConfigureEncounter(AMCTongue* OnTongue,AMCNutRainEvent* OwnerEvent,EMCNutBossRole InRole,
                                    const FMCNutBossSettings& InSettings,int32 Players,int32 Seed)
{
    if(!HasAuthority() || !IsValid(OnTongue)) return;
    Tongue=OnTongue; EncounterOwner=OwnerEvent; if(OwnerEvent) SetOwner(OwnerEvent);
    BossRole=InRole; BossSettings=InSettings; BossSettings.Sanitize(); PartyPlayers=FMath::Clamp(Players,1,8);
    Random.Initialize(int32(uint32(Seed)^(BossRole==EMCNutBossRole::Tank?0x54414e4bu:0x4d414745u)));
    Settings=FMCNutEnemySettings(); Settings.BodyRadius=BossSettings.BodyRadiusForRole(BossRole);
    Settings.MoveSpeed=BossRole==EMCNutBossRole::Tank?BossSettings.TankMoveSpeed:BossSettings.MageMoveSpeed;
    Settings.AttackDamage=BossSettings.MeleeDamage; Settings.AttackRange=BossSettings.MeleeRange;
    Settings.WindupSeconds=BossSettings.MeleeWindup; Settings.HopHeight=BossRole==EMCNutBossRole::Tank?4:9;
    Health=BossSettings.HealthForPlayers(BossRole,PartyPlayers); Settings.MaxHealth=Health; bDefeated=false;
    NutMesh=(BossRole==EMCNutBossRole::Tank?BossSettings.WholeMesh:BossSettings.KernelMesh).LoadSynchronous();
    MeshScale=HeightScale(NutMesh,BossRole==EMCNutBossRole::Tank?BossSettings.TankHeight:BossSettings.MageHeight*.8f);
    EntranceLanding=GetActorLocation(); LockedStart=EntranceLanding+FVector(0,0,BossSettings.EntranceHeight);
    LockedTarget=EntranceLanding; State=EMCNutBossState::Falling; Attack=EMCNutBossAttack::None;
    StateStartedAt=Now(); ResolveAt=StateStartedAt+BossSettings.EntranceSeconds; AttackEndAt=ResolveAt;
    SetActorLocation(LockedStart); RefreshPresentation(); ForceNetUpdate();
}

void AMCNutBoss::BeginPlay()
{
    Super::BeginPlay(); BossSettings.Sanitize(); Settings.MaxHealth=Health;
    TankModel->AddTickPrerequisiteActor(this);
    MageModel->AddTickPrerequisiteActor(this);
    if(GetNetMode()==NM_DedicatedServer) {TankModel->SetComponentTickEnabled(false);MageModel->SetComponentTickEnabled(false);}
    if(HasAuthority()) {
        if(State==EMCNutBossState::Falling) SetActorLocation(LockedStart,false);
        const double Ready=ResolveAt;
        NextTargetAt=Ready; NextMeleeAt=Ready+1.5; NextChargeAt=Ready+3; NextJumpAt=Ready+7; NextRollAt=Ready+11;
        NextFireballAt=Ready+6; NextSummonAt=Ready+10; NextRainAt=Ready+13; NextTeleportAt=Ready+4;
        TrackAttackActor(AMCNutCombatEffect::Spawn(this,Tongue,EMCNutCombatCue::JumpTell,
            EntranceLanding,EntranceLanding,Settings.BodyRadius+35,BossSettings.EntranceSeconds,.2f,Random.RandRange(1,MAX_int32)));
    }
    RefreshPresentation();
}

void AMCNutBoss::RefreshBossPresentation() { RefreshPresentation(); }

void AMCNutBoss::RefreshPresentation()
{
    Super::RefreshPresentation();
    if(!Shield || !Kernel || !OpenShell) return;
    auto* TankMesh=BossRole==EMCNutBossRole::Tank?BossSettings.TankSkeletalMesh.LoadSynchronous():nullptr;
    if(BossRole==EMCNutBossRole::Tank && !TankMesh) {
        if(!bFallbackTankMeshLoaded) {
            bFallbackTankMeshLoaded=true;
            FallbackTankMesh=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/FromBlender8/SM_Nut_Tank.SM_Nut_Tank"));
        }
        TankMesh=FallbackTankMesh.Get();
    }
    if(TankModel->GetSkeletalMeshAsset()!=TankMesh) TankModel->SetSkeletalMeshAsset(TankMesh);
    if(TankMesh) {
        const auto Bounds=TankMesh->GetBounds();
        TankModelScale=FVector(BossSettings.TankHeight/FMath::Max(1.,Bounds.BoxExtent.Z*2));
        TankModelCenter=Bounds.Origin;
    }
    const bool HasTankModel=BossRole==EMCNutBossRole::Tank && TankMesh;
    auto* BallMesh=BossRole==EMCNutBossRole::Tank?BossSettings.TankBallMesh.LoadSynchronous():nullptr;
    if(TankBall->GetStaticMesh()!=BallMesh) TankBall->SetStaticMesh(BallMesh);
    if(BallMesh) {
        TankBallScale=HeightScale(BallMesh,BossSettings.TankBallHeight);
        TankBallCenter=BallMesh->GetBounds().Origin;
    }
    auto* MageMesh=BossRole==EMCNutBossRole::Mage?BossSettings.MageSkeletalMesh.LoadSynchronous():nullptr;
    if(MageModel->GetSkeletalMeshAsset()!=MageMesh) MageModel->SetSkeletalMeshAsset(MageMesh);
    if(MageMesh) {
        const auto Bounds=MageMesh->GetBounds(); MageModelCenter=Bounds.Origin;
        MageModelScale=FVector(BossSettings.MageHeight/FMath::Max(1.,Bounds.BoxExtent.Z*2));
    }
    CacheBossAnimations();
    for(auto* Model:{TankModel.Get(),MageModel.Get()})
        if(Model->GetSkeletalMeshAsset() && GetNetMode()!=NM_DedicatedServer
            && Model->GetAnimClass()!=UMCNutBossAnimInstance::StaticClass()) Model->SetAnimInstanceClass(UMCNutBossAnimInstance::StaticClass());
    const bool HasAuthoredModel=HasTankModel || (BossRole==EMCNutBossRole::Mage && MageMesh);
    // Keep the skeletal asset's Walnut/Material_001 slots and artist materials.
    LeftEye->SetVisibility(!bDefeated && !HasAuthoredModel); RightEye->SetVisibility(!bDefeated && !HasAuthoredModel);
    auto* Shell=BossSettings.ShellMesh.LoadSynchronous();
    SetCenteredMesh(Shield,Shell,BossSettings.TankHeight*.65f,FVector(Settings.BodyRadius*.88f,0,-5),FRotator(0,90,0));
    Shield->SetVisibility(!HasTankModel && !bDefeated && BossRole==EMCNutBossRole::Tank && State!=EMCNutBossState::Falling
        && State!=EMCNutBossState::Recovery && !(State==EMCNutBossState::Executing
            && (Attack==EMCNutBossAttack::Charge || Attack==EMCNutBossAttack::Jump || Attack==EMCNutBossAttack::Roll)));
    SetCenteredMesh(OpenShell,Shell,BossSettings.MageHeight,FVector(-Settings.BodyRadius*.38f,0,-12),FRotator(0,-20,-22));
    OpenShell->SetVisibility(!bDefeated && BossRole==EMCNutBossRole::Mage && !MageMesh);
    Kernel->SetVisibility(false); // the inherited main mesh is the exposed kernel for the mage
    Label->SetText(FText::FromString(FString::Printf(TEXT("%s  %d"),BossRole==EMCNutBossRole::Tank?TEXT("NUT TANK"):TEXT("NUT MAGE"),FMath::CeilToInt(Health))));
    Label->SetTextRenderColor(BossRole==EMCNutBossRole::Tank?FColor(240,185,85):FColor(225,115,255));
    Label->SetRelativeLocation(FVector(0,0,(BossRole==EMCNutBossRole::Tank?BossSettings.TankHeight:BossSettings.MageHeight)*.5f+38));
    PresentTankModel(Now());
}

void AMCNutBoss::CacheBossAnimations()
{
    if(GetNetMode()==NM_DedicatedServer) return;
    constexpr int32 Count=int32(EMCNutBossClip::Count);
    TSoftObjectPtr<UAnimSequence> Clips[Count];
    auto Set=[&](EMCNutBossClip Slot,const TSoftObjectPtr<UAnimSequence>& Asset){Clips[int32(Slot)]=Asset;};
    if(BossRole==EMCNutBossRole::Tank) {
        Set(EMCNutBossClip::Idle,BossSettings.TankIdleAnimation); Set(EMCNutBossClip::Walk,BossSettings.TankWalkAnimation);
        Set(EMCNutBossClip::WalkLeft,BossSettings.TankWalkLeftAnimation); Set(EMCNutBossClip::WalkRight,BossSettings.TankWalkRightAnimation);
        Set(EMCNutBossClip::Melee,BossSettings.TankMeleeAnimation); Set(EMCNutBossClip::Jump,BossSettings.TankJumpAnimation);
        Set(EMCNutBossClip::Transform,BossSettings.TankTransformAnimation);
        Set(EMCNutBossClip::ChargeTell,BossSettings.TankChargeTellAnimation);
        Set(EMCNutBossClip::ChargeLoop,BossSettings.TankChargeLoopAnimation);
        Set(EMCNutBossClip::ChargeRecovery,BossSettings.TankChargeRecoveryAnimation);
    } else {
        Set(EMCNutBossClip::Idle,BossSettings.MageIdleAnimation); Set(EMCNutBossClip::Walk,BossSettings.MageWalkAnimation);
        Set(EMCNutBossClip::Cast,BossSettings.MageCastAnimation); Set(EMCNutBossClip::HeavyCast,BossSettings.MageHeavyCastAnimation);
        Set(EMCNutBossClip::Melee,BossSettings.MageMeleeAnimation);
        Set(EMCNutBossClip::Summon,BossSettings.MageSummonAnimation); Set(EMCNutBossClip::Rain,BossSettings.MageRainAnimation);
        Set(EMCNutBossClip::Hit,BossSettings.MageHitAnimation); Set(EMCNutBossClip::Death,BossSettings.MageDeathAnimation);
    }
    if(CachedAnimationRole!=BossRole) {CachedBossAnimationPaths.Reset();BossAnimations.Reset();CachedAnimationRole=BossRole;}
    BossAnimations.SetNum(Count); CachedBossAnimationPaths.SetNum(Count);
    for(int32 Index=0;Index<Count;++Index) {
        const FSoftObjectPath Path=Clips[Index].ToSoftObjectPath();
        if(Path==CachedBossAnimationPaths[Index]) continue;
        CachedBossAnimationPaths[Index]=Path; BossAnimations[Index]=Clips[Index].LoadSynchronous();
    }
}

FVector AMCNutBoss::GetCastOrigin() const
{
    FVector Forward=(LockedTarget-GetActorLocation()).GetSafeNormal2D();
    if(Forward.IsNearlyZero()) Forward=GetActorForwardVector();
    const FVector Right=FVector::CrossProduct(FVector::UpVector,Forward);
    const FVector Offset=BossSettings.MageCastOffset;
    return GetActorLocation()+Forward*Offset.X+Right*Offset.Y+FVector::UpVector*Offset.Z;
}

void AMCNutBoss::BuildAnimationSnapshot(const USkeletalMeshComponent* Model,FMCNutBossAnimationSnapshot& Out) const
{
    if(!Model || !Model->GetSkeletalMeshAsset()) return;
    const double Time=Now();
    auto Progress=[](double Age,double Seconds){return FMath::Clamp(float(Age/FMath::Max(.01,Seconds)),0.f,1.f);};
    Out.ServerTime=Time; Out.bMage=BossRole==EMCNutBossRole::Mage;
    Out.BlendSeconds=BossSettings.AnimationBlendSeconds; Out.VisualKey=int32(BossRole)*100+int32(State)*10+int32(Attack);
    const FName Names[]={BossSettings.TorsoBone,BossSettings.HeadBone,BossSettings.LeftArmBone,BossSettings.RightArmBone,
        BossSettings.LeftForearmBone,BossSettings.RightForearmBone,BossSettings.LeftHandBone,BossSettings.RightHandBone};
    for(int32 Index=0;Index<int32(EMCNutPoseBone::Count);++Index) {
        Out.Bones[Index]=Model->GetBoneIndex(Names[Index]);
        if(Out.Bones[Index]==INDEX_NONE) Out.Bones[Index]=Model->GetBoneIndex(FName(*Names[Index].ToString().Replace(TEXT("."),TEXT("_"))));
    }
    const FVector LocalMotion=GetActorTransform().InverseTransformVectorNoScale(TankPresentationMotion);
    Out.ForwardSpeed=FMath::Clamp(float(LocalMotion.X)/FMath::Max(1.f,Settings.MoveSpeed),-1.f,1.f);
    Out.SideSpeed=FMath::Clamp(float(LocalMotion.Y)/FMath::Max(1.f,Settings.MoveSpeed),-1.f,1.f);
    const float Moving=FMath::SmoothStep(10.f,65.f,float(TankPresentationMotion.Size2D()));
    const float Side=FMath::Clamp(Out.SideSpeed,-1.f,1.f);
    FVector AimPoint=LockedTarget;
    if(State==EMCNutBossState::Idle && IsLiveTarget(Target)) AimPoint=Target->GetActorLocation();
    const FVector AimLocal=GetActorTransform().InverseTransformVectorNoScale(AimPoint-GetCastOrigin());
    const FRotator Aim=AimLocal.Rotation();
    Out.AimYaw=FMath::Clamp(FRotator::NormalizeAxis(Aim.Yaw),-55.f,55.f); Out.AimPitch=FMath::Clamp(Aim.Pitch,-35.f,40.f);
    Out.CastEmitter=Model->GetComponentTransform().InverseTransformPosition(GetCastOrigin());
    Out.ActorToComponent=(Model->GetComponentQuat().Inverse()*GetActorQuat()).GetNormalized();
    if(Model->DoesSocketExist(BossSettings.MageCastSocket)
        && Model->GetBoneIndex(Model->GetSocketBoneName(BossSettings.MageCastSocket))==Out.Bones[int32(EMCNutPoseBone::RightHand)])
        Out.HandTipOffset=Model->GetSocketTransform(BossSettings.MageCastSocket,RTS_ParentBoneSpace).GetLocation();
    const float HitAge=float(Time-VisualHitAt),ShieldAge=float(Time-ShieldHitAt);
    Out.Hit=HitAge>=0 && HitAge<.4f?FMath::Sin(HitAge/.4f*PI)*(1-HitAge/.4f):0;
    Out.ShieldHit=ShieldAge>=0 && ShieldAge<.32f?FMath::Sin(ShieldAge/.32f*PI):0;
    Out.HitDirection=GetActorTransform().InverseTransformVectorNoScale(VisualHitDirection);
    Out.Death=bDefeated?Progress(Time-StateStartedAt,BossSettings.DeathSeconds):0;
    Out.Guard=!Out.bMage && !bDefeated && State==EMCNutBossState::Idle?1.f:0.f;
    const bool Active=State==EMCNutBossState::Telegraph || State==EMCNutBossState::Executing || State==EMCNutBossState::Recovery;
    const bool Telegraph=State==EMCNutBossState::Telegraph,Recovery=State==EMCNutBossState::Recovery;
    const float RecoveryProgress=Recovery?Progress(Time-StateStartedAt,AttackEndAt-StateStartedAt):0;
    float Windup=BossSettings.MeleeWindup;
    if(Attack==EMCNutBossAttack::Jump) Windup=BossSettings.JumpWindup;
    else if(Attack==EMCNutBossAttack::Roll) Windup=BossSettings.RollWindup;
    else if(Attack==EMCNutBossAttack::Charge) Windup=BossSettings.ChargeWindup;
    else if(Attack==EMCNutBossAttack::Fireball) Windup=BossSettings.FireballWindup;
    else if(Attack==EMCNutBossAttack::Summon) Windup=1.1f;
    else if(Attack==EMCNutBossAttack::NutRain) Windup=BossSettings.RainWindup;
    else if(Attack==EMCNutBossAttack::Teleport) Windup=BossSettings.MageTeleportWindup;
    const float WindupProgress=Progress(Time-(ResolveAt-Windup),Windup);
    auto Has=[&](EMCNutBossClip Slot){return BossAnimations.IsValidIndex(int32(Slot)) && BossAnimations[int32(Slot)]
        && BossAnimations[int32(Slot)]->GetSkeleton()==Model->GetSkeletalMeshAsset()->GetSkeleton()
        && BossAnimations[int32(Slot)]->GetPlayLength()>KINDA_SMALL_NUMBER;};
    const bool MageMelee=Out.bMage && Attack==EMCNutBossAttack::Melee;
    Out.bHasMeleeClip=Has(EMCNutBossClip::Melee);
    float AttackWeight=Active?1.f:0.f,Phase=0;
    bool AttackLoop=false;
    float AttackRate=1.f;
    double AttackLoopStartedAt=0;
    EMCNutBossClip AttackClip=EMCNutBossClip::Idle;
    if(Telegraph) AttackWeight=FMath::SmoothStep(0.f,BossSettings.AnimationBlendSeconds,float(Time-StateStartedAt));
    if(Recovery) AttackWeight=1-FMath::SmoothStep(.65f,1.f,RecoveryProgress);
    if(Active && Attack==EMCNutBossAttack::Roll) {
        AttackClip=EMCNutBossClip::Transform; Phase=Telegraph?WindupProgress:Recovery?1-RecoveryProgress:1;
    } else if(Active && Attack==EMCNutBossAttack::Jump) {
        AttackClip=EMCNutBossClip::Jump;
        Phase=Telegraph?BossSettings.TankJumpTakeoffFraction*WindupProgress:Recovery?
            FMath::Lerp(BossSettings.TankJumpImpactFraction,1.f,RecoveryProgress):
            FMath::Lerp(BossSettings.TankJumpTakeoffFraction,BossSettings.TankJumpImpactFraction,Progress(Time-ResolveAt,BossSettings.JumpFlightSeconds));
        Out.Airborne=State==EMCNutBossState::Executing?1.f:0.f;
    } else if(Active && Attack==EMCNutBossAttack::Melee && !Out.bMage) {
        AttackClip=EMCNutBossClip::Melee;
        Phase=Telegraph?BossSettings.TankMeleeImpactFraction*WindupProgress:
            FMath::Lerp(BossSettings.TankMeleeImpactFraction,1.f,RecoveryProgress);
    } else if(Active && Attack==EMCNutBossAttack::Charge) {
        AttackClip=Telegraph?EMCNutBossClip::ChargeTell:Recovery?EMCNutBossClip::ChargeRecovery:EMCNutBossClip::ChargeLoop;
        Phase=Telegraph?WindupProgress:RecoveryProgress;
        if(Has(AttackClip)) {
            AttackLoop=State==EMCNutBossState::Executing;
            AttackRate=BossSettings.TankChargeLoopPlayRate;
            // Tell ends in the loop's first contact pose, so each charge starts at frame one.
            AttackLoopStartedAt=StateStartedAt;
        } else {
            AttackClip=EMCNutBossClip::Walk; AttackLoop=true; AttackRate=2.f;
        }
    } else if(Active && Out.bMage) {
        const bool Teleporting=Attack==EMCNutBossAttack::Teleport;
        AttackClip=Teleporting?EMCNutBossClip::HeavyCast:MageMelee?EMCNutBossClip::Melee:Attack==EMCNutBossAttack::Summon?EMCNutBossClip::Summon:
            Attack==EMCNutBossAttack::NutRain?EMCNutBossClip::Rain:EMCNutBossClip::Cast;
        const float Release=MageMelee?BossSettings.MageMeleeImpactFraction:BossSettings.MageCastReleaseFraction;
        // The shove is authored directly; casting aim, palm IK and spell release overlays would erase it.
        Out.Cast=MageMelee || Teleporting?0.f:AttackWeight; Out.CastProgress=WindupProgress;
        const float SinceRelease=float(Time-ResolveAt);
        Out.Release=!MageMelee && !Teleporting && SinceRelease>=0 && SinceRelease<.22f?FMath::Sin(SinceRelease/.22f*PI):0;
        Out.Channel=Attack==EMCNutBossAttack::NutRain && State==EMCNutBossState::Executing?1.f:0.f;
        // A channel sustains an authored mid-cast section for its complete server duration.
        // It never reaches the final clip frame merely because the FBX is shorter than the spell.
        Phase=Telegraph?Release*WindupProgress:Out.Channel>.5f?
            Release+.07f*(.5f+.5f*FMath::Sin(SinceRelease*3.5f)):
            FMath::Lerp(Release,1.f,RecoveryProgress);
        if(MageMelee) {
            Out.Melee=AttackWeight;
            Out.MeleePhase=Telegraph?.55f*WindupProgress:FMath::Lerp(.55f,1.f,RecoveryProgress);
        }
    } else if(State==EMCNutBossState::Falling) {
        AttackClip=Out.bMage?EMCNutBossClip::Idle:EMCNutBossClip::Jump; AttackWeight=1;
        Phase=FMath::Lerp(BossSettings.TankJumpTakeoffFraction,BossSettings.TankJumpImpactFraction,Progress(Time-StateStartedAt,BossSettings.EntranceSeconds));
        Out.Airborne=Out.bMage?0.f:1.f;
    }
    if(!Has(AttackClip)) {
        AttackClip=MageMelee?EMCNutBossClip::Idle:
            Out.bMage && Has(EMCNutBossClip::HeavyCast)?EMCNutBossClip::HeavyCast:EMCNutBossClip::Idle;
        if(!Active) AttackWeight=0;
    }
    auto Add=[&](EMCNutBossClip Slot,float Fraction,float Weight,bool Loop=false,float Rate=1.f,double LoopStartedAt=0) {
        if(Weight<=.001f || !Has(Slot)) return;
        UAnimSequence* Clip=BossAnimations[int32(Slot)]; const float Length=Clip->GetPlayLength();
        if(Length<=KINDA_SMALL_NUMBER) return;
        FMCNutBossClipSample Sample; Sample.Clip=Clip;Sample.Weight=Weight;Sample.bLoop=Loop;
        Sample.Seconds=Loop?float(FMath::Fmod(FMath::Max(0.,Time-LoopStartedAt)*Rate,double(Length))):FMath::Clamp(Fraction,0.f,1.f)*Length;
        Out.Samples.Add(Sample);
    };
    const float Living=bDefeated?0.f:1.f,Locomotion=Living*(1-AttackWeight);
    Add(EMCNutBossClip::Idle,0,Locomotion*(1-Moving),true);
    if(Out.bMage) Add(EMCNutBossClip::Walk,0,Locomotion*Moving,true);
    else {
        Add(EMCNutBossClip::Walk,0,Locomotion*Moving*(1-FMath::Abs(Side)),true);
        Add(EMCNutBossClip::WalkLeft,0,Locomotion*Moving*FMath::Max(0.f,-Side),true);
        Add(EMCNutBossClip::WalkRight,0,Locomotion*Moving*FMath::Max(0.f,Side),true);
    }
    Add(AttackClip,Phase,Living*AttackWeight,AttackLoop,AttackRate,AttackLoopStartedAt);
    if(Out.bMage && Out.Hit>.01f) Add(EMCNutBossClip::Hit,Progress(HitAge,.4),Out.Hit*.3f);
    Out.bHasDeathClip=Has(EMCNutBossClip::Death);
    if(bDefeated) Add(Out.bHasDeathClip?EMCNutBossClip::Death:EMCNutBossClip::Idle,Out.Death,1.f);
    if(Out.Samples.IsEmpty()) Add(EMCNutBossClip::Idle,0,1.f,true);
}

void AMCNutBoss::PresentTankModel(double Time)
{
    const FVector Position=GetActorLocation();
    const double Dt=Time-LastTankPresentationAt;
    if(LastTankPresentationAt>=0 && Dt>.001 && Dt<.5) {
        FVector Motion=(Position-LastTankPresentationLocation)/Dt; Motion.Z=0;
        if(Motion.SizeSquared2D()>FMath::Square(2000.f)) Motion=FVector::ZeroVector;
        TankPresentationMotion=FMath::VInterpTo(TankPresentationMotion,Motion,float(Dt),10);
    }
    if(Dt>.001 || LastTankPresentationAt<0) {LastTankPresentationAt=Time;LastTankPresentationLocation=Position;}
    const float DeathAge=bDefeated?FMath::Max(0.f,float(Time-StateStartedAt)):0;
    const float DeathScale=bDefeated?1-FMath::SmoothStep(BossSettings.DeathSeconds-.3f,BossSettings.DeathSeconds,DeathAge):1.f;
    const bool HasMage=BossRole==EMCNutBossRole::Mage && MageModel->GetSkeletalMeshAsset();
    MageModel->SetVisibility(HasMage);
    if(BossRole==EMCNutBossRole::Mage) Visual->SetVisibility(!HasMage);
    if(HasMage) {
        const FVector Scale=MageModelScale*DeathScale;
        const FRotator ModelRotation(0,BossSettings.MageModelYaw,0);
        MageModel->SetRelativeScale3D(Scale); MageModel->SetRelativeLocation(-ModelRotation.RotateVector(MageModelCenter*Scale)+FVector(0,0,BossSettings.MageHeight*.5f-Settings.BodyRadius));
        MageModel->SetRelativeRotation(ModelRotation); Visual->SetVisibility(false);
    }
    if(BossRole!=EMCNutBossRole::Tank) {TankModel->SetVisibility(false);TankBall->SetVisibility(false);return;}
    const bool Rolling=Attack==EMCNutBossAttack::Roll && State==EMCNutBossState::Executing && !bDefeated;
    const bool Winding=Attack==EMCNutBossAttack::Roll && State==EMCNutBossState::Telegraph && !bDefeated;
    const bool Returning=Attack==EMCNutBossAttack::Roll && State==EMCNutBossState::Recovery && !bDefeated;
    float BallWeight=Rolling?1.f:Winding?FMath::SmoothStep(float(ResolveAt-.18),float(ResolveAt),float(Time)):
        Returning?1-FMath::SmoothStep(0.f,.18f,float(Time-StateStartedAt)):0;
    if(!bDefeated) bTankWasBall=BallWeight>.5f;
    else BallWeight=bTankWasBall?1.f:0.f;
    if(!TankBall->GetStaticMesh()) BallWeight=0;
    const bool HasWarrior=TankModel->GetSkeletalMeshAsset()!=nullptr;
    TankBall->SetVisibility(BallWeight>.001f); TankModel->SetVisibility(HasWarrior && BallWeight<.999f);
    Visual->SetVisibility(!HasWarrior && BallWeight<.999f);
    const float RollAge=FMath::Clamp(float(Time-ResolveAt),0.f,BossSettings.RollDuration);
    const float Pitch=Rolling || Returning?FMath::Fmod(RollAge*BossSettings.RollSpeed/FMath::Max(1.f,Settings.BodyRadius)*180.f/PI,360.f):0;
    const FRotator Rotation(Pitch,0,0);
    if(BallWeight>.001f) {
        const FVector Scale=TankBallScale*(BallWeight*DeathScale);
        TankBall->SetRelativeScale3D(Scale); TankBall->SetRelativeRotation(Rotation);
        TankBall->SetRelativeLocation(-Rotation.RotateVector(TankBallCenter*Scale));
    }
    if(HasWarrior) {
        const FVector Scale=TankModelScale*((1-BallWeight)*DeathScale);
        const FRotator ModelRotation(0,BossSettings.TankModelYaw,0);
        TankModel->SetRelativeScale3D(Scale); TankModel->SetRelativeRotation(ModelRotation);
        TankModel->SetRelativeLocation(-ModelRotation.RotateVector(TankModelCenter*Scale)+FVector(0,0,BossSettings.TankHeight*.5f-Settings.BodyRadius));
    } else if(NutMesh) {
        Visual->SetRelativeRotation(Rotation);
        Visual->SetRelativeLocation(-Rotation.RotateVector(NutMesh->GetBounds().Origin*MeshScale*DeathScale));
    }
}

bool AMCNutBoss::IsShieldProtectingFrom(FVector SourcePoint) const
{
    if(BossRole!=EMCNutBossRole::Tank || !CanReceiveToolHit() || State==EMCNutBossState::Recovery
        || (State==EMCNutBossState::Executing
            && (Attack==EMCNutBossAttack::Charge || Attack==EMCNutBossAttack::Jump || Attack==EMCNutBossAttack::Roll))) return false;
    const FVector Towards=(SourcePoint-GetActorLocation()).GetSafeNormal2D();
    return !Towards.IsNearlyZero() && FVector::DotProduct(Towards,GetActorForwardVector())>=FMath::Cos(FMath::DegreesToRadians(BossSettings.ShieldHalfAngle));
}

float AMCNutBoss::ReceiveToolDamage(float Damage,AMCToothCharacter* Source)
{
    if(!HasAuthority() || !CanReceiveToolHit() || !FMath::IsFinite(Damage) || Damage<=0
        || (Source && (Source->GetWorld()!=GetWorld() || !IsLiveTarget(Source)))) return 0;
    const bool Shielded=Source && IsShieldProtectingFrom(Source->GetActorLocation());
    VisualHitAt=Now(); VisualHitDirection=Source?(Source->GetActorLocation()-GetActorLocation()).GetSafeNormal2D():-GetActorForwardVector();
    if(Shielded) {
        Damage*=BossSettings.ShieldFrontDamageScale;
        if(VisualHitAt-ShieldHitAt>=.15) {
            ShieldHitAt=VisualHitAt;
            TrackAttackActor(AMCNutCombatEffect::Spawn(this,Tongue,EMCNutCombatCue::ShieldHit,
                GetActorLocation()+VisualHitDirection*Settings.BodyRadius,GetActorLocation(),Settings.BodyRadius,0,.32f,AttackSeed));
        }
    }
    // A light tool hit registers through the ordinary contact path, without
    // cancelling an entire boss attack each time four workers swing together.
    const float Before=Health;
    const float Applied=Super::ReceiveToolDamage(Damage,Source);
    if(Applied>0) {
        AMCReactionVFX::SpawnHit(this,GetToolTargetPoint(Source?Source->GetActorLocation():GetActorLocation()+VisualHitDirection*Settings.BodyRadius),
            -VisualHitDirection,Before-Health,Shielded);
        if(BossRole==EMCNutBossRole::Mage && IsValid(Source)) {
            if(VisualHitAt-FocusWindowStartedAt>2) {FocusWindowStartedAt=VisualHitAt;FocusHits=0;}
            ++FocusHits;
        }
    }
    return Applied;
}

float AMCNutBoss::GetAttackCooldownRemaining(EMCNutBossAttack Ability) const
{
    double ReadyAt=0;
    switch(Ability) {
    case EMCNutBossAttack::Melee: ReadyAt=NextMeleeAt;break;
    case EMCNutBossAttack::Charge: ReadyAt=NextChargeAt;break;
    case EMCNutBossAttack::Jump: ReadyAt=NextJumpAt;break;
    case EMCNutBossAttack::Roll: ReadyAt=NextRollAt;break;
    case EMCNutBossAttack::Fireball: ReadyAt=NextFireballAt;break;
    case EMCNutBossAttack::Summon: ReadyAt=NextSummonAt;break;
    case EMCNutBossAttack::NutRain: ReadyAt=NextRainAt;break;
    case EMCNutBossAttack::Teleport: ReadyAt=NextTeleportAt;break;
    default: break;
    }
    return FMath::Max(0.f,float(ReadyAt-Now()));
}

void AMCNutBoss::Enter(EMCNutBossState Next,float Seconds)
{
    State=Next; StateStartedAt=Now(); AttackEndAt=StateStartedAt+FMath::Max(0.f,Seconds);
    RefreshPresentation(); ForceNetUpdate();
}

void AMCNutBoss::SelectTarget(bool bRandom)
{
    TArray<AMCToothCharacter*> Heroes;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(IsLiveTarget(*It)) Heroes.Add(*It);
    if(Heroes.IsEmpty()) { Target=nullptr; return; }
    if(bRandom) Target=Heroes[Random.RandRange(0,Heroes.Num()-1)];
    else {
        double Best=DBL_MAX; Target=nullptr;
        for(auto* Hero:Heroes) {
            const double Distance=FVector::DistSquared2D(GetActorLocation(),Hero->GetActorLocation());
            if(Distance<Best) { Best=Distance; Target=Hero; }
        }
    }
    NextTargetAt=Now()+.7;
}

bool AMCNutBoss::LockSurfacePoint(FVector Candidate,float Margin,FVector& Result) const
{
    FHitResult Floor;
    if(!IsValid(Tongue) || !Tongue->InteriorSurfacePoint(Candidate,Margin,Floor)) return false;
    Result=Floor.ImpactPoint+FVector(0,0,Settings.BodyRadius+4); return true;
}

bool AMCNutBoss::IsMageFocused() const
{
    if(BossRole!=EMCNutBossRole::Mage) return false;
    if(Now()-FocusWindowStartedAt<=2 && FocusHits>=2) return true;
    int32 Nearby=0;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        if(IsLiveTarget(*It) && FVector::DistSquared2D(It->GetActorLocation(),GetActorLocation())<FMath::Square(BossSettings.MageFocusRadius)
            && HasLineOfSight(*It,GetActorLocation()) && ++Nearby>=2) return true;
    return false;
}

bool AMCNutBoss::IsTeleportPointFree(FVector Point) const
{
    if(Point.ContainsNaN() || !GetWorld()) return false;
    // Keep enough room to appear, rather than emerging inside a pursuing worker.
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        if(IsLiveTarget(*It) && FVector::DistSquared2D(Point,It->GetActorLocation())
            <FMath::Square(BossSettings.MageFocusRadius+It->GetCapsuleComponent()->GetScaledCapsuleRadius())) return false;
    for(TActorIterator<AMCNutEnemy> It(GetWorld());It;++It)
        if(*It!=this && It->IsEncounterAlive() && FVector::DistSquared(Point,It->GetActorLocation())
            <FMath::Square(Settings.BodyRadius+It->Settings.BodyRadius+20)) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCNutTeleport),false,this);
    Query.AddIgnoredActor(Tongue);
    return !GetWorld()->OverlapBlockingTestByProfile(Point,FQuat::Identity,TEXT("BlockAll"),
        FCollisionShape::MakeSphere(Settings.BodyRadius+10),Query);
}

bool AMCNutBoss::FindTeleportPoint(FVector& Result)
{
    if(!HasAuthority() || !IsValid(Tongue)) return false;
    const FVector Origin=GetActorLocation();
    float BestClearance=-1;
    for(int32 Try=0;Try<32;++Try) {
        const float Angle=Random.FRandRange(-PI,PI);
        const float Distance=Random.FRandRange(BossSettings.MageTeleportMinDistance,BossSettings.MageTeleportMaxDistance);
        const FVector Candidate=Origin+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Distance;
        FHitResult Floor;
        if(!Tongue->GameplaySpawnFootprint(Candidate,Settings.BodyRadius+20,Floor)) continue;
        const FVector Point=Floor.ImpactPoint+FVector(0,0,Settings.BodyRadius+4);
        if(!IsTeleportPointFree(Point)) continue;
        float Clearance=BossSettings.MageTeleportMaxDistance*2;
        for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
            if(IsLiveTarget(*It)) Clearance=FMath::Min(Clearance,float(FVector::Dist2D(Point,It->GetActorLocation())));
        if(Clearance>BestClearance) {BestClearance=Clearance;Result=Point;}
    }
    return BestClearance>=0;
}

void AMCNutBoss::BeginTeleport()
{
    if(!HasAuthority() || BossRole!=EMCNutBossRole::Mage) return;
    FVector Destination;
    // Failed searches are bounded; a crowded arena cannot create a 30 Hz retry loop.
    if(!FindTeleportPoint(Destination)) {NextTeleportAt=Now()+2;ForceNetUpdate();return;}
    Attack=EMCNutBossAttack::Teleport; AttackSeed=Random.RandRange(1,MAX_int32);
    LockedStart=GetActorLocation(); LockedTarget=Destination;
    AttackForward=GetActorForwardVector(); AttackStartedAt=Now();
    ResolveAt=AttackStartedAt+BossSettings.MageTeleportWindup;
    NextTeleportAt=AttackStartedAt+BossSettings.MageTeleportCooldown;
    bResolved=false; Enter(EMCNutBossState::Telegraph,BossSettings.MageTeleportWindup);
    TrackAttackActor(AMCNutCombatEffect::Spawn(this,Tongue,EMCNutCombatCue::TeleportTell,LockedStart,LockedTarget,
        Settings.BodyRadius+35,BossSettings.MageTeleportWindup,.12f,AttackSeed));
}

void AMCNutBoss::PushPlayer(AMCToothCharacter* Hero,FVector Direction,float Speed)
{
    if(!HasAuthority() || !IsValid(Hero) || Speed<=0) return;
    Direction=Direction.GetSafeNormal2D(KINDA_SMALL_NUMBER,AttackForward);
    const float Lift=BossRole==EMCNutBossRole::Tank?BossSettings.TankPushLift:55;
    const FVector Impulse=Direction*Speed+FVector::UpVector*Lift;
    if(Hero->ToothPhysics) Hero->ToothPhysics->ApplyHit(Impulse,Hero->GetActorLocation());
    else Hero->LaunchCharacter(Impulse,false,false);
}

void AMCNutBoss::TriggerRollToothPain(const FHitResult& Hit)
{
    if(!HasAuthority() || !IsValid(Tongue) || !Cast<AMCArenaTooth>(Hit.GetActor()) || Now()<NextRollPainAt) return;
    NextRollPainAt=Now()+BossSettings.RollPainCooldown;
    // Repeated substep contacts cannot restart a wave. An already active tongue
    // motion retains its single authoritative wave instead of being replaced.
    Tongue->TriggerPain(Hit.ImpactPoint);
}

void AMCNutBoss::TrackAttackActor(AActor* Actor)
{
    if(!IsValid(Actor)) return;
    if(IsValid(EncounterOwner)) { Actor->SetOwner(EncounterOwner); EncounterOwner->TrackEncounterActor(Actor); }
    ActiveAttacks.Add(Actor);
    ActiveAttacks.RemoveAll([](const auto& Item){return !Item.IsValid();});
}

void AMCNutBoss::BeginAttack(EMCNutBossAttack Next)
{
    if(!HasAuthority() || !IsLiveTarget(Target)) return;
    const double Time=Now();
    if(Next==EMCNutBossAttack::Charge || Next==EMCNutBossAttack::Jump || Next==EMCNutBossAttack::NutRain || Next==EMCNutBossAttack::Roll) SelectTarget(true);
    if(!IsLiveTarget(Target)) return;
    Attack=Next; AttackSeed=Random.RandRange(1,MAX_int32); LockedStart=GetActorLocation();
    LockedTarget=Target->GetActorLocation(); AttackForward=(LockedTarget-LockedStart).GetSafeNormal2D();
    if(AttackForward.IsNearlyZero()) AttackForward=GetActorForwardVector();
    SetActorRotation(AttackForward.Rotation());
    float Windup=BossSettings.MeleeWindup;
    EMCNutCombatCue Cue=EMCNutCombatCue::MeleeTell;
    float CueRadius=BossSettings.MeleeRange,ActiveSeconds=.25f;
    if(Next==EMCNutBossAttack::Charge) {
        Windup=BossSettings.ChargeWindup; NextChargeAt=Time+BossSettings.ChargeCooldown;
        const float Distance=FMath::Clamp(float(FVector::Dist2D(LockedStart,LockedTarget)+220),350.f,BossSettings.ChargeDistance);
        bool Valid=false;
        for(float Scale:{1.f,.8f,.6f,.4f}) if(LockSurfacePoint(LockedStart+AttackForward*Distance*Scale,Settings.BodyRadius+5,LockedTarget)) {Valid=true;break;}
        if(!Valid) { Recover(.6f); return; }
        Cue=EMCNutCombatCue::ChargeTell; CueRadius=Settings.BodyRadius+35;
        ActiveSeconds=float(FVector::Dist2D(LockedStart,LockedTarget))/BossSettings.ChargeSpeed;
    } else if(Next==EMCNutBossAttack::Roll) {
        Windup=BossSettings.RollWindup; NextRollAt=Time+BossSettings.RollCooldown;
        // The first visible corridor is fixed during the tell. Steering starts only
        // after it resolves, and cannot instantly follow a player's dodge.
        bool Valid=false;
        for(float Distance:{500.f,350.f,200.f})
            if(LockSurfacePoint(LockedStart+AttackForward*Distance,Settings.BodyRadius+5,LockedTarget)) {Valid=true;break;}
        if(!Valid) { Recover(.6f); return; }
        Cue=EMCNutCombatCue::RollTell; CueRadius=Settings.BodyRadius+25; ActiveSeconds=BossSettings.RollDuration;
    } else if(Next==EMCNutBossAttack::Jump) {
        Windup=BossSettings.JumpWindup; NextJumpAt=Time+BossSettings.JumpCooldown;
        const FVector Candidate=LockedStart+(LockedTarget-LockedStart).GetClampedToMaxSize(BossSettings.ChargeDistance);
        if(!LockSurfacePoint(Candidate,Settings.BodyRadius+5,LockedTarget)) { Recover(.6f); return; }
        Cue=EMCNutCombatCue::JumpTell; CueRadius=BossSettings.JumpRadius; ActiveSeconds=BossSettings.JumpFlightSeconds;
    } else if(Next==EMCNutBossAttack::Fireball) {
        Windup=BossSettings.FireballWindup; NextFireballAt=Time+BossSettings.FireballCooldown;
        Cue=EMCNutCombatCue::FireCast; CueRadius=BossSettings.FireballRadius+25;
    } else if(Next==EMCNutBossAttack::Summon) {
        Windup=1.1f; NextSummonAt=Time+BossSettings.SummonCooldown; CueRadius=45; Cue=EMCNutCombatCue::SummonTell;
        SummonPositions.Reset();
        const float CreepRadius=IsValid(EncounterOwner)?FMath::Clamp(EncounterOwner->Settings.Enemy.BodyRadius,20.f,80.f):BossSettings.CreepHeight*.5f;
        const int32 Count=FMath::Min(4,BossSettings.SummonCount+(PartyPlayers-1)/2);
        for(int32 Index=0;Index<Count;++Index) for(int32 Try=0;Try<20;++Try) {
            const float Angle=Random.FRandRange(-PI,PI),Distance=Random.FRandRange(Settings.BodyRadius+CreepRadius+60,Settings.BodyRadius+CreepRadius+260);
            FHitResult Floor; const FVector Point=GetActorLocation()+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Distance;
            if(Tongue->GameplaySpawnFootprint(Point,CreepRadius,Floor)) {
                const FVector Spawn=Floor.ImpactPoint+FVector(0,0,CreepRadius+4);
                if(SummonPositions.ContainsByPredicate([&](FVector Other){return FVector::DistSquared2D(Other,Spawn)<FMath::Square(CreepRadius*2+20);})) continue;
                SummonPositions.Add(Spawn); break;
            }
        }
        for(FVector Point:SummonPositions) TrackAttackActor(AMCNutCombatEffect::Spawn(this,Tongue,Cue,Point,Point,CreepRadius+10,Windup,.3f,AttackSeed));
    } else if(Next==EMCNutBossAttack::NutRain) {
        Windup=BossSettings.RainWindup; NextRainAt=Time+BossSettings.RainCooldown;
        if(!LockSurfacePoint(LockedTarget,BossSettings.RainRadius*.5f,LockedTarget)) { Recover(.6f); return; }
        Cue=EMCNutCombatCue::NutRain; CueRadius=BossSettings.RainRadius; ActiveSeconds=BossSettings.RainActiveSeconds;
    } else {
        NextMeleeAt=Time+BossSettings.MeleeCooldown; bMeleeBetweenAbilities=false;
        LockedTarget=LockedStart+AttackForward*BossSettings.MeleeRange;
    }
    bResolved=false; AttackHits.Reset(); RainHitAt.Reset(); RollHitAt.Reset(); RainDropsResolved=0;
    AttackStartedAt=Time; ResolveAt=Time+Windup;
    Enter(EMCNutBossState::Telegraph,Windup);
    if(Next==EMCNutBossAttack::Fireball || Next==EMCNutBossAttack::Summon || Next==EMCNutBossAttack::NutRain)
        TrackAttackActor(AMCNutCombatEffect::Spawn(this,Tongue,Next==EMCNutBossAttack::Fireball?EMCNutCombatCue::CastCharge:EMCNutCombatCue::RitualCast,
            GetCastOrigin(),LockedTarget,Settings.BodyRadius,Windup,Next==EMCNutBossAttack::NutRain?ActiveSeconds:.25f,AttackSeed));
    else if(Next==EMCNutBossAttack::Roll)
        TrackAttackActor(AMCNutCombatEffect::Spawn(this,Tongue,EMCNutCombatCue::Transform,LockedStart,LockedTarget,Settings.BodyRadius,Windup,.3f,AttackSeed));
    if(Next!=EMCNutBossAttack::Summon) TrackAttackActor(AMCNutCombatEffect::Spawn(this,Tongue,Cue,LockedStart,LockedTarget,
        CueRadius,Windup,ActiveSeconds,AttackSeed,BossSettings.RainImpactRadius,BossSettings.RainDrops,BossSettings.RainActiveSeconds/BossSettings.RainDrops));
}

bool AMCNutBoss::HasLineOfSight(const AMCToothCharacter* Hero,FVector From) const
{
    FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(MCNutBossContact),false,this); Query.AddIgnoredActor(Hero);
    return !GetWorld()->LineTraceSingleByChannel(Hit,From,Hero->GetActorLocation(),ECC_Visibility,Query);
}

void AMCNutBoss::DamageArea(FVector Center,float Radius,float Damage,float Push,TSet<TWeakObjectPtr<AMCToothCharacter>>* HitSet)
{
    if(!HasAuthority()) return;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) {
        AMCToothCharacter* Hero=*It; if(!IsLiveTarget(Hero) || (HitSet && HitSet->Contains(Hero))) continue;
        const FVector Delta=Hero->GetActorLocation()-Center;
        const float Reach=Radius+Hero->GetCapsuleComponent()->GetScaledCapsuleRadius();
        if(Delta.SizeSquared2D()>FMath::Square(Reach) || FMath::Abs(Delta.Z)>230 || !HasLineOfSight(Hero,Center+FVector(0,0,80))) continue;
        FVector Direction=Delta.GetSafeNormal2D(); if(Direction.IsNearlyZero()) Direction=AttackForward;
        if(!Hero->Status->Damage(Damage,Direction)) continue;
        if(HitSet) HitSet->Add(Hero);
        PushPlayer(Hero,Direction,Push);
    }
}

void AMCNutBoss::DamageChargeSegment(FVector Start,FVector End)
{
    if(!HasAuthority()) return;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) {
        auto* Hero=*It; if(!IsLiveTarget(Hero) || AttackHits.Contains(Hero)) continue;
        const FVector Closest=FMath::ClosestPointOnSegment(Hero->GetActorLocation(),Start,End);
        const float Reach=Settings.BodyRadius+Hero->GetCapsuleComponent()->GetScaledCapsuleRadius()+15;
        if(FVector::DistSquared2D(Hero->GetActorLocation(),Closest)>FMath::Square(Reach)
            || FMath::Abs(Hero->GetActorLocation().Z-Closest.Z)>180 || !HasLineOfSight(Hero,Closest)) continue;
        if(Hero->Status->Damage(BossSettings.ChargeDamage,AttackForward)) {
            AttackHits.Add(Hero);
            PushPlayer(Hero,AttackForward,BossSettings.TankChargePush);
        }
    }
}

void AMCNutBoss::DamageRollSegment(FVector Start,FVector End)
{
    if(!HasAuthority()) return;
    const double Time=Now();
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) {
        auto* Hero=*It; if(!IsLiveTarget(Hero)) continue;
        if(const double* Last=RollHitAt.Find(Hero); Last && Time-*Last<BossSettings.RollHitGap) continue;
        const FVector Closest=FMath::ClosestPointOnSegment(Hero->GetActorLocation(),Start,End);
        const float Reach=Settings.BodyRadius+Hero->GetCapsuleComponent()->GetScaledCapsuleRadius()+15;
        const FVector Delta=Hero->GetActorLocation()-Closest;
        if(Delta.SizeSquared2D()>FMath::Square(Reach) || FMath::Abs(Delta.Z)>180 || !HasLineOfSight(Hero,Closest)) continue;
        FVector PushDirection=Delta.GetSafeNormal2D(); if(PushDirection.IsNearlyZero()) PushDirection=AttackForward;
        if(!Hero->Status->Damage(BossSettings.RollDamage,PushDirection)) continue;
        RollHitAt.Add(Hero,Time);
        PushPlayer(Hero,PushDirection,BossSettings.RollPush);
    }
}

void AMCNutBoss::ExecuteRoll(float Dt)
{
    if(!HasAuthority() || !FMath::IsFinite(Dt) || Dt<=0) return;
    const double Time=Now();
    if(Time>=AttackEndAt) {Recover(1.4f);return;}
    if(!IsLiveTarget(Target) || Time>=NextTargetAt) SelectTarget(false);
    const float FrameSeconds=FMath::Min(Dt,.25f);
    if(IsLiveTarget(Target)) {
        const FVector Desired=(Target->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
        if(!Desired.IsNearlyZero()) {
            const float Delta=FMath::Clamp(FMath::FindDeltaAngleDegrees(AttackForward.Rotation().Yaw,Desired.Rotation().Yaw),
                -BossSettings.RollTurnDegreesPerSecond*FrameSeconds,BossSettings.RollTurnDegreesPerSecond*FrameSeconds);
            AttackForward=AttackForward.RotateAngleAxis(Delta,FVector::UpVector).GetSafeNormal2D();
        }
    }
    const int32 Steps=FMath::Clamp(FMath::CeilToInt(BossSettings.RollSpeed*FrameSeconds/45.f),1,6);
    const float StepDistance=BossSettings.RollSpeed*FrameSeconds/Steps;
    for(int32 Index=0;Index<Steps;++Index) {
        const FVector Previous=GetActorLocation(); FVector Goal;
        if(!LockSurfacePoint(Previous+AttackForward*StepDistance,Settings.BodyRadius+5,Goal)) {
            // Probe the moving tongue's footprint to reflect off a concave edge,
            // rather than assuming a rectangular arena or stepping outside it.
            FVector Inward=FVector::ZeroVector;
            for(const FVector Probe:{FVector::ForwardVector,-FVector::ForwardVector,FVector::RightVector,-FVector::RightVector}) {
                FVector Unused;
                if(!LockSurfacePoint(Previous+Probe*FMath::Max(50.f,StepDistance),Settings.BodyRadius+5,Unused)) Inward-=Probe;
            }
            Inward=Inward.GetSafeNormal2D(); if(Inward.IsNearlyZero()) Inward=-AttackForward;
            AttackForward=(AttackForward-2*FVector::DotProduct(AttackForward,Inward)*Inward).GetSafeNormal2D();
            ForceNetUpdate();
            if(!LockSurfacePoint(Previous+AttackForward*StepDistance,Settings.BodyRadius+5,Goal)) continue;
        }
        FHitResult Hit; SetActorLocation(Goal,true,&Hit);
        DamageRollSegment(Previous,GetActorLocation());
        if(Hit.bBlockingHit) {
            TriggerRollToothPain(Hit);
            FVector Normal=Hit.ImpactNormal.GetSafeNormal2D();
            if(Normal.IsNearlyZero()) Normal=-AttackForward;
            AttackForward=(AttackForward-2*FVector::DotProduct(AttackForward,Normal)*Normal).GetSafeNormal2D();
            ForceNetUpdate();
        }
    }
    SetActorRotation(AttackForward.Rotation());
}

void AMCNutBoss::SummonCreeps()
{
    int32 Live=0;
    for(TActorIterator<AMCNutEnemy> It(GetWorld());It;++It)
        if(!Cast<AMCNutBoss>(*It) && It->GetOwner()==EncounterOwner && It->IsEncounterAlive()) ++Live;
    UStaticMesh* Mesh=BossSettings.KernelMesh.LoadSynchronous(); if(!Mesh || !IsValid(EncounterOwner)) return;
    for(FVector Point:SummonPositions) {
        if(Live>=BossSettings.MaxLiveCreeps) break;
        FMCNutEnemySettings Creep=EncounterOwner->Settings.Enemy;
        Creep.MaxHealth=BossSettings.CreepHealth; Creep.BodyRadius=FMath::Clamp(Creep.BodyRadius,20.f,80.f);
        FHitResult Floor; if(!Tongue->GameplaySpawnFootprint(Point,Creep.BodyRadius,Floor)) continue;
        const FTransform Pose(AttackForward.Rotation(),Floor.ImpactPoint+FVector(0,0,Creep.BodyRadius+4));
        auto* Enemy=GetWorld()->SpawnActorDeferred<AMCNutEnemy>(AMCNutEnemy::StaticClass(),Pose,EncounterOwner,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Enemy) continue;
        Enemy->ConfigureEnemy(Tongue,Mesh,HeightScale(Mesh,BossSettings.CreepHeight),Creep); Enemy->FinishSpawning(Pose);
        EncounterOwner->RegisterEncounterEnemy(Enemy); ++Live;
    }
    SummonPositions.Reset();
}

void AMCNutBoss::Recover(float Seconds)
{
    if(Attack!=EMCNutBossAttack::Melee && Attack!=EMCNutBossAttack::None) bMeleeBetweenAbilities=true;
    AttackStartedAt=-100; RollHitAt.Reset(); Enter(EMCNutBossState::Recovery,Seconds);
}

void AMCNutBoss::ExecuteAttack(float Dt)
{
    const double Time=Now();
    if(State==EMCNutBossState::Telegraph) {
        if(Time<ResolveAt) return;
        const float Duration=Attack==EMCNutBossAttack::Charge?float(FVector::Dist2D(LockedStart,LockedTarget))/BossSettings.ChargeSpeed
            :Attack==EMCNutBossAttack::Jump?BossSettings.JumpFlightSeconds:Attack==EMCNutBossAttack::NutRain?BossSettings.RainActiveSeconds
            :Attack==EMCNutBossAttack::Roll?BossSettings.RollDuration:.1f;
        Enter(EMCNutBossState::Executing,Duration);
    }
    if(Attack==EMCNutBossAttack::Teleport) {
        if(!bResolved) {
            bResolved=true;
            FVector Landing;
            // The tongue and the workers can move during the warning. Never
            // substitute a new unannounced destination when the lock is occupied.
            if(LockSurfacePoint(LockedTarget,Settings.BodyRadius+20,Landing) && IsTeleportPointFree(Landing)) {
                SetActorLocation(Landing,false,nullptr,ETeleportType::TeleportPhysics);
                LastTankPresentationAt=-1; TankPresentationMotion=FVector::ZeroVector;
                FocusHits=0; FocusWindowStartedAt=-100;
                SelectTarget(false);
                if(IsLiveTarget(Target)) SetActorRotation((Target->GetActorLocation()-Landing).GetSafeNormal2D().Rotation());
                TrackAttackActor(AMCNutCombatEffect::Spawn(this,Tongue,EMCNutCombatCue::TeleportBurst,LockedStart,Landing,
                    Settings.BodyRadius+35,0,.45f,AttackSeed));
                ForceNetUpdate();
            }
            Recover(.55f);
        }
    } else if(Attack==EMCNutBossAttack::Charge) {
        const FVector Previous=GetActorLocation();
        const float Travel=FMath::Clamp(float(Time-ResolveAt)*BossSettings.ChargeSpeed,0.f,float(FVector::Dist2D(LockedStart,LockedTarget)));
        FVector Goal;
        if(!LockSurfacePoint(LockedStart+AttackForward*Travel,Settings.BodyRadius+5,Goal)) { Recover(1.4f); return; }
        FHitResult Hit; SetActorLocation(Goal,true,&Hit); DamageChargeSegment(Previous,GetActorLocation());
        if(Time>=AttackEndAt || (Hit.bBlockingHit && FVector::DistSquared2D(Previous,GetActorLocation())<4)) Recover(1.4f);
    } else if(Attack==EMCNutBossAttack::Roll) {
        ExecuteRoll(Dt);
    } else if(Attack==EMCNutBossAttack::Jump) {
        const float Alpha=FMath::Clamp(float(Time-ResolveAt)/BossSettings.JumpFlightSeconds,0.f,1.f);
        FVector Goal=FMath::Lerp(LockedStart,LockedTarget,Alpha)+FVector(0,0,FMath::Sin(Alpha*PI)*350);
        SetActorLocation(Goal,false);
        if(Alpha>=1 && !bResolved) {
            bResolved=true; DamageArea(LockedTarget,BossSettings.JumpRadius,BossSettings.JumpDamage,BossSettings.TankJumpPush,&AttackHits);
            TrackAttackActor(AMCNutCombatEffect::Spawn(this,Tongue,EMCNutCombatCue::SlamImpact,LockedTarget,LockedTarget,BossSettings.JumpRadius,0,.6f,AttackSeed));
            Recover(1.7f);
        }
    } else if(Attack==EMCNutBossAttack::NutRain) {
        const double Cadence=BossSettings.RainActiveSeconds/BossSettings.RainDrops;
        for(int32 Budget=0;RainDropsResolved<BossSettings.RainDrops && Time>=ResolveAt+RainDropsResolved*Cadence && Budget<2;++Budget,++RainDropsResolved) {
            TSet<TWeakObjectPtr<AMCToothCharacter>> RecentlyHit;
            for(const auto& Pair:RainHitAt) if(Time-Pair.Value<BossSettings.RainHitGap) RecentlyHit.Add(Pair.Key);
            const auto Before=RecentlyHit;
            // Rain is one sustained storm region. Falling nuts illustrate its
            // pulses; their individual impact decorations never deal extra damage.
            DamageArea(LockedTarget,BossSettings.RainRadius,BossSettings.RainDamage,70,&RecentlyHit);
            for(const auto& Hero:RecentlyHit) if(!Before.Contains(Hero)) RainHitAt.Add(Hero,Time);
        }
        if(Time>=AttackEndAt && RainDropsResolved>=BossSettings.RainDrops) Recover(1.4f);
    } else if(!bResolved) {
        bResolved=true;
        if(Attack==EMCNutBossAttack::Fireball) {
            const FVector Muzzle=GetCastOrigin();
            TrackAttackActor(AMCNutCombatEffect::Spawn(this,Tongue,EMCNutCombatCue::CastRelease,Muzzle,LockedTarget,
                BossSettings.FireballRadius+25,0,.3f,AttackSeed));
            TrackAttackActor(AMCNutSpellProjectile::Spawn(this,Tongue,Muzzle,LockedTarget,BossSettings.FireballSpeed,BossSettings.FireballDamage,BossSettings.FireballRadius));
        } else if(Attack==EMCNutBossAttack::Summon) SummonCreeps();
        else if(Attack==EMCNutBossAttack::Melee) {
            for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) {
                const FVector Direction=(It->GetActorLocation()-LockedStart).GetSafeNormal2D();
                if(FVector::DotProduct(Direction,AttackForward)<.35f) AttackHits.Add(*It);
            }
            DamageArea(LockedStart,BossSettings.MeleeRange,BossSettings.MeleeDamage,
                BossRole==EMCNutBossRole::Tank?BossSettings.TankMeleePush:90,&AttackHits);
            TrackAttackActor(AMCNutCombatEffect::Spawn(this,Tongue,EMCNutCombatCue::MeleeSlash,LockedStart,LockedTarget,
                BossSettings.MeleeRange,0,.22f,AttackSeed));
        }
        Recover(Attack==EMCNutBossAttack::Summon?1.2f:.9f);
    }
}

void AMCNutBoss::Tick(float Dt)
{
    AActor::Tick(Dt); const double Time=Now(); TickPresentation(Time); PresentTankModel(Time);
    if(!HasAuthority() || bDefeated) return;
    const auto* Game=GetWorld()->GetGameState<AMCGameState>();
    if(Game && (Game->Phase==EMCShiftPhase::Won || Game->Phase==EMCShiftPhase::Lost)) {CancelAttacks();return;}
    if(!IsValid(Tongue) || Tongue->IsActorBeingDestroyed()) {Defeat();return;}
    if(State==EMCNutBossState::Falling) {
        const float Alpha=FMath::Clamp(float((Time-StateStartedAt)/BossSettings.EntranceSeconds),0.f,1.f);
        SetActorLocation(FMath::Lerp(LockedStart,EntranceLanding,Alpha*Alpha),false);
        if(Alpha>=1) {
            TrackAttackActor(AMCNutCombatEffect::Spawn(this,Tongue,EMCNutCombatCue::EntranceImpact,EntranceLanding,EntranceLanding,Settings.BodyRadius+35,0,.6f,1));
            Enter(EMCNutBossState::Idle,0);
        }
        return;
    }
    if(State==EMCNutBossState::Telegraph || State==EMCNutBossState::Executing) {ExecuteAttack(Dt);return;}
    if(State==EMCNutBossState::Recovery) {
        if(Time<AttackEndAt) return;
        Attack=EMCNutBossAttack::None; Enter(EMCNutBossState::Idle,0);
    }
    if(!IsLiveTarget(Target) || Time>=NextTargetAt) SelectTarget(false);
    if(!IsLiveTarget(Target)) return;
    if(BossRole==EMCNutBossRole::Mage && Time>=NextTeleportAt && IsMageFocused()) {
        BeginTeleport();
        if(State==EMCNutBossState::Telegraph) return;
    }
    const FVector Direction=(Target->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
    if(!Direction.IsNearlyZero()) SetActorRotation(Direction.Rotation());
    const double Distance=FVector::Dist2D(GetActorLocation(),Target->GetActorLocation());
    const bool SpecialReady=BossRole==EMCNutBossRole::Tank?
        Time>=NextRollAt || Time>=NextJumpAt || Time>=NextChargeAt:
        Time>=NextRainAt || Time>=NextSummonAt || Time>=NextFireballAt;
    // Close combat has a turn between specials; once it resolves, ready special
    // abilities retain priority. During cooldowns ordinary swings can continue.
    if(Time>=NextMeleeAt && (bMeleeBetweenAbilities || !SpecialReady)
        && Distance<BossSettings.MeleeRange*.95f && HasLineOfSight(Target,GetActorLocation())) {
        BeginAttack(EMCNutBossAttack::Melee);return;
    }
    if(BossRole==EMCNutBossRole::Tank) {
        if(Time>=NextRollAt) {BeginAttack(EMCNutBossAttack::Roll);return;}
        if(Time>=NextJumpAt) {BeginAttack(EMCNutBossAttack::Jump);return;}
        if(Time>=NextChargeAt) {BeginAttack(EMCNutBossAttack::Charge);return;}
    } else {
        if(Time>=NextRainAt) {BeginAttack(EMCNutBossAttack::NutRain);return;}
        if(Time>=NextSummonAt) {BeginAttack(EMCNutBossAttack::Summon);return;}
        if(Time>=NextFireballAt) {BeginAttack(EMCNutBossAttack::Fireball);return;}
    }
    if(BossRole==EMCNutBossRole::Tank && Distance>BossSettings.MeleeRange*.8f) MoveOnTongue(Direction,Dt);
    else if(BossRole==EMCNutBossRole::Mage && Distance<BossSettings.MageFocusRadius) MoveOnTongue(-Direction,Dt);
    else if(BossRole==EMCNutBossRole::Mage && Distance>720+Settings.BodyRadius) MoveOnTongue(Direction,Dt);
}

bool AMCNutBoss::IsPlayerInThreat(const AMCToothCharacter* Hero,FVector& EscapeDirection) const
{
    if(!IsLiveTarget(Hero) || !IsEncounterAlive()) return false;
    const FVector Point=Hero->GetActorLocation(); const float HeroRadius=Hero->GetCapsuleComponent()->GetScaledCapsuleRadius();
    if(State==EMCNutBossState::Falling) {
        const FVector Delta=Point-LockedTarget;
        if(Delta.SizeSquared2D()>FMath::Square(Settings.BodyRadius+HeroRadius+35)) return false;
        EscapeDirection=Delta.GetSafeNormal2D();
    } else if(State!=EMCNutBossState::Telegraph && State!=EMCNutBossState::Executing) return false;
    else if(Attack==EMCNutBossAttack::Roll) {
        const FVector Start=State==EMCNutBossState::Telegraph?LockedStart:GetActorLocation();
        const FVector End=State==EMCNutBossState::Telegraph?LockedTarget:Start+AttackForward*BossSettings.RollSpeed*.7f;
        const FVector Closest=FMath::ClosestPointOnSegment(Point,Start,End);
        if(FVector::DistSquared2D(Point,Closest)>FMath::Square(Settings.BodyRadius+HeroRadius+35)
            || FMath::Abs(Point.Z-Closest.Z)>180) return false;
        EscapeDirection=(Point-Closest).GetSafeNormal2D();
        if(EscapeDirection.IsNearlyZero()) EscapeDirection=FVector::CrossProduct(AttackForward,FVector::UpVector);
    } else if(Attack==EMCNutBossAttack::Charge || Attack==EMCNutBossAttack::Fireball) {
        const FVector Closest=FMath::ClosestPointOnSegment(Point,LockedStart,LockedTarget);
        const float Radius=Attack==EMCNutBossAttack::Charge?Settings.BodyRadius+35:BossSettings.FireballRadius+25;
        if(FVector::DistSquared2D(Point,Closest)>FMath::Square(Radius+HeroRadius)) return false;
        EscapeDirection=(Point-Closest).GetSafeNormal2D();
        if(EscapeDirection.IsNearlyZero()) EscapeDirection=FVector::CrossProduct(AttackForward,FVector::UpVector);
    } else if(Attack==EMCNutBossAttack::Jump || Attack==EMCNutBossAttack::NutRain || Attack==EMCNutBossAttack::Teleport) {
        const float Radius=Attack==EMCNutBossAttack::Jump?BossSettings.JumpRadius:
            Attack==EMCNutBossAttack::Teleport?Settings.BodyRadius+35:BossSettings.RainRadius;
        const FVector Delta=Point-LockedTarget;
        if(Delta.SizeSquared2D()>FMath::Square(Radius+HeroRadius+25)) return false;
        EscapeDirection=Delta.GetSafeNormal2D();
    } else if(Attack==EMCNutBossAttack::Melee) {
        const FVector Delta=Point-LockedStart;
        if(Delta.SizeSquared2D()>FMath::Square(BossSettings.MeleeRange+HeroRadius) || FVector::DotProduct(Delta.GetSafeNormal2D(),AttackForward)<.35f) return false;
        EscapeDirection=Delta.GetSafeNormal2D();
    } else return false;
    if(EscapeDirection.IsNearlyZero()) EscapeDirection=GetActorRightVector();
    return true;
}

void AMCNutBoss::CancelAttacks()
{
    if(!HasAuthority()) return;
    for(auto Weak:ActiveAttacks) if(AActor* Actor=Weak.Get()) {
        if(auto* Cue=Cast<AMCNutCombatEffect>(Actor)) Cue->Cancel();
        else if(auto* Projectile=Cast<AMCNutSpellProjectile>(Actor)) Projectile->Cancel();
        else Actor->Destroy();
    }
    ActiveAttacks.Reset(); SummonPositions.Reset(); RollHitAt.Reset();
    if(Attack==EMCNutBossAttack::Roll) { Attack=EMCNutBossAttack::None; ForceNetUpdate(); }
}

void AMCNutBoss::Defeat()
{
    CancelAttacks(); State=EMCNutBossState::Defeated; Attack=EMCNutBossAttack::None;
    StateStartedAt=Now();
    Super::Defeat();
    SetLifeSpan(BossSettings.DeathSeconds);
    TrackAttackActor(AMCNutCombatEffect::Spawn(this,Tongue,EMCNutCombatCue::DeathBurst,GetActorLocation(),GetActorLocation(),Settings.BodyRadius,0,1.1f,AttackSeed));
}

void AMCNutBoss::EndPlay(const EEndPlayReason::Type Reason)
{
    CancelAttacks(); Super::EndPlay(Reason);
}

void AMCNutBoss::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCNutBoss,EncounterOwner); DOREPLIFETIME(AMCNutBoss,BossSettings); DOREPLIFETIME(AMCNutBoss,BossRole);
    DOREPLIFETIME(AMCNutBoss,State); DOREPLIFETIME(AMCNutBoss,Attack); DOREPLIFETIME(AMCNutBoss,StateStartedAt);
    DOREPLIFETIME(AMCNutBoss,ResolveAt); DOREPLIFETIME(AMCNutBoss,AttackEndAt); DOREPLIFETIME(AMCNutBoss,LockedStart);
    DOREPLIFETIME(AMCNutBoss,LockedTarget); DOREPLIFETIME(AMCNutBoss,AttackForward); DOREPLIFETIME(AMCNutBoss,AttackSeed);
    DOREPLIFETIME(AMCNutBoss,VisualHitAt); DOREPLIFETIME(AMCNutBoss,ShieldHitAt); DOREPLIFETIME(AMCNutBoss,VisualHitDirection);
    DOREPLIFETIME(AMCNutBoss,NextMeleeAt); DOREPLIFETIME(AMCNutBoss,NextChargeAt); DOREPLIFETIME(AMCNutBoss,NextJumpAt);
    DOREPLIFETIME(AMCNutBoss,NextRollAt); DOREPLIFETIME(AMCNutBoss,NextFireballAt); DOREPLIFETIME(AMCNutBoss,NextSummonAt);
    DOREPLIFETIME(AMCNutBoss,NextRainAt); DOREPLIFETIME(AMCNutBoss,NextTeleportAt);
}
