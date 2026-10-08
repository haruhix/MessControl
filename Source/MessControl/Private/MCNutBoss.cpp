#include "MCNutBoss.h"

#include "MCNutCombatEffect.h"
#include "MCNutSpellProjectile.h"
#include "MCNutRainEvent.h"
#include "MCGameState.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
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
    Shield=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shield")); Shield->SetupAttachment(Body);
    Kernel=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Kernel")); Kernel->SetupAttachment(Body);
    OpenShell=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OpenShell")); OpenShell->SetupAttachment(Body);
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
    Settings=FMCNutEnemySettings(); Settings.BodyRadius=BossRole==EMCNutBossRole::Tank?100:90;
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
    if(HasAuthority()) {
        if(State==EMCNutBossState::Falling) SetActorLocation(LockedStart,false);
        const double Ready=ResolveAt;
        NextTargetAt=Ready; NextMeleeAt=Ready+1.5; NextChargeAt=Ready+3; NextJumpAt=Ready+7;
        NextFireballAt=Ready+6; NextSummonAt=Ready+10; NextRainAt=Ready+13;
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
    auto* Shell=BossSettings.ShellMesh.LoadSynchronous();
    SetCenteredMesh(Shield,Shell,BossSettings.TankHeight*.65f,FVector(Settings.BodyRadius*.88f,0,-5),FRotator(0,90,0));
    Shield->SetVisibility(!bDefeated && BossRole==EMCNutBossRole::Tank && State!=EMCNutBossState::Falling
        && State!=EMCNutBossState::Recovery && !(State==EMCNutBossState::Executing && (Attack==EMCNutBossAttack::Charge || Attack==EMCNutBossAttack::Jump)));
    SetCenteredMesh(OpenShell,Shell,BossSettings.MageHeight,FVector(-Settings.BodyRadius*.38f,0,-12),FRotator(0,-20,-22));
    OpenShell->SetVisibility(!bDefeated && BossRole==EMCNutBossRole::Mage);
    Kernel->SetVisibility(false); // the inherited main mesh is the exposed kernel for the mage
    Label->SetText(FText::FromString(FString::Printf(TEXT("%s  %d"),BossRole==EMCNutBossRole::Tank?TEXT("NUT TANK"):TEXT("NUT MAGE"),FMath::CeilToInt(Health))));
    Label->SetTextRenderColor(BossRole==EMCNutBossRole::Tank?FColor(240,185,85):FColor(225,115,255));
    Label->SetRelativeLocation(FVector(0,0,(BossRole==EMCNutBossRole::Tank?BossSettings.TankHeight:BossSettings.MageHeight)*.5f+38));
}

bool AMCNutBoss::IsShieldProtectingFrom(FVector SourcePoint) const
{
    if(BossRole!=EMCNutBossRole::Tank || !CanReceiveToolHit() || State==EMCNutBossState::Recovery
        || (State==EMCNutBossState::Executing && (Attack==EMCNutBossAttack::Charge || Attack==EMCNutBossAttack::Jump))) return false;
    const FVector Towards=(SourcePoint-GetActorLocation()).GetSafeNormal2D();
    return !Towards.IsNearlyZero() && FVector::DotProduct(Towards,GetActorForwardVector())>=FMath::Cos(FMath::DegreesToRadians(BossSettings.ShieldHalfAngle));
}

