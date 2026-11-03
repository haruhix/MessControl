#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCVFXPresentationComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPerkPresentationOwnedActivation,
    "MessControl.Passives.Presentation.OwnedActivationAndPawnReplacement",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPerkPresentationOwnedActivation::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::EditorPreview,false);
    GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
    struct FWorldCleanup {UWorld* W;~FWorldCleanup(){GEngine->DestroyWorldContext(W);W->DestroyWorld(false);}} Cleanup{World};
    auto* State=World->SpawnActor<AMCPlayerState>();auto* Hero=World->SpawnActor<AMCToothCharacter>();
    Hero->SetPlayerState(State);const FName ID(TEXT("B1_LightFoot"));
    if(!TestNotNull(TEXT("Persistent native perk owner"),State->Perks.Get())
        || !TestNotNull(TEXT("One pawn-owned passive dispatcher"),Hero->PassivePresentation.Get()))return false;
    auto* Perks=State->Perks.Get();
    if(!TestTrue(TEXT("Explicit native attach operation prepares the currently empty loadout"),Perks->EnsurePresentationForPawn()))return false;
    auto* CanonicalTable=Perks->GetPerkTable();
    if(!TestNotNull(TEXT("Canonical saved table is available for an isolated runtime copy"),CanonicalTable))return false;
    TStrongObjectPtr<UDataTable> CanonicalKeep(CanonicalTable);
    const auto* CanonicalRow=CanonicalTable->FindRow<FMCPerkDefinition>(ID,TEXT("PresentationLifecycleFixture"),false);
    if(!TestNotNull(TEXT("Canonical passive definition exists"),CanonicalRow))return false;
    const auto OriginalProfilePath=CanonicalRow->VFXProfile.ToSoftObjectPath();
    auto* FixtureTable=DuplicateObject<UDataTable>(CanonicalTable,GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(),UDataTable::StaticClass(),TEXT("PresentationLifecycleCatalog")));
    FixtureTable->ClearFlags(RF_Public|RF_Standalone);FixtureTable->SetFlags(RF_Transient);
    Perks->LoadedTable=FixtureTable; // Test-owned catalog only; the saved table is never modified.
    auto* Profile=NewObject<UMCPerkVFXProfile>();FMCPerkVFXSlot Disabled;Disabled.Mode=EMCPerkVFXSlotMode::Disabled;
    TStrongObjectPtr<UMCPerkVFXProfile> OverrideKeep(Profile);
    Profile->Slots.Add(TEXT("State"),Disabled);Profile->Slots.Add(TEXT("Equipped"),Disabled);FString Error;
    const TArray<FName> Events={TEXT("Equipped"),TEXT("State")};
    if(!TestTrue(TEXT("Preview override may prepare before ownership"),Hero->PassivePresentation->PrepareScopedOverride(ID,Events,Profile,Error)))
    {AddError(Error);return false;}
    int32 StateEvents=0,EquippedEvents=0;bool EveryReceiptOwned=true,EveryOverrideRetained=true;
    Hero->PassivePresentation->OnSourceEventAccepted.AddLambda([&](FName Source,FName Event,EMCVFXPresentationResult Result){
        if(Source!=ID)return;
        EveryReceiptOwned&=Perks->GetStacks(ID)>0;
        EveryOverrideRetained&=Result==EMCVFXPresentationResult::Suppressed;
        if(Event==TEXT("State"))++StateEvents;if(Event==TEXT("Equipped"))++EquippedEvents;
    });
    TestEqual(TEXT("Preparation alone does not pretend the perk was acquired"),StateEvents+EquippedEvents,0);
    if(!TestTrue(TEXT("Native accepted grant succeeds"),Perks->ServerGrantPerk(ID)))return false;
    TestEqual(TEXT("First actually owned activation emits State once even though its profile was already prepared"),StateEvents,1);
    TestEqual(TEXT("Accepted native OnApplied emits Equipped once"),EquippedEvents,1);
    TestTrue(TEXT("Every observed lifecycle receipt follows actual ownership"),EveryReceiptOwned);
    TestTrue(TEXT("Canonical grant preparation did not overwrite the scoped preview"),EveryOverrideRetained);
    Perks->EnsurePresentationForPawn();
    TestEqual(TEXT("Repeated readiness calls do not re-emit lifecycle effects"),StateEvents+EquippedEvents,2);
    auto* Replacement=World->SpawnActor<AMCToothCharacter>();int32 ReplacementState=0,ReplacementEquipped=0;
    if(!Replacement->PassivePresentation->PrepareScopedOverride(ID,Events,Profile,Error)){AddError(Error);return false;}
    Replacement->PassivePresentation->OnSourceEventAccepted.AddLambda([&](FName Source,FName Event,EMCVFXPresentationResult){
        if(Source==ID){if(Event==TEXT("State"))++ReplacementState;if(Event==TEXT("Equipped"))++ReplacementEquipped;}
    });
    Replacement->SetPlayerState(State);
    TestTrue(TEXT("Native new-pawn attach restores the existing perk presentation"),Perks->EnsurePresentationForPawn());
    TestEqual(TEXT("Replacement emits one State for its real inherited ownership"),ReplacementState,1);
    TestEqual(TEXT("Replacement does not fabricate another acquisition"),ReplacementEquipped,0);
    TestEqual(TEXT("Presentation lifecycle did not add a second perk stack"),Perks->GetStacks(ID),1);
    TArray<EMCVFXPresentationResult> RegrantResults;
    Replacement->PassivePresentation->OnSourceEventAccepted.AddLambda([&](FName Source,FName Event,EMCVFXPresentationResult Result){
        if(Source==ID && (Event==TEXT("State") || Event==TEXT("Equipped")))RegrantResults.Add(Result);
    });
    if(!TestTrue(TEXT("Native removal succeeds on the same pawn"),Perks->RemovePerk(ID)))return false;
    TestFalse(TEXT("Removal forgets the previously owned prepared source and its override"),Replacement->PassivePresentation->IsSourcePrepared(ID));
    auto* NewProfile=NewObject<UMCPerkVFXProfile>(); // Changed canonical fixture policy: native default instead of old Disabled override.
    TStrongObjectPtr<UMCPerkVFXProfile> ChangedProfileKeep(NewProfile);
    auto* FixtureRow=FixtureTable->FindRow<FMCPerkDefinition>(ID,TEXT("PresentationLifecycleRegrant"),false);
    FixtureRow->VFXProfile=NewProfile;
    if(!TestTrue(TEXT("Native regrant reads the changed canonical profile on the same pawn"),Perks->ServerGrantPerk(ID)))return false;
    TestTrue(TEXT("State and Equipped use the changed profile, not the removed Disabled snapshot"),
        RegrantResults.Num()==2 && RegrantResults[0]==EMCVFXPresentationResult::UseDefault && RegrantResults[1]==EMCVFXPresentationResult::UseDefault);
    TestEqual(TEXT("The original saved catalog profile reference is unchanged"),CanonicalRow->VFXProfile.ToSoftObjectPath(),OriginalProfilePath);
    return true;
}
#endif
