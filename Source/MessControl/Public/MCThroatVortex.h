#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCThroatVortex.generated.h"

class AMCFoodActor;
class UMCWorldPresentationComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class UAudioComponent;
class USoundBase;
struct FMCPerkVFXGameplayValues;
struct FMCThroatVortexGeometry;
struct FMCThroatVortexGeometryDeleter
{
    void operator()(FMCThroatVortexGeometry* Geometry) const;
};

/** Bounded cosmetic projection of an already accepted native intake, not another admission. */
USTRUCT()
struct FMCThroatIntakeBoundary
{
    GENERATED_BODY()
    UPROPERTY() uint32 Serial=0;
    UPROPERTY() double AcceptedAt=0;
    UPROPERTY() FVector Point=FVector::ZeroVector;
};

/** One shared, collisionless intake effect per meal. Cosmetic cost is independent of meal size. */
UCLASS()
class MESSCONTROL_API AMCThroatVortex : public AActor
{
    GENERATED_BODY()
public:
    AMCThroatVortex();
    virtual ~AMCThroatVortex() override;
    static AMCThroatVortex* Spawn(UWorld* World, FVector Inlet, FVector InwardDirection,
        float Radius, float FlowLength, float Duration, const TArray<AMCFoodActor*>& FoodSources,
        float HalfWidth=0.f, float Height=0.f,AActor* PreparedOwner=nullptr);
    UMCWorldPresentationComponent* GetComposerPresentation()const{return ComposerPresentation;}
    const FString& GetPreparedSourceHandoffError()const{return PreparedSourceHandoffError;}
    bool HasPreparedSourceHandoff()const{return bPreparedSourceHandoff;}
    bool HasAcceptedComposerStart()const{return bComposerStartAccepted;}
    bool HasAcceptedComposerLoop()const{return bComposerLoopAccepted;}
    bool HasAcceptedComposerIntake()const{return bComposerIntakeAccepted;}
    bool HasAcceptedComposerEnd()const{return bComposerEndAccepted;}
    void NotifyNativeIntake(FVector AcceptedPoint);
    void NotifyNaturalEnd();
    void CancelPresentation();
    virtual void LifeSpanExpired()override;
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Airflow;
    /** Local room cue on every peer; no distance culling or server audio voice. */
    UPROPERTY(VisibleAnywhere) TObjectPtr<UAudioComponent> SuctionAudio;
    UPROPERTY(EditDefaultsOnly, Category="Throat|VFX") TSoftObjectPtr<UMaterialInterface> RibbonMaterial;
    UPROPERTY(EditDefaultsOnly, Category="Throat|VFX") TSoftObjectPtr<UMaterialInterface> MistMaterial;
    UPROPERTY(EditDefaultsOnly, Category="Throat|VFX") TSoftObjectPtr<UMaterialInterface> GlowMaterial;
    UPROPERTY(EditDefaultsOnly, Category="Throat|Audio") TSoftObjectPtr<USoundBase> IntakeSound;
    UPROPERTY(EditDefaultsOnly, Category="Throat|Audio") TSoftObjectPtr<USoundBase> AirflowSound;
    UPROPERTY(Replicated) double StartedAt=0;
    UPROPERTY(Replicated) float Duration=2;
    UPROPERTY(Replicated) float Radius=260;
    UPROPERTY(Replicated) float FlowLength=600;
    /** Mouth-wide field dimensions; Radius remains the near-inlet curl size. */
    UPROPERTY(Replicated) float FlowHalfWidth=600;
    UPROPERTY(Replicated) float FlowHeight=260;
    UPROPERTY(Replicated) FVector InwardDirection=FVector::ForwardVector;
    /** Sampled on the server once; at most 24 food wakes, even with hundreds of fragments. */
    UPROPERTY(Replicated) TArray<TObjectPtr<AMCFoodActor>> FoodSources;

private:
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
    TUniquePtr<FMCThroatVortexGeometry,FMCThroatVortexGeometryDeleter> Geometry;
    UPROPERTY(Transient) TObjectPtr<class UMCWorldPresentationComponent> ComposerPresentation;
    FGuid ComposerEpisode=FGuid::NewGuid();
    bool bComposerOnsetIssued=false;
    bool bPreparedSourceHandoff=false,bComposerStartAccepted=false,bComposerLoopAccepted=false;
    FString PreparedSourceHandoffError;
    UPROPERTY(Replicated) TArray<FMCThroatIntakeBoundary> RecentIntakes;
    uint32 IntakeSerial=0,ConsumedIntakeSerial=0;
    bool bPresentationCanceled=false,bNormalEndIssued=false;
    UPROPERTY(Replicated) bool bNativeNormalEnd=false;
    bool bComposerIntakeAccepted=false,bComposerEndAccepted=false;
    TSharedPtr<FMCPerkVFXGameplayValues> LastComposerValues;
    void DispatchIntakes(const FMCPerkVFXGameplayValues& Current);
    bool UpdateComposerArtwork(float Age,FVector View,FVector Right,FVector Up);
    float NextAirflowCueAt=0;
    double ServerNow() const;
};
