#include "MCNutBoss.h"

#include "MCActiveSkillMotion.h"
#include "MCDefenseSprayPassives.h"
#include "MCFoodActor.h"
#include "MCNutBossImpact.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

namespace
{
constexpr float ChainHeadRadius=28.f;
constexpr float ChainPlayerSpeed=1500.f;
constexpr float ChainNutSpeed=1200.f;
constexpr float ChainReelSeconds=.34f;
constexpr float ChainRecoverySeconds=1.25f;

bool IsChainNut(const AMCFoodActor* Food)
{
    return IsValid(Food) && !Food->IsActorBeingDestroyed() && Food->Phase==EMCFoodPhase::Free
        && Food->IsNutKineticsActive() && !Food->IsMouthEntryActive() && !Food->IsCutTransactionPending()
        && !Food->IsCutTransactionStaged() && Food->Body && Food->Body->IsSimulatingPhysics();
}

float TimeAlpha(double Time,double Start,double End)
{
    return FMath::Clamp(float((Time-Start)/FMath::Max(.001,End-Start)),0.f,1.f);
}
}

float AMCNutBoss::GetChainPhaseAlpha() const
{
    if(Attack!=EMCNutBossAttack::Chain) return 0.f;
    return State==EMCNutBossState::Telegraph?TimeAlpha(Now(),StateStartedAt,ResolveAt)
        :TimeAlpha(Now(),ChainPhaseStartedAt,ChainPhaseEndsAt);
}

bool AMCNutBoss::BeginChainPreview(AMCToothCharacter* Subject)
{
#if WITH_EDITOR
    if(HasAuthority() && GetWorld() && GetWorld()->WorldType==EWorldType::PIE) return BeginChain(Subject);
#endif
    return false;
}

bool AMCNutBoss::TryBeginChain(double Time)
{
    const FVector Away=Target->GetActorLocation()-GetActorLocation();
    const double Distance=Away.Size2D();
    const bool KeepingDistance=Distance>=BossSettings.TankChainMinRange && Distance<=BossSettings.TankChainRange
        && FMath::Abs(Away.Z)<400. && FVector::DotProduct(Target->GetVelocity(),Away.GetSafeNormal2D())>=-50.;
    if(!KeepingDistance || !MCActiveSkillMotion::IsEligible(Target,false)) {
        ChainRetreatTarget.Reset();ChainRetreatStartedAt=-1.;return false;
    }
    if(ChainRetreatTarget.Get()!=Target) {ChainRetreatTarget=Target;ChainRetreatStartedAt=Time;}
    if(Time<NextSpecialAt || Time<NextChainAt || Time-ChainRetreatStartedAt<BossSettings.TankChainRetreatSeconds) return false;

    // The selected runner can hide behind a whole nut intentionally. An
    // ordinary obstacle in the line of sight will never start a warning.
    FHitResult Blocker;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCKnightChainAim),false,this);
    Query.AddIgnoredActor(Target);
    if(GetWorld()->LineTraceSingleByChannel(Blocker,GetActorLocation(),Target->GetActorLocation(),ECC_Visibility,Query)
        && !IsChainNut(Cast<AMCFoodActor>(Blocker.GetActor()))) return false;
    return BeginChain(Target);
}

