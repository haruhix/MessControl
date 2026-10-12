#include "MCFoodActor.h"
#include "MCPerkComponent.h"
#include "MCToothCharacter.h"

#include "MCTutorialDirector.h"

// Keep serialized absorption fields for existing packages, but ordinary food no
// longer dissolves into tissue or creates lesions. The freshness clock is absolute.
bool AMCFoodActor::FindAbsorptionFloor(FHitResult& Hit,AMCTongue*& Tongue) const {return false;}
float AMCFoodActor::AbsorptionProgress() const {return 0;}
void AMCFoodActor::FinishAbsorption() {}
void AMCFoodActor::AttendFood()
{
    if(!HasAuthority() || bBrushTool || IsDisposed()) return;
    if(SpoilAt<=0 && FoodData.Kind==EMCFoodKind::Food) SpoilAt=HazardNow()+FoodData.SpoilSeconds;
}
void AMCFoodActor::UpdateAbsorption(float Dt)
{
    if (AMCTutorialDirector::IsSafeTutorial(GetWorld())) return;
    if(!HasAuthority() || bBrushTool || IsDisposed() || FoodData.Kind!=EMCFoodKind::Food || ItemName.IsNone()) return;
    if(SpoilAt<=0) SpoilAt=HazardNow()+FoodData.SpoilSeconds;
    const AMCToothCharacter* Carrier=StackCarrier?StackCarrier.Get():EquippedBy?EquippedBy.Get():!Holders.IsEmpty()?Holders[0].Get():nullptr;
    if(!bSpoiled && Carrier) if(const auto* Perks=UMCPerkComponent::ForActor(Carrier))
        SpoilAt+=FMath::Max(0.f,Dt)*(1-Perks->RogueValue(EMCRogueStat::SpoilRate));
    if(!bSpoiled && HazardNow()>=SpoilAt) Spoil();
}
