#include "MCToothCharacter.h"

#include "MCKatanaBlade.h"
#include "MCKatanaEnemyContact.h"
#include "MCCombatPassiveSubsystem.h"
#include "MCFoodActor.h"
#include "MCInventoryComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace
{
constexpr float MaxSampleStep=1.f/240.f;
constexpr float MaxEdgeSpacing=2.25f;
constexpr float BodyContactRadius=2.f;

#if !UE_BUILD_SHIPPING
TAutoConsoleVariable<int32> KatanaTraceDebug(TEXT("mc.Katana.TraceDebug"),0,
    TEXT("Log accepted katana contacts (1) or also blade sweep samples (2)."));
#endif

struct FEdgeContact
{
    FHitResult Hit;
    FVector Direction;
    FVector SweepStart;
};
}

void AMCToothCharacter::CacheKatanaBodyPose(const FTransform& Pose) const
{
    check(IsInGameThread());
    KatanaBodyPose=Pose;
}

void AMCToothCharacter::TickKatanaAction()
{
    if(!HasAuthority() || (KatanaChargeStartedAt<0. && KatanaSwingID==0)) return;
    const auto Frame=GetPresentationFrame();
    if(!CanOwnKatanaAction() || (KatanaSwingID && KatanaBladeResetRevision!=Frame.ResetRevision)) {
        CancelKatanaAction();
        ForceNetUpdate();
        return;
    }
    const auto Variant=MCKatanaSwing::Decode(KatanaSwingID);
    if(Variant==MCKatanaSwing::EVariant::None) return;
    const auto Cuts=MCKatanaSwing::Cuts(Variant);
    const auto* Clip=AnimationProfile?MCKatanaBlade::ClipFor(*AnimationProfile,Variant):nullptr;
    const float End=MCKatanaSwing::ClipDuration(Variant);
    const float Time=FMath::Clamp(GetToolSwingElapsed()*KatanaSwingPlayRate+MCKatanaSwing::StartOffset(Variant),
        KatanaTracedTime,End);
    const FTransform MeshWorld=Frame.ToCanonicalWorld(GetMesh()->GetComponentTransform());
    const float StepDuration=Time-KatanaTracedTime;
    if(Clip && StepDuration>UE_SMALL_NUMBER) {
        const FName Body=RigBone(TEXT("body")),Hand=RigBone(TEXT("hand_r"));
        auto Sample=[&](float At,FTransform& Blade) {
            const float Alpha=(At-KatanaTracedTime)/StepDuration;
            FTransform BodyPose,World;
            BodyPose.Blend(KatanaLastBodyPose,KatanaBodyPose,Alpha);
            World.Blend(KatanaLastMeshWorld,MeshWorld,Alpha);
            return MCKatanaBlade::Sample(*Clip,At,Body,Hand,BodyPose,World,Blade);
        };
        for(int32 Stroke=0;Stroke<Cuts.Num();++Stroke) {
            const float Start=FMath::Max(KatanaTracedTime,Cuts[Stroke].Start);
            const float Finish=FMath::Min(Time,Cuts[Stroke].End);
            if(Finish<=Start) continue;
            if(KatanaTracedStroke!=Stroke) {
                KatanaTracedStroke=Stroke;
                KatanaHitTargets.Reset();
            }
            FTransform Previous;
            if(!Sample(Start,Previous)) continue;
            const int32 Steps=FMath::CeilToInt((Finish-Start)/MaxSampleStep);
            for(int32 Step=1;Step<=Steps;++Step) {
                FTransform Next;
                if(!Sample(FMath::Lerp(Start,Finish,float(Step)/Steps),Next)) break;
                TraceKatanaStroke(Stroke,Previous,Next);
                if(!KatanaSwingID || !CanOwnKatanaAction()) return;
                Previous=Next;
            }
        }
    }
    KatanaTracedTime=Time;
    KatanaLastMeshWorld=MeshWorld;
    KatanaLastBodyPose=KatanaBodyPose;
    if(Time>=End) {
        CancelKatanaAction();
        ForceNetUpdate();
    }
}

