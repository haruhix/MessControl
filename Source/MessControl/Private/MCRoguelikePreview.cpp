#include "MCRoguelikePreview.h"
#include "MCBossCharacter.h"
#include "MCGameMode.h"
#include "MCPerkComponent.h"
#include "MCPerkPickup.h"
#include "MCPlayerState.h"
#include "MCPlayerController.h"
#include "MCPerkChoiceWidget.h"
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
    FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("RogueReview")/Filename,true,false);
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
        for (TActorIterator<AMCBossCharacter> It(GetWorld());It;++It)
        { Finish(false,TEXT("Normal map contains a boss before an explicit F3 spawn.")); return; }
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
        // Exercise the production E interaction RPC instead of bypassing lockpicking.
        Hero->ServerBeginRewardOpening(Chest);
        const auto* PC=Cast<AMCPlayerController>(Hero->GetController());
        if (Chest->Stage!=EMCRewardChestStage::Lockpicking || Chest->OpeningPlayer!=Hero
            || Hero->RewardInteraction!=Chest || !PC || !PC->IsRewardMenuOpen())
        { Finish(false,TEXT("Production E interaction did not reserve the chest and show lockpicking HUD.")); return; }
        Photograph(TEXT("Chest.png"),Chest->LandingPoint+FVector(0,0,60));
        Stage=2; StageStartedAt=Now; return;
    }
    if (Stage==2)
    {
        if (!IsValid(Chest)) { Finish(false,TEXT("Chest disappeared before opening.")); return; }
        if (Chest->Stage==EMCRewardChestStage::Lockpicking && !bOpeningPhotographed && Now-StageStartedAt>=1.5)
        {
            bOpeningPhotographed=true;
            Photograph(TEXT("ChestOpening.png"),Chest->LandingPoint+FVector(0,0,60));
        }
        if (Chest->Stage!=EMCRewardChestStage::Open) return;
        const auto* PC=Cast<AMCPlayerController>(Hero->GetController());
        if (Now-StageStartedAt<4.9 || !bOpeningPhotographed || !PC || !PC->IsRewardMenuOpen()
            || !PC->PerkChoiceWidget || !PC->PerkChoiceWidget->IsShowingChoices())
        { Finish(false,TEXT("Chest skipped its five-second opening or failed to transition into HUD cards.")); return; }
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
        if (Now-StageStartedAt<1.) return;
        for (TActorIterator<AMCPerkPickup> It(GetWorld());It;++It)
            if (It->RewardChest==Chest) { Finish(false,TEXT("Card reward incorrectly spawned a world pickup.")); return; }
        if (!IsValid(Chest) || Chest->LootIDs.Num()!=3) { Finish(false,TEXT("Choice chest or card IDs disappeared.")); return; }
        auto* PC=Cast<AMCPlayerController>(Hero->GetController());
        if (!PC || !PC->IsRewardMenuOpen()) { Finish(false,TEXT("Card choice modal closed before selection.")); return; }
        ChosenID=Chest->LootIDs[0];
        const int32 PreviousChosenStacks=Perks->GetStacks(ChosenID);
        PC->ChooseRewardPerk(0);
        // A repeated owning-controller RPC must remain harmless even after local UI has closed.
        PC->ServerChooseRewardPerk(Chest,0);
        int32 Stacks=0; for (const auto& Perk:Perks->ActivePerks) Stacks+=Perk.Stacks;
        if (Stacks!=BeforeStacks+1 || Perks->GetStacks(ChosenID)!=PreviousChosenStacks+1)
        { Finish(false,TEXT("Card RPC granted an incorrect stack count or accepted a duplicate.")); return; }
        if (Chest->ClaimedMask!=7 || Chest->Stage!=EMCRewardChestStage::Exhausted || PC->IsRewardMenuOpen() || Hero->RewardInteraction)
        { Finish(false,TEXT("Selection did not consume siblings and restore gameplay input.")); return; }
        const FText SpawnResult=Mode->ExecuteDevAction(PC,EMCDevAction::BossPractice);
        for (TActorIterator<AMCBossCharacter> It(GetWorld());It;++It)
            if (It->ActorHasTag(TEXT("MC_DevBoss"))) { Boss=*It; break; }
        if (!Boss || Boss->Runtime.State!=EMCBossState::Dormant || Boss->Runtime.AnimationPreview!=EMCBossAnimationPreview::Idle)
        { Finish(false,FString::Printf(TEXT("F3 spawn did not create a dormant test-only boss: %s"),*SpawnResult.ToString())); return; }
        auto* NavSystem=UNavigationSystemV1::GetCurrent(GetWorld()); FNavLocation NavPoint;
        const FVector Candidate=Boss->GetActorLocation()+Boss->GetActorForwardVector()*650;
        const ANavigationData* BossNav=NavSystem?NavSystem->GetNavDataForProps(Boss->GetNavAgentPropertiesRef(),Boss->GetActorLocation()):nullptr;
        if (!NavSystem || !BossNav || !NavSystem->ProjectPointToNavigation(Candidate,NavPoint,FVector(500,500,600),BossNav) || !PlacePlayer(NavPoint.Location))
        { Finish(false,TEXT("No safe navigable player staging point near the boss.")); return; }
        BossStarted=Boss->GetActorLocation();
        const float Health=Boss->Runtime.Health;
        Boss->ReceiveBossDamage(10.f,Hero);
        if (!FMath::IsNearlyEqual(Boss->Runtime.Health,Health)) { Finish(false,TEXT("Dormant F3 animation preview accepted combat damage.")); return; }
        Stage=4; StageStartedAt=Now;
        Photograph(TEXT("BossIdle.png"),Boss->GetActorLocation()+FVector(0,0,60));
        return;
    }
    if (Stage==4)
    {
        if (Now-StageStartedAt<.8) return;
        if (!IsValid(Boss) || Boss->Runtime.State!=EMCBossState::Dormant || !Boss->GetVelocity().IsNearlyZero())
        { Finish(false,TEXT("F3 presentation preview started moving or activating AI.")); return; }
        auto* PC=Cast<AMCPlayerController>(Hero->GetController());
        // The second explicit F3 command, rather than ordinary gameplay, activates combat.
        const FText AIResult=Mode->ExecuteDevAction(PC,EMCDevAction::BossAI);
        if (Boss->Runtime.State==EMCBossState::Dormant)
        { Finish(false,FString::Printf(TEXT("F3 AI command did not activate the test boss: %s"),*AIResult.ToString())); return; }
        const float Health=Boss->Runtime.Health;
        const float Damage=FMath::Min(10.f,Health*.1f);
        Boss->ReceiveBossDamage(Damage,Hero);
        if (!FMath::IsNearlyEqual(Boss->Runtime.Health,Health-Damage)) { Finish(false,TEXT("Boss authoritative damage failed.")); return; }
        Stage=5; StageStartedAt=Now;
        UE_LOG(LogTemp,Display,TEXT("MC_ROGUE_PREVIEW HUD placeholder %s granted exactly once; F3 boss spawn/AI activated."),*ChosenID.ToString());
        return;
    }
    if (Stage==5)
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
            Stage=6; StageStartedAt=Now; return;
        }
    }
    if (Stage==6 && Now-StageStartedAt>=1.)
    {
        if (!IsValid(Boss) || Boss->Runtime.State!=EMCBossState::Dead || !Boss->GetVelocity().IsNearlyZero())
        { Finish(false,TEXT("Dead boss resumed behavior or movement.")); return; }
        Photograph(TEXT("BossDeath.png"),Boss->GetActorLocation()+FVector(0,0,40));
        Stage=7; StageStartedAt=Now; return;
    }
    if (Stage==7 && Now-StageStartedAt>=.4)
    {
        Finish(true,TEXT("no ordinary-map boss; objective->safe falling chest->E/five-second opening HUD->three distinct same-polarity cards->one owning-controller choice/no pickups/no duplicate; explicit F3 dormant spawn/AI; boss damage/target/nav chase/telegraph/impact/death. Network replication not verified by this solo demonstration."));
    }
}
