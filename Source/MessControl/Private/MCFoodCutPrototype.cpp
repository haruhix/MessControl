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
#include "StaticMeshResources.h"
#include "Materials/MaterialInterface.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "MCFoodStackSettings.h"
#include "MCFoodCutStagingSubsystem.h"
#include "MCFoodProcessingRecoverySubsystem.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCPlayerState.h"
#include "Components/BoxComponent.h"
#include "HAL/PlatformTime.h"
#include "Chaos/Convex.h"
#include "UObject/StrongObjectPtr.h"

struct FMCPreparedFoodCutKey
{
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<UStaticMesh> Mesh;
    const FStaticMeshRenderData* RenderData=nullptr;
    FGuid Revision;
    FString DerivedKey;
    FTransform SourceTransform;
    TArray<TWeakObjectPtr<UMaterialInterface>> Materials;
    TWeakObjectPtr<UMaterialInterface> Cap;
    TArray<FMCFoodCutPlane> History;
    bool operator==(const FMCPreparedFoodCutKey& B) const
    {
        if(World!=B.World || Mesh!=B.Mesh || RenderData!=B.RenderData || Revision!=B.Revision || DerivedKey!=B.DerivedKey
            || !SourceTransform.Equals(B.SourceTransform,0) || Materials!=B.Materials || Cap!=B.Cap || History.Num()!=B.History.Num())return false;
        for(int32 I=0;I<History.Num();++I){const auto& A=History[I];const auto& C=B.History[I];if(A.bEnabled!=C.bEnabled || A.IslandIndex!=C.IslandIndex || !A.Position.Equals(C.Position,0) || !A.Normal.Equals(C.Normal,0))return false;}
        return true;
    }
    friend uint32 GetTypeHash(const FMCPreparedFoodCutKey& K)
    {
        uint32 H=HashCombine(GetTypeHash(K.World),GetTypeHash(K.Mesh));H=HashCombine(H,PointerHash(K.RenderData));H=HashCombine(H,GetTypeHash(K.Revision));H=HashCombine(H,GetTypeHash(K.DerivedKey));
        H=HashCombine(H,GetTypeHash(K.SourceTransform.GetTranslation()));H=HashCombine(H,GetTypeHash(K.SourceTransform.GetScale3D()));H=HashCombine(H,GetTypeHash(K.Cap));
        for(const auto& M:K.Materials)H=HashCombine(H,GetTypeHash(M));
        for(const auto& P:K.History){H=HashCombine(H,GetTypeHash(P.bEnabled));H=HashCombine(H,GetTypeHash(P.IslandIndex));H=HashCombine(H,GetTypeHash(P.Position));H=HashCombine(H,GetTypeHash(P.Normal));}
        return H;
    }
};
struct FMCPreparedFoodIslandBatch;
struct FMCPreparedFoodCutState
{
    FMCPreparedFoodCutKey Key;
    TSharedPtr<const MCFoodCutGeometry::FPreparedMesh> Mesh;
    TSharedPtr<const FMCPreparedFoodCutState> Parent;
    TSharedPtr<const FMCPreparedFoodIslandBatch> Islands;
};
struct FMCPreparedFoodIslandBatch
{
    TSharedPtr<const FMCPreparedFoodCutState> Source;
    TArray<TSharedPtr<const MCFoodCutGeometry::FPreparedMesh>> Parts;
};

/** Owned by the reserved source; the world scheduler holds only a weak source. */
struct FMCFoodCutTransaction
{
    TArray<TArray<FMCFoodCutPlane>> Histories;
    TArray<double> Weights,LocalVolumes;
    TArray<FBox> Bounds;
    TArray<int32> Units;
    TArray<TSharedPtr<const FMCPreparedFoodCutState>> Prepared;
    TArray<TWeakObjectPtr<AMCFoodActor>> Pieces;
    TArray<TStrongObjectPtr<UMaterialInterface>> Materials;
    FMCPreparedFoodCutKey SourceKey;
    FMCFoodRow Food;
    FMCFoodSettings Settings;
    TSoftObjectPtr<UMCFoodProfile> Profile;
    TWeakObjectPtr<AMCGameState> GameState;
    TWeakObjectPtr<AMCPlayerState> Handler,PreviousHandler;
    TWeakObjectPtr<AMCToothCharacter> Worker;
    TWeakObjectPtr<AActor> Owner;
    FGuid GameplayLoadId;
    int32 Day=0,SourceUnits=0;
    EMCShiftPhase ShiftPhase=EMCShiftPhase::Intermission;
    EMCFoodPhase SourcePhase=EMCFoodPhase::Free;
    FTransform Pose;
    FVector Direction=FVector::ZeroVector,CutNormal=FVector::ZeroVector;
    FVector Velocity=FVector::ZeroVector,AngularVelocity=FVector::ZeroVector;
    double TotalWeight=0;
    double QueuedAtSeconds=0,ContactStartedSeconds=0;
    float SourceHealth=0;
    bool bSourceFragment=false,bWasSimulating=false,bCompletionArmed=false,bWeaponPassives=false,bPlayBreak=false;
    mutable TOptional<EMCFoodProcessingFailure> SpawnFailure;
};

