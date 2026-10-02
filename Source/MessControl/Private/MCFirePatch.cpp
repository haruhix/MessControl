#include "MCFirePatch.h"
#include "MCMouthSurface.h"
#include "MCReactionVFX.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

AMCFirePatch::AMCFirePatch()
{bReplicates=true;bAlwaysRelevant=true;PrimaryActorTick.bCanEverTick=true;SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("FireOrigin")));}
AMCFirePatch* AMCFirePatch::Ignite(UObject* Context,FVector Point,float Radius,float Seconds,int32 InBatch,bool CreateLesion)
{
    UWorld* W=Context?Context->GetWorld():nullptr;if(!W || W->GetNetMode()==NM_Client) return nullptr;
    const FTransform T(Point);auto* Fire=W->SpawnActorDeferred<AMCFirePatch>(StaticClass(),T);
    if(Fire) {Fire->BurnRadius=FMath::Clamp(Radius,30.f,250.f);Fire->BurnSeconds=Seconds>0?FMath::Clamp(Seconds,1.f,60.f):0;Fire->Batch=InBatch;Fire->bCreateLesion=CreateLesion;Fire->FinishSpawning(T);}return Fire;
}
void AMCFirePatch::BeginPlay()
{
    Super::BeginPlay();if(!HasAuthority()) return;StartedAt=GetWorld()->GetTimeSeconds();if(BurnSeconds>0) SetLifeSpan(BurnSeconds);
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) {
        FHitResult Hit;if(It->SurfacePoint(GetActorLocation(),Hit) && FMath::Abs(GetActorLocation().Z-Hit.ImpactPoint.Z)<150) {
            Tongue=*It;SurfaceAnchor=It->GetActorTransform().InverseTransformPosition(Hit.ImpactPoint);break;
        }
    }
    if(bCreateLesion) Lesion=AMCMouthSurface::SpawnDamageUlcer(GetWorld(),GetActorLocation(),Batch);
    Flames=AMCReactionVFX::Spawn(GetWorld(),GetActorLocation(),EMCReactionEffect::Fire,BurnSeconds,BurnRadius);
    if(Flames) Flames->AttachToActor(this,FAttachmentTransformRules::KeepWorldTransform);
    ForceNetUpdate();
}
bool AMCFirePatch::Extinguish(AMCToothCharacter* Worker,float Dt)
{
    if(!HasAuthority() || !IsBurning() || !IsValid(Worker) || !Worker->CanWork() || !FMath::IsFinite(Dt) || Dt<=0) return false;
    if(LastTreatmentFrame==GFrameCounter) return true;
    LastTreatmentFrame=GFrameCounter;
    if(IsValid(Lesion)) Lesion->ApplyAnesthetic(.3f);
    Heat=FMath::Max(0.f,Heat-FMath::Min(Dt,.2f)/FMath::Max(.1f,ExtinguishSeconds));
    if(Heat<.0001f) {
        Heat=0;SetLifeSpan(.7f);
        AMCReactionVFX::Spawn(GetWorld(),GetActorLocation(),EMCReactionEffect::Steam,1.2f,BurnRadius);
        bool OtherFire=false;
        for(TActorIterator<AMCFirePatch> It(GetWorld());It;++It) if(*It!=this && It->IsBurning() && (Lesion?It->Lesion==Lesion:It->Batch==Batch)) {OtherFire=true;break;}
        if(!OtherFire && !IsValid(Lesion)) Worker->NotifyTaskFeedback(true,GetActorLocation());
    }
    ForceNetUpdate();return true;
}
void AMCFirePatch::EndPlay(const EEndPlayReason::Type Reason)
{if(HasAuthority() && IsValid(Flames)) Flames->Destroy();Super::EndPlay(Reason);}
void AMCFirePatch::Tick(float Dt)
{
    Super::Tick(Dt);
    if(Tongue) {FHitResult Hit;if(Tongue->SurfacePoint(Tongue->GetActorTransform().TransformPosition(SurfaceAnchor),Hit)) SetActorLocation(Hit.ImpactPoint+Hit.ImpactNormal*5);}
    if(!HasAuthority() || !IsBurning() || GetWorld()->GetTimeSeconds()<NextDamageAt) return;NextDamageAt=GetWorld()->GetTimeSeconds()+.5;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(It->Status->IsAlive()
        && FVector::DistSquared2D(It->GetActorLocation(),GetActorLocation())<FMath::Square(BurnRadius)
        && FMath::Abs(It->GetActorLocation().Z-GetActorLocation().Z)<120) It->Status->Damage(3,(It->GetActorLocation()-GetActorLocation()).GetSafeNormal2D());
}
void AMCFirePatch::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AMCFirePatch,BurnRadius);DOREPLIFETIME(AMCFirePatch,BurnSeconds);DOREPLIFETIME(AMCFirePatch,Batch);DOREPLIFETIME(AMCFirePatch,StartedAt);DOREPLIFETIME(AMCFirePatch,Tongue);DOREPLIFETIME(AMCFirePatch,SurfaceAnchor);DOREPLIFETIME(AMCFirePatch,Lesion);DOREPLIFETIME(AMCFirePatch,Heat);DOREPLIFETIME(AMCFirePatch,ExtinguishSeconds);DOREPLIFETIME(AMCFirePatch,bCreateLesion);}
