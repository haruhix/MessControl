#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodActor.h"
#include "MCToothCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/Material.h"
#include "PhysicsEngine/BodySetup.h"
#include "ProceduralMeshComponent.h"
#include "StaticMeshResources.h"
#include "Chaos/Convex.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
// An exact closed test solid with CPU render buffers. No saved asset, engine
// cube, mesh-description module, import/build pipeline or global flags change.
UStaticMesh* MakePortionTestBox()
{
    auto* Mesh=NewObject<UStaticMesh>(GetTransientPackage(),NAME_None,RF_Transient);
    Mesh->bAllowCPUAccess=true;
    TArray<FStaticMeshBuildVertex> Vertices;
    TArray<uint32> Indices;
    const int32 Faces[6][4]={{0,4,6,2},{1,3,7,5},{0,1,5,4},{2,6,7,3},{0,2,3,1},{4,5,7,6}};
    const auto Corner=[](int32 I){return FVector3f(I&1?50.f:-50.f,I&2?50.f:-50.f,I&4?50.f:-50.f);};
    for(const auto& Face:Faces) {
        const uint32 Base=Vertices.Num();
        const FVector3f X=(Corner(Face[1])-Corner(Face[0])).GetSafeNormal();
        const FVector3f Z=FVector3f::CrossProduct(X,Corner(Face[2])-Corner(Face[0])).GetSafeNormal();
        for(int32 I=0;I<4;++I) {
            auto& V=Vertices.AddDefaulted_GetRef();V.Position=Corner(Face[I]);
            V.TangentX=X;V.TangentY=FVector3f::CrossProduct(Z,X);V.TangentZ=Z;
            V.Color=FColor::White;V.UVs[0]=FVector2f(I==1||I==2?1.f:0.f,I>=2?1.f:0.f);
        }
        for(uint32 I:{0u,1u,2u,0u,2u,3u}) Indices.Add(Base+I);
    }
    auto Data=MakeUnique<FStaticMeshRenderData>();Data->AllocateLODResources(1);
    auto& LOD=Data->LODResources[0];
    LOD.VertexBuffers.PositionVertexBuffer.Init(Vertices,true);
    LOD.VertexBuffers.StaticMeshVertexBuffer.Init(Vertices,1,true);
    LOD.VertexBuffers.ColorVertexBuffer.Init(Vertices,true);
    LOD.IndexBuffer.SetIndices(Indices,EIndexBufferStride::Force32Bit);
    auto& Section=LOD.Sections.AddDefaulted_GetRef();
    Section.FirstIndex=0;Section.NumTriangles=Indices.Num()/3;
    Section.MinVertexIndex=0;Section.MaxVertexIndex=Vertices.Num()-1;Section.MaterialIndex=0;
    LOD.bBuffersInlined=true;Data->NumInlinedLODs=1;Data->ScreenSize[0].Default=1;
    Data->Bounds=FBoxSphereBounds(FBox(FVector(-50),FVector(50)));
    Mesh->GetStaticMaterials().Add(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface)));
    Mesh->SetRenderData(MoveTemp(Data));Mesh->CalculateExtendedBounds();
    Mesh->CreateBodySetup();
    auto* Setup=Mesh->GetBodySetup();Setup->CollisionTraceFlag=CTF_UseSimpleAsComplex;
    auto& Box=Setup->AggGeom.BoxElems.AddDefaulted_GetRef();Box.X=Box.Y=Box.Z=100;
    Mesh->InitResources();
    return Mesh;
}

