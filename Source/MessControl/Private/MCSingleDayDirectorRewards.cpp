#include "MCSingleDayDirector.h"
#include "MCGameMode.h"
#include "MCRewardChest.h"
#include "MCRewardDropZone.h"
#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "MCGameState.h"
#include "MCDayDirector.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "Components/BoxComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"

bool AMCSingleDayDirector::BeginStageReward(FName CompletionId,EMCSingleDayRewardContinuation Continuation)
{
    if(!HasAuthority() || bStopped || IsLegacyTimedFinale() || CompletionId.IsNone()
        || CompletedStageRewards.Contains(CompletionId)) return false;
    if(bStageRewardPending) return true;
    bStageRewardPending=true; StageRewardCompletionId=CompletionId; StageRewardContinuation=Continuation;
    // The shared work is finished now. Placement/choice retries are cosmetics
    // and perk flow, not extra coin objectives or a legacy chest queue signal.
    if(auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>()) Mode->AwardObjectiveCoinsOnce(CompletionId);
    StageRewardPlacementAttempts=0; NextStageRewardAttemptAt=0;
    Record(FString::Printf(TEXT("Этап %s завершён: каждый игрок выбирает свой бустер; следующая подача ждёт выбора"),*CompletionId.ToString()));
    Publish(TEXT("ВЫБЕРИ СВОЙ БУСТЕР"),TEXT("Откройте сундук в центре языка. Каждый игрок выбирает один бустер для себя."));
    TrySpawnStageReward(); ForceNetUpdate(); return true;
}

void AMCSingleDayDirector::TrySpawnStageReward()
{
    if(!HasAuthority() || bStopped || !bStageRewardPending || IsValid(StageRewardChest)) return;
    const double Now=GetWorld()->GetTimeSeconds();
    if(Now<NextStageRewardAttemptAt) return;
    NextStageRewardAttemptAt=Now+1;
    ++StageRewardPlacementAttempts;
    const auto ReportWait=[this](const TCHAR* Reason) {
        if(StageRewardPlacementAttempts==1 || StageRewardPlacementAttempts%10==0)
            UE_LOG(LogTemp,Display,TEXT("MC_STAGE_REWARD_WAIT completion=%s reason=%s attempts=%d"),*StageRewardCompletionId.ToString(),Reason,StageRewardPlacementAttempts);
    };
    AMCTongue* Tongue=nullptr;
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It)
        if(!It->IsActorBeingDestroyed() && It->Surface && It->Surface->IsRegistered()) {Tongue=*It;break;}
    if(!Tongue) {ReportWait(TEXT("visible_tongue_not_ready"));return;}
    UDataTable* Table=nullptr;
    for(TActorIterator<AMCPlayerState> It(GetWorld());It;++It)
        if(!It->IsSpectator() && !It->IsOnlyASpectator() && It->Perks && It->Perks->GetPerkTable()) {Table=It->Perks->GetPerkTable();break;}
    if(!Table || Table->GetRowStruct()!=FMCPerkDefinition::StaticStruct()) {ReportWait(TEXT("team_perk_table_not_ready"));return;}
    for(FName ID:{FName(TEXT("Fallback_Mouthwash")),FName(TEXT("Fallback_BrushFury")),FName(TEXT("Fallback_Grenade"))}) {
        const auto* Row=Table->FindRow<FMCPerkDefinition>(ID,TEXT("Stage reward setup"),false);
        if(!Row || !Row->bConsumableBooster || Row->Polarity!=EMCPerkPolarity::Positive) {ReportWait(TEXT("three_booster_definitions_required"));return;}
    }
    // Use the actual visible tongue's middle, never the mouth actor origin,
    // roof, gums or throat. The native placement validator checks all nine
    // support samples and a clear fall corridor before releasing a chest.
    const FVector Center=Tongue->Surface->Bounds.Origin;
    if(!IsValid(StageRewardZone)) {
        FActorSpawnParameters Params; Params.Owner=this;
        StageRewardZone=GetWorld()->SpawnActor<AMCRewardDropZone>(Center,FRotator::ZeroRotator,Params);
        if(!StageRewardZone) return;
        StageRewardZone->AllowedLandingSurface=Tongue;
        StageRewardZone->bAllowRewardDrops=false;
        StageRewardZone->InwardMargin=30;
        StageRewardZone->Area->SetBoxExtent(FVector(340,340,FMath::Max(300.,Tongue->Surface->Bounds.BoxExtent.Z+100.)));
    }
    const auto* Game=GetWorld()->GetGameState<AMCGameState>();
    const int32 Seed=(Game?Game->RunSeed:41)^int32(GetTypeHash(StageRewardCompletionId));
    FRandomStream Random(Seed+(StageRewardPlacementAttempts-1)*7919);
    constexpr float FallHeight=450;
    UClass* RewardClass=StageRewardChestClass?StageRewardChestClass.Get():AMCRewardChest::StaticClass();
    if(RewardClass->HasAnyClassFlags(CLASS_Abstract|CLASS_Deprecated|CLASS_NewerVersionExists)) RewardClass=AMCRewardChest::StaticClass();
    const FVector Extent=RewardClass->GetDefaultObject<AMCRewardChest>()->GetPlacementHalfExtent();
    FVector Landing,Normal;
    // A stage reward is scripted and cannot hurt or displace players. Waiting
    // in its central arrival area must not postpone/requeue the finished stage.
    // Static geometry, food and other obstacles still use the full validator.
    TArray<AActor*> Ignored;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) Ignored.Add(*It);
    const float ZoneHeight=StageRewardZone->Area->GetUnscaledBoxExtent().Z;
    StageRewardZone->Area->SetBoxExtent(FVector(340,340,ZoneHeight));
    bool Found=false;
    for(int32 I=0;I<24 && !Found;++I)
        Found=StageRewardZone->FindLanding(Random,Extent,FallHeight,Ignored,Landing,Normal);
    if(!Found) {
        // Grounded workers can dent the small middle of the living tongue.
        // Search nearby supported tissue in this same completion frame, with
        // every slope, floor-variation, obstacle and fall-corridor check intact.
        // Keep this footprint on the private zone for the touchdown recheck.
        StageRewardZone->Area->SetBoxExtent(FVector(650,650,ZoneHeight));
        for(int32 I=0;I<24 && !Found;++I)
            Found=StageRewardZone->FindLanding(Random,Extent,FallHeight,Ignored,Landing,Normal);
    }
    if(!Found) {
        ReportWait(TEXT("central_landing_occupied"));
        return;
    }
    const FRotator Rotation=FRotationMatrix::MakeFromXZ(FVector::ForwardVector,Normal).Rotator();
    const FTransform Pose(Rotation,Landing);
    auto* Chest=GetWorld()->SpawnActorDeferred<AMCRewardChest>(RewardClass,Pose,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Chest) return;
    Chest->InitializeTeamStageReward(Landing,Landing+FVector(0,0,FallHeight),Seed,Table,StageRewardZone);
    StageRewardChest=Chest;
    Chest->FinishSpawning(Pose);
    ForceNetUpdate();
    UE_LOG(LogTemp,Display,TEXT("MC_STAGE_REWARD_DROP completion=%s chest=%s landing=%s"),*StageRewardCompletionId.ToString(),*Chest->GetName(),*Landing.ToString());
}

