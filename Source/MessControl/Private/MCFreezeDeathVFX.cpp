#include "MCFreezeDeathVFX.h"
#include "MCToothCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "SkeletalRenderPublic.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

namespace MCFreezeDeathPrivate
{
    constexpr int32 Across=3,High=4,Count=Across*Across*High;
    struct FMesh
    {
        TArray<FVector> Vertices,Normals;
        TArray<FVector2D> UV;
        TArray<int32> Indices;
        TArray<FProcMeshTangent> Tangents;
        void Triangle(FVector A,FVector B,FVector C,FVector NA,FVector NB,FVector NC,
            FVector2D UA,FVector2D UB,FVector2D UC)
        {
            const int32 I=Vertices.Num();Vertices.Append({A,B,C});Normals.Append({NA,NB,NC});UV.Append({UA,UB,UC});
            Indices.Append({I,I+1,I+2});
            const FVector T=(B-A).GetSafeNormal();Tangents.Append({FProcMeshTangent(T,false),FProcMeshTangent(T,false),FProcMeshTangent(T,false)});
        }
        void Quad(FVector A,FVector B,FVector C,FVector D,FVector N)
        {
            Triangle(A,B,C,N,N,N,FVector2D(0,0),FVector2D(1,0),FVector2D(1,1));
            Triangle(A,C,D,N,N,N,FVector2D(0,0),FVector2D(1,1),FVector2D(0,1));
        }
        void Box(FVector E)
        {
            const float X=E.X,Y=E.Y,Z=E.Z;
            Quad({X,-Y,-Z},{X,Y,-Z},{X,Y,Z},{X,-Y,Z},{1,0,0});
            Quad({-X,Y,-Z},{-X,-Y,-Z},{-X,-Y,Z},{-X,Y,Z},{-1,0,0});
            Quad({-X,Y,-Z},{-X,Y,Z},{X,Y,Z},{X,Y,-Z},{0,1,0});
            Quad({X,-Y,-Z},{X,-Y,Z},{-X,-Y,Z},{-X,-Y,-Z},{0,-1,0});
            Quad({-X,-Y,Z},{X,-Y,Z},{X,Y,Z},{-X,Y,Z},{0,0,1});
            Quad({-X,Y,-Z},{X,Y,-Z},{X,-Y,-Z},{-X,-Y,-Z},{0,0,-1});
        }
        void Apply(UProceduralMeshComponent* Component,int32 Section)
        {
            if(!Indices.IsEmpty()) Component->CreateMeshSection_LinearColor(Section,Vertices,Indices,Normals,UV,{},Tangents,false);
        }
    };
}

