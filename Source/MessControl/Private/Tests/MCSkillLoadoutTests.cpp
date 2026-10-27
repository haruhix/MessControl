#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCPerkComponent.h"
#include "MCPerkEffect.h"
#include "MCRoguePerkEffect.h"
#include "MCPlayerState.h"
#include "MCPlayerController.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCHubEconomyComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"

namespace
{
struct FSkillWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCGameState* GS=nullptr;
    AMCPlayerController* PC=nullptr;
    AMCPlayerState* PS=nullptr;
    AMCToothCharacter* Hero=nullptr;
    UDataTable* Table=nullptr;
    UMCPerkComponent* P=nullptr;
    FSkillWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        GS=World->SpawnActor<AMCGameState>();World->SetGameState(GS);
        GS->Phase=EMCShiftPhase::Working;GS->bLobbyWaiting=false;
        Table=NewObject<UDataTable>(World);Table->RowStruct=FMCPerkDefinition::StaticStruct();
        for(int32 I=0;I<10;++I) {
            FMCPerkDefinition Row;Row.Kind=EMCPerkKind::Passive;Row.bRoguePerk=true;
            Row.EffectClass=UMCRoguePerkEffect::StaticClass();Row.MaxStacks=1;
            FMCRogueModifier M;M.Stat=EMCRogueStat::MaxHealth;M.Value=5;M.bFlat=true;Row.Modifiers={M};
            Table->AddRow(FName(*FString::Printf(TEXT("Passive%d"),I)),Row);
        }
        for(int32 I=0;I<6;++I) {
            FMCPerkDefinition Row;Row.Kind=EMCPerkKind::Active;Row.MaxStacks=1;
            Table->AddRow(FName(*FString::Printf(TEXT("Active%d"),I)),Row);
        }
        FMCPerkDefinition Heal;Heal.Kind=EMCPerkKind::Consumable;Heal.InitialCharges=3;
        Heal.ActivationAdapterID=TEXT("RecoverySelfHeal");Table->AddRow(TEXT("HealPack"),Heal);
        FMCPerkDefinition Legacy;Legacy.bConsumableBooster=true;Legacy.InitialCharges=9;
        Table->AddRow(TEXT("LegacyPack"),Legacy);
        FMCPerkDefinition Self;Self.Kind=EMCPerkKind::Active;Self.CooldownSeconds=5;
        Self.ActivationAdapterID=TEXT("RecoverySelfHeal");Table->AddRow(TEXT("ActiveHeal"),Self);
        FMCPerkDefinition Mouth=Self;Mouth.ActivationAdapterID=TEXT("RecoveryMouthHeal");Mouth.CooldownSeconds=3;
        Table->AddRow(TEXT("ActiveMouth"),Mouth);
        FMCPerkDefinition Tool;Tool.Kind=EMCPerkKind::ToolUpgrade;Tool.ToolUpgrade=EMCToolUpgrade::Chainsaw;
        Tool.Rarity=EMCPerkRarity::Rare;Table->AddRow(TEXT("TestSaw"),Tool);
        FMCPerkDefinition SawPassive;SawPassive.Kind=EMCPerkKind::Passive;SawPassive.bRoguePerk=true;
        SawPassive.RequiredTool=EMCToolUpgrade::Chainsaw;Table->AddRow(TEXT("SawPassive"),SawPassive);
        PC=World->SpawnActor<AMCPlayerController>();
        PS=World->SpawnActorDeferred<AMCPlayerState>(AMCPlayerState::StaticClass(),FTransform::Identity,PC);
        PS->Perks->PerkTable=Table;PS->FinishSpawning(FTransform::Identity);
        PC->SetPlayerState(PS);PS->SetOwner(PC);GS->AddPlayerState(PS);P=PS->Perks;
        SpawnHero();
    }
    void SpawnHero()
    {
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,10000),FRotator::ZeroRotator);
        PC->Possess(Hero);Hero->GetCharacterMovement()->DisableMovement();Hero->Status->Initialize(100);
        PC->bShowMouseCursor=false;
    }
    void Health(float HP) {FMCToothStatus S=Hero->Status->State;S.Health=HP;Hero->Status->Restore(S);}
    ~FSkillWorld() {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSkillLoadoutRulesTest,"MessControl.Skills.AcquisitionAndIdentity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSkillLoadoutRulesTest::RunTest(const FString&)
{
    FSkillWorld W;
    TestEqual(TEXT("Exactly four empty slots"),W.P->ActiveSkillSlots.Num(),4);
    for(int32 I=0;I<9;++I) TestTrue(TEXT("Different passives coexist without an eight-item cap"),W.P->TryAcquirePerk(FName(*FString::Printf(TEXT("Passive%d"),I))));
    TestEqual(TEXT("Nine unique passive buffs"),W.P->PassivePerkIDs.Num(),9);
    TestEqual(TEXT("Different passive modifiers combine"),W.Hero->Status->State.MaxHealth,145.f);
    TestEqual(TEXT("Acquiring max health never grants a free heal"),W.Hero->Status->State.Health,100.f);
    TestFalse(TEXT("The same passive cannot stack"),W.P->ServerGrantPerk(TEXT("Passive0")));
    TestEqual(TEXT("Passive compatibility stack is always one"),W.P->GetStacks(TEXT("Passive0")),1);
    for(int32 I=0;I<4;++I) TestTrue(TEXT("Equip four actives"),W.P->TryAcquirePerk(FName(*FString::Printf(TEXT("Active%d"),I))));
    TestTrue(TEXT("A full loadout still receives replacement offers"),W.P->CanOfferPerk(TEXT("Active4")));
    TestFalse(TEXT("A full loadout cannot auto-overwrite"),W.P->TryAcquirePerk(TEXT("Active4")));
    TestFalse(TEXT("Legacy single grant cannot bypass capacity"),W.P->ServerGrantPerk(TEXT("Active4")));
    const FGuid First=W.P->ActiveSkillSlots[0].InstanceID;
    TestTrue(TEXT("Explicit destination query works"),W.P->CanAcquirePerk(TEXT("Active4"),0));
    TestFalse(TEXT("Occupied destination requires both replacement guards"),W.P->TryAcquirePerk(TEXT("Active4"),0));
    TestFalse(TEXT("ID without instance guard is rejected"),W.P->TryAcquirePerk(TEXT("Active4"),0,TEXT("Active0")));
    TestTrue(TEXT("Matching instance replaces chosen slot"),W.P->TryAcquirePerk(TEXT("Active4"),0,TEXT("Active0"),First));
    TestTrue(TEXT("Replaced ID is eligible again"),W.P->CanOfferPerk(TEXT("Active0")));
    const FGuid Second=W.P->ActiveSkillSlots[0].InstanceID;
    TestTrue(TEXT("Original ID can be reacquired as a new instance"),W.P->TryAcquirePerk(TEXT("Active0"),0,TEXT("Active4"),Second));
    TestTrue(TEXT("New instance has new identity"),W.P->ActiveSkillSlots[0].InstanceID!=First);
    TestFalse(TEXT("ABA stale instance cannot replace new item of same ID"),W.P->TryAcquirePerk(TEXT("Active4"),0,TEXT("Active0"),First));
    TestFalse(TEXT("Stale use cannot execute new instance"),W.P->ActivateSkillSlot(0,First));
    FMCPerkGrant A;A.PerkID=TEXT("Passive9");FMCPerkGrant Bad;Bad.PerkID=TEXT("MissingRow");
    TestFalse(TEXT("Prepared batch rejects atomically"),W.P->TryAcquirePerkBatch({A,Bad}));
    TestFalse(TEXT("No earlier entry was partially granted"),W.P->OwnsPerk(TEXT("Passive9")));
    FMCPerkGrant Tool;Tool.PerkID=TEXT("TestSaw");FMCPerkGrant NeedsSaw;NeedsSaw.PerkID=TEXT("SawPassive");
    TestTrue(TEXT("Prospective tool ownership validates same-batch passive"),W.P->TryAcquirePerkBatch({NeedsSaw,Tool}));
    TestEqual(TEXT("Tools use no skill slot"),W.P->ActiveSkillSlots.Num(),4);
    TestTrue(TEXT("Tool replacement channel preserved"),W.P->HasToolUpgrade(EMCToolUpgrade::Chainsaw));
    W.PS->Economy->Coins=77;W.P->ResetPerks();
    TestEqual(TEXT("Run reset clears passive buffs"),W.P->PassivePerkIDs.Num(),0);
    TestEqual(TEXT("Run reset preserves wallet"),W.PS->Economy->Coins,77);
    TestEqual(TEXT("Run reset frees all four slots"),W.P->FindFreeActiveSlot(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSkillChargesTest,"MessControl.Skills.ChargesAndUseGuards",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSkillChargesTest::RunTest(const FString&)
{
    FSkillWorld W;
    TestTrue(TEXT("Stored heal may be acquired while healthy"),W.P->TryAcquirePerk(TEXT("HealPack")));
    const FGuid Pack=W.P->ActiveSkillSlots[0].InstanceID;
    TestEqual(TEXT("Authored initial charges"),W.P->ActiveSkillSlots[0].Charges,3);
    TestFalse(TEXT("Full health rejects no-op without consuming"),W.P->ActivateSkillSlot(0,Pack));
    TestEqual(TEXT("No-op preserves charges"),W.P->ActiveSkillSlots[0].Charges,3);
    W.Health(20);
    TestTrue(TEXT("Accepted self heal uses one charge"),W.P->ActivateSkillSlot(0,Pack));
    TestEqual(TEXT("Known legacy amount retained"),W.Hero->Status->State.Health,45.f);
    TestEqual(TEXT("Exactly one charge spent"),W.P->ActiveSkillSlots[0].Charges,2);
    W.PC->SetIgnoreMoveInput(true);
    TestFalse(TEXT("Menu/input lock rejects activation"),W.P->ActivateSkillSlot(0,Pack));W.PC->ResetIgnoreMoveInput();
    TestEqual(TEXT("Input lock does not debit"),W.P->ActiveSkillSlots[0].Charges,2);
    W.Hero->Status->State.Health=0;
    TestFalse(TEXT("Dead owner cannot use stored consumable"),W.P->ActivateSkillSlot(0,Pack));
    W.SpawnHero();W.Health(1);
    TestEqual(TEXT("Pawn replacement preserves remaining charges"),W.P->ActiveSkillSlots[0].Charges,2);
    TestTrue(TEXT("Replacement pawn can use remaining charge"),W.P->ActivateSkillSlot(0,Pack));
    TestTrue(TEXT("Final accepted charge executes"),W.P->ActivateSkillSlot(0,Pack));
    TestTrue(TEXT("Depleted slot clears"),W.P->ActiveSkillSlots[0].PerkID.IsNone());
    TestTrue(TEXT("Fully spent ID may be offered again"),W.P->CanOfferPerk(TEXT("HealPack")));
    TestTrue(TEXT("Legacy queue groups exact unit counts"),W.P->ServerGrantPerks({TEXT("LegacyPack"),TEXT("LegacyPack"),TEXT("LegacyPack")}));
    TestEqual(TEXT("Three old units do not multiply by authored nine"),W.P->ActiveSkillSlots[0].Charges,3);
    TestFalse(TEXT("Placeholder active rejects default execution"),W.P->ActivateSkillSlot(0,W.P->ActiveSkillSlots[0].InstanceID));
    TestEqual(TEXT("Rejected generic action does not debit charges"),W.P->ActiveSkillSlots[0].Charges,3);
    FMCPerkDefinition Automatic;Automatic.bConsumableBooster=true;
    TestTrue(TEXT("Old booster flag resolves to consumable"),UMCPerkComponent::ResolvePerkKind(Automatic)==EMCPerkKind::Consumable);
    Automatic.bConsumableBooster=false;
    TestTrue(TEXT("Old recovery row resolves to stored consumable"),UMCPerkComponent::ResolvePerkKind(Automatic,TEXT("RecoverySelfHeal"))==EMCPerkKind::Consumable);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSkillCooldownTest,"MessControl.Skills.IndependentCooldownAndTravel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSkillCooldownTest::RunTest(const FString&)
{
    FSkillWorld W;
    TestTrue(TEXT("First reusable active equipped"),W.P->TryAcquirePerk(TEXT("ActiveHeal")));
    TestTrue(TEXT("Second reusable active equipped"),W.P->TryAcquirePerk(TEXT("ActiveMouth")));
    W.Health(10);W.GS->MouthHealth=10;
    const FGuid First=W.P->ActiveSkillSlots[0].InstanceID,Second=W.P->ActiveSkillSlots[1].InstanceID;
    TestTrue(TEXT("First active starts its cooldown"),W.P->ActivateSkillSlot(0,First));
    TestFalse(TEXT("Same active cannot be spammed during cooldown"),W.P->ActivateSkillSlot(0,First));
    TestTrue(TEXT("Other active remains independently available"),W.P->ActivateSkillSlot(1,Second));
    TestEqual(TEXT("Reusable active has no charges to refill"),W.P->ActiveSkillSlots[0].Charges,-1);
    W.World->TimeSeconds+=2.;
    TestTrue(TEXT("First cooldown has three seconds left"),FMath::IsNearlyEqual(W.P->GetSkillCooldownRemaining(0),3.f));
    TestTrue(TEXT("Second cooldown has one second left"),FMath::IsNearlyEqual(W.P->GetSkillCooldownRemaining(1),1.f));
    FSkillWorld Destination;Destination.World->TimeSeconds=100.;
    W.P->CopyPerksTo(Destination.P);
    TestTrue(TEXT("Travel preserves remaining first cooldown across world clocks"),FMath::IsNearlyEqual(Destination.P->GetSkillCooldownRemaining(0),3.f));
    TestTrue(TEXT("Travel preserves independent second cooldown"),FMath::IsNearlyEqual(Destination.P->GetSkillCooldownRemaining(1),1.f));
    TestTrue(TEXT("Travel preserves runtime instance identity"),Destination.P->ActiveSkillSlots[0].InstanceID==First);
    Destination.World->TimeSeconds+=1.01;
    TestTrue(TEXT("Second active recovers first"),Destination.P->GetSkillCooldownRemaining(1)<=0);
    TestTrue(TEXT("First active continues its own cooldown"),Destination.P->GetSkillCooldownRemaining(0)>1.9f);
    return true;
}
#endif
