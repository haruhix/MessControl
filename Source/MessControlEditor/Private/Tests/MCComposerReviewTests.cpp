#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "MCComposerReview.h"
#include "MCPerkActionPreviewPanel.h"
#include "MCPerkPreviewRunner.h"
#include "MCVFXActionSession.h"
#include "MCVFXRecipePreset.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace MCComposerReviewTests
{
bool CanRun(FAutomationTestBase& Test)
{
    if(!IsInGameThread()||!GEditor||GEditor->IsPlaySessionInProgress()||GEditor->IsTransactionActive())
    {Test.AddError(TEXT("Run isolated Composer review tests outside PIE/SIE and active editor transactions."));return false;}
    if(const auto Runner=MCPerkActionPreviewPanel::GetActiveRunner();Runner&&(Runner->IsRunning()||Runner->IsPaused()||Runner->HasSceneAudio()))
    {Test.AddError(TEXT("Stop the active Composer preview before running isolated review tests."));return false;}
    return true;
}

struct FReviewFiles
{
    FString Directory;
    bool Create(FAutomationTestBase& Test)
    {
        Directory=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Checks/ComposerReview"),FGuid::NewGuid().ToString(EGuidFormats::Digits)));
        FPaths::NormalizeDirectoryName(Directory);
        if(!Test.TestTrue(TEXT("Every review fixture lives on E:"),Directory.StartsWith(TEXT("E:/"),ESearchCase::IgnoreCase)))return false;
        return Test.TestTrue(TEXT("Create a unique owned review-fixture directory"),IFileManager::Get().MakeDirectory(*Directory,true));
    }
    FString File(const TCHAR* Name) const {return FPaths::Combine(Directory,Name);}
    // Keep owned JSON evidence under Saved/Checks; never touch the production Config store.
};

bool ReadBytes(FAutomationTestBase& Test,const FString& Path,TArray<uint8>& Out)
{return Test.TestTrue(*FString::Printf(TEXT("Read fixture bytes: %s"),*Path),FFileHelper::LoadFileToArray(Out,*Path));}

FString PackageDigest(const UObject* Asset)
{
    if(!Asset)return FString();
    const FString Path=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
    return LexToString(FMD5Hash::HashFile(*Path));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCComposerReviewPersistence,
    "MessControl.Composer.Review.PersistenceAndStaleness",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCComposerReviewPersistence::RunTest(const FString&)
{
    using namespace MCComposerReviewTests;
    if(!CanRun(*this))return false;
    FReviewFiles Files;if(!Files.Create(*this))return false;
    const FString Path=Files.File(TEXT("Persistence.json"));
    const FName Action(TEXT("QA.Review.Persistence")),Event(TEXT("Impact"));
    FMCComposerReviewStore Store(Path);FString Error;
    const auto Initial=Store.Query(Action,Event,EMCComposerReviewLane::SFX,TEXT("audio-v1"));
    TestFalse(TEXT("New events start unapproved"),Initial.bApproved);
    TestTrue(TEXT("A supplied current fingerprint is known"),Initial.bCurrentKnown);
    TestFalse(TEXT("Read-only queries never create the metadata file"),IFileManager::Get().FileExists(*Path));
    TestFalse(TEXT("Approval without a current version is rejected"),Store.SetApproved(Action,Event,EMCComposerReviewLane::SFX,true,FString(),Error));
    TestFalse(TEXT("Rejected approval still creates no file"),IFileManager::Get().FileExists(*Path));
    if(!TestTrue(TEXT("Explicit approval persists the current audio version"),Store.SetApproved(Action,Event,EMCComposerReviewLane::SFX,true,TEXT("audio-v1"),Error)))return false;
    TArray<uint8> ApprovedBytes;if(!ReadBytes(*this,Path,ApprovedBytes))return false;
    FMCComposerReviewStore Reopened(Path);
    auto State=Reopened.Query(Action,Event,EMCComposerReviewLane::SFX,TEXT("audio-v1"));
    TestTrue(TEXT("Approval survives a new independent store instance"),State.bApproved&&State.bCurrentKnown&&!State.bStale);
    State=Reopened.Query(Action,Event,EMCComposerReviewLane::SFX,TEXT("audio-v2"));
    TestTrue(TEXT("Changed audio retains the historical mark but requires review"),State.bApproved&&State.bCurrentKnown&&State.bStale);
    State=Reopened.Query(Action,Event,EMCComposerReviewLane::SFX,FString());
    TestTrue(TEXT("An unopened current version is distinguished from a stale known version"),State.bApproved&&!State.bCurrentKnown&&!State.bStale);
    TArray<uint8> AfterQueries;if(!ReadBytes(*this,Path,AfterQueries))return false;
    TestTrue(TEXT("Stale/unknown inspection never rewrites the approved version"),AfterQueries==ApprovedBytes);
    if(!TestTrue(TEXT("The author may explicitly approve the newer audio"),Reopened.SetApproved(Action,Event,EMCComposerReviewLane::SFX,true,TEXT("audio-v2"),Error)))return false;
    FMCComposerReviewStore Latest(Path);
    State=Latest.Query(Action,Event,EMCComposerReviewLane::SFX,TEXT("audio-v2"));
    TestTrue(TEXT("Reapproval checkpoints only the newer version"),State.bApproved&&!State.bStale);
    TestTrue(TEXT("The earlier version is no longer the approved baseline"),Latest.Query(Action,Event,EMCComposerReviewLane::SFX,TEXT("audio-v1")).bStale);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCComposerReviewLaneIsolation,
    "MessControl.Composer.Review.LaneEventSeparationAndUnmark",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCComposerReviewLaneIsolation::RunTest(const FString&)
{
    using namespace MCComposerReviewTests;
    if(!CanRun(*this))return false;
    FReviewFiles Files;if(!Files.Create(*this))return false;
    const FString Path=Files.File(TEXT("Lanes.json"));
    const FName Action(TEXT("QA.Review.Lanes")),Event(TEXT("Impact"));FString Error;
    FMCComposerReviewStore First(Path),Second(Path);
    First.Query(Action,Event,EMCComposerReviewLane::SFX,TEXT("sound-1"));
    if(!TestTrue(TEXT("Second client approves VFX"),Second.SetApproved(Action,Event,EMCComposerReviewLane::VFX,true,TEXT("visual-1"),Error)))return false;
    if(!TestTrue(TEXT("First client merges its audio approval with the newer file"),First.SetApproved(Action,Event,EMCComposerReviewLane::SFX,true,TEXT("sound-1"),Error)))return false;
    FMCComposerReviewStore Both(Path);
    TestTrue(TEXT("Concurrent metadata clients preserve the VFX approval"),Both.Query(Action,Event,EMCComposerReviewLane::VFX,TEXT("visual-1")).bApproved);
    TestTrue(TEXT("The same event has a separate SFX approval"),Both.Query(Action,Event,EMCComposerReviewLane::SFX,TEXT("sound-1")).bApproved);
    TestTrue(TEXT("A changed SFX fingerprint invalidates its own approval"),Both.Query(Action,Event,EMCComposerReviewLane::SFX,TEXT("sound-2")).bStale);
    TestFalse(TEXT("The VFX lane stays current while audio is stale"),Both.Query(Action,Event,EMCComposerReviewLane::VFX,TEXT("visual-1")).bStale);
    TestFalse(TEXT("Another event does not inherit approval"),Both.Query(Action,TEXT("End"),EMCComposerReviewLane::SFX,TEXT("sound-1")).bApproved);
    TestFalse(TEXT("Another action does not inherit approval"),Both.Query(TEXT("QA.Review.Other"),Event,EMCComposerReviewLane::SFX,TEXT("sound-1")).bApproved);
    if(!TestTrue(TEXT("Unmarking requires no current fingerprint"),Both.SetApproved(Action,Event,EMCComposerReviewLane::SFX,false,FString(),Error)))return false;
    FMCComposerReviewStore Unmarked(Path);
    TestFalse(TEXT("Unmark persists across store instances"),Unmarked.Query(Action,Event,EMCComposerReviewLane::SFX,TEXT("sound-1")).bApproved);
    const auto Visual=Unmarked.Query(Action,Event,EMCComposerReviewLane::VFX,TEXT("visual-1"));
    TestTrue(TEXT("Removing the audio mark preserves the independent visual mark"),Visual.bApproved&&!Visual.bStale);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCComposerReviewMalformedFile,
    "MessControl.Composer.Review.MalformedFileNeverOverwritten",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCComposerReviewMalformedFile::RunTest(const FString&)
{
    using namespace MCComposerReviewTests;
    if(!CanRun(*this))return false;
    FReviewFiles Files;if(!Files.Create(*this))return false;
    const FString Row=TEXT("{\"action\":\"QA.Review.Malformed\",\"event\":\"Impact\",\"lane\":\"SFX\",\"fingerprint\":\"audio-1\",\"approved_at\":\"2026-11-14T00:00:00Z\"}");
    const TArray<TPair<FString,FString>> Cases={
        {TEXT("Truncated.json"),TEXT("{\"version\":1,\"reviews\":[")},
        {TEXT("UnknownVersion.json"),TEXT("{\"version\":999,\"reviews\":[]}")},
        {TEXT("BrokenEntry.json"),TEXT("{\"version\":1,\"reviews\":[{\"action\":\"QA.Review.Malformed\"}]}")},
        {TEXT("DuplicateEntry.json"),TEXT("{\"version\":1,\"reviews\":[")+Row+TEXT(",")+Row+TEXT("]}")}
    };
    for(const auto& Case:Cases)
    {
        const FString Path=Files.File(*Case.Key);
        if(!TestTrue(*FString::Printf(TEXT("Create owned malformed fixture %s"),*Case.Key),FFileHelper::SaveStringToFile(Case.Value,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))return false;
        TArray<uint8> Before;if(!ReadBytes(*this,Path,Before))return false;
        FMCComposerReviewStore Store(Path);FString Error;
        const auto State=Store.Query(TEXT("QA.Review.Malformed"),TEXT("Impact"),EMCComposerReviewLane::SFX,TEXT("audio-1"));
        TestFalse(*FString::Printf(TEXT("%s cannot invent an approval"),*Case.Key),State.bApproved);
        TestFalse(*FString::Printf(TEXT("%s cannot be overwritten by approval"),*Case.Key),Store.SetApproved(TEXT("QA.Review.Malformed"),TEXT("Impact"),EMCComposerReviewLane::SFX,true,TEXT("audio-1"),Error));
        TestFalse(*FString::Printf(TEXT("%s reports the read failure"),*Case.Key),Error.IsEmpty());
        TestFalse(*FString::Printf(TEXT("%s cannot be overwritten by unmark"),*Case.Key),Store.SetApproved(TEXT("QA.Review.Malformed"),TEXT("Impact"),EMCComposerReviewLane::SFX,false,FString(),Error));
        Store.GetSnapshotJson();Store.Reload();
        TArray<uint8> After;if(!ReadBytes(*this,Path,After))return false;
        TestTrue(*FString::Printf(TEXT("%s preserves every original byte after queries and rejected writes"),*Case.Key),Before==After);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCComposerReviewSemanticFingerprints,
    "MessControl.Composer.Review.SemanticFingerprintsIgnoreOtherLaneAndPreview",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCComposerReviewSemanticFingerprints::RunTest(const FString&)
{
    using namespace MCComposerReviewTests;
    if(!CanRun(*this))return false;
    const FName BeforeContext=MCPerkActionPreviewPanel::GetContextPerkID();
    const auto BeforeRunner=MCPerkActionPreviewPanel::GetActiveRunner();
    auto* BeforeWorking=MCPerkActionPreviewPanel::GetWorkingProfile();
    auto* Sound=LoadObject<USoundWave>(nullptr,TEXT("/Game/Audio/S_Hit.S_Hit"));
    if(!TestNotNull(TEXT("Use the existing saved Sound Wave without editing it"),Sound))return false;
    if(!TestTrue(TEXT("The source sound is a clean saved asset"),!Sound->HasAnyFlags(RF_Transient)&&!Sound->IsEditorOnly()&&!Sound->GetOutermost()->IsDirty()&&FPackageName::DoesPackageExist(Sound->GetOutermost()->GetName())))return false;
    const FString SoundBefore=PackageDigest(Sound);
    FMCVFXActionSession Session;FString Error;
    if(!TestTrue(*FString::Printf(TEXT("Open an independent data-only Rocket draft: %s"),*Error),Session.Initialize(TEXT("Consumable_Rocket"),Error)))return false;
    if(!TestTrue(TEXT("Rocket has a real editable canonical binding"),Session.CanEditProfile()))return false;
    const auto Events=Session.GetActionStages();if(!TestTrue(TEXT("Rocket declares real events"),!Events.IsEmpty()))return false;
    const FName Event=Events.Contains(TEXT("Start"))?FName(TEXT("Start")):Events[0];
    auto* Owner=Session.GetCanonicalOwner().ResolveObject();
    if(!TestNotNull(TEXT("Canonical owner is already resolved by document initialization"),Owner))return false;
    const bool OwnerWasDirty=Owner->GetOutermost()->IsDirty();const FString OwnerBefore=PackageDigest(Owner);
    auto Fingerprint=[&](EMCComposerReviewLane Lane){return MCComposerReview::Fingerprint(Session,Event,Lane);};
    const FString Visual0=Fingerprint(EMCComposerReviewLane::VFX),Audio0=Fingerprint(EMCComposerReviewLane::SFX);
    TestFalse(TEXT("A valid VFX event has a semantic fingerprint"),Visual0.IsEmpty());
    TestFalse(TEXT("A valid SFX event has a semantic fingerprint even without custom layers"),Audio0.IsEmpty());
    const FGuid AudioID=Session.AddSFXLayer(Session.GetSFXDraftToken(),Event,Sound,FGuid(),Error);
    if(!TestTrue(TEXT("Add a sound only to this independent transient draft"),AudioID.IsValid()))return false;
    const FString Audio1=Fingerprint(EMCComposerReviewLane::SFX);
    TestTrue(TEXT("Adding a real sound changes the SFX review version"),Audio1!=Audio0);
    TestEqual(TEXT("Adding audio does not invalidate VFX approval"),Fingerprint(EMCComposerReviewLane::VFX),Visual0);
    const auto* Slot=Session.GetSFXSlot(Event);const auto* Added=Slot?Slot->Layers.FindByPredicate([AudioID](const auto& L){return L.ID==AudioID;}):nullptr;
    if(!TestNotNull(TEXT("The added layer retains its exact GUID"),Added))return false;
    auto AudioLayer=*Added;AudioLayer.Volume=.347f;
    if(!TestTrue(TEXT("Edit a semantic audio parameter through the Session writer"),Session.SetSFXLayer(Session.GetSFXDraftToken(),Event,AudioID,AudioLayer,Error)))return false;
    const FString Audio2=Fingerprint(EMCComposerReviewLane::SFX);
    TestTrue(TEXT("A gain edit changes the SFX fingerprint"),Audio2!=Audio1);
    TestEqual(TEXT("A gain edit still preserves the VFX fingerprint"),Fingerprint(EMCComposerReviewLane::VFX),Visual0);
    const auto* Recipe=Session.GetRecipe(Event);if(!TestNotNull(TEXT("Rocket provides an editable VFX recipe"),Recipe))return false;
    TStrongObjectPtr<UMCVFXRecipePreset> VisualEdit(DuplicateObject<UMCVFXRecipePreset>(Recipe,GetTransientPackage()));
    // Tail has an independent valid range, so this stays valid even if an artist's
    // last layer starts immediately before the maximum composition duration.
    VisualEdit->TailSeconds=Recipe->TailSeconds<9.f?Recipe->TailSeconds+.37f:Recipe->TailSeconds-.37f;
    if(!TestTrue(TEXT("Import a changed VFX tail duration only into this draft"),Session.ImportRecipe(Event,VisualEdit.Get(),Error)))return false;
    const FString Visual1=Fingerprint(EMCComposerReviewLane::VFX);
    TestTrue(TEXT("A visual timing edit changes the VFX review version"),Visual1!=Visual0);
    TestEqual(TEXT("VFX editing does not invalidate the SFX version"),Fingerprint(EMCComposerReviewLane::SFX),Audio2);
    const FGuid Token=Session.GetDraftToken();
    const FGuid Group=Session.CreateEventGroup(Token,Event,TEXT("ReviewFixtureGroup"),Error);
    if(!TestTrue(TEXT("Create an actual preview-filter group in the independent draft"),Group.IsValid()))return false;
    if(!TestTrue(TEXT("Attach only the test's sound to its real shared group"),Session.AssignLayerToGroup(Token,Event,EMCComposerLayerLane::SFX,AudioID,Group,Error)))return false;
    const FString GroupAudio=Fingerprint(EMCComposerReviewLane::SFX),GroupVisual=Fingerprint(EMCComposerReviewLane::VFX);
    if(!TestTrue(TEXT("Move the graph action hub"),Session.SetActionHubPosition(Token,FVector2D(411,227),Error))
        ||!TestTrue(TEXT("Move the event card"),Session.SetEventCardPosition(Token,Event,FVector2D(287,419),Error))
        ||!TestTrue(TEXT("Move an audio graph node"),Session.SetLayerGraphPosition(Token,Event,EMCComposerLayerLane::SFX,AudioID,FVector2D(812,114),Error)))return false;
    TestEqual(TEXT("Graph layout cannot invalidate audio review"),Fingerprint(EMCComposerReviewLane::SFX),GroupAudio);
    TestEqual(TEXT("Graph layout cannot invalidate visual review"),Fingerprint(EMCComposerReviewLane::VFX),GroupVisual);
    if(!TestTrue(TEXT("Set a preview-only Solo mask without playing"),Session.SetPreviewSoloGroups(Token,Event,{Group},Error)))return false;
    TestEqual(TEXT("Preview group Solo does not invalidate SFX approval"),Fingerprint(EMCComposerReviewLane::SFX),GroupAudio);
    TestEqual(TEXT("Preview group Solo does not invalidate VFX approval"),Fingerprint(EMCComposerReviewLane::VFX),GroupVisual);
    Session.ClearPreviewSolo();
    TestEqual(TEXT("Clearing Solo leaves the audio version unchanged"),Fingerprint(EMCComposerReviewLane::SFX),GroupAudio);
    TestEqual(TEXT("Clearing Solo leaves the visual version unchanged"),Fingerprint(EMCComposerReviewLane::VFX),GroupVisual);
    if(!TestTrue(TEXT("Mute only the test group's SFX"),Session.SetEventGroupAudioMuted(Token,Event,Group,true,Error)))return false;
    TestTrue(TEXT("A real SFX group mute requires fresh audio review"),Fingerprint(EMCComposerReviewLane::SFX)!=GroupAudio);
    TestEqual(TEXT("Audio group mute cannot invalidate VFX review"),Fingerprint(EMCComposerReviewLane::VFX),GroupVisual);
    TestEqual(TEXT("No source sound bytes were saved"),PackageDigest(Sound),SoundBefore);
    TestFalse(TEXT("The source sound remains clean"),Sound->GetOutermost()->IsDirty());
    TestEqual(TEXT("No canonical owner bytes were saved"),PackageDigest(Owner),OwnerBefore);
    TestEqual(TEXT("Canonical owner dirty state is unchanged"),Owner->GetOutermost()->IsDirty(),OwnerWasDirty);
    TestTrue(TEXT("Data-only tests never replace the existing preview runner"),MCPerkActionPreviewPanel::GetActiveRunner()==BeforeRunner);
    TestEqual(TEXT("Data-only tests never navigate the preview context"),MCPerkActionPreviewPanel::GetContextPerkID(),BeforeContext);
    TestTrue(TEXT("The previous preview working profile is retained"),MCPerkActionPreviewPanel::GetWorkingProfile()==BeforeWorking);
    // All mutations belong to this transient session. Do not Apply, reset the shared documents, or reset Undo.
    return true;
}
#endif
