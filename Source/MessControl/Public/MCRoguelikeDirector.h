#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCRewardChest.h"
#include "MCRoguelikeDirector.generated.h"

class UDataTable;
class AMCRewardDropZone;

/** Completed task edges enqueue rewards; blocked placement/cooldown never consumes the queue. */
UCLASS()
class MESSCONTROL_API AMCRoguelikeDirector : public AActor
{
    GENERATED_BODY()
public:
    AMCRoguelikeDirector();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    /** A stable ID deduplicates retries. NAME_None is for callers that already emit once per completed edge. */
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Rewards") void NotifyTaskCompleted(FName CompletionId=NAME_None);
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Rewards") void ResetRewards();
    void NotifyChestFinished(AMCRewardChest* Chest,bool bRequeue);
    /** Release one queued reward only while the central director grants its launch. */
    bool TryReleaseQueuedReward();
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Rewards") TSoftObjectPtr<UDataTable> PerkTable;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Rewards") TSubclassOf<AMCRewardChest> ChestClass;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Rewards") EMCRewardSelectionPolicy SelectionPolicy=EMCRewardSelectionPolicy::ChooseOne;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Rewards",meta=(ClampMin="1",ClampMax="8")) int32 MaxActiveChests=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Rewards",meta=(ClampMin="0")) float RewardCooldown=8.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Rewards",meta=(ClampMin="50",ClampMax="1500")) float FallHeight=450.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Rewards",meta=(ClampMin="1",ClampMax="64")) int32 CandidatesPerZone=16;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Rewards") int32 PendingRewards=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Rewards") int32 RewardsSpawned=0;
private:
    void TrySpawnReward();
    void CacheZones();
    UPROPERTY() TObjectPtr<UDataTable> LoadedTable;
    UPROPERTY() TArray<TObjectPtr<AMCRewardChest>> ActiveChests;
    TArray<TWeakObjectPtr<AMCRewardDropZone>> Zones;
    TSet<FName> CompletedIds;
    FRandomStream Random;
    FTimerHandle RetryTimer;
    double NextRewardAt=0;
    bool bResetting=false;
    bool bWarnedInvalidTable=false;
};
