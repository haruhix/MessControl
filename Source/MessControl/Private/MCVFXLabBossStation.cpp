#include "MCVFXLabBossStation.h"
#include "MCVFXLab.h"

#include "MCBossAIController.h"
#include "MCBossCharacter.h"
#include "MCBossClot.h"
#include "MCBossMouthAttackComponent.h"
#include "MCBossProfile.h"
#include "MCGameMode.h"
#include "MCMouthSurface.h"
#include "MCNutBoss.h"
#include "MCNutCombatEffect.h"
#include "MCNutEnemy.h"
#include "MCNutKamikaze.h"
#include "MCNutLandingShadow.h"
#include "MCFoodActor.h"
#include "MCVFXPresentationComponent.h"
#include "MCNutRainEvent.h"
#include "MCNutRainProfile.h"
#include "MCPlaytestBotController.h"
#include "MCReactionVFX.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Animation/AnimSequence.h"
#include "BrainComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

AMCVFXLabBossStation::AMCVFXLabBossStation()
{
    bReplicates=true;
    bAlwaysRelevant=true;
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bStartWithTickEnabled=false;
    PrimaryActorTick.TickInterval=.05f;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("StationRoot")));
    Label=CreateDefaultSubobject<UTextRenderComponent>(TEXT("ObservedStatus"));
    Label->SetupAttachment(RootComponent);
    Label->SetRelativeLocation(FVector(0,-650,650));
    Label->SetRelativeRotation(FRotator(0,-90,0));
    Label->SetHorizontalAlignment(EHTA_Center);
    Label->SetWorldSize(30);
    Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Label->SetTextRenderColor(FColor(210,235,255));
    EntryPresentation=CreateDefaultSubobject<UMCVFXPresentationComponent>(TEXT("EntryPreviewPresentation"));
    EntryPresentation->SourceID=NAME_None; // Only the selected Entry fixture prepares this source.
    EntryPresentation->Events={TEXT("Approach"),TEXT("Impact")};
    NutProfile=FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/DA_NutRain.DA_NutRain"));
    SubjectClass=FSoftObjectPath(TEXT("/Game/Blueprints/BP_PlayerCharacter.BP_PlayerCharacter_C"));
    ZombieClass=FSoftObjectPath(TEXT("/Game/Gameplay/Boss/BP_ZombieBoss.BP_ZombieBoss_C"));
    Phase3Class=FSoftObjectPath(TEXT("/Game/Gameplay/Boss/Phase3/BP_BossPhase3.BP_BossPhase3_C"));
    ZombieProfile=FSoftObjectPath(TEXT("/Game/Gameplay/Boss/DA_ZombieBoss.DA_ZombieBoss"));
    Phase3Profile=FSoftObjectPath(TEXT("/Game/Gameplay/Boss/Phase3/DA_BossPhase3.DA_BossPhase3"));
}

void AMCVFXLabBossStation::BeginPlay()
{
    Super::BeginPlay();
    SetActorTickEnabled(false);
#if UE_BUILD_SHIPPING
    Status=TEXT("Development laboratory disabled in shipping");
#else
    if(HasAuthority()) {
        bRunning=false;
        Status=TEXT("Paused; waiting for a nearby spectator");
        // The lab manager starts only nearby stations after all BeginPlay calls.
        // A station placed on its own retains its authored auto-run behavior.
        if(!bComparisonMode && bAutoRun && !AMCVFXLab::Find(GetWorld())) {
            SetRunning(true);
            NextCycleAt=GetWorld()->GetTimeSeconds()+FMath::Max(.1f,StartDelaySeconds);
        }
    }
#endif
    UpdateLabel();
}

bool AMCVFXLabBossStation::IsNutKind() const
{ return uint8(Kind)<=uint8(EMCVFXLabBossKind::MageEntrance) || Kind==EMCVFXLabBossKind::TankCounterShove || Kind==EMCVFXLabBossKind::TankHeadSplit; }
bool AMCVFXLabBossStation::IsMageKind() const
{ return uint8(Kind)>=uint8(EMCVFXLabBossKind::MageMelee) && uint8(Kind)<=uint8(EMCVFXLabBossKind::MageEntrance); }
bool AMCVFXLabBossStation::IsKamikazeKind() const
{ return Kind==EMCVFXLabBossKind::KamikazeFuse || Kind==EMCVFXLabBossKind::KamikazeBlast; }
bool AMCVFXLabBossStation::IsEntranceKind() const
{ return Kind==EMCVFXLabBossKind::TankEntrance || Kind==EMCVFXLabBossKind::MageEntrance; }
bool AMCVFXLabBossStation::IsDeathKind() const
{ return Kind==EMCVFXLabBossKind::TankDeath || Kind==EMCVFXLabBossKind::MageDeath || Kind==EMCVFXLabBossKind::ZombieDeath; }
bool AMCVFXLabBossStation::IsCombatKind() const
{
    return Kind==EMCVFXLabBossKind::TankMelee || Kind==EMCVFXLabBossKind::TankCharge
        || Kind==EMCVFXLabBossKind::TankJump || Kind==EMCVFXLabBossKind::TankRoll
        || Kind==EMCVFXLabBossKind::MageMelee || Kind==EMCVFXLabBossKind::MageFireball
        || Kind==EMCVFXLabBossKind::MageSummon || Kind==EMCVFXLabBossKind::MageNutRain
        || Kind==EMCVFXLabBossKind::MageTeleport || Kind==EMCVFXLabBossKind::ZombiePunchLeft
        || Kind==EMCVFXLabBossKind::TankCounterShove
        || Kind==EMCVFXLabBossKind::ZombiePunchRight || Kind==EMCVFXLabBossKind::ZombieKick
        || Kind==EMCVFXLabBossKind::Phase3PunchRight || Kind==EMCVFXLabBossKind::Phase3AreaAttack;
}

FString AMCVFXLabBossStation::KindName() const
{
    // Retain the serialized enum value used by saved lab stations.
    if(Kind==EMCVFXLabBossKind::TankShieldBreakOverflow) return TEXT("TankShieldBreak");
    const UEnum* Enum=StaticEnum<EMCVFXLabBossKind>();
    return Enum?Enum->GetNameStringByValue(int64(Kind)):TEXT("Boss");
}

void AMCVFXLabBossStation::Track(AActor* Actor)
{
    if(IsValid(Actor) && Actor!=this && Actor!=Floor) {
        OwnedActors.AddUnique(Actor);
        Actor->Tags.AddUnique(TEXT("MC_VFXLabRuntime"));
    }
}

bool AMCVFXLabBossStation::Owns(const AActor* Actor) const
{
    for(int32 Depth=0;IsValid(Actor) && Depth<8;++Depth,Actor=Actor->GetOwner())
    {
        // A borrowed pawn, its identity and its tool/attachment owner chain
        // belong to the caller. Never claim them for station cleanup.
        if(bBorrowedSubject && IsValid(Subject) && (Actor==Subject || Actor==Subject->GetController() || Actor==Subject->GetPlayerState()))return false;
        if(Actor==this || Actor==NutOwner || Actor==NutBoss || Actor==Boss || Actor==Subject || Actor==SubjectController) return true;
    }
    return false;
}

