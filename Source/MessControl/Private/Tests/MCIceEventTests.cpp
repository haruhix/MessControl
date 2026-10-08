#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCIceEvent.h"
#include "MCLocomotionSurface.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
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
    FIceWorld(bool bMakeTongue=true)
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
            const FTransform Floor(FRotator::ZeroRotator,FVector::ZeroVector,FVector(30,30,1));
            Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Floor);
            Tongue->SourceMesh=Plane;Tongue->bAutomaticYawns=false;Tongue->FinishSpawning(Floor);
            Tongue->SetActorTickEnabled(false);
        }
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(600,0,100),FRotator::ZeroRotator);
        auto* Controller=World->SpawnActor<APlayerController>();
        Controller->SetAsLocalPlayerController();Controller->Possess(Hero);
        Hero->SetActorTickEnabled(false);Hero->GetCharacterMovement()->SetComponentTickEnabled(false);
        Hero->GetMesh()->SetComponentTickEnabled(false);Hero->ToothPhysics->SetComponentTickEnabled(false);
        Event=World->SpawnActor<AMCIceEvent>();
        Event->CircleSeconds=60;Event->FreezeSeconds=60;Event->IcicleIntervalSeconds=20;
        Place(FVector(600,0,0));
    }
    ~FIceWorld()
    {
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    }
    void Place(FVector Feet)
    {
        FHitResult Floor;
        if(Tongue && Tongue->SurfacePoint(Feet,Floor)) Feet=Floor.ImpactPoint;
        Hero->GetCharacterMovement()->StopMovementImmediately();
        Hero->SetActorLocation(Feet+FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2),
            false,nullptr,ETeleportType::TeleportPhysics);
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
    TestFalse(TEXT("A full cold meter kills the exposed player"),F.Hero->Status->IsAlive());
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
    TestEqual(TEXT("An active winter creates one slippery surface"),F.SlipperyCount(),1);
    F.Event->Stop();
    TestFalse(TEXT("Cancellation stops the winter event"),F.Event->IsActive());
    TestEqual(TEXT("Cancellation removes pending impact warnings"),F.Event->Strikes.Num(),0);
    TestEqual(TEXT("Cancellation removes player freeze state"),F.Event->Players.Num(),0);
    TestEqual(TEXT("Cancellation restores normal locomotion"),F.SlipperyCount(),0);
    TestEqual(TEXT("Cancellation clears the climate amount"),F.Event->FrostAmount(),0.f);
    F.Event->Start();auto* Previous=F.Event;
    F.Event=F.World->SpawnActor<AMCIceEvent>();F.Event->Start();
    TestTrue(TEXT("A second start destroys the previous active winter owner"),Previous->IsActorBeingDestroyed());
    TestEqual(TEXT("Replacement cannot stack slippery surfaces"),F.SlipperyCount(),1);
    F.Clock(F.Event->StartedAt+F.Event->ArrivalSeconds+.05);F.Event->Tick(.01f);
    F.Event->CandyHealth=0;F.Event->Tick(.01f);
    TestTrue(TEXT("Breaking the mint completes the winter event"),F.Event->IsComplete());
    TestEqual(TEXT("Completion removes the slippery surface"),F.SlipperyCount(),0);
    TestEqual(TEXT("Completion removes player freeze state"),F.Event->Players.Num(),0);
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
#endif
