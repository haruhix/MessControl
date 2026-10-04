#include "MCRoguelikePreview.h"
#include "MCBossCharacter.h"
#include "MCGameMode.h"
#include "MCPerkComponent.h"
#include "MCPerkPickup.h"
#include "MCPlayerState.h"
#include "MCRewardChest.h"
#include "MCRoguelikeDirector.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "UnrealClient.h"

AMCRoguelikePreview::AMCRoguelikePreview() { PrimaryActorTick.bCanEverTick=false; }

void AMCRoguelikePreview::BeginPlay()
{
    Super::BeginPlay();
#if UE_BUILD_SHIPPING
    Destroy();
#else
    if (!HasAuthority()) { Destroy(); return; }
    FParse::Value(FCommandLine::Get(),TEXT("MCExpectedPlayers="),ExpectedPlayers);
    ExpectedPlayers=FMath::Clamp(ExpectedPlayers,1,8);
    StartedAt=StageStartedAt=GetWorld()->GetTimeSeconds();
    IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("RogueReview")),true);
    GetWorldTimerManager().SetTimer(StepTimer,this,&AMCRoguelikePreview::Step,.1f,true);
    UE_LOG(LogTemp,Display,TEXT("MC_ROGUE_PREVIEW Started; expected players=%d"),ExpectedPlayers);
#endif
}

void AMCRoguelikePreview::EndPlay(const EEndPlayReason::Type Reason)
{
    GetWorldTimerManager().ClearTimer(StepTimer);
    Super::EndPlay(Reason);
}

bool AMCRoguelikePreview::PlacePlayer(FVector FloorPoint)
{
    if (!IsValid(Hero)) return false;
    Hero->GetCharacterMovement()->StopMovementImmediately();
    return Hero->TeleportTo(FloorPoint+FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),Hero->GetActorRotation());
}

void AMCRoguelikePreview::Photograph(const TCHAR* Filename,FVector Focus)
{
    auto* PC=Hero?Cast<APlayerController>(Hero->GetController()):nullptr;
    if (!PC || GetNetMode()==NM_DedicatedServer) return;
    if (!Camera) Camera=GetWorld()->SpawnActor<ACameraActor>();
    if (!Camera) return;
    FVector Eye=Focus+FVector(500,-850,550);
    if(Stage<=3 && IsValid(Chest)) Eye=Focus-Chest->GetActorRightVector()*750+Chest->GetActorForwardVector()*250+FVector(0,0,470);
    else if(IsValid(Boss)) Eye=Focus+Boss->GetActorForwardVector()*750-Boss->GetActorRightVector()*300+FVector(0,0,420);
    Camera->SetActorLocationAndRotation(Eye,(Focus-Eye).Rotation());
    Camera->GetCameraComponent()->SetFieldOfView(60.f);
    PC->SetViewTarget(Camera);
    FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("RogueReview")/Filename,false,false);
}

void AMCRoguelikePreview::Finish(bool Passed,const FString& Detail)
{
    if (bFinished) return;
    bFinished=true;
    GetWorldTimerManager().ClearTimer(StepTimer);
    UE_LOG(LogTemp,Display,TEXT("MC_ROGUE_%s %s"),Passed?TEXT("PASS"):TEXT("FAIL"),*Detail);
    FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);
}

