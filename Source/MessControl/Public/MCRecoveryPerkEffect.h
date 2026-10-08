#pragma once

#include "CoreMinimal.h"
#include "MCPerkEffect.h"
#include "MCRecoveryPerkEffect.generated.h"

/** Each selected stack performs one recovery; consumed stacks never replay on respawn. */
UCLASS()
class MESSCONTROL_API UMCRecoveryPerkEffect : public UMCPerkEffect
{
    GENERATED_BODY()
public:
    virtual void OnApplied_Implementation(FName PerkID,const FMCPerkDefinition& Definition,int32 Stacks) override;
    virtual void OnStacksChanged_Implementation(FName PerkID,const FMCPerkDefinition& Definition,int32 Stacks) override;
    virtual void OnRemoved_Implementation(FName PerkID) override;
private:
    void ApplyPendingRecovery();
    FName RecoveryID;
    int32 TargetStacks=0;
    FTimerHandle RecoveryTimer;
};
