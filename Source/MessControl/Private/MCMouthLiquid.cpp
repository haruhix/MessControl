#include "MCMouthSurface.h"
#include "MCPerkComponent.h"
#include "MCCoffeeWipe.h"
#include "MCInventoryComponent.h"
#include "MCTongue.h"
#include "MCDirtPlacement.h"
#include "MCToothCharacter.h"
#include "MCBrushContactComponent.h"
#include "MCToothStatusComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/App.h"
#include "Components/BoxComponent.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

static TAutoConsoleVariable<int32> CVarMCLiquidRender(TEXT("mc.LiquidRender"),1,
    TEXT("Render and update liquid stain visuals. 0 disables visuals for A/B profiling; gameplay remains active."));
static TAutoConsoleVariable<float> CVarMCLiquidMeshHz(TEXT("mc.LiquidMeshHz"),60.f,
    TEXT("Maximum ordinary puddle mesh updates per second. 0 updates every frame. Paint follows each tongue geometry change; brush masks and gameplay are independent."));

namespace
{
constexpr double PaintSurfaceOffset=.6;
struct FPaintClipVertex
{
    FVector Point;
    FVector Bary;
};
using FPaintClipPolygon=TArray<FPaintClipVertex,TInlineAllocator<8>>;

// Clip the actual tissue triangle, carrying its barycentric coordinates through
// every intersection. Subdividing a separate grid still cuts across tissue ridges.
void ClipPaintPolygon(FPaintClipPolygon& Polygon,int32 Axis,double Boundary,bool KeepGreater)
{
    if(Polygon.IsEmpty()) return;
    FPaintClipPolygon Clipped;
    const auto AddDistinct=[&](const FPaintClipVertex& Vertex)
    {
        if(Clipped.IsEmpty() || !Clipped.Last().Point.Equals(Vertex.Point,1.e-8)) Clipped.Add(Vertex);
    };
    FPaintClipVertex Previous=Polygon.Last();
    double PreviousDistance=(Previous.Point[Axis]-Boundary)*(KeepGreater?1.:-1.);
    for(const FPaintClipVertex& Current:Polygon)
    {
        const double Distance=(Current.Point[Axis]-Boundary)*(KeepGreater?1.:-1.);
        if((Distance>=0)!=(PreviousDistance>=0))
        {
            const double Alpha=FMath::Clamp(PreviousDistance/(PreviousDistance-Distance),0.,1.);
            FPaintClipVertex Intersection{FMath::Lerp(Previous.Point,Current.Point,Alpha),FMath::Lerp(Previous.Bary,Current.Bary,Alpha)};
            Intersection.Point[Axis]=Boundary;
            AddDistinct(Intersection);
        }
        if(Distance>=0) AddDistinct(Current);
        Previous=Current; PreviousDistance=Distance;
    }
    if(Clipped.Num()>1 && Clipped[0].Point.Equals(Clipped.Last().Point,1.e-8)) Clipped.SetNum(Clipped.Num()-1,EAllowShrinking::No);
    Polygon=MoveTemp(Clipped);
}
}

bool AMCMouthSurface::AffectsFooting(FVector Sole) const
{
    if (bUlcer || IsClean() || GroundResponse==EMCGroundSurface::Normal || LiquidHalfSize<=0) return false;
    const FVector P=GetActorTransform().InverseTransformPosition(Sole);
    FHitResult Floor;
    const bool Close=bPaintTile && Tongue && Tongue->SurfacePoint(Sole,Floor)
        ? FMath::Abs(Sole.Z-Floor.ImpactPoint.Z)<18 : FMath::Abs(P.Z)<18;
    return Close && FMCCoffeeWipe::WetAt(WipeMask,FVector2D(P)/(2*LiquidHalfSize)+FVector2D(.5),LiquidSeed,bPaintTile);
}

void AMCMouthSurface::SetLiquidAppearance(UMaterialInterface* Preset)
{
    if (!HasAuthority() || !Preset) return;
    LiquidMaterial=Preset; OnRep_LiquidMaterial(); ForceNetUpdate();
}
void AMCMouthSurface::OnRep_LiquidMaterial()
{
    LiquidMID=nullptr; WipeTexture=nullptr; bWipeDirty=true;
}

void AMCMouthSurface::OnRep_LiquidSize()
{
    LiquidHalfSize=FMath::IsFinite(LiquidHalfSize)?FMath::Clamp(LiquidHalfSize,20.f,1600.f):240.f;
    if (!bUlcer) Area->SetBoxExtent(FVector(LiquidHalfSize*(bPaintTile?1.f:.75f),LiquidHalfSize*(bPaintTile?1.f:.75f),bPaintTile?120.f:6.f));
    LiquidVertices.Reset();PaintSupportMask.Reset();bRemainingLiquidDirty=true;
}

