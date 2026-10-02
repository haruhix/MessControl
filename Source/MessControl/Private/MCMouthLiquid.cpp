#include "MCMouthSurface.h"
#include "MCCoffeeWipe.h"
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
    return FMath::Abs(P.Z)<18 && FMCCoffeeWipe::WetAt(WipeMask,FVector2D(P)/(2*LiquidHalfSize)+FVector2D(.5),LiquidSeed);
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
    LiquidHalfSize=FMath::IsFinite(LiquidHalfSize)?FMath::Clamp(LiquidHalfSize,20.f,260.f):92.f;
    if (!bUlcer) Area->SetBoxExtent(FVector(LiquidHalfSize*.75f,LiquidHalfSize*.75f,6));
    LiquidVertices.Reset();
}

void AMCMouthSurface::ResetLiquid()
{
    if (!HasAuthority()) return;
    FMCCoffeeWipe::Reset(WipeMask); PreviousBrush.Reset(); Finish=0; BrushAt=-100; BrushClock=0;
    OnRep_Wipe(); ForceNetUpdate();
}
void AMCMouthSurface::OnRep_Wipe() { bWipeDirty=true; }

float AMCMouthSurface::RemainingLiquid() const
{
    int32 Total=0; float Left=0;
    const TArray<uint8> Full;
    for(int32 Y=0;Y<FMCCoffeeWipe::Size;++Y) for(int32 X=0;X<FMCCoffeeWipe::Size;++X) {
        const FVector2D UV((X+.5)/FMCCoffeeWipe::Size,(Y+.5)/FMCCoffeeWipe::Size);
        if(!FMCCoffeeWipe::WetAt(Full,UV,LiquidSeed)) continue;
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
    // Sample the same seeded outline and persistent mask used by the material.
    for(int32 Y=1;Y<FMCCoffeeWipe::Size;Y+=2) for(int32 X=1;X<FMCCoffeeWipe::Size;X+=2) {
        const FVector2D UV((X+.5)/FMCCoffeeWipe::Size,(Y+.5)/FMCCoffeeWipe::Size);
        if(!FMCCoffeeWipe::WetAt(Full,UV,LiquidSeed)
            || (WipeMask.Num()==FMCCoffeeWipe::Count && WipeMask[Y*FMCCoffeeWipe::Size+X]<=71)) continue;
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
    if(FVector::DotProduct(Worker->GetActorForwardVector(),(Point-Worker->GetActorLocation()).GetSafeNormal2D())<.85f
        || !Contact->CanReach(Point,Normal)) {
        Contact->Contact(this,Point,Normal);
        return true;
    }
    if(Contact->Target==this && Now-Contact->ContactAt<.2)
        Point=FMath::VInterpConstantTo(Contact->ContactPoint(),Point,Seconds,180.f);
    const FVector Facing=GetActorTransform().InverseTransformVectorNoScale(Worker->GetActorForwardVector());
    const FVector2D Forward=FVector2D(Facing).GetSafeNormal();
    const FVector2D Side(-Forward.Y,Forward.X);
    const FVector Base=Point;
    const FVector Stroke=GetActorTransform().TransformVectorNoScale(FVector(Side.X,Side.Y,0))*FMath::Sin(BrushClock*22)*6;
    Point+=Stroke;
    FHitResult Floor;
    if(Tongue && Tongue->SurfacePoint(Point,Floor)) { Point=Floor.ImpactPoint+Floor.ImpactNormal*2; Normal=Floor.ImpactNormal; }
    if(!Contact->CanReach(Point,Normal)) Point=Base;
    if(!Contact->CanReach(Point,Normal)) return false;
    const FVector2D UV=FVector2D(GetActorTransform().InverseTransformPosition(Point))/(2*LiquidHalfSize)+FVector2D(.5);
    const FVector2D* Previous=PreviousBrush.Find(Worker);
    // Never connect a teleport or a changed target with a long erased stripe.
    const FVector2D From=Previous && FVector2D::Distance(*Previous,UV)<.3?*Previous:UV;
    Worker->BrushContact->Contact(this,Point,Normal);
    if(!Worker->BrushContact->IsTouchingSurface()) return true;
    if (FMCCoffeeWipe::Stroke(WipeMask,From,UV,36/(2*LiquidHalfSize),Seconds)) OnRep_Wipe();
    BrushDirection=(UV-From).IsNearlyZero()?Side:(UV-From).GetSafeNormal();
    BrushUV=UV; PreviousBrush.Add(Worker,UV);
    BrushAt=Now;
    const float Left=RemainingLiquid();
    const int32 RemainingLayers=Left<.025f?0:FMath::Max(1,FMath::CeilToInt(Left*Status->State.CoffeeTotal));
    while(Status->State.CoffeeLeft>RemainingLayers) { Status->CareContact(true); ++Worker->SuccessfulBrushContacts; }
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
        TArray64<uint8> White; White.Init(255,FMCCoffeeWipe::Count*4);
        WipeTexture=UTexture2D::CreateTransient(FMCCoffeeWipe::Size,FMCCoffeeWipe::Size,PF_B8G8R8A8,NAME_None,White);
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
