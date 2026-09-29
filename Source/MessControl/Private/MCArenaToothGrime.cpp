#include "MCArenaTooth.h"
#include "MCSurfaceWipe.h"
#include "MCToothCharacter.h"
#include "MCBrushContactComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/App.h"
#include "ProceduralMeshComponent.h"

void AMCArenaTooth::BuildGrimeRelief()
{
    if (!Visual->GetStaticMesh() || !BrushSurface->IsPhysicsStateCreated()) return;
    GrimeSamples.Reset();
    const FTransform Transform=Visual->GetComponentTransform();
    const FBoxSphereBounds Bounds=Visual->GetStaticMesh()->GetBounds();
    FRandomStream Random(State.ToothId*193+71);
    TArray<FVector> Vertices,Normals; TArray<int32> Triangles; TArray<FVector2D> UV;
    TArray<FLinearColor> Colors; TArray<FProcMeshTangent> Tangents;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCGrimeCoating),true);
    FVector Inward=(-GetActorLocation()).GetSafeNormal2D();
    if(Inward.IsNearlyZero()) Inward=-GetActorForwardVector();
    const float Side=GetActorLocation().Y<0?-1.f:1.f;
    auto SmoothUnion=[](float A,float B,float K)
    {
        const float H=FMath::Clamp(.5f+.5f*(B-A)/K,0.f,1.f);
        return FMath::Lerp(B,A,H)-K*H*(1-H);
    };
    // Two continuous coatings projected onto the actual artist mesh. A shallow
    // meniscus and rounded clumps catch light; no floating balls or extra collision.
    for (int32 Patch=0;Patch<2;++Patch)
    {
        const float Angle=Side*(Patch==0?-.30f:.43f)+Random.FRandRange(-.10f,.10f);
        const FVector Radial=Inward.RotateAngleAxis(FMath::RadiansToDegrees(Angle),FVector::UpVector);
        FVector Aim=Bounds.Origin;
        Aim.Z=Bounds.Origin.Z+Bounds.BoxExtent.Z*((Patch==0?.61f:.43f)*2-1);
        const FVector WorldAim=Transform.TransformPosition(Aim);
        FHitResult CenterHit;
        if (!BrushSurface->LineTraceComponent(CenterHit,WorldAim+Radial*600,WorldAim-Radial*300,Query)) continue;
        const FVector N=CenterHit.ImpactNormal.GetSafeNormal();
        const FVector Y=FVector::VectorPlaneProject(FVector::UpVector,N).GetSafeNormal();
        const FVector X=FVector::CrossProduct(Y,N).GetSafeNormal();
        const float Width=(Patch==0?54.f:42.f)*Random.FRandRange(.85f,1.12f);
        const float Height=(Patch==0?78.f:30.f)*Random.FRandRange(.90f,1.1f);
        constexpr int32 Columns=48;
        const int32 Rows=Patch==0?96:40,Base=Vertices.Num();
        struct FLobe { FVector2D Center,Radius; };
        TArray<FLobe> Lobes;
        for (int32 L=0;L<11;++L)
        {
            const float V=-.72f+L*.144f+Random.FRandRange(-.045f,.045f);
            const float U=Random.FRandRange(-.30f,.30f),R=Width*Random.FRandRange(.17f,.34f);
            // Author these lobes in centimetres: normalized UV radii flattened
            // the short gumline patch into a stack of horizontal pancakes.
            Lobes.Add({FVector2D(U,V),FVector2D(R/Width,R*Random.FRandRange(.85f,1.15f)/Height)});
        }
        struct FClump { FVector2D Center; float Radius,Height; };
        TArray<FClump> Clumps;
        for(int32 I=0;I<32;++I)
            Clumps.Add({FVector2D(Random.FRandRange(-Width*.42f,Width*.42f),Random.FRandRange(-Height*.78f,Height*.78f)),
                float(Random.FRandRange(3.f,8.f)),float(Random.FRandRange(.8f,2.8f))});
        TArray<FVector> WorldPoints; TArray<bool> Valid;
        for (int32 R=0;R<=Rows;++R) for (int32 C=0;C<=Columns;++C)
        {
            const FVector2D Q(double(C)/Columns*2-1,double(R)/Rows*2-1);
            const float Bend=.09f*FMath::Sin(Q.Y*7+State.ToothId);
            float D=(FVector2D((Q.X-Bend)/.21f,Q.Y/.82f).Size()-1)*.21f;
            for (const auto& L:Lobes)
            {
                const float E=((Q-L.Center)/L.Radius).Size()-1;
                D=SmoothUnion(D,E*FMath::Min(L.Radius.X,L.Radius.Y),.055f);
            }
            D+=.011f*FMath::Sin(Q.X*34+Q.Y*13)*FMath::Sin(Q.Y*28+State.ToothId);
            const float Coverage=1-FMath::SmoothStep(-.020f,.018f,D);
            const FVector Plane=CenterHit.ImpactPoint+X*(Q.X*Width)+Y*(Q.Y*Height);
            FHitResult Hit;
            const bool bHit=D<.18f && BrushSurface->LineTraceComponent(Hit,Plane+N*150,Plane-N*210,Query)
                && FVector::DotProduct(Hit.ImpactNormal,N)>.22f;
            const float Interior=FMath::Pow(FMath::Clamp(-D*8,0.f,1.f),.65f);
            const FVector2D PatchPoint(Q.X*Width,Q.Y*Height);
            const float Grain=FMath::Clamp(.5f+.5f*FMath::PerlinNoise2D(PatchPoint*.19f+FVector2D(State.ToothId*17,Patch*31)),0.f,1.f);
            float ClumpDepth=0;
            for(const auto& Clump:Clumps) {
                const float R2=(PatchPoint-Clump.Center).SizeSquared()/FMath::Square(Clump.Radius);
                ClumpDepth+=Clump.Height*FMath::Exp(-R2*2.f);
            }
            const float Depth=.16f+Interior*(.25f+FMath::Min(ClumpDepth,3.8f));
            const FVector WorldPoint=bHit?Hit.ImpactPoint+Hit.ImpactNormal*Depth:Plane;
            WorldPoints.Add(WorldPoint); Valid.Add(bHit);
            Vertices.Add(Transform.InverseTransformPosition(WorldPoint));
            Normals.Add((Transform.InverseTransformVectorNoScale(bHit?Hit.ImpactNormal:N)*Transform.GetScale3D()).GetSafeNormal());
            UV.Add((Q+FVector2D(1,1))*.5);
            Colors.Add(FLinearColor(Coverage,Grain,FMath::Clamp((Depth-.16f)/2.2f,0.f,1.f),1));
            Tangents.Add(FProcMeshTangent(Transform.InverseTransformVectorNoScale(X).GetSafeNormal(),false));
            if(bHit && Coverage>.5f && R%2==0 && C%2==0) {
                const FVector Local=Transform.InverseTransformPosition(Hit.ImpactPoint);
                GrimeSamples.Add({Local,Transform.InverseTransformVectorNoScale(Hit.ImpactNormal).GetSafeNormal(),
                    (Local-(Bounds.Origin-Bounds.BoxExtent))/(Bounds.BoxExtent*2),Coverage});
            }
        }
        for (int32 R=0;R<=Rows;++R) for (int32 C=0;C<=Columns;++C)
        {
            const int32 I=R*(Columns+1)+C;
            const int32 Left=R*(Columns+1)+FMath::Max(C-1,0),Right=R*(Columns+1)+FMath::Min(C+1,Columns);
            const int32 Down=FMath::Max(R-1,0)*(Columns+1)+C,Up=FMath::Min(R+1,Rows)*(Columns+1)+C;
            if (Valid[Left] && Valid[Right] && Valid[Down] && Valid[Up])
            {
                const FVector SmoothNormal=FVector::CrossProduct(WorldPoints[Right]-WorldPoints[Left],WorldPoints[Up]-WorldPoints[Down]).GetSafeNormal();
                Normals[Base+I]=(Transform.InverseTransformVectorNoScale(SmoothNormal)*Transform.GetScale3D()).GetSafeNormal();
            }
            if (R<Rows && C<Columns)
            {
                const int32 B=I+Columns+1;
                if (Valid[I] && Valid[B] && Valid[I+1] && Valid[B+1])
                    Triangles.Append({Base+I,Base+B,Base+I+1,Base+I+1,Base+B,Base+B+1});
            }
        }
    }
    if(FApp::CanEverRender()) GrimeRelief->CreateMeshSection_LinearColor(0,Vertices,Triangles,Normals,UV,Colors,Tangents,false);
}

