#include "MCNutRainEvent.h"

#include "MCFoodActor.h"
#include "MCFoodBodyComponent.h"
#include "MCNutBoss.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Engine/World.h"

namespace
{
constexpr int32 WizardSphereMaximumNuts=8;
constexpr float WizardSphereHoldSeconds=12.f;
constexpr float WizardSphereShotSpeed=2100.f;

bool CanMoveSphereNut(const AMCFoodActor* Food)
{
    return IsValid(Food) && !Food->IsActorBeingDestroyed() && Food->IsWizardGravityLiftActive()
        && Food->IsNutKineticsActive() && !Food->IsCutTransactionPending() && !Food->IsCutTransactionStaged();
}

bool SphereGatherPathClear(const AMCFoodActor& Food,FVector Start,FVector Goal,const AMCTongue* IgnoredFloor=nullptr)
{
    if(Start.Equals(Goal,.1)) return true;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCWizardNutSphereGather),false,&Food);
    if(IgnoredFloor) Query.AddIgnoredActor(IgnoredFloor);
    Query.bFindInitialOverlaps=false;
    FCollisionResponseParams Response(Food.Body->GetCollisionResponseToChannels());
    Response.CollisionResponse.SetResponse(ECC_Pawn,ECR_Ignore);
    Response.CollisionResponse.SetResponse(ECC_PhysicsBody,ECR_Ignore);
    Response.CollisionResponse.SetResponse(AMCFoodActor::FragmentCollisionChannel,ECR_Ignore);
    const FVector CenterOffset=Food.Body->Bounds.Origin-Food.GetActorLocation();
    const auto Shape=FCollisionShape::MakeBox(Food.Body->Bounds.BoxExtent);
    return !Food.GetWorld()->SweepTestByChannel(Start+CenterOffset,Goal+CenterOffset,FQuat::Identity,
        Food.Body->GetCollisionObjectType(),Shape,Query,Response);
}

bool FindSphereShotContact(const AMCFoodActor& Food,FVector Start,FVector Goal,const AActor* Source,FHitResult& Out)
{
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_WorldStatic);
    Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    Objects.AddObjectTypesToQuery(ECC_Pawn);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCWizardNutSphereShot),false,&Food);
    if(Source) Query.AddIgnoredActor(Source);
    const float Radius=FMath::Clamp(float(Food.Body->GetScaledBoxExtent().GetMax())*.8f,35.f,105.f);
    TArray<FHitResult> Contacts;
    Food.GetWorld()->SweepMultiByObjectType(Contacts,Start,Goal,FQuat::Identity,Objects,
        FCollisionShape::MakeSphere(Radius),Query);
    bool bFound=false;
    for(const FHitResult& Hit:Contacts) {
        const auto* Component=Hit.GetComponent();
        if(!Component) continue;
        if(const auto* Hero=Cast<AMCToothCharacter>(Hit.GetActor())) {
            if(!Hero->Status || !Hero->Status->IsAlive() || Hero->IsMimicCaptured()) continue;
        } else if(Component->GetCollisionObjectType()==ECC_Pawn
            || Component->GetCollisionResponseToChannel(Food.Body->GetCollisionObjectType())!=ECR_Block) continue;
        if(!bFound || Hit.Time<Out.Time) {Out=Hit;bFound=true;}
    }
    return bFound;
}
}

