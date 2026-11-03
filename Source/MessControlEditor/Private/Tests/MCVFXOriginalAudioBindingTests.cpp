#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "MCVFXProfileBinding.h"
#include "MCVFXSourceContracts.h"
#include "MCVFXPresentationLibrary.h"
#include "MCPerkVFXProfile.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace MCOriginalAudioBindingTests
{
const FName Source(TEXT("Tool.Brush")),Known(TEXT("Contact")),Unknown(TEXT("Tests.UnmappedEvent"));
template<class T>T* NewAsset(const FString& PackageName)
{
    auto* Package=CreatePackage(*PackageName);
    auto* Object=NewObject<T>(Package,*FPackageName::GetLongPackageAssetName(PackageName),RF_Public|RF_Standalone|RF_Transactional);
    FAssetRegistryModule::AssetCreated(Object);Package->MarkPackageDirty();return Object;
}
bool Save(UObject* Object)
{Object->MarkPackageDirty();return UEditorLoadingAndSavingUtils::SavePackages({Object->GetOutermost()},true);}
struct FScopedBinding
{
    FMCVFXCanonicalProfileState Original;
    bool bRestore=false;
    bool bMounted=false;
    FString MountRoot,ContentDirectory;
    TArray<TWeakObjectPtr<UObject>> Assets;
    bool Mount()
    {
        const FString ID=FGuid::NewGuid().ToString(EGuidFormats::Digits);
        MountRoot=TEXT("/Game/__MCAudioBindingQA_")+ID+TEXT("/");
        ContentDirectory=FPaths::ConvertRelativePathToFull(TEXT("E:/CodexTemp/MessControl/AudioBindingPolicy/")+ID+TEXT("/"));
        FPaths::NormalizeDirectoryName(ContentDirectory);ContentDirectory+=TEXT("/");
        if(!IFileManager::Get().MakeDirectory(*ContentDirectory,true))return false;
        FPackageName::RegisterMountPoint(MountRoot,ContentDirectory);bMounted=true;
        FString Probe=FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(MountRoot+TEXT("DA_PathProbe"),FPackageName::GetAssetPackageExtension()));
        FPaths::NormalizeFilename(Probe);
        if(!Probe.StartsWith(ContentDirectory,ESearchCase::IgnoreCase)){Cleanup();return false;}
        return true;
    }
    void Cleanup()
    {
        if(bRestore)MCVFXProfileBindings::RegisterLibrarySource(Source,Original.OwnerAsset,Original.OwnerKey,Original.AllowedEvents);
        bRestore=false;
        for(const auto& Weak:Assets)if(auto* Asset=Weak.Get()) {
            FAssetRegistryModule::AssetDeleted(Asset);Asset->GetOutermost()->SetDirtyFlag(false);
            Asset->ClearFlags(RF_Public|RF_Standalone);Asset->SetFlags(RF_Transient);Asset->MarkAsGarbage();
        }
        Assets.Reset();
        if(bMounted)FPackageName::UnRegisterMountPoint(MountRoot,ContentDirectory);
        bMounted=false;
        // Owned package files remain as E: validation evidence. No generated
        // package is written into project Content or left in its live catalogue.
    }
    ~FScopedBinding(){Cleanup();}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXOriginalAudioAssignPolicy,
    "MessControl.VFX.ProfileBinding.OriginalAudioPolicyUsesExactSourceEvent",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXOriginalAudioAssignPolicy::RunTest(const FString&)
{
    using namespace MCOriginalAudioBindingTests;
    if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Run outside PIE; only new isolated QA packages are saved."));return false;}
    FScopedBinding Fixture;FString Error;
    if(!TestTrue(TEXT("Capture real saved Brush binding"),MCVFXProfileBindings::ReadCanonical(Source,Fixture.Original,Error)))
    {AddError(Error);return false;}
    TestTrue(TEXT("Known Brush contact has the actual native suppression gate"),MCVFXSourceContracts::CanSuppressOriginalSFX(Source,Known));
    TestFalse(TEXT("An unrelated event has no inferred source-wide suppression"),MCVFXSourceContracts::CanSuppressOriginalSFX(Source,Unknown));
    if(!TestTrue(TEXT("Register isolated E-only package mount"),Fixture.Mount()))return false;
    const FString Root=Fixture.MountRoot;
    auto* Initial=NewAsset<UMCPerkVFXProfile>(Root/TEXT("DA_Initial"));
    auto* Owner=NewAsset<UMCVFXPresentationLibrary>(Root/TEXT("DA_IsolatedOwner"));Owner->Profiles.Add(Source,Initial);
    Fixture.Assets.Add(Initial);Fixture.Assets.Add(Owner);
    if(!TestTrue(TEXT("Save new isolated fixture owner/profile only"),Save(Initial)&&Save(Owner)))return false;
    // The extra allowed event is deliberately metadata-only in this isolated
    // fixture. It exercises exact policy lookup instead of a source-wide bool.
    MCVFXProfileBindings::RegisterLibrarySource(Source,FSoftObjectPath(Owner),Source,{Known,Unknown});Fixture.bRestore=true;
    FMCVFXCanonicalProfileState Expected;
    if(!TestTrue(TEXT("Read isolated saved source state"),MCVFXProfileBindings::ReadCanonical(Source,Expected,Error)))return false;
    FMCEventSFXSlot Disabled;Disabled.Mode=EMCEventSFXMode::Disabled;
    auto* Mapped=NewAsset<UMCPerkVFXProfile>(Root/TEXT("DA_MappedDisabled"));Mapped->SFXSlots.Add(Known,Disabled);
    Fixture.Assets.Add(Mapped);
    if(!TestTrue(TEXT("Save new mapped-audio candidate"),Save(Mapped)))return false;
    FMCVFXProfileCommitResult Result;
    const bool Assigned=MCVFXProfileBindings::AssignSavedProfile(Mapped,Expected,Result,Error);
    if(!TestTrue(*FString::Printf(TEXT("Actual writer accepts wired source/event: %s"),*Error),Assigned))return false;
    TestTrue(TEXT("Known native suppression commits to isolated owner"),Result.bSaved&&Owner->Profiles.FindChecked(Source).Get()==Mapped);
    FMCVFXCanonicalProfileState AfterMapped;
    if(!TestTrue(TEXT("Capture isolated owner after accepted assignment"),MCVFXProfileBindings::ReadCanonical(Source,AfterMapped,Error)))return false;
    auto* Unmapped=NewAsset<UMCPerkVFXProfile>(Root/TEXT("DA_UnmappedDisabled"));Unmapped->SFXSlots.Add(Unknown,Disabled);
    Fixture.Assets.Add(Unmapped);
    if(!TestTrue(TEXT("Save isolated unknown-event candidate"),Save(Unmapped)))return false;
    TestFalse(TEXT("Actual writer refuses unknown event despite another wired event"),MCVFXProfileBindings::AssignSavedProfile(Unmapped,AfterMapped,Result,Error));
    TestFalse(TEXT("Refusal has an explicit native policy reason"),Error.IsEmpty());
    TestFalse(TEXT("Unknown-event refusal never mutates owner assignment"),Result.bChangedInMemory);
    FMCVFXCanonicalProfileState AfterRefusal;
    TestTrue(TEXT("Unknown-event refusal preserves saved fixture owner bytes/profile"),MCVFXProfileBindings::ReadCanonical(Source,AfterRefusal,Error)
        &&AfterRefusal.OwnerDigest==AfterMapped.OwnerDigest&&AfterRefusal.Profile==AfterMapped.Profile);
    Fixture.Cleanup();
    FMCVFXCanonicalProfileState Restored;
    TestTrue(TEXT("Real user source owner/profile bytes unchanged"),MCVFXProfileBindings::ReadCanonical(Source,Restored,Error)
        &&Restored.OwnerDigest==Fixture.Original.OwnerDigest&&Restored.ProfileDigest==Fixture.Original.ProfileDigest
        &&Restored.ProfileValueDigest==Fixture.Original.ProfileValueDigest);
    return !HasAnyErrors();
}
#endif
