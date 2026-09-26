#pragma once
#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "MCTonguePressure.generated.h"

/** Small, bounded dents from supported loads, independent of event motion. */
USTRUCT(BlueprintType)
struct FMCTonguePressureSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pressure") bool bEnabled=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pressure",meta=(ClampMin="0",ClampMax="3")) float DepthPerKg=.9f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pressure",meta=(ClampMin="1",ClampMax="35")) float MaxDepth=24;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Footprint",meta=(ClampMin="60",ClampMax="250")) float PlayerRadius=120;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Footprint",meta=(ClampMin="60",ClampMax="300")) float RagdollRadius=150;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Footprint",meta=(ClampMin="30",ClampMax="120")) float FoodMargin=70;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing",meta=(ClampMin="0.06",ClampMax="1")) float PressSeconds=.12f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing",meta=(ClampMin="0.15",ClampMax="3")) float RecoverSeconds=.6f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Landing",meta=(ClampMin="0",ClampMax="2")) float LandingBoost=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Landing",meta=(ClampMin="100",ClampMax="1200")) float LandingSpeed=650;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Contact",meta=(ClampMin="3",ClampMax="20")) float ContactTolerance=12;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Budget",meta=(ClampMin="4",ClampMax="32")) int32 MaxSources=24;
    void Sanitize();
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
