#pragma once

#include "CoreMinimal.h"
#include "MCVFXComposition.h"
#include "MCVFXRecipePreset.generated.h"

/** Editor graph identity is separate from a layer's display name and survives cloning/reordering. */
USTRUCT()
struct FMCVFXRecipeNodeLayout
{
    GENERATED_BODY()
    UPROPERTY() FGuid ID;
    UPROPERTY() FVector2D Position=FVector2D::ZeroVector;
    UPROPERTY() FGuid Group;
};

USTRUCT()
struct FMCVFXRecipeGroup
{
    GENERATED_BODY()
    UPROPERTY() FGuid ID;
    UPROPERTY() FString Label=TEXT("Группа");
    UPROPERTY() FLinearColor Color=FLinearColor(.12f,.30f,.31f);
};

/** Authoring master only. Runtime uses the inherited independent-NS recipe or a separately exported NS. */
UCLASS()
class MESSCONTROLEDITOR_API UMCVFXRecipePreset : public UMCVFXComposition
{
    GENERATED_BODY()
public:
    UPROPERTY() FString PresetName=TEXT("Новая композиция");
    /** Parallel to Layers. Every mutation in the editor updates both arrays atomically. */
    UPROPERTY() TArray<FMCVFXRecipeNodeLayout> Nodes;
    UPROPERTY() TArray<FMCVFXRecipeGroup> Groups;
    bool ValidateLayout(FString& Error) const;
    int32 FindNode(const FGuid& ID) const;
};
