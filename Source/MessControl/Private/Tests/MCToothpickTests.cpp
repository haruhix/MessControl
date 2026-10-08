#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCToothpick.h"
#include "MCTongue.h"
#include "MCMouthSurface.h"
#include "MCTutorialDirector.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCInventoryComponent.h"
#include "MCHazardWave.h"
#include "MCRewardChest.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCToothpickTestsPrivate
{
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCGameState* State=nullptr;
    AMCTongue* Tongue=nullptr;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/MessControl.MCGameMode"));World->SetGameMode(URL);
        auto* Mode=World->GetAuthGameMode<AMCGameMode>();Mode->bUseAdaptiveDirector=false;
        World->InitializeActorsForPlay(URL);World->BeginPlay();Mode->SetActorTickEnabled(false);
        State=World->GetGameState<AMCGameState>();State->Phase=EMCShiftPhase::Working;State->bTutorialActive=true;
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/Arena/SM_TongueSurface.SM_TongueSurface"));
#if WITH_EDITOR
        if(Mesh) FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
        Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FTransform::Identity);
        Tongue->SourceMesh=Mesh;Tongue->FinishSpawning(FTransform::Identity);Tongue->SetActorTickEnabled(false);
    }
    ~FWorld() {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    AMCToothCharacter* Worker(FVector Position)
    {
        auto* PC=World->SpawnActor<APlayerController>();
        auto* Hero=World->SpawnActor<AMCToothCharacter>(Position,FRotator::ZeroRotator);
        Hero->SetActorTickEnabled(false);Hero->GetCharacterMovement()->DisableMovement();PC->Possess(Hero);
        Hero->Status->Settings.Reach=250;return Hero;
    }
    void Advance(float Seconds,AMCToothpick* Pick)
    {
        // UWorld clamps oversized deltas through WorldSettings::FixupDeltaSeconds.
        // Advance the actual server clock with ordinary frames rather than a .7/1 s tick.
        for(float Elapsed=0;Elapsed<Seconds-KINDA_SMALL_NUMBER;Elapsed+=.1f)
        {
            World->Tick(LEVELTICK_All,FMath::Min(.1f,Seconds-Elapsed));
            Pick->Tick(.01f);
        }
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCToothpickSequenceTest,"MessControl.Tutorial.ToothpickPullBreakHealGatesAndSafety",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCToothpickSequenceTest::RunTest(const FString&)
{
    MCToothpickTestsPrivate::FWorld Fixture;FHitResult Floor;
    if(!TestTrue(TEXT("The authored tongue has a safe center footprint"),Fixture.Tongue->InteriorSurfacePoint(Fixture.Tongue->Surface->Bounds.GetBox().GetCenter(),85,Floor))) return false;
    auto* Hero=Fixture.Worker(Floor.ImpactPoint+FVector(-100,0,100));
    auto* Partner=Fixture.Worker(Floor.ImpactPoint+FVector(-110,40,100));
    auto* Pick=Fixture.World->SpawnActor<AMCToothpick>();
    if(!TestTrue(TEXT("Impaling creates the lesion on the real tongue"),Pick->Impale(Fixture.Tongue,Floor.ImpactPoint,true,AMCTutorialDirector::TutorialFoodBatch))) return false;
    auto* Ulcer=Pick->Ulcer.Get();
    TestTrue(TEXT("Impaling immediately schedules a pain wave"),Fixture.Tongue->IsMotionActive());
    TestTrue(TEXT("The lesion follows the tongue"),Ulcer->GetTongue()==Fixture.Tongue);
    Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
    TestFalse(TEXT("The pickaxe cannot break a toothpick still inside the tongue"),Pick->HitWithPickaxe(Hero,40));
    TestFalse(TEXT("Spray cannot bypass extraction and breaking"),Ulcer->Treat(Hero,.2f));
    ++GFrameCounter;TestTrue(TEXT("A nearby worker can begin extraction"),Pick->TryPull(Hero,.2f));
    const float Partial=Pick->PullProgress;
    Pick->TryPull(Partner,.2f);TestEqual(TEXT("Two workers cannot double credit one server frame"),Pick->PullProgress,Partial);
    TestFalse(TEXT("Invalid time cannot advance extraction"),Pick->TryPull(Hero,-1));
    TestEqual(TEXT("Interrupted extraction preserves progress"),Pick->PullProgress,Partial);
    for(int32 I=0;I<8 && Pick->State==EMCToothpickState::Impaled;++I) {++GFrameCounter;Pick->TryPull(Hero,.2f);}
    if(!TestEqual(TEXT("Extraction produces a separate breakable object"),Pick->State,EMCToothpickState::Extracted)) return false;
    TestTrue(TEXT("Extracted toothpick still blocks treatment"),Ulcer->bTreatmentBlocked);
    Hero->Inventory->ServerSelect(EMCToolSlot::Knife);
    TestFalse(TEXT("Knife cannot substitute for the pickaxe"),Pick->HitWithPickaxe(Hero,40));
    Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
    Hero->SetActorRotation(FRotator(0,180,0));
    TestFalse(TEXT("Facing away still rejects a swing at the lying shaft"),Pick->CanReceivePickaxeHit(Hero));
    Hero->SetActorRotation(FRotator::ZeroRotator);
    TestTrue(TEXT("Standing alongside the shaft can still aim at its visible center"),Pick->CanReceivePickaxeHit(Hero));
    if(!TestTrue(TEXT("Pickaxe breaks the extracted toothpick"),Pick->HitWithPickaxe(Hero,40))) return false;
    TestFalse(TEXT("Broken toothpick unlocks spray"),Ulcer->bTreatmentBlocked);
    TestFalse(TEXT("A second strike cannot complete it twice"),Pick->HitWithPickaxe(Partner,40));
    const float MouthHealth=Fixture.State->MouthHealth;
    Ulcer->DamagePerSecond=100;Ulcer->DisturbDamage=100;Ulcer->Tick(.2f);Ulcer->Disturb();
    TestEqual(TEXT("Only the tutorial lesion suppresses mouth damage"),Fixture.State->MouthHealth,MouthHealth);
    Hero->Inventory->ServerSelect(EMCToolSlot::Spray);
    ++GFrameCounter;TestTrue(TEXT("Unlocked wound accepts continuous treatment"),Ulcer->Treat(Hero,.2f));
    const float Healing=Ulcer->Healing;Ulcer->Tick(.2f);
    TestEqual(TEXT("Releasing spray preserves wound progress"),Ulcer->Healing,Healing);
    for(int32 I=0;I<45 && !Ulcer->IsHealed();++I) {++GFrameCounter;Ulcer->Treat(Hero,.2f);}
    Pick->Tick(.01f);TestTrue(TEXT("Wound completion survives its delayed destruction"),Pick->IsWoundHealed());
    TestEqual(TEXT("Training mechanics do not award ordinary task XP"),Hero->GetPlayerState<AMCPlayerState>()->Points,0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCShortTutorialSharedTargetTest,"MessControl.Tutorial.ShortFlowSharedToothpickAndFinish",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCShortTutorialSharedTargetTest::RunTest(const FString&)
{
    MCToothpickTestsPrivate::FWorld Fixture;
    auto* First=Fixture.Worker(FVector(-100,-200,150));auto* Second=Fixture.Worker(FVector(-100,200,150));
    auto* Director=Fixture.World->SpawnActor<AMCTutorialDirector>();Director->SetActorTickEnabled(false);
    Director->Start(nullptr,true);Director->SetLoaded(First->GetPlayerState<AMCPlayerState>());Director->SetLoaded(Second->GetPlayerState<AMCPlayerState>());Director->Tick(.01f);
    TestEqual(TEXT("Short training waits for the loaded roster before its introduction"),Director->Stage,EMCTutorialStage::Intro);
    TestFalse(TEXT("New introduction no longer promises seven survival days"),Director->FairyLine.ToString().Contains(TEXT("семь дней")));
    // Existing tests cover real food/care actions. Enter with both personal cleaning lessons completed.
    Director->Stage=EMCTutorialStage::CoffeeCleanup;Director->StageEndsAt=0;
    for(auto& Player:Director->Players) {Player.StageRequired=1;Player.StageProgress=1;}
    Director->Tick(.01f);
    if(!TestEqual(TEXT("Cleaning leads directly to the new obstacle"),Director->Stage,EMCTutorialStage::ToothpickPull)) return false;
    auto* Pick=Cast<AMCToothpick>(Director->Players[0].GoalTarget.Get());
    if(!TestNotNull(TEXT("The actual runner creates a shared toothpick"),Pick)) return false;
    TestSamePtr(TEXT("Both players see the same object"),Director->Players[1].GoalTarget.Get(),static_cast<AActor*>(Pick));
    TestEqual(TEXT("The object is a team objective"),Director->TeamTasksTotal,1);
    TestEqual(TEXT("The shared toothpick falls visibly before the lesson can be extracted"),Pick->State,EMCToothpickState::Falling);
    Director->Tick(.01f);TestEqual(TEXT("Falling does not skip the extraction stage"),Director->Stage,EMCTutorialStage::ToothpickPull);
    Fixture.Advance(1.f,Pick);
    if(!TestEqual(TEXT("Contact impales the shared toothpick"),Pick->State,EMCToothpickState::Impaled)) return false;
    const FVector Point=Pick->GetActorLocation();First->SetActorLocation(Point+FVector(-100,0,100));Second->SetActorLocation(Point+FVector(-110,40,100));
    for(int32 I=0;I<8 && Pick->State==EMCToothpickState::Impaled;++I) {++GFrameCounter;Pick->TryPull(First,.2f);}
    Director->Tick(.01f);TestEqual(TEXT("One extraction advances the entire team"),Director->Stage,EMCTutorialStage::ToothpickBreak);
    First->Inventory->ServerSelect(EMCToolSlot::Pickaxe);Pick->HitWithPickaxe(First,40);Director->Tick(.01f);
    if(!TestEqual(TEXT("Breaking switches the shared prompt to healing"),Director->Stage,EMCTutorialStage::ToothpickHeal)) return false;
    auto* Ulcer=Pick->Ulcer.Get();First->Inventory->ServerSelect(EMCToolSlot::Spray);
    for(int32 I=0;I<45 && !Ulcer->IsHealed();++I) {++GFrameCounter;Ulcer->Treat(First,.2f);}
    Director->Tick(.01f);TestEqual(TEXT("Healing completes the short sequence"),Director->Stage,EMCTutorialStage::Complete);
    int32 Finished=0;Director->OnTutorialFinished.AddLambda([&Finished](){++Finished;});Director->Tick(.01f);Director->Tick(.01f);
    TestEqual(TEXT("Short flow completes once without the old foam/ready detour"),Finished,1);
    Director->Stop();
    TestTrue(TEXT("Stopping the lesson also removes its deferred impact wound"),!IsValid(Ulcer) || Ulcer->IsActorBeingDestroyed());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCToothpickImpactContactTest,"MessControl.Tutorial.ToothpickFallsOnceAndPainRequiresFreshStep",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCToothpickImpactContactTest::RunTest(const FString&)
{
    MCToothpickTestsPrivate::FWorld Fixture;FHitResult Floor;
    if(!Fixture.Tongue->InteriorSurfacePoint(Fixture.Tongue->Surface->Bounds.GetBox().GetCenter(),85,Floor)) return false;
    auto* Hero=Fixture.Worker(Floor.ImpactPoint+FVector(-200,0,150));
    auto* Pick=Fixture.World->SpawnActor<AMCToothpick>();Pick->SetActorTickEnabled(false);
    if(!TestTrue(TEXT("The toothpick begins its one visible descent"),Pick->BeginFall(Fixture.Tongue,Floor.ImpactPoint,true,AMCTutorialDirector::TutorialFoodBatch))) return false;
    const float StartedZ=Pick->GetActorLocation().Z;
    TestTrue(TEXT("A falling toothpick starts above the tongue"),StartedZ>Floor.ImpactPoint.Z+500);
    TestNull(TEXT("No wound exists before contact"),Pick->Ulcer.Get());
    TestFalse(TEXT("There is no initial pain before contact"),Fixture.Tongue->IsMotionActive());
    TestFalse(TEXT("A player cannot extract it in the air"),Pick->CanPull(Hero));
    Fixture.Advance(.3f,Pick);
    TestTrue(TEXT("The visible descent advances before landing"),Pick->GetActorLocation().Z<StartedZ && Pick->GetActorLocation().Z>Floor.ImpactPoint.Z+100);
    TestFalse(TEXT("Descent still does not emit pain"),Fixture.Tongue->IsMotionActive());
    Fixture.Advance(.7f,Pick);
    if(!TestEqual(TEXT("One contact impales the toothpick"),Pick->State,EMCToothpickState::Impaled)) return false;
    auto* Ulcer=Pick->Ulcer.Get();if(!TestNotNull(TEXT("Contact creates its wound"),Ulcer)) return false;
    TestTrue(TEXT("Impact emits one large radial pain wave"),Fixture.Tongue->Motion.Serial>0 && Fixture.Tongue->Motion.Settings.Shape==EMCTongueShape::RadialWave && Fixture.Tongue->Motion.Settings.Height>=65);
    const int32 ImpactSerial=Fixture.Tongue->Motion.Serial;
    Pick->Tick(10);Ulcer->Tick(10);
    TestEqual(TEXT("Neither toothpick nor idle wound repeats the initial wave"),Fixture.Tongue->Motion.Serial,ImpactSerial);
    int32 Waves=0;for(TActorIterator<AMCHazardWave> It(Fixture.World);It;++It) ++Waves;
    TestEqual(TEXT("The toothpick wound has no periodic micro-waves"),Waves,0);
    Fixture.Tongue->ResetPain();
    Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Hero->SetActorLocation(Ulcer->GetActorLocation()+FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    const float MouthHealth=Fixture.State->MouthHealth;
    Ulcer->Tick(.1f);
    TestTrue(TEXT("A fresh player step can trigger another pain wave"),Fixture.Tongue->IsMotionActive());
    TestEqual(TEXT("Tutorial contact remains safe for mouth health"),Fixture.State->MouthHealth,MouthHealth);
    Fixture.Tongue->ResetPain();Ulcer->Tick(10);
    TestFalse(TEXT("Standing on the wound does not periodically repeat pain"),Fixture.Tongue->IsMotionActive());
    Hero->SetActorLocation(Ulcer->GetActorLocation()+FVector(200,0,100));Ulcer->Tick(1);
    Hero->SetActorLocation(Ulcer->GetActorLocation()+FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));Ulcer->Tick(.1f);
    TestTrue(TEXT("Leaving and stepping back onto the wound can trigger pain again"),Fixture.Tongue->IsMotionActive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCToothpickHandlePriorityTest,"MessControl.Tutorial.ToothpickHandleOutranksNearbyChest",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCToothpickHandlePriorityTest::RunTest(const FString&)
{
    MCToothpickTestsPrivate::FWorld Fixture;FHitResult Floor;
    if(!Fixture.Tongue->InteriorSurfacePoint(Fixture.Tongue->Surface->Bounds.GetBox().GetCenter(),85,Floor)) return false;
    auto* Hero=Fixture.Worker(Floor.ImpactPoint+FVector(-100,0,100));
    auto* Pick=Fixture.World->SpawnActor<AMCToothpick>();Pick->SetActorTickEnabled(false);
    if(!Pick->Impale(Fixture.Tongue,Floor.ImpactPoint,true,AMCTutorialDirector::TutorialFoodBatch)) return false;
    auto* Chest=Fixture.World->SpawnActor<AMCRewardChest>(Floor.ImpactPoint+FVector(-100,220,100),FRotator::ZeroRotator);
    Chest->SetActorTickEnabled(false);Chest->Stage=EMCRewardChestStage::Landed;Chest->OpenRadius=320;
    if(!TestTrue(TEXT("A toothpick remains reachable alongside the landed chest"),Pick->CanPull(Hero))) return false;
    if(!TestSamePtr(TEXT("The normal interaction query finds the toothpick beside the chest"),AMCToothpick::FindPullTarget(Hero),Pick)) return false;
    Hero->SetHandleInputHeld(true);
    if(!TestTrue(TEXT("Ordinary held E selects extraction before the nearby chest"),Hero->bHandling)) return false;
    ++GFrameCounter;Hero->Tick(.2f);
    TestTrue(TEXT("Ordinary character work advances extraction"),Pick->PullProgress>0);
    TestNull(TEXT("Extraction never enters reward interaction"),Hero->RewardInteraction.Get());
    TestEqual(TEXT("The nearby chest remains landed"),Chest->Stage,EMCRewardChestStage::Landed);
    Hero->SetHandleInputHeld(false);
    return true;
}
#endif
