#include "MCFoodActor.h"
#include "MCFoodBodyComponent.h"
#include "MCFoodCutGeometry.h"
#include "MCThroat.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "MCFoodStackSettings.h"
#include "Chaos/Convex.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
// Stock slicing may produce degenerate triangles at vertices and does not
// validate its cap triangulation result. Never feed bad indices into another
// slice, the renderer, collision, or the portion-size calculation.
bool ValidateTriangles(UProceduralMeshComponent* Mesh)
{
    for(int32 S=0;S<Mesh->GetNumSections();++S) {
        const auto* Data=Mesh->GetProcMeshSection(S);
        if(!Data || Data->ProcIndexBuffer.IsEmpty()) continue;
        if(Data->ProcIndexBuffer.Num()%3) return false;
        TArray<uint32> CleanIndices;CleanIndices.Reserve(Data->ProcIndexBuffer.Num());
        FBox CleanBounds(ForceInit);
        for(int32 I=0;I<Data->ProcIndexBuffer.Num();I+=3) {
            const uint32 A=Data->ProcIndexBuffer[I],B=Data->ProcIndexBuffer[I+1],C=Data->ProcIndexBuffer[I+2];
            if(!Data->ProcVertexBuffer.IsValidIndex(A) || !Data->ProcVertexBuffer.IsValidIndex(B)
                || !Data->ProcVertexBuffer.IsValidIndex(C)) return false;
            const FVector P=Data->ProcVertexBuffer[A].Position,Q=Data->ProcVertexBuffer[B].Position,R=Data->ProcVertexBuffer[C].Position;
            if(P.ContainsNaN() || Q.ContainsNaN() || R.ContainsNaN()) return false;
            // Zero-area faces cannot render or close a solid. Removing them
            // also keeps exact vertex/edge cuts from poisoning the next cap.
            if(FVector::CrossProduct(Q-P,R-P).SizeSquared()<=1.e-20) continue;
            CleanIndices.Append({A,B,C});
            CleanBounds+=P;CleanBounds+=Q;CleanBounds+=R;
        }
        if(CleanIndices.Num()!=Data->ProcIndexBuffer.Num()) {
            FProcMeshSection Clean=*Data;Clean.ProcIndexBuffer=MoveTemp(CleanIndices);Clean.SectionLocalBox=CleanBounds;
            Mesh->SetProcMeshSection(S,Clean);
        }
    }
    return true;
}

bool ValidateKeptSpaces(UProceduralMeshComponent* Mesh,const TArray<FPlane>& Spaces,int32 CapSection)
{
    constexpr double PlaneTolerance=1.e-3;
    for(int32 S=0;S<Mesh->GetNumSections();++S) {
        const auto* Data=Mesh->GetProcMeshSection(S);
        if(!Data) continue;
        for(uint32 Index:Data->ProcIndexBuffer) {
            const FVector P=Data->ProcVertexBuffer[Index].Position;
            for(const FPlane& Space:Spaces) if(Space.PlaneDot(P)<-PlaneTolerance) return false;
            if(S==CapSection && FMath::Abs(Spaces.Last().PlaneDot(P))>PlaneTolerance) return false;
        }
    }
    return true;
}

