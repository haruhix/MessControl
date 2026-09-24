#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Sound/SoundBase.h"
#include "MCDataAssets.generated.h"

UENUM(BlueprintType)
enum class EMCTaskKind : uint8 { Coffee, Food, LooseTooth };

UENUM(BlueprintType)
enum class EMCShiftPhase : uint8 { Intermission, Working, Won, Lost };

USTRUCT(BlueprintType)
struct FMCAnimationSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation", meta=(ClampMin="0", ClampMax="0.6")) float Squash = 0.22f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation", meta=(ClampMin="0", ClampMax="0.6")) float Stretch = 0.18f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation", meta=(ClampMin="0", ClampMax="20")) float Bob = 7.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation", meta=(ClampMin="0", ClampMax="30")) float Lean = 14.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation", meta=(ClampMin="0", ClampMax="1")) float FollowThrough = 0.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation", meta=(ClampMin="0.5", ClampMax="2")) float Tempo = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation", meta=(ClampMin="0.05", ClampMax="0.4")) float Anticipation = 0.16f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation", meta=(ClampMin="0.5", ClampMax="2")) float Exaggeration = 1.f;
};

UCLASS(BlueprintType)
class MESSCONTROL_API UMCAnimationProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") FMCAnimationSettings Settings;
};

USTRUCT(BlueprintType)
struct FMCSoundVariation
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio") TArray<TObjectPtr<USoundBase>> Sounds;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio", meta=(ClampMin="0", ClampMax="2")) float Volume = 0.65f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio", meta=(ClampMin="0.5", ClampMax="2")) float PitchMin = 0.92f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio", meta=(ClampMin="0.5", ClampMax="2")) float PitchMax = 1.08f;
};

UCLASS(BlueprintType)
class MESSCONTROL_API UMCSoundPalette : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    // Step, Jump, Brush, Pull, Repair, Complete, DayStart, Win, Lose.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio") TMap<FName, FMCSoundVariation> Events;
    UFUNCTION(BlueprintCallable, Category="Audio") void Play(UObject* WorldContext, FName Event, FVector Location) const;
};

UCLASS(BlueprintType)
class MESSCONTROL_API UMCDayEvent : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Challenge") FText Title;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Challenge") FText Instruction;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Challenge") EMCTaskKind Kind = EMCTaskKind::Coffee;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Challenge", meta=(ClampMin="1", ClampMax="20")) int32 BaseTaskCount = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Challenge", meta=(ClampMin="10")) float Duration = 95.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Challenge", meta=(ClampMin="0.2")) float WorkSeconds = 3.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Challenge", meta=(ClampMin="1", ClampMax="100")) float MissedTaskDamage = 10.f;
};
