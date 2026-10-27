#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCHubGameMode.h"
#include "MCArenaTooth.h"
#include "MCPlayerState.h"
#include "MCPerkComponent.h"
#include "MCRoguePerkEffect.h"
#include "MCThroat.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "UObject/Package.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCPlayerBoundaryTests
{
struct FFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCGameMode* Mode=nullptr;
    AMCGameState* State=nullptr;
    AMCTongue* Tongue=nullptr;
    AMCToothCharacter* Hero=nullptr;
    APlayerController* Controller=nullptr;
    AMCPlayerState* Player=nullptr;
    FVector Origin;

    FFixture(double FloorZ=1500,bool Hub=false,bool Round=false):Origin(10000,0,FloorZ)
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(Hub?TEXT("game=/Script/MessControl.MCHubGameMode"):TEXT("game=/Script/MessControl.MCGameMode"));
        World->SetGameMode(URL);Mode=World->GetAuthGameMode<AMCGameMode>();
        if(Mode) {Mode->bUseDayOnePlan=false;Mode->bUseAdaptiveDirector=false;Mode->bUseSingleDayLoop=false;}
        World->InitializeActorsForPlay(URL);World->BeginPlay();
        World->GetAuthGameMode()->SetActorTickEnabled(false);
        State=World->GetGameState<AMCGameState>();
        if(State) {State->bLobbyWaiting=false;State->bTutorialActive=false;State->bDevManualEvents=true;State->Phase=EMCShiftPhase::Working;State->PhaseEndsAt=0;State->ArenaTeeth.Reset();}
        for(TActorIterator<AMCTongue> It(World);It;++It) It->Destroy();
        for(TActorIterator<AMCArenaTooth> It(World);It;++It) It->Destroy();
        for(TActorIterator<AMCThroat> It(World);It;++It) {It->SetActorTickEnabled(false);It->SetActorEnableCollision(false);}
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,Round?TEXT("/Engine/BasicShapes/Cylinder.Cylinder"):TEXT("/Engine/BasicShapes/Plane.Plane"));
#if WITH_EDITOR
        if(Mesh) FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
        const FTransform Pose(FQuat::Identity,Origin,FVector(20,20,1));
        Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Pose);
        {
            // Engine primitives retain editor CPU data, but their authored flag
            // is false. Permit only this fixture's procedural copy, then restore
            // both the bitfield and package dirtiness without saving the asset.
            const bool HadCPUAccess=Mesh && Mesh->bAllowCPUAccess;
            const bool WasDirty=Mesh && Mesh->GetOutermost()->IsDirty();
            ON_SCOPE_EXIT {
                if(Mesh) {Mesh->bAllowCPUAccess=HadCPUAccess;Mesh->GetOutermost()->SetDirtyFlag(WasDirty);}
            };
            if(Mesh) Mesh->bAllowCPUAccess=true;
            Tongue->SourceMesh=Mesh;Tongue->FinishSpawning(Pose);Tongue->SetActorTickEnabled(false);
        }
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Hero=World->SpawnActor<AMCToothCharacter>(Origin+FVector(0,0,150),FRotator::ZeroRotator,Params);
        Hero->GetCharacterMovement()->DisableMovement();Hero->ToothPhysics->SetComponentTickEnabled(false);
        // Manual pawn ticks below exercise the actual boundary call from Tick,
        // while Chaos/time advance normally and the day director stays isolated.
        Hero->SetActorTickEnabled(false);
        Controller=World->SpawnActor<APlayerController>();
        Player=Controller->GetPlayerState<AMCPlayerState>();
        if(!Player) {Player=World->SpawnActor<AMCPlayerState>();Controller->SetPlayerState(Player);}
        Controller->Possess(Hero);Hero->Status->Initialize(100);
    }
    ~FFixture()
    {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    void At(FVector Point) {Hero->SetActorLocation(Point,false,nullptr,ETeleportType::TeleportPhysics);}
    void Step(double Seconds)
    {
        for(int32 I=0;I<FMath::CeilToInt(Seconds*60);++I) {++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60);Hero->Tick(1.f/60);}
    }
    AMCArenaTooth* Row(FVector Point)
    {
        auto* Tooth=World->SpawnActor<AMCArenaTooth>(Point,FRotator::ZeroRotator);
        Tooth->SetActorTickEnabled(false);State->ArenaTeeth.Add(Tooth);return Tooth;
    }
};

