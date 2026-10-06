#include "MCDeliveryZoneVisualComponent.h"
#include "MCDeliveryZoneSurface.h"
#include "MCFoodActor.h"
#include "MCThroat.h"
#include "MCTongue.h"
#include "MCGameState.h"
#include "MCDeliveryZoneCaptionWidget.h"
#include "ProceduralMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "UObject/ConstructorHelpers.h"

namespace MCDeliveryGuide
{
struct FMesh
{
    TArray<FVector> Positions,Normals;
    TArray<FVector2D> UV;
    TArray<int32> Triangles;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    void Quad(const FVector& A,const FVector& B,const FVector& C,const FVector& D,float Alpha,float OtherAlpha=-1)
    {
        const int32 Start=Positions.Num();Positions.Append({A,B,C,D});
        Triangles.Append({Start,Start+1,Start+2,Start,Start+2,Start+3});
        for(int32 I=0;I<4;++I) Colors.Add(FLinearColor(1,1,1,OtherAlpha>=0 && (I==1 || I==2)?OtherAlpha:Alpha));
    }
    void Surface(const FSurfaceCache& Cache,const FSurfaceOverlay& Overlay,const FTransform& Transform)
    {
        Cache.Positions(Overlay,Transform,Positions);Triangles=Overlay.Indices;Colors.Reserve(Overlay.Vertices.Num());
        UV.Reserve(Overlay.Vertices.Num());
        for(const auto& V:Overlay.Vertices) {Colors.Add(FLinearColor(1,1,1,V.Alpha));UV.Add(V.UV);}
    }
    void Apply(UProceduralMeshComponent* Mesh)
    {
        auto* Existing=Mesh->GetProcMeshSection(0);
        bool Same=Existing && Existing->ProcVertexBuffer.Num()==Positions.Num() && Existing->ProcIndexBuffer.Num()==Triangles.Num();
        // Layout changes can change connectivity without changing the counts.
        if(Same) for(int32 I=0;I<Triangles.Num();++I) if(Existing->ProcIndexBuffer[I]!=uint32(Triangles[I])) {Same=false;break;}
        if(Same) Mesh->UpdateMeshSection_LinearColor(0,Positions,Normals,UV,Colors,Tangents,false);
        else Mesh->CreateMeshSection_LinearColor(0,Positions,Triangles,Normals,UV,Colors,Tangents,false,false);
    }
};
}

UMCDeliveryZoneVisualComponent::UMCDeliveryZoneVisualComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bAllowTickOnDedicatedServer=false;
    PrimaryComponentTick.TickGroup=TG_PostPhysics;
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Material(
        TEXT("/Game/Gameplay/Delivery/M_DeliveryZoneGuide.M_DeliveryZoneGuide"),LOAD_Quiet|LOAD_NoWarn);
    GuideMaterial=Material.Get();
}

UProceduralMeshComponent* UMCDeliveryZoneVisualComponent::MakeMesh(FName Name)
{
    auto* Mesh=NewObject<UProceduralMeshComponent>(GetOwner(),Name);Mesh->SetupAttachment(GetOwner()->GetRootComponent());
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetGenerateOverlapEvents(false);
    Mesh->SetCanEverAffectNavigation(false);Mesh->SetCastShadow(false);Mesh->SetReceivesDecals(false);
    Mesh->TranslucencySortPriority=3;GetOwner()->AddInstanceComponent(Mesh);Mesh->RegisterComponent();return Mesh;
}