namespace
{
// FinishSpawning can reject a prepared body or mesh. Return its precise fault
// to the parent transaction, which rolls back before reporting recovery.
TWeakObjectPtr<AMCFoodActor> SpawningPortion;
TOptional<EMCFoodProcessingFailure>* SpawningPortionFailure=nullptr;
void ReportCutFailure(AMCFoodActor* Food,EMCFoodProcessingFailure Reason)
{
    if(Food==SpawningPortion && SpawningPortionFailure){*SpawningPortionFailure=Reason;return;}
    UMCFoodProcessingRecoverySubsystem::ReportFailure(Food,Reason);
}
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

bool SeparatesReferencedFood(const MCFoodCutGeometry::FPreparedMesh& Mesh,FVector Point,FVector Normal,FVector ActorScale)
{
    // Prepared vertices are referenced geometry in actor-local centimetres.
    // Convert plane distance back to physical cm for nonuniform actor scales;
    // an AABB crossing alone cannot distinguish empty space from real food.
    if(ActorScale.ContainsNaN() || ActorScale.GetAbs().GetMin()<SMALL_NUMBER)return false;
    const double DistanceScale=(Normal/ActorScale).Size();
    if(!FMath::IsFinite(DistanceScale) || DistanceScale<SMALL_NUMBER)return false;
    bool Positive=false,Negative=false;
    for(const FVector& Vertex:Mesh.Vertices) {
        const double Distance=FVector::DotProduct(Vertex-Point,Normal)/DistanceScale;
        Positive|=Distance>.1;Negative|=Distance<-.1;
        if(Positive&&Negative)return true;
    }
    return false;
}

// Peer-local weak indexes only. Actors retain immutable geometry/ancestors; a
// world, mesh or material is never kept alive by these maps. Prefixes let a late
// client reconstruct a bunch once instead of importing it for every sibling.
TMap<FMCPreparedFoodCutKey,TWeakPtr<const FMCPreparedFoodCutState>> PreparedCuts;
TMap<FMCPreparedFoodCutKey,TWeakPtr<const FMCPreparedFoodIslandBatch>> PreparedIslands;
void PrunePreparedCuts()
{
    static uint64 Frame=MAX_uint64;
    if(Frame==GFrameCounter)return;Frame=GFrameCounter;
    for(auto I=PreparedCuts.CreateIterator();I;++I)if(!I.Key().World.IsValid() || !I.Key().Mesh.IsValid() || !I.Value().IsValid())I.RemoveCurrent();
    for(auto I=PreparedIslands.CreateIterator();I;++I)if(!I.Key().World.IsValid() || !I.Key().Mesh.IsValid() || !I.Value().IsValid())I.RemoveCurrent();
    // Dropping an index never invalidates snapshots already held by actors.
    if(PreparedCuts.Num()>4096)PreparedCuts.Reset();
    if(PreparedIslands.Num()>2048)PreparedIslands.Reset();
}
UMaterialInterface* FoodCapMaterial(UStaticMeshComponent* Visual,UStaticMesh* Mesh)
{
    const FString Name=Mesh->GetName();const TCHAR* Color=Name.Contains(TEXT("Carrot"))?TEXT("Orange"):
        Name.Contains(TEXT("Tomato"))?TEXT("Tomato"):Name.Contains(TEXT("Strawberry"))?TEXT("Strawberry"):
        (Name.Contains(TEXT("Broccoli")) || Name.Contains(TEXT("Cabbage")))?TEXT("Green"):TEXT("Cream");
    const FString Path=FString::Printf(TEXT("/Game/Gameplay/Food/CutMaterials/MI_FoodCut_%s.MI_FoodCut_%s"),Color,Color);
    auto* Material=LoadObject<UMaterialInterface>(nullptr,*Path);return Material?Material:Visual->GetMaterial(0);
}
bool PreparedKey(UWorld* World,UStaticMesh* Mesh,UStaticMeshComponent* Visual,const FTransform& Source,
    const TArray<FMCFoodCutPlane>& History,FMCPreparedFoodCutKey& Out)
{
    if(!World || !Mesh || !Mesh->bAllowCPUAccess || !Visual || Source.ContainsNaN() || Source.GetScale3D().GetAbs().GetMin()<SMALL_NUMBER)return false;
    const auto* Data=Mesh->GetRenderData();if(!Data || Data->LODResources.IsEmpty())return false;
    Out.World=World;Out.Mesh=Mesh;Out.RenderData=Data;Out.Revision=Mesh->GetLightingGuid();
#if WITH_EDITORONLY_DATA
    Out.DerivedKey=Data->DerivedDataKey;
#endif
    Out.SourceTransform=Source;Out.History=History;Out.Cap=FoodCapMaterial(Visual,Mesh);
    for(int32 S=0;S<Mesh->GetNumSections(0);++S)Out.Materials.Add(Visual->GetMaterial(S));
    return true;
}
TSharedPtr<const FMCPreparedFoodIslandBatch> PrepareIslandBatch(const TSharedPtr<const FMCPreparedFoodCutState>& State)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MCFoodPrepareIslandBatch);
    if(!State.IsValid() || !State->Mesh.IsValid() || !State->Mesh->MaterialsValid())return {};
    if(const auto* Found=PreparedIslands.Find(State->Key))if(auto Hit=Found->Pin()){++MCFoodCutGeometry::PreparationStats().CacheHits;return Hit;}
    auto Scratch=TStrongObjectPtr<UProceduralMeshComponent>(NewObject<UProceduralMeshComponent>(GetTransientPackage(),NAME_None,RF_Transient));
    Scratch->SetWorldTransform(State->Key.SourceTransform);
    if(!MCFoodCutGeometry::MaterializePreparedMesh(Scratch.Get(),*State->Mesh))return {};
    auto Batch=MakeShared<FMCPreparedFoodIslandBatch>();Batch->Source=State;
    if(!MCFoodCutGeometry::ExtractPreparedIslands(Scratch.Get(),State->Key.SourceTransform,Batch->Parts))return {};
    PreparedIslands.Add(State->Key,Batch);return Batch;
}
TSharedPtr<const FMCPreparedFoodCutState> PreparedSelection(const FMCPreparedFoodCutKey& Key,
    const TSharedPtr<const FMCPreparedFoodIslandBatch>& Batch,int32 Index)
{
    if(!Batch.IsValid() || !Batch->Parts.IsValidIndex(Index))return {};
    auto Result=MakeShared<FMCPreparedFoodCutState>();Result->Key=Key;Result->Mesh=Batch->Parts[Index];Result->Parent=Batch->Source;Result->Islands=Batch;
    PreparedCuts.Add(Key,Result);return Result;
}
TSharedPtr<const FMCPreparedFoodCutState> PrepareCutNode(const FMCPreparedFoodCutKey& Key,UStaticMeshComponent* Source)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MCFoodPrepareCutNode);
    if(const auto* Found=PreparedCuts.Find(Key))if(auto Hit=Found->Pin())if(Hit->Mesh->MaterialsValid()){++MCFoodCutGeometry::PreparationStats().CacheHits;return Hit;}
    if(!Key.History.IsEmpty()) {
        const auto& Step=Key.History.Last();
        if(Step.IslandIndex>=0) {
            auto Input=Key;
            if(Step.bEnabled)Input.History.Last().IslandIndex=INDEX_NONE;else Input.History.Pop();
            return PreparedSelection(Key,PrepareIslandBatch(PrepareCutNode(Input,Source)),Step.IslandIndex);
        }
        if(!Step.bEnabled || Step.Normal.ContainsNaN() || Step.Normal.IsNearlyZero() || Step.Position.ContainsNaN())return {};
    }
    auto Result=MakeShared<FMCPreparedFoodCutState>();Result->Key=Key;
    auto Scratch=TStrongObjectPtr<UProceduralMeshComponent>(NewObject<UProceduralMeshComponent>(GetTransientPackage(),NAME_None,RF_Transient));
    Scratch->SetWorldTransform(Key.SourceTransform);
    if(Key.History.IsEmpty()) {
        ++MCFoodCutGeometry::PreparationStats().RootImports;
        if(!SliceGeometry(Scratch.Get(),Source,FTransform::Identity,{},Key.Cap.Get()))return {};
    } else {
        auto Input=Key;Input.History.Pop();Result->Parent=PrepareCutNode(Input,Source);
        if(!Result->Parent.IsValid() || !MCFoodCutGeometry::MaterializePreparedMesh(Scratch.Get(),*Result->Parent->Mesh))return {};
        const auto& Step=Key.History.Last();const int32 CapSection=Scratch->GetNumSections();
        ++MCFoodCutGeometry::PreparationStats().HistorySlices;
        if(!MCFoodCutGeometry::SliceAndCap(Scratch.Get(),Step.Position,Step.Normal,Key.Cap.Get()) || !ValidateTriangles(Scratch.Get()))return {};
        TArray<FPlane> Spaces;
        for(const auto& P:Key.History)if(P.bEnabled)Spaces.Emplace(Key.SourceTransform.InverseTransformPosition(P.Position),
            (Key.SourceTransform.GetScale3D()*Key.SourceTransform.InverseTransformVectorNoScale(P.Normal)).GetSafeNormal());
        if(!ValidateKeptSpaces(Scratch.Get(),Spaces,CapSection))return {};
    }
    auto Mesh=MakeShared<MCFoodCutGeometry::FPreparedMesh>();
    if(!MCFoodCutGeometry::CapturePreparedMesh(Scratch.Get(),Key.SourceTransform,*Mesh))return {};
    Result->Mesh=Mesh;PreparedCuts.Add(Key,Result);return Result;
}

