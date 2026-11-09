#if WITH_DEV_AUTOMATION_TESTS
#include "Chaos/Convex.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshResources.h"

namespace MCFoodCapturedBroccoliTests
{
namespace
{
UStaticMesh* InvalidFixture(const TCHAR* Reason)
{
    UE_LOG(LogTemp,Error,TEXT("Captured Broccoli regression fixture rejected: %s"),Reason);
    return nullptr;
}

bool ReadVector(const TSharedPtr<FJsonValue>& Value,FVector& Out)
{
    if(!Value.IsValid() || Value->Type!=EJson::Array)return false;
    const auto& Parts=Value->AsArray();if(Parts.Num()!=3)return false;
    for(int32 Axis=0;Axis<3;++Axis) {
        if(!Parts[Axis].IsValid() || Parts[Axis]->Type!=EJson::Number)return false;
        const double Number=Parts[Axis]->AsNumber();if(!FMath::IsFinite(Number))return false;
        Out[Axis]=Number;
    }
    return true;
}

bool ReadInteger(const TSharedPtr<FJsonValue>& Value,int32& Out)
{
    if(!Value.IsValid() || Value->Type!=EJson::Number)return false;
    const double Number=Value->AsNumber();
    if(!FMath::IsFinite(Number) || Number<0 || Number>MAX_int32 || Number!=FMath::FloorToDouble(Number))return false;
    Out=int32(Number);return true;
}
}

/** Exact remaining user geometry, not the original whole mesh or a box proxy.
 * Initial actor rotation/component scale are baked into the retained vertices.
 * Use row scale=1, actor rotation=identity, actor location=OutFoodPoint and
 * Hero=(0,0,10060) to reproduce the original worker/food relative placement.
 * The JSON lives in Source because these are EditorContext regression tests;
 * missing/invalid evidence is an explicit failure, never substitute geometry.
 */
UStaticMesh* MakeMesh(FVector& OutFoodPoint)
{
    OutFoodPoint=FVector::ZeroVector;
    const FString Path=FPaths::Combine(FPaths::ProjectDir(),
        TEXT("Source/MessControl/Private/Tests/Fixtures/BroccoliOverlap_20261110.json"));
    FString Text;TSharedPtr<FJsonObject> Root;
    if(!FFileHelper::LoadFileToString(Text,*Path)
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root) || !Root.IsValid())
        return InvalidFixture(TEXT("source JSON missing or malformed"));
    const TArray<TSharedPtr<FJsonValue>> *Points=nullptr,*Triangles=nullptr,*Sections=nullptr;
    if(!Root->TryGetArrayField(TEXT("vertices_cm"),Points) || Points->Num()!=417
        || !Root->TryGetArrayField(TEXT("triangles"),Triangles) || Triangles->Num()!=247
        || !Root->TryGetArrayField(TEXT("sections"),Sections) || Sections->Num()!=3)
        return InvalidFixture(TEXT("captured vertex/triangle/section counts changed"));
    const auto* PointField=Root->Values.Find(TEXT("food_point_cm"));FVector FoodPoint;
    if(!PointField || !ReadVector(*PointField,FoodPoint))return InvalidFixture(TEXT("invalid original relative placement"));

    TArray<FVector> Positions;Positions.Reserve(Points->Num());FBox Bounds(ForceInit);
    for(const auto& Value:*Points) {
        FVector Point;if(!ReadVector(Value,Point))return InvalidFixture(TEXT("nonfinite captured vertex"));
        // This is the same float render-vertex representation as the retained
        // procedural source. Build bounds from those actual stored positions.
        Positions.Add(FVector(FVector3f(Point)));Bounds+=Positions.Last();
    }
    if(!Bounds.IsValid || !Bounds.GetCenter().IsNearlyZero(.01)
        || !Bounds.GetSize().Equals(FVector(229.4678560873,315.3113995968,200.0572383016),.02))
        return InvalidFixture(TEXT("captured geometric bounds or recentering changed"));
    TArray<uint32> Indices;Indices.Reserve(Triangles->Num()*3);
    TArray<FVector3f> NormalSums;NormalSums.SetNumZeroed(Positions.Num());
    TBitArray<> Referenced(false,Positions.Num());
    for(const auto& Value:*Triangles) {
        if(!Value.IsValid() || Value->Type!=EJson::Array || Value->AsArray().Num()!=3)
            return InvalidFixture(TEXT("malformed captured triangle"));
        int32 Face[3];
        for(int32 Corner=0;Corner<3;++Corner) {
            if(!ReadInteger(Value->AsArray()[Corner],Face[Corner]) || !Positions.IsValidIndex(Face[Corner]))
                return InvalidFixture(TEXT("captured triangle index out of range"));
            Indices.Add(uint32(Face[Corner]));Referenced[Face[Corner]]=true;
        }
        const FVector3f Normal=FVector3f(FVector::CrossProduct(
            Positions[Face[1]]-Positions[Face[0]],Positions[Face[2]]-Positions[Face[0]]));
        for(int32 Corner:Face)NormalSums[Corner]+=Normal;
    }
    for(int32 I=0;I<Referenced.Num();++I)if(!Referenced[I])return InvalidFixture(TEXT("captured vertex became unreferenced"));
    struct FSection {uint32 First=0,Count=0;};TArray<FSection> ParsedSections;
    int32 NextTriangle=0;
    for(const auto& Value:*Sections) {
        if(!Value.IsValid() || Value->Type!=EJson::Object)return InvalidFixture(TEXT("invalid captured section"));
        const auto Object=Value->AsObject();int32 First=0,Count=0;
        const auto* FirstValue=Object->Values.Find(TEXT("first_triangle"));
        const auto* CountValue=Object->Values.Find(TEXT("triangle_count"));
        if(!FirstValue || !CountValue || !ReadInteger(*FirstValue,First) || !ReadInteger(*CountValue,Count)
            || First!=NextTriangle || Count<=0 || First+Count>Triangles->Num())
            return InvalidFixture(TEXT("captured section index range changed"));
        ParsedSections.Add({uint32(First),uint32(Count)});NextTriangle+=Count;
    }
    if(NextTriangle!=Triangles->Num())return InvalidFixture(TEXT("incomplete captured sections"));