void UMCDeliveryZoneVisualComponent::BeginPlay()
{
    Super::BeginPlay();Zone=Cast<AMCFoodDisposal>(GetOwner());
    if(GetNetMode()==NM_DedicatedServer || !Zone.IsValid() || !GuideMaterial) {SetComponentTickEnabled(false);return;}
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) {Tongue=*It;AddTickPrerequisiteActor(*It);break;}
    SurfaceCache=MakeShared<MCDeliveryGuide::FSurfaceCache>();
    if(auto* Throat=Cast<AMCThroat>(Zone.Get())) SurfaceCache->DepthGuard.Bind(Throat->AuthoredMouth);
    Floor=MakeMesh(TEXT("DeliveryFloorGuide"));Arrows=MakeMesh(TEXT("DeliveryArrowGuide"));Arrows->TranslucencySortPriority=4;
    FloorMID=UMaterialInstanceDynamic::Create(GuideMaterial,this);ArrowMID=UMaterialInstanceDynamic::Create(GuideMaterial,this);
    FloorMID->SetScalarParameterValue(TEXT("ArrowMaskEnabled"),0);
    ArrowMID->SetScalarParameterValue(TEXT("ArrowMaskEnabled"),1);
    Floor->SetMaterial(0,FloorMID);Arrows->SetMaterial(0,ArrowMID);
    Caption=NewObject<UWidgetComponent>(GetOwner(),TEXT("DeliveryCaption"));Caption->SetupAttachment(GetOwner()->GetRootComponent());
    Caption->SetCollisionEnabled(ECollisionEnabled::NoCollision);Caption->SetCastShadow(false);
    Caption->SetWidgetSpace(EWidgetSpace::Screen);Caption->SetWidgetClass(UMCDeliveryZoneCaptionWidget::StaticClass());
    Caption->SetDrawSize(FVector2D(280,56));Caption->SetDrawAtDesiredSize(true);Caption->SetPivot(FVector2D(.5,.5));
    Caption->SetGenerateOverlapEvents(false);Caption->SetCanEverAffectNavigation(false);
    GetOwner()->AddInstanceComponent(Caption);Caption->RegisterComponent();Caption->InitWidget();
    Zone->Label->SetVisibility(false);Rebuild();
}

void UMCDeliveryZoneVisualComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(Dt,TickType,ThisTickFunction);if(!Zone.IsValid() || !Floor || !Arrows) return;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();const float Time=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    FloorMID->SetVectorParameterValue(TEXT("ZoneColor"),Zone->bBrushBin?FLinearColor(1,.11f,.08f):FLinearColor(.06f,1,.40f));
    ArrowMID->SetVectorParameterValue(TEXT("ZoneColor"),Zone->bBrushBin?FLinearColor(1,.20f,.20f):FLinearColor(.06f,1,.50f));
    ArrowMID->SetScalarParameterValue(TEXT("GlowStrength"),2.6f+.35f*FMath::Sin(Time*2*PI/FMath::Max(.5f,ArrowPeriod)));
    ArrowMID->SetScalarParameterValue(TEXT("ArrowCycle"),FMath::Frac(Time/FMath::Max(.5f,ArrowPeriod)));
    const bool Yawning=Tongue.IsValid() && Tongue->IsYawnActive();Floor->SetVisibility(!Yawning);Arrows->SetVisibility(!Yawning);
    const auto* Viewer=GetWorld()->GetFirstPlayerController();
    const bool CloseView=Viewer && Viewer->PlayerCameraManager
        && FVector::DistSquared(Viewer->PlayerCameraManager->GetCameraLocation(),Caption->GetComponentLocation())<FMath::Square(2200.f);
    Caption->SetVisibility(!Yawning && CloseView);
    bool Wrong=false;
    if(const auto* Throat=Cast<AMCThroat>(Zone.Get())) {Wrong=Throat->ThroatPhase==EMCThroatPhase::Spasm || Throat->ThroatPhase==EMCThroatPhase::Vomiting;Throat->ZoneRing->SetVisibility(false);}
    if(auto* Widget=Cast<UMCDeliveryZoneCaptionWidget>(Caption->GetUserWidgetObject())) Widget->SetDeliveryCaption(Zone->bBrushBin,Wrong);
    if(Yawning) return;
    // Fixed footprints follow the tongue; the material animates the chevrons.
    Rebuild();
}

