#include "MCFoodActor.h"
#include "MCFoodBodyComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

// Meshes without CPU data keep the existing fragmentation path.
bool AMCFoodActor::SpawnCutFragments(FVector Direction,FVector WorldPoint,FVector WorldNormal)
{
    if(!HasAuthority() || (bFragment && CutPlanes.IsEmpty()) || CutPlanes.Num()>=4 || !ItemMesh
        || !ItemMesh->bAllowCPUAccess || WorldPoint.ContainsNaN() || WorldNormal.ContainsNaN()
        || WorldNormal.IsNearlyZero()) return false;
    const FTransform Pose=GetActorTransform();
    const FVector LocalPoint=Pose.InverseTransformPosition(WorldPoint);
    const FVector LocalNormal=(Pose.GetScale3D()*Pose.InverseTransformVectorNoScale(WorldNormal)).GetSafeNormal();
    if(LocalNormal.IsNearlyZero()) return false;
    TArray<AMCFoodActor*> Pieces;
    for(int32 Side:{1,-1}) {
        auto* Piece=GetWorld()->SpawnActorDeferred<AMCFoodActor>(StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Piece) break;
        // Keep the source mesh and avoid loading/cooking an unrelated preauthored
        // fragment which would immediately be replaced by this generated half.
        Piece->FoodData=FoodData; Piece->FoodData.FragmentScale=bFragment?FoodData.FragmentScale:FoodData.Scale;
        Piece->FoodData.Mass=FoodData.Mass*.5f; Piece->FoodData.Health=25.f;
        Piece->ItemName=ItemName; Piece->ItemMesh=ItemMesh; Piece->bFragment=true;
        Piece->Health=Piece->FoodData.Health; Piece->Settings.Mass=Piece->FoodData.Mass;
        Piece->Batch=Batch; Piece->LastHandledBy=LastHandledBy;
        const int32 AwardUnits=bFragment?CutAwardUnits:FoodData.FragmentCount();
        Piece->CutAwardUnits=Side==1?(AwardUnits+1)/2:AwardUnits/2;
        Piece->bSpoiled=bSpoiled; Piece->SpoilAt=SpoilAt; Piece->EntryRecoveryLanding=EntryRecoveryLanding;
        Piece->CutPlanes=CutPlanes;
        auto& Plane=Piece->CutPlanes.AddDefaulted_GetRef();
        Plane.bEnabled=true; Plane.Position=LocalPoint; Plane.Normal=LocalNormal*Side;
        Piece->FinishSpawning(Pose); Pieces.Add(Piece);
        if(!Piece->bCutVisualReady) break;
    }
    if(Pieces.Num()!=2 || !Pieces[0]->bCutVisualReady || !Pieces[1]->bCutVisualReady) {
        for(auto* Piece:Pieces) if(IsValid(Piece)) Piece->Destroy();
        return false;
    }
    for(int32 I=0;I<Pieces.Num();++I) {
        auto* Piece=Pieces[I];
        Piece->Body->SetMassOverrideInKg(NAME_None,Piece->FoodData.Mass,true);
        Piece->Body->SetPhysicsLinearVelocity(Direction.GetSafeNormal()*12+WorldNormal.GetSafeNormal()*(I==0?25.f:-25.f)+FVector(0,0,45));
        Piece->ForceNetUpdate();
    }
    return true;
}

bool AMCFoodActor::BuildCutVisual()
{
    bCutVisualReady=false;
    if(!Visual) return false;
    if(CutVisual) { CutVisual->ClearAllMeshSections(); CutVisual->SetHiddenInGame(true); }
    Visual->SetHiddenInGame(false);
    if(CutPlanes.IsEmpty() || !ItemMesh || !ItemMesh->bAllowCPUAccess) return false;
    if(!CutVisual) {
        CutVisual=NewObject<UProceduralMeshComponent>(this,TEXT("CutFoodMesh"));
        CutVisual->SetupAttachment(Body); CutVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        CutVisual->RegisterComponent();
    }
    CutVisual->SetRelativeTransform(Visual->GetRelativeTransform());
    // Copy render triangles only: the stock helper also cooks every original
    // convex hull even with bCreateCollision=false, causing multi-second stalls
    // on several authored food meshes. The sliced body is built separately.
    for(int32 Section=0;Section<ItemMesh->GetNumSections(0);++Section) {
        TArray<FVector> Vertices,Normals; TArray<int32> Triangles;
        TArray<FVector2D> UV; TArray<FProcMeshTangent> Tangents;
        UKismetProceduralMeshLibrary::GetSectionFromStaticMesh(ItemMesh,0,Section,Vertices,Triangles,Normals,UV,Tangents);
        TArray<FLinearColor> Colors;
        CutVisual->CreateMeshSection_LinearColor(Section,Vertices,Triangles,Normals,UV,Colors,Tangents,false);
        CutVisual->SetMaterial(Section,Visual->GetMaterial(Section));
    }
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
    const FTransform Pose=GetActorTransform();
    for(const auto& Plane:CutPlanes) {
        UProceduralMeshComponent* Unused=nullptr;
        const FVector WorldPoint=Pose.TransformPosition(Plane.Position);
        const FVector WorldNormal=Pose.TransformVectorNoScale(Plane.Normal/Pose.GetScale3D()).GetSafeNormal();
        if(!Plane.bEnabled || WorldNormal.IsNearlyZero()) return false;
        UKismetProceduralMeshLibrary::SliceProceduralMesh(CutVisual,WorldPoint,WorldNormal,false,Unused,
            EProcMeshSliceCapOption::CreateNewSectionForCap,CutMaterial);
    }
    TArray<FVector> Vertices;
    for(int32 Section=0;Section<CutVisual->GetNumSections();++Section) {
        if(const FProcMeshSection* Mesh=CutVisual->GetProcMeshSection(Section)) {
            if(Mesh->ProcIndexBuffer.IsEmpty()) continue;
            for(const FProcMeshVertex& Vertex:Mesh->ProcVertexBuffer)
                Vertices.Add(CutVisual->GetRelativeTransform().TransformPosition(Vertex.Position));
        }
    }
    auto* FoodBody=Cast<UMCFoodBodyComponent>(Body);
    if(Vertices.Num()<4 || !FoodBody || !FoodBody->SetCutCollision(Vertices)) return false;
    Visual->SetHiddenInGame(true); CutVisual->SetHiddenInGame(false);
    bCutVisualReady=true; return true;
}
