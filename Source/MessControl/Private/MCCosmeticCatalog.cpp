#include "MCCosmeticCatalog.h"
#include "Engine/StaticMesh.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "MCCosmetics"

const FName UMCCosmeticCatalog::FaceSlot(TEXT("Face"));
const FName UMCCosmeticCatalog::HatSlot(TEXT("Hat"));

FName FMCCharacterCosmeticLoadout::ItemForSlot(FName Slot) const
{
    for(const auto& Entry:Slots) if(Entry.Slot==Slot) return Entry.ItemId;
    return NAME_None;
}

void FMCCharacterCosmeticLoadout::SetItemForSlot(FName Slot,FName ItemId)
{
    // Keep a single canonical record per occupied slot. None removes the accessory.
    Slots.RemoveAll([Slot](const FMCCosmeticSlotSelection& Entry) { return Entry.Slot==Slot; });
    if(!Slot.IsNone() && !ItemId.IsNone()) { FMCCosmeticSlotSelection Entry;Entry.Slot=Slot;Entry.ItemId=ItemId;Slots.Add(Entry); }
    Slots.Sort([](const FMCCosmeticSlotSelection& A,const FMCCosmeticSlotSelection& B) { return A.Slot.LexicalLess(B.Slot); });
}

UDataTable* UMCCosmeticCatalog::GetDefaultCatalog()
{
    return LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_CharacterCosmetics.DT_CharacterCosmetics"));
}

const FMCCosmeticCatalogRow* UMCCosmeticCatalog::Find(const UDataTable* Catalog,FName ItemId)
{
    return Catalog && Catalog->GetRowStruct()==FMCCosmeticCatalogRow::StaticStruct() && !ItemId.IsNone()
        ?Catalog->FindRow<FMCCosmeticCatalogRow>(ItemId,TEXT("CharacterCosmetics"),false):nullptr;
}

TArray<FName> UMCCosmeticCatalog::GetItemsForSlot(UDataTable* Catalog,FName Slot)
{
    TArray<FName> Result;
    if(!Catalog || Catalog->GetRowStruct()!=FMCCosmeticCatalogRow::StaticStruct()) return Result;
    for(const FName Id:Catalog->GetRowNames())
        if(const auto* Row=Find(Catalog,Id);Row && Row->bEnabled && Row->Slot==Slot && Row->Mesh) Result.Add(Id);
    Result.Sort([](FName A,FName B) { return A.LexicalLess(B); });
    return Result;
}

FMCCharacterCosmeticLoadout UMCCosmeticCatalog::WithEquippedItem(FMCCharacterCosmeticLoadout Loadout,FName Slot,FName ItemId)
{
    Loadout.SetItemForSlot(Slot,ItemId);return Loadout;
}

bool UMCCosmeticCatalog::ValidateLoadout(const UDataTable* Catalog,const FMCCharacterCosmeticLoadout& Loadout,FText& OutError)
{
    OutError=FText::GetEmpty();
    if(Loadout.Slots.Num()>16) { OutError=LOCTEXT("TooManySlots","Слишком много аксессуаров.");return false; }
    TSet<FName> Seen;
    for(const auto& Entry:Loadout.Slots) {
        if(Entry.Slot.IsNone() || Seen.Contains(Entry.Slot)) { OutError=LOCTEXT("DuplicateSlot","Некорректный слот аксессуара.");return false; }
        Seen.Add(Entry.Slot);
        if(Entry.ItemId.IsNone()) {
            // Empty Face/Hat entries are valid for Blueprint callers; future empty slots must be authored.
            bool bKnown=Entry.Slot==FaceSlot || Entry.Slot==HatSlot;
            if(Catalog && Catalog->GetRowStruct()==FMCCosmeticCatalogRow::StaticStruct())
                for(const FName Id:Catalog->GetRowNames()) if(const auto* Row=Find(Catalog,Id);Row && Row->Slot==Entry.Slot) { bKnown=true;break; }
            if(!bKnown) { OutError=LOCTEXT("UnknownSlot","Неизвестный слот аксессуара.");return false; }
            continue;
        }
        const auto* Row=Find(Catalog,Entry.ItemId);
        if(!Row || !Row->bEnabled || !Row->Mesh || Row->Slot!=Entry.Slot || Row->AttachBone.IsNone()
            || !Row->LocalTransform.IsValid()) { OutError=LOCTEXT("InvalidItem","Аксессуар недоступен для этого слота.");return false; }
    }
    return true;
}


int32 UMCCosmeticCatalog::EffectivePrice(const FMCCosmeticCatalogRow& Row)
{
    if(Row.BasePrice<0 || Row.DiscountPercent<0 || Row.DiscountPercent>100) return INDEX_NONE;
    return int32((int64(Row.BasePrice)*int64(100-Row.DiscountPercent))/100);
}
FName UMCCosmeticCatalog::ShopItemIdForRow(FName CosmeticId,const FMCCosmeticCatalogRow& Row)
{
    return Row.ShopItemId.IsNone()?CosmeticId:Row.ShopItemId;
}
bool UMCCosmeticCatalog::ValidateCommerce(const FMCCosmeticCatalogRow& Row,FText& OutError)
{
    OutError=FText::GetEmpty();if(!Row.bUseCanonicalCommerce) return true;
    if(!Row.Mesh || Row.Slot.IsNone() || Row.AttachBone.IsNone() || !Row.LocalTransform.IsValid()) {
        OutError=LOCTEXT("ClothingInvalidFit","Для одежды нужны меш, слот и корректная посадка.");return false;
    }
    if(Row.DisplayName.IsEmpty() || Row.Description.IsEmpty() || Row.DisplayNameKey.IsNone() || Row.DescriptionKey.IsNone() || Row.Icon.IsNull()) {
        OutError=LOCTEXT("ClothingMissingCopyOrIcon","Для одежды нужны название, описание, ключи локализации и иконка.");return false;
    }
    if(EffectivePrice(Row)==INDEX_NONE || uint8(Row.Rarity)>uint8(EMCHubItemRarity::Legendary)
        || !FMath::IsFinite(Row.ShopAppearanceChance) || Row.ShopAppearanceChance<0.f || Row.ShopAppearanceChance>1.f) {
        OutError=LOCTEXT("ClothingInvalidCommerce","Проверь цену, скидку, редкость и шанс появления одежды.");return false;
    }
    return true;
}
#if WITH_EDITOR
EDataValidationResult FMCCosmeticCatalogRow::IsDataValid(FDataValidationContext& Context) const
{
    FText Error;if(!UMCCosmeticCatalog::ValidateCommerce(*this,Error)) {Context.AddError(Error);return EDataValidationResult::Invalid;}
    return EDataValidationResult::Valid;
}
#endif

#undef LOCTEXT_NAMESPACE
