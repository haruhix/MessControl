#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace
{
struct FMouthEntryWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Hero=nullptr;
    FMouthEntryWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,2000),FRotator::ZeroRotator);
        Hero->GetCharacterMovement()->DisableMovement();
        Step(.8f);
    }
    ~FMouthEntryWorld()
    { World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
    void Step(float Seconds)
    { for(int32 I=0;I<FMath::CeilToInt(Seconds*60);++I) { ++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60); } }
    AMCFoodActor* Food(FVector Position)
    {
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
        FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
        FMCFoodRow Row; Row.WholeMeshes={Mesh}; Row.FragmentMeshes={Mesh};
        Row.Scale=Row.FragmentScale=FVector(.36); Row.Mass=9; Row.SpoilSeconds=300;
        const FTransform Transform(Position);
        auto* Piece=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FRandomStream Random(7); Piece->ConfigureItem(TEXT("MouthEntryFixture"),Row,Random);
        Piece->FinishSpawning(Transform); return Piece;
    }
    void Contact(AMCFoodActor* Food,AActor* Other,UPrimitiveComponent* Component,FVector Normal)
    {
        FHitResult Hit; Hit.bBlockingHit=true; Hit.ImpactNormal=Hit.Normal=Normal;
        Hit.ImpactPoint=Other->GetActorLocation();
        Food->Body->OnComponentHit.Broadcast(Food->Body,Other,Component,FVector::ZeroVector,Hit);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCMouthEntryGentleImpact,"MessControl.Food.MouthEntry.GentlePushAndOrdinaryImpact",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCMouthEntryGentleImpact::RunTest(const FString&)
{
    FMouthEntryWorld T;
    T.Hero->FoodCollection->Toggle();
    auto* Held=T.Food(T.Hero->GetActorLocation()+FVector(110,-35,-35));
    if(!TestTrue(TEXT("Fixture collects a load before the incoming hit"),T.Hero->FoodCollection->Collect(Held))) return false;
    T.Step(.8f);
    auto* Incoming=T.Food(FVector(-4000,0,2000));
    Incoming->BeginMouthEntry(FVector(600,0,0),140);
    TestTrue(TEXT("Entry preserves ballistic horizontal velocity"),FMath::IsNearlyEqual(Incoming->Body->GetPhysicsLinearVelocity().X,600.));
    TestEqual(TEXT("Entry removes linear drag"),Incoming->Body->GetLinearDamping(),0.f);
    const float Health=T.Hero->Status->State.Health;
    const int32 Knockdowns=T.Hero->ToothPhysics->KnockdownCount;
    // UE ignores LaunchCharacter while MOVE_None. Activate the fixture before
    // the contact, as a live player is, so the entry can queue its momentum.
    T.Hero->GetCharacterMovement()->bRunPhysicsWithNoController=true;
    T.Hero->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    T.Contact(Incoming,T.Hero,T.Hero->GetCapsuleComponent(),FVector(-1,0,0));
    TestTrue(TEXT("Contact with a player does not abort the incoming flight"),Incoming->IsMouthEntryActive());
    TestEqual(TEXT("Incoming food registers exactly one gentle contact"),Incoming->ConfirmedImpacts,1);
    TestEqual(TEXT("Entry push preserves player health"),T.Hero->Status->State.Health,Health);
    TestEqual(TEXT("The entry hit callback does not force release of the held load"),T.Hero->FoodCollection->Pieces.Num(),1);
    TestEqual(TEXT("Entry push does not start a knockdown"),T.Hero->ToothPhysics->KnockdownCount,Knockdowns);
    T.Contact(Incoming,T.Hero,T.Hero->GetCapsuleComponent(),FVector(-1,0,0));
    TestEqual(TEXT("Repeated contact obeys the existing hit cooldown"),Incoming->ConfirmedImpacts,1);
    T.Step(1.f/60);
    const FVector Push=T.Hero->GetVelocity();
    TestTrue(*FString::Printf(TEXT("Entry supplies modest forward kinetic momentum, actual %s"),*Push.ToCompactString()),
        Push.X>50 && Push.X<=145 && Push.Size()<T.Hero->ToothPhysics->Settings.FallThreshold);
    T.Hero->GetCharacterMovement()->DisableMovement();

    auto* Ordinary=T.Food(FVector(-4000,0,2000));
    Ordinary->Body->SetPhysicsLinearVelocity(FVector(600,0,0));
    Ordinary->Tick(1.f/60); // Sample incoming motion through the same PrePhysics path as live food.
    T.Contact(Ordinary,T.Hero,T.Hero->GetCapsuleComponent(),FVector(-1,0,0));
    TestTrue(TEXT("Ordinary incoming food retains health damage"),T.Hero->Status->State.Health<Health);
    TestTrue(TEXT("Ordinary incoming food still spills the load"),T.Hero->FoodCollection->Pieces.IsEmpty());
    TestTrue(TEXT("Ordinary incoming food retains the full knockdown"),T.Hero->ToothPhysics->KnockdownCount>Knockdowns);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutRainEntryImpact,"MessControl.Food.MouthEntry.NutStormContactDamageAndCooldown",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutRainEntryImpact::RunTest(const FString&)
{
    FMouthEntryWorld T;
    auto* Nut=T.Food(FVector(-4000,0,2000));
    Nut->Tags.AddUnique(TEXT("MCNutRain"));
    Nut->BeginMouthEntry(FVector(600,0,0),140);
    const float Health=T.Hero->Status->State.Health;
    T.Step(.1f);
    TestEqual(TEXT("A falling nut that misses the player cannot deal proximity damage"),T.Hero->Status->State.Health,Health);
    T.Contact(Nut,T.Hero,T.Hero->GetCapsuleComponent(),FVector(-1,0,0));
    const float HitHealth=T.Hero->Status->State.Health;
    TestTrue(TEXT("A validated cataclysm contact damages the player"),HitHealth<Health);
    TestTrue(TEXT("The ordinary impact damage limit still applies"),Health-HitHealth<=Nut->Settings.MaxDamage);
    T.Contact(Nut,T.Hero,T.Hero->GetCapsuleComponent(),FVector(-1,0,0));
    TestEqual(TEXT("Duplicate physics contacts cannot apply a second hit immediately"),T.Hero->Status->State.Health,HitHealth);
    auto* Stationary=T.Food(FVector(-5000,0,2000));
    Stationary->Tags.AddUnique(TEXT("MCNutRain"));
    Stationary->BeginMouthEntry(FVector::ZeroVector,140);
    T.Contact(Stationary,T.Hero,T.Hero->GetCapsuleComponent(),FVector(-1,0,0));
    TestEqual(TEXT("A stationary storm nut is not an incoming impact hazard"),T.Hero->Status->State.Health,HitHealth);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCMouthEntryLifecycle,"MessControl.Food.MouthEntry.LandingAndDisposalRestoreDrag",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCMouthEntryLifecycle::RunTest(const FString&)
{
    FMouthEntryWorld T;
    auto* Piece=T.Food(FVector(-4000,0,2000));
    Piece->BeginMouthEntry(FVector(600,0,-300),140);
    auto* Exit=T.World->SpawnActor<AMCFoodDisposal>(Piece->GetActorLocation(),FRotator::ZeroRotator);
    TestFalse(TEXT("An incoming piece cannot enter a delivery intake"),Exit->CanAcceptDelivery(Piece));
    Exit->bBrushBin=true;
    const FVector EntryPosition=Piece->GetActorLocation();
    Exit->Tick(1.f/60);
    TestTrue(TEXT("The front exit does not teleport fresh incoming food"),Piece->GetActorLocation().Equals(EntryPosition));
    Exit->SetActorTickEnabled(false);
    auto* Floor=T.World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(10)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent();
    Floor->SetActorLocation(FVector(10000,0,2000));
    T.Contact(Piece,Floor,Box,FVector::UpVector);
    TestFalse(TEXT("A supporting world contact ends the entry flight"),Piece->IsMouthEntryActive());
    TestEqual(TEXT("A supporting world contact restores ordinary drag immediately"),Piece->Body->GetLinearDamping(),.7f);
    Piece->Tick(1.f/60);
    TestEqual(TEXT("The entry landing follows the normal free-food phase"),Piece->Phase,EMCFoodPhase::Free);
    Exit->bBrushBin=false;
    TestTrue(TEXT("Normal delivery resumes after landing"),Exit->CanAcceptDelivery(Piece));

    for (float Speed:{700.f,1400.f,2600.f})
    {
        auto* FastEntry=T.Food(FVector(-5000,0,2000));
        FastEntry->BeginMouthEntry(FVector(Speed,0,-300),140);
        T.Contact(FastEntry,Floor,Box,FVector::UpVector);
        TestTrue(TEXT("Wide-arena entry retains only slight landing momentum"),FastEntry->Body->GetPhysicsLinearVelocity().Size()<=160.01);
        TestFalse(TEXT("Fast supporting landing ends entry"),FastEntry->IsMouthEntryActive());
        const float Health=T.Hero->Status->State.Health;
        T.Contact(FastEntry,T.Hero,T.Hero->GetCapsuleComponent(),FVector(-1,0,0));
        TestEqual(TEXT("A second contact in the same landing batch cannot become a full attack"),T.Hero->Status->State.Health,Health);
        FastEntry->Destroy();
    }
    auto* Ordinary=T.Food(FVector(-5000,0,2000));
    const FVector ThrowVelocity(2600,0,-300);
    Ordinary->Body->SetPhysicsLinearVelocity(ThrowVelocity);Ordinary->Tick(1.f/60);
    T.Contact(Ordinary,Floor,Box,FVector::UpVector);
    TestTrue(TEXT("Ordinary thrown food retains its post-contact velocity"),Ordinary->Body->GetPhysicsLinearVelocity().Equals(ThrowVelocity,.01));
    Ordinary->Destroy();

    auto* Disposed=T.Food(FVector(-4000,0,2000));
    Disposed->BeginMouthEntry(FVector(600,0,0),140); Disposed->Dispose();
    TestEqual(TEXT("Disposal restores ordinary drag"),Disposed->Body->GetLinearDamping(),.7f);
    auto* TimedOut=T.Food(FVector(-6000,0,2000));
    TimedOut->BeginMouthEntry(FVector(0,0,10),140);
    TimedOut->Body->SetEnableGravity(false); TimedOut->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
    T.Step(8.1f);
    TestEqual(TEXT("An interrupted entry cannot keep zero drag indefinitely"),TimedOut->Body->GetLinearDamping(),.7f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutCollisionCracks,"MessControl.Food.MouthEntry.FallingWalnutsCrackWithoutContactChains",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutCollisionCracks::RunTest(const FString&)
{
    FMouthEntryWorld T;
    auto* Lower=T.Food(FVector(-4000,0,1800));
    auto* Incoming=T.Food(FVector(-4000,0,2000));
    Lower->Batch=Incoming->Batch=991;
    Lower->Tags.Add(TEXT("MCNutRain")); Incoming->Tags.Add(TEXT("MCNutRain"));
    Incoming->BeginMouthEntry(FVector(0,0,-650),140);
    T.Contact(Incoming,Lower,Lower->Body,FVector::UpVector);
    TestTrue(TEXT("A falling whole walnut cracks the struck walnut"),Lower->IsDisposed());
    TestTrue(TEXT("The incoming shell also fractures on the impact"),Incoming->IsDisposed());
    int32 Fragments=0;for(TActorIterator<AMCFoodActor> It(T.World);It;++It) if(It->Batch==991 && It->bFragment) ++Fragments;
    TestEqual(TEXT("Both broken walnuts create their normal collectable fragments"),Fragments,Lower->FoodData.Fragments+Incoming->FoodData.Fragments);
    T.Contact(Incoming,Lower,Lower->Body,FVector::UpVector);
    int32 After=0;for(TActorIterator<AMCFoodActor> It(T.World);It;++It) if(It->Batch==991 && It->bFragment) ++After;
    TestEqual(TEXT("Duplicate solver contacts cannot duplicate fragments"),After,Fragments);
    auto* Resting=T.Food(FVector(-6000,0,2000));auto* Neighbor=T.Food(FVector(-6000,0,1800));
    Resting->Tags.Add(TEXT("MCNutRain"));Neighbor->Tags.Add(TEXT("MCNutRain"));
    T.Contact(Resting,Neighbor,Neighbor->Body,FVector::UpVector);
    TestFalse(TEXT("Resting walnut contact cannot start a fracture chain"),Neighbor->IsDisposed());
    auto* Ordinary=T.Food(FVector(-8000,0,1800));auto* Event=T.Food(FVector(-8000,0,2000));
    Event->Tags.Add(TEXT("MCNutRain"));Event->BeginMouthEntry(FVector(0,0,-650),140);
    T.Contact(Event,Ordinary,Ordinary->Body,FVector::UpVector);
    TestFalse(TEXT("A walnut cannot auto-fracture unrelated ordinary food"),Ordinary->IsDisposed());
    return true;
}
#endif
