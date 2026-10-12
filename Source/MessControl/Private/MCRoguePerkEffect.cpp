#include "MCRoguePerkEffect.h"
#include "MCPerkComponent.h"
void UMCRoguePerkEffect::OnApplied_Implementation(FName,const FMCPerkDefinition&,int32)
{ if(OwnerComponent) OwnerComponent->RefreshRogueTuning(); }
void UMCRoguePerkEffect::OnStacksChanged_Implementation(FName,const FMCPerkDefinition&,int32)
{ if(OwnerComponent) OwnerComponent->RefreshRogueTuning(); }
void UMCRoguePerkEffect::OnRemoved_Implementation(FName)
{ if(OwnerComponent) OwnerComponent->RefreshRogueTuning(); }
