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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSharedLevelsTest,"MessControl.Progression.SharedExperienceAndIndependentChoices",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSharedLevelsTest::RunTest(const FString& Parameters)
{
    FProgressionWorld W;
    auto* A=W.Player(11); auto* B=W.Player(22);
    TestFalse(TEXT("A new run has no pending choices"),W.Progression->HasPendingChoices());
    TestEqual(TEXT("One reward crosses three thresholds"),W.Progression->AddExperience(250),3);
    TestEqual(TEXT("The shared team level advances"),W.Progression->TeamLevel,4);
    TestEqual(TEXT("Excess experience remains available"),W.Progression->ExperienceInLevel,int64(25));
    TestEqual(TEXT("Total earned experience is preserved"),W.Progression->TotalExperience,int64(250));
    TestEqual(TEXT("First player receives all three levels"),A->PendingLevelChoices,3);
    TestEqual(TEXT("Second player independently receives all three levels"),B->PendingLevelChoices,3);
    if (!TestTrue(TEXT("Each player receives exactly three cards"),A->LevelUpOffer.IsValid() && B->LevelUpOffer.IsValid())) return false;
    TestTrue(TEXT("Cards in an offer are distinct"),A->LevelUpOffer.PerkIDs[0]!=A->LevelUpOffer.PerkIDs[1]
        && A->LevelUpOffer.PerkIDs[1]!=A->LevelUpOffer.PerkIDs[2] && A->LevelUpOffer.PerkIDs[0]!=A->LevelUpOffer.PerkIDs[2]);
    const FGuid FirstOffer=A->LevelUpOffer.OfferId;
    const FName FirstPerk=A->LevelUpOffer.PerkIDs[0];
    TestFalse(TEXT("Another player's offer cannot authorize a choice"),A->TryChooseLevelUpPerk(B->LevelUpOffer.OfferId,0));
    TestFalse(TEXT("Out of range index cannot authorize a choice"),A->TryChooseLevelUpPerk(FirstOffer,3));
    TestTrue(TEXT("The offered card grants its personal perk"),A->TryChooseLevelUpPerk(FirstOffer,0));
    TestEqual(TEXT("The perk is granted once"),A->Perks->GetStacks(FirstPerk),1);
    TestFalse(TEXT("A consumed offer cannot be replayed"),A->TryChooseLevelUpPerk(FirstOffer,0));
    TestEqual(TEXT("One choice consumes one level"),A->PendingLevelChoices,2);
    TestEqual(TEXT("A teammate's queue remains untouched"),B->PendingLevelChoices,3);
    TestEqual(TEXT("Next offer retains the earned level order"),A->LevelUpOffer.TeamLevel,3);
    W.Progression->SynchronizePlayer(A);
    TestEqual(TEXT("Repeated synchronization adds no duplicate choices"),A->PendingLevelChoices,2);
    auto* Late=W.Player(33);
    TestEqual(TEXT("Late team join receives the earned personal choices"),Late->PendingLevelChoices,3);

    auto* PC=Cast<APlayerController>(A->GetOwner());
    auto* OldPawn=W.World->SpawnActor<APawn>(); PC->Possess(OldPawn);
    const FGuid PendingOffer=A->LevelUpOffer.OfferId;
    PC->UnPossess(); OldPawn->Destroy(); PC->Possess(W.World->SpawnActor<APawn>());
    TestTrue(TEXT("Pawn replacement preserves the current personal offer"),A->LevelUpOffer.OfferId==PendingOffer);
    TestEqual(TEXT("Pawn replacement preserves active perks"),A->Perks->GetStacks(FirstPerk),1);
    auto* Reconnected=W.Player(44); A->CopyProperties(Reconnected);
    W.Progression->SynchronizePlayer(Reconnected);
    TestTrue(TEXT("PlayerState handoff preserves the authoritative offer"),Reconnected->LevelUpOffer.OfferId==PendingOffer);
    TestEqual(TEXT("PlayerState handoff preserves unclaimed levels without duplication"),Reconnected->PendingLevelChoices,2);
    TestEqual(TEXT("PlayerState handoff preserves already selected perks"),Reconnected->Perks->GetStacks(FirstPerk),1);
    TestTrue(TEXT("Pending team choices gate the next event"),W.Progression->HasPendingChoices());
    W.Progression->ResetProgression();
    TestEqual(TEXT("Run restart clears team experience"),W.Progression->TotalExperience,int64(0));
    TestFalse(TEXT("Run restart clears every pending choice"),W.Progression->HasPendingChoices());
    TestFalse(TEXT("Restart invalidates outstanding offer tokens"),A->TryChooseLevelUpPerk(PendingOffer,0));

    auto* Bot=W.Player(55); Bot->SetIsABot(true);
    TMap<FName,int32> BeforeBotAward;
    for (const auto& Active:A->Perks->ActivePerks) BeforeBotAward.Add(Active.PerkID,Active.Stacks);
    W.Progression->AddExperience(W.Progression->GetExperienceToNextLevel());
    TestEqual(TEXT("A bot claims its first earned choice without a widget"),Bot->PendingLevelChoices,0);
    if (!TestEqual(TEXT("A bot receives exactly one personal perk"),Bot->Perks->ActivePerks.Num(),1)) return false;
    TestEqual(TEXT("A bot choice leaves the human's earned choice pending"),A->PendingLevelChoices,1);
    const FGuid HumanOffer=A->LevelUpOffer.OfferId;
    const FName BotPerk=Bot->Perks->ActivePerks[0].PerkID;
    W.Progression->SynchronizePlayer(Bot);
    TestTrue(TEXT("Bot synchronization preserves the human's exact offer"),A->LevelUpOffer.OfferId==HumanOffer);
    TestEqual(TEXT("A bot's personal choice is not shared with its teammate"),A->Perks->GetStacks(BotPerk),BeforeBotAward.FindRef(BotPerk));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPersonalLegendaryTest,"MessControl.Progression.LegendaryLevelChoiceIsPersonal",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPersonalLegendaryTest::RunTest(const FString& Parameters)
{
    FProgressionWorld W(true); auto* A=W.Player(11); auto* B=W.Player(22);
    W.Progression->AddExperience(W.Progression->GetExperienceToNextLevel());
    if (!TestTrue(TEXT("First earned level offers three legendary cards to everyone"),A->LevelUpOffer.IsValid() && B->LevelUpOffer.IsValid())) return false;
    const FName Selected=A->LevelUpOffer.PerkIDs[0];
    TestTrue(TEXT("Legendary level selection succeeds"),A->TryChooseLevelUpPerk(A->LevelUpOffer.OfferId,0));
    TestEqual(TEXT("The selected legendary belongs to the selecting player"),A->Perks->GetStacks(Selected),1);
    TestEqual(TEXT("The teammate does not inherit the legendary"),B->Perks->GetStacks(Selected),0);
    TestEqual(TEXT("Personal choices do not set the shared tool unlock mask"),int32(W.State->TeamToolUpgrades),0);
    const int32 OtherIndex=B->LevelUpOffer.PerkIDs.IndexOfByKey(Selected);
    TestTrue(TEXT("The teammate may independently choose the same legendary"),B->TryChooseLevelUpPerk(B->LevelUpOffer.OfferId,OtherIndex));
    TestFalse(TEXT("Both independent choices finish without a physical chest"),W.Progression->HasPendingChoices());
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