void AMCVFXLabBossStation::GatherOwnedActors()
{
    if(!GetWorld()) return;
    const FBox Bounds=IsValid(Floor)?Floor->Surface->Bounds.GetBox().ExpandBy(FVector(50,50,1400)):FBox(ForceInit);
    for(TActorIterator<AActor> It(GetWorld());It;++It) {
        AActor* Actor=*It;
        if(Actor==this || Actor==Floor || Actor->IsActorBeingDestroyed()) continue;
        if(Owns(Actor)) Track(Actor);
        // Legacy hit bursts have no owner. Claim only bursts emitted in this
        // station's separate support volume during the current cycle.
        else if(auto* Reaction=Cast<AMCReactionVFX>(Actor); Reaction && !Reaction->GetOwner()
            && Reaction->StartedAt>=CycleStartedAt && Bounds.IsInside(Reaction->GetActorLocation())) {
            Reaction->SetOwner(this);
            Track(Reaction);
        }
    }
}

bool AMCVFXLabBossStation::BeginCycle(AMCToothCharacter* BorrowedSubject)
{
    Cleanup();
    CycleStartedAt=GetWorld()->GetTimeSeconds();
    bCycleOpen=true;
    if(!IsValid(Floor) || Floor->GetWorld()!=GetWorld()) {Status=TEXT("Missing station Floor");return false;}
    FHitResult Ground;
    if(!Floor->InteriorSurfacePoint(GetActorLocation(),250,Ground)) {Status=TEXT("Boss footprint is outside Floor");return false;}
    FRotator Facing=GetActorRotation(); Facing.Pitch=0; Facing.Roll=0;
    const FVector Forward=Facing.Vector();
    const bool Ranged=Kind==EMCVFXLabBossKind::TankCharge || Kind==EMCVFXLabBossKind::TankJump
        || Kind==EMCVFXLabBossKind::TankRoll || Kind==EMCVFXLabBossKind::MageFireball
        || Kind==EMCVFXLabBossKind::MageSummon || Kind==EMCVFXLabBossKind::MageNutRain
        || Kind==EMCVFXLabBossKind::MageTeleport || Kind==EMCVFXLabBossKind::Phase3AreaAttack;
    FHitResult SubjectGround;
    const FVector SubjectPoint=BorrowedSubject?BorrowedSubject->GetActorLocation():Ground.ImpactPoint+Forward*(Ranged?650.f:260.f);
    if(!Floor->InteriorSurfacePoint(SubjectPoint,45,SubjectGround)) {
        Status=TEXT("Subject footprint is outside Floor");return false;
    }
    FActorSpawnParameters Spawn;
    Spawn.Owner=this;
    Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    if(BorrowedSubject) {
        Subject=BorrowedSubject;bBorrowedSubject=true;
        SubjectHealthBefore=Subject->Status->State.Health;
    } else {
    SubjectController=GetWorld()->SpawnActor<AMCPlaytestBotController>(AMCPlaytestBotController::StaticClass(),GetActorLocation(),Facing,Spawn);
    if(!SubjectController) {Status=TEXT("Subject controller spawn failed");return false;}
    Track(SubjectController);
    if(bComparisonMode) SubjectController->Configure(EMCPlaytestBotSkill::Regular,0,ComparisonSeed);
    UClass* ToothClass=SubjectClass.LoadSynchronous();
    if(!ToothClass) {Status=TEXT("Saved tooth Blueprint is missing");return false;}
    const auto* ToothDefaults=ToothClass->GetDefaultObject<AMCToothCharacter>();
    const float SubjectHeight=ToothDefaults->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()+4;
    const FTransform SubjectPose(Facing,SubjectGround.ImpactPoint+FVector(0,0,SubjectHeight));
    if(bComparisonMode) {
        Subject=GetWorld()->SpawnActorDeferred<AMCToothCharacter>(ToothClass,SubjectPose,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(Subject) {
            if(bLegacyFeedback) Subject->Tags.AddUnique(TEXT("MC.LegacyCombatFeedback"));
            Subject->FinishSpawning(SubjectPose);
        }
    } else Subject=GetWorld()->SpawnActor<AMCToothCharacter>(ToothClass,SubjectPose.GetLocation(),Facing,Spawn);
    if(!Subject) {Status=TEXT("Subject pawn spawn failed");return false;}
    Track(Subject);
    SubjectController->Possess(Subject);
    if(bComparisonMode) SubjectController->SetLabControlled(true);
    GetWorldTimerManager().ClearAllTimersForObject(SubjectController);
    SubjectController->StopMovement();
    SubjectController->SetActorTickEnabled(false);
    if(auto* Brain=SubjectController->GetBrainComponent()) Brain->StopLogic(TEXT("VFX laboratory fixed subject"));
    APlayerState* Identity=SubjectController->GetPlayerState<APlayerState>();
    if(!Identity) {Status=TEXT("Subject has no PlayerState");return false;}
    Identity->SetIsSpectator(false); Identity->SetIsOnlyASpectator(false); Identity->SetIsABot(true);
    Identity->SetPlayerName(KindName()+TEXT(" subject")); Track(Identity);
    Subject->Status->Initialize(100);
    SubjectHealthBefore=Subject->Status->State.Health;
    if(!AMCGameMode::IsGameplayParticipant(SubjectController)) {Status=TEXT("Subject is not a gameplay participant");return false;}
    }
    if(IsKamikazeKind() || Kind==EMCVFXLabBossKind::RainEntry) {
        auto* Data=NutProfile.LoadSynchronous();
        if(!Data) {Status=TEXT("Saved DA_NutRain is missing");return false;}
        NutOwner=GetWorld()->SpawnActor<AMCNutRainEvent>(AMCNutRainEvent::StaticClass(),GetActorLocation(),Facing,Spawn);
        if(!NutOwner || !NutOwner->PrepareIsolatedSpellContext(Floor,Data)) {
            Status=TEXT("Disposable real rain owner could not be prepared");return false;
        }
        Track(NutOwner);
        if(Kind==EMCVFXLabBossKind::RainEntry) {
            if(!NutOwner->SpawnIsolatedEntryNut(Ground.ImpactPoint,ComparisonSeed) || NutOwner->Nuts.Num()!=1) {
                Status=TEXT("Real entry nut or its baked food collision is unavailable");return false;
            }
            EntryNut=NutOwner->Nuts[0];Track(EntryNut);
        } else {
            FMCNutEnemySettings Creep=NutOwner->Settings.Enemy;
            const auto& Settings=NutOwner->Settings.Boss;
            Creep.MaxHealth=Settings.CreepHealth;Creep.BodyRadius=FMath::Clamp(Creep.BodyRadius,20.f,80.f);
            Creep.MoveSpeed=Settings.CreepMoveSpeed;Creep.Sanitize();
            PreparedCreepMesh=Settings.WholeMesh.LoadSynchronous();
            if(!PreparedCreepMesh) {Status=TEXT("Saved native summon nut mesh is missing");return false;}
            const FVector Scale(Settings.CreepHeight/FMath::Max(.001,PreparedCreepMesh->GetBounds().BoxExtent.Z*2));
            const FTransform Pose(Facing,Ground.ImpactPoint+FVector(0,0,Creep.BodyRadius+4));
            Kamikaze=GetWorld()->SpawnActorDeferred<AMCNutKamikaze>(AMCNutKamikaze::StaticClass(),Pose,NutOwner,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(!Kamikaze) {Status=TEXT("Real kamikaze spawn failed");return false;}
            Track(Kamikaze);Kamikaze->ConfigureEnemy(Floor,PreparedCreepMesh,Scale,Creep);
            Kamikaze->FinishSpawning(Pose);Kamikaze->SetActorTickEnabled(false);
            NutOwner->RegisterEncounterEnemy(Kamikaze);
            // The native fuse is intentionally NOT armed until the readiness
            // barrier. BeginPlay has already made the real model and MIDs.
        }
    } else if(IsNutKind()) {
        const UMCNutRainProfile* Data=NutProfile.LoadSynchronous();
        if(!Data) {Status=TEXT("Saved DA_NutRain is missing");return false;}
        FMCNutBossSettings Settings=Data->Settings.Boss; Settings.Sanitize();
        NutOwner=GetWorld()->SpawnActor<AMCNutRainEvent>(AMCNutRainEvent::StaticClass(),GetActorLocation(),Facing,Spawn);
        if(!NutOwner) {Status=TEXT("Nut owner spawn failed");return false;}
        Track(NutOwner); NutOwner->Settings=Data->Settings; NutOwner->SetActorTickEnabled(false);
        if(Kind==EMCVFXLabBossKind::TankHeadSplit
            && !NutOwner->PrepareIsolatedSpellContext(Floor,const_cast<UMCNutRainProfile*>(Data))) {
            Status=TEXT("Head split requires the real encounter Enemies stage");return false;
        }
        const EMCNutBossRole BossRole=IsMageKind()?EMCNutBossRole::Mage:EMCNutBossRole::Tank;
        BossStart=Ground.ImpactPoint+FVector(0,0,Settings.BodyRadiusForRole(BossRole)+4);
        const FTransform Pose(Facing,BossStart);
        NutBoss=GetWorld()->SpawnActorDeferred<AMCNutBoss>(AMCNutBoss::StaticClass(),Pose,NutOwner,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!NutBoss) {Status=TEXT("Nut boss spawn failed");return false;}
        Track(NutBoss);
        if(bComparisonMode && bLegacyFeedback) NutBoss->Tags.AddUnique(TEXT("MC.LegacyCombatFeedback"));
        NutBoss->LabStation=this; NutBoss->LabTarget=Subject; NutBoss->Target=Subject;
        NutBoss->ConfigureEncounter(Floor,NutOwner,BossRole,Settings,1,bComparisonMode?ComparisonSeed:GetUniqueID()+Cycles*137);
        FTransform AirbornePose=Pose; AirbornePose.SetLocation(NutBoss->GetActorLocation());
        NutBoss->FinishSpawning(AirbornePose);
        if(Kind==EMCVFXLabBossKind::TankHeadSplit) {
            // Retain the actual future phase on the actual component before
            // readiness. Normal native RevealMagePhase still owns the switch.
            if(!NutBoss->PrepareIsolatedMagePhaseResources()) {
                Status=TEXT("Saved Mage resources for the real second phase are missing");return false;
            }
            NutBoss->MageModel->SetVisibility(false);
        }
        NutOwner->Bosses.Add(NutBoss); NutOwner->RegisterEncounterEnemy(NutBoss);
        bSawEntrance=NutBoss->State==EMCNutBossState::Falling;
    } else {
        const bool Phase3=Kind==EMCVFXLabBossKind::Phase3PunchRight || Kind==EMCVFXLabBossKind::Phase3AreaAttack;
        UClass* Class=Phase3?Phase3Class.LoadSynchronous():ZombieClass.LoadSynchronous();
        UMCBossProfile* Profile=Phase3?Phase3Profile.LoadSynchronous():ZombieProfile.LoadSynchronous();
        if(!Class || !Profile) {Status=TEXT("Saved boss Blueprint/profile is missing");return false;}
        const auto* Defaults=Class->GetDefaultObject<AMCBossCharacter>();
        BossStart=Ground.ImpactPoint+FVector(0,0,Defaults->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()+3);
        const FTransform Pose(Facing,BossStart);
        Boss=GetWorld()->SpawnActorDeferred<AMCBossCharacter>(Class,Pose,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Boss) {Status=TEXT("Boss Blueprint spawn failed");return false;}
        Track(Boss); Boss->Profile=Profile; Boss->Tags.AddUnique(TEXT("MC_DevBoss"));
        if(bComparisonMode && bLegacyFeedback) Boss->Tags.AddUnique(TEXT("MC.LegacyCombatFeedback"));
        Boss->FinishSpawning(Pose);
        Boss->ActivateBoss();
        if(auto* Brain=Cast<AMCBossAIController>(Boss->GetController())) {
            Brain->StopBossBrain(); Brain->SetActorTickEnabled(false); Brain->SetOwner(this); Track(Brain);
        }
        if(Boss->Runtime.State==EMCBossState::Dormant) {Status=TEXT("Boss activation was rejected");return false;}
        if(!AMCBossCharacter::IsLivingPlayer(Subject)) {Status=TEXT("Real boss rejects subject");return false;}
    }
    Status=TEXT("Spawned real boss and subject");
    return true;
}

AMCToothCharacter* AMCVFXLabBossStation::GetComparisonSubject() const
{
    return bComparisonMode && IsValid(Subject)?Subject.Get():nullptr;
}

AActor* AMCVFXLabBossStation::GetComparisonEnemy() const
{
    if(!bComparisonMode) return nullptr;
    if(IsValid(NutBoss)) return NutBoss.Get();
    if(IsValid(Kamikaze)) return Kamikaze.Get();
    if(IsValid(EntryNut)) return EntryNut.Get();
    return IsValid(Boss)?Boss.Get():nullptr;
}

UMCVFXPresentationComponent* AMCVFXLabBossStation::GetComparisonPresentation() const
{
    if(IsValid(NutBoss))return NutBoss->VFXContext.Get();
    if(IsValid(Kamikaze))return Kind==EMCVFXLabBossKind::KamikazeFuse?Kamikaze->FusePresentation.Get():Kamikaze->VFXContext.Get();
    return Kind==EMCVFXLabBossKind::RainEntry?EntryPresentation.Get():nullptr;
}

USceneComponent* AMCVFXLabBossStation::GetComparisonVisualSubject() const
{
    if(IsValid(NutBoss))return NutBoss->BossRole==EMCNutBossRole::Mage?NutBoss->MageModel.Get():NutBoss->TankModel.Get();
    if(IsValid(Kamikaze))return Kamikaze->SummonModel.Get();
    return IsValid(EntryNut)?EntryNut->Visual.Get():nullptr;
}

bool AMCVFXLabBossStation::IsComparisonReady() const
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if(!bComparisonMode || !bCycleOpen || !IsValid(Subject) || !Subject->Status || !Subject->Status->IsAlive()) return false;
    if(IsValid(NutBoss)) return NutBoss->CanReceiveToolHit();
    if(IsValid(Kamikaze))return Kamikaze->IsEncounterAlive() && !Kamikaze->bArmed;
    if(IsValid(EntryNut))return !EntryNut->IsDisposed() && IsValid(NutOwner);
    return IsValid(Boss) && Boss->CanReceiveWeaponHit();
#else
    return false;
#endif
}

bool AMCVFXLabBossStation::PrepareComparison(EMCVFXLabBossKind Selected)
{return PrepareComparisonInternal(Selected,nullptr);}

bool AMCVFXLabBossStation::PrepareComparisonUsingSubject(EMCVFXLabBossKind Selected,AMCToothCharacter* ExistingSubject)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    auto* Controller=IsValid(ExistingSubject)?Cast<APlayerController>(ExistingSubject->GetController()):nullptr;
    if(!GetWorld() || GetWorld()->WorldType!=EWorldType::Game || GetWorld()->GetOutermost()!=GetTransientPackage()
        || !IsValid(ExistingSubject) || ExistingSubject->GetWorld()!=GetWorld() || !ExistingSubject->HasAuthority()
        || !ExistingSubject->Status || !ExistingSubject->Status->IsAlive() || !Controller || Controller->GetPawn()!=ExistingSubject
        || ExistingSubject->GetPlayerState()!=Controller->GetPlayerState<APlayerState>() || !AMCGameMode::IsGameplayParticipant(Controller)) {
        Status=TEXT("Borrowed comparison requires one living pawn with its current controller/PlayerState in an isolated transient Game world.");return false;
    }
    return PrepareComparisonInternal(Selected,ExistingSubject);
#else
    return false;
#endif
}

