#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "MCComposerSceneAudioSession.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace MCComposerSceneAudioSessionTests
{
bool CanRun(FAutomationTestBase& Test)
{
    if (!GEditor || GEditor->PlayWorld || GEditor->IsTransactionActive())
    {
        Test.AddError(TEXT("Run outside PIE and active editor transactions; only owned E: fixtures may be saved."));
        return false;
    }
    return true;
}

TStrongObjectPtr<UMCSoundPalette> TransientPalette()
{
    return TStrongObjectPtr<UMCSoundPalette>(NewObject<UMCSoundPalette>(GetTransientPackage(), NAME_None, RF_Transient));
}

FString FileDigest(const FString& Filename)
{
    return LexToString(FMD5Hash::HashFile(*Filename));
}

/** New packages mount below /Game but their bytes live only in this task's E: Saved directory. */
struct FIsolatedPalette
{
    FString MountRoot;
    FString Directory;
    FString Filename;
    TStrongObjectPtr<UMCSoundPalette> Palette;
    bool bMounted = false;

    bool Create()
    {
        const FString ID = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        MountRoot = TEXT("/Game/__MCComposerSceneAudioQA_") + ID + TEXT("/");
        Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
            TEXT("Checks/ComposerSceneAudioSession"), ID));
        FPaths::NormalizeDirectoryName(Directory);
        Directory += TEXT("/");
        if (!Directory.StartsWith(TEXT("E:/"), ESearchCase::IgnoreCase)
            || !IFileManager::Get().MakeDirectory(*Directory, true)) return false;
        FPackageName::RegisterMountPoint(MountRoot, Directory);
        bMounted = true;
        const FString PackageName = MountRoot + TEXT("DA_SceneAudioFixture");
        Filename = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(
            PackageName, FPackageName::GetAssetPackageExtension()));
        FPaths::NormalizeFilename(Filename);
        if (!Filename.StartsWith(Directory, ESearchCase::IgnoreCase)) return false;
        UPackage* Package = CreatePackage(*PackageName);
        Palette.Reset(NewObject<UMCSoundPalette>(Package, TEXT("DA_SceneAudioFixture"),
            RF_Public | RF_Standalone | RF_Transactional));
        Palette->Events.FindOrAdd(TEXT("KeepOneShot")).Volume = .83f;
        Palette->Events.FindChecked(TEXT("KeepOneShot")).PitchMin = .91f;
        Palette->Events.FindChecked(TEXT("KeepOneShot")).PitchMax = 1.13f;
        FAssetRegistryModule::AssetCreated(Palette.Get());
        Package->MarkPackageDirty();
        return UEditorLoadingAndSavingUtils::SavePackages({Package}, true) && !Package->IsDirty();
    }

    ~FIsolatedPalette()
    {
        if (Palette.IsValid())
        {
            FAssetRegistryModule::AssetDeleted(Palette.Get());
            Palette->GetOutermost()->SetDirtyFlag(false);
            Palette->ClearFlags(RF_Public | RF_Standalone);
            Palette->SetFlags(RF_Transient);
            Palette->MarkAsGarbage();
            Palette.Reset();
        }
        if (bMounted) FPackageName::UnRegisterMountPoint(MountRoot, Directory);
        // Owned package files remain under Saved/Checks on E: as validation evidence.
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCComposerSceneAudioSharedDraftUndo,
    "MessControl.Audio.Composer.SceneSession.SharedDraftIsolationAndUndo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCComposerSceneAudioSharedDraftUndo::RunTest(const FString&)
{
    using namespace MCComposerSceneAudioSessionTests;
    if (!CanRun(*this)) return false;
    TestTrue(TEXT("Production panels resolve the same retained session"),
        &FMCComposerSceneAudioSession::Shared().Get() == &FMCComposerSceneAudioSession::Shared().Get());
    auto Source = TransientPalette();
    Source->Events.FindOrAdd(TEXT("KeepOneShot")).Volume = .62f;
    const FMCSoundScene Before = Source->Scenes.FindChecked(TEXT("NutEncounter"));
    const auto Session = MakeShared<FMCComposerSceneAudioSession>();
    const auto OtherPanel = Session;
    FString Error;
    if (!TestTrue(TEXT("Open isolated palette"), Session->Open(Source.Get(), Error))) return false;
    TestTrue(TEXT("Working palette is a distinct transient object"), Session->GetDraft() != Source.Get()
        && Session->GetDraft()->HasAllFlags(RF_Transient | RF_Transactional));
    FMCSoundScene Edited = Before;
    Edited.Music.Volume = .79f;
    Edited.Ambience.FadeOutSeconds = 2.3f;
    if (!TestTrue(TEXT("First panel edits real scene fields"), Session->SetScene(TEXT("NutEncounter"), Edited, Error))) return false;
    TestEqual(TEXT("Other panel observes the same working copy"), OtherPanel->GetScene(TEXT("NutEncounter"))->Music.Volume, .79f);
    TestEqual(TEXT("Canonical scene remains unchanged before Apply"), Source->Scenes.FindChecked(TEXT("NutEncounter")).Music.Volume, Before.Music.Volume);
    TestEqual(TEXT("Canonical one-shot untouched"), Source->Events.FindChecked(TEXT("KeepOneShot")).Volume, .62f);
    TestTrue(TEXT("Opening same source from another panel retains draft"), OtherPanel->Open(Source.Get(), Error)
        && OtherPanel->GetScene(TEXT("NutEncounter"))->Music.Volume == .79f && Session->HasChanges());
    const uint64 EditedRevision = Session->GetRevision();
    TestTrue(TEXT("Shared editor Undo succeeds"), GEditor->UndoTransaction());
    TestFalse(TEXT("Undo returns working scene to clean baseline"), Session->HasChanges());
    TestTrue(TEXT("Undo invalidates both views' revision"), OtherPanel->GetRevision() > EditedRevision);
    TestTrue(TEXT("Shared editor Redo succeeds"), GEditor->RedoTransaction());
    TestEqual(TEXT("Redo restores edited scene"), OtherPanel->GetScene(TEXT("NutEncounter"))->Ambience.FadeOutSeconds, 2.3f);
    TestTrue(TEXT("Explicit Revert discards local edits"), Session->Revert(Error));
    TestFalse(TEXT("Refreshed draft is clean"), Session->HasChanges());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCComposerSceneAudioSourceConflict,
    "MessControl.Audio.Composer.SceneSession.ExternalValuesAndProtectedEvents",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCComposerSceneAudioSourceConflict::RunTest(const FString&)
{
    using namespace MCComposerSceneAudioSessionTests;
    if (!CanRun(*this)) return false;
    auto Source = TransientPalette();
    auto OtherSource = TransientPalette();
    Source->Events.FindOrAdd(TEXT("KeepOneShot")).Volume = .62f;
    FMCComposerSceneAudioSession Session;
    FString Error;
    if (!TestTrue(TEXT("Open protected source"), Session.Open(Source.Get(), Error))) return false;
    FMCSoundScene Scene = *Session.GetScene(TEXT("Hub"));
    Scene.Music.FadeInSeconds = 2.f;
    if (!TestTrue(TEXT("Create local scene edit"), Session.SetScene(TEXT("Hub"), Scene, Error))) return false;
    TestFalse(TEXT("Changing source cannot discard a dirty shared draft"), Session.Open(OtherSource.Get(), Error));
    TestTrue(TEXT("Original source and draft are retained"), Session.GetSource() == Source.Get() && Session.HasChanges());
    // Deliberately bypass package dirtying to verify the semantic guard, not only IsDirty().
    Source->Events.FindChecked(TEXT("KeepOneShot")).Volume = .41f;
    TestTrue(TEXT("Unrelated canonical Events edit is detected"), Session.HasExternalChanges());
    TestFalse(TEXT("Apply refuses a newer source"), Session.Apply(Error));
    TestEqual(TEXT("External one-shot edit survives rejection"), Source->Events.FindChecked(TEXT("KeepOneShot")).Volume, .41f);
    TestTrue(TEXT("Rejected Apply retains artist's scene draft"), Session.HasChanges());
    TestTrue(TEXT("Explicit Revert rebases in-memory source values"), Session.Revert(Error));
    TestFalse(TEXT("Rebased memory-only source is current"), Session.HasExternalChanges());
    TestEqual(TEXT("Reverted draft includes newer one-shot value"), Session.GetDraft()->Events.FindChecked(TEXT("KeepOneShot")).Volume, .41f);
    Scene.Music.Volume = -1.f;
    TestFalse(TEXT("Out-of-range gain is refused before mutating draft"), Session.SetScene(TEXT("Hub"), Scene, Error));
    TestFalse(TEXT("An unnamed scene is refused"), Session.SetScene(NAME_None, FMCSoundScene(), Error));
    TestFalse(TEXT("Validation failure leaves draft clean"), Session.HasChanges());
    TestTrue(TEXT("A different palette can be opened after explicit discard"), Session.Open(OtherSource.Get(), Error));
    TestFalse(TEXT("A transient palette cannot be persisted as the canonical asset"), Session.Apply(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCComposerSceneAudioSavedApplyPolicy,
    "MessControl.Audio.Composer.SceneSession.ExplicitSavePreservesOneShotsAndDirtySource",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCComposerSceneAudioSavedApplyPolicy::RunTest(const FString&)
{
    using namespace MCComposerSceneAudioSessionTests;
    if (!CanRun(*this)) return false;
    FIsolatedPalette Fixture;
    if (!TestTrue(TEXT("Save only a new E: fixture through isolated package mount"), Fixture.Create())) return false;
    FMCComposerSceneAudioSession Session;
    FString Error;
    if (!TestTrue(TEXT("Open saved fixture"), Session.Open(Fixture.Palette.Get(), Error))) return false;
    const FString BeforeDisk = FileDigest(Fixture.Filename);
    FMCSoundScene Scene = *Session.GetScene(TEXT("NutEncounter"));
    Scene.Music.Volume = .79f;
    Scene.Ambience.FadeInSeconds = 2.4f;
    if (!TestTrue(TEXT("Edit music and ambience without authoring one-shots"), Session.SetScene(TEXT("NutEncounter"), Scene, Error))) return false;
    TestEqual(TEXT("Editing never saves the source file"), FileDigest(Fixture.Filename), BeforeDisk);
    Fixture.Palette->GetOutermost()->MarkPackageDirty();
    TestFalse(TEXT("Apply cannot silently save unrelated dirty source state"), Session.Apply(Error));
    TestEqual(TEXT("Dirty-source rejection preserves file bytes"), FileDigest(Fixture.Filename), BeforeDisk);
    Fixture.Palette->GetOutermost()->SetDirtyFlag(false); // This test owns the fixture and dirtied no values.
    // A caller can access the preview palette; one-shot edits there must never leak through Apply.
    Session.GetDraft()->Events.FindChecked(TEXT("KeepOneShot")).Volume = .12f;
    TestFalse(TEXT("Out-of-scope draft Events edits are explicitly rejected"), Session.Apply(Error));
    TestEqual(TEXT("Canonical Events are never copied from the preview palette"), Fixture.Palette->Events.FindChecked(TEXT("KeepOneShot")).Volume, .83f);
    Session.GetDraft()->Events.FindChecked(TEXT("KeepOneShot")).Volume = .83f;
    if (!TestTrue(*FString::Printf(TEXT("Explicit Apply saves scene-only change: %s"), *Error), Session.Apply(Error))) return false;
    TestFalse(TEXT("Successful save leaves the canonical package clean"), Fixture.Palette->GetOutermost()->IsDirty());
    TestFalse(TEXT("Saved scene values change the isolated file"), FileDigest(Fixture.Filename) == BeforeDisk);
    TestEqual(TEXT("Canonical scene now matches authored gain"), Fixture.Palette->Scenes.FindChecked(TEXT("NutEncounter")).Music.Volume, .79f);
    const auto& Keep = Fixture.Palette->Events.FindChecked(TEXT("KeepOneShot"));
    TestTrue(TEXT("All original one-shot mix fields survive application"), Keep.Volume == .83f && Keep.PitchMin == .91f && Keep.PitchMax == 1.13f);
    TestFalse(TEXT("Successful Apply establishes a clean shared baseline"), Session.HasChanges());
    TestFalse(TEXT("The session recognises its own completed save"), Session.HasExternalChanges());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCComposerSceneAudioDiskConflict,
    "MessControl.Audio.Composer.SceneSession.DiskConflictCannotBeRebasedFromStaleMemory",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCComposerSceneAudioDiskConflict::RunTest(const FString&)
{
    using namespace MCComposerSceneAudioSessionTests;
    if (!CanRun(*this)) return false;
    FIsolatedPalette Fixture;
    if (!TestTrue(TEXT("Create another isolated E: source"), Fixture.Create())) return false;
    FMCComposerSceneAudioSession Session;
    FString Error;
    if (!TestTrue(TEXT("Open guarded file"), Session.Open(Fixture.Palette.Get(), Error))) return false;
    FMCSoundScene Scene = *Session.GetScene(TEXT("Hub"));
    Scene.Music.Volume = .67f;
    if (!Session.SetScene(TEXT("Hub"), Scene, Error)) { AddError(Error); return false; }
    TArray<uint8> Bytes;
    if (!TestTrue(TEXT("Read owned file before simulating an external writer"), FFileHelper::LoadFileToArray(Bytes, *Fixture.Filename))) return false;
    Bytes.Add(0x7f);
    if (!TestTrue(TEXT("Change only the owned fixture's bytes, without updating its loaded UObject"), FFileHelper::SaveArrayToFile(Bytes, *Fixture.Filename))) return false;
    const FString ExternalDigest = FileDigest(Fixture.Filename);
    TestTrue(TEXT("Changed package bytes invalidate loaded source baseline"), Session.HasExternalChanges());
    TestFalse(TEXT("Apply cannot overwrite newer disk bytes with stale memory"), Session.Apply(Error));
    TestEqual(TEXT("Rejected application preserves external bytes"), FileDigest(Fixture.Filename), ExternalDigest);
    TestTrue(TEXT("Artist can explicitly discard local changes"), Session.Revert(Error));
    TestFalse(TEXT("Discard clears only the local draft"), Session.HasChanges());
    TestTrue(TEXT("Discard does not certify stale memory as matching the changed file"), Session.HasExternalChanges());
    TestFalse(TEXT("A second Apply remains blocked until source reload"), Session.Apply(Error));
    TestEqual(TEXT("Revert also never overwrites the external file"), FileDigest(Fixture.Filename), ExternalDigest);
    return true;
}

#endif
