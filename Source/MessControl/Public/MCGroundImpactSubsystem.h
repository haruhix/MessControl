#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MCGroundImpactSubsystem.generated.h"

class UPrimitiveComponent;

/** Server-side heavy prop landings, independent of damage to a player. */
UCLASS()
class MESSCONTROL_API UMCGroundImpactSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UMCGroundImpactSubsystem,STATGROUP_Tickables); }
protected:
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override { return Type==EWorldType::Game || Type==EWorldType::PIE; }
private:
    void QueueActor(AActor* Actor);
    void RegisterActor(AActor* Actor);
    UFUNCTION() void OnBodyHit(UPrimitiveComponent* Body,AActor* OtherActor,UPrimitiveComponent* OtherBody,FVector NormalImpulse,const FHitResult& Hit);
    struct FBodyState { double LastImpactAt=-10.; bool bOriginallyNotify=false; };
    struct FImpact { FVector Point; float Strength=0,Radius=0; };
    TMap<TWeakObjectPtr<UPrimitiveComponent>,FBodyState> Bodies;
    TArray<TWeakObjectPtr<AActor>> PendingActors;
    TArray<FImpact> PendingImpacts;
    FDelegateHandle SpawnHandle;
    float DiscoveryRemaining=0.f;
    double LastDispatchAt=-10.;
};