void AMCMouthSurface::ResetLiquid()
{
    if (!HasAuthority()) return;
    if (LiquidDepositMask.Num()==FMCCoffeeWipe::Count) WipeMask=LiquidDepositMask;
    else FMCCoffeeWipe::Reset(WipeMask);
    if(bPaintTile && EnsurePaintSupport()) for(int32 I=0;I<WipeMask.Num();++I)
        if(!PaintSupportMask[I]) WipeMask[I]=0;
    WipeFractional.Reset(); PreviousBrush.Reset(); BrushCenters.Reset(); Finish=0; BrushAt=-100; BrushClock=0;
    OnRep_Wipe(); ForceNetUpdate();
}
void AMCMouthSurface::SetLiquidDepositMask(const TArray<uint8>& Mask)
{
    if (!HasAuthority() || Mask.Num()!=FMCCoffeeWipe::Count) return;
    LiquidDepositMask=Mask; ResetLiquid();
}
bool AMCMouthSurface::AppendLiquidDepositMask(const TArray<uint8>& Mask)
{
    if(!HasAuthority() || !bPaintTile || Mask.Num()!=FMCCoffeeWipe::Count) return false;
    if(!EnsurePaintSupport()) return false;
    if(LiquidDepositMask.Num()!=FMCCoffeeWipe::Count) LiquidDepositMask.Init(0,FMCCoffeeWipe::Count);
    if(WipeMask.Num()!=FMCCoffeeWipe::Count) WipeMask.Init(0,FMCCoffeeWipe::Count);
    bool Changed=false;
    for(int32 I=0;I<Mask.Num();++I) if(PaintSupportMask[I] && Mask[I]>LiquidDepositMask[I])
    {
        // The deposit ledger is monotonic; a stationary/passed wave never fills
        // a brush stroke again. Only genuinely new footprint may add liquid.
        const int32 Added=Mask[I]-LiquidDepositMask[I];
        LiquidDepositMask[I]=Mask[I];
        WipeMask[I]=FMath::Min(255,int32(WipeMask[I])+Added);
        if(WipeFractional.IsValidIndex(I)) WipeFractional[I]=0;
        Changed=true;
    }
    if(Changed) {bPaintDepositPending=true;Finish=0;OnRep_Wipe();FlushPaintDeposit();}
    return Changed;
}
void AMCMouthSurface::InitializePaintTile(AMCTongue* InTongue)
{
    if(!HasAuthority() || !InTongue) return;
    bPaintTile=true;Tongue=InTongue;
    TongueAnchor=InTongue->GetActorTransform().InverseTransformPosition(GetActorLocation());
    LiquidDepositMask.Init(0,FMCCoffeeWipe::Count);WipeMask=LiquidDepositMask;
    PaintSupportMask.Reset();
    OnRep_LiquidSize();
}
bool AMCMouthSurface::EnsurePaintSupport()
{
    if(!bPaintTile || !IsValid(Tongue) || LiquidHalfSize<=0) return false;
    const auto& Vertices=Tongue->CurrentWorldVertices();
    const auto& Indices=Tongue->TriangleIndices();
    const FProcMeshSection* Section=Tongue->Surface->GetProcMeshSection(0);
    if(!Section || Section->ProcVertexBuffer.Num()!=Vertices.Num() || Indices.IsEmpty()) return false;
    const FTransform TileTransform=GetActorTransform(),TissueTransform=Tongue->Surface->GetComponentTransform();
    FTransform PlanarTileTransform=TileTransform;
    FVector PlanarOrigin=PlanarTileTransform.GetTranslation();PlanarOrigin.Z=0;PlanarTileTransform.SetTranslation(PlanarOrigin);
    if(PaintSupportMask.Num()==FMCCoffeeWipe::Count && PaintSupportSource.Get()==Tongue->SourceMesh.Get()
        && PaintSupportVertexCount==Vertices.Num() && PaintSupportIndexCount==Indices.Num()
        && PaintSupportTileTransform.Equals(PlanarTileTransform) && PaintSupportTongueTransform.Equals(TissueTransform)) return true;
    PaintSupportMask.Init(0,FMCCoffeeWipe::Count);
    const FBox DirtWorkBounds=MCDirtPlacement::WorkBounds(Tongue,65.f);
    // Rasterize the same upward triangles used by BuildLiquid, once per canvas.
    // Tongue waves change Z, so their ordinary deformation needs no new rays,
    // collision cooks or per-frame 64x64 scan.
    for(int32 Face=0;Face+2<Indices.Num();Face+=3) {
        const int32 IA=Indices[Face],IB=Indices[Face+1],IC=Indices[Face+2];
        if(!Vertices.IsValidIndex(IA) || !Vertices.IsValidIndex(IB) || !Vertices.IsValidIndex(IC)) continue;
        const FVector A=Vertices[IA],B=Vertices[IB],C=Vertices[IC];
        const FVector Normal=TissueTransform.TransformVectorNoScale(Section->ProcVertexBuffer[IA].Normal
            +Section->ProcVertexBuffer[IB].Normal+Section->ProcVertexBuffer[IC].Normal);
        FVector FaceNormal=FVector::CrossProduct(B-A,C-A);
        if(FVector::DotProduct(FaceNormal,Normal)<0) FaceNormal=-FaceNormal;
        if(FaceNormal.Z<=UE_SMALL_NUMBER) continue;
        const auto UV=[&](FVector P) {return FVector2D(TileTransform.InverseTransformPosition(P))/(2*LiquidHalfSize)+FVector2D(.5);};
        const FVector2D U=UV(A),V=UV(B),W=UV(C);
        const double Det=(V.Y-W.Y)*(U.X-W.X)+(W.X-V.X)*(U.Y-W.Y);
        if(FMath::Abs(Det)<1.e-12) continue;
        const int32 MinX=FMath::Max(0,FMath::CeilToInt(FMath::Min3(U.X,V.X,W.X)*FMCCoffeeWipe::Size-.5));
        const int32 MaxX=FMath::Min(FMCCoffeeWipe::Size-1,FMath::FloorToInt(FMath::Max3(U.X,V.X,W.X)*FMCCoffeeWipe::Size-.5));
        const int32 MinY=FMath::Max(0,FMath::CeilToInt(FMath::Min3(U.Y,V.Y,W.Y)*FMCCoffeeWipe::Size-.5));
        const int32 MaxY=FMath::Min(FMCCoffeeWipe::Size-1,FMath::FloorToInt(FMath::Max3(U.Y,V.Y,W.Y)*FMCCoffeeWipe::Size-.5));
        for(int32 Y=MinY;Y<=MaxY;++Y) for(int32 X=MinX;X<=MaxX;++X) {
            const FVector2D P((X+.5)/FMCCoffeeWipe::Size,(Y+.5)/FMCCoffeeWipe::Size);
            const double S=((V.Y-W.Y)*(P.X-W.X)+(W.X-V.X)*(P.Y-W.Y))/Det;
            const double T=((W.Y-U.Y)*(P.X-W.X)+(U.X-W.X)*(P.Y-W.Y))/Det;
            if(S>=-1.e-6 && T>=-1.e-6 && S+T<=1.+1.e-6)
            {
                const FVector Point=TileTransform.TransformPosition(FVector((P.X-.5)*2*LiquidHalfSize,(P.Y-.5)*2*LiquidHalfSize,0));
                if(DirtWorkBounds.IsValid && DirtWorkBounds.IsInsideXY(Point)) PaintSupportMask[Y*FMCCoffeeWipe::Size+X]=1;
            }
        }
    }
    PaintSupportSource=Tongue->SourceMesh;PaintSupportVertexCount=Vertices.Num();PaintSupportIndexCount=Indices.Num();
    PaintSupportTileTransform=PlanarTileTransform;PaintSupportTongueTransform=TissueTransform;
    return true;
}
bool AMCMouthSurface::DepositPaintStroke(FVector From,FVector To,float Radius,const TFunction<bool(FVector)>& CoverageAllowed)
{
    if(!HasAuthority() || !bPaintTile || LiquidHalfSize<=0 || From.ContainsNaN() || To.ContainsNaN()) return false;
    if(!EnsurePaintSupport()) return false;
    const FTransform Transform=GetActorTransform();
    const FVector2D A=FVector2D(Transform.InverseTransformPosition(From))/(2*LiquidHalfSize)+FVector2D(.5);
    const FVector2D B=FVector2D(Transform.InverseTransformPosition(To))/(2*LiquidHalfSize)+FVector2D(.5);
    TFunction<bool(FVector2D)> Allowed=[&](FVector2D UV) {
        const int32 X=FMath::Clamp(FMath::FloorToInt(UV.X*FMCCoffeeWipe::Size),0,FMCCoffeeWipe::Size-1);
        const int32 Y=FMath::Clamp(FMath::FloorToInt(UV.Y*FMCCoffeeWipe::Size),0,FMCCoffeeWipe::Size-1);
        return PaintSupportMask[Y*FMCCoffeeWipe::Size+X] && (!CoverageAllowed ||
            CoverageAllowed(Transform.TransformPosition(FVector((UV.X-.5)*2*LiquidHalfSize,(UV.Y-.5)*2*LiquidHalfSize,0))));
    };
    if(!FMCCoffeeWipe::Deposit(WipeMask,LiquidDepositMask,A,B,Radius/(2*LiquidHalfSize),&WipeFractional,Allowed)) return false;
    bPaintDepositPending=true;Finish=0;OnRep_Wipe();return true;
}
void AMCMouthSurface::FlushPaintDeposit()
{
    if(!HasAuthority() || !bPaintDepositPending || !bPaintTile) return;
    bPaintDepositPending=false;
    // ApplyCoffee intentionally resets a whole puddle. Restore only care state here.
    FMCToothStatus Next=Status->State;
    Next.CoffeeLeft=FMCCoffeeWipe::HasWetCells(WipeMask)?FMath::Max(1,FMath::CeilToInt(RemainingLiquid()*Next.CoffeeTotal)):0;
    if(Next.CoffeeLeft!=Status->State.CoffeeLeft) Status->Restore(Next);
    ForceNetUpdate();
}
bool AMCMouthSurface::IsPaintWetAt(FVector Point) const
{
    if(!bPaintTile || LiquidHalfSize<=0 || Point.ContainsNaN()) return false;
    const FVector2D UV=FVector2D(GetActorTransform().InverseTransformPosition(Point))/(2*LiquidHalfSize)+FVector2D(.5);
    return FMCCoffeeWipe::WetAt(WipeMask,UV,LiquidSeed,true,63);
}
bool AMCMouthSurface::GetNearestPaintPoint(FVector Near,FVector& Point,FVector& Normal) const
{
    if(!bPaintTile || IsClean() || Near.ContainsNaN()) return false;
    const FTransform Transform=GetActorTransform();float Best=MAX_flt;
    for(int32 Y=0;Y<FMCCoffeeWipe::Size;++Y) for(int32 X=0;X<FMCCoffeeWipe::Size;++X) {
        const FVector2D UV((X+.5)/FMCCoffeeWipe::Size,(Y+.5)/FMCCoffeeWipe::Size);
        if(!FMCCoffeeWipe::WetAt(WipeMask,UV,LiquidSeed,true,63)) continue;
        FVector Candidate=Transform.TransformPosition(FVector((UV.X-.5)*2*LiquidHalfSize,(UV.Y-.5)*2*LiquidHalfSize,0));
        const float Distance=FVector::DistSquared2D(Candidate,Near);if(Distance>=Best) continue;
        FHitResult Floor;
        if(!Tongue || !Tongue->SurfacePoint(Candidate,Floor)) continue;
        Best=Distance;Point=Floor.ImpactPoint;Normal=Floor.ImpactNormal;
    }
    return Best<MAX_flt;
}
void AMCMouthSurface::OnRep_Wipe() { bWipeDirty=true;bRemainingLiquidDirty=true; }

