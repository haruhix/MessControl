#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCRoguelikePreview.generated.h"

class AMCToothCharacter;
class AMCRewardChest;
class AMCPerkPickup;
class AMCBossCharacter;
class ACameraActor;
class UMCPerkComponent;

/** Explicit command-line demonstration of the integrated foundation; never spawned by a normal match. */
UCLASS()
class MESSCONTROL_API AMCRoguelikePreview : public AActor
{
    GENERATED_BODY()
public:
    AMCRoguelikePreview();
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    void Step();
    void Finish(bool Passed,const FString& Detail);
    void Photograph(const TCHAR* Filename,FVector Focus);
    bool PlacePlayer(FVector FloorPoint);
    UPROPERTY() TObjectPtr<AMCToothCharacter> Hero;
    UPROPERTY() TObjectPtr<UMCPerkComponent> Perks;
    UPROPERTY() TObjectPtr<AMCRewardChest> Chest;
    UPROPERTY() TObjectPtr<AMCPerkPickup> Chosen;
    UPROPERTY() TObjectPtr<AMCBossCharacter> Boss;
    UPROPERTY() TObjectPtr<ACameraActor> Camera;
    FTimerHandle StepTimer;
    int32 Stage=0,ExpectedPlayers=1,BeforeStacks=0,BeforeSpawned=0;
    double StartedAt=0,StageStartedAt=0;
    FName ChosenID;
    FVector BossStarted=FVector::ZeroVector;
    bool bSawChase=false,bSawTarget=false,bSawTelegraph=false,bSawAttack=false,bFinished=false;
};
