#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCCombatPassiveSubsystem.h"
#include "MCPerkPresentationEvents.h"
#include "MCVFXPresentationComponent.h"
#include "MCWorldPreloadSubsystem.h"
#include "MCPassiveCombatCue.h"
#include "MCVFXNativePresentationSettings.h"
#include "ProceduralMeshComponent.h"
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
#include "NiagaraSystem.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformProcess.h"
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
        TestEqual(TEXT("Shared native ghost retains the actual accepted perk"),Ghost->SourcePerkID,Echo);
        TestEqual(TEXT("Shared native ghost retains the actual accepted event"),Ghost->AcceptedEvent,FName(TEXT("Windup")));
        TestEqual(TEXT("Presentation owner remains separate from captured tool geometry"),Ghost->PresentationOwner.Get(),T.Hero);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCombatThunderNativeArt,
    "MessControl.VFX.NativeLightning.PreparedArtUsesRealAcceptedThunderLinks",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCombatThunderNativeArt::RunTest(const FString&)
{
    using namespace MCCombatPassiveTests;FWorld T;FString Error;auto* Target=T.Enemy(FVector(120,-60,0));
    if(!TestTrue(TEXT("Actual passive ownership"),T.Perks->ServerGrantPerk(Thunder)))return false;
    auto* Style=NewObject<UMCVFXNativePresentationSettings>();
    if(!TestTrue(TEXT("Actual lightning renderer has native defaults"),Style->InitFromNativeDefaults(Thunder,Error)))return false;
    auto& Part=Style->Events.FindChecked(TEXT("Link")).Parts[0];
    Part.Feedback.Scale=1.5f;Part.Feedback.DurationScale=2;Part.Tint=FLinearColor(.5f,1,1,1);
    auto* Profile=NewObject<UMCPerkVFXProfile>();Profile->NativePresentation=Style;
    if(!TestTrue(TEXT("Material resources prepared before attack acceptance"),T.Hero->PassivePresentation->PrepareSourceProfile(Thunder,MCPassivePresentation::GetAllowedEvents(Thunder),Profile,Error)))
    {AddError(Error);return false;}
    const auto* Prepared=T.Hero->PassivePresentation->GetPreparedNativeSource(Thunder);
    if(!TestTrue(TEXT("Retained immutable native material is ready"),Prepared && Prepared->Snapshot && Prepared->IsReady()))return false;
    Part.Feedback.Scale=.5f; // Editing the draft cannot alter the accepted prepared version.
    TArray<AMCFoodActor*> Foods;for(int32 I=0;I<3;++I)Foods.Add(T.Food(FVector(250+I*220,0,0)));
    T.Runtime->SetNextThunderRollForTesting(0);T.Hit(Target);
    AMCPassiveCombatCue* Cue=nullptr;int32 Count=0;
    for(TActorIterator<AMCPassiveCombatCue> It(T.World);It;++It)if(It->SourcePerkID==Thunder && It->AcceptedEvent==TEXT("Link")){++Count;if(!Cue)Cue=*It;}
    if(!TestNotNull(TEXT("Real accepted damage produces the source-aware native renderer"),Cue))return false;
    TestEqual(TEXT("One original drawing per accepted jump, no duplicated proc"),Count,3);
    for(auto* Food:Foods)TestEqual(TEXT("Native chain damage is unchanged"),Food->Health,994.f);
    TestTrue(TEXT("Native material is the preprepared retained variant"),Cue->Mesh->GetMaterial(0)==Prepared->GetPreparedMaterial(TEXT("Link"),TEXT("LightningStroke")));
    TestEqual(TEXT("Accepted drawing uses immutable width, not edited source"),Cue->GetPreparedNativeStyle().FindPart(TEXT("Link"),TEXT("LightningStroke"))->Feedback.Scale,1.5f);
    TestTrue(TEXT("Actor survives the complete scaled cosmetic tail"),Cue->GetLifeSpan()>=.43f);
    const FVector From=Cue->GetActorLocation(),To=Cue->LinkEnd;const uint32 Seed=Cue->NoiseSeed;
    AMCPassiveCombatCue::Spawn(T.World,From,To,Seed);AMCPassiveCombatCue* Legacy=nullptr;
    for(TActorIterator<AMCPassiveCombatCue> It(T.World);It;++It)if(It->SourcePerkID.IsNone())Legacy=*It;
    if(!TestNotNull(TEXT("Original same-seed comparison"),Legacy))return false;
    const double Now=T.World->GetGameState()->GetServerWorldTimeSeconds();
    Legacy->StartedAt=Now-.08;Cue->StartedAt=Now-.16;Legacy->Tick(.016f);Cue->Tick(.016f);
    const auto* A=Legacy->Mesh->GetProcMeshSection(0);const auto* B=Cue->Mesh->GetProcMeshSection(0);
    if(!TestNotNull(TEXT("Original mesh section"),A)||!TestNotNull(TEXT("Actual artist-controlled section"),B))return false;
    TestTrue(TEXT("Fixed native topology remains exact"),A->ProcIndexBuffer==B->ProcIndexBuffer && A->ProcVertexBuffer.Num()==B->ProcVertexBuffer.Num());
    if(A->ProcVertexBuffer.Num()!=B->ProcVertexBuffer.Num())return false;
    for(int32 I=0;I+1<A->ProcVertexBuffer.Num();I+=2){
        const FVector AC=(A->ProcVertexBuffer[I].Position+A->ProcVertexBuffer[I+1].Position)*.5;
        const FVector BC=(B->ProcVertexBuffer[I].Position+B->ProcVertexBuffer[I+1].Position)*.5;
        TestTrue(TEXT("All centerline samples preserve accepted endpoints and seeded path"),AC.Equals(BC,.001));
        const double AW=FVector::Distance(A->ProcVertexBuffer[I].Position,A->ProcVertexBuffer[I+1].Position);
        const double BW=FVector::Distance(B->ProcVertexBuffer[I].Position,B->ProcVertexBuffer[I+1].Position);
        TestTrue(TEXT("Only ribbon thickness scales"),FMath::IsNearlyEqual(BW,AW*1.5,.001));
    }
    TestTrue(TEXT("World-space endpoints never scale"),Cue->GetActorLocation()==From && FVector(Cue->LinkEnd)==To);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCombatEchoPresentationSuppression,
    "MessControl.VFX.PassiveHooks.EchoSuppressionKeepsCapturedNativeReplay",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCombatEchoPresentationSuppression::RunTest(const FString&)
{
    using namespace MCCombatPassiveTests;FWorld T;auto* OriginalTarget=T.Enemy(FVector(120,0,0));
    T.Perks->ServerGrantPerk(Echo);
    auto* Profile=NewObject<UMCPerkVFXProfile>();FMCPerkVFXSlot Slot;Slot.Mode=EMCPerkVFXSlotMode::Disabled;
    Profile->Slots.Add(TEXT("Windup"),Slot);Profile->Slots.Add(TEXT("Echo"),Slot);FString Error;
    if(!TestTrue(TEXT("Echo cosmetics prepare separately from native attack state"),T.Hero->PassivePresentation->PrepareScopedOverride(
        Echo,MCPassivePresentation::GetAllowedEvents(Echo),Profile,Error))) {AddError(Error);return false;}
    int32 Windups=0,Contacts=0;
    const auto Handle=T.Hero->PassivePresentation->OnEventAccepted.AddLambda([&](FName Event,EMCVFXPresentationResult Result){
        if(Event==TEXT("Windup"))++Windups;
        if(Event==TEXT("Echo"))++Contacts;
        if(Event==TEXT("Windup") || Event==TEXT("Echo"))TestEqual(TEXT("Only the owned cosmetic is suppressed"),Result,EMCVFXPresentationResult::Suppressed);
    });
    T.Hit(OriginalTarget);TestEqual(TEXT("Primary gameplay damage remains ordinary"),OriginalTarget->Health,980.f);
    T.Hero->SetActorLocation(Base+FVector(1000,0,0));T.Hero->SetActorRotation(FRotator(0,180,0));
    auto* AtNewPose=T.Enemy(FVector(880,0,0));T.Hero->Inventory->ServerSelect(EMCToolSlot::Brush);
    T.Step(.43f);
    int32 Ghosts=0;for(TActorIterator<AMCPassiveCombatCue> It(T.World);It;++It)if(It->Kind==EMCPassiveCombatCueKind::EchoTool)++Ghosts;
    TestEqual(TEXT("Disabled does not spawn a native ghost as a second visual"),Ghosts,0);
    TestEqual(TEXT("Suppressed Windup is accepted exactly once, not retried every tick"),Windups,1);
    T.Step(.1f);
    TestEqual(TEXT("The real delayed replay still deals forty percent at the authored snapshot"),OriginalTarget->Health,972.f);
    TestEqual(TEXT("Cosmetic suppression cannot move damage to the current hero pose"),AtNewPose->Health,1000.f);
    TestEqual(TEXT("One actual delayed hit has one causal Echo receipt"),Contacts,1);
    TestEqual(TEXT("Presentation does not requeue gameplay"),T.Runtime->PendingEchoCountForTesting(),0);
    T.Hero->PassivePresentation->OnEventAccepted.Remove(Handle);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCombatEchoCapturedPayload,
    "MessControl.VFX.PassiveHooks.EchoCarriesImmutableActualMeshAndDeadline",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FMCCombatEchoCapturedPayload::RunTest(const FString&)
{
    using namespace MCCombatPassiveTests;FWorld T;auto* OriginalTarget=T.Enemy(FVector(120,0,0));
    auto* ActualTool=FindObject<UStaticMeshComponent>(T.Hero,TEXT("InventoryTool"));
    if(!TestNotNull(TEXT("Native selected tool exists"),ActualTool) || !TestNotNull(TEXT("Native selected tool has captured geometry"),ActualTool->GetStaticMesh().Get()))return false;
    UStaticMesh* CapturedMesh=ActualTool->GetStaticMesh();
    auto* System=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Gameplay/VFX/NutCombat/NS_NutShieldHit.NS_NutShieldHit"));
    if(!TestNotNull(TEXT("A real existing Niagara source is prepared before the accepted event"),System))return false;
#if WITH_EDITOR
    System->WaitForCompilationComplete(false,false);
#endif
    if(!TestTrue(TEXT("Prepared Niagara source is ready before native attack"),System->IsReadyToRun()))return false;
    // The captured tool is a real location resource, not a receipt-time load.
    // Use the same explicit pre-commit preparation path as the isolated runner.
    TArray<UObject*> CapturableResources;
    TInlineComponentArray<UStaticMeshComponent*> ActualParts(T.Hero);
    for(auto* Part:ActualParts)if(IsValid(Part))if(auto* Mesh=Part->GetStaticMesh().Get())CapturableResources.AddUnique(Mesh);
    auto* Loading=T.World->GetSubsystem<UMCWorldPreloadSubsystem>();FString PreparationError;
    if(!TestNotNull(TEXT("Private native world has its real visual preparation service"),Loading))return false;
    bool bResourcesReady=false;
    for(int32 Attempt=0;Attempt<120 && !bResourcesReady;++Attempt) {
        bResourcesReady=Loading->PrepareResidentVisualResources(CapturableResources,PreparationError);
        if(!bResourcesReady) {T.Step(1.f/120.f);FPlatformProcess::SleepNoStats(.005f);}
    }
    if(!TestTrue(TEXT("Actual tool parts complete resident preparation before the accepted attack"),bResourcesReady))
    {AddError(PreparationError);return false;}
    if(!TestTrue(TEXT("The actual captured tool belongs to this prepared location"),Loading->IsVisualResourceRetained(CapturedMesh)))return false;
    auto* Recipe=NewObject<UMCVFXComposition>();Recipe->DurationSeconds=2.f;
    FMCVFXCompositionLayer Layer;Layer.System=System;Layer.StartSeconds=1.f;Recipe->Layers.Add(Layer);
    auto* Profile=NewObject<UMCPerkVFXProfile>();FMCPerkVFXSlot Slot;Slot.Mode=EMCPerkVFXSlotMode::Composition;Slot.Composition=Recipe;
    Profile->Slots.Add(TEXT("Windup"),Slot);FString Error;
    if(!TestTrue(TEXT("Acquire actual Echo passive"),T.Perks->ServerGrantPerk(Echo))
        || !TestTrue(TEXT("Prepare an independent delayed cosmetic before native attack"),T.Hero->PassivePresentation->PrepareScopedOverride(Echo,MCPassivePresentation::GetAllowedEvents(Echo),Profile,Error)))
    {AddError(Error);return false;}
    // RHI resource preparation above can tick real hands/tool animation. Sample
    // the independent expected pose at this actual native attack, not before
    // those pre-commit ticks. The scope invokes the unchanged BeginAttack.
    FTransform CapturedPose;
    {FMCCombatAttackScope Attack(T.Hero);CapturedPose=ActualTool->GetComponentTransform();OriginalTarget->ReceiveToolDamage(20,T.Hero);}
    T.Hero->SetActorLocation(Base+FVector(1000,0,0));T.Hero->SetActorRotation(FRotator(0,180,0));
    T.Hero->Inventory->ServerSelect(EMCToolSlot::Brush);T.Step(.43f);
    const auto Receipt=T.Hero->PassivePresentation->LastReceipt;
    TestEqual(TEXT("Payload comes from actual accepted Echo source"),Receipt.SourceID,Echo);
    TestEqual(TEXT("Native windup emitted at its real deadline"),Receipt.Event,FName(TEXT("Windup")));
    if(!TestTrue(TEXT("Accepted receipt retains captured visible tool parts"),!Receipt.CapturedMeshes.IsEmpty()))return false;
    TestEqual(TEXT("Switching tools does not replace the captured mesh"),Receipt.CapturedMeshes[0].Mesh.Get(),CapturedMesh);
    TestTrue(TEXT("Captured contact pose remains at original world transform"),(Receipt.CapturedMeshes[0].ContactPose*Receipt.Frame).Equals(CapturedPose,.01f));
    TestTrue(TEXT("Canonical accepted contact deadline remains about .1s after actual windup"),
        Receipt.CapturedContactAt-Receipt.StartedAt>.075 && Receipt.CapturedContactAt-Receipt.StartedAt<=.11);
    TestFalse(TEXT("Enemy strike keeps the horizontal captured action variant"),Receipt.bCapturedMeshOverhead);
    T.Step(.1f);TestEqual(TEXT("Presentation payload does not move or duplicate forty-percent damage"),OriginalTarget->Health,972.f);
    TestEqual(TEXT("Actual native replay is consumed once"),T.Runtime->PendingEchoCountForTesting(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCombatBilliardsVisualSubject,
    "MessControl.VFX.PassiveHooks.BilliardsCarriesActualStruckSubject",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FMCCombatBilliardsVisualSubject::RunTest(const FString&)
{
    using namespace MCCombatPassiveTests;FWorld T;auto* Moving=T.Enemy(FVector(100,0,0));auto* Struck=T.Enemy(FVector(230,0,0));
    auto* System=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Gameplay/VFX/NutCombat/NS_NutShieldHit.NS_NutShieldHit"));
    if(!TestNotNull(TEXT("Existing prepared Niagara fixture"),System))return false;
#if WITH_EDITOR
    System->WaitForCompilationComplete(false,false);
#endif
    if(!TestTrue(TEXT("Source is ready before contact"),System->IsReadyToRun()))return false;
    auto* Recipe=NewObject<UMCVFXComposition>();Recipe->DurationSeconds=2.f;
    FMCVFXCompositionLayer Layer;Layer.System=System;Layer.StartSeconds=1.f;Recipe->Layers.Add(Layer);
    auto* Profile=NewObject<UMCPerkVFXProfile>();FMCPerkVFXSlot Slot;Slot.Mode=EMCPerkVFXSlotMode::Composition;Slot.Composition=Recipe;
    Profile->Slots.Add(TEXT("Impact"),Slot);FString Error;
    if(!TestTrue(TEXT("Acquire actual Billiards passive"),T.Perks->ServerGrantPerk(Billiards))
        || !TestTrue(TEXT("Prepare contact presentation without changing launch"),T.Hero->PassivePresentation->PrepareScopedOverride(Billiards,MCPassivePresentation::GetAllowedEvents(Billiards),Profile,Error)))
    {AddError(Error);return false;}
    T.Hit(Moving);for(int32 I=0;I<5;++I)UMCCombatPassiveSubsystem::TickBilliards(Moving,.1f);
    const auto& Receipt=T.Hero->PassivePresentation->LastReceipt;
    TestEqual(TEXT("Only actual accepted secondary contact emits Impact"),Receipt.Event,FName(TEXT("Impact")));
    TestEqual(TEXT("Stable presentation source remains perk owner identity"),Receipt.SourceID,Billiards);
    TestTrue(TEXT("Actual distinct visual subject is explicit"),Receipt.bHasVisualSubject);
    TestEqual(TEXT("The struck enemy, not the perk-owning Hero, owns the visible cue"),Receipt.VisualSubjectActor.Get(),static_cast<AActor*>(Struck));
    TestEqual(TEXT("Component name is the actual stable replicated subject root"),Receipt.VisualSubjectComponent,Struck->GetRootComponent()->GetFName());
    TestTrue(TEXT("Accepted world contact frame is independent of the Hero position"),FVector::DistSquared(Receipt.Frame.GetLocation(),T.Hero->GetActorLocation())>FMath::Square(100.f));
    TestEqual(TEXT("The same accepted collision still deals exactly thirty percent"),Struck->Health,994.f);
    return true;
}
#endif
