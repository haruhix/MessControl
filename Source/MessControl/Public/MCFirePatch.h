#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCFirePatch.generated.h"
UCLASS()
class MESSCONTROL_API AMCFirePatch : public AActor
{
    GENERATED_BODY()
public:
    AMCFirePatch();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    // Seconds == 0 burns until players extinguish it. Timed effects remain available for authored events.
    UFUNCTION(BlueprintCallable,meta=(WorldContext="Context"),Category="Hazards") static AMCFirePatch* Ignite(UObject* Context,FVector Point,float Radius=100,float Seconds=0,int32 Batch=0,bool CreateLesion=true);
    bool Extinguish(class AMCToothCharacter* Worker,float Dt);
    bool IsBurning() const { return Heat>0 && !IsActorBeingDestroyed(); }
    UPROPERTY(Replicated,BlueprintReadOnly) float Heat=1;
    UPROPERTY(Replicated,BlueprintReadOnly) float ExtinguishSeconds=.85f;
    UPROPERTY(Replicated) bool bCreateLesion=true;
    UPROPERTY(Replicated,BlueprintReadOnly) float BurnRadius=100;
    UPROPERTY(Replicated,BlueprintReadOnly) float BurnSeconds=6;
    UPROPERTY(Replicated,BlueprintReadOnly) int32 Batch=0;
    UPROPERTY(Replicated,BlueprintReadOnly) double StartedAt=0;
    UPROPERTY(Replicated) TObjectPtr<class AMCTongue> Tongue;
    UPROPERTY(Replicated) FVector SurfaceAnchor=FVector::ZeroVector;
    UPROPERTY(Replicated) TObjectPtr<class AMCMouthSurface> Lesion;
private:
    double NextDamageAt=0;
    uint64 LastTreatmentFrame=MAX_uint64;
    UPROPERTY() TObjectPtr<class AMCReactionVFX> Flames;
};
