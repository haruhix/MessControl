#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodActor.h"
#include "MCFoodBodyComponent.h"
#include "MCFoodCutGeometry.h"
#include "MCFoodCutStagingSubsystem.h"
#include "MCThroat.h"
#include "MCToothCharacter.h"
#include "MCWorldStatusComponent.h"
#include "Chaos/Convex.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/Material.h"
#include "PhysicsEngine/BodySetup.h"
#include "ProceduralMeshComponent.h"
#include "StaticMeshResources.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCFoodPreparedCutTests
{
// Same exact CPU-box construction already exercised by the sliver suite. A
// closed diagnostic solid gives two ordinary halves without modifying assets.
UStaticMesh* MakeBox()
{
    auto* Mesh=NewObject<UStaticMesh>(GetTransientPackage(),NAME_None,RF_Transient);
    Mesh->bAllowCPUAccess=true;
    TArray<FStaticMeshBuildVertex> Vertices;TArray<uint32> Indices;
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
    auto& Shape=Setup->AggGeom.BoxElems.AddDefaulted_GetRef();Shape.X=Shape.Y=Shape.Z=100;
    Mesh->InitResources();return Mesh;
}

struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Hero=nullptr;
    AMCThroat* Throat=nullptr;
    bool bEnded=false;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,2000),FRotator::ZeroRotator);
        if(Hero) {Hero->GetCharacterMovement()->DisableMovement();Hero->SetActorTickEnabled(false);}
        Throat=World->SpawnActor<AMCThroat>(FVector(1200,0,2000),FRotator::ZeroRotator);
        if(Throat) Throat->SetActorTickEnabled(false);
    }
    void Step() {World->Tick(LEVELTICK_All,1.f/60.f);}
    void End() {if(!bEnded){bEnded=true;World->EndPlay(EEndPlayReason::Quit);}}
    ~FWorld() {End();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    AMCFoodActor* Spawn(const FMCFoodRow& Row,FName Name,int32 Batch,bool Automatic,FVector ActorScale=FVector::OneVector)
    {
        const FTransform Pose(FRotator(11,27,5).Quaternion(),FVector(-100,0,2000),ActorScale);
        auto* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Food)return nullptr;
        FRandomStream Random(918411);Food->ConfigureItem(Name,Row,Random);
        Food->Batch=Batch;Food->FinishSpawning(Pose);
        Food->Settings.bAutoConsumeSmallCuts=Automatic;Food->Settings.MaxAutoConsumeDimensions=FVector(110);
        Food->Settings.MinCutVolume=1;Food->Settings.MinCutThickness=.1f;
        Food->Phase=EMCFoodPhase::Free;Food->Body->SetSimulatePhysics(false);Food->SetActorTickEnabled(false);
        return Food;
    }
    TArray<AMCFoodActor*> Foods(int32 Batch) const
    {
        TArray<AMCFoodActor*> Out;
        for(TActorIterator<AMCFoodActor> It(World);It;++It)
            if(It->Batch==Batch && !It->IsActorBeingDestroyed())Out.Add(*It);
        return Out;
    }
};

void ImportStatic(UStaticMesh* Mesh,UProceduralMeshComponent* Target)
{
    for(int32 S=0;S<Mesh->GetNumSections(0);++S) {
        TArray<FVector> V,N;TArray<int32> I;TArray<FVector2D> UV;TArray<FProcMeshTangent> T;TArray<FLinearColor> C;
        UKismetProceduralMeshLibrary::GetSectionFromStaticMesh(Mesh,0,S,V,I,N,UV,T);
        Target->CreateMeshSection_LinearColor(S,V,I,N,UV,C,T,false);
        Target->SetMaterial(S,Mesh->GetMaterial(S));
    }
}

// Intentionally bypasses actor/prepared/prefix caches: import raw static CPU
// sections and replay the stable descriptor using only low-level primitives.
bool IndependentReplay(const AMCFoodActor* Food,const FTransform& SourceLocal,UProceduralMeshComponent* Target)
{
    Target->SetWorldTransform(SourceLocal*Food->GetActorTransform());
    ImportStatic(Food->ItemMesh,Target);
    auto* Cap=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/Food/CutMaterials/MI_FoodCut_Cream.MI_FoodCut_Cream"));
    if(!Cap)Cap=Food->Visual->GetMaterial(0);
    const FTransform Pose=Food->GetActorTransform();
    for(const auto& Step:Food->CutPlanes) {
        if(Step.bEnabled) {
            const FVector Point=Pose.TransformPosition(Step.Position);
            const FVector Normal=Pose.TransformVectorNoScale(Step.Normal/Pose.GetScale3D()).GetSafeNormal();
            if(!MCFoodCutGeometry::SliceAndCap(Target,Point,Normal,Cap))return false;
        }
        if(Step.IslandIndex>=0 && !MCFoodCutGeometry::FilterIsland(Target,Step.IslandIndex))return false;
    }
    return true;
}

FBox IndexedBounds(UProceduralMeshComponent* Mesh,const FTransform& Transform)
{
    FBox Bounds(ForceInit);
    if(Mesh)for(int32 S=0;S<Mesh->GetNumSections();++S)
        if(const auto* Section=Mesh->GetProcMeshSection(S))for(uint32 I:Section->ProcIndexBuffer)
            if(Section->ProcVertexBuffer.IsValidIndex(I))Bounds+=Transform.TransformPosition(Section->ProcVertexBuffer[I].Position);
    return Bounds;
}

TArray<FVector> IndexedVertices(UProceduralMeshComponent* Mesh,const FTransform& Transform)
{
    TArray<FVector> Out;
    for(int32 S=0;S<Mesh->GetNumSections();++S)if(const auto* Section=Mesh->GetProcMeshSection(S)) {
        TSet<uint32> Seen;
        for(uint32 I:Section->ProcIndexBuffer)if(!Seen.Contains(I)&&Section->ProcVertexBuffer.IsValidIndex(I)) {
            Seen.Add(I);Out.Add(Transform.TransformPosition(Section->ProcVertexBuffer[I].Position));
        }
    }
    return Out;
}

