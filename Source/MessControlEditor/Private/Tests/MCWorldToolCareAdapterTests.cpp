#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "MCPerkPreviewAdapter.h"
#include "MCVFXSourceContracts.h"
#include "MCBrushContactComponent.h"
#include "MCToothCareFeedbackComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCWorldToolCareSourceContracts,
    "MessControl.VFX.WorldPresentation.ToolCareOriginalBindings",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCWorldToolCareSourceContracts::RunTest(const FString&)
{
    const auto* Brush=MCPerkPreviewAdapters::Find(TEXT("Tool.Brush"));
    const auto* Clean=MCPerkPreviewAdapters::Find(TEXT("EditorOnly.CleanArenaTooth"));
    const auto* Repair=MCPerkPreviewAdapters::Find(TEXT("EditorOnly.RepairArenaTooth"));
    if(!TestNotNull(TEXT("Actual brush contact provider"),Brush)
        ||!TestNotNull(TEXT("Actual accepted cleaning provider"),Clean)
        ||!TestNotNull(TEXT("Repair metadata retained independently of its deferred spray scenario"),Repair))return false;
    TestTrue(TEXT("Brush runs a real Target scenario"),Brush->Create&&Brush->Scenarios.Contains(EMCPerkPreviewScenario::Target));
    TestTrue(TEXT("Cleaning runs a real Target scenario"),Clean->Create&&Clean->Scenarios.Contains(EMCPerkPreviewScenario::Target));
    TestTrue(TEXT("Repair does not invent a spray/contact provider"),!Repair->Create&&Repair->Scenarios.IsEmpty());
    TestTrue(TEXT("Both actual accepted care events are editable"),Clean->Stages.Contains(TEXT("Contact"))&&Clean->Stages.Contains(TEXT("Complete"))
        &&Repair->Stages==Clean->Stages&&Repair->bSupportsProfileOverride);
    const auto* Original=Brush->DefaultSystems.Find(TEXT("Contact"));
    TestTrue(TEXT("Brush uses the actual native FoamSystem, not a replacement stand-in"),Original
        &&Original->ToSoftObjectPath()==GetDefault<UMCBrushContactComponent>()->FoamSystem.ToSoftObjectPath());
    const auto Sources=Brush->DescribeNativeSources(TEXT("Contact"));
    TestTrue(TEXT("Source opening targets the same real Niagara"),Sources.Num()==1&&Sources[0].Kind==EMCVFXNativeSourceKind::Niagara
        &&Sources[0].Asset==GetDefault<UMCBrushContactComponent>()->FoamSystem.ToSoftObjectPath());
    TestTrue(TEXT("Tool resource scope stays shared Hub/Arena only"),MCVFXSourceContracts::Find(TEXT("Tool.Brush"))->Library==MCVFXSourceContracts::ToolLibraryPath());
    TestTrue(TEXT("Care resource scope stays Arena only"),MCVFXSourceContracts::Find(TEXT("EditorOnly.CleanArenaTooth"))->Library==MCVFXSourceContracts::WorldLibraryPath());
    const auto* Audio=MCVFXSourceContracts::Find(TEXT("Tool.Brush"))->OriginalAudio.Find(TEXT("Contact"));
    TestTrue(TEXT("Original strokes are one actual palette entry with independent suppression"),Audio&&Audio->PaletteEvent==TEXT("Brush")
        &&Audio->bSuppressionWired&&MCVFXSourceContracts::CanSuppressOriginalSFX(TEXT("Tool.Brush"),TEXT("Contact")));
    TestFalse(TEXT("Care does not duplicate or suppress another owner's brush strokes"),MCVFXSourceContracts::CanSuppressOriginalSFX(TEXT("EditorOnly.CleanArenaTooth"),TEXT("Contact")));
    TestFalse(TEXT("Repair does not claim an unmapped original sound"),MCVFXSourceContracts::CanSuppressOriginalSFX(TEXT("EditorOnly.RepairArenaTooth"),TEXT("Complete")));
    for(const auto* Provider:{Brush,Clean}) {
        auto Adapter=Provider->Create();FMCPerkPreviewContext Empty;FString Error;
        TestFalse(TEXT("Missing actual private world/hero cannot fake contact"),Adapter->Prepare(Empty,Error));
        TestFalse(TEXT("Rejected contact fixture explains missing context"),Error.IsEmpty());
    }
    for(const FName ID:{FName(TEXT("Tool.Knife")),FName(TEXT("Tool.Pickaxe"))}) {
        const auto* Provider=MCPerkPreviewAdapters::Find(ID);
        TestTrue(TEXT("Native tool Swing and Hit both remain visible for authoring"),Provider&&Provider->Stages.Contains(TEXT("Swing"))&&Provider->Stages.Contains(TEXT("Hit")));
        TestTrue(TEXT("Procedural trail is not mislabeled as an original NS"),Provider&&Provider->DefaultSystems.IsEmpty());
        TestTrue(TEXT("Actual contact and explicit no-contact scenarios are independently registered"),Provider
            &&Provider->Scenarios.Contains(EMCPerkPreviewScenario::Target)&&Provider->Scenarios.Contains(EMCPerkPreviewScenario::Timeout));
        if(Provider&&Provider->Create) {
            auto Adapter=Provider->Create();FMCPerkPreviewContext Empty;Empty.Scenario=EMCPerkPreviewScenario::Target;FString Error;
            TestFalse(TEXT("Missing actual hero/world cannot fake a food contact"),Adapter->Prepare(Empty,Error));
            TestFalse(TEXT("Rejected real-contact scenario explains missing context"),Error.IsEmpty());
        }
    }
    return !HasAnyErrors();
}
#endif
