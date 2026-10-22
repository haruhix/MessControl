#include "MCProgressionComponent.h"
#include "MCPlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UMCProgressionComponent::UMCProgressionComponent()
{
    PrimaryComponentTick.bCanEverTick=false;
    SetIsReplicatedByDefault(true);
}

void UMCProgressionComponent::BeginPlay()
{
    Super::BeginPlay();
    if (!GetOwner()->HasAuthority()) return;
    // Keep the saved component/class for Blueprint compatibility, but there is
    // no experience economy. Only world chests can offer rewards.
    TeamLevel=1; ExperienceInLevel=0; TotalExperience=0; ExperienceToNextLevel=0;
    SynchronizePlayers();
}

int32 UMCProgressionComponent::GetExperienceToNextLevel() const
{
    return 0;
}

int32 UMCProgressionComponent::AddExperience(int32 Amount)
{
    // Legacy Blueprint callers are intentionally harmless; tasks cannot award
    // XP, queue cards or pause the run, even with old saved defaults.
    return 0;
}

void UMCProgressionComponent::ResetProgression()
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;
    TeamLevel=1; ExperienceInLevel=0; TotalExperience=0;
    ExperienceToNextLevel=GetExperienceToNextLevel();
    if (auto* State=Cast<AGameStateBase>(GetOwner()))
        for (const auto& Base:State->PlayerArray) if (auto* Player=Cast<AMCPlayerState>(Base.Get())) Player->ResetLevelUpChoices();
    GetOwner()->ForceNetUpdate();
}

void UMCProgressionComponent::SynchronizePlayer(AMCPlayerState* Player)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !IsValid(Player)
        || Player->GetWorld()!=GetWorld() || Player->IsSpectator() || Player->IsOnlyASpectator()) return;
    // Discard stale level offers without resetting chest reward history/perks.
    Player->RefreshLevelUpOffer();
}

void UMCProgressionComponent::SynchronizePlayers()
{
    if (auto* State=Cast<AGameStateBase>(GetOwner()))
        for (const auto& Base:State->PlayerArray) SynchronizePlayer(Cast<AMCPlayerState>(Base.Get()));
}

bool UMCProgressionComponent::HasPendingChoices() const
{
    return false;
}

void UMCProgressionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UMCProgressionComponent,TeamLevel);
    DOREPLIFETIME(UMCProgressionComponent,ExperienceInLevel);
    DOREPLIFETIME(UMCProgressionComponent,TotalExperience);
    DOREPLIFETIME(UMCProgressionComponent,ExperienceToNextLevel);
}
