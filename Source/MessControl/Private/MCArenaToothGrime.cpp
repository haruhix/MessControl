#include "MCArenaTooth.h"
#include "MCPerkComponent.h"
#include "MCSurfaceWipe.h"
#include "MCInventoryComponent.h"
#include "MCToothCharacter.h"
#include "MCBrushContactComponent.h"
#include "MCToothCareFeedbackComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/App.h"
#include "ProceduralMeshComponent.h"
#include "MCTongue.h"
#include "EngineUtils.h"
#include "Components/BoxComponent.h"
#include "KismetProceduralMeshLibrary.h"

namespace
{
// Subdivide the actual enamel faces instead of spanning their creases with a
// separate projected grid. All new points remain barycentric on an enamel face;
// the authored smooth normals, rather than collision face normals, light the film.
bool FitPlaqueToEnamel(UStaticMesh* Mesh,const FTransform& Transform,FVector Center,
    FVector X,FVector Y,FVector Inward,float Width,float Height,int32 Columns,int32 Rows,
    const TArray<FVector>& GridPoints,const TArray<bool>& Valid,const TArray<FLinearColor>& GridColors,
    TArray<FVector>& Vertices,TArray<int32>& Triangles,TArray<FVector>& Normals,
    TArray<FVector2D>& UV,TArray<FLinearColor>& Colors,TArray<FProcMeshTangent>& Tangents)
{
    if(!Mesh || !Mesh->bAllowCPUAccess) return false;
    TArray<FVector> FittedVertices,FittedNormals;
    TArray<int32> FittedTriangles;
    TArray<FVector2D> FittedUV;
    TArray<FLinearColor> FittedColors;
    TArray<FProcMeshTangent> FittedTangents;
    auto Project=[&](FVector World) {
        const FVector D=World-Center;
        return FVector2D(FVector::DotProduct(D,X)/Width,FVector::DotProduct(D,Y)/Height)*.5+FVector2D(.5,.5);
    };
    auto Sample=[&](FVector2D Q,FVector World) {
        if(Q.X<0 || Q.X>1 || Q.Y<0 || Q.Y>1) return FLinearColor::Transparent;
        const double GX=Q.X*Columns,GY=Q.Y*Rows;
        const int32 C=FMath::Min(FMath::FloorToInt(GX),Columns-1),R=FMath::Min(FMath::FloorToInt(GY),Rows-1);
        const float FX=GX-C,FY=GY-R;
        const int32 I=R*(Columns+1)+C;
        const int32 IDs[]={I,I+1,I+Columns+1,I+Columns+2};
        const float Weights[]={(1-FX)*(1-FY),FX*(1-FY),(1-FX)*FY,FX*FY};
        FVector Grid=FVector::ZeroVector; float Weight=0;
        FLinearColor Color=FLinearColor::Transparent;
        for(int32 K=0;K<4;++K) {
            Color+=GridColors[IDs[K]]*Weights[K];
            if(Valid[IDs[K]]) { Grid+=GridPoints[IDs[K]]*Weights[K];Weight+=Weights[K]; }
        }
        // Reject the back of the crown and folded/occluded faces sharing a UV.
        if(Weight<.001f || FVector::DistSquared(Grid/Weight,World)>36) return FLinearColor::Transparent;
        return Color;
    };
    constexpr int32 Divisions=8;
    for(int32 Section=0;Section<Mesh->GetNumSections(0);++Section) {
        TArray<FVector> Source,SourceNormals; TArray<int32> Indices;
        TArray<FVector2D> SourceUV; TArray<FProcMeshTangent> SourceTangents;
        UKismetProceduralMeshLibrary::GetSectionFromStaticMesh(Mesh,0,Section,Source,Indices,SourceNormals,SourceUV,SourceTangents);
        for(int32 Face=0;Face+2<Indices.Num();Face+=3) {
            const int32 A=Indices[Face],B=Indices[Face+1],C=Indices[Face+2];
            const FVector WA=Transform.TransformPosition(Source[A]),WB=Transform.TransformPosition(Source[B]),WC=Transform.TransformPosition(Source[C]);
            const FVector2D QA=Project(WA),QB=Project(WB),QC=Project(WC);
            if(FMath::Max3(QA.X,QB.X,QC.X)<0 || FMath::Min3(QA.X,QB.X,QC.X)>1
                || FMath::Max3(QA.Y,QB.Y,QC.Y)<0 || FMath::Min3(QA.Y,QB.Y,QC.Y)>1) continue;
            const FVector FaceNormal=Transform.TransformVectorNoScale((SourceNormals[A]+SourceNormals[B]+SourceNormals[C])/Transform.GetScale3D()).GetSafeNormal();
            if(FVector::DotProduct(FaceNormal,Inward)<.15f) continue;
            int32 GridIndex[Divisions+1][Divisions+1];
            for(int32 R=0;R<=Divisions;++R) for(int32 Col=0;Col<=Divisions-R;++Col) {
                const double U=double(Col)/Divisions,V=double(R)/Divisions,W=1-U-V;
                const FVector Local=Source[A]*W+Source[B]*U+Source[C]*V;
                const FVector LocalNormal=(SourceNormals[A]*W+SourceNormals[B]*U+SourceNormals[C]*V).GetSafeNormal();
                const FVector World=Transform.TransformPosition(Local);
                const FVector WorldNormal=Transform.TransformVectorNoScale(LocalNormal/Transform.GetScale3D()).GetSafeNormal();
                const FVector2D Q=QA*W+QB*U+QC*V;
                FLinearColor Color=Sample(Q,World);
                Color.R*=FMath::SmoothStep(.45f,.70f,float(FVector::DotProduct(WorldNormal,Inward)));
                const int32 Index=FittedVertices.Num();GridIndex[R][Col]=Index;
                FittedVertices.Add(Transform.InverseTransformPosition(World+WorldNormal*.55f));
                FittedNormals.Add(LocalNormal);FittedUV.Add(Q);FittedColors.Add(Color);
                const FVector Tangent=FVector::VectorPlaneProject(Transform.InverseTransformVector(X),LocalNormal).GetSafeNormal();
                FittedTangents.Add(FProcMeshTangent(Tangent,false));
            }
            auto Add=[&](int32 I,int32 J,int32 K) {
                if(FMath::Max3(FittedColors[I].R,FittedColors[J].R,FittedColors[K].R)>.001f) FittedTriangles.Append({I,J,K});
            };
            for(int32 R=0;R<Divisions;++R) for(int32 Col=0;Col<Divisions-R;++Col) {
                Add(GridIndex[R][Col],GridIndex[R][Col+1],GridIndex[R+1][Col]);
                if(Col+R<Divisions-1) Add(GridIndex[R][Col+1],GridIndex[R+1][Col+1],GridIndex[R+1][Col]);
            }
        }
    }
    if(FittedTriangles.IsEmpty()) return false;
    Vertices=MoveTemp(FittedVertices);Triangles=MoveTemp(FittedTriangles);Normals=MoveTemp(FittedNormals);
    UV=MoveTemp(FittedUV);Colors=MoveTemp(FittedColors);Tangents=MoveTemp(FittedTangents);
    return true;
}
}