AMCFreezeDeathVFX::AMCFreezeDeathVFX()
{
    PrimaryActorTick.bCanEverTick=true;bReplicates=false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("FrozenDeathRoot")));
    IceCube=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TransparentIceCube"));IceCube->SetupAttachment(GetRootComponent());
    IceCube->SetCollisionEnabled(ECollisionEnabled::NoCollision);IceCube->SetCastShadow(false);IceCube->SetCanEverAffectNavigation(false);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glass(TEXT("/Game/Gameplay/Cold/Death/M_FrozenDeathGlass"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Body(TEXT("/Game/Gameplay/Cold/Death/M_FrozenDeathBody"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Bag(TEXT("/Game/Gameplay/Cold/Death/M_FrozenDeathBag"));
    GlassMaterial=Glass.Object;BodyMaterial=Body.Object;BagMaterial=Bag.Object;
}
AMCFreezeDeathVFX* AMCFreezeDeathVFX::SpawnLocal(AMCToothCharacter* Hero,double At)
{
    if(!IsValid(Hero) || Hero->GetNetMode()==NM_DedicatedServer || !Hero->GetMesh()->GetSkeletalMeshAsset()) return nullptr;
    FActorSpawnParameters P;P.Owner=Hero;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* FX=Hero->GetWorld()->SpawnActor<AMCFreezeDeathVFX>(Hero->GetActorLocation(),FRotator(0,Hero->GetActorRotation().Yaw,0),P);
    if(FX)
    {
        FX->Source=Hero;FX->StartedAt=At;FX->Capture(Hero);FX->SetLifeSpan(5.f);
        if(FX->BodyFragments.IsEmpty()) {FX->Destroy();return nullptr;}
    }
    return FX;
}
UProceduralMeshComponent* AMCFreezeDeathVFX::MakePart(FName Name)
{
    auto* Part=NewObject<UProceduralMeshComponent>(this,Name);Part->SetupAttachment(GetRootComponent());
    Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);Part->SetGenerateOverlapEvents(false);
    Part->SetCanEverAffectNavigation(false);Part->bReceivesDecals=false;
    Part->RegisterComponent();AddInstanceComponent(Part);return Part;
}
void AMCFreezeDeathVFX::Capture(AMCToothCharacter* Hero)
{
    using namespace MCFreezeDeathPrivate;
    auto* Mesh=Hero->GetMesh();auto* Data=Mesh->GetSkeletalMeshAsset()->GetResourceForRendering();
    if(!Data || Data->LODRenderData.IsEmpty() || !Mesh->GetMeshObject()) return;
    // One capture per fatal freeze, including the facial morphs. Never evaluate CPU skinning per tick.
    TArray<FFinalSkinVertex> Skin;Mesh->GetCPUSkinnedVertices(Skin,0);
    if(Skin.IsEmpty()) return;
    const auto& LOD=Data->LODRenderData[0];const auto* Index=LOD.MultiSizeIndexContainer.GetIndexBuffer();
    if(!Index) return;
    TArray<FVector> Positions,Normals;TArray<FVector2D> UV;FBox Bounds(ForceInit);
    const FTransform MeshWorld=Mesh->GetComponentTransform();const FTransform EffectWorld=GetActorTransform();
    for(const auto& V:Skin)
    {
        const FVector P=EffectWorld.InverseTransformPosition(MeshWorld.TransformPosition(FVector(V.Position)));
        Positions.Add(P);Bounds+=P;
        Normals.Add(EffectWorld.InverseTransformVectorNoScale(MeshWorld.TransformVectorNoScale(FVector(V.TangentZ.ToFVector3f()))).GetSafeNormal());
        UV.Add(V.TextureCoordinates[0]);
    }
    const FVector Center=Bounds.GetCenter();
    HalfSize=FMath::Max(70.f,Bounds.GetExtent().GetMax()+15.f);
    const FVector CubeCenter(Center.X,Center.Y,Bounds.Min.Z+HalfSize-4.f);
    IceCube->SetRelativeLocation(CubeCenter);
    FMesh Cube;Cube.Box(FVector(HalfSize));Cube.Apply(IceCube,0);
    auto* Glass=UMaterialInstanceDynamic::Create(GlassMaterial,IceCube);IceCube->SetMaterial(0,Glass);
    TArray<UMaterialInstanceDynamic*> Materials;
    for(int32 Slot=0;Slot<Mesh->GetNumMaterials();++Slot)
    {
        auto* Material=UMaterialInstanceDynamic::Create(Slot==1?BagMaterial.Get():BodyMaterial.Get(),this);
        if(auto* Original=Mesh->GetMaterial(Slot)) Material->CopyMaterialUniformParameters(Original);
        Material->SetScalarParameterValue(TEXT("BodyStretch"),0);
        Material->SetScalarParameterValue(TEXT("Damage"),0);
        Material->SetScalarParameterValue(TEXT("Coffee"),0);
        Materials.Add(Material);
    }
    FRandomStream Random(9173);
    const FVector Cell(HalfSize*2/Across,HalfSize*2/Across,HalfSize*2/High);
    TArray<TArray<FMesh>> Groups;Groups.SetNum(Count);
    for(auto& Group:Groups) Group.SetNum(Materials.Num());
    for(int32 Z=0;Z<High;++Z) for(int32 Y=0;Y<Across;++Y) for(int32 X=0;X<Across;++X)
    {
        const int32 I=X+Across*(Y+Across*Z);
        const FVector Home=CubeCenter-FVector(HalfSize)+Cell*FVector(X+.5f,Y+.5f,Z+.5f);
        FShard M;M.Home=Home;
        const FVector Direction=(Home-CubeCenter).GetSafeNormal();
        M.Velocity=Direction*Random.FRandRange(105.f,225.f)+FVector(Random.FRandRange(-25,25),Random.FRandRange(-25,25),Random.FRandRange(110,230));
        M.Spin=FVector(Random.FRandRange(-130,130),Random.FRandRange(-160,160),Random.FRandRange(-120,120));
        M.Floor=Bounds.Min.Z+Cell.GetMin()*.32f;Motion.Add(M);
        auto* Body=MakePart(*FString::Printf(TEXT("FrozenBodyFragment%d"),I));Body->SetRelativeLocation(Home);BodyFragments.Add(Body);
        auto* Ice=MakePart(*FString::Printf(TEXT("IceCubeFragment%d"),I));Ice->SetRelativeLocation(Home);Ice->SetCastShadow(false);
        FMesh Shard;Shard.Box(Cell*.47f);Shard.Apply(Ice,0);Ice->SetMaterial(0,GlassMaterial);Ice->SetVisibility(false);IceFragments.Add(Ice);
    }
    for(const auto& Section:LOD.RenderSections)
    {
        const int32 Slot=Section.MaterialIndex;if(!Groups[0].IsValidIndex(Slot)) continue;
        for(uint32 T=0;T<Section.NumTriangles;++T)
        {
            const uint32 A=Index->Get(Section.BaseIndex+T*3),B=Index->Get(Section.BaseIndex+T*3+1),C=Index->Get(Section.BaseIndex+T*3+2);
            if(!Positions.IsValidIndex(A) || !Positions.IsValidIndex(B) || !Positions.IsValidIndex(C)) continue;
            const FVector P=((Positions[A]+Positions[B]+Positions[C])/3-CubeCenter+FVector(HalfSize))/Cell;
            const int32 I=FMath::Clamp(FMath::FloorToInt(P.X),0,Across-1)+Across*(FMath::Clamp(FMath::FloorToInt(P.Y),0,Across-1)+Across*FMath::Clamp(FMath::FloorToInt(P.Z),0,High-1));
            const FVector Home=Motion[I].Home;
            Groups[I][Slot].Triangle(Positions[A]-Home,Positions[B]-Home,Positions[C]-Home,Normals[A],Normals[B],Normals[C],UV[A],UV[B],UV[C]);
        }
    }
    for(int32 I=0;I<Count;++I) for(int32 Slot=0;Slot<Materials.Num();++Slot)
    {Groups[I][Slot].Apply(BodyFragments[I],Slot);BodyFragments[I]->SetMaterial(Slot,Materials[Slot]);}
}
void AMCFreezeDeathVFX::Tick(float Dt)
{
    Super::Tick(Dt);
    if(!IsValid(Source)) {Destroy();return;}
    const auto* GS=GetWorld()->GetGameState();const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    const float Age=FMath::Max(0.f,float(Now-StartedAt));const float BreakAge=AMCToothCharacter::FreezeDeathHoldSeconds;
    if(auto* Material=Cast<UMaterialInstanceDynamic>(IceCube->GetMaterial(0)))
        Material->SetScalarParameterValue(TEXT("Cracks"),FMath::SmoothStep(.75f,BreakAge,Age));
    if(Age<BreakAge) return;
    if(!bShattered) {bShattered=true;IceCube->SetVisibility(false);for(UProceduralMeshComponent* Ice:IceFragments) Ice->SetVisibility(true);}
    const float T=Age-BreakAge;
    const float Fade=1-FMath::SmoothStep(1.25f,2.15f,T);
    for(int32 I=0;I<Motion.Num();++I)
    {
        const auto& M=Motion[I];FVector P=M.Home+M.Velocity*T-FVector(0,0,290*T*T);
        if(P.Z<M.Floor) {P.Z=M.Floor+FMath::Abs(FMath::Sin(T*8+I))*(1-FMath::Clamp(T/1.5f,0.f,1.f))*8;P.X=FMath::Lerp(M.Home.X,P.X,.75f);P.Y=FMath::Lerp(M.Home.Y,P.Y,.75f);}
        const FRotator R(M.Spin.X*T,M.Spin.Y*T,M.Spin.Z*T);
        for(auto* Part:{BodyFragments[I].Get(),IceFragments[I].Get()})
        {Part->SetRelativeLocationAndRotation(P,R);Part->SetRelativeScale3D(FVector(Fade));}
    }
    if(T>=2.2f) Destroy();
}
