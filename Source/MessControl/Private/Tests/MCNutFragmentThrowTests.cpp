#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCNutEnemy.h"
#include "MCNutBoss.h"
#include "MCNutRainEvent.h"
#include "MCToothCharacter.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCNutFragmentThrowTestsPrivate
{
struct FWorldFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Hero=nullptr;
    AMCNutRainEvent* Encounter=nullptr;
    FWorldFixture()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,2000),FRotator::ZeroRotator);
        Hero->GetCharacterMovement()->DisableMovement();
        Encounter=World->SpawnActor<AMCNutRainEvent>();Encounter->Settings.bBossEncounter=true;
        Encounter->Stage=EMCNutRainStage::Enemies;Encounter->SetActorTickEnabled(false);
    }
    ~FWorldFixture()
    {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    void Step(float Seconds,float Dt)
    {for(int32 I=0;I<FMath::CeilToInt(Seconds/Dt);++I) {++GFrameCounter;World->Tick(LEVELTICK_All,Dt);}}
    AMCFoodActor* Piece(FName Name=TEXT("Nut"))
    {
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
        FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
        FMCFoodRow Row;Row.WholeMeshes={Mesh};Row.FragmentMeshes={Mesh};Row.FragmentScale=FVector(.24);
        Row.Mass=9;Row.Fragments=3;Row.SpoilSeconds=300;
        const FTransform Pose(Hero->GetActorLocation()+FVector(110,0,-25));
        auto* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FRandomStream Random(44);Food->ConfigureItem(Name,Row,Random,true);Food->Batch=944;
        if(Name==TEXT("Nut")) {Food->Tags.AddUnique(TEXT("MCNutRain"));Food->SetOwner(Encounter);}
        Food->FinishSpawning(Pose);return Food;
    }
    AMCNutEnemy* Enemy(FVector Point)
    {
        auto* Nut=World->SpawnActor<AMCNutEnemy>(Point,FRotator::ZeroRotator);
        Nut->SetOwner(Encounter);Encounter->RegisterEncounterEnemy(Nut);
        // Isolate the projectile; normal enemy movement requires a live tongue.
        Nut->SetActorTickEnabled(false);return Nut;
    }
    void Wall(FVector Point)
    {
        auto* Actor=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Box);Box->SetBoxExtent(FVector(12,180,180));
        Box->SetCollisionProfileName(TEXT("BlockAll"));Box->RegisterComponent();Actor->SetActorLocation(Point);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutFragmentThrownImpact,"MessControl.Nuts.FragmentThrow.PickupHitsOneEnemy",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutFragmentThrownImpact::RunTest(const FString&)
{
    using namespace MCNutFragmentThrowTestsPrivate;
    for(float Dt:{1.f/30,1.f/120}) {
        FWorldFixture T;auto* Food=T.Piece();auto* Collection=T.Hero->FoodCollection.Get();
        Collection->Toggle();
        if(!TestTrue(TEXT("E immediately picks one actual encounter nut into the hand"),Collection->Contains(Food))) return false;
        T.Step(.7f,Dt);
        const FVector Start=Food->GetActorLocation();
        auto* Front=T.Enemy(Start+FVector(280,0,-20));
        auto* Rear=T.Enemy(Start+FVector(500,0,-20));
        const float InitialHealth=Front->Health,RearHealth=Rear->Health;
        const double SpoilAt=Food->SpoilAt;
        const float ShotSpeed=Collection->EffectiveThrowSpeed();
        if(!TestTrue(TEXT("Q launches the same retained nut piece"),Collection->ThrowOne(FVector::ForwardVector))) return false;
        TestTrue(TEXT("Nut shot speed matches the camera aim solve before the hand emptied"),FMath::IsNearlyEqual(float(Food->Body->GetPhysicsLinearVelocity().Size()),ShotSpeed,1.f));
        TestTrue(TEXT("Q empties the hand and ends pickup without a replacement tower"),Collection->Pieces.IsEmpty() && !Collection->bCollecting);
        T.Step(.6f,Dt);
        TestTrue(TEXT("Swept physical travel damages the first query-only nut enemy"),Front->Health<InitialHealth);
        TestTrue(TEXT("A small piece has bounded damage"),InitialHealth-Front->Health<=18.01f);
        TestEqual(TEXT("One launch resolves exactly one enemy contact"),Food->ConfirmedImpacts,1);
        TestEqual(TEXT("The same launch cannot pierce a second enemy"),Rear->Health,RearHealth);
        TestTrue(TEXT("The real piece remains available after bouncing"),!Food->IsDisposed() && Food->bFragment);
        TestEqual(TEXT("Throwing leaves the food batch intact"),Food->Batch,944);
        TestEqual(TEXT("Throwing cannot restart freshness"),Food->SpoilAt,SpoilAt);
        TestTrue(TEXT("Encounter debris keeps its no-food-credit tag"),Food->ActorHasTag(TEXT("MCNutRain")));
        TestEqual(TEXT("Nut throws retain safe teammate collision"),Food->Body->GetCollisionResponseToChannel(ECC_Pawn),ECR_Ignore);
        int32 Pieces=0;for(TActorIterator<AMCFoodActor> It(T.World);It;++It) ++Pieces;
        TestEqual(TEXT("A throw never duplicates the nut or its potential rewards"),Pieces,1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutFragmentThrowOcclusion,"MessControl.Nuts.FragmentThrow.SceneryBlocksHit",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutFragmentThrowOcclusion::RunTest(const FString&)
{
    using namespace MCNutFragmentThrowTestsPrivate;
    FWorldFixture T;auto* Food=T.Piece();const FVector Start=Food->GetActorLocation();
    auto* Nut=T.Enemy(Start+FVector(300,0,0));const float Health=Nut->Health;
    T.Wall(Start+FVector(150,0,0));Food->LaunchManualThrow(T.Hero,FVector::ForwardVector,1200);
    T.Step(.6f,1.f/30);
    TestEqual(TEXT("A wall blocks the physical nut-piece attack"),Nut->Health,Health);
    TestEqual(TEXT("Blocked throws cannot invent an enemy contact"),Food->ConfirmedImpacts,0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutFragmentThrowIntent,"MessControl.Nuts.FragmentThrow.RequiresDeliberateNutLaunch",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutFragmentThrowIntent::RunTest(const FString&)
{
    using namespace MCNutFragmentThrowTestsPrivate;
    for(int32 Scenario:{0,1,2}) {
        FWorldFixture T;auto* Food=T.Piece(Scenario==1?TEXT("Carrot"):TEXT("Nut"));
        if(Scenario==2) Food->SetOwner(nullptr); // A tag/name alone is not encounter ammunition.
        const FVector Start=Food->GetActorLocation();auto* Nut=T.Enemy(Start+FVector(220,0,-20));
        const float Health=Nut->Health;
        if(Scenario!=0) Food->LaunchManualThrow(T.Hero,FVector::ForwardVector,1200);
        else {Food->MarkManualThrow(T.Hero);Food->Body->SetPhysicsLinearVelocity(FVector(1200,0,0));}
        T.Step(.4f,1.f/60);
        TestEqual(TEXT("Ordinary food and loose nut debris cannot turn into enemy projectiles"),Nut->Health,Health);
        TestEqual(TEXT("A stale manual-handling marker cannot award a new impact"),Food->ConfirmedImpacts,0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutFragmentSingleHand,"MessControl.Nuts.FragmentThrow.SingleHandPreservesFoodStacks",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutFragmentSingleHand::RunTest(const FString&)
{
    using namespace MCNutFragmentThrowTestsPrivate;
    FWorldFixture T;auto* First=T.Piece();auto* Collection=T.Hero->FoodCollection.Get();
    Collection->Toggle();
    if(!TestTrue(TEXT("E takes one nearby boss nut immediately"),Collection->Contains(First))) return false;
    auto* Second=T.Piece();Second->Body->SetEnableGravity(false);
    auto* Food=T.Piece(TEXT("Carrot"));Food->SetActorLocation(Food->GetActorLocation()+FVector(0,50,0));Food->Body->SetEnableGravity(false);
    TestTrue(TEXT("The replicated hand is explicitly nut ammunition"),Collection->HasNutAmmunition());
    TestEqual(TEXT("Nut ammunition has exactly one hand slot"),Collection->EffectiveCapacity(),1);
    TestFalse(TEXT("A second nut cannot start a tower"),Collection->Collect(Second));
    TestFalse(TEXT("Food cannot be stacked on top of the nut"),Collection->Collect(Food));
    T.Step(.8f,1.f/60);
    TestEqual(TEXT("Waiting beside fragments never collects additional ammunition"),Collection->Pieces.Num(),1);
    TestTrue(TEXT("Q fires the one held nut"),Collection->ThrowOne(FVector::ForwardVector));
    T.Step(.6f,1.f/60);
    TestTrue(TEXT("Firing never automatically loads another nearby fragment"),Collection->Pieces.IsEmpty() && !Collection->bCollecting && !Second->StackCarrier);
    Second->SetActorLocation(T.Hero->GetActorLocation()+FVector(800,0,0),false,nullptr,ETeleportType::TeleportPhysics);
    Collection->Toggle();
    if(!TestTrue(TEXT("Ordinary food still uses normal collection"),Collection->Collect(Food))) return false;
    auto* OtherFood=T.Piece(TEXT("Carrot"));OtherFood->SetActorLocation(OtherFood->GetActorLocation()+FVector(0,-50,0));
    TestTrue(TEXT("A second ordinary piece still joins the food tower"),Collection->Collect(OtherFood));
    TestTrue(TEXT("Ordinary food keeps its configured stack capacity"),Collection->EffectiveCapacity()>1);
    Second->SetActorLocation(T.Hero->GetActorLocation()+FVector(110,0,0),false,nullptr,ETeleportType::TeleportPhysics);
    TestFalse(TEXT("A boss nut cannot be mixed into an ordinary food tower"),Collection->Collect(Second));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutFragmentEncounterGuard,"MessControl.Nuts.FragmentThrow.RequiresSameActiveEncounter",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutFragmentEncounterGuard::RunTest(const FString&)
{
    using namespace MCNutFragmentThrowTestsPrivate;
    for(int32 Scenario:{0,1,2,3,4}) {
        FWorldFixture T;auto* Food=T.Piece();
        auto* Nut=T.Enemy(Food->GetActorLocation()+FVector(220,0,-20));const float Health=Nut->Health;
        if(Scenario==0) Nut->SetOwner(nullptr);
        else if(Scenario==1) {
            auto* Other=T.World->SpawnActor<AMCNutRainEvent>();Other->Settings.bBossEncounter=true;
            Other->Stage=EMCNutRainStage::Enemies;Other->SetActorTickEnabled(false);Nut->SetOwner(Other);
        } else if(Scenario==2) T.Encounter->Stage=EMCNutRainStage::Idle;
        else if(Scenario==3) T.Encounter->Stage=EMCNutRainStage::Complete;
        else T.Encounter->bFailed=true;
        Food->LaunchManualThrow(T.Hero,FVector::ForwardVector,1200);
        T.Step(.4f,1.f/60);
        TestEqual(TEXT("Unowned, foreign and inactive-encounter targets cannot take nut-shot damage"),Nut->Health,Health);
        TestEqual(TEXT("A rejected encounter contact cannot confirm a gameplay hit"),Food->ConfirmedImpacts,0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutFragmentBossShield,"MessControl.Nuts.FragmentThrow.BossShieldOwnsShot",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutFragmentBossShield::RunTest(const FString&)
{
    using namespace MCNutFragmentThrowTestsPrivate;
    FWorldFixture T;auto* Food=T.Piece();auto* Collection=T.Hero->FoodCollection.Get();
    Collection->Toggle();T.Step(.7f,1.f/60);
    const FTransform Pose(Food->GetActorLocation()+FVector(320,0,-20));
    auto* Boss=T.World->SpawnActorDeferred<AMCNutBoss>(AMCNutBoss::StaticClass(),Pose,T.Encounter,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    Boss->EncounterOwner=T.Encounter;
    Boss->State=EMCNutBossState::Idle;Boss->ShieldHealth=100;Boss->FinishSpawning(Pose);Boss->SetActorTickEnabled(false);
    T.Encounter->RegisterEncounterEnemy(Boss);
    const float Health=Boss->Health,Shield=Boss->ShieldHealth;
    if(!TestTrue(TEXT("The ordinary Q action fires boss ammunition"),Collection->ThrowOne(FVector::ForwardVector))) return false;
    T.Step(.4f,1.f/60);
    TestEqual(TEXT("A nut shot cannot bypass an intact boss shield"),Boss->Health,Health);
    TestTrue(TEXT("The ordinary boss damage resolver removes shield durability"),Boss->ShieldHealth<Shield);
    TestTrue(TEXT("Boss shield feedback accepts the projectile as one blocked hit"),Boss->bLastToolHitBlocked && Food->ConfirmedImpacts==1);
    return true;
}
#endif
