#include "MCRoguelikeEditorLibrary.h"
#include "MCGameState.h"
#include "MCGameDirector.h"
#include "MCSingleDayDirector.h"
#include "MCPlayerState.h"
#include "MCPlayerController.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothStatusComponent.h"
#include "MCInventoryComponent.h"
#include "MCPerkComponent.h"
#include "MCProgressionComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCRewardChest.h"
#include "MCRewardDropZone.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"

FString UMCRoguelikeEditorLibrary::ReviewRogueGameplay(UWorld* World,FName Action)
{
    if(!World || World->WorldType!=EWorldType::PIE || World->GetNetMode()!=NM_Standalone) return TEXT("REJECT: solo PIE required");
    auto* PC=Cast<AMCPlayerController>(World->GetFirstPlayerController());
    auto* PS=PC?PC->GetPlayerState<AMCPlayerState>():nullptr;
    auto* Hero=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    auto* GS=World->GetGameState<AMCGameState>();
    if(!Hero || !PS || !GS || !Hero->HasAuthority()) return TEXT("REJECT: no local authoritative player");
    if(Action==TEXT("level")) {
        for(TActorIterator<AMCGameDirector> It(World);It;++It) It->Destroy();
        for(TActorIterator<AMCSingleDayDirector> It(World);It;++It) It->Destroy();
        GS->SingleDayDirector=nullptr;GS->bLobbyWaiting=false;GS->bTutorialActive=false;GS->Phase=EMCShiftPhase::Working;
        GS->bDevManualEvents=true;GS->bDayOneComplete=false;
        GS->RogueOptionalEvents={TEXT("Food"),TEXT("Progression"),TEXT("NutEncounter"),TEXT("NutBossKnight"),TEXT("IceEvent"),TEXT("FogBrawl")};
        if(GS->Progression) GS->Progression->ResetProgression();
        PS->ResetLevelUpChoices();PS->Perks->ResetPerks();Hero->Status->Initialize(100);
        Hero->SetActorLocation(FVector(0,0,11080),false,nullptr,ETeleportType::TeleportPhysics);Hero->GetCharacterMovement()->DisableMovement();
        PS->QueueChoicesThroughLevel(3);PC->RefreshLevelPerkChoices();
    } else if(Action==TEXT("reroll")) PC->RerollLevelPerks();
    else if(Action==TEXT("choose")) PC->ServerChooseLevelPerk(PS->LevelUpOffer.OfferId,0);
    else if(Action==TEXT("contract") || Action==TEXT("altar")) {
        PS->ResetLevelUpChoices();PC->RefreshLevelPerkChoices();PS->PersonalChoicesTaken=2;
        for(TActorIterator<AMCRewardChest> It(World);It;++It) if(It->ActorHasTag(TEXT("RogueReview"))) It->Destroy();
        auto* Zone=World->SpawnActor<AMCRewardDropZone>(FVector(0,0,11000),FRotator::ZeroRotator);
        auto* Chest=World->SpawnActor<AMCRewardChest>(FVector(0,0,11000),FRotator::ZeroRotator);
        Chest->Tags.Add(TEXT("RogueReview"));Chest->RogueKind=Action==TEXT("contract")?EMCRogueRewardKind::CursedContract:EMCRogueRewardKind::RiskyAltar;
        Chest->InitializeReward(Chest->GetActorLocation(),Chest->GetActorLocation(),67,PS->Perks->GetPerkTable(),EMCRewardSelectionPolicy::ChooseOne,Zone);
        Chest->ForceMimicForTest(false);Chest->Stage=EMCRewardChestStage::Landed;
        const FTransform Contact=Chest->GetLockpickContact();FVector At=Contact.GetLocation()+Contact.GetUnitAxis(EAxis::X)*120;At.Z=11061;
        Hero->SetActorLocation(At,false,nullptr,ETeleportType::TeleportPhysics);
        if(!Chest->BeginLockpicking(Hero)) return TEXT("REJECT: real lockpick validation failed");
        Chest->Stage=EMCRewardChestStage::Open;Chest->PublishRogueOffer();
    } else if(Action==TEXT("curse") || Action==TEXT("reward") || Action==TEXT("confirm") || Action==TEXT("decline")) {
        auto* Chest=Hero->RewardInteraction.Get();if(!Chest) return TEXT("REJECT: no active reward");
        if(Action==TEXT("confirm") || Action==TEXT("decline")) PC->ServerConfirmRogueReward(Chest,Action==TEXT("confirm"),Chest->GetChoiceOfferID(PS));
        else PC->ChooseRewardPerk(0);
    } else if(Action==TEXT("boosters")) {
        PS->ResetLevelUpChoices();PC->RefreshLevelPerkChoices();PS->Perks->ResetPerks();
        Hero->Status->Initialize(100);Hero->Status->ApplyCoffee();
        PS->Perks->ServerGrantPerks({TEXT("Fallback_Mouthwash"),TEXT("Fallback_BrushFury"),TEXT("Fallback_Grenade")});
    } else if(Action==TEXT("activate")) PC->ActivateRogueBooster();
    else if(Action==TEXT("skill1")) PC->ActivateSkillSlot(0);
    else if(Action==TEXT("skill2")) PC->ActivateSkillSlot(1);
    else if(Action==TEXT("skill3")) PC->ActivateSkillSlot(2);
    else if(Action==TEXT("skill4")) PC->ActivateSkillSlot(3);
    else if(Action==TEXT("knockdown")) Hero->ToothPhysics->ApplyHit(FVector(1000,0,250),Hero->GetActorLocation());
    else if(Action==TEXT("saw")) {
        PS->ResetLevelUpChoices();PC->RefreshLevelPerkChoices();PS->Perks->ResetPerks();Hero->Status->Initialize(100);
        auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
        auto Box=[&](FVector At,FVector Scale) {
            auto* Actor=World->SpawnActor<AActor>();Actor->Tags.Add(TEXT("RogueReview"));
            auto* Mesh=NewObject<UStaticMeshComponent>(Actor);Actor->SetRootComponent(Mesh);Actor->AddInstanceComponent(Mesh);
            Mesh->SetStaticMesh(Cube);Mesh->SetCollisionProfileName(TEXT("BlockAll"));Mesh->RegisterComponent();
            Actor->SetActorLocation(At);Actor->SetActorScale3D(Scale);
        };
        Box(FVector(0,0,10950),FVector(30,30,1));Box(FVector(420,0,11200),FVector(1,8,4));
        Hero->SetActorLocation(FVector(0,0,11080),false,nullptr,ETeleportType::TeleportPhysics);Hero->SetActorRotation(FRotator::ZeroRotator);
        CastChecked<UMCToothMovementComponent>(Hero->GetCharacterMovement())->SetMovementMode(MOVE_Walking);
        PS->Perks->ServerGrantPerk(TEXT("Tool_Chainsaw_Rare"));Hero->Inventory->ServerSelect(EMCToolSlot::Knife);
        Hero->ServerSetPrimary(true);Hero->ServerSetPrimary(false);
    }
    const FString Result=FString::Printf(TEXT("action=%s menu=%d offers=%d taken=%d active=%d queue=%d grenade=%d fury=%d saw=%d HP=%.1f"),
        *Action.ToString(),PC->IsRewardMenuOpen(),PS->LevelUpOffer.PerkIDs.Num(),PS->PersonalChoicesTaken,PS->Perks->ActivePerks.Num(),
        PS->Perks->BoosterQueue.Num(),PS->Perks->bGrenadeArmed,PS->Perks->IsFuryActive(),Hero->Inventory->bChainsawRunning,Hero->Status->State.Health);
    UE_LOG(LogTemp,Display,TEXT("MC_ROGUE_PIE %s"),*Result);return Result;
}
