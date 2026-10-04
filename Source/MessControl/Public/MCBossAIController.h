#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "MCBossAIController.generated.h"

class AMCBossCharacter;
class AMCToothCharacter;

/** A small server state machine. Behavior Trees/State Trees can replace Decide without changing the boss contract. */
UCLASS(Blueprintable)
class MESSCONTROL_API AMCBossAIController : public AAIController
{
    GENERATED_BODY()
public:
    AMCBossAIController();
    void StartBossBrain();
    void StopBossBrain();
protected:
    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnUnPossess() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void OnMoveCompleted(FAIRequestID RequestID,const FPathFollowingResult& Result) override;
private:
    void Decide();
    AMCToothCharacter* FindVisiblePlayer() const;
    UPROPERTY(Transient) TObjectPtr<AMCBossCharacter> Boss;
    FTimerHandle DecisionTimer;
    double NextTargetScanAt=0;
    double TargetLastVisibleAt=0;
    double NextPathRequestAt=0;
    FVector LastVisiblePosition=FVector::ZeroVector;
};
