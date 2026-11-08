#include "MCFoodActor.h"
#include "MCFoodCutIntakePresentation.h"
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
        && !bBrushTool && !IsWrongIngredient() && !IsNutEncounterFragment()
        && !(IsValid(Encounter) && Encounter->Settings.bBossEncounter);
}

bool AMCFoodActor::TryConsumeCutPortion(FVector Direction)
{
    if(!HasAuthority() || !UsesAutomaticCutIntake() || !FitsAutomaticIntakePortion()
        || IsDisposed() || bAutomaticallyConsumed || bDeliveryScored || IsInActiveContainer()
        || Phase==EMCFoodPhase::Swallowing || Phase==EMCFoodPhase::Absorbing
        || StackCarrier || EquippedBy || !Holders.IsEmpty() || Direction.ContainsNaN()) return false;

    // Use the remaining cut geometry, whose origin may lie outside its visible half.
    const FBox Bounds=bCutVisualReady && CutStackLocalBounds.IsValid
        ?CutStackLocalBounds.TransformBy(GetActorTransform()):GetCanonicalModelBounds();
    if(!Bounds.IsValid || Bounds.GetCenter().ContainsNaN()) return false;
    const FVector Start=Bounds.GetCenter();
    AMCThroat* Destination=nullptr;
    FVector Target=FVector::ZeroVector;
    double Closest=DBL_MAX;
    for(TActorIterator<AMCThroat> It(GetWorld());It;++It) {
        if(It->IsActorBeingDestroyed()) continue;
        const FVector Inlet=It->VacuumInlet();
        if(Inlet.ContainsNaN()) continue;
        const double Distance=FVector::DistSquared(Start,Inlet);
        if(Distance<Closest) {Closest=Distance;Destination=*It;Target=Inlet;}
    }
    // An absent intake is an invalid fixture/map, never an excuse to delete food.
    if(!Destination) return false;

    bAutomaticallyConsumed=true;
    if(!bFragment) {
        Health=0;
        CutAwardUnits=FoodData.FragmentCount();
        if(auto* Tutorial=AMCTutorialDirector::Find(GetWorld());Tutorial && LastHandledBy)
            Tutorial->NotifyAction(Cast<AMCToothCharacter>(LastHandledBy->GetPawn()),EMCTutorialAction::FoodCut,this);
    }
    // The stable presentation bridge captures this event before the source actor
    // disappears; newly spawned halves need no actor channel or particle bodies.
    const FTransform Source(GetActorQuat(),Start);
    MCFoodCutIntakePresentation::Play(this,Source,Target,Direction.GetSafeNormal(),float(Bounds.GetExtent().GetMax()));
    AwardDelivery();
    ++Destination->FoodSwallowed;
    ++Destination->MealSequence;
    Destination->ForceNetUpdate();
    UE_LOG(LogTemp,Log,TEXT("MC_FOOD_AUTO_INTAKE item=%s batch=%d units=%d start=%s target=%s part=%s mass=%.6f"),
        *ItemName.ToString(),Batch,CutAwardUnits,*Start.ToCompactString(),*Target.ToCompactString(),*GetName(),FoodData.Mass);
    Dispose();
    return true;
}
