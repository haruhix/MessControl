#include "MCFoodCollisionBaker.h"
#include "MCFoodCollisionData.h"
#include "MCDayPlan.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Chaos/Convex.h"
#include "ConvexDecompTool.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "HAL/FileManager.h"
#include "Misc/EngineVersion.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodySetup.h"
#include "ScopedTransaction.h"
#include "Serialization/MemoryWriter.h"
#include "StaticMeshCompiler.h"
#include "StaticMeshResources.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCFoodCollisionBake, Log, All);

namespace
{
    constexpr uint32 BakeVersion=8;
    struct FMeshInput
    {
        TArray<FVector3f> Vertices;
        TArray<uint32> Indices;
        FBox Bounds{ForceInit};
        FString Key;
    };
    struct FRequest
    {
        UStaticMesh* Mesh=nullptr;
        int32 HullLimit=0, VertexLimit=0, Resolution=0;
        double MaxRelevantScale=0, SupportTolerance=0;
        FString ScaleSignature;
        FMeshInput Input;
        TStrongObjectPtr<UBodySetup> Staged;
        UMCFoodCollisionData* Profile=nullptr;
        bool bChanged=false;
    };
    struct FRowRequest
    {
        FMCFoodRow* Row=nullptr;
        FName Name;
        TArray<FString> Keys;
    };

    FString Digest(const TArray<uint8>& Bytes)
    {
        return FSHA1::HashBuffer(Bytes.GetData(),Bytes.Num()).ToString();
    }
    FString DigestString(const FString& Value)
    {
        FTCHARToUTF8 UTF8(*Value);
        return FSHA1::HashBuffer(UTF8.Get(),UTF8.Length()).ToString();
    }
    FString SupportSignature(TArray<FVector> Scales,double MaxScale,double Tolerance)
    {
        Scales.Sort([](const FVector& A,const FVector& B)
        {return A.X!=B.X?A.X<B.X:A.Y!=B.Y?A.Y<B.Y:A.Z<B.Z;});
        TArray<uint8> Bytes; FMemoryWriter Writer(Bytes);
        Writer<<Scales<<MaxScale<<Tolerance;
        return Digest(Bytes);
    }
    FString RequestKey(UStaticMesh* Mesh,int32 Hulls,int32 Vertices,int32 Resolution,const FString& ScaleSignature)
    {
        return FString::Printf(TEXT("%s|%d|%d|%d|%s"),*Mesh->GetPathName(),Hulls,Vertices,Resolution,*ScaleSignature);
    }

    void HashAuthoredCollision(FMemoryWriter& Writer,const UBodySetup& Source)
    {
        // Serialize numbers explicitly: FMemoryWriter's FName representation is process-local,
        // and cooked convex IndexData is transient. Neither belongs in a persistent bake key.
        TArray<FString> Shapes;
        for(const FKConvexElem& Hull:Source.AggGeom.ConvexElems)
        {
            TArray<uint8> Bytes; FMemoryWriter ShapeWriter(Bytes);
            uint8 Kind=1; ShapeWriter<<Kind;
            FTransform Transform=Hull.GetTransform(); ShapeWriter<<Transform;
            TArray<FVector> Points=Hull.VertexData;
            Points.Sort([](const FVector& A,const FVector& B)
            {return A.X!=B.X?A.X<B.X:A.Y!=B.Y?A.Y<B.Y:A.Z<B.Z;});
            ShapeWriter<<Points;
            Shapes.Add(Digest(Bytes));
        }
        for(const FKBoxElem& Box:Source.AggGeom.BoxElems)
        {
            TArray<uint8> Bytes; FMemoryWriter ShapeWriter(Bytes);
            uint8 Kind=2; ShapeWriter<<Kind;
            FTransform Transform=Box.GetTransform(); ShapeWriter<<Transform;
            float X=Box.X,Y=Box.Y,Z=Box.Z; ShapeWriter<<X<<Y<<Z;
            Shapes.Add(Digest(Bytes));
        }
        for(const FKSphereElem& Sphere:Source.AggGeom.SphereElems)
        {
            TArray<uint8> Bytes; FMemoryWriter ShapeWriter(Bytes);
            uint8 Kind=3; ShapeWriter<<Kind;
            FTransform Transform=Sphere.GetTransform(); ShapeWriter<<Transform;
            float Radius=Sphere.Radius; ShapeWriter<<Radius;
            Shapes.Add(Digest(Bytes));
        }
        for(const FKSphylElem& Capsule:Source.AggGeom.SphylElems)
        {
            TArray<uint8> Bytes; FMemoryWriter ShapeWriter(Bytes);
            uint8 Kind=4; ShapeWriter<<Kind;
            FTransform Transform=Capsule.GetTransform(); ShapeWriter<<Transform;
            float Radius=Capsule.Radius,Length=Capsule.Length; ShapeWriter<<Radius<<Length;
            Shapes.Add(Digest(Bytes));
        }
        Shapes.Sort(); Writer<<Shapes;
        int32 Count=Source.AggGeom.GetElementCount(); Writer<<Count;
        FGuid Revision=Source.BodySetupGuid; Writer<<Revision;
        FString Material=GetPathNameSafe(Source.PhysMaterial.Get()); Writer<<Material;
        uint8 TraceFlag=uint8(Source.GetCollisionTraceFlag()); Writer<<TraceFlag;
    }

