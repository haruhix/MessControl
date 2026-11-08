#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodCutGeometry.h"
#include "MCFoodActor.h"
#include "MCThroat.h"
#include "MCToothCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/Material.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCFoodIslandTests
{
using MCFoodCutGeometry::FIsland;

TStrongObjectPtr<UProceduralMeshComponent> Mesh()
{
    return TStrongObjectPtr<UProceduralMeshComponent>(NewObject<UProceduralMeshComponent>(GetTransientPackage(),NAME_None,RF_Transient));
}

void Box(UProceduralMeshComponent* Target,int32 Section,FVector Center,FVector Extent,bool UnreferencedVertex=false)
{
    TArray<FVector> Vertices,Normals;TArray<int32> Indices;TArray<FVector2D> UV;TArray<FProcMeshTangent> Tangents;
    UKismetProceduralMeshLibrary::GenerateBoxMesh(Extent,Vertices,Indices,Normals,UV,Tangents);
    for(auto& P:Vertices) P+=Center;
    if(UnreferencedVertex) {
        Vertices.Add(FVector(100000,100000,100000));Normals.Add(FVector::UpVector);
        UV.Add(FVector2D(99,101));Tangents.Add(FProcMeshTangent(FVector::ForwardVector,false));
    }
    TArray<FLinearColor> Colors;Colors.Init(FLinearColor(.1f,.4f,.7f,1),Vertices.Num());
    UV[0]=FVector2D(.125,.875);Colors[0]=FLinearColor(.25f,.5f,.75f,1);
    Target->CreateMeshSection_LinearColor(Section,Vertices,Indices,Normals,UV,Colors,Tangents,false,false);
}

void Copy(const UProceduralMeshComponent* Source,UProceduralMeshComponent* Target)
{
    for(int32 Section=0;Section<Source->GetNumSections();++Section) {
        // GetProcMeshSection is non-const in the plugin API, although this copy
        // does not modify the source section or its referenced attributes.
        const auto* Data=const_cast<UProceduralMeshComponent*>(Source)->GetProcMeshSection(Section);
        if(Data) Target->SetProcMeshSection(Section,*Data);
        Target->SetMaterial(Section,Source->GetMaterial(Section));
    }
}

int32 Triangles(UProceduralMeshComponent* Target)
{
    if(!Target) return 0;
    int32 Count=0;
    for(int32 Section=0;Section<Target->GetNumSections();++Section)
        if(const auto* Data=Target->GetProcMeshSection(Section)) Count+=Data->ProcIndexBuffer.Num()/3;
    return Count;
}

void Triangle(UProceduralMeshComponent* Target,int32 Section,FVector A,FVector B,FVector C)
{
    const FVector Normal=FVector::CrossProduct(B-A,C-A).GetSafeNormal();
    TArray<FVector> Positions={A,B,C},Normals={Normal,Normal,Normal};
    TArray<int32> Indices={0,1,2};TArray<FVector2D> UV={FVector2D(0,0),FVector2D(1,0),FVector2D(0,1)};
    TArray<FLinearColor> Colors={FLinearColor::White,FLinearColor::White,FLinearColor::White};
    TArray<FProcMeshTangent> Tangents;Tangents.Init(FProcMeshTangent((B-A).GetSafeNormal(),false),3);
    Target->CreateMeshSection_LinearColor(Section,Positions,Indices,Normals,UV,Colors,Tangents,false,false);
}

struct FActorWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Hero=nullptr;
    AMCThroat* Throat=nullptr;
    static constexpr int32 Batch=918191;
    FActorWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,2000),FRotator::ZeroRotator);
        if(Hero) Hero->GetCharacterMovement()->DisableMovement();
        Throat=World->SpawnActor<AMCThroat>(FVector(1000,0,2000),FRotator::ZeroRotator);
    }
    ~FActorWorld()
    {
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    }
    TArray<AMCFoodActor*> Foods() const
    {
        TArray<AMCFoodActor*> Result;
        for(TActorIterator<AMCFoodActor> It(World);It;++It)
            if(It->Batch==Batch && !It->IsActorBeingDestroyed()) Result.Add(*It);
        return Result;
    }
};

