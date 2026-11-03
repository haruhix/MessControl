#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCVomitBurst.generated.h"

class UProceduralMeshComponent;
class UInstancedStaticMeshComponent;
class AMCThroat;

/** Server-owned ballistic portions; clients animate the same flight and confirmed impacts. */
USTRUCT()
struct FMCVomitPortion
{
    GENERATED_BODY()
    UPROPERTY() FVector Start=FVector::ZeroVector;
    UPROPERTY() FVector Velocity=FVector::ZeroVector;
    UPROPERTY() FVector Impact=FVector::ZeroVector;
    UPROPERTY() FVector Normal=FVector::UpVector;
    UPROPERTY() float Delay=0;
    UPROPERTY() float Duration=1;
    UPROPERTY() float Radius=18;
    UPROPERTY() float ImpactAge=1;
    UPROPERTY() bool bLanded=false;
    UPROPERTY() bool bOnTongue=false;
};

UCLASS()
class MESSCONTROL_API AMCVomitBurst : public AActor
{
    GENERATED_BODY()
public:
    AMCVomitBurst();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    void Configure(AMCThroat* Throat);
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Stream;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> Droplets;
    UPROPERTY(Replicated) TArray<FMCVomitPortion> Portions;
    UPROPERTY(Replicated) double StartedAt=0;
    UPROPERTY(Replicated) int32 Batch=0;
    int32 LandedPortions() const;
    bool HasAcceptedComposerBurst()const{return bComposerBurstAccepted;}
    int32 AirborneInstances=0,SplashInstances=0;
    static FVector FlightPoint(const FMCVomitPortion& Portion,float T);
private:
    double ServerNow() const;
    void Land(int32 Index,const FHitResult& Hit,float Age);
    void UpdatePresentation(float Age);
    UPROPERTY(Transient) TObjectPtr<class UMCWorldPresentationComponent> ComposerPresentation;
    FGuid ComposerEpisode=FGuid::NewGuid();
    TArray<FVector4f> SpreadSamples;
    int32 SampleBatch=MIN_int32,SampleCount=INDEX_NONE;
    void EnsureSpreadSamples();
    bool UpdateComposerArtwork(float Age);
    bool bComposerBurstAccepted=false;
    float CheckedAge=0;
};