// Independent legacy-equivalent hull oracle. Do not use the prepared-collision
// builder here: batching must preserve the original visible piece's proxy.
bool SameCollisionProxy(FAutomationTestBase& Test,UMCFoodBodyComponent* Body,const TArray<FVector>& Vertices)
{
    const auto* Setup=Body?Body->GetBodySetup():nullptr;
    if(!Test.TestTrue(TEXT("A physical prepared piece retains exactly one real convex proxy"),Setup&&Setup->AggGeom.ConvexElems.Num()==1))return false;
    TArray<Chaos::FConvex::FVec3Type> Points;
    for(const FVector& V:Vertices)Points.Add(Chaos::FConvex::FVec3Type(V));
    const Chaos::FConvex Reference(Points,0);
    if(!Test.TestTrue(TEXT("Independently replayed physical geometry has a valid convex hull"),Reference.IsValidGeometry()))return false;
    const auto& Actual=Setup->AggGeom.ConvexElems[0].VertexData;
    if(!Test.TestEqual(TEXT("Prepared collision retains the independently built hull vertex count"),Actual.Num(),Reference.NumVertices()))return false;
    FBox ActualBounds(ForceInit),ExpectedBounds(ForceInit);
    for(const FVector& V:Actual)ActualBounds+=V;
    for(int32 I=0;I<Reference.NumVertices();++I) {
        const FVector Expected(Reference.GetVertex(I));ExpectedBounds+=Expected;
        if(!Test.TestTrue(TEXT("Every independently built collision vertex remains in the prepared proxy"),
            Actual.ContainsByPredicate([&](const FVector& V){return V.Equals(Expected,.01);})))return false;
    }
    return Test.TestTrue(TEXT("Prepared collision bounds preserve the independent proxy"),ActualBounds.Equals(ExpectedBounds,.01));
}

FVector IndexedCenter(UProceduralMeshComponent* Mesh)
{
    FVector Sum=FVector::ZeroVector;int32 Count=0;
    for(int32 S=0;S<Mesh->GetNumSections();++S)if(const auto* Section=Mesh->GetProcMeshSection(S))
        for(uint32 I:Section->ProcIndexBuffer)if(Section->ProcVertexBuffer.IsValidIndex(I)) {
            Sum+=Mesh->GetComponentTransform().TransformPosition(Section->ProcVertexBuffer[I].Position);++Count;
        }
    return Count?Sum/Count:Mesh->Bounds.Origin;
}