void AMCMouthSurface::ClearLiquidForBooster()
{
    if(!HasAuthority() || bUlcer) return;
    WipeMask.Init(0,FMCCoffeeWipe::Count);WipeFractional.Reset();
    Status->ClearCoffeeForBooster();
    OnRep_Wipe();ForceNetUpdate();
}
float AMCMouthSurface::RemainingLiquid() const
{
    if(!bRemainingLiquidDirty) return CachedRemainingLiquid;
    int32 Total=0; float Left=0;
    for(int32 Y=0;Y<FMCCoffeeWipe::Size;++Y) for(int32 X=0;X<FMCCoffeeWipe::Size;++X) {
        const FVector2D UV((X+.5)/FMCCoffeeWipe::Size,(Y+.5)/FMCCoffeeWipe::Size);
        if(!FMCCoffeeWipe::WetAt(LiquidDepositMask,UV,LiquidSeed,bPaintTile,bPaintTile?63:100)) continue;
        ++Total;
        const float Value=WipeMask.Num()==FMCCoffeeWipe::Count?WipeMask[Y*FMCCoffeeWipe::Size+X]/255.f:1.f;
        Left+=FMath::Clamp((Value-.25f)/.75f,0.f,1.f);
    }
    CachedRemainingLiquid=Total>0?Left/Total:0.f;bRemainingLiquidDirty=false;
    return CachedRemainingLiquid;
}
bool AMCMouthSurface::FindDirtyContact(AMCToothCharacter* Worker,FVector& Point,FVector& Normal) const
{
    if(!IsValid(Worker) || !Worker->BrushContact->CanAcquireSurface(this)) return false;
    const auto* Contact=Worker->BrushContact.Get();
    const FVector Origin=Worker->GetActorLocation();
    const FVector Aim=Contact->Target==this && Contact->Alpha()>.5f?Contact->ContactPoint():Origin+Worker->GetActorForwardVector()*65;
    float Best=MAX_flt;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCLiquidBrushTarget),false,Worker); Query.AddIgnoredActor(this);
    const TArray<uint8> Full;
    // Tiles include every cell: a thin surviving line may occupy only one texel.
    const int32 Stride=bPaintTile?1:2,Start=bPaintTile?0:1;
    int32 MinX=Start,MinY=Start,MaxX=FMCCoffeeWipe::Size-1,MaxY=MaxX;
    if(bPaintTile) {
        const FVector2D Center=FVector2D(GetActorTransform().InverseTransformPosition(Origin))/(2*LiquidHalfSize)+FVector2D(.5);
        const float ReachUV=(Contact->SurfaceReach+25)/(2*LiquidHalfSize);
        MinX=FMath::Clamp(FMath::FloorToInt((Center.X-ReachUV)*FMCCoffeeWipe::Size),0,MaxX);
        MinY=FMath::Clamp(FMath::FloorToInt((Center.Y-ReachUV)*FMCCoffeeWipe::Size),0,MaxY);
        MaxX=FMath::Clamp(FMath::CeilToInt((Center.X+ReachUV)*FMCCoffeeWipe::Size),0,MaxX);
        MaxY=FMath::Clamp(FMath::CeilToInt((Center.Y+ReachUV)*FMCCoffeeWipe::Size),0,MaxY);
    }
    for(int32 Y=MinY;Y<=MaxY;Y+=Stride) for(int32 X=MinX;X<=MaxX;X+=Stride) {
        const FVector2D UV((X+.5)/FMCCoffeeWipe::Size,(Y+.5)/FMCCoffeeWipe::Size);
        if(bPaintTile ? !FMCCoffeeWipe::WetAt(WipeMask,UV,LiquidSeed,true,63)
            : (!FMCCoffeeWipe::WetAt(Full,UV,LiquidSeed) || (WipeMask.Num()==FMCCoffeeWipe::Count && WipeMask[Y*FMCCoffeeWipe::Size+X]<=63))) continue;
        FVector P=GetActorTransform().TransformPosition(FVector((UV.X-.5)*2*LiquidHalfSize,(UV.Y-.5)*2*LiquidHalfSize,0));
        const float Score=FVector::DistSquared2D(P,Aim);
        if(Score>=Best || FVector::Dist2D(P,Origin)>Contact->SurfaceReach) continue;
        FVector N=GetActorUpVector(); FHitResult Floor;
        if(Tongue && Tongue->SurfacePoint(P,Floor)) { P=Floor.ImpactPoint+Floor.ImpactNormal*2; N=Floor.ImpactNormal; }
        if(!Contact->CanReachAfterFacing(P,N)) continue;
        FHitResult Block;
        if(GetWorld()->LineTraceSingleByChannel(Block,Origin+FVector(0,0,35),P+N*4,ECC_Visibility,Query)) continue;
        Best=Score; Point=P; Normal=N;
    }
    return Best<MAX_flt;
}
bool AMCMouthSurface::BrushLiquid(AMCToothCharacter* Worker,float Seconds)
{
    if (!HasAuthority() || bUlcer || IsClean() || !IsValid(Worker) || !Worker->bBrushing ||
        !Worker->HasBrush() || Worker->HeldFood || Worker->bInCoffee || !Worker->BrushContact->CanAcquireSurface(this) ||
        !FMath::IsFinite(Seconds) || Seconds<=0) return false;
    Seconds=FMath::Min(Seconds,.1f); BrushClock+=Seconds;
    FVector Point,Normal; if(!FindDirtyContact(Worker,Point,Normal)) return false;
    const auto* GS=GetWorld()->GetGameState();
    const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    auto* Contact=Worker->BrushContact.Get();
    auto& Center=BrushCenters.FindOrAdd(Worker);
    if(FVector::DotProduct(Worker->GetActorForwardVector(),(Point-Worker->GetActorLocation()).GetSafeNormal2D())<.85f
        || !Contact->CanReach(Point,Normal)) {
        Center.At=-100;
        Contact->Contact(this,Point,Normal);
        return true;
    }
    const FTransform Surface=GetActorTransform();
    // Interpolate the unstroked center. Feeding the previous sine offset back
    // into this interpolation accumulates motion differently at each FPS.
    if(Contact->Target==this && Now-Contact->ContactAt<.2 && Now-Center.At<.2)
        Point=FMath::VInterpConstantTo(Surface.TransformPosition(Center.Point),Point,Seconds,180.f);
    const FVector Facing=GetActorTransform().InverseTransformVectorNoScale(Worker->GetActorForwardVector());
    const FVector2D Forward=FVector2D(Facing).GetSafeNormal();
    const FVector2D Side(-Forward.Y,Forward.X);
    const FVector Base=Point;
    const FVector Stroke=GetActorTransform().TransformVectorNoScale(FVector(Side.X,Side.Y,0))*FMath::Sin(BrushClock*22)*6;
    Point+=Stroke;
    FHitResult Floor;
    if(Tongue && Tongue->SurfacePoint(Point,Floor)) { Point=Floor.ImpactPoint+Floor.ImpactNormal*2; Normal=Floor.ImpactNormal; }
    if(!Contact->CanReach(Point,Normal)) Point=Base;
    if(!Contact->CanReach(Point,Normal)) { Center.At=-100; return false; }
    Center.Point=Surface.InverseTransformPosition(Base); Center.At=Now;
    const FVector2D UV=FVector2D(GetActorTransform().InverseTransformPosition(Point))/(2*LiquidHalfSize)+FVector2D(.5);
    const FBrushStroke* Previous=PreviousBrush.Find(Worker);
    // The worker identity, live target and approach epoch must all continue.
    // A large canvas must not turn a resumed brush into a long erased bridge.
    const bool Continuing=Previous && Contact->Target==this && Now-Previous->At>=0 && Now-Previous->At<.2
        && Contact->ApproachStartedAt==Previous->ApproachStartedAt && Now-Contact->ContactAt<.2
        && FVector::DistSquared(Previous->WorldPoint,Point)<=FMath::Square(40.f);
    const FVector2D From=Continuing?Previous->UV:UV;
    Worker->BrushContact->Contact(this,Point,Normal,&Base);
    if(!Worker->BrushContact->IsWorkReady()) return true;
    if (FMCCoffeeWipe::Stroke(WipeMask,From,UV,Worker->Inventory->CleaningRadius(LiquidHalfSize)/(2*LiquidHalfSize),Seconds,&WipeFractional,Worker->Inventory->CleaningSpeedMultiplier())) {
        OnRep_Wipe();if(auto* Perks=UMCPerkComponent::ForActor(Worker)) Perks->NotifyCleaned(Point);
    }
    BrushDirection=(UV-From).IsNearlyZero()?Side:(UV-From).GetSafeNormal();
    BrushUV=UV;
    FBrushStroke& History=PreviousBrush.FindOrAdd(Worker);
    History.UV=UV;History.WorldPoint=Point;History.At=Now;History.ApproachStartedAt=Contact->ApproachStartedAt;
    BrushAt=Now;
    const float Left=RemainingLiquid();
    const bool Finished=bPaintTile?!FMCCoffeeWipe::HasWetCells(WipeMask):Left<.025f;
    const int32 RemainingLayers=Finished?0:FMath::Max(1,FMath::CeilToInt(Left*Status->State.CoffeeTotal));
    while(Status->State.CoffeeLeft>RemainingLayers) { Status->CareContact(true,Worker); ++Worker->SuccessfulBrushContacts; }
    return true;
}

