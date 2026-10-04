#include "MCBossCharacter.h"
#include "MCBossAIController.h"
#include "MCBossFaceComponent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

AMCBossCharacter::AMCBossCharacter()
{
    bReplicates=true;
    SetReplicateMovement(true);
    SetNetUpdateFrequency(15.f);
    AIControllerClass=AMCBossAIController::StaticClass();
    AutoPossessAI=EAutoPossessAI::PlacedInWorldOrSpawned;
    GetCapsuleComponent()->InitCapsuleSize(60.f,110.f);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    BodyHitbox=CreateDefaultSubobject<UCapsuleComponent>(TEXT("BodyHitbox"));
    BodyHitbox->SetupAttachment(GetCapsuleComponent());
    BodyHitbox->InitCapsuleSize(85.f,100.f);
    BodyHitbox->SetRelativeLocation(FVector(0.f,0.f,-10.f));
    BodyHitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    BodyHitbox->SetCollisionResponseToAllChannels(ECR_Ignore);
    BodyHitbox->SetGenerateOverlapEvents(false);
    BodyHitbox->SetCanEverAffectNavigation(false);
    Face=CreateDefaultSubobject<UMCBossFaceComponent>(TEXT("BossFace"));
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    bUseControllerRotationYaw=false;
    GetCharacterMovement()->bOrientRotationToMovement=false;
    GetCharacterMovement()->bUseControllerDesiredRotation=true;
    GetCharacterMovement()->RotationRate=FRotator(0.f,540.f,0.f);
    GetCharacterMovement()->MaxWalkSpeed=BaseWalkSpeed;
    PrimaryActorTick.bCanEverTick=true;
}

void AMCBossCharacter::BeginPlay()
{
    Super::BeginPlay();
    OnRep_Profile();
    if (!HasAuthority()) { OnRep_Runtime(); return; }
    auto Safe=[](float Value,float Default,float Min,float Max)
    { return FMath::IsFinite(Value)?FMath::Clamp(Value,Min,Max):Default; };
    Runtime.MaxHealth=ResolvedProfile?Safe(ResolvedProfile->MaxHealth,600.f,1.f,1000000.f):600.f;
    Runtime.Health=Runtime.MaxHealth;
    BaseWalkSpeed=ResolvedProfile?Safe(ResolvedProfile->WalkSpeed,260.f,1.f,2000.f):260.f;
    GetCharacterMovement()->MaxWalkSpeed=BaseWalkSpeed;
    AttackDefinitions=ResolvedProfile?ResolvedProfile->Attacks:TArray<FMCBossAttackDefinition>{FMCBossAttackDefinition()};
    TSet<FName> AttackIds;
    for (int32 I=AttackDefinitions.Num()-1;I>=0;--I)
    {
        auto& Attack=AttackDefinitions[I];
        Attack.Sanitize();
        // Duplicate or empty IDs cannot address a reliable cooldown/ability slot.
        if (Attack.AttackId.IsNone() || AttackIds.Contains(Attack.AttackId)) AttackDefinitions.RemoveAt(I);
        else AttackIds.Add(Attack.AttackId);
    }
    PhaseDefinitions=ResolvedProfile?ResolvedProfile->Phases:TArray<FMCBossPhaseDefinition>();
    PhaseDefinitions.SetNum(FMath::Min(PhaseDefinitions.Num(),32));
    for (auto& Phase:PhaseDefinitions)
    {
        Phase.HealthFraction=Safe(Phase.HealthFraction,.5f,0.f,1.f);
        Phase.MovementMultiplier=Safe(Phase.MovementMultiplier,1.2f,.1f,4.f);
    }
    PhaseDefinitions.Sort([](const FMCBossPhaseDefinition& A,const FMCBossPhaseDefinition& B)
    { return A.HealthFraction>B.HealthFraction; });
    ChangeState(EMCBossState::Dormant);
    // No encounter is wired yet. An imported/placed boss must not wake from the normal game flow.
}

