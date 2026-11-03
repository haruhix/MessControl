#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCVFXCatalogueFilter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXCatalogueFilterUsesSourceMetadata,
    "MessControl.VFX.Composer.CatalogueMetadataFilters",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXCatalogueFilterUsesSourceMetadata::RunTest(const FString&)
{
    FMCVFXActionCatalogueEntry Consumable;
    // Deliberately misleading identity proves classification is from the DT kind.
    Consumable.ID=TEXT("Active_LabelDoesNotDefineKind");Consumable.Label=FText::FromString(TEXT("Ракетница"));
    Consumable.Section=EMCVFXActionSection::Boosters;Consumable.PerkKind=EMCPerkKind::Consumable;
    Consumable.SearchNames={TEXT("Rocket Launcher"),TEXT("UI.Skill.Rocket.Name"),TEXT("ЁЛОЧНЫЙ")};
    FMCVFXCatalogueFilter Filter;Filter.Section=EMCVFXActionSection::Boosters;Filter.BoosterKind=EMCPerkKind::Consumable;
    TestTrue(TEXT("Consumable filter uses the real canonical Kind"),Filter.Matches(Consumable));
    Filter.BoosterKind=EMCPerkKind::Active;
    TestFalse(TEXT("An Active-looking ID cannot reclassify a Consumable"),Filter.Matches(Consumable));
    Filter.Section=EMCVFXActionSection::All;
    TestTrue(TEXT("All sections ignore a dormant booster-only subfilter"),Filter.Matches(Consumable));
    for(const FString Query:{FString(TEXT("ракет")),FString(TEXT("РАКЕТ")),FString(TEXT("рАкЕт")),FString(TEXT("ёлоч")),
        FString(TEXT(" ROCKET LAUNCHER ")),FString(TEXT("ui.skill.rocket")),FString(TEXT("labeldoesnotdefinekind"))}) {
        Filter.Query=Query;
        TestTrue(FString::Printf(TEXT("Localized display/alias/key/ID matches query '%s'"),*Query),Filter.Matches(Consumable));
    }
    Filter.Query=TEXT("does not exist");TestFalse(TEXT("Unmatched search remains empty instead of substituting the first row"),Filter.Matches(Consumable));
    Filter.Query=TEXT("   ");TestTrue(TEXT("Empty/whitespace search restores the row"),Filter.Matches(Consumable));
    FMCVFXActionCatalogueEntry Unknown;Unknown.ID=TEXT("UnregisteredFutureAction");
    Filter=FMCVFXCatalogueFilter();TestTrue(TEXT("Unknown source category remains discoverable in All"),Filter.Matches(Unknown));
    Filter.Section=EMCVFXActionSection::Other;TestTrue(TEXT("Unknown source category is retained under Other"),Filter.Matches(Unknown));
    TestTrue(TEXT("Boss group is from declared source metadata"),MCVFXCatalogueNavigation::FromSourceCategory(TEXT("Boss"))==EMCVFXActionSection::Bosses);
    TestTrue(TEXT("Unrecognized source category does not borrow a skill family"),MCVFXCatalogueNavigation::FromSourceCategory(TEXT("Unknown"))==EMCVFXActionSection::Other);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXCatalogueFilterPreservesSourceItems,
    "MessControl.VFX.Composer.CatalogueFilteringKeepsBackingIdentity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXCatalogueFilterPreservesSourceItems::RunTest(const FString&)
{
    using FItem=TSharedPtr<FMCVFXActionCatalogueEntry>;
    TArray<FItem> Full;auto Selected=MakeShared<FMCVFXActionCatalogueEntry>();Selected->ID=TEXT("SelectedDirtySource");
    Selected->Section=EMCVFXActionSection::Bosses;Full.Add(Selected);
    auto Other=MakeShared<FMCVFXActionCatalogueEntry>();Other->ID=TEXT("Other");Other->Section=EMCVFXActionSection::General;Full.Add(Other);
    FMCVFXCatalogueFilter Filter;Filter.Section=EMCVFXActionSection::General;
    TArray<FItem> Visible;for(const auto& Item:Full)if(Filter.Matches(*Item))Visible.Add(Item);
    TestEqual(TEXT("Filtering changes only visible pointers"),Visible.Num(),1);
    TestEqual(TEXT("Full backing catalogue remains complete"),Full.Num(),2);
    TestTrue(TEXT("Filtered-out selected source retains exact identity"),Full[0]==Selected);
    Filter=FMCVFXCatalogueFilter();Visible.Reset();for(const auto& Item:Full)if(Filter.Matches(*Item))Visible.Add(Item);
    TestTrue(TEXT("Reset restores the same selected source pointer"),Visible.Contains(Selected));
    return true;
}
#endif
