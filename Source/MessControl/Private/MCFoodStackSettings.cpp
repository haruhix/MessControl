#include "MCFoodStackSettings.h"
#include "Engine/StaticMesh.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
float SafeStackValue(float Value,float Default,float Max)
{ return FMath::IsFinite(Value)?FMath::Clamp(Value,0.f,Max):Default; }
}
float FMCFoodStackSettings::SafeLayerGap() const {return SafeStackValue(LayerGap,3,12);}
float FMCFoodStackSettings::SafeMaxSwayDegrees() const {return SafeStackValue(MaxSwayDegrees,8,12);}
void FMCFoodStackSettings::Sanitize()
{
    LayerGap=SafeLayerGap();MaxSwayDegrees=SafeMaxSwayDegrees();
    HorizontalOffset=SafeStackValue(HorizontalOffset,2.5f,8);YawVariation=SafeStackValue(YawVariation,8,20);
}
FVector FMCFoodStackSettings::VerticalAxis(UStaticMesh* Mesh,FVector Extent) const
{
    for(const auto& Pose:PoseOverrides) if(Mesh && Pose.Mesh.Get()==Mesh) {
        if(Pose.VerticalAxis==EMCFoodStackAxis::X) return FVector::ForwardVector;
        if(Pose.VerticalAxis==EMCFoodStackAxis::Y) return FVector::RightVector;
        if(Pose.VerticalAxis==EMCFoodStackAxis::Z) return FVector::UpVector;
        break;
    }
    Extent=Extent.ContainsNaN()?FVector::OneVector:Extent.GetAbs();
    // Prefer Z for ties so round ingredients and cube fixtures keep their authored pose.
    if(Extent.Z<=FMath::Min(Extent.X,Extent.Y)+KINDA_SMALL_NUMBER) return FVector::UpVector;
    return Extent.X<=Extent.Y?FVector::ForwardVector:FVector::RightVector;
}
FQuat FMCFoodStackSettings::RestRotation(UStaticMesh* Mesh,FVector Extent,int32 Slot) const
{
    const FQuat Flat=FQuat::FindBetweenNormals(VerticalAxis(Mesh,Extent),FVector::UpVector);
    const float Yaw=Slot>0?FMath::Sin(Slot*2.399963f)*SafeStackValue(YawVariation,8,20):0;
    return (FRotator(0,Yaw,0).Quaternion()*Flat).GetNormalized();
}
FVector FMCFoodStackSettings::RotatedExtent(FVector Extent,const FQuat& Rotation)
{
    Extent=Extent.GetAbs();
    return Rotation.RotateVector(FVector(Extent.X,0,0)).GetAbs()
        +Rotation.RotateVector(FVector(0,Extent.Y,0)).GetAbs()
        +Rotation.RotateVector(FVector(0,0,Extent.Z)).GetAbs();
}
FVector FMCFoodStackSettings::SlotOffset(FVector Extent,const FQuat& Rotation,int32 Slot) const
{
    if(Slot<=0) return FVector::ZeroVector;
    const FVector Flat=RotatedExtent(Extent,Rotation);
    const float Radius=FMath::Min(SafeStackValue(HorizontalOffset,2.5f,8),float(FMath::Min(Flat.X,Flat.Y))*.18f);
    return FVector(FMath::Cos(Slot*2.399963f)*Radius,FMath::Sin(Slot*2.399963f)*Radius,0);
}
#if WITH_EDITOR
bool FMCFoodStackSettings::Validate(FDataValidationContext& Context) const
{
    bool Valid=true;
    auto Check=[&](float Value,float Max,const TCHAR* Name) {
        if(!FMath::IsFinite(Value) || Value<0 || Value>Max) {
            Context.AddError(FText::FromString(FString::Printf(TEXT("%s must be finite and between 0 and %.0f."),Name,Max)));Valid=false;
        }
    };
    Check(LayerGap,12,TEXT("LayerGap"));Check(HorizontalOffset,8,TEXT("HorizontalOffset"));
    Check(YawVariation,20,TEXT("YawVariation"));Check(MaxSwayDegrees,12,TEXT("MaxSwayDegrees"));
    TSet<FSoftObjectPath> Meshes;
    for(const auto& Pose:PoseOverrides) {
        if(Pose.Mesh.IsNull() || Meshes.Contains(Pose.Mesh.ToSoftObjectPath()) || uint8(Pose.VerticalAxis)>uint8(EMCFoodStackAxis::Z)) {
            Context.AddError(FText::FromString(TEXT("Horizontal pose overrides require a unique mesh and a valid local vertical axis.")));Valid=false;
        }
        Meshes.Add(Pose.Mesh.ToSoftObjectPath());
    }
    return Valid;
}
#endif