void AMCBossCharacter::OnRep_Profile()
{
    ResolvedProfile=Profile.LoadSynchronous();
    if (ResolvedProfile)
    {
        // Explicitly configured presentation only: no dependency on a particular zombie rig or animation file.
        if (!ResolvedProfile->SkeletalMesh.IsNull())
            if (auto* BossMesh=ResolvedProfile->SkeletalMesh.LoadSynchronous()) GetMesh()->SetSkeletalMeshAsset(BossMesh);
        if (!ResolvedProfile->AnimationClass.IsNull())
            if (auto* AnimClass=ResolvedProfile->AnimationClass.LoadSynchronous()) GetMesh()->SetAnimInstanceClass(AnimClass);
        GetMesh()->SetRelativeTransform(ResolvedProfile->MeshTransform);
        LoadedAnimations.Reset();
        auto KeepClip=[this](const TSoftObjectPtr<UAnimSequence>& Reference)
        {
            if (!Reference.IsNull()) if (UAnimSequence* Clip=Reference.LoadSynchronous())
                LoadedAnimations.AddUnique(Clip);
        };
        KeepClip(ResolvedProfile->IdleAnimation);
        KeepClip(ResolvedProfile->WalkAnimation);
        KeepClip(ResolvedProfile->HurtAnimation);
        KeepClip(ResolvedProfile->DeathAnimation);
        KeepClip(ResolvedProfile->RoarAnimation);
        for (const auto& Attack:ResolvedProfile->Attacks) KeepClip(Attack.Animation);
        CurrentAnimation=nullptr;
        UpdateAnimationPresentation();
    }
    Face->RefreshMorphTargets();
}

FVector AMCBossCharacter::GetMeleeTargetPoint(const FVector& Source) const
{
    if (!BodyHitbox) return GetActorLocation();
    const FVector Center=BodyHitbox->GetComponentLocation();
    const FVector Axis=BodyHitbox->GetUpVector();
    const float Radius=BodyHitbox->GetScaledCapsuleRadius();
    const float Segment=FMath::Max(0.f,BodyHitbox->GetScaledCapsuleHalfHeight()-Radius);
    const FVector Spine=Center+Axis*FMath::Clamp(FVector::DotProduct(Source-Center,Axis),-Segment,Segment);
    const FVector Offset=Source-Spine;
    // A source already inside the volume has zero distance to it.
    return Offset.SizeSquared()<=FMath::Square(Radius)?Source:Spine+Offset.GetSafeNormal()*Radius;
}

void AMCBossCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(AttackTimer);
    if (auto* Brain=Cast<AMCBossAIController>(GetController())) Brain->StopBossBrain();
    Super::EndPlay(EndPlayReason);
}

double AMCBossCharacter::ServerNow() const
{
    const auto* GameState=GetWorld()?GetWorld()->GetGameState():nullptr;
    return GameState?GameState->GetServerWorldTimeSeconds():(GetWorld()?GetWorld()->GetTimeSeconds():0.);
}

float AMCBossCharacter::GetStateAge() const
{
    return FMath::Max(0.f,float(ServerNow()-Runtime.StateStartedAt));
}

void AMCBossCharacter::ActivateBoss()
{
    if (!HasAuthority() || !IsBossAlive()) return;
    Runtime.AnimationPreview=EMCBossAnimationPreview::None;
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    if (!GetController()) SpawnDefaultController();
    if (Runtime.State==EMCBossState::Dormant) ChangeState(EMCBossState::Searching);
    if (auto* Brain=Cast<AMCBossAIController>(GetController())) Brain->StartBossBrain();
}

void AMCBossCharacter::DeactivateBoss()
{
    if (!HasAuthority() || !IsBossAlive()) return;
    GetWorldTimerManager().ClearTimer(AttackTimer);
    if (auto* Brain=Cast<AMCBossAIController>(GetController())) Brain->StopBossBrain();
    Runtime.Target=nullptr;
    Runtime.AttackId=NAME_None;
    Runtime.AnimationPreview=EMCBossAnimationPreview::None;
    Face->ClearRoarExpression();
    ChangeState(EMCBossState::Dormant);
}

