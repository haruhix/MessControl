#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCInventoryComponent.h"
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
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace
{
struct FFoodPortionWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Hero=nullptr;
    FMCFoodRow Row;
    UStaticMesh* Mesh=nullptr;

    FFoodPortionWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,2000),FRotator::ZeroRotator);
        if(Hero) Hero->GetCharacterMovement()->DisableMovement();
    }

    ~FFoodPortionWorld()
    { World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false); }

    bool LoadFixture(FAutomationTestBase& Test)
    {
        if(!Test.TestNotNull(TEXT("The portion fixture has a player"),Hero)) return false;
        const auto* Menu=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
        if(!Test.TestNotNull(TEXT("The current breakfast menu loads"),Menu)) return false;
        const auto* SavedRow=Menu->FindRow<FMCFoodRow>(TEXT("Fibre"),TEXT("Food portion regression"),false);
        if(!Test.TestNotNull(TEXT("The menu contains the current cabbage row"),SavedRow)) return false;
        Row=*SavedRow;
        for(const auto& Candidate:Row.WholeMeshes) if(!Candidate.IsNull()) {
            Mesh=Candidate.LoadSynchronous();
            if(Mesh) break;
        }
        if(!Test.TestNotNull(TEXT("The cabbage mesh loads"),Mesh)) return false;
#if WITH_EDITOR
        FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
        if(!Test.TestTrue(TEXT("The real cabbage mesh supports runtime CPU slicing"),Mesh->bAllowCPUAccess)) return false;
        Row.WholeMeshes={Mesh};Row.FragmentMeshes={Mesh};
        Row.Kind=EMCFoodKind::Food;Row.Resistance=EMCFoodResistance::Soft;
        Row.Health=100;Row.Mass=6;Row.Fragments=2;Row.SpoilSeconds=300;
        return true;
    }

    AMCFoodActor* Food(FVector FullSize,bool Fragment=false)
    {
        // Keep the authored render triangles and collision data. Only this
        // transient fixture's dimensions change; the saved menu is untouched.
        const FVector NativeSize=Mesh->GetBoundingBox().GetSize();
        FMCFoodRow SizedRow=Row;
        SizedRow.Scale=SizedRow.FragmentScale=FullSize/NativeSize;
        const FTransform Pose(Hero->GetActorLocation()+FVector(-100,0,0));
        auto* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Food) return nullptr;
        FRandomStream Random(7);Food->ConfigureItem(TEXT("Fibre"),SizedRow,Random,Fragment);
        Food->FinishSpawning(Pose);
        Food->Settings.MaxHandDimensions=FVector(110);
        Food->Settings.MinCutVolume=1000;
        Food->Settings.MinCutThickness=12;
        Food->Phase=EMCFoodPhase::Free;
        Food->Body->SetSimulatePhysics(false);Food->SetActorTickEnabled(false);
        return Food;
    }

    TArray<AMCFoodActor*> LiveFood() const
    {
        TArray<AMCFoodActor*> Result;
        for(TActorIterator<AMCFoodActor> It(World);It;++It)
            if(!It->IsDisposed() && !It->IsActorBeingDestroyed()) Result.Add(*It);
        return Result;
    }

    static FBox CutBounds(const AMCFoodActor* Food)
    {
        FBox Result(ForceInit);
        if(Food->CutVisual) for(int32 I=0;I<Food->CutVisual->GetNumSections();++I)
            if(const auto* Section=Food->CutVisual->GetProcMeshSection(I))
                for(uint32 Index:Section->ProcIndexBuffer) if(Section->ProcVertexBuffer.IsValidIndex(Index))
                    Result+=Food->CutVisual->GetComponentTransform().TransformPosition(Section->ProcVertexBuffer[Index].Position);
        return Result;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodOversizedPortion,"MessControl.Food.Portions.OversizedFragmentsCannotCollect",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodOversizedPortion::RunTest(const FString&)
{
    FFoodPortionWorld T;if(!T.LoadFixture(*this)) return false;
    auto* Whole=T.Food(FVector(300,160,140));
    if(!TestNotNull(TEXT("The oversized whole ingredient spawns"),Whole)) return false;
    TestFalse(TEXT("The original ingredient does not fit a hand"),Whole->FitsHandPortion());
    const FVector AuthoredScale=Whole->Visual->GetRelativeScale3D();
    Whole->Visual->SetRelativeScale3D(AuthoredScale*.1);
    TestFalse(TEXT("Temporary hit squash cannot make an oversized ingredient fit"),Whole->FitsHandPortion());
    Whole->Visual->SetRelativeScale3D(AuthoredScale);
    if(!TestTrue(TEXT("A central real slice succeeds"),Whole->HitFood(999,FVector::RightVector,T.Hero,Whole->Visual->Bounds.Origin,FVector::ForwardVector))) return false;
    const auto Pieces=T.LiveFood();
    if(!TestEqual(TEXT("The slice produces exactly two live halves"),Pieces.Num(),2)) return false;
    TestTrue(TEXT("The original is replaced only after a successful cut"),Whole->IsDisposed());
    TestEqual(TEXT("A successful whole cut preserves the tutorial's broken-food contract"),Whole->Health,0.f);
    for(auto* Piece:Pieces) {
        TestTrue(TEXT("Each half is actual sliced geometry"),Piece->bFragment && !Piece->CutPlanes.IsEmpty() && Piece->CutVisual);
        TestFalse(TEXT("Cutting in half does not bypass the hand size limit"),Piece->FitsHandPortion());
        TestFalse(TEXT("Oversized generated halves cannot be collected"),T.Hero->FoodCollection->CanCollect(Piece));
    }
    auto* LegacyFragment=T.Food(FVector(180,150,130),true);
    if(!TestNotNull(TEXT("An authored fragment spawns"),LegacyFragment)) return false;
    TestFalse(TEXT("The same limit applies to authored fragments"),LegacyFragment->FitsHandPortion());
    TestFalse(TEXT("The fragment flag alone cannot grant pickup"),T.Hero->FoodCollection->CanCollect(LegacyFragment));
    LegacyFragment->FoodData.Resistance=EMCFoodResistance::Hard;
    T.Hero->Inventory->Selected=EMCToolSlot::Knife;
    TestFalse(TEXT("A broken ordinary hard ingredient exposes knife-cuttable flesh"),LegacyFragment->IsHardFood());
    TestTrue(TEXT("The normal knife can target an oversized hard ingredient's fragment"),T.Hero->Inventory->CanBreak(LegacyFragment));
    TestTrue(TEXT("An oversized authored fragment can be sliced again"),LegacyFragment->HitFood(999,FVector::RightVector,T.Hero,
        LegacyFragment->Visual->Bounds.Origin,FVector::ForwardVector));
    TestTrue(TEXT("Only an accepted recut replaces the authored fragment"),LegacyFragment->IsDisposed());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodOffCenterPortion,"MessControl.Food.Portions.OffCenterCutUsesRemainingMesh",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodOffCenterPortion::RunTest(const FString&)
{
    FFoodPortionWorld T;if(!T.LoadFixture(*this)) return false;
    auto* Whole=T.Food(FVector(300,90,80));
    if(!TestNotNull(TEXT("The long ingredient spawns"),Whole)) return false;
    const FBox Bounds=Whole->Visual->Bounds.GetBox();
    const FVector Point(Bounds.Max.X-60,Bounds.GetCenter().Y,Bounds.GetCenter().Z);
    if(!TestTrue(TEXT("An off-center real slice succeeds"),Whole->HitFood(999,FVector::RightVector,T.Hero,Point,FVector::ForwardVector))) return false;
    const auto Pieces=T.LiveFood();
    if(!TestEqual(TEXT("The off-center slice preserves both pieces"),Pieces.Num(),2)) return false;
    AMCFoodActor* Small=nullptr;AMCFoodActor* Large=nullptr;
    for(auto* Piece:Pieces) {
        if(FFoodPortionWorld::CutBounds(Piece).GetSize().GetMax()<=110) Small=Piece;
        else Large=Piece;
    }
    if(!TestNotNull(TEXT("The geometry contains a hand-sized outer cap"),Small)
        || !TestNotNull(TEXT("The geometry also retains an oversized remainder"),Large)) return false;
    TestTrue(TEXT("The cap retains an off-center collision origin, exercising the old symmetric-bounds trap"),Small->Body->GetScaledBoxExtent().GetMax()*2>110);
    TestTrue(TEXT("The cap reports its actual remaining dimensions"),Small->GetHandPortionSize().GetMax()<=110);
    TestTrue(TEXT("The small outer cap fits the hand"),Small->FitsHandPortion());
    TestTrue(TEXT("The small cap is collectible from its exposed side"),T.Hero->FoodCollection->CanCollect(Small));
    TestFalse(TEXT("The large remainder still needs cutting"),Large->FitsHandPortion());
    TestFalse(TEXT("The large remainder cannot be collected"),T.Hero->FoodCollection->CanCollect(Large));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodMinimumPortion,"MessControl.Food.Portions.MinimumCutIsTransactional",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodMinimumPortion::RunTest(const FString&)
{
    for(bool VolumeOnly:{false,true}) {
        FFoodPortionWorld T;if(!T.LoadFixture(*this)) return false;
        auto* Whole=T.Food(FVector(200,160,140));
        if(!TestNotNull(TEXT("The minimum-cut fixture spawns"),Whole)) return false;
        if(VolumeOnly) Whole->Settings.MinCutVolume=1.e9f;
        const FBox Bounds=Whole->Visual->Bounds.GetBox();
        const FVector Point=VolumeOnly?Bounds.GetCenter():FVector(Bounds.Max.X-6,Bounds.GetCenter().Y,Bounds.GetCenter().Z);
        const float Health=Whole->Health,Mass=Whole->FoodData.Mass;
        const auto Phase=Whole->Phase;
        TestFalse(VolumeOnly?TEXT("A cut below the minimum child volume is rejected"):TEXT("A six-centimetre grazing slice is rejected"),
            Whole->HitFood(999,FVector::RightVector,T.Hero,Point,FVector::ForwardVector));
        TestEqual(TEXT("Rejected slicing cannot damage the parent"),Whole->Health,Health);
        TestEqual(TEXT("Rejected slicing preserves the parent's mass"),Whole->FoodData.Mass,Mass);
        TestEqual(TEXT("Rejected slicing preserves the parent's phase"),Whole->Phase,Phase);
        TestFalse(TEXT("Rejected slicing cannot dispose the parent"),Whole->IsDisposed());
        TestTrue(TEXT("Rejected slicing does not turn the parent into a fragment"),!Whole->bFragment && Whole->CutPlanes.IsEmpty());
        const auto Remaining=T.LiveFood();
        TestTrue(TEXT("No cut candidate or fallback fragments survive a rejected cut"),Remaining.Num()==1 && Remaining[0]==Whole);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodReadyPortion,"MessControl.Food.Portions.BalancedCutConservesMassAndStopsAtReadySize",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodReadyPortion::RunTest(const FString&)
{
    FFoodPortionWorld T;if(!T.LoadFixture(*this)) return false;
    auto* Whole=T.Food(FVector(200,100,80));
    if(!TestNotNull(TEXT("The balanced-cut fixture spawns"),Whole)) return false;
    const float Mass=Whole->FoodData.Mass,BodyMass=Whole->Body->GetMass();
    const FBox OriginalBounds=Whole->Visual->Bounds.GetBox();
    if(!TestTrue(TEXT("A balanced cut of an oversized ingredient succeeds"),Whole->HitFood(999,FVector::RightVector,T.Hero,OriginalBounds.GetCenter(),FVector::ForwardVector))) return false;
    const auto Pieces=T.LiveFood();
    if(!TestEqual(TEXT("A balanced cut yields two live portions"),Pieces.Num(),2)) return false;
    TestTrue(TEXT("A successful cut replaces the original ingredient"),Whole->IsDisposed());
    float TotalMass=0,TotalBodyMass=0;FBox RemainingBounds(ForceInit);
    for(auto* Piece:Pieces) {
        TestTrue(TEXT("Each child has generated render geometry"),Piece->CutVisual && Piece->CutVisual->GetNumSections()>0 && Piece->CutPlanes.Num()==1);
        TotalMass+=Piece->FoodData.Mass;TotalBodyMass+=Piece->Body->GetMass();
        RemainingBounds+=FFoodPortionWorld::CutBounds(Piece);
        TestTrue(TEXT("Each half is now small enough for the hand"),Piece->FitsHandPortion());
        Piece->Body->SetSimulatePhysics(false);Piece->SetActorTickEnabled(false);
        const float Health=Piece->Health;
        TestFalse(TEXT("A ready portion refuses further real slicing"),Piece->HitFood(999,FVector::RightVector,T.Hero,Piece->CutVisual->Bounds.Origin,FVector::RightVector));
        TestEqual(TEXT("Refused cutting preserves the ready portion's health"),Piece->Health,Health);
        TestFalse(TEXT("Refused cutting leaves the ready portion alive"),Piece->IsDisposed());
        TestEqual(TEXT("Refused cutting leaves its existing geometry intact"),Piece->CutPlanes.Num(),1);
    }
    TestTrue(TEXT("The children's geometry retains the original outer bounds"),RemainingBounds.Min.Equals(OriginalBounds.Min,.1) && RemainingBounds.Max.Equals(OriginalBounds.Max,.1));
    TestTrue(TEXT("Cutting conserves food mass"),FMath::IsNearlyEqual(TotalMass,Mass,.001f));
    TestTrue(TEXT("Cutting conserves simulated body mass"),FMath::IsNearlyEqual(TotalBodyMass,BodyMass,.01f));
    TestEqual(TEXT("Ready portions cannot be subdivided into extra live actors"),T.LiveFood().Num(),2);
    return true;
}
#endif