bool AMCVFXLabBossStation::PrepareComparisonInternal(EMCVFXLabBossKind Selected,AMCToothCharacter* BorrowedSubject)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if(!HasAuthority() || !GetWorld() || !GetWorld()->IsGameWorld()
        || !StaticEnum<EMCVFXLabBossKind>()->IsValidEnumValue(int64(Selected))) return false;
    bComparisonMode=true;
    bRunning=false;
    Kind=Selected;
    SetActorTickEnabled(true);
    const bool Prepared=BeginCycle(BorrowedSubject);
    if(!Prepared) {
        const FString Reason=Status;
        Cleanup();
        Status=Reason;
    } else Status=TEXT("Comparison prepared; waiting for landing or a manual strike");
    ForceNetUpdate(); UpdateLabel();
    return Prepared;
#else
    return false;
#endif
}

bool AMCVFXLabBossStation::RunComparisonAction()
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if(!HasAuthority() || !IsComparisonReady()) return false;
    // A melee artist scenario retains this exact knight for Attack1, Attack2,
    // then Combo. Never reset/force its native sequence or skip recovery/cooldown.
    // Every other comparison remains a single accepted start.
    if(bActionStarted && Kind!=EMCVFXLabBossKind::TankMelee) return false;
    if(IsValid(NutBoss) && NutBoss->State!=EMCNutBossState::Idle) return false;
    if(Kind==EMCVFXLabBossKind::TankMelee && (!IsValid(NutBoss)
        || NutBoss->BossRole!=EMCNutBossRole::Tank
        || NutBoss->GetAttackCooldownRemaining(EMCNutBossAttack::Melee)>0.f)) return false;
    if(IsValid(Boss) && Boss->Runtime.State!=EMCBossState::Searching && Boss->Runtime.State!=EMCBossState::Chasing) return false;
    SubjectHealthBefore=Subject->Status->State.Health;
    ComparisonIncomingHealth=0;
    if(IsEntranceKind()) {
        bActionStarted=true; ActionStartedAt=GetWorld()->GetTimeSeconds();
        const bool Observed=bSawEntrance && SeenCues.Contains(uint8(EMCNutCombatCue::EntranceImpact));
        FinishCycle(Observed,TEXT("Observed real comparison entrance; fixtures retained"));
        return Observed;
    }
    const float EnemyHealthBefore=IsValid(NutBoss)?NutBoss->Health:IsValid(Boss)?Boss->Runtime.Health:0;
    const float EnemyShieldBefore=IsValid(NutBoss)?NutBoss->ShieldHealth:0;
    const bool Accepted=LaunchAction();
    if(IsDeathKind() || Kind==EMCVFXLabBossKind::TankHeadSplit || Kind==EMCVFXLabBossKind::MageHurt || Kind==EMCVFXLabBossKind::ZombieHurt
        || Kind==EMCVFXLabBossKind::TankShieldHit || Kind==EMCVFXLabBossKind::TankShieldBreakOverflow) {
        const float EnemyHealthAfter=IsValid(NutBoss)?NutBoss->Health:Boss->Runtime.Health;
        const float EnemyShieldAfter=IsValid(NutBoss)?NutBoss->ShieldHealth:0;
        ComparisonOutgoingHealth=FMath::Max(0.f,EnemyHealthBefore-EnemyHealthAfter);
        ComparisonOutgoingShield=FMath::Max(0.f,EnemyShieldBefore-EnemyShieldAfter);
    }
    bRunning=Accepted;
    if(!Accepted) {
        bActionStarted=false;
        Status=TEXT("Selected real ability rejected; fixtures retained");
    } else Status=TEXT("Selected real ability running; fixtures retained after recovery");
    ForceNetUpdate(); UpdateLabel();
    return Accepted;
