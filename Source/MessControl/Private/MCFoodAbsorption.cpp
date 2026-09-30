#include "MCFoodActor.h"
#include "MCTongue.h"
#include "MCMouthSurface.h"
#include "MCGameState.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "EngineUtils.h"

bool AMCFoodActor::FindAbsorptionFloor(FHitResult& Hit,AMCTongue*& Tongue) const
{
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) {
        if(!It->SurfacePoint(GetActorLocation(),Hit)) continue;
        const float Gap=GetActorLocation().Z-Body->Bounds.BoxExtent.Z-Hit.ImpactPoint.Z;
        if(Gap>=-25 && Gap<=18) { Tongue=*It; return true; }
    }
    return false;
}
float AMCFoodActor::AbsorptionProgress() const
{
    return Phase==EMCFoodPhase::Absorbing?FMath::Clamp(float(HazardNow()-AbsorbStartedAt)/FMath::Max(.5f,FoodData.AbsorbSeconds),0.f,1.f):0.f;
}
void AMCFoodActor::AttendFood()
{
    if(!HasAuthority() || IsDisposed() || bBrushTool) return;
    if(Phase==EMCFoodPhase::Absorbing) {
        FHitResult Hit;
        if(AbsorptionTongue && AbsorptionTongue->SurfacePoint(AbsorptionTongue->GetActorTransform().TransformPosition(AbsorptionAnchor),Hit))
            SetActorLocation(Hit.ImpactPoint+Hit.ImpactNormal*(Body->Bounds.BoxExtent.Z+2),false,nullptr,ETeleportType::TeleportPhysics);
        Phase=EMCFoodPhase::Free; OnRep_Item(); OnRep_Phase();
    }
    SpoilAt=HazardNow()+FoodData.SpoilSeconds; AbsorbStartedAt=0; AbsorptionTongue=nullptr; ForceNetUpdate();
}
void AMCFoodActor::UpdateAbsorption(float Dt)
{
    if(bBrushTool || ItemName.IsNone() || FoodData.Kind!=EMCFoodKind::Food || IsDisposed()) return;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    if(GS && (GS->bDayOneComplete || GS->Phase==EMCShiftPhase::Won || GS->Phase==EMCShiftPhase::Lost)) return;
    if(Phase==EMCFoodPhase::Absorbing) {
        FHitResult Hit;
        if(!AbsorptionTongue || !AbsorptionTongue->SurfacePoint(AbsorptionTongue->GetActorTransform().TransformPosition(AbsorptionAnchor),Hit)) {
            if(HasAuthority()) AttendFood(); return;
        }
        const float Progress=FMath::SmoothStep(0.f,1.f,AbsorptionProgress());
        const float Half=Body->Bounds.BoxExtent.Z;
        // Use the replicated anchor and server clock on proxies too; their usual
        // kinematic interpolation is skipped while absorption owns the pose.
        SetActorLocation(Hit.ImpactPoint+Hit.ImpactNormal*FMath::Lerp(Half+2,-Half-12,Progress),false,nullptr,ETeleportType::TeleportPhysics);
        if(HasAuthority() && Progress>=1) { FinishAbsorption(); return; }
        Visual->SetRelativeScale3D((bFragment?FoodData.FragmentScale:FoodData.Scale)*(1-.20f*Progress));
        return;
    }
    if(!HasAuthority()) {
        const FVector Scale=bFragment?FoodData.FragmentScale:FoodData.Scale;
        if(ItemMesh && !Visual->GetRelativeScale3D().Equals(Scale)) OnRep_Item();
        return;
    }
    if(Phase==EMCFoodPhase::Carried || Phase==EMCFoodPhase::Swallowing || Phase==EMCFoodPhase::Falling || !Holders.IsEmpty()) {
        SpoilAt=HazardNow()+FoodData.SpoilSeconds; return;
    }
    FHitResult Hit; AMCTongue* Floor=nullptr;
    if(!FindAbsorptionFloor(Hit,Floor)) { SpoilAt=HazardNow()+FoodData.SpoilSeconds; return; }
    if(SpoilAt<=0) SpoilAt=HazardNow()+FoodData.SpoilSeconds;
    if(HazardNow()<SpoilAt) return;
    int32 Count=0; for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if(It->bUlcer) ++Count;
    for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if(It->Phase==EMCFoodPhase::Absorbing) ++Count;
    if(Count>=24) return;
    AbsorptionTongue=Floor; AbsorptionAnchor=Floor->GetActorTransform().InverseTransformPosition(Hit.ImpactPoint);
    AbsorbStartedAt=HazardNow(); Phase=EMCFoodPhase::Absorbing; Spoil(); OnRep_Phase(); ForceNetUpdate();
}
void AMCFoodActor::FinishAbsorption()
{
    if(!HasAuthority() || Phase!=EMCFoodPhase::Absorbing || !AbsorptionTongue) return;
    FHitResult Hit;
    if(!AbsorptionTongue->SurfacePoint(AbsorptionTongue->GetActorTransform().TransformPosition(AbsorptionAnchor),Hit)) { AttendFood(); return; }
    const FTransform T(FRotationMatrix::MakeFromZ(Hit.ImpactNormal).Rotator(),Hit.ImpactPoint+Hit.ImpactNormal*5);
    auto* Patch=GetWorld()->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),T);
    if(!Patch) { AttendFood(); return; }
    Patch->bUlcer=true; Patch->Batch=Batch; Patch->bRandomizeLiquidSize=false;
    if(const auto* GS=GetWorld()->GetGameState<AMCGameState>(); GS && GS->DayPlan) {
        Patch->HealSeconds=FMath::Clamp(GS->DayPlan->UlcerHealSeconds,6.f,8.f);
        Patch->DamagePerSecond=GS->DayPlan->UlcerDamagePerSecond; Patch->DisturbDamage=GS->DayPlan->UlcerDisturbDamage;
        Patch->PulseInterval=GS->DayPlan->UlcerPulseInterval;
    }
    Patch->FinishSpawning(T); AbsorbedUlcer=Patch; bAbsorbed=true;
    Dispose(); SetLifeSpan(0); // Retain the hidden objective until its ulcer is cured.
    ForceNetUpdate();
}