bool FindPreparedCollision(const MCFoodCutGeometry::FPreparedMesh& Mesh,bool AllowProxy,
    TSharedPtr<const MCFoodCutGeometry::FPreparedCollision>& Out)
{
    check(IsInGameThread());Out.Reset();
    if(!Mesh.bCollisionPrepared)return false;
    if(Mesh.Collision.IsValid()) {
        ++MCFoodCutGeometry::PreparationStats().ConvexCacheHits;
        if(!Mesh.Collision->bPlanarProxy || AllowProxy)Out=Mesh.Collision;
        return true; // A known proxy cannot satisfy the retained legacy policy.
    }
    return Mesh.bCollisionAllowedPlanarProxy || !AllowProxy;
}
bool PrepareRequiredCollisions(const TArray<const MCFoodCutGeometry::FPreparedMesh*>& Meshes,bool AllowProxy)
{
    check(IsInGameThread());
    TArray<MCFoodCutGeometry::FConvexRequest> Requests;
    TArray<const MCFoodCutGeometry::FPreparedMesh*> Missing;TSet<const MCFoodCutGeometry::FPreparedMesh*> Seen;
    for(const auto* Mesh:Meshes) {
        if(!Mesh || Seen.Contains(Mesh))continue;Seen.Add(Mesh);
        TSharedPtr<const MCFoodCutGeometry::FPreparedCollision> Cached;
        if(FindPreparedCollision(*Mesh,AllowProxy,Cached)){if(!Cached.IsValid())return false;continue;}
        auto& Request=Requests.AddDefaulted_GetRef();Request.Vertices=Mesh->Vertices;Request.bAllowPlanarProxy=AllowProxy;Missing.Add(Mesh);
    }
    TArray<TUniquePtr<MCFoodCutGeometry::FPreparedCollision>> Results;
    if(!MCFoodCutGeometry::PrepareConvexBatch(Requests,Results))return false;
    // Publication is GT-only and occurs only after every required job succeeds.
    for(int32 I=0;I<Missing.Num();++I){Missing[I]->Collision=TSharedPtr<const MCFoodCutGeometry::FPreparedCollision>(Results[I].Release());Missing[I]->bCollisionPrepared=true;Missing[I]->bCollisionAllowedPlanarProxy=AllowProxy;}
    return true;
}

}

bool AMCFoodActor::PrepareCutGeometry()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MCFoodPrepareCutGeometry);
    FMCPreparedFoodCutKey Key;if(!PreparedKey(GetWorld(),ItemMesh,Visual,PortionSourceTransform(),CutPlanes,Key))return false;
    if(PreparedCutState.IsValid() && PreparedCutState->Key==Key && PreparedCutState->Mesh->MaterialsValid())return true;
    PrunePreparedCuts();PreparedCutState=PrepareCutNode(Key,Visual);return PreparedCutState.IsValid();
}
void AMCFoodActor::ApplyPreparedSourceMaterials()
{
    if(!bStagedCutPortion || !PreparedCutState.IsValid() || PreparedCutState->Key.Mesh.Get()!=ItemMesh
        || !PreparedCutState->Key.SourceTransform.Equals(PortionSourceTransform(),0))return;
    for(int32 S=0;S<PreparedCutState->Key.Materials.Num();++S)Visual->SetMaterial(S,PreparedCutState->Key.Materials[S].Get());
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
    return FitsAutomaticIntakeMetrics(MeasuredPortionLocalVolume);
}

double AMCFoodActor::AutomaticIntakeVolume() const
{
    const FVector Scale=GetActorScale3D().GetAbs();
    return MeasuredPortionLocalVolume>=0?MeasuredPortionLocalVolume*Scale.X*Scale.Y*Scale.Z:-1;
}

bool AMCFoodActor::FitsAutomaticIntakeMetrics(double LocalVolume) const
{
    const FVector Limit=Settings.MaxAutoConsumeDimensions;
    if(Limit.ContainsNaN()||Limit.GetMin()<=0||!FMath::IsFinite(LocalVolume)||LocalVolume<=0)return false;
    const FVector Scale=GetActorScale3D().GetAbs();
    const double Volume=LocalVolume*Scale.X*Scale.Y*Scale.Z;
    const double Budget=double(Limit.X)*Limit.Y*Limit.Z;
    // Unknown geometry never substitutes a bounding box for food volume.
    return FMath::IsFinite(Volume)&&Volume>0&&FMath::IsFinite(Budget)&&Volume<=Budget+FMath::Max(.001,Budget*1.e-6);
}

bool AMCFoodActor::SupportsPlanarCut() const
{
    return FoodData.Kind==EMCFoodKind::Food && !IsNutEncounterFragment()
        && ItemMesh && ItemMesh->bAllowCPUAccess;
}

