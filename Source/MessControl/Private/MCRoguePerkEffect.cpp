#include "MCRoguePerkEffect.h"
#include "MCPerkComponent.h"
#include "MCPerkPresentationEvents.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
void UMCRoguePerkEffect::OnApplied_Implementation(FName PerkID,const FMCPerkDefinition&,int32)
{
    if(!OwnerComponent)return;
    OwnerComponent->RefreshRogueTuning();
    if(const auto* State=Cast<AMCPlayerState>(OwnerComponent->GetOwner()))
        if(auto* Hero=Cast<AMCToothCharacter>(State->GetPawn()))
            MCPassivePresentation::Play(Hero,PerkID,TEXT("Equipped"),FTransform(Hero->GetActorRotation(),Hero->GetActorLocation()));
}
void UMCRoguePerkEffect::OnStacksChanged_Implementation(FName,const FMCPerkDefinition&,int32)
{ if(OwnerComponent) OwnerComponent->RefreshRogueTuning(); }
void UMCRoguePerkEffect::OnRemoved_Implementation(FName PerkID)
{
    if(!OwnerComponent)return;
    if(const auto* State=Cast<AMCPlayerState>(OwnerComponent->GetOwner()))
        MCPassivePresentation::ForgetSourceState(Cast<AMCToothCharacter>(State->GetPawn()),PerkID);
    OwnerComponent->RefreshRogueTuning();
}
