#include "MCBossMouthAttackComponent.h"
#include "MCBossCharacter.h"
#include "MCBossClot.h"
#include "MCMouthSurface.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothMovementComponent.h"
#include "MCReactionVFX.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"

UMCBossMouthAttackComponent::UMCBossMouthAttackComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickGroup=TG_PostPhysics;
}
void UMCBossMouthAttackComponent::BeginPlay()
{
    Super::BeginPlay();Boss=Cast<AMCBossCharacter>(GetOwner());
    if(Boss.IsValid()) AddTickPrerequisiteComponent(Boss->GetMesh());
    // Autonomous players predict the same small wind drift from replicated attack timestamps.
    SetComponentTickEnabled(true);
}
void UMCBossMouthAttackComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    StopAttack(true);Super::EndPlay(Reason);
}
FVector UMCBossMouthAttackComponent::GetMouthLocation() const
{
    const auto* Owner=Boss.IsValid()?Boss.Get():Cast<AMCBossCharacter>(GetOwner());
    if(!Owner) return GetOwner()->GetActorLocation();
    const FVector Jaw=Owner->GetMesh()->GetSkeletalMeshAsset() && Owner->GetMesh()->DoesSocketExist(MouthBone)?Owner->GetMesh()->GetSocketLocation(MouthBone):Owner->GetActorLocation()+FVector(0,0,95);
    return Jaw+Owner->GetActorTransform().TransformVectorNoScale(MouthOffset);
}
int32 UMCBossMouthAttackComponent::GetActiveUlcerCount() const
{
    int32 Count=0;for(const auto& Ulcer:Ulcers) if(Ulcer.IsValid() && !Ulcer->IsHealed()) ++Count;return Count;
}
bool UMCBossMouthAttackComponent::IsSalvoValid(int32 Serial) const
{
    return GetOwner()->HasAuthority() && Boss.IsValid() && !Boss->IsActorBeingDestroyed() && Boss->IsBossAlive()
        && Boss->Runtime.State!=EMCBossState::Dormant && Boss->Runtime.AnimationPreview==EMCBossAnimationPreview::None && Serial==SalvoSerial;
}
void UMCBossMouthAttackComponent::StartAttack(const FMCBossAttackDefinition& Attack)
{
    if(!GetOwner()->HasAuthority() || !Boss.IsValid() || !Attack.bMouthClotAttack) return;
    StopAttack();Definition=Attack;Definition.Sanitize();SalvoSerial=Boss->Runtime.AttackSerial;
    StartedAt=GetWorld()->GetTimeSeconds();Emitted=0;bEmitting=true;
    Wind=AMCReactionVFX::Spawn(GetWorld(),GetMouthLocation(),EMCReactionEffect::BossWind,Definition.ActiveSeconds,Definition.Range*.70f,Boss->Runtime.AttackForward,Definition.Range);
    if(Wind.IsValid()) {Wind->SetOwner(Boss.Get());Wind->AttachToComponent(Boss->GetMesh(),FAttachmentTransformRules::KeepWorldTransform,MouthBone);}
    EmitClot(Emitted++);
}
void UMCBossMouthAttackComponent::StopAttack(bool bClearUlcers)
{
    bEmitting=false;SalvoSerial=INDEX_NONE;DamageDealt.Reset();
    ClearWind();
    if(Wind.IsValid()) Wind->Destroy();Wind.Reset();
    for(const auto& Clot:Clots) if(Clot.IsValid()) Clot->Destroy();Clots.Reset();
    if(bClearUlcers) {for(const auto& Ulcer:Ulcers) if(Ulcer.IsValid()) Ulcer->Destroy();Ulcers.Reset();}
}
void UMCBossMouthAttackComponent::EmitClot(int32 Index)
{
    FRandomStream Random(HashCombine(uint32(SalvoSerial),uint32(Index+731)));
    const FVector Forward=Boss->Runtime.AttackForward.GetSafeNormal2D();
    const float Angle=Random.FRandRange(-Definition.HalfAngleDegrees,Definition.HalfAngleDegrees);
    FVector Landing=Boss->GetActorLocation()+Forward.RotateAngleAxis(Angle,FVector::UpVector)*Random.FRandRange(170.f,Definition.Range);
    if(Index%5==0 && AMCBossCharacter::IsLivingPlayer(Boss->Runtime.Target))
        Landing=Boss->Runtime.Target->GetActorLocation()+FVector(Random.FRandRange(-22,22),Random.FRandRange(-22,22),-15);
    else
    {
        for(TActorIterator<AMCTongue> It(GetWorld());It;++It)
        {
            FHitResult Floor;if(It->SurfacePoint(Landing,Floor)) {Landing=Floor.ImpactPoint+Floor.ImpactNormal*8;break;}
        }
    }
    const FVector Origin=GetMouthLocation();
    const float Flight=FMath::Clamp(FVector::Dist2D(Origin,Landing)/850.f,.42f,1.1f);
    if(auto* Clot=AMCBossClot::Spawn(this,SalvoSerial,Origin,Landing,Flight,Random.FRandRange(10,16))) Clots.Add(Clot);
}
void UMCBossMouthAttackComponent::ResolveClotHit(int32 Serial,const FHitResult& Hit)
{
    if(!IsSalvoValid(Serial)) return;
    if(auto* Player=Cast<AMCToothCharacter>(Hit.GetActor()))
    {
        if(!AMCBossCharacter::IsLivingPlayer(Player)) return;
        float& Dealt=DamageDealt.FindOrAdd(Player);
        const float Damage=FMath::Min(Definition.ClotDamage,FMath::Max(0.f,Definition.Damage-Dealt));
        if(Player->Status->Damage(Damage,(Player->GetActorLocation()-GetMouthLocation()).GetSafeNormal())) Dealt+=Damage;
        return;
    }
    // Only an actual tongue collision can produce a lesion; walls/teeth/props cannot.
    if(!Cast<AMCTongue>(Hit.GetActor()) || Hit.ImpactNormal.Z<.35f) return;
    Ulcers.RemoveAll([](const auto& P){return !P.IsValid() || P->IsHealed();});
    // Global cap for this boss ability, including multiple bosses and repeat salvos.
    int32 Active=0;for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It)
        if(It->bUlcer && !It->IsHealed()) {
            if(FVector::DistSquared(It->GetActorLocation(),Hit.ImpactPoint)<FMath::Square(85.f)) return;
            if(It->ActorHasTag(TEXT("MC_BossClotUlcer"))) ++Active;
        }
    if(Active>=2) return;
    auto* Patch=AMCMouthSurface::SpawnDamageUlcer(GetWorld(),Hit.ImpactPoint);
    // SpawnDamageUlcer can return an existing nearby ulcer. Never adopt another system's patch.
    if(Patch && !Patch->GetOwner())
    {
        Patch->SetOwner(Boss.Get());Patch->Tags.AddUnique(TEXT("MC_BossClotUlcer"));Ulcers.AddUnique(Patch);
    }
}
void UMCBossMouthAttackComponent::PushWind(float Dt)
{
    const FVector Origin=GetMouthLocation(),Direction=Boss->Runtime.AttackForward.GetSafeNormal2D();
    TSet<TWeakObjectPtr<AMCToothCharacter>> Affected;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        auto* Player=*It;if(!AMCBossCharacter::IsLivingPlayer(Player) || Player->IsYawning()) continue;
        if(!GetOwner()->HasAuthority() && !Player->IsLocallyControlled()) continue;
        const FVector Offset=Player->GetActorLocation()-Origin;
        const float Distance=Offset.Size2D();const FVector Away=Offset.GetSafeNormal2D();
        if(Distance>Definition.Range || FMath::Abs(Offset.Z)>280.f
            || FVector::DotProduct(Away,Direction)<FMath::Cos(FMath::DegreesToRadians(Definition.HalfAngleDegrees))) continue;
        FHitResult Block;FCollisionQueryParams Params(SCENE_QUERY_STAT(MCBossWind),false,Boss.Get());Params.AddIgnoredActor(Player);
        if(GetWorld()->LineTraceSingleByChannel(Block,Origin,Player->GetActorLocation(),ECC_Visibility,Params)) continue;
        auto* Movement=Player->GetCharacterMovement();
        if(!Player->ToothPhysics->CanAct() || Player->ClingTooth || (!Movement->IsMovingOnGround() && !Movement->IsFalling())) continue;
        // Additive root motion is applied after ground braking, retaining the player's own input.
        // Unlike a tiny impulse, it also moves a stationary character with strong braking.
        const float Strength=1.f-.65f*FMath::Clamp(Distance/Definition.Range,0.f,1.f);
        const FVector Drift=Away*FMath::Min(55.f,Definition.WindPushAcceleration*.18f)*Strength;
        Affected.Add(Player);
        auto* SourceId=WindSources.Find(Player);
        TSharedPtr<FRootMotionSource> Current=SourceId?Movement->GetRootMotionSourceByID(*SourceId):nullptr;
        if(Current.IsValid() && !Current->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval))
            static_cast<FMCLocomotionRootMotionSource*>(Current.Get())->Force=Drift;
        else {
            auto Source=MakeShared<FMCLocomotionRootMotionSource>();
            Source->InstanceName=FName(*FString::Printf(TEXT("MCBossWind_%s"),*Boss->GetFName().ToString()));
            Source->Priority=105;Source->AccumulateMode=ERootMotionAccumulateMode::Additive;
            Source->Duration=-1;Source->Force=Drift;Source->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
            WindSources.Add(Player,Movement->ApplyRootMotionSource(Source));
        }
    }
    for(auto It=WindSources.CreateIterator();It;++It) if(!Affected.Contains(It.Key())) {
        if(It.Key().IsValid()) It.Key()->GetCharacterMovement()->RemoveRootMotionSourceByID(It.Value());It.RemoveCurrent();
    }
}
void UMCBossMouthAttackComponent::ClearWind()
{
    for(const auto& Pair:WindSources) if(Pair.Key.IsValid()) Pair.Key->GetCharacterMovement()->RemoveRootMotionSourceByID(Pair.Value);
    WindSources.Reset();
}
void UMCBossMouthAttackComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Dt,TickType,Tick);
    if(!GetOwner()->HasAuthority()) {
        const auto* Profile=Boss.IsValid()?Boss->GetResolvedProfile():nullptr;
        const auto* Attack=Profile?Profile->Attacks.FindByPredicate([this](const auto& A){return A.bMouthClotAttack && A.AttackId==Boss->Runtime.AttackId;}):nullptr;
        const auto* GS=GetWorld()->GetGameState();const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
        const double Age=Boss.IsValid()?Now-Boss->Runtime.AttackStartedAt:0;
        if(Attack && Boss->IsBossAlive() && Boss->Runtime.State==EMCBossState::Attacking && Age>=Attack->WindupSeconds && Age<Attack->WindupSeconds+Attack->ActiveSeconds) {
            Definition=*Attack;Definition.Sanitize();PushWind(Dt);
        } else ClearWind();
        return;
    }
    if(!bEmitting) return;
    if(!IsSalvoValid(SalvoSerial)) {StopAttack();return;}
    const float Age=FMath::Max(0.f,float(GetWorld()->GetTimeSeconds()-StartedAt));
    const float Interval=Definition.ActiveSeconds/FMath::Max(1,Definition.ClotCount-1);
    while(Emitted<Definition.ClotCount && Age>=Emitted*Interval) EmitClot(Emitted++);
    if(Age<Definition.ActiveSeconds) PushWind(Dt);
    else {bEmitting=false;ClearWind();if(Wind.IsValid()) Wind->Destroy();Wind.Reset();}
}
