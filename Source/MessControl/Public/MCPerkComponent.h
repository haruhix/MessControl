#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCPerkTypes.h"
#include "MCPerkComponent.generated.h"

class UMCPerkEffect;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMCOnPerksChanged);

/** Run perks live on PlayerState, so death and pawn replacement do not erase rewards. */
UCLASS(ClassGroup=(MessControl), meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCPerkComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCPerkComponent();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** Authority-only API, intentionally not a client RPC. Reward actors validate claims first. */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Perks")
    bool ServerGrantPerk(FName PerkID);

    /** Validate every requested stack before changing state; a batch cannot partially apply. */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Perks")
    bool ServerGrantPerks(const TArray<FName>& PerkIDs);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Perks")
    bool RemovePerk(FName PerkID);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Perks")
    void ResetPerks();

    UFUNCTION(BlueprintPure, Category="Perks")
    bool CanGrantPerk(FName PerkID) const;

    UFUNCTION(BlueprintPure, Category="Perks")
    int32 GetStacks(FName PerkID) const;

    UFUNCTION(BlueprintPure, Category="Perks")
    bool GetPerkDefinition(FName PerkID, FMCPerkDefinition& Definition) const;

    const FMCPerkDefinition* FindDefinition(FName PerkID) const;

    UFUNCTION(BlueprintPure, Category="Perks")
    UDataTable* GetPerkTable() const { return LoadedTable; }

    void CopyPerksTo(UMCPerkComponent* Target) const;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Perks")
    TSoftObjectPtr<UDataTable> PerkTable;

    UPROPERTY(ReplicatedUsing=OnRep_ActivePerks, BlueprintReadOnly, Category="Perks")
    TArray<FMCActivePerk> ActivePerks;

    UPROPERTY(BlueprintAssignable, Category="Perks")
    FMCOnPerksChanged OnPerksChanged;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void OnRep_ActivePerks();

    void LoadDefinitions();
    void RebuildEffects();
    bool HasAuthority() const;
    static bool IsDefinitionUsable(const FMCPerkDefinition& Definition);

    UPROPERTY(Transient)
    TObjectPtr<UDataTable> LoadedTable;

    UPROPERTY(Transient)
    TMap<FName, TObjectPtr<UMCPerkEffect>> Effects;

    TMap<FName, int32> AppliedStacks;
    bool bDefinitionsLoaded = false;
    bool bRebuilding = false;
};
