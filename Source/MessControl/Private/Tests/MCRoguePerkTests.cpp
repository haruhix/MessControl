#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCPerkComponent.h"
#include "MCRoguePerkEffect.h"
#include "MCPlayerState.h"
#include "MCPlayerController.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothMovementComponent.h"
#include "MCFoodCollectionComponent.h"
#include "MCInventoryComponent.h"
#include "MCNutEnemy.h"
#include "MCRewardChest.h"
#include "MCRewardDropZone.h"
#include "MCDayPlan.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"

namespace
{
struct FRogueWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCGameState* GS=nullptr;
    AMCPlayerState* PS=nullptr;
    AMCToothCharacter* Hero=nullptr;
    UMCPerkComponent* P=nullptr;
    FRogueWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        GS=World->SpawnActor<AMCGameState>();World->SetGameState(GS);
        GS->Phase=EMCShiftPhase::Working;GS->bLobbyWaiting=false;GS->RunSeed=1041;
        GS->DayPlan=NewObject<UMCDayPlan>();GS->bSingleDayLoop=true;
        GS->RogueOptionalEvents={TEXT("IceEvent"),TEXT("FogBrawl"),TEXT("NutEncounter"),TEXT("NutBossKnight"),TEXT("CoffeeWave"),TEXT("Mimic"),TEXT("NightMeal")};
        auto* PC=World->SpawnActor<AMCPlayerController>();PS=World->SpawnActor<AMCPlayerState>();
        PS->SetPlayerId(17);PC->SetPlayerState(PS);PS->SetOwner(PC);PC->bShowMouseCursor=false;GS->AddPlayerState(PS);
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,10000),FRotator::ZeroRotator);
        PC->Possess(Hero);Hero->GetCharacterMovement()->DisableMovement();Hero->Status->Initialize(100);P=PS->Perks;
    }
    ~FRogueWorld() {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    AMCRewardChest* Chest(EMCRogueRewardKind Kind,int32 Seed)
    {
        auto* Zone=World->SpawnActor<AMCRewardDropZone>();
        auto* C=World->SpawnActor<AMCRewardChest>(FVector(0,0,10000),FRotator::ZeroRotator);
        C->RogueKind=Kind;C->InitializeReward(C->GetActorLocation(),C->GetActorLocation(),Seed,P->GetPerkTable(),EMCRewardSelectionPolicy::ChooseOne,Zone);
        C->ForceMimicForTest(false);C->Stage=EMCRewardChestStage::Landed;
        const auto Contact=C->GetLockpickContact();FVector At=Contact.GetLocation()+Contact.GetUnitAxis(EAxis::X)*120;At.Z=10061;
        Hero->SetActorLocation(At,false,nullptr,ETeleportType::TeleportPhysics);return C;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCRogueCatalogTest,"MessControl.Rogue.CatalogAndEligibility",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCRogueCatalogTest::RunTest(const FString&)
{
    FRogueWorld W;int32 Counts[3]={0,0,0};
    if(!TestNotNull(TEXT("Saved perk table loads"),W.P->GetPerkTable())) return false;
    for(FName ID:W.P->GetPerkTable()->GetRowNames()) if(const auto* Row=W.P->FindDefinition(ID);Row && Row->bRoguePerk && W.P->GetPerkKind(ID)==EMCPerkKind::Passive) {
        ++Counts[uint8(Row->Polarity)];
        TestTrue(ID.ToString()+TEXT(" has a concrete runtime effect"),Row->EffectClass==UMCRoguePerkEffect::StaticClass());
        TestTrue(ID.ToString()+TEXT(" has text, family and finite weight"),(!Row->DisplayName.IsEmpty() || !Row->DisplayNameKey.IsNone()) && (!Row->Description.IsEmpty() || !Row->DescriptionKey.IsNone()) && !Row->ChoiceFamily.IsNone() && FMath::IsFinite(Row->Weight) && Row->Weight>0);
    }
    TestTrue(TEXT("Saved catalog retains positive, negative and risky passive families"),Counts[0]>0 && Counts[1]>0 && Counts[2]>0);
    TestTrue(TEXT("Runtime component ticks for procs and pawn replacement"),W.P->PrimaryComponentTick.bCanEverTick);
    TestFalse(TEXT("Saw modifier gated before owning saw"),W.P->CanGrantPerk(TEXT("P11")));
    TestFalse(TEXT("Water ricochet gated before owning watergun"),W.P->CanGrantPerk(TEXT("P12")));
    TestFalse(TEXT("Team rescue excluded in solo"),W.P->CanGrantPerk(TEXT("P32")));
    W.GS->RogueOptionalEvents.Reset();
    TestFalse(TEXT("Ice perk excluded from route without ice"),W.P->CanGrantPerk(TEXT("P27")));
    TestFalse(TEXT("Night perk excluded from route without night"),W.P->CanGrantPerk(TEXT("P33")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCRogueMechanicsTest,"MessControl.Rogue.RealEffectsAndLimits",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCRogueMechanicsTest::RunTest(const FString&)
{
    FRogueWorld W;auto* Move=CastChecked<UMCToothMovementComponent>(W.Hero->GetCharacterMovement());
    W.Hero->Status->Damage(30);
    TestTrue(TEXT("Enamel granted"),W.P->ServerGrantPerk(TEXT("P25")));
    TestEqual(TEXT("Max HP actually changes"),W.Hero->Status->State.MaxHealth,115.f);
    TestEqual(TEXT("Max HP grant never heals"),W.Hero->Status->State.Health,70.f);
    W.P->ServerGrantPerk(TEXT("P02"));TestEqual(TEXT("Actual stamina capacity changes"),Move->GetMaxStamina(),115.f);
    TestTrue(TEXT("Unique speed perk granted"),W.P->ServerGrantPerk(TEXT("P01")));
    Move->SetMovementMode(MOVE_Falling);
    TestTrue(TEXT("Movement speed receives one unique modifier"),FMath::IsNearlyEqual(Move->GetMaxSpeed(),Move->WalkSpeed*1.08f,.01f));
    TestFalse(TEXT("Owned ID rejects a duplicate"),W.P->ServerGrantPerk(TEXT("P01")));
    TestTrue(TEXT("Different carry modifiers combine"),W.P->ServerGrantPerks({TEXT("P20"),TEXT("X11")}));
    TestEqual(TEXT("Unique carry modifiers grant three extra pieces"),W.Hero->FoodCollection->EffectiveCapacity(),9);
    TestTrue(TEXT("Already owned carry perk excluded"),!W.P->CanGrantPerk(TEXT("P20")));
    W.P->ResetPerks();
    TestTrue(TEXT("Distinct damage modifiers combine"),W.P->ServerGrantPerks({TEXT("P07"),TEXT("X02"),TEXT("X01")}));
    auto* Enemy=W.World->SpawnActor<AMCNutEnemy>(FVector(900,0,10000),FRotator::ZeroRotator);
    Enemy->Health=1000;W.Hero->Inventory->Selected=EMCToolSlot::Pickaxe;
    Enemy->ReceiveToolDamage(100,W.Hero);
    TestTrue(TEXT("Real enemy damage combines each unique modifier once"),FMath::IsNearlyEqual(Enemy->Health,823.f,.01f));
    TestTrue(TEXT("Heavy head slows actual swing rate"),FMath::IsNearlyEqual(W.Hero->Inventory->RogueAttackRate(),.8f));
    W.P->ResetPerks();TestFalse(TEXT("Saw perk cannot be granted before its tool"),W.P->ServerGrantPerk(TEXT("P29")));
    // Owning the saw is a real eligibility requirement.
    TestEqual(TEXT("No unowned saw perk slipped through"),W.P->GetStacks(TEXT("P29")),0);
    W.P->ServerGrantPerk(TEXT("Tool_Chainsaw_Rare"));TestTrue(TEXT("Different saw modifiers granted"),W.P->ServerGrantPerks({TEXT("P29"),TEXT("X03")}));
    TestEqual(TEXT("Saw crash flat costs combine once per ID"),W.P->RogueValue(EMCRogueStat::ChainsawCrashDamage,30),40.f);
    W.P->ResetPerks();W.P->ServerGrantPerk(TEXT("P18"));
    TestEqual(TEXT("Unique warm perk adds one second"),W.P->RogueValue(EMCRogueStat::WarmDuration,12),13.f);
    W.P->ServerGrantPerk(TEXT("X07"));TestTrue(TEXT("Risky warmth cost applied"),FMath::IsNearlyEqual(W.P->RogueValue(EMCRogueStat::WarmDuration,12),7.8f,.001f));
    W.P->ResetPerks();
    TestFalse(TEXT("An XP-only perk is excluded from every live reward pool"),W.P->CanGrantPerk(TEXT("P24")));
    TestFalse(TEXT("A saved XP perk cannot be granted after retiring experience"),W.P->ServerGrantPerk(TEXT("P24")));
    int32 Bonus=0;for(int32 I=0;I<10;++I) Bonus+=W.P->FoodExperienceBonus(1);
    TestEqual(TEXT("Legacy food XP bonus calls cannot restore experience"),Bonus,0);
    W.P->ResetPerks();W.Hero->Status->Initialize(100);W.Hero->Status->Damage(20);
    W.P->ServerGrantPerks({TEXT("P30"),TEXT("P17")});W.P->NotifyCleaned(W.Hero->GetActorLocation());W.P->NotifyCleaned(W.Hero->GetActorLocation());
    TestEqual(TEXT("Cleaning heals once per cooldown"),W.Hero->Status->State.Health,83.f);
    TestTrue(TEXT("Cleaning speed burst works"),FMath::IsNearlyEqual(W.P->RogueValue(EMCRogueStat::NormalSpeed),1.1f));
    W.P->ResetPerks();TestEqual(TEXT("Reset removes burst"),W.P->RogueValue(EMCRogueStat::NormalSpeed),1.f);
    TestFalse(TEXT("Duplicate-ID curse batch rejected atomically"),W.P->ServerGrantPerks({TEXT("N01"),TEXT("N01"),TEXT("N02"),TEXT("N03"),TEXT("P25")}));
    TestEqual(TEXT("Failed batch did not grant reward"),W.P->GetStacks(TEXT("P25")),0);
    TestEqual(TEXT("Failed batch did not grant curse"),W.P->GetStacks(TEXT("N01")),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCRogueOffersTest,"MessControl.Rogue.ChestOfferExclusionsAndRarities",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCRogueOffersTest::RunTest(const FString&)
{
    FRogueWorld W;
    for(int32 Seed=0;Seed<256;++Seed) {
        TArray<FName> IDs;TestTrue(TEXT("Three eligible cards exist"),W.P->BuildRogueOffer(EMCPerkPolarity::Positive,Seed,IDs,false,false,Seed%5==0));
        TSet<FName> Families;int32 Legendary=0,Rare=0;
        for(FName ID:IDs) {const auto* R=W.P->FindDefinition(ID);Families.Add(R->ChoiceFamily.IsNone()?ID:R->ChoiceFamily);Legendary+=R->Rarity==EMCPerkRarity::Legendary;Rare+=R->Rarity==EMCPerkRarity::Rare;}
        TestEqual(TEXT("Distinct families"),Families.Num(),3);TestTrue(TEXT("At most one legendary"),Legendary<=1);
        if(Seed%5==0) TestTrue(TEXT("Fifth choice contains rare positive"),Rare>=1);
    }
    // The reusable offer builder still validates deterministic families and
    // caller-supplied history. It no longer needs an XP queue or a free card UI.
    TArray<FName> First,Repeated,Second,Third;
    if(!TestTrue(TEXT("An explicit early offer has three cards"),W.P->BuildRogueOffer(EMCPerkPolarity::Positive,101,First,true))) return false;
    W.P->BuildRogueOffer(EMCPerkPolarity::Positive,101,Repeated,true);
    TestTrue(TEXT("The same seed produces the same authored offer"),First==Repeated);
    for(FName ID:First) {const auto* Row=W.P->FindDefinition(ID);TestTrue(TEXT("Explicit early cards remain universal standard positive"),
        Row->bEarlyChoice && Row->Rarity==EMCPerkRarity::Standard && Row->Polarity==EMCPerkPolarity::Positive);}
    TArray<FName> Excluded={First[1],First[2]};
    if(!TestTrue(TEXT("An explicit next offer respects supplied rejected cards"),W.P->BuildRogueOffer(EMCPerkPolarity::Positive,102,Second,true,false,false,Excluded))) return false;
    for(FName ID:Excluded) TestFalse(TEXT("Rejected cards are absent from the next offer"),Second.Contains(ID));
    Excluded.Append({Second[1],Second[2]});
    if(!TestTrue(TEXT("An explicit third offer accepts two history entries"),W.P->BuildRogueOffer(EMCPerkPolarity::Positive,103,Third,true,false,false,Excluded))) return false;
    for(int32 I=1;I<3;++I) TestFalse(TEXT("First rejected cards remain absent across two explicit offers"),Third.Contains(First[I]));
    W.PS->QueueChoicesThroughLevel(4);
    TestFalse(TEXT("A legacy queue call cannot open automatic cards"),W.PS->LevelUpOffer.IsValid());
    TestFalse(TEXT("There is no free level reroll outside a chest"),W.PS->TryRerollLevelPerks(FGuid::NewGuid()));
    TestTrue(TEXT("Building a preview never grants a perk"),W.P->ActivePerks.IsEmpty());
    W.P->ResetPerks();
    TestTrue(TEXT("Three risky perks allowed"),W.P->ServerGrantPerks({TEXT("X01"),TEXT("X02"),TEXT("X04")}));
    TestTrue(TEXT("Fourth different risky perk is allowed"),W.P->ServerGrantPerk(TEXT("X11")));
    W.P->ResetPerks();
    // Exhaust the currently useful passive/tool catalog through the real grant API.
    // A second pass resolves passives which became eligible after acquiring tools.
    for(int32 Pass=0;Pass<2;++Pass) for(FName ID:W.P->GetPerkTable()->GetRowNames())
        if(const auto* Row=W.P->FindDefinition(ID);Row && Row->Polarity==EMCPerkPolarity::Positive
            && (W.P->GetPerkKind(ID)==EMCPerkKind::Passive || W.P->GetPerkKind(ID)==EMCPerkKind::ToolUpgrade)
            && W.P->CanAcquirePerk(ID)) W.P->TryAcquirePerk(ID);
    W.PS->PersonalChoicesTaken=20;
    auto* Fallback=W.Chest(EMCRogueRewardKind::Ordinary,104);
    if(!TestTrue(TEXT("An exhausted passive pool still opens a real item reward"),Fallback->BeginLockpicking(W.Hero))) return false;
    if(!TestTrue(TEXT("One to three remaining eligible item cards"),Fallback->LootIDs.Num()>0 && Fallback->LootIDs.Num()<=3)) return false;
    for(FName ID:Fallback->LootIDs) {
        const auto Kind=W.P->GetPerkKind(ID);
        TestTrue(TEXT("Remaining cards use active slots"),Kind==EMCPerkKind::Active || Kind==EMCPerkKind::Consumable);
        TestFalse(TEXT("Currently owned IDs never return in the offer"),W.P->OwnsPerk(ID));
    }
    const FName Selected=Fallback->LootIDs[0];
    Fallback->Stage=EMCRewardChestStage::Open;
    TestTrue(TEXT("A remaining item is selected from its chest"),Fallback->TryChooseCard(W.Hero,0));
    TestTrue(TEXT("Chest selection stores the selected canonical item"),W.P->OwnsPerk(Selected));
    TestTrue(TEXT("Stored item occupies one active slot"),W.P->ActiveSkillSlots.ContainsByPredicate([Selected](const FMCActiveSkillSlot& Slot){return Slot.PerkID==Selected;}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCRogueContractTest,"MessControl.Rogue.ContractAtomicityAndBoosterActivation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCRogueContractTest::RunTest(const FString&)
{
    FRogueWorld W;W.PS->PersonalChoicesTaken=2;
    auto* Cancel=W.Chest(EMCRogueRewardKind::CursedContract,51);
    if(!TestTrue(TEXT("Real cursed chest opens"),Cancel->BeginLockpicking(W.Hero))) return false;
    Cancel->Stage=EMCRewardChestStage::Open;
    TestEqual(TEXT("Both triples fixed before decision"),Cancel->ContractCurses.Num()+Cancel->ContractRewards.Num(),6);
    TestTrue(TEXT("Choose curse preview"),Cancel->TryChooseCard(W.Hero,0));TestEqual(TEXT("No curse applied before confirmation"),W.P->ActivePerks.Num(),0);
    TestTrue(TEXT("Choose reward preview"),Cancel->TryChooseCard(W.Hero,0));TestEqual(TEXT("No reward applied before confirmation"),W.P->ActivePerks.Num(),0);
    TestTrue(TEXT("Decline succeeds"),Cancel->DeclineRogueReward(W.Hero));TestEqual(TEXT("Decline grants neither"),W.P->ActivePerks.Num(),0);Cancel->Destroy();
    auto* Confirm=W.Chest(EMCRogueRewardKind::CursedContract,52);
    if(!TestTrue(TEXT("Second cursed chest opens"),Confirm->BeginLockpicking(W.Hero))) return false;
    Confirm->Stage=EMCRewardChestStage::Open;Confirm->TryChooseCard(W.Hero,0);Confirm->TryChooseCard(W.Hero,0);
    const FName Curse=Confirm->ContractCurse,Reward=Confirm->ContractReward;
    TestTrue(TEXT("Final pair commits together"),Confirm->ConfirmRogueContract(W.Hero));
    TestEqual(TEXT("One curse"),W.P->GetStacks(Curse),1);TestEqual(TEXT("One reward"),W.P->GetStacks(Reward),1);
    TestFalse(TEXT("Consumed contract cannot replay"),Confirm->ConfirmRogueContract(W.Hero));Confirm->Destroy();
    W.P->ResetPerks();
    TestTrue(TEXT("Fallback grants charge"),W.P->ServerGrantPerk(TEXT("Fallback_Grenade")));
    TestEqual(TEXT("Charge queued exactly once"),W.P->BoosterQueue.Num(),1);TestFalse(TEXT("Grant does not arm grenade"),W.P->bGrenadeArmed);
    TestTrue(TEXT("Explicit slot input arms grenade"),W.P->ActivateSkillSlot(0,W.P->ActiveSkillSlots[0].InstanceID));TestTrue(TEXT("Armed flag visible"),W.P->bGrenadeArmed);
    W.Hero->Status->Initialize(100);W.P->OnRogueKnockdown();TestFalse(TEXT("Explosion consumes armed flag first"),W.P->bGrenadeArmed);
    TestEqual(TEXT("Grenade damages owner"),W.Hero->Status->State.Health,40.f);
    W.P->ServerGrantPerk(TEXT("Fallback_BrushFury"));TestTrue(TEXT("Fury explicitly activates its owned slot"),W.P->ActivateSkillSlot(0,W.P->ActiveSkillSlots[0].InstanceID));
    TestTrue(TEXT("Fury lasts across input release"),W.P->IsFuryActive());W.P->TickRogueBoosters(.1f);
    TestTrue(TEXT("Fury creates forced root movement"),W.Hero->GetCharacterMovement()->GetRootMotionSource(TEXT("MCRogueFury")).IsValid());
    W.P->ResetPerks();TestFalse(TEXT("New run stops fury"),W.P->IsFuryActive());TestEqual(TEXT("New run clears charges"),W.P->BoosterQueue.Num(),0);
    return true;
}
#endif