    bool ReadInput(UStaticMesh* Mesh,FMeshInput& Out,FString& Error)
    {
        FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
        const FStaticMeshRenderData* Render=Mesh->GetRenderData();
        if(!Render || Render->LODResources.IsEmpty())
        { Error=TEXT("Source has no built LOD0 render geometry"); return false; }
        const FStaticMeshLODResources& LOD=Render->LODResources[0];
        const auto& Positions=LOD.VertexBuffers.PositionVertexBuffer;
        Out.Vertices.Reserve(Positions.GetNumVertices());
        TArray<uint8> Signature;
        FMemoryWriter Writer(Signature);
        uint32 Version=BakeVersion; Writer<<Version;
        FString Engine=FEngineVersion::Current().ToString(),Path=Mesh->GetPathName(); Writer<<Engine<<Path;
        for(uint32 I=0;I<Positions.GetNumVertices();++I)
        {
            FVector3f P=Positions.VertexPosition(I);
            if(P.ContainsNaN()) { Error=TEXT("Source has non-finite vertices"); return false; }
            Out.Vertices.Add(P); Writer<<P;
        }
        TArray<uint32> AllIndices; LOD.IndexBuffer.GetCopy(AllIndices);
        for(const FStaticMeshSection& Section:LOD.Sections)
        {
            bool Enabled=Section.bEnableCollision; Writer<<Enabled;
            if(!Enabled) continue;
            const uint64 End=uint64(Section.FirstIndex)+uint64(Section.NumTriangles)*3;
            if(End>uint64(AllIndices.Num())) { Error=TEXT("Source section has invalid indices"); return false; }
            for(uint64 I=Section.FirstIndex;I<End;++I)
            {
                const uint32 Index=AllIndices[int32(I)];
                if(Index>=uint32(Out.Vertices.Num())) { Error=TEXT("Source triangle has invalid vertices"); return false; }
                Out.Indices.Add(Index); Out.Bounds+=FVector(Out.Vertices[Index]);
            }
        }
        Writer<<Out.Indices;
        if(UBodySetup* Source=Mesh->GetBodySetup())
        {
            // Preserve-path geometry participates in freshness too; a collider-only edit must rebake.
            HashAuthoredCollision(Writer,*Source);
        }
        if(Out.Vertices.Num()<4 || Out.Indices.Num()<3 || !Out.Bounds.IsValid)
        { Error=TEXT("Source has no collision-enabled triangle solid"); return false; }
        Out.Key=Digest(Signature);
        return true;
    }

    bool Collect(UDataTable* Menu,TMap<FString,FRequest>& Requests,TArray<FRowRequest>& Rows,TArray<FString>& Errors)
    {
        if(!Menu || Menu->GetRowStruct()!=FMCFoodRow::StaticStruct())
        { Errors.Add(TEXT("Food collision bake requires an FMCFoodRow data table")); return false; }
        TArray<FName> Names=Menu->GetRowNames(); Names.Sort(FNameLexicalLess());
        for(FName Name:Names)
        {
            FMCFoodRow* Row=Menu->FindRow<FMCFoodRow>(Name,TEXT("Food collision bake"));
            if(!Row || !Row->Collision.bAutoOptimize) continue;
            FMCFoodCollisionSettings Policy=Row->Collision; Policy.Sanitize();
            for(bool Fragment:{false,true})
            {
                const auto& Choices=Fragment?Row->FragmentMeshes:Row->WholeMeshes;
                if(!Choices.ContainsByPredicate([](const TSoftObjectPtr<UStaticMesh>& Mesh){return !Mesh.IsNull();}))
                    Errors.Add(FString::Printf(TEXT("%s: automatic collision requires a %s mesh; prototype fallbacks are not baked"),
                        *Name.ToString(),Fragment?TEXT("fragment"):TEXT("whole")));
            }
            struct FRolePolicy {int32 HullLimit;double MaxScale;TArray<FVector> Scales;};
            TMap<UStaticMesh*,FRolePolicy> Limits;
            auto Add=[&](const TArray<TSoftObjectPtr<UStaticMesh>>& Meshes,int32 Limit,FVector Scale,const TCHAR* Role)
            {
                if(Scale.ContainsNaN() || Scale.GetMin()<=0)
                {Errors.Add(FString::Printf(TEXT("%s: %s collision requires finite positive scale on every axis"),*Name.ToString(),Role));return;}
                // Runtime Sanitize raises each positive scale axis to at least .01.
                const double MaxScale=FMath::Max(.01,Scale.GetAbs().GetMax());
                for(const auto& Reference:Meshes)
                {
                    if(Reference.IsNull()) continue;
                    UStaticMesh* Mesh=Reference.LoadSynchronous();
                    if(!Mesh) {Errors.Add(FString::Printf(TEXT("%s: food mesh %s cannot be loaded"),*Name.ToString(),*Reference.ToString())); continue;}
                    if(FRolePolicy* Existing=Limits.Find(Mesh))
                    {
                        Existing->HullLimit=FMath::Min(Existing->HullLimit,Limit);
                        Existing->MaxScale=FMath::Max(Existing->MaxScale,MaxScale);
                        Existing->Scales.AddUnique(Scale);
                    }
                    else Limits.Add(Mesh,FRolePolicy{Limit,MaxScale,{Scale}});
                }
            };
            Add(Row->WholeMeshes,Policy.WholeHullLimit,Row->Scale,TEXT("whole"));
            Add(Row->FragmentMeshes,Policy.FragmentHullLimit,Row->FragmentScale,TEXT("fragment"));
            if(Limits.IsEmpty()) Errors.Add(FString::Printf(TEXT("%s: automatic collision has no source meshes"),*Name.ToString()));
            FRowRequest RowRequest; RowRequest.Row=Row; RowRequest.Name=Name;
            TArray<UStaticMesh*> Meshes; Limits.GenerateKeyArray(Meshes);
            Meshes.Sort([](const UStaticMesh& A,const UStaticMesh& B){return A.GetPathName()<B.GetPathName();});
            for(UStaticMesh* Mesh:Meshes)
            {
                const FRolePolicy& Role=Limits.FindChecked(Mesh);
                const double Tolerance=1.75/Role.MaxScale;
                if(!FMath::IsFinite(Tolerance) || Tolerance<=0)
                {Errors.Add(FString::Printf(TEXT("%s: support tolerance is non-finite or non-positive"),*Name.ToString()));continue;}
                const FString Signature=SupportSignature(Role.Scales,Role.MaxScale,Tolerance);
                const FString Key=RequestKey(Mesh,Role.HullLimit,Policy.HullVertexLimit,Policy.VoxelResolution,Signature);
                if(!Requests.Contains(Key))
                {
                    FRequest Request; Request.Mesh=Mesh; Request.HullLimit=Role.HullLimit;
                    Request.VertexLimit=Policy.HullVertexLimit; Request.Resolution=Policy.VoxelResolution;
                    Request.MaxRelevantScale=Role.MaxScale; Request.SupportTolerance=Tolerance; Request.ScaleSignature=Signature;
                    FString Error;
                    if(!ReadInput(Mesh,Request.Input,Error)) Errors.Add(FString::Printf(TEXT("%s: %s: %s"),*Name.ToString(),*Mesh->GetName(),*Error));
                    // Scale edits change the required support accuracy and must invalidate a saved profile.
                    Request.Input.Key=DigestString(Request.Input.Key+TEXT("|")+Signature);
                    Requests.Add(Key,MoveTemp(Request));
                }
                RowRequest.Keys.Add(Key);
            }
            Rows.Add(MoveTemp(RowRequest));
        }
        return Errors.IsEmpty();
    }