void UMCDeliveryZoneVisualComponent::Rebuild()
{
    FTransform Geometry;FVector Extent;bool Circular=false;Zone->GetDeliveryZoneGeometry(Geometry,Extent,Circular);
    const FTransform MeshTransform=Floor->GetComponentTransform();
    TArray<FVector> Outer,Inner;const bool HasCap=Zone->GetDeliveryZoneOutline(Outer,Inner) && Outer.Num()>1 && Outer.Num()==Inner.Num();
    const FVector Direction=Zone->GetDeliveryDirection(),Side=FVector::CrossProduct(FVector::UpVector,Direction);
    bool NewArrows=!bGuidesBuilt || HasCap!=bCachedCap || Direction!=CachedDirection;
    const bool GeometryChanged=!bGuidesBuilt || !Geometry.Equals(CachedGeometry) || Extent!=CachedExtent || Circular!=bCachedCircular;
    bool SurfaceChanged=false,Native=false;
    if(Tongue.IsValid()) {SurfaceChanged=SurfaceCache->Refresh(Tongue.Get());Native=SurfaceCache->bReady && HasCap;}
    if(Tongue.IsValid() && !Native) {Floor->SetVisibility(false);Arrows->SetVisibility(false);return;}
    const bool UpdatePositions=!bGuidesBuilt || !MeshTransform.Equals(CachedMeshTransform) || SurfaceCache->bPositionsChanged;
    if(Native)
    {
        const bool OutlineChanged=Outer!=SurfaceCache->FloorOuter || Inner!=SurfaceCache->FloorInner;
        NewArrows|=SurfaceChanged || OutlineChanged;
        const bool NewFloor=SurfaceChanged || OutlineChanged || FillOpacity!=SurfaceCache->FloorOpacity;
        if(NewFloor)
        {
            SurfaceCache->Floor.Reset();SurfaceCache->FloorOuter=Outer;SurfaceCache->FloorInner=Inner;SurfaceCache->FloorOpacity=FillOpacity;
            constexpr int32 Columns=8;
            for(int32 Row=0;Row+1<Outer.Num();++Row) for(int32 Column=0;Column<Columns;++Column)
            {
                const float T0=float(Column)/Columns,T1=float(Column+1)/Columns;
                SurfaceCache->ClipQuad(SurfaceCache->Floor,FMath::Lerp(Outer[Row],Inner[Row],T0),FMath::Lerp(Outer[Row],Inner[Row],T1),
                    FMath::Lerp(Outer[Row+1],Inner[Row+1],T1),FMath::Lerp(Outer[Row+1],Inner[Row+1],T0),
                    FillOpacity*FMath::Pow(1-T0,1.5f),FillOpacity*FMath::Pow(1-T1,1.5f));
            }
            SurfaceCache->AddRim(SurfaceCache->Floor,Outer,Inner);
            SurfaceCache->Floor.Compact();
            MCDeliveryGuide::FMesh Fill;Fill.Surface(*SurfaceCache,SurfaceCache->Floor,MeshTransform);Fill.Apply(Floor);
        }
        else if(UpdatePositions)
        {
            SurfaceCache->Positions(SurfaceCache->Floor,MeshTransform,FloorPositions);
            Floor->UpdateMeshSection_LinearColor(0,FloorPositions,{}, {}, {}, {},false);
        }
    }
    else
    {
        // A replacement map with no native tongue retains its flat authored guide.
        NewArrows|=GeometryChanged;
        MCDeliveryGuide::FMesh Fill;
        for(int32 Row=0;Row<16;++Row)
        {
            const float Y0=FMath::Lerp(-Extent.Y,Extent.Y,Row/16.f),Y1=FMath::Lerp(-Extent.Y,Extent.Y,(Row+1)/16.f);
            const float X0=Circular?FMath::Sqrt(FMath::Max(0.f,Extent.X*Extent.X-Y0*Y0)):Extent.X;
            const float X1=Circular?FMath::Sqrt(FMath::Max(0.f,Extent.X*Extent.X-Y1*Y1)):Extent.X;
            auto P=[&](float X,float Y){return MeshTransform.InverseTransformPosition(Geometry.TransformPosition(FVector(X,Y,6)));};
            Fill.Quad(P(-X0,Y0),P(X0,Y0),P(X1,Y1),P(-X1,Y1),FillOpacity*.5f);
        }
        Fill.Apply(Floor);
    }
    if(NewArrows)
    {
        MCDeliveryGuide::FMesh ArrowMesh;MCDeliveryGuide::FSurfaceOverlay Strokes;
        constexpr float Scale=2.4f;
        // Both chevrons, including their halo and the entire +/-45 cm sweep,
        // share one fixed patch per row. Clip against native faces only here.
        const float Travel=HasCap?45.f:0.f;
        const FVector2D Corners[]={FVector2D(-177.6f-Travel,-108),FVector2D(69.6f+Travel,-108),
            FVector2D(69.6f+Travel,108),FVector2D(-177.6f-Travel,108)};
        ArrowMID->SetScalarParameterValue(TEXT("ArrowTravelDistance"),Travel*2);
        for(int32 Row=0;Row<5;++Row)
        {
            FVector Center;
            if(HasCap)
            {
                const float Sample=FMath::Lerp(.12f,.88f,Row/4.f)*(Outer.Num()-1);const int32 Index=FMath::Min(FMath::FloorToInt(Sample),Outer.Num()-2);const float T=Sample-Index;
                Center=FMath::Lerp(FMath::Lerp(Inner[Index],Inner[Index+1],T),FMath::Lerp(Outer[Index],Outer[Index+1],T),.63f);
            }
            else Center=Geometry.TransformPosition(FVector(0,FMath::Lerp(-Extent.Y*.78f,Extent.Y*.78f,Row/4.f),0));
            FVector P[4];for(int32 I=0;I<4;++I) P[I]=Center+Direction*Corners[I].X+Side*Corners[I].Y;
            if(Native) SurfaceCache->ClipTexturedQuad(Strokes,P[0],P[1],P[2],P[3],Center,Direction,Side,Scale,6);
            else
            {
                ArrowMesh.Quad(MeshTransform.InverseTransformPosition(P[0]+FVector(0,0,12)),MeshTransform.InverseTransformPosition(P[1]+FVector(0,0,12)),
                    MeshTransform.InverseTransformPosition(P[2]+FVector(0,0,12)),MeshTransform.InverseTransformPosition(P[3]+FVector(0,0,12)),1);
                for(const auto& Corner:Corners) ArrowMesh.UV.Add(Corner/Scale);
            }
        }
        if(Native)
        {
            SurfaceCache->Arrows=MoveTemp(Strokes);
            SurfaceCache->Arrows.Compact();
            ArrowMesh.Surface(*SurfaceCache,SurfaceCache->Arrows,MeshTransform);
        }
        ArrowMesh.Apply(Arrows);
    }
    else if(Native && UpdatePositions)
    {
        SurfaceCache->Positions(SurfaceCache->Arrows,MeshTransform,ArrowPositions);
        Arrows->UpdateMeshSection_LinearColor(0,ArrowPositions,{}, {}, {}, {},false);
    }
    if(UpdatePositions || GeometryChanged || NewArrows)
    {
        FVector LabelPoint=HasCap?FMath::Lerp(Inner[Inner.Num()/2],Outer[Outer.Num()/2],.35f):Geometry.GetLocation();FHitResult Hit;
        if(Tongue.IsValid() && Tongue->SurfacePoint(LabelPoint,Hit)) LabelPoint=Hit.ImpactPoint;
        Caption->SetWorldLocation(LabelPoint+FVector(0,0,110));
    }
    bGuidesBuilt=true;CachedGeometry=Geometry;CachedMeshTransform=MeshTransform;CachedExtent=Extent;CachedDirection=Direction;bCachedCircular=Circular;bCachedCap=HasCap;
}

void UMCDeliveryZoneVisualComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if(SurfaceCache) SurfaceCache->DepthGuard.Restore();
    for(auto* Mesh:{Floor.Get(),Arrows.Get()}) if(IsValid(Mesh)) Mesh->DestroyComponent();
    if(IsValid(Caption)) Caption->DestroyComponent();SurfaceCache.Reset();Super::EndPlay(Reason);
}