void AMCArenaTooth::BuildGrimeRelief()
{
    if (bGrimeReliefBuilt || !Visual->GetStaticMesh() || !BrushSurface->IsPhysicsStateCreated()) return;
    GrimeSamples.Reset();
    const FTransform Transform=Visual->GetComponentTransform();
    const FBoxSphereBounds Bounds=Appearance.Mesh?Appearance.Mesh->GetBounds():Visual->GetStaticMesh()->GetBounds();
    auto HalfSpan=[&](FVector Axis) {
        return FMath::Abs(FVector::DotProduct(Transform.TransformVector(FVector(Bounds.BoxExtent.X,0,0)),Axis))
            +FMath::Abs(FVector::DotProduct(Transform.TransformVector(FVector(0,Bounds.BoxExtent.Y,0)),Axis))
            +FMath::Abs(FVector::DotProduct(Transform.TransformVector(FVector(0,0,Bounds.BoxExtent.Z)),Axis));
    };
    const float ToothHeight=2*HalfSpan(FVector::UpVector);
    FRandomStream Random(State.ToothId*193+71);
    TArray<FVector> Vertices,Normals; TArray<int32> Triangles; TArray<FVector2D> UV;
    TArray<FLinearColor> Colors; TArray<FProcMeshTangent> Tangents;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCGrimeCoating),true);
    FVector Inward=(-GetActorLocation()).GetSafeNormal2D();
    if(Inward.IsNearlyZero()) Inward=-GetActorForwardVector();
    const float Side=GetActorLocation().Y<0?-1.f:1.f;
    AMCTongue* Tongue=nullptr;
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) { Tongue=*It; break; }
    FCollisionQueryParams Room(SCENE_QUERY_STAT(MCGrimeRoom),false,this);
    // Only permanent mouth geometry defines the coating. Players, food and
    // temporary tongue deformation must never cut holes in a replicated stain.
    if(Tongue) Room.AddIgnoredActor(Tongue);
    FCollisionObjectQueryParams StaticObjects(ECC_WorldStatic);
    FCollisionQueryParams FloorRoom=Room;
    const FTransform BodyTransform=Body->GetComponentTransform();
    const FBox BodyBox(-Body->GetUnscaledBoxExtent(),Body->GetUnscaledBoxExtent());
    TArray<AMCArenaTooth*> Neighbors;
    for(TActorIterator<AMCArenaTooth> It(GetWorld());It;++It) {
        FloorRoom.AddIgnoredActor(*It);
        if(*It!=this && It->IsAvailable()) Neighbors.Add(*It);
    }
    // The mouth's centre points diagonally along the row on the back teeth.
    // Follow the local row normal so plaque stays on the tongue-facing crown,
    // rather than wrapping behind the padded physical body into an unreachable side.
    const AMCArenaTooth* Before=nullptr; const AMCArenaTooth* After=nullptr;
    for(const auto* Neighbor:Neighbors) {
        if(Neighbor->GetActorLocation().Y*GetActorLocation().Y<=0) continue;
        const double DX=Neighbor->GetActorLocation().X-GetActorLocation().X;
        if(DX<0 && (!Before || Neighbor->GetActorLocation().X>Before->GetActorLocation().X)) Before=Neighbor;
        if(DX>0 && (!After || Neighbor->GetActorLocation().X<After->GetActorLocation().X)) After=Neighbor;
    }
    if(Before || After) {
        const FVector Along=((After?After->GetActorLocation():GetActorLocation())
            -(Before?Before->GetActorLocation():GetActorLocation())).GetSafeNormal2D();
        FVector RowNormal=FVector::CrossProduct(Along,FVector::UpVector).GetSafeNormal2D();
        if(FVector::DotProduct(RowNormal,Inward)<0) RowNormal=-RowNormal;
        if(!RowNormal.IsNearlyZero()) Inward=RowNormal;
    }
    // One broad film covers the exposed inner crown from the gum upward.
    // Its envelope follows the crown; pigment/grain and relief are in centimetres.
    for (int32 Patch=0;Patch<1;++Patch)
    {
        const float Angle=Side*-.04f;
        const FVector Radial=Inward.RotateAngleAxis(FMath::RadiansToDegrees(Angle),FVector::UpVector);
        FVector Aim=Bounds.Origin;
        Aim.Z=Bounds.Origin.Z+Bounds.BoxExtent.Z*((Patch==0?.61f:.43f)*2-1);
        FVector WorldAim=Transform.TransformPosition(Aim);
        if(Tongue) {
            FVector Floor;
            if(Tongue->RestSurfacePoint(WorldAim+Inward*75,Floor)) {
                WorldAim.Z=FMath::Min(Visual->Bounds.GetBox().Max.Z-12,
                    FMath::Clamp(WorldAim.Z,Floor.Z+(Patch==0?90:52),Floor.Z+(Patch==0?110:65)));
            }
        }
        FHitResult CenterHit;
        if (!BrushSurface->LineTraceComponent(CenterHit,WorldAim+Radial*600,WorldAim-Radial*300,Query)) continue;
        FVector N=CenterHit.ImpactNormal.GetSafeNormal();
        FVector Y=FVector::VectorPlaneProject(FVector::UpVector,N).GetSafeNormal();
        FVector X=FVector::CrossProduct(Y,N).GetSafeNormal();
        const float Width=float(HalfSpan(X))*.82f;
        float Height=FMath::Min(ToothHeight*.4f,140.f);
        // A horizontal room sweep can run underneath the permanent gum ridge.
        // Cache floor heights across the coating and the approach footprint;
        // never scan the tongue triangles again for individual coating vertices.
        auto CacheFloor=[&]() {
            float RestZ=-MAX_flt;
            FVector Floor;
            if(Tongue && Tongue->RestSurfacePoint(WorldAim+Inward*75,Floor)) RestZ=Floor.Z;
            TArray<FVector,TInlineAllocator<9>> FloorSamples;
            for(float Offset:{-Width,0.f,Width}) for(float Approach:{25.f,60.f,95.f}) {
                const FVector Probe=CenterHit.ImpactPoint+X*Offset+Inward*Approach;
                FloorSamples.Add(Probe);
                if(Tongue && Tongue->RestSurfacePoint(Probe,Floor)) RestZ=FMath::Max(RestZ,float(Floor.Z));
            }
            float Highest=RestZ;
            const bool HasRestFloor=RestZ> -MAX_flt;
            const float PatchUpper=CenterHit.ImpactPoint.Z+Height+22;
            const float ProbeTop=HasRestFloor?FMath::Min(PatchUpper,RestZ+100):PatchUpper;
            const float ProbeBottom=(HasRestFloor?RestZ:float(Visual->Bounds.GetBox().Min.Z))-100;
            TArray<FHitResult> FloorHits;
            for(const FVector& Probe:FloorSamples) {
                GetWorld()->LineTraceMultiByObjectType(FloorHits,FVector(Probe.X,Probe.Y,ProbeTop),FVector(Probe.X,Probe.Y,ProbeBottom),StaticObjects,FloorRoom);
                for(const auto& Hit:FloorHits) if(Hit.ImpactNormal.Z>.5f) Highest=FMath::Max(Highest,float(Hit.ImpactPoint.Z));
            }
            return Highest;
        };
        float FloorZ=CacheFloor();
        // An approach sample can reveal a higher floor than the initial aim.
        // Cover the exposed reachable crown above the cached gum/tongue floor.
        if(FloorZ> -MAX_flt) {
            const float Bottom=FloorZ+24;
            const float Top=FMath::Min(float(Visual->Bounds.GetBox().Max.Z)-20,FloorZ+280);
            Height=FMath::Max(20.f,(Top-Bottom)*.5f);
            const float RaisedZ=Bottom+Height;
            if(FMath::Abs(RaisedZ-WorldAim.Z)>UE_SMALL_NUMBER) {
                const FVector RaisedAim(WorldAim.X,WorldAim.Y,RaisedZ);
                FHitResult RaisedHit;
                if(BrushSurface->LineTraceComponent(RaisedHit,RaisedAim+Radial*600,RaisedAim-Radial*300,Query)) {
                    WorldAim=RaisedAim; CenterHit=RaisedHit;
                    N=CenterHit.ImpactNormal.GetSafeNormal();
                    Y=FVector::VectorPlaneProject(FVector::UpVector,N).GetSafeNormal();
                    X=FVector::CrossProduct(Y,N).GetSafeNormal();
                    // Keep the common cached floor: resampling from this high
                    // projection must not mistake a neighboring crown for gum.
                }
            }
        }
        constexpr int32 Columns=128,SampleStride=4;
        const int32 Rows=128,Base=Vertices.Num();
        struct FClump { FVector2D Center; float Radius,Height; };
        TArray<FClump> Clumps;
        for(int32 I=0;I<32;++I)
            Clumps.Add({FVector2D(Random.FRandRange(-Width*.85f,Width*.85f),Random.FRandRange(-Height*.78f,Height*.78f)),
                float(Random.FRandRange(3.f,8.f)),float(Random.FRandRange(.8f,2.8f))});
        TArray<FVector> WorldPoints; TArray<bool> Valid; TArray<FGrimeSample> Candidates;
        for (int32 R=0;R<=Rows;++R) for (int32 C=0;C<=Columns;++C)
        {
            const FVector2D Q(double(C)/Columns*2-1,double(R)/Rows*2-1);
            const FVector2D PatchPoint(Q.X*Width,Q.Y*Height);
            const FVector2D Seed(State.ToothId*11,19);
            const float EdgeNoise=FMath::PerlinNoise2D(PatchPoint*.035f+Seed);
            const float TopEdge=Height*(.79f+.10f*FMath::Sin(Q.X*7+State.ToothId)) + EdgeNoise*9;
            const float D=FMath::Max(FMath::Abs(PatchPoint.X)-Width*.94f+EdgeNoise*7,
                FMath::Max(PatchPoint.Y-TopEdge,-PatchPoint.Y-Height*.96f));
            float Coverage=1-FMath::SmoothStep(-1.5f,1.5f,D),FacingCoverage=1;
            const FVector Plane=CenterHit.ImpactPoint+X*(Q.X*Width)+Y*(Q.Y*Height);
            FHitResult Hit;
            bool bHit=D<8.f && BrushSurface->LineTraceComponent(Hit,Plane+N*150,Plane-N*210,Query)
                && FVector::DotProduct(Hit.ImpactNormal,N)>.22f;
            if(bHit) {
                // Leave space for the whole brush head above the tongue/gum,
                // and keep the coating on the face visible from the mouth.
                Coverage*=FMath::SmoothStep(FloorZ+22,FloorZ+35,float(Hit.ImpactPoint.Z));
                FacingCoverage=FMath::SmoothStep(.45f,.70f,float(FVector::DotProduct(Hit.ImpactNormal,Inward)));
                // The padded tooth body can keep a walking pawn far from a
                // recessed enamel lip even when the surface normal looks reachable.
                // Trim those recesses using the actual physical body, not tooth size.
                FVector BodyPoint,BodyNormal; float BodyTime=0;
                if(FMath::LineExtentBoxIntersection(BodyBox,
                    BodyTransform.InverseTransformPosition(Hit.ImpactPoint+Inward*700),
                    BodyTransform.InverseTransformPosition(Hit.ImpactPoint),FVector::ZeroVector,BodyPoint,BodyNormal,BodyTime)) {
                    const float Recess=FVector::DotProduct(Hit.ImpactPoint-BodyTransform.TransformPosition(BodyPoint),-Inward);
                    Coverage*=1-FMath::SmoothStep(20.f,55.f,Recess);
                }
                const FVector Start=Hit.ImpactPoint+Hit.ImpactNormal*12,End=Start+Inward*65;
                FHitResult Obstacle;
                bool Blocked=GetWorld()->SweepSingleByObjectType(Obstacle,Start,End,FQuat::Identity,StaticObjects,FCollisionShape::MakeSphere(8),Room);
                for(const auto* Neighbor:Neighbors) if(!Blocked) {
                    const FVector Local=Neighbor->Body->GetComponentTransform().InverseTransformPosition(Start);
                    const FVector LocalEnd=Neighbor->Body->GetComponentTransform().InverseTransformPosition(End);
                    const FVector Extent=Neighbor->Body->GetScaledBoxExtent()+FVector(8);
                    Blocked=FMath::LineBoxIntersection(FBox(-Extent,Extent),Local,LocalEnd,LocalEnd-Local);
                }
                // Keep transparent border vertices. Clipping mesh cells by the
                // coverage threshold made a staircase before the shader could
                // interpolate the actual outline across each triangle.
                bHit=!Blocked;
                if(!bHit) Coverage=0;
            }
            const float Interior=FMath::Pow(FMath::Clamp(-D/8.f,0.f,1.f),.65f);
            const float Grain=FMath::Clamp(.5f+.5f*FMath::PerlinNoise2D(PatchPoint*.19f+FVector2D(State.ToothId*17,Patch*31)),0.f,1.f);
            float ClumpDepth=0;
            for(const auto& Clump:Clumps) {
                const float R2=(PatchPoint-Clump.Center).SizeSquared()/FMath::Square(Clump.Radius);
                ClumpDepth+=Clump.Height*FMath::Exp(-R2*2.f);
            }
            const float Depth=.40f+Interior*(.12f+FMath::Min(ClumpDepth*.08f,.35f));
            const FVector WorldPoint=bHit?Hit.ImpactPoint+Hit.ImpactNormal*Depth:Plane;
            WorldPoints.Add(WorldPoint); Valid.Add(bHit);
            Vertices.Add(Transform.InverseTransformPosition(WorldPoint));
            Normals.Add((Transform.InverseTransformVectorNoScale(bHit?Hit.ImpactNormal:N)*Transform.GetScale3D()).GetSafeNormal());
            UV.Add((Q+FVector2D(1,1))*.5);
            Colors.Add(FLinearColor(Coverage,Grain,FMath::Clamp((Depth-.12f)/1.4f,0.f,1.f),1));
            Tangents.Add(FProcMeshTangent(Transform.InverseTransformVectorNoScale(X).GetSafeNormal(),false));
            const FVector Local=Transform.InverseTransformPosition(bHit?Hit.ImpactPoint:Plane);
            Candidates.Add({Local,Transform.InverseTransformVectorNoScale(bHit?Hit.ImpactNormal:N).GetSafeNormal(),
                (Local-(Bounds.Origin-Bounds.BoxExtent))/(Bounds.BoxExtent*2),Coverage*FacingCoverage});
        }
        TArray<bool> Drawn; Drawn.Init(false,Valid.Num());
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
                {
                    Triangles.Append({Base+I,Base+B,Base+I+1,Base+I+1,Base+B,Base+B+1});
                    Drawn[I]=Drawn[B]=Drawn[I+1]=Drawn[B+1]=true;
                }
            }
        }
        // Only vertices belonging to a visible triangle contribute to progress.
        // A rejected neighbor must not leave an invisible, counted sample.
        // Rendering is denser; gameplay retains the same sample spacing.
        for(int32 R=0;R<=Rows;R+=SampleStride) for(int32 C=0;C<=Columns;C+=SampleStride) {
            const int32 I=R*(Columns+1)+C;
            if(Drawn[I] && Candidates[I].Weight>.5f) GrimeSamples.Add(Candidates[I]);
        }
        if(FApp::CanEverRender()) FitPlaqueToEnamel(Visual->GetStaticMesh(),Transform,CenterHit.ImpactPoint,X,Y,Inward,
            Width,Height,Columns,Rows,WorldPoints,Valid,Colors,Vertices,Triangles,Normals,UV,Colors,Tangents);
    }
    if(FApp::CanEverRender()) GrimeRelief->CreateMeshSection_LinearColor(0,Vertices,Triangles,Normals,UV,Colors,Tangents,false);
    bGrimeReliefBuilt=true;
}

