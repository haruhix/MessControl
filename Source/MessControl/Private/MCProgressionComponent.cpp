#include "MCProgressionComponent.h"
#include "MCPlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "TimerManager.h"
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
    ExperienceToNextLevel=GetExperienceToNextLevel();
    GetWorld()->GetTimerManager().SetTimer(SynchronizeTimer,this,&UMCProgressionComponent::SynchronizePlayers,.25f,true);
    SynchronizePlayers();
}

void UMCProgressionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(SynchronizeTimer);
    Super::EndPlay(Reason);
}

int32 UMCProgressionComponent::GetExperienceToNextLevel() const
{
    if (TeamLevel>=FMath::Clamp(MaxTeamLevel,2,1000)) return 0;
    const int64 Required=int64(FMath::Max(1,FirstLevelExperience))
        +int64(FMath::Max(0,TeamLevel-1))*FMath::Max(0,ExperienceGrowthPerLevel);
    return int32(FMath::Min<int64>(Required,MAX_int32));
}

int32 UMCProgressionComponent::AddExperience(int32 Amount)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || Amount<=0) return 0;
    ExperienceInLevel=FMath::Min<int64>(ExperienceInLevel,MAX_int64-Amount)+Amount;
    TotalExperience=FMath::Min<int64>(TotalExperience,MAX_int64-Amount)+Amount;
    const int32 PreviousLevel=TeamLevel;
    while (const int32 Required=GetExperienceToNextLevel())
    {
        if (ExperienceInLevel<Required) break;
        ExperienceInLevel-=Required;
        ++TeamLevel;
    }
    ExperienceToNextLevel=GetExperienceToNextLevel();
    SynchronizePlayers();
    GetOwner()->ForceNetUpdate();
    return TeamLevel-PreviousLevel;
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
    Player->QueueChoicesThroughLevel(TeamLevel);
    Player->RefreshLevelUpOffer();
    // Playtest bots have no local card widget. Claim one personal card per
    // synchronization pass; the timer drains further earned levels gradually.
    if (Player->IsABot() && Player->LevelUpOffer.IsValid())
        Player->TryChooseLevelUpPerk(Player->LevelUpOffer.OfferId,0);
}

void UMCProgressionComponent::SynchronizePlayers()
{
    if (auto* State=Cast<AGameStateBase>(GetOwner()))
        for (const auto& Base:State->PlayerArray) SynchronizePlayer(Cast<AMCPlayerState>(Base.Get()));
}

bool UMCProgressionComponent::HasPendingChoices() const
{
    if (const auto* State=Cast<AGameStateBase>(GetOwner()))
        for (const auto& Base:State->PlayerArray)
            if (const auto* Player=Cast<AMCPlayerState>(Base.Get()); Player && !Player->IsSpectator()
                && !Player->IsOnlyASpectator() && Player->HasPendingLevelChoices()) return true;
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
