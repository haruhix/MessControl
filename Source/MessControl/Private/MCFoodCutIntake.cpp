#include "MCFoodActor.h"
#include "MCFoodCutIntakePresentation.h"
#include "MCFoodProcessingRecoverySubsystem.h"
#include "MCThroat.h"
#include "MCToothCharacter.h"
#include "MCPlayerState.h"
#include "MCTutorialDirector.h"
#include "MCNutRainEvent.h"
#include "EngineUtils.h"

bool AMCFoodActor::UsesAutomaticCutIntake() const
{
    const auto* Encounter=Cast<AMCNutRainEvent>(GetOwner());
    return Settings.bAutoConsumeSmallCuts && FoodData.Kind==EMCFoodKind::Food
        && !bBrushTool && !IsNutEncounterFragment()
        && !(IsValid(Encounter) && Encounter->Settings.bBossEncounter);
}

bool AMCFoodActor::ResolveAutomaticCutDestination(FVector Start,FVector& OutTarget,AMCThroat*& OutThroat) const
{
    OutTarget=FVector::ZeroVector;OutThroat=nullptr;
    if(!GetWorld() || Start.ContainsNaN())return false;
    double Closest=DBL_MAX;bool Found=false;
    if(bSpoiled) {
        for(TActorIterator<AMCFoodDisposal> It(GetWorld());It;++It) {
            if(!It->bBrushBin || It->IsActorBeingDestroyed())continue;
            FTransform Zone;FVector Extent;bool Circular=false;
            It->GetDeliveryZoneGeometry(Zone,Extent,Circular);
            const FVector Outward=It->GetDeliveryDirection().GetSafeNormal2D();
            if(Zone.ContainsNaN() || Extent.ContainsNaN() || Extent.GetMin()<0 || Outward.IsNearlyZero())continue;
            const double Distance=FVector::DistSquared(Start,Zone.GetLocation());
            if(Distance>=Closest)continue;
            // Reuse the retained exit's actual front lip, including its curve.
            // A disabled legacy floor zone is still a valid disposal endpoint.
            TArray<FVector> Outer,Inner;
            FVector Boundary=Zone.GetLocation();bool HasBoundary=false;
            if(It->GetDeliveryZoneOutline(Outer,Inner) && Outer.Num()>1) {
                const FVector Side=FVector::CrossProduct(FVector::UpVector,Outward).GetSafeNormal();
                double BestSide=DBL_MAX,BestFront=-DBL_MAX;
                for(int32 I=1;I<Outer.Num();++I) {
                    const FVector A=Outer[I-1],Edge=Outer[I]-A;
                    if(A.ContainsNaN() || Outer[I].ContainsNaN())continue;
                    const double Across=FVector::DotProduct(Edge,Side);
                    const double T=FMath::Abs(Across)>SMALL_NUMBER
                        ?FMath::Clamp(FVector::DotProduct(Start-A,Side)/Across,0.,1.):.5;
                    const FVector Point=A+Edge*T;
                    const double SideDistance=FMath::Abs(FVector::DotProduct(Point-Start,Side));
                    const double Front=FVector::DotProduct(Point,Outward);
                    if(SideDistance<BestSide-KINDA_SMALL_NUMBER
                        || (FMath::IsNearlyEqual(SideDistance,BestSide,KINDA_SMALL_NUMBER) && Front>BestFront)) {
                        BestSide=SideDistance;BestFront=Front;Boundary=Point;HasBoundary=true;
                    }
                }
            }
            if(!HasBoundary) {
                // Isolated fixtures/no lip geometry retain the marker's own
                // transformed footprint; never assume a world-space -X exit.
                const FVector Size=Extent*Zone.GetScale3D().GetAbs();
                const double Support=FMath::Abs(FVector::DotProduct(Outward,Zone.GetUnitAxis(EAxis::X)))*Size.X
                    +FMath::Abs(FVector::DotProduct(Outward,Zone.GetUnitAxis(EAxis::Y)))*Size.Y
                    +FMath::Abs(FVector::DotProduct(Outward,Zone.GetUnitAxis(EAxis::Z)))*Size.Z;
                Boundary+=Outward*Support;
            }
            const FVector Target=Boundary+Outward*180.f+FVector(0,0,80.f);
            if(Target.ContainsNaN())continue;
            Closest=Distance;OutTarget=Target;Found=true;
        }
        return Found;
    }
    for(TActorIterator<AMCThroat> It(GetWorld());It;++It) {
        if(It->IsActorBeingDestroyed())continue;
        const FVector Inlet=It->VacuumInlet();
        if(Inlet.ContainsNaN())continue;
        const double Distance=FVector::DistSquared(Start,Inlet);
        if(Distance<Closest) {Closest=Distance;OutThroat=*It;OutTarget=Inlet;Found=true;}
    }
    return Found;
}

