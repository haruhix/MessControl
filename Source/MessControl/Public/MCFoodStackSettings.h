#pragma once
#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "MCFoodStackSettings.generated.h"

class UStaticMesh;

UENUM(BlueprintType)
enum class EMCFoodStackAxis : uint8 { Automatic, X, Y, Z };

/** The local normal of a flat face. Automatic uses the shortest scaled mesh axis. */
USTRUCT(BlueprintType)
struct FMCFoodStackPoseOverride
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TSoftObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) EMCFoodStackAxis VerticalAxis=EMCFoodStackAxis::Automatic;
};

/** Always enforces a flat side; saved under Stack in each existing breakfast menu row. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCFoodStackSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout",meta=(ClampMin="0",ClampMax="12")) float LayerGap=3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout",meta=(ClampMin="0",ClampMax="8")) float HorizontalOffset=2.5f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout",meta=(ClampMin="0",ClampMax="20")) float YawVariation=8;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sway",meta=(ClampMin="0",ClampMax="12")) float MaxSwayDegrees=8;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Horizontal pose") TArray<FMCFoodStackPoseOverride> PoseOverrides;
    FVector VerticalAxis(UStaticMesh* Mesh,FVector ScaledExtent) const;
    FQuat RestRotation(UStaticMesh* Mesh,FVector ScaledExtent,int32 Slot) const;
    FVector SlotOffset(FVector ScaledExtent,const FQuat& Rotation,int32 Slot) const;
    float SafeLayerGap() const;
    float SafeMaxSwayDegrees() const;
    static FVector RotatedExtent(FVector Extent,const FQuat& Rotation);
    void Sanitize();
#if WITH_EDITOR
    bool Validate(class FDataValidationContext& Context) const;
#endif
};