AMCFoodActor* AMCFoodActor::SpawnPreparedPortion(const FMCFoodCutTransaction& Plan,int32 Index,bool bQueued)
{
    Plan.SpawnFailure.Reset();
    const bool Ready=UsesAutomaticCutIntake()&&FitsAutomaticIntakeMetrics(Plan.LocalVolumes[Index]);
    FVector Target;AMCThroat* Throat=nullptr;
    if(Ready && !ResolveAutomaticCutDestination(Plan.Pose.TransformPosition(Plan.Bounds[Index].GetCenter()),Target,Throat)) {
        Plan.SpawnFailure=EMCFoodProcessingFailure::MissingDestination;return nullptr;
    }
    FActorSpawnParameters Spawn;Spawn.bDeferConstruction=true;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Spawn.CustomPreSpawnInitialization=[bQueued](AActor* Actor) {
        auto* Food=CastChecked<AMCFoodActor>(Actor);
        Food->Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Food->Visual->SetStaticMesh(nullptr);
        if(bQueued){
            Food->bCutTransactionStaged=true;Food->SetReplicates(false);Food->SetActorHiddenInGame(true);Food->SetActorTickEnabled(false);
            Food->GripSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
    };
    AMCFoodActor* Piece=nullptr;
    {TRACE_CPUPROFILER_EVENT_SCOPE(MCFoodSpawnDeferredPortion);Piece=GetWorld()->SpawnActor<AMCFoodActor>(StaticClass(),Plan.Pose,Spawn);}
    if(!Piece){Plan.SpawnFailure=EMCFoodProcessingFailure::SpawnFailure;return nullptr;}
    TGuardValue<TWeakObjectPtr<AMCFoodActor>> PortionScope(SpawningPortion,TWeakObjectPtr<AMCFoodActor>(Piece));
    TGuardValue<TOptional<EMCFoodProcessingFailure>*> FailureScope(SpawningPortionFailure,&Plan.SpawnFailure);
    const float Mass=float(double(Plan.Food.Mass)*Plan.Weights[Index]/Plan.TotalWeight);
    Piece->FoodData=Plan.Food;Piece->FoodData.FragmentScale=Plan.bSourceFragment?Plan.Food.FragmentScale:Plan.Food.Scale;
    Piece->FoodData.Mass=Mass;Piece->FoodData.Health=25.f;
    Piece->ItemName=ItemName;Piece->ItemMesh=ItemMesh;Piece->bFragment=true;Piece->Health=25.f;
    Piece->Batch=Batch;Piece->LastHandledBy=Plan.Handler.Get();Piece->SetOwner(Plan.Owner.Get());
    Piece->CutAwardUnits=Plan.Units[Index];Piece->bSpoiled=bSpoiled;Piece->SpoilAt=SpoilAt;Piece->EntryRecoveryLanding=EntryRecoveryLanding;
    if(FoodData.Kind==EMCFoodKind::Food && !HasFreshnessTimer() && ActorHasTag(TEXT("MCNutRain")))Piece->Tags.AddUnique(TEXT("MCNutRain"));
    Piece->CutPlanes=Plan.Histories[Index];Piece->Settings=Plan.Settings;Piece->Profile=Plan.Profile;
    Piece->MeasuredPortionLocalVolume=Plan.LocalVolumes[Index];Piece->bStagedCutPortion=true;
    if(!Ready)Piece->PreparedCutState=Plan.Prepared[Index];
    if(Ready){
        Piece->bCutIntakeReceipt=true;Piece->CutIntakeReceiptBounds=Plan.Bounds[Index];
        Piece->SetReplicates(false);Piece->Phase=EMCFoodPhase::Disposed;
    }
    {TRACE_CPUPROFILER_EVENT_SCOPE(MCFoodFinishPreparedPortion);Piece->FinishSpawning(Plan.Pose);}
    if(!Piece->HasActorBegunPlay())Piece->OnRep_Item();
    Piece->Settings.Mass=Mass;Piece->FoodData.Mass=Mass;
    if(!Piece->bCutVisualReady){if(!Plan.SpawnFailure.IsSet())Plan.SpawnFailure=EMCFoodProcessingFailure::InvalidGeometry;Piece->Destroy();return nullptr;}
    if(Ready)Piece->Phase=EMCFoodPhase::Free;
    else {
        Piece->Body->SetMassOverrideInKg(NAME_None,Mass,true);
        if(!bQueued){
            Piece->Body->SetPhysicsLinearVelocity(Plan.Direction.GetSafeNormal()*12+FVector(0,0,45));
            Piece->ForceNetUpdate();
        }
    }
    if(bQueued){
        Piece->SetActorTickEnabled(false);Piece->SetActorHiddenInGame(true);
        Piece->GripSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    return Piece;
}

bool AMCFoodActor::SpawnPortionActors(const TArray<TArray<FMCFoodCutPlane>>& Histories,
    const TArray<double>& Weights,const TArray<FBox>& LocalBounds,const TArray<double>& LocalVolumes,FVector Direction,TArray<AMCFoodActor*>& OutPieces,
    const TArray<TSharedPtr<const FMCPreparedFoodCutState>>& Prepared)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MCFoodSpawnPreparedPortions);
    OutPieces.Reset();
    if(IsCutTransactionPending() || Histories.IsEmpty() || Histories.Num()!=Weights.Num() || Histories.Num()!=LocalBounds.Num() || Histories.Num()!=LocalVolumes.Num() || Histories.Num()>512)return false;
    if(Prepared.Num()!=Histories.Num())return false;
    for(int32 I=0;I<Prepared.Num();++I)if(!Prepared[I].IsValid() || !Prepared[I]->Mesh.IsValid() || !Prepared[I]->Mesh->MaterialsValid()
        || !LocalBounds[I].IsValid)return false;
    double Total=0;for(double W:Weights){if(!FMath::IsFinite(W) || W<=0)return false;Total+=W;}
    if(!FMath::IsFinite(Total) || Total<=0)return false;
    const int32 Budget=bFragment?CutAwardUnits:FoodData.FragmentCount();
    if(Budget<0 || !FMath::IsFinite(FoodData.Mass) || FoodData.Mass<=0)return false;
    TArray<const MCFoodCutGeometry::FPreparedMesh*> PhysicalMeshes;
    for(int32 I=0;I<Prepared.Num();++I) {
        const bool Ready=UsesAutomaticCutIntake()&&FitsAutomaticIntakeMetrics(LocalVolumes[I]);
        if(Ready) {
            FVector Target;AMCThroat* Throat=nullptr;
            if(!ResolveAutomaticCutDestination(GetActorTransform().TransformPosition(LocalBounds[I].GetCenter()),Target,Throat)) {
                ReportCutFailure(this,EMCFoodProcessingFailure::MissingDestination);return false;
            }
        } else PhysicalMeshes.Add(Prepared[I]->Mesh.Get());
    }
    if(!PrepareRequiredCollisions(PhysicalMeshes,UsesAutomaticCutIntake())) {
        ReportCutFailure(this,EMCFoodProcessingFailure::CollisionPreparation);return false;
    }
    auto Plan=MakeShared<FMCFoodCutTransaction>();
    Plan->Histories=Histories;Plan->Weights=Weights;Plan->Bounds=LocalBounds;Plan->LocalVolumes=LocalVolumes;Plan->Prepared=Prepared;
    Plan->TotalWeight=Total;Plan->Food=FoodData;Plan->Settings=Settings;Plan->Profile=Profile;Plan->Pose=GetActorTransform();Plan->Direction=Direction;
    Plan->Handler=LastHandledBy;Plan->PreviousHandler=LastHandledBy;Plan->Owner=GetOwner();Plan->bSourceFragment=bFragment;
    Plan->SourceUnits=CutAwardUnits;Plan->SourceHealth=Health;Plan->SourcePhase=Phase;
    TArray<int32> Order;TArray<double> Remainders;Plan->Units.SetNum(Weights.Num());
    int32 Assigned=0;
    for(int32 I=0;I<Weights.Num();++I){
        const double Exact=double(Budget)*Weights[I]/Total;Plan->Units[I]=FMath::FloorToInt(Exact);
        Assigned+=Plan->Units[I];Remainders.Add(Exact-Plan->Units[I]);Order.Add(I);
    }
    Order.Sort([&](int32 A,int32 B){return Remainders[A]==Remainders[B]?A<B:Remainders[A]>Remainders[B];});
    for(int32 I=0;I<Budget-Assigned;++I)++Plan->Units[Order[I%Order.Num()]];
    if(!UsesAutomaticCutIntake() && Plan->Units.Num()==2){Plan->Units[0]=(Budget+1)/2;Plan->Units[1]=Budget/2;}
    if(UsesAutomaticCutIntake() && Histories.Num()>8) {
        auto* Scheduler=GetWorld()->GetSubsystem<UMCFoodCutStagingSubsystem>();
        if(!Scheduler || StackCarrier || !Holders.IsEmpty() || EquippedBy || Phase==EMCFoodPhase::Absorbing)return false;
        if(!PreparedKey(GetWorld(),ItemMesh,Visual,PortionSourceTransform(),CutPlanes,Plan->SourceKey))return false;
        for(const auto& State:Prepared)for(const auto& Material:State->Mesh->Materials)if(Material.IsValid())Plan->Materials.Emplace(Material.Get());
        Plan->GameState=GetWorld()->GetGameState<AMCGameState>();
        if(const auto* GS=Plan->GameState.Get()){Plan->GameplayLoadId=GS->GameplayLoadId;Plan->Day=GS->Day;Plan->ShiftPhase=GS->Phase;}
        Plan->bWasSimulating=Body->IsSimulatingPhysics();Plan->Velocity=Body->GetPhysicsLinearVelocity();Plan->AngularVelocity=Body->GetPhysicsAngularVelocityInRadians();
        Plan->QueuedAtSeconds=FPlatformTime::Seconds();Plan->ContactStartedSeconds=CutContactStartedSeconds>0?CutContactStartedSeconds:Plan->QueuedAtSeconds;
        PendingCutTransaction=Plan;bCutTransactionPending=true;Body->SetSimulatePhysics(false);
        Scheduler->Enqueue(this);ForceNetUpdate();
        ++MCFoodCutGeometry::PreparationStats().QueuedCuts;
        UE_LOG(LogMCFoodCuts,Log,TEXT("MC_FOOD_CUT_BEGIN source=%s parts=%d physical=%d preflight_ms=%.3f"),*GetName(),Histories.Num(),PhysicalMeshes.Num(),(Plan->QueuedAtSeconds-Plan->ContactStartedSeconds)*1000.);
        return true;
    }
    for(int32 I=0;I<Histories.Num();++I){
        auto* Piece=SpawnPreparedPortion(*Plan,I,false);if(!Piece)break;OutPieces.Add(Piece);
    }
    if(OutPieces.Num()!=Histories.Num()){
        for(auto* Piece:OutPieces)if(IsValid(Piece))Piece->Destroy();OutPieces.Reset();
        if(Plan->SpawnFailure.IsSet())ReportCutFailure(this,Plan->SpawnFailure.GetValue());return false;
    }
    UE_LOG(LogMCFoodCuts,Log,TEXT("MC_FOOD_ISLANDS_STAGED source=%s item=%s batch=%d parts=%d ready=%d mass=%.6f units=%d"),
        *GetName(),*ItemName.ToString(),Batch,OutPieces.Num(),
        OutPieces.FilterByPredicate([](const AMCFoodActor* P){return P->bCutIntakeReceipt;}).Num(),FoodData.Mass,Budget);
    return true;
}