void AMCBossCharacter::ResetForRun()
{
    if (!HasAuthority() || IsActorBeingDestroyed()) return;
    GetWorldTimerManager().ClearTimer(AttackTimer);
    if (AMCBossAIController* Brain=Cast<AMCBossAIController>(GetController())) Brain->StopBossBrain();
    NextAttackAt.Reset();
    PendingAttack=FMCBossAttackDefinition();
    Runtime.Health=Runtime.MaxHealth;
    Runtime.Phase=0;
    Runtime.Target=nullptr;
    Runtime.AttackId=NAME_None;
    Runtime.AnimationPreview=EMCBossAnimationPreview::None;
    Runtime.HurtStartedAt=-1000;
    Face->ClearRoarExpression();
    Runtime.AttackForward=GetActorForwardVector();
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    BodyHitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    GetCharacterMovement()->MaxWalkSpeed=BaseWalkSpeed;
    ChangeState(EMCBossState::Dormant);
}

float AMCBossCharacter::TakeDamage(float DamageAmount,const FDamageEvent& DamageEvent,AController* EventInstigator,AActor* DamageCauser)
{
    if (!HasAuthority() || !IsBossAlive() || !FMath::IsFinite(DamageAmount) || DamageAmount<=0.f) return 0.f;
    const float Accepted=Super::TakeDamage(DamageAmount,DamageEvent,EventInstigator,DamageCauser);
    return ReceiveBossDamage(Accepted,DamageCauser);
}

float AMCBossCharacter::ReceiveBossDamage(float Damage,AActor* DamageCauser)
{
    if (!HasAuthority() || IsActorBeingDestroyed() || !CanReceiveWeaponHit()
        || !FMath::IsFinite(Damage) || Damage<=0.f) return 0.f;
    const float Applied=FMath::Min(Runtime.Health,Damage);
    Runtime.Health-=Applied;
    if (Runtime.Health<=0.f)
    {
        GetWorldTimerManager().ClearTimer(AttackTimer);
        if (auto* Brain=Cast<AMCBossAIController>(GetController())) Brain->StopBossBrain();
        GetCharacterMovement()->DisableMovement();
        GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        BodyHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Runtime.Target=nullptr;
        ChangeState(EMCBossState::Dead);
    }
    else
    {
        UpdatePhase();
        Runtime.HurtStartedAt=ServerNow();
        PublishRuntime();
    }
    return Applied;
}

void AMCBossCharacter::UpdatePhase()
{
    int32 NewPhase=0;
    const float Fraction=Runtime.Health/FMath::Max(1.f,Runtime.MaxHealth);
    for (const auto& Phase:PhaseDefinitions) if (Fraction<=Phase.HealthFraction) ++NewPhase;
    Runtime.Phase=FMath::Max(Runtime.Phase,NewPhase);
    GetCharacterMovement()->MaxWalkSpeed=BaseWalkSpeed*(Runtime.Phase>0?PhaseDefinitions[Runtime.Phase-1].MovementMultiplier:1.f);
}

bool AMCBossCharacter::IsLivingPlayer(const AMCToothCharacter* Target)
{
    return IsValid(Target) && IsValid(Cast<APlayerController>(Target->GetController()))
        && IsValid(Target->Status) && Target->Status->IsAlive() && !Target->SwallowedBy;
}

bool AMCBossCharacter::CanSeePlayer(const AMCToothCharacter* Target) const
{
    if (!IsLivingPlayer(Target)) return false;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MCBossSight),false,this);
    Params.AddIgnoredActor(Target);
    return !GetWorld()->LineTraceSingleByChannel(Hit,GetPawnViewLocation(),Target->GetPawnViewLocation(),ECC_Visibility,Params);
}

bool AMCBossCharacter::IsPlayerInAttack(const AMCToothCharacter* Target,const FMCBossAttackDefinition& Attack,const FVector& Forward) const
{
    if (!IsLivingPlayer(Target)) return false;
    const FVector Offset=Target->GetActorLocation()-GetActorLocation();
    return Offset.SizeSquared2D()<=FMath::Square(Attack.Range) && FMath::Abs(Offset.Z)<=Attack.VerticalReach
        && (Offset.IsNearlyZero() || FVector::DotProduct(Forward.GetSafeNormal2D(),Offset.GetSafeNormal2D())>=FMath::Cos(FMath::DegreesToRadians(Attack.HalfAngleDegrees)))
        && CanSeePlayer(Target);
}

