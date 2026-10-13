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

namespace MCIceBossPhaseTestsPrivate
{
/** Native, authoritative rules fixture; no network peer or saved map changes. */
struct FFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCTongue* Tongue=nullptr;
    AMCToothCharacter* Hero=nullptr;
    AMCIceEvent* Ice=nullptr;

    FFixture()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
        auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
#if WITH_EDITOR
        if(Plane) FStaticMeshCompilingManager::Get().FinishCompilation({Plane});
#endif
        const FTransform Floor(FRotator::ZeroRotator,FVector::ZeroVector,FVector(36,36,1));
        Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Floor);
        Tongue->SourceMesh=Plane; Tongue->bAutomaticYawns=false;
        Tongue->FinishSpawning(Floor); Tongue->SetActorTickEnabled(false);
        Hero=MakeHero(FVector(1000,0,0));
        Ice=World->SpawnActor<AMCIceEvent>();
        Ice->SetActorTickEnabled(false);
    }
    ~FFixture()
    {
        World->EndPlay(EEndPlayReason::Quit);
        GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    }
    AMCToothCharacter* MakeHero(FVector Feet)
    {
        auto* Player=World->SpawnActor<AMCToothCharacter>(Feet+FVector(0,0,100),FRotator::ZeroRotator);
        auto* Controller=World->SpawnActor<APlayerController>();
        Controller->SetAsLocalPlayerController(); Controller->Possess(Player);
        Player->SetActorTickEnabled(false);
        Player->GetCharacterMovement()->SetComponentTickEnabled(false);
        Player->GetMesh()->SetComponentTickEnabled(false);
        Player->ToothPhysics->SetComponentTickEnabled(false);
        Place(Player,Feet);
        return Player;
    }
    FVector Floor(FVector Anchor) const
    {
        FHitResult Hit;
        const FVector Point=Tongue->GetActorTransform().TransformPosition(Anchor);
        return Tongue->SurfacePoint(Point,Hit)?Hit.ImpactPoint:Point;
    }
    void Place(AMCToothCharacter* Player,FVector Feet)
    {
        FHitResult Hit;
        if(Tongue->SurfacePoint(Feet,Hit)) Feet=Hit.ImpactPoint;
        Player->GetCharacterMovement()->StopMovementImmediately();
        Player->SetActorLocation(Feet+FVector(0,0,Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2),
            false,nullptr,ETeleportType::TeleportPhysics);
    }
    void Clock(double Time)
    {
        World->GetWorldSettings()->MaxUndilatedFrameTime=3600;
        ++GFrameCounter;
        World->Tick(LEVELTICK_TimeOnly,FMath::Max(.001f,float(Time-World->GetTimeSeconds())));
    }
    bool Start()
    {
        Ice->Start(); Clock(Ice->StartedAt+Ice->ArrivalSeconds+.01); Ice->Tick(.001f);
        // Start and arrival OnRep_State restore active ticking. This rules
        // fixture drives Ice explicitly; native swing timers must not also
        // advance unrelated event attacks or prune a cancelled strike record.
        Ice->SetActorTickEnabled(false);
        return Ice->Stage==EMCIceEventStage::Active && !Ice->bFailed;
    }
    void StepTimers(float Seconds)
    {
        const int32 Frames=FMath::CeilToInt(Seconds*60);
        for(int32 Index=0;Index<Frames;++Index)
        { ++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60); }
    }
    int32 SlipperyCount() const
    {
        int32 Count=0;
        for(TActorIterator<AMCLocomotionSurface> It(World);It;++It)
            if(It->GetOwner()==Ice && !It->IsActorBeingDestroyed()) ++Count;
        return Count;
    }
};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FMCIceBossPhaseTest,"MessControl.IceBoss.ApprovedPhases",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

void FMCIceBossPhaseTest::GetTests(TArray<FString>& Names,TArray<FString>& Commands) const
{
    for(const TCHAR* Scenario:{TEXT("PhaseLatchAndFortySecondCycle"),TEXT("LocalGlobalWarmthAndFatalMeter"),
        TEXT("AbilityCooldownsAndBombDamagePush"),TEXT("PrisonNativePickaxeEscapeAndPunishment"),
        TEXT("GroundWaveAllowsJump")})
    { Names.Add(Scenario); Commands.Add(Scenario); }
}

