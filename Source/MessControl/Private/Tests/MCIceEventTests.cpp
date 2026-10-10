#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCIceEvent.h"
#include "MCLocomotionSurface.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCInventoryComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include <limits>
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCIceEventTestsPrivate
{
struct FIceWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCTongue* Tongue=nullptr;
    AMCToothCharacter* Hero=nullptr;
    AMCIceEvent* Event=nullptr;
    FIceWorld(bool bMakeTongue=true,FVector TongueScale=FVector(30,30,1))
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        // The fixture drives only winter rules. Sequence/game-mode failure and
        // real arena integration are covered by the SingleDay tests and smoke.
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        if(bMakeTongue)
        {
            auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
#if WITH_EDITOR
            if(Plane) FStaticMeshCompilingManager::Get().FinishCompilation({Plane});
#endif
            const FTransform Floor(FRotator::ZeroRotator,FVector::ZeroVector,TongueScale);
            Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Floor);
            Tongue->SourceMesh=Plane;Tongue->bAutomaticYawns=false;Tongue->FinishSpawning(Floor);
            Tongue->SetActorTickEnabled(false);
        }
        Hero=MakeHero(FVector(600,0,0));
        Event=World->SpawnActor<AMCIceEvent>();
        Event->CircleSeconds=60;Event->FreezeSeconds=60;Event->IcicleIntervalSeconds=20;
        Event->ZoneCrystalIntervalSeconds=300;Event->NovaIntervalSeconds=300;
        Place(FVector(600,0,0));
    }
    AMCToothCharacter* MakeHero(FVector Feet)
    {
        auto* Player=World->SpawnActor<AMCToothCharacter>(Feet+FVector(0,0,100),FRotator::ZeroRotator);
        auto* Controller=World->SpawnActor<APlayerController>();
        Controller->SetAsLocalPlayerController();Controller->Possess(Player);
        Player->SetActorTickEnabled(false);Player->GetCharacterMovement()->SetComponentTickEnabled(false);
        Player->GetMesh()->SetComponentTickEnabled(false);Player->ToothPhysics->SetComponentTickEnabled(false);
        PlacePlayer(Player,Feet);
        return Player;
    }
    ~FIceWorld()
    {
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    }
    void PlacePlayer(AMCToothCharacter* Player,FVector Feet)
    {
        FHitResult Floor;
        if(Tongue && Tongue->SurfacePoint(Feet,Floor)) Feet=Floor.ImpactPoint;
        Player->GetCharacterMovement()->StopMovementImmediately();
        Player->SetActorLocation(Feet+FVector(0,0,Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2),
            false,nullptr,ETeleportType::TeleportPhysics);
    }
    void Place(FVector Feet) { PlacePlayer(Hero,Feet); }
    FVector FloorPoint(FVector Anchor) const
    {
        FHitResult Floor;
        const FVector Point=Tongue->GetActorTransform().TransformPosition(Anchor);
        return Tongue->SurfacePoint(Point,Floor)?Floor.ImpactPoint:Point;
    }
    void Clock(double Time)
    {
        World->GetWorldSettings()->MaxUndilatedFrameTime=3600;
        ++GFrameCounter;World->Tick(LEVELTICK_TimeOnly,FMath::Max(.001f,float(Time-World->GetTimeSeconds())));
    }
    void StartActive()
    {
        Event->Start();Clock(Event->StartedAt+Event->ArrivalSeconds+.05);Event->Tick(.01f);
    }
    void QueueFirstStrike()
    {
        Clock(Event->StartedAt+Event->ArrivalSeconds+5.05);Event->Tick(.01f);
    }
    void StepNative(float Seconds)
    {
        // Run the real swing timer and contact code without allowing unrelated
        // periodic hazards to advance during this interaction fixture.
        Event->SetActorTickEnabled(false);
        constexpr float Dt=1.f/60;
        for(int32 I=0;I<FMath::CeilToInt(Seconds/Dt);++I)
        { ++GFrameCounter;World->Tick(LEVELTICK_All,Dt); }
    }
    int32 SlipperyCount() const
    {
        int32 Count=0;
        for(TActorIterator<AMCLocomotionSurface> It(World);It;++It)
            if(!It->IsActorBeingDestroyed() && It->Priority==150) ++Count;
        return Count;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCIceFreezeMeterTest,"MessControl.IceEvent.MeterWarmsCoolsAndKillsAtFull",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCIceFreezeMeterTest::RunTest(const FString&)
{
    MCIceEventTestsPrivate::FIceWorld F;F.Event->FreezeSeconds=4;F.Event->ThawSeconds=2;F.StartActive();
    if(!TestTrue(TEXT("A valid native tongue starts the actual active ice event"),F.Event->Stage==EMCIceEventStage::Active && !F.Event->bFailed)) return false;
    const FVector Safe=F.Tongue->GetActorTransform().TransformPosition(F.Event->SafeAnchor);
    F.Place(Safe+FVector(600,0,0));
    if(!TestFalse(TEXT("The cold fixture stands outside both warm circles"),F.Event->IsSafePoint(F.Hero->GetActorLocation()-FVector(0,0,F.Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())))) return false;
    F.Event->Tick(1);
    const float Cold=F.Event->FreezeAmount(F.Hero);
    TestTrue(TEXT("Cold increases the meter without damaging health before full"),Cold>0 && Cold<1 && F.Hero->Status->State.Health==F.Hero->Status->State.MaxHealth);
    F.Place(Safe);F.Event->Tick(.25f);
    TestTrue(TEXT("Returning to the warm circle lowers existing cold gradually"),F.Event->FreezeAmount(F.Hero)>0 && F.Event->FreezeAmount(F.Hero)<Cold);
    F.Event->Tick(1);
    TestEqual(TEXT("Continued warmth clears the meter"),F.Event->FreezeAmount(F.Hero),0.f);
    F.Place(Safe+FVector(600,0,0));F.Event->Tick(4.1f);
    TestTrue(TEXT("A full cold meter locks the exposed player inside an ice cube"),F.Hero->IsFreezingToDeath());
    TestTrue(TEXT("The cube hold precedes fatal damage"),F.Hero->Status->IsAlive());
    TestFalse(TEXT("A fully frozen player can no longer use tools"),F.Hero->CanWork());
    TestFalse(TEXT("Other damage cannot bypass the cube hold"),F.Hero->Status->Damage(100));
    F.Clock(F.Hero->FreezeDeathStartedAt+AMCToothCharacter::FreezeDeathHoldSeconds+.05f);
    F.Hero->Tick(.01f);
    TestFalse(TEXT("The player dies when the frozen cube shatters"),F.Hero->Status->IsAlive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCIcicleFixedWarningTest,"MessControl.IceEvent.IcicleWarningStaysFixedAndAllowsDodge",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCIcicleFixedWarningTest::RunTest(const FString&)
{
    MCIceEventTestsPrivate::FIceWorld F;F.StartActive();F.QueueFirstStrike();
    if(!TestEqual(TEXT("The real event queues one player-targeted warning"),F.Event->Strikes.Num(),1)) return false;
    const FVector Anchor=F.Event->Strikes[0].Anchor;
    const double Impact=F.Event->Strikes[0].ImpactAt;
    const float Health=F.Hero->Status->State.Health;
    F.Place(F.Tongue->GetActorTransform().TransformPosition(Anchor)+FVector(450,450,0));
    F.Clock(Impact-.2);F.Event->Tick(.01f);
    TestTrue(TEXT("Moving after the warning cannot retarget its anchor"),F.Event->Strikes[0].Anchor.Equals(Anchor,.001));
    TestFalse(TEXT("The warning cannot damage the player before impact"),F.Event->Strikes[0].bImpacted);
    F.Clock(Impact+.05);F.Event->Tick(.01f);
    TestTrue(TEXT("The marked strike resolves at its scheduled time"),F.Event->Strikes[0].bImpacted);
    TestEqual(TEXT("Leaving the marked area avoids icicle damage"),F.Hero->Status->State.Health,Health);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCIcicleOneImpactTest,"MessControl.IceEvent.IcicleDamagesMarkedAreaOnlyOnce",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCIcicleOneImpactTest::RunTest(const FString&)
{
    MCIceEventTestsPrivate::FIceWorld F;F.StartActive();F.QueueFirstStrike();
    if(!TestEqual(TEXT("The stationary fixture receives one warning"),F.Event->Strikes.Num(),1)) return false;
    const double Impact=F.Event->Strikes[0].ImpactAt;
    const float Health=F.Hero->Status->State.Health;
    F.Clock(Impact+.05);F.Event->Tick(.01f);
    TestTrue(TEXT("Staying in the marked area takes one icicle hit"),FMath::IsNearlyEqual(F.Hero->Status->State.Health,Health-F.Event->IcicleDamage));
    const float AfterImpact=F.Hero->Status->State.Health;
    F.Event->Tick(.01f);F.Event->Tick(.01f);
    TestEqual(TEXT("The same strike cannot deal damage on later event ticks"),F.Hero->Status->State.Health,AfterImpact);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCIceCleanupTest,"MessControl.IceEvent.CancelReplaceAndCompleteRestoreArena",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCIceCleanupTest::RunTest(const FString&)
{
    MCIceEventTestsPrivate::FIceWorld F;F.StartActive();F.QueueFirstStrike();
    F.Hero->FreezeLegs(80);
    auto& Crystal=F.Event->ZoneCrystals.AddDefaulted_GetRef();
    Crystal.Anchor=F.Event->SafeAnchor;Crystal.ZoneIndex=F.Event->CircleIndex;Crystal.Health=80;
    F.Event->bNovaWarning=true;F.Event->NovaImpactAt=F.World->GetTimeSeconds()+2;
    TestEqual(TEXT("An active winter creates one slippery surface"),F.SlipperyCount(),1);
    F.Event->Stop();
    TestFalse(TEXT("Cancellation stops the winter event"),F.Event->IsActive());
    TestEqual(TEXT("Cancellation removes pending impact warnings"),F.Event->Strikes.Num(),0);
    TestEqual(TEXT("Cancellation removes player freeze state"),F.Event->Players.Num(),0);
    TestFalse(TEXT("Cancellation releases leg ice without requiring another hit"),F.Hero->HasFrozenLegs());
    TestEqual(TEXT("Cancellation removes warmth blockers"),F.Event->ZoneCrystals.Num(),0);
    TestFalse(TEXT("Cancellation removes the pending frost cone"),F.Event->bNovaWarning);
    TestEqual(TEXT("Cancellation restores normal locomotion"),F.SlipperyCount(),0);
    TestEqual(TEXT("Cancellation clears the climate amount"),F.Event->FrostAmount(),0.f);
    F.Event->Start();auto* Previous=F.Event;
    F.Event=F.World->SpawnActor<AMCIceEvent>();F.Event->Start();
    TestTrue(TEXT("A second start destroys the previous active winter owner"),Previous->IsActorBeingDestroyed());
    TestEqual(TEXT("Replacement cannot stack slippery surfaces"),F.SlipperyCount(),1);
    F.Clock(F.Event->StartedAt+F.Event->ArrivalSeconds+.05);F.Event->Tick(.01f);
    F.Hero->FreezeLegs(80);
    F.Event->CandyHealth=0;F.Event->Tick(.01f);
    TestTrue(TEXT("Breaking the mint completes the winter event"),F.Event->IsComplete());
    TestEqual(TEXT("Completion removes the slippery surface"),F.SlipperyCount(),0);
    TestEqual(TEXT("Completion removes player freeze state"),F.Event->Players.Num(),0);
    TestFalse(TEXT("Completion restores a player's frozen feet"),F.Hero->HasFrozenLegs());
    TestEqual(TEXT("Completion removes candy collision"),F.Event->Body->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCIceAuthorityTest,"MessControl.IceEvent.ClientCannotStartOrApplyGameplay",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCIceAuthorityTest::RunTest(const FString&)
{
    MCIceEventTestsPrivate::FIceWorld F;F.Event->SetRole(ROLE_SimulatedProxy);F.Event->Start();
    TestEqual(TEXT("A client cannot start an authoritative winter"),F.Event->Stage,EMCIceEventStage::Idle);
    TestEqual(TEXT("A client cannot create slippery gameplay"),F.SlipperyCount(),0);
    F.Event->Stage=EMCIceEventStage::Active;F.Event->Tongue=F.Tongue;
    const float Health=F.Event->CandyHealth;
    F.Event->Tick(1);
    TestEqual(TEXT("Client presentation ticks cannot mutate the freeze roster"),F.Event->Players.Num(),0);
    TestFalse(TEXT("A client cannot apply pickaxe damage"),F.Event->HitWithPickaxe(F.Hero,100));
    TestEqual(TEXT("Rejected client damage preserves candy health"),F.Event->CandyHealth,Health);
    auto& Crystal=F.Event->ZoneCrystals.AddDefaulted_GetRef();
    Crystal.Anchor=F.Event->SafeAnchor;Crystal.ZoneIndex=0;Crystal.Health=80;
    F.Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
    FVector Point;
    TestFalse(TEXT("A client cannot damage a warming-zone blocker"),F.Event->HitObstructionWithPickaxe(F.Hero,100,Point));
    TestEqual(TEXT("Rejected client blocker damage preserves its health"),F.Event->ZoneCrystals[0].Health,80.f);
    F.Hero->SetRole(ROLE_SimulatedProxy);F.Hero->FreezeLegs(80);
    TestFalse(TEXT("A client cannot author its own frozen-leg state"),F.Hero->HasFrozenLegs());
    F.Hero->SetRole(ROLE_Authority);F.Hero->FreezeLegs(80);F.Hero->SetRole(ROLE_SimulatedProxy);
    TestFalse(TEXT("A client cannot break replicated leg ice"),F.Hero->HitFrozenLegsWithPickaxe(F.Hero,100));
    TestEqual(TEXT("Rejected client leg damage preserves ice health"),F.Hero->IceLegHealth,80.f);
    F.Hero->SetRole(ROLE_Authority);F.Hero->ClearFrozenLegs();
    F.Event->SetRole(ROLE_Authority);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCIceMissingSurfaceTest,"MessControl.IceEvent.MissingTongueFailsWithoutLeakingGameplay",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCIceMissingSurfaceTest::RunTest(const FString&)
{
    MCIceEventTestsPrivate::FIceWorld F(false);F.Event->Start();
    TestTrue(TEXT("An event without usable tongue reports failed setup"),F.Event->bFailed);
    TestFalse(TEXT("Failed setup cannot be mistaken for successful completion"),F.Event->IsComplete());
    TestFalse(TEXT("Failed setup stops active winter rules"),F.Event->IsActive());
    TestEqual(TEXT("Failed setup does not leave a slippery surface"),F.SlipperyCount(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCIceCircleProgressionTest,"MessControl.IceEvent.WarmZoneLifetimeAcceleratesToTwentySeconds",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCIceCircleProgressionTest::RunTest(const FString&)
{
    MCIceEventTestsPrivate::FIceWorld F;
    F.Event->CircleSeconds=45;F.Event->CircleStepSeconds=5;F.Event->MinCircleSeconds=20;F.Event->CircleOverlapSeconds=10;
    F.StartActive();
    const TArray<float> Durations={45,40,35,30,25,20,20,20};
    for(int32 I=0;I<Durations.Num();++I)
        TestEqual(*FString::Printf(TEXT("Zone %d has the agreed usable lifetime"),I+1),F.Event->CircleDuration(I),Durations[I]);
    TestEqual(TEXT("A long event never shortens a warming zone below twenty seconds"),F.Event->CircleDuration(1000),20.f);
    TestEqual(TEXT("A newly spawned warm zone begins at its full radius"),F.Event->SafeRadius(),F.Event->CircleRadius,.5f);
    F.Clock(F.Event->CircleStartedAt+34.9);F.Event->Tick(.01f);
    TestTrue(TEXT("The usable warm area gradually shrinks during its own lifetime"),F.Event->SafeRadius()<F.Event->CircleRadius && F.Event->SafeRadius()>F.Event->MinCircleRadius);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCIceCircleOverlapTest,"MessControl.IceEvent.PeripheralZonesOverlapAndCountLifetimeFromSpawn",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCIceCircleOverlapTest::RunTest(const FString&)
{
    MCIceEventTestsPrivate::FIceWorld F;
    F.Event->CircleSeconds=45;F.Event->CircleStepSeconds=5;F.Event->MinCircleSeconds=20;F.Event->CircleOverlapSeconds=10;
    F.Event->IcicleDamage=0;
    F.StartActive();
    const double FirstSpawn=F.Event->CircleStartedAt;
    const FVector Center=F.FloorPoint(F.Event->CandyAnchor),First=F.FloorPoint(F.Event->SafeAnchor);
    const FVector Extent=F.Tongue->Surface->Bounds.BoxExtent;
    TestTrue(TEXT("The first warm zone is peripheral instead of remaining beside the central candy"),
        FVector::DistXY(Center,First)>.55*FMath::Min(Extent.X,Extent.Y));
    F.Clock(FirstSpawn+34.9);F.Event->Tick(.01f);
    TestFalse(TEXT("A second warming zone is not exposed before the transfer window"),F.Event->bNextCircle);
    F.Clock(FirstSpawn+35.01);F.Event->Tick(.01f);
    if(!TestTrue(TEXT("The next warming zone appears with ten seconds of the old zone remaining"),F.Event->bNextCircle)) return false;
    const double SecondSpawn=F.Event->NextCircleStartedAt;
    const FVector Second=F.FloorPoint(F.Event->NextSafeAnchor);
    TestTrue(TEXT("The destination is peripheral as well"),FVector::DistXY(Center,Second)>.55*FMath::Min(Extent.X,Extent.Y));
    TestTrue(TEXT("The destination requires a real crossing between distinct zones"),FVector::DistXY(First,Second)>F.Event->CircleRadius*2);
    TestTrue(TEXT("Both the departing and destination zones actually warm players during the overlap"),F.Event->IsSafePoint(First) && F.Event->IsSafePoint(Second));
    TestEqual(TEXT("The next zone begins shrinking from its appearance time"),F.Event->NextSafeRadius(),F.Event->CircleRadius,.5f);
    F.Clock(FirstSpawn+44.9);F.Event->Tick(.01f);
    TestTrue(TEXT("Both zones remain usable until the departing zone expires"),F.Event->IsSafePoint(First) && F.Event->IsSafePoint(Second));
    TestTrue(TEXT("The departing zone approaches its minimum radius before expiry"),F.Event->SafeRadius()<F.Event->MinCircleRadius+1);
    const float SecondRadiusBeforePromotion=F.Event->NextSafeRadius();
    TestTrue(TEXT("The destination has already shrunk during its initial overlap"),SecondRadiusBeforePromotion<F.Event->CircleRadius-10 && SecondRadiusBeforePromotion>F.Event->MinCircleRadius);
    F.Clock(FirstSpawn+45.01);F.Event->Tick(.01f);
    TestEqual(TEXT("The destination becomes current at the first zone's expiry"),F.Event->CircleIndex,1);
    TestEqual(TEXT("Promotion retains the destination's original spawn clock"),F.Event->CircleStartedAt,SecondSpawn,.001);
    TestFalse(TEXT("The expired zone stops warming players"),F.Event->IsSafePoint(First));
    TestTrue(TEXT("The promoted zone remains warm"),F.Event->IsSafePoint(Second));
    TestEqual(TEXT("Promotion does not reset the zone's already shrinking radius"),F.Event->SafeRadius(),SecondRadiusBeforePromotion,1.f);
    F.Clock(SecondSpawn+29.9);F.Event->Tick(.01f);
    TestFalse(TEXT("The next transfer does not begin early"),F.Event->bNextCircle);
    F.Clock(SecondSpawn+30.01);F.Event->Tick(.01f);
    if(!TestTrue(TEXT("The forty-second zone opens its transfer after thirty actual seconds"),F.Event->bNextCircle)) return false;
    const double ThirdSpawn=F.Event->NextCircleStartedAt;
    F.Clock(SecondSpawn+40.01);F.Event->Tick(.01f);
    TestEqual(TEXT("The second zone expires forty seconds after appearance, including its initial overlap"),F.Event->CircleIndex,2);
    TestEqual(TEXT("The third zone also retains its appearance clock"),F.Event->CircleStartedAt,ThirdSpawn,.001);
    for(int32 Index=3;Index<=7;++Index)
    {
        const double End=F.Event->CircleStartedAt+F.Event->CircleDuration(F.Event->CircleIndex);
        F.Clock(End-F.Event->CircleOverlapSeconds+.01);F.Event->Tick(.01f);
        if(!TestTrue(TEXT("Transfers continue after the third zone into the twenty-second plateau"),F.Event->bNextCircle)) return false;
        TestTrue(TEXT("Later zones remain distinct when the native arena supports a full crossing"),
            FVector::DistXY(F.FloorPoint(F.Event->SafeAnchor),F.FloorPoint(F.Event->NextSafeAnchor))>=F.Event->CircleRadius*2-1);
        const double Appearance=F.Event->NextCircleStartedAt;
        F.Clock(End+.01);F.Event->Tick(.01f);
        TestEqual(TEXT("Each later transfer advances exactly one zone"),F.Event->CircleIndex,Index);
        TestEqual(TEXT("Later zones also retain their actual appearance clocks"),F.Event->CircleStartedAt,Appearance,.001);
    }
    // An offset central crystal on narrow tissue makes the farthest radial
    // shell concentrate beside the old zone. Valid distant tissue still exists.
    MCIceEventTestsPrivate::FIceWorld Asymmetric(true,FVector(30,8,1));
    Asymmetric.Event->CircleSeconds=45;Asymmetric.Event->CircleOverlapSeconds=10;Asymmetric.Event->IcicleDamage=0;
    Asymmetric.StartActive();
    FHitResult CandyFloor,WarmFloor,AlternativeFloor;
    if(!TestTrue(TEXT("The asymmetric tongue has supported crystal, current-zone and distant alternative footprints"),
        Asymmetric.Tongue->InteriorSurfacePoint(FVector(-1000,0,0),330,CandyFloor)
        && Asymmetric.Tongue->GameplaySpawnFootprint(FVector(600,0,0),330,WarmFloor)
        && Asymmetric.Tongue->GameplaySpawnFootprint(FVector(-600,0,0),330,AlternativeFloor))) return false;
    Asymmetric.Event->CandyAnchor=Asymmetric.Tongue->GetActorTransform().InverseTransformPosition(CandyFloor.ImpactPoint);
    Asymmetric.Event->SafeAnchor=Asymmetric.Tongue->GetActorTransform().InverseTransformPosition(WarmFloor.ImpactPoint);
    Asymmetric.Clock(Asymmetric.Event->CircleStartedAt+35.01);Asymmetric.Event->Tick(.01f);
    if(!TestTrue(TEXT("The asymmetric native tongue can produce a transfer zone"),Asymmetric.Event->bNextCircle)) return false;
    TestTrue(TEXT("A farthest-shell preference cannot collapse two zones onto the same side when separated tissue exists"),
        FVector::DistXY(Asymmetric.FloorPoint(Asymmetric.Event->SafeAnchor),Asymmetric.FloorPoint(Asymmetric.Event->NextSafeAnchor))>=Asymmetric.Event->CircleRadius*2-1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCIceCrystalSpawnTest,"MessControl.IceEvent.RuntimeCrystalTemporarilyDisablesItsWarmZone",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCIceCrystalSpawnTest::RunTest(const FString&)
{
    MCIceEventTestsPrivate::FIceWorld F;F.Event->ZoneCrystalIntervalSeconds=5;F.StartActive();
    const double ActiveAt=F.Event->StartedAt+F.Event->ArrivalSeconds;
    F.Clock(ActiveAt+4.9);F.Event->Tick(.01f);
    TestEqual(TEXT("The initial zone starts usable without an immediate blocker"),F.Event->ZoneCrystals.Num(),0);
    F.Clock(ActiveAt+5.01);F.Event->Tick(.01f);
    if(!TestEqual(TEXT("The event creates an actual breakable crystal on its periodic clock"),F.Event->ZoneCrystals.Num(),1)) return false;
    const auto& Crystal=F.Event->ZoneCrystals[0];
    TestEqual(TEXT("The crystal belongs to the active zone"),Crystal.ZoneIndex,F.Event->CircleIndex);
    TestTrue(TEXT("The live crystal disables warming in that zone"),F.Event->IsCircleBlocked(F.Event->CircleIndex));
    TestFalse(TEXT("Standing in the blocked zone does not count as warmth"),F.Event->IsSafePoint(F.FloorPoint(F.Event->SafeAnchor)));
    TestTrue(TEXT("The crystal spawns within the zone that it blocks"),
        FVector::DistXY(F.FloorPoint(Crystal.Anchor),F.FloorPoint(F.Event->SafeAnchor))<F.Event->CircleRadius);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCIceCrystalPickaxeTest,"MessControl.IceEvent.NativePickaxeRestoresOnlyTheBlockedWarmZone",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCIceCrystalPickaxeTest::RunTest(const FString&)
{
    MCIceEventTestsPrivate::FIceWorld F;F.StartActive();
    F.Clock(F.Event->CircleStartedAt+F.Event->CircleDuration(0)-F.Event->CircleOverlapSeconds+.01);F.Event->Tick(.01f);
    if(!TestTrue(TEXT("The blocker fixture has two live warming zones"),F.Event->bNextCircle)) return false;
    const FVector Current=F.FloorPoint(F.Event->SafeAnchor),Next=F.FloorPoint(F.Event->NextSafeAnchor);
    auto& Crystal=F.Event->ZoneCrystals.AddDefaulted_GetRef();
    Crystal.Anchor=F.Event->SafeAnchor;Crystal.ZoneIndex=F.Event->CircleIndex;Crystal.Health=80;
    F.Event->Tick(.01f);
    TestFalse(TEXT("A crystal disables only its own zone"),F.Event->IsSafePoint(Current));
    TestTrue(TEXT("The other active zone still warms players"),F.Event->IsSafePoint(Next));
    F.Place(Current);F.Event->Tick(.5f);
    const float Cold=F.Event->FreezeAmount(F.Hero);
    TestTrue(TEXT("Cold accumulates even inside a blocked warming circle"),Cold>0);
    F.Place(Current+FVector(150,0,0));F.Hero->SetActorRotation(FRotator(0,180,0));
    FVector Point;
    F.Hero->Inventory->ServerSelect(EMCToolSlot::Knife);
    TestFalse(TEXT("A knife cannot break the warming-zone crystal"),F.Event->HitObstructionWithPickaxe(F.Hero,80,Point));
    F.Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
    if(!TestTrue(TEXT("The real tool worker can act at the crystal"),F.Hero->CanWork())) return false;
    const int32 HitsBefore=F.Hero->ConfirmedHitCount;
    F.Hero->SwingBrush();F.StepNative(F.Hero->Inventory->SwingDuration()+.05f);
    TestEqual(TEXT("The first native pickaxe swing confirms one crystal hit"),F.Hero->ConfirmedHitCount,HitsBefore+1);
    TestTrue(TEXT("A partly broken crystal still interrupts warmth"),F.Event->IsCircleBlocked(F.Event->CircleIndex));
    F.Hero->SwingBrush();F.StepNative(F.Hero->Inventory->SwingDuration()+.05f);
    TestEqual(TEXT("Two native swings deliver exactly two crystal hits"),F.Hero->ConfirmedHitCount,HitsBefore+2);
    TestFalse(TEXT("Breaking the crystal removes the warming interruption"),F.Event->IsCircleBlocked(F.Event->CircleIndex));
    TestTrue(TEXT("The original zone immediately becomes usable again"),F.Event->IsSafePoint(Current));
    TestTrue(TEXT("Clearing one zone does not disturb the second"),F.Event->IsSafePoint(Next));
    F.Place(Current);F.Event->Tick(.5f);
    TestTrue(TEXT("The restored warming zone resumes reducing accumulated cold"),F.Event->FreezeAmount(F.Hero)<Cold);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCIcicleSeriesTest,"MessControl.IceEvent.ThreeStrikeSeriesUsesRandomPlayerAndTwelveSecondCooldown",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCIcicleSeriesTest::RunTest(const FString&)
{
    MCIceEventTestsPrivate::FIceWorld F;
    F.Event->IcicleIntervalSeconds=12;F.Event->IcicleSeriesSpacingSeconds=1.2f;F.Event->IcicleDamage=0;
    F.Place(FVector(-700,-500,0));
    auto* Nearby=F.MakeHero(FVector(-650,-500,0));
    auto* Isolated=F.MakeHero(FVector(700,500,0));
    const TArray<AMCToothCharacter*> Candidates={F.Hero,Nearby,Isolated};
    F.StartActive();
    int32 LargestId=0,IsolatedSeries=0,GroupedSeries=0;
    const double FirstSeriesAt=F.Event->StartedAt+F.Event->ArrivalSeconds+5.05;
    for(int32 Series=0;Series<12;++Series)
    {
        const double SeriesAt=FirstSeriesAt+Series*12.01;
        AMCToothCharacter* Chosen=nullptr;
        for(int32 Shot=0;Shot<3;++Shot)
        {
            const double WarningAt=SeriesAt+Shot*(F.Event->IcicleSeriesSpacingSeconds+.01);
            F.Clock(WarningAt);F.Event->Tick(.01f);
            const FMCIcicleStrike* Latest=nullptr;
            for(const auto& Strike:F.Event->Strikes) if(Strike.Id>LargestId && (!Latest || Strike.Id>Latest->Id)) Latest=&Strike;
            if(!TestNotNull(TEXT("Each shot adds a new warning in the real three-strike series"),Latest)) return false;
            TestEqual(TEXT("The series queues one warning at a time"),Latest->Id,LargestId+1);
            LargestId=Latest->Id;
            AMCToothCharacter* Target=nullptr;
            for(auto* Candidate:Candidates)
                if(FVector::DistXY(F.FloorPoint(Latest->Anchor),Candidate->GetActorLocation())<5) Target=Candidate;
            if(!TestNotNull(TEXT("The warning marks a living player instead of an arbitrary arena point"),Target)) return false;
            if(Shot==0) Chosen=Target;
            else TestTrue(TEXT("All three strikes in one series pursue the same chosen player"),Target==Chosen);
            if(Series==0 && Shot<2)
            {
                const FVector FixedAnchor=Latest->Anchor;
                const int32 FixedId=Latest->Id;
                const FVector Feet=Target->GetActorLocation()-FVector(0,0,Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
                F.PlacePlayer(Target,Feet+FVector(0,40,0));F.Event->Tick(.01f);
                const auto* Fixed=F.Event->Strikes.FindByPredicate([FixedId](const FMCIcicleStrike& Strike){return Strike.Id==FixedId;});
                TestTrue(TEXT("A moving series target cannot drag an already telegraphed strike"),Fixed && Fixed->Anchor.Equals(FixedAnchor,.001));
            }
        }
        IsolatedSeries+=Chosen==Isolated;GroupedSeries+=Chosen!=Isolated;
        F.Clock(SeriesAt+11.9);F.Event->Tick(.01f);
        int32 MaximumSeen=LargestId;
        for(const auto& Strike:F.Event->Strikes) MaximumSeen=FMath::Max(MaximumSeen,Strike.Id);
        TestEqual(TEXT("No fourth strike or new series appears before the twelve-second cooldown"),MaximumSeen,LargestId);
    }
    TestEqual(TEXT("Twelve complete series queue exactly thirty-six strikes"),LargestId,36);
    TestTrue(TEXT("Random selection can target an isolated player despite a larger group elsewhere"),IsolatedSeries>0);
    TestTrue(TEXT("Random selection also targets players in the group"),GroupedSeries>0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCIceNovaConeTest,"MessControl.IceEvent.FrostWindTelegraphsAConicalFootFreeze",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCIceNovaConeTest::RunTest(const FString&)
{
    MCIceEventTestsPrivate::FIceWorld F;F.Event->NovaIntervalSeconds=5;F.Event->NovaRange=1000;F.Event->IcicleDamage=0;
    auto* Rear=F.MakeHero(FVector(-600,0,0));
    auto* Side=F.MakeHero(FVector(0,600,0));
    auto* Beyond=F.MakeHero(FVector(1200,0,0));
    F.StartActive();
    F.Clock(F.Event->StartedAt+F.Event->ArrivalSeconds+5.01);F.Event->Tick(.01f);
    if(!TestTrue(TEXT("The central crystal begins a warning before the frost-wind impact"),F.Event->bNovaWarning)) return false;
    const double Impact=F.Event->NovaImpactAt;
    const FVector Center=F.FloorPoint(F.Event->CandyAnchor),Direction=F.Event->NovaDirection.GetSafeNormal2D();
    const FVector Perpendicular(-Direction.Y,Direction.X,0);
    F.Place(Center+Direction*600);F.PlacePlayer(Rear,Center-Direction*600);
    F.PlacePlayer(Side,Center+Perpendicular*600);F.PlacePlayer(Beyond,Center+Direction*1200);
    TestTrue(TEXT("The forward player is inside the warned cone"),F.Event->IsInsideNova(F.Hero->GetActorLocation()));
    TestFalse(TEXT("The rear side is not part of a circular frost nova"),F.Event->IsInsideNova(Rear->GetActorLocation()));
    TestFalse(TEXT("A player beside the cone is outside the wind"),F.Event->IsInsideNova(Side->GetActorLocation()));
    TestFalse(TEXT("Wind has a finite reach"),F.Event->IsInsideNova(Beyond->GetActorLocation()));
    F.Clock(Impact-.1);F.Event->Tick(.01f);
    TestFalse(TEXT("The warning gives the player time to move before legs freeze"),F.Hero->HasFrozenLegs());
    const float Health=F.Hero->Status->State.Health;
    F.Clock(Impact+.01);F.Event->Tick(.01f);
    TestTrue(TEXT("The cone impact encases the exposed player's feet"),F.Hero->HasFrozenLegs());
    TestEqual(TEXT("Foot freeze uses the configured breakable ice health"),F.Hero->IceLegHealth,F.Event->NovaFootHealth);
    TestTrue(TEXT("Players behind, beside and beyond the cone retain movement"),!Rear->HasFrozenLegs() && !Side->HasFrozenLegs() && !Beyond->HasFrozenLegs());
    TestEqual(TEXT("The cone roots feet without dealing a full-body health hit"),F.Hero->Status->State.Health,Health);
    TestTrue(TEXT("Frozen legs do not disable the hands or tool actions"),F.Hero->CanWork());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCIceSelfRescueTest,"MessControl.IceEvent.FrozenPlayerCanBreakOwnFeetWithNativePickaxe",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCIceSelfRescueTest::RunTest(const FString&)
{
    MCIceEventTestsPrivate::FIceWorld F;F.StartActive();
    auto* Move=CastChecked<UMCToothMovementComponent>(F.Hero->GetCharacterMovement());
    Move->SetMovementMode(MOVE_Walking);
    F.Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
    const float PerHit=F.Hero->Inventory->Damage();
    F.Hero->FreezeLegs(PerHit*2);
    if(!TestTrue(TEXT("The standing player is encased in breakable foot ice"),F.Hero->HasFrozenLegs())) return false;
    TestTrue(TEXT("A rooted player can still use tools without another player's assistance"),F.Hero->CanWork());
    TestEqual(TEXT("Foot ice removes locomotion speed"),Move->GetMaxSpeed(),0.f);
    TestEqual(TEXT("Foot ice removes locomotion acceleration"),Move->GetMaxAcceleration(),0.f);
    TestEqual(TEXT("The movement component holds the root in MOVE_None"),uint8(Move->MovementMode),uint8(MOVE_None));
    Move->SetMovementMode(MOVE_Walking);
    TestEqual(TEXT("A movement-mode request cannot bypass frozen feet"),uint8(Move->MovementMode),uint8(MOVE_None));
    TestFalse(TEXT("The root prevents jumping"),Move->CanAttemptJump());
    F.Hero->SetSprintInputHeld(true);
    TestFalse(TEXT("A dash request cannot bypass the root"),F.Hero->IsDashing());
    F.Hero->SetSprintInputHeld(false);
    F.Hero->Inventory->ServerSelect(EMCToolSlot::Knife);
    TestFalse(TEXT("The wrong tool cannot remove foot ice"),F.Hero->HitFrozenLegsWithPickaxe(F.Hero,PerHit));
    F.Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
    TestFalse(TEXT("Invalid damage cannot corrupt foot ice"),F.Hero->HitFrozenLegsWithPickaxe(F.Hero,std::numeric_limits<float>::quiet_NaN()));
    const int32 HitsBefore=F.Hero->ConfirmedHitCount;
    F.Hero->SwingBrush();F.StepNative(F.Hero->Inventory->SwingDuration()+.05f);
    TestEqual(TEXT("The player's ordinary attack hits their own feet"),F.Hero->ConfirmedHitCount,HitsBefore+1);
    TestEqual(TEXT("The first native swing removes one tool-damage amount"),F.Hero->IceLegHealth,PerHit,.001f);
    TestTrue(TEXT("Remaining foot ice continues to hold the player"),F.Hero->HasFrozenLegs() && Move->MovementMode==MOVE_None);
    F.Hero->SwingBrush();F.StepNative(F.Hero->Inventory->SwingDuration()+.05f);
    TestEqual(TEXT("The second ordinary attack confirms another foot-ice hit"),F.Hero->ConfirmedHitCount,HitsBefore+2);
    TestFalse(TEXT("The player can completely free their own feet"),F.Hero->HasFrozenLegs());
    TestEqual(TEXT("Self rescue restores the prior standing movement mode"),uint8(Move->MovementMode),uint8(MOVE_Walking));
    TestTrue(TEXT("Self rescue restores usable movement speed and acceleration"),Move->GetMaxSpeed()>0 && Move->GetMaxAcceleration()>0);
    return true;
}
#endif