void AMCMouthSurface::BuildLiquid()
{
    constexpr int32 Steps=20;
    LiquidVertices.Reset(); LiquidNormals.Reset(); LiquidUV.Reset(); LiquidTangents.Reset(); SurfaceBindings.Reset();
    TArray<int32> Triangles;
    const FTransform Transform=GetActorTransform();
    if(bPaintTile && Tongue)
    {
        const auto& Idx=Tongue->TriangleIndices();
        const auto& WorldVertices=Tongue->CurrentWorldVertices();
        const FProcMeshSection* Section=Tongue->Surface->GetProcMeshSection(0);
        const FTransform TT=Tongue->Surface->GetComponentTransform();
        BoundTongue=Tongue; BoundTongueSource=Tongue->SourceMesh.Get();
        BoundTongueVertexCount=WorldVertices.Num(); BoundTongueIndexCount=Idx.Num();
        PaintSurfaceRevision=MAX_uint64;
        if(!Section || Section->ProcVertexBuffer.Num()!=WorldVertices.Num()) { Liquid->ClearMeshSection(0); return; }
        for(int32 Face=0;Face+2<Idx.Num();Face+=3)
        {
            const int32 IA=Idx[Face],IB=Idx[Face+1],IC=Idx[Face+2];
            if(!WorldVertices.IsValidIndex(IA) || !WorldVertices.IsValidIndex(IB) || !WorldVertices.IsValidIndex(IC)) continue;
            const FVector A=WorldVertices[IA],B=WorldVertices[IB],C=WorldVertices[IC];
            const FVector NA=TT.TransformVectorNoScale(Section->ProcVertexBuffer[IA].Normal);
            const FVector NB=TT.TransformVectorNoScale(Section->ProcVertexBuffer[IB].Normal);
            const FVector NC=TT.TransformVectorNoScale(Section->ProcVertexBuffer[IC].Normal);
            FVector FaceNormal=FVector::CrossProduct(B-A,C-A);
            if(FVector::DotProduct(FaceNormal,NA+NB+NC)<0) FaceNormal=-FaceNormal;
            // Z displacement does not change projected winding. Include every
            // upward face even during a steep wave; omit underside/vertical faces.
            if(FaceNormal.Z<=UE_SMALL_NUMBER) continue;
            FPaintClipPolygon Polygon;
            Polygon.Add({Transform.InverseTransformPosition(A),FVector(1,0,0)});
            Polygon.Add({Transform.InverseTransformPosition(B),FVector(0,1,0)});
            Polygon.Add({Transform.InverseTransformPosition(C),FVector(0,0,1)});
            if(FMath::Max3(Polygon[0].Point.X,Polygon[1].Point.X,Polygon[2].Point.X)<-LiquidHalfSize
                || FMath::Min3(Polygon[0].Point.X,Polygon[1].Point.X,Polygon[2].Point.X)>LiquidHalfSize
                || FMath::Max3(Polygon[0].Point.Y,Polygon[1].Point.Y,Polygon[2].Point.Y)<-LiquidHalfSize
                || FMath::Min3(Polygon[0].Point.Y,Polygon[1].Point.Y,Polygon[2].Point.Y)>LiquidHalfSize) continue;
            ClipPaintPolygon(Polygon,0,-LiquidHalfSize,true);
            ClipPaintPolygon(Polygon,0,LiquidHalfSize,false);
            ClipPaintPolygon(Polygon,1,-LiquidHalfSize,true);
            ClipPaintPolygon(Polygon,1,LiquidHalfSize,false);
            if(Polygon.Num()<3) continue;
            double PolygonArea=0;
            for(int32 I=0;I<Polygon.Num();++I)
            {
                const FVector& P=Polygon[I].Point;
                const FVector& Q=Polygon[(I+1)%Polygon.Num()].Point;
                PolygonArea+=P.X*Q.Y-P.Y*Q.X;
            }
            if(FMath::Abs(PolygonArea)<=UE_SMALL_NUMBER) continue;
            const int32 First=LiquidVertices.Num();
            for(const FPaintClipVertex& Vertex:Polygon)
            {
                const FVector& W=Vertex.Bary;
                const FVector World=A*W.X+B*W.Y+C*W.Z;
                const FVector P=Transform.InverseTransformPosition(World+FVector(0,0,PaintSurfaceOffset));
                const FVector Normal=Transform.InverseTransformVectorNoScale((NA*W.X+NB*W.Y+NC*W.Z).GetSafeNormal());
                FSurfaceBinding Binding; Binding.Face=Face; Binding.Bary=W; Binding.Fallback=P;
                SurfaceBindings.Add(Binding); LiquidVertices.Add(P); LiquidNormals.Add(Normal);
                LiquidUV.Add(FVector2D(Vertex.Point)/(2*LiquidHalfSize)+FVector2D(.5));
                LiquidTangents.Add(FProcMeshTangent(FVector::VectorPlaneProject(FVector::ForwardVector,Normal).GetSafeNormal(),false));
            }
            // Preserve the source tongue's winding rather than the old grid's.
            for(int32 I=1;I+1<Polygon.Num();++I)
                if(FVector::CrossProduct(LiquidVertices[First+I]-LiquidVertices[First],LiquidVertices[First+I+1]-LiquidVertices[First]).SizeSquared()>UE_SMALL_NUMBER)
                    Triangles.Append({First,First+I,First+I+1});
        }
        Liquid->CreateMeshSection(0,LiquidVertices,Triangles,LiquidNormals,LiquidUV,TArray<FColor>(),LiquidTangents,false);
        if(!LiquidVertices.IsEmpty())
        {
            PaintSurfaceRevision=Tongue->SurfaceRevision();
            PaintGeometryTransform=Transform; PaintTongueTransform=TT;
        }
        return;
    }
    for (int32 Y=0;Y<=Steps;++Y) for (int32 X=0;X<=Steps;++X)
    {
        const FVector2D UV(double(X)/Steps,double(Y)/Steps);
        FVector P((UV.X*2-1)*LiquidHalfSize,(UV.Y*2-1)*LiquidHalfSize,-4.5);
        FVector Normal=FVector::UpVector; FSurfaceBinding Binding;
        FHitResult Hit;
        const FVector World=Transform.TransformPosition(P);
        bool bHit=Tongue && Tongue->SurfacePoint(World,Hit);
        if (!Tongue)
        {
            FCollisionQueryParams Params(SCENE_QUERY_STAT(MCPuddle),true,this);
            bHit=GetWorld()->LineTraceSingleByChannel(Hit,World+FVector(0,0,50),World-FVector(0,0,100),ECC_WorldStatic,Params);
        }
        if (bHit)
        {
            P=Transform.InverseTransformPosition(Hit.ImpactPoint+Hit.ImpactNormal*.45);
            Normal=Transform.InverseTransformVectorNoScale(Hit.ImpactNormal);
            if (Tongue)
            {
                const auto& Idx=Tongue->TriangleIndices(); const auto& V=Tongue->CurrentVertices();
                const int32 Face=Hit.FaceIndex*3;
                if (Hit.FaceIndex>=0 && Idx.IsValidIndex(Face+2))
                {
                    Binding.Face=Face;
                    Binding.Bary=FMath::ComputeBaryCentric2D(Tongue->GetActorTransform().InverseTransformPosition(Hit.ImpactPoint),V[Idx[Face]],V[Idx[Face+1]],V[Idx[Face+2]]);
                }
            }
        }
        Binding.Fallback=P; SurfaceBindings.Add(Binding); LiquidVertices.Add(P); LiquidNormals.Add(Normal);
        LiquidUV.Add(UV); LiquidTangents.Add(FProcMeshTangent(FVector(1,0,0),false));
        if (X<Steps && Y<Steps)
        {
            const int32 I=Y*(Steps+1)+X;
            Triangles.Append({I,I+Steps+1,I+1,I+1,I+Steps+1,I+Steps+2});
        }
    }
    Liquid->CreateMeshSection(0,LiquidVertices,Triangles,LiquidNormals,LiquidUV,TArray<FColor>(),LiquidTangents,false);
    BoundTongue=Tongue;
}

