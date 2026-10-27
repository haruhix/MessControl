#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFishingRodComponent.h"
#include "MCHubEconomyComponent.h"
#include "MCHubFishingZone.h"
#include "MCHubGameState.h"
#include "MCHubProfileSubsystem.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/Package.h"
#include <limits>

namespace MCFishingRewardTests
{
struct FWorld
{
    UWorld* World=nullptr;
    AMCToothCharacter* Hero=nullptr;
    AMCPlayerState* Player=nullptr;
    APlayerController* Controller=nullptr;
    AMCHubFishingZone* Zone=nullptr;
    FWorld()
    {
        const FString PackageName=TEXT("/Temp/MCFishing_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT("_Hub");
        World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("Hub"),CreatePackage(*PackageName));
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* State=World->SpawnActor<AMCHubGameState>();World->SetGameState(State);State->Phase=EMCHubPhase::Gathering;
        Controller=World->SpawnActor<APlayerController>();Controller->SetPlayer(NewObject<ULocalPlayer>(GEngine));
        Player=World->SpawnActorDeferred<AMCPlayerState>(AMCPlayerState::StaticClass(),FTransform::Identity,
            Controller,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        auto* Catalog=NewObject<UDataTable>(World);Catalog->RowStruct=FMCHubCatalogRow::StaticStruct();
        auto* Rewards=NewObject<UDataTable>(World);Rewards->RowStruct=FMCGrillRewardRow::StaticStruct();
        FMCGrillRewardRow CoinReward;CoinReward.MinimumCoins=CoinReward.MaximumCoins=9;
        CoinReward.ReelSymbols={TEXT("Cherry"),TEXT("Lemon"),TEXT("Watermelon")};Rewards->AddRow(TEXT("Coins"),CoinReward);
        auto* Rules=NewObject<UDataTable>(World);Rules->RowStruct=FMCHubEconomyRulesRow::StaticStruct();
        Rules->AddRow(TEXT("Default"),FMCHubEconomyRulesRow());
        Player->Economy->CatalogTable=Catalog;Player->Economy->GrillRewardsTable=Rewards;Player->Economy->RulesTable=Rules;
        Player->FinishSpawning(FTransform::Identity);Controller->SetPlayerState(Player);
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,2000),FRotator::ZeroRotator);
        Controller->Possess(Hero);Hero->Status->Initialize(100);Hero->GetCharacterMovement()->DisableMovement();
        Zone=World->SpawnActor<AMCHubFishingZone>(Hero->GetActorLocation(),FRotator::ZeroRotator);
        Hero->FishingRod->RefreshFishingZone();
    }
    bool Ready() const
    {
        return Hero->HasActorBegunPlay() && Hero->FishingRod->HasBegunPlay() && Hero->IsLocallyControlled()
            && Hero->GetPlayerState<AMCPlayerState>()==Player && Hero->FishingRod->IsInsideFishingZone();
    }
    bool Hook(float Roll)
    {
        auto* Rod=Hero->FishingRod.Get();
        if(!Rod->SelectCatchReward(Roll,Rod->HookedCatch)) return false;
        Rod->bFishHooked=true;Rod->bLineOut=true;Rod->bReeling=true;
        return true;
    }
    ~FWorld()
    {
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFishingWeightedValueTest,"MessControl.Hub.Fishing.RarityValuesAndInvalidData",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFishingWeightedValueTest::RunTest(const FString&)
{
    auto* Rod=NewObject<UMCFishingRodComponent>();FMCFishingCatchReward Reward;
    const float Rolls[]={.1f,.8f,.95f,.995f};
    const int32 Values[]={1,2,5,10};
    for(int32 Index=0;Index<4;++Index)
    {
        TestTrue(TEXT("Every authored rarity has a reachable server-weighted interval"),Rod->SelectCatchReward(Rolls[Index],Reward));
        TestEqual(TEXT("The selected interval grants its configured fish value"),Reward.FishCoins,Values[Index]);
        TestEqual(TEXT("Rarity and value originate from the same selected row"),int32(Reward.Rarity),Index);
    }
    TestTrue(TEXT("The exact upper endpoint still resolves a valid catch"),Rod->SelectCatchReward(1.f,Reward));
    TestEqual(TEXT("Upper endpoint selects the final rarity"),Reward.Rarity,EMCHubItemRarity::Legendary);
    Rod->CatchRewards={
        {EMCHubItemRarity::Common,std::numeric_limits<float>::quiet_NaN(),1},
        {EMCHubItemRarity::Rare,-4.f,2},
        {EMCHubItemRarity::Epic,5.f,MAX_int32},
        {EMCHubItemRarity::Legendary,0.f,10},
        {EMCHubItemRarity::Rare,3.f,17}
    };
    TestTrue(TEXT("Invalid rows are excluded without preventing another valid authored reward"),Rod->SelectCatchReward(.5f,Reward));
    TestEqual(TEXT("A designer-edited fish value is used rather than a rarity hardcode"),Reward.FishCoins,17);
    Rod->CatchRewards.Pop();
    TestFalse(TEXT("An entirely invalid reward pool produces no catch award"),Rod->SelectCatchReward(.5f,Reward));
    TestFalse(TEXT("A nonfinite random value cannot enter the weighted selection"),Rod->SelectCatchReward(std::numeric_limits<float>::quiet_NaN(),Reward));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFishingRecoveryRewardTest,"MessControl.Hub.Fishing.RecoveryCreditsSnapshotExactlyOnce",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFishingRecoveryRewardTest::RunTest(const FString&)
{
    MCFishingRewardTests::FWorld W;
    if(!TestTrue(TEXT("Fishing fixture has a real begun-play local pawn and pier area"),W.Ready())) return false;
    W.Player->FishCaught=23;W.Player->Economy->Coins=7;W.Player->Economy->PendingBoosters={TEXT("ExistingBooster")};
    if(!TestTrue(TEXT("A legendary hook captures its configured value"),W.Hook(.995f))) return false;
    // Editing the reward pool after the bite must not change the already hooked fish.
    W.Hero->FishingRod->CatchRewards={{EMCHubItemRarity::Common,1.f,99}};
    W.Hero->FishingRod->ServerSetEquipped(false);
    TestEqual(TEXT("Stowing recovers the frozen catch value, not a new roll"),W.Player->FishCaught,33);
    TestFalse(TEXT("The hook is consumed before another cancellation callback can award it"),W.Hero->FishingRod->bFishHooked);
    W.Hero->FishingRod->ServerSetEquipped(false);W.Hero->FishingRod->ServerSetEquipped(false);
    TestEqual(TEXT("Repeated recovery callbacks cannot duplicate the catch"),W.Player->FishCaught,33);
    TestEqual(TEXT("Fishing never grants shop coins"),W.Player->Economy->Coins,7);
    TestEqual(TEXT("Fishing never adds or removes queued boosters"),W.Player->Economy->PendingBoosters.Num(),1);
    TestEqual(TEXT("The existing queued booster remains unchanged"),W.Player->Economy->PendingBoosters[0],FName(TEXT("ExistingBooster")));

    W.Hero->FishingRod->bLineOut=true;W.Hero->FishingRod->ServerSetEquipped(false);
    TestEqual(TEXT("An interrupted line without a hooked fish has no reward"),W.Player->FishCaught,33);
    W.Hero->FishingRod->CatchRewards={{EMCHubItemRarity::Epic,1.f,5}};
    if(!TestTrue(TEXT("A second actual hooked fish remains recoverable"),W.Hook(.5f))) return false;
    W.Hero->SetActorLocation(W.Hero->GetActorLocation()+FVector(2000,0,0));W.Hero->FishingRod->RefreshFishingZone();
    TestEqual(TEXT("Leaving the pier recovers that catch once rather than stranding it"),W.Player->FishCaught,38);
    W.Hero->FishingRod->RefreshFishingZone();
    TestEqual(TEXT("A repeated out-of-zone refresh cannot duplicate it"),W.Player->FishCaught,38);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFishingWalletCompatibilityTest,"MessControl.Hub.Fishing.ExistingWalletSaveAndGrillStayCompatible",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFishingWalletCompatibilityTest::RunTest(const FString&)
{
    MCFishingRewardTests::FWorld W;
    if(!TestTrue(TEXT("Wallet fixture has initialized player and fishing components"),W.Ready())) return false;
    auto* Legacy=NewObject<UMCHubProfileSave>();Legacy->Fish=41;
    TArray<uint8> Bytes;
    if(!TestTrue(TEXT("An existing version-one profile still serializes without schema migration"),UGameplayStatics::SaveGameToMemory(Legacy,Bytes))) return false;
    const auto* Read=Cast<UMCHubProfileSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if(!TestNotNull(TEXT("The unchanged saved profile class restores from memory"),Read)) return false;
    TestEqual(TEXT("The legacy balance is retained exactly"),Read->Fish,41);W.Player->FishCaught=Read->Fish;
    TestEqual(TEXT("A rare catch credits the existing balance"),W.Player->AddCaughtFish(2),2);
    auto* Replacement=W.World->SpawnActor<AMCPlayerState>();W.Player->CopyProperties(Replacement);
    TestEqual(TEXT("Travel or player-state replacement retains the same fish-wallet balance"),Replacement->FishCaught,43);
    auto* Current=NewObject<UMCHubProfileSave>();Current->Fish=W.Player->FishCaught;
    if(!TestTrue(TEXT("The new balance uses the same saved Fish field"),UGameplayStatics::SaveGameToMemory(Current,Bytes))) return false;
    TestEqual(TEXT("The new balance round-trips without resetting earlier fish"),CastChecked<UMCHubProfileSave>(UGameplayStatics::LoadGameFromMemory(Bytes))->Fish,43);

    FGuid Receipt;FMCGrillReward GrillPreview;FText Error;
    if(!TestTrue(TEXT("The grill accepts that existing fish wallet as before"),W.Player->Economy->BeginGrillRoll(Receipt,GrillPreview,Error))) return false;
    TestEqual(TEXT("The unchanged grill reserves one fish currency unit"),W.Player->FishCaught,42);
    TestTrue(TEXT("The unchanged grill receipt still settles normally"),W.Player->Economy->CompleteGrillRoll(Receipt));
    TestEqual(TEXT("Only the grill's authored reward grants shop coins"),W.Player->Economy->Coins,9);

    W.Player->FishCaught=999998;
    TestEqual(TEXT("A near-cap catch reports the actual credited amount"),W.Player->AddCaughtFish(10),2);
    TestEqual(TEXT("The balance stays within the existing persisted wallet cap"),W.Player->FishCaught,1000000);
    TestEqual(TEXT("A full wallet reports no credit"),W.Player->AddCaughtFish(10),0);
    W.Player->FishCaught=1000023;
    TestEqual(TEXT("An already oversized legacy runtime balance is never reduced by a new catch"),W.Player->AddCaughtFish(10),0);
    TestEqual(TEXT("That legacy balance is preserved"),W.Player->FishCaught,1000023);
    W.Player->FishCaught=17;
    TestEqual(TEXT("Negative native credit requests cannot remove fish"),W.Player->AddCaughtFish(-1),0);
    TestEqual(TEXT("An unsafe single-catch amount is rejected"),W.Player->AddCaughtFish(MAX_int32),0);
    TestEqual(TEXT("Invalid requests leave an ordinary non-full wallet untouched"),W.Player->FishCaught,17);
    return true;
}
#endif
