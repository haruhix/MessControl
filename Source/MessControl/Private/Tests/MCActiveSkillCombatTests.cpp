#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCActiveSkillImpact.h"
#include "MCGrenadeSkillAction.h"
#include "MCRocketSkillAction.h"
#include "MCArenaTooth.h"
#include "MCBossCharacter.h"
#include "MCCataclysmJelly.h"
#include "MCCombatFeedbackComponent.h"
#include "MCFoodActor.h"
#include "MCGameState.h"
#include "MCNutBoss.h"
#include "MCNutEnemy.h"
#include "MCPerkComponent.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCActiveSkillCombatTests
{
// These physics/ownership tests do not exercise art or readiness. Avoid loading
// skill presentation packages into the fixture; restore exact CDO references.
struct FScopedNoSkillArt
{
    struct FSaved
    {
        AMCActiveSkillAction* CDO;
        TSoftObjectPtr<UStaticMesh> Prop;
        TSoftObjectPtr<UNiagaraSystem> Start,Loop,Impact,End,Warning;
    };
    TArray<FSaved> Saved;
    FScopedNoSkillArt()
    {
        for(auto* CDO:{static_cast<AMCActiveSkillAction*>(GetMutableDefault<AMCGrenadeSkillAction>()),
                       static_cast<AMCActiveSkillAction*>(GetMutableDefault<AMCRocketSkillAction>())}) {
            Saved.Add({CDO,CDO->PropAsset,CDO->StartVFX,CDO->LoopVFX,CDO->ImpactVFX,CDO->EndVFX,CDO->WarningVFX});
            CDO->PropAsset.Reset();CDO->StartVFX.Reset();CDO->LoopVFX.Reset();CDO->ImpactVFX.Reset();CDO->EndVFX.Reset();CDO->WarningVFX.Reset();
        }
    }
    ~FScopedNoSkillArt()
    {
        for(const auto& S:Saved) {
            S.CDO->PropAsset=S.Prop;S.CDO->StartVFX=S.Start;S.CDO->LoopVFX=S.Loop;
            S.CDO->ImpactVFX=S.Impact;S.CDO->EndVFX=S.End;S.CDO->WarningVFX=S.Warning;
        }
    }
};

struct FWorld
{
    FScopedNoSkillArt Art;
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Hero=nullptr;
    AMCPlayerController* Controller=nullptr;
    AMCPlayerState* Player=nullptr;
    UMCPerkComponent* Perks=nullptr;
    TStrongObjectPtr<UStaticMesh> Cube;
    bool HadCPU=false,HadDirty=false;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));World->SetGameMode(URL);
        World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* GS=World->SpawnActor<AMCGameState>();World->SetGameState(GS);GS->Phase=EMCShiftPhase::Working;GS->bLobbyWaiting=false;
        Box(FVector(0,0,9980),FVector(5000,5000,20));
        Controller=World->SpawnActor<AMCPlayerController>();
        Player=World->SpawnActorDeferred<AMCPlayerState>(AMCPlayerState::StaticClass(),FTransform::Identity,Controller);
        auto* Table=NewObject<UDataTable>(World);Table->RowStruct=FMCPerkDefinition::StaticStruct();
        FMCPerkDefinition Grenade;Grenade.Kind=EMCPerkKind::Consumable;Grenade.InitialCharges=1;
        Grenade.EffectClass=UMCGrenadeSkillPerkEffect::StaticClass();
        Grenade.Parameters={{TEXT("FuseSeconds"),.8f},{TEXT("ThrowSpeed"),200.f},{TEXT("ThrowLift"),0.f},
            {TEXT("BlastRadius"),150.f},{TEXT("FoodDamage"),0.f},{TEXT("EnemyDamage"),10.f},
            {TEXT("PlayerDamage"),0.f},{TEXT("PushSpeed"),0.f},{TEXT("MaxLifetime"),10.f}};
        Table->AddRow(TEXT("GrenadeLast"),Grenade);
        FMCPerkDefinition Rocket=Grenade;Rocket.EffectClass=UMCRocketSkillPerkEffect::StaticClass();
        Rocket.Parameters={{TEXT("FireWindupSeconds"),0.f},{TEXT("FlightSeconds"),2.f},{TEXT("ProjectileSpeed"),1200.f},
            {TEXT("BlastRadius"),300.f},{TEXT("FoodDamage"),0.f},{TEXT("EnemyDamage"),0.f},
            {TEXT("PlayerDamage"),0.f},{TEXT("PushSpeed"),0.f},{TEXT("ArenaDamage"),120.f},{TEXT("MaxLifetime"),10.f}};
        Table->AddRow(TEXT("RocketLast"),Rocket);
        Player->Perks->PerkTable=Table;Player->FinishSpawning(FTransform::Identity);
        Controller->SetPlayerState(Player);Player->SetOwner(Controller);GS->AddPlayerState(Player);Perks=Player->Perks;
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,10085),FRotator::ZeroRotator);
        Controller->Possess(Hero);Controller->bShowMouseCursor=false;
        Hero->GetCharacterMovement()->DisableMovement();Hero->Status->Initialize(100.f);
    }
    ~FWorld()
    {
        TStrongObjectPtr<UPackage> OwnedPackage(World->GetOutermost());
        const FString OwnedPackageName=OwnedPackage->GetName();
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
        // A destroyed test world can leave its own dirty package resident.
        // Never clear dirty state on a source asset, map or unrelated world.
        if(OwnedPackageName.StartsWith(TEXT("/Temp/")) && OwnedPackage->GetName()==OwnedPackageName)
            OwnedPackage->SetDirtyFlag(false);
        if(auto* Mesh=Cube.Get()) {Mesh->bAllowCPUAccess=HadCPU;Mesh->GetOutermost()->SetDirtyFlag(HadDirty);}
    }
    void Step(float Seconds)
    {for(float At=0;At<Seconds-UE_SMALL_NUMBER;At+=1.f/60) {++GFrameCounter;World->Tick(LEVELTICK_All,FMath::Min(1.f/60,Seconds-At));}}
    AActor* Box(FVector Point,FVector Extent)
    {
        auto* A=World->SpawnActor<AActor>();auto* C=NewObject<UBoxComponent>(A);A->SetRootComponent(C);
        C->SetBoxExtent(Extent);C->SetCollisionProfileName(TEXT("BlockAll"));C->RegisterComponent();A->SetActorLocation(Point);return A;
    }
    AMCNutEnemy* Enemy(FVector Point)
    {auto* E=World->SpawnActor<AMCNutEnemy>(Point,FRotator::ZeroRotator);E->SetActorTickEnabled(false);return E;}
    FMCActiveSkillInputHandle Handle(uint32 Sequence=1) const
    {FMCActiveSkillInputHandle H;H.SlotIndex=0;H.SlotInstanceID=Perks->ActiveSkillSlots[0].InstanceID;H.PressSequence=Sequence;return H;}
    AMCFoodActor* Whole(FVector Point)
    {
        if(!Cube.IsValid()) {
            Cube.Reset(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
#if WITH_EDITOR
            FStaticMeshCompilingManager::Get().FinishCompilation({Cube.Get()});
#endif
            HadCPU=Cube->bAllowCPUAccess;HadDirty=Cube->GetOutermost()->IsDirty();Cube->bAllowCPUAccess=true;
        }
        FMCFoodRow Row;Row.WholeMeshes={Cube.Get()};Row.FragmentMeshes={Cube.Get()};Row.Scale=FVector(.8);
        Row.FragmentScale=FVector(.15);Row.Health=75.f;Row.Fragments=3;Row.SpoilSeconds=300.f;
        const FTransform Pose(Point);
        auto* F=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FRandomStream R(19);F->ConfigureItem(TEXT("BlastFixture"),Row,R);F->Batch=512;
        F->FinishSpawning(Pose);F->Body->SetSimulatePhysics(false);return F;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCActiveCombatPreparation,"MessControl.ActiveSkills.Combat.PreparedProjectilesAreInert",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCActiveCombatPreparation::RunTest(const FString&)
{
    using namespace MCActiveSkillCombatTests;
    FWorld T;
    for(bool bRocket:{false,true}) {
        auto* Effect=NewObject<UMCActiveSkillPerkEffect>(T.Perks);Effect->OwnerComponent=T.Perks;
        Effect->ActionClass=bRocket?AMCRocketSkillAction::StaticClass():AMCGrenadeSkillAction::StaticClass();
        FMCPerkDefinition Definition;FMCActiveSkillInputHandle Input;
        Input.SlotIndex=0;Input.SlotInstanceID=FGuid::NewGuid();Input.PressSequence=1;
        auto* A=Effect->PrepareAction(TEXT("PreparedCombatFixture"),Definition,Input);
        if(!TestNotNull(TEXT("Clear native preparation produces a reviewable uncommitted actor"),A)) return false;
        auto* Sphere=bRocket?CastChecked<AMCRocketSkillAction>(A)->Body.Get():CastChecked<AMCGrenadeSkillAction>(A)->Body.Get();
        auto* Movement=bRocket?CastChecked<AMCRocketSkillAction>(A)->Movement.Get():CastChecked<AMCGrenadeSkillAction>(A)->Movement.Get();
        TestFalse(TEXT("FinishSpawning does not commit a fuse or shot"),A->IsCommitted());
        TestEqual(TEXT("Prepared projectile has no collision"),Sphere->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
        TestFalse(TEXT("Prepared projectile movement is inactive"),Movement->IsActive());
        TestTrue(TEXT("Prepared projectile has no velocity"),Movement->Velocity.IsNearlyZero());
        TestNull(TEXT("Preparation does not seize ordinary tool presentation"),T.Hero->GetActiveSkillPresentation());
        if(bRocket) {
            FHitResult Hit;Hit.bBlockingHit=true;Hit.ImpactPoint=T.Hero->GetActorLocation();
            Sphere->OnComponentHit.Broadcast(Sphere,T.Hero,nullptr,FVector::ZeroVector,Hit);
            TestEqual(TEXT("Even a prepared hit callback cannot explode"),CastChecked<AMCRocketSkillAction>(A)->DetonationCount,0);
        } else TestEqual(TEXT("Preparation does not start a fuse deadline"),CastChecked<AMCGrenadeSkillAction>(A)->FuseEndsAt,0.);
        A->Finish(EMCActiveSkillEndReason::InputCanceled);
        TestEqual(TEXT("Rejected/uncommitted cleanup cannot damage its original owner"),T.Hero->Status->State.Health,100.f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCActiveBlastSnapshot,"MessControl.ActiveSkills.Combat.SnapshotDedupCoverAndFoodIdentity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCActiveBlastSnapshot::RunTest(const FString&)
{
    using namespace MCActiveSkillCombatTests;
    FWorld T;const FVector Origin(0,0,10085);
    auto* Food=T.Whole(Origin+FVector(160,0,0));const double Spoil=Food->SpoilAt;
    auto* BehindFood=T.Enemy(Origin+FVector(330,0,0));const float BehindHP=BehindFood->Health;
    auto* BehindWall=T.Enemy(Origin+FVector(0,330,0));const float WallHP=BehindWall->Health;
    T.Box(Origin+FVector(0,160,0),FVector(100,12,100));
    auto* Exposed=T.Enemy(Origin+FVector(-210,0,0));const float ExposedHP=Exposed->Health;
    auto* Duplicate=NewObject<UBoxComponent>(Exposed);Duplicate->SetupAttachment(Exposed->GetRootComponent());
    Duplicate->SetBoxExtent(FVector(30));Duplicate->SetCollisionProfileName(TEXT("BlockAllDynamic"));Duplicate->RegisterComponent();
    auto* Causer=T.World->SpawnActor<AActor>();
    T.Hero->Destroy(); // The immutable PlayerState outlives the original pawn.
    FMCActiveSkillBlastSettings Blast;Blast.Radius=500;Blast.FoodDamage=100;Blast.EnemyDamage=30;Blast.PlayerDamage=0;Blast.PushSpeed=0;
    const auto Result=MCActiveSkillImpact::ApplyRadial(Causer,T.Player,Origin,Blast);
    TestEqual(TEXT("Overlapping components resolve one original enemy once"),Result.EnemyHits,1);
    TestEqual(TEXT("Exposed enemy loses exact active damage without a living wielder"),Exposed->Health,ExposedHP-30.f);
    TestEqual(TEXT("Destroyed food still supplied cover at the pre-mutation blast snapshot"),BehindFood->Health,BehindHP);
    TestEqual(TEXT("Static scenery blocks the same explosion"),BehindWall->Health,WallHP);
    TestTrue(TEXT("Both cover decisions were recorded before mutations"),Result.Occluded>=2);
    TestEqual(TEXT("New fragments do not join a second blast pass"),Result.FoodHits,1);
    TestEqual(TEXT("Enemy retains immutable source PlayerState after pawn loss"),Exposed->LastExternalImpactSourcePS.Get(),T.Player);
    int32 Count=0;
    for(TActorIterator<AMCFoodActor> It(T.World);It;++It) if(It->bFragment) {
        ++Count;TestEqual(TEXT("Same authored item identity survives fragmentation"),It->ItemName,FName(TEXT("BlastFixture")));
        TestEqual(TEXT("Food batch is not replaced"),It->Batch,512);
        TestEqual(TEXT("Absolute spoil deadline is unchanged"),It->SpoilAt,Spoil);
        TestEqual(TEXT("Native fragment credit remains with the original PS"),It->GetLastHandledBy(),T.Player);
    }
    TestEqual(TEXT("Native whole-food fragmentation creates exactly three pieces"),Count,3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCActiveBlastFriendlyFire,"MessControl.ActiveSkills.Combat.HazardFriendlyFirePreservesToolGuard",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCActiveBlastFriendlyFire::RunTest(const FString&)
{
    using namespace MCActiveSkillCombatTests;
    FWorld T;
    auto* Ally=T.World->SpawnActor<AMCToothCharacter>(T.Hero->GetActorLocation()+FVector(150,0,0),FRotator::ZeroRotator);
    Ally->GetCharacterMovement()->DisableMovement();Ally->Status->Initialize(100);
    TestFalse(TEXT("Ordinary player tool damage remains rejected"),Ally->Status->Damage(15,FVector::ForwardVector,T.Hero));
    TestEqual(TEXT("Rejected tool friendly fire leaves health intact"),Ally->Status->State.Health,100.f);
    auto* Causer=T.World->SpawnActor<AActor>();Causer->SetInstigator(T.Hero);
    FMCActiveSkillBlastSettings Blast;Blast.Radius=250;Blast.FoodDamage=0;Blast.EnemyDamage=0;Blast.PlayerDamage=15;Blast.PushSpeed=0;
    const auto Result=MCActiveSkillImpact::ApplyRadial(Causer,T.Player,T.Hero->GetActorLocation()+FVector(60,0,0),Blast);
    TestEqual(TEXT("One blast damages owner and teammate through native guards"),Result.PlayerHits,2);
    TestEqual(TEXT("Owner is exposed to the accepted hazard"),T.Hero->Status->State.Health,85.f);
    TestEqual(TEXT("Teammate receives actual HP loss from the hazard actor"),Ally->Status->State.Health,85.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCActiveExternalFamilies,"MessControl.ActiveSkills.Combat.ExternalOriginShieldAndEnemyFamilies",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCActiveExternalFamilies::RunTest(const FString&)
{
    using namespace MCActiveSkillCombatTests;
    FWorld T;auto* Causer=T.World->SpawnActor<AActor>();Causer->SetActorLocation(FVector(900,900,10085));
    auto* Knight=T.World->SpawnActor<AMCNutBoss>(FVector(300,0,10085),FRotator::ZeroRotator);
    Knight->SetActorTickEnabled(false);Knight->State=EMCNutBossState::Idle;Knight->Health=100;Knight->ShieldHealth=40;
    const FVector Origin=Knight->GetActorLocation()-FVector(200,0,0);
    Knight->ReceiveExternalImpact(70,Causer,T.Player,Origin);
    TestEqual(TEXT("Breaking shield discards excess active damage"),Knight->Health,100.f);
    TestEqual(TEXT("Native knight shield is actually broken"),Knight->ShieldHealth,0.f);
    TestTrue(TEXT("Feedback points away from real blast, not the distant causer actor"),
        FVector::DotProduct(Knight->Feedback->LastHit.Direction,FVector::ForwardVector)>.9f);
    const int32 Serial=Knight->Feedback->LastHit.Serial;
    TestEqual(TEXT("Unshielded active damage uses the native health/death resolver"),Knight->ReceiveExternalImpact(30,Causer,T.Player,Origin),30.f);
    TestEqual(TEXT("One external hit emits one combined feedback result"),Knight->Feedback->LastHit.Serial,Serial+1);
    TestEqual(TEXT("Knight keeps source PS credit"),Knight->LastExternalImpactSourcePS.Get(),T.Player);
    auto* Jelly=T.World->SpawnActor<AMCCataclysmJelly>();Jelly->SetActorTickEnabled(false);
    const float JellyHP=Jelly->Health;Jelly->ReceiveExternalImpact(10,Causer,T.Player,Origin);
    TestEqual(TEXT("Jelly inherits the same external damage contract"),Jelly->Health,JellyHP-10.f);
    TestNull(TEXT("Active damage does not turn gel into a pursuit enemy"),Jelly->Target.Get());
    auto* Boss=T.World->SpawnActor<AMCBossCharacter>();const float BossHP=Boss->Runtime.Health;
    TestEqual(TEXT("Generic boss accepts the same attributed external damage"),Boss->ReceiveExternalImpact(15,Causer,T.Player,Origin),15.f);
    TestEqual(TEXT("Generic boss loses exact raw active damage"),Boss->Runtime.Health,BossHP-15.f);
    TestEqual(TEXT("Generic boss retains original source PS"),Boss->LastExternalImpactSourcePS.Get(),T.Player);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGrenadeCommittedFuse,"MessControl.ActiveSkills.Combat.GrenadeLastChargeReleaseOverholdAndDeath",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGrenadeCommittedFuse::RunTest(const FString&)
{
    using namespace MCActiveSkillCombatTests;
    for(int32 Scenario:{0,1,2,3,4}) {
        FWorld T;
        // EnterStanding gives a newly spawned/recovered pawn 0.6s of native
        // hit immunity. Let that real protection expire BEFORE arming the
        // short fuse; never clear its private gate or extend an accepted fuse.
        if(Scenario>=3) T.Step(.65f);
        if(!TestTrue(TEXT("Acquire the last-charge fixture"),T.Perks->TryAcquirePerk(TEXT("GrenadeLast")))) return false;
        const auto Press=T.Handle();
        if(!TestTrue(TEXT("Start commits one real grenade action"),T.Perks->BeginSkillSlot(Press))) return false;
        auto* A=Cast<AMCGrenadeSkillAction>(T.Perks->FindRunningAction(Press.SlotInstanceID));
        if(!TestNotNull(TEXT("Depleted slot retains its independent cooking action"),A)) return false;
        const TWeakObjectPtr<AMCGrenadeSkillAction> Lifetime=A;const double Fuse=A->FuseEndsAt;
        TestTrue(TEXT("The last charge frees its slot without auto-dropping"),T.Perks->ActiveSkillSlots[0].PerkID.IsNone() && A->Phase==EMCGrenadeSkillPhase::Cooking);
        // Keep the native enemy sphere outside the held grenade's muzzle
        // sweep, while its near surface remains inside the eventual blast.
        auto* Enemy=T.Enemy(A->GetActorLocation()+FVector(120,0,0));const float Before=Enemy->Health;
        if(Scenario==0) {
            T.Step(.5f);
            TestTrue(TEXT("Release reaches original cooking action after slot depletion"),T.Perks->ReleaseSkillInput(Press));
            TestEqual(TEXT("Release throws instead of discarding the charge"),A->Phase,EMCGrenadeSkillPhase::Thrown);
        } else if(Scenario==1) {
            T.Step(.7f);TestEqual(TEXT("Overhold drops the still-live grenade before expiry"),A->Phase,EMCGrenadeSkillPhase::Dropped);
            T.Perks->ReleaseSkillInput(Press);
        } else if(Scenario==2) {
            T.Step(.2f);auto* Injury=T.World->SpawnActor<AActor>();
            T.Hero->Status->Damage(1000,FVector::UpVector,Injury);T.Step(.1f);
            TestEqual(TEXT("Actual owner death safely drops its accepted hazard"),A->Phase,EMCGrenadeSkillPhase::Dropped);
        } else {
            T.Step(.2f);
            T.Hero->ToothPhysics->ApplyHit(FVector(0,0,T.Hero->ToothPhysics->Settings.FallThreshold+10),T.Hero->GetActorLocation());
            if(!TestEqual(TEXT("A native physical hit really puts the living owner into ragdoll"),T.Hero->ToothPhysics->GetBodyState(),EMCBodyState::Ragdoll)) return false;
            TestTrue(TEXT("Knockdown fixture preserves the living owner"),T.Hero->Status->IsAlive());
            if(Scenario==3) {
                TestTrue(TEXT("Original Completed release still reaches a knocked-down action"),T.Perks->ReleaseSkillInput(Press));
                TestEqual(TEXT("Knocked-down release drops instead of throwing from the stale capsule"),A->Phase,EMCGrenadeSkillPhase::Dropped);
            } else {
                T.Step(.03f);
                TestEqual(TEXT("Cooking tick drops immediately on native incapacity without waiting for release"),A->Phase,EMCGrenadeSkillPhase::Dropped);
            }
        }
        TestEqual(TEXT("Release/overhold/death/ragdoll cannot restart the fuse"),A->FuseEndsAt,Fuse);
        while(T.World->GetTimeSeconds()<Fuse+.04) T.Step(1.f/60);
        if(!TestTrue(TEXT("Detonation remains alive long enough for its replicated cue"),Lifetime.IsValid())) return false;
        TestEqual(TEXT("Accepted fuse detonates once"),Lifetime->DetonationCount,1);
        const float After=Enemy->Health;
        TestTrue(TEXT("The dropped/thrown physical hazard produces a real nearby enemy hit"),After<Before);
        Lifetime->Cancel(EMCActiveSkillEndReason::TimedOut);Lifetime->ReleaseInput();
        TestEqual(TEXT("Repeated terminal callbacks cannot repeat the blast"),Lifetime->DetonationCount,1);
        TestEqual(TEXT("Repeated terminal callbacks cause no extra damage"),Enemy->Health,After);
        TestEqual(TEXT("Blast credit survives owner death"),Enemy->LastExternalImpactSourcePS.Get(),T.Player);
        T.Step(.6f);TestFalse(TEXT("The detonated projectile has finite cleanup"),Lifetime.IsValid());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCRocketMuzzleAndArena,"MessControl.ActiveSkills.Combat.RocketMuzzleRejectAndNativeArenaDestruction",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCRocketMuzzleAndArena::RunTest(const FString&)
{
    using namespace MCActiveSkillCombatTests;
    FWorld T;TestTrue(TEXT("Acquire rocket fixture"),T.Perks->TryAcquirePerk(TEXT("RocketLast")));
    const auto Blocked=T.Handle();
    auto* Wall=T.Box(T.Hero->GetActorLocation()+FVector(75,0,61),FVector(10,100,100));
    TestFalse(TEXT("A wall at the muzzle rejects preparation before charge commit"),T.Perks->BeginSkillSlot(Blocked));
    TestEqual(TEXT("Rejected preparation preserves its charge"),T.Perks->ActiveSkillSlots[0].Charges,1);
    TestTrue(TEXT("Rejected preparation starts no physical hazard"),T.Perks->GetActiveSkillActions().IsEmpty());
    TestNull(TEXT("Rejected preparation claims no hand/animation lease"),T.Hero->GetActiveSkillPresentation());
    Wall->Destroy();
    auto* Tooth=T.World->SpawnActor<AMCArenaTooth>(T.Hero->GetActorLocation()+FVector(450,0,61),FRotator::ZeroRotator);
    // A compact authored-collision fixture isolates the real projectile hit
    // and native tooth-health callback from this map prop's display bounds.
    Tooth->SetActorTickEnabled(false);Tooth->Body->SetBoxExtent(FVector(48,48,78));
    Tooth->Initialize(1,FMCArenaToothSettings());const float Health=Tooth->Status->State.Health;
    const auto Press=T.Handle(2);if(!TestTrue(TEXT("Clear muzzle commits a projectile"),T.Perks->BeginSkillSlot(Press))) return false;
    auto* A=Cast<AMCRocketSkillAction>(T.Perks->FindRunningAction(Press.SlotInstanceID));
    if(!TestNotNull(TEXT("Rocket outlives the emptied slot"),A)) return false;
    int32 EndCueCount=0;
    A->OnVFXStage.AddLambda([&EndCueCount](FName Stage){if(Stage==TEXT("End"))++EndCueCount;});
    const TWeakObjectPtr<AMCRocketSkillAction> Lifetime=A;
    TestTrue(TEXT("Last charge is spent while rocket remains in flight"),T.Perks->ActiveSkillSlots[0].PerkID.IsNone() && A->Phase==EMCRocketSkillPhase::Flying);
    TestEqual(TEXT("The caster keeps the launcher for recoil while the projectile flies"),T.Hero->GetActiveSkillPresentation(),static_cast<AMCActiveSkillAction*>(A));
    FTransform LeftWrist,RightWrist;
    TestTrue(TEXT("Both launcher grips provide complete wrist targets"),T.Hero->ActiveSkillHandTarget(true,LeftWrist) && T.Hero->ActiveSkillHandTarget(false,RightWrist));
    TestTrue(TEXT("The accepted projectile starts at the actual held model muzzle"),A->GetActorLocation().Equals(A->GetLauncherMuzzlePoint(),.1));
    T.Step(.5f);
    if(!TestTrue(TEXT("Impact cue keeps its finite projectile actor briefly alive"),Lifetime.IsValid())) return false;
    TestNull(TEXT("Ordinary tools return after launcher recoil recovery"),T.Hero->GetActiveSkillPresentation());
    TestEqual(TEXT("Swept collision produced exactly one detonation"),Lifetime->DetonationCount,1);
    TestTrue(TEXT("Rocket goes through ReceiveArenaHit and destroys the native tooth"),Health>0 && Tooth->Status->State.Health<=0 && Tooth->State.bLost);
    FHitResult Duplicate(Tooth,Tooth->Body,Tooth->GetActorLocation(),FVector::UpVector);
    Lifetime->Body->OnComponentHit.Broadcast(Lifetime->Body,Tooth,Tooth->Body,FVector::ZeroVector,Duplicate);
    Lifetime->Cancel(EMCActiveSkillEndReason::TimedOut);
    TestEqual(TEXT("Repeated hit plus timeout cannot detonate twice"),Lifetime->DetonationCount,1);
    T.Step(.5f);TestFalse(TEXT("Rocket impact cleans up without waiting on its former slot"),Lifetime.IsValid());
    TestEqual(TEXT("Natural projectile recovery emits one End event for the editable VFX slot"),EndCueCount,1);
    return true;
}
#endif