void AMCMouthSurface::UploadWipe()
{
    if (!WipeTexture || WipeMask.Num()!=FMCCoffeeWipe::Count) return;
    // Ownership of both allocations transfers to the render-thread cleanup callback.
    auto* Bytes=new uint8[FMCCoffeeWipe::Count*4];
    for (int32 I=0;I<FMCCoffeeWipe::Count;++I)
    { Bytes[I*4]=Bytes[I*4+1]=Bytes[I*4+2]=WipeMask[I]; Bytes[I*4+3]=255; }
    auto* Region=new FUpdateTextureRegion2D(0,0,0,0,FMCCoffeeWipe::Size,FMCCoffeeWipe::Size);
    WipeTexture->UpdateTextureRegions(0,1,Region,FMCCoffeeWipe::Size*4,4,Bytes,
        [](uint8* Data,const FUpdateTextureRegion2D* Rect){delete[] Data; delete Rect;});
    bWipeDirty=false;
}

void AMCMouthSurface::UpdateLiquid(float Dt)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MCLiquidUpdate);
    if (bUlcer || !FApp::CanEverRender() || CVarMCLiquidRender.GetValueOnGameThread()==0) { Liquid->SetVisibility(false); return; }
    if (!LiquidMID)
    {
        auto* Base=LiquidMaterial.LoadSynchronous(); if (!Base) return;
        LiquidMID=UMaterialInstanceDynamic::Create(Base,this); Liquid->SetMaterial(0,LiquidMID);
        TArray64<uint8> Initial;Initial.SetNumUninitialized(FMCCoffeeWipe::Count*4);
        for(int32 I=0;I<FMCCoffeeWipe::Count;++I) {
            const uint8 Value=WipeMask.IsValidIndex(I)?WipeMask[I]:(bPaintTile?0:255);
            Initial[I*4]=Initial[I*4+1]=Initial[I*4+2]=Value;Initial[I*4+3]=255;
        }
        WipeTexture=UTexture2D::CreateTransient(FMCCoffeeWipe::Size,FMCCoffeeWipe::Size,PF_B8G8R8A8,NAME_None,Initial);
        WipeTexture->SRGB=false; WipeTexture->Filter=TF_Bilinear; WipeTexture->AddressX=TA_Clamp; WipeTexture->AddressY=TA_Clamp;
        WipeTexture->NeverStream=true; WipeTexture->UpdateResource();
        LiquidMID->SetTextureParameterValue(TEXT("WipeMask"),WipeTexture);
    }
    Finish=IsClean()?FMath::Min(1.f,Finish+Dt/0.45f):0;
    Liquid->SetVisibility(Finish<1);
    if (Finish>=1) return;
    const bool PaintTopologyChanged=bPaintTile && Tongue && (BoundTongueSource.Get()!=Tongue->SourceMesh.Get()
        || BoundTongueVertexCount!=Tongue->CurrentVertices().Num() || BoundTongueIndexCount!=Tongue->TriangleIndices().Num());
    if (LiquidVertices.IsEmpty() || BoundTongue.Get()!=Tongue || PaintTopologyChanged) BuildLiquid();
    GeometryElapsed+=Dt;
    const float MeshHz=FMath::Clamp(CVarMCLiquidMeshHz.GetValueOnGameThread(),0.f,240.f);
    const float MeshInterval=MeshHz>0?1.f/MeshHz:0.f;
    const bool FollowPaintSurface=bPaintTile && Tongue;
    const bool PaintSurfaceChanged=FollowPaintSurface && (PaintSurfaceRevision!=Tongue->SurfaceRevision()
        || !PaintGeometryTransform.Equals(GetActorTransform())
        || !PaintTongueTransform.Equals(Tongue->Surface->GetComponentTransform()));
    // Paint is sampled on every tongue write, after its existing tick prerequisite.
    // A separate Hz clock could skip the very frame in which a ridge moves up.
    if (Tongue && !LiquidVertices.IsEmpty() && (FollowPaintSurface?PaintSurfaceChanged:GeometryElapsed>=MeshInterval))
    {
        GeometryElapsed=!FollowPaintSurface && MeshInterval>0?FMath::Fmod(GeometryElapsed,MeshInterval):0;
        const auto& Idx=Tongue->TriangleIndices(); const auto& V=Tongue->CurrentVertices();
        const FTransform TT=Tongue->Surface->GetComponentTransform(),LT=GetActorTransform();
        const FProcMeshSection* Section=bPaintTile?Tongue->Surface->GetProcMeshSection(0):nullptr;
        for (int32 I=0;I<SurfaceBindings.Num();++I)
        {
            const auto& B=SurfaceBindings[I]; if (B.Face==INDEX_NONE || !Idx.IsValidIndex(B.Face+2)) continue;
            const FVector A=TT.TransformPosition(V[Idx[B.Face]]),C=TT.TransformPosition(V[Idx[B.Face+1]]),D=TT.TransformPosition(V[Idx[B.Face+2]]);
            FVector N=FVector::CrossProduct(C-A,D-A).GetSafeNormal(); if (N.Z<0) N=-N;
            LiquidVertices[I]=LT.InverseTransformPosition(A*B.Bary.X+C*B.Bary.Y+D*B.Bary.Z
                +(bPaintTile?FVector(0,0,PaintSurfaceOffset):N*.45));
            if(Section && Section->ProcVertexBuffer.Num()==V.Num())
            {
                N=TT.TransformVectorNoScale(Section->ProcVertexBuffer[Idx[B.Face]].Normal*B.Bary.X
                    +Section->ProcVertexBuffer[Idx[B.Face+1]].Normal*B.Bary.Y
                    +Section->ProcVertexBuffer[Idx[B.Face+2]].Normal*B.Bary.Z).GetSafeNormal();
            }
            LiquidNormals[I]=LT.InverseTransformVectorNoScale(N);
            if(bPaintTile) LiquidTangents[I]=FProcMeshTangent(FVector::VectorPlaneProject(FVector::ForwardVector,LiquidNormals[I]).GetSafeNormal(),false);
        }
        // Barycentric bindings follow the existing tongue triangles, with no per-frame raycasts or collision cooks.
        Liquid->UpdateMeshSection(0,LiquidVertices,LiquidNormals,LiquidUV,TArray<FColor>(),LiquidTangents);
        if(FollowPaintSurface)
        {
            PaintSurfaceRevision=Tongue->SurfaceRevision();
            PaintGeometryTransform=LT; PaintTongueTransform=TT;
        }
    }
    TextureElapsed+=Dt;
    if (bWipeDirty && TextureElapsed>=1.f/30) { UploadWipe(); TextureElapsed=0; }
    const auto* GS=GetWorld()->GetGameState(); const float Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    LiquidMID->SetScalarParameterValue(TEXT("Seed"),LiquidSeed);
    LiquidMID->SetScalarParameterValue(TEXT("WorldSize"),LiquidHalfSize*2);
    LiquidMID->SetScalarParameterValue(TEXT("Finish"),Finish);
    LiquidMID->SetScalarParameterValue(TEXT("Arrival"),LiquidBornAt<0?1.f:FMath::SmoothStep(0.f,.35f,float(Now-LiquidBornAt)));
    // The wake has decayed below visibility after three seconds. Keep the value
    // stable then, so idle puddles do not upload a new material uniform every frame.
    LiquidMID->SetScalarParameterValue(TEXT("BrushAge"),FMath::Clamp(Now-BrushAt,0.f,3.f));
    LiquidMID->SetVectorParameterValue(TEXT("Brush"),FLinearColor(BrushUV.X,BrushUV.Y,BrushDirection.X,BrushDirection.Y));
}