void AMCFoodActor::SetPendingCutCompletion(AMCToothCharacter* Worker,bool bWeaponPassives,bool bPlayBreak,AMCPlayerState* PreviousHandler)
{
    if(!PendingCutTransaction.IsValid())return;
    PendingCutTransaction->Worker=Worker;PendingCutTransaction->bWeaponPassives=bWeaponPassives;
    PendingCutTransaction->bPlayBreak=bPlayBreak;PendingCutTransaction->PreviousHandler=PreviousHandler;
    PendingCutTransaction->bCompletionArmed=true;LastHandledBy=PreviousHandler;
}

void AMCFoodActor::CancelPendingCut(bool bRestoreSource)
{
    if(!PendingCutTransaction.IsValid()){bCutTransactionPending=false;return;}
    const auto Plan=MoveTemp(PendingCutTransaction);bCutTransactionPending=false;
    for(const auto& Weak:Plan->Pieces)if(auto* Piece=Weak.Get())Piece->Destroy();
    if(bRestoreSource && IsValid(this) && !IsActorBeingDestroyed() && !IsDisposed()) {
        LastHandledBy=Plan->PreviousHandler.Get();OnRep_Phase();
        Body->SetSimulatePhysics(Plan->bWasSimulating);
        if(Plan->bWasSimulating){Body->SetPhysicsLinearVelocity(Plan->Velocity);Body->SetPhysicsAngularVelocityInRadians(Plan->AngularVelocity);}
        ForceNetUpdate();
    }
    ++MCFoodCutGeometry::PreparationStats().CancelledCuts;
    UE_LOG(LogMCFoodCuts,Log,TEXT("MC_FOOD_CUT_CANCEL source=%s staged=%d"),*GetName(),Plan->Pieces.Num());
}

