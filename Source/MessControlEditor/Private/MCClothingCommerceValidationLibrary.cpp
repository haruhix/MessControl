#include "MCClothingCommerceValidationLibrary.h"
#include "MCCosmeticCatalog.h"
#include "MCHubEconomyComponent.h"
#include "MCHubGameState.h"
#include "MCPlayerState.h"
#include "MCPerkComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace MCClothingCommerceValidationPrivate
{
    constexpr const TCHAR* CosmeticPath=TEXT("/Game/Data/DT_CharacterCosmetics.DT_CharacterCosmetics");
    constexpr const TCHAR* HubPath=TEXT("/Game/Gameplay/Hub/Economy/DT_HubCatalog.DT_HubCatalog");
    bool bRunning=false;
    struct FReport
    {
        TSharedRef<FJsonObject> Root=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Checks;bool Passed=true;
        void Expect(bool Value,const TCHAR* Name) {
            auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("check"),Name);Item->SetBoolField(TEXT("passed"),Value);
            Checks.Add(MakeShared<FJsonValueObject>(Item));Passed&=Value;
        }
        FString Finish() {
            Root->SetBoolField(TEXT("passed"),Passed);Root->SetArrayField(TEXT("checks"),Checks);
            Root->SetStringField(TEXT("scope"),TEXT("Native isolated authority/data contracts only; no PIE, live player, gameplay presentation or visual acceptance."));
            FString Text;const auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(Root,Writer);return Text;
        }
    };
    TArray<FString> DirtyPackages() {
        TArray<FString> Names;for(TObjectIterator<UPackage> It;It;++It)
            if(*It!=GetTransientPackage() && !It->HasAnyFlags(RF_Transient) && !It->HasAnyPackageFlags(PKG_PlayInEditor) && It->IsDirty()) Names.Add(It->GetName());
        Names.Sort();return Names;
    }
    bool SetFixtureTable(UObject* Object,FName PropertyName,UDataTable* Table) {
        auto* Property=FindFProperty<FObjectPropertyBase>(Object->GetClass(),PropertyName);
        if(!Property || !Property->PropertyClass->IsChildOf(UDataTable::StaticClass())) return false;
        Property->SetObjectPropertyValue_InContainer(Object,Table);return true;
    }
    struct FFixture
    {
        UWorld* World=nullptr;UDataTable* Cosmetics=nullptr;UDataTable* Catalog=nullptr;UDataTable* Rules=nullptr;UDataTable* Rewards=nullptr;UDataTable* Perks=nullptr;
        FName CosmeticId,OfferId;FName BoosterId=TEXT("MCValidation_Booster");
        ~FFixture() {if(World) {if(GEngine) GEngine->DestroyWorldContext(World);World->DestroyWorld(false);World=nullptr;}}
        bool Initialize(UDataTable* SourceCosmetics,UDataTable* SourceHub,FReport& Report) {
            const auto Values=UWorld::InitializationValues().InitializeScenes(false).AllowAudioPlayback(false).RequiresHitProxies(false).CreateFXSystem(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
            World=UWorld::CreateWorld(EWorldType::EditorPreview,false,MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("MCClothingValidationWorld")),GetTransientPackage(),true,ERHIFeatureLevel::Num,&Values);
            if(!World) {Report.Expect(false,TEXT("Create private world"));return false;}
            GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
            FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;
            auto* Hub=World->SpawnActor<AMCHubGameState>(AMCHubGameState::StaticClass(),FTransform::Identity,Spawn);
            if(!Hub) {Report.Expect(false,TEXT("Create private Hub authority state"));return false;}
            World->SetGameState(Hub);
            Cosmetics=DuplicateObject<UDataTable>(SourceCosmetics,World);Catalog=DuplicateObject<UDataTable>(SourceHub,World);
            for(auto* Table:{Cosmetics,Catalog}) {Table->ClearFlags(RF_Public|RF_Standalone|RF_Transactional);Table->SetFlags(RF_Transient);}
            Rules=NewObject<UDataTable>(World,NAME_None,RF_Transient);Rules->RowStruct=FMCHubEconomyRulesRow::StaticStruct();
            FMCHubEconomyRulesRow Settings;Settings.CommonShopAvailability=Settings.RareShopAvailability=Settings.EpicShopAvailability=Settings.LegendaryShopAvailability=1.f;
            Rules->AddRow(TEXT("Default"),Settings);
            Rewards=NewObject<UDataTable>(World,NAME_None,RF_Transient);Rewards->RowStruct=FMCGrillRewardRow::StaticStruct();
            Perks=NewObject<UDataTable>(World,NAME_None,RF_Transient);Perks->RowStruct=FMCPerkDefinition::StaticStruct();
            FMCPerkDefinition Skill;Skill.Kind=EMCPerkKind::Passive;Skill.MaxStacks=1;Skill.DisplayName=FText::AsCultureInvariant(TEXT("Fixture booster"));Perks->AddRow(BoosterId,Skill);
            FMCHubCatalogRow Booster;Booster.Kind=EMCHubCatalogKind::NextRunBooster;Booster.PerkID=BoosterId;Booster.Price=17;Catalog->AddRow(BoosterId,Booster);
            auto Names=SourceCosmetics->GetRowNames();Names.Sort(FNameLexicalLess());
            for(FName Name:Names) {
                const auto* Original=UMCCosmeticCatalog::Find(SourceCosmetics,Name);
                if(!Original || !Original->bEnabled || Original->bDefaultUnlocked || !Original->Mesh) continue;
                for(const auto& Pair:SourceHub->GetRowMap()) {
                    const auto* Offer=reinterpret_cast<const FMCHubCatalogRow*>(Pair.Value);
                    if(Offer->Kind!=EMCHubCatalogKind::Cosmetic || Offer->CosmeticID!=Name || Offer->Icon.IsNull()) continue;
                    FMCCosmeticCatalogRow Row=*Original;Row.bUseCanonicalCommerce=true;Row.ShopItemId=Pair.Key;
                    Row.DisplayName=Offer->DisplayName;Row.Description=Offer->Description;Row.DisplayNameKey=Offer->DisplayNameKey;Row.DescriptionKey=Offer->DescriptionKey;
                    Row.Icon=Offer->Icon;Row.BasePrice=101;Row.DiscountPercent=15;Row.ShopAppearanceChance=1.f;Row.bInShop=true;
                    Cosmetics->AddRow(Name,Row);CosmeticId=Name;OfferId=Pair.Key;
                    FMCHubCatalogRow Legacy=*Offer;Legacy.Price=777;Legacy.DisplayName=FText::AsCultureInvariant(TEXT("Contradictory legacy name"));Legacy.DisplayNameKey=TEXT("MCValidation.LegacyName");Legacy.Icon.Reset();Catalog->AddRow(Pair.Key,Legacy);break;
                }
                if(!CosmeticId.IsNone()) break;
            }
            Report.Expect(!CosmeticId.IsNone(),TEXT("Use actual saved enabled nonfree cosmetic identity/mesh and actual linked Hub offer"));
            Report.Expect(!World->HasBegunPlay() && !World->GetGameInstance() && World->GetNumPlayerControllers()==0,TEXT("No BeginPlay, GameInstance, controller or profile subsystem"));
            return !CosmeticId.IsNone();
        }
        UMCHubEconomyComponent* NewEconomy(FReport& Report) {
            FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;
            auto* Player=World->SpawnActor<AMCPlayerState>(AMCPlayerState::StaticClass(),FTransform::Identity,Spawn);
            if(!Player || !Player->Economy || !Player->Perks) {Report.Expect(false,TEXT("Create isolated authority PlayerState/components"));return nullptr;}
            auto* Economy=Player->Economy.Get();Player->Perks->PerkTable=Perks;
            Economy->CatalogTable=Catalog;Economy->RulesTable=Rules;Economy->GrillRewardsTable=Rewards;
            const bool Bound=SetFixtureTable(Economy,TEXT("LoadedCatalog"),Catalog) && SetFixtureTable(Economy,TEXT("LoadedCosmetics"),Cosmetics)
                && SetFixtureTable(Economy,TEXT("LoadedRules"),Rules) && SetFixtureTable(Economy,TEXT("LoadedRewards"),Rewards)
                && SetFixtureTable(Player->Perks,TEXT("LoadedTable"),Perks);
            Report.Expect(Bound && Player->HasAuthority() && !Economy->OnEconomyChanged.IsBound(),TEXT("Fixture tables bound only on private authority, no external economy listeners"));
            Economy->Coins=1000;return Bound?Economy:nullptr;
        }
        FMCCosmeticCatalogRow& Row() {return *Cosmetics->FindRow<FMCCosmeticCatalogRow>(CosmeticId,TEXT("Clothing validation fixture"),false);}
    };
    void Exercise(FFixture& Fixture,FReport& Report)
    {
        auto* Economy=Fixture.NewEconomy(Report);if(!Economy) return;FText Error;
        auto& Row=Fixture.Row();Report.Expect(UMCCosmeticCatalog::ValidateCommerce(Row,Error),TEXT("Cloned canonical commerce validates"));
        struct FPriceCase {int32 Base,Discount,Expected;};
        const FPriceCase Cases[]={{101,15,85},{1,1,0},{MAX_int32,0,MAX_int32},{MAX_int32,100,0},{0,50,0},{100,101,INDEX_NONE},{-1,0,INDEX_NONE}};
        bool Prices=true;for(const auto& Case:Cases) {FMCCosmeticCatalogRow Copy=Row;Copy.BasePrice=Case.Base;Copy.DiscountPercent=Case.Discount;Prices&=UMCCosmeticCatalog::EffectivePrice(Copy)==Case.Expected;}
        Report.Expect(Prices,TEXT("Actual native int64 discount/floor boundary cases"));
        FMCHubEconomyCatalogEntry Entry;const bool Resolved=Economy->FindCatalogEntry(Fixture.OfferId,Entry);
        Report.Expect(Resolved && Entry.bCanonicalClothing && Entry.Definition.Price==85 && Entry.BasePrice==101 && Entry.DiscountPercent==15
            && Entry.Definition.CosmeticID==Fixture.CosmeticId && Entry.Definition.Icon==Row.Icon
            && Entry.Definition.DisplayName.EqualTo(Row.DisplayName) && Entry.Definition.DisplayNameKey==Row.DisplayNameKey && Entry.Definition.DescriptionKey==Row.DescriptionKey,TEXT("Public offer projects canonical price/copy/icon instead of contradictory legacy777"));
        Report.Expect(!Economy->CanPurchase(Fixture.OfferId,Error) && Economy->Coins==1000,TEXT("Clothing requires initial stock epoch before purchase"));
        const FGuid Rotation(1,2,3,4);Report.Expect(Economy->EnsureShopStock(Rotation,42),TEXT("Original authority stock epoch prepared"));
        const auto Stock=Economy->ShopStockIDs;
        Report.Expect(Stock.Contains(Fixture.BoosterId) && !Stock.Contains(Fixture.OfferId),TEXT("Persisted stock contains booster and never clothing"));
        Report.Expect(Economy->IsInShopStock(Fixture.OfferId) && Economy->CanPurchase(Fixture.OfferId,Error),TEXT("Chance1 enables actual canonical purchase"));
        Row.ShopAppearanceChance=0.f;
        Report.Expect(!Economy->IsInShopStock(Fixture.OfferId) && !Economy->TryPurchase(Fixture.OfferId,Error) && Economy->Coins==1000 && !Economy->OwnsCosmetic(Fixture.CosmeticId),TEXT("Chance0 rejects actual debit/grant"));
        Row.ShopAppearanceChance=.35f;const bool InitialMembership=Economy->IsInShopStock(Fixture.OfferId);
        bool Stable=true;for(int32 Index=0;Index<8;++Index) Stable&=Economy->IsInShopStock(Fixture.OfferId)==InitialMembership;
        Report.Expect(Stable && Economy->EnsureShopStock(Rotation,999) && Economy->ShopRotationSeed==42 && Economy->ShopStockIDs==Stock
            && Economy->IsInShopStock(Fixture.OfferId)==InitialMembership,TEXT("Same epoch preserves seed, booster stock and derived clothing roll"));
        FMCCosmeticCatalogRow Extra=Row;Extra.Slot=TEXT("ValidationBackSlot");Extra.ShopItemId=TEXT("MCValidation_BackOffer");
        const FName ExtraId(TEXT("MCValidation_BackItem"));Fixture.Cosmetics->AddRow(ExtraId,Extra);
        Report.Expect(Economy->IsInShopStock(Fixture.OfferId)==InitialMembership,TEXT("Adding another garment does not change existing per-item roll"));
        const auto SlotItems=UMCCosmeticCatalog::GetItemsForSlot(Fixture.Cosmetics,Extra.Slot);
        auto Loadout=UMCCosmeticCatalog::WithEquippedItem(FMCCharacterCosmeticLoadout(),Extra.Slot,ExtraId);
        const bool SlotValid=SlotItems.Contains(ExtraId) && UMCCosmeticCatalog::ValidateLoadout(Fixture.Cosmetics,Loadout,Error);
        Loadout=UMCCosmeticCatalog::WithEquippedItem(Loadout,Extra.Slot,NAME_None);
        Report.Expect(SlotValid && Loadout.Slots.IsEmpty() && UMCCosmeticCatalog::ValidateLoadout(Fixture.Cosmetics,Loadout,Error),TEXT("Native arbitrary data slot discovery/loadout/clear; UI remains separate review"));
        Fixture.Row().ShopItemId=TEXT("MCValidation_RenamedOffer");
        Report.Expect(!Economy->IsInShopStock(Fixture.OfferId) && Economy->EnsureShopStock(Rotation,999) && Economy->ShopStockIDs==Stock && Economy->ShopRotationSeed==42,TEXT("Retired clothing offer ID never rerolls booster stock"));
        Fixture.Row().ShopItemId=Fixture.BoosterId;
        const auto* Foreign=Economy->FindCatalogItem(Fixture.BoosterId);
        Report.Expect(Foreign && Foreign->Kind==EMCHubCatalogKind::NextRunBooster && Foreign->PerkID==Fixture.BoosterId && Foreign->Price==17
            && Economy->FindCatalogEntry(Fixture.BoosterId,Entry) && !Entry.bCanonicalClothing,TEXT("Foreign noncosmetic ID wins malformed garment collision"));
        Report.Expect(Economy->EnsureShopStock(Rotation,999) && Economy->ShopStockIDs==Stock && Economy->ShopRotationSeed==42,TEXT("Foreign-ID collision does not invalidate booster assortment"));
        Fixture.Row().ShopItemId=Fixture.OfferId;Fixture.Row().ShopAppearanceChance=1.f;
        Fixture.Row().bUseCanonicalCommerce=false;
        Report.Expect(Economy->FindCatalogItem(Fixture.OfferId)==Fixture.Catalog->FindRow<FMCHubCatalogRow>(Fixture.OfferId,TEXT("Legacy fixture"),false),TEXT("Non-opted legacy offer returns original Hub source row"));
        Fixture.Row().bUseCanonicalCommerce=true;
        Report.Expect(Economy->TryPurchase(Fixture.OfferId,Error) && Economy->Coins==915 && Economy->OwnsCosmetic(Fixture.CosmeticId),TEXT("Actual authority purchase debits displayed85 and grants exact cosmetic ID once"));
        Report.Expect(!Economy->TryPurchase(Fixture.OfferId,Error) && Economy->Coins==915,TEXT("Duplicate purchase never debits twice"));
        Fixture.Row().ShopAppearanceChance=0.f;Fixture.Row().bInShop=false;
        Report.Expect(Economy->OwnsCosmetic(Fixture.CosmeticId) && !Economy->IsInShopStock(Fixture.OfferId) && !Economy->CanPurchase(Fixture.OfferId,Error)
            && Economy->FindCatalogEntry(Fixture.OfferId,Entry) && Entry.bCanonicalClothing && Entry.Definition.Icon==Fixture.Row().Icon,TEXT("Owned offstock clothing retains canonical offer/icon and cannot be repurchased"));
        const auto OwnedLoadout=UMCCosmeticCatalog::WithEquippedItem(FMCCharacterCosmeticLoadout(),Fixture.Row().Slot,Fixture.CosmeticId);
        Report.Expect(Economy->TryEquipLoadout(OwnedLoadout,Error) && Economy->EquippedLoadout==OwnedLoadout && Economy->Coins==915,TEXT("Actual saved cosmetic remains authority-equippable while offstock, no debit"));
        const auto Save=Economy->ExportSave();auto* Restored=Fixture.NewEconomy(Report);if(!Restored) return;
        Report.Expect(Restored->ImportSave(Save,Error) && Restored->Coins==915 && Restored->OwnsCosmetic(Fixture.CosmeticId)
            && Restored->ShopStockIDs==Stock && Restored->ShopRotationId==Rotation && Restored->ShopRotationSeed==42 && Restored->EquippedLoadout==OwnedLoadout,TEXT("Existing actual save schema roundtrip retains wallet, stable owned ID and booster epoch"));
        Report.Root->SetStringField(TEXT("actualCosmeticID"),Fixture.CosmeticId.ToString());Report.Root->SetStringField(TEXT("actualOfferID"),Fixture.OfferId.ToString());
        Report.Root->SetNumberField(TEXT("displayedAndDebitedPrice"),85);Report.Root->SetNumberField(TEXT("walletAfterPurchase"),Economy->Coins);
    }
}
FString UMCClothingCommerceValidationLibrary::ValidateTransientCommerce()
{
    using namespace MCClothingCommerceValidationPrivate;FReport Report;
    if(!IsInGameThread() || !GIsEditor || !GEditor || !GEngine || IsRunningCommandlet() || GEditor->IsPlaySessionInProgress() || bRunning) {
        Report.Expect(false,TEXT("Requires idle Editor game thread, no active/queued PIE and no reentrant validation"));return Report.Finish();
    }
    TGuardValue<bool> Running(bRunning,true);
    auto* SourceCosmetics=FindObject<UDataTable>(nullptr,CosmeticPath);auto* SourceHub=FindObject<UDataTable>(nullptr,HubPath);
    const bool Sources=SourceCosmetics && SourceHub && SourceCosmetics->GetRowStruct()==FMCCosmeticCatalogRow::StaticStruct()
        && SourceHub->GetRowStruct()==FMCHubCatalogRow::StaticStruct() && !SourceCosmetics->GetOutermost()->IsDirty() && !SourceHub->GetOutermost()->IsDirty();
    Report.Expect(Sources,TEXT("Exact saved canonical source tables already resident, matching schema and clean"));if(!Sources) return Report.Finish();
    const auto DirtyBefore=DirtyPackages();const FString CosmeticsBefore=SourceCosmetics->GetTableAsJSON(),HubBefore=SourceHub->GetTableAsJSON();
    UWorld* const PriorWorld=GWorld;const int32 ContextsBefore=GEngine->GetWorldContexts().Num();TWeakObjectPtr<UWorld> RetiredWorld;
    {
        FFixture Fixture;if(Fixture.Initialize(SourceCosmetics,SourceHub,Report)) {RetiredWorld=Fixture.World;Exercise(Fixture,Report);}
    }
    Report.Expect(GWorld==PriorWorld && GEngine->GetWorldContexts().Num()==ContextsBefore && (!RetiredWorld.IsValid() || (!RetiredWorld->IsRooted() && !GEngine->GetWorldContextFromWorld(RetiredWorld.Get()))),TEXT("Private world cleaned/unrooted; original world and context count restored"));
    Report.Expect(SourceCosmetics->GetTableAsJSON()==CosmeticsBefore && SourceHub->GetTableAsJSON()==HubBefore && DirtyPackages()==DirtyBefore,TEXT("Source table JSON and all preexisting persistent package dirty states unchanged"));
    Report.Root->SetStringField(TEXT("cosmeticSource"),CosmeticPath);Report.Root->SetStringField(TEXT("hubSource"),HubPath);
    Report.Root->SetBoolField(TEXT("beginPlayInvoked"),false);Report.Root->SetBoolField(TEXT("gameInstanceOrProfileCreated"),false);
    return Report.Finish();
}
