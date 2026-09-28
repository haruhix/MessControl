#include "MCArenaTooth.h"
#include "MCSurfaceWipe.h"
#include "MCToothCharacter.h"
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
    const FTransform Transform=Visual->GetComponentTransform();
    const FBoxSphereBounds Bounds=Visual->GetStaticMesh()->GetBounds();
    FRandomStream Random(State.ToothId*193+71);
    TArray<FVector> Vertices,Normals; TArray<int32> Triangles; TArray<FVector2D> UV;
    TArray<FLinearColor> Colors; TArray<FProcMeshTangent> Tangents;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCGrimeCoating),true);
    const FVector Inward=(-GetActorLocation()).GetSafeNormal2D();
    const float Side=GetActorLocation().Y<0?-1.f:1.f;
    auto SmoothUnion=[](float A,float B,float K)
    {
        const float H=FMath::Clamp(.5f+.5f*(B-A)/K,0.f,1.f);
        return FMath::Lerp(B,A,H)-K*H*(1-H);
    };
    // Two continuous coatings projected onto the actual artist mesh. A shallow
    // meniscus and fused ridges catch light; no floating balls or extra collision.
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
        constexpr int32 Columns=32;
        const int32 Rows=Patch==0?68:28,Base=Vertices.Num();
        struct FLobe { FVector2D Center,Radius; };
        TArray<FLobe> Lobes;
        for (int32 L=0;L<11;++L)
        {
            const float V=-.72f+L*.144f;
            Lobes.Add({FVector2D(Random.FRandRange(-.30f,.30f),V),
                FVector2D(Random.FRandRange(.17f,.34f),Random.FRandRange(.11f,.23f))});
        }
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
            const float Ridges=.5f+.5f*FMath::Sin(Q.Y*23+FMath::Sin(Q.X*11)*2);
            const float Depth=.70f+Interior*(2.7f+Ridges*1.1f);
            const FVector WorldPoint=bHit?Hit.ImpactPoint+Hit.ImpactNormal*Depth:Plane;
            WorldPoints.Add(WorldPoint); Valid.Add(bHit);
            Vertices.Add(Transform.InverseTransformPosition(WorldPoint));
            Normals.Add((Transform.InverseTransformVectorNoScale(bHit?Hit.ImpactNormal:N)*Transform.GetScale3D()).GetSafeNormal());
            UV.Add((Q+FVector2D(1,1))*.5);
            Colors.Add(FLinearColor(Coverage,.5f+.5f*FMath::Sin(Q.Y*5+Q.X*9+State.ToothId),Interior,1));
            Tangents.Add(FProcMeshTangent(Transform.InverseTransformVectorNoScale(X).GetSafeNormal(),false));
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
    GrimeRelief->CreateMeshSection_LinearColor(0,Vertices,Triangles,Normals,UV,Colors,Tangents,false);
}

void AMCArenaTooth::ResetGrime()
{
    if (!HasAuthority()) return;
    FMCSurfaceWipe::Reset(GrimeMask); BrushHistory.Reset(); BrushAt=-100;
    GrimeAmount=Status->CoffeeAmount(); GrimeFinish=GrimeAmount>0?0:1;
    OnRep_Grime(); ForceNetUpdate();
}
void AMCArenaTooth::OnRep_Grime() { bGrimeDirty=true; }

bool AMCArenaTooth::BrushGrime(AMCToothCharacter* Worker,float Seconds)
{
    if (!HasAuthority() || !IsAvailable() || !Status->NeedsCare(true) || !IsValid(Worker) ||
        !Worker->bBrushing || !Worker->HasBrush() || Worker->HeldFood || Worker->bInCoffee ||
        !Worker->CanContact(this) || !FMath::IsFinite(Seconds) || Seconds<=0 || !Visual->GetStaticMesh()) return false;
    const auto* GS=GetWorld()->GetGameState();
    const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    auto& History=BrushHistory.FindOrAdd(Worker); History.Clock+=FMath::Min(Seconds,.1f);
    const FVector Side=Worker->GetActorRightVector();
    const FVector Sweep=Side*(FMath::Sin(History.Clock*18)*15)+FVector(0,0,FMath::Sin(History.Clock*11)*10);
    const FVector From=Worker->GetActorLocation()+Sweep;
    FVector Aim=Visual->Bounds.Origin;
    Aim.Z=FMath::Clamp(From.Z,Visual->Bounds.GetBox().Min.Z+4,Visual->Bounds.GetBox().Max.Z-4);
    FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(MCToothBrush),true,Worker);
    // Keep the stroke under the facing brush instead of pulling every ray
    // toward the center of a large tooth. Retain the nearby care-target fallback.
    const FVector ForwardEnd=From+Worker->GetActorForwardVector()*(Worker->Status->Settings.Reach+30);
    if (!BrushSurface->LineTraceComponent(Hit,From,ForwardEnd,Query) &&
        !BrushSurface->LineTraceComponent(Hit,From,Aim+(Aim-From).GetSafeNormal()*20,Query)) return false;
    if (FVector::Dist(Hit.ImpactPoint,Worker->GetActorLocation())>Worker->Status->Settings.Reach+20) return false;
    // Validate the real visible contact as well as the broad-phase care target.
    FHitResult Block; Query.AddIgnoredActor(this);
    if (GetWorld()->LineTraceSingleByChannel(Block,From,Hit.ImpactPoint,ECC_Visibility,Query)) return false;
    const auto Bounds=Visual->GetStaticMesh()->GetBounds();
    const FVector Size=Bounds.BoxExtent*2;
    BrushLocal=Visual->GetComponentTransform().InverseTransformPosition(Hit.ImpactPoint);
    const FVector UV=(BrushLocal-(Bounds.Origin-Bounds.BoxExtent))/Size;
    const FVector Dimensions=Size*Visual->GetComponentScale().GetAbs();
    const bool Continuous=Now-History.At<.2 && ((UV-History.UV)*Dimensions).Size()<65;
    if (GrimeMask.Num()!=FMCSurfaceWipe::Count) FMCSurfaceWipe::Reset(GrimeMask);
    if (FMCSurfaceWipe::Stroke(GrimeMask,Continuous?History.UV:UV,UV,Dimensions,34,Seconds)) OnRep_Grime();
    History.UV=UV; History.At=Now; BrushAt=Now;
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
