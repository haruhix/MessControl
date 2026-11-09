#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodActor.h"
#include "MCFoodCutGeometry.h"
#include "MCFoodProcessingRecoverySubsystem.h"
#include "MCGameState.h"
#include "MCInventoryComponent.h"
#include "MCTongue.h"
#include "MCPlayerState.h"
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
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

// Reuse the existing closed, CPU-readable diagnostic solid. No source asset
// flags, package state, or authored mesh dimensions are modified by this file.
namespace MCFoodPreparedCutTests { UStaticMesh* MakeBox(); void ImportStatic(UStaticMesh*,UProceduralMeshComponent*); }
namespace MCFoodProcessingRecoveryTests
{
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCGameState* Game=nullptr;
    AMCToothCharacter* Hero=nullptr;
    AMCPlayerState* Player=nullptr;
    AMCThroat* Throat=nullptr;
    AMCFoodDisposal* Exit=nullptr;
    TStrongObjectPtr<UStaticMesh> TongueMesh;
    TStrongObjectPtr<UMaterialInterface> Cap;
    UMCFoodProcessingRecoverySubsystem* Recovery=nullptr;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        Cap.Reset(LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/Food/CutMaterials/MI_FoodCut_Cream.MI_FoodCut_Cream")));
        TongueMesh.Reset(MCFoodPreparedCutTests::MakeBox());
        const FTransform TissuePose(FQuat::Identity,FVector(0,0,1000),FVector(30,20,.01));
        auto* Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),TissuePose);
        Tongue->SourceMesh=TongueMesh.Get();Tongue->FinishSpawning(TissuePose);Tongue->SetActorTickEnabled(false);
        Game=World->SpawnActor<AMCGameState>();World->SetGameState(Game);
        Game->Phase=EMCShiftPhase::Working;Game->Day=1;Game->GameplayLoadId=FGuid::NewGuid();Game->SetActorTickEnabled(false);
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,2000),FRotator::ZeroRotator);
        Hero->GetCharacterMovement()->DisableMovement();Hero->SetActorTickEnabled(false);
        Player=World->SpawnActor<AMCPlayerState>();Player->Points=17;Player->SetActorTickEnabled(false);
        Throat=World->SpawnActor<AMCThroat>(FVector(1800,0,2000),FRotator::ZeroRotator);
        Throat->SetActorTickEnabled(false);
        const FTransform Pose(FRotator(0,37,0),FVector(-1800,0,2000));
        Exit=World->SpawnActorDeferred<AMCFoodDisposal>(AMCFoodDisposal::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Exit->bBrushBin=true;Exit->bEnableLegacyDeliveryZone=false;Exit->Volume->SetBoxExtent(FVector(130,95,60));
        Exit->FinishSpawning(Pose);Exit->SetActorTickEnabled(false);
        Recovery=World->GetSubsystem<UMCFoodProcessingRecoverySubsystem>();
    }
    ~FWorld(){World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    void Step(){World->Tick(LEVELTICK_All,1.f/60.f);}
    AMCFoodActor* Spawn(const FMCFoodRow& Row,FName Name,int32 Batch,bool Fragment=false,UStaticMesh* Mesh=nullptr,bool AuthoredSettings=false)
    {
        const FTransform Pose(FRotator::ZeroRotator,FVector(0,0,2500));
        auto* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Food)return nullptr;
        FRandomStream Random(919711);Food->ConfigureItem(Name,Row,Random,Fragment);
        if(Mesh)Food->ItemMesh=Mesh;
        Food->Batch=Batch;Food->FinishSpawning(Pose);
        if(!AuthoredSettings){Food->Settings.bAutoConsumeSmallCuts=true;Food->Settings.MaxAutoConsumeDimensions=FVector(110);Food->Settings.MinCutVolume=1;Food->Settings.MinCutThickness=.1f;}
        Food->Phase=EMCFoodPhase::Free;Food->Body->SetSimulatePhysics(false);Food->SetActorTickEnabled(false);
        return Food;
    }
    TArray<AMCFoodActor*> Live(int32 Batch) const
    {
        TArray<AMCFoodActor*> Out;
        for(TActorIterator<AMCFoodActor> It(World);It;++It)
            if(It->Batch==Batch&&!It->IsActorBeingDestroyed()&&!It->IsDisposed())Out.Add(*It);
        return Out;
    }
};
FMCFoodRow BoxRow(UStaticMesh* Mesh)
{
    FMCFoodRow Row;Row.WholeMeshes={Mesh};Row.FragmentMeshes={Mesh};Row.Scale=Row.FragmentScale=FVector(2);
    Row.Kind=EMCFoodKind::Food;Row.Resistance=EMCFoodResistance::Soft;Row.Mass=6.25f;Row.Health=100;Row.Fragments=5;Row.SpoilSeconds=300;
    return Row;
}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FMCFoodProcessingRecoveryTest,"MessControl.Food.ProcessingRecovery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FMCFoodProcessingRecoveryTest::GetTests(TArray<FString>& Names,TArray<FString>& Commands) const
{
    for(const TCHAR* Name:{TEXT("ConfirmedFaultPreservesLedgerAndProcesses"),TEXT("OrdinaryAndProtectedStatesDoNotReplace"),TEXT("AtomicFailureEpochAndCleanupPreserveOriginal"),TEXT("MissingEndpointsRestoreTheirSavedRecipe"),TEXT("CurrentMenuProcessingCoverage")}){Names.Add(Name);Commands.Add(Name);}
}
bool FMCFoodProcessingRecoveryTest::RunTest(const FString& Case)
{
    using namespace MCFoodProcessingRecoveryTests;
    const auto NoAward=[this](FWorld& T,AMCFoodActor* Source,AMCFoodActor* Replacement=nullptr)
    {
        TestEqual(TEXT("Recovery never increments player points"),T.Player->Points,17);
        TestEqual(TEXT("Recovery never feeds the throat"),T.Throat->FoodSwallowed,0);
        TestEqual(TEXT("Recovery never advances a meal"),T.Throat->MealSequence,0);
        TestFalse(TEXT("Recovery does not award the failed source"),Source->bDeliveryScored);
        TestFalse(TEXT("Recovery does not consume the failed source"),Source->WasAutomaticallyConsumed());
        TestFalse(TEXT("Recovery does not discard the failed source"),Source->WasAutomaticallyDiscarded());
        if(Replacement){TestFalse(TEXT("Replacement has no awarded delivery"),Replacement->bDeliveryScored);TestFalse(TEXT("Replacement is still working food"),Replacement->WasAutomaticallyConsumed()||Replacement->WasAutomaticallyDiscarded());}
    };
    if(Case==TEXT("ConfirmedFaultPreservesLedgerAndProcesses"))
    {
        for(bool Unsupported:{false,true})
        {
            FWorld T;TStrongObjectPtr<UStaticMesh> Box(MCFoodPreparedCutTests::MakeBox()),Healthy(MCFoodPreparedCutTests::MakeBox());
            FMCFoodRow Row=BoxRow(Box.Get());Row.FragmentMeshes={Healthy.Get()};
            auto* Source=T.Spawn(Row,TEXT("FailedRemainder"),Unsupported?919712:919711,true,Box.Get());
            if(!TestNotNull(TEXT("Failed source exists"),Source)||!TestNotNull(TEXT("World recovery exists"),T.Recovery))return false;
            Source->FoodData.Mass=1.25f;Source->Settings.Mass=1.25f;Source->CutAwardUnits=Unsupported?3:0;
            Source->bSpoiled=Unsupported;Source->SpoilAt=200.;Source->LastHandledBy=T.Player;Source->SetOwner(T.Hero);
            if(Unsupported)Box->bAllowCPUAccess=false;
            else {FMCFoodCutPlane Selector;Selector.IslandIndex=999999;Source->CutPlanes={Selector};}
            const float Mass=Source->FoodData.Mass;const double Clock=Source->SpoilAt;const int32 Units=Source->CutAwardUnits;
            TestFalse(TEXT("A genuine technical knife failure refuses without awarding"),Source->HitFood(1,FVector::ForwardVector,T.Hero,Source->Visual->Bounds.Origin,FVector::RightVector));
            TestEqual(TEXT("Production failure hook records one source"),T.Recovery->GetPendingRecordCount(),1);
            UMCFoodProcessingRecoverySubsystem::ReportFailure(Source,Unsupported?EMCFoodProcessingFailure::UnsupportedGeometry:EMCFoodProcessingFailure::InvalidGeometry);
            TestEqual(TEXT("Repeated fault reports deduplicate"),T.Recovery->GetPendingRecordCount(),1);
            T.Recovery->TickProcessRecords();TestFalse(TEXT("Reporting cannot replace in the same world time"),Source->IsDisposed());NoAward(T,Source);
            T.Step();auto Live=T.Live(Source->Batch);
            if(!TestEqual(TEXT("Confirmed fault becomes one working replacement"),Live.Num(),1))return false;
            auto* Replacement=Live[0];if(!TestTrue(TEXT("Replacement differs from source"),Replacement!=Source&&Source->IsDisposed()))return false;
            TestEqual(TEXT("Exactly one recovery commits"),T.Recovery->GetReplacementCount(),1);
            TestEqual(TEXT("Replacement preserves batch"),Replacement->Batch,Source->Batch);
            TestEqual(TEXT("Replacement preserves ingredient identity"),Replacement->ItemName,Source->ItemName);
            TestEqual(TEXT("Replacement preserves remaining mass"),Replacement->FoodData.Mass,Mass);
            TestTrue(TEXT("Replacement physics retains the same mass"),FMath::IsNearlyEqual(Replacement->Settings.Mass,Mass)&&FMath::IsNearlyEqual(Replacement->Body->GetMass(),Mass,.001f));
            TestEqual(TEXT("Replacement preserves zero as well as positive units"),Replacement->CutAwardUnits,Units);
            TestEqual(TEXT("Replacement preserves absolute spoil clock"),Replacement->SpoilAt,Clock);
            TestEqual(TEXT("Replacement preserves spoiled state"),Replacement->bSpoiled,Unsupported);
            TestTrue(TEXT("Replacement preserves owner/credit handler"),Replacement->GetOwner()==T.Hero&&Replacement->GetLastHandledBy()==T.Player);
            NoAward(T,Source,Replacement);
            UMCFoodProcessingRecoverySubsystem::ReportFailure(Source,EMCFoodProcessingFailure::InvalidGeometry);T.Step();
            TestEqual(TEXT("Disposed original cannot enqueue a duplicate replacement"),T.Recovery->GetReplacementCount(),1);
            TestTrue(TEXT("Working replacement accepts actual food processing"),Replacement->HitFood(1,FVector::ForwardVector,T.Hero,Replacement->Visual->Bounds.Origin,FVector::RightVector));
            for(int32 I=0;I<24&&Replacement->IsCutTransactionPending();++I)T.Step();
            TestFalse(TEXT("Replacement processing does not remain pending"),Replacement->IsCutTransactionPending());
            TestTrue(TEXT("Normal processing completes the replacement"),Replacement->IsDisposed());
        }
        return true;
    }
    if(Case==TEXT("OrdinaryAndProtectedStatesDoNotReplace"))
    {
        FWorld T;TStrongObjectPtr<UStaticMesh> Box(MCFoodPreparedCutTests::MakeBox());auto Row=BoxRow(Box.Get());
        auto* Food=T.Spawn(Row,TEXT("HealthyIdle"),919713,true);
        T.Recovery->TickProcessRecords();T.Step();
        TestEqual(TEXT("Idle food is never interpreted as completed or broken"),T.Recovery->GetReplacementCount(),0);
        T.Hero->Inventory->Selected=EMCToolSlot::Brush;TestFalse(TEXT("Ordinary wrong tool is refused by inventory"),T.Hero->Inventory->CanBreak(Food));
        TestFalse(TEXT("A finite non-splitting/grazing plane is a normal refusal"),Food->HitFood(1,FVector::ForwardVector,T.Hero,Food->Visual->Bounds.Origin+FVector(0,10000,0),FVector::RightVector));
        TestFalse(TEXT("An exactly tangent plane is not a technical fault"),Food->HitFood(1,FVector::ForwardVector,T.Hero,Food->Visual->Bounds.Origin+FVector(0,Food->Visual->Bounds.BoxExtent.Y,0),FVector::RightVector));
        TestEqual(TEXT("A no-split contact creates no fault record"),T.Recovery->GetPendingRecordCount(),0);
        auto* Repaired=T.Spawn(Row,TEXT("ExternallyRepairedCPU"),919716,true);
        Box->bAllowCPUAccess=false;
        TestFalse(TEXT("Temporarily CPU-less fragment reports an actual processing failure"),Repaired->HitFood(1,FVector::ForwardVector,T.Hero,Repaired->Visual->Bounds.Origin,FVector::RightVector));
        TestEqual(TEXT("Temporary CPU failure is observed"),T.Recovery->GetPendingRecordCount(),1);
        Box->bAllowCPUAccess=true;T.Step();
        TestTrue(TEXT("A repaired source clears its fault without replacement"),!Repaired->IsDisposed()&&T.Recovery->GetPendingRecordCount()==0&&T.Recovery->GetReplacementCount()==0);
        Food->Holders.Add(T.Hero);UMCFoodProcessingRecoverySubsystem::ReportFailure(Food,EMCFoodProcessingFailure::InvalidGeometry);T.Step();
        TestFalse(TEXT("A held source cannot be replaced"),Food->IsDisposed());TestEqual(TEXT("Held state retains the source actor"),T.Live(Food->Batch).Num(),1);
        Food->Holders.Reset();
        Food->bCutTransactionPending=true;UMCFoodProcessingRecoverySubsystem::ReportFailure(Food,EMCFoodProcessingFailure::InvalidGeometry);T.Step();
        TestFalse(TEXT("A pending cut source cannot be replaced"),Food->IsDisposed());TestEqual(TEXT("No protected state commits recovery"),T.Recovery->GetReplacementCount(),0);
        Food->bCutTransactionPending=false;NoAward(T,Food);return true;
    }
    if(Case==TEXT("AtomicFailureEpochAndCleanupPreserveOriginal"))
    {
        AddExpectedMessage(TEXT("MC_FOOD_PROCESSING_RECOVERY_BLOCKED"),ELogVerbosity::Error,EAutomationExpectedMessageFlags::Contains,1);
        FWorld T;TStrongObjectPtr<UStaticMesh> Box(MCFoodPreparedCutTests::MakeBox());auto Row=BoxRow(Box.Get());
        auto* Source=T.Spawn(Row,TEXT("NoEndpointRecipe"),919714,true);Source->CutAwardUnits=0;Source->bSpoiled=true;Source->SpoilAt=200;
        const auto Pose=Source->GetActorTransform();const float Mass=Source->FoodData.Mass,Health=Source->Health;
        T.Throat->Destroy();T.Exit->Destroy();
        UMCFoodProcessingRecoverySubsystem::ReportFailure(Source,EMCFoodProcessingFailure::MissingDestination);T.Step();
        TestEqual(TEXT("No prior destination recipe cannot fabricate a successful replacement"),T.Recovery->GetReplacementCount(),0);
        TestTrue(TEXT("Atomic refusal keeps the visible original intact"),!Source->IsDisposed()&&!Source->IsHidden()&&Source->GetActorTransform().Equals(Pose)&&Source->FoodData.Mass==Mass&&Source->Health==Health&&Source->CutAwardUnits==0&&Source->bSpoiled&&Source->SpoilAt==200);
        for(int32 I=0;I<125;++I)T.Step();
        TestTrue(TEXT("Repeated unavailable recovery is bounded and diagnosed"),T.Recovery->GetBlockedCount()>0);
        T.Game->GameplayLoadId=FGuid::NewGuid();T.Step();
        TestEqual(TEXT("Run epoch change invalidates stale recovery"),T.Recovery->GetPendingRecordCount(),0);
        UMCFoodProcessingRecoverySubsystem::ReportFailure(Source,EMCFoodProcessingFailure::SpawnFailure);++T.Game->Day;T.Step();
        TestEqual(TEXT("Day change invalidates stale recovery"),T.Recovery->GetPendingRecordCount(),0);
        UMCFoodProcessingRecoverySubsystem::ReportFailure(Source,EMCFoodProcessingFailure::SpawnFailure);T.Game->Phase=EMCShiftPhase::Intermission;T.Step();
        TestEqual(TEXT("Phase change invalidates stale recovery"),T.Recovery->GetPendingRecordCount(),0);
        T.Game->Phase=EMCShiftPhase::Working;
        UMCFoodProcessingRecoverySubsystem::ReportFailure(Source,EMCFoodProcessingFailure::SpawnFailure);
        auto* NewGame=T.World->SpawnActor<AMCGameState>();NewGame->Day=T.Game->Day;NewGame->Phase=T.Game->Phase;NewGame->GameplayLoadId=T.Game->GameplayLoadId;NewGame->SetActorTickEnabled(false);
        T.World->SetGameState(NewGame);T.Game=NewGame;T.Step();
        TestEqual(TEXT("GameState identity change invalidates stale work even with the same epoch"),T.Recovery->GetPendingRecordCount(),0);
        UMCFoodProcessingRecoverySubsystem::ReportFailure(Source,EMCFoodProcessingFailure::SpawnFailure);
        TestEqual(TEXT("Lifecycle fixture has an outstanding record"),T.Recovery->GetPendingRecordCount(),1);
        T.Recovery->Deinitialize();T.Recovery->Deinitialize();
        TestEqual(TEXT("Deinitialize safely clears pending work twice"),T.Recovery->GetPendingRecordCount(),0);
        TestTrue(TEXT("Lifecycle cleanup never awards/removes the original"),!Source->IsDisposed()&&!Source->bDeliveryScored&&Source->CutAwardUnits==0&&T.Player->Points==17);
        return true;
    }
    if(Case==TEXT("MissingEndpointsRestoreTheirSavedRecipe"))
    {
        FWorld T;TStrongObjectPtr<UStaticMesh> Box(MCFoodPreparedCutTests::MakeBox());auto Row=BoxRow(Box.Get());
        auto* Source=T.Spawn(Row,TEXT("MissingExit"),919715,true);Source->bSpoiled=true;
        T.Throat->FoodSwallowed=2;T.Throat->MealSequence=3;
        T.Throat->ThroatPhase=EMCThroatPhase::Swallowing;T.Throat->Weight=4;T.Throat->FoodInZone=3;T.Throat->QueuedFoodCount=2;
        const FTransform ThroatPose=T.Throat->GetActorTransform(),ExitPose=T.Exit->GetActorTransform();const FVector Extent=T.Exit->Volume->GetUnscaledBoxExtent();
        T.Recovery->TickProcessRecords();
        // Runtime completions after the last periodic recipe snapshot must be
        // retained by the endpoint's destruction notification as well.
        T.Throat->FoodSwallowed=7;T.Throat->MealSequence=11;
        T.Throat->Destroy();T.Exit->Destroy();
        UMCFoodProcessingRecoverySubsystem::ReportFailure(Source,EMCFoodProcessingFailure::MissingDestination);T.Step();
        AMCThroat* Throat=nullptr;AMCFoodDisposal* Exit=nullptr;int32 Fresh=0,Spoiled=0;
        for(TActorIterator<AMCFoodDisposal> It(T.World);It;++It)if(!It->IsActorBeingDestroyed())
            if(auto* Candidate=Cast<AMCThroat>(*It)){Throat=Candidate;++Fresh;}else if(It->bBrushBin){Exit=*It;++Spoiled;}
        if(!TestTrue(TEXT("Cached endpoints are recreated once on their original sides"),Fresh==1&&Spoiled==1&&Throat&&Exit))return false;
        TestTrue(TEXT("Fresh endpoint retains its actual transform"),Throat->GetActorTransform().Equals(ThroatPose));
        TestTrue(TEXT("Disposal retains transform, footprint, and disabled legacy zone"),Exit->GetActorTransform().Equals(ExitPose)&&Exit->Volume->GetUnscaledBoxExtent().Equals(Extent)&&Exit->bBrushBin&&!Exit->bEnableLegacyDeliveryZone);
        TestTrue(TEXT("Endpoint-only repair preserves the healthy original food"),!Source->IsDisposed()&&T.Recovery->GetReplacementCount()==0&&T.Live(Source->Batch).Num()==1);
        TestEqual(TEXT("Endpoint repair preserves completed swallowed count"),Throat->FoodSwallowed,7);TestEqual(TEXT("Endpoint repair preserves completed meal count"),Throat->MealSequence,11);TestEqual(TEXT("Endpoint repair does not score"),T.Player->Points,17);
        TestTrue(TEXT("Endpoint repair does not clone active meal phase, weight or queued counts"),Throat->ThroatPhase==EMCThroatPhase::Collecting&&Throat->Weight==0&&Throat->FoodInZone==0&&Throat->QueuedFoodCount==0);
        return true;
    }
    if(Case==TEXT("CurrentMenuProcessingCoverage"))
    {
        FWorld T;auto* Menu=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
        if(!TestNotNull(TEXT("Current saved menu is available"),Menu))return false;
        // Match admission's resident-resource contract before testing variants;
        // a soft row reference alone does not mean its fallback is loaded.
        TArray<TStrongObjectPtr<UStaticMesh>> Resident;TArray<UStaticMesh*> ToCompile;
        for(const auto& Pair:Menu->GetRowMap())
        {
            const auto& Row=*reinterpret_cast<const FMCFoodRow*>(Pair.Value);if(Row.Kind!=EMCFoodKind::Food)continue;
            for(const auto* Variants:{&Row.WholeMeshes,&Row.FragmentMeshes})for(const auto& Ref:*Variants)if(!Ref.IsNull())
                if(auto* Mesh=Ref.LoadSynchronous()){Resident.Emplace(Mesh);ToCompile.AddUnique(Mesh);}
        }
#if WITH_EDITOR
        FStaticMeshCompilingManager::Get().FinishCompilation(ToCompile);
#endif
        int32 Whole=0,Fragments=0,Carrots=0;int32 Batch=919720;
        for(const auto& Pair:Menu->GetRowMap())
        {
            const auto& Row=*reinterpret_cast<const FMCFoodRow*>(Pair.Value);if(Row.Kind!=EMCFoodKind::Food)continue;
            for(bool Fragment:{false,true})for(const auto& Ref:Fragment?Row.FragmentMeshes:Row.WholeMeshes)
            {
                if(Ref.IsNull())continue;auto* Mesh=Ref.LoadSynchronous();
                if(!TestNotNull(*FString::Printf(TEXT("Saved menu mesh %s"),*Ref.ToString()),Mesh))return false;
                auto* Food=T.Spawn(Row,Pair.Key,++Batch,Fragment,Mesh,true);
                if(!TestNotNull(TEXT("Current authored variant spawns"),Food))return false;
                const FString Label=FString::Printf(TEXT("%s %s %s"),*Pair.Key.ToString(),Fragment?TEXT("fragment"):TEXT("whole"),*Mesh->GetName());
                TestTrue(*FString::Printf(TEXT("Resident admission accepts %s with both real endpoints"),*Label),UMCFoodProcessingRecoverySubsystem::CanAdmit(Food));
                if(Fragment)
                {
                    ++Fragments;TestTrue(*FString::Printf(TEXT("Authored fragment has readable geometry: %s"),*Label),Mesh->bAllowCPUAccess&&Food->PrepareCutGeometry());
                    TestTrue(*FString::Printf(TEXT("Authored fragment retains the actual automatic planar route: %s"),*Label),Food->UsesAutomaticCutIntake()&&Food->SupportsPlanarCut());
                    double Volume=Food->AutomaticIntakeVolume();
                    if(Volume<=0)
                    {
                        // A disconnected authored fragment legitimately has no
                        // single-piece actor metric until its islands split.
                        TStrongObjectPtr<UProceduralMeshComponent> Scratch(NewObject<UProceduralMeshComponent>(GetTransientPackage(),NAME_None,RF_Transient));
                        MCFoodPreparedCutTests::ImportStatic(Mesh,Scratch.Get());
                        TArray<MCFoodCutGeometry::FIsland> Islands;
                        const bool Measured=MCFoodCutGeometry::FindIslands(Scratch.Get(),Islands,nullptr,256,Food->PortionSourceTransform());
                        TestTrue(*FString::Printf(TEXT("Authored fragment islands are measurable: %s"),*Label),Measured&&!Islands.IsEmpty());
                        Volume=0;for(const auto& Island:Islands)Volume+=Island.Volume;
                    }
                    TestTrue(*FString::Printf(TEXT("Authored fragment has finite positive physical volume: %s"),*Label),FMath::IsFinite(Volume)&&Volume>0);
                    if(Pair.Key==TEXT("Carrot")&&(Mesh->GetName()==TEXT("SM_CarrotSliceC")||Mesh->GetName()==TEXT("SM_CarrotSliceD")))
                    {
                        ++Carrots;const bool Spoiled=Mesh->GetName()==TEXT("SM_CarrotSliceD");Food->bSpoiled=Spoiled;
                        TestTrue(TEXT("Saved Carrot C/D uses the authored fragment scale"),Food->FoodData.FragmentScale.Equals(Row.FragmentScale));
                        TestTrue(TEXT("Saved Carrot C/D qualifies by measured 110-cubed volume"),Food->FitsAutomaticIntakePortion());
                        const int32 Swallowed=T.Throat->FoodSwallowed,Meal=T.Throat->MealSequence;
                        TestTrue(TEXT("Saved Carrot C/D is processed through actual HitFood"),Food->HitFood(1,FVector::ForwardVector,T.Hero,Food->Visual->Bounds.Origin,FVector::RightVector));
                        TestTrue(TEXT("Saved Carrot C/D takes its correct fresh/spoiled path"),Food->IsDisposed()&&(Spoiled?Food->WasAutomaticallyDiscarded():Food->WasAutomaticallyConsumed()));
                        TestEqual(TEXT("Only fresh carrot feeds once"),T.Throat->FoodSwallowed,Swallowed+(Spoiled?0:1));
                        TestEqual(TEXT("Only the fresh carrot advances its accepted meal sequence once"),T.Throat->MealSequence,Meal+(Spoiled?0:1));
                        TestFalse(TEXT("Disposed carrot cannot be processed twice"),Food->HitFood(1,FVector::ForwardVector,T.Hero,Food->Visual->Bounds.Origin,FVector::RightVector));
                    }
                }
                else ++Whole;
                Food->Destroy();
            }
        }
        AddInfo(FString::Printf(TEXT("Current saved menu coverage: %d whole variants, %d fragment variants, %d fixed carrot variants"),Whole,Fragments,Carrots));
        TestTrue(TEXT("Coverage includes current whole and fragment variants"),Whole>0&&Fragments>0);TestEqual(TEXT("Both fixed saved carrot variants were exercised"),Carrots,2);return true;
    }
    AddError(TEXT("Unknown food recovery case"));return false;
}
#endif
