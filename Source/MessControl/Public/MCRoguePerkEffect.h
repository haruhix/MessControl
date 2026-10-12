#pragma once
#include "CoreMinimal.h"
#include "MCPerkEffect.h"
#include "MCRoguePerkEffect.generated.h"
UCLASS()
class MESSCONTROL_API UMCRoguePerkEffect : public UMCPerkEffect
{
    GENERATED_BODY()
public:
    virtual void OnApplied_Implementation(FName ID,const FMCPerkDefinition& Definition,int32 Stacks) override;
    virtual void OnStacksChanged_Implementation(FName ID,const FMCPerkDefinition& Definition,int32 Stacks) override;
    virtual void OnRemoved_Implementation(FName ID) override;
};
