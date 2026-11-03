#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "MCVFXActionSession.h"
#include "MCVFXActionCatalog.h"
#include "MCPerkPreviewAdapter.h"
#include "MCPerkActionPreviewPanel.h"
#include "MCVFXProfileBinding.h"
#include "MCVFXRecipePreset.h"
#include "Editor.h"

namespace
{
/** Synthetic metadata capability over an existing genuine saved binding.
 * Only registry metadata changes, never canonical packages or a gameplay factory.
 * Replacing the same entry avoids leaving a test-only catalogue/binding behind. */
struct FScopedMetadataProvider
{
    FMCPerkPreviewAdapterEntry Original;
    bool bRegistered=false;
    explicit FScopedMetadataProvider(const FMCPerkPreviewAdapterEntry& In):Original(In)
    {
        auto Metadata=Original;Metadata.Create=nullptr;Metadata.Scenarios.Reset();Metadata.ScenarioLabels.Reset();
        MCPerkPreviewAdapters::Register(MoveTemp(Metadata));bRegistered=true;
    }
    ~FScopedMetadataProvider(){if(bRegistered)MCPerkPreviewAdapters::Register(MoveTemp(Original));}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXMetadataEditingWithoutPreview,
    "MessControl.VFX.ActionSession.MetadataEditingWithoutPreview",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXMetadataEditingWithoutPreview::RunTest(const FString&)
{
    if(!GEditor || GEditor->PlayWorld)
    {AddError(TEXT("Run this source-binding regression outside PIE."));return false;}
    const FName ID(TEXT("Tool.Brush"));
    const auto* Real=MCPerkPreviewAdapters::Find(ID);
    if(!TestNotNull(TEXT("Genuine brush source registration"),Real))return false;
    const auto ProviderCopy=*Real;
    const FName PreviousContext=MCPerkActionPreviewPanel::GetContextPerkID();
    const auto PreviousRunner=MCPerkActionPreviewPanel::GetActiveRunner();
    FMCVFXCanonicalProfileState Before;FString Error;
    if(!TestTrue(TEXT("Read-only canonical binding capture"),MCVFXProfileBindings::ReadCanonical(ID,Before,Error)))
    {AddError(Error);return false;}
    {
        FScopedMetadataProvider Fixture(ProviderCopy);
        const auto Catalogue=MCVFXActionCatalog::GetCatalogue();
        const auto* Row=Catalogue.FindByPredicate([ID](const FMCVFXActionCatalogueEntry& E){return E.ID==ID;});
        if(!TestNotNull(TEXT("Metadata retains its ordinary catalogue row"),Row))return false;
        TestFalse(TEXT("Catalogue keeps contextual preview unavailable"),Row->bSupportsPreview);
        TestTrue(TEXT("Saved runtime binding is independently editable"),Row->bSupportsProfileOverride);
        TestFalse(TEXT("Unavailable preview has an explicit reason"),Row->UnsupportedReason.IsEmpty());
        FMCVFXActionSession Session;
        const bool Opened=Session.Initialize(ID,Error);
        if(!TestTrue(*FString::Printf(TEXT("Metadata context opens without factory: %s"),*Error),Opened))return false;
        TestTrue(TEXT("Metadata owns its authoring context"),Session.IsContextCurrent());
        TestTrue(TEXT("Metadata can edit its actual saved binding"),Session.CanEditProfile());
        TestFalse(TEXT("Metadata cannot play"),Session.CanPreview());
        TestTrue(TEXT("Metadata has no scenarios"),Session.GetScenarios().IsEmpty());
        TestFalse(TEXT("Metadata explains unavailable preview"),Session.GetPreviewUnavailableReason().IsEmpty());
        for(FName Event:ProviderCopy.Stages)
        {
            TestNotNull(TEXT("Actual event has an editable master"),Session.GetRecipe(Event));
            if(const auto* Label=ProviderCopy.StageLabels.Find(Event))
                TestEqual(TEXT("Real event label survives"),Session.GetEventLabel(Event).ToString(),Label->ToString());
            if(const auto* Original=ProviderCopy.DefaultSystems.Find(Event))
                if(Session.GetOriginalSlot(Event).Mode==EMCPerkVFXSlotMode::UseDefault&&!Original->IsNull())
                    if(const auto* Recipe=Session.GetRecipe(Event))
                        TestTrue(TEXT("UseDefault exposes the original NS"),Recipe->Layers.ContainsByPredicate(
                            [Original](const FMCVFXCompositionLayer& Layer){return Layer.System.ToSoftObjectPath()==Original->ToSoftObjectPath();}));
        }
        TestFalse(TEXT("Metadata Play refuses before runner entry"),Session.PlayFullAction(Error));
        TestFalse(TEXT("Rejected Play reports why"),Error.IsEmpty());
        TestTrue(TEXT("Metadata never switches shared preview context"),PreviousContext==MCPerkActionPreviewPanel::GetContextPerkID());
        TestTrue(TEXT("Metadata never creates/replaces shared runner"),PreviousRunner==MCPerkActionPreviewPanel::GetActiveRunner());
        TestFalse(TEXT("Inspection/rejected Play leave draft clean"),Session.IsDirty());
        const FString Snapshot=Session.GetSnapshotJson();
        TestTrue(TEXT("Snapshot exposes preview capability"),Snapshot.Contains(TEXT("\"can_preview\": false"))||Snapshot.Contains(TEXT("\"can_preview\":false")));
    }
    const auto* Restored=MCPerkPreviewAdapters::Find(ID);
    TestTrue(TEXT("Original real provider restored by RAII"),Restored&&Restored->Create&&Restored->Scenarios==ProviderCopy.Scenarios);
    FMCVFXCanonicalProfileState After;
    TestTrue(TEXT("Read-only metadata fixture preserves saved owner/profile"),MCVFXProfileBindings::ReadCanonical(ID,After,Error)
        &&Before.OwnerDigest==After.OwnerDigest&&Before.ProfileDigest==After.ProfileDigest&&Before.ProfileValueDigest==After.ProfileValueDigest);
    const auto Catalogue=MCVFXActionCatalog::GetCatalogue();
    for(const FName NutID:{FName(TEXT("Boss.NutKnight.CounterShove")),FName(TEXT("Boss.NutKnight.Transform")),
        FName(TEXT("Enemy.NutKamikaze.Fuse")),FName(TEXT("Enemy.NutKamikaze.Blast")),FName(TEXT("Event.NutRain.Entry"))})
    {
        const auto* Provider=MCPerkPreviewAdapters::Find(NutID);
        const auto* Row=Catalogue.FindByPredicate([NutID](const FMCVFXActionCatalogueEntry& E){return E.ID==NutID;});
        TestTrue(*FString::Printf(TEXT("Actual Nut auxiliary provider is now previewable: %s"),*NutID.ToString()),
            Provider&&Provider->Create&&!Provider->Scenarios.IsEmpty()&&Row&&Row->bSupportsPreview);
    }
    return !HasAnyErrors();
}
#endif