bool MixedSavedFixture(FAutomationTestBase& Test,FMCFoodRow& Row,UStaticMesh*& SourceMesh,int32& ExpectedSmall,int32& ExpectedLarge)
{
    const auto* Menu=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
    if(!Test.TestNotNull(TEXT("The current saved menu supplies the actor fixture"),Menu)) return false;
    auto Names=Menu->GetRowNames();Names.Sort([](FName A,FName B){return A.LexicalLess(B);});
    for(FName Name:Names) {
        const auto* Saved=Menu->FindRow<FMCFoodRow>(Name,TEXT("Food island regression"),false);
        if(!Saved || Saved->Kind!=EMCFoodKind::Food) continue;
        TArray<TSoftObjectPtr<UStaticMesh>> Candidates=Saved->WholeMeshes;Candidates.Append(Saved->FragmentMeshes);
        for(const auto& Ref:Candidates) {
            const FString Asset=Ref.ToString();
            // These existing food families contain the motivating disconnected
            // artwork. No asset is resized or modified on disk by this test.
            if(!Asset.Contains(TEXT("Strawberry")) && !Asset.Contains(TEXT("Grapes")) && !Asset.Contains(TEXT("Grape_B"))) continue;
            auto* Candidate=Ref.LoadSynchronous();
            if(!Candidate || !Candidate->bAllowCPUAccess) continue;
#if WITH_EDITOR
            FStaticMeshCompilingManager::Get().FinishCompilation({Candidate});
#endif
            auto Geometry=Mesh();
            for(int32 S=0;S<Candidate->GetNumSections(0);++S) {
                TArray<FVector> Vertices,Normals;TArray<int32> Indices;TArray<FVector2D> UV;TArray<FProcMeshTangent> Tangents;TArray<FLinearColor> Colors;
                UKismetProceduralMeshLibrary::GetSectionFromStaticMesh(Candidate,0,S,Vertices,Indices,Normals,UV,Tangents);
                Geometry->CreateMeshSection_LinearColor(S,Vertices,Indices,Normals,UV,Colors,Tangents,false);
            }
            TArray<FIsland> Islands;FString Error;
            if(!MCFoodCutGeometry::FindIslands(Geometry.Get(),Islands,&Error) || Islands.Num()<2) continue;
            double LargestClosed=0;
            for(const auto& Island:Islands) if(Island.Bounds.GetSize().GetMin()>.001)
                LargestClosed=FMath::Max(LargestClosed,Island.Bounds.GetSize().GetMax());
            if(LargestClosed<=0) continue;
            const double Scale=330./LargestClosed;
            int32 Small=0,Large=0;bool InvalidLarge=false;
            for(const auto& Island:Islands) {
                const FVector Size=Island.Bounds.GetSize()*Scale;
                if(Size.GetMax()<=220.+KINDA_SMALL_NUMBER) ++Small;
                else {++Large;InvalidLarge|=Size.GetMin()<=.001;}
            }
            if(!Small || !Large || InvalidLarge) continue;
            Row=*Saved;Row.WholeMeshes={Candidate};Row.FragmentMeshes={Candidate};
            // A native-only diagnostic scale creates a mixed readiness boundary;
            // this is not a visual preview or a claim about saved balancing.
            Row.Scale=Row.FragmentScale=FVector(Scale);Row.Health=25;Row.Mass=12;Row.Fragments=7;
            Row.Resistance=EMCFoodResistance::Soft;Row.SpoilSeconds=300;
            SourceMesh=Candidate;ExpectedSmall=Small;ExpectedLarge=Large;
            Test.AddInfo(FString::Printf(TEXT("Mixed island fixture %s: %d ready / %d large, diagnostic uniform scale %.6f; saved collision data retained"),*Candidate->GetPathName(),Small,Large,Scale));
            return true;
        }
    }
    Test.AddError(TEXT("No current CPU strawberry/grape mesh provides both a connected large body and a smaller disconnected island; inspect fixture assets rather than faking a result."));
    return false;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodIslandBoundsSelection,"MessControl.Food.Islands.DisconnectedBoundsAndReplaySelection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodIslandBoundsSelection::RunTest(const FString&)
{
    using namespace MCFoodIslandTests;
    auto Source=Mesh();auto Selected=Mesh();auto Replay=Mesh();
    TStrongObjectPtr<UMaterial> Skin(NewObject<UMaterial>(GetTransientPackage(),NAME_None,RF_Transient));
    TStrongObjectPtr<UMaterial> Pulp(NewObject<UMaterial>(GetTransientPackage(),NAME_None,RF_Transient));
    Box(Source.Get(),0,FVector::ZeroVector,FVector(180,80,65));
    Box(Source.Get(),1,FVector(460,0,0),FVector(15,12,10),true);
    Source->SetMaterial(0,Skin.Get());Source->SetMaterial(1,Pulp.Get());
    const auto InitialIndices=Source->GetProcMeshSection(1)->ProcIndexBuffer;
    const auto OriginalVertex=Source->GetProcMeshSection(1)->ProcVertexBuffer[0];
    const int32 OriginalVertices=Source->GetProcMeshSection(1)->ProcVertexBuffer.Num();
    TArray<FIsland> Islands,Again;FString Error;
    if(!TestTrue(TEXT("Disconnected closed geometry is classified"),MCFoodCutGeometry::FindIslands(Source.Get(),Islands,&Error))) {
        AddError(Error);return false;
    }
    if(!TestEqual(TEXT("One large body and one small body are independent islands"),Islands.Num(),2)) return false;
    TestEqual(TEXT("The first surviving triangle defines deterministic island zero"),Islands[0].Index,0);
    TestEqual(TEXT("The later section defines deterministic island one"),Islands[1].Index,1);
    TestTrue(TEXT("The large body's bounds exclude the small body"),Islands[0].Bounds.Equals(FBox(FVector(-180,-80,-65),FVector(180,80,65)),.001));
    TestTrue(TEXT("The small body's bounds exclude both the large body and unused remote vertices"),Islands[1].Bounds.Equals(FBox(FVector(445,-12,-10),FVector(475,12,10)),.001));
    TestEqual(TEXT("Each closed box retains twelve referenced triangles"),Islands[0].TriangleCount,12);
    TestEqual(TEXT("The small box retains twelve referenced triangles"),Islands[1].TriangleCount,12);
    TestTrue(TEXT("Island discovery is repeatable"),MCFoodCutGeometry::FindIslands(Source.Get(),Again,&Error));
    if(!TestEqual(TEXT("Repeated discovery retains the same island count"),Again.Num(),Islands.Num())) return false;
    for(int32 I=0;I<Islands.Num();++I) {
        TestEqual(TEXT("Repeated discovery retains each selector"),Again[I].Index,Islands[I].Index);
        TestTrue(TEXT("Repeated discovery retains each independent bound"),Again[I].Bounds.Equals(Islands[I].Bounds,.001));
    }
    TestTrue(TEXT("Discovery does not change triangle indices"),Source->GetProcMeshSection(1)->ProcIndexBuffer==InitialIndices);
    TestEqual(TEXT("Discovery does not compact or alter the source"),Source->GetProcMeshSection(1)->ProcVertexBuffer.Num(),OriginalVertices);
    Copy(Source.Get(),Selected.Get());Copy(Source.Get(),Replay.Get());
    if(!TestTrue(TEXT("The small selector filters the first reconstruction"),MCFoodCutGeometry::FilterIsland(Selected.Get(),Islands[1].Index,&Error))) {AddError(Error);return false;}
    if(!TestTrue(TEXT("The same selector filters an independent reconstruction"),MCFoodCutGeometry::FilterIsland(Replay.Get(),Islands[1].Index,&Error))) {AddError(Error);return false;}
    TArray<FIsland> Kept,Replayed;
    TestTrue(TEXT("Filtered geometry can be classified again"),MCFoodCutGeometry::FindIslands(Selected.Get(),Kept,&Error));
    TestTrue(TEXT("Independent replay can be classified again"),MCFoodCutGeometry::FindIslands(Replay.Get(),Replayed,&Error));
    if(!TestEqual(TEXT("Only one selected island remains"),Kept.Num(),1) || !TestEqual(TEXT("Replay leaves one selected island"),Replayed.Num(),1)) return false;
    TestTrue(TEXT("Filtering retains only the small piece's exact bound"),Kept[0].Bounds.Equals(Islands[1].Bounds,.001));
    TestTrue(TEXT("Replay produces the same bound"),Replayed[0].Bounds.Equals(Kept[0].Bounds,.001));
    TestEqual(TEXT("Filtering preserves the small body's complete topology"),Triangles(Selected.Get()),12);
    TestTrue(TEXT("The surviving material stays at its original section index"),Selected->GetMaterial(1)==Pulp.Get());
    const auto* Data=Selected->GetProcMeshSection(1);
    if(!TestNotNull(TEXT("The surviving section remains available"),Data)) return false;
    TestEqual(TEXT("Filtering compacts the unused remote vertex"),Data->ProcVertexBuffer.Num(),24);
    const auto* Preserved=Data->ProcVertexBuffer.FindByPredicate([&](const auto& V){return V.Position.Equals(OriginalVertex.Position,.001) && V.UV0.Equals(OriginalVertex.UV0,.000001);});
    if(TestNotNull(TEXT("The source UV seam vertex is retained"),Preserved)) {
        TestTrue(TEXT("Vertex color survives extraction"),Preserved->Color==OriginalVertex.Color);
        TestTrue(TEXT("Vertex normal survives extraction"),Preserved->Normal.Equals(OriginalVertex.Normal,.000001));
        TestTrue(TEXT("Vertex tangent survives extraction"),Preserved->Tangent.TangentX.Equals(OriginalVertex.Tangent.TangentX,.000001)
            && Preserved->Tangent.bFlipTangentY==OriginalVertex.Tangent.bFlipTangentY);
    }
    const int32 BeforeInvalid=Triangles(Replay.Get());
    TestFalse(TEXT("An invalid replay selector is rejected"),MCFoodCutGeometry::FilterIsland(Replay.Get(),99,&Error));
    TestEqual(TEXT("An invalid selector cannot silently erase the piece"),Triangles(Replay.Get()),BeforeInvalid);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodIslandSeams,"MessControl.Food.Islands.MaterialAndUVSeamsRemainConnected",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodIslandSeams::RunTest(const FString&)
{
    using namespace MCFoodIslandTests;
    auto Source=Mesh();
    TArray<FVector> Vertices,Normals;TArray<int32> Indices;TArray<FVector2D> UV;TArray<FProcMeshTangent> Tangents;
    UKismetProceduralMeshLibrary::GenerateBoxMesh(FVector(50,40,30),Vertices,Indices,Normals,UV,Tangents);
    TArray<int32> Sections[2];
    for(int32 I=0;I<Indices.Num();I+=3) {
        const int32 Section=(I/6)%2;
        Sections[Section].Append({Indices[I],Indices[I+1],Indices[I+2]});
    }
    for(int32 S=0;S<2;++S) {
        TArray<FVector2D> DifferentUV=UV;
        for(auto& V:DifferentUV) V+=FVector2D(S*.25,S*.125);
        TArray<FLinearColor> Colors;Colors.Init(S?FLinearColor::Red:FLinearColor::Blue,Vertices.Num());
        Source->CreateMeshSection_LinearColor(S,Vertices,Sections[S],Normals,DifferentUV,Colors,Tangents,false,false);
    }
    // GenerateBoxMesh has duplicated vertices at every hard normal/UV seam;
    // alternating complete faces also crosses the material-section boundary.
    TArray<FIsland> Islands;FString Error;
    if(!TestTrue(TEXT("The seamed closed body is classified"),MCFoodCutGeometry::FindIslands(Source.Get(),Islands,&Error))) {AddError(Error);return false;}
    TestEqual(TEXT("UV, hard normals and separate material faces do not detach one body"),Islands.Num(),1);
    if(Islands.Num()==1) {
        TestEqual(TEXT("All twelve body triangles stay connected"),Islands[0].TriangleCount,12);
        TestTrue(TEXT("The combined bound contains only the original box"),Islands[0].Bounds.Equals(FBox(FVector(-50,-40,-30),FVector(50,40,30)),.001));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodIslandCapContact,"MessControl.Food.Islands.CapTJunctionAndPointContact",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodIslandCapContact::RunTest(const FString&)
{
    using namespace MCFoodIslandTests;
    auto Source=Mesh();
    Triangle(Source.Get(),0,FVector(0,0,0),FVector(100,0,0),FVector(0,100,0));
    Triangle(Source.Get(),1,FVector(0,0,0),FVector(50,0,0),FVector(0,-100,0));
    Triangle(Source.Get(),2,FVector(0,0,0),FVector(-50,0,0),FVector(0,0,-50));
    TArray<FIsland> Islands;FString Error;
    if(!TestTrue(TEXT("Partially shared cap edges are classified"),MCFoodCutGeometry::FindIslands(Source.Get(),Islands,&Error))) {AddError(Error);return false;}
    if(!TestEqual(TEXT("A shared edge joins two faces while a point-only contact stays independent"),Islands.Num(),2)) return false;
    TestEqual(TEXT("The T-junction keeps both touching edge faces together"),Islands[0].TriangleCount,2);
    TestEqual(TEXT("A point-touching ornamental face remains its own island"),Islands[1].TriangleCount,1);
    TestTrue(TEXT("The point-only island has its own bounds"),Islands[1].Bounds.Equals(FBox(FVector(-50,0,-50),FVector(0,0,0)),.001));
    auto Jittered=Mesh();
    Triangle(Jittered.Get(),0,FVector::ZeroVector,FVector(.01,.0005,0),FVector(0,-100,0));
    Triangle(Jittered.Get(),1,FVector::ZeroVector,FVector(100,0,0),FVector(0,100,0));
    TArray<FIsland> Joined;
    TestTrue(TEXT("A short cap seam within weld tolerance is classified"),MCFoodCutGeometry::FindIslands(Jittered.Get(),Joined,&Error));
    TestEqual(TEXT("Sub-millimetre endpoint jitter does not detach a cap edge"),Joined.Num(),1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodIslandActorAccounting,"MessControl.Food.Islands.ActorAccountingIndependentIntakeAndReconstruction",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodIslandActorAccounting::RunTest(const FString&)
{
    using namespace MCFoodIslandTests;
    FMCFoodRow Row;UStaticMesh* StaticMesh=nullptr;int32 ExpectedSmall=0,ExpectedLarge=0;
    if(!MixedSavedFixture(*this,Row,StaticMesh,ExpectedSmall,ExpectedLarge)) return false;
    TStrongObjectPtr<UStaticMesh> RetainMesh(StaticMesh);
    FActorWorld T;
    if(!TestNotNull(TEXT("The actor test has a worker"),T.Hero) || !TestNotNull(TEXT("The actor test has a real intake"),T.Throat)) return false;
    const FTransform Pose(FVector(0,0,2000));
    auto* Parent=T.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!TestNotNull(TEXT("The ordinary food actor spawns"),Parent)) return false;
    FRandomStream Random(918191);Parent->ConfigureItem(TEXT("IslandRegression"),Row,Random);
    Parent->Batch=FActorWorld::Batch;Parent->FinishSpawning(Pose);
    Parent->Settings.bAutoConsumeSmallCuts=true;Parent->Settings.MaxAutoConsumeDimensions=FVector(220);
    Parent->Settings.MinCutVolume=1;Parent->Settings.MinCutThickness=1;
    Parent->Phase=EMCFoodPhase::Free;Parent->Body->SetSimulatePhysics(false);Parent->SetActorTickEnabled(false);
    const float OriginalMass=Parent->FoodData.Mass;const int32 OriginalAwards=Parent->FoodData.FragmentCount();
    if(!TestTrue(TEXT("One accepted production knife contact separates disconnected geometry"),Parent->HitFood(1,FVector::ForwardVector,T.Hero,Parent->Visual->Bounds.Origin,FVector::RightVector))) return false;
    TestTrue(TEXT("The grouped parent is retired without its own delivery receipt"),Parent->IsDisposed() && !Parent->WasAutomaticallyConsumed() && !Parent->bDeliveryScored);
    TArray<AMCFoodActor*> Leaves=T.Foods();Leaves.Remove(Parent);
    if(!TestEqual(TEXT("Every referenced component has an independent actor or consumed receipt"),Leaves.Num(),ExpectedSmall+ExpectedLarge)) return false;
    float Mass=0;int32 Awards=0,Consumed=0,Physical=0;AMCFoodActor* Recut=nullptr;
    for(auto* Part:Leaves) {
        Mass+=Part->FoodData.Mass;Awards+=Part->CutAwardUnits;
        TestTrue(TEXT("Every component retains deterministic selection history"),Part->CutPlanes.ContainsByPredicate([](const auto& Step){return !Step.bEnabled && Step.IslandIndex>=0;}));
        if(Part->FitsAutomaticIntakePortion()) {
            ++Consumed;TestTrue(TEXT("Each ready island completes independently"),Part->IsDisposed() && Part->WasAutomaticallyConsumed());
            TestFalse(TEXT("Accepted tiny islands do not create physical network actors"),Part->GetIsReplicated());
            TestTrue(TEXT("Accepted tiny islands have no collision or live rigid body"),Part->Body->GetCollisionEnabled()==ECollisionEnabled::NoCollision && !Part->Body->IsSimulatingPhysics());
        } else {
            ++Physical;Recut=Part;TestFalse(TEXT("A large island remains active"),Part->IsDisposed());
            Part->Body->SetSimulatePhysics(false);Part->SetActorTickEnabled(false);
        }
    }
    TestEqual(TEXT("All small components finish without waiting for a large neighbour"),Consumed,ExpectedSmall);
    TestEqual(TEXT("Only large components remain as gameplay food"),Physical,ExpectedLarge);
    TestTrue(TEXT("Separating islands conserves source mass, including sub-kilogram pieces"),FMath::IsNearlyEqual(Mass,OriginalMass,.001f));
    TestEqual(TEXT("Separating islands cannot create extra delivery award units"),Awards,OriginalAwards);
    TestEqual(TEXT("The real throat records each accepted small island once"),T.Throat->FoodSwallowed,ExpectedSmall);
    if(!TestNotNull(TEXT("A large independent component is available for reconstruction"),Recut)) return false;
    if(!TestNotNull(TEXT("The large component retains its actual selected render mesh"),Recut->CutVisual.Get())) return false;
    const FVector BeforeSize=Recut->GetHandPortionSize();
    const int32 BeforeTriangles=Triangles(Recut->CutVisual);
    const FTransform RecutPose=Recut->GetActorTransform();
    auto* Reconstructed=T.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),RecutPose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!TestNotNull(TEXT("An independent proxy-like reconstruction spawns"),Reconstructed)) return false;
    Reconstructed->FoodData=Recut->FoodData;Reconstructed->ItemName=Recut->ItemName;Reconstructed->ItemMesh=Recut->ItemMesh;
    Reconstructed->bFragment=Recut->bFragment;Reconstructed->CutPlanes=Recut->CutPlanes;
    Reconstructed->FinishSpawning(RecutPose);Reconstructed->Body->SetSimulatePhysics(false);Reconstructed->SetActorTickEnabled(false);
    TestTrue(TEXT("Saved island selectors rebuild the same independent bounds"),Reconstructed->GetHandPortionSize().Equals(BeforeSize,.01));
    TestEqual(TEXT("Saved island selectors rebuild the same triangle count"),Triangles(Reconstructed->CutVisual),BeforeTriangles);
    const auto ValidHistory=Reconstructed->CutPlanes;
    Reconstructed->CutPlanes.Last().IslandIndex=MAX_int32;
    TestFalse(TEXT("An invalid island history fails closed"),Reconstructed->BuildCutVisual());
    TestTrue(TEXT("Invalid history cannot expose the full original food or its collider"),Reconstructed->IsHidden() && Reconstructed->Body->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
    Reconstructed->CutPlanes=ValidHistory;
    TestTrue(TEXT("A later valid descriptor restores its selected geometry"),Reconstructed->BuildCutVisual());
    TestTrue(TEXT("Valid reconstruction restores visibility and query collision"),!Reconstructed->IsHidden() && Reconstructed->Body->GetCollisionEnabled()!=ECollisionEnabled::NoCollision);
    Reconstructed->Destroy();

    const auto BeforeActors=T.Foods();
    const FBox LocalBounds=Recut->CutStackLocalBounds;
    const FVector Size=LocalBounds.GetSize();
    const FVector Axis=Size.X>=Size.Y && Size.X>=Size.Z?FVector::ForwardVector:Size.Y>=Size.Z?FVector::RightVector:FVector::UpVector;
    const FVector Point=RecutPose.TransformPosition(LocalBounds.GetCenter());
    const FVector Normal=RecutPose.TransformVectorNoScale(Axis);
    if(!TestTrue(TEXT("A selected large component can be cut again through its preserved history"),Recut->HitFood(999,FVector::ForwardVector,T.Hero,Point,Normal))) return false;
    Leaves.Remove(Recut);
    for(auto* Part:T.Foods()) if(!BeforeActors.Contains(Part)) Leaves.Add(Part);
    Mass=0;Awards=0;
    const int32 AcceptedBeforeRepeat=T.Throat->FoodSwallowed;
    for(auto* Part:Leaves) {
        Mass+=Part->FoodData.Mass;Awards+=Part->CutAwardUnits;
        if(Part->WasAutomaticallyConsumed())
            TestFalse(TEXT("A repeated request cannot deliver an island twice"),Part->TryConsumeCutPortion(FVector::ForwardVector));
    }
    TestTrue(TEXT("A re-cut conserves the entire live-or-consumed leaf mass"),FMath::IsNearlyEqual(Mass,OriginalMass,.001f));
    TestEqual(TEXT("A re-cut preserves the original aggregate award budget"),Awards,OriginalAwards);
    TestEqual(TEXT("Repeated intake requests cannot duplicate the throat count"),T.Throat->FoodSwallowed,AcceptedBeforeRepeat);
    return true;
}
#endif
