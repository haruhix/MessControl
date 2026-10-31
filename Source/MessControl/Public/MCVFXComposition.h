#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MCVFXComposition.generated.h"

class UNiagaraSystem;

/** Presentation binding only: collision, damage and surface sampling belong to the calling action. */
UENUM(BlueprintType)
enum class EMCVFXCompositionBinding : uint8
{
    Composition UMETA(DisplayName="Composition anchor"),
    World UMETA(DisplayName="World (leave particles at launch)"),
    Subject UMETA(DisplayName="Subject component / socket")
};

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCVFXFloatOverride
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bOverride=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="bOverride")) float Value=0;
};

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCVFXColorOverride
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bOverride=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="bOverride")) FLinearColor Value=FLinearColor::White;
};

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCVFXVectorOverride
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bOverride=false;
    /** Niagara Vector3, not Niagara Position. Large-world position inputs are deliberately separate. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="bOverride")) FVector Value=FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCVFXBoolOverride
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bOverride=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="bOverride")) bool Value=false;
};

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCVFXCompositionLayer
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layer") FName Name=TEXT("Layer");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layer") bool bEnabled=true;
    /** Audition only: the game player ignores Solo unless explicitly requested by the editor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layer") bool bSolo=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layer") TSoftObjectPtr<UNiagaraSystem> System;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Placement") EMCVFXCompositionBinding Binding=EMCVFXCompositionBinding::Composition;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Placement") FTransform Transform=FTransform::Identity;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Placement", meta=(EditCondition="Binding == EMCVFXCompositionBinding::Subject")) FName Socket;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timing", meta=(ClampMin="0",ClampMax="30",Units="s")) float StartSeconds=0;
    /** Zero emits until the composition ends; this does not change particle lifetime inside Niagara. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timing", meta=(ClampMin="0",ClampMax="30",Units="s")) float EmissionSeconds=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="User Parameters") TArray<FMCVFXFloatOverride> Floats;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="User Parameters") TArray<FMCVFXColorOverride> Colors;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="User Parameters") TArray<FMCVFXVectorOverride> Vectors;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="User Parameters") TArray<FMCVFXBoolOverride> Bools;
};

/** A composition of independent editable Niagara Systems. It is not a merged Niagara graph. */
UCLASS(BlueprintType)
class MESSCONTROL_API UMCVFXComposition : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Composition", meta=(ClampMin="0.05",ClampMax="30",Units="s")) float DurationSeconds=3;
    /** Graceful particles may finish here. The bounded tail also cleans up systems which ignore Deactivate. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Composition", meta=(ClampMin="0",ClampMax="10",Units="s")) float TailSeconds=3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Composition", meta=(TitleProperty="Name")) TArray<FMCVFXCompositionLayer> Layers;

    /** Pure structure/value validation; never loads or modifies source Niagara assets. */
    bool Validate(FString& OutError) const;
    /** Explicit authoring operation. Reads User defaults; retains authored matching overrides. */
    bool DiscoverParameters(int32 LayerIndex,FString& OutError);
    static bool ValidateParameters(const FMCVFXCompositionLayer& Layer,const UNiagaraSystem* System,FString& OutError);
};
