#include "MCFoodActor.h"
#include "MCToothCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"

void AMCFoodActor::SetStackCarrier(AMCToothCharacter* Hero)
{
    if(!HasAuthority() || StackCarrier==Hero) return;
    if(auto* Previous=StackCarrier.Get()) {
        Body->IgnoreActorWhenMoving(Previous,false); Previous->GetCapsuleComponent()->IgnoreActorWhenMoving(this,false);
    }
    StackCarrier=Hero; CollisionIgnoredCarrier=Hero;
    if(Hero) {
        Body->IgnoreActorWhenMoving(Hero,true); Hero->GetCapsuleComponent()->IgnoreActorWhenMoving(this,true);
        if(Phase==EMCFoodPhase::Falling) Phase=EMCFoodPhase::Free;
    }
    OnRep_Phase();ForceNetUpdate();
}
void AMCFoodActor::ReactToImpact(float Strength)
{
    if(!HasAuthority() || bBrushTool || IsDisposed() || HazardNow()-ImpactAt<.12) return;
    ImpactAt=HazardNow();ImpactStrength=FMath::Clamp(Strength,.15f,1.f);ForceNetUpdate();
}
void AMCFoodActor::UpdateReaction(float Dt)
{
    if(bBrushTool || IsDisposed() || !ItemMesh) return;
    const float Age=FMath::Max(0.,HazardNow()-ImpactAt);
    // Hold the contact pose briefly so a slow frame cannot skip the entire squash.
    const float BounceAge=FMath::Max(0.f,Age-.12f);
    const float Kick=Age<.7f?ImpactStrength*FMath::Cos(BounceAge*18)*FMath::Exp(-BounceAge*8)*.12f:0;
    const float Speed=HasAuthority()?FMath::Abs(Body->GetPhysicsLinearVelocity().Z):FMath::Abs(GetReplicatedMovement().LinearVelocity.Z);
    const bool FreeFall=Phase==EMCFoodPhase::Falling || Phase==EMCFoodPhase::Free && !StackCarrier && Holders.IsEmpty();
    const float Stretch=FreeFall?FMath::Clamp(Speed/1800.f,0.f,.28f)*FMath::SmoothStep(.15f,.45f,Age):0;
    const float Z=FMath::Clamp(1+Stretch-Kick,.65f,1.4f),XY=1/FMath::Sqrt(Z);
    // Hazard animation runs first, so its fuse pulse also receives the shared deformation.
    const FVector BaseScale=FoodData.Kind==EMCFoodKind::Spicy?Visual->GetRelativeScale3D():bFragment?FoodData.FragmentScale:FoodData.Scale;
    const FVector Scale=BaseScale*FVector(XY,XY,Z);
    Visual->SetRelativeScale3D(Scale);Visual->SetRelativeLocation(-ItemMesh->GetBounds().Origin*Scale);
    if(GetNetMode()==NM_DedicatedServer) return;
    const float Red=Age<.6f?ImpactStrength*(1-FMath::SmoothStep(.08f,.6f,Age)):bSpoiled?.38f:0;
    if(Red>0 && !ReactionMaterial) {
        if(auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/VFX/M_FoodHit.M_FoodHit"))) ReactionMaterial=UMaterialInstanceDynamic::Create(Base,this);
    }
    if(ReactionMaterial) {ReactionMaterial->SetScalarParameterValue(TEXT("Opacity"),Red*.65f);ReactionMaterial->SetVectorParameterValue(TEXT("Color"),bSpoiled && Age>.6f?FLinearColor(.28f,.12f,.01f):FLinearColor(1,.012f,.005f));Visual->SetOverlayMaterial(Red>0?ReactionMaterial.Get():nullptr);}
}