    TArray<Chaos::FConvex::FVec3Type> HullPoints;HullPoints.Reserve(Positions.Num());
    for(const FVector& Point:Positions)HullPoints.Add(Chaos::FConvex::FVec3Type(Point));
    Chaos::FConvexPtr Convex=new Chaos::FConvex(HullPoints,0);
    if(!Convex->IsValidGeometry())return InvalidFixture(TEXT("captured convex could not be prepared"));
    auto* Mesh=NewObject<UStaticMesh>(GetTransientPackage(),NAME_None,RF_Transient);
    Mesh->bAllowCPUAccess=true;
    TArray<FStaticMeshBuildVertex> Vertices;Vertices.SetNum(Positions.Num());
    for(int32 I=0;I<Vertices.Num();++I) {
        auto& V=Vertices[I];V.Position=FVector3f(Positions[I]);
        V.TangentZ=NormalSums[I].GetSafeNormal();if(V.TangentZ.IsNearlyZero())V.TangentZ=FVector3f(0,0,1);
        V.TangentX=FVector3f::CrossProduct(V.TangentZ,FVector3f(0,0,1)).GetSafeNormal();
        if(V.TangentX.IsNearlyZero())V.TangentX=FVector3f(1,0,0);
        V.TangentY=FVector3f::CrossProduct(V.TangentZ,V.TangentX);V.Color=FColor::White;V.UVs[0]=FVector2f::ZeroVector;
    }
    auto Data=MakeUnique<FStaticMeshRenderData>();Data->AllocateLODResources(1);auto& LOD=Data->LODResources[0];
    LOD.VertexBuffers.PositionVertexBuffer.Init(Vertices,true);LOD.VertexBuffers.StaticMeshVertexBuffer.Init(Vertices,1,true);
    LOD.VertexBuffers.ColorVertexBuffer.Init(Vertices,true);LOD.IndexBuffer.SetIndices(Indices,EIndexBufferStride::Force32Bit);
    for(const auto& Captured:ParsedSections) {
        auto& Section=LOD.Sections.AddDefaulted_GetRef();Section.FirstIndex=Captured.First*3;Section.NumTriangles=Captured.Count;
        Section.MinVertexIndex=0;Section.MaxVertexIndex=Vertices.Num()-1;Section.MaterialIndex=0;
    }
    LOD.bBuffersInlined=true;Data->NumInlinedLODs=1;Data->ScreenSize[0].Default=1;Data->Bounds=FBoxSphereBounds(Bounds);
    Mesh->GetStaticMaterials().Add(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface)));
    Mesh->SetRenderData(MoveTemp(Data));Mesh->CalculateExtendedBounds();Mesh->CreateBodySetup();
    auto* Setup=Mesh->GetBodySetup();Setup->CollisionTraceFlag=CTF_UseSimpleAsComplex;Setup->bNeverNeedsCookedCollisionData=true;
    auto& Hull=Setup->AggGeom.ConvexElems.AddDefaulted_GetRef();Hull.VertexData=Positions;Hull.UpdateElemBox();
    Hull.SetConvexMeshObject(MoveTemp(Convex));Setup->bCreatedPhysicsMeshes=true;
    Mesh->InitResources();OutFoodPoint=FoodPoint;return Mesh;
}
}
#endif