struct FSliverWorld
{
    TStrongObjectPtr<UStaticMesh> Mesh{MakePortionTestBox()};
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Hero=nullptr;
    FSliverWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,2000),FRotator::ZeroRotator);
        if(Hero) Hero->GetCharacterMovement()->DisableMovement();
    }
    ~FSliverWorld()
    {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}

    AMCFoodActor* Food(FVector FullSize)
    {
        FMCFoodRow Row;Row.WholeMeshes={Mesh.Get()};Row.FragmentMeshes={Mesh.Get()};
        Row.Scale=Row.FragmentScale=FullSize/100;Row.Kind=EMCFoodKind::Food;
        Row.Resistance=EMCFoodResistance::Soft;Row.Health=100;Row.Mass=6;Row.Fragments=2;Row.SpoilSeconds=300;
        const FTransform Pose(FVector(-100,0,2000));
        auto* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Food) return nullptr;
        FRandomStream Random(7);Food->ConfigureItem(TEXT("SliverFixture"),Row,Random);Food->FinishSpawning(Pose);
        // These thickness/cap regressions exercise the retained manual pattern.
        Food->Settings.bAutoConsumeSmallCuts=false;
        Food->Settings.MaxHandDimensions=FVector(110);Food->Settings.MinCutVolume=1000;Food->Settings.MinCutThickness=12;
        Food->Phase=EMCFoodPhase::Free;Food->Body->SetSimulatePhysics(false);Food->SetActorTickEnabled(false);
        return Food;
    }
    TArray<AMCFoodActor*> Live() const
    {
        TArray<AMCFoodActor*> Out;
        for(TActorIterator<AMCFoodActor> It(World);It;++It)
            if(!It->IsDisposed()&&!It->IsActorBeingDestroyed()) Out.Add(*It);
        return Out;
    }
};