void AMCRoguelikePreview::Step()
{
    const double Now=GetWorld()->GetTimeSeconds();
    if (Now-StartedAt>60.)
    {
        Finish(false,FString::Printf(TEXT("Timeout stage=%d chest=%d boss=%d chase=%d target=%d telegraph=%d attack=%d"),
            Stage,Chest?int32(Chest->Stage):-1,Boss?int32(Boss->Runtime.State):-1,bSawChase,bSawTarget,bSawTelegraph,bSawAttack));
        return;
    }
    auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>();
    if (!Mode || !Mode->RoguelikeDirector) return;
    if (Stage==0)
    {
        int32 LivingPlayers=0;
        for (auto It=GetWorld()->GetPlayerControllerIterator();It;++It)
        {
            auto* PC=It->Get(); auto* Player=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
            auto* PS=Player?Player->GetPlayerState<AMCPlayerState>():nullptr;
            if (!AMCBossCharacter::IsLivingPlayer(Player) || !PS || !PS->Perks || !PS->Perks->GetPerkTable()) continue;
            ++LivingPlayers; if (!Hero) { Hero=Player; Perks=PS->Perks; }
        }
        if (LivingPlayers<ExpectedPlayers || !Hero) return;
        for (const auto& Perk:Perks->ActivePerks) BeforeStacks+=Perk.Stacks;
        auto* Director=Mode->RoguelikeDirector.Get();
        BeforeSpawned=Director->RewardsSpawned;
        const int32 BeforeRewards=Director->PendingRewards+Director->RewardsSpawned;
        Mode->AwardTask(Hero,EMCScoreTask::Coffee);
        if (Director->PendingRewards+Director->RewardsSpawned!=BeforeRewards)
        { Finish(false,TEXT("An individual score award incorrectly created an objective reward.")); return; }
        Mode->NotifyObjectiveCompleted(TEXT("RoguePreviewObjective"));
        Mode->NotifyObjectiveCompleted(TEXT("RoguePreviewObjective"));
        if (Director->PendingRewards+Director->RewardsSpawned!=BeforeRewards+1)
        { Finish(false,TEXT("Objective completion did not enqueue exactly one deduplicated reward.")); return; }
        Stage=1; StageStartedAt=Now;
        UE_LOG(LogTemp,Display,TEXT("MC_ROGUE_PREVIEW Objective reward enqueued once through GameMode::NotifyObjectiveCompleted."));
        return;
    }
    if (!IsValid(Hero) || !IsValid(Perks) || !Hero->Status->IsAlive())
    { Finish(false,TEXT("Demonstration player died or disappeared before completion.")); return; }
    if (Stage==1)
    {
        if (Mode->RoguelikeDirector->RewardsSpawned<=BeforeSpawned) return;
        if (!Chest) for (TActorIterator<AMCRewardChest> It(GetWorld());It;++It)
            if (!It->bPlacedReward && It->GetOwner()==Mode->RoguelikeDirector) { Chest=*It; break; }
        if (!IsValid(Chest) || Chest->Stage!=EMCRewardChestStage::Landed) return;
        const FVector Approach=Chest->GetActorTransform().TransformPosition(FVector(0,-Chest->OpenRadius*.8f,0));
        if (!PlacePlayer(Approach)) { Finish(false,TEXT("Safe chest approach teleport failed.")); return; }
        Photograph(TEXT("Chest.png"),Chest->LandingPoint+FVector(0,0,60));
        Stage=2; StageStartedAt=Now; return;
    }
    if (Stage==2)
    {
        if (!IsValid(Chest)) { Finish(false,TEXT("Chest disappeared before opening.")); return; }
        if (Chest->Stage!=EMCRewardChestStage::Open) return;
        if (Chest->LootIDs.Num()!=3 || Chest->LootIDs[0]==Chest->LootIDs[1] || Chest->LootIDs[0]==Chest->LootIDs[2] || Chest->LootIDs[1]==Chest->LootIDs[2])
        { Finish(false,TEXT("Chest did not expose three distinct perk IDs.")); return; }
        for (FName ID:Chest->LootIDs)
        {
            FMCPerkDefinition Definition;
            if (!Perks->GetPerkDefinition(ID,Definition) || Definition.Polarity!=Chest->Polarity)
            { Finish(false,TEXT("Chest mixed positive and negative perk definitions.")); return; }
        }
        Photograph(TEXT("Choices.png"),Chest->LandingPoint+FVector(0,-70,60));
        Stage=3; StageStartedAt=Now; return;
    }
    if (Stage==3)
    {
        if (Now-StageStartedAt<1.5) return;
        for (TActorIterator<AMCPerkPickup> It(GetWorld());It;++It)
            if (It->RewardChest==Chest && It->LootIndex==0 && !It->bClaimed) { Chosen=*It; break; }
        if (!Chosen) { Finish(false,TEXT("First choice pickup missing.")); return; }
        ChosenID=Chosen->PerkID;
        const int32 PreviousChosenStacks=Perks->GetStacks(ChosenID);
        // Arming has elapsed while choices were visible. Exit first so an existing observer must re-enter.
        const FVector ExitPoint=Chosen->GetActorLocation()-Chest->GetActorForwardVector()*120-FVector(0,0,45);
        if (!PlacePlayer(ExitPoint)) { Finish(false,TEXT("Pickup exit teleport failed.")); return; }
        const FVector FootPoint=Chosen->GetActorLocation()-FVector(0,0,45);
        if (!PlacePlayer(FootPoint)) { Finish(false,TEXT("Pickup approach teleport failed.")); return; }
        // Overlap may grant during teleport; otherwise invoke the same server proximity/LoS validated claim.
        if (!Chosen->bClaimed && !Chosen->TryCollect(Hero)) { Finish(false,TEXT("Validated nearby pickup claim failed.")); return; }
        int32 Stacks=0; for (const auto& Perk:Perks->ActivePerks) Stacks+=Perk.Stacks;
        if (Stacks!=BeforeStacks+1 || Perks->GetStacks(ChosenID)!=PreviousChosenStacks+1 || Chosen->TryCollect(Hero))
        { Finish(false,TEXT("Pickup granted a different choice, an incorrect stack count or accepted a duplicate.")); return; }
        if (!Chest || Chest->ClaimedMask!=7) { Finish(false,TEXT("Sibling placeholder choices remain collectible.")); return; }
        for (TActorIterator<AMCPerkPickup> It(GetWorld());It;++It)
            if (It->RewardChest==Chest && !It->bClaimed) { Finish(false,TEXT("Unchosen pickup remained claimable under ChooseOne policy.")); return; }
        for (TActorIterator<AMCBossCharacter> It(GetWorld());It;++It) if (It->IsBossAlive()) { Boss=*It; break; }
        if (!Boss) { Finish(false,TEXT("Placed boss foundation missing.")); return; }
        auto* NavSystem=UNavigationSystemV1::GetCurrent(GetWorld()); FNavLocation NavPoint;
        const FVector Candidate=Boss->GetActorLocation()+Boss->GetActorForwardVector()*650;
        const ANavigationData* BossNav=NavSystem?NavSystem->GetNavDataForProps(Boss->GetNavAgentPropertiesRef(),Boss->GetActorLocation()):nullptr;
        if (!NavSystem || !BossNav || !NavSystem->ProjectPointToNavigation(Candidate,NavPoint,FVector(500,500,600),BossNav) || !PlacePlayer(NavPoint.Location))
        { Finish(false,TEXT("No safe navigable player staging point near the boss.")); return; }
        BossStarted=Boss->GetActorLocation();
        const float Health=Boss->Runtime.Health;
        const float Damage=FMath::Min(10.f,Health*.1f);
        Boss->ReceiveBossDamage(Damage,Hero);
        if (!FMath::IsNearlyEqual(Boss->Runtime.Health,Health-Damage)) { Finish(false,TEXT("Boss authoritative damage failed.")); return; }
        Boss->ActivateBoss();
        Stage=4; StageStartedAt=Now;
        UE_LOG(LogTemp,Display,TEXT("MC_ROGUE_PREVIEW Placeholder %s granted exactly once; boss activated."),*ChosenID.ToString());
        return;
    }
    if (Stage==4)
    {
        if (!IsValid(Boss)) { Finish(false,TEXT("Boss disappeared during demonstration.")); return; }
        bSawTarget|=Boss->Runtime.Target==Hero;
        bSawChase|=Boss->Runtime.State==EMCBossState::Chasing && FVector::DistSquared(BossStarted,Boss->GetActorLocation())>FMath::Square(20.f);
        if (Boss->Runtime.State==EMCBossState::Telegraph && !bSawTelegraph)
        { bSawTelegraph=true; Photograph(TEXT("BossTelegraph.png"),Boss->GetActorLocation()+FVector(0,0,60)); }
        if (Boss->Runtime.State==EMCBossState::Attacking) bSawAttack=true;
        if (bSawTarget && bSawChase && bSawTelegraph && bSawAttack && Now-StageStartedAt>=5.)
        {
            Boss->ReceiveBossDamage(Boss->Runtime.MaxHealth,Hero);
            if (Boss->Runtime.State!=EMCBossState::Dead || Boss->IsBossAlive()) { Finish(false,TEXT("Boss death did not cancel behavior.")); return; }
            Stage=5; StageStartedAt=Now; return;
        }
    }
    if (Stage==5 && Now-StageStartedAt>=1.)
    {
        if (!IsValid(Boss) || Boss->Runtime.State!=EMCBossState::Dead || !Boss->GetVelocity().IsNearlyZero())
        { Finish(false,TEXT("Dead boss resumed behavior or movement.")); return; }
        Finish(true,TEXT("objective->safe falling chest->three distinct same-polarity placeholders->one choice/no duplicate; boss damage/target/nav chase/telegraph/impact/death. Network replication not verified by this solo demonstration."));
    }
}
