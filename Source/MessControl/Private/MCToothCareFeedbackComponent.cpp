#include "MCToothCareFeedbackComponent.h"
#include "MCArenaTooth.h"
#include "MCWorldPresentationComponent.h"
#include "MCVFXNativeInputs.h"
#include "MCVFXPresentationComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
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
    Composer=MCWorldPresentation::PrepareOwner(Tooth,{TEXT("EditorOnly.CleanArenaTooth"),TEXT("EditorOnly.RepairArenaTooth")});
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
    if(!ProjectSurface(Point,Normal.GetSafeNormal(),SurfacePoint,SurfaceNormal)) {
        // The repair trace can hit the support box where a large piece of the
        // crown is missing. Healing is already accepted, but that ray need
        // not intersect the remaining enamel. Keep the repair presentation
        // alive at the authored fracture anchor in this case.
        static const FName ChipSocket(TEXT("MC_TreatmentChip"));
        if(Kind!=EMCToothCareFeedbackKind::Repair || !OwnerTooth->Visual->DoesSocketExist(ChipSocket)) return;
        SurfacePoint=OwnerTooth->Visual->GetSocketLocation(ChipSocket);
        SurfaceNormal=Normal.GetSafeNormal();
        if(SurfacePoint.ContainsNaN() || SurfaceNormal.IsNearlyZero()) return;
    }
    Point=SurfacePoint;Normal=SurfaceNormal;
    const FTransform Surface=OwnerTooth->Visual->GetComponentTransform();
    if(State.WorkSerial<=0 || State.Kind!=Kind || Now-State.WorkAt>=.24)State.ContactStartedAt=Now;
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
    if(GetNetMode()==NM_DedicatedServer || !Tooth || !Tooth->IsAvailable()) return;
    const double Now=Clock();
    UpdateComposer(Now);
    if(State.CompletionSerial!=SeenCompletion) {
        SeenCompletion=State.CompletionSerial;
        // A late join reconstructs active work but never replays old completion bursts.
        if(Presentation && bNativeCompletion && State.CompletionSerial>0 && Now>=State.CompletedAt && Now-State.CompletedAt<1.15) EmitCompletion();
    }
    if(!Particles.IsEmpty() || (State.WorkSerial>0 && Now>=State.WorkAt && Now-State.WorkAt<.24)
        || (State.CompletionSerial>0 && Now>=State.CompletedAt && Now-State.CompletedAt<1.15)) SetComponentTickEnabled(true);
}

