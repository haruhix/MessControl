#pragma once
#include "CoreMinimal.h"
class USkeletalMesh;
class UStaticMeshComponent;
class USkinnedMeshComponent;

/** Shared fitting presentation. No ownership, player, scene or commerce operations. */
namespace MCCosmeticFit
{
    /** Returns attach-local transform using the exact existing reference-pose chain order.
     * A missing reference bone keeps the authored transform, matching the existing socket path.
     * Callers validate actual attach socket support separately. */
    MESSCONTROL_API FTransform ResolveAttachmentTransform(const USkeletalMesh* BodyMesh,FName AttachBone,const FTransform& AuthoredTransform,bool bMeshSpaceFit);
    /** Applies existing body shader stretch/frame primitive data; no extra material/MID is allocated. */
    MESSCONTROL_API void UpdateDeformation(UStaticMeshComponent* Part,const USkinnedMeshComponent* Body);
}
