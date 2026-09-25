#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/HUD.h"
#include "MCArenaDemo.generated.h"
class AMCArenaTooth;
class ACameraActor;

/** Opt-in development recording, spawned only with -MCArenaDemo. */
UCLASS()
class AMCArenaDemo : public AActor
{
    GENERATED_BODY()
public:
    AMCArenaDemo();
    virtual void Tick(float DeltaSeconds) override;
    float Age=-4;
    UPROPERTY() TObjectPtr<AMCArenaTooth> Subject;
private:
    UPROPERTY() TObjectPtr<ACameraActor> Camera;
    FVector Anchor;
    int32 Frame=0;
    int32 Stage=0;
};

UCLASS()
class AMCArenaDemoHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};
