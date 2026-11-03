#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "MCVFXActionPacks.h"
#include "MCVFXRecipePreset.h"
#include "MCPerkVFXProfile.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "NiagaraSystem.h"
#include "UObject/Package.h"

namespace MCActionPackTests
{
template<class T>T* NewFixture(const FString& PackageName)
{
    auto* Package=CreatePackage(*PackageName);
    auto* Object=NewObject<T>(Package,*FPackageName::GetLongPackageAssetName(PackageName),RF_Public|RF_Standalone|RF_Transactional);
    FAssetRegistryModule::AssetCreated(Object);Package->MarkPackageDirty();return Object;
}
template<class T>T* SaveFixture(const FString& PackageName)
{
    auto* Object=NewFixture<T>(PackageName);auto* Package=Object->GetOutermost();
    return UEditorLoadingAndSavingUtils::SavePackages({Package},true)?Object:nullptr;
}
FString Digest(const UObject* Object)
{
    FString File;return FPackageName::DoesPackageExist(Object->GetOutermost()->GetName(),&File)?LexToString(FMD5Hash::HashFile(*File)):FString();
}
UMCVFXActionPack* Shape()
{
    auto* Pack=NewObject<UMCVFXActionPack>();Pack->PackID=FGuid::NewGuid();Pack->SourceID=TEXT("EditorOnly.PackFixture");
    Pack->EventIDs={TEXT("ActualImpact")};Pack->Label=FText::FromString(TEXT("Portable fixture"));
    Pack->RuntimeProfile=FSoftObjectPath(TEXT("/Game/Tests/VFXPractice/PackFixture/DA_Profile.DA_Profile"));
    Pack->RecipeMasters.Add(TEXT("ActualImpact"),TSoftObjectPtr<UMCVFXRecipePreset>(FSoftObjectPath(TEXT("/Game/Tests/VFXPractice/PackFixture/DA_Master.DA_Master"))));
    FMCVFXActionPackDependencyFingerprint Fingerprint;Fingerprint.PackageName=TEXT("/Game/Tests/VFXPractice/PackFixture/DA_Profile");
    Fingerprint.ContentDigest=TEXT("0123456789abcdef0123456789abcdef");Pack->Dependencies.Add(Fingerprint);return Pack;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXActionPackPortableShape,"MessControl.VFX.ActionPacks.PortableSchemaAndStableLayout",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXActionPackPortableShape::RunTest(const FString&)
{
    using namespace MCActionPackTests;auto* Pack=Shape();FString Error;
    FMCVFXEventGraphLayout Event;Event.Event=TEXT("ActualImpact");Event.bHasCardPosition=true;Event.CardPosition=FVector2D(120,240);
    FMCComposerGroupLayout Group;Group.GroupID=FGuid::NewGuid();Event.Groups.Add(Group);
    const FGuid SoundID=FGuid::NewGuid();Event.SFXLayerPositions.Add(SoundID,FVector2D(40,80));Pack->Layout.Events.Add(Event);
    TestTrue(TEXT("Manifest and transaction state are explicitly editor-only"),Pack->IsEditorOnly()&&GetDefault<UMCVFXActionLayoutState>()->IsEditorOnly());
    TestTrue(TEXT("Portable manifest shape supports existing stable group/SFX IDs without runtime copies"),Pack->ValidateStructure(Error));
    FMCComposerGroupLayout DefaultLayout;Pack->Layout.Events[0].Groups.Add(DefaultLayout);
    TestTrue(TEXT("One implicit Default projection persists without creating a runtime group"),Pack->ValidateStructure(Error));
    Pack->Layout.Events[0].Groups.Add(DefaultLayout);
    TestFalse(TEXT("Duplicate implicit Default projections are rejected"),Pack->ValidateStructure(Error));
    Pack->Layout.Events[0].Groups.Pop();
    auto* Capacity=DuplicateObject<UMCVFXActionPack>(Pack,GetTransientPackage());
    for(int32 Index=1;Index<32;++Index){FMCComposerGroupLayout Additional;Additional.GroupID=FGuid::NewGuid();Capacity->Layout.Events[0].Groups.Add(Additional);}
    TestTrue(TEXT("All 32 explicit groups can retain their additional pinned Default projection"),Capacity->ValidateStructure(Error));
    FMCComposerGroupLayout Excess;Excess.GroupID=FGuid::NewGuid();Capacity->Layout.Events[0].Groups.Add(Excess);
    TestFalse(TEXT("Implicit Default does not raise the limit on explicit runtime groups"),Capacity->ValidateStructure(Error));
    auto* Copy=DuplicateObject<UMCVFXActionPack>(Pack,GetTransientPackage());
    TestEqual(TEXT("Group identity survives a working-copy duplicate"),Copy->Layout.Events[0].Groups[0].GroupID,Group.GroupID);
    TestTrue(TEXT("SFX graph position retains its real instance ID"),Copy->Layout.Events[0].SFXLayerPositions.Contains(SoundID));
    Copy->RuntimeProfile=FSoftObjectPath(TEXT("/Engine/Tests/DA_Profile.DA_Profile"));
    TestFalse(TEXT("Manifest/profile/master roots cannot escape /Game"),Copy->ValidateStructure(Error));
    Copy->RuntimeProfile=Pack->RuntimeProfile;Copy->EventIDs.Add(TEXT("ActualImpact"));
    TestFalse(TEXT("Duplicate event schema is rejected"),Copy->ValidateStructure(Error));
    Copy->EventIDs=Pack->EventIDs;Copy->Layout.Events[0].Event=TEXT("SyntheticOtherEvent");
    TestFalse(TEXT("Layout cannot introduce a synthetic gameplay event"),Copy->ValidateStructure(Error));
    Copy->Layout=Pack->Layout;Copy->Dependencies[0].PackageName=TEXT("E:/Working/asset.uasset");
    TestFalse(TEXT("Absolute machine paths cannot enter dependency fingerprints"),Copy->ValidateStructure(Error));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXActionPackSavedRoundtrip,"MessControl.VFX.ActionPacks.GroupOnlySavedRoundtripAndImmutableVersions",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXActionPackSavedRoundtrip::RunTest(const FString&)
{
    using namespace MCActionPackTests;if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Run outside PIE."));return false;}
    FString Error;const FName Event(TEXT("ActualImpact"));const FString Root=TEXT("/Game/Tests/VFXPractice/Profiles/GenericBindingQA/Packs_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto* Profile=SaveFixture<UMCPerkVFXProfile>(Root/TEXT("DA_Profile"));
    auto* Master=SaveFixture<UMCVFXRecipePreset>(Root/TEXT("DA_Master"));
    if(!TestNotNull(TEXT("Isolated runtime profile saved"),Profile)||!TestNotNull(TEXT("Real empty event master saved"),Master))return false;
    FMCEventPresentationGroup A,B;A.Name=TEXT("SimultaneousA");B.Name=TEXT("SimultaneousB");
    Profile->EventGroups.FindOrAdd(Event).Groups={A,B};Profile->Slots.FindOrAdd(Event).Mode=EMCPerkVFXSlotMode::UseDefault;
    Profile->MarkPackageDirty();if(!TestTrue(TEXT("Save group-only source with explicit native-default policy"),UEditorLoadingAndSavingUtils::SavePackages({Profile->GetOutermost()},true)))return false;
    const FString ProfileHash=Digest(Profile),MasterHash=Digest(Master);
    FMCVFXActionPackSaveRequest Request;Request.ManifestPackagePath=Root/TEXT("DA_PackA");Request.SourceID=TEXT("EditorOnly.PackFixture");
    Request.EventIDs={Event};Request.Label=FText::FromString(TEXT("Group only A"));Request.VersionLabel=TEXT("A");
    Request.RuntimeProfile=Profile;Request.RecipeMasters.Add(Event,Master);
    FMCVFXEventGraphLayout Layout;Layout.Event=Event;Layout.bHasCardPosition=true;Layout.CardPosition=FVector2D(200,100);
    FMCComposerGroupLayout GA,GB,DefaultLayout;GA.GroupID=A.ID;GB.GroupID=B.ID;GB.Position=FVector2D(400,0);DefaultLayout.Position=FVector2D(0,300);
    Layout.Groups={GA,GB,DefaultLayout};Request.Layout.Events.Add(Layout);
    FMCVFXActionPackSaveResult Saved;
    if(!TestTrue(*FString::Printf(TEXT("Save manifest last without canonical assignment: %s"),*Error),MCVFXActionPacks::SaveNew(Request,Saved,Error))) {AddError(Error);return false;}
    TestTrue(TEXT("Actual new manifest is saved and retained"),Saved.bSaved&&Saved.Pack.IsValid());
    TestTrue(TEXT("Manifest package is explicitly editor-only"),Saved.Pack->GetOutermost()->HasAnyPackageFlags(PKG_EditorOnly));
    TestEqual(TEXT("Save Pack does not rewrite its existing runtime profile"),Digest(Profile),ProfileHash);
    TestEqual(TEXT("Save Pack does not rewrite its existing recipe master"),Digest(Master),MasterHash);
    TestEqual(TEXT("Zero Niagara layers stay zero"),Master->Layers.Num(),0);
    FMCVFXActionPackLoaded Loaded;
    if(!TestTrue(*FString::Printf(TEXT("Load validated saved snapshot: %s"),*Error),MCVFXActionPacks::Load(Saved.Pack.Get(),Request.SourceID,{Event},1,0,Loaded,Error))) {AddError(Error);return false;}
    TestTrue(TEXT("Load retains saved refs for Session to clone, without replacing them"),Loaded.RuntimeProfile.Get()==Profile&&Loaded.RecipeMasters.FindChecked(Event).Get()==Master);
    TestTrue(TEXT("Both independent groups remain enabled"),Profile->EventGroups[Event].Groups[0].bEnabled&&Profile->EventGroups[Event].Groups[1].bEnabled);
    TestTrue(TEXT("Layout-only state never turns UseDefault into Disabled"),Profile->Slots[Event].Mode==EMCPerkVFXSlotMode::UseDefault);
    TestEqual(TEXT("Stable group identity survives save/load"),Loaded.Layout.Events[0].Groups[1].GroupID,B.ID);
    TestFalse(TEXT("Saved implicit Default projection stays implicit without a runtime group ID"),Loaded.Layout.Events[0].Groups[2].GroupID.IsValid());
    auto* PreviousLoaded=Loaded.Pack.Get();
    TestFalse(TEXT("Cross-source saved versions are rejected"),MCVFXActionPacks::Load(Saved.Pack.Get(),TEXT("EditorOnly.OtherSource"),{Event},1,0,Loaded,Error));
    TestTrue(TEXT("Failed Load preserves previous retained snapshot"),Loaded.Pack.Get()==PreviousLoaded);
    const FString PackHash=Digest(Saved.Pack.Get());FMCVFXActionPackSaveResult Collision;
    TestFalse(TEXT("Saved version collision is refused instead of overwritten"),MCVFXActionPacks::SaveNew(Request,Collision,Error));
    TestEqual(TEXT("Collision preserves the saved manifest bytes"),Digest(Saved.Pack.Get()),PackHash);
    TestEqual(TEXT("Collision creates no intermediate asset"),Collision.CreatedAssets.Num(),0);
    Request.ManifestPackagePath=Root/TEXT("DA_PackB");Request.Label=FText::FromString(TEXT("Group only B"));Request.VersionLabel=TEXT("B");
    Request.Layout.Events[0].Groups[0].Position=FVector2D(80,120);FMCVFXActionPackSaveResult BPack;
    if(!TestTrue(TEXT("A second saved version reuses clean runtime assets for layout-only changes"),MCVFXActionPacks::SaveNew(Request,BPack,Error))) {AddError(Error);return false;}
    TestTrue(TEXT("A/B are distinct manifests referencing the same immutable runtime profile"),BPack.Pack->PackID!=Saved.Pack->PackID&&BPack.Pack->RuntimeProfile==Saved.Pack->RuntimeProfile);
    TestEqual(TEXT("Saving B preserves A"),Digest(Saved.Pack.Get()),PackHash);
    // Modify only the isolated fixture master to prove a real saved shared-resource change is reported.
    Master->PresetName=TEXT("Changed isolated shared master");Master->MarkPackageDirty();
    if(!TestTrue(TEXT("Save isolated source edit"),UEditorLoadingAndSavingUtils::SavePackages({Master->GetOutermost()},true)))return false;
    TArray<FString> Warnings;TestTrue(TEXT("Dependency validation remains queryable"),MCVFXActionPacks::CheckDependencies(Saved.Pack.Get(),Warnings,Error));
    TestTrue(TEXT("Changed saved shared source is an explicit warning"),!Warnings.IsEmpty());
    TestFalse(TEXT("A/B Load cannot silently accept the changed shared source"),MCVFXActionPacks::Load(Saved.Pack.Get(),Request.SourceID,{Event},1,0,Loaded,Error));
    Request.ManifestPackagePath=Root/TEXT("DA_StaleBaseline");Request.ExpectedDependencies=Saved.Pack->Dependencies;FMCVFXActionPackSaveResult Stale;
    TestFalse(TEXT("Save rejects a changed captured source baseline before creating a manifest"),MCVFXActionPacks::SaveNew(Request,Stale,Error));
    TestEqual(TEXT("Stale baseline creates no intermediate assets"),Stale.CreatedAssets.Num(),0);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXActionPackMasterRuntime,"MessControl.VFX.ActionPacks.CompositionMasterMatchesActualRuntimeIncludingDisabledLayers",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXActionPackMasterRuntime::RunTest(const FString&)
{
    using namespace MCActionPackTests;if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Run outside PIE."));return false;}
    auto* System=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Rocket_Start.NS_Rocket_Start"));
    if(!TestNotNull(TEXT("A real saved Niagara source is available without editing it"),System))return false;
    const FString SourceHash=Digest(System),Root=TEXT("/Game/Tests/VFXPractice/Profiles/GenericBindingQA/PackRuntime_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto* Profile=NewFixture<UMCPerkVFXProfile>(Root/TEXT("DA_Profile"));
    auto* Runtime=NewFixture<UMCVFXComposition>(Root/TEXT("DA_Runtime"));
    auto* Master=NewFixture<UMCVFXRecipePreset>(Root/TEXT("DA_Master"));
    FMCVFXCompositionLayer Enabled,Disabled;Enabled.System=System;Disabled=Enabled;Disabled.bEnabled=false;Disabled.StartSeconds=.2f;
    Runtime->Layers={Enabled,Disabled};Master->Layers=Runtime->Layers;Master->DurationSeconds=Runtime->DurationSeconds;Master->TailSeconds=Runtime->TailSeconds;
    for(int32 Index=0;Index<Master->Layers.Num();++Index){FMCVFXRecipeNodeLayout Node;Node.ID=FGuid::NewGuid();Node.Position=FVector2D(Index*300,0);Master->Nodes.Add(Node);}
    const FName Event(TEXT("ActualImpact"));auto& Slot=Profile->Slots.FindOrAdd(Event);Slot.Mode=EMCPerkVFXSlotMode::Composition;Slot.Composition=Runtime;
    if(!TestTrue(TEXT("Save isolated actual runtime and its real event master"),UEditorLoadingAndSavingUtils::SavePackages({Profile->GetOutermost(),Runtime->GetOutermost(),Master->GetOutermost()},true)))return false;
    auto* Pack=Shape();Pack->RuntimeProfile=Profile;Pack->RecipeMasters[Event]=Master;FString Error;TMap<FName,UMCVFXRecipePreset*> Masters;Masters.Add(Event,Master);
    if(!TestTrue(TEXT("Capture selected saved closure"),MCVFXActionPacks::CaptureDependencies(Profile,Masters,Pack->Dependencies,Error))) {AddError(Error);return false;}
    if(!TestTrue(TEXT("Matching composition/master pair is valid"),MCVFXActionPacks::Validate(Pack,Error,false))) {AddError(Error);return false;}
    Master->Layers[1].StartSeconds=.4f;Master->MarkPackageDirty();
    if(!TestTrue(TEXT("Save only isolated disabled-layer mismatch"),UEditorLoadingAndSavingUtils::SavePackages({Master->GetOutermost()},true)))return false;
    TestFalse(TEXT("A disabled layer cannot silently diverge between authoring and actual runtime"),MCVFXActionPacks::Validate(Pack,Error,false));
    TestTrue(TEXT("Mismatch reason names the disabled-layer contract"),Error.Contains(TEXT("including disabled layers")));
    Master->Layers[1].StartSeconds=.2f;Master->DurationSeconds=4.f;Master->MarkPackageDirty();
    if(!TestTrue(TEXT("Save only isolated duration mismatch"),UEditorLoadingAndSavingUtils::SavePackages({Master->GetOutermost()},true)))return false;
    TestFalse(TEXT("Different master/runtime duration is refused"),MCVFXActionPacks::Validate(Pack,Error,false));
    TestEqual(TEXT("The original Niagara source was not rewritten"),Digest(System),SourceHash);return true;
}
#endif