bool AMCNutBoss::BeginChain(AMCToothCharacter* Subject)
{
    if(!HasAuthority() || BossRole!=EMCNutBossRole::Tank || State!=EMCNutBossState::Idle
        || !IsEncounterAlive() || !IsLiveTarget(Subject)) return false;
    const FVector Direction=(Subject->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
    if(Direction.IsNearlyZero()) return false;

    Target=Subject;ChainBaitTarget=Subject;ChainStrikeTarget.Reset();ChainRetreatTarget.Reset();ChainRetreatStartedAt=-1.;
    Attack=EMCNutBossAttack::Chain;AttackSeed=Random.RandRange(1,MAX_int32);
    AttackForward=Direction;
    LockedTarget=Subject->GetActorLocation();
    LockedStart=GetChainOrigin();
    ChainFlightEnd=LockedStart+(LockedTarget-LockedStart).GetSafeNormal(SMALL_NUMBER,AttackForward)*BossSettings.TankChainRange;
    ChainTip=LockedStart;
    AttackStartedAt=Now();ResolveAt=AttackStartedAt+BossSettings.TankChainWindup;
    SetChainPhase(EMCNutBossChainPhase::Inactive,0.f);
    StartAttackCooldown(ResolveAt+BossSettings.TankChainRange/BossSettings.TankChainFlightSpeed+ChainReelSeconds+ChainRecoverySeconds);
    Enter(EMCNutBossState::Telegraph,BossSettings.TankChainWindup);
    UMCDefenseSprayPassives::RegisterThreat(Subject,this,uint64(AttackSeed));
    PresentChainEvent(TEXT("Tell"),LockedStart,ChainFlightEnd);
    return true;
}

void AMCNutBoss::SetChainPhase(EMCNutBossChainPhase Next,float Seconds)
{
    ChainPhase=Next;ChainPhaseStartedAt=Now();ChainPhaseEndsAt=ChainPhaseStartedAt+Seconds;
    ForceNetUpdate();
}

bool AMCNutBoss::FindChainContact(FVector From,FVector To,FHitResult& OutHit) const
{
    if(From.Equals(To,.01)) return false;
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(MCKnightChainFlight),false,this);
    const FCollisionShape Shape=FCollisionShape::MakeSphere(ChainHeadRadius);
    bool HitAnything=GetWorld()->SweepSingleByChannel(OutHit,From,To,FQuat::Identity,ECC_Visibility,Shape,Query);
    float FirstTime=HitAnything?OutHit.Time:2.f;
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_Pawn);Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
    TArray<FHitResult> Hits;
    GetWorld()->SweepMultiByObjectType(Hits,From,To,FQuat::Identity,Objects,Shape,Query);
    for(const FHitResult& Hit:Hits) {
        if(Hit.Time>FirstTime) continue;
        const auto* Pawn=Cast<AMCToothCharacter>(Hit.GetActor());
        const auto* Nut=Cast<AMCFoodActor>(Hit.GetActor());
        if(!IsLiveTarget(Pawn) && !IsChainNut(Nut)) continue;
        if(Hit.Time==FirstTime && OutHit.GetActor()!=Hit.GetActor()
            && !(IsChainNut(Nut) && IsLiveTarget(Cast<AMCToothCharacter>(OutHit.GetActor())))) continue;
        FirstTime=Hit.Time;OutHit=Hit;HitAnything=true;
    }
    return HitAnything;
}

void AMCNutBoss::ReleaseChainCargo()
{
    if(ChainMotionID.IsValid()) if(auto* Pawn=Cast<AMCToothCharacter>(ChainCargo))
        if(auto* Move=Cast<UMCToothMovementComponent>(Pawn->GetCharacterMovement())) Move->EndAcceptedHookPull(ChainMotionID);
    ChainMotionID.Invalidate();ChainCargo=nullptr;
}

void AMCNutBoss::CancelChain()
{
    if(Attack!=EMCNutBossAttack::Chain && ChainPhase==EMCNutBossChainPhase::Inactive) return;
    ReleaseChainCargo();ChainBaitTarget.Reset();ChainStrikeTarget.Reset();ChainRetreatTarget.Reset();ChainRetreatStartedAt=-1.;
    StopChainPresentation();
    SetChainPhase(EMCNutBossChainPhase::Inactive,0.f);
    if(Attack==EMCNutBossAttack::Chain) Attack=EMCNutBossAttack::None;
}

void AMCNutBoss::BeginChainReel()
{
    ReleaseChainCargo();ChainReelFrom=ChainTip;
    SetChainPhase(EMCNutBossChainPhase::Reeling,ChainReelSeconds);
    PresentChainEvent(TEXT("Reel"),GetChainOrigin(),ChainTip);
}