// The same geometry operation is used for preflight and for the visible result.
// Preview components are unregistered, with no collision triangles or actors.
bool SliceGeometry(UProceduralMeshComponent* Target,UStaticMeshComponent* Source,
    const FTransform& FoodPose,const TArray<FMCFoodCutPlane>& Planes,UMaterialInterface* CapMaterial)
{
    UStaticMesh* Mesh=Source?Source->GetStaticMesh():nullptr;
    if(!Mesh || !Mesh->bAllowCPUAccess) return false;
    Target->ClearAllMeshSections();
    for(int32 Section=0;Section<Mesh->GetNumSections(0);++Section) {
        TArray<FVector> Vertices,Normals; TArray<int32> Triangles;
        TArray<FVector2D> UV; TArray<FProcMeshTangent> Tangents;
        UKismetProceduralMeshLibrary::GetSectionFromStaticMesh(Mesh,0,Section,Vertices,Triangles,Normals,UV,Tangents);
        TArray<FLinearColor> Colors;
        Target->CreateMeshSection_LinearColor(Section,Vertices,Triangles,Normals,UV,Colors,Tangents,false);
        Target->SetMaterial(Section,Source->GetMaterial(Section));
    }
    if(!ValidateTriangles(Target)) return false;
    const FVector Scale=FoodPose.GetScale3D();
    if(Scale.GetAbs().GetMin()<SMALL_NUMBER) return false;
    TArray<FPlane> KeptSpaces;
    for(const auto& Plane:Planes) {
        if(!Plane.bEnabled) {
            if(Plane.IslandIndex<0 || !MCFoodCutGeometry::FilterIsland(Target,Plane.IslandIndex)) return false;
            continue;
        }
        const FVector WorldPoint=FoodPose.TransformPosition(Plane.Position);
        const FVector WorldNormal=FoodPose.TransformVectorNoScale(Plane.Normal/Scale).GetSafeNormal();
        if(!Plane.bEnabled || WorldNormal.IsNearlyZero()) return false;
        const int32 CapSection=Target->GetNumSections();
        if(!MCFoodCutGeometry::SliceAndCap(Target,WorldPoint,WorldNormal,CapMaterial)) return false;
        const FTransform ComponentPose=Target->GetComponentTransform();
        const FVector LocalPoint=ComponentPose.InverseTransformPosition(WorldPoint);
        const FVector LocalNormal=(ComponentPose.GetScale3D()*ComponentPose.InverseTransformVectorNoScale(WorldNormal)).GetSafeNormal();
        KeptSpaces.Emplace(LocalPoint,LocalNormal);
        if(!ValidateTriangles(Target) || !ValidateKeptSpaces(Target,KeptSpaces,CapSection)) return false;
        if(Plane.IslandIndex>=0 && !MCFoodCutGeometry::FilterIsland(Target,Plane.IslandIndex)) return false;
    }
    return true;
}

TArray<FVector> ReferencedVertices(UProceduralMeshComponent* Mesh,const FTransform& Transform)
{
    TArray<FVector> Vertices;
    for(int32 Section=0;Section<Mesh->GetNumSections();++Section) {
        if(const FProcMeshSection* Data=Mesh->GetProcMeshSection(Section)) {
            TBitArray<> Used(false,Data->ProcVertexBuffer.Num());
            for(uint32 Index:Data->ProcIndexBuffer) {
                if(!Data->ProcVertexBuffer.IsValidIndex(Index) || Used[Index]) continue;
                Used[Index]=true;
                Vertices.Add(Transform.TransformPosition(Data->ProcVertexBuffer[Index].Position));
            }
        }
    }
    return Vertices;
}

struct FPortionShape
{
    double Volume=0;
    double MinimumWidth=0;
};

FPortionShape MeasureShape(const TArray<FVector>& Vertices)
{
    if(Vertices.Num()<4) return {};
    TArray<Chaos::FConvex::FVec3Type> Points; Points.Reserve(Vertices.Num());
    for(const FVector& V:Vertices) {
        if(V.ContainsNaN()) return {};
        Points.Add(Chaos::FConvex::FVec3Type(V));
    }
    const Chaos::FConvex Hull(Points,0);
    if(!Hull.IsValidGeometry() || !FMath::IsFinite(Hull.GetVolume())) return {};
    double Width=DBL_MAX;
    for(int32 Face=0;Face<Hull.NumPlanes();++Face) {
        const FVector Normal=FVector(Hull.GetPlane(Face).Normal()).GetSafeNormal();
        // Chaos can retain a degenerate plane when nearly coplanar authored
        // vertices merge. A zero normal is not a supporting direction.
        if(Normal.ContainsNaN() || Normal.IsNearlyZero()) continue;
        double Low=DBL_MAX,High=-DBL_MAX;
        for(int32 V=0;V<Hull.NumVertices();++V) {
            const double D=FVector::DotProduct(FVector(Hull.GetVertex(V)),Normal);
            Low=FMath::Min(Low,D);High=FMath::Max(High,D);
        }
        Width=FMath::Min(Width,High-Low);
    }
    return {Hull.GetVolume(),FMath::IsFinite(Width) && Width<DBL_MAX?Width:0};
}

