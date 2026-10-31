#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MCVFXComposition.h"
#include "MCPerkVFXProfile.generated.h"

class UNiagaraSystem;

UENUM(BlueprintType)
enum class EMCPerkVFXSlotMode : uint8
{
    UseDefault UMETA(DisplayName="Use action default"),
    Replace UMETA(DisplayName="Use this Niagara System"),
    Disabled UMETA(DisplayName="Disabled"),
    Composition UMETA(DisplayName="Independent Niagara composition")
};

/** A cosmetic slot. Offset is local to the action's actual event frame, never a gameplay radius. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCPerkVFXSlot
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Slot") EMCPerkVFXSlotMode Mode=EMCPerkVFXSlotMode::UseDefault;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Slot",meta=(EditCondition="Mode == EMCPerkVFXSlotMode::Replace")) TSoftObjectPtr<UNiagaraSystem> System;
    /** Native runtime asset, never an editor-only recipe preset. Each layer retains its original Niagara System. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Slot",meta=(EditCondition="Mode == EMCPerkVFXSlotMode::Composition")) TSoftObjectPtr<UMCVFXComposition> Composition;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Placement",meta=(EditCondition="Mode == EMCPerkVFXSlotMode::Replace || Mode == EMCPerkVFXSlotMode::Composition")) FTransform Transform=FTransform::Identity;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="User Parameters") TArray<FMCVFXFloatOverride> Floats;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="User Parameters") TArray<FMCVFXColorOverride> Colors;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="User Parameters") TArray<FMCVFXVectorOverride> Vectors;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="User Parameters") TArray<FMCVFXBoolOverride> Bools;
};

/** Replicated per-action snapshot: arrays are supported by replication, unlike authored TMaps. */
USTRUCT()
struct MESSCONTROL_API FMCPerkVFXBinding
{
    GENERATED_BODY()
    UPROPERTY() FName Stage;
    UPROPERTY() FMCPerkVFXSlot Slot;
};

/** Named presentation slots used by the actual action and its sandbox preview. */
UCLASS(BlueprintType)
class MESSCONTROL_API UMCPerkVFXProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    /** Currently supported action events: Start, Loop, Impact, End and Warning. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Perk VFX") TMap<FName,FMCPerkVFXSlot> Slots;
    bool Validate(FString& OutError) const;
    bool DiscoverParameters(FName Stage,FString& OutError);
    static bool IsSupportedStage(FName Stage);
    static TArray<FName> GetSupportedStages();
    static bool ValidateSlot(const FMCPerkVFXSlot& Slot,FString& OutError);
    static FMCVFXCompositionLayer AsLayer(const FMCPerkVFXSlot& Slot);
};
