#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MCFoodCollisionData.generated.h"

class UBodySetup;
class UStaticMesh;

/** Editor bake limits; runtime consumes the saved hulls without decomposition. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCFoodCollisionSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Collision")
    bool bAutoOptimize=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Collision", meta=(ClampMin="1", ClampMax="32", ToolTip="Maximum convex shapes per whole food. Bake happens in the editor, never during a food drop."))
    int32 WholeHullLimit=12;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Collision", meta=(ClampMin="1", ClampMax="16"))
    int32 FragmentHullLimit=4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Collision", meta=(ClampMin="8", ClampMax="256", ToolTip="Maximum vertices per convex shape. A higher budget preserves smooth, already inexpensive single-hull food such as the whole egg."))
    int32 HullVertexLimit=32;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Collision", meta=(ClampMin="10000", ClampMax="1000000", ToolTip="Editor decomposition precision. Higher values increase bake time; generated shapes always respect hull and vertex limits."))
    int32 VoxelResolution=100000;

    void Sanitize();
};

/** Generated collision only. Render geometry and detailed grip queries stay on SourceMesh. */
UCLASS(BlueprintType)
class MESSCONTROL_API UMCFoodCollisionData : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, Category="Source") TSoftObjectPtr<UStaticMesh> SourceMesh;
    UPROPERTY(VisibleAnywhere, Category="Source") FString SourceGeometryKey;
    UPROPERTY(VisibleAnywhere, Category="Budget") int32 HullLimit=0;
    UPROPERTY(VisibleAnywhere, Category="Budget") int32 HullVertexLimit=0;
    UPROPERTY(VisibleAnywhere, Category="Budget") int32 VoxelResolution=0;
    UPROPERTY(VisibleAnywhere, Instanced, Category="Collision") TObjectPtr<UBodySetup> BodySetup;

    bool MatchesSource(const UStaticMesh* Mesh) const;
    bool HasValidCollision() const;
};
