#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCCoffeeWipe.h"
#include "MCPaintVisibility.h"
#include "MCMouthSurface.h"
#include "MCToothStatusComponent.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "MCBrushContactComponent.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCTongue.h"
#include "MCCataclysmEvent.h"
#include "MCDayPlan.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include <limits>
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCoffeeMaskTest,"MessControl.Liquid.PersistentLocalWipe",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCoffeeMaskTest::RunTest(const FString&)
{
    TArray<uint8> Mask; FMCCoffeeWipe::Reset(Mask);
    TestTrue(TEXT("Valid stroke removes local liquid"),FMCCoffeeWipe::Stroke(Mask,{.25f,.25f},{.5f,.25f},.12f,.1f));
    TestTrue(TEXT("Bristle path cleared"),Mask[(FMCCoffeeWipe::Size/4)*FMCCoffeeWipe::Size+FMCCoffeeWipe::Size*3/8]<255);
    TestEqual(TEXT("Opposite side remains wet"),Mask[(FMCCoffeeWipe::Size*3/4)*FMCCoffeeWipe::Size+FMCCoffeeWipe::Size*3/4],uint8(255));
    const auto Before=Mask;
    TestFalse(TEXT("Distant stroke ignored"),FMCCoffeeWipe::Stroke(Mask,{-4,-4},{-3,-3},.1f,.1f));
    TestFalse(TEXT("NaN time ignored"),FMCCoffeeWipe::Stroke(Mask,{.5f,.5f},{.6f,.5f},.1f,std::numeric_limits<float>::quiet_NaN()));
    TestFalse(TEXT("Negative time ignored"),FMCCoffeeWipe::Stroke(Mask,{.5f,.5f},{.6f,.5f},.1f,-1));
    TestTrue(TEXT("Invalid input preserves previous tracks"),Mask==Before);
    FMCCoffeeWipe::Stroke(Mask,{.7f,.6f},{.7f,.8f},.12f,.1f);
    TestEqual(TEXT("Second worker cannot refill first track"),Mask[(FMCCoffeeWipe::Size/4)*FMCCoffeeWipe::Size+FMCCoffeeWipe::Size*3/8],Before[(FMCCoffeeWipe::Size/4)*FMCCoffeeWipe::Size+FMCCoffeeWipe::Size*3/8]);
    auto Clamped=Before,Huge=Before;
    FMCCoffeeWipe::Stroke(Clamped,{.5f,.5f},{.6f,.5f},.12f,.1f);
    FMCCoffeeWipe::Stroke(Huge,{.5f,.5f},{.6f,.5f},.12f,1000);
    TestTrue(TEXT("Large time step is bounded"),Huge==Clamped);
    FMCCoffeeWipe::Reset(Mask); TestEqual(TEXT("Fresh spill restores liquid"),FMCCoffeeWipe::Remaining(Mask),1.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPaintResidueVisibilityTest,"MessControl.Liquid.PaintResidueVisibilityKeepsStrictCleanup",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPaintResidueVisibilityTest::RunTest(const FString&)
{
    const uint8 Values[]={0,63,64,127,128,255};
    const uint8 Expected[]={0,0,128,128,128,255};
    for(int32 I=0;I<UE_ARRAY_COUNT(Values);++I) {
        TestEqual(*FString::Printf(TEXT("Paint display density at byte %u"),Values[I]),
            MCPaintVisibility::UploadByte(Values[I],true),Expected[I]);
    }
    for(int32 Value=0;Value<=255;++Value)
        TestEqual(TEXT("Ordinary seeded puddles keep every authored coverage byte"),
            MCPaintVisibility::UploadByte(uint8(Value),false),uint8(Value));

    TArray<uint8> Raw;Raw.Init(0,FMCCoffeeWipe::Count);
    constexpr int32 X=FMCCoffeeWipe::Size/2,Y=FMCCoffeeWipe::Size/2;
    constexpr int32 Center=Y*FMCCoffeeWipe::Size+X;
    Raw[Center]=64;
    Raw[0]=63; // A previously wiped cell must not be lifted with the live residue.
    const auto Before=Raw;
    TArray<uint8> Display;Display.Reserve(Raw.Num());
    for(uint8 Value:Raw) Display.Add(MCPaintVisibility::UploadByte(Value,true));
    TestTrue(TEXT("Preparing visual pixels preserves the authoritative mask byte-for-byte"),Raw==Before);
    TestTrue(TEXT("The weak cell still requires real brush work"),FMCCoffeeWipe::HasWetCells(Raw));
    TestEqual(TEXT("A cleared cell stays absent from the visual mask"),Display[0],uint8(0));

    // At one quarter texel along both axes, bilinear sampling gives the lone
    // center cell a .75*.75 weight. Test the material's actual .25 clip contract,
    // rather than claiming that a center-only maximum is readable in gameplay.
    const float OriginalCoverage=Raw[Center]/255.f*.75f*.75f;
    const float DisplayCoverage=Display[Center]/255.f*.75f*.75f;
    TestTrue(TEXT("A raw byte64 collapses below the clip within the same texel"),OriginalCoverage<.25f);
    TestTrue(TEXT("The displayed residue has a finite two-dimensional footprint above clip"),DisplayCoverage>.25f);
    const FVector2D Probe((X+.75)/FMCCoffeeWipe::Size,(Y+.75)/FMCCoffeeWipe::Size);
    TestTrue(TEXT("The existing bilinear mask sampler also sees that display footprint"),
        FMCCoffeeWipe::WetAt(Display,Probe,0,true,64));
    TestFalse(TEXT("The original weak mask remains below clip at that footprint probe"),
        FMCCoffeeWipe::WetAt(Raw,Probe,0,true,64));

    const FVector2D CenterUV((X+.5)/FMCCoffeeWipe::Size,(Y+.5)/FMCCoffeeWipe::Size);
    TestTrue(TEXT("The existing wipe stroke removes the final working residue"),
        FMCCoffeeWipe::Stroke(Raw,CenterUV,CenterUV,.03f,.1f));
    TestFalse(TEXT("Actual mask cleanup releases the final cell"),FMCCoffeeWipe::HasWetCells(Raw));
    TestEqual(TEXT("That completed cell no longer contributes display coverage"),
        MCPaintVisibility::UploadByte(Raw[Center],true),uint8(0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCoffeeContactTest,"MessControl.Liquid.AuthoritativeBrushContact",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCoffeeContactTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL; URL.AddOption(TEXT("game=/Script/MessControl.MCGameMode"));
    World->SetGameMode(URL);
    World->GetAuthGameMode<AMCGameMode>()->bUseDayOnePlan=false;
    World->GetAuthGameMode<AMCGameMode>()->bUseAdaptiveDirector=false;
    World->InitializeActorsForPlay(URL); World->BeginPlay();
    auto* GS=World->GetGameState<AMCGameState>(); GS->Phase=EMCShiftPhase::Working; GS->bPhysicalBrushes=false;
    // A normal-sized stain tests locality after a sustained contact. The old
    // 92cm half-size was almost covered by the widened60cm brush in one pass.
    const FTransform PatchPose(FVector(300,0,5));
    auto* Patch=World->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),PatchPose);
    Patch->bRandomizeLiquidSize=false;Patch->LiquidHalfSize=240;
    Patch->FinishSpawning(PatchPose);Patch->Status->ApplyCoffee();
    auto* A=World->SpawnActor<AMCToothCharacter>(FVector(235,0,80),FRotator::ZeroRotator);
    auto* B=World->SpawnActor<AMCToothCharacter>(FVector(365,0,80),FRotator(0,180,0));
    for (auto* Worker:{A,B}) { Worker->SetActorTickEnabled(false); Worker->GetCharacterMovement()->DisableMovement(); Worker->bBrushing=true; }
    A->AdvanceCare(.1f);
    TestEqual(TEXT("Approaching brush cannot erase liquid immediately"),FMCCoffeeWipe::Remaining(Patch->WipeMask),1.f);
    // Brush acquisition uses server time, including on a server without rendering.
    for (int32 I=0;I<8;++I) { ++GFrameCounter; World->Tick(LEVELTICK_TimeOnly,.1f); A->AdvanceCare(.1f); }
    TestTrue(TEXT("Held brush makes a persistent local trail"),FMCCoffeeWipe::Remaining(Patch->WipeMask)<.99f);
    TestTrue(TEXT("A local trail leaves the rest of the stain dirty"),Patch->Status->State.CoffeeLeft>0);
    TestTrue(TEXT("One local contact cannot complete the whole normal-sized visible stain"),!Patch->IsClean() && Patch->RemainingLiquid()>=.025f);
    const auto Mask=Patch->WipeMask;
    A->SetActorLocation(FVector(2000,0,80)); A->AdvanceCare(.1f);
    TestTrue(TEXT("Out of reach preserves trail"),Mask==Patch->WipeMask);
    A->SetActorLocation(FVector(235,0,80)); Patch->Status->ApplyCoffee(); A->ResetContact(); B->ResetContact();
    TestEqual(TEXT("Reapplying same amount resets old trail"),FMCCoffeeWipe::Remaining(Patch->WipeMask),1.f);
    for (int32 I=0;I<240 && !Patch->IsClean();++I) {
        // Disabled pawn ticks do not perform the ordinary body turn, and the
        // TimeOnly fixture skips contact-component ticks. Follow the same
        // proven fixture policy as MCCleanupConsistency without bypassing
        // approach time, reach, visibility or the real mask-completion gate.
        for(auto* Worker:{A,B}) {
            FVector Point,Normal;
            if(Patch->FindDirtyContact(Worker,Point,Normal)) {
                const FVector Facing=(Point-Worker->GetActorLocation()).GetSafeNormal2D();
                if(!Facing.IsNearlyZero()) Worker->SetActorRotation(Facing.Rotation());
            }
        }
        ++GFrameCounter;World->Tick(LEVELTICK_TimeOnly,.1f);
        for(auto* Worker:{A,B}) {
            Worker->AdvanceCare(.1f);
            Worker->BrushContact->TickComponent(.1f,LEVELTICK_All,nullptr);
        }
    }
    TestTrue(TEXT("Two stationary workers seek and clean the remaining visible dirt"),Patch->IsClean());
    TestTrue(TEXT("Completion follows actual visible coverage"),Patch->RemainingLiquid()<.025f);
    Patch->Status->ApplyCoffee(); GS->bPhysicalBrushes=true; A->Inventory->ServerSelect(EMCToolSlot::Pickaxe); A->bBrushing=true;
    Patch->BrushLiquid(A,.1f);
    TestEqual(TEXT("A non-cleaning slot cannot erase liquid"),FMCCoffeeWipe::Remaining(Patch->WipeMask),1.f);
    A->Inventory->ServerSelect(EMCToolSlot::Brush);
    TestTrue(TEXT("Returning to slot one restores the brush without a physical pickup"),A->HasBrush());
    A->bBrushing=true; Patch->bUlcer=true; Patch->BrushLiquid(A,.1f);
    TestEqual(TEXT("Ulcers are never wiped by the liquid path"),FMCCoffeeWipe::Remaining(Patch->WipeMask),1.f);
    World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPaintTileMaskTest,"MessControl.Liquid.AdditivePaintTile",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPaintTileMaskTest::RunTest(const FString&)
{
    TArray<uint8> Mask,Deposited;
    // A path across a tile reaches its outside edge instead of a seeded blob.
    TestTrue(TEXT("A crossing pen stroke deposits paint"),FMCCoffeeWipe::Deposit(Mask,Deposited,{-.1,.25},{1.1,.25},.038f));
    for(int32 X=0;X<FMCCoffeeWipe::Size;++X) {
        const FVector2D UV((X+.5)/FMCCoffeeWipe::Size,16.5/FMCCoffeeWipe::Size);
        if(!TestTrue(TEXT("Every centerline cell is connected, including tile edges"),FMCCoffeeWipe::WetAt(Mask,UV,3,true,63))) return false;
    }
    TestFalse(TEXT("A pen line does not fill untouched floor"),FMCCoffeeWipe::WetAt(Mask,{.5,.5},3,true,63));
    for(int32 I=0;I<30;++I) FMCCoffeeWipe::Stroke(Mask,{.2,.25},{.35,.25},.065f,.1f);
    const auto Cleaned=Mask;
    TestFalse(TEXT("Actual local wipe makes the old pen path dry"),FMCCoffeeWipe::WetAt(Mask,{.275,.25},3,true,63));
    FMCCoffeeWipe::Deposit(Mask,Deposited,{.7,.6},{.8,.6},.038f);
    const int32 Old=16*FMCCoffeeWipe::Size+17;
    TestEqual(TEXT("New paint does not refill a cleaned unrelated cell"),Mask[Old],Cleaned[Old]);
    FMCCoffeeWipe::Deposit(Mask,Deposited,{.25,.25},{.3,.25},.038f);
    TestTrue(TEXT("Revisiting only the old pen segment adds paint there"),FMCCoffeeWipe::WetAt(Mask,{.275,.25},3,true,63));
    TestFalse(TEXT("The adjacent cleaned path stays dry"),FMCCoffeeWipe::WetAt(Mask,{.21,.25},3,true,63));
    const auto Before=Mask;
    TestFalse(TEXT("Distant deposits change no tile cells"),FMCCoffeeWipe::Deposit(Mask,Deposited,{-3,-3},{-2,-2},.038f));
    TestTrue(TEXT("Rejected deposits preserve old coverage"),Mask==Before);
    // A neighboring tile has the same coverage at the shared world-space seam.
    TArray<uint8> Right,RightDeposit;
    FMCCoffeeWipe::Deposit(Right,RightDeposit,{-.6,.25},{.25,.25},.038f);
    for(int32 Y=0;Y<FMCCoffeeWipe::Size;++Y)
        TestEqual(TEXT("Shared tile edge remains joined"),Mask[Y*FMCCoffeeWipe::Size+63],Right[Y*FMCCoffeeWipe::Size]);
    // A projected death splash must not restore cleaned cells beyond an occluder.
    TArray<uint8> Clipped,ClippedDeposit;
    Clipped.Init(0,FMCCoffeeWipe::Count);ClippedDeposit=Clipped;
    TestTrue(TEXT("Allowed splash coverage is deposited"),FMCCoffeeWipe::Deposit(Clipped,ClippedDeposit,{.1,.5},{.9,.5},.1f,nullptr,
        [](FVector2D UV){return UV.X<.5;}));
    TestTrue(TEXT("The near side receives the spill"),FMCCoffeeWipe::WetAt(Clipped,{.3,.5},0,true,63));
    TestFalse(TEXT("Clipped far-side coverage stays dry"),FMCCoffeeWipe::WetAt(Clipped,{.7,.5},0,true,63));
    const auto ClippedBefore=Clipped;
    TestFalse(TEXT("A fully occluded splash is rejected"),FMCCoffeeWipe::Deposit(Clipped,ClippedDeposit,{.6,.5},{.8,.5},.1f,nullptr,
        [](FVector2D){return false;}));
    TestTrue(TEXT("A rejected splash preserves previous cleaning"),Clipped==ClippedBefore);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPaintTileBrushTest,"MessControl.Liquid.PaintTileRealBrush",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPaintTileBrushTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL;URL.AddOption(TEXT("game=/Script/MessControl.MCGameMode"));World->SetGameMode(URL);
    World->GetAuthGameMode<AMCGameMode>()->bUseDayOnePlan=false;World->GetAuthGameMode<AMCGameMode>()->bUseAdaptiveDirector=false;
    World->InitializeActorsForPlay(URL);World->BeginPlay();
    auto* GS=World->GetGameState<AMCGameState>();GS->Phase=EMCShiftPhase::Working;GS->bPhysicalBrushes=false;
    auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
#if WITH_EDITOR
    if(Plane) FStaticMeshCompilingManager::Get().FinishCompilation({Plane});
#endif
    const FTransform Ground(FQuat::Identity,FVector::ZeroVector,FVector(30,30,1));
    auto* Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Ground);
    Tongue->SourceMesh=Plane;Tongue->FinishSpawning(Ground);Tongue->SetActorTickEnabled(false);
    const FTransform Pose(FRotator::ZeroRotator,FVector(300,0,5));
    auto* Tile=World->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),Pose);
    Tile->bRandomizeLiquidSize=false;Tile->LiquidHalfSize=600;Tile->InitializePaintTile(Tongue);Tile->FinishSpawning(Pose);
    auto* Worker=World->SpawnActor<AMCToothCharacter>(FVector(235,0,80),FRotator::ZeroRotator);
    Worker->SetActorTickEnabled(false);Worker->GetCharacterMovement()->DisableMovement();Worker->bBrushing=true;
    const auto Advance=[&](int32 Steps,bool FollowContact=true) {for(int32 I=0;I<Steps;++I) {
        // This stationary fixture disables the actor tick that normally turns
        // the pawn toward its published brush contact. Follow a real remaining
        // contact here; native reach, approach time and wiping still decide work.
        FVector Point,Normal;
        if(FollowContact && (Tile->FindDirtyContact(Worker,Point,Normal)
            || Tile->GetNearestPaintPoint(Worker->GetActorLocation()+Worker->GetActorForwardVector()*65.f,Point,Normal))) {
            const FVector Direction=(Point-Worker->GetActorLocation()).GetSafeNormal2D();
            if(!Direction.IsNearlyZero()) Worker->SetActorRotation(Direction.Rotation());
        }
        ++GFrameCounter;World->Tick(LEVELTICK_TimeOnly,.1f);Tile->BrushLiquid(Worker,.1f);
    }};
    const auto DiagnoseRemaining=[&](const TCHAR* Stage) {
        if(Tile->IsClean()) return;
        int32 Cells=0;for(uint8 Cell:Tile->WipeMask) Cells+=Cell>63;
        FVector Point,Normal;const bool Found=Tile->FindDirtyContact(Worker,Point,Normal);
        const auto* Contact=Worker->BrushContact.Get();
        AddInfo(FString::Printf(TEXT("Paint fixture %s: wet=%d remaining=%.4f dirtyContact=%d target=%s ready=%d facing=%s point=%s reach=%d"),
            Stage,Cells,Tile->RemainingLiquid(),Found,*GetNameSafe(Contact->Target.Get()),Contact->IsWorkReady(),
            *Worker->GetActorForwardVector().ToString(),*(Found?Point:Contact->ContactPoint()).ToString(),
            Found && Contact->CanReach(Point,Normal,Tile)));
    };
    Tile->DepositPaintStroke(FVector(290,-24,0),FVector(310,24,0),38);Tile->FlushPaintDeposit();
    const float Health=Tile->Status->State.Health;
    const int32 Repair=Tile->Status->State.RepairLeft;
    const auto Initial=Tile->WipeMask;
    Tile->BrushLiquid(Worker,.1f);
    TestTrue(TEXT("A new brush first approaches the paint"),Tile->WipeMask==Initial);
    Advance(15);
    TestTrue(TEXT("The real authoritative brush erases pen coverage"),Tile->WipeMask!=Initial);
    Advance(180);
    DiagnoseRemaining(TEXT("initial stroke"));
    TestTrue(TEXT("Tile completion waits for the last brushable mask cell"),Tile->IsClean() && !FMCCoffeeWipe::HasWetCells(Tile->WipeMask));
    TestFalse(TEXT("Erased tile no longer affects footing"),Tile->AffectsFooting(FVector(300,0,0)));
    const auto Cleaned=Tile->WipeMask;
    // A tiny fresh stroke must remain visible/careable even when historical
    // deposited coverage spans most of this canvas.
    Tile->LiquidDepositMask.Init(255,FMCCoffeeWipe::Count);
    Tile->DepositPaintStroke(FVector(400,-12,0),FVector(400,12,0),25);Tile->FlushPaintDeposit();
    TestTrue(TEXT("Tiny new paint reopens cleanup without a global percentage cutoff"),!Tile->IsClean());
    TestEqual(TEXT("Additive care state preserves health"),Tile->Status->State.Health,Health);
    TestEqual(TEXT("Additive care state preserves repair"),Tile->Status->State.RepairLeft,Repair);
    const int32 Center=32*FMCCoffeeWipe::Size+32;
    TestEqual(TEXT("A new source preserves old brush cleanup"),Tile->WipeMask[Center],Cleaned[Center]);
    Worker->SetActorLocation(FVector(335,0,80));Worker->ResetContact();Advance(180);
    DiagnoseRemaining(TEXT("tiny fresh stroke"));
    TestTrue(TEXT("The real brush also finishes tiny fresh paint"),Tile->IsClean());
    // Restart on the same large canvas after a released contact. The old and
    // new valid bristle points are too far apart to form a traversed stroke.
    Tile->DepositPaintStroke(FVector(300,0,0),FVector(540,0,0),20);Tile->FlushPaintDeposit();
    Worker->SetActorLocation(FVector(235,0,80));Worker->SetActorRotation(FRotator::ZeroRotator);Worker->ResetContact();Advance(8,false);
    const auto BeforeResume=Tile->WipeMask;
    const int32 UntouchedBridge=31*FMCCoffeeWipe::Size+37;
    TestTrue(TEXT("The path between contacts still contains paint"),BeforeResume[UntouchedBridge]>63);
    Worker->SetActorLocation(FVector(455,0,80));Worker->ResetContact();Advance(8,false);
    TestEqual(TEXT("Resuming elsewhere cannot erase an untraversed bridge"),Tile->WipeMask[UntouchedBridge],BeforeResume[UntouchedBridge]);
    // The actual event path must clip both its broad center spill and radial
    // spokes against a wall, while keeping supported nearby paint cleanable.
    auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
    if(Cube) FStaticMeshCompilingManager::Get().FinishCompilation({Cube});
#endif
    auto* Wall=World->SpawnActor<AStaticMeshActor>(FVector(420,0,250),FRotator::ZeroRotator);
    Wall->GetStaticMeshComponent()->SetStaticMesh(Cube);
    Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    Wall->SetActorScale3D(FVector(.15,10,5));
    auto* Event=World->SpawnActor<AMCCataclysmEvent>();
    TestTrue(TEXT("The real event accepts the isolated tongue fixture"),Event->Start(NewObject<UMCDayPlan>(Event),EMCCataclysmVariant::Jellies,99173));
    Event->SetActorTickEnabled(false);
    Event->RegisterDirtyPatch(FVector(300,0,0),280,true);
    TestTrue(TEXT("The balloon deposits supported near-side paint"),Event->IsPaintWetAt(FVector(300,0,0)));
    TestFalse(TEXT("The balloon cannot restart paint behind a wall"),Event->IsPaintWetAt(FVector(650,0,0)));
    World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    return true;
}
#endif