int32 AMCNutRainEvent::BeginWizardNutSphere(FVector Center,float GatherSeconds,float Height)
{
    if(!HasAuthority() || Stage!=EMCNutRainStage::Enemies || !Settings.bBossEncounter || !IsValid(Tongue)
        || Center.ContainsNaN() || !FMath::IsFinite(GatherSeconds) || GatherSeconds<=0.f
        || !FMath::IsFinite(Height) || Height<140.f || !WizardNutSphereNuts.IsEmpty() || HasWizardGravityDropsInFlight()) return 0;
    for(const auto& Nut:WizardGravityNuts)
        if(const auto* Food=Nut.Food.Get(); Food && Food->IsWizardGravityLiftActive()) return 0;

    const float ClusterRadius=FMath::Clamp(Settings.LargeNutHeight*.8f,120.f,170.f);
    FHitResult CenterFloor;
    if(!Tongue->GameplaySpawnFootprint(Center,ClusterRadius+55.f,CenterFloor)) {
        bool bFound=false;
        for(int32 Index=0;Index<8 && !bFound;++Index) {
            const float Angle=Index*PI/4;
            bFound=Tongue->GameplaySpawnFootprint(Center+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*260.f,
                ClusterRadius+55.f,CenterFloor);
        }
        if(!bFound) return 0;
    }
    GatherSeconds=FMath::Clamp(GatherSeconds,.15f,5.f);
    const FVector SphereLocation=CenterFloor.ImpactPoint+FVector::UpVector*FMath::Min(Height,650.f);
    TArray<AMCFoodActor*> Candidates;
    GetAvailableRainNuts(Candidates);
    Candidates.RemoveAll([this](const AMCFoodActor* Food) {
        FHitResult Floor;
        return !FindWizardGravityFloor(Food,Floor,true);
    });
    Candidates.Sort([SphereLocation](const AMCFoodActor& A,const AMCFoodActor& B) {
        const double DistanceA=FVector::DistSquared2D(A.GetActorLocation(),SphereLocation);
        const double DistanceB=FVector::DistSquared2D(B.GetActorLocation(),SphereLocation);
        return DistanceA==DistanceB?A.GetUniqueID()<B.GetUniqueID():DistanceA<DistanceB;
    });
    // Keep at least one grounded walnut available for the subsequent summon.
    const int32 Limit=FMath::Min(WizardSphereMaximumNuts,Candidates.Num()-1);
    for(AMCFoodActor* Food:Candidates) {
        const int32 Slot=WizardNutSphereNuts.Num();
        if(Slot>=Limit) break;
        const float Angle=2.f*PI*Slot/FMath::Max(1,Limit);
        const float Tier=Settings.LargeNutHeight*(Slot%2?.36f:-.36f);
        const FVector End=SphereLocation+FVector(ClusterRadius*FMath::Cos(Angle),ClusterRadius*FMath::Sin(Angle),Tier);
        const FVector Origin=Food->GetActorLocation();
        const float LiftHeight=Food->BeginWizardGravityLift(Tongue,GatherSeconds,WizardSphereHoldSeconds,
            FMath::Max(120.f,float(End.Z-Origin.Z)+20.f),true);
        if(LiftHeight<=0.f) continue;
        const FVector Lift=Origin+FVector::UpVector*LiftHeight;
        if(!SphereGatherPathClear(*Food,Origin,Lift,Tongue) || !SphereGatherPathClear(*Food,Lift,End)) {
            Food->CancelWizardGravityMotion();
            continue;
        }
        auto& Nut=WizardNutSphereNuts.Emplace_GetRef();
        Nut.Food=Food;Nut.Origin=Origin;Nut.LiftPoint=Lift;Nut.GatherEnd=End;
    }
    if(WizardNutSphereNuts.IsEmpty()) return 0;
    WizardNutSphereLocation=SphereLocation;
    WizardNutSphereGatherAt=GetWorld()->GetTimeSeconds();
    WizardNutSphereReadyAt=WizardNutSphereGatherAt+GatherSeconds;
    // Composer starts an isolated spell context without the full encounter tick.
    if(!IsActorTickEnabled()) {bWizardNutSphereTickOnly=true;SetActorTickEnabled(true);}
    return WizardNutSphereNuts.Num();
}

bool AMCNutRainEvent::FireWizardNutSphereShot(FVector Target,FVector& OutOrigin,FVector& OutTarget)
{
    if(!HasAuthority() || Stage!=EMCNutRainStage::Enemies || !IsValid(Tongue) || Target.ContainsNaN()) return false;
    const double Time=GetWorld()->GetTimeSeconds();
    if(Time<WizardNutSphereReadyAt) return false;
    const FVector Aim=(Target-WizardNutSphereLocation).GetSafeNormal();
    FWizardNutSphereNut* Selected=nullptr;
    double BestScore=-DBL_MAX;
    for(auto& Nut:WizardNutSphereNuts) {
        const auto* Food=Nut.Food.Get();
        if(Nut.ShotSeconds>0.f || !CanMoveSphereNut(Food)
            || !SphereGatherPathClear(*Food,Food->GetActorLocation(),Nut.GatherEnd)) continue;
        const double Score=FVector::DotProduct(Nut.GatherEnd-WizardNutSphereLocation,Aim);
        if(Score>BestScore) {Selected=&Nut;BestScore=Score;}
    }
    if(!Selected) return false;
    const FVector Start=Selected->GatherEnd;
    Selected->ShotTarget=Start+(Target-Start).GetClampedToMaxSize(WizardSphereShotSpeed*2.f);
    Selected->ShotStartedAt=Time;
    Selected->ShotSeconds=FMath::Max(.15f,float(FVector::Dist(Start,Selected->ShotTarget))/WizardSphereShotSpeed);
    if(auto* Food=Selected->Food.Get()) {
        Food->SetActorLocation(Start,false,nullptr,ETeleportType::TeleportPhysics);
        Food->ForceNetUpdate();
    }
    OutOrigin=Start;
    OutTarget=Selected->ShotTarget;
    return true;
}

bool AMCNutRainEvent::HasWizardNutSphereAmmo() const
{
    return WizardNutSphereNuts.ContainsByPredicate([](const FWizardNutSphereNut& Nut) {
        return Nut.ShotSeconds<=0.f && CanMoveSphereNut(Nut.Food.Get());
    });
}

bool AMCNutRainEvent::HasWizardNutSphereShotsInFlight() const
{
    return WizardNutSphereNuts.ContainsByPredicate([](const FWizardNutSphereNut& Nut) {
        return Nut.ShotSeconds>0.f && CanMoveSphereNut(Nut.Food.Get());
    });
}