#else
    return false;
#endif
}

float AMCVFXLabBossStation::ApplyComparisonOutgoing(float Damage)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if(!HasAuthority() || !IsComparisonReady() || (!IsValid(NutBoss) && !IsValid(Boss)) || !FMath::IsFinite(Damage) || Damage<=0) return 0;
    const float HealthBefore=IsValid(NutBoss)?NutBoss->Health:Boss->Runtime.Health;
    const float ShieldHealthBefore=IsValid(NutBoss)?NutBoss->ShieldHealth:0;
    if(IsValid(NutBoss)) NutBoss->ReceiveToolDamage(Damage,Subject);
    else Boss->ReceiveBossDamage(Damage,Subject);
    const float HealthAfter=IsValid(NutBoss)?NutBoss->Health:Boss->Runtime.Health;
    const float ShieldHealthAfter=IsValid(NutBoss)?NutBoss->ShieldHealth:0;
    ComparisonOutgoingHealth=FMath::Max(0.f,HealthBefore-HealthAfter);
    ComparisonOutgoingShield=FMath::Max(0.f,ShieldHealthBefore-ShieldHealthAfter);
    Status=FString::Printf(TEXT("%s outgoing: HP %.1f, shield %.1f"),
        ComparisonOutgoingHealth>0 || ComparisonOutgoingShield>0?TEXT("Accepted"):TEXT("Rejected"),
        ComparisonOutgoingHealth,ComparisonOutgoingShield);
    ForceNetUpdate(); UpdateLabel();
    return ComparisonOutgoingHealth;
