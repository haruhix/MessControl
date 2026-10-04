#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MCPerkTypes.h"
#include "MCPerkEffect.generated.h"

class UMCPerkComponent;

/** Server-side strategy per active row. The base class is an empty placeholder effect. */
UCLASS(Blueprintable, BlueprintType)
class MESSCONTROL_API UMCPerkEffect : public UObject
{
    GENERATED_BODY()
public:
    virtual UWorld* GetWorld() const override;

    /** Lifecycle events run on the authority only. Bind custom gameplay delegates here. */
    UFUNCTION(BlueprintNativeEvent, Category="Perk")
    void OnApplied(FName PerkID, const FMCPerkDefinition& Definition, int32 Stacks);
    virtual void OnApplied_Implementation(FName PerkID, const FMCPerkDefinition& Definition, int32 Stacks);

    UFUNCTION(BlueprintNativeEvent, Category="Perk")
    void OnStacksChanged(FName PerkID, const FMCPerkDefinition& Definition, int32 Stacks);
    virtual void OnStacksChanged_Implementation(FName PerkID, const FMCPerkDefinition& Definition, int32 Stacks);

    /** Undo delegate bindings or temporary actors here, including when the match ends. */
    UFUNCTION(BlueprintNativeEvent, Category="Perk")
    void OnRemoved(FName PerkID);
    virtual void OnRemoved_Implementation(FName PerkID);

    UPROPERTY(Transient, BlueprintReadOnly, Category="Perk")
    TObjectPtr<UMCPerkComponent> OwnerComponent;
};
