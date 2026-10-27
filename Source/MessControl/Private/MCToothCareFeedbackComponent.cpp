#include "MCToothCareFeedbackComponent.h"
#include "MCArenaTooth.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"

namespace MCToothCareFX
{
constexpr int32 MaxQuads=64,MaxParticles=56;
FLinearColor Tint(EMCToothCareFeedbackKind Kind)
{return Kind==EMCToothCareFeedbackKind::Repair?FLinearColor(.16f,.86f,.66f):FLinearColor(.97f,.99f,.91f);}
int32 NextSerial(int32 Value) {return Value==MAX_int32?1:Value+1;}
}

UMCToothCareFeedbackComponent::UMCToothCareFeedbackComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bStartWithTickEnabled=false;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork;SetIsReplicatedByDefault(true);
    CareMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Gameplay/Care/M_ToothCarePearl.M_ToothCarePearl")));
}
double UMCToothCareFeedbackComponent::Clock() const
{
    const auto* World=GetWorld();const auto* GS=World?World->GetGameState():nullptr;
    return GS?GS->GetServerWorldTimeSeconds():World?World->GetTimeSeconds():0.;
}
void UMCToothCareFeedbackComponent::BeginPlay()
{
    Super::BeginPlay();Tooth=Cast<AMCArenaTooth>(GetOwner());
    if(!Tooth) return;
    Random.Initialize(Tooth->State.ToothId*3397+743);Particles.Reserve(MCToothCareFX::MaxParticles);
    AddTickPrerequisiteActor(Tooth);
    PreparePresentation(); // Before the world's resource barrier, never on first cleaning.
    OnRep_Presentation();
}
void UMCToothCareFeedbackComponent::PreparePresentation()
{
    if(Presentation || !Tooth || GetNetMode()==NM_DedicatedServer || !FApp::CanEverRender()) return;
    RetainedMaterial=CareMaterial.LoadSynchronous();
    if(!RetainedMaterial) return; // Missing authored material is reported at the checkpoint, not replaced by a flat fallback.
    Presentation=NewObject<UProceduralMeshComponent>(Tooth,TEXT("ToothCarePresentation"));
    Presentation->SetupAttachment(Tooth->GetRootComponent());Presentation->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Presentation->SetGenerateOverlapEvents(false);Presentation->SetCanEverAffectNavigation(false);
    Presentation->SetCastShadow(false);Presentation->SetMaterial(0,RetainedMaterial);
    Presentation->SetCullDistance(2800.f);
    Tooth->AddInstanceComponent(Presentation);Presentation->RegisterComponent();
    auto& Vertices=DrawVertices;auto& Normals=DrawNormals;auto& UV=DrawUV;auto& Variants=DrawVariants;
    TArray<FLinearColor> Colors;
    TArray<int32> Indices;TArray<FProcMeshTangent> Tangents;
    Vertices.Init(FVector::ZeroVector,MCToothCareFX::MaxQuads*4);Normals.Init(FVector::UpVector,Vertices.Num());
    UV.Init(FVector2D::ZeroVector,Vertices.Num());Variants=UV;Colors.Init(FLinearColor(1,1,1,0),Vertices.Num());
    DrawPackedColors.Init(FColor(255,255,255,0),Vertices.Num());
    for(int32 I=0;I<MCToothCareFX::MaxQuads;++I) Indices.Append({I*4,I*4+1,I*4+2,I*4,I*4+2,I*4+3});
    // Keep a valid tiny, invisible primitive registered for material/PSO preparation.
    Vertices[0]=FVector(-1,-1,0);Vertices[1]=FVector(1,-1,0);Vertices[2]=FVector(1,1,0);Vertices[3]=FVector(-1,1,0);
    Presentation->CreateMeshSection_LinearColor(0,Vertices,Indices,Normals,UV,Variants,{}, {},Colors,Tangents,false);
    Presentation->SetVisibility(false);
}
void UMCToothCareFeedbackComponent::ReportAccepted(EMCToothCareFeedbackKind Kind,FVector Point,FVector Normal,bool bCompleted)
{
    auto* OwnerTooth=Cast<AMCArenaTooth>(GetOwner());
    if(!OwnerTooth || !OwnerTooth->HasAuthority() || !OwnerTooth->IsAvailable() || !OwnerTooth->Visual
        || Point.ContainsNaN() || Normal.ContainsNaN() || Normal.IsNearlyZero()) return;
    const double Now=Clock();
    if(!bCompleted && Now-LastPublishedAt<.09) return;
    Tooth=OwnerTooth;
    FVector SurfacePoint,SurfaceNormal;
    // Tool collision may hit the supporting box. Seat cosmetics on this tooth's
    // real enamel instead of drawing a floating cloud on a box corner.
    if(!ProjectSurface(Point,Normal.GetSafeNormal(),SurfacePoint,SurfaceNormal)) return;
    Point=SurfacePoint;Normal=SurfaceNormal;
    const FTransform Surface=OwnerTooth->Visual->GetComponentTransform();
    State.Kind=Kind;State.WorkAt=Now;State.WorkSerial=MCToothCareFX::NextSerial(State.WorkSerial);
    State.ContactLocal=Surface.InverseTransformPosition(Point);
    State.NormalLocal=Surface.InverseTransformVectorNoScale(Normal).GetSafeNormal();
    if(bCompleted) {
        State.CompletionSerial=MCToothCareFX::NextSerial(State.CompletionSerial);State.CompletedAt=Now;
        State.CompletionKind=Kind;State.CompletionLocal=State.ContactLocal;State.CompletionNormalLocal=State.NormalLocal;
    }
    LastPublishedAt=Now;OwnerTooth->ForceNetUpdate();
    if(GetNetMode()!=NM_DedicatedServer) OnRep_Presentation();
}
void UMCToothCareFeedbackComponent::OnRep_Presentation()
{
    if(!Tooth) Tooth=Cast<AMCArenaTooth>(GetOwner());
    if(GetNetMode()==NM_DedicatedServer || !Presentation || !Tooth || !Tooth->IsAvailable()) return;
    const double Now=Clock();
    if(State.CompletionSerial!=SeenCompletion) {
        SeenCompletion=State.CompletionSerial;
        // A late join reconstructs active work but never replays old completion bursts.
        if(State.CompletionSerial>0 && Now>=State.CompletedAt && Now-State.CompletedAt<1.15) EmitCompletion();
    }
    if(!Particles.IsEmpty() || (State.WorkSerial>0 && Now>=State.WorkAt && Now-State.WorkAt<.24)) SetComponentTickEnabled(true);
}
void UMCToothCareFeedbackComponent::AddParticle(const FParticle& Particle)
{
    if(Particles.Num()<MCToothCareFX::MaxParticles) Particles.Add(Particle);
}
bool UMCToothCareFeedbackComponent::ProjectSurface(FVector Candidate,FVector Normal,FVector& Point,FVector& HitNormal) const
{
    if(!Tooth || !Tooth->BrushSurface) return false;
    const float Span=FMath::Max(200.f,float(Tooth->Visual->Bounds.BoxExtent.GetMax())*2.f);
    FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCToothCareSurface),true);
    if(!Tooth->BrushSurface->LineTraceComponent(Hit,Candidate+Normal*Span,Candidate-Normal*Span,Query)) return false;
    Point=Hit.ImpactPoint;HitNormal=Hit.ImpactNormal.GetSafeNormal();return true;
}
void UMCToothCareFeedbackComponent::EmitProcess(double Now)
{
    const FTransform Surface=Tooth->Visual->GetComponentTransform();
    const FVector N=Surface.TransformVectorNoScale(FVector(State.NormalLocal)).GetSafeNormal();
    const FVector Up=FVector::VectorPlaneProject(FVector::UpVector,N).GetSafeNormal(SMALL_NUMBER,FVector::RightVector);
    const FVector Side=FVector::CrossProduct(Up,N).GetSafeNormal();
    FParticle P;P.Born=Now;P.Life=Random.FRandRange(.36f,.70f);P.Size=Random.FRandRange(7.f,12.f);
    P.Position=Surface.TransformPosition(FVector(State.ContactLocal))+N*3+Side*Random.FRandRange(-10.f,10.f)+Up*Random.FRandRange(-4.f,8.f);
    P.Velocity=N*Random.FRandRange(12.f,26.f)+FVector::UpVector*Random.FRandRange(15.f,30.f)+Side*Random.FRandRange(-7.f,7.f);
    P.Shape=Random.FRand()<.38f?1:0;P.Color=MCToothCareFX::Tint(State.Kind);
    if(P.Shape==1) {P.Size*=1.35f;P.Life*=1.2f;}
    AddParticle(P);
}
void UMCToothCareFeedbackComponent::EmitCompletion()
{
    const FTransform Surface=Tooth->Visual->GetComponentTransform();
    const FVector N=Surface.TransformVectorNoScale(FVector(State.CompletionNormalLocal)).GetSafeNormal();
    const FVector Up=FVector::VectorPlaneProject(FVector::UpVector,N).GetSafeNormal(SMALL_NUMBER,FVector::RightVector);
    const FVector Side=FVector::CrossProduct(Up,N).GetSafeNormal();
    const FVector Contact=Surface.TransformPosition(FVector(State.CompletionLocal));
    const float Scale=FMath::Clamp(float(Tooth->Visual->Bounds.BoxExtent.GetMax())/200.f,.75f,1.8f);
    for(int32 I=0;I<5;++I) {
        FVector Point,Normal;const FVector Candidate=Contact+Side*((I-2)*25.f*Scale)+Up*((I%3)*23.f*Scale);
        if(!ProjectSurface(Candidate,N,Point,Normal)) continue;
        FParticle P;P.Position=Surface.InverseTransformPosition(Point+Normal*3);P.bOnSurface=true;
        P.Born=State.CompletedAt+.05+I*.075;P.Life=.70f;P.Size=(I==2?24.f:15.5f)*Scale;
        P.Shape=2;P.Color=MCToothCareFX::Tint(State.CompletionKind);AddParticle(P);
    }
    // Pearls depart in 3D with a separate, slightly longer tail than the glints.
    for(int32 I=0;I<6;++I) {
        FParticle P;P.Position=Contact+N*5+Side*Random.FRandRange(-18.f,18.f)*Scale;
        P.Velocity=N*Random.FRandRange(18.f,35.f)+Up*Random.FRandRange(18.f,38.f)+Side*Random.FRandRange(-14.f,14.f);
        P.Born=State.CompletedAt+.04+I*.025;P.Life=Random.FRandRange(.45f,.68f);P.Size=Random.FRandRange(5.f,9.f)*Scale;
        P.Shape=I%3==0?1:0;P.Color=MCToothCareFX::Tint(State.CompletionKind);AddParticle(P);
    }
}
void UMCToothCareFeedbackComponent::Draw(double Now)
{
    auto* PC=UGameplayStatics::GetPlayerController(GetWorld(),0);
    if(!PC || !PC->PlayerCameraManager) return;
    const FRotator View=PC->PlayerCameraManager->GetCameraRotation();
    const FVector Right=View.RotateVector(FVector::RightVector),Up=View.RotateVector(FVector::UpVector);
    const FTransform ToLocal=Presentation->GetComponentTransform();
    auto& Vertices=DrawVertices;auto& Normals=DrawNormals;auto& UV=DrawUV;auto& Variants=DrawVariants;auto& Colors=DrawPackedColors;
    TArray<FProcMeshTangent> Tangents;
    for(int32 I=0;I<Vertices.Num();++I) {Vertices[I]=FVector::ZeroVector;Colors[I].A=0;}
    int32 Count=0;
    auto Quad=[&](FVector Center,FVector X,FVector Y,float Shape,FLinearColor Color) {
        if(Count>=MCToothCareFX::MaxQuads) return;
        const FVector Corners[]={Center-X-Y,Center+X-Y,Center+X+Y,Center-X+Y};
        const FVector2D Tex[]={FVector2D(0,1),FVector2D(1,1),FVector2D(1,0),FVector2D(0,0)};
        const FColor Packed=Color.ToFColor(false);
        for(int32 I=0;I<4;++I) {const int32 V=Count*4+I;Vertices[V]=ToLocal.InverseTransformPosition(Corners[I]);UV[V]=Tex[I];Variants[V]=FVector2D(Shape,0);Colors[V]=Packed;}
        ++Count;
    };
    for(const auto& P:Particles) {
        const float Age=float(Now-P.Born);if(Age<0 || Age>P.Life) continue;
        const float T=Age/P.Life;
        const float Alpha=FMath::SmoothStep(0.f,.16f,T)*(1.f-FMath::SmoothStep(.48f,1.f,T));
        const float Size=P.Size*FMath::Lerp(.65f,1.10f,FMath::SmoothStep(0.f,1.f,T));
        FVector Position=P.bOnSurface?Tooth->Visual->GetComponentTransform().TransformPosition(P.Position)
            :P.Position+P.Velocity*Age+FVector(0,0,7.f*Age*Age);
        FLinearColor Color=P.Color;Color.A=Alpha;Quad(Position,Right*Size,Up*Size,P.Shape,Color);
    }
    const float CompletionAge=float(Now-State.CompletedAt);
    if(State.CompletionSerial>0 && CompletionAge>.06f && CompletionAge<1.06f) {
        const float T=(CompletionAge-.06f)/1.f;
        const FVector N=Tooth->Visual->GetComponentTransform().TransformVectorNoScale(FVector(State.CompletionNormalLocal)).GetSafeNormal();
        const auto Bounds=Tooth->Visual->Bounds;
        FVector Point,Normal;
        if(ProjectSurface(Bounds.Origin+FVector(0,0,Bounds.BoxExtent.Z*FMath::Lerp(-.25f,.75f,T)),N,Point,Normal)) {
            FVector Along=FVector::CrossProduct(FVector::UpVector,Normal).GetSafeNormal(SMALL_NUMBER,Right);
            FVector Across=FVector::CrossProduct(Normal,Along).GetSafeNormal();
            const float Width=FMath::Clamp(float(Bounds.BoxExtent.GetMax())*.55f,40.f,120.f);
            FLinearColor Color=MCToothCareFX::Tint(State.CompletionKind);Color.A=FMath::Sin(T*PI)*.98f;
            Quad(Point+Normal*3,Along*Width,Across*10.f,3,Color);
        }
    }
    // The native FColor+UV1 overload avoids LinearColor's per-frame conversion allocation.
    Presentation->UpdateMeshSection(0,Vertices,Normals,UV,Variants,{}, {},Colors,Tangents);
    Presentation->SetVisibility(Count>0);
}
void UMCToothCareFeedbackComponent::TickComponent(float Dt,ELevelTick Tick,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Dt,Tick,Function);
    if(!Tooth || !Tooth->IsAvailable() || !Presentation) {if(Presentation) Presentation->SetVisibility(false);Particles.Reset();SetComponentTickEnabled(false);return;}
    const double Now=Clock();Particles.RemoveAll([Now](const FParticle& P){return Now>P.Born+P.Life;});
    const bool Working=State.WorkSerial>0 && Now>=State.WorkAt && Now-State.WorkAt<.24;
    if(Working) {
        EmitRemainder+=FMath::Clamp(Dt,0.f,.1f)*12.f;
        for(int32 I=0;I<2 && EmitRemainder>=1.f;++I) {EmitRemainder-=1.f;EmitProcess(Now);}
    } else EmitRemainder=0;
    Draw(Now);
    if(!Working && Particles.IsEmpty() && Now-State.CompletedAt>1.15) {Presentation->SetVisibility(false);SetComponentTickEnabled(false);}
}
void UMCToothCareFeedbackComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if(Presentation) {Presentation->DestroyComponent();Presentation=nullptr;}
    Particles.Reset();RetainedMaterial=nullptr;Super::EndPlay(Reason);
}
void UMCToothCareFeedbackComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(UMCToothCareFeedbackComponent,State);}
