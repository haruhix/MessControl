#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MCFoodCutStagingSubsystem.generated.h"

class AMCFoodActor;

/** Game-thread actor staging, shared by all large food cuts in this world. */
UCLASS()
class MESSCONTROL_API UMCFoodCutStagingSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    static constexpr int32 MaxStagedActorsPerTick=8;
    static constexpr double FrameBudgetSeconds=.004;
    void Enqueue(AMCFoodActor* Food);
    virtual void Tick(float DeltaTime) override;
    virtual void Deinitialize() override;
    virtual bool IsTickable() const override;
    virtual TStatId GetStatId() const override;
    int32 GetLastStagedActors() const {return LastStagedActors;}
private:
    TArray<TWeakObjectPtr<AMCFoodActor>> Pending;
    int32 LastStagedActors=0;
};