bool AMCBossCharacter::IsAttackReady(const FMCBossAttackDefinition& Attack) const
{
    const auto* Next=NextAttackAt.Find(Attack.AttackId);
    return HasAuthority() && IsBossAlive() && Runtime.State!=EMCBossState::Dormant
        && Runtime.State!=EMCBossState::Telegraph && Runtime.State!=EMCBossState::Attacking && Runtime.State!=EMCBossState::Recovering
        && Runtime.Phase>=Attack.MinimumPhase && Attack.SelectionWeight>0.f && (!Next || ServerNow()>=*Next);
}

bool AMCBossCharacter::BeginAttack(FName AttackId,AMCToothCharacter* Target)
{
    const auto* Definition=AttackDefinitions.FindByPredicate([AttackId](const FMCBossAttackDefinition& Attack){return Attack.AttackId==AttackId;});
    if (!Definition || !IsAttackReady(*Definition) || !IsPlayerInAttack(Target,*Definition,Target?Target->GetActorLocation()-GetActorLocation():FVector::ZeroVector)) return false;
    PendingAttack=*Definition;
    if (auto* Brain=Cast<AMCBossAIController>(GetController()))
    {
        Brain->StopMovement();
        Brain->ClearFocus(EAIFocusPriority::Gameplay);
    }
    GetCharacterMovement()->StopMovementImmediately();
    Runtime.Target=Target;
    Runtime.AttackId=AttackId;
    Runtime.AttackStartedAt=ServerNow();
    ++Runtime.AttackSerial;
    Runtime.AttackForward=(Target->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
    if (Runtime.AttackForward.IsNearlyZero()) Runtime.AttackForward=GetActorForwardVector();
    SetActorRotation(Runtime.AttackForward.Rotation());
    if (auto* Brain=Cast<AMCBossAIController>(GetController())) Brain->SetControlRotation(Runtime.AttackForward.Rotation());
    NextAttackAt.Add(AttackId,ServerNow()+PendingAttack.WindupSeconds+PendingAttack.ActiveSeconds+PendingAttack.RecoverySeconds+PendingAttack.CooldownSeconds);
    ChangeState(EMCBossState::Telegraph,PendingAttack.WindupSeconds);
    GetWorldTimerManager().SetTimer(AttackTimer,this,&AMCBossCharacter::ImpactAttack,PendingAttack.WindupSeconds,false);
    return true;
}

void AMCBossCharacter::ImpactAttack()
{
    if (!HasAuthority() || !IsBossAlive() || Runtime.State!=EMCBossState::Telegraph) return;
    ChangeState(EMCBossState::Attacking,PendingAttack.ActiveSeconds);
    MulticastAttackImpact(Runtime.AttackId,Runtime.AttackSerial);
    ExecuteAttack(PendingAttack,Runtime.Target);
    // Custom abilities may kill, deactivate or destroy their owner during ExecuteAttack.
    if (!IsActorBeingDestroyed() && IsBossAlive() && Runtime.State==EMCBossState::Attacking)
        GetWorldTimerManager().SetTimer(AttackTimer,this,&AMCBossCharacter::RecoverAttack,PendingAttack.ActiveSeconds,false);
}

void AMCBossCharacter::ExecuteAttack_Implementation(const FMCBossAttackDefinition& Attack,AMCToothCharacter* Target)
{
    if (!HasAuthority() || !IsBossAlive() || Runtime.State!=EMCBossState::Attacking) return;
    // A single impact damages each eligible player once; no actor scan, collision tick or persistent damage volume.
    for (auto It=GetWorld()->GetPlayerControllerIterator();It;++It)
    {
        const auto* PC=It->Get();
        auto* Player=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
        if (IsPlayerInAttack(Player,Attack,Runtime.AttackForward)) Player->Status->Damage(Attack.Damage,Runtime.AttackForward);
    }
}

void AMCBossCharacter::RecoverAttack()
{
    if (!HasAuthority() || !IsBossAlive() || Runtime.State!=EMCBossState::Attacking) return;
    ChangeState(EMCBossState::Recovering,PendingAttack.RecoverySeconds);
    GetWorldTimerManager().SetTimer(AttackTimer,this,&AMCBossCharacter::FinishRecovery,PendingAttack.RecoverySeconds,false);
}

void AMCBossCharacter::FinishRecovery()
{
    if (!HasAuthority() || !IsBossAlive() || Runtime.State!=EMCBossState::Recovering) return;
    Runtime.AttackId=NAME_None;
    if (!IsLivingPlayer(Runtime.Target)) Runtime.Target=nullptr;
    ChangeState(Runtime.Target?EMCBossState::Chasing:EMCBossState::Searching);
}

void AMCBossCharacter::SetBrainState(EMCBossState State,AMCToothCharacter* Target)
{
    if (!HasAuthority() || !IsBossAlive() || Runtime.State==EMCBossState::Dormant || Runtime.State==EMCBossState::Telegraph
        || Runtime.State==EMCBossState::Attacking || Runtime.State==EMCBossState::Recovering) return;
    if (State!=EMCBossState::Chasing && State!=EMCBossState::Searching) return;
    if (Runtime.State==State && Runtime.Target==Target) return;
    Runtime.Target=Target;
    ChangeState(State);
}

void AMCBossCharacter::ChangeState(EMCBossState State,float Duration)
{
    Runtime.State=State;
    Runtime.StateStartedAt=ServerNow();
    Runtime.StateEndsAt=Duration>0.f?Runtime.StateStartedAt+Duration:0.;
    PublishRuntime();
}

void AMCBossCharacter::PublishRuntime()
{
    OnRep_Runtime();
    ForceNetUpdate();
}

void AMCBossCharacter::OnRep_Runtime()
{
    if (LastPresented.State==EMCBossState::Dead && Runtime.State!=EMCBossState::Dead)
    {
        GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        BodyHitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    }
    if (Runtime.Health!=LastPresented.Health || Runtime.MaxHealth!=LastPresented.MaxHealth) OnBossHealthChanged(Runtime.Health,Runtime.MaxHealth);
    if (Runtime.Phase!=LastPresented.Phase) OnBossPhaseChanged(Runtime.Phase);
    if (Runtime.State!=LastPresented.State || Runtime.StateStartedAt!=LastPresented.StateStartedAt || Runtime.Target!=LastPresented.Target)
        OnBossStateChanged(Runtime);
    if (Runtime.State==EMCBossState::Telegraph && (LastPresented.State!=EMCBossState::Telegraph || Runtime.AttackSerial!=LastPresented.AttackSerial))
        OnBossTelegraph(Runtime.AttackId,Runtime.AttackSerial,Runtime.AttackForward,Runtime.StateStartedAt,Runtime.StateEndsAt);
    if (Runtime.State==EMCBossState::Dead && LastPresented.State!=EMCBossState::Dead)
    {
        GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        BodyHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        OnBossDied();
    }
    LastPresented=Runtime;
    UpdateAnimationPresentation();
}

void AMCBossCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (GetNetMode()!=NM_DedicatedServer) UpdateAnimationPresentation();
}