void AMCArenaTooth::ResetGrime()
{
    if (!HasAuthority()) return;
    bGrimeRewardEligible=true;
    FMCSurfaceWipe::Reset(GrimeMask); PreciseGrimeMask.Reset(); BrushHistory.Reset(); BrushAt=-100;
    GrimeAmount=Status->CoffeeAmount(); GrimeFinish=GrimeAmount>0?0:1;
    OnRep_Grime(); ForceNetUpdate();
}
void AMCArenaTooth::OnRep_Grime() { bGrimeDirty=true; }

bool AMCArenaTooth::DepositLiquidGrime(TFunctionRef<bool(FVector)> IsWet,bool bRewardEligible)
{
    if (!HasAuthority() || !IsAvailable() || !Visual->GetStaticMesh()) return false;
    if (!bGrimeReliefBuilt) BuildGrimeRelief();
    const FTransform Transform=Visual->GetComponentTransform();
    bool Touched=false;
    for (const auto& Sample:GrimeSamples)
        if (Sample.Weight>.1f && IsWet(Transform.TransformPosition(Sample.Point))) { Touched=true; break; }
    if (!Touched) return false;
    const FBoxSphereBounds Bounds=Appearance.Mesh?Appearance.Mesh->GetBounds():Visual->GetStaticMesh()->GetBounds();
    TArray<uint8> Deposited;
    if (Status->NeedsCare(true) && GrimeMask.Num()==FMCSurfaceWipe::Count) Deposited=GrimeMask;
    else Deposited.Init(0,FMCSurfaceWipe::Count);
    for (int32 Z=0;Z<FMCSurfaceWipe::Size;++Z) for (int32 Y=0;Y<FMCSurfaceWipe::Size;++Y) for (int32 X=0;X<FMCSurfaceWipe::Size;++X) {
        const FVector UV=FVector(X,Y,Z)/double(FMCSurfaceWipe::Size-1);
        const FVector World=Transform.TransformPosition(Bounds.Origin-Bounds.BoxExtent+UV*Bounds.BoxExtent*2);
        if (IsWet(World)) Deposited[FMCSurfaceWipe::Index(X,Y,Z)]=255;
    }
    // A barely touched voxel must still produce a coating sample the brush can acquire.
    bool Brushable=false;
    for (const auto& Sample:GrimeSamples)
        if (Sample.Weight>.1f && FMCSurfaceWipe::Sample(Deposited,Sample.UV)>.25f) { Brushable=true; break; }
    if (!Brushable) return false;
    // ApplyCoffee calls ResetGrime. Preserve provenance across that reset, then
    // keep it through the final texel/Changed callback for the completion gate.
    const bool RewardEligible=Status->NeedsCare(true)?bGrimeRewardEligible || bRewardEligible:bRewardEligible;
    Status->ApplyCoffee(1.f);
    bGrimeRewardEligible=RewardEligible;
    GrimeMask=MoveTemp(Deposited); PreciseGrimeMask.Reset();
    OnRep_Grime(); ForceNetUpdate();
    return true;
}