void AMCNutRainEvent::TickWizardNutSphere(double Now)
{
    if(WizardNutSphereNuts.IsEmpty()) return;
    if(!IsValid(Tongue) || Now>=WizardNutSphereReadyAt+WizardSphereHoldSeconds) {CancelWizardNutSphere();return;}
    const float Gather=FMath::Clamp(float((Now-WizardNutSphereGatherAt)/(WizardNutSphereReadyAt-WizardNutSphereGatherAt)),0.f,1.f);
    AActor* Source=this;
    for(AMCNutBoss* Boss:Bosses)
        if(IsValid(Boss) && Boss->BossRole==EMCNutBossRole::Mage && Boss->IsEncounterAlive()) {Source=Boss;break;}
    for(int32 Index=WizardNutSphereNuts.Num()-1;Index>=0;--Index) {
        const auto& Nut=WizardNutSphereNuts[Index];
        auto* Food=Nut.Food.Get();
        if(!CanMoveSphereNut(Food)) {
            ReleaseWizardNutSphereNut(Nut,false);
            WizardNutSphereNuts.RemoveAtSwap(Index,1,EAllowShrinking::No);
            continue;
        }
        if(Nut.ShotSeconds<=0.f) {
            constexpr float RisePortion=.42f;
            const FVector Goal=Gather<RisePortion
                ?FMath::Lerp(Nut.Origin,Nut.LiftPoint,FMath::SmoothStep(0.f,RisePortion,Gather))
                :FMath::Lerp(Nut.LiftPoint,Nut.GatherEnd,FMath::SmoothStep(RisePortion,1.f,Gather));
            if(SphereGatherPathClear(*Food,Food->GetActorLocation(),Goal,Gather<RisePortion?Tongue.Get():nullptr)) {
                if(!Food->GetActorLocation().Equals(Goal,.1)) Food->SetActorLocation(Goal,false,nullptr,ETeleportType::TeleportPhysics);
            } else {
                ReleaseWizardNutSphereNut(Nut,true);
                WizardNutSphereNuts.RemoveAtSwap(Index,1,EAllowShrinking::No);
            }
            continue;
        }

        const float Alpha=FMath::Clamp(float((Now-Nut.ShotStartedAt)/Nut.ShotSeconds),0.f,1.f);
        if(Alpha<=0.f) continue;
        const FVector Goal=FMath::Lerp(Nut.GatherEnd,Nut.ShotTarget,Alpha);
        FHitResult Hit;
        const bool bHit=FindSphereShotContact(*Food,Food->GetActorLocation(),Goal,Source,Hit);
        const FVector Direction=(Nut.ShotTarget-Nut.GatherEnd).GetSafeNormal(SMALL_NUMBER,FVector::ForwardVector);
        Food->SetActorLocation(bHit?Hit.Location-Direction*5.f:Goal,false,nullptr,ETeleportType::TeleportPhysics);
        if(!bHit && Alpha<1.f) continue;

        FHitResult Floor;
        ReleaseWizardNutSphereNut(Nut,!FindWizardGravityFloor(Food,Floor,false));
        WizardNutSphereNuts.RemoveAtSwap(Index,1,EAllowShrinking::No);
        if(auto* Hero=bHit?Cast<AMCToothCharacter>(Hit.GetActor()):nullptr; Hero && Hero->Status) {
            const float Damage=Settings.ImpactDamageLimit*.65f;
            if(Hero->Status->Damage(Damage,Direction,Source,EMCRogueDamageKind::FallingNut) && Hero->ToothPhysics)
                Hero->ToothPhysics->ApplyHit(Direction*Settings.ImpactPushSpeed+FVector(0,0,55),Hit.ImpactPoint);
            if(Stage!=EMCNutRainStage::Enemies || WizardNutSphereNuts.IsEmpty()) break;
        }
    }
    if(WizardNutSphereNuts.IsEmpty()) CancelWizardNutSphere();
}

void AMCNutRainEvent::ReleaseWizardNutSphereNut(const FWizardNutSphereNut& Nut,bool bRestoreOrigin)
{
    auto* Food=Nut.Food.Get();
    if(!IsValid(Food) || Food->IsActorBeingDestroyed() || !Food->IsWizardGravityLiftActive()) return;
    if(bRestoreOrigin && CanMoveSphereNut(Food))
        Food->SetActorLocation(Nut.Origin+FVector(0,0,35),false,nullptr,ETeleportType::TeleportPhysics);
    Food->CancelWizardGravityMotion();
}

void AMCNutRainEvent::CancelWizardNutSphere()
{
    if(!HasAuthority()) return;
    for(const auto& Nut:WizardNutSphereNuts) ReleaseWizardNutSphereNut(Nut,true);
    WizardNutSphereNuts.Reset();
    WizardNutSphereLocation=FVector::ZeroVector;
    WizardNutSphereGatherAt=WizardNutSphereReadyAt=0.;
    if(bWizardNutSphereTickOnly) {bWizardNutSphereTickOnly=false;SetActorTickEnabled(false);}
}
