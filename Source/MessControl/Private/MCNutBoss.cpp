#include "MCNutBoss.h"
#include "MCPerkComponent.h"
#include "MCGameDirector.h"
#include "MCNutBossAnimInstance.h"
#include "MCCombatFeedbackComponent.h"

#include "MCNutCombatEffect.h"
#include "MCNutSpellProjectile.h"
#include "MCNutRainEvent.h"
#include "MCNutKamikaze.h"
#include "MCFoodActor.h"
#include "MCGameState.h"
#include "MCArenaTooth.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SceneComponent.h"
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
#include "ReferenceSkeleton.h"

namespace
{
bool AuthoredSpellGestures(const UAnimSequence* Clip)
{
    return Clip && Clip->EvaluateCurveData(TEXT("MCSpellGesture"),FAnimExtractContext(0.,false))>.5f;
}

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
    Feedback->bUseProceduralRecoil=false;
    PrimaryActorTick.TickInterval=1.f/30.f;
    Shield=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shield")); Shield->SetupAttachment(Body);
    Kernel=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Kernel")); Kernel->SetupAttachment(Body);
    OpenShell=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OpenShell")); OpenShell->SetupAttachment(Body);
    TankTurnPivot=CreateDefaultSubobject<USceneComponent>(TEXT("TankTurnPivot"));TankTurnPivot->SetupAttachment(Body);
    TankModel=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("TankModel")); TankModel->SetupAttachment(TankTurnPivot);
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
    ShieldHealth=BossRole==EMCNutBossRole::Tank?BossSettings.ShieldMaxHealth:0.f;
    ShieldBrokenAt=-100;
    TargetSwitchCount=0;HeadTurnVariant=0;PendingHeadTurns.Reset();
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
    bTankRollRotationInitialized=false;
    TankModel->AddTickPrerequisiteActor(this);
    MageModel->AddTickPrerequisiteActor(this);
    if(GetNetMode()==NM_DedicatedServer) {TankModel->SetComponentTickEnabled(false);MageModel->SetComponentTickEnabled(false);}
    if(HasAuthority()) {
        if(State==EMCNutBossState::Falling) SetActorLocation(LockedStart,false);
        const double Ready=ResolveAt;
        NextTargetAt=Ready; NextMeleeAt=Ready+1.5; NextChargeAt=Ready+3; NextJumpAt=Ready+7; NextRollAt=Ready+11;
        NextFireballAt=Ready+6; NextSummonAt=Ready+10; NextRainAt=Ready+13; NextTeleportAt=Ready+4;
        NextSpecialAt=Ready+BossSettings.SpecialAttackGap;
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
    if(TankModel->GetAttachParent()!=TankTurnPivot)
        TankModel->AttachToComponent(TankTurnPivot,FAttachmentTransformRules::KeepRelativeTransform);
    auto* TankMesh=BossRole==EMCNutBossRole::Tank?BossSettings.TankSkeletalMesh.LoadSynchronous():nullptr;
    if(BossRole==EMCNutBossRole::Tank && !TankMesh) {
        if(!bFallbackTankMeshLoaded) {
            bFallbackTankMeshLoaded=true;
            FallbackTankMesh=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/FromBlender8/SM_Nut_Tank.SM_Nut_Tank"));
        }
        TankMesh=FallbackTankMesh.Get();
    }
    if(BossRole==EMCNutBossRole::Tank) {
        // Pin the optional variant before combat, so breaking a shield does not
        // synchronously load a fresh mesh in the impact frame.
        const FSoftObjectPath Path=BossSettings.TankUnshieldedSkeletalMesh.ToSoftObjectPath();
        if(Path!=CachedUnshieldedTankMeshPath) {
            CachedUnshieldedTankMeshPath=Path;
            UnshieldedTankMesh=BossSettings.TankUnshieldedSkeletalMesh.LoadSynchronous();
        }
    }
    USkeletalMesh* RenderTankMesh=TankMesh;
    if(TankMesh && ShieldHealth<=0 && UnshieldedTankMesh
        && UnshieldedTankMesh->GetSkeleton()==TankMesh->GetSkeleton()) RenderTankMesh=UnshieldedTankMesh;
    if(TankModel->GetSkeletalMeshAsset()!=RenderTankMesh) TankModel->SetSkeletalMeshAsset(RenderTankMesh);
    if(TankMesh) {
        // Keep the original anchor: removing the shield changes mesh bounds,
        // but the feet and body must not jump when the render asset changes.
        const auto Bounds=TankMesh->GetBounds();
        TankModelScale=FVector(BossSettings.TankHeight/FMath::Max(1.,Bounds.BoxExtent.Z*2));
        // Weapons skew overall bounds far in front of the body. The reference
        // feet define a stable horizontal axis, shared by both shield meshes.
        const FReferenceSkeleton& Ref=TankMesh->GetRefSkeleton();
        auto RefPoint=[&](FName Name,FVector& Point) {
            int32 Index=Ref.FindBoneIndex(Name);if(Index==INDEX_NONE) return false;
            FTransform Transform=FTransform::Identity;
            for(int32 Depth=0;Index!=INDEX_NONE && Depth<64;++Depth) {
                Transform=Transform*Ref.GetRefBonePose()[Index];Index=Ref.GetParentIndex(Index);
            }
            Point=Transform.GetLocation();return true;
        };
        FVector Left,Right;
        TankModelGroundAnchor=RefPoint(TEXT("foot_l"),Left) && RefPoint(TEXT("foot_r"),Right)?
            (Left+Right)*.5f:Bounds.Origin;
        TankModelGroundAnchor.Z=Bounds.Origin.Z-Bounds.BoxExtent.Z;
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
    Shield->SetVisibility(ShieldHealth>0 && !HasTankModel && !bDefeated && BossRole==EMCNutBossRole::Tank && State!=EMCNutBossState::Falling
        && State==EMCNutBossState::Idle);
    SetCenteredMesh(OpenShell,Shell,BossSettings.MageHeight,FVector(-Settings.BodyRadius*.38f,0,-12),FRotator(0,-20,-22));
    OpenShell->SetVisibility(!bDefeated && BossRole==EMCNutBossRole::Mage && !MageMesh);
    Kernel->SetVisibility(false); // the inherited main mesh is the exposed kernel for the mage
    Label->SetText(FText::FromString(BossRole==EMCNutBossRole::Tank?
        FString::Printf(TEXT("NUT TANK  %d | SHIELD %d"),FMath::CeilToInt(Health),FMath::CeilToInt(ShieldHealth)):
        FString::Printf(TEXT("NUT MAGE  %d"),FMath::CeilToInt(Health))));
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
        Set(EMCNutBossClip::UnshieldedIdle,BossSettings.TankUnshieldedIdleAnimation);
        Set(EMCNutBossClip::UnshieldedWalk,BossSettings.TankUnshieldedWalkAnimation);
        Set(EMCNutBossClip::WalkLeft,BossSettings.TankWalkLeftAnimation); Set(EMCNutBossClip::WalkRight,BossSettings.TankWalkRightAnimation);
        Set(EMCNutBossClip::Melee,BossSettings.TankMeleeAnimation); Set(EMCNutBossClip::Jump,BossSettings.TankJumpAnimation);
        Set(EMCNutBossClip::Hit,BossSettings.TankHitAnimation);
        Set(EMCNutBossClip::Melee2,BossSettings.TankMelee2Animation);
        Set(EMCNutBossClip::MeleeCombo,BossSettings.TankComboAnimation);
        Set(EMCNutBossClip::Block,BossSettings.TankBlockAnimation);
        Set(EMCNutBossClip::StunHit,BossSettings.TankStunHitAnimation);
        Set(EMCNutBossClip::Death,BossSettings.TankDeathAnimation);
        Set(EMCNutBossClip::ShieldBreak,BossSettings.TankShieldBreakAnimation);
        Set(EMCNutBossClip::Transform,BossSettings.TankTransformAnimation);
        Set(EMCNutBossClip::BallEnd,BossSettings.TankBallEndAnimation);
        Set(EMCNutBossClip::HeadLeft,BossSettings.TankHeadLeftAnimation);
        Set(EMCNutBossClip::HeadRight,BossSettings.TankHeadRightAnimation);
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

FVector AMCNutBoss::GetSpellFocusOrigin() const
{
    const auto Slot=Attack==EMCNutBossAttack::NutRain?EMCNutBossClip::Rain:
        Attack==EMCNutBossAttack::Summon?EMCNutBossClip::Summon:EMCNutBossClip::Cast;
    if(BossRole!=EMCNutBossRole::Mage || !MageModel || State==EMCNutBossState::Idle
        || !BossAnimations.IsValidIndex(int32(Slot)) || !AuthoredSpellGestures(BossAnimations[int32(Slot)])) return GetCastOrigin();
    auto Palm=[&](FName Bone,FVector& Point) {
        if(MageModel->GetBoneIndex(Bone)==INDEX_NONE) Bone=FName(*Bone.ToString().Replace(TEXT("."),TEXT("_")));
        if(MageModel->GetBoneIndex(Bone)==INDEX_NONE) return false;
        Point=MageModel->GetSocketTransform(Bone,RTS_World).TransformPosition(FVector(0,8,0));
        return !Point.ContainsNaN();
    };
    FVector Left,Right;
    return Palm(BossSettings.LeftHandBone,Left) && Palm(BossSettings.RightHandBone,Right)?(Left+Right)*.5:GetCastOrigin();
}

void AMCNutBoss::BuildAnimationSnapshot(const USkeletalMeshComponent* Model,FMCNutBossAnimationSnapshot& Out) const
{
    if(!Model || !Model->GetSkeletalMeshAsset()) return;
    const double Time=Now();
    auto Progress=[](double Age,double Seconds){return FMath::Clamp(float(Age/FMath::Max(.01,Seconds)),0.f,1.f);};
    Out.ServerTime=Time; Out.bMage=BossRole==EMCNutBossRole::Mage;
    Out.BlendSeconds=Out.bMage?BossSettings.AnimationBlendSeconds:BossSettings.TankAnimationBlendSeconds;
    Out.VisualKey=int32(BossRole)*100+int32(State)*10+int32(Attack);
    const FName Names[]={BossSettings.TorsoBone,BossSettings.HeadBone,BossSettings.LeftArmBone,BossSettings.RightArmBone,
        BossSettings.LeftForearmBone,BossSettings.RightForearmBone,BossSettings.LeftHandBone,BossSettings.RightHandBone};
    for(int32 Index=0;Index<int32(EMCNutPoseBone::Count);++Index) {
        Out.Bones[Index]=Model->GetBoneIndex(Names[Index]);
        if(Out.Bones[Index]==INDEX_NONE) Out.Bones[Index]=Model->GetBoneIndex(FName(*Names[Index].ToString().Replace(TEXT("."),TEXT("_"))));
    }
    const FVector LocalMotion=GetActorTransform().InverseTransformVectorNoScale(TankPresentationMotion);
    Out.ForwardSpeed=FMath::Clamp(float(LocalMotion.X)/FMath::Max(1.f,Settings.MoveSpeed),-1.f,1.f);
    Out.SideSpeed=FMath::Clamp(float(LocalMotion.Y)/FMath::Max(1.f,Settings.MoveSpeed),-1.f,1.f);
    Out.bMoving=TankPresentationMotion.SizeSquared2D()>FMath::Square(25.f);
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
    Out.ShieldHit=ShieldHealth>0 && ShieldAge>=0 && ShieldAge<.32f?FMath::Sin(ShieldAge/.32f*PI):0;
    Out.HitDirection=GetActorTransform().InverseTransformVectorNoScale(VisualHitDirection);
    Out.Death=bDefeated?Progress(Time-StateStartedAt,DeathSeconds()):0;
    Out.Guard=!Out.bMage && ShieldHealth>0 && !bDefeated && State==EMCNutBossState::Idle?1.f:0.f;
    const bool Active=State==EMCNutBossState::Telegraph || State==EMCNutBossState::Executing || State==EMCNutBossState::Recovery;
    Out.bAllowTankScale=!Out.bMage && Active && Attack==EMCNutBossAttack::Roll;
    const bool Telegraph=State==EMCNutBossState::Telegraph,Recovery=State==EMCNutBossState::Recovery;
    // These phases advance one continuous clip. A gameplay state change must
    // not freeze its last pose and start another cosmetic transition.
    if(Active && Attack!=EMCNutBossAttack::Charge && Attack!=EMCNutBossAttack::NutRain && Attack!=EMCNutBossAttack::Roll)
        Out.VisualKey=2000+int32(BossRole)*100+int32(Attack);
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
    const EMCNutBossClip IdleClip=!Out.bMage && ShieldHealth<=0 && Has(EMCNutBossClip::UnshieldedIdle)?
        EMCNutBossClip::UnshieldedIdle:EMCNutBossClip::Idle;
    const EMCNutBossClip WalkClip=!Out.bMage && ShieldHealth<=0 && Has(EMCNutBossClip::UnshieldedWalk)?
        EMCNutBossClip::UnshieldedWalk:EMCNutBossClip::Walk;
    const bool MageMelee=Out.bMage && Attack==EMCNutBossAttack::Melee;
    Out.bHasMeleeClip=Has(EMCNutBossClip::Melee);
    float AttackWeight=Active?1.f:0.f,Phase=0;
    bool AttackLoop=false;
    float AttackRate=1.f;
    double AttackLoopStartedAt=0;
    EMCNutBossClip AttackClip=IdleClip;
    if(Out.bMage && Telegraph) AttackWeight=FMath::SmoothStep(0.f,BossSettings.AnimationBlendSeconds,float(Time-StateStartedAt));
    if(Recovery) AttackWeight=Out.bMage?1-FMath::SmoothStep(.65f,1.f,RecoveryProgress):
        1-FMath::SmoothStep(float(AttackEndAt-StateStartedAt)-Out.BlendSeconds,
        float(AttackEndAt-StateStartedAt),float(Time-StateStartedAt));
    if(Active && Attack==EMCNutBossAttack::Roll) {
        const bool NewExit=Recovery && Has(EMCNutBossClip::BallEnd);
        AttackClip=NewExit?EMCNutBossClip::BallEnd:EMCNutBossClip::Transform;
        Phase=Telegraph?WindupProgress:NewExit?Progress(Time-StateStartedAt,BossSettings.TankBallEndSeconds):Recovery?1-RecoveryProgress:1;
        if(NewExit) AttackWeight=1-FMath::SmoothStep(BossSettings.TankBallEndSeconds-Out.BlendSeconds,
            BossSettings.TankBallEndSeconds,float(Time-StateStartedAt));
    } else if(Active && Attack==EMCNutBossAttack::Jump) {
        AttackClip=EMCNutBossClip::Jump;
        Phase=Telegraph?BossSettings.TankJumpTakeoffFraction*WindupProgress:Recovery?
            FMath::Clamp(BossSettings.TankJumpImpactFraction+float(Time-StateStartedAt)/BossSettings.TankJumpAnimationSeconds,0.f,1.f):
            FMath::Lerp(BossSettings.TankJumpTakeoffFraction,BossSettings.TankJumpImpactFraction,Progress(Time-ResolveAt,BossSettings.JumpFlightSeconds));
        if(!Out.bMage) AttackWeight=1.f;
        Out.Airborne=State==EMCNutBossState::Executing?1.f:0.f;
    } else if(Active && Attack==EMCNutBossAttack::Melee && !Out.bMage) {
        AttackClip=TankMeleeVariant==2?EMCNutBossClip::MeleeCombo:TankMeleeVariant==1?EMCNutBossClip::Melee2:EMCNutBossClip::Melee;
        const float First=TankMeleeContactFraction(0);
        const float Exit=First+TankMeleeActiveSeconds()/TankMeleeSeconds();
        Phase=Telegraph?First*WindupProgress:Recovery?FMath::Clamp(Exit+float(Time-StateStartedAt)/TankMeleeSeconds(),0.f,1.f):
            First+float(Time-ResolveAt)/TankMeleeSeconds();
        AttackWeight=1.f;
        // One continuous authored performance spans tell, all contacts and recovery.
        Out.VisualKey=2400+TankMeleeVariant;
    } else if(Active && Attack==EMCNutBossAttack::Charge) {
        AttackClip=Telegraph?EMCNutBossClip::ChargeTell:Recovery?EMCNutBossClip::ChargeRecovery:EMCNutBossClip::ChargeLoop;
        Phase=Telegraph?WindupProgress:RecoveryProgress;
        if(Has(AttackClip)) {
            AttackLoop=State==EMCNutBossState::Executing;
            AttackRate=BossSettings.TankChargeLoopPlayRate;
            // Tell ends in the loop's first contact pose, so each charge starts at frame one.
            AttackLoopStartedAt=StateStartedAt;
        } else {
            AttackClip=WalkClip; AttackLoop=true; AttackRate=2.f;
        }
    } else if(Active && Out.bMage) {
        const bool Teleporting=Attack==EMCNutBossAttack::Teleport;
        AttackClip=Teleporting?EMCNutBossClip::HeavyCast:MageMelee?EMCNutBossClip::Melee:Attack==EMCNutBossAttack::Summon?EMCNutBossClip::Summon:
            Attack==EMCNutBossAttack::NutRain?EMCNutBossClip::Rain:EMCNutBossClip::Cast;
        const float Release=MageMelee?BossSettings.MageMeleeImpactFraction:BossSettings.MageCastReleaseFraction;
        // The shove is authored directly; casting aim, palm IK and spell release overlays would erase it.
        Out.Cast=MageMelee || Teleporting?0.f:AttackWeight;
        Out.CastReleaseFraction=Release;
        const float SinceRelease=float(Time-ResolveAt);
        Out.Release=!MageMelee && !Teleporting && SinceRelease>=0 && SinceRelease<.22f?FMath::Sin(SinceRelease/.22f*PI):0;
        Out.Channel=Attack==EMCNutBossAttack::NutRain && State==EMCNutBossState::Executing?1.f:0.f;
        const UAnimSequence* SpellClip=Has(AttackClip)?BossAnimations[int32(AttackClip)]:nullptr;
        Out.bAuthoredSpellGestures=!MageMelee && AuthoredSpellGestures(SpellClip);
        float ChannelEnd=Release;
        float ChannelPeriod=0.f;
        bool bAuthoredChannelLoop=false;
        if(Out.bAuthoredSpellGestures) {
            // A spell remains one performance across its server phases.
            Out.VisualKey=1000+int32(Attack);
            if(Attack==EMCNutBossAttack::NutRain) {
                const float MarkedEnd=SpellClip->EvaluateCurveData(TEXT("MCSpellChannelEnd"),FAnimExtractContext(0.,false));
                const float MarkedPeriod=SpellClip->EvaluateCurveData(TEXT("MCSpellChannelPeriod"),FAnimExtractContext(0.,false));
                // Only explicit, valid markers identify an authored loop. An
                // original one-shot holds its release pose instead of wrapping
                // an arbitrary section whose endpoints may not match.
                bAuthoredChannelLoop=FMath::IsFinite(MarkedEnd) && FMath::IsFinite(MarkedPeriod)
                    && MarkedEnd>=Release+.01f && MarkedEnd<=.99f && MarkedPeriod>=.3f && MarkedPeriod<=4.f;
                if(bAuthoredChannelLoop) {ChannelEnd=MarkedEnd;ChannelPeriod=MarkedPeriod;}
                // A loop can stop midway; crossfade from its evaluated pose.
                // A held one-shot is already continuous into its authored exit.
                if(Recovery && bAuthoredChannelLoop) Out.VisualKey+=100;
            }
        }
        // A channel sustains an authored mid-cast section for its complete server duration.
        // It never reaches the final clip frame merely because the FBX is shorter than the spell.
        Phase=Telegraph?Release*WindupProgress:Out.Channel>.5f?
            Release+.07f*(.5f+.5f*FMath::Sin(SinceRelease*3.5f)):
            FMath::Lerp(Release,1.f,RecoveryProgress);
        if(Out.bAuthoredSpellGestures && Attack==EMCNutBossAttack::NutRain) {
            if(Out.Channel>.5f) Phase=bAuthoredChannelLoop?
                FMath::Lerp(Release,ChannelEnd,float(FMath::Fmod(FMath::Max(0.f,SinceRelease)/ChannelPeriod,1.f))):Release;
            else if(Recovery) Phase=FMath::Lerp(ChannelEnd,1.f,RecoveryProgress);
        }
        Out.CastProgress=Phase;
        if(MageMelee) {
            Out.Melee=AttackWeight;
            Out.MeleePhase=Telegraph?.55f*WindupProgress:FMath::Lerp(.55f,1.f,RecoveryProgress);
        }
    } else if(State==EMCNutBossState::Retargeting && !Out.bMage) {
        AttackClip=HeadTurnVariant==0?EMCNutBossClip::HeadLeft:EMCNutBossClip::HeadRight;
        Phase=Progress(Time-StateStartedAt,BossSettings.TankHeadTurnSeconds);AttackWeight=1.f;
        Out.VisualKey=int32(HashCombine(GetTypeHash(3200+int32(HeadTurnVariant)),GetTypeHash(StateStartedAt)));
        // The authored turn already supplies the look; do not add procedural aim.
        Out.AimYaw=0;Out.AimPitch=0;
    } else if(State==EMCNutBossState::Falling) {
        AttackClip=Out.bMage?EMCNutBossClip::Idle:EMCNutBossClip::Jump; AttackWeight=1;
        Phase=FMath::Lerp(BossSettings.TankJumpTakeoffFraction,BossSettings.TankJumpImpactFraction,Progress(Time-StateStartedAt,BossSettings.EntranceSeconds));
        Out.Airborne=Out.bMage?0.f:1.f;
    }
    if(!Has(AttackClip)) {
        AttackClip=MageMelee?EMCNutBossClip::Idle:
            Out.bMage && Has(EMCNutBossClip::HeavyCast)?EMCNutBossClip::HeavyCast:IdleClip;
        if(!Active) AttackWeight=0;
    }
    auto Add=[&](EMCNutBossClip Slot,float Fraction,float Weight,bool Loop=false,float Rate=1.f,double LoopStartedAt=0,uint8 LocomotionMode=0) {
        if(Weight<=.001f || !Has(Slot)) return;
        UAnimSequence* Clip=BossAnimations[int32(Slot)]; const float Length=Clip->GetPlayLength();
        if(Length<=KINDA_SMALL_NUMBER) return;
        FMCNutBossClipSample Sample; Sample.Clip=Clip;Sample.Weight=Weight;Sample.bLoop=Loop;Sample.LocomotionMode=LocomotionMode;
        Sample.bAllowTankScale=Slot==EMCNutBossClip::Transform || Slot==EMCNutBossClip::BallEnd;
        Sample.Seconds=Loop?float(FMath::Fmod(FMath::Max(0.,Time-LoopStartedAt)*Rate,double(Length))):FMath::Clamp(Fraction,0.f,1.f)*Length;
        Out.Samples.Add(Sample);
    };
    const float Living=bDefeated?0.f:1.f,Locomotion=Living*(1-AttackWeight);
    if(Out.bMage) {
        const float Moving=FMath::SmoothStep(10.f,65.f,float(TankPresentationMotion.Size2D()));
        Add(IdleClip,0,Locomotion*(1-Moving),true);Add(EMCNutBossClip::Walk,0,Locomotion*Moving,true);
    } else {
        Add(IdleClip,0,Locomotion,true,1.f,0,1);
        if(WalkClip==EMCNutBossClip::UnshieldedWalk) Add(WalkClip,0,Locomotion,true,1.f,0,2);
        else {
        Add(EMCNutBossClip::Walk,0,Locomotion*(1-FMath::Abs(Side)),true,1.f,0,2);
        Add(EMCNutBossClip::WalkLeft,0,Locomotion*FMath::Max(0.f,-Side),true,1.f,0,2);
        Add(EMCNutBossClip::WalkRight,0,Locomotion*FMath::Max(0.f,Side),true,1.f,0,2);
        }
    }
    Add(AttackClip,Phase,Living*AttackWeight,AttackLoop,AttackRate,AttackLoopStartedAt);
    if(Out.bMage && Out.Hit>.01f) Add(EMCNutBossClip::Hit,Progress(HitAge,.4),Out.Hit*.3f);
    const bool Stunned=!Out.bMage && State==EMCNutBossState::Stunned && !bDefeated;
    if(Stunned && Has(EMCNutBossClip::ShieldBreak)) {
        Out.Samples.Reset();
        Add(EMCNutBossClip::ShieldBreak,0,1.f,true,1.f,StateStartedAt);
        Out.AuthoredTankHitWeight=1.f;
    }
    const EMCNutBossClip Reaction=Stunned?EMCNutBossClip::StunHit:
        bLastToolHitBlocked?EMCNutBossClip::Block:EMCNutBossClip::Hit;
    const float ReactionSeconds=Has(Reaction)?BossAnimations[int32(Reaction)]->GetPlayLength():.4f;
    // Block is a guarded loop: show a short response to each actual frontal hit.
    const float ReactionWindow=Reaction==EMCNutBossClip::Block?FMath::Min(.55f,ReactionSeconds):ReactionSeconds;
    if(!Out.bMage && !bDefeated && HitAge>=0 && HitAge<ReactionWindow && Has(Reaction)
        && (!Stunned || VisualHitAt>ShieldBrokenAt+.01)) {
        // A sharp reaction reads on contact, then gives control back to the same
        // attack timeline. Keep a telegraph or executing strike visible under it.
        const bool bCommitted=State==EMCNutBossState::Telegraph || State==EMCNutBossState::Executing;
        const float Weight=(bCommitted?.28f:1.f)*FMath::SmoothStep(0.f,Out.BlendSeconds,HitAge)
            *(1-FMath::SmoothStep(FMath::Max(Out.BlendSeconds,ReactionWindow-Out.BlendSeconds),ReactionWindow,HitAge));
        for(auto& Sample:Out.Samples) Sample.Weight*=1-Weight;
        Add(Reaction,Progress(HitAge,ReactionSeconds),Weight);
        Out.AuthoredTankHitWeight=Stunned?1.f:Weight;
        Out.VisualKey=int32(HashCombine(GetTypeHash(Out.VisualKey),GetTypeHash(VisualHitAt)));
    }
    Out.bHasDeathClip=Has(EMCNutBossClip::Death);
    if(bDefeated) Add(Out.bHasDeathClip?EMCNutBossClip::Death:IdleClip,Out.Death,1.f);
    if(Out.Samples.IsEmpty()) Add(IdleClip,0,1.f,true);
    if(!Out.bMage) {
        // Match the final pose mixture, including hit rescaling. A tiny marked
        // sample must not abruptly remove the idle guard or procedural breath.
        float Sum=0,Authored=0;
        for(const auto& Sample:Out.Samples) {
            const float Weight=Sample.Weight*(Sample.LocomotionMode==1?(Out.bMoving?0.f:1.f):Sample.LocomotionMode==2?(Out.bMoving?1.f:0.f):1.f);
            Sum+=Weight;
            if(Sample.Clip->EvaluateCurveData(TEXT("MCAuthoredCartoon"),FAnimExtractContext(0.,false))>.5f)
                Authored+=Weight;
        }
        Out.AuthoredTankMotionWeight=Sum>KINDA_SMALL_NUMBER?FMath::Clamp(Authored/Sum,0.f,1.f):0.f;
        Out.AuthoredTankHitWeight=Sum>KINDA_SMALL_NUMBER?FMath::Clamp(Out.AuthoredTankHitWeight/Sum,0.f,1.f):0.f;
        // Blend from the evaluated shielded pose when switching base clips;
        // this cosmetic key never changes the server attack/state timeline.
        if(ShieldHealth<=0) Out.VisualKey+=10000;
    }
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
    const float DeathScale=bDefeated?1-FMath::SmoothStep(DeathSeconds()-.3f,DeathSeconds(),DeathAge):1.f;
    const bool HasMage=BossRole==EMCNutBossRole::Mage && MageModel->GetSkeletalMeshAsset();
    MageModel->SetVisibility(HasMage);
    if(BossRole==EMCNutBossRole::Mage) {
        Visual->SetVisibility(!HasMage);
        Feedback->SetVisualTarget(HasMage?static_cast<UMeshComponent*>(MageModel.Get()):Visual.Get());
    }
    if(HasMage) {
        const FVector Scale=MageModelScale*DeathScale;
        const FRotator ModelRotation(0,BossSettings.MageModelYaw,0);
        MageModel->SetRelativeScale3D(Scale); MageModel->SetRelativeLocation(-ModelRotation.RotateVector(MageModelCenter*Scale)+FVector(0,0,BossSettings.MageHeight*.5f-Settings.BodyRadius));
        MageModel->SetRelativeRotation(ModelRotation); Visual->SetVisibility(false);
    }
    if(BossRole!=EMCNutBossRole::Tank) {TankModel->SetVisibility(false);TankBall->SetVisibility(false);return;}
    TankTurnPivot->SetRelativeLocation(FVector(0,0,-Settings.BodyRadius-4.f));
    const bool Rolling=Attack==EMCNutBossAttack::Roll && State==EMCNutBossState::Executing && !bDefeated;
    const bool Winding=Attack==EMCNutBossAttack::Roll && State==EMCNutBossState::Telegraph && !bDefeated;
    const bool Returning=Attack==EMCNutBossAttack::Roll && State==EMCNutBossState::Recovery && !bDefeated;
    const float Blend=BossSettings.TankAnimationBlendSeconds;
    float BallWeight=Rolling?1.f:Winding?FMath::SmoothStep(float(ResolveAt-Blend),float(ResolveAt),float(Time)):
        Returning?1-FMath::SmoothStep(0.f,Blend,float(Time-StateStartedAt)):0;
    if(!bDefeated) bTankWasBall=BallWeight>.5f;
    else BallWeight=bTankWasBall?1.f:0.f;
    if(!TankBall->GetStaticMesh()) BallWeight=0;
    const bool HasWarrior=TankModel->GetSkeletalMeshAsset()!=nullptr;
    TankBall->SetVisibility(BallWeight>.001f); TankModel->SetVisibility(HasWarrior && BallWeight<.999f);
    Visual->SetVisibility(!HasWarrior && BallWeight<.999f);
    Feedback->SetVisualTarget(BallWeight>.5f?static_cast<UMeshComponent*>(TankBall.Get()):
        HasWarrior?static_cast<UMeshComponent*>(TankModel.Get()):Visual.Get());
    const FQuat ParentRotation=GetActorQuat();
    const bool ShowingBall=BallWeight>.001f || (!HasWarrior && (Rolling || Returning));
    if(!ShowingBall) bTankRollRotationInitialized=false;
    FQuat Rotation=FQuat::Identity;
    if(ShowingBall) {
        if(!bTankRollRotationInitialized) {
            TankRollWorldRotation=ParentRotation; LastTankRollLocation=Position;
            bTankRollRotationInitialized=true;
        } else if(Rolling) {
            FVector Travel=Position-LastTankRollLocation; Travel.Z=0;
            const float Distance=Travel.Size();
            if(Distance>KINDA_SMALL_NUMBER && Distance<200.f) {
                // Physical rolling: top of the ball moves with travel, while
                // its contact point moves against it. World-space accumulation
                // also keeps orientation continuous through turns and bounces.
                const FVector Axis=FVector::CrossProduct(FVector::UpVector,Travel/Distance);
                const float Radius=FMath::Max(1.f,(TankBall->GetStaticMesh()?BossSettings.TankBallHeight:BossSettings.TankHeight)*.5f);
                TankRollWorldRotation=(FQuat(Axis,Distance/Radius)*TankRollWorldRotation).GetNormalized();
            }
        }
        LastTankRollLocation=Position;
        Rotation=(ParentRotation.Inverse()*TankRollWorldRotation).GetNormalized();
    }
    if(BallWeight>.001f) {
        const FVector Scale=TankBallScale*BallWeight;
        TankBall->SetRelativeScale3D(Scale); TankBall->SetRelativeRotation(Rotation);
        TankBall->SetRelativeLocation(-Rotation.RotateVector(TankBallCenter*Scale));
    }
    if(HasWarrior) {
        const FVector Scale=TankModelScale*(1-BallWeight);
        const FRotator ModelRotation(0,BossSettings.TankModelYaw,0);
        TankModel->SetRelativeScale3D(Scale); TankModel->SetRelativeRotation(ModelRotation);
        TankModel->SetRelativeLocation(-ModelRotation.RotateVector(TankModelGroundAnchor*Scale));
    } else if(NutMesh) {
        Visual->SetRelativeRotation(Rotation);
        Visual->SetRelativeLocation(-Rotation.RotateVector(NutMesh->GetBounds().Origin*MeshScale*DeathScale));
    }
}

bool AMCNutBoss::IsShieldProtectingFrom(FVector SourcePoint) const
{
    // Guard exists only in the matching visible stance. Every cast, attack and
    // recovery lowers it, so a player can read the full-damage window from the pose.
    if(BossRole!=EMCNutBossRole::Tank || ShieldHealth<=0 || !CanReceiveToolHit() || State!=EMCNutBossState::Idle) return false;
    const FVector Towards=(SourcePoint-GetActorLocation()).GetSafeNormal2D();
    return !Towards.IsNearlyZero() && FVector::DotProduct(Towards,GetActorForwardVector())>=FMath::Cos(FMath::DegreesToRadians(BossSettings.ShieldHalfAngle));
}

float AMCNutBoss::ReceiveToolDamage(float Damage,AMCToothCharacter* Source)
{
    if(!HasAuthority() || !CanReceiveToolHit() || !FMath::IsFinite(Damage) || Damage<=0
        || (Source && (Source->GetWorld()!=GetWorld() || !IsLiveTarget(Source)))) return 0;
    const bool Shielded=Source && IsShieldProtectingFrom(Source->GetActorLocation());
    bLastToolHitBlocked=Shielded;
    auto* Perks=UMCPerkComponent::ForActor(Source);
    if(Perks) Damage=Perks->WeaponDamage(Damage,this);
    const float ShieldBefore=ShieldHealth;
    VisualHitAt=Now(); VisualHitDirection=Source?(Source->GetActorLocation()-GetActorLocation()).GetSafeNormal2D():-GetActorForwardVector();
    if(Shielded) {
        // The incoming hit damages shield durability and deals 20% to the boss.
        // A broken shield stops protecting subsequent hits, not half of this hit.
        const float ShieldBonus=Perks && !Perks->bRogueExtraHit?Perks->RogueValue(EMCRogueStat::ShieldDamage):1.f;
        ShieldHealth=FMath::Max(0.f,ShieldHealth-Damage*ShieldBonus);
        Damage*=BossSettings.ShieldFrontDamageScale;
        const bool Broken=ShieldHealth<=0;
        if(Broken) {
            ShieldBrokenAt=VisualHitAt;
            CancelAttacks();Attack=EMCNutBossAttack::None;
            Enter(EMCNutBossState::Stunned,BossSettings.TankStunSeconds);
        }
        if(Broken || VisualHitAt-ShieldHitAt>=.15) {
            ShieldHitAt=VisualHitAt;
            const FVector Contact=AMCNutCombatEffect::GetShieldContactOrigin(this);
            TrackAttackActor(AMCNutCombatEffect::Spawn(this,Tongue,EMCNutCombatCue::ShieldHit,
                Contact,Contact-GetActorForwardVector()*Settings.BodyRadius,Settings.BodyRadius*(Broken?1.4f:1.f),0,Broken?1.2f:.32f,AttackSeed));
        }
    }
    // A light tool hit registers through the ordinary contact path, without
    // cancelling an entire boss attack each time four workers swing together.
    const float Before=Health;
    const AMCToothCharacter* PreviousTarget=Target;
    float Applied=0;
    if(Perks) {TGuardValue<bool> Resolved(Perks->bRogueExtraHit,true);Applied=Super::ReceiveToolDamage(Damage,Source);}
    else Applied=Super::ReceiveToolDamage(Damage,Source);
    if(!bDefeated) RecordTargetSwitch(PreviousTarget);
    if(Perks && (Applied>0 || ShieldHealth<ShieldBefore)) Perks->NotifyWeaponHit(this);
    if(Shielded && Applied<=0) {
        RefreshPresentation();ForceNetUpdate();
        if(!ActorHasTag(TEXT("MC.LegacyCombatFeedback"))) {
            Source->NotifyCameraImpact(550.f,-VisualHitDirection);
        }
    }
    Feedback->ReportAcceptedHit(Source,GetToolTargetPoint(Source?Source->GetActorLocation():GetActorLocation()+VisualHitDirection*Settings.BodyRadius),
        -VisualHitDirection,Before-Health,FMath::Max(0.f,ShieldBefore-ShieldHealth),Shielded,Health<=0.f);
    if(Applied>0) {
        if(BossRole==EMCNutBossRole::Mage && IsValid(Source)) {
            if(VisualHitAt-FocusWindowStartedAt>2) {FocusWindowStartedAt=VisualHitAt;FocusHits=0;}
            ++FocusHits;
        }
    }
    return Applied;
}

void AMCNutBoss::ReportDamageFeedback(AMCToothCharacter* Source,float Applied)
{
    // ReceiveToolDamage reports once after resolving shield durability and health overflow.
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
    if(Ability!=EMCNutBossAttack::None && Ability!=EMCNutBossAttack::Melee) ReadyAt=FMath::Max(ReadyAt,NextSpecialAt);
    return FMath::Max(0.f,float(ReadyAt-Now()));
}

void AMCNutBoss::Enter(EMCNutBossState Next,float Seconds)
{
    State=Next; StateStartedAt=Now(); AttackEndAt=StateStartedAt+FMath::Max(0.f,Seconds);
    if(LabStation.IsValid()) {
        if(Next==EMCNutBossState::Telegraph) ++LabTelegraphs;
        else if(Next==EMCNutBossState::Executing) ++LabExecutions;
        else if(Next==EMCNutBossState::Recovery) ++LabRecoveries;
    }
    RefreshPresentation(); ForceNetUpdate();
}

void AMCNutBoss::SelectTarget(bool /*bRandom*/)
{
    if(LabStation.IsValid()) {
        Target=IsLiveTarget(LabTarget.Get())?LabTarget.Get():nullptr;
        NextTargetAt=Now()+.7;
        return;
    }
    const AMCToothCharacter* PreviousTarget=Target;
    TArray<AMCToothCharacter*> Heroes;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(IsLiveTarget(*It)) Heroes.Add(*It);
    if(Heroes.IsEmpty()) { Target=nullptr; return; }
    if(auto* Director=AMCGameDirector::Find(GetWorld())) Target=Director->ChooseBossTarget(this,Heroes,Target);
    else {
        double Best=DBL_MAX; Target=nullptr;
        for(auto* Hero:Heroes) {
            const double Distance=FVector::DistSquared2D(GetActorLocation(),Hero->GetActorLocation());
            if(Distance<Best) { Best=Distance; Target=Hero; }
        }
    }
    RecordTargetSwitch(PreviousTarget);
    NextTargetAt=Now()+.7;
}

void AMCNutBoss::RecordTargetSwitch(const AMCToothCharacter* PreviousTarget)
{
    if(BossRole==EMCNutBossRole::Tank && IsValid(PreviousTarget) && IsLiveTarget(Target) && Target!=PreviousTarget) {
        ++TargetSwitchCount;
        if(TargetSwitchCount%6==0) PendingHeadTurns.Add(uint8(Random.RandRange(0,1)));
        ForceNetUpdate();
    }
}

void AMCNutBoss::TurnTowards(FVector Direction,float Dt)
{
    if(Direction.IsNearlyZero()) return;
    if(BossRole!=EMCNutBossRole::Tank) {SetActorRotation(Direction.Rotation());return;}
    // Guard, model and collision share a bounded facing. Crossing +/-180 also
    // follows the shortest arc instead of flipping the actor in one frame.
    const float Rate=180.f/FMath::Max(.08f,BossSettings.TankAnimationBlendSeconds);
    const float Yaw=FMath::FixedTurn(GetActorRotation().Yaw,Direction.Rotation().Yaw,Rate*FMath::Min(Dt,.05f));
    SetActorRotation(FRotator(0,Yaw,0));
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
    StartAttackCooldown(ResolveAt+RecoverySeconds(.55f));
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
    if(Next==EMCNutBossAttack::Melee && BossRole==EMCNutBossRole::Tank) TankMeleeVariant=uint8(TankMeleeSequence++%3);
    LockedTarget=Target->GetActorLocation(); AttackForward=(LockedTarget-LockedStart).GetSafeNormal2D();
    if(AttackForward.IsNearlyZero()) AttackForward=GetActorForwardVector();
    // Tank finishes turning during its visible windup, without snapping here.
    if(BossRole!=EMCNutBossRole::Tank) SetActorRotation(AttackForward.Rotation());
    float Windup=BossSettings.MeleeWindup;
    EMCNutCombatCue Cue=EMCNutCombatCue::MeleeTell;
    float CueRadius=BossSettings.MeleeRange,ActiveSeconds=.25f;
    if(Next==EMCNutBossAttack::Charge) {
        Windup=BossSettings.ChargeWindup;
        const float Distance=FMath::Clamp(float(FVector::Dist2D(LockedStart,LockedTarget)+220),350.f,BossSettings.ChargeDistance);
        bool Valid=false;
        for(float Scale:{1.f,.8f,.6f,.4f}) if(LockSurfacePoint(LockedStart+AttackForward*Distance*Scale,Settings.BodyRadius+5,LockedTarget)) {Valid=true;break;}
        if(!Valid) { Recover(.6f); return; }
        Cue=EMCNutCombatCue::ChargeTell; CueRadius=Settings.BodyRadius+35;
        ActiveSeconds=float(FVector::Dist2D(LockedStart,LockedTarget))/BossSettings.ChargeSpeed;
    } else if(Next==EMCNutBossAttack::Roll) {
        Windup=BossSettings.RollWindup;
        // The first visible corridor is fixed during the tell. Steering starts only
        // after it resolves, and cannot instantly follow a player's dodge.
        bool Valid=false;
        for(float Distance:{500.f,350.f,200.f})
            if(LockSurfacePoint(LockedStart+AttackForward*Distance,Settings.BodyRadius+5,LockedTarget)) {Valid=true;break;}
        if(!Valid) { Recover(.6f); return; }
        PlannedRollSeconds=Random.FRandRange(BossSettings.RollDuration,BossSettings.RollDurationMax);
        Cue=EMCNutCombatCue::RollTell; CueRadius=Settings.BodyRadius+25; ActiveSeconds=PlannedRollSeconds;
    } else if(Next==EMCNutBossAttack::Jump) {
        Windup=BossSettings.JumpWindup;
        const FVector Candidate=LockedStart+(LockedTarget-LockedStart).GetClampedToMaxSize(BossSettings.ChargeDistance);
        if(!LockSurfacePoint(Candidate,Settings.BodyRadius+5,LockedTarget)) { Recover(.6f); return; }
        Cue=EMCNutCombatCue::JumpTell; CueRadius=BossSettings.JumpRadius; ActiveSeconds=BossSettings.JumpFlightSeconds;
    } else if(Next==EMCNutBossAttack::Fireball) {
        Windup=BossSettings.FireballWindup;
        Cue=EMCNutCombatCue::FireCast; CueRadius=BossSettings.FireballRadius+25;
    } else if(Next==EMCNutBossAttack::Summon) {
        Windup=1.1f; CueRadius=45; Cue=EMCNutCombatCue::SummonTell;
        SummonPositions.Reset();
        const float CreepRadius=IsValid(EncounterOwner)?FMath::Clamp(EncounterOwner->Settings.Enemy.BodyRadius,20.f,80.f):BossSettings.CreepHeight*.5f;
        TArray<AMCFoodActor*> Available;
        if(IsValid(EncounterOwner)) EncounterOwner->GetAvailableRainNuts(Available);
        for(const auto* Nut:Available) SummonPositions.Add(Nut->GetActorLocation());
        for(FVector Point:SummonPositions) TrackAttackActor(AMCNutCombatEffect::Spawn(this,Tongue,Cue,Point,Point,CreepRadius+10,Windup,.3f,AttackSeed));
    } else if(Next==EMCNutBossAttack::NutRain) {
        Windup=BossSettings.RainWindup;
        const float LandingMargin=IsValid(EncounterOwner)?EncounterOwner->Settings.ImpactRadius:BossSettings.RainImpactRadius;
        FHitResult RainFloor;
        // A player may stand near a tongue edge or in a delivery lane. That
        // cannot silently cancel every cast; select a reachable storm centre.
        if(!Tongue->GameplaySpawnFootprint(LockedTarget,LandingMargin,RainFloor)
            && !Tongue->RandomGameplayInterestPoint(Random,LandingMargin,0,TConstArrayView<FVector>(),RainFloor)
            && !Tongue->RandomGameplaySpawnPoint(Random,LandingMargin,0,TConstArrayView<FVector>(),RainFloor)) {
            Recover(.6f); return;
        }
        LockedTarget=RainFloor.ImpactPoint+FVector(0,0,Settings.BodyRadius+4);
        Cue=EMCNutCombatCue::NutRain; CueRadius=BossSettings.RainRadius; ActiveSeconds=BossSettings.RainActiveSeconds;
    } else {
        bMeleeBetweenAbilities=false;
        LockedTarget=LockedStart+AttackForward*BossSettings.MeleeRange;
        if(BossRole==EMCNutBossRole::Tank) ActiveSeconds=TankMeleeActiveSeconds();
    }
    bResolved=false; AttackHits.Reset(); RainHitAt.Reset(); RollHitAt.Reset(); RainDropsResolved=0; MeleeHitsResolved=0;
    AttackStartedAt=Time; ResolveAt=Time+Windup;
    const bool Sustained=Next==EMCNutBossAttack::Charge || Next==EMCNutBossAttack::Roll
        || Next==EMCNutBossAttack::Jump || Next==EMCNutBossAttack::NutRain
        || (Next==EMCNutBossAttack::Melee && BossRole==EMCNutBossRole::Tank);
    const float Recovery=Next==EMCNutBossAttack::Jump?1.7f:Sustained?1.4f:Next==EMCNutBossAttack::Summon?1.2f:.9f;
    StartAttackCooldown(ResolveAt+(Sustained?ActiveSeconds:0.f)+RecoverySeconds(Recovery));
    Enter(EMCNutBossState::Telegraph,Windup);
    if(Next==EMCNutBossAttack::Fireball || Next==EMCNutBossAttack::Summon || Next==EMCNutBossAttack::NutRain)
        TrackAttackActor(AMCNutCombatEffect::Spawn(this,Tongue,Next==EMCNutBossAttack::Fireball?EMCNutCombatCue::CastCharge:EMCNutCombatCue::RitualCast,
            GetCastOrigin(),LockedTarget,Settings.BodyRadius,Windup,Next==EMCNutBossAttack::NutRain?ActiveSeconds:.25f,AttackSeed));
    else if(Next==EMCNutBossAttack::Roll)
        TrackAttackActor(AMCNutCombatEffect::Spawn(this,Tongue,EMCNutCombatCue::Transform,LockedStart,LockedTarget,Settings.BodyRadius,Windup,.3f,AttackSeed));
    if(Next!=EMCNutBossAttack::Summon && !(Next==EMCNutBossAttack::NutRain && IsValid(EncounterOwner))) TrackAttackActor(AMCNutCombatEffect::Spawn(this,Tongue,Cue,LockedStart,LockedTarget,
        CueRadius,Windup,ActiveSeconds,AttackSeed,BossSettings.RainImpactRadius,BossSettings.RainDrops,BossSettings.RainActiveSeconds/BossSettings.RainDrops));
    if(Next==EMCNutBossAttack::Melee && BossRole==EMCNutBossRole::Tank && TankMeleeVariant==2) {
        const FVector Slam=LockedStart+AttackForward*BossSettings.MeleeRange*.55f;
        TrackAttackActor(AMCNutCombatEffect::Spawn(this,Tongue,EMCNutCombatCue::JumpTell,Slam,Slam,
            BossSettings.MeleeRange*.8f,Windup+TankMeleeContactOffset(2),.12f,AttackSeed));
    }
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
        if(Delta.SizeSquared2D()>FMath::Square(Reach) || FMath::Abs(Delta.Z)>230) {
            if(Hero==Target) if(auto* Perks=UMCPerkComponent::ForActor(Hero)) Perks->NotifyDodgedAttack();
            continue;
        }
        if(!HasLineOfSight(Hero,Center+FVector(0,0,80))) continue;
        FVector Direction=Delta.GetSafeNormal2D(); if(Direction.IsNearlyZero()) Direction=AttackForward;
        if(!Hero->Status->Damage(Damage,Direction,this,Attack==EMCNutBossAttack::NutRain?EMCRogueDamageKind::FallingNut:EMCRogueDamageKind::Ordinary)) continue;
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
        if(Hero->Status->Damage(BossSettings.ChargeDamage,AttackForward,this)) {
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
        if(!Hero->Status->Damage(BossSettings.RollDamage,PushDirection,this)) continue;
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
    {
        FVector Desired=IsLiveTarget(Target)?(Target->GetActorLocation()-GetActorLocation()).GetSafeNormal2D():AttackForward;
        FVector Ahead;
        if(!LockSurfacePoint(GetActorLocation()+AttackForward*BossSettings.RollSpeed*.7f,Settings.BodyRadius+5,Ahead)) {
            // Start a bounded turn before the arena edge. Actual blocking hits
            // still reflect momentum, but normal pursuit never snaps toward a target.
            for(float Angle:{30.f,-30.f,60.f,-60.f,90.f,-90.f,120.f,-120.f,150.f,-150.f}) {
                const FVector Probe=AttackForward.RotateAngleAxis(Angle,FVector::UpVector);
                if(LockSurfacePoint(GetActorLocation()+Probe*BossSettings.RollSpeed*.7f,Settings.BodyRadius+5,Ahead)) {Desired=Probe;break;}
            }
        }
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
    // Rebase the rolling sphere against the final parent yaw in this frame.
    PresentTankModel(Time);
}

void AMCNutBoss::SummonCreeps()
{
    if(!HasAuthority() || !IsValid(EncounterOwner) || !IsValid(Tongue)) return;
    // Resolve from the remaining ground nuts, not a count captured during the
    // tell. Breaking a marked nut before release prevents that summon.
    TArray<AMCFoodActor*> Available;
    EncounterOwner->GetAvailableRainNuts(Available);
    UStaticMesh* Fallback=BossSettings.WholeMesh.LoadSynchronous();
    for(auto* Nut:Available) {
        UStaticMesh* Mesh=Nut->ItemMesh?Nut->ItemMesh.Get():Fallback;
        if(!Mesh) continue;
        FMCNutEnemySettings Creep=EncounterOwner->Settings.Enemy;
        Creep.MaxHealth=BossSettings.CreepHealth; Creep.BodyRadius=FMath::Clamp(Creep.BodyRadius,20.f,80.f);
        Creep.MoveSpeed=BossSettings.CreepMoveSpeed;
        FHitResult Floor; if(!Tongue->GameplaySpawnFootprint(Nut->GetActorLocation(),Creep.BodyRadius,Floor)) continue;
        const FTransform Pose(AttackForward.Rotation(),Floor.ImpactPoint+FVector(0,0,Creep.BodyRadius+4));
        auto* Enemy=GetWorld()->SpawnActorDeferred<AMCNutKamikaze>(AMCNutKamikaze::StaticClass(),Pose,EncounterOwner,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Enemy) continue;
        if(!Enemy->ConfigureKamikaze(Tongue,Mesh,HeightScale(Mesh,BossSettings.CreepHeight),Creep)) {Enemy->Destroy();continue;}
        Enemy->FinishSpawning(Pose);
        EncounterOwner->RegisterEncounterEnemy(Enemy);
        Nut->Dispose();
    }
    SummonPositions.Reset();
}

void AMCNutBoss::Recover(float Seconds)
{
    if(Attack!=EMCNutBossAttack::Melee && Attack!=EMCNutBossAttack::None) bMeleeBetweenAbilities=true;
    Seconds=RecoverySeconds(Seconds);
    StartAttackCooldown(Now()+Seconds);
    AttackStartedAt=-100; RollHitAt.Reset(); Enter(EMCNutBossState::Recovery,Seconds);
}

float AMCNutBoss::RecoverySeconds(float Requested) const
{
    if(BossRole!=EMCNutBossRole::Tank || Attack==EMCNutBossAttack::None) return Requested;
    // Play every source frame at full weight, then retain the final pose for
    // one blend interval. The following idle transition blends from that pose.
    if(Attack==EMCNutBossAttack::Melee) return FMath::Max(.01f,TankMeleeSeconds()*(1-TankMeleeContactFraction(TankMeleeVariant==2?2:0))-.1f)+BossSettings.TankAnimationBlendSeconds;
    if(Attack==EMCNutBossAttack::Jump) return BossSettings.TankJumpAnimationSeconds*(1-BossSettings.TankJumpImpactFraction)+BossSettings.TankAnimationBlendSeconds;
    const float Minimum=Attack==EMCNutBossAttack::Roll?BossSettings.RollRecoverySeconds:
        Attack==EMCNutBossAttack::Melee?1.2f:BossSettings.TankRecoverySeconds;
    return FMath::Max(Requested,Minimum);
}

float AMCNutBoss::TankMeleeSeconds() const
{
    return TankMeleeVariant==2?BossSettings.TankComboAnimationSeconds:BossSettings.TankMeleeAnimationSeconds;
}
float AMCNutBoss::TankMeleeContactFraction(int32 Contact) const
{
    if(TankMeleeVariant==2) return float(BossSettings.TankComboContactFractions[FMath::Clamp(Contact,0,2)]);
    return TankMeleeVariant==1?BossSettings.TankMelee2ImpactFraction:BossSettings.TankMeleeImpactFraction;
}
float AMCNutBoss::TankMeleeContactOffset(int32 Contact) const
{
    return (TankMeleeContactFraction(Contact)-TankMeleeContactFraction(0))*TankMeleeSeconds();
}
float AMCNutBoss::TankMeleeActiveSeconds() const
{
    return TankMeleeContactOffset(TankMeleeVariant==2?2:0)+.1f;
}
float AMCNutBoss::DeathSeconds() const
{
    return BossRole==EMCNutBossRole::Tank?BossSettings.TankDeathSeconds:BossSettings.DeathSeconds;
}

void AMCNutBoss::StartAttackCooldown(double RecoveryEndsAt)
{
    // Publish the predicted ready time during the tell, then correct it when
    // the attack actually finishes (a charge can stop early against a wall).
    switch(Attack) {
    case EMCNutBossAttack::Melee: NextMeleeAt=RecoveryEndsAt+BossSettings.MeleeCooldown;break;
    case EMCNutBossAttack::Charge: NextChargeAt=RecoveryEndsAt+BossSettings.ChargeCooldown;break;
    case EMCNutBossAttack::Jump: NextJumpAt=RecoveryEndsAt+BossSettings.JumpCooldown;break;
    case EMCNutBossAttack::Roll: NextRollAt=RecoveryEndsAt+BossSettings.RollCooldown;break;
    case EMCNutBossAttack::Fireball: NextFireballAt=RecoveryEndsAt+BossSettings.FireballCooldown;break;
    case EMCNutBossAttack::Summon: NextSummonAt=RecoveryEndsAt+BossSettings.SummonCooldown;break;
    case EMCNutBossAttack::NutRain: NextRainAt=RecoveryEndsAt+BossSettings.RainCooldown;break;
    case EMCNutBossAttack::Teleport: NextTeleportAt=RecoveryEndsAt+BossSettings.MageTeleportCooldown;break;
    default: return;
    }
    if(Attack!=EMCNutBossAttack::Melee) NextSpecialAt=RecoveryEndsAt+BossSettings.SpecialAttackGap;
}

void AMCNutBoss::ExecuteAttack(float Dt)
{
    const double Time=Now();
    if(State==EMCNutBossState::Telegraph) {
        if(BossRole==EMCNutBossRole::Tank) TurnTowards(AttackForward,Dt);
        if(Time<ResolveAt) return;
        const float Duration=Attack==EMCNutBossAttack::Charge?float(FVector::Dist2D(LockedStart,LockedTarget))/BossSettings.ChargeSpeed
            :Attack==EMCNutBossAttack::Jump?BossSettings.JumpFlightSeconds:Attack==EMCNutBossAttack::NutRain?BossSettings.RainActiveSeconds
            :Attack==EMCNutBossAttack::Roll?PlannedRollSeconds:
            Attack==EMCNutBossAttack::Melee && BossRole==EMCNutBossRole::Tank?TankMeleeActiveSeconds():.1f;
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
            // The warning and contact damage belong to each real falling food.
            // No invisible whole-region damage is added to the same cast.
            if(IsValid(EncounterOwner)) EncounterOwner->SpawnSpellNut(
                AMCNutCombatEffect::GetNutRainDropPoint(LockedTarget,BossSettings.RainRadius,AttackSeed,RainDropsResolved),
                int32(uint32(AttackSeed)+uint32(RainDropsResolved)));
        }
        if(Time>=AttackEndAt && RainDropsResolved>=BossSettings.RainDrops) Recover(1.4f);
    } else if(Attack==EMCNutBossAttack::Melee && BossRole==EMCNutBossRole::Tank) {
        const int32 ContactCount=TankMeleeVariant==2?3:1;
        while(MeleeHitsResolved<ContactCount && Time>=ResolveAt+TankMeleeContactOffset(MeleeHitsResolved)) {
            AttackHits.Reset();
            const bool GroundHit=TankMeleeVariant==2 && MeleeHitsResolved==2;
            const FVector Center=GroundHit?LockedStart+AttackForward*BossSettings.MeleeRange*.55f:LockedStart;
            if(!GroundHit) for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
                if(FVector::DotProduct((It->GetActorLocation()-LockedStart).GetSafeNormal2D(),AttackForward)<.35f) AttackHits.Add(*It);
            DamageArea(Center,BossSettings.MeleeRange*(GroundHit?.8f:1.f),BossSettings.MeleeDamage,BossSettings.TankMeleePush,&AttackHits);
            TrackAttackActor(AMCNutCombatEffect::Spawn(this,Tongue,GroundHit?EMCNutCombatCue::SlamImpact:EMCNutCombatCue::MeleeSlash,
                Center,GroundHit?Center:LockedTarget,BossSettings.MeleeRange*(GroundHit?.8f:1.f),0,GroundHit?.6f:.22f,AttackSeed+MeleeHitsResolved));
            ++MeleeHitsResolved;
        }
        if(Time>=AttackEndAt) Recover(2.f);
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
    AMCNutCombatEffect::UpdateShieldDamageVisual(this);
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
    if(State==EMCNutBossState::Stunned) {
        // Further hits play Damage2 but never restart or extend this window.
        if(Time<AttackEndAt) return;
        Enter(EMCNutBossState::Idle,0);
        NextMeleeAt=FMath::Max(NextMeleeAt,Time+.5);
        NextSpecialAt=FMath::Max(NextSpecialAt,Time+BossSettings.SpecialAttackGap);
    }
    if(State==EMCNutBossState::Retargeting) {
        if(IsLiveTarget(Target)) TurnTowards((Target->GetActorLocation()-GetActorLocation()).GetSafeNormal2D(),Dt);
        if(Time<AttackEndAt) return;
        Enter(EMCNutBossState::Idle,0);
    }
    if(State==EMCNutBossState::Telegraph || State==EMCNutBossState::Executing) {ExecuteAttack(Dt);return;}
    if(State==EMCNutBossState::Recovery) {
        if(Time<AttackEndAt) return;
        Attack=EMCNutBossAttack::None; Enter(EMCNutBossState::Idle,0);
    }
    if(LabStation.IsValid()) return;
    if(!IsLiveTarget(Target) || Time>=NextTargetAt) SelectTarget(false);
    if(!IsLiveTarget(Target)) return;
    if(BossRole==EMCNutBossRole::Tank && !PendingHeadTurns.IsEmpty()) {
        HeadTurnVariant=PendingHeadTurns[0];PendingHeadTurns.RemoveAt(0);
        Attack=EMCNutBossAttack::None;Enter(EMCNutBossState::Retargeting,BossSettings.TankHeadTurnSeconds);
        return;
    }
    if(BossRole==EMCNutBossRole::Mage && !IsValid(EncounterOwner) && Time>=NextSpecialAt && Time>=NextTeleportAt && IsMageFocused()) {
        BeginTeleport();
        if(State==EMCNutBossState::Telegraph) return;
    }
    const FVector Direction=(Target->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
    TurnTowards(Direction,Dt);
    const double Distance=FVector::Dist2D(GetActorLocation(),Target->GetActorLocation());
    const bool SpecialReady=Time>=NextSpecialAt && (BossRole==EMCNutBossRole::Tank?
        Time>=NextRollAt || Time>=NextJumpAt:
        Time>=NextRainAt || Time>=NextSummonAt || Time>=NextFireballAt);
    // Close combat has a turn between specials; once it resolves, ready special
    // abilities retain priority. During cooldowns ordinary swings can continue.
    if((BossRole==EMCNutBossRole::Tank || !IsValid(EncounterOwner)) && Time>=NextMeleeAt && (bMeleeBetweenAbilities || !SpecialReady)
        && Distance<BossSettings.MeleeRange*.95f && HasLineOfSight(Target,GetActorLocation())) {
        BeginAttack(EMCNutBossAttack::Melee);return;
    }
    if(Time>=NextSpecialAt && BossRole==EMCNutBossRole::Tank) {
        if(Time>=NextRollAt) {BeginAttack(EMCNutBossAttack::Roll);return;}
        if(Time>=NextJumpAt) {BeginAttack(EMCNutBossAttack::Jump);return;}
    } else if(Time>=NextSpecialAt && BossRole==EMCNutBossRole::Mage) {
        if(Time>=NextRainAt) {BeginAttack(EMCNutBossAttack::NutRain);return;}
        if(Time>=NextSummonAt && IsValid(EncounterOwner)) {
            TArray<AMCFoodActor*> Available; EncounterOwner->GetAvailableRainNuts(Available);
            if(!Available.IsEmpty()) {BeginAttack(EMCNutBossAttack::Summon);return;}
        }
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
    SetLifeSpan(DeathSeconds());
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
    DOREPLIFETIME(AMCNutBoss,ShieldHealth); DOREPLIFETIME(AMCNutBoss,NextSpecialAt);
    DOREPLIFETIME(AMCNutBoss,ShieldBrokenAt);
    DOREPLIFETIME(AMCNutBoss,TankMeleeVariant);DOREPLIFETIME(AMCNutBoss,bLastToolHitBlocked);
    DOREPLIFETIME(AMCNutBoss,TargetSwitchCount);DOREPLIFETIME(AMCNutBoss,HeadTurnVariant);
    DOREPLIFETIME(AMCNutBoss,NextMeleeAt); DOREPLIFETIME(AMCNutBoss,NextChargeAt); DOREPLIFETIME(AMCNutBoss,NextJumpAt);
    DOREPLIFETIME(AMCNutBoss,NextRollAt); DOREPLIFETIME(AMCNutBoss,NextFireballAt); DOREPLIFETIME(AMCNutBoss,NextSummonAt);
    DOREPLIFETIME(AMCNutBoss,NextRainAt); DOREPLIFETIME(AMCNutBoss,NextTeleportAt);
}
