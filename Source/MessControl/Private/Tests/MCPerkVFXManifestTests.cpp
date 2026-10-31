#if WITH_DEV_AUTOMATION_TESTS
#include "MCPerkVFXProfile.h"
#include "MCPerkTypes.h"
#include "MCPreloadManifest.h"
#include "Engine/DataTable.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPerkVFXManifestTest,
    "MessControl.VFX.PerkProfile.ManifestReachability",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPerkVFXManifestTest::RunTest(const FString&)
{
    auto* Table=NewObject<UDataTable>(GetTransientPackage());
    Table->RowStruct=FMCPerkDefinition::StaticStruct();
    const FSoftObjectPath ProfilePath(TEXT("/Game/Tests/VFXPractice/Profiles/DA_ManifestProbe.DA_ManifestProbe"));
    FMCPerkDefinition Row;Row.VFXProfile=ProfilePath;
    Table->AddRow(TEXT("RocketManifestProbe"),Row);
    TArray<FSoftObjectPath> Paths;
    FMCPreloadManifest::CollectSoftReferences(Table,Paths);
    TestTrue(TEXT("Manifest sees a row's profile before it is loaded"),Paths.Contains(ProfilePath));

    auto* Profile=NewObject<UMCPerkVFXProfile>(GetTransientPackage());
    const FSoftObjectPath SystemPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Rocket_Loop.NS_Rocket_Loop"));
    FMCPerkVFXSlot Slot;Slot.Mode=EMCPerkVFXSlotMode::Replace;Slot.System=SystemPath;
    Profile->Slots.Add(TEXT("Loop"),Slot);Paths.Reset();
    FMCPreloadManifest::CollectSoftReferences(Profile,Paths);
    TestTrue(TEXT("Manifest sees the Niagara reference inside the profile's slot map"),Paths.Contains(SystemPath));
    TestTrue(TEXT("Pure traversal does not resolve the fixture profile"),!ProfilePath.ResolveObject());
    return true;
}
#endif