void AMCArenaTooth::ResetGrime()
{
    if (!HasAuthority()) return;
    FMCSurfaceWipe::Reset(GrimeMask); BrushHistory.Reset(); BrushAt=-100;
    GrimeAmount=Status->CoffeeAmount(); GrimeFinish=GrimeAmount>0?0:1;
    OnRep_Grime(); ForceNetUpdate();
}
void AMCArenaTooth::OnRep_Grime() { bGrimeDirty=true; }

float AMCArenaTooth::RemainingGrime() const
{
    float Total=0,Left=0;
    for(const auto& S:GrimeSamples) { Total+=S.Weight; Left+=S.Weight*FMath::Clamp((FMCSurfaceWipe::Sample(GrimeMask,S.UV)-.25f)/.75f,0.f,1.f); }
    return Total>0?Left/Total:1.f;
}
bool AMCArenaTooth::FindDirtyContact(AMCToothCharacter* Worker,FVector& Point,FVector& Normal,int32 Preferred)
{
    if(!IsAvailable() || !Status->NeedsCare(true) || !IsValid(Worker) || !Worker->BrushContact->CanAcquireSurface(this)) return false;
    if(GrimeSamples.IsEmpty()) BuildGrimeRelief();
    const FTransform T=Visual->GetComponentTransform();
    const FVector Origin=Worker->GetActorLocation(),Aim=Origin+Worker->GetActorForwardVector()*65+FVector(0,0,65);
    float Best=MAX_flt; SelectedSample=INDEX_NONE;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCDirtyTarget),false,Worker); Query.AddIgnoredActor(this);
    for(int32 I=0;I<GrimeSamples.Num();++I) {
        const auto& S=GrimeSamples[I]; if(FMCSurfaceWipe::Sample(GrimeMask,S.UV)<=.28f) continue;
        const FVector P=T.TransformPosition(S.Point),N=T.TransformVectorNoScale(S.Normal).GetSafeNormal(),D=P-Origin;
        if(D.Size2D()>Worker->BrushContact->SurfaceReach || FVector::DotProduct(N,(Origin-P).GetSafeNormal())<.05f) continue;
        const bool Locked=Worker->BrushContact->Target==this && Worker->BrushContact->Alpha()>.5f;
        const float Distance=Locked?FVector::DistSquared(P,Worker->BrushContact->ContactPoint())*.7f+FVector::DistSquared(P,Aim)*.3f:FVector::DistSquared(P,Aim);
        const float Score=Distance*(I==Preferred?.3f:1.f);
        if(Score>=Best || !Worker->BrushContact->CanReach(P,N)) continue;
        FHitResult Block; if(GetWorld()->LineTraceSingleByChannel(Block,Origin+FVector(0,0,40),P-N*2,ECC_Visibility,Query)) continue;
        Best=Score; SelectedSample=I; Point=P; Normal=N;
    }
    return SelectedSample!=INDEX_NONE;
}

