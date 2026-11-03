#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCNutBoss.h"
#include "MCNutSpellProjectile.h"
#include "MCNutCombatEffect.h"
#include "MCVFXPresentationComponent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCTongue.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "ProceduralMeshComponent.h"
#include "UObject/StrongObjectPtr.h"

namespace MCMageFireballPresentationTests
{
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCTongue* Tongue=nullptr;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        // Empty native tongue is sufficient for the airborne/character-contact
        // scenarios. No canonical mesh or package needs to be edited for a floor.
        Tongue=World->SpawnActor<AMCTongue>();Tongue->SetActorTickEnabled(false);
    }
    ~FWorld(){World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    void Step(float Seconds)
    {for(int32 I=0;I<FMath::CeilToInt(Seconds*60);++I){++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60);}}
    AMCNutBoss* Boss()
    {
        auto* B=World->SpawnActor<AMCNutBoss>(FVector(-500,0,130),FRotator::ZeroRotator);
        FMCNutBossSettings Settings;B->ConfigureEncounter(Tongue,nullptr,EMCNutBossRole::Mage,Settings,1,71);
        B->SetActorLocation(FVector(-500,0,130));B->State=EMCNutBossState::Idle;B->SetActorTickEnabled(false);return B;
    }
    AMCToothCharacter* Hero()
    {
        auto* H=World->SpawnActor<AMCToothCharacter>(FVector(300,0,130),FRotator::ZeroRotator);
        H->GetCharacterMovement()->DisableMovement();return H;
    }
};
UMCPerkVFXProfile* DisabledProfile()
{
    auto* P=NewObject<UMCPerkVFXProfile>();FMCPerkVFXSlot Disabled;Disabled.Mode=EMCPerkVFXSlotMode::Disabled;
    for(FName Event:{FName(TEXT("Charge")),FName(TEXT("Release")),FName(TEXT("Flight")),FName(TEXT("Impact"))})P->Slots.Add(Event,Disabled);
    return P;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCMagePreparedHandoff,
    "MessControl.VFX.MageFireball.PreparedHandoffIsImmutable",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCMagePreparedHandoff::RunTest(const FString&)
{
    using namespace MCMageFireballPresentationTests;FWorld T;FString Error;
    auto* Boss=T.Boss();TStrongObjectPtr<UMCPerkVFXProfile> Profile(DisabledProfile());
    if(!TestTrue(TEXT("Declared four-event source prepares before the cast"),Boss->FireballPresentation->PrepareProfileOverride(Profile.Get(),Error)))
    {AddError(Error);return false;}
    const FTransform Pose(FVector(0,0,400));
    auto* Shot=T.World->SpawnActorDeferred<AMCNutSpellProjectile>(AMCNutSpellProjectile::StaticClass(),Pose,Boss,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!TestNotNull(TEXT("Real deferred shot exists before commit"),Shot))return false;
    if(!TestTrue(TEXT("Prepared handoff completes without loading or emitting a receipt"),Boss->FireballPresentation->CopyPreparedSourceTo(
        Boss->FireballPresentation->SourceID,Shot->FireballPresentation,Error))){AddError(Error);return false;}
    Profile->Slots.Reset();Boss->FireballPresentation->PrepareProfileOverride(Profile.Get(),Error);
    EMCPerkVFXSlotMode Mode=EMCPerkVFXSlotMode::UseDefault;
    TestTrue(TEXT("Shot retains the previously committed Flight definition"),Shot->FireballPresentation->GetPreparedEventMode(TEXT("Boss.NutMage.Fireball"),TEXT("Flight"),Mode));
    TestEqual(TEXT("An author edit on the source cannot change the frozen shot"),Mode,EMCPerkVFXSlotMode::Disabled);
    TestEqual(TEXT("An uncommitted target has no receipt before FinishSpawning"),Shot->FireballPresentation->GetHighestReceivedSerial(),0u);
    Shot->FireballPresentation->Events.Pop();
    TestFalse(TEXT("Mismatched event contract refuses atomically"),Boss->FireballPresentation->CopyPreparedSourceTo(Boss->FireballPresentation->SourceID,Shot->FireballPresentation,Error));
    TestTrue(TEXT("Failed replacement preserves the prior prepared shot"),Shot->FireballPresentation->GetPreparedEventMode(TEXT("Boss.NutMage.Fireball"),TEXT("Impact"),Mode)
        && Mode==EMCPerkVFXSlotMode::Disabled);
    Shot->Destroy();return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCMageFireballNativeEvents,
    "MessControl.VFX.MageFireball.NativeEventsPreserveContactDamage",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCMageFireballNativeEvents::RunTest(const FString&)
{
    using namespace MCMageFireballPresentationTests;FWorld T;FString Error;
    auto* Boss=T.Boss();auto* Hero=T.Hero();Boss->Target=Hero;
    TStrongObjectPtr<UMCPerkVFXProfile> Profile(DisabledProfile());
    if(!Boss->FireballPresentation->PrepareProfileOverride(Profile.Get(),Error)){AddError(Error);return false;}
    TArray<FName> Events;TWeakObjectPtr<AMCNutSpellProjectile> ActualShot;bool BeforeBegin=false,FrozenBeforeBegin=false;
    Boss->FireballPresentation->OnEventAccepted.AddLambda([&](FName Event,EMCVFXPresentationResult Result){
        if(Result==EMCVFXPresentationResult::Suppressed)Events.Add(Event);
    });
    Boss->OnFireballSpawned.AddLambda([&](AMCNutSpellProjectile* Shot){
        ActualShot=Shot;BeforeBegin=!Shot->HasActorBegunPlay() && Shot->SourceActor==Boss && Shot->EpisodeID.IsValid();
        EMCPerkVFXSlotMode Mode=EMCPerkVFXSlotMode::UseDefault;
        FrozenBeforeBegin=Shot->FireballPresentation->GetPreparedEventMode(TEXT("Boss.NutMage.Fireball"),TEXT("Flight"),Mode)
            && Mode==EMCPerkVFXSlotMode::Disabled;
        Shot->FireballPresentation->OnEventAccepted.AddLambda([&](FName Event,EMCVFXPresentationResult Result){
            if(Result==EMCVFXPresentationResult::Suppressed)Events.Add(Event);
        });
    });
    // Test friendship schedules the same native attack as the laboratory;
    // no synthetic receipt or manually invoked impact replaces gameplay.
    Boss->BeginAttack(EMCNutBossAttack::Fireball);
    TestTrue(TEXT("Actual accepted telegraph emits Charge"),Events.Num()==1 && Events[0]==TEXT("Charge"));
    const FGuid CastID=Boss->FireballEpisodeID;const int32 Seed=Boss->AttackSeed;
    T.Step(Boss->BossSettings.FireballWindup+.03f);Boss->ExecuteAttack(1.f/60);
    if(!TestTrue(TEXT("Native release creates the actual configured shot before Flight"),ActualShot.IsValid() && BeforeBegin && FrozenBeforeBegin))return false;
    TestTrue(TEXT("Release and Flight are genuine ordered action boundaries"),Events.Num()==3 && Events[1]==TEXT("Release") && Events[2]==TEXT("Flight"));
    TestEqual(TEXT("Projectile retains the original native attack seed"),ActualShot->Flight.Seed,Seed);
    TestEqual(TEXT("Cast/projectile episode identity is shared without another gameplay RNG sample"),ActualShot->EpisodeID,CastID);
    const float Before=Hero->Status->State.Health;
    T.Step(.5f);
    TestTrue(TEXT("Actual swept contact emits one Impact after Flight"),Events.Num()==4 && Events[3]==TEXT("Impact"));
    TestTrue(TEXT("Projectile uses its native terminal guard"),ActualShot.IsValid() && ActualShot->Impact.bImpacted && ActualShot->DamageApplications==1);
    TestTrue(TEXT("Disabled accents do not disable the native 80 percent current-health hit"),FMath::IsNearlyEqual(Hero->Status->State.Health,Before*.2f,.01f));
    int32 ImpactCues=0;
    for(TActorIterator<AMCNutCombatEffect> It(T.World);It;++It)if(It->Cue.Type==EMCNutCombatCue::FireImpact) {
        ++ImpactCues;TestTrue(TEXT("The native ground-ring carrier remains when accents are disabled"),It->Cue.bSuppressCosmeticAccents);
    }
    TestEqual(TEXT("Impact preserves exactly one native danger-ring carrier"),ImpactCues,1);
    T.Step(.05f);TestEqual(TEXT("No repeated impact or extra native damage"),Events.Num(),4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCMageFireballDefaultAndCancel,
    "MessControl.VFX.MageFireball.DefaultFlightAndExplicitCancellation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCMageFireballDefaultAndCancel::RunTest(const FString&)
{
    using namespace MCMageFireballPresentationTests;FWorld T;FString Error;
    auto* Boss=T.Boss();TStrongObjectPtr<UMCPerkVFXProfile> Native(NewObject<UMCPerkVFXProfile>());
    if(!Boss->FireballPresentation->PrepareProfileOverride(Native.Get(),Error)){AddError(Error);return false;}
    TArray<FName> Events;Boss->OnFireballSpawned.AddLambda([&](AMCNutSpellProjectile* Shot){
        Shot->FireballPresentation->OnEventAccepted.AddLambda([&](FName Event,EMCVFXPresentationResult Result){
            if(Result==EMCVFXPresentationResult::UseDefault)Events.Add(Event);
        });
    });
    auto* Shot=AMCNutSpellProjectile::Spawn(Boss,T.Tongue,FVector(-200,0,400),FVector(900,0,400),600,18,28,Boss->FireballPresentation);
    if(!TestNotNull(TEXT("Default native flight starts"),Shot))return false;
    TestTrue(TEXT("Default slot keeps native accents and walnut body"),Shot->Flight.bUseNativeAccents && Shot->Nut->IsVisible());
    TestTrue(TEXT("Native flight is observed before cancellation"),Events.Num()==1 && Events[0]==TEXT("Flight"));
    const TWeakObjectPtr<AMCNutSpellProjectile> WeakShot=Shot;Shot->Cancel();T.Step(.1f);
    TestTrue(TEXT("Explicit cancellation destroys the actual shot"),!WeakShot.IsValid() || WeakShot->IsActorBeingDestroyed());
    TestEqual(TEXT("Cancellation does not manufacture a terminal Impact"),Events.Num(),1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCMageFireballAcceptedTail,
    "MessControl.VFX.MageFireball.TerminalTailOutlivesProjectile",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCMageFireballAcceptedTail::RunTest(const FString&)
{
    using namespace MCMageFireballPresentationTests;FWorld T;FString Error;
    auto* Boss=T.Boss();
    TStrongObjectPtr<UNiagaraSystem> Original(LoadObject<UNiagaraSystem>(nullptr,
        TEXT("/Game/Gameplay/VFX/NutCombat/NS_NutFireImpact.NS_NutFireImpact")));
    if(!TestNotNull(TEXT("Existing native impact source is available for explicit preparation"),Original.Get()))return false;
#if WITH_EDITORONLY_DATA
    Original->WaitForCompilationComplete(false,false);
#endif
    if(!TestTrue(TEXT("Existing effect is ready before any native launch"),Original->IsReadyToRun()))return false;
    TStrongObjectPtr<UMCVFXComposition> Recipe(NewObject<UMCVFXComposition>());
    FMCVFXCompositionLayer L;L.System=Original.Get();L.StartSeconds=2.f;L.EmissionSeconds=.1f;Recipe->Layers.Add(L);
    TStrongObjectPtr<UMCPerkVFXProfile> Profile(NewObject<UMCPerkVFXProfile>());
    FMCPerkVFXSlot Custom;Custom.Mode=EMCPerkVFXSlotMode::Composition;Custom.Composition=Recipe.Get();
    Profile->Slots.Add(TEXT("Impact"),Custom);
    if(!Boss->FireballPresentation->PrepareProfileOverride(Profile.Get(),Error)){AddError(Error);return false;}
    int32 ActualImpacts=0;
    Boss->OnFireballSpawned.AddLambda([&](AMCNutSpellProjectile* Shot){
        Shot->FireballPresentation->OnEventAccepted.AddLambda([&](FName Event,EMCVFXPresentationResult Result){
            if(Event==TEXT("Impact") && Result==EMCVFXPresentationResult::Played)++ActualImpacts;
        });
    });
    auto* Shot=AMCNutSpellProjectile::Spawn(Boss,T.Tongue,FVector(-200,0,400),FVector(200,0,400),2000,18,28,Boss->FireballPresentation);
    if(!TestNotNull(TEXT("A native airborne shot is committed"),Shot))return false;
    const TWeakObjectPtr<AMCNutSpellProjectile> ShotLife=Shot;T.Step(.27f);
    TestEqual(TEXT("Only the actual terminal boundary accepts the custom Impact"),ActualImpacts,1);
    TWeakObjectPtr<AMCPerkVFXCompositionPlayer> Tail;
    for(TActorIterator<AMCPerkVFXCompositionPlayer> It(T.World);It;++It)if(It->IsRunning())Tail=*It;
    if(!TestTrue(TEXT("Actual accepted Impact owns its delayed layer"),Tail.IsValid() && Tail->GetPendingLayerCount()==1))return false;
    T.Step(.8f);
    TestTrue(TEXT("Native .7-second terminal lifespan removes the actual projectile"),!ShotLife.IsValid() || ShotLife->IsActorBeingDestroyed());
    TestTrue(TEXT("Prepared terminal tail survives normal owner destruction"),Tail.IsValid() && Tail->IsRunning() && Tail->GetPendingLayerCount()==1);
    UMCVFXPresentationComponent::StopWorldPresentations(T.World);
    TestTrue(TEXT("Explicit run cleanup cancels its still-pending terminal layer"),!Tail.IsValid() || !Tail->IsRunning());
    return true;
}
#endif
