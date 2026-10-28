#include "MCResultPassives.h"
#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCArenaTooth.h"
#include "MCMouthSurface.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"

namespace
{
double ResultNow(const UWorld* World)
{ return World->GetGameState()?World->GetGameState()->GetServerWorldTimeSeconds():World->GetTimeSeconds(); }
bool LiveContributor(const AMCToothCharacter* Worker,const UWorld* World)
{
    const auto* State=IsValid(Worker)?Worker->GetPlayerState<AMCPlayerState>():nullptr;
    return Worker && Worker->HasAuthority() && Worker->GetWorld()==World && Worker->Status
        && Worker->Status->IsAlive() && State && State->GetPawn()==Worker && State->Perks;
}
bool CleanableTarget(const AActor* Target)
{
    if(!IsValid(Target) || Target->IsActorBeingDestroyed()) return false;
    if(const auto* Surface=Cast<AMCMouthSurface>(Target)) return !Surface->bUlcer;
    if(const auto* Tooth=Cast<AMCArenaTooth>(Target)) return Tooth->IsAvailable() && Tooth->Status;
    return false;
}
float Parameter(const UMCPerkComponent* Perks,FName ID,FName Key,float Default,float Low,float High)
{
    const float Value=Perks->PerkParameter(ID,Key,Default);
    return FMath::IsFinite(Value)?FMath::Clamp(Value,Low,High):Default;
}
}