void UMCToothCareFeedbackComponent::UpdateComposer(double Now)
{
    bNativeContact=bNativeCompletion=true;
    if(!Composer||!Tooth||!Tooth->Visual)return;
    const FTransform Surface=Tooth->Visual->GetComponentTransform();
    for(const bool Completion:{false,true})
    {
        const auto Kind=Completion?State.CompletionKind:State.Kind;
        const FName ID=Kind==EMCToothCareFeedbackKind::Repair?TEXT("EditorOnly.RepairArenaTooth"):TEXT("EditorOnly.CleanArenaTooth");
        const FName Other=Kind==EMCToothCareFeedbackKind::Repair?TEXT("EditorOnly.CleanArenaTooth"):TEXT("EditorOnly.RepairArenaTooth");
        const FName Event=Completion?TEXT("Complete"):TEXT("Contact");
        const double Started=Completion?State.CompletedAt:State.WorkAt;
        const int32 Serial=Completion?State.CompletionSerial:State.WorkSerial;
        const float Life=Completion?1.15f:.24f,Age=float(Now-Started);
        const FVector Local=Completion?FVector(State.CompletionLocal):FVector(State.ContactLocal);
        const FVector Normal=Surface.TransformVectorNoScale(Completion?FVector(State.CompletionNormalLocal):FVector(State.NormalLocal)).GetSafeNormal();
        const FTransform Frame(FRotationMatrix::MakeFromZ(Normal).ToQuat(),Surface.TransformPosition(Local));
        FMCPerkVFXGameplayValues Values;Values.Direction=Normal;Values.Radius=Tooth->Visual->Bounds.BoxExtent.GetMax();
        Values.Duration=Life;Values.Fill=FMath::Clamp(Age/Life,0.f,1.f);
        Values.LocalStateAgeSeconds=FMath::Max(0.f,float(Now-(Completion?State.CompletedAt:State.ContactStartedAt)));
        Values.VisualSubject=Tooth->Visual.Get();
        const bool Active=Serial>0&&Age>=0&&Age<Life&&Tooth->IsAvailable();
        EMCPerkVFXSlotMode Mode=EMCPerkVFXSlotMode::UseDefault;
        if(Active)if(const auto* Source=Composer->GetSource(ID))
            if(Source->GetPreparedEventMode(ID,Event,Mode)
                && (Mode==EMCPerkVFXSlotMode::Replace||Mode==EMCPerkVFXSlotMode::Composition))
                AppendNativeInputs(Completion,Now,Frame,Values.NativeInputs);
        if(Completion)Values.EpisodeID=FGuid(uint32(State.CompletionSerial),uint32(Tooth->State.ToothId),uint32(Kind),0xCA7E0001);
        FString Error;Composer->UpdateState(Other,Event,false,Frame,Values,this,Error);
        const bool Updated=Composer->UpdateState(ID,Event,Active,Frame,Values,this,Error);
        const bool Native=!Updated||Composer->UsesNativeVisuals(ID,Event);
        if(Completion)bNativeCompletion=Native;else bNativeContact=Native;
    }
}
void UMCToothCareFeedbackComponent::RefreshSurfaceSamples(FVector WorldNormal)
{
    if(!Tooth || !Tooth->Visual)return;
    const FTransform Surface=Tooth->Visual->GetComponentTransform();
    const FVector NormalLocal=Surface.InverseTransformVectorNoScale(WorldNormal).GetSafeNormal();
    if(SampledMesh.Get()==Tooth->Visual->GetStaticMesh() && !SurfacePointsLocal.IsEmpty()
        && SampledScale.Equals(Surface.GetScale3D(),.001f) && FVector::DotProduct(SampledNormal,NormalLocal)>.995f)return;
    SampledMesh=Tooth->Visual->GetStaticMesh();SampledNormal=NormalLocal;SampledScale=Surface.GetScale3D();
    SurfacePointsLocal.Reset();SurfaceNormalsLocal.Reset();SurfaceValid.Reset();
    // Untimed collision geometry over the whole visible crown. Niagara owns
    // selection, offsets, interpolation, speed and all artistic curves.
    const auto Bounds=Tooth->Visual->Bounds;
    const FVector Up=FVector::VectorPlaneProject(FVector::UpVector,WorldNormal).GetSafeNormal(SMALL_NUMBER,FVector::RightVector);
    const FVector Side=FVector::CrossProduct(Up,WorldNormal).GetSafeNormal();
    const float SideExtent=float(FVector::DotProduct(Side.GetAbs(),Bounds.BoxExtent));
    const float UpExtent=float(FVector::DotProduct(Up.GetAbs(),Bounds.BoxExtent));
    constexpr int32 SideCount=9;
    for(int32 Y=0;Y<SideCount;++Y)for(int32 X=0;X<SideCount;++X) {
        const FVector Candidate=Bounds.Origin+Side*((2.f*X/(SideCount-1)-1.f)*SideExtent)
            +Up*((2.f*Y/(SideCount-1)-1.f)*UpExtent);
        FVector Point=Candidate,Normal=WorldNormal;
        const bool Hit=ProjectSurface(Candidate,WorldNormal,Point,Normal);
        SurfacePointsLocal.Add(Surface.InverseTransformPosition(Point));
        SurfaceNormalsLocal.Add(Surface.InverseTransformVectorNoScale(Normal).GetSafeNormal());SurfaceValid.Add(Hit?1.f:0.f);
    }
}
void UMCToothCareFeedbackComponent::AppendNativeInputs(bool Completion,double Now,const FTransform& Frame,FMCVFXNativeInputs& Out)
{
    const FTransform Surface=Tooth->Visual->GetComponentTransform();
    const FVector Normal=Frame.GetUnitAxis(EAxis::Z);RefreshSurfaceSamples(Normal);
    auto Add=[&](const TCHAR* Name,EMCVFXNativeInputType Type)->FMCVFXNativeInput& {
        auto& Input=Out.Entries.AddDefaulted_GetRef();Input.Name=FName(Name);Input.Type=Type;return Input;
    };
    Add(TEXT("User.Native.CareAcceptedAge"),EMCVFXNativeInputType::Float).FloatValue=
        FMath::Max(0.f,float(Now-(Completion?State.CompletedAt:State.ContactStartedAt)));
    Add(TEXT("User.Native.CareLastWorkAge"),EMCVFXNativeInputType::Float).FloatValue=FMath::Max(0.f,float(Now-State.WorkAt));
    Add(TEXT("User.Native.CareSerial"),EMCVFXNativeInputType::Int).IntValue=Completion?State.CompletionSerial:State.WorkSerial;
    Add(TEXT("User.Native.CareContactPosition"),EMCVFXNativeInputType::Position).PositionValue=Frame.GetLocation();
    Add(TEXT("User.Native.CareContactNormal"),EMCVFXNativeInputType::Vector).VectorValue=Normal;
    Add(TEXT("User.Native.CareFrameRotation"),EMCVFXNativeInputType::Quaternion).QuaternionValue=Frame.GetRotation();
    Add(TEXT("User.Native.CareSurfaceSide"),EMCVFXNativeInputType::Int).IntValue=9;
    const int32 PointsIndex=Out.Entries.Num();Add(TEXT("User.Native.CareSurfacePointsLocal"),EMCVFXNativeInputType::VectorArray);
    const int32 NormalsIndex=Out.Entries.Num();Add(TEXT("User.Native.CareSurfaceNormalsLocal"),EMCVFXNativeInputType::VectorArray);
    Add(TEXT("User.Native.CareSurfaceValid"),EMCVFXNativeInputType::FloatArray).FloatValues=SurfaceValid;
    for(int32 I=0;I<SurfacePointsLocal.Num();++I) {
        Out.Entries[PointsIndex].VectorValues.Add(Frame.InverseTransformPosition(Surface.TransformPosition(SurfacePointsLocal[I])));
        Out.Entries[NormalsIndex].VectorValues.Add(Frame.InverseTransformVectorNoScale(Surface.TransformVectorNoScale(SurfaceNormalsLocal[I])).GetSafeNormal());
    }
    const auto Bounds=Tooth->Visual->Bounds;
    const FVector ChartNormal=Surface.TransformVectorNoScale(SampledNormal).GetSafeNormal();
    const FVector Up=FVector::VectorPlaneProject(FVector::UpVector,ChartNormal).GetSafeNormal(SMALL_NUMBER,FVector::RightVector);
    const FVector Side=FVector::CrossProduct(Up,ChartNormal).GetSafeNormal();
    const float HalfWidth=float(FVector::DotProduct(Side.GetAbs(),Bounds.BoxExtent)),HalfHeight=float(FVector::DotProduct(Up.GetAbs(),Bounds.BoxExtent));
    const FVector ContactOffset=Frame.GetLocation()-Bounds.Origin;
    Add(TEXT("User.Native.CareContactU"),EMCVFXNativeInputType::Float).FloatValue=.5f+.5f*float(FVector::DotProduct(ContactOffset,Side))/FMath::Max(1.f,HalfWidth);
    Add(TEXT("User.Native.CareContactV"),EMCVFXNativeInputType::Float).FloatValue=.5f+.5f*float(FVector::DotProduct(ContactOffset,Up))/FMath::Max(1.f,HalfHeight);
    Add(TEXT("User.Native.CareHalfWidth"),EMCVFXNativeInputType::Float).FloatValue=HalfWidth;
    Add(TEXT("User.Native.CareHalfHeight"),EMCVFXNativeInputType::Float).FloatValue=HalfHeight;
    // Ordinary stock Direct Set array inputs consume the same complete 9x9
    // geometry snapshot. Selection/interpolation remain editable in Niagara.
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
        P.Shape=2;P.bCompletion=true;P.Color=MCToothCareFX::Tint(State.CompletionKind);AddParticle(P);
    }
    // Pearls depart in 3D with a separate, slightly longer tail than the glints.
    for(int32 I=0;I<6;++I) {
        FParticle P;P.Position=Contact+N*5+Side*Random.FRandRange(-18.f,18.f)*Scale;
        P.Velocity=N*Random.FRandRange(18.f,35.f)+Up*Random.FRandRange(18.f,38.f)+Side*Random.FRandRange(-14.f,14.f);
        P.Born=State.CompletedAt+.04+I*.025;P.Life=Random.FRandRange(.45f,.68f);P.Size=Random.FRandRange(5.f,9.f)*Scale;
        P.Shape=I%3==0?1:0;P.bCompletion=true;P.Color=MCToothCareFX::Tint(State.CompletionKind);AddParticle(P);
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
        if(P.bCompletion?!bNativeCompletion:!bNativeContact)continue;
        const float Age=float(Now-P.Born);if(Age<0 || Age>P.Life) continue;
        const float T=Age/P.Life;
        const float Alpha=FMath::SmoothStep(0.f,.16f,T)*(1.f-FMath::SmoothStep(.48f,1.f,T));
        const float Size=P.Size*FMath::Lerp(.65f,1.10f,FMath::SmoothStep(0.f,1.f,T));
        FVector Position=P.bOnSurface?Tooth->Visual->GetComponentTransform().TransformPosition(P.Position)
            :P.Position+P.Velocity*Age+FVector(0,0,7.f*Age*Age);
        FLinearColor Color=P.Color;Color.A=Alpha;Quad(Position,Right*Size,Up*Size,P.Shape,Color);
    }
    const float CompletionAge=float(Now-State.CompletedAt);
    if(bNativeCompletion && State.CompletionSerial>0 && CompletionAge>.06f && CompletionAge<1.06f) {
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
    if(!Tooth || !Tooth->IsAvailable()) {
        if(Composer){Composer->StopStates(TEXT("EditorOnly.CleanArenaTooth"));Composer->StopStates(TEXT("EditorOnly.RepairArenaTooth"));}
        if(Presentation)Presentation->SetVisibility(false);Particles.Reset();SetComponentTickEnabled(false);return;
    }
    const double Now=Clock();Particles.RemoveAll([Now](const FParticle& P){return Now>P.Born+P.Life;});
    UpdateComposer(Now);
    const bool Working=State.WorkSerial>0 && Now>=State.WorkAt && Now-State.WorkAt<.24;
    if(Working && bNativeContact && Presentation) {
        EmitRemainder+=FMath::Clamp(Dt,0.f,.1f)*12.f;
        for(int32 I=0;I<2 && EmitRemainder>=1.f;++I) {EmitRemainder-=1.f;EmitProcess(Now);}
    } else EmitRemainder=0;
    if(Presentation)Draw(Now);
    if(!Working && Particles.IsEmpty() && Now-State.CompletedAt>1.15) {if(Presentation)Presentation->SetVisibility(false);SetComponentTickEnabled(false);}
}
void UMCToothCareFeedbackComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if(Composer){Composer->StopStates(TEXT("EditorOnly.CleanArenaTooth"));Composer->StopStates(TEXT("EditorOnly.RepairArenaTooth"));}
    if(Presentation) {Presentation->DestroyComponent();Presentation=nullptr;}
    Particles.Reset();RetainedMaterial=nullptr;Super::EndPlay(Reason);
}
void UMCToothCareFeedbackComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(UMCToothCareFeedbackComponent,State);}
