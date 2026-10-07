#include "MCArenaTooth.h"
#include "MCSurfaceWipe.h"
#include "MCInventoryComponent.h"
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
#include "MCTongue.h"
#include "EngineUtils.h"
#include "Components/BoxComponent.h"

void AMCArenaTooth::BuildGrimeRelief()
{
    if (bGrimeReliefBuilt || !Visual->GetStaticMesh() || !BrushSurface->IsPhysicsStateCreated()) return;
    GrimeSamples.Reset();
    const FTransform Transform=Visual->GetComponentTransform();
    const FBoxSphereBounds Bounds=Visual->GetStaticMesh()->GetBounds();
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
    TArray<AMCArenaTooth*> Neighbors;
    for(TActorIterator<AMCArenaTooth> It(GetWorld());It;++It) {
        FloorRoom.AddIgnoredActor(*It);
        if(*It!=this && It->IsAvailable()) Neighbors.Add(*It);
    }
    auto SmoothUnion=[](float A,float B,float K)
    {
        const float H=FMath::Clamp(.5f+.5f*(B-A)/K,0.f,1.f);
        return FMath::Lerp(B,A,H)-K*H*(1-H);
    };
    // Two projection windows share the existing mesh budget. Each contains broad,
    // irregular islands scaled to this crown, with detail still authored in cm.
    for (int32 Patch=0;Patch<2;++Patch)
    {
        const float Angle=Side*(Patch==0?-.22f:.30f)+Random.FRandRange(-.08f,.08f);
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
        const float Width=FMath::Clamp(float(HalfSpan(X))*.42f,60.f,96.f)*(Patch==0?1.f:.9f)*Random.FRandRange(.93f,1.06f);
        const float Height=FMath::Clamp(ToothHeight*(Patch==0?.12f:.095f),34.f,58.f)*Random.FRandRange(.95f,1.05f);
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
        // Keep both windows above it and within the lower reachable face, then
        // cache their adjusted footprint once rather than scanning per vertex.
        if(FloorZ> -MAX_flt) {
            const float RaisedZ=FMath::Min(Visual->Bounds.GetBox().Max.Z-12,
                FMath::Clamp(WorldAim.Z,double(FloorZ+(Patch==0?90:52)),double(FloorZ+(Patch==0?110:65))));
            if(FMath::Abs(RaisedZ-WorldAim.Z)>UE_SMALL_NUMBER) {
                const FVector RaisedAim(WorldAim.X,WorldAim.Y,RaisedZ);
                FHitResult RaisedHit;
                if(BrushSurface->LineTraceComponent(RaisedHit,RaisedAim+Radial*600,RaisedAim-Radial*300,Query)) {
                    WorldAim=RaisedAim; CenterHit=RaisedHit;
                    N=CenterHit.ImpactNormal.GetSafeNormal();
                    Y=FVector::VectorPlaneProject(FVector::UpVector,N).GetSafeNormal();
                    X=FVector::CrossProduct(Y,N).GetSafeNormal();
                    FloorZ=CacheFloor();
                }
            }
        }
        constexpr int32 Columns=96,SampleStride=4;
        const int32 Rows=Patch==0?192:80,Base=Vertices.Num();
        struct FLobe { FVector2D Center,Radius,Rotation; };
        TArray<FLobe> Lobes;
        for (int32 L=0;L<3;++L)
        {
            const FVector2D Center=L==0?FVector2D(-.26,.16):L==1?FVector2D(.28,-.08):FVector2D(-.02,-.55);
            const float Size=L==2?.38f:1.f;
            const FVector2D Position((Center.X+Random.FRandRange(-.04f,.04f))*Width,(Center.Y+Random.FRandRange(-.06f,.06f))*Height);
            const FVector2D Radius(Width*Size*Random.FRandRange(.36f,.40f),Height*Size*Random.FRandRange(.46f,.54f));
            const float LobeAngle=Random.FRandRange(-.35f,.35f);
            Lobes.Add({Position,Radius,FVector2D(FMath::Cos(LobeAngle),FMath::Sin(LobeAngle))});
        }
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
            float D=MAX_flt;
            for (const auto& L:Lobes)
            {
                const FVector2D Delta=PatchPoint-L.Center;
                const FVector2D Rotated(Delta.X*L.Rotation.X-Delta.Y*L.Rotation.Y,Delta.X*L.Rotation.Y+Delta.Y*L.Rotation.X);
                const float E=(Rotated/L.Radius).Size()-1;
                D=SmoothUnion(D,E*FMath::Min(L.Radius.X,L.Radius.Y),2.5f);
            }
            D+=3.f*FMath::PerlinNoise2D(PatchPoint*.065f+FVector2D(State.ToothId*11,Patch*19))
                +.65f*FMath::Sin(PatchPoint.X*.5f+PatchPoint.Y*.19f)*FMath::Sin(PatchPoint.Y*.47f+State.ToothId);
            float Coverage=1-FMath::SmoothStep(-1.5f,1.5f,D);
            const FVector Plane=CenterHit.ImpactPoint+X*(Q.X*Width)+Y*(Q.Y*Height);
            FHitResult Hit;
            bool bHit=D<8.f && BrushSurface->LineTraceComponent(Hit,Plane+N*150,Plane-N*210,Query)
                && FVector::DotProduct(Hit.ImpactNormal,N)>.22f;
            if(bHit) {
                // Leave space for the whole brush head above the tongue/gum,
                // and keep the coating on the face visible from the mouth.
                Coverage*=FMath::SmoothStep(FloorZ+22,FloorZ+38,float(Hit.ImpactPoint.Z))
                    *FMath::SmoothStep(.45f,.70f,float(FVector::DotProduct(Hit.ImpactNormal,Inward)));
                if(FloorZ> -MAX_flt) Coverage*=1-FMath::SmoothStep(FloorZ+160,FloorZ+180,float(Hit.ImpactPoint.Z));
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
            const float Depth=.12f+Interior*(.18f+FMath::Min(ClumpDepth*.42f,1.6f));
            const FVector WorldPoint=bHit?Hit.ImpactPoint+Hit.ImpactNormal*Depth:Plane;
            WorldPoints.Add(WorldPoint); Valid.Add(bHit);
            Vertices.Add(Transform.InverseTransformPosition(WorldPoint));
            Normals.Add((Transform.InverseTransformVectorNoScale(bHit?Hit.ImpactNormal:N)*Transform.GetScale3D()).GetSafeNormal());
            UV.Add((Q+FVector2D(1,1))*.5);
            Colors.Add(FLinearColor(Coverage,Grain,FMath::Clamp((Depth-.12f)/1.4f,0.f,1.f),1));
            Tangents.Add(FProcMeshTangent(Transform.InverseTransformVectorNoScale(X).GetSafeNormal(),false));
            const FVector Local=Transform.InverseTransformPosition(bHit?Hit.ImpactPoint:Plane);
            Candidates.Add({Local,Transform.InverseTransformVectorNoScale(bHit?Hit.ImpactNormal:N).GetSafeNormal(),
                (Local-(Bounds.Origin-Bounds.BoxExtent))/(Bounds.BoxExtent*2),Coverage});
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
    }
    if(FApp::CanEverRender()) GrimeRelief->CreateMeshSection_LinearColor(0,Vertices,Triangles,Normals,UV,Colors,Tangents,false);
    bGrimeReliefBuilt=true;
}

void AMCArenaTooth::ResetGrime()
{
    if (!HasAuthority()) return;
    FMCSurfaceWipe::Reset(GrimeMask); PreciseGrimeMask.Reset(); BrushHistory.Reset(); BrushAt=-100;
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
void AMCArenaTooth::GetDirtyContactSamples(TArray<FVector>& Points,TArray<FVector>& Normals)
{
    Points.Reset(); Normals.Reset();
    if(!IsAvailable() || !Status->NeedsCare(true)) return;
    if(!bGrimeReliefBuilt) BuildGrimeRelief();
    const FTransform Transform=Visual->GetComponentTransform();
    Points.Reserve(GrimeSamples.Num()); Normals.Reserve(GrimeSamples.Num());
    for(const auto& Sample:GrimeSamples) {
        if(FMCSurfaceWipe::Sample(GrimeMask,Sample.UV)<=.25f) continue;
        Points.Add(Transform.TransformPosition(Sample.Point));
        Normals.Add(Transform.TransformVectorNoScale(Sample.Normal).GetSafeNormal());
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
        const float Score=Distance*(I==Preferred?.3f:1.f);
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
    const FVector Up=FVector::VectorPlaneProject(FVector::UpVector,N).GetSafeNormal(),Side=FVector::CrossProduct(Up,N).GetSafeNormal();
    // Small tangential strokes stay on the chosen stain; project each stroke back onto enamel.
    const FVector Sweep=Side*(FMath::Sin(History.Clock*24)*3)+Up*(FMath::Sin(History.Clock*12)*2.5f);
    FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(MCToothBrush),true,Worker);
    auto ValidContact=[&](FVector Point,FVector Normal) {
        if(FVector::Dist2D(Point,Worker->GetActorLocation())>Worker->BrushContact->SurfaceReach+20
            || !Worker->BrushContact->CanReach(Point,Normal,this)) return false;
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
    Worker->BrushContact->Contact(this,Hit.ImpactPoint,Hit.ImpactNormal,&SelectedPoint);
    if(!Worker->BrushContact->IsWorkReady()) { History.At=Now; return true; }
    const float DirtRadius=GrimeRelief?FMath::Clamp(float(GrimeRelief->Bounds.SphereRadius)*.6f,36.f,120.f):36.f;
    if(FMCSurfaceWipe::Stroke(GrimeMask,Continuous?History.UV:UV,UV,Dimensions,Worker->Inventory->CleaningRadius(DirtRadius),Seconds,&PreciseGrimeMask,Worker->Inventory->CleaningSpeedMultiplier())) OnRep_Grime();
    History.UV=UV; History.At=Now; BrushAt=Now;
    const float Left=RemainingGrime();
    const int32 RemainingLayers=Left<.025f?0:FMath::Max(1,FMath::CeilToInt(Left*Status->State.CoffeeTotal));
    while(Status->State.CoffeeLeft>RemainingLayers) { Status->CareContact(true,Worker); ++Worker->SuccessfulBrushContacts; }
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