float AMCNutBoss::ReceiveToolDamage(float Damage,AMCToothCharacter* Source)
{
    if(!HasAuthority() || !CanReceiveToolHit() || !FMath::IsFinite(Damage) || Damage<=0
        || (Source && (Source->GetWorld()!=GetWorld() || !IsLiveTarget(Source)))) return 0;
    if(Source && IsShieldProtectingFrom(Source->GetActorLocation())) Damage*=BossSettings.ShieldFrontDamageScale;
    // A light tool hit registers through the ordinary contact path, without
    // cancelling an entire boss attack each time four workers swing together.
    return Super::ReceiveToolDamage(Damage,Source);
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
    if(Next==EMCNutBossAttack::Charge || Next==EMCNutBossAttack::Jump || Next==EMCNutBossAttack::NutRain) SelectTarget(true);
    if(!IsLiveTarget(Target)) return;
    Attack=Next; AttackSeed=Random.RandRange(1,MAX_int32); LockedStart=GetActorLocation();
    LockedTarget=Target->GetActorLocation(); AttackForward=(LockedTarget-LockedStart).GetSafeNormal2D();
    if(AttackForward.IsNearlyZero()) AttackForward=GetActorForwardVector();
    SetActorRotation(AttackForward.Rotation());
    float Windup=BossSettings.MeleeWindup;
    EMCNutCombatCue Cue=EMCNutCombatCue::JumpTell;
    float CueRadius=BossSettings.MeleeRange,ActiveSeconds=.25f;
    if(Next==EMCNutBossAttack::Charge) {
        Windup=BossSettings.ChargeWindup; NextChargeAt=Time+BossSettings.ChargeCooldown;
        const float Distance=FMath::Clamp(float(FVector::Dist2D(LockedStart,LockedTarget)+220),350.f,BossSettings.ChargeDistance);
        bool Valid=false;
        for(float Scale:{1.f,.8f,.6f,.4f}) if(LockSurfacePoint(LockedStart+AttackForward*Distance*Scale,Settings.BodyRadius+5,LockedTarget)) {Valid=true;break;}
        if(!Valid) { Recover(.6f); return; }
        Cue=EMCNutCombatCue::ChargeTell; CueRadius=Settings.BodyRadius+35;
        ActiveSeconds=float(FVector::Dist2D(LockedStart,LockedTarget))/BossSettings.ChargeSpeed;
    } else if(Next==EMCNutBossAttack::Jump) {
        Windup=BossSettings.JumpWindup; NextJumpAt=Time+BossSettings.JumpCooldown;
        const FVector Candidate=LockedStart+(LockedTarget-LockedStart).GetClampedToMaxSize(BossSettings.ChargeDistance);
        if(!LockSurfacePoint(Candidate,Settings.BodyRadius+5,LockedTarget)) { Recover(.6f); return; }
        CueRadius=BossSettings.JumpRadius; ActiveSeconds=BossSettings.JumpFlightSeconds;
    } else if(Next==EMCNutBossAttack::Fireball) {
        Windup=BossSettings.FireballWindup; NextFireballAt=Time+BossSettings.FireballCooldown;
        Cue=EMCNutCombatCue::FireCast; CueRadius=BossSettings.FireballRadius+25;
    } else if(Next==EMCNutBossAttack::Summon) {
        Windup=1.1f; NextSummonAt=Time+BossSettings.SummonCooldown; CueRadius=45; Cue=EMCNutCombatCue::SummonTell;
        SummonPositions.Reset();
        const int32 Count=FMath::Min(4,BossSettings.SummonCount+(PartyPlayers-1)/2);
        for(int32 Index=0;Index<Count;++Index) for(int32 Try=0;Try<20;++Try) {
            const float Angle=Random.FRandRange(-PI,PI),Distance=Random.FRandRange(240,440);
            FHitResult Floor; const FVector Point=GetActorLocation()+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Distance;
            if(Tongue->GameplaySpawnFootprint(Point,40,Floor)) {
                const FVector Spawn=Floor.ImpactPoint+FVector(0,0,32);
                if(SummonPositions.ContainsByPredicate([&](FVector Other){return FVector::DistSquared2D(Other,Spawn)<FMath::Square(90.f);})) continue;
                SummonPositions.Add(Spawn); break;
            }
        }
        for(FVector Point:SummonPositions) TrackAttackActor(AMCNutCombatEffect::Spawn(this,Tongue,Cue,Point,Point,45,Windup,.3f,AttackSeed));
    } else if(Next==EMCNutBossAttack::NutRain) {
        Windup=BossSettings.RainWindup; NextRainAt=Time+BossSettings.RainCooldown;
        if(!LockSurfacePoint(LockedTarget,BossSettings.RainRadius*.5f,LockedTarget)) { Recover(.6f); return; }
        Cue=EMCNutCombatCue::NutRain; CueRadius=BossSettings.RainRadius; ActiveSeconds=BossSettings.RainActiveSeconds;
    } else { NextMeleeAt=Time+BossSettings.MeleeCooldown; LockedTarget=LockedStart; }
    bResolved=false; AttackHits.Reset(); RainHitAt.Reset(); RainDropsResolved=0;
    AttackStartedAt=Time; ResolveAt=Time+Windup;
    Enter(EMCNutBossState::Telegraph,Windup);
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
        if(Hero->ToothPhysics && Push>0) Hero->ToothPhysics->ApplyHit(Direction*Push+FVector(0,0,55),Hero->GetActorLocation());
    }
}

