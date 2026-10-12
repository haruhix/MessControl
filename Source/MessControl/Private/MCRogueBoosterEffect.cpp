#include "MCRogueBoosterEffect.h"
#include "MCPerkComponent.h"
#include "MCPlayerState.h"
void UMCRogueBoosterEffect::AddCharges(FName ID,int32 Stacks)
{
    auto* State=OwnerComponent?Cast<AMCPlayerState>(OwnerComponent->GetOwner()):nullptr;
    if(!State || !State->HasAuthority()) return;
    int32& Used=State->ConsumedRecoveryStacks.FindOrAdd(ID);
    while(Used<Stacks) {OwnerComponent->AddBoosterCharge(ID);++Used;}
}
void UMCRogueBoosterEffect::OnApplied_Implementation(FName ID,const FMCPerkDefinition&,int32 Stacks) {AddCharges(ID,Stacks);}
void UMCRogueBoosterEffect::OnStacksChanged_Implementation(FName ID,const FMCPerkDefinition&,int32 Stacks) {AddCharges(ID,Stacks);}
void UMCRogueBoosterEffect::OnRemoved_Implementation(FName ID)
{
    if(auto* State=OwnerComponent?Cast<AMCPlayerState>(OwnerComponent->GetOwner()):nullptr) State->ConsumedRecoveryStacks.Remove(ID);
}