bool FMCIceBossPhaseTest::RunTest(const FString& Scenario)
{
    using namespace MCIceBossPhaseTestsPrivate;
    FFixture F;
    if(!TestTrue(TEXT("Real native tongue starts the authoritative ice encounter"),F.Start())) return false;
    const double Active=F.World->GetTimeSeconds();
    const FVector Boss=F.Floor(F.Ice->CandyAnchor);
    // Keep the one worker on supported tissue, away from the strong aura and
    // the authored peripheral warm-circle choice unless a scenario moves them.
    F.Ice->SafeAnchor=F.Tongue->GetActorTransform().InverseTransformPosition(Boss+FVector(-1200,0,0));
    F.Place(F.Hero,Boss+FVector(1100,0,0));

    if(Scenario==TEXT("PhaseLatchAndFortySecondCycle"))
    {
        TestEqual(TEXT("Eternal winter creates one slippery arena surface"),F.SlipperyCount(),1);
        F.Ice->CandyHealth=F.Ice->MaxCandyHealth*.5f+.01f;
        F.Ice->UpdateColdThrone(Active);
        TestFalse(TEXT("Health above half cannot start phase two"),F.Ice->bSecondPhase);
        TestFalse(TEXT("Phase one has no global cold"),F.Ice->IsGlobalColdActive());
        F.Ice->CandyHealth=F.Ice->MaxCandyHealth*.5f;
        F.Ice->UpdateColdThrone(Active);
        TestTrue(TEXT("Exactly half health latches phase two"),F.Ice->bSecondPhase);
        TestTrue(TEXT("The phase transition starts global cold"),F.Ice->IsGlobalColdActive());
        TestEqual(TEXT("The first global cold lasts forty seconds"),F.Ice->ColdThroneEndsAt,Active+40.,.002);
        TestEqual(TEXT("The first next cycle begins after forty active plus forty rest seconds"),F.Ice->NextColdThroneAt,Active+80.,.002);
        F.Ice->CandyHealth=F.Ice->MaxCandyHealth;
        F.Clock(Active+39.9); F.Ice->UpdateColdThrone(F.World->GetTimeSeconds());
        TestTrue(TEXT("Health changes cannot reset an entered phase"),F.Ice->bSecondPhase);
        TestTrue(TEXT("Global cold remains active immediately before forty seconds"),F.Ice->IsGlobalColdActive());
        F.Clock(Active+40.01); F.Ice->UpdateColdThrone(F.World->GetTimeSeconds());
        TestFalse(TEXT("Global cold ends at its forty-second deadline"),F.Ice->IsGlobalColdActive());
        TestEqual(TEXT("Rest preserves the original next-cycle deadline"),F.Ice->NextColdThroneAt,Active+80.,.002);
        TestTrue(TEXT("The cooldown getter exposes almost forty seconds of rest"),F.Ice->ColdThroneCooldownRemaining()>39.9f);
        TestEqual(TEXT("The slippery floor remains through the global-cold rest"),F.SlipperyCount(),1);
        F.Clock(Active+79.99); F.Ice->UpdateColdThrone(F.World->GetTimeSeconds());
        TestFalse(TEXT("The next cycle cannot begin before the rest deadline"),F.Ice->IsGlobalColdActive());
        F.Clock(Active+80.01); F.Ice->UpdateColdThrone(F.World->GetTimeSeconds());
        TestTrue(TEXT("The next global cold begins after forty seconds of rest"),F.Ice->IsGlobalColdActive());
        TestEqual(TEXT("A new cycle keeps the original rhythm"),F.Ice->ColdThroneEndsAt,Active+120.,.002);
        // A hitch which skips complete periods must land on the existing clock,
        // rather than inventing forty fresh seconds of cold at resume time.
        F.Clock(Active+205.); F.Ice->UpdateColdThrone(F.World->GetTimeSeconds());
        TestFalse(TEXT("A large time jump lands in the correct rest interval"),F.Ice->IsGlobalColdActive());
        TestEqual(TEXT("A large time jump keeps the next original cycle"),F.Ice->NextColdThroneAt,Active+240.,.002);
        F.Ice->Stop();
        TestFalse(TEXT("Stopping the boss removes global cold"),F.Ice->IsGlobalColdActive());
        TestEqual(TEXT("Stopping the boss removes the slippery arena"),F.SlipperyCount(),0);
        return true;
    }

    if(Scenario==TEXT("LocalGlobalWarmthAndFatalMeter"))
    {
        F.Ice->Players.Reset();
        F.Ice->UpdateFreeze(2.f);
        TestEqual(TEXT("A phase-one player outside the aura gains no freeze"),F.Ice->FreezeAmount(F.Hero),0.f);
        F.Place(F.Hero,Boss+FVector(350,0,0)); F.Ice->UpdateFreeze(.5f);
        const float Strong=F.Ice->FreezeAmount(F.Hero);
        TestTrue(TEXT("The permanent close aura accumulates freeze before phase two"),Strong>0 && Strong<1);
        F.Ice->Players.Reset(); F.Place(F.Hero,Boss+FVector(1100,0,0));
        F.Ice->CandyHealth=F.Ice->MaxCandyHealth*.5f; F.Ice->UpdateColdThrone(Active);
        F.Ice->UpdateFreeze(.5f);
        const float Weak=F.Ice->FreezeAmount(F.Hero);
        TestTrue(TEXT("Phase-two global cold gains freeze more slowly than close aura"),Weak>0 && Weak<Strong);
        TestTrue(TEXT("Starting global cold does not instantly kill a distant worker"),F.Hero->Status->IsAlive() && !F.Hero->IsFreezingToDeath());
        F.Place(F.Hero,F.Floor(F.Ice->SafeAnchor)); F.Ice->UpdateFreeze(.1f);
        TestTrue(TEXT("A far warm zone lowers accumulated freeze rather than just pausing it"),F.Ice->FreezeAmount(F.Hero)<Weak);
        F.Ice->SafeAnchor=F.Tongue->GetActorTransform().InverseTransformPosition(Boss+FVector(350,0,0));
        F.Place(F.Hero,F.Floor(F.Ice->SafeAnchor));
        TestTrue(TEXT("The strong aura closes an overlapping warm circle"),F.Ice->IsCircleBlocked(F.Ice->CircleIndex));
        TestFalse(TEXT("An overlapping warm circle cannot protect from close aura"),F.Ice->IsSafePoint(F.Floor(F.Ice->SafeAnchor)));
        auto& Patch=F.Ice->BrushedWarmPatches.AddDefaulted_GetRef();
        Patch.Anchor=F.Ice->SafeAnchor; Patch.Cleaned=1; Patch.LastBrushedAt=F.World->GetTimeSeconds();
        TestEqual(TEXT("The strong aura immediately closes a brushed warm patch too"),F.Ice->BrushedWarmRadius(Patch),0.f);
        const float BeforeClose=F.Ice->FreezeAmount(F.Hero);
        F.Ice->UpdateFreeze(.1f);
        TestTrue(TEXT("Close aura still gains freeze inside the closed warm zone"),F.Ice->FreezeAmount(F.Hero)>BeforeClose);
        F.Ice->UpdateFreeze(30.f);
        TestEqual(TEXT("The actual meter caps at full"),F.Ice->FreezeAmount(F.Hero),1.f);
        TestTrue(TEXT("Full freeze begins the existing fatal cube sequence"),F.Hero->IsFreezingToDeath());
        TestFalse(TEXT("A fully frozen worker cannot continue tool actions"),F.Hero->CanWork());
        F.Clock(F.Hero->FreezeDeathStartedAt+AMCToothCharacter::FreezeDeathHoldSeconds+.05f);
        F.Hero->Tick(.001f);
        TestFalse(TEXT("The full-meter cube sequence kills the player"),F.Hero->Status->IsAlive());
        return true;
    }

    if(Scenario==TEXT("AbilityCooldownsAndBombDamagePush"))
    {
        F.Ice->NextIceBombAt=F.Ice->NextColdWaveAt=F.Ice->NextIcePrisonAt=Active;
        TestFalse(TEXT("Prison is unavailable during phase one"),F.Ice->BeginBossAttack(EMCCandyAttack::Scream));
        if(!TestTrue(TEXT("A ready phase-one bomb starts normally"),F.Ice->BeginBossAttack(EMCCandyAttack::MiniCandy))) return false;
        TestEqual(TEXT("Bomb cooldown is five seconds"),F.Ice->GetAbilityCooldownRemaining(EMCCandyAttack::MiniCandy),5.f,.002f);
        F.Clock(Active+4.99);
        TestFalse(TEXT("Bomb cannot recast before its independent cooldown"),F.Ice->BeginBossAttack(EMCCandyAttack::MiniCandy));
        F.Clock(Active+5.01);
        TestTrue(TEXT("Bomb becomes eligible after five seconds"),F.Ice->BeginBossAttack(EMCCandyAttack::MiniCandy));
        const FVector Target=F.Floor(F.Ice->BossAttack.TargetAnchor);
        auto* Outside=F.MakeHero(Target+FVector(F.Ice->IceBombRadius+500,0,0));
        // Keep this contact fixture standing so the native pending launch can
        // prove the actual configured push without depending on ragdoll assets.
        F.Hero->ToothPhysics->Settings.FallThreshold=1400;
        F.Place(F.Hero,Target+FVector(80,0,0));
        const float Before=F.Hero->Status->State.Health,OutsideBefore=Outside->Status->State.Health;
        F.Clock(F.Ice->BossAttack.ImpactAt+.001); F.Ice->ResolveIceBomb();
        TestEqual(TEXT("Bomb damages the player in its locked area"),F.Hero->Status->State.Health,Before-F.Ice->IceBombDamage,.001f);
        TestEqual(TEXT("Bomb cannot damage a worker outside its area"),Outside->Status->State.Health,OutsideBefore,.001f);
        const FVector Launch=F.Hero->GetCharacterMovement()->PendingLaunchVelocity;
        TestTrue(TEXT("Bomb sends the configured outward push into native movement"),Launch.X>=F.Ice->IceBombPush-.5 && Launch.Z>0);
        F.Ice->ResolveIceBomb();
        TestEqual(TEXT("The same bomb cannot apply a second damage hit"),F.Hero->Status->State.Health,Before-F.Ice->IceBombDamage,.001f);
        F.Clock(F.Ice->BossAttack.EndsAt+.01);
        F.Ice->NextColdWaveAt=F.World->GetTimeSeconds();
        TestTrue(TEXT("The wave uses its own ready clock"),F.Ice->BeginBossAttack(EMCCandyAttack::FloorHit));
        TestEqual(TEXT("Wave cooldown is ten seconds"),F.Ice->GetAbilityCooldownRemaining(EMCCandyAttack::FloorHit),10.f,.002f);
        F.Clock(F.Ice->BossAttack.EndsAt+.01);
        F.Ice->CandyHealth=F.Ice->MaxCandyHealth*.5f; F.Ice->UpdateColdThrone(F.World->GetTimeSeconds());
        TestTrue(TEXT("Phase two enables a ready prison"),F.Ice->BeginBossAttack(EMCCandyAttack::Scream));
        TestEqual(TEXT("Prison cooldown is fifteen seconds"),F.Ice->GetAbilityCooldownRemaining(EMCCandyAttack::Scream),15.f,.002f);
        return true;
    }

    if(Scenario==TEXT("PrisonNativePickaxeEscapeAndPunishment"))
    {
        F.Ice->CandyHealth=F.Ice->MaxCandyHealth*.5f; F.Ice->UpdateColdThrone(Active);
        F.Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
        if(!TestTrue(TEXT("A phase-two prison starts normally"),F.Ice->BeginBossAttack(EMCCandyAttack::Scream))) return false;
        const float Before=F.Hero->Status->State.Health;
        F.Clock(F.Ice->BossAttack.ImpactAt-.01);
        TestFalse(TEXT("A prison warning cannot freeze feet before impact"),F.Hero->HasFrozenLegs());
        F.Clock(F.Ice->BossAttack.ImpactAt+.001); F.Ice->ResolveScream();
        if(!TestTrue(TEXT("Prison freezes the exposed worker's feet"),F.Hero->HasFrozenLegs())) return false;
        TestEqual(TEXT("Prison deals its configured initial area damage"),F.Hero->Status->State.Health,Before-F.Ice->IcePrisonDamage,.001f);
        if(!TestEqual(TEXT("Prison queues one personal punishment"),F.Ice->Strikes.Num(),1)) return false;
        const int32 PrisonId=F.Ice->Strikes[0].Id;
        const double Impact=F.Ice->Strikes[0].ImpactAt;
        TestEqual(TEXT("Default prison permits four seconds of escape"),Impact-double(F.World->GetTimeSeconds()),4.,.002);
        const int32 Hits=F.Hero->ConfirmedHitCount;
        while(F.Hero->HasFrozenLegs() && F.World->GetTimeSeconds()<Impact-.1)
        {
            // Ordinary native swing presses and contacts, with actual tool
            // damage and authored 160 HP foot ice; no damage/thaw shortcut.
            F.Hero->SwingBrush();
            F.StepTimers(F.Hero->Inventory->SwingDuration()+.015f);
        }
        if(!TestFalse(TEXT("Normal pickaxe swings free the feet within the prison deadline"),F.Hero->HasFrozenLegs())) return false;
        if(!TestTrue(TEXT("Self rescue comes from actual confirmed tool contacts"),F.Hero->ConfirmedHitCount>Hits)) return false;
        auto* Original=F.Ice->Strikes.FindByPredicate([PrisonId](const FMCIcicleStrike& Strike){return Strike.Id==PrisonId;});
        if(!TestNotNull(TEXT("The explicitly driven fixture retains the original cancellation record"),Original)) return false;
        if(!TestTrue(TEXT("Freeing the feet marks the original punishment cancelled"),Original->bCancelled)) return false;
        const float RescuedHealth=F.Hero->Status->State.Health;
        F.Hero->FreezeLegs(F.Ice->NovaFootHealth);
        F.Clock(Impact+.01);
        Original=F.Ice->Strikes.FindByPredicate([PrisonId](const FMCIcicleStrike& Strike){return Strike.Id==PrisonId;});
        if(!TestNotNull(TEXT("The original cancelled record remains before its old deadline is resolved"),Original)) return false;
        F.Ice->ResolveIcicle(*Original);
        TestEqual(TEXT("Re-freezing from another wave cannot revive a rescued prison's icicle"),F.Hero->Status->State.Health,RescuedHealth,.001f);
        TestFalse(TEXT("Cancelled prison has no late impact"),Original->bImpacted);

        // A separate unresolved prison still punishes a stationary worker.
        F.Ice->BossAttack=FMCCandyAttackState(); F.Ice->Strikes.Reset();
        F.Ice->NextIcePrisonAt=F.World->GetTimeSeconds();
        if(!TestTrue(TEXT("A second independent prison can start"),F.Ice->BeginBossAttack(EMCCandyAttack::Scream))) return false;
        F.Clock(F.Ice->BossAttack.ImpactAt+.001); F.Ice->ResolveScream();
        if(!TestEqual(TEXT("The unresolved prisoner receives a fresh warning"),F.Ice->Strikes.Num(),1)) return false;
        const int32 TrappedPrisonId=F.Ice->Strikes[0].Id;
        const double TrappedImpact=F.Ice->Strikes[0].ImpactAt;
        const float TrappedHealth=F.Hero->Status->State.Health;
        F.Clock(TrappedImpact+.001);
        auto* Trapped=F.Ice->Strikes.FindByPredicate([TrappedPrisonId](const FMCIcicleStrike& Strike){return Strike.Id==TrappedPrisonId;});
        if(!TestNotNull(TEXT("The trapped prison record exists at its deadline"),Trapped)) return false;
        F.Ice->ResolveIcicle(*Trapped);
        TestTrue(TEXT("An unresolved prison impacts at its deadline"),Trapped->bImpacted);
        TestEqual(TEXT("An unresolved prison's icicle damages the stationary worker"),F.Hero->Status->State.Health,TrappedHealth-F.Ice->IcicleDamage,.001f);
        F.Ice->ResolveIcicle(*Trapped);
        TestEqual(TEXT("Prison icicle damage applies only once"),F.Hero->Status->State.Health,TrappedHealth-F.Ice->IcicleDamage,.001f);
        return true;
    }

    if(Scenario==TEXT("GroundWaveAllowsJump"))
    {
        F.Ice->NextColdWaveAt=Active;
        if(!TestTrue(TEXT("The native ground wave starts normally"),F.Ice->BeginBossAttack(EMCCandyAttack::FloorHit))) return false;
        auto* Jumping=F.MakeHero(Boss+FVector(400,0,0));
        auto* Standing=F.MakeHero(Boss+FVector(-400,0,0));
        F.Place(Jumping,Boss+FVector(400,0,0));
        Jumping->SetActorLocation(Jumping->GetActorLocation()+FVector(0,0,45),false,nullptr,ETeleportType::TeleportPhysics);
        Jumping->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
        const float JumpHealth=Jumping->Status->State.Health,StandHealth=Standing->Status->State.Health;
        TestTrue(TEXT("A modest airborne sole clearance passes the real wave escape check"),Jumping->ClearsGroundWave(12.f));
        F.Clock(F.Ice->BossAttack.ImpactAt+F.Ice->WaveTravelSeconds*.25);
        F.Ice->ResolveNova(); F.Ice->UpdateGroundWave(F.World->GetTimeSeconds());
        TestFalse(TEXT("The wave leaves a player with clear airborne feet mobile"),Jumping->HasFrozenLegs());
        TestTrue(TEXT("The same wave freezes an exposed grounded player's feet"),Standing->HasFrozenLegs());
        TestEqual(TEXT("The cold wave itself does not deal HP damage to the jumper"),Jumping->Status->State.Health,JumpHealth);
        TestEqual(TEXT("The cold wave itself does not deal HP damage to the grounded worker"),Standing->Status->State.Health,StandHealth);
        return true;
    }
    AddError(TEXT("Unknown ice boss scenario"));
    return false;
}
#endif
