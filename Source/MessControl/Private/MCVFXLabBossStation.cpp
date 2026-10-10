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
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
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
        if(bAutoRun && !AMCVFXLab::Find(GetWorld())) {
            SetRunning(true);
            NextCycleAt=GetWorld()->GetTimeSeconds()+FMath::Max(.1f,StartDelaySeconds);
        }
    }
#endif
    UpdateLabel();
}

bool AMCVFXLabBossStation::IsNutKind() const { return uint8(Kind)<=uint8(EMCVFXLabBossKind::MageEntrance); }
bool AMCVFXLabBossStation::IsMageKind() const
{ return uint8(Kind)>=uint8(EMCVFXLabBossKind::MageMelee) && IsNutKind(); }
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
        || Kind==EMCVFXLabBossKind::ZombiePunchRight || Kind==EMCVFXLabBossKind::ZombieKick
        || Kind==EMCVFXLabBossKind::Phase3PunchRight || Kind==EMCVFXLabBossKind::Phase3AreaAttack;
}

FString AMCVFXLabBossStation::KindName() const
{
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
        if(Actor==this || Actor==NutOwner || Actor==NutBoss || Actor==Boss || Actor==Subject || Actor==SubjectController) return true;
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

bool AMCVFXLabBossStation::BeginCycle()
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
    if(!Floor->InteriorSurfacePoint(Ground.ImpactPoint+Forward*(Ranged?650.f:260.f),45,SubjectGround)) {
        Status=TEXT("Subject footprint is outside Floor");return false;
    }
    FActorSpawnParameters Spawn;
    Spawn.Owner=this;
    Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SubjectController=GetWorld()->SpawnActor<AMCPlaytestBotController>(AMCPlaytestBotController::StaticClass(),GetActorLocation(),Facing,Spawn);
    if(!SubjectController) {Status=TEXT("Subject controller spawn failed");return false;}
    Track(SubjectController);
    UClass* ToothClass=SubjectClass.LoadSynchronous();
    if(!ToothClass) {Status=TEXT("Saved tooth Blueprint is missing");return false;}
    const auto* ToothDefaults=ToothClass->GetDefaultObject<AMCToothCharacter>();
    const float SubjectHeight=ToothDefaults->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()+4;
    Subject=GetWorld()->SpawnActor<AMCToothCharacter>(ToothClass,SubjectGround.ImpactPoint+FVector(0,0,SubjectHeight),Facing,Spawn);
    if(!Subject) {Status=TEXT("Subject pawn spawn failed");return false;}
    Track(Subject);
    SubjectController->Possess(Subject);
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
    if(IsNutKind()) {
        const UMCNutRainProfile* Data=NutProfile.LoadSynchronous();
        if(!Data) {Status=TEXT("Saved DA_NutRain is missing");return false;}
        FMCNutBossSettings Settings=Data->Settings.Boss; Settings.Sanitize();
        NutOwner=GetWorld()->SpawnActor<AMCNutRainEvent>(AMCNutRainEvent::StaticClass(),GetActorLocation(),Facing,Spawn);
        if(!NutOwner) {Status=TEXT("Nut owner spawn failed");return false;}
        Track(NutOwner); NutOwner->Settings=Data->Settings; NutOwner->SetActorTickEnabled(false);
        const EMCNutBossRole BossRole=IsMageKind()?EMCNutBossRole::Mage:EMCNutBossRole::Tank;
        BossStart=Ground.ImpactPoint+FVector(0,0,Settings.BodyRadiusForRole(BossRole)+4);
        const FTransform Pose(Facing,BossStart);
        NutBoss=GetWorld()->SpawnActorDeferred<AMCNutBoss>(AMCNutBoss::StaticClass(),Pose,NutOwner,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!NutBoss) {Status=TEXT("Nut boss spawn failed");return false;}
        Track(NutBoss);
        NutBoss->LabStation=this; NutBoss->LabTarget=Subject; NutBoss->Target=Subject;
        NutBoss->ConfigureEncounter(Floor,NutOwner,BossRole,Settings,1,GetUniqueID()+Cycles*137);
        FTransform AirbornePose=Pose; AirbornePose.SetLocation(NutBoss->GetActorLocation());
        NutBoss->FinishSpawning(AirbornePose);
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
        Track(Boss); Boss->Profile=Profile; Boss->Tags.AddUnique(TEXT("MC_DevBoss")); Boss->FinishSpawning(Pose);
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

bool AMCVFXLabBossStation::LaunchAction()
{
    ActionStartedAt=GetWorld()->GetTimeSeconds(); bActionStarted=true;
    MaximumTravel=0;
    if(IsNutKind()) {
        if(!IsValid(NutBoss) || !NutBoss->IsEncounterAlive()) return false;
        NutBoss->Target=Subject;
        BossHealthBefore=NutBoss->Health; ShieldBefore=NutBoss->ShieldHealth;
        if(IsDeathKind()) {
            AppliedHealthDamage=NutBoss->ReceiveToolDamage(NutBoss->Health+1,nullptr);
            bSawDeath=NutBoss->State==EMCNutBossState::Defeated;
            return bSawDeath && AppliedHealthDamage>0;
        }
        if(Kind==EMCVFXLabBossKind::MageHurt) {
            AppliedHealthDamage=NutBoss->ReceiveToolDamage(25,Subject);
            bSawHurt=AppliedHealthDamage>0 && NutBoss->VisualHitAt>=ActionStartedAt;
            return bSawHurt;
        }
        if(Kind==EMCVFXLabBossKind::TankShieldHit || Kind==EMCVFXLabBossKind::TankShieldBreakOverflow) {
            if(!FMath::IsNearlyEqual(ShieldBefore,500.f,.1f) || !NutBoss->IsShieldProtectingFrom(Subject->GetActorLocation())) return false;
            const float Raw=Kind==EMCVFXLabBossKind::TankShieldHit?75.f:ShieldBefore+100.f;
            AppliedHealthDamage=NutBoss->ReceiveToolDamage(Raw,Subject);
            const float Expected=FMath::Min(Raw,ShieldBefore)*NutBoss->BossSettings.ShieldFrontDamageScale+FMath::Max(0.f,Raw-ShieldBefore);
            return FMath::IsNearlyEqual(BossHealthBefore-NutBoss->Health,Expected,.1f)
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
    if(!HasAuthority() || !bRunning || !GetWorld()->IsGameWorld()) return;
    const double Now=GetWorld()->GetTimeSeconds();
    if(!bCycleOpen) {
        if(Now<NextCycleAt) return;
        if(!BeginCycle()) {FinishCycle(false,Status);return;}
    }
    Observe();
    if(Now-CycleStartedAt>40) {FinishCycle(false,TEXT("Timeout: expected live callback/effect was not observed"));return;}
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
    if(IsDeathKind() && Age>=1.2) {
        const bool Burst=!IsNutKind() || SeenCues.Contains(uint8(EMCNutCombatCue::DeathBurst));
        FinishCycle(bSawDeath && Burst,TEXT("Observed actual defeat/death callback"));return;
    }
    if((Kind==EMCVFXLabBossKind::MageHurt || Kind==EMCVFXLabBossKind::ZombieHurt) && Age>=1.2) {
        FinishCycle(bSawHurt && AppliedHealthDamage>0,TEXT("Observed accepted damage and hurt timestamp"));return;
    }
    if((Kind==EMCVFXLabBossKind::TankShieldHit || Kind==EMCVFXLabBossKind::TankShieldBreakOverflow) && Age>=1) {
        const bool Cue=SeenCues.Contains(uint8(EMCNutCombatCue::ShieldHit));
        FinishCycle(Cue && AppliedHealthDamage>0,Kind==EMCVFXLabBossKind::TankShieldHit?
            TEXT("Shield 500: raw75 removed75 durability, protected health damage observed"):
            TEXT("Shield 500: raw600 broke shield; reduced500 + full100 overflow observed"));return;
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
    bCycleOpen=false;
    NextCycleAt=GetWorld()->GetTimeSeconds()+FMath::Max(.5f,CycleSeconds);
    ForceNetUpdate(); UpdateLabel();
}

void AMCVFXLabBossStation::Cleanup()
{
    if(GetWorld() && HasAuthority()) {
        GatherOwnedActors();
        if(IsValid(Boss)) Boss->ResetForRun();
        if(IsValid(NutOwner)) NutOwner->Stop();
        for(const auto& Weak:OwnedActors) if(AActor* Actor=Weak.Get(); IsValid(Actor) && !Actor->IsActorBeingDestroyed()) Actor->Destroy();
    }
    OwnedActors.Reset(); ObservedActors.Reset(); SeenCues.Reset();
    NutBoss=nullptr; Boss=nullptr; NutOwner=nullptr; Subject=nullptr; SubjectController=nullptr;
    bCycleOpen=false; bActionStarted=false; bSawEntrance=false; bSawDeath=false; bSawHurt=false;
    bSawWind=false; bSawClot=false; bSawClotImpact=false; bSawUlcer=false; bSawFireImpact=false;
    LastTelegraphs=LastExecutions=LastRecoveries=0;
    CycleTelegraphs=CycleExecutions=CycleRecoveries=GeneratedCreeps=0;
    LastBossState=255; MaximumTravel=0; AppliedHealthDamage=0;
}

void AMCVFXLabBossStation::Reset()
{
    if(!HasAuthority() || !GetWorld() || !GetWorld()->IsGameWorld()) return;
    Cleanup(); Cycles=Passed=Failed=Telegraphs=Executions=Recoveries=Impacts=0;
    NextCycleAt=GetWorld()->GetTimeSeconds()+.1;
    Status=bRunning?TEXT("Reset; waiting to spawn"):TEXT("Reset; paused"); ForceNetUpdate(); UpdateLabel();
}

void AMCVFXLabBossStation::SetRunning(bool bEnabled)
{
#if !UE_BUILD_SHIPPING
    if(!HasAuthority() || !HasActorBegunPlay() || !GetWorld() || !GetWorld()->IsGameWorld() || bRunning==bEnabled) return;
    bRunning=bEnabled;
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