bool AMCFoodActor::AdvancePendingCut()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MCFoodAdvancePendingCut);
    const auto Plan=PendingCutTransaction;
    if(!Plan.IsValid() || !bCutTransactionPending)return false;
    FMCPreparedFoodCutKey CurrentKey;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const bool RunMatches=GS==Plan->GameState.Get() && (!GS || (GS->GameplayLoadId==Plan->GameplayLoadId && GS->Day==Plan->Day && GS->Phase==Plan->ShiftPhase));
    if(!HasAuthority() || IsActorBeingDestroyed() || IsDisposed() || !RunMatches || !Plan->bCompletionArmed || !UsesAutomaticCutIntake()
        || StackCarrier || !Holders.IsEmpty() || EquippedBy || Phase!=Plan->SourcePhase || bFragment!=Plan->bSourceFragment
        || Health!=Plan->SourceHealth || CutAwardUnits!=Plan->SourceUnits || GetOwner()!=Plan->Owner.Get() || Profile!=Plan->Profile
        || !GetActorTransform().Equals(Plan->Pose,.001) || !FMCFoodRow::StaticStruct()->CompareScriptStruct(&FoodData,&Plan->Food,0)
        || !FMCFoodSettings::StaticStruct()->CompareScriptStruct(&Settings,&Plan->Settings,0)
        || !PreparedKey(GetWorld(),ItemMesh,Visual,PortionSourceTransform(),CutPlanes,CurrentKey) || !(CurrentKey==Plan->SourceKey)) {
        CancelPendingCut();return false;
    }
    // The absolute freshness clock continues while actor staging is deferred.
    if(HasFreshnessTimer() && !bSpoiled && SpoilAt>0 && HazardNow()>=SpoilAt)Spoil();
    for(const auto& State:Plan->Prepared)if(!State.IsValid() || !State->Mesh.IsValid() || !State->Mesh->MaterialsValid()){
        CancelPendingCut();ReportCutFailure(this,EMCFoodProcessingFailure::InvalidGeometry);return false;
    }
    for(const auto& Weak:Plan->Pieces)if(!Weak.IsValid() || Weak->IsActorBeingDestroyed() || Weak->IsDisposed()){CancelPendingCut();return false;}
    if(Plan->Pieces.Num()<Plan->Histories.Num()) {
        auto* Piece=SpawnPreparedPortion(*Plan,Plan->Pieces.Num(),true);
        if(!Piece){CancelPendingCut();if(Plan->SpawnFailure.IsSet())ReportCutFailure(this,Plan->SpawnFailure.GetValue());return false;}
        Plan->Pieces.Add(Piece);++MCFoodCutGeometry::PreparationStats().StagedActors;
        return true;
    }
    // Validate every endpoint before the first award or disposal. This includes
    // a fresh source that spoiled while its final physical children were built.
    TArray<AMCFoodActor*> Pieces;
    for(int32 I=0;I<Plan->Pieces.Num();++I) {
        auto* Piece=Plan->Pieces[I].Get();
        if(!IsValid(Piece) || Piece->IsActorBeingDestroyed()){CancelPendingCut();return false;}
        if(!Piece->bCutVisualReady){CancelPendingCut();ReportCutFailure(this,EMCFoodProcessingFailure::InvalidGeometry);return false;}
        Piece->bSpoiled=bSpoiled;Piece->SpoilAt=SpoilAt;
        if(Piece->bCutIntakeReceipt){
            FVector Target;AMCThroat* Throat=nullptr;
            if(!Piece->ResolveAutomaticCutDestination(Plan->Pose.TransformPosition(Plan->Bounds[I].GetCenter()),Target,Throat)){
                CancelPendingCut();ReportCutFailure(this,EMCFoodProcessingFailure::MissingDestination);return false;
            }
        }
        Pieces.Add(Piece);
    }
    UE_LOG(LogMCFoodCuts,Log,TEXT("MC_FOOD_ISLANDS_STAGED source=%s item=%s batch=%d parts=%d ready=%d mass=%.6f units=%d"),
        *GetName(),*ItemName.ToString(),Batch,Pieces.Num(),
        Pieces.FilterByPredicate([](const AMCFoodActor* P){return P->bCutIntakeReceipt;}).Num(),FoodData.Mass,bFragment?CutAwardUnits:FoodData.FragmentCount());
    TRACE_CPUPROFILER_EVENT_SCOPE(MCFoodCommitPendingCut);
    PendingCutTransaction.Reset();bCutTransactionPending=false;LastHandledBy=Plan->Handler.Get();
    // The final shape was registered earlier with all responses muted. Restore
    // filters and simulation together; no hidden staged body could push the world.
    for(int32 I=0;I<Pieces.Num();++I) {
        auto* Piece=Pieces[I];Piece->bCutTransactionStaged=false;
        if(!Piece->bCutIntakeReceipt) {
            if(Piece->bHasStagedCollisionResponses)Piece->Body->SetCollisionResponseToChannels(Piece->StagedCollisionResponses);
            Piece->GripSurface->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Piece->SetReplicates(true);Piece->SetActorTickEnabled(true);Piece->OnRep_Phase();
            Piece->Body->SetPhysicsLinearVelocity(Plan->Direction.GetSafeNormal()*12+Plan->CutNormal.GetSafeNormal()*(I%2==0?25.f:-25.f)+FVector(0,0,45));
            Piece->ForceNetUpdate();
        }
    }
    ++MCFoodCutGeometry::PreparationStats().CommittedCuts;
    CompleteCutPortions(Pieces,Plan->Direction,Plan->Worker.Get(),Plan->bWeaponPassives,Plan->bPlayBreak);
    const double CompletedAt=FPlatformTime::Seconds();
    UE_LOG(LogMCFoodCuts,Log,TEXT("MC_FOOD_CUT_COMMIT source=%s parts=%d staging_elapsed_ms=%.3f contact_elapsed_ms=%.3f"),*GetName(),Pieces.Num(),(CompletedAt-Plan->QueuedAtSeconds)*1000.,(CompletedAt-Plan->ContactStartedSeconds)*1000.);
    return false;
}