#else
    return 0;
#endif
}

float AMCVFXLabBossStation::ApplyComparisonIncoming(float Damage)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    AActor* Enemy=GetComparisonEnemy();
    if(!HasAuthority() || !IsComparisonReady() || !IsValid(Enemy) || !FMath::IsFinite(Damage) || Damage<=0) return 0;
    const float HealthBefore=Subject->Status->State.Health;
    const FVector Direction=(Subject->GetActorLocation()-Enemy->GetActorLocation()).GetSafeNormal2D(SMALL_NUMBER,GetActorForwardVector());
    Subject->Status->Damage(Damage,Direction,Enemy);
    ComparisonIncomingHealth=FMath::Max(0.f,HealthBefore-Subject->Status->State.Health);
    Status=FString::Printf(TEXT("%s incoming: HP %.1f"),ComparisonIncomingHealth>0?TEXT("Accepted"):TEXT("Rejected"),ComparisonIncomingHealth);
    ForceNetUpdate(); UpdateLabel();
    return ComparisonIncomingHealth;
#else
    return 0;
#endif
}

FString AMCVFXLabBossStation::GetComparisonStatusText() const
{
    if(!bComparisonMode) return Status;
    const float PlayerHealth=IsValid(Subject) && Subject->Status?Subject->Status->State.Health:0;
    const float PlayerMaxHealth=IsValid(Subject) && Subject->Status?Subject->Status->State.MaxHealth:0;
    const float EnemyHealth=IsValid(NutBoss)?NutBoss->Health:IsValid(Boss)?Boss->Runtime.Health:0;
    const float EnemyMaxHealth=IsValid(NutBoss)?NutBoss->Settings.MaxHealth:IsValid(Boss)?Boss->Runtime.MaxHealth:0;
    const float ShieldHealth=IsValid(NutBoss)?NutBoss->ShieldHealth:0;
    const FString Phase=IsValid(NutBoss)?StaticEnum<EMCNutBossState>()->GetNameStringByValue(int64(NutBoss->State)):
        IsValid(Boss)?StaticEnum<EMCBossState>()->GetNameStringByValue(int64(Boss->Runtime.State)):TEXT("Missing fixtures");
    const float ObservedIncoming=bActionStarted?FMath::Max(0.f,SubjectHealthBefore-PlayerHealth):ComparisonIncomingHealth;
    return FString::Printf(TEXT("%s | %s\nPlayer HP %.0f/%.0f | Boss HP %.0f/%.0f | Shield %.0f\nOutgoing HP %.1f, shield %.1f | Incoming HP %.1f\n%s"),
        *KindName(),*Phase,PlayerHealth,PlayerMaxHealth,EnemyHealth,EnemyMaxHealth,ShieldHealth,
        ComparisonOutgoingHealth,ComparisonOutgoingShield,ObservedIncoming,*Status);
}

void AMCVFXLabBossStation::ResumeComparisonBot()
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if(!HasAuthority() || !bComparisonMode || !IsValid(Subject) || !IsValid(SubjectController)
        || (Subject->GetController() && Subject->GetController()!=SubjectController)) return;
    if(Subject->GetController()!=SubjectController) SubjectController->Possess(Subject);
    SubjectController->SetLabControlled(true);
    GetWorldTimerManager().ClearAllTimersForObject(SubjectController);
    SubjectController->SetActorTickEnabled(false);
    if(APlayerState* Identity=SubjectController->GetPlayerState<APlayerState>()) {
        Identity->SetIsSpectator(false); Identity->SetIsOnlyASpectator(false); Identity->SetIsABot(true);
    }
#endif
}