bool SameIndexedMesh(FAutomationTestBase& Test,UProceduralMeshComponent* Actual,UProceduralMeshComponent* Replay)
{
    if(!Test.TestNotNull(TEXT("Prepared physical part has a visible mesh"),Actual))return false;
    if(!Test.TestEqual(TEXT("Prepared result retains replay material section slots"),Actual->GetNumSections(),Replay->GetNumSections()))return false;
    for(int32 S=0;S<Actual->GetNumSections();++S) {
        const auto* A=Actual->GetProcMeshSection(S);const auto* B=Replay->GetProcMeshSection(S);
        if(!Test.TestTrue(TEXT("Prepared/replayed sections both exist"),A && B))return false;
        Test.TestTrue(TEXT("Prepared skin/cap material binding matches independent replay"),Actual->GetMaterial(S)==Replay->GetMaterial(S));
        if(!Test.TestEqual(TEXT("Prepared indexed stream length matches independent replay"),A->ProcIndexBuffer.Num(),B->ProcIndexBuffer.Num()))return false;
        // Prepared sections compact unused vertices. Compare the referenced
        // triangle attribute stream, never raw buffer length or index values.
        for(int32 I=0;I<A->ProcIndexBuffer.Num();++I) {
            if(!Test.TestTrue(TEXT("Every compared triangle index is valid"),A->ProcVertexBuffer.IsValidIndex(A->ProcIndexBuffer[I])&&B->ProcVertexBuffer.IsValidIndex(B->ProcIndexBuffer[I])))return false;
            const auto& X=A->ProcVertexBuffer[A->ProcIndexBuffer[I]];const auto& Y=B->ProcVertexBuffer[B->ProcIndexBuffer[I]];
            if(!Test.TestTrue(TEXT("Prepared indexed vertex attributes preserve position, normal, UV, tangent and color"),
                X.Position.Equals(Y.Position,.0001)&&X.Normal.Equals(Y.Normal,.0001)&&X.UV0.Equals(Y.UV0,.0001)
                &&X.UV1.Equals(Y.UV1,.0001)&&X.UV2.Equals(Y.UV2,.0001)&&X.UV3.Equals(Y.UV3,.0001)
                &&X.Tangent.TangentX.Equals(Y.Tangent.TangentX,.0001)&&X.Tangent.bFlipTangentY==Y.Tangent.bFlipTangentY&&X.Color==Y.Color))return false;
        }
    }
    return Test.TestTrue(TEXT("Prepared/replayed world-space referenced bounds agree"),IndexedBounds(Actual,Actual->GetComponentTransform()).Equals(IndexedBounds(Replay,Replay->GetComponentTransform()),.01));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodPreparedCutReuse,
    "MessControl.Food.PreparedCut.ReusesPreparedGeometryTransactionally",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodPreparedCutReuse::RunTest(const FString&)
{
    using namespace MCFoodPreparedCutTests;
    using namespace MCFoodCutGeometry;
    const auto CheckAccounting=[this](const TArray<AMCFoodActor*>& Parts,float Mass,int32 Units) {
        float ActualMass=0;int32 ActualUnits=0;
        for(const auto* Part:Parts){ActualMass+=Part->FoodData.Mass;ActualUnits+=Part->CutAwardUnits;}
        TestTrue(TEXT("Prepared staging conserves complete mass including accepted receipts"),FMath::IsNearlyEqual(ActualMass,Mass,.001f));
        TestEqual(TEXT("Prepared staging conserves the integer award budget"),ActualUnits,Units);
    };
    const auto CheckReplay=[this](AMCFoodActor* Food) {
        TStrongObjectPtr<UProceduralMeshComponent> Replay(NewObject<UProceduralMeshComponent>(GetTransientPackage(),NAME_None,RF_Transient));
        const FTransform SourceLocal=Food->PortionSourceTransform();
        if(!TestTrue(TEXT("Independent raw-source descriptor replay succeeds without prepared caches"),IndependentReplay(Food,SourceLocal,Replay.Get())))return false;
        if(!SameIndexedMesh(*this,Food->CutVisual,Replay.Get()))return false;
        TArray<FIsland> Islands;FString Error;
        if(!TestTrue(TEXT("Independent replay remains a measurable capped solid"),FindIslands(Replay.Get(),Islands,&Error,256,SourceLocal))){AddError(Error);return false;}
        double Volume=0;for(const auto& Island:Islands)Volume+=Island.Volume;
        if(Food->UsesAutomaticCutIntake())TestTrue(TEXT("Prepared volume matches independently replayed indexed geometry"),
            Food->MeasuredPortionLocalVolume>0&&FMath::IsNearlyEqual(Food->MeasuredPortionLocalVolume,Volume,FMath::Max(.001,Volume*.00001)));
        const FBox Expected=IndexedBounds(Replay.Get(),SourceLocal);
        TestTrue(TEXT("Prepared canonical bounds match independent referenced geometry"),Food->CutStackLocalBounds.Equals(Expected,.01));
        return SameCollisionProxy(*this,CastChecked<UMCFoodBodyComponent>(Food->Body.Get()),IndexedVertices(Replay.Get(),SourceLocal));
    };
    const auto CheckStaged=[this](const FWorld& T,AMCFoodActor* Source) {
        for(auto* Part:T.Foods(Source->Batch))if(Part!=Source) {
            if(!TestTrue(TEXT("An unfinished transaction exposes no visible, replicated or rewarded partial child"),
                Part->IsCutTransactionStaged()&&Part->IsHidden()&&!Part->GetIsReplicated()&&!Part->Body->IsSimulatingPhysics()
                &&!Part->WasAutomaticallyConsumed()&&!Part->WasAutomaticallyDiscarded()&&!Part->bDeliveryScored))return false;
            if(!TestTrue(TEXT("A staged part has no usable grip or screen status"),
                Part->GripSurface->GetCollisionEnabled()==ECollisionEnabled::NoCollision
                &&(!Part->WorldStatus->Screen||!Part->WorldStatus->Screen->IsVisible())))return false;
            if(Part->bCutIntakeReceipt) {
                if(!TestTrue(TEXT("A staged ready receipt has no collision or procedural renderer"),
                    Part->Body->GetCollisionEnabled()==ECollisionEnabled::NoCollision&&!Part->CutVisual))return false;
            } else for(int32 Channel=0;Channel<int32(ECC_MAX);++Channel)
                if(!TestEqual(TEXT("A warmed staged body ignores every gameplay collision channel"),
                    Part->Body->GetCollisionResponseToChannel(ECollisionChannel(Channel)),ECR_Ignore))return false;
        }
        return true;
    };
    const auto FinishTransactions=[this,&CheckStaged](FWorld& T,const TArray<AMCFoodActor*>& Sources) {
        auto* Scheduler=T.World->GetSubsystem<UMCFoodCutStagingSubsystem>();
        if(!TestNotNull(TEXT("The game world owns its shared food staging scheduler"),Scheduler))return false;
        TArray<float> Healths,Masses;TArray<int32> AwardUnits;TArray<EMCFoodPhase> Phases;
        for(const auto* Source:Sources){Healths.Add(Source->Health);Masses.Add(Source->FoodData.Mass);AwardUnits.Add(Source->CutAwardUnits);Phases.Add(Source->Phase);}
        int32 Frames=0;
        for(;Frames<240&&Sources.ContainsByPredicate([](const auto* Source){return Source->IsCutTransactionPending();});++Frames) {
            const uint64 Before=GetPreparationStats().StagedActors;T.Step();
            if(!TestTrue(TEXT("One world tick respects the shared staging actor budget"),
                GetPreparationStats().StagedActors-Before<=uint64(Scheduler->MaxStagedActorsPerTick)
                &&Scheduler->GetLastStagedActors()<=Scheduler->MaxStagedActorsPerTick))return false;
            for(int32 I=0;I<Sources.Num();++I) {
                auto* Source=Sources[I];
                if(Source->IsCutTransactionPending()) {
                    if(!TestTrue(TEXT("A pending source retains its health, mass, phase and unspent awards"),
                        !Source->IsDisposed()&&!Source->bDeliveryScored&&Source->Health==Healths[I]&&Source->FoodData.Mass==Masses[I]
                        &&Source->CutAwardUnits==AwardUnits[I]&&Source->Phase==Phases[I]))return false;
                    if(!CheckStaged(T,Source))return false;
                } else for(auto* Part:T.Foods(Source->Batch)) {Part->Body->SetSimulatePhysics(false);Part->SetActorTickEnabled(false);}
            }
        }
        for(auto* Source:Sources)if(!TestTrue(TEXT("A queued cut completes atomically within bounded world ticks"),
            !Source->IsCutTransactionPending()&&Source->IsDisposed()))return false;
        AddInfo(FString::Printf(TEXT("Prepared food transaction completed in %d bounded world ticks"),Frames));
        return true;
    };

    // Real saved many-island artwork is the motivating path. The expected
    // populations are measured before resetting production-work counters;
    // there is no fixed47/9/38 assumption about an artist's future mesh edits.
    {
        FWorld T;
        if(!TestNotNull(TEXT("Prepared grape test has a worker"),T.Hero)||!TestNotNull(TEXT("Prepared grape test has an intake"),T.Throat))return false;
        const auto* Menu=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
        if(!TestNotNull(TEXT("Current saved menu loads for reuse regression"),Menu))return false;
        const auto* Saved=Menu->FindRow<FMCFoodRow>(TEXT("Bacon"),TEXT("Prepared grape reuse"),false);
        if(!TestNotNull(TEXT("Current grape row is available"),Saved))return false;
        FMCFoodRow Row=*Saved;
        UStaticMesh* Mesh=nullptr;
        for(const auto& Choice:Row.WholeMeshes)if(Choice.ToString().Contains(TEXT("Grapes"))){Mesh=Choice.LoadSynchronous();if(Mesh)break;}
        if(!TestNotNull(TEXT("Actual saved grape mesh loads"),Mesh))return false;
#if WITH_EDITOR
        FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
        if(!TestTrue(TEXT("Saved grapes retain their runtime CPU mesh"),Mesh->bAllowCPUAccess))return false;
        Row.WholeMeshes={Mesh};
        TStrongObjectPtr<UProceduralMeshComponent> Reference(NewObject<UProceduralMeshComponent>(GetTransientPackage(),NAME_None,RF_Transient));
        ImportStatic(Mesh,Reference.Get());
        TArray<FIsland> Expected;FString Error;
        if(!TestTrue(TEXT("Saved grape disconnected parts can be inspected independently"),FindIslands(Reference.Get(),Expected,&Error,256,FTransform(FQuat::Identity,FVector::ZeroVector,Row.Scale)))){AddError(Error);return false;}
        int32 ExpectedReady=0,ExpectedPhysical=0;
        for(const auto& Island:Expected)if(Island.Volume>0&&Island.Volume<=110.*110.*110.)++ExpectedReady;else ++ExpectedPhysical;
        if(!TestTrue(TEXT("Saved-scale grapes exercise deferred many-part staging and mixed readiness"),Expected.Num()>8&&ExpectedReady>0&&ExpectedPhysical>2))return false;
        ResetPreparationStats();
        auto* Source=T.Spawn(Row,TEXT("Bacon"),918411,true);
        if(!TestNotNull(TEXT("Saved-scale grape actor spawns"),Source))return false;
        const float Mass=Source->FoodData.Mass,Health=Source->Health;const int32 Awards=Source->FoodData.FragmentCount(),Units=Source->CutAwardUnits;
        if(!TestTrue(TEXT("A production knife contact stages the saved independent grape parts"),Source->HitFood(1,FVector::ForwardVector,T.Hero,Source->Visual->Bounds.Origin,FVector::RightVector)))return false;
        TestTrue(TEXT("An accepted many-part contact keeps its source and awards intact until completion"),
            Source->IsCutTransactionPending()&&!Source->IsDisposed()&&Source->Health==Health&&Source->FoodData.Mass==Mass
            &&Source->CutAwardUnits==Units&&!Source->bDeliveryScored&&T.Throat->FoodSwallowed==0);
        TestFalse(TEXT("A second contact cannot enqueue or award the pending source again"),Source->HitFood(1,FVector::ForwardVector,T.Hero,Source->Visual->Bounds.Origin,FVector::RightVector));
        if(!CheckStaged(T,Source)||!FinishTransactions(T,{Source}))return false;
        auto Parts=T.Foods(Source->Batch);Parts.Remove(Source);
        const auto Work=GetPreparationStats();
        if(!TestEqual(TEXT("Every saved grape island has one physical part or receipt"),Parts.Num(),Expected.Num()))return false;
        TestEqual(TEXT("Many grape children import their static source only once"),Work.RootImports,uint64(1));
        TestEqual(TEXT("Disconnected source does not replay a cut per child"),Work.HistorySlices,uint64(0));
        TestEqual(TEXT("Many grape children share one topology pass"),Work.TopologyPasses,uint64(1));
        TestEqual(TEXT("All disconnected grape parts are extracted in one batch"),Work.IslandExtractions,uint64(1));
        TestEqual(TEXT("Only physical grape parts install prepared render geometry"),Work.Installations,uint64(ExpectedPhysical));
        TestEqual(TEXT("Each physical grape child builds its convex once"),Work.PhysicsBuilds,uint64(ExpectedPhysical));
        TestEqual(TEXT("The grape batch prepares only the physical pieces' convexes"),Work.ConvexBuilds,uint64(ExpectedPhysical));
        TestEqual(TEXT("The grape batch installs only the physical pieces' collision"),Work.CollisionInstalls,uint64(ExpectedPhysical));
        TestEqual(TEXT("All physical grape convexes are prepared together"),Work.ConvexBatches,uint64(1));
        TestEqual(TEXT("Exactly one many-part transaction was queued"),Work.QueuedCuts,uint64(1));
        TestEqual(TEXT("The many-part transaction committed only once"),Work.CommittedCuts,uint64(1));
        TestEqual(TEXT("Every planned result was staged once"),Work.StagedActors,uint64(Expected.Num()));
        TestTrue(TEXT("Prepared materialization is bounded by physical outputs plus shared source work"),Work.Materializations<=uint64(ExpectedPhysical+2));
        int32 Ready=0,Physical=0;
        for(auto* Part:Parts) {
            if(Part->WasAutomaticallyConsumed()) {
                ++Ready;TestTrue(TEXT("Ready prepared island stays a hidden collision-free receipt"),Part->IsDisposed()&&!Part->GetIsReplicated()&&Part->Body->GetCollisionEnabled()==ECollisionEnabled::NoCollision&&!Part->Body->IsSimulatingPhysics());
                TestNull(TEXT("Ready prepared receipt does not register a procedural renderer"),Part->CutVisual.Get());
                TestFalse(TEXT("A ready receipt has no mesh/convex collision installation"),CastChecked<UMCFoodBodyComponent>(Part->Body.Get())->HasMeshCollision());
                TestTrue(TEXT("Each ready receipt uses its own positive volume within 110 cubed"),Part->MeasuredPortionLocalVolume>0&&Part->MeasuredPortionLocalVolume<=110.*110.*110.);
            } else {
                ++Physical;TestTrue(TEXT("Unready prepared island retains gameplay visibility/collision"),!Part->IsDisposed()&&!Part->IsHidden()&&Part->GetIsReplicated()&&Part->CutVisual&&Part->Body->GetCollisionEnabled()!=ECollisionEnabled::NoCollision);
                TestTrue(TEXT("Each persistent grape piece exceeds the unchanged 110 cubed limit"),Part->MeasuredPortionLocalVolume>110.*110.*110.);
                const int32 IslandIndex=Part->CutPlanes.IsEmpty()?INDEX_NONE:Part->CutPlanes.Last().IslandIndex;
                if(!TestTrue(TEXT("A physical grape part retains its original deterministic island selector"),Expected.IsValidIndex(IslandIndex)))return false;
                TArray<FVector> CollisionVertices;
                const FTransform SourceLocal=Part->PortionSourceTransform();
                for(const FVector& V:Expected[IslandIndex].Vertices)CollisionVertices.Add(SourceLocal.TransformPosition(V));
                if(!SameCollisionProxy(*this,CastChecked<UMCFoodBodyComponent>(Part->Body.Get()),CollisionVertices))return false;
                Part->Body->SetSimulatePhysics(false);Part->SetActorTickEnabled(false);
                Part->OnRep_Item(); // Repeated replication is a real idempotency boundary.
            }
        }
        TestEqual(TEXT("Every ready grape part completes independently"),Ready,ExpectedReady);
        TestEqual(TEXT("Every oversized grape part remains physical"),Physical,ExpectedPhysical);
        TestEqual(TEXT("Ready grape receipts reach the actual throat once"),T.Throat->FoodSwallowed,ExpectedReady);
        CheckAccounting(Parts,Mass,Awards);
        const auto Repeated=GetPreparationStats();
        TestEqual(TEXT("Repeated item notification does not re-import source"),Repeated.RootImports,Work.RootImports);
        TestEqual(TEXT("Repeated item notification does not repeat topology"),Repeated.TopologyPasses,Work.TopologyPasses);
        TestEqual(TEXT("Repeated item notification does not reinstall geometry"),Repeated.Installations,Work.Installations);
        TestEqual(TEXT("Repeated item notification does not rebuild physics"),Repeated.PhysicsBuilds,Work.PhysicsBuilds);
        TestEqual(TEXT("Repeated item notification does not rebuild convex geometry"),Repeated.ConvexBuilds,Work.ConvexBuilds);
        TestEqual(TEXT("Repeated item notification does not reinstall collision"),Repeated.CollisionInstalls,Work.CollisionInstalls);
        AddInfo(FString::Printf(TEXT("Saved grape prepared path: parts=%d ready=%d physical=%d rootImports=%llu topology=%llu installs=%llu physics=%llu"),Parts.Num(),Ready,Physical,Work.RootImports,Work.TopologyPasses,Work.Installations,Work.PhysicsBuilds));

        const auto Queue=[this,&Row](FWorld& Fixture,int32 Batch,FVector Location=FVector(-100,0,2000))->AMCFoodActor* {
            auto* Food=Fixture.Spawn(Row,TEXT("PreparedLifecycleGrapes"),Batch,true);
            if(!TestNotNull(TEXT("A lifecycle grape source spawns"),Food))return nullptr;
            Food->SetActorLocation(Location);
            if(!TestTrue(TEXT("The lifecycle source accepts one deferred real knife contact"),
                Food->HitFood(1,FVector::ForwardVector,Fixture.Hero,Food->Visual->Bounds.Origin,FVector::RightVector)
                &&Food->IsCutTransactionPending()&&!Food->IsDisposed()))return nullptr;
            return Food;
        };
        const auto StageSome=[this,&CheckStaged](FWorld& Fixture,AMCFoodActor* Food) {
            for(int32 Frame=0;Frame<24&&Food->IsCutTransactionPending();++Frame) {
                Fixture.Step();if(!CheckStaged(Fixture,Food))return false;
                if(Fixture.Foods(Food->Batch).Num()>1)return true;
            }
            return TestTrue(TEXT("The fixture reaches real partial staging before cancellation"),false);
        };
        {
            FWorld Shared;
            auto* A=Queue(Shared,918416);auto* B=Queue(Shared,918417,FVector(5000,0,2000));
            if(!A||!B)return false;
            const uint64 StagedBefore=GetPreparationStats().StagedActors;
            const uint64 CommittedBefore=GetPreparationStats().CommittedCuts;
            if(!FinishTransactions(Shared,{A,B}))return false;
            TestEqual(TEXT("Two simultaneous sources share the same perworld budget and each stage once"),
                GetPreparationStats().StagedActors-StagedBefore,uint64(Expected.Num()*2));
            TestEqual(TEXT("Two queued sources each commit once"),GetPreparationStats().CommittedCuts-CommittedBefore,uint64(2));
            TestEqual(TEXT("Both committed batches account only for their own ready islands"),Shared.Throat->FoodSwallowed,ExpectedReady*2);
            for(auto* Food:{A,B}) {
                auto Results=Shared.Foods(Food->Batch);Results.Remove(Food);
                TestEqual(TEXT("Shared scheduling retains every planned result"),Results.Num(),Expected.Num());
                CheckAccounting(Results,Food->FoodData.Mass,Food->FoodData.FragmentCount());
            }
        }
        {
            FWorld Lifecycle;
            auto* Cancelled=Queue(Lifecycle,918418);if(!Cancelled||!StageSome(Lifecycle,Cancelled))return false;
            const float OriginalHealth=Cancelled->Health,OriginalMass=Cancelled->FoodData.Mass;
            const int32 OriginalUnits=Cancelled->CutAwardUnits;
            // Put the real legacy intake footprint over the unchanged source.
            // Spatial admission is true, so pending/staged state must be the
            // reason its direct actor-iteration query refuses these objects.
            const FTransform IntakePose=Lifecycle.Throat->GetActorTransform();
            const bool ManualIntake=Lifecycle.Throat->bManualIntake;Lifecycle.Throat->bManualIntake=false;
            Lifecycle.Throat->SetActorLocation(Cancelled->GetActorLocation()-IntakePose.TransformVector(Lifecycle.Throat->ZoneCenter));
            TestTrue(TEXT("Pending-source delivery test is inside the real intake footprint"),Lifecycle.Throat->ContainsDeliveryPosition(Cancelled->GetActorLocation()));
            TestFalse(TEXT("The real throat's direct query refuses an otherwise in-zone pending source"),Lifecycle.Throat->ContainsFood(Cancelled));
            for(auto* Part:Lifecycle.Foods(Cancelled->Batch))if(Part!=Cancelled) {
                TestTrue(TEXT("A staged child is spatially inside the same intake footprint"),Lifecycle.Throat->ContainsDeliveryPosition(Part->GetActorLocation()));
                TestFalse(TEXT("The real throat's direct query refuses a hidden staged child"),Lifecycle.Throat->ContainsFood(Part));
            }
            Cancelled->CancelPendingCut();
            TestTrue(TEXT("The same source becomes directly admissible after cancellation restores it"),Lifecycle.Throat->ContainsFood(Cancelled));
            Lifecycle.Throat->bManualIntake=ManualIntake;Lifecycle.Throat->SetActorTransform(IntakePose);
            for(int32 I=0;I<3;++I)Lifecycle.Step();
            TestTrue(TEXT("Explicit cancellation restores the source without mass, health or award loss"),
                !Cancelled->IsCutTransactionPending()&&!Cancelled->IsDisposed()&&!Cancelled->bDeliveryScored
                &&Cancelled->Health==OriginalHealth&&Cancelled->FoodData.Mass==OriginalMass&&Cancelled->CutAwardUnits==OriginalUnits
                &&Cancelled->CutPlanes.IsEmpty()&&Lifecycle.Foods(Cancelled->Batch).Num()==1&&Lifecycle.Throat->FoodSwallowed==0);
            TestTrue(TEXT("A cancelled source can accept a later normal knife contact"),
                Cancelled->HitFood(1,FVector::ForwardVector,Lifecycle.Hero,Cancelled->Visual->Bounds.Origin,FVector::RightVector));
            Cancelled->CancelPendingCut();

            auto* Disposed=Queue(Lifecycle,918419);if(!Disposed||!StageSome(Lifecycle,Disposed))return false;
            Disposed->Dispose();for(int32 I=0;I<3;++I)Lifecycle.Step();
            TestTrue(TEXT("External disposal cancels staged children without intake or task credit"),
                Disposed->IsDisposed()&&!Disposed->IsCutTransactionPending()&&!Disposed->bDeliveryScored
                &&Lifecycle.Foods(Disposed->Batch).Num()==1&&Lifecycle.Throat->FoodSwallowed==0);

            auto* Destroyed=Queue(Lifecycle,918420);if(!Destroyed||!StageSome(Lifecycle,Destroyed))return false;
            const int32 DestroyedBatch=Destroyed->Batch;Destroyed->Destroy();
            for(int32 I=0;I<3;++I)Lifecycle.Step();
            TestTrue(TEXT("Actor teardown leaves no child or award behind"),
                Lifecycle.Foods(DestroyedBatch).IsEmpty()&&Lifecycle.Throat->FoodSwallowed==0);

            auto* Expired=Queue(Lifecycle,918421);if(!Expired||!StageSome(Lifecycle,Expired))return false;
            auto* OldEndpoint=Lifecycle.Throat;OldEndpoint->Destroy();Lifecycle.Throat=nullptr;
            for(int32 I=0;I<120&&Expired->IsCutTransactionPending();++I)Lifecycle.Step();
            TestTrue(TEXT("Endpoint loss cancels the entire uncommitted batch and preserves its source"),
                !Expired->IsCutTransactionPending()&&!Expired->IsDisposed()&&!Expired->bDeliveryScored
                &&Expired->Health==OriginalHealth&&Expired->FoodData.Mass==OriginalMass&&Expired->CutAwardUnits==OriginalUnits
                &&Lifecycle.Foods(Expired->Batch).Num()==1&&OldEndpoint->FoodSwallowed==0);
        }
        {
            FWorld Ending;
            auto* Food=Queue(Ending,918422);if(!Food||!StageSome(Ending,Food))return false;
            Ending.End();
            TestTrue(TEXT("World EndPlay cancels every staged child without intake or source award"),
                !Food->IsCutTransactionPending()&&!Food->bDeliveryScored&&Ending.Foods(Food->Batch).Num()==1&&Ending.Throat->FoodSwallowed==0);
        }
    }

    TStrongObjectPtr<UStaticMesh> Box(MakeBox());
    FMCFoodRow Row;Row.WholeMeshes={Box.Get()};Row.FragmentMeshes={Box.Get()};
    Row.Scale=Row.FragmentScale=FVector(3,2.6,2.2);Row.Kind=EMCFoodKind::Food;Row.Resistance=EMCFoodResistance::Soft;
    Row.Health=100;Row.Mass=6;Row.Fragments=4;Row.SpoilSeconds=300;
    for(bool Automatic:{true,false}) {
        FWorld T;
        if(!TestNotNull(TEXT("Ordinary prepared-cut fixture has a worker"),T.Hero))return false;
        auto* Source=T.Spawn(Row,TEXT("PreparedBox"),Automatic?918412:918413,Automatic,FVector(1.1,.85,1.2));
        if(!TestNotNull(TEXT("Rotated/nonuniform ordinary solid spawns"),Source))return false;
        const float Mass=Source->FoodData.Mass;const int32 Awards=Source->FoodData.FragmentCount();
        ResetPreparationStats();
        const FVector Normal=FVector(1,.37,.19).GetSafeNormal();
        if(!TestTrue(TEXT("Ordinary oblique knife cut succeeds in automatic and retained legacy mode"),Source->HitFood(999,FVector::ForwardVector,T.Hero,Source->Visual->Bounds.Origin,Normal)))return false;
        auto Parts=T.Foods(Source->Batch);Parts.Remove(Source);
        if(!TestEqual(TEXT("One connected solid produces two ordinary prepared halves"),Parts.Num(),2))return false;
        const auto FirstCut=GetPreparationStats();
        TestTrue(TEXT("Ordinary halves do not each rebuild the root"),FirstCut.RootImports<=1);
        TestTrue(TEXT("Ordinary half staging slices each outcome once without replaying in children"),FirstCut.HistorySlices>0&&FirstCut.HistorySlices<=2);
        TestEqual(TEXT("Each ordinary half installs once"),FirstCut.Installations,uint64(2));
        TestEqual(TEXT("Each ordinary half builds one convex"),FirstCut.PhysicsBuilds,uint64(2));
        TestEqual(TEXT("Two ordinary half convexes are prepared once each"),FirstCut.ConvexBuilds,uint64(2));
        TestEqual(TEXT("The two prepared half proxies are installed once each"),FirstCut.CollisionInstalls,uint64(2));
        for(auto* Part:Parts) {
            TestEqual(TEXT("Prepared child retains the selected intake mode"),Part->UsesAutomaticCutIntake(),Automatic);
            Part->Body->SetSimulatePhysics(false);Part->SetActorTickEnabled(false);
            if(!CheckReplay(Part))return false;
        }
        CheckAccounting(Parts,Mass,Awards);
        // Reconstruct from the replicated descriptor without copying a prepared
        // state pointer. The live sibling may share its immutable cached proxy.
        const auto BeforeReconstruction=GetPreparationStats();
        auto* Peer=Parts[0];const FTransform PeerPose=Peer->GetActorTransform();
        auto* Reconstruction=T.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),PeerPose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!TestNotNull(TEXT("A fresh descriptor reconstruction spawns"),Reconstruction))return false;
        Reconstruction->FoodData=Peer->FoodData;Reconstruction->ItemName=Peer->ItemName;Reconstruction->ItemMesh=Peer->ItemMesh;
        Reconstruction->bFragment=Peer->bFragment;Reconstruction->CutPlanes=Peer->CutPlanes;
        Reconstruction->Settings=Peer->Settings;Reconstruction->Profile=Peer->Profile;Reconstruction->bStagedCutPortion=true;
        Reconstruction->Batch=918415;Reconstruction->FinishSpawning(PeerPose);
        Reconstruction->Body->SetSimulatePhysics(false);Reconstruction->SetActorTickEnabled(false);
        const auto Reconstructed=GetPreparationStats();
        TestEqual(TEXT("A fresh descriptor reconstruction reuses the live sibling's convex"),Reconstructed.ConvexBuilds,BeforeReconstruction.ConvexBuilds);
        TestEqual(TEXT("The fresh reconstruction installs its own collision exactly once"),Reconstructed.CollisionInstalls,BeforeReconstruction.CollisionInstalls+1);
        if(!CheckReplay(Reconstruction))return false;
        Reconstruction->OnRep_Item();Reconstruction->OnRep_Item();
        const auto Renotified=GetPreparationStats();
        TestEqual(TEXT("Repeated reconstruction notification never recooks its convex"),Renotified.ConvexBuilds,Reconstructed.ConvexBuilds);
        TestEqual(TEXT("Repeated reconstruction notification never reinstalls its collision"),Renotified.CollisionInstalls,Reconstructed.CollisionInstalls);
        Reconstruction->Destroy();
        auto* Recut=Parts[0];
        ResetPreparationStats();
        if(!TestTrue(TEXT("An already prepared half supports a second oblique cut"),Recut->HitFood(999,FVector::ForwardVector,T.Hero,IndexedCenter(Recut->CutVisual),FVector(-.21,.71,1).GetSafeNormal())))return false;
        auto Leaves=T.Foods(Source->Batch);Leaves.Remove(Source);Leaves.Remove(Recut);
        if(!TestEqual(TEXT("A recut replaces only its half, keeping its sibling"),Leaves.Num(),3))return false;
        const auto SecondCut=GetPreparationStats();
        TestEqual(TEXT("Recutting a prepared half does not import its complete source again"),SecondCut.RootImports,uint64(0));
        TestTrue(TEXT("Recutting applies only its new planes without replaying the old descriptor in each child"),SecondCut.HistorySlices>0&&SecondCut.HistorySlices<=2);
        TestEqual(TEXT("Only the two new recut children install geometry"),SecondCut.Installations,uint64(2));
        TestEqual(TEXT("Only the two new recut children build convexes"),SecondCut.PhysicsBuilds,uint64(2));
        TestEqual(TEXT("A repeated cut prepares only its two new convexes"),SecondCut.ConvexBuilds,uint64(2));
        TestEqual(TEXT("A repeated cut installs only its two new proxies"),SecondCut.CollisionInstalls,uint64(2));
        for(auto* Part:Leaves)if(!CheckReplay(Part))return false;
        CheckAccounting(Leaves,Mass,Awards);
    }

    // Missing destination rejects a mixed ready/physical split, then the same
    // source can succeed after an intake appears. No fake actor/HP outcome.
    {
        FWorld T;if(!TestNotNull(TEXT("Atomic prepared-cut fixture has a worker"),T.Hero))return false;
        if(!TestNotNull(TEXT("Atomic fixture starts with a real intake"),T.Throat))return false;
        T.Throat->Destroy();T.Throat=nullptr;
        auto MixedRow=Row;MixedRow.Scale=MixedRow.FragmentScale=FVector(4,1,1);
        auto* Source=T.Spawn(MixedRow,TEXT("PreparedAtomic"),918414,true);
        if(!TestNotNull(TEXT("Mixed-ready source spawns without a destination"),Source))return false;
        const float HP=Source->Health,Mass=Source->FoodData.Mass;
        const int32 Awards=Source->FoodData.FragmentCount(),Units=Source->CutAwardUnits;
        const FTransform Pose=Source->GetActorTransform();
        const FVector Point=Pose.TransformPosition(FVector(-150,0,0));
        const FVector Normal=Pose.TransformVectorNoScale(FVector::ForwardVector);
        TestFalse(TEXT("Prepared mixed staging refuses when its ready child's endpoint is absent"),Source->HitFood(999,FVector::ForwardVector,T.Hero,Point,Normal));
        TestTrue(TEXT("Failed prepared staging retains source state, mass and original award ownership"),!Source->IsDisposed()&&Source->Health==HP&&Source->FoodData.Mass==Mass&&Source->CutAwardUnits==Units&&!Source->bDeliveryScored&&!Source->WasAutomaticallyConsumed()&&Source->CutPlanes.IsEmpty());
        const auto AfterFailure=T.Foods(Source->Batch);
        TestTrue(TEXT("No staged render/physics child leaks after an atomic refusal"),AfterFailure.Num()==1&&AfterFailure[0]==Source);
        T.Throat=T.World->SpawnActor<AMCThroat>(FVector(1200,0,2000),FRotator::ZeroRotator);
        if(!TestNotNull(TEXT("A real endpoint becomes available for retry"),T.Throat))return false;
        if(!TestTrue(TEXT("The preserved source accepts the same real knife cut after refusal"),Source->HitFood(999,FVector::ForwardVector,T.Hero,Point,Normal)))return false;
        auto Parts=T.Foods(Source->Batch);Parts.Remove(Source);
        if(!TestEqual(TEXT("The retried closed-box split has exactly two outcomes"),Parts.Num(),2))return false;
        TestEqual(TEXT("Exactly the small prepared cap is accepted once"),T.Throat->FoodSwallowed,1);
        TestEqual(TEXT("The large prepared sibling remains active"),Parts.FilterByPredicate([](const auto* P){return !P->IsDisposed();}).Num(),1);
        CheckAccounting(Parts,Mass,Awards);
    }
    {
        FConvexRequest Solid;
        for(int32 I=0;I<8;++I)Solid.Vertices.Add(FVector(I&1?50.:-50.,I&2?50.:-50.,I&4?50.:-50.));
        FConvexRequest Planar;Planar.Vertices={FVector(-100,-100,0),FVector(100,-100,0),FVector(0,100,0)};
        TArray<TUniquePtr<FPreparedCollision>> Proxies;
        if(!TestTrue(TEXT("A normal closed solid has a prepared convex"),PrepareConvexBatch({Solid},Proxies)))return false;
        if(!TestEqual(TEXT("A successful batch publishes its complete proxy list"),Proxies.Num(),1))return false;
        TestFalse(TEXT("A nonplanar solid does not use the thin-surface fallback"),Proxies[0]->bPlanarProxy);
        // The output already holds a valid result. Failure must clear it as
        // well as any successfully prepared prefix of the next batch.
        TestFalse(TEXT("A batch rejects a planar triangle when no proxy is permitted"),PrepareConvexBatch({Solid,Planar},Proxies));
        TestEqual(TEXT("A planar refusal publishes no partial or stale convex batch"),Proxies.Num(),0);
        FConvexRequest Invalid;Invalid.Vertices={FVector::ZeroVector,FVector::ForwardVector};
        TestFalse(TEXT("A batch rejects an unbuildable physical member"),PrepareConvexBatch({Solid,Invalid},Proxies));
        TestEqual(TEXT("An invalid member publishes no usable prefix for actor staging"),Proxies.Num(),0);
        Planar.bAllowPlanarProxy=true;
        if(!TestTrue(TEXT("The existing automatic thin-surface policy accepts an explicit planar proxy"),PrepareConvexBatch({Solid,Planar},Proxies)))return false;
        if(!TestEqual(TEXT("An accepted mixed collision batch publishes both complete proxies"),Proxies.Num(),2))return false;
        TestTrue(TEXT("Only the thin triangle uses the planar proxy policy"),!Proxies[0]->bPlanarProxy&&Proxies[1]->bPlanarProxy);
        const auto& Thin=*Proxies[1];
        TestTrue(TEXT("A planar collision proxy preserves its surface and nominal 0.1 cm thickness"),
            Thin.Bounds.IsValid&&Thin.Bounds.Min.Equals(FVector(-100,-100,-.05),.001)&&Thin.Bounds.Max.Equals(FVector(100,100,.05),.001));
        for(const auto& Proxy:Proxies) {
            TestTrue(TEXT("Each published proxy carries a valid convex and triangular indices"),Proxy->Convex&&Proxy->Convex->IsValidGeometry()&&!Proxy->HullIndices.IsEmpty()&&Proxy->HullIndices.Num()%3==0);
            for(int32 I:Proxy->HullIndices)if(!TestTrue(TEXT("Prepared hull indices address only its immutable vertices"),Proxy->HullVertices.IsValidIndex(I)))return false;
        }
    }
    return true;
}
#endif