int32 AMCFoodActor::SpawnDisconnectedPortions(FVector Direction,TArray<AMCFoodActor*>& OutPieces)
{
    OutPieces.Reset();
    if(!HasAuthority() || !UsesAutomaticCutIntake() || !SupportsPlanarCut())return 0;
    if(!PrepareCutGeometry()){ReportCutFailure(this,EMCFoodProcessingFailure::InvalidGeometry);return -1;}
    const auto IslandBatch=PrepareIslandBatch(PreparedCutState);
    if(!IslandBatch.IsValid()){ReportCutFailure(this,EMCFoodProcessingFailure::InvalidGeometry);return -1;}
    if(IslandBatch->Parts.Num()<=1){
        if(!IslandBatch->Parts.IsEmpty())MeasuredPortionLocalVolume=IslandBatch->Parts[0]->LocalVolume;
        // Retain the one analysis for a subsequent hit without changing the descriptor.
        auto Measured=MakeShared<FMCPreparedFoodCutState>(*PreparedCutState);Measured->Mesh=IslandBatch->Parts[0];Measured->Islands=IslandBatch;
        PreparedCutState=Measured;PreparedCuts.Add(Measured->Key,Measured);
        return 0;
    }
    TArray<TArray<FMCFoodCutPlane>> Histories;TArray<double> Weights,LocalVolumes;TArray<FBox> Bounds;
    TArray<TSharedPtr<const FMCPreparedFoodCutState>> Prepared;
    for(int32 I=0;I<IslandBatch->Parts.Num();++I){
        auto History=CutPlanes;auto& Selection=History.AddDefaulted_GetRef();Selection.IslandIndex=I;
        auto Key=PreparedCutState->Key;Key.History=History;
        Prepared.Add(PreparedSelection(Key,IslandBatch,I));
        Histories.Add(MoveTemp(History));Weights.Add(IslandBatch->Parts[I]->LocalVolume);LocalVolumes.Add(IslandBatch->Parts[I]->LocalVolume);
        Bounds.Add(IslandBatch->Parts[I]->Bounds);
    }
    return SpawnPortionActors(Histories,Weights,Bounds,LocalVolumes,Direction,OutPieces,Prepared)?1:-1;
}

