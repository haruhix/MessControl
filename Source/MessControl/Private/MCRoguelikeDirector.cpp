#include "MCRoguelikeDirector.h"
#include "MCRewardDropZone.h"
#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

AMCRoguelikeDirector::AMCRoguelikeDirector()
{
    bReplicates=true; bAlwaysRelevant=true; PrimaryActorTick.bCanEverTick=false;
    ChestClass=AMCRewardChest::StaticClass();
    PerkTable=TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Gameplay/Roguelike/DT_Perks.DT_Perks")));
}

void AMCRoguelikeDirector::CacheZones()
{
    Zones.Reset();
    for(TActorIterator<AMCRewardDropZone> It(GetWorld());It;++It) if (It->bAllowRewardDrops) Zones.Add(*It);
    Zones.Sort([](const TWeakObjectPtr<AMCRewardDropZone>& A,const TWeakObjectPtr<AMCRewardDropZone>& B) {
        return A->GetPathName()<B->GetPathName();
    });
}

void AMCRoguelikeDirector::BeginPlay()
{
    Super::BeginPlay(); if(!HasAuthority()) return;
    LoadedTable=PerkTable.LoadSynchronous(); CacheZones();
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    Random.Initialize(State?State->RunSeed:0);
}

void AMCRoguelikeDirector::NotifyTaskCompleted(FName CompletionId)
{
    if(!HasAuthority() || bResetting) return;
    if(!CompletionId.IsNone()) {
        if(CompletedIds.Contains(CompletionId)) return;
        CompletedIds.Add(CompletionId);
    }
    if(PendingRewards==MAX_int32) { UE_LOG(LogTemp,Error,TEXT("Reward queue exceeded int32 capacity")); return; }
    ++PendingRewards; ForceNetUpdate();
    if(!GetWorldTimerManager().IsTimerActive(RetryTimer))
        GetWorldTimerManager().SetTimer(RetryTimer,this,&AMCRoguelikeDirector::TrySpawnReward,1.f,true);
    TrySpawnReward();
}

