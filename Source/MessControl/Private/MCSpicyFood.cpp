#include "MCFoodActor.h"
#include "MCThroat.h"
#include "MCGameState.h"
#include "MCHazardWave.h"
#include "MCMouthSurface.h"
#include "MCFirePatch.h"
#include "MCTongue.h"
#include "EngineUtils.h"
#include "MCToothStatusComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"

double AMCFoodActor::HazardNow() const
{ const auto* GS=GetWorld()->GetGameState(); return GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds(); }
bool AMCFoodActor::IsWrongIngredient() const
{ return bBrushTool || bSpoiled || FoodData.Kind==EMCFoodKind::ForeignObject; }
float AMCFoodActor::FuseRemaining() const
{ return bFusePaused?PausedFuse:FuseEndsAt>0?FMath::Max(0.,FuseEndsAt-HazardNow()):FoodData.FuseSeconds; }
float AMCFoodActor::DetonationRadius() const
{ return FMath::Clamp(FoodData.FirstPulseRadius+FMath::Max(0,HazardRound-1)*FoodData.RadiusPerRound,50.f,900.f); }
void AMCFoodActor::ArmSpicy()
{
    if(!HasAuthority() || FoodData.Kind!=EMCFoodKind::Spicy || FuseEndsAt>0 || bFusePaused || IsDisposed()) return;
    if(const auto* GS=GetWorld()->GetGameState<AMCGameState>()) HazardRound=FMath::Max(1,GS->Day);
    // Eight seconds in the first round; later rounds reduce it to six.
    FuseEndsAt=HazardNow()+FMath::Clamp(FoodData.FuseSeconds-.5f*(HazardRound-1),6.f,8.f);
    ForceNetUpdate();
}
void AMCFoodActor::PauseFuse(AMCThroat* Throat)
{
    if(!HasAuthority() || FoodData.Kind!=EMCFoodKind::Spicy || !IsValid(Throat) || IsDisposed() || bFusePaused) return;
    ArmSpicy(); PausedFuse=FuseRemaining(); bFusePaused=true; FuseOwner=Throat; ForceNetUpdate();
}
void AMCFoodActor::ResumeFuse(AMCThroat* Throat)
{
    if(!HasAuthority() || !bFusePaused || FuseOwner.Get()!=Throat) return;
    FuseEndsAt=HazardNow()+PausedFuse; bFusePaused=false; FuseOwner.Reset(); ForceNetUpdate();
}
void AMCFoodActor::Detonate()
{
    if(!HasAuthority() || IsDisposed() || FoodData.Kind!=EMCFoodKind::Spicy || bFusePaused || Phase==EMCFoodPhase::Swallowing) return;
    FHitResult Floor; FCollisionQueryParams Q(SCENE_QUERY_STAT(MCPepperFloor),false,this);
    const FVector P=GetActorLocation();
    const bool Found=GetWorld()->LineTraceSingleByObjectType(Floor,P+FVector(0,0,80),P-FVector(0,0,1000),FCollisionObjectQueryParams(ECC_WorldStatic),Q);
    const FVector Impact=Found?Floor.ImpactPoint+Floor.ImpactNormal*5:P;
    // Legacy entry point now ignites a road on landing without an explosion.
    BurnLesion=AMCMouthSurface::SpawnDamageUlcer(GetWorld(),Impact,Batch);
    const FVector Motion=PrePhysicsVelocity.GetSafeNormal2D();
    const FVector Axis=Motion.IsNearlyZero()?GetActorForwardVector().GetSafeNormal2D():Motion;
    const FVector Side=FVector::CrossProduct(FVector::UpVector,Axis);
    for(int32 I=-3;I<=3;++I) {
        const FVector Candidate=Impact+Axis*(I*72)+Side*(FMath::Sin(I*1.4f)*22);
        FVector FloorPoint=Candidate;bool OnSurface=false;
        for(TActorIterator<AMCTongue> It(GetWorld());It;++It) {
            FHitResult H;if(It->SurfacePoint(Candidate,H) && FMath::Abs(H.ImpactPoint.Z-Candidate.Z)<180) {FloorPoint=H.ImpactPoint+H.ImpactNormal*5;OnSurface=true;break;}
        }
        if(!OnSurface) {
            FHitResult H;if(GetWorld()->LineTraceSingleByObjectType(H,Candidate+FVector(0,0,80),Candidate-FVector(0,0,250),FCollisionObjectQueryParams(ECC_WorldStatic),Q)) FloorPoint=H.ImpactPoint+H.ImpactNormal*5;
            else continue;
        }
        if(auto* Fire=AMCFirePatch::Ignite(this,FloorPoint,56,0,Batch,false)) {Fire->Lesion=BurnLesion;Fire->SetOwner(this);FireTrail.Add(Fire);Fire->ForceNetUpdate();}
    }
    Dispose();
}
bool AMCFoodActor::IsHazardResolved() const
{
    for(const auto& Fire:FireTrail) if(IsValid(Fire) && Fire->IsBurning()) return false;
    return !IsValid(BurnLesion) || BurnLesion->IsHealed() || BurnLesion->IsActorBeingDestroyed();
}
void AMCFoodActor::UpdateHazard(float Dt)
{
    if(FoodData.Kind!=EMCFoodKind::Spicy || IsDisposed()) return;
    if(HasAuthority())
    {
        if(bLandingPending && !bFusePaused && Phase!=EMCFoodPhase::Swallowing) {bLandingPending=false;Detonate();return;}
        if(bFusePaused && (!FuseOwner.IsValid() || (FuseOwner->ThroatPhase==EMCThroatPhase::Collecting && Phase!=EMCFoodPhase::Swallowing))) ResumeFuse(FuseOwner.Get());
    }
    if(GetNetMode()==NM_DedicatedServer) return;
    if(HazardMaterials.IsEmpty())
        if(auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/Hazards/M_SpicyPepper.M_SpicyPepper")))
            for(int32 I=0;I<Visual->GetNumMaterials();++I) { auto* MID=UMaterialInstanceDynamic::Create(Base,this); Visual->SetMaterial(I,MID); HazardMaterials.Add(MID); }
    const float Urgency=.35f;
    // The sine phase's derivative rises smoothly from one to five flashes per second.
    const float Age=FoodData.FuseSeconds-FuseRemaining();
    const float Flash=.1f;
    // Only the cosmetic mesh expands. Physics mass and the authoritative body
    // retain their food-table dimensions, including while carried or swallowed.
    const float Beat=0;
    const float Expansion=1+Beat*FMath::Lerp(.14f,.38f,Urgency);
    const FVector BaseScale=bFragment?FoodData.FragmentScale:FoodData.Scale;
    Visual->SetRelativeScale3D(BaseScale*Expansion);
    if(ItemMesh) Visual->SetRelativeLocation(-ItemMesh->GetBounds().Origin*BaseScale*Expansion);
    for(const auto& MID:HazardMaterials) if(MID) { MID->SetScalarParameterValue(TEXT("Urgency"),Urgency); MID->SetScalarParameterValue(TEXT("Flash"),Flash); }
}
