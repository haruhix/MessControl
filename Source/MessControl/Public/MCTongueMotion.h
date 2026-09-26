#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MCTongueMotion.generated.h"

UENUM(BlueprintType)
enum class EMCTongueShape : uint8 { FrontBend, RadialWave, LocalLift, DirectionalWave };

/** Per-event value snapshot. Replicated, so clients never depend on an asset edit during playback. */
USTRUCT(BlueprintType)
struct FMCTongueMotionSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") EMCTongueShape Shape=EMCTongueShape::LocalLift;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape",meta=(ClampMin="-220",ClampMax="220")) float Height=60;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape",meta=(ClampMin="100")) float Radius=550;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Wave",meta=(ClampMin="100")) float Width=260;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Wave",meta=(ClampMin="100")) float Speed=850;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing",meta=(ClampMin="0")) float Anticipation=.5f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing",meta=(ClampMin="0.25")) float Rise=.35f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing",meta=(ClampMin="0")) float Hold=.15f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing",meta=(ClampMin="0.5")) float Return=1.4f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing",meta=(ClampMin="0")) float RestAfter=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Animation",meta=(ClampMin="0",ClampMax="0.25")) float Compression=.12f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Animation",meta=(ClampMin="0",ClampMax="0.1")) float FollowThrough=.035f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color",meta=(ClampMin="0",ClampMax="1")) float Redness=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Force",meta=(ClampMin="0",ClampMax="1200")) float Lift=450;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Force",meta=(ClampMin="0",ClampMax="1000")) float Push=200;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Force",meta=(ClampMin="20",ClampMax="400")) float AffectHeight=170;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Force") bool bPushFromOrigin=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Force") bool bFoodLiftIgnoresMass=false;
    void Sanitize();
    bool IsWave() const { return Shape==EMCTongueShape::RadialWave || Shape==EMCTongueShape::DirectionalWave; }
    float Duration() const;
    float Envelope(float Age) const;
    float Band(float Distance,float Age) const;
    bool Crossed(float Distance,float Before,float Age) const;
};

UCLASS(BlueprintType)
class MESSCONTROL_API UMCTongueMotionProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Motion") FText Label;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Motion") FMCTongueMotionSettings Settings;
};

USTRUCT(BlueprintType)
struct FMCTongueMotionState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FMCTongueMotionSettings Settings;
    UPROPERTY(BlueprintReadOnly) FVector Origin=FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FVector Direction=FVector::ForwardVector;
    UPROPERTY(BlueprintReadOnly) double StartedAt=-100;
    UPROPERTY(BlueprintReadOnly) int32 Serial=0;
};