bool AMCFoodActor::SpawnCutFragments(FVector Direction,FVector WorldPoint,FVector WorldNormal,TArray<AMCFoodActor*>* OutPieces)
{
    // Automatic mode must leave every oversized remainder finishable. Its
    // history reconstructs geometry; twelve prior shaves are not completion.
    // Retain the original safeguard for the inactive manual-delivery mode.
    if(!HasAuthority() || !SupportsPlanarCut() || (UsesAutomaticCutIntake()?FitsAutomaticIntakePortion():FitsHandPortion())
        || (!UsesAutomaticCutIntake() && EnabledCutCount(CutPlanes)>=12)
        || WorldPoint.ContainsNaN() || WorldNormal.ContainsNaN()
        || WorldNormal.IsNearlyZero()) return false;
    const FTransform Pose=GetActorTransform();
    const FVector LocalPoint=Pose.InverseTransformPosition(WorldPoint);
    const FVector LocalNormal=(Pose.GetScale3D()*Pose.InverseTransformVectorNoScale(WorldNormal)).GetSafeNormal();
    if(LocalNormal.IsNearlyZero()) return false;
    if(!PrepareCutGeometry()){ReportCutFailure(this,EMCFoodProcessingFailure::InvalidGeometry);return false;}
    const bool IntersectsFood=SeparatesReferencedFood(*PreparedCutState->Mesh,LocalPoint,LocalNormal,Pose.GetScale3D());
    FPortionShape Original;
    if(!UsesAutomaticCutIntake()){
        auto RootKey=PreparedCutState->Key;RootKey.History.Reset();
        const auto Root=PrepareCutNode(RootKey,Visual);if(!Root.IsValid())return false;
        TArray<FVector> Vertices;for(const auto& P:Root->Mesh->Vertices)Vertices.Add(P*Pose.GetScale3D());
        Original=MeasureShape(Vertices);
        if(Original.Volume<=0 || Original.MinimumWidth<=0) return false;
    }
    // Thin authored food may be divided lengthwise without changing its thin
    // axis. Always compare with the ORIGINAL source, never a shrinking parent.
    const double RequiredWidth=FMath::Min(double(Settings.MinCutThickness),Original.MinimumWidth);
    const double WidthTolerance=FMath::Max(.01,RequiredWidth*.001);
    TArray<FMCFoodCutPlane> CandidatePlanes[2];
    double Volumes[2]={0,0};
    TArray<TArray<FMCFoodCutPlane>> Histories;TArray<double> Weights,LocalVolumes;TArray<FBox> Bounds;
    TArray<TSharedPtr<const FMCPreparedFoodCutState>> Prepared;
    for(int32 I=0;I<2;++I) {
        CandidatePlanes[I]=CutPlanes;
        auto& Plane=CandidatePlanes[I].AddDefaulted_GetRef();
        Plane.bEnabled=true; Plane.Position=LocalPoint; Plane.Normal=LocalNormal*(I==0?1:-1);
        auto Key=PreparedCutState->Key;Key.History=CandidatePlanes[I];
        const auto Half=PrepareCutNode(Key,Visual);
        if(!Half.IsValid()) {
            if(IntersectsFood)ReportCutFailure(this,EMCFoodProcessingFailure::InvalidGeometry);
            return false;
        }
        if(UsesAutomaticCutIntake()){
            const auto IslandBatch=PrepareIslandBatch(Half);
            if(!IslandBatch.IsValid()) {
                if(IntersectsFood)ReportCutFailure(this,EMCFoodProcessingFailure::InvalidGeometry);
                return false;
            }
            for(int32 Part=0;Part<IslandBatch->Parts.Num();++Part){
                auto History=CandidatePlanes[I];History.Last().IslandIndex=Part;
                auto PartKey=Key;PartKey.History=History;Prepared.Add(PreparedSelection(PartKey,IslandBatch,Part));
                Histories.Add(MoveTemp(History));Weights.Add(IslandBatch->Parts[Part]->LocalVolume);LocalVolumes.Add(IslandBatch->Parts[Part]->LocalVolume);
                Bounds.Add(IslandBatch->Parts[Part]->Bounds);
            }
            continue;
        }
        // Translation and world rotation cannot affect volume or measured depth.
        TArray<FVector> Vertices;for(const auto& P:Half->Mesh->Vertices)Vertices.Add(P*Pose.GetScale3D());
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
        Histories.Add(CandidatePlanes[I]);Weights.Add(Volumes[I]);LocalVolumes.Add(-1);
        Bounds.Add(Half->Mesh->Bounds);Prepared.Add(Half);
    }
    TArray<AMCFoodActor*> Pieces;
    if(!SpawnPortionActors(Histories,Weights,Bounds,LocalVolumes,Direction,Pieces,Prepared))return false;
    if(IsCutTransactionPending()){PendingCutTransaction->CutNormal=WorldNormal;return true;}
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
    TRACE_CPUPROFILER_EVENT_SCOPE(MCFoodInstallPreparedCut);
    if(!Visual){ReportCutFailure(this,EMCFoodProcessingFailure::InvalidGeometry);return false;}
    if(bCutIntakeReceipt){
        CutStackLocalBounds=CutIntakeReceiptBounds;bCutVisualReady=CutStackLocalBounds.IsValid!=0;
        Visual->SetHiddenInGame(true);return bCutVisualReady;
    }
    if(CutPlanes.IsEmpty()) {
        bCutVisualReady=false;CutStackLocalBounds=FBox(ForceInit);MeasuredPortionLocalVolume=-1;
        if(CutVisual){CutVisual->ClearAllMeshSections();CutVisual->SetHiddenInGame(true);}
        Visual->SetHiddenInGame(false);
        if(bFragment && UsesAutomaticCutIntake() && SupportsPlanarCut()) {
            if(!PrepareCutGeometry()){ReportCutFailure(this,EMCFoodProcessingFailure::InvalidGeometry);return false;}
            const auto IslandBatch=PrepareIslandBatch(PreparedCutState);
            if(IslandBatch.IsValid()) {
                auto Measured=MakeShared<FMCPreparedFoodCutState>(*PreparedCutState);Measured->Islands=IslandBatch;
                PreparedCutState=Measured;PreparedCuts.Add(Measured->Key,Measured);
                if(IslandBatch->Parts.Num()==1)MeasuredPortionLocalVolume=IslandBatch->Parts[0]->LocalVolume;
            } else ReportCutFailure(this,EMCFoodProcessingFailure::InvalidGeometry);
        }
        return false;
    }
    auto Fail=[&](EMCFoodProcessingFailure Reason=EMCFoodProcessingFailure::InvalidGeometry){
        bCutVisualReady=false;CutStackLocalBounds=FBox(ForceInit);MeasuredPortionLocalVolume=-1;
        SetActorHiddenInGame(true);Body->SetSimulatePhysics(false);Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        ReportCutFailure(this,Reason);return false;
    };
    const auto Previous=PreparedCutState;
    if(!SupportsPlanarCut())return Fail(EMCFoodProcessingFailure::UnsupportedGeometry);
    if(!PrepareCutGeometry())return Fail();
    // RepNotify may be called once for each changed item property in the same
    // replication bunch. An unchanged descriptor must not recreate its proxy/body.
    const bool AlreadyInstalled=bCutVisualReady && CutVisual && CutVisual->IsRegistered() && Previous.IsValid()
        && Previous->Key==PreparedCutState->Key && Previous->Mesh==PreparedCutState->Mesh;
    const auto& Mesh=*PreparedCutState->Mesh;
    if(!Mesh.Bounds.IsValid || Mesh.Vertices.Num()<3 || !Mesh.MaterialsValid())return Fail();
    if(!AlreadyInstalled || MeasuredPortionLocalVolume<0)MeasuredPortionLocalVolume=Mesh.LocalVolume;
    if(UsesAutomaticCutIntake() && MeasuredPortionLocalVolume<0) {
        const auto IslandBatch=PrepareIslandBatch(PreparedCutState);if(!IslandBatch.IsValid())return Fail();
        MeasuredPortionLocalVolume=0;for(const auto& Part:IslandBatch->Parts)MeasuredPortionLocalVolume+=Part->LocalVolume;
        auto Measured=MakeShared<FMCPreparedFoodCutState>(*PreparedCutState);Measured->Islands=IslandBatch;
        PreparedCutState=Measured;PreparedCuts.Add(Measured->Key,Measured);
    }
    if(AlreadyInstalled){OnRep_Phase();return true;}
    auto* FoodBody=Cast<UMCFoodBodyComponent>(Body);if(!FoodBody)return Fail(EMCFoodProcessingFailure::CollisionPreparation);
    CutStackLocalBounds=Mesh.Bounds;
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(MCFoodInstallPreparedCollision);
        ++MCFoodCutGeometry::PreparationStats().PhysicsBuilds;
        const bool Ready=UsesAutomaticCutIntake()&&FitsAutomaticIntakePortion();
        TSharedPtr<const MCFoodCutGeometry::FPreparedCollision> Collision;
        if(!FindPreparedCollision(Mesh,UsesAutomaticCutIntake()&&!Ready,Collision)) {
            TArray<const MCFoodCutGeometry::FPreparedMesh*> One={&Mesh};
            if(PrepareRequiredCollisions(One,UsesAutomaticCutIntake()&&!Ready))Collision=Mesh.Collision;
        }
        if(!Collision.IsValid()) {if(!Ready)return Fail(EMCFoodProcessingFailure::CollisionPreparation);Body->SetSimulatePhysics(false);Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);}
        else if(!FoodBody->InstallPreparedCutCollision(*Collision))return Fail(EMCFoodProcessingFailure::CollisionPreparation);
    }
    // Upload compact final sections before the first registration. No temporary
    // full ingredient proxy is ever registered for an individual cut portion.
    if(!CutVisual){CutVisual=NewObject<UProceduralMeshComponent>(this,TEXT("CutFoodMesh"));CutVisual->SetupAttachment(PresentationRoot);CutVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);}
    else if(CutVisual->IsRegistered())CutVisual->UnregisterComponent();
    CutVisual->SetRelativeTransform(PortionSourceTransform());
    if(!MCFoodCutGeometry::MaterializePreparedMesh(CutVisual,Mesh))return Fail();
    CutVisual->RegisterComponent();
    ++MCFoodCutGeometry::PreparationStats().Installations;
    Visual->SetHiddenInGame(true);CutVisual->SetHiddenInGame(false);
    bCutVisualReady=true;OnRep_Phase();return true;
}
