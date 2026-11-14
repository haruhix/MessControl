#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "MCDataAssets.h"
#include "MCPerkPreviewRunner.h"
#include "AudioDeviceManager.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSceneAudioPreviewOffRetention,
    "MessControl.Audio.ScenePreview.DisabledRetainsDraftWithoutOutput",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSceneAudioPreviewOffRetention::RunTest(const FString&)
{
    FMCPerkPreviewRunner Runner;FString Error;
    TStrongObjectPtr<UMCSoundPalette> Palette(NewObject<UMCSoundPalette>(GetTransientPackage(),NAME_None,RF_Transient));
    TWeakObjectPtr<UMCSoundPalette> Retained=Palette.Get();
    TestTrue(TEXT("A scene draft can be selected while output is disabled"),Runner.SetSceneAudio(Palette.Get(),TEXT("Hub"),Error));
    TestNull(TEXT("Selecting the background does not create a world or select another audio device"),Runner.GetWorld());
    TestFalse(TEXT("Selecting a scene never implicitly enables sound"),Runner.IsAudioEnabled());
    TestFalse(TEXT("A retained draft does not start the preview service clock"),Runner.HasSceneAudio());
    TestFalse(TEXT("A missing scene is rejected before replacing the current draft"),Runner.SetSceneAudio(Palette.Get(),TEXT("Tests.MissingScene"),Error));
    TestTrue(TEXT("Missing scene provides a reason"),!Error.IsEmpty());
    Palette.Reset();CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("The runner retains the transient draft across GC"),Retained.IsValid());
    Runner.Cancel();
    TestTrue(TEXT("Disabling sound remains safe without a preview world"),Runner.SetAudioEnabled(false,Error));
    Runner.SetSceneAudioMix(0.f,.4f);Runner.SetPaused(true);
    TestNull(TEXT("Cancel, local mix and pause do not create output"),Runner.GetWorld());
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("Cancel/off retain the desired scene for the next explicit preview"),Retained.IsValid());
    Runner.ClearSceneAudio();CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("Clearing the background releases the retained transient draft"),Retained.IsValid());
    return true;
}

namespace
{
// Exercises the runner's real fixture-reset boundary without a saved perk row,
// emitted audio or a claimed gameplay scenario. Removed when the test exits.
class FSceneAudioResetBoundary final:public IMCPerkPreviewAdapter
{
public:
    bool Prepare(FMCPerkPreviewContext& Context,FString&)override{return Context.World && Context.Hero;}
    void Tick(FMCPerkPreviewContext&,float)override{}
    FString GetPhaseName(const FMCPerkPreviewContext&)const override{return TEXT("Scene audio reset boundary regression");}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSceneAudioPreviewResetRetention,
    "MessControl.Audio.ScenePreview.PrivateWorldResetRetainsDraftWithoutOutput",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSceneAudioPreviewResetRetention::RunTest(const FString&)
{
    if(!GEditor || GEditor->PlayWorld){AddError(TEXT("Run the private scene reset regression outside PIE."));return false;}
    const FName FixtureID(TEXT("Tests.SceneAudio.ResetBoundary"));
    FMCPerkPreviewAdapterEntry Entry;Entry.PerkID=FixtureID;Entry.Kind=EMCPerkPreviewAdapterKind::Context;
    Entry.bSupportsProfileOverride=false;Entry.Scenarios={EMCPerkPreviewScenario::Target};
    Entry.Create=[]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FSceneAudioResetBoundary>();};
    MCPerkPreviewAdapters::Register(MoveTemp(Entry));
    ON_SCOPE_EXIT{MCPerkPreviewAdapters::Unregister(FixtureID);};
    UWorld* EditorWorld=GEditor->GetEditorWorldContext().World();
    auto* AudioManager=FAudioDeviceManager::Get();
    const uint32 MainAudioID=AudioManager?AudioManager->GetMainAudioDeviceID():INDEX_NONE;
    FMCPerkPreviewRunner Runner;FString Error;
    TStrongObjectPtr<UMCSoundPalette> Palette(NewObject<UMCSoundPalette>(GetTransientPackage(),NAME_None,RF_Transient));
    TWeakObjectPtr<UMCSoundPalette> DesiredPalette=Palette.Get();
    if(!TestTrue(TEXT("Background is staged before fixture initialization"),Runner.SetSceneAudio(Palette.Get(),TEXT("Hub"),Error)))return false;
    Palette.Reset();
    if(!TestTrue(TEXT("Actual private preview world initializes"),Runner.Initialize())){AddError(Runner.GetStatus());return false;}
    UWorld* PrivateWorld=Runner.GetWorld();
    TestTrue(TEXT("Preview uses a transient Game world distinct from the user's level"),PrivateWorld && PrivateWorld!=EditorWorld
        && PrivateWorld->WorldType==EWorldType::Game && PrivateWorld->GetOutermost()==GetTransientPackage());
    for(int32 Attempt=0;Attempt<2;++Attempt) {
        Runner.Reset();
        TestTrue(TEXT("Fixture reset keeps the same isolated world"),Runner.GetWorld()==PrivateWorld);
        if(!TestTrue(TEXT("StartAction crosses its own reset boundary"),Runner.StartAction(FixtureID,EMCPerkPreviewScenario::Target))) {
            AddError(Runner.GetStatus());return false;
        }
        CollectGarbage(RF_NoFlags);
        TestTrue(TEXT("Reset and StartAction retain the staged palette across GC"),DesiredPalette.IsValid());
        TestTrue(TEXT("Action preparation never switches the preview world"),Runner.GetWorld()==PrivateWorld);
        TestFalse(TEXT("Staged background never enables playback during reset"),Runner.IsAudioEnabled());
        TestFalse(TEXT("No scene actor or audio service clock is started without output"),Runner.HasSceneAudio());
        TestFalse(TEXT("Private world keeps its output gate closed"),PrivateWorld->AllowAudioPlayback());
    }
    Runner.Cancel();Runner.ClearSceneAudio();CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("Explicit clear releases the palette after actual fixture resets"),DesiredPalette.IsValid());
    TestTrue(TEXT("The user's editor world is preserved"),GEditor->GetEditorWorldContext().World()==EditorWorld);
    if(AudioManager)TestEqual(TEXT("The editor's main audio device is unchanged"),AudioManager->GetMainAudioDeviceID(),MainAudioID);
    return true;
}
#endif
