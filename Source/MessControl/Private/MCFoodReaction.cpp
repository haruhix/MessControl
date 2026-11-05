#include "MCFoodActor.h"
#include "MCToothCharacter.h"
#include "MCPlayerState.h"
#include "MCFoodCollectionComponent.h"
#include "MCFoodStackSettings.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"

void AMCFoodActor::ProtectPlayersOnStackRelease(bool bThrown,float GraceSeconds)
{
    const float Duration=FMath::IsFinite(GraceSeconds)?FMath::Clamp(GraceSeconds,0.f,3.f):.75f;
    if(HasAuthority()) StackReleaseSafeUntil=bThrown?-100:GetWorld()->GetTimeSeconds()+Duration;
}
void AMCFoodActor::SetStackCarrier(AMCToothCharacter* Hero)
{
    if(!HasAuthority() || StackCarrier==Hero) return;
    if(auto* Previous=StackCarrier.Get()) {
        Body->IgnoreActorWhenMoving(Previous,false); Previous->GetCapsuleComponent()->IgnoreActorWhenMoving(this,false);
    }
    StackCarrier=Hero; CollisionIgnoredCarrier=Hero;
    if(!Hero) StackPickup.StartedAt=-100;
    if(Hero) {
        bManuallyThrown=false;
        LastHandledBy=Hero->GetPlayerState<AMCPlayerState>();
        Body->IgnoreActorWhenMoving(Hero,true); Hero->GetCapsuleComponent()->IgnoreActorWhenMoving(this,true);
        if(Phase==EMCFoodPhase::Falling) Phase=EMCFoodPhase::Free;
    }
    OnRep_Phase();ForceNetUpdate();
}
void AMCFoodActor::BeginStackPickup(AMCToothCharacter* Hero,float SlotHeight)
{
    if(!HasAuthority() || !Hero) return;
    StackPickup.StartLocation=GetActorLocation();StackPickup.StartRotation=GetActorRotation();
    StackPickup.StartedAt=HazardNow();StackPickup.SlotHeight=SlotHeight;
    const float Distance=FVector::Dist(GetActorLocation(),Hero->FoodCollection->StackPose(this).GetLocation());
    StackPickup.FlightSeconds=FMath::Clamp(.26f+Distance*.00045f,.28f,.36f);
    StackPickup.ArcHeight=FMath::Clamp(24.f+Distance*.24f,40.f,90.f);
    SetStackCarrier(Hero);
}
float AMCFoodActor::PrepareHorizontalStackPose(const FMCFoodStackSettings* StackSettings,int32 Slot)
{
    if(!StackSettings) StackSettings=&FoodData.Stack;
    const FVector Extent=Body->GetScaledBoxExtent();
    const FQuat Rotation=StackSettings->RestRotation(Visual->GetStaticMesh(),Extent,Slot);
    StackPickup.SlotRotation=Rotation.Rotator();
    StackPickup.SlotOffset=StackSettings->SlotOffset(Extent,Rotation,Slot);
    if(bCutVisualReady && CutStackLocalBounds.IsValid) {
        const FVector Center=Rotation.RotateVector(CutStackLocalBounds.GetCenter()*GetActorScale3D());
        StackPickup.SlotOffset=FVector(-Center.X,-Center.Y,0);
    }
    return StackHalfHeight();
}
FQuat AMCFoodActor::StackRestRotation(const FQuat& BaseRotation) const
{ return BaseRotation*StackPickup.SlotRotation.Quaternion(); }
float AMCFoodActor::StackHalfHeight() const
{ return float(FMCFoodStackSettings::RotatedExtent(Body->GetScaledBoxExtent(),StackPickup.SlotRotation.Quaternion()).Z); }
void AMCFoodActor::StackVerticalBounds(float& MinHeight,float& MaxHeight) const
{
    // Cut pieces retain the original actor origin and box; measure the visible
    // sliced mesh relative to that origin so asymmetric halves touch the stack.
    if(bCutVisualReady && CutStackLocalBounds.IsValid) {
        MinHeight=MAX_flt; MaxHeight=-MAX_flt;
        const FQuat Rotation=StackPickup.SlotRotation.Quaternion();
        const FVector Scale=GetActorScale3D();
        for(int32 Corner=0;Corner<8;++Corner) {
            const FVector P(Corner&1?CutStackLocalBounds.Max.X:CutStackLocalBounds.Min.X,
                Corner&2?CutStackLocalBounds.Max.Y:CutStackLocalBounds.Min.Y,
                Corner&4?CutStackLocalBounds.Max.Z:CutStackLocalBounds.Min.Z);
            const float Z=Rotation.RotateVector(P*Scale).Z;
            MinHeight=FMath::Min(MinHeight,Z); MaxHeight=FMath::Max(MaxHeight,Z);
        }
        return;
    }
    MaxHeight=StackHalfHeight(); MinHeight=-MaxHeight;
}
float AMCFoodActor::StackGapBefore(const AMCFoodActor* Above) const
{
    if(!Above || !(bCutVisualReady || Above->bCutVisualReady)) return FoodData.Stack.SafeLayerGap();
    float MinHeight,MaxHeight,AboveMin,AboveMax;
    StackVerticalBounds(MinHeight,MaxHeight);
    Above->StackVerticalBounds(AboveMin,AboveMax);
    // Slightly nest irregular surfaces so curved cut pieces do not appear to
    // hover above one another. Limit overlap for thin slices and small items.
    return -FMath::Min(6.f,FMath::Max(0.f,FMath::Min(MaxHeight-MinHeight,AboveMax-AboveMin)*.15f));
}
bool AMCFoodActor::IsStackPickupActive() const
{ return StackCarrier && StackPickup.StartedAt>=0 && HazardNow()-StackPickup.StartedAt<StackPickupDuration(); }
FTransform AMCFoodActor::StackPickupPose(const FTransform& Goal) const
{
    if(!IsStackPickupActive()) return Goal;
    const float Age=FMath::Max(0.,HazardNow()-StackPickup.StartedAt)*FMCStackPickup::PlayRate;
    if(Age<.075f) return FTransform(StackPickup.StartRotation,FVector(StackPickup.StartLocation));
    const float T=FMath::Clamp((Age-.075f)/StackPickup.FlightSeconds,0.f,1.f);
    // Pose-to-pose timing with zero endpoint velocity; the bowed path keeps the
    // jump readable without spinning away the food's recognisable silhouette.
    const float Ease=T*T*T*(T*(T*6-15)+10);
    FVector Location=FMath::Lerp(FVector(StackPickup.StartLocation),Goal.GetLocation(),Ease);
    Location.Z+=FMath::Square(FMath::Sin(PI*T))*StackPickup.ArcHeight;
    FQuat Rotation=FQuat::Slerp(StackPickup.StartRotation.Quaternion(),Goal.GetRotation(),Ease);
    Rotation=Rotation*FRotator(-FMath::Sin(PI*T)*10,0,FMath::Sin(2*PI*T)*4).Quaternion();
    if(T>=1) {
        const float Settle=Age-.075f-StackPickup.FlightSeconds;
        const float Envelope=FMath::Exp(-22*Settle)*(1-FMath::SmoothStep(.1f,.18f,Settle));
        Location.Z+=FMath::Sin(26*Settle)*8*Envelope;
        Rotation=Rotation*FRotator(FMath::Sin(30*Settle)*5*Envelope,0,0).Quaternion();
    }
    return FTransform(Rotation,Location);
}
void AMCFoodActor::ReactToImpact(float Strength)
{
    if(!HasAuthority() || bBrushTool || IsDisposed() || HazardNow()-ImpactAt<.12) return;
    ImpactAt=HazardNow();ImpactStrength=FMath::Clamp(Strength,.15f,1.f);ForceNetUpdate();
}
void AMCFoodActor::UpdateReaction(float Dt)
{
    if(bBrushTool || IsDisposed() || !ItemMesh || GetNetMode()==NM_DedicatedServer) return;
    const float Age=FMath::Max(0.,HazardNow()-ImpactAt);
    // Hold the contact pose briefly so a slow frame cannot skip the entire squash.
    const float BounceAge=FMath::Max(0.f,Age-.12f);
    const float Kick=Age<.7f?ImpactStrength*FMath::Cos(BounceAge*18)*FMath::Exp(-BounceAge*8)*.12f:0;
    const float Speed=HasAuthority()?FMath::Abs(Body->GetPhysicsLinearVelocity().Z):FMath::Abs(GetReplicatedMovement().LinearVelocity.Z);
    const bool FreeFall=Phase==EMCFoodPhase::Falling || Phase==EMCFoodPhase::Free && !StackCarrier && Holders.IsEmpty();
    const float Stretch=FreeFall?FMath::Clamp(Speed/1800.f,0.f,.28f)*FMath::SmoothStep(.15f,.45f,Age):0;
    float PickupZ=1,ContactWeight=0;
    if(IsStackPickupActive()) {
        const float PickupAge=FMath::Max(0.,HazardNow()-StackPickup.StartedAt)*FMCStackPickup::PlayRate;
        if(PickupAge<.075f) {
            PickupZ=1-.16f*FMath::Sin(PickupAge/.075f*PI*.5f);ContactWeight=1;
        } else {
            const float Flight=(PickupAge-.075f)/StackPickup.FlightSeconds;
            if(Flight<1) PickupZ=FMath::Lerp(.84f,1.18f,FMath::SmoothStep(0.f,.16f,Flight))-.18f*FMath::SmoothStep(.45f,1.f,Flight);
            else {
                const float Settle=PickupAge-.075f-StackPickup.FlightSeconds;
                PickupZ=1-.12f*FMath::Cos(22*Settle)*FMath::Exp(-18*Settle)*(1-FMath::SmoothStep(.1f,.18f,Settle));
                ContactWeight=1;
            }
        }
    }
    const float Z=FMath::Clamp((1+Stretch-Kick)*PickupZ,.65f,1.4f),XY=1/FMath::Sqrt(Z);
    // Hazard animation runs first, so its fuse pulse also receives the shared deformation.
    const FVector BaseScale=FoodData.Kind==EMCFoodKind::Spicy?Visual->GetRelativeScale3D():bFragment?FoodData.FragmentScale:FoodData.Scale;
    const FVector Scale=BaseScale*FVector(XY,XY,Z);
    if(!Visual->GetRelativeScale3D().Equals(Scale,.0001)) Visual->SetRelativeScale3D(Scale);
    // Preserve volume and the bottom contact during anticipation/landing. Only
    // the presentation deforms; the mesh collision and authored item size stay intact.
    const FVector Location=-ItemMesh->GetBounds().Origin*Scale+FVector(0,0,(Scale.Z-BaseScale.Z)*ItemMesh->GetBounds().BoxExtent.Z*ContactWeight);
    if(!Visual->GetRelativeLocation().Equals(Location,.0001)) Visual->SetRelativeLocation(Location);
    const float Red=Age<.6f?ImpactStrength*(1-FMath::SmoothStep(.08f,.6f,Age)):bSpoiled?.38f:0;
    if(Red>0 && !ReactionMaterial) {
        if(auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/VFX/M_FoodHit.M_FoodHit"))) ReactionMaterial=UMaterialInstanceDynamic::Create(Base,this);
    }
    if(ReactionMaterial) {ReactionMaterial->SetScalarParameterValue(TEXT("Opacity"),Red*.65f);ReactionMaterial->SetVectorParameterValue(TEXT("Color"),bSpoiled && Age>.6f?FLinearColor(.28f,.12f,.01f):FLinearColor(1,.012f,.005f));Visual->SetOverlayMaterial(Red>0?ReactionMaterial.Get():nullptr);}
}
