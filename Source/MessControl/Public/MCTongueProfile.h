#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MCTongueProfile.generated.h"

USTRUCT(BlueprintType)
struct FMCTongueSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Idle",meta=(ClampMin="0",ClampMax="8")) float IdleHeight=3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Idle",meta=(ClampMin="2")) float IdlePeriod=5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pain",meta=(ClampMin="0",ClampMax="50")) float WaveHeight=28;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pain",meta=(ClampMin="100")) float WaveSpeed=850;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pain",meta=(ClampMin="100")) float WaveWidth=260;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pain",meta=(ClampMin="200")) float WaveRadius=2600;
    // A single pulse at a time; this is also a limit on repeated ulcer disturbance.
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pain",meta=(ClampMin="1")) float Cooldown=4;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pain",meta=(ClampMin="0",ClampMax="1000")) float PushSpeed=470;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pain",meta=(ClampMin="0",ClampMax="500")) float LiftSpeed=210;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pain",meta=(ClampMin="20",ClampMax="400")) float AffectHeight=170;
    void Sanitize();
    float Duration() const { return (WaveRadius+WaveWidth)/WaveSpeed; }
    float Band(float Distance,float Age) const;
    bool Crossed(float Distance,float PreviousAge,float Age) const;
};

UCLASS(BlueprintType)
class MESSCONTROL_API UMCTongueProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tongue") FMCTongueSettings Settings;
};