int32 EnabledCutCount(const TArray<FMCFoodCutPlane>& History)
{
    int32 Result=0;for(const auto& Step:History)Result+=Step.bEnabled?1:0;return Result;
}

double IslandWeight(const MCFoodCutGeometry::FIsland& Island,const FTransform& GeometryTransform)
{
    TArray<FVector> Points;Points.Reserve(Island.Vertices.Num());FBox Bounds(ForceInit);
    for(const FVector& P:Island.Vertices){const FVector V=GeometryTransform.TransformPosition(P);Points.Add(V);Bounds+=V;}
    const double Volume=MeasureShape(Points).Volume;
    if(FMath::IsFinite(Volume) && Volume>1.e-9)return Volume;
    // A real one-sided skin sliver still receives a finite share. It must not
    // disappear from the transaction just because it has no rigid-body volume.
    const FVector Size=Bounds.GetSize();
    return FMath::Max(1.e-9,FMath::Max3(Size.X*Size.Y,Size.X*Size.Z,Size.Y*Size.Z)*.001);
}
}

FTransform AMCFoodActor::PortionSourceTransform() const
{
    // Impact squash animates Visual. Gameplay and reconstructed halves must
    // always measure the authored mesh rather than that temporary reaction.
    const FVector Scale=bFragment?FoodData.FragmentScale:FoodData.Scale;
    return ItemMesh?FTransform(FQuat::Identity,-ItemMesh->GetBounds().Origin*Scale,Scale):CanonicalModelLocal;
}

FVector AMCFoodActor::PortionExtent() const
{
    if(bCutVisualReady && CutStackLocalBounds.IsValid)
        return CutStackLocalBounds.GetExtent()*GetActorScale3D().GetAbs();
    if(ItemMesh && Visual)
        return ItemMesh->GetBoundingBox().TransformBy(PortionSourceTransform()).GetExtent()*GetActorScale3D().GetAbs();
    return Body->GetScaledBoxExtent();
}

FVector AMCFoodActor::GetHandPortionSize() const
{
    const FVector Extent=PortionExtent();
    return FMCFoodStackSettings::RotatedExtent(Extent,FoodData.Stack.RestRotation(ItemMesh,Extent,0))*2;
}

bool AMCFoodActor::FitsHandPortion() const
{
    if(FoodData.Kind!=EMCFoodKind::Food || bBrushTool || IsNutEncounterFragment()) return true;
    const FVector Size=GetHandPortionSize();
    const FVector Limit=Settings.MaxHandDimensions;
    return !Size.ContainsNaN() && Size.GetMin()>0 && Size.X<=Limit.X+KINDA_SMALL_NUMBER
        && Size.Y<=Limit.Y+KINDA_SMALL_NUMBER && Size.Z<=Limit.Z+KINDA_SMALL_NUMBER;
}

bool AMCFoodActor::FitsAutomaticIntakePortion() const
{
    if(FoodData.Kind!=EMCFoodKind::Food || bBrushTool || IsNutEncounterFragment()) return true;
    const FVector Size=GetHandPortionSize();
    const FVector Limit=Settings.MaxAutoConsumeDimensions;
    return !Size.ContainsNaN() && Size.GetMin()>=0 && Size.GetMax()>SMALL_NUMBER && Size.X<=Limit.X+KINDA_SMALL_NUMBER
        && Size.Y<=Limit.Y+KINDA_SMALL_NUMBER && Size.Z<=Limit.Z+KINDA_SMALL_NUMBER;
}

bool AMCFoodActor::SupportsPlanarCut() const
{
    return FoodData.Kind==EMCFoodKind::Food && !IsNutEncounterFragment()
        && ItemMesh && ItemMesh->bAllowCPUAccess;
}

