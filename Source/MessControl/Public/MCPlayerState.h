#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "MCPresentationTypes.h"
#include "MCLevelUpOffer.h"
#include "MCPlayerState.generated.h"

class UMCPerkComponent;

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
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Multiplayer") bool bSessionHost=false;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Appearance") FLinearColor PlayerColor=FLinearColor(.24f,.65f,1.f);
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Alarms") EMCPlayerAlarm Alarm=EMCPlayerAlarm::None;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Alarms") double AlarmUntil=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Perks") TObjectPtr<UMCPerkComponent> Perks;
    void AddPoints(int32 Amount);
    void ResetMatchScore();
    /** Queue all unclaimed team levels; repeated synchronization cannot duplicate choices. */
    void QueueChoicesThroughLevel(int32 TeamLevel);
    void RefreshLevelUpOffer();
    bool TryChooseLevelUpPerk(const FGuid& OfferId,int32 ChoiceIndex);
    void ResetLevelUpChoices();
    UFUNCTION(BlueprintPure,Category="Progression") bool HasPendingLevelChoices() const { return PendingLevelChoices>0; }
    UPROPERTY(ReplicatedUsing=OnRep_LevelUpOffer,BlueprintReadOnly,Category="Progression") FMCLevelUpOffer LevelUpOffer;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Progression") int32 PendingLevelChoices=0;
    /** Server-only consumption record accompanies perks when PlayerState is replaced. */
    UPROPERTY(Transient) TMap<FName,int32> ConsumedRecoveryStacks;
private:
    UFUNCTION() void OnRep_LevelUpOffer();
    UPROPERTY() TArray<int32> PendingLevels;
    int32 LastQueuedTeamLevel=1;
    bool bChoosingLevelPerk=false;
    bool bWarnedEmptyLevelPool=false;
};