void AMCNutBoss::ExecuteChain(float Dt)
{
    const double Time=Now();
    if(State==EMCNutBossState::Telegraph) {
        const auto* AimTarget=ChainBaitTarget.Get();
        const bool bTrackAim=Time<ResolveAt-BossSettings.TankChainAimLockLeadSeconds && IsLiveTarget(AimTarget);
        if(bTrackAim) {
            LockedTarget=AimTarget->GetActorLocation();
            AttackForward=(LockedTarget-GetActorLocation()).GetSafeNormal2D(SMALL_NUMBER,AttackForward);
        }
        TurnTowards(AttackForward,Dt);
        if(bTrackAim) {
            LockedStart=GetChainOrigin();
            ChainFlightEnd=LockedStart+(LockedTarget-LockedStart).GetSafeNormal(SMALL_NUMBER,AttackForward)*BossSettings.TankChainRange;
        }
        const double SpinAt=FMath::Lerp(StateStartedAt,ResolveAt,double(CowboyUnpark));
        if(Time>=SpinAt && ChainPhaseStartedAt<SpinAt && HasCowboyChainAnchor()) {
            // Mark the accepted start of the tether windup on the phase clock.
            // The spin still starts if a sparse tick crossed its boundary.
            SetChainPhase(EMCNutBossChainPhase::Inactive,float(FMath::Max(0.,ResolveAt-Time)));
            PresentChainEvent(TEXT("Spin"),LockedStart,ChainFlightEnd);
        }
        if(Time<ResolveAt) return;
        // Preserve the locked direction and use a fixed launch origin for every sweep.
        const FVector FlightOffset=ChainFlightEnd-LockedStart;
        LockedStart=GetChainOrigin();
        ChainFlightEnd=LockedStart+FlightOffset;
        ChainTip=LockedStart;
        const float Seconds=float(FlightOffset.Size())/BossSettings.TankChainFlightSpeed;
        SetChainPhase(EMCNutBossChainPhase::Flight,Seconds);
        Enter(EMCNutBossState::Executing,Seconds);
        PresentChainEvent(TEXT("Release"),LockedStart,ChainFlightEnd);
        PresentChainEvent(TEXT("Flight"),LockedStart,ChainTip);
        return;
    }

    const FVector Origin=GetChainOrigin();
    if(ChainPhase==EMCNutBossChainPhase::Flight) {
        const FVector Step=FMath::Lerp(LockedStart,ChainFlightEnd,GetChainPhaseAlpha());
        FHitResult Hit;
        if(!FindChainContact(ChainTip,Step,Hit)) {
            ChainTip=Step;
            if(Time>=ChainPhaseEndsAt) BeginChainReel();
            return;
        }
        ChainTip=Hit.bStartPenetrating?Hit.Location:Hit.ImpactPoint;
        if(auto* Nut=Cast<AMCFoodActor>(Hit.GetActor());IsChainNut(Nut)) {
            ChainCargo=Nut;PreviousChainNutCenter=Nut->Body->GetCenterOfMass();
            ChainCargoLocalContact=Nut->GetActorTransform().InverseTransformPosition(ChainTip);
            // A far bait still needs time to accelerate and reach the boss.
            const float PullSeconds=FMath::Max(2.f,float(FVector::Dist2D(PreviousChainNutCenter,GetActorLocation()))/ChainNutSpeed+.45f);
            SetChainPhase(EMCNutBossChainPhase::PullNut,PullSeconds);
            PresentChainEvent(TEXT("CatchNut"),Origin,ChainTip);
            return;
        }
        if(auto* Pawn=Cast<AMCToothCharacter>(Hit.GetActor());IsLiveTarget(Pawn) && MCActiveSkillMotion::IsEligible(Pawn,false)) {
            auto* Move=Cast<UMCToothMovementComponent>(Pawn->GetCharacterMovement());
            const FVector Goal(GetActorLocation().X,GetActorLocation().Y,Pawn->GetActorLocation().Z);
            const FVector Velocity=(Goal-Pawn->GetActorLocation()).GetSafeNormal2D()*ChainPlayerSpeed;
            const float Stop=FMath::Min(300.f,Settings.BodyRadius+Pawn->GetCapsuleComponent()->GetScaledCapsuleRadius()+20.f);
            const float Travel=FMath::Max(0.f,float(FVector::Dist2D(Goal,Pawn->GetActorLocation()))-Stop);
            const float PullSeconds=FMath::Max(1.4f,Travel/ChainPlayerSpeed+.30f);
            const FGuid Operation=FGuid::NewGuid();
            if(Move && MCActiveSkillMotion::ClearPullStep(Pawn,this,this,Velocity,.04f)
                && Move->BeginAcceptedHookPull(Operation,Goal,ChainPlayerSpeed,Stop,PullSeconds+.05f,false)) {
                ChainMotionID=Operation;ChainCargo=Pawn;
                ChainCargoLocalContact=Pawn->GetActorTransform().InverseTransformPosition(ChainTip);
                SetChainPhase(EMCNutBossChainPhase::PullPlayer,PullSeconds);
                UMCDefenseSprayPassives::NotifyIncomingHit(Pawn,this);
                PresentChainEvent(TEXT("CatchPlayer"),Origin,ChainTip);
                return;
            }
        }
        BeginChainReel();return;
    }

    if(ChainPhase==EMCNutBossChainPhase::PullPlayer) {
        auto* Pawn=Cast<AMCToothCharacter>(ChainCargo);
        if(!IsLiveTarget(Pawn) || !MCActiveSkillMotion::IsEligible(Pawn,false) || Time>=ChainPhaseEndsAt) {BeginChainReel();return;}
        const auto* Move=Cast<UMCToothMovementComponent>(Pawn->GetCharacterMovement());
        const FVector Delta=GetActorLocation()-Pawn->GetActorLocation();
        const float Stop=FMath::Min(300.f,Settings.BodyRadius+Pawn->GetCapsuleComponent()->GetScaledCapsuleRadius()+20.f);
        ChainTip=Pawn->GetActorTransform().TransformPosition(ChainCargoLocalContact);
        if(!Move || !Move->IsAcceptedHookPullOwned(ChainMotionID)) {BeginChainReel();return;}
        if(Delta.SizeSquared2D()<=FMath::Square(Stop+2.f)) {
            ChainStrikeTarget=Pawn;
            BeginChainReel();
        } else if(!MCActiveSkillMotion::ClearPullStep(Pawn,this,this,Delta.GetSafeNormal2D()*ChainPlayerSpeed,Dt)) BeginChainReel();
        return;
    }

    if(ChainPhase==EMCNutBossChainPhase::PullNut) {
        auto* Nut=Cast<AMCFoodActor>(ChainCargo);
        if(!IsChainNut(Nut) || Time>=ChainPhaseEndsAt) {BeginChainReel();return;}
        const FVector Center=Nut->Body->GetCenterOfMass();
        ChainTip=Nut->GetActorTransform().TransformPosition(ChainCargoLocalContact);
        const FVector BossCenter=GetActorLocation();
        FHitResult Obstacle;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MCKnightChainCargo),false,this);
        Query.AddIgnoredActor(Nut);
        if(GetWorld()->LineTraceSingleByChannel(Obstacle,Center,BossCenter,ECC_Visibility,Query)) {BeginChainReel();return;}
        const FVector TravelOffset=FMath::ClosestPointOnSegment(BossCenter,PreviousChainNutCenter,Center)-Center;
        FVector Contact;
        const float Distance=Nut->Body->GetClosestPointOnCollision(BossCenter-TravelOffset,Contact);
        PreviousChainNutCenter=Center;
        if(Distance>=0.f && Distance<=Settings.BodyRadius+6.f) {
            Contact+=TravelOffset;
            const FVector Normal=(Contact-BossCenter).GetSafeNormal(SMALL_NUMBER,AttackForward);
            const auto* Bait=ChainBaitTarget.Get();
            auto* Credit=IsValid(Bait)?Bait->GetPlayerState<AMCPlayerState>():nullptr;
            ReleaseChainCargo();
            ReceiveExternalImpact(BossSettings.TankChainNutDamage,Nut,Credit,Center);
            if(IsValid(Nut)) MCNutBossImpact::Fracture(*Nut,ChainPresentation.Get(),TEXT("Fracture"),Contact,Normal,ChainNutSpeed*.48f);
            if(!IsEncounterAlive() || State==EMCNutBossState::Transforming) return;
            // A shield break may have ended the chain during damage handling.
            // Retain its longer stun and publish the real nut impact once.
            const float Existing=State==EMCNutBossState::Stunned?float(FMath::Max(0.,AttackEndAt-Time)):0.f;
            const float Stun=FMath::Max(Existing,BossSettings.TankChainStunSeconds);
            Attack=EMCNutBossAttack::Chain;ChainTip=Contact;
            ResetTankPressure();PendingHeadTurns.Reset();
            StartAttackCooldown(Time+Stun);
            SetChainPhase(EMCNutBossChainPhase::Impact,Stun);
            Enter(EMCNutBossState::Stunned,Stun);
            PresentChainEvent(TEXT("Impact"),Contact,BossCenter);
            UMCDefenseSprayPassives::NotifyAttackFinished(this,uint64(AttackSeed));
            return;
        }
        const FVector Desired=(BossCenter-Center).GetSafeNormal2D()*ChainNutSpeed;
        const FVector Velocity=Nut->Body->GetPhysicsLinearVelocity();
        const FVector Change=(Desired-FVector(Velocity.X,Velocity.Y,0)).GetClampedToMaxSize(7200.f*FMath::Min(Dt,.05f));
        Nut->ApplyNutImpulse(Change,Center);
        return;
    }

    if(ChainPhase==EMCNutBossChainPhase::Reeling) {
        ChainTip=FMath::Lerp(ChainReelFrom,Origin,GetChainPhaseAlpha());
        if(Time<ChainPhaseEndsAt) return;
        SetChainPhase(EMCNutBossChainPhase::Inactive,0.f);
        PresentChainEvent(TEXT("End"),Origin,Origin);
        auto* StrikeTarget=ChainStrikeTarget.Get();
        ChainStrikeTarget.Reset();ChainBaitTarget.Reset();
        if(IsLiveTarget(StrikeTarget)) {
            UMCDefenseSprayPassives::NotifyAttackFinished(this,uint64(AttackSeed));
            Target=StrikeTarget;
            BeginAttack(EMCNutBossAttack::Melee,true);
            return;
        }
        Recover(ChainRecoverySeconds);
    }
}
