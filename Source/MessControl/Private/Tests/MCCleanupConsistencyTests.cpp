#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCGameDirector.h"
#include "MCDayDirector.h"
#include "MCFoodDirectorHooks.h"
#include "MCMouthSurface.h"
#include "MCCoffeeWipe.h"
#include "MCArenaTooth.h"
#include "MCTongue.h"
#include "MCDirtPlacement.h"
#include "Components/BoxComponent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCBrushContactComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCCleanupConsistencyTests
{
struct FFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCGameState* State=nullptr;
    AMCTongue* Tongue=nullptr;
    FFixture()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/MessControl.MCGameMode"));World->SetGameMode(URL);
        auto* Mode=World->GetAuthGameMode<AMCGameMode>();
        Mode->bUseSingleDayLoop=false;Mode->bUseDayOnePlan=false;Mode->bUseAdaptiveDirector=false;
        World->InitializeActorsForPlay(URL);World->BeginPlay();Mode->SetActorTickEnabled(false);
        State=World->GetGameState<AMCGameState>();State->Phase=EMCShiftPhase::Working;State->bPhysicalBrushes=false;
        auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
#if WITH_EDITOR
        if(Plane) FStaticMeshCompilingManager::Get().FinishCompilation({Plane});
#endif
        const FTransform Ground(FQuat::Identity,FVector::ZeroVector,FVector(4,4,1));
        Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Ground);
        Tongue->SourceMesh=Plane;Tongue->Settings.IdleHeight=0;Tongue->bAutomaticYawns=false;Tongue->FinishSpawning(Ground);
        Tongue->SetActorTickEnabled(false);
    }
    ~FFixture() {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    AMCToothCharacter* Worker(FVector Location)
    {
        auto* Hero=World->SpawnActor<AMCToothCharacter>(Location,FRotator::ZeroRotator);
        Hero->SetActorTickEnabled(false);Hero->GetCharacterMovement()->DisableMovement();Hero->bBrushing=true;
        return Hero;
    }
    struct FBrushResult
    {
        int32 Steps=0,AcquiredContacts=0,AcceptedCalls=0,ChangedMasks=0,ReadyContacts=0;
        float MaxAlpha=0;
    };
    FBrushResult Brush(AMCMouthSurface* Patch,AMCToothCharacter* Hero)
    {
        FBrushResult Result;
        for(int32 I=0;I<300 && !Patch->IsClean();++I) {
            FVector Point,Normal;
            if(Patch->FindDirtyContact(Hero,Point,Normal)) {
                ++Result.AcquiredContacts;
                const FVector Facing=(Point-Hero->GetActorLocation()).GetSafeNormal2D();
                if(!Facing.IsNearlyZero()) Hero->SetActorRotation(Facing.Rotation());
            }
            const auto Before=Patch->WipeMask;
            ++GFrameCounter;World->Tick(LEVELTICK_TimeOnly,.1f);
            Result.AcceptedCalls+=Patch->BrushLiquid(Hero,.1f);
            // TimeOnly deliberately keeps directors/physics out of this isolated
            // fixture, but also skips component ticks. Advance the real contact
            // component after work, as it does after the pawn's gameplay tick.
            // Otherwise Alpha stays zero and target lookup never follows the
            // acquired contact; opposite dirty rims keep resetting its approach.
            Hero->BrushContact->TickComponent(.1f,LEVELTICK_All,nullptr);
            Result.ReadyContacts+=Hero->BrushContact->IsWorkReady();
            Result.ChangedMasks+=Before!=Patch->WipeMask;
            Result.MaxAlpha=FMath::Max(Result.MaxAlpha,Hero->BrushContact->Alpha());
            ++Result.Steps;
        }
        return Result;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCleanupSupportedPaintTest,"MessControl.Cleanup.OverhangingWavePaintHasNoInvisibleDebt",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCleanupSupportedPaintTest::RunTest(const FString&)
{
    MCCleanupConsistencyTests::FFixture F;
    constexpr int32 Batch=43121;
    // This canvas overlaps the outer tissue edge and the stricter central work
    // boundary. Neither inaccessible region may contribute cleanup debt.
    const FTransform Pose(FVector(120,0,5));
    // Face the remaining central coverage from its open side; the old observer
    // position would put that newly clipped coverage behind a forward-facing brush.
    const FVector WorkerStart(-40,0,80);
    auto* Tile=F.World->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),Pose);
    Tile->bRandomizeLiquidSize=false;Tile->LiquidHalfSize=100;Tile->Batch=Batch;
    Tile->Tags.Add(TEXT("MCFoodServedDirt"));Tile->InitializePaintTile(F.Tongue);Tile->FinishSpawning(Pose);
    TArray<uint8> Full;Full.Init(255,FMCCoffeeWipe::Count);
    TestTrue(TEXT("A wave deposits its supported portion"),Tile->AppendLiquidDepositMask(Full));
    int32 Wet=0,Outside=0;float MaxInitialReach=0;
    for(int32 Y=0;Y<FMCCoffeeWipe::Size;++Y) for(int32 X=0;X<FMCCoffeeWipe::Size;++X) {
        if(Tile->WipeMask[Y*FMCCoffeeWipe::Size+X]<=63) continue;
        ++Wet;FVector Point;
        const FVector Probe=Pose.TransformPosition(FVector(((X+.5)/FMCCoffeeWipe::Size-.5)*200,((Y+.5)/FMCCoffeeWipe::Size-.5)*200,0));
        MaxInitialReach=FMath::Max(MaxInitialReach,float(FVector::Dist2D(Probe,WorkerStart)));
        if(!F.Tongue->RestSurfacePoint(Probe,Point) || !MCDirtPlacement::IsInWorkArea(F.Tongue,Probe,65.f)) ++Outside;
    }
    TestTrue(TEXT("Real visible residue remains instead of skipping the task"),Wet>0 && Wet<FMCCoffeeWipe::Count);
    TestEqual(TEXT("No dirty byte belongs outside rendered tissue or the accessible central work area"),Outside,0);
    TestEqual(TEXT("The first-meal gate counts the supported residue"),MCCountDirectedFoodDirt(F.World,Batch),1);
    auto* Hero=F.Worker(WorkerStart);
    TestTrue(TEXT("Every supported cell is inside the fixture worker's surface reach"),MaxInitialReach<Hero->BrushContact->SurfaceReach);
    const auto Before=Tile->WipeMask;
    Tile->BrushLiquid(Hero,.1f);
    TestTrue(TEXT("The regular brush still needs approach time"),Before==Tile->WipeMask);
    TestTrue(TEXT("The fixture worker can actually work"),Hero->CanWork());
    FVector FirstContact,FirstNormal;
    TestTrue(TEXT("The supported residue has a reachable first brush contact"),Tile->FindDirtyContact(Hero,FirstContact,FirstNormal));
    const auto Brush=F.Brush(Tile,Hero);
    int32 RemainingWet=0;float MaxRemainingDistance=0;
    for(int32 Y=0;Y<FMCCoffeeWipe::Size;++Y) for(int32 X=0;X<FMCCoffeeWipe::Size;++X) {
        if(Tile->WipeMask[Y*FMCCoffeeWipe::Size+X]<=63) continue;
        ++RemainingWet;
        const FVector P=Pose.TransformPosition(FVector(((X+.5)/FMCCoffeeWipe::Size-.5)*200,((Y+.5)/FMCCoffeeWipe::Size-.5)*200,0));
        MaxRemainingDistance=FMath::Max(MaxRemainingDistance,float(FVector::Dist2D(P,Hero->GetActorLocation())));
    }
    AddInfo(FString::Printf(TEXT("Brush fixture: initialWet=%d outside=%d steps=%d acquired=%d accepted=%d changed=%d ready=%d maxAlpha=%.2f remainingWet=%d remainingFraction=%.6f maxInitialReach=%.2f maxRemainingReach=%.2f/%.2f"),
        Wet,Outside,Brush.Steps,Brush.AcquiredContacts,Brush.AcceptedCalls,Brush.ChangedMasks,Brush.ReadyContacts,Brush.MaxAlpha,
        RemainingWet,Tile->RemainingLiquid(),MaxInitialReach,MaxRemainingDistance,Hero->BrushContact->SurfaceReach));
    TestTrue(TEXT("The actual contact component completes its approach"),Brush.MaxAlpha>.99f && Brush.ReadyContacts>0);
    TestTrue(TEXT("Real brush work changed the deposit mask"),Brush.ChangedMasks>0);
    TestTrue(TEXT("Actual bristle contacts finish the whole supported residue"),Tile->IsClean());
    TestEqual(TEXT("Completing that residue releases the actual first-meal dirt gate"),MCCountDirectedFoodDirt(F.World,Batch),0);
    TestFalse(TEXT("A passed wave cannot fill the cleaned tile again"),Tile->AppendLiquidDepositMask(Full));
    TestTrue(TEXT("An old source leaves completion intact"),Tile->IsClean());
    TestFalse(TEXT("A slime cannot deposit an invisible stroke outside tissue"),Tile->DepositPaintStroke(FVector(300,-40,0),FVector(300,40,0),20));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCleanupLegacySparseResidueTest,"MessControl.Cleanup.LegacySparseResidueRemainsBrushable",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCleanupLegacySparseResidueTest::RunTest(const FString&)
{
    MCCleanupConsistencyTests::FFixture F;
    constexpr int32 Batch=43122;
    const FTransform Pose(FVector(0,0,5));
    auto* Patch=F.World->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),Pose);
    Patch->bRandomizeLiquidSize=false;Patch->LiquidHalfSize=100;Patch->Batch=Batch;
    Patch->Tags.Add(TEXT("MCFoodServedDirt"));Patch->FinishSpawning(Pose);
    Patch->Status->ApplyCoffee(1.f);
    // A sparse wiped edge can still exceed the ordinary completion allowance.
    // Keep real wet coverage on even columns only: the legacy lookup sampled
    // odd X/odd Y, although its completion mask counts every supported cell.
    Patch->WipeMask.Init(0,FMCCoffeeWipe::Count);
    const TArray<uint8> Full;
    for(int32 Y=24;Y<40;++Y) for(int32 X=24;X<40;X+=2) {
        const FVector2D UV((X+.5)/FMCCoffeeWipe::Size,(Y+.5)/FMCCoffeeWipe::Size);
        if(FMCCoffeeWipe::WetAt(Full,UV,Patch->LiquidSeed)) Patch->WipeMask[Y*FMCCoffeeWipe::Size+X]=255;
    }
    const float Remaining=Patch->RemainingLiquid();
    TestTrue(TEXT("The real residue exceeds the normal completion allowance"),Remaining>.025f && Remaining<.4f);
    TestEqual(TEXT("That residue holds the first-meal dirt gate"),MCCountDirectedFoodDirt(F.World,Batch),1);
    auto* Hero=F.Worker(FVector(-50,0,80));
    FVector Point,Normal;
    TestTrue(TEXT("The real brush can acquire residue missed by every coarse sample"),Patch->FindDirtyContact(Hero,Point,Normal));
    const auto Before=Patch->WipeMask;
    const auto Brush=F.Brush(Patch,Hero);
    TestTrue(TEXT("Actual bristle work reaches and changes the sparse residue"),Brush.ChangedMasks>0 && Patch->WipeMask!=Before);
    TestTrue(TEXT("The regular cleanup completion path clears the legacy care state"),Patch->IsClean());
    TestEqual(TEXT("Cleaning that reachable residue releases the first-meal gate"),MCCountDirectedFoodDirt(F.World,Batch),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCleanupCentralPlacementTest,"MessControl.Cleanup.CentralDirtRejectsRimsAndStaticCover",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCleanupCentralPlacementTest::RunTest(const FString&)
{
    MCCleanupConsistencyTests::FFixture F;
    FHitResult Floor;
    TestTrue(TEXT("Open central tissue accepts a small real stain and brush approach"),
        MCDirtPlacement::FindSurface(F.Tongue,FVector::ZeroVector,20.f,Floor));
    TestFalse(TEXT("Visible tissue near the side still rejects risky dirt placement"),
        MCDirtPlacement::FindSurface(F.Tongue,FVector(0,160,0),20.f,Floor));
    auto* Cover=F.World->SpawnActor<AActor>();
    auto* Collision=NewObject<UBoxComponent>(Cover);
    Cover->SetRootComponent(Collision);
    Collision->SetBoxExtent(FVector(30,30,100));
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Collision->SetCollisionObjectType(ECC_WorldStatic);
    Collision->SetCollisionResponseToAllChannels(ECR_Block);
    Collision->RegisterComponent();Cover->SetActorLocation(FVector(0,0,105));
    TestFalse(TEXT("A real static covering mesh blocks otherwise central dirt"),
        MCDirtPlacement::FindSurface(F.Tongue,FVector::ZeroVector,20.f,Floor));
    Cover->Destroy();
    TestTrue(TEXT("The same central patch is usable again after its cover is removed"),
        MCDirtPlacement::FindSurface(F.Tongue,FVector::ZeroVector,20.f,Floor));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCleanupBothSurfaceKindsTest,"MessControl.Cleanup.BothStainKindsAndTwelveTeethAreObserved",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCleanupBothSurfaceKindsTest::RunTest(const FString&)
{
    MCCleanupConsistencyTests::FFixture F;
    const FTransform Pose(FVector(0,0,5));
    auto* Tile=F.World->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),Pose);
    Tile->bRandomizeLiquidSize=false;Tile->LiquidHalfSize=100;Tile->InitializePaintTile(F.Tongue);Tile->FinishSpawning(Pose);
    Tile->DepositPaintStroke(FVector(-20,0,0),FVector(20,0,0),25);Tile->FlushPaintDeposit();
    auto* Blob=F.World->SpawnActor<AMCMouthSurface>(FVector(-140,-100,5),FRotator::ZeroRotator);
    Blob->Status->ApplyCoffee();
    auto* Director=F.World->SpawnActor<AMCGameDirector>();
    auto* Legacy=F.World->SpawnActor<AMCDayDirector>();
    TestEqual(TEXT("Both source types create cleanup tasks"),Director->Observe().CleaningTasks,2);
    // Deliberately skew the compatibility care counter: the tile's actual mask,
    // used by IsClean/first meal, must remain the cleanup source of truth.
    Tile->Status->ClearCoffeeForBooster();
    TestEqual(TEXT("Legacy cleanup also observes a dirty paint mask"),Legacy->CountDirt(),2);
    for(int32 Id=1;Id<=12;++Id) {
        auto* Tooth=F.World->SpawnActor<AMCArenaTooth>(FVector(600+Id*200,1000,200),FRotator::ZeroRotator);
        Tooth->Initialize(Id,FMCArenaToothSettings());F.State->ArenaTeeth.Add(Tooth);
        Tooth->SetCoffee(Id>8?1.f:0.f);
    }
    TestEqual(TEXT("All twelve physical teeth participate; the four outer teeth are not capped by HUD slots"),Director->Observe().CleaningTasks,6);
    Tile->ClearLiquidForBooster();Blob->ClearLiquidForBooster();
    TestEqual(TEXT("The regular Director still observes four dirty outer teeth"),Director->Observe().CleaningTasks,4);
    TestEqual(TEXT("Legacy cleanup uses the same remaining outer teeth"),Legacy->CountDirt(),4);
    return true;
}
#endif
