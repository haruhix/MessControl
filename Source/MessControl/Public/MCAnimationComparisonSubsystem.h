#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MCAnimationComparisonSubsystem.generated.h"

UENUM(BlueprintType)
enum class EMCSecondaryAnimationMode : uint8
{
    Native,
    StylizedDynamics,
    KawaiiPhysics
};

class USkeletalMeshComponent;

USTRUCT(BlueprintType)
struct FMCAnimationComparisonSample
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) FString MeshName;
    UPROPERTY(BlueprintReadOnly) EMCSecondaryAnimationMode Mode=EMCSecondaryAnimationMode::Native;
    UPROPERTY(BlueprintReadOnly) int32 ActiveBones=0;
    UPROPERTY(BlueprintReadOnly) int64 Evaluations=0;
    UPROPERTY(BlueprintReadOnly) float MaxAngleDegrees=0;
    UPROPERTY(BlueprintReadOnly) float MaxTranslation=0;
};

/** Local visual comparison. Never changes authoritative movement, damage, or physics. */
UCLASS()
class MESSCONTROL_API UMCAnimationComparisonSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure,Category="Animation comparison") EMCSecondaryAnimationMode GetMode() const;
    UFUNCTION(BlueprintCallable,Category="Animation comparison") void SetMode(EMCSecondaryAnimationMode Mode);
    UFUNCTION(BlueprintPure,Category="Animation comparison") bool IsModeAvailable(EMCSecondaryAnimationMode Mode) const;
    UFUNCTION(BlueprintPure,Category="Animation comparison") TArray<FMCAnimationComparisonSample> GetSamples() const;
    UFUNCTION(BlueprintPure,Category="Animation comparison") FString GetStatusText() const;

    void RecordSample(USkeletalMeshComponent* Mesh,EMCSecondaryAnimationMode Mode,int32 Bones,int64 Evaluations,float MaxAngle,float MaxTranslation);
private:
    struct FEntry
    {
        FMCAnimationComparisonSample Sample;
        double LastUpdate=0;
    };
    TMap<TWeakObjectPtr<USkeletalMeshComponent>,FEntry> Samples;
    double NextPruneAt=0;
};
