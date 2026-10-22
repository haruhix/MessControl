#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCProgressionComponent.h"
#include "MCGameState.h"
#include "MCPlayerState.h"
#include "MCPerkComponent.h"
#include "MCRecoveryPerkEffect.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

namespace
{
struct FProgressionWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCGameState* State=nullptr;
    UMCProgressionComponent* Progression=nullptr;
    UDataTable* Table=nullptr;
    FProgressionWorld(bool Legendary=false)
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
        State=World->SpawnActor<AMCGameState>(); State->Phase=EMCShiftPhase::Working; State->RunSeed=41;
        World->SetGameState(State);
        Progression=State->FindComponentByClass<UMCProgressionComponent>();
        if (!Progression)
        {
            Progression=NewObject<UMCProgressionComponent>(State); State->AddInstanceComponent(Progression);
            Progression->RegisterComponent();
        }
        Progression->FirstLevelExperience=50; Progression->ExperienceGrowthPerLevel=25;
        Progression->ResetProgression();
        Table=NewObject<UDataTable>(); Table->RowStruct=FMCPerkDefinition::StaticStruct();
        for (int32 I=0;I<(Legendary?3:6);++I)
        {
            FMCPerkDefinition Row; Row.Weight=1+I; Row.MaxStacks=Legendary?1:5;
            Row.DisplayName=FText::FromString(FString::Printf(TEXT("Personal fixture %d"),I));
            if (Legendary) { Row.Rarity=EMCPerkRarity::Legendary; Row.ToolUpgrade=EMCToolUpgrade(I+1); }
            Table->AddRow(FName(*FString::Printf(TEXT("LevelFixture%d"),I)),Row);
        }
    }
    ~FProgressionWorld()
    { World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
    AMCPlayerState* Player(int32 ID)
    {
        auto* PC=World->SpawnActor<APlayerController>();
        const FTransform Pose;
        auto* PS=World->SpawnActorDeferred<AMCPlayerState>(AMCPlayerState::StaticClass(),Pose,PC);
        PS->Perks->PerkTable=Table; PS->SetPlayerId(ID); PS->FinishSpawning(Pose); PC->SetPlayerState(PS);
        State->AddPlayerState(PS); Progression->SynchronizePlayer(PS); return PS;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSharedLevelsTest,"MessControl.Progression.ExperienceCannotCreateRewards",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSharedLevelsTest::RunTest(const FString& Parameters)
{
    FProgressionWorld W;
    auto* A=W.Player(11); auto* B=W.Player(22);
    auto* Bot=W.Player(55); Bot->SetIsABot(true);
    A->AddPoints(17);
    for(int32 Amount:{-1,0,250,MAX_int32})
        TestEqual(TEXT("Legacy XP awards never generate levels"),W.Progression->AddExperience(Amount),0);
    TestEqual(TEXT("The compatibility level never advances"),W.Progression->TeamLevel,1);
    TestEqual(TEXT("There is no retained level XP"),W.Progression->ExperienceInLevel,int64(0));
    TestEqual(TEXT("There is no earned XP economy"),W.Progression->TotalExperience,int64(0));
    TestEqual(TEXT("There is no next XP threshold"),W.Progression->GetExperienceToNextLevel(),0);
    TestEqual(TEXT("Ordinary task score remains independent of XP"),A->Points,17);
    for(auto* Player:{A,B,Bot}) {
        Player->QueueChoicesThroughLevel(1000);
        W.Progression->SynchronizePlayer(Player);
        TestEqual(TEXT("A saved level-queue caller cannot create choices"),Player->PendingLevelChoices,0);
        TestFalse(TEXT("A human or bot receives no automatic cards"),Player->LevelUpOffer.IsValid());
        TestTrue(TEXT("A human or bot receives no automatic perk"),Player->Perks->ActivePerks.IsEmpty());
    }
    auto* Late=W.Player(33);
    TestEqual(TEXT("Late joining cannot backfill removed level choices"),Late->PendingLevelChoices,0);
    TestTrue(TEXT("Late joining grants no initial perk"),Late->Perks->ActivePerks.IsEmpty());
    TestFalse(TEXT("XP cannot hold the next event behind a choice"),W.Progression->HasPendingChoices());
    W.Progression->ResetProgression();
    TestEqual(TEXT("Run restart retains zero XP"),W.Progression->TotalExperience,int64(0));
    TestEqual(TEXT("Removing XP does not erase ordinary score"),A->Points,17);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPersonalLegendaryTest,"MessControl.Progression.LegacyOffersRejectedWithoutErasingChestRewards",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPersonalLegendaryTest::RunTest(const FString& Parameters)
{
    FProgressionWorld W(true); auto* A=W.Player(11); auto* B=W.Player(22);
    const FName Reward(TEXT("LevelFixture0"));
    TestTrue(TEXT("An explicitly awarded fixture perk still works"),A->Perks->ServerGrantPerk(Reward));
    A->PersonalChoicesTaken=4;A->bRogueRerollSpent=true;
    auto StaleOffer=[&]() {
        A->LevelUpOffer.OfferId=FGuid::NewGuid();A->LevelUpOffer.TeamLevel=2;
        A->LevelUpOffer.PerkIDs={TEXT("LevelFixture0"),TEXT("LevelFixture1"),TEXT("LevelFixture2")};
        A->PendingLevelChoices=3;return A->LevelUpOffer.OfferId;
    };
    const FGuid OldToken=StaleOffer();
    TestFalse(TEXT("A stale replicated offer cannot authorize another grant"),A->TryChooseLevelUpPerk(OldToken,0));
    TestFalse(TEXT("A stale replicated offer cannot be rerolled into new cards"),A->TryRerollLevelPerks(OldToken));
    TestFalse(TEXT("A stale payload cannot resurrect a progression gate"),A->HasPendingLevelChoices());
    W.Progression->SynchronizePlayer(A);
    TestFalse(TEXT("Synchronization removes the stale payload"),A->LevelUpOffer.IsValid());
    TestEqual(TEXT("Synchronization removes the stale counter"),A->PendingLevelChoices,0);
    TestEqual(TEXT("Removing stale XP preserves already awarded perks"),A->Perks->GetStacks(Reward),1);
    TestEqual(TEXT("Removing stale XP preserves chest reward history"),A->PersonalChoicesTaken,4);
    TestTrue(TEXT("Removing stale XP preserves the spent reroll marker"),A->bRogueRerollSpent);

    auto* PC=Cast<APlayerController>(A->GetOwner());
    auto* OldPawn=W.World->SpawnActor<APawn>();PC->Possess(OldPawn);
    PC->UnPossess();OldPawn->Destroy();PC->Possess(W.World->SpawnActor<APawn>());
    TestEqual(TEXT("Pawn replacement preserves existing rewards"),A->Perks->GetStacks(Reward),1);
    StaleOffer();A->CopyProperties(B);
    TestFalse(TEXT("PlayerState handoff does not revive removed level cards"),B->LevelUpOffer.IsValid());
    TestEqual(TEXT("PlayerState handoff does not revive a level queue"),B->PendingLevelChoices,0);
    TestEqual(TEXT("PlayerState handoff preserves already awarded perks"),B->Perks->GetStacks(Reward),1);
    TestEqual(TEXT("PlayerState handoff preserves chest reward history"),B->PersonalChoicesTaken,4);
    W.Progression->SynchronizePlayer(B);
    TestEqual(TEXT("Repeated synchronization leaves chest rewards intact"),B->Perks->GetStacks(Reward),1);
    TestFalse(TEXT("Compatibility state cannot block a chest-only run"),W.Progression->HasPendingChoices());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCRecoveryChoiceTest,"MessControl.Progression.RecoveryStacksApplyOnceAndSurviveHandoff",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCRecoveryChoiceTest::RunTest(const FString& Parameters)
{
    FProgressionWorld W; auto* Player=W.Player(11);
    for (FName ID:{FName(TEXT("RecoverySelfHeal")),FName(TEXT("RecoveryMouthHeal")),FName(TEXT("RecoveryCleanse"))})
    {
        FMCPerkDefinition Row; Row.MaxStacks=100; Row.EffectClass=UMCRecoveryPerkEffect::StaticClass();
        W.Table->AddRow(ID,Row);
    }
    auto* PC=Cast<APlayerController>(Player->GetOwner());
    auto* Hero=W.World->SpawnActor<AMCToothCharacter>(); PC->Possess(Hero);
    Hero->Status->Initialize(100); Hero->Status->Damage(70);
    TestTrue(TEXT("An instant healing stack can be granted"),Player->Perks->ServerGrantPerk(TEXT("RecoverySelfHeal")));
    TestEqual(TEXT("First stack restores exactly 25 health"),Hero->Status->State.Health,55.f);
    TestTrue(TEXT("A second stack is independently useful"),Player->Perks->ServerGrantPerk(TEXT("RecoverySelfHeal")));
    TestEqual(TEXT("Second stack applies once rather than replaying prior stacks"),Hero->Status->State.Health,80.f);

    Hero->Status->ApplyCoffee(); Hero->Status->Loosen();
    Player->Perks->ServerGrantPerk(TEXT("RecoveryCleanse"));
    TestEqual(TEXT("Cleanse removes personal coffee"),Hero->Status->State.CoffeeLeft,0);
    TestEqual(TEXT("Cleanse removes the loose/repair condition"),Hero->Status->State.RepairLeft,0);
    TestEqual(TEXT("Recovery does not farm task points"),Player->Points,0);
    W.State->MouthHealth=50;
    Player->Perks->ServerGrantPerk(TEXT("RecoveryMouthHeal"));
    TestEqual(TEXT("Mouth recovery restores ten shared health"),W.State->MouthHealth,60.f);

    auto* Target=W.Player(22); auto* TargetPC=Cast<APlayerController>(Target->GetOwner());
    auto* NewHero=W.World->SpawnActor<AMCToothCharacter>(); TargetPC->Possess(NewHero);
    NewHero->Status->Initialize(100); NewHero->Status->Damage(70);
    Player->CopyProperties(Target);
    TestEqual(TEXT("PlayerState handoff cannot replay already consumed healing"),NewHero->Status->State.Health,30.f);
    TestEqual(TEXT("PlayerState handoff cannot replay shared mouth recovery"),W.State->MouthHealth,60.f);
    Target->Perks->ServerGrantPerk(TEXT("RecoverySelfHeal"));
    TestEqual(TEXT("A newly earned stack still applies after handoff"),NewHero->Status->State.Health,55.f);
    return true;
}
#endif
