#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "MCComposerDocuments.h"
#include "MCComposerDocumentView.h"
#include "MCDataAssets.h"
#include "MCPerkActionPreviewPanel.h"
#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewRunner.h"
#include "MCVFXActionSession.h"
#include "MCVFXPresentationLibrary.h"
#include "MCVFXProfileBinding.h"
#include "MCVFXRecipePreset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "NiagaraSystem.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace MCComposerSharedDocumentTests
{
const FName Event(TEXT("FixtureEvent"));

bool CanRun(FAutomationTestBase& Test)
{
    if(!IsInGameThread() || !GEditor || GEditor->IsPlaySessionInProgress() || GEditor->IsTransactionActive())
    { Test.AddError(TEXT("Run these isolated Composer tests outside PIE/SIE and active transactions.")); return false; }
    // Never take an active sandbox away from an artist or another automation client.
    if(const auto Runner=MCPerkActionPreviewPanel::GetActiveRunner();Runner && (Runner->IsRunning() || Runner->IsPaused() || Runner->HasSceneAudio()))
    { Test.AddError(TEXT("Stop the existing Composer preview before running isolated shared-document tests.")); return false; }
    return true;
}

USoundWave* SavedSound(FAutomationTestBase& Test)
{
    auto* Sound=LoadObject<USoundWave>(nullptr,TEXT("/Game/Audio/S_Hit.S_Hit"));
    if(!Test.TestNotNull(TEXT("Fixture uses the real imported S_Hit Sound Wave"),Sound)) return nullptr;
    if(!Test.TestTrue(TEXT("Read-only sound source is clean, saved and finite"),
        !Sound->HasAnyFlags(RF_Transient) && !Sound->IsEditorOnly() && !Sound->GetOutermost()->IsDirty()
        && Sound->Duration>0 && FMath::IsFinite(Sound->Duration)
        && FPackageName::DoesPackageExist(Sound->GetOutermost()->GetName()))) return nullptr;
    return Sound;
}

FString Digest(const UObject* Object)
{
    return Object?LexToString(FMD5Hash::HashFile(*FPackageName::LongPackageNameToFilename(
        Object->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension()))):FString();
}

/** Lifecycle-only provider. It deliberately does not claim to exercise gameplay mechanics. */
class FLeaseFixtureAdapter final : public IMCPerkPreviewAdapter
{
public:
    bool Prepare(FMCPerkPreviewContext& Context,FString& Error) override
    {
        if(Context.World && Context.Hero) return true;
        Error=TEXT("Lease fixture needs the private preview world and hero."); return false;
    }
    void Tick(FMCPerkPreviewContext&,float) override {}
    FString GetPhaseName(const FMCPerkPreviewContext&) const override { return TEXT("Composer lease test fixture; no gameplay assertion"); }
};

/** Every byte written by these tests is below an isolated E: Saved/Checks mount. */
struct FFixture
{
    FString MountRoot,Directory;
    FName A,B;
    UMCVFXPresentationLibrary* Library=nullptr;
    UMCPerkVFXProfile* ProfileA=nullptr;
    TArray<TStrongObjectPtr<UObject>> OwnedAssets;
    bool bMounted=false,bRegistered=false;

    template<typename T> T* NewAsset(const TCHAR* Name)
    {
        UPackage* Package=CreatePackage(*(MountRoot+Name));
        auto* Asset=NewObject<T>(Package,FName(Name),RF_Public|RF_Standalone|RF_Transactional);
        OwnedAssets.Emplace(Asset); FAssetRegistryModule::AssetCreated(Asset); Package->MarkPackageDirty();
        return Asset;
    }
    bool Create()
    {
        const FString ID=FGuid::NewGuid().ToString(EGuidFormats::Digits);
        // Full GUID identities, with short stems so ApplyAudio's generated filenames
        // also stay practical on Windows below this task's relatively long mount.
        A=FName(*(TEXT("QA.")+ID+TEXT(".A")));
        B=FName(*(TEXT("QA.")+ID+TEXT(".B")));
        MountRoot=TEXT("/Game/__MCComposerSharedQA_")+ID+TEXT("/");
        Directory=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Checks/ComposerSharedDocuments"),ID));
        FPaths::NormalizeDirectoryName(Directory); Directory+=TEXT("/");
        if(!Directory.StartsWith(TEXT("E:/"),ESearchCase::IgnoreCase) || !IFileManager::Get().MakeDirectory(*Directory,true)) return false;
        FPackageName::RegisterMountPoint(MountRoot,Directory); bMounted=true;
        ProfileA=NewAsset<UMCPerkVFXProfile>(TEXT("DA_ProfileA"));
        auto* ProfileB=NewAsset<UMCPerkVFXProfile>(TEXT("DA_ProfileB"));
        ProfileA->Slots.Add(Event,FMCPerkVFXSlot()); ProfileB->Slots.Add(Event,FMCPerkVFXSlot());
        Library=NewAsset<UMCVFXPresentationLibrary>(TEXT("DA_PresentationFixture"));
        Library->Profiles.Add(A,ProfileA); Library->Profiles.Add(B,ProfileB);
        TArray<UPackage*> Packages;
        for(const auto& Asset:OwnedAssets)
        {
            FString Filename=FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(
                Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension()));
            FPaths::NormalizeFilename(Filename);
            if(!Filename.StartsWith(Directory,ESearchCase::IgnoreCase)) return false;
            Packages.Add(Asset->GetOutermost());
        }
        if(!UEditorLoadingAndSavingUtils::SavePackages(Packages,true)) return false;
        for(const auto& Asset:OwnedAssets) if(Asset->GetOutermost()->IsDirty()) return false;
        for(FName Action:{A,B})
        {
            MCVFXProfileBindings::RegisterLibrarySource(Action,FSoftObjectPath(Library),Action,{Event});
            FMCPerkPreviewAdapterEntry Provider;
            Provider.PerkID=Action; Provider.SourceRowID=NAME_None; Provider.Kind=EMCPerkPreviewAdapterKind::Context;
            Provider.Label=FText::FromString(TEXT("Shared-document lifecycle fixture"));
            Provider.Stages={Event}; Provider.Scenarios={EMCPerkPreviewScenario::Target}; Provider.bSupportsProfileOverride=true;
            Provider.Create=[]()->TUniquePtr<IMCPerkPreviewAdapter> { return MakeUnique<FLeaseFixtureAdapter>(); };
            MCPerkPreviewAdapters::Register(MoveTemp(Provider));
        }
        bRegistered=true; return true;
    }
    bool RetainOutput(UObject* Asset)
    {
        if(!Asset || !Asset->GetOutermost()->GetName().StartsWith(MountRoot)) return false;
        OwnedAssets.Emplace(Asset); return true;
    }
    ~FFixture()
    {
        if(bRegistered) { MCPerkPreviewAdapters::Unregister(A); MCPerkPreviewAdapters::Unregister(B); }
        for(auto& Asset:OwnedAssets) if(Asset.IsValid())
        {
            FAssetRegistryModule::AssetDeleted(Asset.Get());
            Asset->GetOutermost()->SetDirtyFlag(false);
            Asset->ClearFlags(RF_Public|RF_Standalone); Asset->SetFlags(RF_Transient); Asset->MarkAsGarbage();
        }
        OwnedAssets.Reset();
        if(bMounted) FPackageName::UnRegisterMountPoint(MountRoot,Directory);
        // Do not call Documents::Shutdown or reset the global Undo buffer: both contain user state.
        // Unique registry documents/binding metadata expire at normal editor shutdown.
        // Files remain under E: Saved/Checks as isolated validation evidence.
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCComposerSharedAcquireNavigationUndo,
    "MessControl.Composer.Shared.AcquireIndependentNavigationAndUndoRedo",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCComposerSharedAcquireNavigationUndo::RunTest(const FString&)
{
    using namespace MCComposerSharedDocumentTests;
    if(!CanRun(*this)) return false;
    FFixture Fixture;
    if(!TestTrue(TEXT("Create only isolated E: saved fixture sources"),Fixture.Create())) return false;
    auto* Sound=SavedSound(*this); if(!Sound) return false;
    const FString OwnerBefore=Digest(Fixture.Library),ProfileBefore=Digest(Fixture.ProfileA),SoundBefore=Digest(Sound);
    FString Error;
    TSharedPtr<FMCComposerDocumentView> VFX=MakeShared<FMCComposerDocumentView>(),SFX=MakeShared<FMCComposerDocumentView>();
    if(!TestTrue(TEXT("VFX view selects fixture A"),VFX->Select(Fixture.A,Error))
        || !TestTrue(TEXT("SFX view independently selects fixture A"),SFX->Select(Fixture.A,Error))) return false;
    const auto Document=VFX->GetDocument();
    TestTrue(TEXT("Two production view clients acquire one document"),Document==SFX->GetDocument());
    TestTrue(TEXT("View identities remain independent"),VFX->GetViewID()!=SFX->GetViewID());
    const FGuid Token=Document->GetSFXDraftToken();
    const FGuid LayerID=Document->AddSFXLayer(Token,Event,Sound,FGuid(),Error);
    if(!TestTrue(TEXT("VFX-side Add creates a shared stable audio layer"),LayerID.IsValid())) return false;
    const auto Reacquired=MCComposerDocuments::Acquire(Fixture.A,Error);
    TestTrue(TEXT("Reacquire never reinitializes an authored document"),Reacquired==Document && Reacquired->GetSFXDraftToken()==Token);
    auto Layer=SFX->GetDocument()->GetSFXSlot(Event)->Layers[0]; Layer.Volume=.42f;
    if(!TestTrue(TEXT("SFX-side edit uses the same shared writer"),SFX->GetDocument()->SetSFXLayer(Token,Event,LayerID,Layer,Error))) return false;
    TestEqual(TEXT("VFX reads the SFX edit immediately"),Document->GetSFXSlot(Event)->Layers[0].Volume,.42f);
    const uint64 EditedRevision=Document->GetRevision();
    // The most recent transaction is this test's one SetSFXLayer above. Never clear user history.
    if(!TestTrue(TEXT("Undo this test's audio edit"),GEditor->UndoTransaction())) return false;
    TestEqual(TEXT("Undo restores gain in the VFX client"),Document->GetSFXSlot(Event)->Layers[0].Volume,1.f);
    TestEqual(TEXT("Undo preserves source-layer identity in the SFX client"),SFX->GetDocument()->GetSFXSlot(Event)->Layers[0].ID,LayerID);
    TestTrue(TEXT("Undo increments the shared revision seen by both views"),SFX->GetDocument()->GetRevision()>EditedRevision);
    if(!TestTrue(TEXT("Redo this test's audio edit"),GEditor->RedoTransaction())) return false;
    TestEqual(TEXT("Redo restores gain in both clients"),Document->GetSFXSlot(Event)->Layers[0].Volume,.42f);
    TestEqual(TEXT("Redo keeps the same GUID"),Document->GetSFXSlot(Event)->Layers[0].ID,LayerID);
    if(!TestTrue(TEXT("SFX view navigates to separate action B"),SFX->Select(Fixture.B,Error))) return false;
    TestEqual(TEXT("VFX view stays on A"),VFX->GetDocument()->GetActionID(),Fixture.A);
    TestTrue(TEXT("A remains editable while another view selects B"),Document->CanEditProfile());
    TestTrue(TEXT("B has a distinct clean document"),SFX->GetDocument()!=Document && !SFX->GetDocument()->IsDirty());
    Layer=Document->GetSFXSlot(Event)->Layers[0]; Layer.DelaySeconds=.12f;
    TestTrue(TEXT("VFX-side edit still succeeds after independent SFX navigation"),Document->SetSFXLayer(Token,Event,LayerID,Layer,Error));
    if(!TestTrue(TEXT("Returning SFX to A reuses its dirty draft"),SFX->Select(Fixture.A,Error))) return false;
    TestEqual(TEXT("Returning view observes the other view's latest delay"),SFX->GetDocument()->GetSFXSlot(Event)->Layers[0].DelaySeconds,.12f);
    VFX.Reset();
    TestTrue(TEXT("Closing one view retains the shared document"),SFX->GetDocument()==Document && Document->GetSFXDraftToken()==Token);
    TestEqual(TEXT("Closing one view preserves shared gain"),SFX->GetDocument()->GetSFXSlot(Event)->Layers[0].Volume,.42f);
    TestEqual(TEXT("Data authoring never saves the canonical owner"),Digest(Fixture.Library),OwnerBefore);
    TestEqual(TEXT("Data authoring never saves the original profile"),Digest(Fixture.ProfileA),ProfileBefore);
    TestEqual(TEXT("Read-only imported sound remains unchanged"),Digest(Sound),SoundBefore);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCComposerSharedPreviewLease,
    "MessControl.Composer.Shared.PreviewLeaseSameAndDifferentActionHandoff",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCComposerSharedPreviewLease::RunTest(const FString&)
{
    using namespace MCComposerSharedDocumentTests;
    if(!CanRun(*this)) return false;
    FFixture Fixture;
    if(!TestTrue(TEXT("Create isolated provider/profile fixtures"),Fixture.Create())) return false;
    auto* Sound=SavedSound(*this); if(!Sound) return false;
    TStrongObjectPtr<UMCSoundPalette> Palette(NewObject<UMCSoundPalette>(GetTransientPackage(),NAME_None,RF_Transient));
    FMCSoundScene Scene; Scene.Music.Sound=Sound; Scene.Music.Volume=.15f; Scene.Music.FadeInSeconds=0;
    Palette->Scenes.Add(TEXT("FixtureScene"),Scene);
    FString Error;
    TSharedPtr<FMCComposerDocumentView> First=MakeShared<FMCComposerDocumentView>(),Second=MakeShared<FMCComposerDocumentView>(),Other=MakeShared<FMCComposerDocumentView>();
    if(!TestTrue(TEXT("First view selects A"),First->Select(Fixture.A,Error))
        || !TestTrue(TEXT("Second view selects A"),Second->Select(Fixture.A,Error))) return false;
    if(!TestTrue(*FString::Printf(TEXT("Start first lifecycle fixture: %s"),*Error),First->Play(EMCPerkPreviewScenario::Target,Error))) return false;
    const auto FirstRunner=First->GetRunner();
    if(!TestTrue(TEXT("First view owns a running/pending private fixture"),FirstRunner.IsValid() && FirstRunner->IsRunning())) return false;
    auto* FirstHero=FirstRunner->GetHero();
    TestFalse(TEXT("Non-owning same-action view cannot access the runner"),Second->GetRunner().IsValid());
    Second->Stop(); Second->Pause(true); Second->ClearSceneAudio();
    TestTrue(TEXT("Non-owner controls preserve the first run"),First->OwnsPreview() && FirstRunner->GetHero()==FirstHero && !FirstRunner->IsPaused());
    if(!TestTrue(TEXT("Second view opts into its private audio output"),Second->SetPreviewAudioEnabled(true,Error))
        || !TestTrue(TEXT("Second view retains a transient scene palette"),Second->SetSceneAudio(Palette.Get(),TEXT("FixtureScene"),1,1,Error))
        || !TestTrue(*FString::Printf(TEXT("Same-action handoff: %s"),*Error),Second->Play(EMCPerkPreviewScenario::Target,Error))) return false;
    const auto SecondRunner=Second->GetRunner();
    if(!TestTrue(TEXT("Same-action handoff grants only the second view a lease"),SecondRunner.IsValid() && Second->OwnsPreview() && !First->OwnsPreview())) return false;
    auto* SecondHero=SecondRunner->GetHero();
    TestTrue(TEXT("Same-action restart replaces the run identity"),SecondHero && SecondHero!=FirstHero);
    if(!TestTrue(TEXT("Actual private scene component exists (requires editor audio)"),SecondRunner->HasSceneAudio())) return false;
    First->Pause(true); First->Stop(); First->ClearSceneAudio(); First.Reset();
    TestTrue(TEXT("Stale same-action Stop/Pause/close preserve owner, run and scene"),Second->OwnsPreview() && SecondRunner->GetHero()==SecondHero
        && SecondRunner->IsRunning() && !SecondRunner->IsPaused() && SecondRunner->HasSceneAudio());
    if(!TestTrue(TEXT("Third view navigates to different action B without claiming preview"),Other->Select(Fixture.B,Error))) return false;
    TestTrue(TEXT("Different-action navigation does not interrupt A"),Second->OwnsPreview() && SecondRunner->GetHero()==SecondHero && SecondRunner->HasSceneAudio());
    if(!TestTrue(TEXT("Third view opts into audio"),Other->SetPreviewAudioEnabled(true,Error))
        || !TestTrue(TEXT("Third view retains its scene palette"),Other->SetSceneAudio(Palette.Get(),TEXT("FixtureScene"),1,1,Error))
        || !TestTrue(*FString::Printf(TEXT("Different-action handoff: %s"),*Error),Other->Play(EMCPerkPreviewScenario::Target,Error))) return false;
    const auto OtherRunner=Other->GetRunner();
    if(!TestTrue(TEXT("Only B view owns the different-action run"),OtherRunner.IsValid() && Other->OwnsPreview() && !Second->OwnsPreview())) return false;
    auto* OtherHero=OtherRunner->GetHero();
    TestEqual(TEXT("Runner action matches the new owning view"),OtherRunner->GetPerkID(),Fixture.B);
    Second->Pause(true); Second->Stop(); Second->ClearSceneAudio(); Second.Reset();
    TestTrue(TEXT("Stale different-action Stop/Pause/close preserve B scene and run"),Other->OwnsPreview() && OtherRunner->GetHero()==OtherHero
        && OtherRunner->IsRunning() && !OtherRunner->IsPaused() && OtherRunner->HasSceneAudio());
    Other->Stop();
    TestFalse(TEXT("The actual owner can release its lease"),Other->OwnsPreview());
    TestFalse(TEXT("Owner Stop releases the private scene"),OtherRunner->HasSceneAudio());
    TestFalse(TEXT("Owner Stop resets the fixture run"),OtherRunner->IsRunning());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCComposerSharedAudioApplyIsolation,
    "MessControl.Composer.Shared.ApplyAudioPreservesUnappliedVisualDraft",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCComposerSharedAudioApplyIsolation::RunTest(const FString&)
{
    using namespace MCComposerSharedDocumentTests;
    if(!CanRun(*this)) return false;
    FFixture Fixture;
    if(!TestTrue(TEXT("Create isolated E: canonical owner and original profile"),Fixture.Create())) return false;
    auto* Sound=SavedSound(*this); if(!Sound) return false;
    auto* System=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Rocket_Start.NS_Rocket_Start"));
    if(!TestNotNull(TEXT("Read-only existing saved Niagara source"),System)
        || !TestFalse(TEXT("Niagara source has no unsaved edits"),System->GetOutermost()->IsDirty())) return false;
    const FString ProfileBefore=Digest(Fixture.ProfileA),SystemBefore=Digest(System),SoundBefore=Digest(Sound);
    FString Error;
    const auto Document=MCComposerDocuments::Acquire(Fixture.A,Error);
    if(!TestTrue(TEXT("Acquire isolated fixture document"),Document.IsValid())) return false;
    const FGuid VisualID=Document->AddSystem(Event,System,Error);
    if(!TestTrue(TEXT("Create a real unapplied VFX recipe edit"),VisualID.IsValid() && Document->IsRuntimeDirty())) return false;
    const FGuid AudioID=Document->AddSFXLayer(Document->GetSFXDraftToken(),Event,Sound,FGuid(),Error);
    if(!TestTrue(TEXT("Create additive audio in the shared draft"),AudioID.IsValid())) return false;
    auto Audio=Document->GetSFXSlot(Event)->Layers[0]; Audio.Volume=.37f; Audio.DelaySeconds=.09f;
    if(!TestTrue(TEXT("Author audio parameters"),Document->SetSFXLayer(Document->GetSFXDraftToken(),Event,AudioID,Audio,Error))) return false;
    const uint64 LayoutRevision=Document->GetLayoutRevision();
    if(!TestTrue(*FString::Printf(TEXT("Apply only audio to the isolated E: owner: %s"),*Error),
        Document->ApplyAudio(Error,Fixture.MountRoot+TEXT("AudioOnlyOutput")))) return false;
    FMCVFXCanonicalProfileState Canonical;
    if(!TestTrue(TEXT("Read the fixture's new canonical assignment"),MCVFXProfileBindings::ReadCanonical(Fixture.A,Canonical,Error))) return false;
    auto* Saved=Canonical.Profile.LoadSynchronous();
    if(!TestTrue(TEXT("Output profile belongs only to the E: fixture mount"),Fixture.RetainOutput(Saved))) return false;
    const auto* SavedAudio=Saved->SFXSlots.Find(Event);
    if(!TestNotNull(TEXT("Saved profile contains the authored audio event"),SavedAudio)
        || !TestEqual(TEXT("Exactly one audio instance saved"),SavedAudio->Layers.Num(),1)) return false;
    TestEqual(TEXT("Audio-only writer preserves layer GUID"),SavedAudio->Layers[0].ID,AudioID);
    TestEqual(TEXT("Audio-only writer preserves gain"),SavedAudio->Layers[0].Volume,.37f);
    TestEqual(TEXT("Audio-only writer preserves delay"),SavedAudio->Layers[0].DelaySeconds,.09f);
    TestEqual(TEXT("Audio-only writer retains additive native policy"),SavedAudio->Mode,EMCEventSFXMode::Add);
    const auto* SavedVisual=Saved->Slots.Find(Event);
    if(!TestNotNull(TEXT("Canonical VFX slot remains present"),SavedVisual)) return false;
    TestTrue(TEXT("Canonical visual slot matches the pre-edit saved profile"),FMCPerkVFXSlot::StaticStruct()->CompareScriptStruct(
        SavedVisual,&Fixture.ProfileA->Slots.FindChecked(Event),0));
    TestFalse(TEXT("Saved audio baseline is now clean"),Document->IsAudioDirty());
    TestTrue(TEXT("Unapplied visual draft remains dirty"),Document->IsRuntimeDirty());
    TestTrue(TEXT("Unapplied VFX layer and identity remain in the document"),Document->GetRecipe(Event)->FindNode(VisualID)!=INDEX_NONE);
    TestEqual(TEXT("Audio save does not checkpoint or rewrite graph layout"),Document->GetLayoutRevision(),LayoutRevision);
    TestEqual(TEXT("Original saved profile bytes remain untouched"),Digest(Fixture.ProfileA),ProfileBefore);
    TestEqual(TEXT("Niagara source bytes remain untouched"),Digest(System),SystemBefore);
    TestEqual(TEXT("Sound source bytes remain untouched"),Digest(Sound),SoundBefore);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCComposerSharedKnownSiblingCommit,
    "MessControl.Composer.Shared.KnownSiblingCommitRebasesOwnerWithoutLosingDraft",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCComposerSharedKnownSiblingCommit::RunTest(const FString&)
{
    using namespace MCComposerSharedDocumentTests;
    if(!CanRun(*this)) return false;
    FFixture Fixture;
    if(!TestTrue(TEXT("Create two saved profiles in one isolated E: presentation library"),Fixture.Create())) return false;
    auto* Sound=SavedSound(*this); if(!Sound) return false;
    FString Error;
    const auto DocumentA=MCComposerDocuments::Acquire(Fixture.A,Error);
    const auto DocumentB=MCComposerDocuments::Acquire(Fixture.B,Error);
    if(!TestTrue(TEXT("Acquire both sibling documents before any commit"),DocumentA.IsValid() && DocumentB.IsValid())) return false;
    const FGuid TokenB=DocumentB->GetSFXDraftToken();
    const FString OwnerBefore=Digest(Fixture.Library);
    const FGuid AudioA=DocumentA->AddSFXLayer(DocumentA->GetSFXDraftToken(),Event,Sound,FGuid(),Error);
    if(!TestTrue(TEXT("Author additive audio for A"),AudioA.IsValid())
        || !TestTrue(TEXT("Save A through the ordinary audio writer"),DocumentA->ApplyAudio(Error,Fixture.MountRoot+TEXT("SiblingOutput")))) return false;
    FMCVFXCanonicalProfileState CanonicalA;
    if(!TestTrue(TEXT("Read A's owned saved output"),MCVFXProfileBindings::ReadCanonical(Fixture.A,CanonicalA,Error))
        || !TestTrue(TEXT("Retain A's fixture output for scoped cleanup"),Fixture.RetainOutput(CanonicalA.Profile.LoadSynchronous()))) return false;
    TestTrue(TEXT("A's commit actually changes the shared owner file"),Digest(Fixture.Library)!=OwnerBefore);
    TestTrue(TEXT("Known sibling commit retains B's exact document object"),MCComposerDocuments::Acquire(Fixture.B,Error)==DocumentB);
    TestEqual(TEXT("Known sibling commit never regenerates B's draft token"),DocumentB->GetSFXDraftToken(),TokenB);
    TestTrue(TEXT("B remains editable without reopening after the owned A commit"),DocumentB->CanEditProfile());
    const FGuid AudioB=DocumentB->AddSFXLayer(TokenB,Event,Sound,FGuid(),Error);
    if(!TestTrue(TEXT("B can author audio after A changed their shared owner"),AudioB.IsValid())) return false;
    auto Audio=DocumentB->GetSFXSlot(Event)->Layers[0]; Audio.Volume=.58f;
    if(!TestTrue(TEXT("B keeps its original token for subsequent edits"),DocumentB->SetSFXLayer(TokenB,Event,AudioB,Audio,Error))
        || !TestTrue(*FString::Printf(TEXT("Known A commit permits B audio apply without reinitialization: %s"),*Error),
            DocumentB->ApplyAudio(Error,Fixture.MountRoot+TEXT("SiblingOutput")))) return false;
    FMCVFXCanonicalProfileState CanonicalB;
    if(!TestTrue(TEXT("Read B's saved assignment"),MCVFXProfileBindings::ReadCanonical(Fixture.B,CanonicalB,Error))) return false;
    auto* SavedB=CanonicalB.Profile.LoadSynchronous();
    if(!TestTrue(TEXT("Retain B's fixture output for scoped cleanup"),Fixture.RetainOutput(SavedB))) return false;
    const auto* SavedAudio=SavedB->SFXSlots.Find(Event);
    if(!TestNotNull(TEXT("B's canonical profile contains audio"),SavedAudio)
        || !TestEqual(TEXT("B saves exactly its own audio layer"),SavedAudio->Layers.Num(),1)) return false;
    TestEqual(TEXT("B layer identity survives sibling rebase and apply"),SavedAudio->Layers[0].ID,AudioB);
    TestEqual(TEXT("B layer gain survives sibling rebase and apply"),SavedAudio->Layers[0].Volume,.58f);
    TestEqual(TEXT("B commit preserves A's already saved assignment"),Fixture.Library->Profiles.FindChecked(Fixture.A).ToSoftObjectPath(),CanonicalA.Profile.ToSoftObjectPath());

    // Negative control: a direct external edit of B is not a known sibling commit.
    // Only this fixture-owned saved profile is changed, never a project canonical asset.
    const FGuid CurrentTokenB=DocumentB->GetSFXDraftToken();
    Audio=DocumentB->GetSFXSlot(Event)->Layers[0]; Audio.DelaySeconds=.17f;
    if(!TestTrue(TEXT("Create a fresh B audio draft for the external-conflict control"),
        DocumentB->SetSFXLayer(CurrentTokenB,Event,AudioB,Audio,Error))) return false;
    SavedB->Slots.FindChecked(Event).Mode=EMCPerkVFXSlotMode::Disabled;
    SavedB->GetOutermost()->MarkPackageDirty();
    if(!TestTrue(TEXT("Save only the owned B fixture profile as an external author would"),
        UEditorLoadingAndSavingUtils::SavePackages({SavedB->GetOutermost()},true))) return false;
    const FString OwnerBeforeReject=Digest(Fixture.Library),ExternalProfile=Digest(SavedB);
    Error.Reset();
    TestFalse(TEXT("An external change to B's own profile must still reject ApplyAudio"),
        DocumentB->ApplyAudio(Error,Fixture.MountRoot+TEXT("RejectedOutput")));
    TestFalse(TEXT("The rejected external conflict supplies a reason"),Error.IsEmpty());
    TestEqual(TEXT("Rejected apply never changes the owner file"),Digest(Fixture.Library),OwnerBeforeReject);
    TestEqual(TEXT("Rejected apply never overwrites the external profile"),Digest(SavedB),ExternalProfile);
    TestEqual(TEXT("Rejected apply preserves the current draft token"),DocumentB->GetSFXDraftToken(),CurrentTokenB);
    TestTrue(TEXT("Rejected apply retains B's unsaved audio edit"),DocumentB->IsAudioDirty());
    TestEqual(TEXT("Rejected apply preserves the intended audio GUID"),DocumentB->GetSFXSlot(Event)->Layers[0].ID,AudioB);
    TestEqual(TEXT("Rejected apply preserves the intended delay"),DocumentB->GetSFXSlot(Event)->Layers[0].DelaySeconds,.17f);
    return true;
}
#endif
