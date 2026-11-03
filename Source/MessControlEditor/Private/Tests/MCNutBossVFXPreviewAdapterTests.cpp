#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCPerkPreviewAdapter.h"
#include "MCVFXProfileBinding.h"
#include "MCNutBoss.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXPresentationLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutKnightShieldPreviewContract,
    "MessControl.VFX.NutKnightShield.PreviewProviderMatchesNativeOwner",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutKnightShieldPreviewContract::RunTest(const FString&)
{
    const FName ID(TEXT("Boss.NutKnight.Shield"));
    const auto* Entry=MCPerkPreviewAdapters::Find(ID);
    const auto* Boss=GetDefault<AMCNutBoss>();
    if(!TestNotNull(TEXT("Canonical knight shield has a real provider"),Entry)
        || !TestNotNull(TEXT("Native boss presentation component"),Boss->ShieldPresentation.Get()))return false;
    TestTrue(TEXT("No invented active perk/DT row is required"),Entry->Kind==EMCPerkPreviewAdapterKind::Context && Entry->SourceRowID.IsNone());
    TestTrue(TEXT("Native shield event source matches editor source"),Boss->ShieldPresentation->SourceID==ID);
    TestTrue(TEXT("Provider events exactly match the native accepted-event contract"),Entry->Stages==Boss->ShieldPresentation->Events
        && Entry->Stages.Num()==3 && Entry->Stages.Contains(TEXT("Hit")) && Entry->Stages.Contains(TEXT("Break")) && Entry->Stages.Contains(TEXT("Durability")));
    TestTrue(TEXT("Real native binding supports custom presentation"),Entry->bSupportsProfileOverride);
    TestTrue(TEXT("Two explicit actual-contact scenarios"),Entry->Scenarios.Num()==2
        && Entry->Scenarios.Contains(EMCPerkPreviewScenario::Target) && Entry->Scenarios.Contains(EMCPerkPreviewScenario::NearWall));
    TestTrue(TEXT("Break scenario is labeled by its actual action, not a fictitious wall"),Entry->ScenarioLabels.Contains(EMCPerkPreviewScenario::NearWall));
    TestTrue(TEXT("Procedural plus Niagara fallback is not flattened into a fake default layer"),Entry->DefaultSystems.IsEmpty());
    auto Adapter=Entry->Create();FMCPerkPreviewContext Missing;Missing.PerkID=ID;FString Error;int32 Receipts=0;
    Missing.RecordEvent=[&](FName){++Receipts;};
    TestFalse(TEXT("No private world/live hero cannot manufacture a shield preview"),Adapter->Prepare(Missing,Error));
    TestFalse(TEXT("Refused setup explains the missing context"),Error.IsEmpty());
    TestEqual(TEXT("No editable gameplay event was fabricated during refusal"),Receipts,0);
    TestFalse(TEXT("Refused fixture is not a finished scenario"),Adapter->IsComplete(Missing));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutKnightShieldCanonicalBinding,
    "MessControl.VFX.NutKnightShield.CanonicalLibraryNeedsNoPerkRow",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutKnightShieldCanonicalBinding::RunTest(const FString&)
{
    FMCVFXCanonicalProfileState Binding;FString Error;
    if(!TestTrue(TEXT("Saved mouth library resolves the registered non-perk source"),
        MCVFXProfileBindings::ReadCanonical(TEXT("Boss.NutKnight.Shield"),Binding,Error)))
    {AddError(Error);return false;}
    TestTrue(TEXT("Owner is the canonical reflected mouth presentation library"),Binding.OwnerAsset==UMCVFXPresentationLibrary::MouthLibraryPath());
    TestTrue(TEXT("Stable source is the library key, without a dummy DT row"),Binding.OwnerKey==TEXT("Boss.NutKnight.Shield") && Binding.CanonicalRowID.IsNone());
    TestTrue(TEXT("Only the actual native shield events can be assigned"),Binding.AllowedEvents.Num()==3
        && Binding.AllowedEvents.Contains(TEXT("Hit")) && Binding.AllowedEvents.Contains(TEXT("Break")) && Binding.AllowedEvents.Contains(TEXT("Durability")));
    return true;
}
#endif