    bool Matches(const UMCFoodCollisionData* Profile,const FRequest& Request)
    {
        return Profile && Profile->MatchesSource(Request.Mesh) && Profile->SourceGeometryKey==Request.Input.Key
            && Profile->HullLimit==Request.HullLimit && Profile->HullVertexLimit==Request.VertexLimit
            && Profile->VoxelResolution==Request.Resolution && Profile->HasValidCollision();
    }
    FString ProfilePackage(const FRequest& Request)
    {
        // Source revisions get distinct packages, retaining previous valid bodies on a failed bake.
        const FString SourceHash=DigestString(Request.Mesh->GetPathName()+TEXT("|")+Request.Input.Key).Left(12);
        const FString PolicyHash=DigestString(FString::Printf(TEXT("%d|%d|%d|%u|%s"),Request.HullLimit,Request.VertexLimit,Request.Resolution,BakeVersion,*Request.ScaleSignature)).Left(8);
        return FString::Printf(TEXT("/Game/Generated/FoodCollision/%s_%s_%s"),*Request.Mesh->GetName(),*SourceHash,*PolicyHash);
    }

    bool CoverSourceVertices(UBodySetup* Body,const FMeshInput& Input,int32 VertexLimit,
        double SupportTolerance,double OutwardTolerance,double& MaxSupportError,double& MaxOutwardError,int32& AddedPoints,FString& Error)
    {
        struct FTriangle {FVector A,B,C;bool Degenerate;};
        struct FSurface
        {
            TUniquePtr<Chaos::FConvex> Convex;
            TArray<FTriangle> Triangles;
        };
        auto BuildSurface=[&](const TArray<Chaos::FConvex::FVec3Type>& Points,FSurface& Surface)
        {
            TArray<Chaos::FConvex::FPlaneType> Planes;
            TArray<TArray<int32>> Faces;
            TArray<Chaos::FConvex::FVec3Type> Vertices;
            Chaos::FConvex::FAABB3Type Bounds;
            // Keep exact supporting faces: the normal points constructor merges near-parallel
            // planes. These editor-only envelopes never replace the saved physical hulls.
            Chaos::FConvexBuilder::Build(Points,Planes,Faces,Vertices,Bounds,Chaos::FConvexBuilder::EBuildMethod::ConvexHull3);
            if(Planes.IsEmpty() || Vertices.IsEmpty()) {Error=TEXT("Could not build a source support surface");return false;}
            Surface.Convex=MakeUnique<Chaos::FConvex>(MoveTemp(Planes),MoveTemp(Faces),MoveTemp(Vertices));
            if(!Surface.Convex->IsValidGeometry()) {Error=TEXT("Source support surface has invalid geometry");return false;}
            for(int32 PlaneIndex=0;PlaneIndex<Surface.Convex->NumPlanes();++PlaneIndex)
            {
                const int32 Count=Surface.Convex->NumPlaneVertices(PlaneIndex);
                for(int32 VertexIndex=1;VertexIndex+1<Count;++VertexIndex)
                {
                    const FVector A(Surface.Convex->GetVertex(Surface.Convex->GetPlaneVertex(PlaneIndex,0)));
                    const FVector B(Surface.Convex->GetVertex(Surface.Convex->GetPlaneVertex(PlaneIndex,VertexIndex)));
                    const FVector C(Surface.Convex->GetVertex(Surface.Convex->GetPlaneVertex(PlaneIndex,VertexIndex+1)));
                    const double AreaSquared=FVector::CrossProduct(B-A,C-A).SizeSquared();
                    if(!FMath::IsFinite(AreaSquared)) {Error=TEXT("Source support surface has non-finite triangles");return false;}
                    Surface.Triangles.Add(FTriangle{A,B,C,AreaSquared<=UE_SMALL_NUMBER});
                }
            }
            if(Surface.Triangles.IsEmpty()) {Error=TEXT("Source support surface has no triangles");return false;}
            return true;
        };
        auto DistanceToSurface=[&](const FSurface& Surface,const FVector& Point,FVector* OutClosest=nullptr)
        {
            if(Point.ContainsNaN()) return TNumericLimits<double>::Max();
            bool Inside=true;
            for(int32 PlaneIndex=0;PlaneIndex<Surface.Convex->NumPlanes();++PlaneIndex)
            {
                Chaos::FVec3 Normal,PlanePoint; Surface.Convex->GetPlaneNX(PlaneIndex,Normal,PlanePoint);
                const double Distance=FVector::DotProduct(FVector(Normal),Point-FVector(PlanePoint));
                if(!FMath::IsFinite(Distance)) return TNumericLimits<double>::Max();
                if(Distance>0) Inside=false;
            }
            if(Inside) {if(OutClosest) *OutClosest=Point;return 0.;}
            double DistanceSquared=TNumericLimits<double>::Max();
            FVector ClosestPoint=Point;
            auto ConsiderClosest=[&](const FVector& Candidate)
            {
                if(Candidate.ContainsNaN()) return false;
                const double CandidateDistanceSquared=(Point-Candidate).SizeSquared();
                if(!FMath::IsFinite(CandidateDistanceSquared)) return false;
                if(CandidateDistanceSquared<DistanceSquared)
                {DistanceSquared=CandidateDistanceSquared;ClosestPoint=Candidate;}
                return true;
            };
            for(const FTriangle& Triangle:Surface.Triangles)
            {
                if(Triangle.Degenerate)
                {
                    // Tiny triangle projection can return the input point when its normal
                    // vanishes. Segment witnesses stay inside the convex and are conservative.
                    const FVector AB=FMath::ClosestPointOnSegment(Point,Triangle.A,Triangle.B);
                    const FVector BC=FMath::ClosestPointOnSegment(Point,Triangle.B,Triangle.C);
                    const FVector CA=FMath::ClosestPointOnSegment(Point,Triangle.C,Triangle.A);
                    if(!ConsiderClosest(AB) || !ConsiderClosest(BC) || !ConsiderClosest(CA)) return TNumericLimits<double>::Max();
                }
                else
                {
                    const FVector Closest=FMath::ClosestPointOnTriangleToPoint(Point,Triangle.A,Triangle.B,Triangle.C);
                    if(!ConsiderClosest(Closest)) return TNumericLimits<double>::Max();
                }
            }
            if(DistanceSquared==TNumericLimits<double>::Max()) return TNumericLimits<double>::Max();
            if(OutClosest) *OutClosest=ClosestPoint;
            const double Distance=FMath::Sqrt(DistanceSquared);
            return FMath::IsFinite(Distance)?Distance:TNumericLimits<double>::Max();
        };
        TBitArray<> Seen(false,Input.Vertices.Num());
        TArray<Chaos::FConvex::FVec3Type> SourcePoints,OriginalPoints;
        for(uint32 VertexIndex:Input.Indices)
        {
            if(Seen[VertexIndex]) continue;
            Seen[VertexIndex]=true; SourcePoints.Add(Chaos::FConvex::FVec3Type(Input.Vertices[VertexIndex]));
        }
        FSurface SourceSurface,OriginalEnvelope;
        if(!BuildSurface(SourcePoints,SourceSurface)) return false;
        bool Clipped=false;
        for(FKConvexElem& Hull:Body->AggGeom.ConvexElems)
        {
            const FTransform Transform=Hull.GetTransform();
            if(!Transform.IsValid()) {Error=TEXT("Local support clipping has an invalid element transform");return false;}
            bool HullClipped=false;
            for(FVector& Vertex:Hull.VertexData)
            {
                FVector Closest;
                const double Distance=DistanceToSurface(SourceSurface,Transform.TransformPosition(Vertex),&Closest);
                if(!FMath::IsFinite(Distance) || Distance==TNumericLimits<double>::Max() || Closest.ContainsNaN())
                {Error=TEXT("Local support clipping could not find a finite source witness");return false;}
                if(Distance==0) continue;
                // Voxelization can place decomposition points outside the render support
                // envelope. Move only those points to an exact source face/edge witness.
                Vertex=Transform.InverseTransformPosition(Closest);
                if(Vertex.ContainsNaN()) {Error=TEXT("Local support clipping produced a non-finite vertex");return false;}
                HullClipped=true;
            }
            if(HullClipped)
            {Hull.IndexData.Reset();Hull.UpdateElemBox();Hull.ResetChaosConvexMesh();Clipped=true;}
        }
        // Nearest-hull assignment must use the clipped physical geometry, not the old
        // voxelized cook. All clipping/repair is confined to this staged body setup.
        if(Clipped) {Body->InvalidatePhysicsData();Body->CreatePhysicsMeshes();}
        TArray<FSurface> OriginalHulls;
        OriginalHulls.Reserve(Body->AggGeom.ConvexElems.Num());
        for(const FKConvexElem& Hull:Body->AggGeom.ConvexElems)
        {
            const auto& Convex=Hull.GetChaosConvexMesh();
            if(!Convex || !Convex->IsValidGeometry()) {Error=TEXT("Local support repair requires valid original cooked hulls");return false;}
            TArray<Chaos::FConvex::FVec3Type> Points;
            for(int32 VertexIndex=0;VertexIndex<Convex->NumVertices();++VertexIndex)
            {
                const auto& Point=Convex->GetVertex(VertexIndex);
                if(FVector(Point).ContainsNaN()) {Error=TEXT("Original cooked hull has non-finite vertices");return false;}
                Points.Add(Point); OriginalPoints.Add(Point);
            }
            if(!BuildSurface(Points,OriginalHulls.AddDefaulted_GetRef())) return false;
        }
        if(!BuildSurface(OriginalPoints,OriginalEnvelope)) return false;
        TArray<FVector> Extremes;
        for(int32 VertexIndex=0;VertexIndex<SourceSurface.Convex->NumVertices();++VertexIndex)
            Extremes.Add(FVector(SourceSurface.Convex->GetVertex(VertexIndex)));
        Extremes.Sort([](const FVector& A,const FVector& B)
        {return A.X!=B.X?A.X<B.X:A.Y!=B.Y?A.Y<B.Y:A.Z<B.Z;});
        TBitArray<> Touched(false,OriginalHulls.Num());
        AddedPoints=0;
        for(const FVector& Point:Extremes)
        {
            // Repair only missing outer support. Interior gaps between compounds do not
            // affect contact with a plane, and never justify inflating every hull.
            if(DistanceToSurface(OriginalEnvelope,Point)<=SupportTolerance*(1.-1.e-4)) continue;
            int32 ClosestHull=INDEX_NONE;
            double ClosestDistance=TNumericLimits<double>::Max();
            for(int32 HullIndex=0;HullIndex<OriginalHulls.Num();++HullIndex)
            {
                const double Distance=DistanceToSurface(OriginalHulls[HullIndex],Point);
                if(Distance<ClosestDistance) {ClosestDistance=Distance;ClosestHull=HullIndex;}
            }
            if(ClosestHull==INDEX_NONE) {Error=TEXT("Missing source extreme has no valid original hull");return false;}
            FKConvexElem& Hull=Body->AggGeom.ConvexElems[ClosestHull];
            const FTransform Transform=Hull.GetTransform();
            if(!Transform.IsValid()) {Error=TEXT("Local support repair has an invalid element transform");return false;}
            const FVector LocalPoint=Transform.InverseTransformPosition(Point);
            if(LocalPoint.ContainsNaN()) {Error=TEXT("Local support repair produced a non-finite vertex");return false;}
            const int32 Before=Hull.VertexData.Num();
            Hull.VertexData.AddUnique(LocalPoint);
            if(Hull.VertexData.Num()!=Before) {++AddedPoints;Touched[ClosestHull]=true;}
        }
        for(int32 HullIndex=0;HullIndex<Touched.Num();++HullIndex)
        {
            if(!Touched[HullIndex]) continue;
            FKConvexElem& Hull=Body->AggGeom.ConvexElems[HullIndex];
            TArray<Chaos::FConvex::FVec3Type> Points,Vertices;
            for(const FVector& Vertex:Hull.VertexData) Points.Add(Chaos::FConvex::FVec3Type(Vertex));
            TArray<Chaos::FConvex::FPlaneType> Planes;
            TArray<TArray<int32>> Faces;
            Chaos::FConvex::FAABB3Type Bounds;
            // Drop interior/repeated points, retaining the exact repaired solid within budget.
            Chaos::FConvexBuilder::Build(Points,Planes,Faces,Vertices,Bounds,Chaos::FConvexBuilder::EBuildMethod::ConvexHull3);
            if(Planes.IsEmpty() || Vertices.Num()<4 || Vertices.Num()>VertexLimit)
            {Error=TEXT("Local support repair cannot retain its solid within the vertex budget");return false;}
            Hull.VertexData.Reset(); Hull.IndexData.Reset();
            for(const auto& Vertex:Vertices) Hull.VertexData.Add(FVector(Vertex));
            Hull.UpdateElemBox(); Hull.ResetChaosConvexMesh();
        }
        if(AddedPoints>0) {Body->InvalidatePhysicsData();Body->CreatePhysicsMeshes();}
        TArray<Chaos::FConvex::FVec3Type> CookedPoints;
        for(const FKConvexElem& Hull:Body->AggGeom.ConvexElems)
        {
            const auto& Convex=Hull.GetChaosConvexMesh();
            if(!Convex || !Convex->IsValidGeometry()) {Error=TEXT("Local support repair could not recook a valid convex");return false;}
            for(int32 VertexIndex=0;VertexIndex<Convex->NumVertices();++VertexIndex)
                CookedPoints.Add(Convex->GetVertex(VertexIndex));
        }
        FSurface RepairedEnvelope;
        if(!BuildSurface(CookedPoints,RepairedEnvelope)) return false;
        MaxSupportError=0; MaxOutwardError=0;
        for(const auto& Point:SourcePoints)
            MaxSupportError=FMath::Max(MaxSupportError,DistanceToSurface(RepairedEnvelope,FVector(Point)));
        for(const auto& Point:CookedPoints)
            MaxOutwardError=FMath::Max(MaxOutwardError,DistanceToSurface(SourceSurface,FVector(Point)));
        // Bidirectional Euclidean distance bounds support in every orientation. The
        // separate saved compounds preserve cavities; neither temporary envelope is saved.
        if(MaxSupportError>SupportTolerance || MaxOutwardError>OutwardTolerance)
        {
            Error=FString::Printf(TEXT("Recooked local support errors exceed accuracy (inward %.9f/%.9f cm, outward %.9f/%.9f cm)"),
                MaxSupportError,SupportTolerance,MaxOutwardError,OutwardTolerance);
            return false;
        }
        return true;
    }

