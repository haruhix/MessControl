#include "MCVomitBurst.h"
#include "MCThroat.h"
#include "MCTongue.h"
#include "MCMouthSurface.h"
#include "MCToothStatusComponent.h"
#include "Components/SceneComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AMCVomitBurst::AMCVomitBurst()
{
    bReplicates=true; bAlwaysRelevant=true; SetReplicateMovement(false); SetNetUpdateFrequency(20);
    PrimaryActorTick.bCanEverTick=true;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Stream=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ViscousStream")); Stream->SetupAttachment(RootComponent);
    Droplets=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("GooAndSpray")); Droplets->SetupAttachment(RootComponent);
    for(auto* C:{static_cast<UPrimitiveComponent*>(Stream),static_cast<UPrimitiveComponent*>(Droplets)}) {
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetCanEverAffectNavigation(false); C->SetCastShadow(false);
    }
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Droplets->SetStaticMesh(Sphere.Object);
    auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/Hazards/M_VomitMass.M_VomitMass"));
    Stream->SetMaterial(0,Material); Droplets->SetMaterial(0,Material);
    Tags.Add(TEXT("DayOne"));
}
double AMCVomitBurst::ServerNow() const
{
    const auto* GS=GetWorld()->GetGameState(); return GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
}
FVector AMCVomitBurst::FlightPoint(const FMCVomitPortion& P,float T)
{
    return P.Start+P.Velocity*T-FVector(0,0,490*T*T);
}
void AMCVomitBurst::Configure(AMCThroat* Throat)
{
    if(!HasAuthority() || !IsValid(Throat)) return;
    SetOwner(Throat); StartedAt=ServerNow(); Batch=10000+Throat->MealSequence; Portions.Reset(); CheckedAge=0;
    for(int32 I=0;I<3;++I) {
        FHitResult Floor; bool Found=false;
        const FVector Target=Throat->GetActorTransform().TransformPosition(Throat->ZoneCenter+FVector(-Throat->ZoneRadius-270-I*205,(I%2?1:-1)*(95+I*28),0));
        for(TActorIterator<AMCTongue> T(GetWorld());T;++T) if(T->SurfacePoint(Target,Floor)) { Found=true; break; }
        if(!Found) continue;
        FMCVomitPortion P;
        P.Start=Throat->GetActorTransform().TransformPosition(Throat->VomitOrigin+FVector(0,(I-1)*18,I*6));
        P.Delay=I*.22f; P.Duration=1.05f+I*.18f; P.Radius=19+I*3;
        P.Impact=Floor.ImpactPoint; P.Normal=Floor.ImpactNormal; P.ImpactAge=P.Duration;
        P.Velocity=(P.Impact+P.Normal*P.Radius-P.Start)/P.Duration+FVector(0,0,490*P.Duration);
        Portions.Add(P);
    }
    ForceNetUpdate(); SetLifeSpan(5);
}
int32 AMCVomitBurst::LandedPortions() const
{
    int32 Count=0; for(const auto& P:Portions) if(P.bLanded && P.bOnTongue) ++Count; return Count;
}
void AMCVomitBurst::Land(int32 Index,const FHitResult& Hit,float Age)
{
    auto& P=Portions[Index]; if(P.bLanded) return;
    P.bLanded=true; P.ImpactAge=Age; P.Impact=Hit.ImpactPoint; P.Normal=Hit.ImpactNormal;
    P.bOnTongue=Cast<AMCTongue>(Hit.GetActor())!=nullptr;
    if(P.bOnTongue) {
        const FTransform Pose(FRotationMatrix::MakeFromZ(P.Normal).ToQuat(),P.Impact+P.Normal*2);
        if(auto* Patch=GetWorld()->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),Pose)) {
            Patch->bRandomizeLiquidSize=false; Patch->LiquidHalfSize=78+Index*13; Patch->Batch=Batch;
            Patch->LiquidBornAt=ServerNow();
            Patch->LiquidMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Gameplay/Hazards/MI_VomitPuddle.MI_VomitPuddle")));
            Patch->FinishSpawning(Pose); Patch->Status->ApplyCoffee(.65f); Patch->ForceNetUpdate();
        }
    }
    UE_LOG(LogTemp,Display,TEXT("MC_VOMIT_IMPACT batch=%d portion=%d tongue=%d age=%.3f actor=%s component=%s location=%s start=%s"),Batch,Index,P.bOnTongue,Age,*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),*P.Impact.ToString(),*P.Start.ToString());
    ForceNetUpdate();
}
void AMCVomitBurst::Tick(float Dt)
{
    Super::Tick(Dt); const float Age=FMath::Max(0.f,float(ServerNow()-StartedAt));
    if(HasAuthority()) {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MCVomitFlight),true,this); Query.AddIgnoredActor(GetOwner());
        for(int32 I=0;I<Portions.Num();++I) {
            auto& P=Portions[I]; if(P.bLanded || Age<P.Delay) continue;
            const float End=FMath::Min(Age-P.Delay,P.Duration+.8f);
            float T=FMath::Max(0.f,CheckedAge-P.Delay);
            while(T<End) {
                const float Next=FMath::Min(T+1.f/60,End);
                FHitResult Hit;
                // Returned food and pawns share the emitter. They must not consume
                // the goo's first impact while the portion is still leaving the mouth.
                if(GetWorld()->SweepSingleByObjectType(Hit,FlightPoint(P,T),FlightPoint(P,Next),FQuat::Identity,FCollisionObjectQueryParams(ECC_WorldStatic),
                    FCollisionShape::MakeSphere(P.Radius),Query)) { Land(I,Hit,FMath::Lerp(T,Next,Hit.Time)); break; }
                T=Next;
            }
        }
        CheckedAge=Age;
    }
    if(GetNetMode()!=NM_DedicatedServer) UpdatePresentation(Age);
}
void AMCVomitBurst::UpdatePresentation(float Age)
{
    TArray<FVector> V,N; TArray<FVector2D> UV; TArray<int32> Tri; TArray<FColor> Colors; TArray<FProcMeshTangent> Tangents;
    Droplets->ClearInstances(); AirborneInstances=SplashInstances=0;
    auto Drop=[&](FVector P,FVector Size,FVector Direction) {
        Droplets->AddInstance(FTransform(FRotationMatrix::MakeFromZ(Direction).ToQuat(),GetActorTransform().InverseTransformPosition(P),Size/50));
    };
    for(int32 I=0;I<Portions.Num();++I) {
        const auto& P=Portions[I]; const float AgeP=Age-P.Delay;
        if(AgeP<0) continue;
        const float End=P.ImpactAge;
        if(AgeP<End) {
            const FVector Head=FlightPoint(P,AgeP),Velocity=P.Velocity-FVector(0,0,980*AgeP);
            for(int32 K=0;K<3;++K) {
                const float R=P.Radius*(K==0?1.f:.6f);
                Drop(Head+FVector(FMath::Sin(float(I*3+K))*R*.6,FMath::Cos(float(K*4))*R*.7,K==2?R*.35:0),FVector(R,R*.88,R*1.5),Velocity);
                ++AirborneInstances;
            }
            constexpr int32 Rings=12,Sides=10; const int32 Base=V.Num();
            for(int32 R=0;R<=Rings;++R) {
                const float Along=R/float(Rings),T=FMath::Max(0.f,AgeP-.30f)+FMath::Min(AgeP,.30f)*Along;
                const FVector Center=FlightPoint(P,T),Dir=(P.Velocity-FVector(0,0,980*T)).GetSafeNormal();
                const FVector Side=FVector::CrossProduct(Dir,FVector::UpVector).GetSafeNormal(),Up=FVector::CrossProduct(Side,Dir).GetSafeNormal();
                const float Radius=P.Radius*(.13f+.62f*FMath::Pow(Along,.7f))*(1+.12f*FMath::Sin(T*25+I*3));
                for(int32 S=0;S<=Sides;++S) {
                    const float A=S*2*PI/Sides; const FVector Normal=Side*FMath::Cos(A)+Up*FMath::Sin(A);
                    V.Add(GetActorTransform().InverseTransformPosition(Center+Normal*Radius)); N.Add(Normal); UV.Add(FVector2D(S/float(Sides),Along));
                    Colors.Add(FColor::White); Tangents.Add(FProcMeshTangent(Dir,false));
                    if(R<Rings && S<Sides) { const int32 B=Base+R*(Sides+1)+S; Tri.Append({B,B+Sides+1,B+1,B+1,B+Sides+1,B+Sides+2}); }
                }
            }
            FRandomStream Random(Batch+I*193);
            for(int32 K=0;K<16;++K) {
                const float Lag=Random.FRandRange(.01f,.15f),T=AgeP-Lag,R=Random.FRandRange(2.8f,6.5f);
                const FVector Spread(0,Random.FRandRange(-100,100),Random.FRandRange(-70,60));
                if(T>0) { Drop(FlightPoint(P,T)+Spread*T,FVector(R,R,R*1.65),Velocity+Spread); ++AirborneInstances; }
            }
        }
        const float SplashAge=AgeP-End;
        if(P.bLanded && SplashAge>=0 && SplashAge<.62f) {
            FVector Side,Up; P.Normal.FindBestAxisVectors(Side,Up);
            for(int32 K=0;K<12;++K) {
                const float A=K*2*PI/12+I*1.3f,Speed=170+(K%3)*45,Life=.34f+(K%4)*.065f;
                if(SplashAge>Life) continue;
                const FVector Velocity=(Side*FMath::Cos(A)+Up*FMath::Sin(A))*Speed+P.Normal*(170+(K%3)*25);
                const FVector Pos=P.Impact+P.Normal*8+Velocity*SplashAge-FVector(0,0,490*SplashAge*SplashAge);
                const float R=(4+(K%3)*2)*(1-.65f*SplashAge/Life);
                Drop(Pos,FVector(R,R,R*1.35),Velocity-FVector(0,0,980*SplashAge)); ++SplashInstances;
            }
        }
    }
    Droplets->SetVisibility(Droplets->GetInstanceCount()>0);
    if(V.IsEmpty()) { Stream->SetVisibility(false); return; }
    Stream->SetVisibility(true);
    auto* Section=Stream->GetProcMeshSection(0);
    if(!Section || Section->ProcVertexBuffer.Num()!=V.Num()) Stream->CreateMeshSection(0,V,Tri,N,UV,Colors,Tangents,false);
    else Stream->UpdateMeshSection(0,V,N,UV,Colors,Tangents);
}
void AMCVomitBurst::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCVomitBurst,Portions); DOREPLIFETIME(AMCVomitBurst,StartedAt); DOREPLIFETIME(AMCVomitBurst,Batch);
}