void UMCResultPassives::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    SpawnHandle=GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this,&UMCResultPassives::RegisterTarget));
}
void UMCResultPassives::Deinitialize()
{
    if(GetWorld() && SpawnHandle.IsValid()) GetWorld()->RemoveOnActorSpawnedHandler(SpawnHandle);
    Targets.Reset();PendingAttempts.Reset();
    Super::Deinitialize();
}
bool UMCResultPassives::ShouldCreateSubsystem(UObject* Outer) const
{
    const auto* World=Cast<UWorld>(Outer);
    return Super::ShouldCreateSubsystem(Outer) && World && (World->WorldType==EWorldType::Game || World->WorldType==EWorldType::PIE);
}
UMCResultPassives* UMCResultPassives::ForWorld(const UObject* Context)
{
    auto* World=Context?Context->GetWorld():nullptr;
    return World && World->GetNetMode()!=NM_Client?World->GetSubsystem<UMCResultPassives>():nullptr;
}
void UMCResultPassives::RegisterTarget(AActor* Target)
{
    // Deferred spawns may not have initialized availability/ulcer flags yet.
    if(IsValid(Target) && (Target->IsA<AMCMouthSurface>() || Target->IsA<AMCArenaTooth>())) Targets.Add(Target);
}
void UMCResultPassives::EnsureTargets()
{
    if(!bCollectedExistingTargets) {
        bCollectedExistingTargets=true;
        for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) RegisterTarget(*It);
        for(TActorIterator<AMCArenaTooth> It(GetWorld());It;++It) RegisterTarget(*It);
    }
    for(auto It=Targets.CreateIterator();It;++It) if(!It->IsValid()) It.RemoveCurrent();
    for(auto It=PendingAttempts.CreateIterator();It;++It) if(!It.Key().IsValid()) It.RemoveCurrent();
}
bool UMCResultPassives::IsDirtyTarget(const AActor* Target)
{
    if(!CleanableTarget(Target)) return false;
    if(const auto* Surface=Cast<AMCMouthSurface>(Target)) return !Surface->IsClean();
    const auto* Tooth=CastChecked<AMCArenaTooth>(Target);
    return Tooth->Status->NeedsCare(true);
}
void UMCResultPassives::GetDirtyTargets(TArray<AActor*>& Out)
{
    EnsureTargets();Out.Reset();
    for(const auto& Target:Targets) if(IsDirtyTarget(Target.Get())) Out.Add(Target.Get());
}
bool UMCResultPassives::IsMouthClean()
{
    EnsureTargets();
    for(const auto& Target:Targets) if(IsDirtyTarget(Target.Get())) return false;
    return true;
}
float UMCResultPassives::CleaningRate(const AMCToothCharacter* Worker,float Remaining,float BaseRate)
{
    if(!FMath::IsFinite(BaseRate) || BaseRate<=0) return 0;
    float Rate=FMath::Clamp(BaseRate,0.f,16.f);
    const auto* Perks=UMCPerkComponent::ForActor(Worker);
    if(Perks && Perks->GetStacks(TEXT("B2_LastStroke"))>0 && FMath::IsFinite(Remaining) && Remaining>=0
        && Remaining<Parameter(Perks,TEXT("B2_LastStroke"),TEXT("Threshold"),.15f,0.f,1.f))
        Rate*=Parameter(Perks,TEXT("B2_LastStroke"),TEXT("Multiplier"),2.f,0.f,16.f);
    return FMath::Clamp(Rate,0.f,16.f);
}
FMCStainCleaningAttempt UMCResultPassives::BeginCleaning(AActor* Target)
{
    FMCStainCleaningAttempt Attempt;
    if(GetWorld()->GetNetMode()==NM_Client || !IsDirtyTarget(Target) || Target->GetWorld()!=GetWorld()) return Attempt;
    RegisterTarget(Target);Attempt.Target=Target;Attempt.Serial=++NextSerial;
    Attempt.bWasDirty=true;Attempt.bMouthWasDirty=!IsMouthClean();
    PendingAttempts.Add(Target,Attempt.Serial);
    return Attempt;
}
bool UMCResultPassives::CompleteCleaning(const FMCStainCleaningAttempt& Attempt,AMCToothCharacter* Worker,
    EMCStainCleanReason Reason,bool bActuallyRemoved)
{
    const uint64* Serial=PendingAttempts.Find(Attempt.Target);
    if(!Attempt.Serial || !Serial || *Serial!=Attempt.Serial) return false;
    PendingAttempts.Remove(Attempt.Target); // Seal before healing, task callbacks or neighbor cleanup.
    auto* Target=Attempt.Target.Get();
    if(GetWorld()->GetNetMode()==NM_Client || Reason==EMCStainCleanReason::Technical || !bActuallyRemoved
        || !Attempt.bWasDirty || !CleanableTarget(Target) || Target->GetWorld()!=GetWorld()
        || IsDirtyTarget(Target) || !LiveContributor(Worker,GetWorld())) return false;
    RewardStain(Worker);
    if(Attempt.bMouthWasDirty && IsMouthClean()) RewardCleanMouth();
    if(Reason!=EMCStainCleanReason::NeatFreak) TryNeighbor(Target,Worker);
    return true;
}
void UMCResultPassives::RewardStain(AMCToothCharacter* Worker)
{
    auto* Perks=UMCPerkComponent::ForActor(Worker);
    if(Perks->GetStacks(TEXT("B2_CleanBreath"))>0)
        Worker->Status->Heal(Parameter(Perks,TEXT("B2_CleanBreath"),TEXT("Heal"),3.f,0.f,10000.f));
    if(Perks->GetStacks(TEXT("B2_CleanAcceleration"))>0) {
        Perks->CleanSpeedUntil=ResultNow(GetWorld())+Parameter(Perks,TEXT("B2_CleanAcceleration"),TEXT("Duration"),3.f,0.f,60.f);
        Perks->GetOwner()->ForceNetUpdate();
    }
}
void UMCResultPassives::RewardCleanMouth()
{
    const auto* Game=GetWorld()->GetGameState();if(!Game) return;
    for(const auto& Entry:Game->PlayerArray) {
        const auto* State=Cast<AMCPlayerState>(Entry.Get());
        auto* Hero=State?Cast<AMCToothCharacter>(State->GetPawn()):nullptr;
        auto* Perks=State?State->Perks.Get():nullptr;
        if(!LiveContributor(Hero,GetWorld()) || !Perks || Perks->GetStacks(TEXT("B2_RefreshingWork"))<=0) continue;
        Hero->Status->Heal(Hero->Status->State.MaxHealth
            *Parameter(Perks,TEXT("B2_RefreshingWork"),TEXT("HealFraction"),.2f,0.f,1.f));
    }
}
void UMCResultPassives::TryNeighbor(AActor* Finished,AMCToothCharacter* Worker)
{
    const auto* Perks=UMCPerkComponent::ForActor(Worker);
    if(Perks->GetStacks(TEXT("B2_NeatFreak"))<=0
        || FMath::FRand()>=Parameter(Perks,TEXT("B2_NeatFreak"),TEXT("Chance"),.2f,0.f,1.f)) return;
    const float Radius=Parameter(Perks,TEXT("B2_NeatFreak"),TEXT("Radius"),300.f,0.f,3000.f);
    AActor* Best=nullptr;double BestDistance=FMath::Square(double(Radius));
    TArray<AActor*> Dirty;GetDirtyTargets(Dirty);
    for(auto* Candidate:Dirty) {
        if(Candidate==Finished) continue;
        const double Distance=FVector::DistSquared(Finished->GetActorLocation(),Candidate->GetActorLocation());
        if(Distance>BestDistance || (Distance==BestDistance && Best && Candidate->GetUniqueID()>=Best->GetUniqueID())) continue;
        Best=Candidate;BestDistance=Distance;
    }
    if(auto* Surface=Cast<AMCMouthSurface>(Best)) Surface->CleanLiquidFromPassive(Worker);
    else if(auto* Tooth=Cast<AMCArenaTooth>(Best)) Tooth->CleanGrimeFromPassive(Worker);
}
