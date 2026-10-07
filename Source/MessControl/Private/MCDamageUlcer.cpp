#include "MCMouthSurface.h"
#include "MCTongue.h"
#include "MCGameState.h"
#include "MCGameDirector.h"
#include "MCDayPlan.h"
#include "MCReactionVFX.h"
#include "EngineUtils.h"

AMCMouthSurface* AMCMouthSurface::SpawnDamageUlcer(UWorld* W,FVector P,int32 InBatch)
{
    if(!W || W->GetNetMode()==NM_Client) return nullptr;
    FHitResult Floor;AMCTongue* Tongue=nullptr;
    for(TActorIterator<AMCTongue> It(W);It;++It) if(It->SurfacePoint(P,Floor) && FMath::Abs(P.Z-Floor.ImpactPoint.Z)<180) {Tongue=*It;break;}
    if(!Tongue) return nullptr;
    int32 Count=0;
    for(TActorIterator<AMCMouthSurface> It(W);It;++It) if(It->bUlcer && !It->IsHealed()) {
        if(FVector::DistSquared(It->GetActorLocation(),Floor.ImpactPoint)<FMath::Square(85.f)) return *It;
        if(++Count>=24) return nullptr;
    }
    const FTransform T(FRotationMatrix::MakeFromZ(Floor.ImpactNormal).Rotator(),Floor.ImpactPoint+Floor.ImpactNormal*5);
    auto* Patch=W->SpawnActorDeferred<AMCMouthSurface>(StaticClass(),T);if(!Patch) return nullptr;
    Patch->bUlcer=true;Patch->bRandomizeLiquidSize=false;Patch->Batch=InBatch;
    const auto* GS=W->GetGameState<AMCGameState>();
    const auto* Director=AMCGameDirector::Find(W);
    const UMCDayPlan* Plan=Director && Director->IsManagingEvents()?Director->Mechanics.Get():GS?GS->DayPlan.Get():nullptr;
    if(Plan) {
        Patch->HealSeconds=Plan->UlcerHealSeconds;Patch->DamagePerSecond=Plan->UlcerDamagePerSecond;
        Patch->DisturbDamage=Plan->UlcerDisturbDamage;Patch->PulseInterval=Plan->UlcerPulseInterval;
    }
    Patch->FinishSpawning(T);Tongue->TriggerPain(Floor.ImpactPoint);
    AMCReactionVFX::Spawn(W,Floor.ImpactPoint+Floor.ImpactNormal*8,EMCReactionEffect::Impact,1,110);return Patch;
}
