#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "MCVFXActionSession.h"
#include "MCVFXNativePresentationSettings.h"
#include "MCVFXPresentationLibrary.h"
#include "MCVFXProfileBinding.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "UObject/Package.h"

namespace
{
FString NativeSaveDigest(const FSoftObjectPath& Asset)
{
    const FString File=FPackageName::LongPackageNameToFilename(Asset.GetLongPackageName(),FPackageName::GetAssetPackageExtension());
    return LexToString(FMD5Hash::HashFile(*File));
}
struct FRestoreNativeCanonicalBinding
{
    FMCVFXCanonicalProfileState Original;
    ~FRestoreNativeCanonicalBinding()
    {MCVFXProfileBindings::RegisterLibrarySource(Original.SourceID,Original.OwnerAsset,Original.OwnerKey,Original.AllowedEvents);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNativeOnlySavedRoundtrip,
    "MessControl.VFX.ActionSession.NativeOnlySaveRoundtripUsesIsolatedOwner",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNativeOnlySavedRoundtrip::RunTest(const FString&)
{
    if(!GEditor || GEditor->PlayWorld){AddError(TEXT("Run outside PIE."));return false;}
    const FName Source(TEXT("Boss.NutKnight.Shield"));FString Error;
    FRestoreNativeCanonicalBinding Restore;
    if(!TestTrue(TEXT("Read actual source owner before fixture"),MCVFXProfileBindings::ReadCanonical(Source,Restore.Original,Error)))return false;
    const FString OriginalDigest=NativeSaveDigest(Restore.Original.OwnerAsset);
    const FString Root=TEXT("/Game/Tests/VFXPractice/Profiles/GenericBindingQA/NativeSave_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString OwnerPackage=Root/TEXT("DA_IsolatedOwner");
    UPackage* Package=CreatePackage(*OwnerPackage);
    auto* Library=NewObject<UMCVFXPresentationLibrary>(Package,TEXT("DA_IsolatedOwner"),RF_Public|RF_Standalone|RF_Transactional);
    Library->Profiles.Add(Source,TSoftObjectPtr<UMCPerkVFXProfile>());
    FAssetRegistryModule::AssetCreated(Library);Package->MarkPackageDirty();
    if(!TestTrue(TEXT("Save an isolated fixture owner"),UEditorLoadingAndSavingUtils::SavePackages({Package},true)))return false;
    MCVFXProfileBindings::RegisterLibrarySource(Source,FSoftObjectPath(Library),Source,Restore.Original.AllowedEvents);
    FMCVFXActionSession Session;
    if(!TestTrue(TEXT("Open fixture source"),Session.Initialize(Source,Error)))return false;
    const auto* Existing=Session.GetNativeDraft()?Session.GetNativeDraft()->FindPart(TEXT("Hit"),TEXT("ShieldContactMagic")):nullptr;
    if(!TestNotNull(TEXT("Actual renderer part exists"),Existing))return false;
    auto Part=*Existing;Part.Tint=FLinearColor(.16f,.73f,.91f);
    if(!TestTrue(TEXT("Native-only edit"),Session.SetNativePart(Session.GetNativeDraftToken(),false,TEXT("Hit"),Part.PartID,Part,Error)))return false;
    if(!TestTrue(*FString::Printf(TEXT("Save native-only draft: %s"),*Error),Session.ApplyAll(Error,Root/TEXT("Saved"))))return false;
    FMCVFXCanonicalProfileState Saved;
    if(!TestTrue(TEXT("Read saved fixture assignment"),MCVFXProfileBindings::ReadCanonical(Source,Saved,Error)))return false;
    auto* Profile=Saved.Profile.LoadSynchronous();
    if(!TestNotNull(TEXT("New runtime profile exists"),Profile))return false;
    TestFalse(TEXT("Native source is serialized even with zero changed Niagara events"),Profile->NativePresentation.IsNull());
    TestTrue(TEXT("All native resources are saved and runtime-compatible"),MCVFXProfileBindings::ValidateSavedProfileResources(Profile,Saved.AllowedEvents,Error));
    Session.Clear();
    if(!TestTrue(TEXT("Reopen through ordinary canonical reader"),Session.Initialize(Source,Error)))return false;
    const auto* Loaded=Session.GetNativeDraft()->FindPart(TEXT("Hit"),Part.PartID);
    TestTrue(TEXT("Reopened draft preserves authored native tint"),Loaded && Loaded->Tint==Part.Tint);
    TestEqual(TEXT("No Niagara layer was fabricated for the native-only edit"),Session.GetLayerCount(TEXT("Hit")),0);
    TestFalse(TEXT("Reopened saved native-only draft is clean"),Session.IsDirty());
    TestEqual(TEXT("The real mouth library was never written"),NativeSaveDigest(Restore.Original.OwnerAsset),OriginalDigest);
    Session.Clear();return true;
}
#endif
