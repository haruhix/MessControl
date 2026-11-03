#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "MCVFXActionSession.h"
#include "MCVFXActionPack.h"
#include "MCVFXNativePresentationSettings.h"
#include "MCVFXPresentationLibrary.h"
#include "MCVFXProfileBinding.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "UObject/Package.h"

namespace
{
FString PackSessionDigest(const FSoftObjectPath& Asset)
{return LexToString(FMD5Hash::HashFile(*FPackageName::LongPackageNameToFilename(Asset.GetLongPackageName(),FPackageName::GetAssetPackageExtension())));}
struct FRestorePackCanonical
{
    FMCVFXCanonicalProfileState Original;
    ~FRestorePackCanonical(){MCVFXProfileBindings::RegisterLibrarySource(Original.SourceID,Original.OwnerAsset,Original.OwnerKey,Original.AllowedEvents);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXSessionPackVersions,
    "MessControl.VFX.ActionPacks.SessionVersionsDoNotAssignUntilExplicitApply",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXSessionPackVersions::RunTest(const FString&)
{
    if(!GEditor || GEditor->PlayWorld){AddError(TEXT("Run outside PIE."));return false;}
    const FName Source(TEXT("Boss.NutKnight.Shield")),Event(TEXT("Hit"));FString Error;
    FRestorePackCanonical Restore;
    if(!TestTrue(TEXT("Read the real owner for restoration"),MCVFXProfileBindings::ReadCanonical(Source,Restore.Original,Error)))return false;
    const FString RealDigest=PackSessionDigest(Restore.Original.OwnerAsset);
    const FString Root=TEXT("/Game/Tests/VFXPractice/Profiles/GenericBindingQA/SessionPack_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* Package=CreatePackage(*(Root/TEXT("DA_IsolatedOwner")));
    auto* Owner=NewObject<UMCVFXPresentationLibrary>(Package,TEXT("DA_IsolatedOwner"),RF_Public|RF_Standalone|RF_Transactional);
    Owner->Profiles.Add(Source,TSoftObjectPtr<UMCPerkVFXProfile>());FAssetRegistryModule::AssetCreated(Owner);Package->MarkPackageDirty();
    if(!TestTrue(TEXT("Save only an isolated test owner"),UEditorLoadingAndSavingUtils::SavePackages({Package},true)))return false;
    MCVFXProfileBindings::RegisterLibrarySource(Source,FSoftObjectPath(Owner),Source,Restore.Original.AllowedEvents);
    FMCVFXActionSession Session;
    if(!TestTrue(TEXT("Open the native shield through the public Session"),Session.Initialize(Source,Error)))return false;
    const auto Group=Session.CreateEventGroup(Session.GetDraftToken(),Event,TEXT("ImpactPack"),Error);
    if(!TestTrue(TEXT("Create independent group"),Group.IsValid()))return false;
    const auto* Part=Session.GetNativeDraft()->FindPart(Event,TEXT("ShieldSparks"));
    auto* NS=Part?Part->System.LoadSynchronous():nullptr;
    if(!TestNotNull(TEXT("Real shield source Niagara"),NS))return false;
    const auto VFX=Session.AddSystemToGroup(Session.GetDraftToken(),Event,NS,Group,Error);
    auto* Sound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Audio/Rewards/S_TaskCoins.S_TaskCoins"));
    const auto SFX=Session.AddSFXLayer(Session.GetDraftToken(),Event,Sound,Group,Error);
    if(!TestTrue(TEXT("Both lanes have real saved sources"),VFX.IsValid()&&SFX.IsValid()))return false;
    TestTrue(TEXT("Move the event without changing its semantic identity"),Session.SetEventCardPosition(Session.GetDraftToken(),Event,FVector2D(360,120),Error));
    UMCVFXActionPack* A=Session.SavePackNew(FText::FromString(TEXT("A impact and sound")),Error,Root/TEXT("Versions"));
    if(!TestNotNull(*FString::Printf(TEXT("Save A: %s"),*Error),A))return false;
    TestTrue(TEXT("Saving never assigns the game"),Owner->Profiles.FindChecked(Source).IsNull());
    TestFalse(TEXT("Saved draft no longer needs another pack save"),Session.HasUnsavedPackChanges());
    TestTrue(TEXT("Saving a pack preserves pending game changes"),Session.IsRuntimeDirty());
    TestTrue(TEXT("Preview Solo remains a local mask"),Session.SetPreviewSoloGroups(Session.GetDraftToken(),Event,{Group},Error));
    TestFalse(TEXT("Solo cannot dirty a saved version"),Session.HasUnsavedPackChanges());
    TestTrue(TEXT("Create B by muting only the group's audio"),Session.SetEventGroupAudioMuted(Session.GetDraftToken(),Event,Group,true,Error));
    UMCVFXActionPack* B=Session.SavePackNew(FText::FromString(TEXT("B muted sound")),Error,Root/TEXT("Versions"));
    if(!TestNotNull(*FString::Printf(TEXT("Save B: %s"),*Error),B))return false;
    TestTrue(TEXT("Versions have independent immutable manifests"),A!=B&&A->PackID!=B->PackID&&A->RuntimeProfile!=B->RuntimeProfile);
    TestTrue(TEXT("B still did not assign the game"),Owner->Profiles.FindChecked(Source).IsNull());
    TestTrue(TEXT("Load A through the ordinary public Session"),Session.LoadPack(A,Error));
    const auto* Loaded=Session.GetEventGroups(Event);
    TestTrue(TEXT("A restores its unmuted independently enabled group"),Loaded&&Loaded->Find(Group)&&!Loaded->Find(Group)->bMuteSFX);
    TestTrue(TEXT("Pack loading clears the temporary Solo filter"),Session.GetPreviewSoloGroups(Event).IsEmpty());
    FVector2D Position;TestTrue(TEXT("Saved event layout survives A/B"),Session.GetEventCardPosition(Event,Position)&&Position==FVector2D(360,120));
    const auto* Audio=Session.GetSFXSlot(Event);
    TestTrue(TEXT("SFX instance and real source remain stable"),Audio&&Audio->Layers.Num()==1&&Audio->Layers[0].ID==SFX&&Audio->Layers[0].Sound.ToSoftObjectPath()==FSoftObjectPath(Sound));
    TestFalse(TEXT("Loaded A has no unsaved pack edits"),Session.HasUnsavedPackChanges());
    const auto OldToken=Session.GetDraftToken();
    TestTrue(TEXT("Move A layout to make a dirty draft"),Session.SetActionHubPosition(OldToken,FVector2D(5,9),Error));
    TestFalse(TEXT("Loading another version requires explicit discard"),Session.LoadPack(B,Error));
    TestTrue(TEXT("Explicit discard opens B"),Session.LoadPack(B,Error,true));
    TestFalse(TEXT("Old callbacks cannot edit the new loaded version"),Session.SetEventGroupAudioMuted(OldToken,Event,Group,false,Error));
    TestTrue(TEXT("Explicit saved-pack Apply uses the one canonical writer"),Session.ApplySavedPack(B,Error));
    TestEqual(TEXT("Only the isolated owner receives the saved profile"),Owner->Profiles.FindChecked(Source).ToSoftObjectPath(),B->RuntimeProfile.ToSoftObjectPath());
    TestFalse(TEXT("Assigned saved version is clean for the game"),Session.IsRuntimeDirty());
    TestEqual(TEXT("Actual mouth library was not written"),PackSessionDigest(Restore.Original.OwnerAsset),RealDigest);
    Session.Clear();return true;
}
#endif