    bool Stage(FRequest& Request,FString& Error)
    {
        Request.Staged.Reset(NewObject<UBodySetup>(GetTransientPackage(),NAME_None,RF_Transient));
        UBodySetup* Body=Request.Staged.Get(); Body->CollisionTraceFlag=CTF_UseSimpleAsComplex;
        UBodySetup* Source=Request.Mesh->GetBodySetup();
        FBox AuthoredBounds(ForceInit);
        bool Preserve=Source && Source->AggGeom.GetElementCount()>0
            && Source->AggGeom.GetElementCount()==Source->AggGeom.ConvexElems.Num()+Source->AggGeom.BoxElems.Num()
            && Source->AggGeom.GetElementCount()<=Request.HullLimit;
        if(Preserve)
        {
            Source->CreatePhysicsMeshes();
            Body->AggGeom.ConvexElems=Source->AggGeom.ConvexElems;
            for(const FKBoxElem& Box:Source->AggGeom.BoxElems)
            { FKConvexElem Hull; Hull.ConvexFromBoxElem(Box); Body->AggGeom.ConvexElems.Add(MoveTemp(Hull)); }
            for(int32 HullIndex=0;HullIndex<Body->AggGeom.ConvexElems.Num();++HullIndex)
            {
                FKConvexElem& Hull=Body->AggGeom.ConvexElems[HullIndex];
                // FKConvexElem copying retains the authored data but drops its cooked pointer.
                // Read original convexes directly; converted boxes have no source convex.
                Chaos::FConvexPtr AuthoredConvex=HullIndex<Source->AggGeom.ConvexElems.Num()
                    ?Source->AggGeom.ConvexElems[HullIndex].GetChaosConvexMesh():Hull.GetChaosConvexMesh();
                // Existing authored collision may deliberately include a small cooked thickness
                // outside a thin render surface. Preserving it must use the existing physical
                // bounds, including cooked extreme points, rather than introduce a render-only cap.
                const FTransform Transform=Hull.GetTransform();
                for(const FVector& Vertex:Hull.VertexData) AuthoredBounds+=Transform.TransformPosition(Vertex);
                if(AuthoredConvex)
                    for(int32 I=0;I<AuthoredConvex->NumVertices();++I)
                        AuthoredBounds+=FVector(AuthoredConvex->GetVertex(I));
                if(Hull.VertexData.Num()<4) {Preserve=false;break;}
                if(Hull.VertexData.Num()>Request.VertexLimit)
                {
                    // Imported one-hull meshes often retain interior/repeated source vertices.
                    // Keep the same authored solid using its exact cooked extreme points.
                    Chaos::FConvexPtr Convex=AuthoredConvex;
                    if(!Convex)
                    {
                        TArray<Chaos::FConvex::FVec3Type> Points;
                        for(const FVector& Vertex:Hull.VertexData) Points.Add(Chaos::FConvex::FVec3Type(Vertex));
                        Convex=new Chaos::FConvex(Points,0);
                    }
                    if(!Convex->IsValidGeometry() || Convex->NumVertices()>Request.VertexLimit)
                    {Preserve=false;break;}
                    Hull.VertexData.Reset(); Hull.IndexData.Reset();
                    for(int32 I=0;I<Convex->NumVertices();++I)
                    {
                        const FVector Vertex(Convex->GetVertex(I));
                        // Source cooking already applied the element transform. Raw data keeps
                        // that transform separately; the locally built fallback is already local.
                        Hull.VertexData.Add(AuthoredConvex?Transform.InverseTransformPosition(Vertex):Vertex);
                    }
                    Hull.UpdateElemBox();
                }
            }
        }
        if(!Preserve)
        {
            Body->AggGeom.EmptyElements();
            // Normalize only the temporary decomposition input. Small imported meshes otherwise
            // hit V-HACD's native <1cm/<0.1cm rejection; authored output returns to asset coordinates.
            const FVector Center=Request.Input.Bounds.GetCenter(),Size=Request.Input.Bounds.GetSize();
            if(Size.GetMin()<=UE_DOUBLE_SMALL_NUMBER)
            { Error=TEXT("Collision-enabled triangles are planar or degenerate"); return false; }
            const double Scale=FMath::Max(100./Size.GetMax(),.2/Size.GetMin());
            TArray<FVector3f> Normalized; Normalized.Reserve(Request.Input.Vertices.Num());
            for(FVector3f Vertex:Request.Input.Vertices) Normalized.Add(FVector3f((FVector(Vertex)-Center)*Scale));
            DecomposeMeshToHulls(Body,Normalized,Request.Input.Indices,Request.HullLimit,Request.VertexLimit,Request.Resolution);
            for(FKConvexElem& Hull:Body->AggGeom.ConvexElems)
            {
                for(FVector& Vertex:Hull.VertexData) Vertex=Vertex/Scale+Center;
                Hull.UpdateElemBox();
            }
        }
        if(Body->AggGeom.ConvexElems.IsEmpty() || Body->AggGeom.ConvexElems.Num()>Request.HullLimit)
        { Error=TEXT("Native decomposition returned no hulls or exceeded the shape budget"); return false; }
        for(FKConvexElem& Hull:Body->AggGeom.ConvexElems)
        {
            if(Hull.VertexData.Num()<4 || Hull.VertexData.Num()>Request.VertexLimit)
            { Error=TEXT("Native hull exceeded the vertex budget"); return false; }
            for(const FVector& Vertex:Hull.VertexData) if(Vertex.ContainsNaN())
            { Error=TEXT("Native hull has non-finite vertices"); return false; }
            Hull.ResetChaosConvexMesh();
        }
        Body->PhysMaterial=Source?Source->PhysMaterial:nullptr;
        Body->InvalidatePhysicsData(); Body->CreatePhysicsMeshes();
        double MaxSupportError=0,MaxOutwardError=0;
        int32 AddedPoints=0;
        const double OutwardTolerance=2.75/Request.MaxRelevantScale;
        if(!Preserve && !CoverSourceVertices(Body,Request.Input,Request.VertexLimit,Request.SupportTolerance,OutwardTolerance,
            MaxSupportError,MaxOutwardError,AddedPoints,Error)) return false;
        FBox Bounds(ForceInit); double Volume=0; int32 TotalVertices=0;
        const double Tolerance=FMath::Max(.001,Request.Input.Bounds.GetSize().Size()*.02);
        FBox AllowedBounds=Request.Input.Bounds;
        if(Preserve && AuthoredBounds.IsValid) AllowedBounds+=AuthoredBounds;
        AllowedBounds=AllowedBounds.ExpandBy(Tolerance);
        for(const FKConvexElem& Hull:Body->AggGeom.ConvexElems)
        {
            TotalVertices+=Hull.VertexData.Num();
            const auto& Convex=Hull.GetChaosConvexMesh();
            if(!Convex || !Convex->IsValidGeometry()) {Error=TEXT("Chaos could not cook a valid convex solid");return false;}
            const double HullVolume=Convex->GetVolume()*FMath::Abs(Hull.GetTransform().GetDeterminant());
            if(!FMath::IsFinite(HullVolume) || HullVolume<=0) {Error=TEXT("Chaos hull has invalid volume");return false;}
            Volume+=HullVolume;
            for(const FVector& Vertex:Hull.VertexData)
            {
                const FVector Point=Hull.GetTransform().TransformPosition(Vertex);
                if(!AllowedBounds.IsInsideOrOn(Point))
                {Error=TEXT("Raw collision extends outside the source geometry bounds");return false;}
                Bounds+=Point;
            }
            // Chaos cooking stores convex vertices in body space and can add native
            // thickness to planar inputs. Validate the saved physical solid too.
            for(int32 VertexIndex=0;VertexIndex<Convex->NumVertices();++VertexIndex)
            {
                const FVector Point(Convex->GetVertex(VertexIndex));
                if(Point.ContainsNaN() || !AllowedBounds.IsInsideOrOn(Point))
                {Error=TEXT("Cooked collision extends outside the source geometry bounds");return false;}
                Bounds+=Point;
            }
        }
        if(!Bounds.IsValid || !Bounds.Intersect(Request.Input.Bounds))
        {Error=TEXT("Cooked collision does not intersect the source bounds");return false;}
        UE_LOG(LogMCFoodCollisionBake,Display,TEXT("MC_FOOD_COLLISION_BAKE mesh=%s method=%s hulls=%d/%d vertex_limit=%d total_vertices=%d volume_cm3=%.4f bounds=%s source_bounds=%s"),
            *Request.Mesh->GetPathName(),Preserve?TEXT("authored"):TEXT("vhacd"),Body->AggGeom.ConvexElems.Num(),Request.HullLimit,Request.VertexLimit,TotalVertices,Volume,*Bounds.ToString(),*Request.Input.Bounds.ToString());
        if(Preserve)
        {
            UE_LOG(LogMCFoodCollisionBake,Display,TEXT("MC_FOOD_SUPPORT_VALIDATED mesh=%s method=authored_unchanged error=not_measured relevant_scale=%.9f"),*Request.Mesh->GetPathName(),Request.MaxRelevantScale);
        }
        else
        {
            UE_LOG(LogMCFoodCollisionBake,Display,TEXT("MC_FOOD_SUPPORT_VALIDATED mesh=%s method=local_source_extremes inward_native_cm=%.9f inward_world_cm=%.9f outward_native_cm=%.9f outward_world_cm=%.9f relevant_scale=%.9f inward_tolerance_cm=%.9f outward_tolerance_cm=%.9f added_points=%d"),
                *Request.Mesh->GetPathName(),MaxSupportError,MaxSupportError*Request.MaxRelevantScale,MaxOutwardError,MaxOutwardError*Request.MaxRelevantScale,
                Request.MaxRelevantScale,Request.SupportTolerance,OutwardTolerance,AddedPoints);
        }
        return true;
    }