bool AMCFoodActor::TryConsumeCutPortion(FVector Direction)
{
    if(IsCutTransactionPending() || IsCutTransactionStaged())return false;
    if(!HasAuthority() || !UsesAutomaticCutIntake() || !FitsAutomaticIntakePortion()
        || IsDisposed() || bAutomaticallyConsumed || bAutomaticallyDiscarded || bDeliveryScored || IsInActiveContainer()
        || Phase==EMCFoodPhase::Swallowing || Phase==EMCFoodPhase::Absorbing
        || StackCarrier || EquippedBy || !Holders.IsEmpty() || Direction.ContainsNaN()) return false;

    // Use the remaining cut geometry, whose origin may lie outside its visible half.
    const FBox Bounds=bCutVisualReady && CutStackLocalBounds.IsValid
        ?CutStackLocalBounds.TransformBy(GetActorTransform()):GetCanonicalModelBounds();
    if(!Bounds.IsValid || Bounds.GetCenter().ContainsNaN()) {
        UMCFoodProcessingRecoverySubsystem::ReportFailure(this,EMCFoodProcessingFailure::InvalidGeometry);
        return false;
    }
    const FVector Start=Bounds.GetCenter();
    AMCThroat* Destination=nullptr;
    FVector Target=FVector::ZeroVector;
    // Missing food/disposal endpoints cannot silently delete a ready piece.
    if(!ResolveAutomaticCutDestination(Start,Target,Destination)) {
        UMCFoodProcessingRecoverySubsystem::ReportFailure(this,EMCFoodProcessingFailure::MissingDestination);
        return false;
    }

    const bool Discard=bSpoiled;
    bAutomaticallyDiscarded=Discard;bAutomaticallyConsumed=!Discard;
    if(!bFragment) {
        Health=0;
        CutAwardUnits=FoodData.FragmentCount();
        if(auto* Tutorial=AMCTutorialDirector::Find(GetWorld());Tutorial && LastHandledBy)
            Tutorial->NotifyAction(Cast<AMCToothCharacter>(LastHandledBy->GetPawn()),EMCTutorialAction::FoodCut,this);
    }
    // The stable presentation bridge captures this event before the source actor
    // disappears; newly spawned halves need no actor channel or particle bodies.
    const FTransform Source(GetActorQuat(),Start);
    MCFoodCutIntakePresentation::Play(this,Source,Target,
        Discard?(Target-Start).GetSafeNormal():Direction.GetSafeNormal(),float(Bounds.GetExtent().GetMax()));
    if(!Discard) {
        AwardDelivery();
        ++Destination->FoodSwallowed;
        ++Destination->MealSequence;
        Destination->ForceNetUpdate();
        UE_LOG(LogTemp,Log,TEXT("MC_FOOD_AUTO_INTAKE item=%s batch=%d units=%d start=%s target=%s part=%s mass=%.6f size=%s volume_cm3=%.6f"),
            *ItemName.ToString(),Batch,CutAwardUnits,*Start.ToCompactString(),*Target.ToCompactString(),*GetName(),FoodData.Mass,*GetHandPortionSize().ToCompactString(),AutomaticIntakeVolume());
    } else {
        UE_LOG(LogTemp,Log,TEXT("MC_FOOD_AUTO_DISCARD item=%s batch=%d units=%d start=%s target=%s part=%s mass=%.6f volume_cm3=%.6f"),
            *ItemName.ToString(),Batch,CutAwardUnits,*Start.ToCompactString(),*Target.ToCompactString(),*GetName(),FoodData.Mass,AutomaticIntakeVolume());
    }
    Dispose();
    if(Discard && AMCTutorialDirector::IsTutorialTarget(this))
        if(auto* Tutorial=AMCTutorialDirector::Find(GetWorld()))
            Tutorial->NotifyAction(LastHandledBy?Cast<AMCToothCharacter>(LastHandledBy->GetPawn()):nullptr,
                EMCTutorialAction::SpoiledDiscarded,this);
    return true;
}
