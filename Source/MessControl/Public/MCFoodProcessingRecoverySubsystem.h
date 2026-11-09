#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MCFoodProcessingRecoverySubsystem.generated.h"

class AMCFoodActor;
class AActor;
struct FMCFoodProcessingRecoveryState;

enum class EMCFoodProcessingFailure : uint8
{
    UnsupportedGeometry,
    InvalidGeometry,
    CollisionPreparation,
    MissingDestination,
    SpawnFailure
};

/** Authority-only recovery of reported processing defects, never idle-food completion. */
UCLASS()
class MESSCONTROL_API UMCFoodProcessingRecoverySubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    /** Queue a defect; replacement cannot occur inside this call or its world tick. */
    static void ReportFailure(AMCFoodActor* Food,EMCFoodProcessingFailure Reason);
    /** Resident-only route/end-point checks before a whole ingredient becomes issued work. */
    static bool CanAdmit(const AMCFoodActor* Food);
    void TickProcessRecords();
    virtual void Tick(float DeltaTime) override;
    virtual void Deinitialize() override;
    virtual bool IsTickable() const override;
    virtual TStatId GetStatId() const override;
    int32 GetPendingRecordCount() const;
    int32 GetReplacementCount() const;
    int32 GetBlockedCount() const;
private:
    bool bDeinitialized=false;
    TSharedPtr<FMCFoodProcessingRecoveryState> State;
    void CaptureHealthyEndpoints(bool bForce=false);
    bool EnsureDestinations();
    bool RecoverFood(AMCFoodActor* Food,EMCFoodProcessingFailure Reason,int32& CandidateCursor,bool& bExhausted);
    UFUNCTION() void OnEndpointDestroyed(AActor* Endpoint);
    bool IsWorldReady() const;
};
