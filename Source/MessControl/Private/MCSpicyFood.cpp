#include "MCFoodActor.h"
#include "MCThroat.h"
#include "MCGameState.h"
#include "MCHazardWave.h"
#include "MCMouthSurface.h"
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
    const FTransform T(FRotator::ZeroRotator,Impact);
    auto* Wave=GetWorld()->SpawnActorDeferred<AMCHazardWave>(AMCHazardWave::StaticClass(),T);
    if(Wave) { Wave->MaxRadius=DetonationRadius(); Wave->Damage=FoodData.PulseDamage; Wave->bSpicy=true; Wave->WarningSeconds=.15f; Wave->FinishSpawning(T); }
    auto* Patch=GetWorld()->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),T);
    if(Patch) { Patch->bRandomizeLiquidSize=false; Patch->LiquidHalfSize=FMath::Clamp(DetonationRadius()*.35f,40.f,180.f); Patch->Batch=Batch; Patch->FinishSpawning(T); Patch->Status->ApplyCoffee(.55f); }
    Dispose();
}
void AMCFoodActor::UpdateHazard(float Dt)
{
    if(FoodData.Kind!=EMCFoodKind::Spicy || IsDisposed()) return;
    if(HasAuthority())
    {
        if(bLandingPending || Phase==EMCFoodPhase::Free || Phase==EMCFoodPhase::Stuck || Phase==EMCFoodPhase::Carried) ArmSpicy();
        if(bFusePaused && (!FuseOwner.IsValid() || (FuseOwner->ThroatPhase==EMCThroatPhase::Collecting && Phase!=EMCFoodPhase::Swallowing))) ResumeFuse(FuseOwner.Get());
        if(FuseEndsAt>0 && !bFusePaused && FuseRemaining()<=0) { Detonate(); return; }
    }
    if(GetNetMode()==NM_DedicatedServer) return;
    if(HazardMaterials.IsEmpty())
        if(auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/Hazards/M_SpicyPepper.M_SpicyPepper")))
            for(int32 I=0;I<Visual->GetNumMaterials();++I) { auto* MID=UMaterialInstanceDynamic::Create(Base,this); Visual->SetMaterial(I,MID); HazardMaterials.Add(MID); }
    const float Urgency=1-FMath::Clamp(FuseRemaining()/FMath::Max(1.f,FoodData.FuseSeconds),0.f,1.f);
    // The sine phase's derivative rises smoothly from one to five flashes per second.
    const float Age=FoodData.FuseSeconds-FuseRemaining();
    const float Flash=bFusePaused?.15f:.5f+.5f*FMath::Sin(2*PI*(Age+2*Age*Age/FMath::Max(1.f,FoodData.FuseSeconds)));
    // Only the cosmetic mesh expands. Physics mass and the authoritative body
    // retain their food-table dimensions, including while carried or swallowed.
    const float Beat=bFusePaused?0.f:FMath::Pow(Flash,3.f);
    const float Expansion=1+Beat*FMath::Lerp(.14f,.38f,Urgency);
    const FVector BaseScale=bFragment?FoodData.FragmentScale:FoodData.Scale;
    Visual->SetRelativeScale3D(BaseScale*Expansion);
    if(ItemMesh) Visual->SetRelativeLocation(-ItemMesh->GetBounds().Origin*BaseScale*Expansion);
    for(const auto& MID:HazardMaterials) if(MID) { MID->SetScalarParameterValue(TEXT("Urgency"),Urgency); MID->SetScalarParameterValue(TEXT("Flash"),Flash); }
}
