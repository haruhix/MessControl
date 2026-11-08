#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UProceduralMeshComponent;
DECLARE_LOG_CATEGORY_EXTERN(LogMCFoodCuts,Log,All);

namespace MCFoodCutGeometry
{
/** Clip with the stock triangle slicer, then cap independent valid closed contours. */
bool SliceAndCap(UProceduralMeshComponent* Mesh,FVector WorldPoint,FVector WorldNormal,UMaterialInterface* CapMaterial);
}
