#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCMotionRecorder.generated.h"

/** Optional per-frame diagnostics, sampled after skeletal physics blending. */
UCLASS()
class MESSCONTROL_API UMCMotionRecorder : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCMotionRecorder();
    void Start(float Seconds,const FString& Label);
    void Stop();
    FString Stage=TEXT("gameplay");
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* TickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    float Remaining=0;
    FString Filename,Rows;
};