void AMCNutBoss::DamageChargeSegment(FVector Start,FVector End)
{
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) {
        auto* Hero=*It; if(!IsLiveTarget(Hero) || AttackHits.Contains(Hero)) continue;
        const FVector Closest=FMath::ClosestPointOnSegment(Hero->GetActorLocation(),Start,End);
        const float Reach=Settings.BodyRadius+Hero->GetCapsuleComponent()->GetScaledCapsuleRadius()+15;
        if(FVector::DistSquared2D(Hero->GetActorLocation(),Closest)>FMath::Square(Reach)
            || FMath::Abs(Hero->GetActorLocation().Z-Closest.Z)>180 || !HasLineOfSight(Hero,Closest)) continue;
        if(Hero->Status->Damage(BossSettings.ChargeDamage,AttackForward)) {
            AttackHits.Add(Hero);
            if(Hero->ToothPhysics) Hero->ToothPhysics->ApplyHit(AttackForward*260+FVector(0,0,45),Hero->GetActorLocation());
        }
    }
}

void AMCNutBoss::SummonCreeps()
{
    int32 Live=0;
    for(TActorIterator<AMCNutEnemy> It(GetWorld());It;++It)
        if(!Cast<AMCNutBoss>(*It) && It->GetOwner()==EncounterOwner && It->IsEncounterAlive()) ++Live;
    UStaticMesh* Mesh=BossSettings.KernelMesh.LoadSynchronous(); if(!Mesh || !IsValid(EncounterOwner)) return;
    for(FVector Point:SummonPositions) {
        if(Live>=BossSettings.MaxLiveCreeps) break;
        FHitResult Floor; if(!Tongue->GameplaySpawnFootprint(Point,35,Floor)) continue;
        FMCNutEnemySettings Creep=EncounterOwner->Settings.Enemy;
        Creep.MaxHealth=BossSettings.CreepHealth; Creep.BodyRadius=FMath::Clamp(Creep.BodyRadius,20.f,45.f);
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
    AttackStartedAt=-100; Enter(EMCNutBossState::Recovery,Seconds);
}

void AMCNutBoss::ExecuteAttack(float Dt)
{
    const double Time=Now();
    if(State==EMCNutBossState::Telegraph) {
        if(Time<ResolveAt) return;
        const float Duration=Attack==EMCNutBossAttack::Charge?float(FVector::Dist2D(LockedStart,LockedTarget))/BossSettings.ChargeSpeed
            :Attack==EMCNutBossAttack::Jump?BossSettings.JumpFlightSeconds:Attack==EMCNutBossAttack::NutRain?BossSettings.RainActiveSeconds:.1f;
        Enter(EMCNutBossState::Executing,Duration);
    }
    if(Attack==EMCNutBossAttack::Charge) {
        const FVector Previous=GetActorLocation();
        const float Travel=FMath::Clamp(float(Time-ResolveAt)*BossSettings.ChargeSpeed,0.f,float(FVector::Dist2D(LockedStart,LockedTarget)));
        FVector Goal;
        if(!LockSurfacePoint(LockedStart+AttackForward*Travel,Settings.BodyRadius+5,Goal)) { Recover(1.4f); return; }
        FHitResult Hit; SetActorLocation(Goal,true,&Hit); DamageChargeSegment(Previous,GetActorLocation());
        if(Time>=AttackEndAt || (Hit.bBlockingHit && FVector::DistSquared2D(Previous,GetActorLocation())<4)) Recover(1.4f);
    } else if(Attack==EMCNutBossAttack::Jump) {
        const float Alpha=FMath::Clamp(float(Time-ResolveAt)/BossSettings.JumpFlightSeconds,0.f,1.f);
        FVector Goal=FMath::Lerp(LockedStart,LockedTarget,Alpha)+FVector(0,0,FMath::Sin(Alpha*PI)*350);
        SetActorLocation(Goal,false);
        if(Alpha>=1 && !bResolved) {
            bResolved=true; DamageArea(LockedTarget,BossSettings.JumpRadius,BossSettings.JumpDamage,220,&AttackHits);
            TrackAttackActor(AMCNutCombatEffect::Spawn(this,Tongue,EMCNutCombatCue::SlamImpact,LockedTarget,LockedTarget,BossSettings.JumpRadius,0,.6f,AttackSeed));
            Recover(1.7f);
        }
    } else if(Attack==EMCNutBossAttack::NutRain) {
        const double Cadence=BossSettings.RainActiveSeconds/BossSettings.RainDrops;
        for(int32 Budget=0;RainDropsResolved<BossSettings.RainDrops && Time>=ResolveAt+RainDropsResolved*Cadence && Budget<2;++Budget,++RainDropsResolved) {
            const FVector Candidate=AMCNutCombatEffect::GetNutRainDropPoint(LockedTarget,BossSettings.RainRadius,AttackSeed,RainDropsResolved);
            FHitResult Floor; if(!Tongue->InteriorSurfacePoint(Candidate,BossSettings.RainImpactRadius,Floor)) continue;
            TSet<TWeakObjectPtr<AMCToothCharacter>> RecentlyHit;
            for(const auto& Pair:RainHitAt) if(Time-Pair.Value<BossSettings.RainHitGap) RecentlyHit.Add(Pair.Key);
            const auto Before=RecentlyHit;
            DamageArea(Floor.ImpactPoint,BossSettings.RainImpactRadius,BossSettings.RainDamage,70,&RecentlyHit);
            for(const auto& Hero:RecentlyHit) if(!Before.Contains(Hero)) RainHitAt.Add(Hero,Time);
        }
        if(Time>=AttackEndAt && RainDropsResolved>=BossSettings.RainDrops) Recover(1.4f);
    } else if(!bResolved) {
        bResolved=true;
        if(Attack==EMCNutBossAttack::Fireball) {
            const FVector Muzzle=GetActorLocation()+AttackForward*(Settings.BodyRadius+32)+FVector(0,0,30);
            TrackAttackActor(AMCNutSpellProjectile::Spawn(this,Tongue,Muzzle,LockedTarget,BossSettings.FireballSpeed,BossSettings.FireballDamage,BossSettings.FireballRadius));
        } else if(Attack==EMCNutBossAttack::Summon) SummonCreeps();
        else if(Attack==EMCNutBossAttack::Melee) {
            for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) {
                const FVector Direction=(It->GetActorLocation()-LockedStart).GetSafeNormal2D();
                if(FVector::DotProduct(Direction,AttackForward)<.35f) AttackHits.Add(*It);
            }
            DamageArea(LockedStart,BossSettings.MeleeRange,BossSettings.MeleeDamage,90,&AttackHits);
        }
        Recover(Attack==EMCNutBossAttack::Summon?1.2f:.9f);
    }
}

