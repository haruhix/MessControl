#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "MCSuperCarVisualComponent.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

/** Reusable presentation only. Its owner supplies travel/steering updates; this component never ticks or drives gameplay. */
UCLASS(ClassGroup=(MessControl),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCSuperCarVisualComponent : public USceneComponent
{
    GENERATED_BODY()
public:
    UMCSuperCarVisualComponent();
    /** Reflected defaults are traversed by loading/prewarm before first visible use. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SuperCar|Assembly") TMap<FName,TSoftObjectPtr<UStaticMesh>> PartAssets;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SuperCar|Assembly") TMap<FName,FTransform> PartTransforms;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="SuperCar|Assembly") TMap<FName,TObjectPtr<UStaticMeshComponent>> MeshParts;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SuperCar|Anchors") FVector SeatLocal=FVector(-31.229538,1.673586,98.544419);
    /** Native 106-vertex mittens fitted to the actual cockpit rim with 0.5cm clearance. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SuperCar|Anchors") FVector LeftHandLocal=FVector(-22.319839,-26.634930,138.254333);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SuperCar|Anchors") FVector RightHandLocal=FVector(-21.373074,26.836048,138.610611);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SuperCar|Anchors") FRotator LeftHandRotation=FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SuperCar|Anchors") FRotator RightHandRotation=FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SuperCar|Anchors") FVector LeftFootLocal=FVector(-26.757216,-22.415012,65.950161);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SuperCar|Anchors") FVector RightFootLocal=FVector(-25.763425,25.556290,65.950167);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SuperCar|Motion",meta=(ClampMin="5",ClampMax="100")) float FrontWheelRadius=33.9f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SuperCar|Motion",meta=(ClampMin="5",ClampMax="100")) float RearWheelRadius=40.8f;
    /** Retained for serialized six-part Driver assemblies; unused by the new disc-brush model. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SuperCar|Motion",meta=(ClampMin="5",ClampMax="100")) float LegacyRollerRadius=24.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SuperCar|Motion",meta=(ClampMin="0",ClampMax="45")) float MaxSteeringAngle=25.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SuperCar|Motion",meta=(ClampMin="0")) float SteeringResponse=10.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SuperCar|Motion") float BrushRPM=180.f;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="SuperCar|Motion") float CurrentSteeringAngle=0;
    UFUNCTION(BlueprintCallable,Category="SuperCar|Assembly") void RebuildVisuals();
    UFUNCTION(BlueprintPure,Category="SuperCar|Assembly") UStaticMeshComponent* GetPart(FName Name) const;
    /** Absolute signed distance, in centimeters. Also supports reverse rolling. */
    UFUNCTION(BlueprintCallable,Category="SuperCar|Motion") void SetTravelDistance(float Distance);
    /** Immediate steering for authored events. UpdateTravel provides interpolation. */
    UFUNCTION(BlueprintCallable,Category="SuperCar|Motion") void SetSteeringAngle(float Degrees);
    UFUNCTION(BlueprintCallable,Category="SuperCar|Motion") void SetBrushSpeed(float RPM);
    /** Call from an owner, sequencer adapter or event system; zero travel still spins brushes. */
    UFUNCTION(BlueprintCallable,Category="SuperCar|Motion") void UpdateTravel(float DeltaDistance,float SteeringDegrees,float DeltaSeconds);
    UFUNCTION(BlueprintPure,Category="SuperCar|Anchors") FTransform GetSeatFrame() const;
    UFUNCTION(BlueprintPure,Category="SuperCar|Anchors") FTransform GetHandFrame(bool bLeft) const;
    UFUNCTION(BlueprintPure,Category="SuperCar|Anchors") FTransform GetFootFrame(bool bLeft) const;
protected:
    virtual void OnRegister() override;
    virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
private:
    void ApplyPose();
    double TravelDistance=0;
    float BrushDegrees=0;
};
