#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MCResultPassives.generated.h"

class AActor;
class AMCToothCharacter;

/** Native provenance is never supplied by a client RPC. Secondary cleaning cannot proc another neighbor. */
enum class EMCStainCleanReason : uint8 { Brush, Spray, Residue, NeatFreak, Technical };

/** A synchronous mask mutation receipt. Consuming its serial twice cannot award twice. */
struct FMCStainCleaningAttempt
{
    TWeakObjectPtr<AActor> Target;
    uint64 Serial=0;
    bool bWasDirty=false;
    bool bMouthWasDirty=false;
};

/** Authoritative stain results; body coffee care and technical state restoration are separate. */
UCLASS()
class MESSCONTROL_API UMCResultPassives : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    static UMCResultPassives* ForWorld(const UObject* Context);
    static bool IsDirtyTarget(const AActor* Target);
    static float CleaningRate(const AMCToothCharacter* Worker,float Remaining,float BaseRate);

    void GetDirtyTargets(TArray<AActor*>& Out);
    bool IsMouthClean();
    FMCStainCleaningAttempt BeginCleaning(AActor* Target);
    bool CompleteCleaning(const FMCStainCleaningAttempt& Attempt,AMCToothCharacter* Worker,
        EMCStainCleanReason Reason,bool bActuallyRemoved);

private:
    void RegisterTarget(AActor* Target);
    void EnsureTargets();
    void RewardStain(AMCToothCharacter* Worker);
    void RewardCleanMouth();
    void TryNeighbor(AActor* Finished,AMCToothCharacter* Worker);
    TSet<TWeakObjectPtr<AActor>> Targets;
    TMap<TWeakObjectPtr<AActor>,uint64> PendingAttempts;
    FDelegateHandle SpawnHandle;
    uint64 NextSerial=0;
    bool bCollectedExistingTargets=false;
};