bool AMCVFXLabBossStation::LaunchAction()
{
    ActionStartedAt=GetWorld()->GetTimeSeconds(); bActionStarted=true;
    MaximumTravel=0;
    if(IsKamikazeKind()) {
        if(!IsValid(Kamikaze) || !PreparedCreepMesh || Kamikaze->bArmed)return false;
        const bool Accepted=Kamikaze->ConfigureKamikaze(Floor,PreparedCreepMesh,Kamikaze->MeshScale,Kamikaze->Settings);
        if(Accepted)Kamikaze->SetActorTickEnabled(true);
        return Accepted;
    }
    if(Kind==EMCVFXLabBossKind::RainEntry) {
        return IsValid(EntryNut) && IsValid(NutOwner) && NutOwner->LaunchIsolatedEntryNut();
    }
    if(IsNutKind()) {
        if(!IsValid(NutBoss) || !NutBoss->IsEncounterAlive()) return false;
        NutBoss->Target=Subject;
        BossHealthBefore=NutBoss->Health; ShieldBefore=NutBoss->ShieldHealth;
        if(Kind==EMCVFXLabBossKind::TankCounterShove) {
            CounterContacts=0;bSawCounter=false;NextCounterContactAt=ActionStartedAt;
            // Actual low-damage player contacts will feed native pressure on
            // later ticks. Never set its random threshold/queued flag.
            SetActorTickEnabled(true);return true;
        }
        if(IsDeathKind() || Kind==EMCVFXLabBossKind::TankHeadSplit) {
            if(NutBoss->ShieldHealth>0) NutBoss->ReceiveToolDamage(NutBoss->ShieldHealth,nullptr);
            AppliedHealthDamage=NutBoss->ReceiveToolDamage(NutBoss->Health+1,nullptr);
            bSawDeath=NutBoss->State==EMCNutBossState::Defeated;
            return AppliedHealthDamage>0 && (bSawDeath || (Kind==EMCVFXLabBossKind::TankHeadSplit && NutBoss->State==EMCNutBossState::Transforming));
        }
        if(Kind==EMCVFXLabBossKind::MageHurt) {
            AppliedHealthDamage=NutBoss->ReceiveToolDamage(25,Subject);
            bSawHurt=AppliedHealthDamage>0 && NutBoss->VisualHitAt>=ActionStartedAt;
            return bSawHurt;
        }
        if(Kind==EMCVFXLabBossKind::TankShieldHit || Kind==EMCVFXLabBossKind::TankShieldBreakOverflow) {
            const float ExpectedShield=bComparisonMode?NutBoss->BossSettings.ShieldMaxHealth:500.f;
            if(!FMath::IsNearlyEqual(ShieldBefore,ExpectedShield,.1f) || !NutBoss->IsShieldProtectingFrom(Subject->GetActorLocation())) return false;
            const float Raw=Kind==EMCVFXLabBossKind::TankShieldHit?75.f:ShieldBefore+100.f;
            AppliedHealthDamage=NutBoss->ReceiveToolDamage(Raw,Subject);
            return FMath::IsNearlyEqual(BossHealthBefore,NutBoss->Health,.1f) && FMath::IsNearlyZero(AppliedHealthDamage,.1f)
                && FMath::IsNearlyEqual(NutBoss->ShieldHealth,FMath::Max(0.f,ShieldBefore-Raw),.1f);
        }
        if(Kind==EMCVFXLabBossKind::MageTeleport) NutBoss->BeginTeleport();
        else {
            EMCNutBossAttack Attack=EMCNutBossAttack::Melee;
            switch(Kind) {
            case EMCVFXLabBossKind::TankCharge: Attack=EMCNutBossAttack::Charge;break;
            case EMCVFXLabBossKind::TankJump: Attack=EMCNutBossAttack::Jump;break;
            case EMCVFXLabBossKind::TankRoll: Attack=EMCNutBossAttack::Roll;break;
            case EMCVFXLabBossKind::MageFireball: Attack=EMCNutBossAttack::Fireball;break;
            case EMCVFXLabBossKind::MageSummon: Attack=EMCNutBossAttack::Summon;break;
            case EMCVFXLabBossKind::MageNutRain: Attack=EMCNutBossAttack::NutRain;break;
            default: break;
            }
            NutBoss->BeginAttack(Attack);
        }
        return NutBoss->State==EMCNutBossState::Telegraph;
    }
    if(!IsValid(Boss) || !Boss->IsBossAlive()) return false;
    BossHealthBefore=Boss->Runtime.Health;
    if(Kind==EMCVFXLabBossKind::ZombieDeath) {
        AppliedHealthDamage=Boss->ReceiveBossDamage(Boss->Runtime.Health+1,Subject);
        bSawDeath=Boss->Runtime.State==EMCBossState::Dead;
        return bSawDeath && AppliedHealthDamage>0;
    }
    if(Kind==EMCVFXLabBossKind::ZombieHurt) {
        AppliedHealthDamage=Boss->ReceiveBossDamage(25,Subject);
        bSawHurt=AppliedHealthDamage>0 && Boss->Runtime.HurtStartedAt>=ActionStartedAt;
        return bSawHurt;
    }
    if(Kind==EMCVFXLabBossKind::ZombieRoar) return Boss->PreviewAnimation(EMCBossAnimationPreview::Roar);
    FName AttackId=TEXT("PunchRight");
    if(Kind==EMCVFXLabBossKind::ZombiePunchLeft) AttackId=TEXT("PunchLeft");
    else if(Kind==EMCVFXLabBossKind::ZombieKick) AttackId=TEXT("Kick");
    else if(Kind==EMCVFXLabBossKind::Phase3AreaAttack) AttackId=TEXT("AreaAttack");
    return Boss->BeginAttack(AttackId,Subject);
}

void AMCVFXLabBossStation::Observe()
{
    GatherOwnedActors();
    for(const auto& Weak:OwnedActors) if(AActor* Actor=Weak.Get(); Actor && !ObservedActors.Contains(Weak)) {
        ObservedActors.Add(Weak);
        if(const auto* Cue=Cast<AMCNutCombatEffect>(Actor)) {
            SeenCues.Add(uint8(Cue->Cue.Type));
            if(Cue->Cue.Type==EMCNutCombatCue::FireImpact) {bSawFireImpact=true;++Impacts;}
            else if(Cue->Cue.Type==EMCNutCombatCue::SlamImpact || Cue->Cue.Type==EMCNutCombatCue::MeleeSlash
                || Cue->Cue.Type==EMCNutCombatCue::EntranceImpact || Cue->Cue.Type==EMCNutCombatCue::ShieldHit
                || Cue->Cue.Type==EMCNutCombatCue::TeleportBurst || Cue->Cue.Type==EMCNutCombatCue::DeathBurst) ++Impacts;
        }
        if(Cast<AMCBossClot>(Actor)) bSawClot=true;
        if(const auto* Enemy=Cast<AMCNutEnemy>(Actor); Enemy && !Cast<AMCNutBoss>(Enemy)) ++GeneratedCreeps;
        if(const auto* Patch=Cast<AMCMouthSurface>(Actor); Patch && Patch->bUlcer) bSawUlcer=true;
        if(const auto* Reaction=Cast<AMCReactionVFX>(Actor)) {
            if(Reaction->Effect==EMCReactionEffect::BossWind) bSawWind=true;
            if(Reaction->Effect==EMCReactionEffect::BlackClotImpact) {bSawClotImpact=true;++Impacts;}
        }
    }
    if(IsValid(NutBoss)) {
        const int32 Tell=NutBoss->LabTelegraphs-LastTelegraphs,Execute=NutBoss->LabExecutions-LastExecutions,Recover=NutBoss->LabRecoveries-LastRecoveries;
        Telegraphs+=Tell; CycleTelegraphs+=Tell; Executions+=Execute; CycleExecutions+=Execute; Recoveries+=Recover; CycleRecoveries+=Recover;
        LastTelegraphs=NutBoss->LabTelegraphs; LastExecutions=NutBoss->LabExecutions; LastRecoveries=NutBoss->LabRecoveries;
        bSawDeath|=NutBoss->State==EMCNutBossState::Defeated;
        if(bActionStarted) MaximumTravel=FMath::Max(MaximumTravel,float(FVector::Dist(NutBoss->GetActorLocation(),BossStart)));
    }
    if(IsValid(Boss)) {
        const auto State=Boss->Runtime.State;
        if(uint8(State)!=LastBossState) {
            LastBossState=uint8(State);
            if(State==EMCBossState::Telegraph) {++Telegraphs;++CycleTelegraphs;}
            else if(State==EMCBossState::Attacking) {++Executions;++CycleExecutions;}
            else if(State==EMCBossState::Recovering) {++Recoveries;++CycleRecoveries;}
        }
        bSawDeath|=State==EMCBossState::Dead;
        bSawUlcer|=Boss->MouthAttack->GetActiveUlcerCount()>0;
    }
}

