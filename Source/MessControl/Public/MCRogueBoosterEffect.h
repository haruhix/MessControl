#pragma once
#include "CoreMinimal.h"
#include "MCPerkEffect.h"
#include "MCRogueBoosterEffect.generated.h"
UCLASS()
class MESSCONTROL_API UMCRogueBoosterEffect : public UMCPerkEffect
{
    GENERATED_BODY()
public:
    virtual void OnApplied_Implementation(FName ID,const FMCPerkDefinition&,int32 Stacks) override;
    virtual void OnStacksChanged_Implementation(FName ID,const FMCPerkDefinition&,int32 Stacks) override;
    virtual void OnRemoved_Implementation(FName ID) override;
private:
    void AddCharges(FName ID,int32 Stacks);
};