double IntrinsicFaceWidth(const AMCFoodActor* Food)
{
    TArray<Chaos::FConvex::FVec3Type> Points;
    if(Food->CutVisual) for(int32 I=0;I<Food->CutVisual->GetNumSections();++I)
        if(const auto* Section=Food->CutVisual->GetProcMeshSection(I))
            for(uint32 Index:Section->ProcIndexBuffer) if(Section->ProcVertexBuffer.IsValidIndex(Index))
                Points.Add(Chaos::FConvex::FVec3Type(Food->CutVisual->GetComponentTransform().TransformPosition(Section->ProcVertexBuffer[Index].Position)));
    const Chaos::FConvex Hull(Points,0);
    if(!Hull.IsValidGeometry()) return 0;
    double Width=DBL_MAX;
    for(int32 Face=0;Face<Hull.NumPlanes();++Face) {
        const FVector Normal=FVector(Hull.GetPlane(Face).Normal()).GetSafeNormal();
        double Low=DBL_MAX,High=-DBL_MAX;
        for(int32 V=0;V<Hull.NumVertices();++V) {
            const double D=FVector::DotProduct(FVector(Hull.GetVertex(V)),Normal);
            Low=FMath::Min(Low,D);High=FMath::Max(High,D);
        }
        Width=FMath::Min(Width,High-Low);
    }
    return Width;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCObliqueTipSliver,"MessControl.Food.Portions.ObliqueTipCannotBypassThickness",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCObliqueTipSliver::RunTest(const FString&)
{
    FSliverWorld T;
    if(!TestNotNull(TEXT("The isolated fixture has a player"),T.Hero)) return false;
    auto* Whole=T.Food(FVector(300,160,140));
    if(!TestNotNull(TEXT("The exact CPU box food spawns"),Whole)) return false;
    const FBox Bounds=Whole->Visual->Bounds.GetBox();
    const FVector FirstNormal=FVector(1.f/3.f,-1,0).GetSafeNormal();
    const FVector FirstPoint(Bounds.Min.X,Bounds.Min.Y+2,Bounds.GetCenter().Z);
    if(!TestTrue(TEXT("The broad diagonal cut succeeds"),Whole->HitFood(999,FVector::RightVector,T.Hero,FirstPoint,FirstNormal))) return false;
    AMCFoodActor* Wedge=nullptr;
    for(auto* Piece:T.Live()) if(!Piece->CutPlanes.IsEmpty()&&FVector::DotProduct(Piece->CutPlanes.Last().Normal,FirstNormal)>.99) Wedge=Piece;
    if(!TestNotNull(TEXT("The broad diagonal half remains available for a real recut"),Wedge)) return false;
    TestTrue(TEXT("The initial diagonal half has ample intrinsic thickness"),IntrinsicFaceWidth(Wedge)>12);
    const float Health=Wedge->Health,Mass=Wedge->FoodData.Mass;
    const int32 Count=T.Live().Num();
    const auto Phase=Wedge->Phase;
    const FVector TipPoint(Bounds.Min.X+20,Bounds.Min.Y,Bounds.GetCenter().Z);
    // The tip projects 20 cm along this NEW plane normal and has volume about
    // 14933 cm^3, but is only 8.22 cm wide along its inherited diagonal face.
    TestFalse(TEXT("A 20cm-deep but intrinsically thin diagonal tip is refused"),
        Wedge->HitFood(999,FVector::RightVector,T.Hero,TipPoint,-FVector::ForwardVector));
    TestFalse(TEXT("Refusal cannot dispose the diagonal parent"),Wedge->IsDisposed());
    TestEqual(TEXT("Refusal preserves parent health"),Wedge->Health,Health);
    TestEqual(TEXT("Refusal preserves parent mass"),Wedge->FoodData.Mass,Mass);
    TestEqual(TEXT("Refusal preserves parent phase"),Wedge->Phase,Phase);
    TestEqual(TEXT("No sliver or fallback actors survive refusal"),T.Live().Num(),Count);
    TestEqual(TEXT("Refusal cannot add a cut plane to the parent"),Wedge->CutPlanes.Num(),1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCAuthoredThinPortion,"MessControl.Food.Portions.AuthoredThinFoodCanStillBePortioned",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCAuthoredThinPortion::RunTest(const FString&)
{
    FSliverWorld T;
    auto* Whole=T.Food(FVector(300,160,6));
    if(!TestNotNull(TEXT("An intentionally six-centimetre-thick source spawns"),Whole)) return false;
    TestFalse(TEXT("The thin source is still too long to pick up"),Whole->FitsHandPortion());
    if(!TestTrue(TEXT("The unchanged authored thin axis does not block a lengthwise split"),
        Whole->HitFood(999,FVector::RightVector,T.Hero,Whole->Visual->Bounds.Origin,FVector::ForwardVector))) return false;
    const auto Pieces=T.Live();
    if(!TestEqual(TEXT("The thin authored food still yields exactly two portions"),Pieces.Num(),2)) return false;
    float TotalMass=0;
    for(auto* Piece:Pieces) {
        TestTrue(TEXT("Lengthwise cutting preserves the source's six-centimetre thickness"),FMath::IsNearlyEqual(IntrinsicFaceWidth(Piece),6.,.02));
        TotalMass+=Piece->FoodData.Mass;
        const float Health=Piece->Health;
        TestFalse(TEXT("The authored baseline exception cannot permit halving the six-centimetre axis"),
            Piece->HitFood(999,FVector::RightVector,T.Hero,Piece->CutVisual->Bounds.Origin,FVector::UpVector));
        TestEqual(TEXT("Rejected thinning preserves the parent health"),Piece->Health,Health);
        TestFalse(TEXT("Rejected thinning preserves the parent itself"),Piece->IsDisposed());
    }
    TestTrue(TEXT("The legal lengthwise split conserves mass"),FMath::IsNearlyEqual(TotalMass,6.f,.001f));
    TestEqual(TEXT("Rejected thinning cannot create additional fragments"),T.Live().Num(),2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCRepeatedObliqueCutTopology,"MessControl.Food.Portions.RepeatedObliqueCutsKeepValidCapPlanes",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCRepeatedObliqueCutTopology::RunTest(const FString&)
{
    FSliverWorld T;
    auto* Whole=T.Food(FVector(600,450,300));
    if(!TestNotNull(TEXT("The large exact CPU box spawns"),Whole)) return false;
    const auto WorldBounds=[](const AMCFoodActor* Food) {
        if(!Food->CutVisual) return Food->Visual->Bounds.GetBox();
        FBox Bounds(ForceInit);
        for(int32 S=0;S<Food->CutVisual->GetNumSections();++S)
            if(const auto* Data=Food->CutVisual->GetProcMeshSection(S))
                for(uint32 Index:Data->ProcIndexBuffer) if(Data->ProcVertexBuffer.IsValidIndex(Index))
                    Bounds+=Food->CutVisual->GetComponentTransform().TransformPosition(Data->ProcVertexBuffer[Index].Position);
        return Bounds;
    };
    int32 Accepted=0;
    for(int32 Step=0;Step<4;++Step) {
        const auto Before=T.Live();
        AMCFoodActor* Target=nullptr;double Largest=-1;
        for(auto* Food:Before) if(!Food->FitsHandPortion()) {
            const double Size=Food->GetHandPortionSize().GetMax();
            if(Size>Largest) {Largest=Size;Target=Food;}
        }
        if(!TestNotNull(TEXT("An oversized piece remains for the next broad cut"),Target)) return false;
        const FBox Bounds=WorldBounds(Target);
        const FVector Size=Bounds.GetSize();
        int32 Axis=Size.Y>Size.X?1:0;if(Size.Z>Size[Axis]) Axis=2;
        FVector Normal=FVector::ZeroVector;Normal[Axis]=1;
        Normal[(Axis+1)%3]=.2;Normal[(Axis+2)%3]=-.15;Normal.Normalize();
        if(!TestTrue(FString::Printf(TEXT("Broad oblique cut %d is accepted through real food damage"),Step+1),
            Target->HitFood(999,FVector::RightVector,T.Hero,Bounds.GetCenter(),Normal))) return false;
        ++Accepted;
        const auto After=T.Live();
        TestEqual(TEXT("One accepted cut replaces one parent with exactly two results"),After.Num(),Before.Num()+1);
        int32 Added=0;
        for(auto* Piece:After) if(!Before.Contains(Piece)) {
            ++Added;
            if(!TestNotNull(TEXT("Every accepted child has actual render geometry"),Piece->CutVisual.Get())) return false;
            TestTrue(TEXT("Every accepted child retains at least twelve centimetres of intrinsic thickness"),IntrinsicFaceWidth(Piece)>=12.-.02);
            const int32 SkinSections=Piece->ItemMesh->GetNumSections(0);
            const FTransform ToActor=Piece->CutVisual->GetRelativeTransform();
            for(int32 S=0;S<Piece->CutVisual->GetNumSections();++S) {
                const auto* Data=Piece->CutVisual->GetProcMeshSection(S);
                if(!Data || Data->ProcIndexBuffer.IsEmpty()) continue;
                if(!TestEqual(TEXT("Each render section contains complete triangle triplets"),Data->ProcIndexBuffer.Num()%3,0)) return false;
                for(int32 I=0;I<Data->ProcIndexBuffer.Num();I+=3) {
                    FVector P[3];
                    for(int32 Corner=0;Corner<3;++Corner) {
                        const uint32 Index=Data->ProcIndexBuffer[I+Corner];
                        if(!TestTrue(TEXT("Every render index addresses a real vertex"),Data->ProcVertexBuffer.IsValidIndex(Index))) return false;
                        const FVector Raw=Data->ProcVertexBuffer[Index].Position;
                        if(!TestFalse(TEXT("Every referenced render position is finite"),Raw.ContainsNaN())) return false;
                        P[Corner]=ToActor.TransformPosition(Raw);
                        for(const auto& Plane:Piece->CutPlanes) if(Plane.bEnabled)
                            if(!TestTrue(TEXT("Every used vertex remains inside every retained half-space"),
                                FVector::DotProduct(P[Corner]-Plane.Position,Plane.Normal)>=-.02)) return false;
                    }
                    if(!TestTrue(TEXT("Every rendered triangle has nonzero geometric area"),
                        FVector::CrossProduct(P[1]-P[0],P[2]-P[0]).SizeSquared()>1.e-12)) return false;
                    if(S>=SkinSections) {
                        bool SamePlane=false;
                        for(const auto& Plane:Piece->CutPlanes) if(Plane.bEnabled) {
                            bool All=true;
                            for(const FVector& Point:P) All&=FMath::Abs(FVector::DotProduct(Point-Plane.Position,Plane.Normal))<=.02;
                            SamePlane|=All;
                        }
                        if(!TestTrue(TEXT("A cap triangle lies entirely on one retained cut plane, never across unrelated planes"),SamePlane)) return false;
                    }
                }
            }
        }
        TestEqual(TEXT("The accepted split creates exactly two new child actors"),Added,2);
        TestTrue(TEXT("The successful parent is disposed"),Target->IsDisposed());
    }
    TestTrue(TEXT("The recipe exercises at least three real accepted cuts"),Accepted>=3);
    TestEqual(TEXT("Four cuts conserve the live result count"),T.Live().Num(),5);
    float Mass=0;for(auto* Piece:T.Live()) Mass+=Piece->FoodData.Mass;
    TestTrue(TEXT("Repeated cutting conserves the original food mass"),FMath::IsNearlyEqual(Mass,6.f,.001f));
    return true;
}
#endif
