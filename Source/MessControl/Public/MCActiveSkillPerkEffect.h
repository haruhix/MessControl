#pragma once

#include "CoreMinimal.h"
#include "MCPerkEffect.h"
#include "MCActiveSkillAction.h"
#include "MCActiveSkillPerkEffect.generated.h"

/** Stateless factory. Removing the row/effect on depletion cannot destroy a committed action. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCActiveSkillPerkEffect : public UMCPerkEffect
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Skill") TSubclassOf<AMCActiveSkillAction> ActionClass;
    AMCActiveSkillAction* PrepareAction(FName PerkID,const FMCPerkDefinition& Definition,
        const FMCActiveSkillInputHandle& Handle) const;
    virtual bool CanActivate_Implementation(FName PerkID,const FMCPerkDefinition& Definition) const override;
};