    bool SaveProfile(UMCFoodCollisionData* Profile,FString& Error)
    {
        UPackage* Package=Profile->GetOutermost();
        const FString Filename=FPackageName::LongPackageNameToFilename(Package->GetName(),FPackageName::GetAssetPackageExtension());
        if(!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true))
        {Error=FString::Printf(TEXT("Could not create generated collision folder for %s"),*Package->GetName());return false;}
        FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
        if(!UPackage::SavePackage(Package,Profile,*Filename,Args))
        {Error=FString::Printf(TEXT("Could not save generated food collision profile %s"),*Package->GetName());return false;}
        return true;
    }
}

bool MCFoodCollisionBaker::IsFoodMenuCollisionCurrent(UDataTable* Menu,TArray<FString>& OutErrors)
{
    TMap<FString,FRequest> Requests; TArray<FRowRequest> Rows;
    if(!Collect(Menu,Requests,Rows,OutErrors)) return false;
    for(const FRowRequest& Row:Rows) for(const FString& Key:Row.Keys)
    {
        const FRequest& Request=Requests.FindChecked(Key);
        if(!Matches(Row.Row->FindCollisionData(Request.Mesh),Request))
            OutErrors.Add(FString::Printf(TEXT("%s: food collision for %s is missing or stale; rebake the menu"),*Row.Name.ToString(),*Request.Mesh->GetName()));
    }
    return OutErrors.IsEmpty();
}