bool AMCFoodActor::SpawnPortionActors(const TArray<TArray<FMCFoodCutPlane>>& Histories,
    const TArray<double>& Weights,const TArray<FBox>& LocalBounds,FVector Direction,TArray<AMCFoodActor*>& OutPieces)
{
    OutPieces.Reset();
    if(Histories.IsEmpty() || Histories.Num()!=Weights.Num() || Histories.Num()!=LocalBounds.Num() || Histories.Num()>512)return false;
    double Total=0;for(double W:Weights){if(!FMath::IsFinite(W) || W<=0)return false;Total+=W;}
    if(!FMath::IsFinite(Total) || Total<=0)return false;
    const int32 Budget=bFragment?CutAwardUnits:FoodData.FragmentCount();
    if(Budget<0 || !FMath::IsFinite(FoodData.Mass) || FoodData.Mass<=0)return false;
    TArray<int32> Units,Order;TArray<double> Remainders;Units.SetNum(Weights.Num());
    int32 Assigned=0;
    for(int32 I=0;I<Weights.Num();++I){
        const double Exact=double(Budget)*Weights[I]/Total;Units[I]=FMath::FloorToInt(Exact);
        Assigned+=Units[I];Remainders.Add(Exact-Units[I]);Order.Add(I);
    }
    Order.Sort([&](int32 A,int32 B){return Remainders[A]==Remainders[B]?A<B:Remainders[A]>Remainders[B];});
    for(int32 I=0;I<Budget-Assigned;++I)++Units[Order[I%Order.Num()]];
    if(!UsesAutomaticCutIntake() && Units.Num()==2){Units[0]=(Budget+1)/2;Units[1]=Budget/2;}
    bool HasIntake=false;
    for(TActorIterator<AMCThroat> It(GetWorld());It;++It)
        if(!It->IsActorBeingDestroyed() && !It->VacuumInlet().ContainsNaN()){HasIntake=true;break;}
    const FTransform Pose=GetActorTransform();
    for(int32 I=0;I<Histories.Num();++I){
        if(!LocalBounds[I].IsValid)break;
        const FVector Extent=LocalBounds[I].GetExtent()*Pose.GetScale3D().GetAbs();
        const FVector Size=FMCFoodStackSettings::RotatedExtent(Extent,FoodData.Stack.RestRotation(ItemMesh,Extent,0))*2;
        const FVector Limit=Settings.MaxAutoConsumeDimensions;
        const bool Ready=UsesAutomaticCutIntake() && !Size.ContainsNaN() && Size.GetMin()>=0 && Size.GetMax()>SMALL_NUMBER
            && Size.X<=Limit.X+KINDA_SMALL_NUMBER && Size.Y<=Limit.Y+KINDA_SMALL_NUMBER && Size.Z<=Limit.Z+KINDA_SMALL_NUMBER;
        if(Ready && !HasIntake)break;
        auto* Piece=GetWorld()->SpawnActorDeferred<AMCFoodActor>(StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Piece)break;
        OutPieces.Add(Piece);
        const float Mass=float(double(FoodData.Mass)*Weights[I]/Total);
        Piece->FoodData=FoodData;Piece->FoodData.FragmentScale=bFragment?FoodData.FragmentScale:FoodData.Scale;
        Piece->FoodData.Mass=Mass;Piece->FoodData.Health=25.f;
        Piece->ItemName=ItemName;Piece->ItemMesh=ItemMesh;Piece->bFragment=true;Piece->Health=25.f;
        Piece->Batch=Batch;Piece->LastHandledBy=LastHandledBy;Piece->SetOwner(GetOwner());
        Piece->CutAwardUnits=Units[I];Piece->bSpoiled=bSpoiled;Piece->SpoilAt=SpoilAt;Piece->EntryRecoveryLanding=EntryRecoveryLanding;
        if(FoodData.Kind==EMCFoodKind::Food && !HasFreshnessTimer() && ActorHasTag(TEXT("MCNutRain")))Piece->Tags.AddUnique(TEXT("MCNutRain"));
        Piece->CutPlanes=Histories[I];Piece->Settings=Settings;Piece->Profile=Profile;
        if(Ready){
            // There is no frame where this tiny fragment is a visible/physical
            // network actor. Its stable GameState cue owns the entire flight.
            Piece->bCutIntakeReceipt=true;Piece->CutIntakeReceiptBounds=LocalBounds[I];
            Piece->SetReplicates(false);Piece->Phase=EMCFoodPhase::Disposed;
        }
        Piece->FinishSpawning(Pose);
        Piece->Settings=Settings;Piece->Settings.Mass=Mass;Piece->FoodData.Mass=Mass;
        const bool Built=Ready?Piece->bCutVisualReady:Piece->BuildCutVisual();
        if(!Built)break;
        if(Ready)Piece->Phase=EMCFoodPhase::Free; // Keep the already hidden, collision-free receipt until accepted below.
        else {
            Piece->Body->SetMassOverrideInKg(NAME_None,Mass,true);
            Piece->Body->SetPhysicsLinearVelocity(Direction.GetSafeNormal()*12+FVector(0,0,45));
            Piece->ForceNetUpdate();
        }
    }
    if(OutPieces.Num()!=Histories.Num() || OutPieces.ContainsByPredicate([](const AMCFoodActor* P){return !IsValid(P)||!P->bCutVisualReady;})){
        for(auto* Piece:OutPieces)if(IsValid(Piece))Piece->Destroy();OutPieces.Reset();return false;
    }
    UE_LOG(LogMCFoodCuts,Log,TEXT("MC_FOOD_ISLANDS_STAGED source=%s item=%s batch=%d parts=%d ready=%d mass=%.6f units=%d"),
        *GetName(),*ItemName.ToString(),Batch,OutPieces.Num(),
        OutPieces.FilterByPredicate([](const AMCFoodActor* P){return P->bCutIntakeReceipt;}).Num(),FoodData.Mass,Budget);
    return true;
}