void AMCRoguelikeDirector::TrySpawnReward()
{
    if(!HasAuthority() || bResetting) return;
    ActiveChests.RemoveAll([](const TObjectPtr<AMCRewardChest>& Chest) { return !IsValid(Chest); });
    if(PendingRewards<=0) { GetWorldTimerManager().ClearTimer(RetryTimer); return; }
    if(ActiveChests.Num()>=FMath::Clamp(MaxActiveChests,1,8) || GetWorld()->GetTimeSeconds()<NextRewardAt) return;
    if(!ChestClass || !AMCRewardChest::HasValidLoot(LoadedTable)) {
        if(!bWarnedInvalidTable) {
            UE_LOG(LogTemp,Warning,TEXT("MC_REWARD_WAIT: need a valid DT_Perks with >=3 usable rows of one polarity"));
            bWarnedInvalidTable=true;
        }
        return;
    }
    TArray<AMCToothCharacter*> Players;
    bool Eligible=false;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) {
        auto* PS=It->GetPlayerState<AMCPlayerState>();
        if(!It->Status || !It->Status->IsAlive() || It->SwallowedBy || !PS || PS->GetPawn()!=*It) continue;
        Players.Add(*It);
        Eligible|=PS->Perks && PS->Perks->GetPerkTable()==LoadedTable && AMCRewardChest::HasValidLoot(LoadedTable,PS->Perks);
    }
    if(!Eligible) {
        if(FParse::Param(FCommandLine::Get(),TEXT("MCRoguelikePreview"))) {
            static double LastReport=-100.;
            if(GetWorld()->GetTimeSeconds()-LastReport>=10.) {
                LastReport=GetWorld()->GetTimeSeconds();
                for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) {
                    const auto* PS=It->GetPlayerState<AMCPlayerState>();
                    UE_LOG(LogTemp,Display,TEXT("MC_REWARD_ELIGIBILITY pawn=%s alive=%d swallowed=%d state=%s statePawn=%s perksTable=%s directorTable=%s"),
                        *It->GetName(),It->Status && It->Status->IsAlive(),bool(It->SwallowedBy),*GetNameSafe(PS),*GetNameSafe(PS?PS->GetPawn():nullptr),
                        *GetNameSafe(PS && PS->Perks?PS->Perks->GetPerkTable():nullptr),*GetNameSafe(LoadedTable));
                }
            }
        }
        return; // Keep capped/dead players' rewards queued instead of dropping unusable chests.
    }
    if(Zones.IsEmpty()) CacheZones();
    Zones.RemoveAll([](const TWeakObjectPtr<AMCRewardDropZone>& Zone) { return !Zone.IsValid(); });
    const auto* Defaults=ChestClass.GetDefaultObject();
    const FVector Extent=Defaults->GetPlacementHalfExtent();
    TArray<AActor*> Ignored; // Existing chests are blockers, not ignored placement obstacles.
    AMCRewardDropZone* BestZone=nullptr; FVector Best=FVector::ZeroVector,BestNormal=FVector::UpVector;
    int32 BestDensity=MAX_int32; float BestTie=-1;
    const float Height=FMath::IsFinite(FallHeight)?FMath::Clamp(FallHeight,50.f,1500.f):450.f;
    for(const auto& Weak:Zones) if(auto* Zone=Weak.Get()) for(int32 I=0;I<FMath::Clamp(CandidatesPerZone,1,64);++I) {
        FVector Point,Normal;
        if(!Zone->FindLanding(Random,Extent,Height,Ignored,Point,Normal)) continue;
        int32 Density=0;
        const float Radius=FMath::IsFinite(Zone->PlayerDensityRadius)?FMath::Max(100.f,Zone->PlayerDensityRadius):700.f;
        for(auto* Player:Players) Density+=FVector::DistSquared2D(Point,Player->GetActorLocation())<FMath::Square(Radius);
        const float Tie=Random.FRand();
        if(Density<BestDensity || (Density==BestDensity && Tie>BestTie)) { BestDensity=Density; BestTie=Tie; Best=Point; BestNormal=Normal; BestZone=Zone; }
    }
    if(!BestZone) return;
    const FVector Inward=(-Best).GetSafeNormal2D();
    const FRotator Facing(0,Inward.IsNearlyZero()?0:Inward.Rotation().Yaw+90,0);
    const FRotator Rotation=FRotationMatrix::MakeFromXZ(Facing.Vector(),BestNormal).Rotator();
    const FTransform Transform(Rotation,Best);
    auto* Chest=GetWorld()->SpawnActorDeferred<AMCRewardChest>(ChestClass,Transform,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Chest) return;
    Chest->InitializeReward(Best,Best+FVector(0,0,Height),int32(Random.GetUnsignedInt()),LoadedTable,SelectionPolicy,BestZone);
    ActiveChests.Add(Chest); --PendingRewards; ++RewardsSpawned;
    NextRewardAt=GetWorld()->GetTimeSeconds()+(FMath::IsFinite(RewardCooldown)?FMath::Max(0.f,RewardCooldown):8.f);
    Chest->FinishSpawning(Transform);
    ForceNetUpdate();
    UE_LOG(LogTemp,Display,TEXT("MC_REWARD_SPAWN point=%s density=%d pending=%d"),*Best.ToString(),BestDensity,PendingRewards);
}

void AMCRoguelikeDirector::NotifyChestFinished(AMCRewardChest* Chest,bool bRequeue)
{
    if(!HasAuthority() || !ActiveChests.Remove(Chest)) return;
    if(bRequeue && !bResetting && PendingRewards<MAX_int32) ++PendingRewards;
    ForceNetUpdate();
    if(PendingRewards>0 && !bResetting && !GetWorldTimerManager().IsTimerActive(RetryTimer))
        GetWorldTimerManager().SetTimer(RetryTimer,this,&AMCRoguelikeDirector::TrySpawnReward,1.f,true);
}

void AMCRoguelikeDirector::ResetRewards()
{
    if(!HasAuthority()) return;
    bResetting=true; GetWorldTimerManager().ClearTimer(RetryTimer);
    const auto Existing=ActiveChests;
    for(AMCRewardChest* Chest:Existing) if(IsValid(Chest)) Chest->Destroy();
    ActiveChests.Reset(); CompletedIds.Reset(); PendingRewards=0; RewardsSpawned=0; NextRewardAt=0;
    const auto* State=GetWorld()->GetGameState<AMCGameState>(); Random.Initialize(State?State->RunSeed:0);
    bResetting=false; ForceNetUpdate();
}

void AMCRoguelikeDirector::EndPlay(const EEndPlayReason::Type Reason)
{
    bResetting=true; GetWorldTimerManager().ClearAllTimersForObject(this); Super::EndPlay(Reason);
}

void AMCRoguelikeDirector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCRoguelikeDirector,PendingRewards); DOREPLIFETIME(AMCRoguelikeDirector,RewardsSpawned);
}