void AMCSingleDayDirector::NotifyStageRewardFinished(AMCRewardChest* Chest,bool bRetry)
{
    if(!HasAuthority() || bStopped || !bStageRewardPending || !Chest || StageRewardChest!=Chest) return;
    StageRewardChest=nullptr;
    if(!bRetry) CompletedStageRewards.Add(StageRewardCompletionId);
    NextStageRewardAttemptAt=GetWorld()->GetTimeSeconds()+1;
    ForceNetUpdate();
    // Continue on the next unpaused director tick, after the reward controller
    // has closed its cards/released its lease. Never reenter the stage from an
    // effect callback or create a duplicate completion XP/level offer.
}

void AMCSingleDayDirector::TickStageReward()
{
    if(!bStageRewardPending) return;
    if(!CompletedStageRewards.Contains(StageRewardCompletionId)) {
        TrySpawnStageReward();
        Publish(TEXT("ВЫБЕРИ СВОЙ БУСТЕР"),TEXT("Откройте сундук в центре языка. Каждый игрок выбирает один бустер для себя; следующая подача начнётся после всех выборов."));
        return;
    }
    const auto Next=StageRewardContinuation;
    bStageRewardPending=false;
    if(IsValid(StageRewardZone)) StageRewardZone->Destroy(); StageRewardZone=nullptr;
    Record(FString::Printf(TEXT("Личные бустеры %s выбраны: продолжается последовательность"),*StageRewardCompletionId.ToString()));
    ForceNetUpdate();
    switch(Next) {
    case EMCSingleDayRewardContinuation::OpeningCoffee:BeginMealCoffeeWave();break;
    case EMCSingleDayRewardContinuation::OpeningRest:
        if(IsValid(MealServices)) MealServices->Destroy();MealServices=nullptr;BeginOpeningPause(true);break;
    case EMCSingleDayRewardContinuation::PostNutPortion:BeginPostNutPortion(MealPortionNumber);break;
    case EMCSingleDayRewardContinuation::PostNutMint:
        if(IsValid(MealServices)) MealServices->Destroy();MealServices=nullptr;BeginSmallMint();break;
    case EMCSingleDayRewardContinuation::NextKeyEvent:++KeyEventIndex;BeginKeyEvent();break;
    case EMCSingleDayRewardContinuation::EventSupport:BeginDirector();break;
    }
}

void AMCSingleDayDirector::ResetStageRewards()
{
    bStageRewardPending=false;
    auto* Chest=StageRewardChest.Get();StageRewardChest=nullptr;
    if(IsValid(Chest)) Chest->Destroy();
    if(IsValid(StageRewardZone)) StageRewardZone->Destroy();StageRewardZone=nullptr;
    StageRewardCompletionId=NAME_None;CompletedStageRewards.Reset();
    NextStageRewardAttemptAt=0;StageRewardPlacementAttempts=0;
}
