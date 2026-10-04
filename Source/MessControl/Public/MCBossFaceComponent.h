#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCBossFaceComponent.generated.h"

class AMCBossCharacter;
class USkeletalMesh;
class USkeletalMeshComponent;

/** Local facial presentation from the boss's replicated state; it never changes combat. */
UCLASS(ClassGroup=(Presentation), meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCBossFaceComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCBossFaceComponent();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /** Call after changing the skeletal mesh or reimporting its morph targets. */
    UFUNCTION(BlueprintCallable, Category="Boss|Face") void RefreshMorphTargets();
    /** Cosmetic only. Pass the replicated server start time for the encounter intro. */
    UFUNCTION(BlueprintCallable, Category="Boss|Face") void PlayRoarExpression(double ServerStartedAt, float Duration=5.f);
    UFUNCTION(BlueprintCallable, Category="Boss|Face") void ClearRoarExpression();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boss|Face", meta=(ClampMin="0", ClampMax="1")) float CombatAnger=.78f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boss|Face", meta=(ClampMin="0.1", ClampMax="2")) float HitReactionSeconds=.55f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boss|Face", meta=(ClampMin="0.05", ClampMax="0.5")) float BlinkSeconds=.16f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boss|Face", meta=(ClampMin="1", ClampMax="10")) float MinBlinkInterval=2.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boss|Face", meta=(ClampMin="1", ClampMax="12")) float MaxBlinkInterval=4.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boss|Face", meta=(ClampMin="1", ClampMax="60")) float ResponseSpeed=20.f;

    UPROPERTY(Transient, BlueprintReadOnly, Category="Boss|Face") float Pain=0.f;
    UPROPERTY(Transient, BlueprintReadOnly, Category="Boss|Face") float Anger=0.f;
    UPROPERTY(Transient, BlueprintReadOnly, Category="Boss|Face") float Roar=0.f;
    UPROPERTY(Transient, BlueprintReadOnly, Category="Boss|Face") float Blink=0.f;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    struct FMorphChannel
    {
        FName Name;
        float Weight=0.f;
        bool bAvailable=false;
    };
    TWeakObjectPtr<AMCBossCharacter> Boss;
    TWeakObjectPtr<USkeletalMeshComponent> Mesh;
    TWeakObjectPtr<USkeletalMesh> CachedAsset;
    TArray<FMorphChannel> Channels;
    FRandomStream BlinkRandom;
    double BlinkStartedAt=-1000.;
    double NextBlinkAt=0.;
    double RoarStartedAt=-1000.;
    float RoarSeconds=0.f;
    double ServerNow() const;
    void ApplyChannel(int32 Index, float Target, float Smoothing);
};
