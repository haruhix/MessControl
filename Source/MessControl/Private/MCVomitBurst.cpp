#include "MCVomitBurst.h"
#include "MCWorldPresentationComponent.h"
#include "MCVFXPresentationComponent.h"
#include "MCThroat.h"
#include "MCTongue.h"
#include "MCMouthSurface.h"
#include "MCGameState.h"
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
    if(ComposerPresentation)ComposerPresentation->StopStates(TEXT("World.Vomit"));ComposerEpisode=FGuid::NewGuid();bComposerBurstAccepted=false;
    SetOwner(Throat); StartedAt=ServerNow(); Batch=10000+Throat->MealSequence; Portions.Reset(); CheckedAge=0;
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    FRandomStream Random(HashCombine(GetTypeHash(State?State->RunSeed:1),GetTypeHash(Batch)));
    TArray<FVector> Excluded;
    for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It)
        if(It->bUlcer || !It->IsClean()) Excluded.Add(It->GetActorLocation());
    for(int32 I=0;I<3;++I) {
        const float HalfSize=78+I*13;
        FMCVomitPortion P;
        P.Start=Throat->GetActorTransform().TransformPosition(Throat->VomitOrigin+FVector(0,(I-1)*18,I*6));
        P.Delay=I*.22f; P.Duration=1.05f+I*.18f; P.Radius=19+I*3;
        bool Found=false;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MCVomitSpawnPath),true,this); Query.AddIgnoredActor(Throat);
        for(int32 Attempt=0;Attempt<16 && !Found;++Attempt)
        {
            FHitResult Floor; AMCTongue* Tongue=nullptr;
            for(TActorIterator<AMCTongue> T(GetWorld());T;++T)
                if(T->RandomInteriorPoint(Random,HalfSize*1.415f+40,HalfSize*2+100,Excluded,Floor)) {Tongue=*T;break;}
            if(!Tongue) break;
            P.Impact=Floor.ImpactPoint; P.Normal=Floor.ImpactNormal; P.ImpactAge=P.Duration;
            P.Velocity=(P.Impact+P.Normal*P.Radius-P.Start)/P.Duration+FVector(0,0,490*P.Duration);
            // A random target beyond a ridge or the roof can still hit the same
            // nearby obstacle first. Validate the actual first ballistic impact.
            constexpr int32 Steps=64;
            const float FlightEnd=P.Duration+.25f;
            for(int32 Step=0;Step<Steps;++Step)
            {
                FHitResult Impact;
                if(!GetWorld()->SweepSingleByObjectType(Impact,FlightPoint(P,FlightEnd*Step/Steps),FlightPoint(P,FlightEnd*(Step+1)/Steps),
                    FQuat::Identity,FCollisionObjectQueryParams(ECC_WorldStatic),FCollisionShape::MakeSphere(P.Radius),Query)) continue;
                FHitResult Interior;
                Found=Impact.GetActor()==Tongue && Tongue->InteriorSurfacePoint(Impact.ImpactPoint,HalfSize*1.415f+40,Interior);
                for(const FVector& Position:Excluded)
                    if(FVector::DistSquared2D(Impact.ImpactPoint,Position)<FMath::Square(HalfSize*2+100)) Found=false;
                if(Found) {P.Impact=Impact.ImpactPoint;P.Normal=Impact.ImpactNormal;}
                break;
            }
        }
        if(!Found) {UE_LOG(LogTemp,Warning,TEXT("MC_VOMIT_SPAWN no clear interior flight for portion %d"),I);continue;}
        Portions.Add(P); Excluded.Add(P.Impact);
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
    EnsureSpreadSamples();
    AirborneInstances=SplashInstances=0;
    const bool KeepNative=UpdateComposerArtwork(Age);
    if(!KeepNative){Stream->SetVisibility(false);Droplets->SetVisibility(false);return;}
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
            for(int32 K=0;K<16;++K) {
                const auto& Sample=SpreadSamples[I*16+K];
                const float T=AgeP-Sample.X,R=Sample.Y;const FVector Spread(0,Sample.Z,Sample.W);
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
void AMCVomitBurst::BeginPlay()
{Super::BeginPlay();ComposerPresentation=MCWorldPresentation::PrepareOwner(this,{TEXT("World.Vomit")});}
void AMCVomitBurst::EndPlay(const EEndPlayReason::Type Reason)
{if(ComposerPresentation)ComposerPresentation->StopStates(TEXT("World.Vomit"));Super::EndPlay(Reason);}
void AMCVomitBurst::EnsureSpreadSamples()
{
    if(SampleBatch==Batch&&SampleCount==Portions.Num())return;
    SampleBatch=Batch;SampleCount=Portions.Num();SpreadSamples.SetNum(Portions.Num()*16);
    for(int32 I=0;I<Portions.Num();++I){FRandomStream Random(Batch+I*193);
        for(int32 K=0;K<16;++K){const float Lag=Random.FRandRange(.01f,.15f),R=Random.FRandRange(2.8f,6.5f);
            // Keep the original vector-constructor RNG expression and call order.
            const FVector Spread(0,Random.FRandRange(-100,100),Random.FRandRange(-70,60));
            SpreadSamples[I*16+K]=FVector4f(Lag,R,float(Spread.Y),float(Spread.Z));}}
}
bool AMCVomitBurst::UpdateComposerArtwork(float Age)
{
    if(!ComposerPresentation)return true;
    constexpr auto ID=TEXT("World.Vomit");auto* Channel=ComposerPresentation->GetSource(ID);
    EMCPerkVFXSlotMode Mode=EMCPerkVFXSlotMode::UseDefault;
    if(!Channel||!Channel->GetPreparedEventMode(ID,TEXT("Burst"),Mode))return true;
    const bool Supported=Portions.Num()<=3&&GetActorQuat().IsIdentity()&&GetActorScale3D().Equals(FVector::OneVector);
    FMCPerkVFXGameplayValues Values;Values.EpisodeID=ComposerEpisode;Values.LocalStateAgeSeconds=Age;Values.Duration=5;
    Values.VisualSubject=Stream;
    const FVector Origin=Portions.IsEmpty()?GetActorLocation():Portions[0].Start;
    auto Add=[&](const TCHAR* N,EMCVFXNativeInputType T)->FMCVFXNativeInput&{auto& I=Values.NativeInputs.Entries.AddDefaulted_GetRef();I.Name=N;I.Type=T;return I;};
    Add(TEXT("User.Native.VomitAge"),EMCVFXNativeInputType::Float).FloatValue=Age;
    Add(TEXT("User.Native.VomitCount"),EMCVFXNativeInputType::Int).IntValue=FMath::Min(3,Portions.Num());
    Add(TEXT("User.Native.VomitOrigin"),EMCVFXNativeInputType::Position).PositionValue=Origin;
    Add(TEXT("User.Native.VomitReady"),EMCVFXNativeInputType::Bool).BoolValue=Supported&&!Portions.IsEmpty();
    auto Vectors=[&](const TCHAR* N){return &Add(N,EMCVFXNativeInputType::VectorArray).VectorValues;};
    auto Floats=[&](const TCHAR* N){return &Add(N,EMCVFXNativeInputType::FloatArray).FloatValues;};
    // Reserve entries first: later Add cannot invalidate references to arrays.
    Values.NativeInputs.Entries.Reserve(20);
    auto* Starts=Vectors(TEXT("User.Native.VomitStarts"));auto* Velocities=Vectors(TEXT("User.Native.VomitVelocities"));
    auto* Impacts=Vectors(TEXT("User.Native.VomitImpacts"));auto* Normals=Vectors(TEXT("User.Native.VomitNormals"));
    auto* Sides=Vectors(TEXT("User.Native.VomitSplashSide"));auto* Ups=Vectors(TEXT("User.Native.VomitSplashUp"));
    auto* Delays=Floats(TEXT("User.Native.VomitDelays"));auto* Radii=Floats(TEXT("User.Native.VomitRadii"));
    auto* Ends=Floats(TEXT("User.Native.VomitEnds"));auto* Landed=Floats(TEXT("User.Native.VomitLanded"));
    auto* Spread=Vectors(TEXT("User.Native.VomitSpread"));auto* Lags=Floats(TEXT("User.Native.VomitLags"));
    if(Supported)for(int32 J=0;J<Portions.Num();++J){const auto& P=Portions[J];Starts->Add(P.Start-Origin);Velocities->Add(P.Velocity);Impacts->Add(P.Impact-Origin);Normals->Add(P.Normal);
        Delays->Add(P.Delay);Radii->Add(P.Radius);Ends->Add(P.ImpactAge);Landed->Add(P.bLanded?1:0);FVector Side,Up;P.Normal.FindBestAxisVectors(Side,Up);Sides->Add(Side);Ups->Add(Up);
        for(int32 K=0;K<16;++K){const auto& S=SpreadSamples[J*16+K];Lags->Add(S.X);Spread->Add(FVector(S.Y,S.Z,S.W));}}
    FString Error;const bool Accepted=ComposerPresentation->UpdateState(ID,TEXT("Burst"),Age<5,FTransform(Origin),Values,Stream,Error);
    bComposerBurstAccepted|=Accepted&&Supported&&!Portions.IsEmpty()&&Age<5;
    if(Mode==EMCPerkVFXSlotMode::Disabled)return !Accepted;
    return !Supported||!Accepted||ComposerPresentation->UsesNativeVisuals(ID,TEXT("Burst"));
}

void AMCVomitBurst::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCVomitBurst,Portions); DOREPLIFETIME(AMCVomitBurst,StartedAt); DOREPLIFETIME(AMCVomitBurst,Batch);
}
