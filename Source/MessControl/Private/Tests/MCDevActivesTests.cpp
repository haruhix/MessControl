#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "MCDevCommands.h"
#include "MCActiveSkillTestTypes.h"
#include "MCAirSkillAction.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCHubEconomyComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/Package.h"

namespace MCDevActivesTests
{
struct FPlayer
{
    AMCPlayerController* Controller=nullptr;
    AMCPlayerState* State=nullptr;
    UMCPerkComponent* Perks=nullptr;
    AMCToothCharacter* Hero=nullptr;
    FMCActiveSkillInputHandle Press(int32 Slot,uint32 Sequence) const
    {FMCActiveSkillInputHandle H;H.SlotIndex=Slot;H.SlotInstanceID=Perks->ActiveSkillSlots[Slot].InstanceID;H.PressSequence=Sequence;return H;}
};
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    UPackage* Package=World->GetOutermost();
    const FName PackageName=Package->GetFName();
    AMCGameMode* Mode=nullptr;
    AMCGameState* State=nullptr;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/MessControl.MCGameMode"));URL.AddOption(TEXT("Seed=41"));
        World->SetGameMode(URL);Mode=World->GetAuthGameMode<AMCGameMode>();
        Mode->bUseDayOnePlan=false;Mode->bUseAdaptiveDirector=false;Mode->bUseSingleDayLoop=false;
        World->InitializeActorsForPlay(URL);World->BeginPlay();
        State=World->GetGameState<AMCGameState>();
        State->Phase=EMCShiftPhase::Working;State->bLobbyWaiting=false;State->bTutorialActive=false;
    }
    FPlayer Player(UDataTable* Table=nullptr,bool Local=true,bool WithHero=false)
    {
        FPlayer P;P.Controller=World->SpawnActor<AMCPlayerController>();
        if(Local) P.Controller->SetAsLocalPlayerController();
        P.State=World->SpawnActorDeferred<AMCPlayerState>(AMCPlayerState::StaticClass(),FTransform::Identity,P.Controller);
        if(Table) P.State->Perks->PerkTable=Table;
        P.State->FinishSpawning(FTransform::Identity);P.Controller->SetPlayerState(P.State);
        P.State->SetOwner(P.Controller);State->AddPlayerState(P.State);P.Perks=P.State->Perks;
        if(WithHero) {
            P.Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,10000),FRotator::ZeroRotator);
            P.Controller->Possess(P.Hero);P.Hero->GetCharacterMovement()->DisableMovement();P.Hero->Status->Initialize(100);
            P.Controller->bShowMouseCursor=false;
        }
        return P;
    }
    UDataTable* Catalog()
    {
        auto* T=NewObject<UDataTable>(World);T->RowStruct=FMCPerkDefinition::StaticStruct();
        FMCPerkDefinition Row;Row.Kind=EMCPerkKind::Consumable;Row.InitialCharges=3;Row.CooldownSeconds=30;
        Row.DisplayName=FText::FromString(TEXT("Dev test"));Row.EffectClass=UMCActiveSkillProtocolProbeEffect::StaticClass();
        T->AddRow(TEXT("DevCharge"),Row);T->AddRow(TEXT("DevOtherCharge"),Row);
        Row.EffectClass=UMCAirSkillEffect::StaticClass();T->AddRow(TEXT("DevAir"),Row);
        Row.Kind=EMCPerkKind::Active;Row.EffectClass=UMCActiveSkillProtocolProbeEffect::StaticClass();
        T->AddRow(TEXT("DevReusable"),Row);
        Row.Kind=EMCPerkKind::Passive;Row.EffectClass=nullptr;T->AddRow(TEXT("DevPassive"),Row);
        Row.Kind=EMCPerkKind::ToolUpgrade;Row.ToolUpgrade=EMCToolUpgrade::MeshaBrush;T->AddRow(TEXT("DevTool"),Row);
        return T;
    }
    ~FWorld()
    {
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
        if(IsValid(Package) && Package->GetFName()==PackageName && Package->GetName().StartsWith(TEXT("/Temp/"))) Package->SetDirtyFlag(false);
    }
};
bool SameSlot(const FMCActiveSkillSlot& A,const FMCActiveSkillSlot& B)
{return A.PerkID==B.PerkID && A.InstanceID==B.InstanceID && A.Charges==B.Charges && A.CooldownEndsAt==B.CooldownEndsAt;}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDevActivesCatalogTest,"MessControl.Skills.DevPanel.CanonicalCatalogAndExplicitDestination",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDevActivesCatalogTest::RunTest(const FString&)
{
    using namespace MCDevActivesTests;
    FWorld F;const auto Host=F.Player();
    if(!TestTrue(TEXT("The ordinary host guard accepts this controller"),F.Mode->CanUseDevPanel(Host.Controller))
        || !TestNotNull(TEXT("Canonical saved table loaded"),Host.Perks->GetPerkTable())) return false;
    const TArray<FName> IDs=MCDevActiveIDs(Host.Perks);
    const TArray<FName> Expected={TEXT("Active_Sponge"),TEXT("Active_Hook"),TEXT("Consumable_Grenade"),TEXT("Consumable_Air"),TEXT("Consumable_Rocket"),TEXT("Consumable_Driver")};
    if(!TestEqual(TEXT("All six implemented skills are present"),IDs.Num(),Expected.Num())) return false;
    TSet<int32> Tokens;
    for(int32 I=0;I<IDs.Num();++I) {
        TestTrue(TEXT("No passive, weapon upgrade or retired definition is listed"),Expected.Contains(IDs[I]));
        if(I) TestTrue(TEXT("Stable lexical order"),IDs[I-1].LexicalLess(IDs[I]));
        Tokens.Add(MCDevActiveToken(IDs[I]));
    }
    TestEqual(TEXT("Distinct stable tokens for the current catalog"),Tokens.Num(),IDs.Num());
    TestEqual(TEXT("Empty ID has no token"),MCDevActiveToken(NAME_None),INDEX_NONE);
    TestEqual(TEXT("Invalid slot never clamps to a valid slot"),MCDevActiveGrantSlot(MCDevActiveGrantAction(4)),INDEX_NONE);
    TestEqual(TEXT("Negative slot never becomes slot zero"),MCDevActiveGrantSlot(MCDevActiveGrantAction(-1)),INDEX_NONE);
    for(int32 Slot=0;Slot<4;++Slot) {
        TestEqual(TEXT("Grant action round-trips exact slot"),MCDevActiveGrantSlot(MCDevActiveGrantAction(Slot)),Slot);
        F.Mode->ExecuteDevAction(Host.Controller,MCDevActiveGrantAction(Slot),MCDevActiveToken(IDs[Slot]));
        TestEqual(TEXT("Grant fills the explicitly selected destination"),Host.Perks->ActiveSkillSlots[Slot].PerkID,IDs[Slot]);
    }
    const auto Before=Host.Perks->ActiveSkillSlots;
    F.Mode->ExecuteDevAction(Host.Controller,MCDevActiveGrantAction(3),MCDevActiveToken(IDs[0]));
    for(int32 Slot=0;Slot<4;++Slot) TestTrue(TEXT("Duplicate ownership leaves every slot unchanged"),SameSlot(Before[Slot],Host.Perks->ActiveSkillSlots[Slot]));
    TestTrue(TEXT("Owned definitions remain listed without retargeting commands"),MCDevActiveIDs(Host.Perks)==IDs);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDevActivesValidationTest,"MessControl.Skills.DevPanel.HostGuardAndInvalidStaleDefinitions",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDevActivesValidationTest::RunTest(const FString&)
{
    using namespace MCDevActivesTests;
    FWorld F;auto* Table=F.Catalog();const auto Host=F.Player(Table);
    const int32 Token=MCDevActiveToken(TEXT("DevCharge"));const auto Grant=MCDevActiveGrantAction(2);
    F.Mode->ExecuteDevAction(nullptr,Grant,Token);
    const auto Remote=F.Player(Table,false);TestFalse(TEXT("Remote controller cannot issue host commands"),F.Mode->CanUseDevPanel(Remote.Controller));
    F.Mode->ExecuteDevAction(Remote.Controller,Grant,Token);
    Host.Controller->SetRole(ROLE_SimulatedProxy);F.Mode->ExecuteDevAction(Host.Controller,Grant,Token);Host.Controller->SetRole(ROLE_Authority);
    {FWorld Other;const auto Foreign=Other.Player();F.Mode->ExecuteDevAction(Foreign.Controller,Grant,Token);
        TestFalse(TEXT("A different world's host is never mutated"),Foreign.Perks->ActiveSkillSlots.ContainsByPredicate([](const auto& S){return !S.PerkID.IsNone();}));}
    const int32 InvalidTokens[]={INDEX_NONE,MAX_int32};
    for(int32 Invalid:InvalidTokens) F.Mode->ExecuteDevAction(Host.Controller,Grant,Invalid);
    for(FName Wrong:{FName(TEXT("DevPassive")),FName(TEXT("DevTool"))})
        F.Mode->ExecuteDevAction(Host.Controller,Grant,MCDevActiveToken(Wrong));
    auto* Row=Table->FindRow<FMCPerkDefinition>(TEXT("DevCharge"),TEXT("Dev validation"),false);
    Row->bEnabled=false;F.Mode->ExecuteDevAction(Host.Controller,Grant,Token);
    Row->bEnabled=true;Row->InitialCharges=0;F.Mode->ExecuteDevAction(Host.Controller,Grant,Token);
    Row->InitialCharges=3;Row->EffectClass=UMCActiveSkillPerkEffect::StaticClass();F.Mode->ExecuteDevAction(Host.Controller,Grant,Token);
    Row->EffectClass=UMCActiveSkillProtocolProbeEffect::StaticClass();
    TestTrue(TEXT("A supported row is initially visible"),MCDevActiveIDs(Host.Perks).Contains(TEXT("DevCharge")));
    Table->RemoveRow(TEXT("DevCharge"));F.Mode->ExecuteDevAction(Host.Controller,Grant,Token);
    // These distinct lower-case IDs collide in FCrc::StrCrc32's 31-bit
    // command payload. Even an unlisted disabled collision must fail closed.
    const FName CollisionA(TEXT("devcollisionhdx28hfotvox")),CollisionB(TEXT("devcollisionw8dxjmh3v631"));
    if(!TestEqual(TEXT("Fixture exercises a real token collision"),MCDevActiveToken(CollisionA),MCDevActiveToken(CollisionB))) return false;
    FMCPerkDefinition Collision;Collision.Kind=EMCPerkKind::Consumable;Collision.InitialCharges=3;
    Collision.EffectClass=UMCActiveSkillProtocolProbeEffect::StaticClass();Table->AddRow(CollisionA,Collision);
    Collision.bEnabled=false;Table->AddRow(CollisionB,Collision);
    F.Mode->ExecuteDevAction(Host.Controller,Grant,MCDevActiveToken(CollisionA));
    for(int32 Slot=0;Slot<4;++Slot) {
        TestTrue(TEXT("Rejected commands cannot grant a sorted neighbour"),Host.Perks->ActiveSkillSlots[Slot].PerkID.IsNone());
        TestTrue(TEXT("Rejected commands cannot mutate a remote player's loadout"),Remote.Perks->ActiveSkillSlots[Slot].PerkID.IsNone());
    }
    F.Mode->ExecuteDevAction(Host.Controller,Grant,MCDevActiveToken(TEXT("DevReusable")));
    const auto Before=Host.Perks->ActiveSkillSlots[2];
    for(int32 Invalid:{-1,4,MAX_int32}) for(auto Action:{EMCDevAction::RemoveActiveSlot,EMCDevAction::RefillActiveSlot,EMCDevAction::ResetActiveCooldown})
        F.Mode->ExecuteDevAction(Host.Controller,Action,Invalid);
    TestTrue(TEXT("Utility commands reject invalid slots without clamping"),SameSlot(Before,Host.Perks->ActiveSkillSlots[2]));
    TestFalse(TEXT("Canonical refresh refuses stale instance identity"),Host.Perks->DebugRefreshActiveSlot(2,FGuid::NewGuid(),false,true));
    TestFalse(TEXT("Canonical remove refuses stale instance identity"),Host.Perks->DebugRemoveActiveSlot(2,FGuid::NewGuid()));
    TestFalse(TEXT("Reusable skills cannot be converted into charged consumables"),Host.Perks->DebugRefreshActiveSlot(2,Before.InstanceID,true,false));
    Host.State->SetRole(ROLE_SimulatedProxy);
    TestFalse(TEXT("The targeted mutator itself requires authority"),Host.Perks->DebugRefreshActiveSlot(2,Before.InstanceID,false,true));
    TestFalse(TEXT("The targeted removal itself requires authority"),Host.Perks->DebugRemoveActiveSlot(2,Before.InstanceID));
    Host.State->SetRole(ROLE_Authority);
    TestTrue(TEXT("Rejected canonical refreshes leave slot identity and fields intact"),SameSlot(Before,Host.Perks->ActiveSkillSlots[2]));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDevActivesMutationTest,"MessControl.Skills.DevPanel.HeldAirCleanupAndTargetedMutation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDevActivesMutationTest::RunTest(const FString&)
{
    using namespace MCDevActivesTests;
    for(auto Operation:{EMCDevAction::RefillActiveSlot,EMCDevAction::ResetActiveCooldown,EMCDevAction::RemoveActiveSlot,EMCDevAction::GrantActiveSlot2}) {
        FWorld F;const auto Host=F.Player(F.Catalog(),true,true);
        if(!TestTrue(TEXT("Prepare unrelated passive"),Host.Perks->TryAcquirePerk(TEXT("DevPassive")))
            || !TestTrue(TEXT("Prepare independent consumable"),Host.Perks->TryAcquirePerk(TEXT("DevOtherCharge"),1))) return false;
        const auto OtherPress=Host.Press(1,1);
        if(!TestTrue(TEXT("Accept independent hazard press"),Host.Perks->BeginSkillSlot(OtherPress))) return false;
        auto* Other=Cast<AMCActiveSkillProtocolProbe>(Host.Perks->FindRunningAction(OtherPress));
        if(!TestNotNull(TEXT("Independent action exists"),Other)) return false;
        TestTrue(TEXT("Release its hand while hazard remains live"),Host.Perks->ReleaseSkillInput(OtherPress));
        if(!TestTrue(TEXT("Prepare held Air in explicitly selected slot"),Host.Perks->TryAcquirePerk(TEXT("DevAir"),2))) return false;
        const auto AirPress=Host.Press(2,2);
        if(!TestTrue(TEXT("Accept real held Air press"),Host.Perks->BeginSkillSlot(AirPress))) return false;
        auto* Air=Cast<AMCAirSkillAction>(Host.Perks->FindRunningAction(AirPress));
        if(!TestNotNull(TEXT("Production Air action is sucking"),Air)) return false;
        TestTrue(TEXT("Air began the held cycle"),Air->bSucking);
        const auto Before=Host.Perks->ActiveSkillSlots[2],OtherSlot=Host.Perks->ActiveSkillSlots[1];
        auto* Economy=Host.State->Economy.Get();Economy->Coins=123;
        FMCHubPendingPurchase Paid;Paid.InstanceID=FGuid::NewGuid();Paid.PerkID=TEXT("RetainedPaid");Paid.CatalogID=TEXT("PaidCatalog");
        Economy->PendingPurchases.Add(Paid);Economy->PendingBoosters.Add(TEXT("LegacyPaid"));
        Economy->ShopRotationId=FGuid::NewGuid();Economy->ShopStockIDs.Add(TEXT("RetainedStock"));const FGuid Rotation=Economy->ShopRotationId;
        const auto Phase=F.State->Phase;const int32 Step=F.State->StepIndex,Tasks=F.State->TasksLeft;
        const auto Director=F.Mode->GameDirector;const FGuid Run=F.Mode->GetHubBoosterRunId();
        F.Mode->ExecuteDevAction(Host.Controller,Operation,Operation==EMCDevAction::GrantActiveSlot2?MCDevActiveToken(TEXT("DevReusable")):2);
        TestTrue(TEXT("Selected Air action is cleanly finished"),Air->IsFinished());
        TestFalse(TEXT("Cancelled Air no longer sucks"),Air->bSucking);
        TestFalse(TEXT("The late old Air release cannot blow or reach the new occupant"),Host.Perks->ReleaseSkillInput(AirPress));
        const auto After=Host.Perks->ActiveSkillSlots[2];
        if(Operation==EMCDevAction::RefillActiveSlot) {
            TestEqual(TEXT("Refill restores authored charges"),After.Charges,3);
            TestEqual(TEXT("Refill preserves exact cooldown timestamp"),After.CooldownEndsAt,Before.CooldownEndsAt);
            TestTrue(TEXT("Refill rotates accepted input identity"),After.InstanceID!=Before.InstanceID);
            TestEqual(TEXT("Refill refreshes legacy queue from canonical charges"),Host.Perks->BoosterQueue.FilterByPredicate([](FName ID){return ID==TEXT("DevAir");}).Num(),3);
        } else if(Operation==EMCDevAction::ResetActiveCooldown) {
            TestEqual(TEXT("Cooldown reset preserves consumed charge count"),After.Charges,Before.Charges);
            TestEqual(TEXT("Cooldown reset makes the skill ready"),Host.Perks->GetSkillCooldownRemaining(2),0.f);
            TestTrue(TEXT("Cooldown reset invalidates the held input identity"),After.InstanceID!=Before.InstanceID);
        } else if(Operation==EMCDevAction::RemoveActiveSlot) TestTrue(TEXT("Remove empties only selected slot"),After.PerkID.IsNone());
        else TestEqual(TEXT("Grant replaces selected occupied slot"),After.PerkID,FName(TEXT("DevReusable")));
        TestTrue(TEXT("Other slot fields and identity are untouched"),SameSlot(OtherSlot,Host.Perks->ActiveSkillSlots[1]));
        TestTrue(TEXT("Other independent hazard is not cancelled"),!Other->IsFinished() && Other->Cancels==0);
        TestEqual(TEXT("Existing passive ownership is preserved"),Host.Perks->GetStacks(TEXT("DevPassive")),1);
        TestEqual(TEXT("Wallet unchanged"),Economy->Coins,123);
        TestTrue(TEXT("Paid pending data remains intact"),Economy->PendingPurchases.Num()==1 && Economy->PendingPurchases[0].InstanceID==Paid.InstanceID
            && Economy->PendingPurchases[0].PerkID==Paid.PerkID && Economy->PendingPurchases[0].CatalogID==Paid.CatalogID
            && Economy->PendingBoosters==TArray<FName>{TEXT("LegacyPaid")});
        TestTrue(TEXT("No shop reroll"),Economy->ShopRotationId==Rotation && Economy->ShopStockIDs==TArray<FName>{TEXT("RetainedStock")});
        TestTrue(TEXT("No run reset or objective transition"),F.Mode->GetHubBoosterRunId()==Run && F.Mode->GameDirector==Director
            && F.State->Phase==Phase && F.State->StepIndex==Step && F.State->TasksLeft==Tasks);
    }
    {
        FWorld F;auto* Table=F.Catalog();
        Table->FindRow<FMCPerkDefinition>(TEXT("DevAir"),TEXT("Held last-charge fixture"),false)->InitialCharges=1;
        const auto Host=F.Player(Table,true,true);
        if(!TestTrue(TEXT("Prepare last-charge Air"),Host.Perks->TryAcquirePerk(TEXT("DevAir"),2))) return false;
        const auto Press=Host.Press(2,1);
        if(!TestTrue(TEXT("Hold the final Air charge"),Host.Perks->BeginSkillSlot(Press))) return false;
        auto* Air=Cast<AMCAirSkillAction>(Host.Perks->FindRunningAction(Press));
        if(!TestNotNull(TEXT("Last-charge Air outlives the now-empty slot"),Air)) return false;
        TestTrue(TEXT("Final charge emptied canonical slot"),Host.Perks->ActiveSkillSlots[2].PerkID.IsNone());
        F.Mode->ExecuteDevAction(Host.Controller,MCDevActiveGrantAction(2),MCDevActiveToken(TEXT("DevReusable")));
        TestEqual(TEXT("Debug grant fills selected depleted slot"),Host.Perks->ActiveSkillSlots[2].PerkID,FName(TEXT("DevReusable")));
        TestTrue(TEXT("Debug grant relinquishes only its still-held old Air"),Air->IsFinished() && !Air->bSucking);
        TestFalse(TEXT("Late depleted-Air release cannot reach replacement"),Host.Perks->ReleaseSkillInput(Press));
    }
    {
        FWorld F;auto* Table=F.Catalog();
        Table->FindRow<FMCPerkDefinition>(TEXT("DevCharge"),TEXT("Old hazard fixture"),false)->InitialCharges=1;
        const auto Host=F.Player(Table,true,true);
        if(!TestTrue(TEXT("Prepare last-charge hazard"),Host.Perks->TryAcquirePerk(TEXT("DevCharge"),2))) return false;
        const auto OldPress=Host.Press(2,1);
        if(!TestTrue(TEXT("Accept last charge"),Host.Perks->BeginSkillSlot(OldPress))) return false;
        auto* Old=Cast<AMCActiveSkillProtocolProbe>(Host.Perks->FindRunningAction(OldPress));
        if(!TestNotNull(TEXT("Old hazard owns its original identity"),Old)) return false;
        TestTrue(TEXT("Release old hazard without destroying it"),Host.Perks->ReleaseSkillInput(OldPress));
        Table->FindRow<FMCPerkDefinition>(TEXT("DevCharge"),TEXT("New hazard fixture"),false)->InitialCharges=3;
        if(!TestTrue(TEXT("Reacquire same ID after depletion"),Host.Perks->TryAcquirePerk(TEXT("DevCharge"),2))) return false;
        const auto NewPress=Host.Press(2,2);
        if(!TestTrue(TEXT("New instance can start while old released hazard remains"),Host.Perks->BeginSkillSlot(NewPress))) return false;
        auto* Current=Cast<AMCActiveSkillProtocolProbe>(Host.Perks->FindRunningAction(NewPress));
        if(!TestNotNull(TEXT("New selected instance is distinct"),Current)) return false;
        F.Mode->ExecuteDevAction(Host.Controller,EMCDevAction::RemoveActiveSlot,2);
        TestTrue(TEXT("Debug remove interrupts current selected instance"),Current->IsFinished() && Current->Cancels==1);
        TestTrue(TEXT("Debug remove preserves older released hazard of the same ID"),!Old->IsFinished() && Old->Cancels==0);
        TestFalse(TEXT("A removed held input is sealed"),Host.Perks->ReleaseSkillInput(NewPress));
    }
    return true;
}
#endif
