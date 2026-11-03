#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MCVFXActionLayout.generated.h"

/** Editor projection of an existing event group. Runtime name/enabled/mute live only in the profile. */
USTRUCT()
struct FMCComposerGroupLayout
{
    GENERATED_BODY()
    UPROPERTY() FGuid GroupID;
    UPROPERTY() FVector2D Position=FVector2D::ZeroVector;
    UPROPERTY() FVector2D Size=FVector2D(340,220);
    UPROPERTY() FLinearColor Color=FLinearColor(.12f,.30f,.31f);
    UPROPERTY() int32 Order=0;
};

USTRUCT()
struct FMCVFXEventGraphLayout
{
    GENERATED_BODY()
    UPROPERTY() FName Event;
    UPROPERTY() bool bHasCardPosition=false;
    UPROPERTY() FVector2D CardPosition=FVector2D::ZeroVector;
    UPROPERTY() TArray<FMCComposerGroupLayout> Groups;
    /** VFX positions remain exclusively in Recipe.Nodes. */
    UPROPERTY() TMap<FGuid,FVector2D> SFXLayerPositions;
};

USTRUCT()
struct FMCVFXActionLayout
{
    GENERATED_BODY()
    UPROPERTY() bool bHasHubPosition=false;
    UPROPERTY() FVector2D HubPosition=FVector2D::ZeroVector;
    UPROPERTY() TArray<FMCVFXEventGraphLayout> Events;
};

/** One transactional authoring state; never referenced by a runtime profile. */
UCLASS()
class UMCVFXActionLayoutState : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY() FMCVFXActionLayout Layout;
    virtual bool IsEditorOnly() const override { return true; }
};