bool AMCArenaTooth::BrushGrime(AMCToothCharacter* Worker,float Seconds)
{
    if (!HasAuthority() || !IsAvailable() || !Status->NeedsCare(true) || !IsValid(Worker) ||
        !Worker->bBrushing || !Worker->HasBrush() || Worker->HeldFood || Worker->bInCoffee ||
        !Worker->BrushContact->CanAcquireSurface(this) || !FMath::IsFinite(Seconds) || Seconds<=0 || !Visual->GetStaticMesh()) return false;
    const auto* GS=GetWorld()->GetGameState();
    const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    auto& History=BrushHistory.FindOrAdd(Worker); History.Clock+=FMath::Min(Seconds,.1f);
    FVector Aim,N; if(!FindDirtyContact(Worker,Aim,N,History.Sample)) return false;
    const FVector SelectedPoint=Aim,SelectedNormal=N;
    History.Sample=SelectedSample;
    const FTransform Surface=Visual->GetComponentTransform();
    if(Now-History.At<.2) {
        Aim=FMath::VInterpConstantTo(Surface.TransformPosition(History.Aim),Aim,FMath::Min(Seconds,.1f),220.f);
        N=FMath::Lerp(Surface.TransformVectorNoScale(History.Normal),N,1-FMath::Exp(-24.f*Seconds)).GetSafeNormal();
    }
    History.Aim=Surface.InverseTransformPosition(Aim); History.Normal=Surface.InverseTransformVectorNoScale(N);
    const FVector Up=FVector::VectorPlaneProject(FVector::UpVector,N).GetSafeNormal(),Side=FVector::CrossProduct(Up,N).GetSafeNormal();
    // Small tangential strokes stay on the chosen stain; project each stroke back onto enamel.
    const FVector Sweep=Side*(FMath::Sin(History.Clock*24)*3)+Up*(FMath::Sin(History.Clock*12)*2.5f);
    FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(MCToothBrush),true,Worker);
    auto ValidContact=[&](FVector Point,FVector Normal) {
        if(FVector::Dist2D(Point,Worker->GetActorLocation())>Worker->BrushContact->SurfaceReach+20
            || !Worker->BrushContact->CanReach(Point,Normal)) return false;
        FHitResult Block; FCollisionQueryParams Occlusion(SCENE_QUERY_STAT(MCBrushOcclusion),false,Worker); Occlusion.AddIgnoredActor(this);
        return !GetWorld()->LineTraceSingleByChannel(Block,Worker->GetActorLocation()+FVector(0,0,40),Point-Normal*2,ECC_Visibility,Occlusion);
    };
    const bool Projected=BrushSurface->LineTraceComponent(Hit,Aim+Sweep+N*25,Aim+Sweep-N*25,Query)
        && ValidContact(Hit.ImpactPoint,Hit.ImpactNormal);
    if(!Projected) {
        // A rounded lower edge can occlude the tangential stroke. Keep the
        // already validated dirty sample instead of repeatedly dropping it.
        Hit.ImpactPoint=SelectedPoint; Hit.ImpactNormal=SelectedNormal;
        if(!ValidContact(Hit.ImpactPoint,Hit.ImpactNormal)) return false;
    }
    const auto Bounds=Visual->GetStaticMesh()->GetBounds();
    const FVector Size=Bounds.BoxExtent*2;
    BrushLocal=Visual->GetComponentTransform().InverseTransformPosition(Hit.ImpactPoint);
    const FVector UV=(BrushLocal-(Bounds.Origin-Bounds.BoxExtent))/Size;
    const FVector Dimensions=Size*Visual->GetComponentScale().GetAbs();
    const bool Continuous=Now-History.At<.2 && ((UV-History.UV)*Dimensions).Size()<65;
    if (GrimeMask.Num()!=FMCSurfaceWipe::Count) FMCSurfaceWipe::Reset(GrimeMask);
    Worker->BrushContact->Contact(this,Hit.ImpactPoint,Hit.ImpactNormal);
    if(!Worker->BrushContact->IsTouchingSurface()) { History.At=Now; return true; }
    if (FMCSurfaceWipe::Stroke(GrimeMask,Continuous?History.UV:UV,UV,Dimensions,36,Seconds)) OnRep_Grime();
    History.UV=UV; History.At=Now; BrushAt=Now;
    const float Left=RemainingGrime();
    const int32 RemainingLayers=Left<.025f?0:FMath::Max(1,FMath::CeilToInt(Left*Status->State.CoffeeTotal));
    while(Status->State.CoffeeLeft>RemainingLayers) { Status->CareContact(true); ++Worker->SuccessfulBrushContacts; }
    return true;
}