UAnimSequence* AMCBossCharacter::PreviewSequence(EMCBossAnimationPreview Preview) const
{
    if (!ResolvedProfile) return nullptr;
    switch (Preview)
    {
    case EMCBossAnimationPreview::Idle: return ResolvedProfile->IdleAnimation.Get();
    case EMCBossAnimationPreview::Walk: return ResolvedProfile->WalkAnimation.Get();
    case EMCBossAnimationPreview::Hurt: return ResolvedProfile->HurtAnimation.Get();
    case EMCBossAnimationPreview::Death: return ResolvedProfile->DeathAnimation.Get();
    case EMCBossAnimationPreview::Roar: return ResolvedProfile->RoarAnimation.Get();
    default: break;
    }
    const FName Id=Preview==EMCBossAnimationPreview::PunchLeft?FName(TEXT("PunchLeft")):
        Preview==EMCBossAnimationPreview::PunchRight?FName(TEXT("PunchRight")):Preview==EMCBossAnimationPreview::Kick?FName(TEXT("Kick")):FName(NAME_None);
    for (const auto& Attack:ResolvedProfile->Attacks) if (Attack.AttackId==Id) return Attack.Animation.Get();
    return nullptr;
}

bool AMCBossCharacter::PreviewAnimation(EMCBossAnimationPreview Preview)
{
#if UE_BUILD_SHIPPING
    return false;
#else
    if (!HasAuthority() || !ActorHasTag(TEXT("MC_DevBoss")) || !PreviewSequence(Preview)) return false;
    ResetForRun();
    GetCharacterMovement()->DisableMovement();
    Runtime.AnimationPreview=Preview;
    Runtime.PreviewStartedAt=ServerNow();
    ++Runtime.PreviewSerial;
    PublishRuntime();
    return true;
#endif
}

