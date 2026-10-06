#include "MCFoodActor.h"

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
    if(!bSpoiled && HazardNow()>=SpoilAt) Spoil();
}