int32 AMCFoodActor::SpawnDisconnectedPortions(FVector Direction,TArray<AMCFoodActor*>& OutPieces)
{
    OutPieces.Reset();
    if(!HasAuthority() || !UsesAutomaticCutIntake() || !SupportsPlanarCut())return 0;
    const FTransform Pose=GetActorTransform();
    TStrongObjectPtr<UProceduralMeshComponent> Source(NewObject<UProceduralMeshComponent>(GetTransientPackage(),NAME_None,RF_Transient));
    Source->SetWorldTransform(PortionSourceTransform()*Pose);
    if(!SliceGeometry(Source.Get(),Visual,Pose,CutPlanes,nullptr))return -1;
    TArray<MCFoodCutGeometry::FIsland> Islands;
    if(!MCFoodCutGeometry::FindIslands(Source.Get(),Islands))return -1;
    if(Islands.Num()<=1)return 0;
    TArray<TArray<FMCFoodCutPlane>> Histories;TArray<double> Weights;TArray<FBox> Bounds;
    const FTransform GeometryTransform=PortionSourceTransform()*FTransform(FQuat::Identity,FVector::ZeroVector,Pose.GetScale3D());
    for(const auto& Island:Islands){
        auto History=CutPlanes;auto& Selection=History.AddDefaulted_GetRef();Selection.IslandIndex=Island.Index;
        Histories.Add(MoveTemp(History));Weights.Add(IslandWeight(Island,GeometryTransform));
        Bounds.Add(Island.Bounds.TransformBy(PortionSourceTransform()));
    }
    return SpawnPortionActors(Histories,Weights,Bounds,Direction,OutPieces)?1:-1;
}

