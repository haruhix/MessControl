#include "MCFoodCutIntakePresentation.h"
#include "MCFoodActor.h"
#include "MCGameState.h"
#include "MCVFXPresentationComponent.h"
#include "Engine/World.h"

void MCFoodCutIntakePresentation::Play(AMCFoodActor* Food,const FTransform& Source,
    FVector Target,FVector Direction,float Radius)
{
    if(!IsValid(Food) || !Food->HasAuthority() || !Food->GetWorld())return;
    auto* State=Food->GetWorld()->GetGameState<AMCGameState>();
    if(!State || !State->FoodCutIntakePresentation)return;

    FMCPerkVFXGameplayValues Values;
    Values.SourcePosition=Source.GetLocation();Values.TargetPosition=Target;Values.bHasEndpoints=true;
    Values.Direction=Direction.GetSafeNormal();Values.Radius=FMath::Clamp(Radius,4.f,110.f);
    Values.Duration=FMath::Clamp(float(FVector::Distance(Values.SourcePosition,Target))/1500.f,.65f,1.3f);
    Values.Intensity=1.f;Values.Color=Food->FoodData.CrumbColor;Values.bHasColor=true;
    // One captured seed identifies a cosmetic burst; it never changes gameplay decisions.
    Values.Seed=GetTypeHash(Food->GetFName())^uint32(State->RunSeed);Values.bHasSeed=true;
    const FTransform Frame(FQuat::Identity,Source.GetLocation(),FVector::OneVector);
    State->FoodCutIntakePresentation->PlaySourceEvent(TEXT("Food.AutoMouth"),
        Food->bSpoiled?TEXT("Discard"):TEXT("Consume"),Frame,Values);
}
