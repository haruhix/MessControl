#pragma once

#include "CoreMinimal.h"
#include "MCVFXCapturedMesh.generated.h"

class UStaticMesh;

/** Immutable visible mesh and actual captured contact pose. The historical
 * reflected name is retained so existing assets/cues do not require migration.
 * Presentation must never replace this with the owner's currently held tool.
 */
USTRUCT()
struct MESSCONTROL_API FMCEchoToolPart
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(VisibleAnywhere) FTransform ContactPose=FTransform::Identity;
};
