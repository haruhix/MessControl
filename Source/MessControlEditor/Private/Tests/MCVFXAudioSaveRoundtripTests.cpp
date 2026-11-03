#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "MCVFXActionSession.h"
#include "MCVFXPresentationLibrary.h"
#include "MCVFXProfileBinding.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Sound/SoundBase.h"
#include "UObject/Package.h"

namespace
{
FString AudioSaveDigest(const FSoftObjectPath& Asset)
{
    return LexToString(FMD5Hash::HashFile(*FPackageName::LongPackageNameToFilename(
        Asset.GetLongPackageName(),FPackageName::GetAssetPackageExtension())));
}
struct FRestoreAudioCanonicalBinding
{
    FMCVFXCanonicalProfileState Original;
    ~FRestoreAudioCanonicalBinding()
    {MCVFXProfileBindings::RegisterLibrarySource(Original.SourceID,Original.OwnerAsset,Original.OwnerKey,Original.AllowedEvents);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCAudioOnlySavedRoundtrip,
    "MessControl.Audio.Composer.AudioOnlySaveRoundtripUsesIsolatedOwner",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCAudioOnlySavedRoundtrip::RunTest(const FString&)
{
    if(!GEditor || GEditor->PlayWorld){AddError(TEXT("Run outside PIE."));return false;}
    const FName Source(TEXT("EditorOnly.NutEnemyDeath")),Event(TEXT("Death"));FString Error;
    FRestoreAudioCanonicalBinding Restore;
    if(!TestTrue(TEXT("Read original source owner"),MCVFXProfileBindings::ReadCanonical(Source,Restore.Original,Error)))return false;
    const FString OriginalDigest=AudioSaveDigest(Restore.Original.OwnerAsset);
    const FString Root=TEXT("/Game/Tests/VFXPractice/Profiles/GenericBindingQA/AudioSave_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString OwnerPackage=Root/TEXT("DA_IsolatedOwner");UPackage* Package=CreatePackage(*OwnerPackage);
    auto* Library=NewObject<UMCVFXPresentationLibrary>(Package,TEXT("DA_IsolatedOwner"),RF_Public|RF_Standalone|RF_Transactional);
    Library->Profiles.Add(Source,TSoftObjectPtr<UMCPerkVFXProfile>());
    FAssetRegistryModule::AssetCreated(Library);Package->MarkPackageDirty();
    if(!TestTrue(TEXT("Save isolated canonical owner"),UEditorLoadingAndSavingUtils::SavePackages({Package},true)))return false;
    MCVFXProfileBindings::RegisterLibrarySource(Source,FSoftObjectPath(Library),Source,Restore.Original.AllowedEvents);
    FMCVFXActionSession Session;
    if(!TestTrue(TEXT("Open isolated source"),Session.Initialize(Source,Error)))return false;
    const int32 OriginalVFXLayers=Session.GetLayerCount(Event);
    auto* Sound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Audio/Rewards/S_TaskCoins.S_TaskCoins"));
    if(!TestNotNull(TEXT("Real saved sound"),Sound))return false;
    const FGuid ID=Session.AddSFXLayer(Session.GetSFXDraftToken(),Event,Sound,FGuid(),Error);
    if(!TestTrue(TEXT("Add audio without any Niagara edit"),ID.IsValid()))return false;
    auto Layer=Session.GetSFXSlot(Event)->Layers[0];Layer.Volume=.42f;Layer.DelaySeconds=.08f;
    if(!TestTrue(TEXT("Author audio instance values"),Session.SetSFXLayer(Session.GetSFXDraftToken(),Event,ID,Layer,Error)))return false;
    if(!TestTrue(*FString::Printf(TEXT("Apply audio-only draft: %s"),*Error),Session.ApplyAll(Error,Root/TEXT("Saved"))))return false;
    FMCVFXCanonicalProfileState Saved;
    if(!TestTrue(TEXT("Read saved assignment"),MCVFXProfileBindings::ReadCanonical(Source,Saved,Error)))return false;
    auto* Profile=Saved.Profile.LoadSynchronous();
    if(!TestNotNull(TEXT("Audio-only runtime profile exists"),Profile))return false;
    TestTrue(TEXT("Saved resource closure is valid"),MCVFXProfileBindings::ValidateSavedProfileResources(Profile,Saved.AllowedEvents,Error));
    Session.Clear();
    if(!TestTrue(TEXT("Reopen ordinary source"),Session.Initialize(Source,Error)))return false;
    const auto* Loaded=Session.GetSFXSlot(Event);
    if(TestNotNull(TEXT("Audio event survives reopen"),Loaded))
    {
        TestEqual(TEXT("Additive policy survives"),Loaded->Mode,EMCEventSFXMode::Add);
        if(TestEqual(TEXT("One sound instance"),Loaded->Layers.Num(),1))
        {
            TestEqual(TEXT("Stable instance identity survives"),Loaded->Layers[0].ID,ID);
            TestEqual(TEXT("Gain survives"),Loaded->Layers[0].Volume,.42f);
            TestEqual(TEXT("Delay survives"),Loaded->Layers[0].DelaySeconds,.08f);
            TestEqual(TEXT("Actual sound source survives"),Loaded->Layers[0].Sound.ToSoftObjectPath(),FSoftObjectPath(Sound));
        }
    }
    TestEqual(TEXT("No VFX layer fabricated"),Session.GetLayerCount(Event),OriginalVFXLayers);
    TestFalse(TEXT("Reopened source clean"),Session.IsDirty());
    TestEqual(TEXT("Real mouth library remains untouched"),AudioSaveDigest(Restore.Original.OwnerAsset),OriginalDigest);
    Session.Clear();return true;
}
#endif
