#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MCTongueMotion.h"
#include "MCTongueProfile.generated.h"

USTRUCT(BlueprintType)
struct FMCTongueSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Idle",meta=(ClampMin="0",ClampMax="8")) float IdleHeight=3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Idle",meta=(ClampMin="2")) float IdlePeriod=5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Jolt") bool bAutomaticJolts=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Jolt",meta=(ClampMin="8")) float JoltRestMin=22;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Jolt",meta=(ClampMin="8")) float JoltRestMax=32;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Jolt",meta=(ClampMin="0",ClampMax="220")) float JoltHeight=180;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Jolt",meta=(ClampMin="0.4")) float JoltAnticipation=.7f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Jolt",meta=(ClampMin="0.25")) float JoltRise=.35f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Jolt",meta=(ClampMin="0.7")) float JoltReturn=1.4f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Jolt",meta=(ClampMin="400",ClampMax="1200")) float JoltLift=1000;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Jolt",meta=(ClampMin="0",ClampMax="500")) float JoltPush=260;
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
    float JoltDuration() const { return JoltAnticipation+JoltRise+.15f+JoltReturn+.5f; }
    float JoltShape(float Age) const;
};

UCLASS(BlueprintType)
class MESSCONTROL_API UMCTongueProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tongue") FMCTongueSettings Settings;
    // Unassigned references retain the original Settings values for existing maps.
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Events") TObjectPtr<UMCTongueMotionProfile> PainMotion;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Events") TObjectPtr<UMCTongueMotionProfile> JoltMotion;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Events") TArray<TObjectPtr<UMCTongueMotionProfile>> DevMotions;
};