void AMCArenaTooth::ClearGrimeForBooster()
{
    if(!HasAuthority()) return;
    GrimeMask.Init(0,FMCSurfaceWipe::Count);PreciseGrimeMask.Init(0,FMCSurfaceWipe::Count);
    Status->ClearCoffeeForBooster();
    OnRep_Grime();ForceNetUpdate();
}
float AMCArenaTooth::RemainingGrime() const
{
    float Total=0,Left=0;
    for(const auto& S:GrimeSamples) { Total+=S.Weight; Left+=S.Weight*FMath::Clamp((FMCSurfaceWipe::Sample(GrimeMask,S.UV)-.25f)/.75f,0.f,1.f); }
    return Total>0?Left/Total:1.f;
}
void AMCArenaTooth::GetDirtyContactSamples(TArray<FVector>& Points,TArray<FVector>& Normals,TArray<FVector>* MaskUVs)
{
    Points.Reset(); Normals.Reset();
    if(MaskUVs) MaskUVs->Reset();
    if(!IsAvailable() || !Status->NeedsCare(true)) return;
    if(!bGrimeReliefBuilt) BuildGrimeRelief();
    const FTransform Transform=Visual->GetComponentTransform();
    Points.Reserve(GrimeSamples.Num()); Normals.Reserve(GrimeSamples.Num());
    for(const auto& Sample:GrimeSamples) {
        if(FMCSurfaceWipe::Sample(GrimeMask,Sample.UV)<=.25f) continue;
        Points.Add(Transform.TransformPosition(Sample.Point));
        Normals.Add(Transform.TransformVectorNoScale(Sample.Normal).GetSafeNormal());
        if(MaskUVs) MaskUVs->Add(Sample.UV);
    }
}
bool AMCArenaTooth::FindDirtyContact(AMCToothCharacter* Worker,FVector& Point,FVector& Normal,int32 Preferred)
{
    if(!IsAvailable() || !Status->NeedsCare(true) || !IsValid(Worker) || !Worker->BrushContact->CanAcquireSurface(this)) return false;
    if(!bGrimeReliefBuilt) BuildGrimeRelief();
    const FTransform T=Visual->GetComponentTransform();
    const FVector Origin=Worker->GetActorLocation(),Aim=Origin+Worker->GetActorForwardVector()*65+FVector(0,0,65);
    float Best=MAX_flt; SelectedSample=INDEX_NONE;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCDirtyTarget),false,Worker); Query.AddIgnoredActor(this);
    for(int32 I=0;I<GrimeSamples.Num();++I) {
        // Select down to the same visible threshold used by RemainingGrime.
        const auto& S=GrimeSamples[I]; if(FMCSurfaceWipe::Sample(GrimeMask,S.UV)<=.25f) continue;
        const FVector P=T.TransformPosition(S.Point),N=T.TransformVectorNoScale(S.Normal).GetSafeNormal(),D=P-Origin;
        if(D.Size2D()>Worker->BrushContact->SurfaceReach || FVector::DotProduct(N,(Origin-P).GetSafeNormal())<.05f) continue;
        const bool Locked=Worker->BrushContact->Target==this && Worker->BrushContact->Alpha()>.5f;
        const float Distance=Locked?FVector::DistSquared(P,Worker->BrushContact->ContactPoint())*.7f+FVector::DistSquared(P,Aim)*.3f:FVector::DistSquared(P,Aim);
        // Favor low-to-high neighboring strokes, without jumping to a distant
        // low point on the other side of a crown or reaching behind the pawn.
        const float Score=Distance*(I==Preferred?.3f:1.f)+FMath::Max(0.f,float(P.Z-Origin.Z))*90.f;
        if(Score>=Best || !Worker->BrushContact->CanReachAfterFacing(P,N,this)) continue;
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
    Worker->BrushContact->Contact(this,SelectedPoint,SelectedNormal);
    if(!Worker->BrushContact->IsFacingContact() || !Worker->BrushContact->CanReach(SelectedPoint,SelectedNormal,this)) {
        History.At=-100; // Restart the stroke after turning; the old aim has no current contact.
        return true; // Keep the target while the body turns; no mask changes yet.
    }
    const FTransform Surface=Visual->GetComponentTransform();
    if(Now-History.At<.2) {
        Aim=FMath::VInterpConstantTo(Surface.TransformPosition(History.Aim),Aim,FMath::Min(Seconds,.1f),220.f);
        N=FMath::Lerp(Surface.TransformVectorNoScale(History.Normal),N,1-FMath::Exp(-24.f*Seconds)).GetSafeNormal();
    }
    History.Aim=Surface.InverseTransformPosition(Aim); History.Normal=Surface.InverseTransformVectorNoScale(N);
    FVector Up=FVector::VectorPlaneProject(FVector::UpVector,N).GetSafeNormal();
    if(Up.IsNearlyZero()) Up=FVector::VectorPlaneProject(Worker->GetActorForwardVector(),N).GetSafeNormal();
    const FVector Side=FVector::CrossProduct(Up,N).GetSafeNormal();
    // Short predominantly vertical strokes stay tangent to the enamel.
    const FVector Sweep=Side*(FMath::Sin(History.Clock*18)*4)+Up*(FMath::Sin(History.Clock*18)*10);
    FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(MCToothBrush),true,Worker);
    auto ValidContact=[&](FVector Point,FVector Normal) {
        if(FVector::Dist2D(Point,Worker->GetActorLocation())>Worker->BrushContact->SurfaceReach+20
            || !Worker->BrushContact->CanReach(Point,Normal,this)) return false;
        FHitResult Block; FCollisionQueryParams Occlusion(SCENE_QUERY_STAT(MCBrushOcclusion),false,Worker); Occlusion.AddIgnoredActor(this);
        return !GetWorld()->LineTraceSingleByChannel(Block,Worker->GetActorLocation()+FVector(0,0,40),Point-Normal*2,ECC_Visibility,Occlusion);
    };
    bool Projected=false;
    for(float StrokeScale:{1.f,.5f,.25f}) {
        if(BrushSurface->LineTraceComponent(Hit,Aim+Sweep*StrokeScale+N*25,Aim+Sweep*StrokeScale-N*25,Query)
            && ValidContact(Hit.ImpactPoint,Hit.ImpactNormal)) {Projected=true;break;}
    }
    if(!Projected) {
        // A rounded lower edge can occlude the tangential stroke. Keep the
        // already validated dirty sample instead of repeatedly dropping it.
        Hit.ImpactPoint=SelectedPoint; Hit.ImpactNormal=SelectedNormal;
        if(!ValidContact(Hit.ImpactPoint,Hit.ImpactNormal)) return false;
    }
    const auto Bounds=Appearance.Mesh?Appearance.Mesh->GetBounds():Visual->GetStaticMesh()->GetBounds();
    const FVector Size=Bounds.BoxExtent*2;
    BrushLocal=Visual->GetComponentTransform().InverseTransformPosition(Hit.ImpactPoint);
    const FVector UV=(BrushLocal-(Bounds.Origin-Bounds.BoxExtent))/Size;
    const FVector Dimensions=Size*Visual->GetComponentScale().GetAbs();
    const bool Continuous=Now-History.At<.2 && ((UV-History.UV)*Dimensions).Size()<65;
    if (GrimeMask.Num()!=FMCSurfaceWipe::Count) FMCSurfaceWipe::Reset(GrimeMask);
    Worker->BrushContact->Contact(this,Hit.ImpactPoint,Hit.ImpactNormal,&SelectedPoint);
    if(!Worker->BrushContact->IsWorkReady()) { History.At=Now; return true; }
    // A rendered local pawn must first bring its bristles to the actual stain.
    // Headless/server authority retains the geometry/time contact contract.
    if(FApp::CanEverRender() && Worker->IsLocallyControlled() && !Worker->BrushContact->IsTouchingSurface()) { History.At=-100; return true; }
    // Keep the same number of passes when artists resize a crown. The wipe
    // stays local to the current stroke; there is no timer erasing distant dirt.
    const float CoverageScale=FMath::Max(1.f,float(Dimensions.GetMax())/500.f);
    const float DirtRadius=54.f;
    const float Radius=Worker->Inventory->CleaningRadius(DirtRadius)*CoverageScale*1.25f;
    const bool AcceptedStroke=FMCSurfaceWipe::Stroke(GrimeMask,Continuous?History.UV:UV,UV,Dimensions,Radius,Seconds*2.f,&PreciseGrimeMask,Worker->Inventory->CleaningSpeedMultiplier());
    if(AcceptedStroke) {
        OnRep_Grime();if(auto* Perks=UMCPerkComponent::ForActor(Worker)) Perks->NotifyCleaned(Hit.ImpactPoint);
    }
    History.UV=UV; History.At=Now; BrushAt=Now;
    const float Left=RemainingGrime();
    // Finish the thin residual rim with the existing short fade. Requiring the
    // final few interpolated volume texels made large crowns stall on tiny tips.
    // A surface fraction gives the same finish tolerance at every tooth size.
    const int32 RemainingLayers=Left<.06f?0:FMath::Max(1,FMath::CeilToInt(Left*Status->State.CoffeeTotal));
    while(Status->State.CoffeeLeft>RemainingLayers) { Status->CareContact(true,Worker); ++Worker->SuccessfulBrushContacts; }
    if(AcceptedStroke && CareFeedback) CareFeedback->ReportAccepted(EMCToothCareFeedbackKind::Cleaning,
        Hit.ImpactPoint,Hit.ImpactNormal,!Status->NeedsCare(true));
    return true;
}

