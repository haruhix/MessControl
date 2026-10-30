#include "MCFoodActor.h"
#include "MCPerkComponent.h"
#include "MCToothCharacter.h"
#include "MCFoodCollectionComponent.h"
#include "MCGripComponent.h"
#include "MCToothStatusComponent.h"

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
    // A pickup after an already elapsed deadline cannot revive freshness by
    // adding a fraction of this frame to the old timestamp.
    if(!bSpoiled && HazardNow()>=SpoilAt) {Spoil();return;}
    const auto Alive=[](const AMCToothCharacter* Hero) {
        return IsValid(Hero) && Hero->Status && Hero->Status->IsAlive();
    };
    const AMCToothCharacter* Carrier=nullptr;
    if(Alive(StackCarrier) && StackCarrier->FoodCollection && StackCarrier->FoodCollection->Contains(this)) Carrier=StackCarrier;
    if(!Carrier) for(const auto& Holder:Holders) {
        if(Alive(Holder) && Holder->Grip && Holder->Grip->Holds(this)) {Carrier=Holder;break;}
    }
    if(!Carrier && IsInActiveContainer() && Alive(GetActiveContainerCarrier())) Carrier=GetActiveContainerCarrier();
    if(!bSpoiled && Carrier) if(const auto* Perks=UMCPerkComponent::ForActor(Carrier)) {
        const float Step=FMath::IsFinite(Dt)?FMath::Max(0.f,Dt):0.f;
        SpoilAt+=Step*(1-Perks->RogueValue(EMCRogueStat::SpoilRate));
    }
    if(!bSpoiled && HazardNow()>=SpoilAt) Spoil();
}