bool AMCFoodActor::SpawnCutFragments(FVector Direction,FVector WorldPoint,FVector WorldNormal,TArray<AMCFoodActor*>* OutPieces)
{
    if(!HasAuthority() || !SupportsPlanarCut() || (UsesAutomaticCutIntake()?FitsAutomaticIntakePortion():FitsHandPortion()) || EnabledCutCount(CutPlanes)>=12
        || WorldPoint.ContainsNaN() || WorldNormal.ContainsNaN()
        || WorldNormal.IsNearlyZero()) return false;
    const FTransform Pose=GetActorTransform();
    const FVector LocalPoint=Pose.InverseTransformPosition(WorldPoint);
    const FVector LocalNormal=(Pose.GetScale3D()*Pose.InverseTransformVectorNoScale(WorldNormal)).GetSafeNormal();
    if(LocalNormal.IsNearlyZero()) return false;
    const FTransform GeometryTransform=PortionSourceTransform()*FTransform(FQuat::Identity,FVector::ZeroVector,Pose.GetScale3D());
    FPortionShape Original;
    if(!UsesAutomaticCutIntake()){
        TStrongObjectPtr<UProceduralMeshComponent> Source(NewObject<UProceduralMeshComponent>(GetTransientPackage(),NAME_None,RF_Transient));
        Source->SetWorldTransform(PortionSourceTransform()*Pose);
        if(!SliceGeometry(Source.Get(),Visual,Pose,{},nullptr)) return false;
        Original=MeasureShape(ReferencedVertices(Source.Get(),GeometryTransform));
        if(Original.Volume<=0 || Original.MinimumWidth<=0) return false;
    }
    // Thin authored food may be divided lengthwise without changing its thin
    // axis. Always compare with the ORIGINAL source, never a shrinking parent.
    const double RequiredWidth=FMath::Min(double(Settings.MinCutThickness),Original.MinimumWidth);
    const double WidthTolerance=FMath::Max(.01,RequiredWidth*.001);
    TArray<FMCFoodCutPlane> CandidatePlanes[2];
    double Volumes[2]={0,0};
    TArray<TArray<FMCFoodCutPlane>> Histories;TArray<double> Weights;TArray<FBox> Bounds;
    for(int32 I=0;I<2;++I) {
        CandidatePlanes[I]=CutPlanes;
        auto& Plane=CandidatePlanes[I].AddDefaulted_GetRef();
        Plane.bEnabled=true; Plane.Position=LocalPoint; Plane.Normal=LocalNormal*(I==0?1:-1);
        TStrongObjectPtr<UProceduralMeshComponent> Preview(NewObject<UProceduralMeshComponent>(GetTransientPackage(),NAME_None,RF_Transient));
        Preview->SetWorldTransform(PortionSourceTransform()*Pose);
        if(!SliceGeometry(Preview.Get(),Visual,Pose,CandidatePlanes[I],nullptr)) return false;
        if(UsesAutomaticCutIntake()){
            TArray<MCFoodCutGeometry::FIsland> Islands;
            if(!MCFoodCutGeometry::FindIslands(Preview.Get(),Islands) || Islands.IsEmpty())return false;
            for(const auto& Island:Islands){
                auto History=CandidatePlanes[I];History.Last().IslandIndex=Island.Index;
                Histories.Add(MoveTemp(History));Weights.Add(IslandWeight(Island,GeometryTransform));
                Bounds.Add(Island.Bounds.TransformBy(PortionSourceTransform()));
            }
            continue;
        }
        // Translation and world rotation cannot affect volume or measured depth.
        const TArray<FVector> Vertices=ReferencedVertices(Preview.Get(),GeometryTransform);
        const FPortionShape Shape=MeasureShape(Vertices);
        Volumes[I]=Shape.Volume;
        if(Shape.MinimumWidth+WidthTolerance<RequiredWidth) {
            UE_LOG(LogMCFoodCuts,Verbose,TEXT("Portion refused: width %.4f < %.4f cm"),Shape.MinimumWidth,RequiredWidth);return false;
        }
        double Min=DBL_MAX,Max=-DBL_MAX;
        const FVector Normal=Pose.InverseTransformVectorNoScale(WorldNormal).GetSafeNormal();
        for(const FVector& V:Vertices) {const double D=FVector::DotProduct(V,Normal);Min=FMath::Min(Min,D);Max=FMath::Max(Max,D);}
        if(Volumes[I]<Settings.MinCutVolume || Max-Min<Settings.MinCutThickness) {
            UE_LOG(LogMCFoodCuts,Verbose,TEXT("Portion refused: volume %.4f cm3, new depth %.4f cm"),Volumes[I],Max-Min);return false;
        }
        Histories.Add(CandidatePlanes[I]);Weights.Add(Volumes[I]);
        FBox LocalBounds(ForceInit);for(const FVector& P:ReferencedVertices(Preview.Get(),PortionSourceTransform()))LocalBounds+=P;
        Bounds.Add(LocalBounds);
    }
    TArray<AMCFoodActor*> Pieces;
    if(!SpawnPortionActors(Histories,Weights,Bounds,Direction,Pieces))return false;
    for(int32 I=0;I<Pieces.Num();++I) {
        auto* Piece=Pieces[I];
        if(!Piece->bCutIntakeReceipt){
            Piece->Body->SetPhysicsLinearVelocity(Direction.GetSafeNormal()*12+WorldNormal.GetSafeNormal()*(I%2==0?25.f:-25.f)+FVector(0,0,45));
            Piece->ForceNetUpdate();
        }
    }
    if(OutPieces) *OutPieces=Pieces;
    return true;
}