void AMCArenaTooth::UpdateGrime(float Dt)
{
    if (!Material || !FApp::CanEverRender()) return;
    if (!ReliefMaterial)
        if (auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/Care/MI_ArenaToothGrime.MI_ArenaToothGrime")))
        { ReliefMaterial=UMaterialInstanceDynamic::Create(Base,this); GrimeRelief->SetMaterial(0,ReliefMaterial); }
    // Spread one-time surface projection over frames when a coffee wave hits all teeth.
    static uint64 LastBuildFrame=MAX_uint64;
    if (GrimeAmount>0 && Status->NeedsCare(true) && !bGrimeReliefBuilt && LastBuildFrame!=GFrameCounter)
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
        const auto Bounds=Appearance.Mesh?Appearance.Mesh->GetBounds():Visual->GetStaticMesh()->GetBounds();
        ReliefMaterial->SetTextureParameterValue(TEXT("GrimeWipe"),GrimeTexture);
        ReliefMaterial->SetVectorParameterValue(TEXT("GrimeMin"),FLinearColor(Bounds.Origin-Bounds.BoxExtent));
        ReliefMaterial->SetVectorParameterValue(TEXT("GrimeSize"),FLinearColor(Bounds.BoxExtent*2));
        ReliefMaterial->SetVectorParameterValue(TEXT("GrimeScale"),FLinearColor(MeshBaseScale*GetActorScale3D().GetAbs()));
        ReliefMaterial->SetVectorParameterValue(TEXT("GrimeColor"),Material->K2_GetVectorParameterValue(TEXT("GrimeColor")));
        ReliefMaterial->SetScalarParameterValue(TEXT("GrimeAmount"),GrimeAmount*(1-GrimeFinish));
        ReliefMaterial->SetScalarParameterValue(TEXT("GrimeSeed"),State.ToothId+1);
        ReliefMaterial->SetScalarParameterValue(TEXT("GrimeThickness"),.10f);
        ReliefMaterial->SetScalarParameterValue(TEXT("BrushAge"),FMath::Clamp(float(Now-BrushAt),0.f,4.f));
        ReliefMaterial->SetVectorParameterValue(TEXT("BrushLocal"),FLinearColor(BrushLocal));
    }
}
