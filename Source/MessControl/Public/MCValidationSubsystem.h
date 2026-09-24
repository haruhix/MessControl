#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MCValidationSubsystem.generated.h"

// Opt-in development harness. Inert without -MCSmoke / -MCCapture; disabled in Shipping.
UCLASS()
class UMCValidationSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Tick(float DeltaSeconds) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UMCValidationSubsystem,STATGROUP_Tickables); }
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override { return WorldType == EWorldType::Game || WorldType == EWorldType::PIE; }
private:
    float Age = 0;
    float NextLog = 5;
    bool bCaptured = false;
    bool bCapturedLab = false;
    bool bObservedWork = false;
    bool bObservedPlayers = false;
    bool bRagdollSetup=false;
    float NextSwing=3.f;
    int32 ObservedFalls=0;
    int32 ObservedRecoveries=0;
    int32 CaptureStage=0;
    bool bInvalidPhysics=false;
};
