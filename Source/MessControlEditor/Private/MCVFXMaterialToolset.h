#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "MCVFXMaterialToolset.generated.h"

class UMaterial;
class UMaterialExpressionCustom;

/** Narrow authoring tools for Custom HLSL inputs in owned project materials. */
UCLASS()
class UMCVFXMaterialToolset final : public UToolsetDefinition
{
    GENERATED_BODY()

public:
    /** Read the exact Custom expression's ordered input names and connected expression outputs. */
    UFUNCTION(meta=(AICallable))
    static FString GetOwnedCustomMaterialInputs(UMaterial* Material, UMaterialExpressionCustom* Expression,
        FName OwnerMetadataKey, const FString& ExpectedOwnerValue);

    /** Replace the ordered list of Custom HLSL inputs. Retained names keep their wires and new inputs start empty.
     * Refuses removal of a connected input. Ordinary material refresh runs, but no package is saved. */
    UFUNCTION(meta=(AICallable))
    static FString SetOwnedCustomMaterialInputs(UMaterial* Material, UMaterialExpressionCustom* Expression,
        const TArray<FName>& InputNames, FName OwnerMetadataKey, const FString& ExpectedOwnerValue);
};
