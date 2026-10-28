#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCCombatPassiveSubsystem.h"
#include "MCPassiveCombatCue.h"
#include "MCPerkComponent.h"
#include "MCRoguePerkEffect.h"
#include "MCPlayerState.h"
#include "MCPlayerController.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCInventoryComponent.h"
#include "MCNutEnemy.h"
#include "MCNutBoss.h"
#include "MCBossCharacter.h"
#include "MCCataclysmJelly.h"
#include "MCFoodActor.h"
#include "MCTongue.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCCombatPassiveTests
{
const FName Mighty(TEXT("B2_MightyTooth")),Dash(TEXT("B2_StartFromDash")),Thunder(TEXT("B2_ThunderGod")),Echo(TEXT("B2_EchoSwing")),Billiards(TEXT("B2_Billiards"));
const FVector Base(0,0,10000);
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCPlayerController* Controller=nullptr;
    AMCPlayerState* Player=nullptr;
    AMCToothCharacter* Hero=nullptr;
    UMCPerkComponent* Perks=nullptr;
    UMCCombatPassiveSubsystem* Runtime=nullptr;
    UDataTable* Table=nullptr;
    UStaticMesh* Cube=nullptr;
    AMCTongue* Tongue=nullptr;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* State=World->SpawnActor<AMCGameState>();World->SetGameState(State);
        State->Phase=EMCShiftPhase::Working;State->bLobbyWaiting=false;
        Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
        FStaticMeshCompilingManager::Get().FinishCompilation({Cube});
#endif
        Table=NewObject<UDataTable>(World);Table->RowStruct=FMCPerkDefinition::StaticStruct();
        for(FName ID:{Mighty,Dash,Thunder,Echo,Billiards}) {
            FMCPerkDefinition Row;Row.Kind=EMCPerkKind::Passive;Row.bRoguePerk=true;Row.MaxStacks=1;
            Row.ChoiceFamily=ID;Row.EffectClass=UMCRoguePerkEffect::StaticClass();Table->AddRow(ID,Row);
        }
        Controller=World->SpawnActor<AMCPlayerController>();
        Player=World->SpawnActorDeferred<AMCPlayerState>(AMCPlayerState::StaticClass(),FTransform::Identity,Controller);
        Player->Perks->PerkTable=Table;Player->FinishSpawning(FTransform::Identity);
        Controller->SetPlayerState(Player);Player->SetOwner(Controller);State->AddPlayerState(Player);Perks=Player->Perks;
        Hero=World->SpawnActor<AMCToothCharacter>(Base,FRotator::ZeroRotator);
        Controller->Possess(Hero);Hero->GetCharacterMovement()->DisableMovement();Hero->Status->Initialize(100);
        Controller->bShowMouseCursor=false;Hero->Inventory->ServerSelect(EMCToolSlot::Knife);
        Tongue=World->SpawnActor<AMCTongue>();Tongue->SetActorTickEnabled(false);
        Runtime=World->GetSubsystem<UMCCombatPassiveSubsystem>();Step(.8f);
    }
    ~FWorld() {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    void Step(float Seconds)
    {
        constexpr float Dt=1.f/120;
        for(int32 I=0;I<FMath::CeilToInt(Seconds/Dt);++I) {++GFrameCounter;World->Tick(LEVELTICK_All,Dt);}
    }
    AMCNutEnemy* Enemy(FVector Offset)
    {
        auto* Enemy=World->SpawnActor<AMCNutEnemy>(Base+Offset,FRotator::ZeroRotator);
        Enemy->SetActorTickEnabled(false);Enemy->Settings.BodyRadius=20;Enemy->Body->SetSphereRadius(20);Enemy->Health=1000;
        return Enemy;
    }
    AMCFoodActor* Food(FVector Offset)
    {
        FMCFoodRow Row;Row.Kind=EMCFoodKind::Food;Row.WholeMeshes={Cube};Row.FragmentMeshes={Cube};
        Row.Scale=FVector(.25f);Row.Health=1000;Row.SpoilSeconds=1000;
        const FTransform Pose(Base+Offset);
        auto* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FRandomStream Random(69);Food->ConfigureItem(TEXT("CombatPassiveFixture"),Row,Random,false);Food->FinishSpawning(Pose);
        Food->SetActorTickEnabled(false);Food->Body->SetSimulatePhysics(false);return Food;
    }
    void Hit(AMCNutEnemy* Target,float Damage=20,bool Melee=true)
    {FMCCombatAttackScope Attack(Hero,Melee);Target->ReceiveToolDamage(Damage,Hero);}
    void Wall(FVector Offset,FVector Extent)
    {
        auto* Actor=World->SpawnActor<AActor>();auto* Shape=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Shape);Shape->SetBoxExtent(Extent);Shape->SetCollisionProfileName(TEXT("BlockAll"));
        Shape->RegisterComponent();Actor->SetActorLocation(Base+Offset);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCombatDashConsumption,"MessControl.Passives.Batch2.Combat.DashConsumesOnlyAcceptedAttack",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCombatDashConsumption::RunTest(const FString&)
{
    using namespace MCCombatPassiveTests;FWorld T;auto* A=T.Enemy(FVector(120,0,0));
    TestTrue(TEXT("Acquire first-hit passive"),T.Perks->ServerGrantPerk(Dash));
    UMCCombatPassiveSubsystem::NotifyDashStarted(T.Hero);
    {FMCCombatAttackScope Empty(T.Hero);TestEqual(TEXT("A query previews +25 percent"),T.Perks->WeaponDamage(20,A),25.f);}
    A->bDefeated=true;T.Hit(A);A->bDefeated=false;
    T.Hit(A);TestEqual(TEXT("Empty scope and rejected damage did not consume bonus"),A->Health,975.f);
    T.Hit(A);TestEqual(TEXT("Second accepted attack uses ordinary damage"),A->Health,955.f);
    UMCCombatPassiveSubsystem::NotifyDashStarted(T.Hero);T.Step(2.05f);T.Hit(A);
    TestEqual(TEXT("The two-second window expires"),A->Health,935.f);
    UMCCombatPassiveSubsystem::NotifyDashStarted(T.Hero);
    {TGuardValue<bool> Secondary(T.Perks->bRogueExtraHit,true);A->ReceiveToolDamage(20,T.Hero);}
    T.Hit(A);TestEqual(TEXT("Secondary hit does not consume new dash window"),A->Health,890.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCombatMightyCleave,"MessControl.Passives.Batch2.Combat.CleaveSharesOneDashBonusAndHonorsGeometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCombatMightyCleave::RunTest(const FString&)
{
    using namespace MCCombatPassiveTests;FWorld T;
    auto* A=T.Enemy(FVector(115,-55,0));auto* B=T.Enemy(FVector(140,60,0));auto* Behind=T.Enemy(FVector(-100,0,0));
    auto* Blocked=T.Enemy(FVector(110,110,0));T.Wall(FVector(55,70,0),FVector(10,15,100));
    TestTrue(TEXT("Acquire cleave"),T.Perks->ServerGrantPerk(Mighty));TestTrue(TEXT("Acquire dash bonus"),T.Perks->ServerGrantPerk(Dash));
    UMCCombatPassiveSubsystem::NotifyDashStarted(T.Hero);const float Damage=T.Hero->Inventory->Damage()*1.25f;
    T.Hero->SwingBrush();T.Step(.65f);
    TestTrue(TEXT("Real swing hit first permitted target once"),FMath::IsNearlyEqual(A->Health,1000-Damage));
    TestTrue(TEXT("Second cleave target shares same once-per-attack dash bonus"),FMath::IsNearlyEqual(B->Health,1000-Damage));
    TestEqual(TEXT("A target behind the saved facing is excluded"),Behind->Health,1000.f);
    TestEqual(TEXT("A wall blocks extra targets"),Blocked->Health,1000.f);
    const float Before=A->Health;T.Hit(A);TestTrue(TEXT("Next attack cannot reuse cleave's dash bonus"),FMath::IsNearlyEqual(A->Health,Before-20));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCombatDashShield,"MessControl.Passives.Batch2.Combat.ShieldContactConsumesBonusButImmuneBossDoesNot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCombatDashShield::RunTest(const FString&)
{
    using namespace MCCombatPassiveTests;FWorld T;T.Perks->ServerGrantPerk(Dash);
    const FTransform Pose(Base+FVector(140,0,0));
    auto* Boss=T.World->SpawnActorDeferred<AMCNutBoss>(AMCNutBoss::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    FMCNutBossSettings Settings;Settings.WholeMesh=Settings.KernelMesh=Settings.ShellMesh=T.Cube;
    Boss->ConfigureEncounter(T.Tongue,nullptr,EMCNutBossRole::Tank,Settings,1,91);
    Boss->FinishSpawning(Pose);Boss->SetActorTickEnabled(false);Boss->State=EMCNutBossState::Falling;
    UMCCombatPassiveSubsystem::NotifyDashStarted(T.Hero);const float Shield=Boss->ShieldHealth,Health=Boss->Health;
    T.Hit(Boss);TestEqual(TEXT("Invulnerable entrance rejects shield damage"),Boss->ShieldHealth,Shield);
    Boss->State=EMCNutBossState::Idle;T.Hit(Boss);
    TestEqual(TEXT("First accepted shield contact receives the unspent bonus"),Boss->ShieldHealth,Shield-25);
    TestEqual(TEXT("Shield-only hit preserves boss HP"),Boss->Health,Health);
    T.Hit(Boss);TestEqual(TEXT("Accepted shield contact consumes once"),Boss->ShieldHealth,Shield-45);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCombatEchoPose,"MessControl.Passives.Batch2.Combat.EchoKeepsOriginalPositionAndDirection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCombatEchoPose::RunTest(const FString&)
{
    using namespace MCCombatPassiveTests;FWorld T;auto* A=T.Enemy(FVector(120,0,0));
    auto* ActualTool=FindObject<UStaticMeshComponent>(T.Hero,TEXT("InventoryTool"));
    if(!TestNotNull(TEXT("Actual primary tool component exists"),ActualTool)) return false;
    UStaticMesh* CapturedMesh=ActualTool->GetStaticMesh();const FTransform CapturedPose=ActualTool->GetComponentTransform();
    TestTrue(TEXT("Acquire echo"),T.Perks->ServerGrantPerk(Echo));T.Hit(A);
    TestEqual(TEXT("Exactly one echo queued"),T.Runtime->PendingEchoCountForTesting(),1);
    T.Hero->SetActorLocation(Base+FVector(1000,0,0));T.Hero->SetActorRotation(FRotator(0,180,0));
    auto* AtNewPose=T.Enemy(FVector(880,0,0));T.Hero->Inventory->ServerSelect(EMCToolSlot::Brush);
    T.Step(.43f);
    AMCPassiveCombatCue* Ghost=nullptr;
    for(TActorIterator<AMCPassiveCombatCue> It(T.World);It;++It) if(It->Kind==EMCPassiveCombatCueKind::EchoTool) {Ghost=*It;break;}
    if(TestNotNull(TEXT("A ghost tool visibly winds up before the delayed contact"),Ghost)) {
        TestTrue(TEXT("The ghost retains captured geometry after a later switch"),!Ghost->ToolParts.IsEmpty() && Ghost->ToolParts[0].Mesh==CapturedMesh);
        if(!Ghost->ToolParts.IsEmpty()) TestTrue(TEXT("Captured contact transform stays at original world pose"),(Ghost->ToolParts[0].ContactPose*Ghost->GetActorTransform()).Equals(CapturedPose,.01f));
        if(TestTrue(TEXT("Cosmetic mesh copy is present"),!Ghost->GhostMeshes.IsEmpty())) {
            TestEqual(TEXT("Ghost has no collision"),Ghost->GhostMeshes[0]->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
            TestFalse(TEXT("Ghost never becomes a physics weapon"),Ghost->GhostMeshes[0]->IsSimulatingPhysics());
            TestTrue(TEXT("Ghost uses the existing retained material directly"),Ghost->GhostMeshes[0]->GetMaterial(0)==Ghost->GhostMaterial);
            if(auto* Material=Ghost->GhostMeshes[0]->GetMaterial(0)) TestFalse(TEXT("No first-use dynamic material instance is created"),Material->IsA<UMaterialInstanceDynamic>());
            else AddError(TEXT("Existing ghost material was not prepared"));
        }
    }
    T.Step(.05f);TestEqual(TEXT("Echo does not hit early"),A->Health,980.f);
    T.Step(.05f);TestEqual(TEXT("Echo deals forty percent from original pose"),A->Health,972.f);
    TestEqual(TEXT("Moving and turning does not transfer echo to new pose"),AtNewPose->Health,1000.f);
    TestEqual(TEXT("Echo cannot queue another echo"),T.Runtime->PendingEchoCountForTesting(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCombatEchoLifecycle,"MessControl.Passives.Batch2.Combat.RemovalDeathAndPawnChangeCancelPendingWork",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCombatEchoLifecycle::RunTest(const FString&)
{
    using namespace MCCombatPassiveTests;
    for(int32 Case=0;Case<3;++Case) {
        FWorld T;auto* A=T.Enemy(FVector(120,0,0));T.Perks->ServerGrantPerk(Echo);T.Hit(A);
        T.Step(.43f);
        TWeakObjectPtr<AMCPassiveCombatCue> PendingGhost;
        for(TActorIterator<AMCPassiveCombatCue> It(T.World);It;++It) if(It->Kind==EMCPassiveCombatCueKind::EchoTool) {PendingGhost=*It;break;}
        if(Case==0) {T.Perks->RemovePerk(Echo);T.Perks->ServerGrantPerk(Echo);}
        if(Case==1) T.Hero->Status->Damage(1000,FVector::ForwardVector,nullptr,EMCRogueDamageKind::System);
        if(Case==2) {
            auto* Replacement=T.World->SpawnActor<AMCToothCharacter>(Base+FVector(1000,0,0),FRotator::ZeroRotator);
            T.Controller->Possess(Replacement);Replacement->GetCharacterMovement()->DisableMovement();
        }
        T.Step(.01f);TestTrue(TEXT("Cancellation removes the visible wind-up before its contact deadline"),!PendingGhost.IsValid() || PendingGhost->IsActorBeingDestroyed());
        T.Step(.6f);TestEqual(TEXT("Canceled echo leaves original target at direct-hit health"),A->Health,980.f);
        TestEqual(TEXT("Canceled delayed work is discarded"),T.Runtime->PendingEchoCountForTesting(),0);
        TestTrue(TEXT("Canceled tool ghost is also discarded"),!PendingGhost.IsValid() || PendingGhost->IsActorBeingDestroyed());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCombatThunder,"MessControl.Passives.Batch2.Combat.ChainHasThreeUniqueFoodTargetsAndNoRecursiveProcs",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCombatThunder::RunTest(const FString&)
{
    using namespace MCCombatPassiveTests;FWorld T;auto* A=T.Enemy(FVector(120,-60,0));
    T.Perks->ServerGrantPerk(Thunder);T.Perks->ServerGrantPerk(Echo);
    T.Runtime->SetNextThunderRollForTesting(0);
    TArray<AMCFoodActor*> Foods;for(int32 I=0;I<4;++I) Foods.Add(T.Food(FVector(250+I*220,0,0)));
    auto* Boss=T.World->SpawnActor<AMCBossCharacter>(Base+FVector(300,200,0),FRotator::ZeroRotator);Boss->SetActorTickEnabled(false);const float BossHP=Boss->Runtime.Health;
    T.Hit(A);
    for(int32 I=0;I<3;++I) TestEqual(TEXT("Each of three food jumps receives thirty percent once"),Foods[I]->Health,994.f);
    TestEqual(TEXT("Fourth jump is capped"),Foods[3]->Health,1000.f);
    TestEqual(TEXT("Bosses are never chain targets"),Boss->Runtime.Health,BossHP);
    TestEqual(TEXT("Secondary food hits did not create extra echoes"),T.Runtime->PendingEchoCountForTesting(),1);
    int32 Links=0;for(TActorIterator<AMCPassiveCombatCue> It(T.World);It;++It) ++Links;
    TestEqual(TEXT("Each accepted jump has one visible link"),Links,3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCombatThunderAlly,"MessControl.Passives.Batch2.Combat.ChainAllyUsesPhysicalFriendlyContactAndRespectsWalls",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCombatThunderAlly::RunTest(const FString&)
{
    using namespace MCCombatPassiveTests;FWorld T;auto* A=T.Enemy(FVector(120,-60,0));
    T.Perks->ServerGrantPerk(Thunder);T.Runtime->SetNextThunderRollForTesting(0);
    auto* Ally=T.World->SpawnActor<AMCToothCharacter>(Base+FVector(250,0,0),FRotator::ZeroRotator);
    Ally->GetCharacterMovement()->DisableMovement();Ally->Status->Initialize(1000);
    auto* Food=T.Food(FVector(470,0,0));T.Wall(FVector(365,0,0),FVector(10,150,150));
    // EnterStanding grants every fresh pawn .6s of ordinary recovery protection.
    // Observe its natural expiry; never bypass protected physics state.
    T.Step(.7f);
    auto Describe=[&](const TCHAR* Phase,AMCToothCharacter* Pawn) {
        const auto* Physics=Pawn->ToothPhysics.Get();const auto* Movement=Pawn->GetCharacterMovement();
        const bool HitGate=Physics->Tooth && Pawn->HasAuthority() && !Pawn->SwallowedBy && !Pawn->IsMimicCaptured()
            && Physics->LocalState!=EMCBodyState::Recovering && Physics->ServerTime()>=Physics->RecoveryInvulnerableUntil;
        AddInfo(FString::Printf(TEXT("%s pawn=%s auth=%d alive=%d hidden=%d CanAct=%d body=%s move=%s active=%d validData=%d actor=%s physical=%s velocity=%s pendingLaunch=%s worldTime=%.6f serverTime=%.6f recoveryUntil=%.6f ApplyHitGate=%d"),
            Phase,*GetNameSafe(Pawn),Pawn->HasAuthority(),Pawn->Status->IsAlive(),Pawn->IsHidden(),Physics->CanAct(),
            *UEnum::GetValueAsString(Physics->GetBodyState()),*UEnum::GetValueAsString(Movement->MovementMode.GetValue()),Movement->IsActive(),Movement->HasValidData(),
            *Pawn->GetActorLocation().ToString(),*Physics->PhysicalLocation().ToString(),*Pawn->GetVelocity().ToString(),*Movement->PendingLaunchVelocity.ToString(),
            T.World->GetTimeSeconds(),Physics->ServerTime(),Physics->RecoveryInvulnerableUntil,HitGate));
    };
    Describe(TEXT("Before fixture movement release"),Ally);
    // MOVE_None deliberately rejects CMC::Launch. Supply real support and a
    // normal walking mode so the fixture permits the game's ordinary small push.
    const float Half=Ally->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    T.Wall(FVector(250,0,-Half-20.f),FVector(75,75,20));
    Ally->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Describe(TEXT("Before chain source"),T.Hero);Describe(TEXT("Before chain ally"),Ally);
    int32 LinksBefore=0;for(TActorIterator<AMCPassiveCombatCue> It(T.World);It;++It) if(It->Kind==EMCPassiveCombatCueKind::Lightning) ++LinksBefore;
    T.Hit(A);TestEqual(TEXT("Lightning does not introduce friendly HP damage"),Ally->Status->State.Health,1000.f);
    Describe(TEXT("After chain source"),T.Hero);Describe(TEXT("After chain ally"),Ally);
    int32 LinksAfter=0;bool SelectedAlly=false;
    for(TActorIterator<AMCPassiveCombatCue> It(T.World);It;++It) if(It->Kind==EMCPassiveCombatCueKind::Lightning) {
        ++LinksAfter;const FVector End(It->LinkEnd);
        SelectedAlly|=End.Equals(Ally->GetActorLocation(),1.f) || End.Equals(Ally->ToothPhysics->PhysicalLocation(),1.f);
        AddInfo(FString::Printf(TEXT("Actual chain cue=%s from=%s to=%s allyEndpoint=%d"),*It->GetName(),*It->GetActorLocation().ToString(),*End.ToString(),SelectedAlly));
    }
    AddInfo(FString::Printf(TEXT("Chain links before=%d after=%d; selectedAlly=%d; actual native pending launch=%s"),LinksBefore,LinksAfter,SelectedAlly,*Ally->GetCharacterMovement()->PendingLaunchVelocity.ToString()));
    TestTrue(TEXT("Chain selected the actual ally endpoint"),SelectedAlly);
    TestTrue(TEXT("Allied lightning has visible physical consequence"),Ally->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll
        || !Ally->GetCharacterMovement()->PendingLaunchVelocity.IsNearlyZero());
    TestEqual(TEXT("A wall blocks the next jump"),Food->Health,1000.f);
    TestEqual(TEXT("Owner never receives its own chain"),T.Hero->Status->State.Health,100.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCombatBilliards,"MessControl.Passives.Batch2.Combat.BilliardsUsesOriginalDamageOncePerContact",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCombatBilliards::RunTest(const FString&)
{
    using namespace MCCombatPassiveTests;FWorld T;auto* A=T.Enemy(FVector(100,0,0));auto* B=T.Enemy(FVector(230,0,0));
    T.Perks->ServerGrantPerk(Billiards);T.Perks->ServerGrantPerk(Echo);T.Hit(A);
    TestEqual(TEXT("Real small-enemy hit creates a launch"),T.Runtime->LaunchCountForTesting(),1);
    for(int32 I=0;I<5;++I) UMCCombatPassiveSubsystem::TickBilliards(A,.1f);
    TestTrue(TEXT("Small enemy really moves away from original attacker"),A->GetActorLocation().X>100);
    TestTrue(TEXT("Launch movement preserves its starting height without a tongue binding"),FMath::IsNearlyEqual(A->GetActorLocation().Z,Base.Z,.01));
    AddInfo(FString::Printf(TEXT("Actual swept launch endpoint: %s; collision target: %s; radius sum: %.3fcm"),*A->GetActorLocation().ToString(),*B->GetActorLocation().ToString(),A->Settings.BodyRadius+B->Settings.BodyRadius));
    TestEqual(TEXT("Collision deals exactly thirty percent of original effective damage"),B->Health,994.f);
    TestEqual(TEXT("Repeated overlap cannot damage a target twice"),B->Health,994.f);
    TestEqual(TEXT("Collision never launches another enemy"),T.Runtime->LaunchCountForTesting(),1);
    TestEqual(TEXT("Collision never creates another echo"),T.Runtime->PendingEchoCountForTesting(),1);
    T.Perks->RemovePerk(Billiards);TestEqual(TEXT("Removing passive clears launch authoring state"),T.Runtime->LaunchCountForTesting(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCombatBilliardsFloorGuard,"MessControl.Passives.Batch2.Combat.BilliardsRejectsDistantFloorProjection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCombatBilliardsFloorGuard::RunTest(const FString&)
{
    using namespace MCCombatPassiveTests;FWorld T;auto* A=T.Enemy(FVector(100,0,0));
    // Deliberately stale support binding: pawn/enemy test plane is 10,000cm
    // above this tongue. A short launch must never project across that gap.
    A->Tongue=T.Tongue;T.Perks->ServerGrantPerk(Billiards);T.Hit(A);
    UMCCombatPassiveSubsystem::TickBilliards(A,.1f);
    TestTrue(TEXT("A stale floor binding cannot teleport a launched enemy vertically"),FMath::IsNearlyEqual(A->GetActorLocation().Z,Base.Z,.01));
    TestTrue(TEXT("Rejected distant support does not cancel valid horizontal push"),A->GetActorLocation().X>100);
    return true;
}
#endif
