#include "MCHazardWave.h"
#include "Components/BoxComponent.h"
#include "MCMouthSurface.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCArenaTooth.h"
#include "Components/CapsuleComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/GameStateBase.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

AMCHazardWave::AMCHazardWave()
{
    bReplicates=true; bAlwaysRelevant=true; PrimaryActorTick.bCanEverTick=true;
    Ring=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("PulseRing")); SetRootComponent(Ring);
    Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision); Ring->SetCastShadow(false);
}
double AMCHazardWave::Now() const
{ const auto* GS=GetWorld()->GetGameState(); return GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds(); }
void AMCHazardWave::BeginPlay()
{
    Super::BeginPlay();
    if(HasAuthority()) { StartedAt=Now(); ForceNetUpdate(); }
    if(GetNetMode()!=NM_DedicatedServer)
        if(auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/MI_ThroatRing.MI_ThroatRing")))
        { Material=UMaterialInstanceDynamic::Create(Base,this); Ring->SetMaterial(0,Material); }
}
float AMCHazardWave::Radius() const
{ return FMath::Clamp(float(Now()-StartedAt-WarningSeconds)/FMath::Max(.1f,TravelSeconds),0.f,1.f)*MaxRadius; }
bool AMCHazardWave::Crosses(float Before,float After,float R0,float R1,float Width)
{
    const float A=Before-R0,B=After-R1;
    return FMath::Min(A,B)<=Width && FMath::Max(A,B)>=-Width;
}
void AMCHazardWave::Tick(float Dt)
{
    Super::Tick(Dt);
    if(const auto* Ulcer=Cast<AMCMouthSurface>(Source); Ulcer && (Ulcer->IsNumb() || !Ulcer->bUlcer)) { if(HasAuthority()) Destroy(); return; }
    const float CurrentRadius=Radius();
    if(HasAuthority() && Now()-StartedAt>=WarningSeconds)
    {
        for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        {
            auto* Hero=*It;
            if(!Hero->Status->IsAlive() || Hero->SwallowedBy || HitActors.Contains(Hero)) continue;
            const FVector P=Hero->ToothPhysics->PhysicalLocation();
            const float Distance=FVector::Dist2D(P,GetActorLocation());
            const float Before=PreviousDistances.FindRef(Hero); const bool Had=PreviousDistances.Contains(Hero);
            PreviousDistances.Add(Hero,Distance);
            if(!Crosses(Had?Before:Distance,Distance,PreviousRadius,CurrentRadius,Hero->GetCapsuleComponent()->GetScaledCapsuleRadius()+12)) continue;
            FHitResult Floor; bool Found=false;
            for(TActorIterator<AMCTongue> T(GetWorld());T;++T) if(T->SurfacePoint(P,Floor)) { Found=true; break; }
            if(!Found) { FCollisionQueryParams Q(SCENE_QUERY_STAT(MCWaveFloor),false,Hero); Found=GetWorld()->LineTraceSingleByObjectType(Floor,P+FVector(0,0,100),P-FVector(0,0,1000),FCollisionObjectQueryParams(ECC_WorldStatic),Q); }
            const float FloorZ=Found?Floor.ImpactPoint.Z:GetActorLocation().Z;
            const float Sole=P.Z-Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            // Once crossed, a wave cannot damage a player who lands behind it.
            HitActors.Add(Hero);
            if(Sole-FloorZ>ClearHeight) continue;
            FVector Direction=(P-GetActorLocation()).GetSafeNormal2D();
            if(Direction.IsNearlyZero()) Direction=FVector::ForwardVector;
            Hero->Status->Damage(Damage,Direction); Hero->NotifyTaskFeedback(false); ++HitCount;
            Hero->ToothPhysics->ApplyHit(Direction*260+FVector(0,0,130),P);
        }
        if(bSpicy) for(TActorIterator<AMCArenaTooth> It(GetWorld());It;++It)
        {
            if(!It->IsAvailable() || HitActors.Contains(*It)) continue;
            const float D=FVector::Dist2D(It->Body->Bounds.Origin,GetActorLocation());
            if(D<=CurrentRadius+It->Body->Bounds.BoxExtent.Size2D()) { HitActors.Add(*It); It->Status->Damage(Damage,(*It)->GetActorLocation()-GetActorLocation()); }
        }
        PreviousRadius=CurrentRadius;
        if(Now()-StartedAt>WarningSeconds+TravelSeconds+.15f) { Destroy(); return; }
    }
    GeometryClock+=Dt;
    if(GetNetMode()!=NM_DedicatedServer && GeometryClock>=1.f/15) { GeometryClock=0; DrawRing(); }
}
void AMCHazardWave::DrawRing()
{
    const bool Warning=Now()-StartedAt<WarningSeconds;
    const float R=Warning?FMath::Max(25.f,MaxRadius*.15f):FMath::Max(10.f,Radius());
    TArray<FVector> V,N; TArray<FVector2D> UV; TArray<int32> Tri; TArray<FLinearColor> C; TArray<FProcMeshTangent> Tangents;
    AMCTongue* Tongue=nullptr;
    for(TActorIterator<AMCTongue> T(GetWorld());T;++T) { Tongue=*T; break; }
    constexpr int32 Segments=32;
    for(int32 I=0;I<=Segments;++I) for(int32 Edge=0;Edge<2;++Edge)
    {
        const float A=2*PI*I/Segments,RingRadius=R+(Edge?9:-9);
        FVector P=GetActorLocation()+FVector(FMath::Cos(A)*RingRadius,FMath::Sin(A)*RingRadius,8);
        if(Tongue) { FHitResult Hit; if(Tongue->SurfacePoint(P,Hit)) P=Hit.ImpactPoint+Hit.ImpactNormal*8; }
        V.Add(GetActorTransform().InverseTransformPosition(P)); N.Add(FVector::UpVector); UV.Add(FVector2D(float(I)/Segments,Edge)); C.Add(FLinearColor::White); Tangents.Add(FProcMeshTangent(1,0,0));
        if(I<Segments && Edge==0) { const int32 B=I*2; Tri.Append({B,B+2,B+1,B+1,B+2,B+3}); }
    }
    Ring->CreateMeshSection_LinearColor(0,V,Tri,N,UV,C,Tangents,false);
    if(Material) Material->SetVectorParameterValue(TEXT("ZoneColor"),FLinearColor(1,.015f,bSpicy?.005f:.12f)*(Warning?1.5f+.6f*FMath::Sin(Now()*12):2.f));
}
void AMCHazardWave::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCHazardWave,MaxRadius); DOREPLIFETIME(AMCHazardWave,Damage); DOREPLIFETIME(AMCHazardWave,WarningSeconds);
    DOREPLIFETIME(AMCHazardWave,TravelSeconds); DOREPLIFETIME(AMCHazardWave,ClearHeight); DOREPLIFETIME(AMCHazardWave,bSpicy);
    DOREPLIFETIME(AMCHazardWave,StartedAt); DOREPLIFETIME(AMCHazardWave,Source); DOREPLIFETIME(AMCHazardWave,HitCount);
}