void AMCBossCharacter::UpdateAnimationPresentation()
{
    if (!ResolvedProfile || !ResolvedProfile->AnimationClass.IsNull() || GetNetMode()==NM_DedicatedServer) return;
    const double Now=ServerNow();
    UAnimSequence* Clip=nullptr;
    double StartedAt=Runtime.StateStartedAt;
    float Rate=1.f;
    bool bLoop=false;
    if (Runtime.AnimationPreview!=EMCBossAnimationPreview::None)
    {
        Clip=PreviewSequence(Runtime.AnimationPreview);
        StartedAt=Runtime.PreviewStartedAt;
        bLoop=Runtime.AnimationPreview==EMCBossAnimationPreview::Idle || Runtime.AnimationPreview==EMCBossAnimationPreview::Walk;
    }
    else if (Runtime.State==EMCBossState::Dead) Clip=ResolvedProfile->DeathAnimation.Get();
    else if (Runtime.State==EMCBossState::Telegraph || Runtime.State==EMCBossState::Attacking || Runtime.State==EMCBossState::Recovering)
    {
        for (const auto& Attack:ResolvedProfile->Attacks) if (Attack.AttackId==Runtime.AttackId)
        {
            Clip=Attack.Animation.Get();
            // The complete clip retains its phase through Telegraph -> Attacking -> Recovering.
            StartedAt=Runtime.AttackStartedAt;
            if (Clip) Rate=Clip->GetPlayLength()/FMath::Max(.15f,Attack.WindupSeconds+Attack.ActiveSeconds+Attack.RecoverySeconds);
            break;
        }
    }
    else if (UAnimSequence* Hurt=ResolvedProfile->HurtAnimation.Get(); Hurt && Now-Runtime.HurtStartedAt<Hurt->GetPlayLength())
    {
        Clip=Hurt;
        StartedAt=Runtime.HurtStartedAt;
    }
    else
    {
        const float Speed=GetVelocity().Size2D();
        Clip=Speed>12.f?ResolvedProfile->WalkAnimation.Get():ResolvedProfile->IdleAnimation.Get();
        bLoop=true;
        if (Speed>12.f) Rate=FMath::Clamp(Speed/115.f,.5f,2.5f);
        StartedAt=Clip==CurrentAnimation?CurrentAnimationStartedAt:Now;
    }
    if (!Clip || !GetMesh()->GetSkeletalMeshAsset() || Clip->GetSkeleton()!=GetMesh()->GetSkeletalMeshAsset()->GetSkeleton()) return;
    if (Clip!=CurrentAnimation || StartedAt!=CurrentAnimationStartedAt)
    {
        CurrentAnimation=Clip;
        CurrentAnimationStartedAt=StartedAt;
        GetMesh()->PlayAnimation(Clip,bLoop);
    }
    if (UAnimSingleNodeInstance* Instance=GetMesh()->GetSingleNodeInstance())
    {
        const float Length=Clip->GetPlayLength();
        float Position=FMath::Max(0.f,float(Now-StartedAt))*Rate;
        Position=bLoop && Length>0.f?FMath::Fmod(Position,Length):FMath::Min(Position,Length);
        // Presentation follows the replicated server timestamps. Notifies never own combat damage.
        Instance->SetPlaying(false);
        Instance->SetPosition(Position,false);
    }
}

void AMCBossCharacter::MulticastAttackImpact_Implementation(FName AttackId,int32 AttackSerial)
{
    OnBossAttackImpact(AttackId,AttackSerial);
}

void AMCBossCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCBossCharacter,Profile);
    DOREPLIFETIME(AMCBossCharacter,Runtime);
}
