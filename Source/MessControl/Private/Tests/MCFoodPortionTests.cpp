#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodActor.h"
#include "MCFoodCutGeometry.h"
#include "MCFoodCollectionComponent.h"
#include "MCInventoryComponent.h"
#include "MCToothCharacter.h"
#include "MCThroat.h"
#include "MCGameState.h"
#include "MCVFXPresentationComponent.h"
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
#include "UObject/StrongObjectPtr.h"
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
        // These existing geometry/carry checks exercise the retained legacy
        // mode explicitly. Automatic intake has its own cases below.
        Food->Settings.bAutoConsumeSmallCuts=false;
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

    bool SourceVolume(FAutomationTestBase& Test,const AMCFoodActor* Food,double& Out) const
    {
        TStrongObjectPtr<UProceduralMeshComponent> Geometry(NewObject<UProceduralMeshComponent>(GetTransientPackage(),NAME_None,RF_Transient));
        for(int32 S=0;S<Mesh->GetNumSections(0);++S) {
            TArray<FVector> V,N;TArray<int32> I;TArray<FVector2D> UV;TArray<FProcMeshTangent> Tangents;TArray<FLinearColor> Colors;
            UKismetProceduralMeshLibrary::GetSectionFromStaticMesh(Mesh,0,S,V,I,N,UV,Tangents);
            Geometry->CreateMeshSection_LinearColor(S,V,I,N,UV,Colors,Tangents,false);
        }
        const FVector Scale=(Food->bFragment?Food->FoodData.FragmentScale:Food->FoodData.Scale)*Food->GetActorScale3D();
        TArray<MCFoodCutGeometry::FIsland> Islands;FString Error;
        if(!Test.TestTrue(TEXT("The source fixture has measurable actual triangle volume"),MCFoodCutGeometry::FindIslands(
            Geometry.Get(),Islands,&Error,256,FTransform(FQuat::Identity,FVector::ZeroVector,Scale))))return false;
        Out=0;for(const auto& Island:Islands)Out+=Island.Volume;
        return Test.TestTrue(TEXT("The measured source volume is finite and positive"),FMath::IsFinite(Out)&&Out>0);
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodAutomaticIntakeSwitch,"MessControl.Food.AutoIntake.SwitchRetainsLegacyDelivery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodAutomaticIntakeSwitch::RunTest(const FString&)
{
    FFoodPortionWorld T;if(!T.LoadFixture(*this)) return false;
    TestTrue(TEXT("Automatic intake is the default for new and existing profile fields"),FMCFoodSettings().bAutoConsumeSmallCuts);
    auto* Food=T.Food(FVector(80,60,50),true);
    if(!TestNotNull(TEXT("A ready portion spawns"),Food)) return false;
    Food->Settings.bAutoConsumeSmallCuts=true;
    TestTrue(TEXT("Fresh ordinary food selects automatic intake"),Food->UsesAutomaticCutIntake());
    TestFalse(TEXT("Automatic intake disables the old hand tower"),T.Hero->FoodCollection->CanCollect(Food));
    TestFalse(TEXT("Automatic intake disables old physical throat delivery"),Food->BeginSwallow());
    Food->bSpoiled=true;
    TestTrue(TEXT("Spoiled ordinary portions use the same cutting mode with a discard destination"),Food->UsesAutomaticCutIntake());
    Food->bSpoiled=false;Food->Settings.bAutoConsumeSmallCuts=false;
    TestFalse(TEXT("The profile switch deactivates automatic intake"),Food->UsesAutomaticCutIntake());
    TestTrue(TEXT("The retained manual throat handoff works with the switch off"),Food->BeginSwallow());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodAutomaticSpoiledReceipt,
    "MessControl.Food.AutoIntake.SpoiledKnifeDiscardsWithoutFeeding",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodAutomaticSpoiledReceipt::RunTest(const FString&)
{
    const auto* Presentation=GetDefault<AMCGameState>()->FoodCutIntakePresentation.Get();
    if(!TestNotNull(TEXT("The game state owns the automatic food presentation"),Presentation))return false;
    TestTrue(TEXT("Fresh food retains its runtime presentation event"),Presentation->Events.Contains(TEXT("Consume")));
    TestTrue(TEXT("Spoiled food has a registered runtime discard presentation event"),Presentation->Events.Contains(TEXT("Discard")));
    FFoodPortionWorld T;if(!T.LoadFixture(*this))return false;
    auto* Exit=T.World->SpawnActor<AMCFoodDisposal>(FVector(-800,0,2000),FRotator(0,90,0));
    if(!TestNotNull(TEXT("The retained front-disposal marker spawns"),Exit))return false;
    Exit->bBrushBin=true;Exit->bEnableLegacyDeliveryZone=false;
    Exit->Volume->SetBoxExtent(FVector(120,90,60));Exit->SetActorScale3D(FVector(1.5,.75,1));

    auto* Fresh=T.Food(FVector(80,60,50),true);
    if(!TestNotNull(TEXT("Fresh flesh spawns"),Fresh))return false;
    Fresh->Settings.bAutoConsumeSmallCuts=true;
    TestFalse(TEXT("Fresh food cannot substitute a front bin for a missing throat"),
        Fresh->HitFood(1,FVector::ForwardVector,T.Hero,Fresh->Visual->Bounds.Origin,FVector::RightVector));
    TestFalse(TEXT("The refused fresh food remains available"),Fresh->IsDisposed());
    TestFalse(TEXT("Fresh food cannot get a discard receipt"),Fresh->WasAutomaticallyDiscarded());

    auto* Throat=T.World->SpawnActor<AMCThroat>(FVector(400,0,2000),FRotator::ZeroRotator);
    auto* Rotten=T.Food(FVector(80,60,50),true);
    if(!TestNotNull(TEXT("A throat exists to catch accidental fresh accounting"),Throat)
        || !TestNotNull(TEXT("Measured rotten flesh spawns"),Rotten))return false;
    Rotten->Settings.bAutoConsumeSmallCuts=true;Rotten->bSpoiled=true;
    const int32 Swallowed=Throat->FoodSwallowed,Sequence=Throat->MealSequence;
    const FVector Point=Rotten->Visual->Bounds.Origin;
    TestTrue(TEXT("A real knife contact accepts a ready spoiled fragment at the disabled legacy front marker"),
        Rotten->HitFood(1,FVector::ForwardVector,T.Hero,Point,FVector::RightVector));
    TestTrue(TEXT("The accepted spoiled piece has its own automatic-discard receipt"),Rotten->WasAutomaticallyDiscarded());
    TestFalse(TEXT("Discarding is not reported as fresh consumption"),Rotten->WasAutomaticallyConsumed());
    TestTrue(TEXT("The accepted spoiled piece is disposed"),Rotten->IsDisposed());
    TestFalse(TEXT("Automatic disposal never fabricates a manual throw"),Rotten->WasManuallyThrown());
    TestEqual(TEXT("Discarding does not increment swallowed food"),Throat->FoodSwallowed,Swallowed);
    TestEqual(TEXT("Discarding does not start a throat meal"),Throat->MealSequence,Sequence);
    TestFalse(TEXT("Repeated automatic disposal cannot accept twice"),Rotten->TryConsumeCutPortion(FVector::ForwardVector));
    TestFalse(TEXT("A second knife hit cannot accept disposed rotten food"),Rotten->HitFood(1,FVector::ForwardVector,T.Hero,Point,FVector::RightVector));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodAutomaticSpoiledCutTransaction,
    "MessControl.Food.AutoIntake.SpoiledCutUsesAtomicDiscardRoute",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodAutomaticSpoiledCutTransaction::RunTest(const FString&)
{
    FFoodPortionWorld T;if(!T.LoadFixture(*this))return false;
    auto* Throat=T.World->SpawnActor<AMCThroat>(FVector(400,0,2000),FRotator::ZeroRotator);
    auto* Whole=T.Food(FVector(180,90,80));
    if(!TestNotNull(TEXT("The wrong-route throat spawns"),Throat)
        || !TestNotNull(TEXT("A large spoiled ingredient spawns"),Whole))return false;
    Whole->Settings.bAutoConsumeSmallCuts=true;Whole->Settings.MaxAutoConsumeDimensions=FVector(110);Whole->bSpoiled=true;
    double Volume=0;if(!T.SourceVolume(*this,Whole,Volume))return false;
    Whole->SetActorScale3D(FVector(FMath::Pow(1.25*110.*110.*110./Volume,1./3.)));
    const float Health=Whole->Health,Mass=Whole->FoodData.Mass;
    const int32 Units=Whole->FoodData.FragmentCount(),Live=T.LiveFood().Num();
    const auto Phase=Whole->Phase;
    const FVector Point=Whole->Visual->Bounds.Origin;
    TestFalse(TEXT("Ready rotten cut children refuse atomically when only a throat exists"),
        Whole->HitFood(999,FVector::ForwardVector,T.Hero,Point,FVector::ForwardVector));
    TestFalse(TEXT("Missing disposal leaves the parent active"),Whole->IsDisposed());
    TestEqual(TEXT("Refusal preserves health"),Whole->Health,Health);
    TestEqual(TEXT("Refusal preserves mass"),Whole->FoodData.Mass,Mass);
    TestEqual(TEXT("Refusal preserves phase"),Whole->Phase,Phase);
    TestEqual(TEXT("No staged child survives a missing front destination"),T.LiveFood().Num(),Live);
    TestEqual(TEXT("A refused cut adds no geometry descriptor"),Whole->CutPlanes.Num(),0);

    auto* Exit=T.World->SpawnActor<AMCFoodDisposal>(FVector(-800,0,2000),FRotator(0,90,0));
    if(!TestNotNull(TEXT("The rotated front-disposal marker spawns"),Exit))return false;
    Exit->bBrushBin=true;Exit->bEnableLegacyDeliveryZone=false;
    Exit->Volume->SetBoxExtent(FVector(120,90,60));Exit->SetActorScale3D(FVector(1.5,.75,1));
    FVector Target;AMCThroat* WrongThroat=nullptr;
    if(!TestTrue(TEXT("A disabled legacy marker resolves automatic discard"),Whole->ResolveAutomaticCutDestination(Point,Target,WrongThroat)))return false;
    TestNull(TEXT("The spoiled route never selects a throat"),WrongThroat);
    TestTrue(TEXT("The endpoint clears the rotated/scaled front edge, not a hardcoded world -X side"),
        FVector::DotProduct(Target-Exit->GetActorLocation(),Exit->GetDeliveryDirection())>Exit->Volume->GetScaledBoxExtent().X);
    TestTrue(TEXT("The destination is distinct from the throat inlet"),FVector::Dist(Target,Throat->VacuumInlet())>500.f);
    if(!TestTrue(TEXT("The same real knife cut succeeds once a front endpoint exists"),
        Whole->HitFood(999,FVector::ForwardVector,T.Hero,Point,FVector::ForwardVector)))return false;
    TestTrue(TEXT("The original ingredient is replaced"),Whole->IsDisposed());
    TestFalse(TEXT("The replaced source is not an extra discarded portion"),Whole->WasAutomaticallyDiscarded());
    int32 Accepted=0,AwardUnits=0;float ChildMass=0;
    for(TActorIterator<AMCFoodActor> It(T.World);It;++It) {
        auto* Piece=*It;if(Piece==Whole)continue;
        ++Accepted;ChildMass+=Piece->FoodData.Mass;AwardUnits+=Piece->CutAwardUnits;
        TestTrue(TEXT("Each ready spoiled child receives its own disposal"),Piece->WasAutomaticallyDiscarded()&&Piece->IsDisposed());
        TestFalse(TEXT("No discarded child counts as fresh consumption"),Piece->WasAutomaticallyConsumed());
        TestFalse(TEXT("No discarded child acquires the fresh food-credit flag"),Piece->bDeliveryScored);
        TestFalse(TEXT("A repeated child request cannot discard again"),Piece->TryConsumeCutPortion(FVector::ForwardVector));
    }
    TestEqual(TEXT("Both measured cut halves are processed"),Accepted,2);
    TestTrue(TEXT("Disposed children conserve the complete source mass"),FMath::IsNearlyEqual(ChildMass,Mass,.001f));
    TestEqual(TEXT("The original integer award budget is preserved, without granting it as fresh food"),AwardUnits,Units);
    TestEqual(TEXT("Spoiled halves do not feed the throat"),Throat->FoodSwallowed,0);
    TestEqual(TEXT("Spoiled halves do not advance a throat meal"),Throat->MealSequence,0);
    TestFalse(TEXT("The source acquires no fresh credit"),Whole->bDeliveryScored);

    // Exact actor337 geometry captured by native getall on 2026-11-08
    // 23:38:23: one island selector followed by twelve enabled cuts. It was
    // still above 110 cubed, so an artificial depth limit stranded the meal.
    auto* Apple=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Stylized_Fruits/Meshes/SM_AppleRedCut.SM_AppleRedCut"));
    if(!TestNotNull(TEXT("The user's actual AppleRedCut source loads"),Apple))return false;
#if WITH_EDITOR
    FStaticMeshCompilingManager::Get().FinishCompilation({Apple});
#endif
    if(!TestTrue(TEXT("The captured apple retains CPU-readable geometry"),Apple->bAllowCPUAccess))return false;
    const FVector Positions[]={
        {72.228969,27.175627,139.388530},{-89.789346,101.776573,88.446151},
        {-84.985359,26.514549,64.951332},{-13.034102,28.538899,-15.698088},
        {-14.422994,28.727005,-63.612152},{-49.810403,26.575356,25.613262},
        {-56.147255,29.611650,7.541107},{-56.180986,29.911809,7.535638},
        {-22.393185,29.822060,-41.475707},{-43.653617,22.064071,19.162035},
        {-14.424467,25.316192,-73.709542},{-33.305250,23.621397,-76.970218}};
    const FVector Normals[]={
        {-.953854,-.077234,.290167},{.984587,.106221,-.138945},
        {-.743878,-.063898,-.665254},{-.996052,-.084055,.028543},
        {-.938909,-.083296,-.333932},{.944050,.100346,-.314165},
        {.700614,.065785,-.710501},{.703330,.062437,-.708116},
        {-.945239,-.074102,-.317855},{.112520,.019402,-.993460},
        {.168065,.023697,-.985491},{.168554,.023895,-.985403}};
    TArray<FMCFoodCutPlane> History;History.AddDefaulted_GetRef().IslandIndex=0;
    for(int32 I=0;I<UE_ARRAY_COUNT(Positions);++I) {
        auto& Step=History.AddDefaulted_GetRef();Step.bEnabled=true;Step.Position=Positions[I];Step.Normal=Normals[I];Step.IslandIndex=0;
    }
    FMCFoodRow Captured;Captured.WholeMeshes={Apple};Captured.FragmentMeshes={Apple};
    Captured.Scale=Captured.FragmentScale=FVector(30);Captured.Kind=EMCFoodKind::Food;
    Captured.Resistance=EMCFoodResistance::Automatic;Captured.Health=25;Captured.Mass=.751208f;Captured.Fragments=3;Captured.SpoilSeconds=180;
    Captured.CrumbColor=FLinearColor(.95f,.49f,.24f,1.f);
    const FTransform CapturedPose(FQuat(-.344972,-.621676,.586503,.387978).GetNormalized(),FVector(324.892183,-1659.489346,232.645002));
    for(bool Rotten:{false,true}) {
        auto* Source=T.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),CapturedPose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!TestNotNull(TEXT("The captured twelve-cut remainder spawns"),Source))return false;
        Source->FoodData=Captured;Source->ItemName=TEXT("Apple");Source->ItemMesh=Apple;Source->bFragment=true;
        Source->Health=25;Source->CutAwardUnits=1;Source->CutPlanes=History;Source->bSpoiled=Rotten;Source->Batch=Rotten?918432:918431;
        Source->Settings.bAutoConsumeSmallCuts=true;Source->Settings.MaxAutoConsumeDimensions=FVector(110);
        Source->bStagedCutPortion=true;Source->FinishSpawning(CapturedPose);
        Source->Phase=EMCFoodPhase::Free;Source->Body->SetSimulatePhysics(false);Source->SetActorTickEnabled(false);
        if(!TestTrue(TEXT("The exact historical apple rebuilds and still needs cutting"),Source->bCutVisualReady&&Source->CutVisual&&!Source->FitsAutomaticIntakePortion()))return false;
        const float SourceMass=Source->FoodData.Mass;const int32 SourceUnits=Source->CutAwardUnits;
        const int32 BeforeSwallowed=Throat->FoodSwallowed,BeforeMeal=Throat->MealSequence;
        const FBox Bounds=Source->CutStackLocalBounds;const FVector Size=Bounds.GetSize();
        const FVector Axes[]={FVector::ForwardVector,FVector::RightVector,FVector::UpVector};
        const int32 Longest=Size.X>=Size.Y&&Size.X>=Size.Z?0:Size.Y>=Size.Z?1:2;
        bool AcceptedCut=false;
        for(int32 Attempt=0;Attempt<3&&!AcceptedCut;++Attempt)
            AcceptedCut=Source->HitFood(999,FVector::ForwardVector,T.Hero,CapturedPose.TransformPosition(Bounds.GetCenter()),CapturedPose.TransformVectorNoScale(Axes[(Longest+Attempt)%3]));
        if(!TestTrue(TEXT("Fresh and rotten unfinished apples accept a valid thirteenth cut"),AcceptedCut&&Source->IsDisposed()))return false;
        float ResultMass=0;int32 ResultUnits=0,Results=0,ReadyResults=0;
        for(TActorIterator<AMCFoodActor> It(T.World);It;++It)if(*It!=Source&&It->Batch==Source->Batch&&!It->IsActorBeingDestroyed()) {
            ++Results;ResultMass+=It->FoodData.Mass;ResultUnits+=It->CutAwardUnits;
            TestEqual(TEXT("A thirteenth cut preserves its child's spoil state"),It->bSpoiled,Rotten);
            TestTrue(TEXT("Every resulting apple keeps its thirteenth enabled descriptor"),It->CutPlanes.Num()==History.Num()+1&&It->CutPlanes.Last().bEnabled);
            if(It->FitsAutomaticIntakePortion()) {
                ++ReadyResults;TestTrue(TEXT("Every ready thirteenth-cut result completes on its correct route"),It->IsDisposed()&&(Rotten?It->WasAutomaticallyDiscarded():It->WasAutomaticallyConsumed()));
            } else TestTrue(TEXT("An oversized remainder retains usable prepared geometry and collision"),!It->IsDisposed()&&It->bCutVisualReady&&It->CutVisual&&It->SupportsPlanarCut()&&It->Body->GetCollisionEnabled()!=ECollisionEnabled::NoCollision);
            if(Rotten)TestTrue(TEXT("Rotten apple results never acquire fresh food credit"),!It->bDeliveryScored&&!It->WasAutomaticallyConsumed());
            It->Body->SetSimulatePhysics(false);It->SetActorTickEnabled(false);
        }
        TestTrue(TEXT("The thirteenth cut has real independent results and ready portions"),Results>=2&&ReadyResults>0);
        TestTrue(TEXT("The thirteenth cut conserves the captured source mass"),FMath::IsNearlyEqual(ResultMass,SourceMass,.0001f));
        TestEqual(TEXT("The thirteenth cut conserves remaining integer award ownership"),ResultUnits,SourceUnits);
        TestEqual(TEXT("Only fresh ready apple results feed the throat"),Throat->FoodSwallowed-BeforeSwallowed,Rotten?0:ReadyResults);
        TestEqual(TEXT("Only fresh ready apple results advance the meal sequence"),Throat->MealSequence-BeforeMeal,Rotten?0:ReadyResults);
        if(Rotten)TestFalse(TEXT("Replacing the rotten historical source grants no fresh credit"),Source->bDeliveryScored);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodAutomaticIntakeReceipt,"MessControl.Food.AutoIntake.AcceptedKnifeCompletesExactlyOnce",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodAutomaticIntakeReceipt::RunTest(const FString&)
{
    FFoodPortionWorld T;if(!T.LoadFixture(*this)) return false;
    auto* Food=T.Food(FVector(80,60,50),true);
    if(!TestNotNull(TEXT("The small-cut fixture spawns"),Food)) return false;
    Food->Settings.bAutoConsumeSmallCuts=true;
    TestTrue(TEXT("Authored flesh is initialized with its actual measured volume"),Food->FitsAutomaticIntakePortion());
    TestFalse(TEXT("An absent mouth cannot silently discard a measured ready portion"),Food->TryConsumeCutPortion(FVector::ForwardVector));
    const FVector Point=Food->Visual->Bounds.Origin;
    TestFalse(TEXT("A real knife contact without a mouth cannot silently discard the portion"),Food->HitFood(1,FVector::ForwardVector,T.Hero,Point,FVector::RightVector));
    TestFalse(TEXT("The portion remains usable after an absent-target refusal"),Food->IsDisposed());
    auto* Throat=T.World->SpawnActor<AMCThroat>(FVector(400,0,2000),FRotator::ZeroRotator);
    if(!TestNotNull(TEXT("The real mouth intake spawns"),Throat)) return false;
    TestTrue(TEXT("One accepted real knife hit consumes the already-small portion"),Food->HitFood(1,FVector::ForwardVector,T.Hero,Point,FVector::RightVector));
    TestTrue(TEXT("The accepted portion carries the automatic receipt"),Food->WasAutomaticallyConsumed());
    TestTrue(TEXT("The physical food is deactivated after the accepted event"),Food->IsDisposed());
    TestEqual(TEXT("The mouth records one accepted portion"),Throat->FoodSwallowed,1);
    TestFalse(TEXT("A repeated accepted request cannot consume again"),Food->TryConsumeCutPortion(FVector::ForwardVector));
    TestFalse(TEXT("A second hit cannot consume disposed food"),Food->HitFood(1,FVector::ForwardVector,T.Hero,Point,FVector::RightVector));
    TestEqual(TEXT("Repeated requests cannot duplicate accepted delivery"),Throat->FoodSwallowed,1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodAutomaticIntakeSplit,"MessControl.Food.AutoIntake.ThresholdCrossingCutConsumesOnlyReadyHalves",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodAutomaticIntakeSplit::RunTest(const FString&)
{
    FFoodPortionWorld T;if(!T.LoadFixture(*this)) return false;
    auto* Throat=T.World->SpawnActor<AMCThroat>(FVector(400,0,2000),FRotator::ZeroRotator);
    if(!TestNotNull(TEXT("The mouth intake spawns"),Throat)) return false;
    auto* Whole=T.Food(FVector(180,90,80));
    if(!TestNotNull(TEXT("The threshold-crossing food spawns"),Whole)) return false;
    Whole->Settings.bAutoConsumeSmallCuts=true;
    Whole->Settings.MaxAutoConsumeDimensions=FVector(110); // Explicit configurable threshold for this split fixture.
    double Volume=0;if(!T.SourceVolume(*this,Whole,Volume))return false;
    // The real cabbage fills less than its bounds. Set an actual source volume
    // above the budget so this remains a split test after removing axis gates.
    Whole->SetActorScale3D(FVector(FMath::Pow(1.25*110.*110.*110./Volume,1./3.)));
    TestFalse(TEXT("The parent must still be cut"),Whole->FitsHandPortion());
    TestFalse(TEXT("An unmeasured source cannot auto-complete before accepted knife contact"),Whole->TryConsumeCutPortion(FVector::ForwardVector));
    if(!TestTrue(TEXT("A central cut produces two small halves"),Whole->HitFood(999,FVector::RightVector,T.Hero,Whole->Visual->Bounds.Origin,FVector::ForwardVector))) return false;
    TestTrue(TEXT("The replaced parent has no delivery receipt"),Whole->IsDisposed() && !Whole->WasAutomaticallyConsumed());
    int32 AcceptedHalves=0;float DeliveredMass=0;
    for(TActorIterator<AMCFoodActor> It(T.World);It;++It) if(It->WasAutomaticallyConsumed()) {
        ++AcceptedHalves;DeliveredMass+=It->FoodData.Mass;
        TestTrue(TEXT("Every accepted half passes its measured-volume limit"),It->FitsAutomaticIntakePortion() && It->bFragment && !It->CutPlanes.IsEmpty());
    }
    TestEqual(TEXT("Only the two cut halves complete"),AcceptedHalves,2);
    TestEqual(TEXT("The mouth records each accepted half once"),Throat->FoodSwallowed,2);
    TestTrue(TEXT("Consumed halves preserve the parent's food mass"),FMath::IsNearlyEqual(DeliveredMass,Whole->FoodData.Mass,.001f));
    TestTrue(TEXT("No hand-carry fragments remain after ready halves complete"),T.LiveFood().IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodAutomaticUnsupportedSource,"MessControl.Food.AutoIntake.UnsupportedSourceCannotCompleteThroughFallback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodAutomaticUnsupportedSource::RunTest(const FString&)
{
    FFoodPortionWorld T;if(!T.LoadFixture(*this)) return false;
    auto* Whole=T.Food(FVector(180,140,130));
    if(!TestNotNull(TEXT("The unsupported-source ingredient spawns"),Whole)) return false;
    Whole->Settings.bAutoConsumeSmallCuts=true;
    // Simulate an unavailable native slicing source and no authored fragments;
    // the retained legacy ConfigureItem fallback must not become a meal receipt.
    Whole->ItemMesh=nullptr;Whole->FoodData.FragmentMeshes.Reset();
    const float Health=Whole->Health,Mass=Whole->FoodData.Mass;
    TestFalse(TEXT("A lethal unsupported hit is refused before legacy fallback credit"),Whole->HitFood(999,FVector::ForwardVector,T.Hero));
    TestEqual(TEXT("Refusal retains the parent's health"),Whole->Health,Health);
    TestEqual(TEXT("Refusal retains the parent's mass"),Whole->FoodData.Mass,Mass);
    TestFalse(TEXT("The unfinished ingredient remains active"),Whole->IsDisposed());
    TestFalse(TEXT("No automatic delivery receipt was issued"),Whole->WasAutomaticallyConsumed());
    TestEqual(TEXT("No replacement actors were fabricated"),T.LiveFood().Num(),1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodAutomaticIndependentLimit,"MessControl.Food.AutoIntake.VolumeOnlyKeepsLegacyHandSizeIndependent",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodAutomaticIndependentLimit::RunTest(const FString&)
{
    FFoodPortionWorld T;if(!T.LoadFixture(*this)) return false;
    const FMCFoodSettings Defaults;
    TestTrue(TEXT("Automatic intake defaults to a 110 cubed cm volume budget"),Defaults.MaxAutoConsumeDimensions.Equals(FVector(110)));
    TestTrue(TEXT("The retained hand pattern still defaults to 110 cm per axis"),Defaults.MaxHandDimensions.Equals(FVector(110)));
    FMCFoodSettings Custom=Defaults;Custom.MaxAutoConsumeDimensions=FVector(40);
    TestTrue(TEXT("Changing the volume budget leaves legacy hand dimensions independent"),Custom.MaxHandDimensions.Equals(FVector(110)));
    auto* Throat=T.World->SpawnActor<AMCThroat>(FVector(400,0,2000),FRotator::ZeroRotator);
    if(!TestNotNull(TEXT("The real intake spawns"),Throat)) return false;

    auto* LargeReady=T.Food(FVector(250,30,20),true);
    if(!TestNotNull(TEXT("A long low-volume automatic portion spawns"),LargeReady)) return false;
    LargeReady->Settings.bAutoConsumeSmallCuts=true;
    TestFalse(TEXT("This portion remains too large for the legacy hand"),LargeReady->FitsHandPortion());
    TestTrue(TEXT("Authored flesh is ready from its initialized actual geometry volume"),LargeReady->FitsAutomaticIntakePortion());
    TestTrue(TEXT("Accepted knife contact completes a low-volume piece longer than 110 cm"),LargeReady->HitFood(1,FVector::ForwardVector,T.Hero,LargeReady->Visual->Bounds.Origin,FVector::RightVector));
    TestTrue(TEXT("The long piece gets one automatic receipt"),LargeReady->WasAutomaticallyConsumed());
    TestTrue(TEXT("Measured volume can pass while the hand dimensions still fail"),LargeReady->FitsAutomaticIntakePortion()&&!LargeReady->FitsHandPortion());

    auto* AboveLimit=T.Food(FVector(250,30,20));
    if(!TestNotNull(TEXT("An unmeasured whole ingredient spawns"),AboveLimit)) return false;
    AboveLimit->Settings.bAutoConsumeSmallCuts=true;
    TestFalse(TEXT("Whole-food readiness does not invent a volume from dimensions"),AboveLimit->FitsAutomaticIntakePortion());
    TestFalse(TEXT("A direct request without an accepted measuring contact cannot consume source food"),AboveLimit->TryConsumeCutPortion(FVector::ForwardVector));
    TestFalse(TEXT("The refused portion is preserved"),AboveLimit->IsDisposed());
    TestTrue(TEXT("The first accepted knife contact measures a genuinely low-volume whole"),AboveLimit->HitFood(1,FVector::ForwardVector,T.Hero,AboveLimit->Visual->Bounds.Origin,FVector::RightVector));
    TestTrue(TEXT("A measured low-volume whole needs no artificial minimum cut count"),AboveLimit->WasAutomaticallyConsumed());

    auto* SmallerSetting=T.Food(FVector(100,90,80));
    if(!TestNotNull(TEXT("A configurable stricter automatic fixture spawns"),SmallerSetting)) return false;
    SmallerSetting->Settings.bAutoConsumeSmallCuts=true;
    double Volume=0;if(!T.SourceVolume(*this,SmallerSetting,Volume))return false;
    SmallerSetting->Settings.MaxAutoConsumeDimensions=FVector(FMath::Pow(Volume*.8,1./3.));
    TestTrue(TEXT("The stricter fixture would fit the retained 110 cm hand"),SmallerSetting->FitsHandPortion());
    TestFalse(TEXT("Its unmeasured compact dimensions cannot replace the custom volume check"),SmallerSetting->FitsAutomaticIntakePortion());
    TestTrue(TEXT("Legacy hand readiness cannot block a valid stricter automatic cut"),SmallerSetting->HitFood(999,FVector::ForwardVector,T.Hero,SmallerSetting->Visual->Bounds.Origin,FVector::ForwardVector));
    TestTrue(TEXT("Compact food above its measured custom budget is actually split instead of directly consumed"),SmallerSetting->IsDisposed() && !SmallerSetting->WasAutomaticallyConsumed());
    TestEqual(TEXT("Two low-volume sources plus two stricter cut results are accepted once"),Throat->FoodSwallowed,4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodAutomaticSliverVolume,
    "MessControl.Food.AutoIntake.LongThinCutUsesOwnVolumeAndCompletesOnce",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodAutomaticSliverVolume::RunTest(const FString&)
{
    FFoodPortionWorld T;if(!T.LoadFixture(*this)) return false;
    auto* Throat=T.World->SpawnActor<AMCThroat>(FVector(400,0,2000),FRotator::ZeroRotator);
    if(!TestNotNull(TEXT("The volume fixture has a real mouth intake"),Throat)) return false;
    // This diagnostic stretches the real cabbage triangles into a flat strip.
    // Its 50 cm outer cap has at most 1,000,000 cm3 in its entire bounding box,
    // below 110^3, while leaving a substantially larger physical sibling.
    // Saved meshes and menu balance remain unchanged.
    auto* Whole=T.Food(FVector(1000,10000,2));
    if(!TestNotNull(TEXT("The long thin source spawns"),Whole)) return false;
    Whole->Settings.bAutoConsumeSmallCuts=true;
    Whole->Settings.MaxAutoConsumeDimensions=FVector(110);
    const float SourceMass=Whole->FoodData.Mass;
    const int32 SourceAwards=Whole->FoodData.FragmentCount();
    TestFalse(TEXT("The uncut strip still needs its first accepted cut"),Whole->FitsAutomaticIntakePortion());
    const FBox Bounds=Whole->Visual->Bounds.GetBox();
    const FVector Point(Bounds.Max.X-50,Bounds.GetCenter().Y,Bounds.GetCenter().Z);
    if(!TestTrue(TEXT("A real off-centre knife cut accepts the thin outer cap"),Whole->HitFood(999,FVector::RightVector,T.Hero,Point,FVector::ForwardVector))) return false;
    TestTrue(TEXT("The parent is replaced without its own delivery receipt"),Whole->IsDisposed() && !Whole->WasAutomaticallyConsumed());

    int32 Accepted=0,LongAccepted=0,Large=0,Awards=0;
    float LeafMass=0;
    for(TActorIterator<AMCFoodActor> It(T.World);It;++It) {
        auto* Piece=*It;
        if(Piece==Whole) continue;
        LeafMass+=Piece->FoodData.Mass;Awards+=Piece->CutAwardUnits;
        if(Piece->WasAutomaticallyConsumed()) {
            ++Accepted;
            if(Piece->GetHandPortionSize().GetMax()>110) ++LongAccepted;
            TestTrue(TEXT("A staged volume receipt agrees with post-spawn readiness"),Piece->FitsAutomaticIntakePortion());
            TestTrue(TEXT("The ready child is disposed in the same accepted transaction"),Piece->IsDisposed());
            TestFalse(TEXT("A ready sliver never becomes a replicated physical actor"),Piece->GetIsReplicated());
            TestTrue(TEXT("A ready sliver has neither collision nor a live rigid body"),Piece->Body->GetCollisionEnabled()==ECollisionEnabled::NoCollision && !Piece->Body->IsSimulatingPhysics());
            TestFalse(TEXT("A repeated delivery request cannot consume the sliver again"),Piece->TryConsumeCutPortion(FVector::ForwardVector));
        } else {
            ++Large;
            TestFalse(TEXT("The large sibling remains active"),Piece->IsDisposed());
            TestFalse(TEXT("The large sibling does not fit the shared intake predicate"),Piece->FitsAutomaticIntakePortion());
            TestNotNull(TEXT("The retained large sibling has actual sliced render geometry"),Piece->CutVisual.Get());
            TestFalse(TEXT("A hidden ready receipt cannot remain stranded"),Piece->IsHidden());
        }
    }
    TestTrue(TEXT("At least one child longer than 110 cm completes because its own volume is small"),LongAccepted>0);
    TestTrue(TEXT("A genuinely large-volume sibling still needs more cutting"),Large>0);
    TestTrue(TEXT("The cut conserves the complete live-or-consumed food mass"),FMath::IsNearlyEqual(LeafMass,SourceMass,.001f));
    TestEqual(TEXT("The cut preserves the original integer delivery award budget"),Awards,SourceAwards);
    TestEqual(TEXT("Each accepted child is recorded by the throat exactly once"),Throat->FoodSwallowed,Accepted);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodPoweredKnifeHardShell,
    "MessControl.Food.AutoIntake.PoweredKnifeOpensHardShellBeforeIntake",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodPoweredKnifeHardShell::RunTest(const FString&)
{
    FFoodPortionWorld T;if(!T.LoadFixture(*this)) return false;
    auto* Whole=T.Food(FVector(80,60,50));
    if(!TestNotNull(TEXT("A hard whole-food fixture spawns"),Whole)) return false;
    Whole->FoodData.Resistance=EMCFoodResistance::Hard;
    Whole->Settings.bAutoConsumeSmallCuts=true;
    auto* Inventory=T.Hero->Inventory.Get();
    if(!TestNotNull(TEXT("The powered-knife worker has inventory"),Inventory)) return false;
    Inventory->Selected=EMCToolSlot::Knife;
    if(!TestTrue(TEXT("The real development upgrade grants the powered knife"),Inventory->GrantDevToolUpgrade(EMCToolUpgrade::Chainsaw))) return false;
    TestTrue(TEXT("The powered knife retains permission to open hard food"),Inventory->CanBreak(Whole));
    TestFalse(TEXT("An unopened shell has no accepted measured portion volume"),Whole->FitsAutomaticIntakePortion());
    auto* Throat=T.World->SpawnActor<AMCThroat>(FVector(400,0,2000),FRotator::ZeroRotator);
    if(!TestNotNull(TEXT("A real mouth intake is available"),Throat)) return false;
    const float ContactDamage=Whole->FoodData.Health*.5f;
    TestTrue(TEXT("The first powered contact damages the shell"),Whole->HitFood(ContactDamage,FVector::ForwardVector,T.Hero));
    TestFalse(TEXT("A small hard shell cannot bypass opening through automatic intake"),Whole->IsDisposed() || Whole->WasAutomaticallyConsumed());
    TestEqual(TEXT("An unopened shell produces no accepted intake"),Throat->FoodSwallowed,0);
    TestTrue(TEXT("The second powered contact opens the authored shell"),Whole->HitFood(ContactDamage,FVector::ForwardVector,T.Hero));
    TestTrue(TEXT("The shell is replaced without a direct whole-food intake receipt"),Whole->IsDisposed() && !Whole->WasAutomaticallyConsumed());
    int32 AcceptedFlesh=0;
    for(TActorIterator<AMCFoodActor> It(T.World);It;++It) if(It->WasAutomaticallyConsumed()) {
        ++AcceptedFlesh;
        TestTrue(TEXT("Only opened, ready flesh completes"),It->bFragment && !It->IsHardFood() && It->FitsAutomaticIntakePortion());
    }
    TestEqual(TEXT("Both authored ready flesh pieces complete after opening"),AcceptedFlesh,2);
    TestEqual(TEXT("The mouth accepts only the two opened pieces"),Throat->FoodSwallowed,2);
    return true;
}
#endif