struct FArmor
{
    UMCPerkComponent* Perks=nullptr;
    UDataTable* Table=nullptr;
    const FName ID=TEXT("__MCBoundaryArmorTest");
    bool bAdded=false,bWasDirty=false;
    FArmor(AMCPlayerState* Player):Perks(Player->Perks)
    {
        Table=Perks->GetPerkTable();
        if(!Table || Table->GetRowMap().Contains(ID)) {Table=nullptr;return;}
        bWasDirty=Table->GetOutermost()->IsDirty();
        FMCPerkDefinition Row;Row.DisplayName=FText::FromString(TEXT("Boundary fixture armor"));
        Row.bRoguePerk=true;Row.MaxStacks=1;Row.EffectClass=UMCRoguePerkEffect::StaticClass();
        FMCRogueModifier Mod;Mod.Stat=EMCRogueStat::IncomingDamage;Mod.Value=-.4f;Row.Modifiers.Add(Mod);
        Table->AddRow(ID,Row);bAdded=true;Perks->ServerGrantPerk(ID);
    }
    ~FArmor()
    {
        if(Table && bAdded) {
            Perks->RemovePerk(ID);Table->RemoveRow(ID);
            Table->GetOutermost()->SetDirtyFlag(bWasDirty);
        }
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlayerBoundaryShiftAndReserveTest,"MessControl.ArenaBoundary.ShiftedFloorArmorAndSingleReserveDeath",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlayerBoundaryShiftAndReserveTest::RunTest(const FString&)
{
    using namespace MCPlayerBoundaryTests;
    FFixture F(-1000);
    if(!TestTrue(TEXT("The fixture is a real active arena participant"),F.Mode && AMCGameMode::IsGameplayParticipant(F.Controller))) return false;
    if(!TestTrue(TEXT("Authored tissue is available"),F.Tongue->CurrentVertices().Num()>0)) return false;
    F.Row(F.Origin+FVector(400,0,80));
    F.Step(.2);
    TestTrue(TEXT("A legitimate lowered tongue below the old world-Z cutoff is safe"),F.Hero->Status->IsAlive());
    FArmor Armor(F.Player);
    if(!TestTrue(TEXT("The actual perk pipeline reduces incoming damage"),F.Player->Perks->IncomingDamage(100,EMCRogueDamageKind::Ordinary)<70)) return false;
    F.Hero->Status->Damage(100);
    TestTrue(TEXT("Ordinary MaxHealth combat damage alone does not kill the armored player"),F.Hero->Status->IsAlive());
    F.Hero->Status->Initialize(100);
    const int32 Reserves=F.State->AvailableArenaTeeth();
    F.At(F.Origin+FVector(0,0,-301));F.Hero->Tick(1.f/60);
    TestFalse(TEXT("Below the map-relative plane kills immediately despite armor"),F.Hero->Status->IsAlive());
    TestTrue(TEXT("Boundary death uses the ordinary controller respawn wait"),F.Hero->RespawnAt>F.State->GetServerWorldTimeSeconds());
    const double Deadline=F.Hero->RespawnAt;
    F.Step(.2);TestEqual(TEXT("Repeated samples do not reschedule or duplicate death"),F.Hero->RespawnAt,Deadline);
    F.Mode->ProcessRespawns();TestTrue(TEXT("The normal wait keeps the original body possessed"),F.Controller->GetPawn()==F.Hero);
    F.Hero->RespawnAt=-1;F.Mode->ProcessRespawns();
    auto* Replacement=Cast<AMCToothCharacter>(F.Controller->GetPawn());
    if(!TestTrue(TEXT("The normal reserve flow supplies a different player body"),Replacement && Replacement!=F.Hero)) return false;
    TestEqual(TEXT("Exactly one reserve is consumed"),F.State->AvailableArenaTeeth(),Reserves-1);
    F.Mode->ProcessRespawns();TestEqual(TEXT("No second reserve is consumed"),F.State->AvailableArenaTeeth(),Reserves-1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlayerBoundaryGraceTest,"MessControl.ArenaBoundary.ReturnResetsGraceAndFirstFrameOutsideIsChecked",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlayerBoundaryGraceTest::RunTest(const FString&)
{
    using namespace MCPlayerBoundaryTests;
    FFixture F;
    if(!TestTrue(TEXT("Real participant, not a preview exclusion"),F.Mode && AMCGameMode::IsGameplayParticipant(F.Controller))) return false;
    // The very first pawn tick starts outside; no earlier footprint arming.
    F.At(F.Origin+FVector(2600,0,150));F.Step(1.);
    TestTrue(TEXT("Brief external flight receives a recovery window"),F.Hero->Status->IsAlive());
    F.At(F.Origin+FVector(0,0,1200));F.Step(.1);
    TestTrue(TEXT("A high legitimate jump over tissue is safe"),F.Hero->Status->IsAlive());
    F.At(F.Origin+FVector(2600,0,150));F.Step(1.);
    TestTrue(TEXT("Returning to the mouth reset the whole deadline"),F.Hero->Status->IsAlive());
    F.Step(.7);
    TestFalse(TEXT("A living body left on an exterior ledge cannot survive indefinitely"),F.Hero->Status->IsAlive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlayerBoundaryPhysicalBodyTest,"MessControl.ArenaBoundary.PhysicalBodyOutsideAndLegalToothRow",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlayerBoundaryPhysicalBodyTest::RunTest(const FString&)
{
    using namespace MCPlayerBoundaryTests;
    FFixture F;
    if(!TestTrue(TEXT("Active arena participant"),F.Mode && AMCGameMode::IsGameplayParticipant(F.Controller))) return false;
    auto* Row=F.Row(F.Origin+FVector(0,1750,80));
    F.Hero->Tick(1.f/60); // Capture the stationary registered row's footprint.
    F.At(Row->Body->Bounds.Origin+FVector(0,0,350));F.Step(2.);
    TestTrue(TEXT("The legal tooth row outside the tongue footprint remains safe"),F.Hero->Status->IsAlive());
    F.At(F.Origin+FVector(0,0,150));F.Step(.8);
    F.Hero->ToothPhysics->ApplyHit(FVector(300,0,0),F.Hero->GetActorLocation());
    if(!TestEqual(TEXT("The actual hit entered ragdoll"),F.Hero->ToothPhysics->GetBodyState(),EMCBodyState::Ragdoll)) return false;
    const FVector Capsule=F.Hero->GetActorLocation();
    auto* Mesh=F.Hero->GetMesh();Mesh->SetEnableGravity(false);
    Mesh->AddWorldOffset(F.Origin+FVector(0,0,-350)-F.Hero->ToothPhysics->PhysicalLocation(),false,nullptr,ETeleportType::TeleportPhysics);
    Mesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
    TestTrue(TEXT("The capsule still remains inside the mouth"),F.Hero->GetActorLocation().Equals(Capsule,.1));
    F.Hero->Tick(1.f/60);
    TestFalse(TEXT("A detached physical body beyond the lower plane cannot hide behind its capsule"),F.Hero->Status->IsAlive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlayerBoundaryOutlineTest,"MessControl.ArenaBoundary.ClosedMeshOutlineAndFrozenDeathCannotEscape",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlayerBoundaryOutlineTest::RunTest(const FString&)
{
    using namespace MCPlayerBoundaryTests;
    FFixture F(1500,false,true);
    if(!TestTrue(TEXT("Actual curved closed tissue mesh is available"),F.Tongue->CurrentVertices().Num()>0 && AMCGameMode::IsGameplayParticipant(F.Controller))) return false;
    F.At(F.Origin+FVector(1250,0,300));F.Step(2.);
    TestTrue(TEXT("A brief edge extension has the same clearance on a closed mesh"),F.Hero->Status->IsAlive());
    F.At(F.Origin+FVector(990,990,300));F.Step(1.8);
    TestFalse(TEXT("An exterior corner inside the coarse AABB is still outside the actual round footprint"),F.Hero->Status->IsAlive());
    FFixture Frozen;
    if(!TestTrue(TEXT("The real freezing cinematic starts"),Frozen.Hero->BeginFreezeDeath())) return false;
    Frozen.At(Frozen.Origin+FVector(0,0,-301));Frozen.Hero->Tick(1.f/60);
    TestFalse(TEXT("A cinematic damage hold cannot make a physical arena escape immortal"),Frozen.Hero->Status->IsAlive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlayerBoundarySafeModesTest,"MessControl.ArenaBoundary.HubLobbyTutorialAndArrivalKeepTheirSafety",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlayerBoundarySafeModesTest::RunTest(const FString&)
{
    using namespace MCPlayerBoundaryTests;
    FFixture Hub(1500,true);Hub.At(Hub.Origin+FVector(3000,0,-1200));Hub.Step(2.);
    TestTrue(TEXT("A real hub game mode is excluded from mouth death bounds"),Hub.Hero->Status->IsAlive());
    FFixture F;
    F.At(F.Origin+FVector(0,0,-301));
    F.State->bLobbyWaiting=true;F.Step(2.);TestTrue(TEXT("Startup wait remains protected"),F.Hero->Status->IsAlive());
    F.State->bLobbyWaiting=false;F.State->bTutorialActive=true;F.Step(2.);TestTrue(TEXT("Safe tutorial remains protected"),F.Hero->Status->IsAlive());
    F.State->bTutorialActive=false;F.Hero->SetArenaArrivalHeld(true);F.Step(2.);TestTrue(TEXT("The actual arrival barrier remains protected"),F.Hero->Status->IsAlive());
    F.Hero->SetArenaArrivalHeld(false);F.Hero->Tick(1.f/60);
    TestFalse(TEXT("Releasing safe startup immediately restores real boundary checks"),F.Hero->Status->IsAlive());
    return true;
}
#endif
