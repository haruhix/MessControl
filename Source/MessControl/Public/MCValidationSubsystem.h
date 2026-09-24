#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MCValidationSubsystem.generated.h"

// Opt-in development harness (-MCSmoke, -MCCapture, -MCRagdoll, -MCLimbs); disabled in Shipping.
UCLASS()
class UMCValidationSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Tick(float DeltaSeconds) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UMCValidationSubsystem,STATGROUP_Tickables); }
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override { return WorldType == EWorldType::Game || WorldType == EWorldType::PIE; }
private:
    void TickLimbStability(float DeltaSeconds);
    TMap<FName,FQuat> LastLimbRotations;
    TMap<FName,FQuat> NeutralLimbRotations;
    float MaxLimbAngle=0.f;
    float MaxIdleSpin=0.f;
    float MaxIdleDeviation=0.f;
    float BadLimbSeconds=0.f;
    float LimbSnapshotAt=3.f;
    bool bLimbKnockedDown=false;
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
    bool bArenaInitial=false;
    bool bArenaDamaged=false;
    bool bArenaLost=false;
    int32 ArenaStage=0;
    float ArenaReadyAt=-1;
};
