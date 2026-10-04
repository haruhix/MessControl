#include "MCPerkEffect.h"
#include "MCPerkComponent.h"

UWorld* UMCPerkEffect::GetWorld() const
{
    return OwnerComponent ? OwnerComponent->GetWorld() : nullptr;
}

void UMCPerkEffect::OnApplied_Implementation(FName, const FMCPerkDefinition&, int32) {}
void UMCPerkEffect::OnStacksChanged_Implementation(FName, const FMCPerkDefinition&, int32) {}
void UMCPerkEffect::OnRemoved_Implementation(FName) {}
