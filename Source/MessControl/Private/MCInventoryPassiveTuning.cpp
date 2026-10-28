#include "MCInventoryComponent.h"
#include "MCPerkComponent.h"

float UMCInventoryComponent::SprayReach() const
{
    const float Authored=HasUpgrade(EMCToolUpgrade::Watergun)
        ?(Settings?Settings->WatergunCareReach:1000.f):(Settings?Settings->SprayReach:235.f);
    const float Base=FMath::IsFinite(Authored)?FMath::Clamp(Authored,10.f,3000.f):235.f;
    const auto* Perks=UMCPerkComponent::ForActor(GetOwner());
    const float Modified=Perks?Perks->RogueValue(EMCRogueStat::SprayRange,Base):Base;
    return FMath::IsFinite(Modified)?FMath::Clamp(Modified,10.f,3000.f):Base;
}

float UMCInventoryComponent::SprayWidthMultiplier() const
{
    const auto* Perks=UMCPerkComponent::ForActor(GetOwner());
    const float Scale=Perks?Perks->RogueValue(EMCRogueStat::SprayWidth):1.f;
    return FMath::IsFinite(Scale)?FMath::Clamp(Scale,.1f,3.f):1.f;
}

float UMCInventoryComponent::SprayRadiusAtDistance(float Distance) const
{
    if(!FMath::IsFinite(Distance) || Distance<=0.f) return 0.f;
    // The existing mist and social reaction both use an 18-degree cone.
    // Width scales its radius at a fixed distance, independently of reach.
    return FMath::Clamp(Distance,0.f,SprayReach())*FMath::Tan(FMath::DegreesToRadians(18.f))*SprayWidthMultiplier();
}