bool AMCFoodActor::BuildCutVisual()
{
    bCutVisualReady=false;
    CutStackLocalBounds=FBox(ForceInit);
    if(!Visual) return false;
    if(bCutIntakeReceipt){
        CutStackLocalBounds=CutIntakeReceiptBounds;bCutVisualReady=CutStackLocalBounds.IsValid!=0;
        Visual->SetHiddenInGame(true);return bCutVisualReady;
    }
    if(CutVisual) { CutVisual->ClearAllMeshSections(); CutVisual->SetHiddenInGame(true); }
    Visual->SetHiddenInGame(!CutPlanes.IsEmpty());
    if(CutPlanes.IsEmpty()) return false;
    auto Fail=[&](){
        bCutVisualReady=false;CutStackLocalBounds=FBox(ForceInit);
        SetActorHiddenInGame(true);Body->SetSimulatePhysics(false);Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);return false;
    };
    if(!ItemMesh || !ItemMesh->bAllowCPUAccess) return Fail();
    if(!CutVisual) {
        CutVisual=NewObject<UProceduralMeshComponent>(this,TEXT("CutFoodMesh"));
        CutVisual->SetupAttachment(PresentationRoot); CutVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        CutVisual->RegisterComponent();
    }
    CutVisual->SetRelativeTransform(PortionSourceTransform());
    // The cut surface has its own opaque color; the source skin texture is
    // authored for UVs on the outside and stretches across generated caps.
    const FString MeshName=ItemMesh->GetName();
    const TCHAR* Color=MeshName.Contains(TEXT("Carrot"))?TEXT("Orange"):
        MeshName.Contains(TEXT("Tomato"))?TEXT("Tomato"):
        MeshName.Contains(TEXT("Strawberry"))?TEXT("Strawberry"):
        (MeshName.Contains(TEXT("Broccoli")) || MeshName.Contains(TEXT("Cabbage")))?TEXT("Green"):TEXT("Cream");
    const FString MaterialPath=FString::Printf(TEXT("/Game/Gameplay/Food/CutMaterials/MI_FoodCut_%s.MI_FoodCut_%s"),Color,Color);
    UMaterialInterface* CutMaterial=LoadObject<UMaterialInterface>(nullptr,*MaterialPath);
    if(!CutMaterial) CutMaterial=Visual->GetMaterial(0);
    if(!SliceGeometry(CutVisual,Visual,PresentationRoot->GetComponentTransform(),CutPlanes,CutMaterial)) return Fail();
    const TArray<FVector> Vertices=ReferencedVertices(CutVisual,CutVisual->GetRelativeTransform());
    auto* FoodBody=Cast<UMCFoodBodyComponent>(Body);
    if(Vertices.Num()<3 || !FoodBody) return Fail();
    for(const FVector& Vertex:Vertices) CutStackLocalBounds+=Vertex;
    bCutVisualReady=true;
    if(!FoodBody->SetCutCollision(Vertices)){
        if(!UsesAutomaticCutIntake())return Fail();
        if(FitsAutomaticIntakePortion()){
            Body->SetSimulatePhysics(false);Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }else{
            // An authored one-sided leaf can remain larger than the intake
            // limit. Give its query/rigid proxy a tiny thickness, while keeping
            // its visible geometry and measured size completely unchanged.
            FVector Normal=FVector::ZeroVector,Edge=FVector::ZeroVector;
            for(int32 I=1;I<Vertices.Num();++I){
                if(Edge.IsNearlyZero()){Edge=Vertices[I]-Vertices[0];continue;}
                Normal=FVector::CrossProduct(Edge,Vertices[I]-Vertices[0]).GetSafeNormal();
                if(!Normal.IsNearlyZero())break;
            }
            if(Normal.IsNearlyZero())return Fail();
            TArray<FVector> Proxy;Proxy.Reserve(Vertices.Num()*2);
            for(const FVector& P:Vertices){Proxy.Add(P+Normal*.05);Proxy.Add(P-Normal*.05);}
            if(!FoodBody->SetCutCollision(Proxy))return Fail();
        }
    }
    Visual->SetHiddenInGame(true); CutVisual->SetHiddenInGame(false);
    bCutVisualReady=true;OnRep_Phase();return true;
}
