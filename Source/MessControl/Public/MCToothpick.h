#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCToothpick.generated.h"

class AMCTongue;
class AMCMouthSurface;
class AMCToothCharacter;
class UStaticMeshComponent;
class UWidgetComponent;

UENUM(BlueprintType)
enum class EMCToothpickState : uint8 { Impaled, Extracted, Broken };

/** One shared obstacle: pull it out, break it with a pickaxe, then treat its wound. */
UCLASS()
class MESSCONTROL_API AMCToothpick : public AActor
{
    GENERATED_BODY()
public:
    AMCToothpick();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    bool Impale(AMCTongue* Surface, FVector Point, bool bTraining=false, int32 InBatch=0);
    bool CanPull(const AMCToothCharacter* Worker) const;
    bool TryPull(AMCToothCharacter* Worker,float Seconds);
    bool CanReceivePickaxeHit(const AMCToothCharacter* Worker) const;
    bool HitWithPickaxe(AMCToothCharacter* Worker,float Damage);
    static AMCToothpick* FindPullTarget(const AMCToothCharacter* Worker);
    bool IsBroken() const { return State==EMCToothpickState::Broken; }
    bool IsWoundHealed() const { return bWoundHealed; }
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UWidgetComponent> ActionIndicator;
    UPROPERTY(ReplicatedUsing=RefreshAppearance,BlueprintReadOnly) EMCToothpickState State=EMCToothpickState::Impaled;
    UPROPERTY(Replicated,BlueprintReadOnly) float PullProgress=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.2",ClampMax="5")) float PullSeconds=1.2f;
    UPROPERTY(Replicated,BlueprintReadOnly) TObjectPtr<AMCMouthSurface> Ulcer;
    UPROPERTY(Replicated,BlueprintReadOnly) int32 Batch=0;
    UPROPERTY(Replicated,BlueprintReadOnly) bool bWoundHealed=false;
    UFUNCTION() void RefreshAppearance();
private:
    UPROPERTY(Replicated) TObjectPtr<AMCTongue> Tongue;
    UPROPERTY(Replicated) FVector SurfaceAnchor=FVector::ZeroVector;
    uint64 LastPullFrame=MAX_uint64;
};
