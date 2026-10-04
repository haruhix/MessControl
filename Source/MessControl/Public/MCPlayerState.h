#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "MCPresentationTypes.h"
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
};