void AMCToothCharacter::TraceKatanaStroke(int32 Stroke,const FTransform& From,const FTransform& To)
{
    TArray<FVector,TInlineAllocator<128>> Previous,Current;
    FBox Bounds(ForceInit);
    auto AddStation=[&](const FVector& Local) {
        Previous.Add(From.TransformPosition(Local));
        Current.Add(To.TransformPosition(Local));
        Bounds+=Previous.Last();
        Bounds+=Current.Last();
    };
    AddStation(MCKatanaBlade::Edge[0]);
    const float Scale=FMath::Max(From.GetScale3D().GetAbsMax(),To.GetScale3D().GetAbsMax());
    for(int32 Index=1;Index<UE_ARRAY_COUNT(MCKatanaBlade::Edge);++Index) {
        const FVector& Start=MCKatanaBlade::Edge[Index-1];
        const FVector& End=MCKatanaBlade::Edge[Index];
        const int32 Stations=FMath::Max(1,FMath::CeilToInt(FVector::Dist(Start,End)*Scale/MaxEdgeSpacing));
        for(int32 Station=1;Station<=Stations;++Station)
            AddStation(FMath::Lerp(Start,End,float(Station)/Stations));
    }
#if !UE_BUILD_SHIPPING
    if(KatanaTraceDebug.GetValueOnGameThread()>1)
        UE_LOG(LogTemp,Display,TEXT("MC_KATANA_SWEEP attack=%d stroke=%d base=%s tip=%s"),
            KatanaSwingID,Stroke,*Current[0].ToString(),*Current.Last().ToString());
#endif
    Bounds=Bounds.ExpandBy(BodyContactRadius);
    FCollisionObjectQueryParams Objects(ECC_WorldDynamic);
    Objects.AddObjectTypesToQuery(ECC_Pawn);
    Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
    Objects.AddObjectTypesToQuery(AMCFoodActor::FragmentCollisionChannel);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCKatanaBroadPhase),false,this);
    TArray<FOverlapResult> Overlaps;
    GetWorld()->OverlapMultiByObjectType(Overlaps,Bounds.GetCenter(),FQuat::Identity,Objects,
        FCollisionShape::MakeBox(Bounds.GetExtent()),Query);
    TSet<TWeakObjectPtr<AActor>> Candidates;
    for(const auto& Overlap:Overlaps) {
        AActor* Actor=Overlap.GetActor();
        if(!IsValid(Actor) || KatanaHitTargets.Contains(Actor)) continue;
        if(const auto* Food=Cast<AMCFoodActor>(Actor)) {
            // New portions can remain inside the blade's arc in the same stroke.
            if(Inventory->CanBreak(Food) && !KatanaHitTargets.Contains(Food->GetCutParent()))
                Candidates.Add(Actor);
        } else if(MCKatanaEnemyContact::BodyFor(*Actor)) Candidates.Add(Actor);
    }
    if(Candidates.IsEmpty()) return;

    // The cutting edge is sampled along its curve; the handle and spine
    // never generate a cut. Food requires a triangle hit, while living
    // targets use the game's query-enabled damage body.
    TArray<FEdgeContact,TInlineAllocator<24>> Contacts;
    const FCollisionShape BodyShape=FCollisionShape::MakeSphere(BodyContactRadius);
    for(const auto& Candidate:Candidates) {
        AActor* Actor=Candidate.Get();
        if(!Actor) continue;
        auto* Food=Cast<AMCFoodActor>(Actor);
        UPrimitiveComponent* Body=Food?nullptr:MCKatanaEnemyContact::BodyFor(*Actor);
        if(!Food && !Body) continue;
        for(int32 Station=0;Station<Current.Num();++Station) {
            const FVector Direction=(Current[Station]-Previous[Station]).GetSafeNormal();
            if(Direction.IsNearlyZero()) continue;
            FHitResult Hit;
            if(Food) {
                if(!Food->TraceKatanaCut(Previous[Station],Current[Station],Hit)) continue;
            } else {
                if(!Body->SweepComponent(Hit,Previous[Station],Current[Station],FQuat::Identity,BodyShape,false)) continue;
                if(Hit.bStartPenetrating) Hit.ImpactPoint=Previous[Station];
                Hit.HitObjectHandle=FActorInstanceHandle(Actor);
            }
            Contacts.Add({Hit,Direction,Previous[Station]});
        }
    }
    Contacts.Sort([](const FEdgeContact& A,const FEdgeContact& B){return A.Hit.Time<B.Hit.Time;});
    const uint8 Attack=KatanaSwingID;
    for(const auto& Contact:Contacts) {
        if(KatanaSwingID!=Attack || !CanOwnKatanaAction()) break;
        AActor* Actor=Contact.Hit.GetActor();
        if(!IsValid(Actor) || KatanaHitTargets.Contains(Actor)) continue;
        FTransform Blade;
        Blade.Blend(From,To,FMath::Clamp(Contact.Hit.Time,0.f,1.f));
        const FVector CutNormal=Blade.TransformVectorNoScale(FVector::RightVector).GetSafeNormal();
        const FVector Base=Blade.TransformPosition(MCKatanaBlade::Edge[0]);
        const FVector Point=Contact.Hit.ImpactPoint;
        FCollisionQueryParams Blockers(SCENE_QUERY_STAT(MCKatanaObstruction),true,this);
        Blockers.AddIgnoredActor(Actor);
        FHitResult Block;
        const FCollisionObjectQueryParams StaticObjects(ECC_WorldStatic);
        if(GetWorld()->LineTraceSingleByObjectType(Block,Base,Point,StaticObjects,Blockers)
            || GetWorld()->LineTraceSingleByObjectType(Block,Contact.SweepStart,Point,StaticObjects,Blockers))
            continue;
        FMCCombatAttackScope PassiveAttack(this,true);
        bool Accepted=false;
        if(auto* Food=Cast<AMCFoodActor>(Actor)) {
            if(KatanaHitTargets.Contains(Food->GetCutParent())) continue;
            Accepted=ApplyKatanaFoodContact(Food,Contact.Hit,CutNormal,Contact.Direction,KatanaContactDamage);
        } else {
            KatanaHitTargets.Add(Actor);
            Accepted=MCKatanaEnemyContact::Apply(*this,*Actor,Point,KatanaContactDamage);
            if(Accepted) {
                ++ConfirmedHitCount;
                MulticastHitSound(Point,uint8(EMCToolSlot::Knife),FMath::Clamp(KatanaContactDamage/60.f,.25f,1.f));
            }
        }
        if(!Accepted) continue;
        KatanaHitTargets.Add(Actor);
#if !UE_BUILD_SHIPPING
        if(KatanaTraceDebug.GetValueOnGameThread())
            UE_LOG(LogTemp,Display,TEXT("MC_KATANA_CONTACT attack=%d stroke=%d target=%s point=%s normal=%s base=%s tip=%s direction=%s"),
                Attack,Stroke,*GetNameSafe(Actor),*Point.ToString(),*CutNormal.ToString(),*Base.ToString(),
                *Blade.TransformPosition(MCKatanaBlade::Edge[UE_ARRAY_COUNT(MCKatanaBlade::Edge)-1]).ToString(),
                *Contact.Direction.ToString());
#endif
    }
}
