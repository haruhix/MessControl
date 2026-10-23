#pragma once

#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"

/** Per-primitive data consumed by M_HeadCosmeticDeform. No per-item MID is needed.
 * Bone attachment supplies the pose; apply the body's shader stretch afterwards
 * in the body's mesh frame, rather than stretching around the accessory pivot.
 */
namespace MCCosmeticDeformation
{
inline void Update(UStaticMeshComponent* Part,const USkinnedMeshComponent* Body)
{
    if(!Part || !Body) return;
    float Stretch=0.f;
    if(const auto* Material=Body->GetMaterial(0))
        Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("BodyStretch")),Stretch);
    if(!FMath::IsFinite(Stretch)) Stretch=0.f;
    const FTransform& Frame=Body->GetComponentTransform();
    const FVector Origin=Frame.GetLocation(),Scale=Frame.GetScale3D();
    const FQuat Rotation=Frame.GetRotation();
    Part->SetCustomPrimitiveDataFloat(0,FMath::Clamp(Stretch,-.35f,.4f));
    Part->SetCustomPrimitiveDataVector4(1,FVector4(Origin,0));
    Part->SetCustomPrimitiveDataVector4(5,FVector4(Rotation.X,Rotation.Y,Rotation.Z,Rotation.W));
    Part->SetCustomPrimitiveDataVector4(9,FVector4(Scale,0));
}
}
