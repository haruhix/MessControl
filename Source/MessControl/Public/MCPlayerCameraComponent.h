#pragma once
#include "CoreMinimal.h"
#include "Camera/CameraComponent.h"
#include "MCPlayerCameraComponent.generated.h"

class AMCToothCharacter;
class UMeshComponent;

/** Local view history belongs to the spectator, never to the observed pawn. */
struct FMCSpectatorCameraViewState
{
    struct FRevealedWall { ECollisionResponse CameraResponse=ECR_Block; float Amount=0.f; };
    FVector Focus=FVector::ZeroVector;
    FRotator Rotation=FRotator::ZeroRotator;
    float Distance=1000.f;
    float SafeArmLength=-1.f;
    bool bInitialized=false;
    TMap<TWeakObjectPtr<UMeshComponent>,FRevealedWall> RevealedWalls;
    double NextWallScan=0;
};

/** Player autofocus and short, additive physical impact feedback. */
UCLASS()
class MESSCONTROL_API UMCPlayerCameraComponent : public UCameraComponent
{
    GENERATED_BODY()
public:
    UMCPlayerCameraComponent();
    virtual void GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView) override;
    void AddImpact(float ImpactSpeed, const FVector& WorldDirection);
    void AddGroundImpact(float Strength,const FVector& Source);
    void AddCombatConfirm(float Strength);
    static void GetSpectatorView(const AMCToothCharacter* Target,const AActor* ViewerPawn,
        FRotator Orbit,float Distance,float DeltaTime,FMCSpectatorCameraViewState& State,FMinimalViewInfo& View);
    static void ClearSpectatorView(FMCSpectatorCameraViewState& State);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Depth of Field") bool bAutoFocusPlayer=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Impact",meta=(ClampMin="0",ClampMax="2")) float ImpactShakeScale=1.f;
private:
    double ImpactStartedAt=-1.;
    float ImpactStrength=0.f;
    FVector ImpactDirection=FVector::UpVector;
    double GroundImpactStartedAt=-1.;
    float GroundImpactStrength=0.f;
    FVector GroundImpactSource=FVector::ZeroVector;
    double CombatConfirmAt=-100.;
    float CombatConfirmStrength=0.f;
    double PresentedDamageAt=-100.;
    float PreviousHealth=-1.f;
    float DamageStrength=0.f;
};
