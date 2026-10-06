#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MCTutorialTypes.h"
#include "MCFrontEndValidationSubsystem.generated.h"

class AMCTutorialDirector;
class AMCToothCharacter;
class AMCFoodActor;

/** Opt-in integration smoke harness; it cannot be instantiated in a normal or Shipping run. */
UCLASS()
class UMCFrontEndValidationSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override { return Type==EWorldType::Game || Type==EWorldType::PIE; }
    virtual void Tick(float Dt) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UMCFrontEndValidationSubsystem,STATGROUP_Tickables); }
private:
    void TickMenu();
    void TickTutorial();
    bool Deliver(AMCFoodActor* Food,AMCToothCharacter* Hero);
    bool ClearCalculus(AMCTutorialDirector* Director,AMCToothCharacter* Hero,class AMCArenaTooth* Tooth);
    void Capture(const FString& Name);
    void Finish(bool bSuccess,const FString& Reason);
    double StartedAt=0,StageSeenAt=0,NextMenuAt=2,FinishedAt=0;
    EMCTutorialStage LastStage=EMCTutorialStage::Finished;
    uint32 SeenStages=0;
    int32 MenuStep=0,ExpectedPeers=4;
    bool bCaptured=false,bSawTutorial=false,bSawWidget=false,bSawComplete=false,bReadyWaitVerified=false,bReadySent=false;
    bool bWrongFreshChecked=false,bWrongFreshPending=false,bWrongSpoiledChecked=false,bCoffeeSafetyChecked=false,bFinished=false;
    float InitialMouthHealth=-1;
};