void AMCArenaTooth::UpdateGrime(float Dt)
{
    if (!Material || !FApp::CanEverRender()) return;
    if (!ReliefMaterial)
        if (auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/Care/M_ArenaToothRelief.M_ArenaToothRelief")))
        { ReliefMaterial=UMaterialInstanceDynamic::Create(Base,this); GrimeRelief->SetMaterial(0,ReliefMaterial); }
    // Spread one-time surface projection over frames when a coffee wave hits all teeth.
    static uint64 LastBuildFrame=MAX_uint64;
    if (GrimeAmount>0 && Status->NeedsCare(true) && GrimeRelief->GetNumSections()==0 && LastBuildFrame!=GFrameCounter)
    { BuildGrimeRelief(); LastBuildFrame=GFrameCounter; }
    if (!GrimeTexture)
    {
        TArray64<uint8> White; White.Init(255,FMCSurfaceWipe::Count*4);
        GrimeTexture=UTexture2D::CreateTransient(FMCSurfaceWipe::Atlas,FMCSurfaceWipe::Atlas,PF_B8G8R8A8,NAME_None,White);
        GrimeTexture->SRGB=false; GrimeTexture->Filter=TF_Bilinear;
        GrimeTexture->AddressX=TA_Clamp; GrimeTexture->AddressY=TA_Clamp; GrimeTexture->NeverStream=true;
        GrimeTexture->UpdateResource(); bGrimeDirty=true;
    }
    Material->SetTextureParameterValue(TEXT("GrimeWipe"),GrimeTexture);
    GrimeUploadElapsed+=Dt;
    if (bGrimeDirty && GrimeUploadElapsed>=.05f && GrimeMask.Num()==FMCSurfaceWipe::Count)
    {
        auto* Bytes=new uint8[FMCSurfaceWipe::Count*4];
        for (int32 I=0;I<FMCSurfaceWipe::Count;++I)
        { Bytes[I*4]=Bytes[I*4+1]=Bytes[I*4+2]=GrimeMask[I]; Bytes[I*4+3]=255; }
        auto* Region=new FUpdateTextureRegion2D(0,0,0,0,FMCSurfaceWipe::Atlas,FMCSurfaceWipe::Atlas);
        GrimeTexture->UpdateTextureRegions(0,1,Region,FMCSurfaceWipe::Atlas*4,4,Bytes,
            [](uint8* Data,const FUpdateTextureRegion2D* Rect){delete[] Data; delete Rect;});
        bGrimeDirty=false; GrimeUploadElapsed=0;
    }
    GrimeFinish=Status->NeedsCare(true)?0:FMath::Min(1.f,GrimeFinish+Dt/.4f);
    const auto* GS=GetWorld()->GetGameState();
    const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    Material->SetScalarParameterValue(TEXT("GrimeAmount"),GrimeAmount*(1-GrimeFinish));
    Material->SetScalarParameterValue(TEXT("GrimeSeed"),State.ToothId+1);
    Material->SetScalarParameterValue(TEXT("BrushAge"),FMath::Clamp(float(Now-BrushAt),0.f,4.f));
    Material->SetVectorParameterValue(TEXT("BrushLocal"),FLinearColor(BrushLocal));
    GrimeRelief->SetVisibility(IsAvailable() && GrimeAmount*(1-GrimeFinish)>.001f && ReliefMaterial!=nullptr);
    if (ReliefMaterial)
    {
        const auto Bounds=Visual->GetStaticMesh()->GetBounds();
        ReliefMaterial->SetTextureParameterValue(TEXT("GrimeWipe"),GrimeTexture);
        ReliefMaterial->SetVectorParameterValue(TEXT("GrimeMin"),FLinearColor(Bounds.Origin-Bounds.BoxExtent));
        ReliefMaterial->SetVectorParameterValue(TEXT("GrimeSize"),FLinearColor(Bounds.BoxExtent*2));
        ReliefMaterial->SetVectorParameterValue(TEXT("GrimeScale"),FLinearColor(MeshBaseScale));
        ReliefMaterial->SetVectorParameterValue(TEXT("GrimeColor"),Material->K2_GetVectorParameterValue(TEXT("GrimeColor")));
        ReliefMaterial->SetScalarParameterValue(TEXT("GrimeAmount"),GrimeAmount*(1-GrimeFinish));
        ReliefMaterial->SetScalarParameterValue(TEXT("GrimeSeed"),State.ToothId+1);
        ReliefMaterial->SetScalarParameterValue(TEXT("GrimeThickness"),.10f);
        ReliefMaterial->SetScalarParameterValue(TEXT("BrushAge"),FMath::Clamp(float(Now-BrushAt),0.f,4.f));
        ReliefMaterial->SetVectorParameterValue(TEXT("BrushLocal"),FLinearColor(BrushLocal));
    }
}