void AMCVFXLabBossStation::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdateLabel();
    if(!HasAuthority() || !GetWorld()->IsGameWorld()) return;
    if(bComparisonMode) {
        // Entrance, native attack timers and presentation keep progressing while
        // the comparison owner decides when to strike or prepare the next case.
        if(bCycleOpen) Observe();
        if(!bRunning || !bActionStarted || !bCycleOpen) return;
    } else if(!bRunning) return;
    const double Now=GetWorld()->GetTimeSeconds();
    if(!bCycleOpen) {
        if(Now<NextCycleAt) return;
        if(!BeginCycle()) {FinishCycle(false,Status);return;}
    }
    Observe();
    if(bActionStarted && Kind==EMCVFXLabBossKind::TankCounterShove && IsValid(NutBoss)) {
        bSawCounter|=NutBoss->IsCounterShoving();
        if(!bSawCounter && Now>=NextCounterContactAt && CounterContacts<32 && NutBoss->CanReceiveToolHit()) {
            ++CounterContacts;NextCounterContactAt=Now+.18;
            NutBoss->ReceiveToolDamage(1.f,Subject);
            bSawCounter|=NutBoss->IsCounterShoving();
        }
    }
    if(Now-(bComparisonMode?ActionStartedAt:CycleStartedAt)>40) {FinishCycle(false,TEXT("Timeout: expected live callback/effect was not observed"));return;}
    if(!bActionStarted) {
        if(IsNutKind()) {
            if(!IsValid(NutBoss)) {FinishCycle(false,TEXT("Nut disappeared before action"));return;}
            if(NutBoss->State==EMCNutBossState::Falling) return;
            if(IsEntranceKind()) {
                const bool Impact=SeenCues.Contains(uint8(EMCNutCombatCue::EntranceImpact));
                FinishCycle(bSawEntrance && NutBoss->State==EMCNutBossState::Idle && Impact,TEXT("Observed falling, landing and entrance impact"));return;
            }
        }
        if(!LaunchAction()) {FinishCycle(false,TEXT("Real action was rejected or expected damage differs"));return;}
        Observe();
    }
    const double Age=Now-ActionStartedAt;
    if(Kind==EMCVFXLabBossKind::TankHeadSplit && IsValid(NutBoss) && Age>AMCNutBoss::MagePhaseSeconds
        && NutBoss->BossRole==EMCNutBossRole::Mage && NutBoss->State==EMCNutBossState::Idle) {
        FinishCycle(SeenCues.Contains(uint8(EMCNutCombatCue::KnightHeadSplit)),TEXT("Real lethal contact, native head split and Mage second phase"));return;
    }
    if(IsKamikazeKind() && IsValid(Kamikaze) && Kamikaze->bExploded) {
        FinishCycle(bSawFireImpact,TEXT("Native fuse expired and real blast resolved"));return;
    }
    if(Kind==EMCVFXLabBossKind::RainEntry && IsValid(NutOwner)) {
        for(const auto& Shadow:NutOwner->LandingShadows)if(IsValid(Shadow.Get()) && Shadow->bImpacted) {
            FinishCycle(true,TEXT("Real falling food, warning and collision impact"));return;
        }
    }
    if(IsDeathKind() && Age>=1.2) {
        const bool Burst=!IsNutKind() || SeenCues.Contains(uint8(EMCNutCombatCue::DeathBurst));
        FinishCycle(bSawDeath && Burst,TEXT("Observed actual defeat/death callback"));return;
    }
    if((Kind==EMCVFXLabBossKind::MageHurt || Kind==EMCVFXLabBossKind::ZombieHurt) && Age>=1.2) {
        FinishCycle(bSawHurt && AppliedHealthDamage>0,TEXT("Observed accepted damage and hurt timestamp"));return;
    }
    if((Kind==EMCVFXLabBossKind::TankShieldHit || Kind==EMCVFXLabBossKind::TankShieldBreakOverflow) && Age>=1) {
        const bool Cue=SeenCues.Contains(uint8(EMCNutCombatCue::ShieldHit));
        if(Kind==EMCVFXLabBossKind::TankShieldBreakOverflow) {
            const float BeforeFollowup=NutBoss->Health;
            const float Followup=NutBoss->ReceiveToolDamage(25.f,Subject);
            const auto* Unshielded=NutBoss->BossSettings.TankUnshieldedSkeletalMesh.Get();
            const bool LostShield=NutBoss->ShieldHealth<=0 && !NutBoss->IsShieldProtectingFrom(Subject->GetActorLocation())
                && (!Unshielded || NutBoss->TankModel->GetSkeletalMeshAsset()==Unshielded);
            FinishCycle(Cue && FMath::IsNearlyZero(AppliedHealthDamage,.1f) && LostShield && FMath::IsNearlyEqual(Followup,25.f,.1f)
                && FMath::IsNearlyEqual(BeforeFollowup-NutBoss->Health,25.f,.1f),
                TEXT("Shield 500: raw600 broke shield with zero HP overflow; next hit removed full25 HP"));
        } else FinishCycle(Cue && FMath::IsNearlyZero(AppliedHealthDamage,.1f) && NutBoss->ShieldHealth<ShieldBefore
            && FMath::IsNearlyEqual(BossHealthBefore,NutBoss->Health,.1f),
            TEXT("Shield 500: raw75 removed75 durability and preserved boss HP"));
        return;
    }
    if(Kind==EMCVFXLabBossKind::ZombieRoar && IsValid(Boss)) {
        const UMCBossProfile* Profile=Boss->GetResolvedProfile();
        const UAnimSequence* Clip=Profile?Profile->RoarAnimation.Get():nullptr;
        if(Clip && Age>=Clip->GetPlayLength()) {
            FinishCycle(Boss->Runtime.AnimationPreview==EMCBossAnimationPreview::Roar,TEXT("Authored roar presentation completed; no combat callback"));return;
        }
    }
    if(IsCombatKind() && IsNutKind() && IsValid(NutBoss)
        && NutBoss->State==EMCNutBossState::Idle && CycleRecoveries>0) {
        bool Evidence=CycleTelegraphs>0 && CycleExecutions>0;
        if(Kind==EMCVFXLabBossKind::MageFireball) Evidence&=bSawFireImpact;
        else if(Kind==EMCVFXLabBossKind::MageSummon) Evidence&=GeneratedCreeps>0;
        else if(Kind==EMCVFXLabBossKind::MageNutRain) Evidence&=NutBoss->RainDropsResolved>=NutBoss->BossSettings.RainDrops;
        else if(Kind==EMCVFXLabBossKind::TankJump) Evidence&=SeenCues.Contains(uint8(EMCNutCombatCue::SlamImpact));
        else if(Kind==EMCVFXLabBossKind::TankCounterShove) Evidence&=bSawCounter && SeenCues.Contains(uint8(EMCNutCombatCue::SlamImpact));
        else if(Kind==EMCVFXLabBossKind::TankRoll || Kind==EMCVFXLabBossKind::TankCharge) Evidence&=MaximumTravel>60;
        else if(Kind==EMCVFXLabBossKind::MageTeleport) Evidence&=MaximumTravel>200 && SeenCues.Contains(uint8(EMCNutCombatCue::TeleportBurst));
        else Evidence&=SeenCues.Contains(uint8(EMCNutCombatCue::MeleeSlash));
        FinishCycle(Evidence,TEXT("Observed real telegraph, execution, effect and full recovery"));return;
    }
    if(IsCombatKind() && !IsNutKind() && IsValid(Boss) && CycleRecoveries>0
        && (Boss->Runtime.State==EMCBossState::Chasing || Boss->Runtime.State==EMCBossState::Searching)) {
        bool Evidence=CycleTelegraphs>0 && CycleExecutions>0;
        if(Kind==EMCVFXLabBossKind::Phase3AreaAttack) Evidence&=bSawWind && bSawClot && bSawClotImpact && bSawUlcer;
        else Evidence&=IsValid(Subject) && Subject->Status->State.Health<SubjectHealthBefore;
        FinishCycle(Evidence,Kind==EMCVFXLabBossKind::Phase3AreaAttack?
            TEXT("Observed real wind, clots, collision impacts, tongue ulcer and recovery"):
            TEXT("Observed real melee damage, attack phases and recovery"));return;
    }
    if(IsValid(NutBoss)) Status=FString::Printf(TEXT("Real nut phase %s"),*StaticEnum<EMCNutBossState>()->GetNameStringByValue(int64(NutBoss->State)));
    else if(IsValid(Boss)) Status=FString::Printf(TEXT("Real boss phase %s"),*StaticEnum<EMCBossState>()->GetNameStringByValue(int64(Boss->Runtime.State)));
}