bool MCFoodCollisionBaker::BakeFoodMenuCollisions(UDataTable* Menu,bool bSaveProfiles,TArray<FString>& OutErrors,TArray<UMCFoodCollisionData*>& OutChanged)
{
    TMap<FString,FRequest> Requests; TArray<FRowRequest> Rows;
    if(!Collect(Menu,Requests,Rows,OutErrors)) return false;
    TArray<FString> Keys; Requests.GenerateKeyArray(Keys); Keys.Sort();
    // Stage the entire menu first. A decomposition failure changes neither old profiles nor rows.
    for(const FString& Key:Keys)
    {
        FRequest& Request=Requests.FindChecked(Key);
        const FString PackageName=ProfilePackage(Request),ObjectPath=PackageName+TEXT(".")+FPackageName::GetLongPackageAssetName(PackageName);
        Request.Profile=LoadObject<UMCFoodCollisionData>(nullptr,*ObjectPath,nullptr,LOAD_NoWarn);
        if(Matches(Request.Profile,Request)) continue;
        if(Request.Profile)
        {OutErrors.Add(FString::Printf(TEXT("Generated collision profile %s is invalid; refusing to replace an existing body"),*ObjectPath));continue;}
        FString Error;
        if(!Stage(Request,Error)) OutErrors.Add(FString::Printf(TEXT("%s: %s"),*Request.Mesh->GetPathName(),*Error));
    }
    if(!OutErrors.IsEmpty()) return false;
    for(const FString& Key:Keys)
    {
        FRequest& Request=Requests.FindChecked(Key);
        if(!Request.Profile)
        {
            const FString PackageName=ProfilePackage(Request);
            UPackage* Package=CreatePackage(*PackageName);
            Request.Profile=NewObject<UMCFoodCollisionData>(Package,*FPackageName::GetLongPackageAssetName(PackageName),RF_Public|RF_Standalone|RF_Transactional);
            Request.Profile->SourceMesh=Request.Mesh; Request.Profile->SourceGeometryKey=Request.Input.Key;
            Request.Profile->HullLimit=Request.HullLimit; Request.Profile->HullVertexLimit=Request.VertexLimit; Request.Profile->VoxelResolution=Request.Resolution;
            Request.Profile->BodySetup=DuplicateObject<UBodySetup>(Request.Staged.Get(),Request.Profile,TEXT("FoodBodySetup"));
            Request.Profile->BodySetup->ClearFlags(RF_Transient); Request.Profile->BodySetup->SetFlags(RF_Transactional);
            Request.Profile->BodySetup->CreatePhysicsMeshes();
            FAssetRegistryModule::AssetCreated(Request.Profile); Request.Profile->MarkPackageDirty(); Request.bChanged=true;
        }
        if(bSaveProfiles && (Request.bChanged || Request.Profile->GetOutermost()->IsDirty()))
        {
            FString Error; if(!SaveProfile(Request.Profile,Error)) OutErrors.Add(Error);
        }
        if(Request.bChanged) OutChanged.Add(Request.Profile);
    }
    if(!OutErrors.IsEmpty()) return false;
    const FScopedTransaction Transaction(NSLOCTEXT("MessControl","BakeFoodCollision","Bake food collision"));
    bool Modified=false;
    for(const FRowRequest& Row:Rows)
    {
        TArray<TObjectPtr<UMCFoodCollisionData>> Profiles;
        for(const FString& Key:Row.Keys) Profiles.Add(Requests.FindChecked(Key).Profile);
        if(Row.Row->CollisionData!=Profiles)
        {
            if(!Modified) {Menu->Modify(); Modified=true;}
            Row.Row->CollisionData=MoveTemp(Profiles);
        }
    }
    if(Modified) Menu->MarkPackageDirty();
    return true;
}
