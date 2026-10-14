#include "MCMouthSurface.h"
#include "MCPerkComponent.h"
#include "MCCoffeeWipe.h"
#include "MCInventoryComponent.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCBrushContactComponent.h"
#include "MCToothStatusComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/App.h"
#include "Components/BoxComponent.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

static TAutoConsoleVariable<int32> CVarMCLiquidRender(TEXT("mc.LiquidRender"),1,
    TEXT("Render and update liquid stain visuals. 0 disables visuals for A/B profiling; gameplay remains active."));
static TAutoConsoleVariable<float> CVarMCLiquidMeshHz(TEXT("mc.LiquidMeshHz"),60.f,
    TEXT("Maximum visual liquid mesh updates per second. 0 updates every frame. Brush masks and gameplay are independent."));

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
    LiquidVertices.Reset();
}

void AMCMouthSurface::ResetLiquid()
{
    if (!HasAuthority()) return;
    if (LiquidDepositMask.Num()==FMCCoffeeWipe::Count) WipeMask=LiquidDepositMask;
    else FMCCoffeeWipe::Reset(WipeMask);
    WipeFractional.Reset(); PreviousBrush.Reset(); BrushCenters.Reset(); Finish=0; BrushAt=-100; BrushClock=0;
    OnRep_Wipe(); ForceNetUpdate();
}
void AMCMouthSurface::SetLiquidDepositMask(const TArray<uint8>& Mask)
{
    if (!HasAuthority() || Mask.Num()!=FMCCoffeeWipe::Count) return;
    LiquidDepositMask=Mask; ResetLiquid();
}
void AMCMouthSurface::InitializePaintTile(AMCTongue* InTongue)
{
    if(!HasAuthority() || !InTongue) return;
    bPaintTile=true;Tongue=InTongue;
    TongueAnchor=InTongue->GetActorTransform().InverseTransformPosition(GetActorLocation());
    LiquidDepositMask.Init(0,FMCCoffeeWipe::Count);WipeMask=LiquidDepositMask;
    OnRep_LiquidSize();
}
bool AMCMouthSurface::DepositPaintStroke(FVector From,FVector To,float Radius,const TFunction<bool(FVector)>& CoverageAllowed)
{
    if(!HasAuthority() || !bPaintTile || LiquidHalfSize<=0 || From.ContainsNaN() || To.ContainsNaN()) return false;
    const FTransform Transform=GetActorTransform();
    const FVector2D A=FVector2D(Transform.InverseTransformPosition(From))/(2*LiquidHalfSize)+FVector2D(.5);
    const FVector2D B=FVector2D(Transform.InverseTransformPosition(To))/(2*LiquidHalfSize)+FVector2D(.5);
    TFunction<bool(FVector2D)> Allowed;
    if(CoverageAllowed) Allowed=[&](FVector2D UV) {
        return CoverageAllowed(Transform.TransformPosition(FVector((UV.X-.5)*2*LiquidHalfSize,(UV.Y-.5)*2*LiquidHalfSize,0)));
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
void AMCMouthSurface::OnRep_Wipe() { bWipeDirty=true; }

void AMCMouthSurface::ClearLiquidForBooster()
{
    if(!HasAuthority() || bUlcer) return;
    WipeMask.Init(0,FMCCoffeeWipe::Count);WipeFractional.Reset();
    Status->ClearCoffeeForBooster();
    OnRep_Wipe();ForceNetUpdate();
}
float AMCMouthSurface::RemainingLiquid() const
{
    int32 Total=0; float Left=0;
    for(int32 Y=0;Y<FMCCoffeeWipe::Size;++Y) for(int32 X=0;X<FMCCoffeeWipe::Size;++X) {
        const FVector2D UV((X+.5)/FMCCoffeeWipe::Size,(Y+.5)/FMCCoffeeWipe::Size);
        if(!FMCCoffeeWipe::WetAt(LiquidDepositMask,UV,LiquidSeed,bPaintTile,bPaintTile?63:100)) continue;
        ++Total;
        const float Value=WipeMask.Num()==FMCCoffeeWipe::Count?WipeMask[Y*FMCCoffeeWipe::Size+X]/255.f:1.f;
        Left+=FMath::Clamp((Value-.25f)/.75f,0.f,1.f);
    }
    return Total>0?Left/Total:0.f;
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
    if (LiquidVertices.IsEmpty() || BoundTongue.Get()!=Tongue) BuildLiquid();
    GeometryElapsed+=Dt;
    const float MeshHz=FMath::Clamp(CVarMCLiquidMeshHz.GetValueOnGameThread(),0.f,240.f);
    const float MeshInterval=MeshHz>0?1.f/MeshHz:0.f;
    if (Tongue && GeometryElapsed>=MeshInterval)
    {
        GeometryElapsed=MeshInterval>0?FMath::Fmod(GeometryElapsed,MeshInterval):0;
        const auto& Idx=Tongue->TriangleIndices(); const auto& V=Tongue->CurrentVertices();
        const FTransform TT=Tongue->GetActorTransform(),LT=GetActorTransform();
        for (int32 I=0;I<SurfaceBindings.Num();++I)
        {
            const auto& B=SurfaceBindings[I]; if (B.Face==INDEX_NONE || !Idx.IsValidIndex(B.Face+2)) continue;
            const FVector A=TT.TransformPosition(V[Idx[B.Face]]),C=TT.TransformPosition(V[Idx[B.Face+1]]),D=TT.TransformPosition(V[Idx[B.Face+2]]);
            FVector N=FVector::CrossProduct(C-A,D-A).GetSafeNormal(); if (N.Z<0) N=-N;
            LiquidVertices[I]=LT.InverseTransformPosition(A*B.Bary.X+C*B.Bary.Y+D*B.Bary.Z+N*.45);
            LiquidNormals[I]=LT.InverseTransformVectorNoScale(N);
        }
        // Barycentric bindings follow the existing tongue triangles, with no per-frame raycasts or collision cooks.
        Liquid->UpdateMeshSection(0,LiquidVertices,LiquidNormals,LiquidUV,TArray<FColor>(),LiquidTangents);
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
