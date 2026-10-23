#include "MCCosmeticCatalog.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCosmeticLoadoutValidationTest,
    "MessControl.Hub.CosmeticsRejectInvalidSlotSelections",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FMCCosmeticLoadoutValidationTest::RunTest(const FString& Parameters)
{
    auto* Table=NewObject<UDataTable>();Table->RowStruct=FMCCosmeticCatalogRow::StaticStruct();
    auto* Mesh=NewObject<UStaticMesh>();
    FMCCosmeticCatalogRow Hat;Hat.Slot=UMCCosmeticCatalog::HatSlot;Hat.Mesh=Mesh;
    FMCCosmeticCatalogRow Face=Hat;Face.Slot=UMCCosmeticCatalog::FaceSlot;
    Table->AddRow(TEXT("TestHat"),Hat);Table->AddRow(TEXT("TestFace"),Face);
    FMCCharacterCosmeticLoadout Loadout;FText Error;
    TestTrue(TEXT("Default character needs no catalog or accessory"),UMCCosmeticCatalog::ValidateLoadout(nullptr,Loadout,Error));
    Loadout.SetItemForSlot(TEXT("Hat"),TEXT("TestHat"));Loadout.SetItemForSlot(TEXT("Face"),TEXT("TestFace"));
    TestTrue(TEXT("Two distinct fitted slots are compatible"),UMCCosmeticCatalog::ValidateLoadout(Table,Loadout,Error));
    Loadout.SetItemForSlot(TEXT("Hat"),TEXT("TestHat"));
    TestEqual(TEXT("Repeated selection keeps one record per slot"),Loadout.Slots.Num(),2);
    TestEqual(TEXT("Face selection survives a hat update"),Loadout.ItemForSlot(TEXT("Face")),FName(TEXT("TestFace")));
    const FMCCharacterCosmeticLoadout Valid=Loadout;
    // TArray rejects an Add whose argument aliases its own allocation.
    // Copy the malformed fixture first so validation receives the duplicate.
    const FMCCosmeticSlotSelection DuplicateSlot=Loadout.Slots[0];
    Loadout.Slots.Add(DuplicateSlot);
    TestFalse(TEXT("Duplicate slots cannot smuggle another item"),UMCCosmeticCatalog::ValidateLoadout(Table,Loadout,Error));
    Loadout=Valid;Loadout.SetItemForSlot(TEXT("Hat"),TEXT("TestFace"));
    TestFalse(TEXT("Face item cannot be worn as a hat"),UMCCosmeticCatalog::ValidateLoadout(Table,Loadout,Error));
    Loadout=Valid;Loadout.SetItemForSlot(TEXT("Hat"),TEXT("MissingItem"));
    TestFalse(TEXT("Unknown items are rejected before presentation"),UMCCosmeticCatalog::ValidateLoadout(Table,Loadout,Error));
    Loadout=Valid;Hat.bEnabled=false;Table->AddRow(TEXT("TestHat"),Hat);
    TestFalse(TEXT("Disabled items cannot be equipped"),UMCCosmeticCatalog::ValidateLoadout(Table,Loadout,Error));
    Loadout=Valid;Loadout.SetItemForSlot(TEXT("Hat"),NAME_None);
    TestTrue(TEXT("Removing an accessory retains the other slot"),UMCCosmeticCatalog::ValidateLoadout(Table,Loadout,Error));
    TestEqual(TEXT("Unequipped hat returns the default appearance"),Loadout.ItemForSlot(TEXT("Hat")),NAME_None);
    FMCCosmeticCatalogRow Extra=Face;Extra.Slot=TEXT("FutureBackSlot");Table->AddRow(TEXT("TestFuture"),Extra);
    Loadout.SetItemForSlot(TEXT("FutureBackSlot"),TEXT("TestFuture"));
    TestTrue(TEXT("An authored future slot works without changing an enum"),UMCCosmeticCatalog::ValidateLoadout(Table,Loadout,Error));
    TestEqual(TEXT("Catalog queries keep slots separate"),UMCCosmeticCatalog::GetItemsForSlot(Table,TEXT("Face")).Num(),1);
    return true;
}
#endif
