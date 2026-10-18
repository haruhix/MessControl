#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCCataclysmEvent.h"
#include "MCCataclysmJelly.h"
#include "MCCoffeeWipe.h"
#include "MCDayPlan.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCMouthSurface.h"
#include "MCPlaytestBotController.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCataclysmJellySplitTest,"MessControl.Cataclysm.JellySplitPaintAndPush",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCataclysmJellySplitTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL;URL.AddOption(TEXT("game=/Script/MessControl.MCGameMode"));World->SetGameMode(URL);
    auto* Mode=World->GetAuthGameMode<AMCGameMode>();
    Mode->bUseDayOnePlan=false;Mode->bUseAdaptiveDirector=false;
    World->InitializeActorsForPlay(URL);World->BeginPlay();
    World->GetGameState<AMCGameState>()->Phase=EMCShiftPhase::Working;
    const auto Advance=[&](int32 Steps) {for(int32 Index=0;Index<Steps;++Index) {++GFrameCounter;World->Tick(LEVELTICK_All,.05f);}};
    auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
#if WITH_EDITOR
    if(Plane) FStaticMeshCompilingManager::Get().FinishCompilation({Plane});
#endif
    const FTransform Ground(FQuat::Identity,FVector::ZeroVector,FVector(40,40,1));
    auto* Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Ground);
    Tongue->SourceMesh=Plane;Tongue->FinishSpawning(Ground);Tongue->SetActorTickEnabled(false);
    auto* Event=World->SpawnActor<AMCCataclysmEvent>();
    Event->MaxPaintTiles=4;
    TestTrue(TEXT("The real event owns the tongue fixture"),Event->Start(NewObject<UMCDayPlan>(Event),EMCCataclysmVariant::Jellies,99271));
    Event->Phase=EMCCataclysmPhase::JellyFight;
    // Wave residue shares cleanup Batch, but not the finite trail-canvas budget.
    // Even matching event ownership must not misclassify its non-trail tiles.
    TArray<uint8> Full;Full.Init(255,FMCCoffeeWipe::Count);
    for(int32 I=0;I<Event->MaxPaintTiles;++I) {
        const FTransform ResiduePose(FVector(-600+I*150,-1000,5));
        auto* Residue=World->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),ResiduePose,Event);
        Residue->Batch=Event->DirtBatch;Residue->bRandomizeLiquidSize=false;Residue->LiquidHalfSize=60;
        Residue->Tags.Add(TEXT("MCCoffeeWaveResidue"));Residue->InitializePaintTile(Tongue);Residue->FinishSpawning(ResiduePose);
        TestTrue(TEXT("The same-batch residue has real wet coverage"),Residue->AppendLiquidDepositMask(Full));
    }
    for(float X:{-800.f,900.f}) for(float Y:{-1000.f,1000.f}) {
        const FVector Point(X,Y,0);
        Event->RegisterPaintStroke(Point,Point,40.f);
    }
    TestEqual(TEXT("Wave residue does not consume any of the four trail canvases"),Event->PaintTilesCreated,Event->MaxPaintTiles);
    AMCMouthSurface* RetiredTile=nullptr;
    const FVector ReplacementPoint(900,1000,0);
    for(TActorIterator<AMCMouthSurface> It(World);It;++It)
        if(It->GetOwner()==Event && It->ActorHasTag(TEXT("MCCataclysmPaintTile")) && It->IsPaintWetAt(ReplacementPoint)) RetiredTile=*It;
    TestNotNull(TEXT("The bounded canvas at the replacement point exists"),RetiredTile);
    if(RetiredTile) {
        RetiredTile->Destroy();
        Event->RegisterPaintStroke(ReplacementPoint,ReplacementPoint,40.f);
        TestEqual(TEXT("A retired trail canvas releases its slot"),Event->PaintTilesCreated,Event->MaxPaintTiles+1);
    }
    const FTransform Pose(FVector(300,0,94));
    auto* Large=World->SpawnActorDeferred<AMCCataclysmJelly>(AMCCataclysmJelly::StaticClass(),Pose,Event);
    Large->ConfigureJelly(Tongue,360,EMCCataclysmJellySize::Large);
    Large->OnDirtyPatchRequested.AddUObject(Event,&AMCCataclysmEvent::RegisterDirtyPatch);
    Event->Jellies.Add(Large);Large->FinishSpawning(Pose);
    Advance(30);
    TestEqual(TEXT("Cleanup still counts both wave and trail tiles"),Event->PaintTileCount,Event->MaxPaintTiles*2);
    TestTrue(TEXT("A live large jelly deposits connected paint"),Large->PaintStrokeRequests>0 && Event->PaintStrokesAccepted>0);
    const auto KillAndSplit=[&](AMCCataclysmJelly* Jelly,int32 ExpectedChildren)
    {
        const int32 Before=Event->Jellies.Num();
        TestTrue(TEXT("Ordinary damage is accepted"),Jelly->ReceiveToolDamage(10000,nullptr)>0);
        TestTrue(TEXT("A lethal hit starts the visible rupture cue"),Jelly->bPendingRupture);
        Advance(14);
        TestEqual(TEXT("Exactly one rupture"),Jelly->RuptureCount,1);
        TestEqual(TEXT("Child count matches tier"),Jelly->ChildrenSpawned,ExpectedChildren);
        TestEqual(TEXT("Every child joins the event ledger"),Event->Jellies.Num()-Before,ExpectedChildren);
    };
    KillAndSplit(Large,3);
    TArray<AMCCataclysmJelly*> Medium;
    for(const auto& Entry:Event->Jellies) if(IsValid(Entry) && Entry->JellySize==EMCCataclysmJellySize::Medium) Medium.Add(Entry);
    TestEqual(TEXT("One large creates three medium"),Medium.Num(),3);
    Advance(24);
    for(auto* Jelly:Medium)
    {
        TestTrue(TEXT("Medium painters retain real trails"),Jelly->PaintStrokeRequests>0);
        TestTrue(TEXT("Medium body is smaller than large"),Jelly->Settings.BodyRadius<90.f);
        KillAndSplit(Jelly,3);
    }
    TArray<AMCCataclysmJelly*> Small;
    for(const auto& Entry:Event->Jellies) if(IsValid(Entry) && Entry->JellySize==EMCCataclysmJellySize::Small) Small.Add(Entry);
    TestEqual(TEXT("Three medium create nine small"),Small.Num(),9);
    Advance(24);
    if(!Small.IsEmpty())
    {
        auto* Jelly=Small[0];
        TestTrue(TEXT("Small painters retain real trails"),Jelly->PaintStrokeRequests>0);
        auto* Hero=World->SpawnActor<AMCToothCharacter>(Jelly->GetActorLocation()+FVector(85,0,44),FRotator::ZeroRotator);
        auto* Controller=World->SpawnActor<AMCPlaytestBotController>();
        Controller->SetActorTickEnabled(false);Controller->Possess(Hero);Hero->SetActorTickEnabled(false);
        Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        const EMCBodyState BodyBefore=Hero->ToothPhysics->GetBodyState();
        const int32 KnockdownsBefore=Hero->ToothPhysics->KnockdownCount;
        // Hold the observer beside the pop. The test leaves the actual native
        // rupture, coverage deposit and impulse calculation in control.
        Jelly->ReceiveToolDamage(10000,nullptr);
        Hero->SetActorLocation(Jelly->GetActorLocation()+FVector(85,0,44),false);
        Advance(14);
        TestEqual(TEXT("Small rupture creates no children"),Jelly->ChildrenSpawned,0);
        TestTrue(TEXT("Small rupture pushes the nearby identified player"),Jelly->RupturePlayersPushed>0);
        TestEqual(TEXT("The pop never starts a knockdown"),Hero->ToothPhysics->KnockdownCount,KnockdownsBefore);
        TestEqual(TEXT("The pop preserves body state"),Hero->ToothPhysics->GetBodyState(),BodyBefore);
        TestTrue(TEXT("All tier deaths request a persistent coffee splash"),Event->DeathPaintBursts>=5);
    }
    World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    return true;
}
#endif
