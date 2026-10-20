#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MCTravelLoadingSubsystem.generated.h"

struct FWorldContext;

/** Native Slate fallback can animate while a normal map load blocks the game thread. */
UCLASS()
class MESSCONTROL_API UMCTravelLoadingSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
private:
    void BeforeMapLoad(const FWorldContext& Context,const FString& MapName);
    FDelegateHandle BeforeLoadHandle;
};
