#include "MCVFXCatalogueFilter.h"
#include "Internationalization/TextTransformer.h"

#define LOCTEXT_NAMESPACE "MCVFXCatalogueNavigation"

bool FMCVFXCatalogueFilter::Matches(const FMCVFXActionCatalogueEntry& Entry)const
{
    if(Section!=EMCVFXActionSection::All && Entry.Section!=Section)return false;
    if(Section==EMCVFXActionSection::Boosters && BoosterKind.IsSet()
        && (!Entry.PerkKind.IsSet() || Entry.PerkKind.GetValue()!=BoosterKind.GetValue()))return false;
    // FString's IgnoreCase substring path uses ASCII-only TChar::ToUpper for
    // the first character. Use the engine's Unicode/ICU text transform so a
    // lowercase Cyrillic query also finds an uppercase localized display name.
    const FString Needle=FTextTransformer::ToLower(Query.TrimStartAndEnd());
    if(Needle.IsEmpty())return true;
    auto Contains=[&](const FString& Name){return FTextTransformer::ToLower(Name).Contains(Needle,ESearchCase::CaseSensitive);};
    if(Contains(Entry.Label.ToString())||Contains(Entry.ID.ToString())||Contains(Entry.Category.ToString()))return true;
    return Entry.SearchNames.ContainsByPredicate(Contains);
}

FText MCVFXCatalogueNavigation::Label(EMCVFXActionSection Section)
{
    switch(Section)
    {
        case EMCVFXActionSection::All:return LOCTEXT("All","Все действия");
        case EMCVFXActionSection::Boosters:return LOCTEXT("Boosters","Бустеры");
        case EMCVFXActionSection::Bosses:return LOCTEXT("Bosses","Боссы");
        case EMCVFXActionSection::WorldEvents:return LOCTEXT("WorldEvents","Эвенты / окружение");
        case EMCVFXActionSection::General:return LOCTEXT("General","Общее");
        case EMCVFXActionSection::InterfaceHub:return LOCTEXT("InterfaceHub","Интерфейс / хаб");
        default:return LOCTEXT("Other","Другое");
    }
}

FText MCVFXCatalogueNavigation::BoosterLabel(TOptional<EMCPerkKind> Kind)
{
    if(!Kind.IsSet())return LOCTEXT("AllBoosters","Все бустеры");
    switch(Kind.GetValue())
    {
        case EMCPerkKind::Active:return LOCTEXT("Active","Активные");
        case EMCPerkKind::Passive:return LOCTEXT("Passive","Пассивные");
        case EMCPerkKind::Consumable:return LOCTEXT("Consumable","Расходники");
        default:return LOCTEXT("OtherBoosters","Другие бустеры");
    }
}

EMCVFXActionSection MCVFXCatalogueNavigation::FromSourceCategory(FName Category)
{
    if(Category==TEXT("Boss"))return EMCVFXActionSection::Bosses;
    if(Category==TEXT("World") || Category==TEXT("WorldEvents") || Category==TEXT("Environment"))return EMCVFXActionSection::WorldEvents;
    if(Category==TEXT("Hub") || Category==TEXT("UI"))return EMCVFXActionSection::InterfaceHub;
    if(Category==TEXT("Cleaning") || Category==TEXT("ToothCare") || Category==TEXT("Enemies")
        || Category==TEXT("Character") || Category==TEXT("Tools") || Category==TEXT("Rewards"))return EMCVFXActionSection::General;
    return EMCVFXActionSection::Other;
}

#undef LOCTEXT_NAMESPACE
