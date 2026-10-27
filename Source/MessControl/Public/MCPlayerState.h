#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "MCPresentationTypes.h"
#include "MCLevelUpOffer.h"
#include "MCRogueTypes.h"
#include "MCPlayerState.generated.h"

class UMCPerkComponent;
class UMCHubEconomyComponent;

UENUM(BlueprintType)
enum class EMCScoreTask : uint8 { Coffee, Repair, Food, Ulcer, Ice };

USTRUCT(BlueprintType)
struct FMCScoreRewards
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) int32 Coffee=10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) int32 Repair=20;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) int32 Food=5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) int32 Ulcer=25;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) int32 Ice=10;
    int32 ForTask(EMCScoreTask Task) const;
};

/** Identity and contribution survive pawn death and are visible to every peer. */
UCLASS()
class MESSCONTROL_API AMCPlayerState : public APlayerState
{
    GENERATED_BODY()
public:
    AMCPlayerState();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    virtual void CopyProperties(APlayerState* Target) override;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Score") int32 Points=0;
    /** Existing saved fish-wallet field; kept stable for old profiles and grill receipts. */
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Fishing") int32 FishCaught=0;
    /** Authority only; returns the actual credit after the persisted wallet's safety cap. */
    int32 AddCaughtFish(int32 Amount=1);
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Multiplayer") bool bSessionHost=false;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Appearance") FLinearColor PlayerColor=FLinearColor(.24f,.65f,1.f);
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Alarms") EMCPlayerAlarm Alarm=EMCPlayerAlarm::None;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Alarms") double AlarmUntil=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Perks") TObjectPtr<UMCPerkComponent> Perks;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Economy") TObjectPtr<UMCHubEconomyComponent> Economy;
    /** Session/travel marker; never supplied by clients or written to the profile. */
    bool bHubProfileLoaded=false;
    void AddPoints(int32 Amount);
    void ResetMatchScore();
    /** Count completed chest choices for the optional reward pool's early-run gate. */
    void RecordChestRewardChoice();
    /** Legacy entry points discard retired XP offers; only chests create rewards. */
    void QueueChoicesThroughLevel(int32 TeamLevel);
    void RefreshLevelUpOffer();
    bool TryChooseLevelUpPerk(const FGuid& OfferId,int32 ChoiceIndex);
    bool TryRerollLevelPerks(const FGuid& OfferId);
    void ResetLevelUpChoices();
    UFUNCTION(BlueprintPure,Category="Progression") bool HasPendingLevelChoices() const { return false; }
    UPROPERTY(ReplicatedUsing=OnRep_LevelUpOffer,BlueprintReadOnly,Category="Progression") FMCLevelUpOffer LevelUpOffer;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Progression") int32 PendingLevelChoices=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Progression") int32 PersonalChoicesTaken=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Progression") bool bRogueRerollSpent=false;
    /** Server-only consumption record accompanies perks when PlayerState is replaced. */
    UPROPERTY(Transient) TMap<FName,int32> ConsumedRecoveryStacks;
private:
    UFUNCTION() void OnRep_LevelUpOffer();
    UPROPERTY() TArray<int32> PendingLevels;
    int32 LastQueuedTeamLevel=1;
    bool bChoosingLevelPerk=false;
    bool bWarnedEmptyLevelPool=false;
    UPROPERTY() TArray<FMCRogueRecentOffer> RecentRogueOffers;
    bool CreateRogueLevelOffer(bool Reroll);
};