void AMCNutBoss::Tick(float Dt)
{
    AActor::Tick(Dt); const double Time=Now(); TickPresentation(Time);
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
    if(BossRole==EMCNutBossRole::Tank) {
        if(Time>=NextJumpAt) {BeginAttack(EMCNutBossAttack::Jump);return;}
        if(Time>=NextChargeAt) {BeginAttack(EMCNutBossAttack::Charge);return;}
    } else {
        if(Time>=NextRainAt) {BeginAttack(EMCNutBossAttack::NutRain);return;}
        if(Time>=NextSummonAt) {BeginAttack(EMCNutBossAttack::Summon);return;}
        if(Time>=NextFireballAt) {BeginAttack(EMCNutBossAttack::Fireball);return;}
    }
    const FVector Direction=(Target->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
    if(!Direction.IsNearlyZero()) SetActorRotation(Direction.Rotation());
    const double Distance=FVector::Dist2D(GetActorLocation(),Target->GetActorLocation());
    if(Time>=NextMeleeAt && Distance<BossSettings.MeleeRange*.9f && HasLineOfSight(Target,GetActorLocation())) {BeginAttack(EMCNutBossAttack::Melee);return;}
    if(BossRole==EMCNutBossRole::Tank && Distance>BossSettings.MeleeRange*.8f) MoveOnTongue(Direction,Dt);
    else if(BossRole==EMCNutBossRole::Mage && Distance<330) MoveOnTongue(-Direction,Dt);
    else if(BossRole==EMCNutBossRole::Mage && Distance>720) MoveOnTongue(Direction,Dt);
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
    else if(Attack==EMCNutBossAttack::Charge || Attack==EMCNutBossAttack::Fireball) {
        const FVector Closest=FMath::ClosestPointOnSegment(Point,LockedStart,LockedTarget);
        const float Radius=Attack==EMCNutBossAttack::Charge?Settings.BodyRadius+35:BossSettings.FireballRadius+25;
        if(FVector::DistSquared2D(Point,Closest)>FMath::Square(Radius+HeroRadius)) return false;
        EscapeDirection=(Point-Closest).GetSafeNormal2D();
        if(EscapeDirection.IsNearlyZero()) EscapeDirection=FVector::CrossProduct(AttackForward,FVector::UpVector);
    } else if(Attack==EMCNutBossAttack::Jump || Attack==EMCNutBossAttack::NutRain) {
        const float Radius=Attack==EMCNutBossAttack::Jump?BossSettings.JumpRadius:BossSettings.RainRadius;
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
    ActiveAttacks.Reset(); SummonPositions.Reset();
}

void AMCNutBoss::Defeat()
{
    CancelAttacks(); State=EMCNutBossState::Defeated; Attack=EMCNutBossAttack::None;
    Super::Defeat();
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
}