void AMCVFXLabBossStation::FinishCycle(bool bPassed,const FString& Reason)
{
    ++Cycles;
    if(bPassed) ++Passed; else ++Failed;
    Status=FString(bPassed?TEXT("Observed: "):TEXT("FAILED: "))+Reason;
    if(!bPassed) Status+=FString::Printf(TEXT(" | tell/execute/recover %d/%d/%d; creeps %d; fire %d; wind/clot/impact/ulcer %d/%d/%d/%d; travel %.0f"),
        CycleTelegraphs,CycleExecutions,CycleRecoveries,GeneratedCreeps,bSawFireImpact,bSawWind,bSawClot,bSawClotImpact,bSawUlcer,MaximumTravel);
    if(bComparisonMode) bRunning=false;
    else {
        bCycleOpen=false;
        NextCycleAt=GetWorld()->GetTimeSeconds()+FMath::Max(.5f,CycleSeconds);
    }
    ForceNetUpdate(); UpdateLabel();
}

void AMCVFXLabBossStation::Cleanup()
{
    if(GetWorld() && HasAuthority()) {
        GatherOwnedActors();
        if(IsValid(Boss)) Boss->ResetForRun();
        if(IsValid(NutOwner)) NutOwner->Stop();
        for(const auto& Weak:OwnedActors) if(AActor* Actor=Weak.Get(); IsValid(Actor) && !Actor->IsActorBeingDestroyed()) {
            if(bComparisonMode) GetWorldTimerManager().ClearAllTimersForObject(Actor);
            Actor->Destroy();
        }
    }
    OwnedActors.Reset(); ObservedActors.Reset(); SeenCues.Reset();
    NutBoss=nullptr; Boss=nullptr; NutOwner=nullptr; Kamikaze=nullptr;EntryNut=nullptr;PreparedCreepMesh=nullptr;Subject=nullptr; SubjectController=nullptr;
    if(EntryPresentation) {EntryPresentation->StopPresentation(true);EntryPresentation->StopLocalSourceStates(TEXT("Event.NutRain.Entry"));}
    bBorrowedSubject=false;
    bCycleOpen=false; bActionStarted=false; bSawEntrance=false; bSawDeath=false; bSawHurt=false;
    bSawWind=false; bSawClot=false; bSawClotImpact=false; bSawUlcer=false; bSawFireImpact=false;
    LastTelegraphs=LastExecutions=LastRecoveries=0;
    CycleTelegraphs=CycleExecutions=CycleRecoveries=GeneratedCreeps=0;
    LastBossState=255; MaximumTravel=0; AppliedHealthDamage=0;
    ComparisonOutgoingHealth=ComparisonOutgoingShield=ComparisonIncomingHealth=0;
    NextCounterContactAt=0;CounterContacts=0;bSawCounter=false;
}

void AMCVFXLabBossStation::Reset()
{
    if(!HasAuthority() || !GetWorld() || !GetWorld()->IsGameWorld()) return;
    if(bComparisonMode) {
        Cycles=Passed=Failed=Telegraphs=Executions=Recoveries=Impacts=0;
        if(bBorrowedSubject)PrepareComparisonUsingSubject(Kind,Subject);
        else PrepareComparison(Kind);
        return;
    }
    Cleanup(); Cycles=Passed=Failed=Telegraphs=Executions=Recoveries=Impacts=0;
    NextCycleAt=GetWorld()->GetTimeSeconds()+.1;
    Status=bRunning?TEXT("Reset; waiting to spawn"):TEXT("Reset; paused"); ForceNetUpdate(); UpdateLabel();
}

void AMCVFXLabBossStation::SetRunning(bool bEnabled)
{
#if !UE_BUILD_SHIPPING
    if(!HasAuthority() || !HasActorBegunPlay() || !GetWorld() || !GetWorld()->IsGameWorld() || bRunning==bEnabled) return;
    bRunning=bEnabled;
    if(bComparisonMode) {
        // Pause scheduling and observation completion, retaining the fixtures.
        // An already accepted native ability may finish its normal recovery.
        SetActorTickEnabled(true);
        ForceNetUpdate(); UpdateLabel();
        return;
    }
    SetActorTickEnabled(bRunning);
    if(!bRunning) {Cleanup();Status=TEXT("Paused; owned actors cleared");}
    else {NextCycleAt=GetWorld()->GetTimeSeconds()+.1;Status=TEXT("Waiting to spawn real boss");}
    ForceNetUpdate(); UpdateLabel();
#endif
}

void AMCVFXLabBossStation::OnRep_Status()
{
    UpdateLabel();
}

void AMCVFXLabBossStation::UpdateLabel()
{
    if(Label) Label->SetText(FText::FromString(FString::Printf(TEXT("%s\nCycles %d | observed %d | failed %d\nTell %d | execute %d | recover %d | impacts %d\n%s"),
        *KindName(),Cycles,Passed,Failed,Telegraphs,Executions,Recoveries,Impacts,*Status)));
}

void AMCVFXLabBossStation::EndPlay(const EEndPlayReason::Type Reason)
{
    bRunning=false; SetActorTickEnabled(false); Cleanup(); Super::EndPlay(Reason);
}

void AMCVFXLabBossStation::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCVFXLabBossStation,bRunning); DOREPLIFETIME(AMCVFXLabBossStation,Cycles);
    DOREPLIFETIME(AMCVFXLabBossStation,Passed); DOREPLIFETIME(AMCVFXLabBossStation,Failed);
    DOREPLIFETIME(AMCVFXLabBossStation,Telegraphs); DOREPLIFETIME(AMCVFXLabBossStation,Executions);
    DOREPLIFETIME(AMCVFXLabBossStation,Recoveries); DOREPLIFETIME(AMCVFXLabBossStation,Impacts);
    DOREPLIFETIME(AMCVFXLabBossStation,Status);
}
