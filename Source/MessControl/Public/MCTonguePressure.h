#pragma once
#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "Engine/DataAsset.h"
#include "MCTonguePressure.generated.h"

/** Small, bounded dents from supported loads, independent of event motion. */
USTRUCT(BlueprintType)
struct FMCTonguePressureSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pressure",meta=(ToolTip="Weight deforms the same surface used for rendering and collision.")) bool bEnabled=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pressure",meta=(ClampMin="0",ClampMax="3")) float DepthPerKg=.4f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pressure",meta=(ClampMin="1",ClampMax="35")) float MaxDepth=8;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pressure",meta=(ClampMin="2",ClampMax="6",ToolTip="2 gives a broad soft bowl; 6 concentrates pressure near the centre. The rim always joins smoothly.")) float FalloffPower=3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Weight Scales",meta=(ClampMin="0",ClampMax="3",ToolTip="Response to standing/walking players. 0 disables their pressure.")) float PlayerDepthScale=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Weight Scales",meta=(ClampMin="0",ClampMax="3")) float RagdollDepthScale=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Weight Scales",meta=(ClampMin="0",ClampMax="3")) float FoodDepthScale=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Footprint",meta=(ClampMin="60",ClampMax="250")) float PlayerRadius=120;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Footprint",meta=(ClampMin="60",ClampMax="300")) float RagdollRadius=150;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Footprint",meta=(ClampMin="30",ClampMax="120")) float FoodMargin=70;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Footprint",meta=(ClampMin="0.5",ClampMax="1.5",ToolTip="Width across the player's facing direction relative to Player Radius.")) float PlayerWidthRatio=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Footprint",meta=(ClampMin="0.5",ClampMax="1.5")) float RagdollWidthRatio=.8f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing",meta=(ClampMin="0.06",ClampMax="1")) float PressSeconds=.12f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing",meta=(ClampMin="0.15",ClampMax="5",ToolTip="Time for roughly 63% of a vacated dent to recover. Applies to both the visible mesh and collision.")) float RecoverSeconds=.6f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing",meta=(ClampMin="0",ClampMax="1",ToolTip="How long the surface remembers a stronger load before recovering. 0 starts recovery immediately.")) float TrailHoldSeconds=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Landing",meta=(ClampMin="0",ClampMax="2")) float LandingBoost=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Landing",meta=(ClampMin="100",ClampMax="1200")) float LandingSpeed=650;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Landing",meta=(ClampMin="0.08",ClampMax="1",ToolTip="Decay time of the extra pressure from landing, in seconds.")) float LandingSeconds=.22f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Contact",meta=(ClampMin="3",ClampMax="20")) float ContactTolerance=12;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Budget",meta=(ClampMin="4",ClampMax="32")) int32 MaxSources=24;
    void Sanitize();
};

/** Duplicate this asset to author a pressure behaviour and matching material instance. */
UCLASS(BlueprintType)
class MESSCONTROL_API UMCTonguePressurePreset : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preset") FText Label;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preset",meta=(MultiLine="true")) FText Description;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preset") FMCTonguePressureSettings Settings;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance",meta=(ToolTip="Optional material instance. Uses the tongue's usual material when empty. Pressure is VertexColor.G; rim slope is VertexColor.B. Do not add WPO.")) TObjectPtr<class UMaterialInterface> SurfaceMaterial;
};

UENUM(BlueprintType)
enum class EMCTongueLoadKind : uint8 { Player, Ragdoll, Food };

USTRUCT(BlueprintType)
struct FMCTongueLoad
{
    GENERATED_BODY()
    // Actor is for inspection only: unresolved/destroyed network actors do not invalidate the point.
    UPROPERTY(BlueprintReadOnly) TObjectPtr<AActor> Actor;
    UPROPERTY(BlueprintReadOnly) EMCTongueLoadKind Kind=EMCTongueLoadKind::Player;
    UPROPERTY(BlueprintReadOnly) FVector_NetQuantize10 LocalPoint=FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FVector_NetQuantizeNormal Axis=FVector::ForwardVector;
    UPROPERTY(BlueprintReadOnly) float RadiusX=120;
    UPROPERTY(BlueprintReadOnly) float RadiusY=120;
    UPROPERTY(BlueprintReadOnly) float Depth=0;
};

USTRUCT()
struct FMCTonguePressureFrame
{
    GENERATED_BODY()
    UPROPERTY() TArray<FMCTongueLoad> Sources;
    UPROPERTY() double UpdatedAt=0;
    UPROPERTY() int32 Epoch=0;
};
