#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCInventoryComponent.h"
#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "MCPlayerController.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCBrushContactComponent.h"
#include "MCArenaTooth.h"
#include "MCCoffeeWipe.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include <limits>

namespace MCBatch1CleanSpray
{
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Hero=nullptr;
    AMCPlayerState* Player=nullptr;
    UMCPerkComponent* Perks=nullptr;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));World->SetGameMode(URL);
        World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* State=World->SpawnActor<AMCGameState>();World->SetGameState(State);
        State->Phase=EMCShiftPhase::Working;State->bLobbyWaiting=false;
        auto* Controller=World->SpawnActor<AMCPlayerController>();Player=World->SpawnActor<AMCPlayerState>();
        Controller->SetPlayerState(Player);Player->SetOwner(Controller);State->AddPlayerState(Player);
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,10000),FRotator::ZeroRotator);
        Controller->Possess(Hero);Controller->bShowMouseCursor=false;
        Hero->GetCharacterMovement()->DisableMovement();Hero->Status->Initialize(100);
        Perks=Player->Perks;Perks->ResetPerks();
    }
    ~FWorld()
    {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    AActor* Wall(FVector Center,FVector HalfSize)
    {
        auto* Actor=World->SpawnActor<AActor>();
        auto* Box=NewObject<UBoxComponent>(Actor);Actor->SetRootComponent(Box);Actor->AddInstanceComponent(Box);
        Box->SetBoxExtent(HalfSize);Box->SetCollisionProfileName(TEXT("BlockAllDynamic"));Box->RegisterComponent();
        Actor->SetActorLocation(Center);return Actor;
    }
    AMCArenaTooth* Tooth(FVector Center,int32 ID,bool Damaged=true)
    {
        const FTransform Transform(Center);
        auto* Tooth=World->SpawnActorDeferred<AMCArenaTooth>(AMCArenaTooth::StaticClass(),Transform,nullptr,nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FMCArenaToothSettings Settings;Settings.InitialCalculusEveryNthTooth=0;
        Tooth->Initialize(ID,Settings);Tooth->FinishSpawning(Transform);
        if(Damaged) Tooth->Status->Damage(20);
        // This query fixture uses an explicit small convex body rather than
        // depending on the varying bounds of the artist's damage-stage meshes.
        Tooth->Body->SetSimulatePhysics(false);Tooth->Body->SetBoxExtent(FVector(14));
        return Tooth;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBatch1SprayCoverageTest,"MessControl.Perks.Batch1.CleanAndSprayCoverage",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBatch1SprayCoverageTest::RunTest(const FString&)
{
    MCBatch1CleanSpray::FWorld T;auto* I=T.Hero->Inventory.Get();
    if(!TestNotNull(TEXT("The real inventory is initialized"),I)
        || !TestNotNull(TEXT("The actual equipment profile loaded before duplication"),I->Settings.Get())
        || !TestNotNull(TEXT("Saved batch-one wide-bristle definition exists"),T.Perks->FindDefinition(TEXT("B1_WideBristles")))
        || !TestNotNull(TEXT("Saved batch-one pressure definition exists"),T.Perks->FindDefinition(TEXT("B1_UnderPressure")))) return false;
    // A private duplicate changes only this fixture's baseline. Saved assets
    // and all other actors retain their actual profile objects and settings.
    I->Settings=DuplicateObject<UMCEquipmentProfile>(I->Settings.Get(),I);
    I->Settings->BrushCleaningRadius=60.f;I->Settings->SprayReach=235.f;
    const float ContactReach=T.Hero->BrushContact->SurfaceReach;
    const FVector ContactLocal=I->BrushContactLocal();
    TestEqual(TEXT("Ordinary brush radius starts at60cm"),I->CleaningRadius(440.f),60.f);
    if(!TestTrue(TEXT("Grant the actual new wide-bristle passive"),T.Perks->ServerGrantPerk(TEXT("B1_WideBristles")))) return false;
    TestEqual(TEXT("Fifteen percent more width gives69cm radius"),I->CleaningRadius(440.f),69.f,.001f);
    TestEqual(TEXT("Large stains cannot expand the69cm brush into a whole-stain tool"),I->CleaningRadius(900.f),69.f,.001f);
    TestEqual(TEXT("Width does not increase cleaning rate"),I->CleaningSpeedMultiplier(),1.f);
    TestEqual(TEXT("Width does not increase contact acquisition reach"),T.Hero->BrushContact->SurfaceReach,ContactReach);
    TestTrue(TEXT("Width keeps the same physical brush contact point"),I->BrushContactLocal().Equals(ContactLocal));
    TArray<uint8> Ordinary,Wide;TArray<float> OrdinaryFractional,WideFractional;
    FMCCoffeeWipe::Reset(Ordinary);FMCCoffeeWipe::Reset(Wide);
    const FVector2D At(32.5/64.,32.5/64.);constexpr float WholeWidth=480.f;
    FMCCoffeeWipe::Stroke(Ordinary,At,At,60.f/WholeWidth,.1f,&OrdinaryFractional);
    FMCCoffeeWipe::Stroke(Wide,At,At,I->CleaningRadius(440.f)/WholeWidth,.1f,&WideFractional,I->CleaningSpeedMultiplier());
    constexpr int32 Center=32*64+32,NewStripe=32*64+41,Outside=32*64+42;
    TestEqual(TEXT("At equal time the actual central cell gets the same dose"),Wide[Center],Ordinary[Center]);
    TestTrue(TEXT("The new actual mask stripe reaches67.5cm"),Ordinary[NewStripe]==255 && Wide[NewStripe]<255);
    TestEqual(TEXT("A point75cm away remains outside the new brush"),Wide[Outside],uint8(255));
    TestTrue(TEXT("Removing the passive is accepted"),T.Perks->RemovePerk(TEXT("B1_WideBristles")));
    TestEqual(TEXT("Removal restores60cm without a latched profile mutation"),I->CleaningRadius(440.f),60.f);

    I->ServerSelect(EMCToolSlot::Spray);
    const FVector Origin=I->SprayOrigin(),Direction=I->SprayDirection();
    // This headless controller has no ULocalPlayer/camera. Without a submitted
    // view, SprayAim starts at the hero centre and converges from the offset
    // nozzle: adding a tooth changes that convergence ray, invalidating an
    // off-axis fixture positioned against the previous direction. Submit one
    // valid, nozzle-aligned view through the real authority RPC so this test
    // measures cone coverage/occlusion, not camera acquisition moving its aim.
    I->ServerCommitSprayView(Origin,Direction);
    TestTrue(TEXT("The submitted fixture view keeps the original nozzle axis"),I->SprayDirection().Equals(Direction,.0001f));
    const FVector Right=FVector::CrossProduct(Direction,FVector::UpVector).GetSafeNormal();
    auto* PointTarget=T.World->SpawnActor<AActor>();
    float Distance=0;
    const float BaseRadius=I->SprayRadiusAtDistance(180.f);
    const FVector WiderPoint=Origin+Direction*180.f+Right*(BaseRadius*1.1f);
    const FVector FarPoint=Origin+Direction*265.f;
    TestFalse(TEXT("The base cone excludes a point outside its18deg envelope"),I->SprayTargetDistance(PointTarget,WiderPoint,0,Distance));
    TestFalse(TEXT("The base spray cannot reach265cm"),I->SprayTargetDistance(PointTarget,FarPoint,0,Distance));
    if(!TestTrue(TEXT("Grant the actual new pressure passive"),T.Perks->ServerGrantPerk(TEXT("B1_UnderPressure")))) return false;
    TestEqual(TEXT("Pressure increases actual reach by25percent"),I->SprayReach(),293.75f,.001f);
    TestFalse(TEXT("The same new pressure passive does not stack"),T.Perks->ServerGrantPerk(TEXT("B1_UnderPressure")));
    TestEqual(TEXT("Rejecting the duplicate leaves the original25percent range bonus"),I->SprayReach(),293.75f,.001f);
    TestEqual(TEXT("Pressure increases width at a fixed distance by20percent"),I->SprayRadiusAtDistance(180.f),BaseRadius*1.2f,.001f);
    TestTrue(TEXT("The real target query now accepts the wider point"),I->SprayTargetDistance(PointTarget,WiderPoint,0,Distance));
    TestTrue(TEXT("The real target query now accepts265cm"),I->SprayTargetDistance(PointTarget,FarPoint,0,Distance));
    TestFalse(TEXT("A point beyond the new cone still misses"),I->SprayTargetDistance(PointTarget,Origin+Direction*180+Right*(BaseRadius*1.25f),0,Distance));
    TestFalse(TEXT("Pressure never enlarges the target's own radius"),I->SprayTargetDistance(PointTarget,Origin+Direction*20+Right*109,100,Distance));
    TestFalse(TEXT("Behind-nozzle points cannot receive treatment"),I->SprayTargetDistance(PointTarget,Origin-Direction*20,100,Distance));
    TestFalse(TEXT("Nonfinite points are rejected"),I->SprayTargetDistance(PointTarget,FVector(std::numeric_limits<float>::quiet_NaN(),0,0),0,Distance));
    auto* Wall=T.Wall(Origin+Direction*120.f,FVector(6,200,200));
    TestFalse(TEXT("The widened real treatment query cannot pass a wall"),I->SprayTargetDistance(PointTarget,WiderPoint,0,Distance));
    Wall->Destroy();
    TestTrue(TEXT("Removing the wall restores the unobstructed query"),I->SprayTargetDistance(PointTarget,WiderPoint,0,Distance));
    I->Settings->SprayReach=3000.f;
    TestEqual(TEXT("A modified large authored reach retains the3000cm safety cap"),I->SprayReach(),3000.f);
    I->Settings->SprayReach=235.f;
    TestTrue(TEXT("Removing pressure is accepted"),T.Perks->RemovePerk(TEXT("B1_UnderPressure")));
    TestEqual(TEXT("Removing pressure restores baseline reach"),I->SprayReach(),235.f);
    TestFalse(TEXT("Removing pressure restores the original width query"),I->SprayTargetDistance(PointTarget,WiderPoint,0,Distance));

    // The physical repair consumer must share the cone, select a real collision
    // contact and keep nearer healthy teeth and walls solid.
    auto* Side=T.Tooth(Origin+Direction*210.f+Right*86.5f,101);
    T.Hero->ServerSetPrimary(true);
    TestTrue(TEXT("Adding an off-axis repair body does not move the submitted nozzle axis"),I->SprayDirection().Equals(Direction,.0001f));
    TestNull(TEXT("A side tooth outside the ordinary cone is not repaired"),I->FindToothRepairTarget());
    if(!TestTrue(TEXT("Pressure may be acquired again after removal"),T.Perks->ServerGrantPerk(TEXT("B1_UnderPressure")))) return false;
    FHitResult Contact;
    TestEqual(TEXT("Pressure reaches the real side tooth collision inside the widened cone"),I->FindToothRepairTarget(&Contact),Side);
    TestEqual(TEXT("Repair returns that tooth's physical contact"),Contact.GetActor(),static_cast<AActor*>(Side));
    auto* Front=T.Tooth(Origin+Direction*120.f,102);
    TestTrue(TEXT("The front body lies on the same submitted nozzle axis"),I->SprayDirection().Equals(Direction,.0001f));
    TestEqual(TEXT("The nearest damaged tooth remains the first repair target"),I->FindToothRepairTarget(),Front);
    Front->Status->Initialize(100);Front->Body->SetBoxExtent(FVector(14));
    TestNull(TEXT("A healthy front tooth prevents healing a tooth behind it"),I->FindToothRepairTarget());
    Front->Destroy();
    Wall=T.Wall(Origin+Direction*120.f,FVector(6,200,200));
    TestNull(TEXT("A wall blocks all repair candidates behind its first contact"),I->FindToothRepairTarget());
    Wall->Destroy();
    TestEqual(TEXT("The side tooth is available again when occlusion is removed"),I->FindToothRepairTarget(),Side);
    T.Hero->ServerSetPrimary(false);
    return true;
}
#endif
