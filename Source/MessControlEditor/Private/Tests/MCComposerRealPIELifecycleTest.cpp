#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "MCPerkPreviewRunner.h"
#include "MCGameUserSettings.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/Package.h"

namespace MCComposerRealPIETest
{
struct FState
{
    FAutomationTestBase* Test=nullptr;
    TUniquePtr<FMCPerkPreviewRunner> Runner;
    // The old instance/world are deliberately weak. A strong test-owned
    // reference would recreate the engine leak and invalidate this regression.
    TWeakObjectPtr<UGameInstance> BeforeInstance;
    TWeakObjectPtr<UWorld> BeforeWorld,FixtureWorld;
    FString OriginalMapFilename;
    EPlayNetMode OldNetMode=PIE_Standalone;
    int32 OldClientCount=1;
    bool OldOneProcess=true,OldVoice=false,bSettingsCaptured=false,bCleanupReported=false;
    TWeakObjectPtr<UMCGameUserSettings> PreferencesObject;
    bool bActualPIEObserved=false,bShutdownObserved=false,bPreBeginObserved=false,bReleasedBeforeBegin=false;
    FDelegateHandle BeforeHandle,BeginHandle,ShutdownHandle;
    ~FState()
    {
        FEditorDelegates::PreBeginPIE.Remove(BeforeHandle);
        FEditorDelegates::BeginPIE.Remove(BeginHandle);
        FEditorDelegates::ShutdownPIE.Remove(ShutdownHandle);
        Runner.Reset();RestoreSettings();
    }
    bool RestoreSettings()
    {
        if(!bSettingsCaptured)return true;
        // In particular, never restore OldVoice=true while actual/queued PIE
        // still exists. This also guards the destructor/failure path.
        const bool Idle=GEditor && !GEditor->PlayWorld && !GEditor->IsPlaySessionRequestQueued()
            && !GEditor->IsPlayingSessionInEditor();
        auto* Preferences=PreferencesObject.Get();
        if(!Idle || !Preferences)
        {
            if(Preferences)Preferences->SetVoiceEnabled(false);
            if(!bCleanupReported && Test)
            {
                Test->AddError(FString::Printf(TEXT("CLEANUP NEEDED: actual/queued PIE or settings ownership remains unresolved. Voice stays disabled; preferences were not saved. After owned PIE ends, restore voice=%s, netMode=%d, clients=%d, oneProcess=%s and map %s."),
                    OldVoice?TEXT("true"):TEXT("false"),int32(OldNetMode),OldClientCount,OldOneProcess?TEXT("true"):TEXT("false"),*OriginalMapFilename));
                bCleanupReported=true;
            }
            return false;
        }
        auto* Play=GetMutableDefault<ULevelEditorPlaySettings>();
        Play->SetPlayNetMode(OldNetMode);Play->SetPlayNumberOfClients(OldClientCount);Play->SetRunUnderOneProcess(OldOneProcess);
        Preferences->SetVoiceEnabled(OldVoice);
        bSettingsCaptured=false; // Never SaveConfig/SaveSettings.
        return true;
    }
};

class FAwaitActualPIE final:public IAutomationLatentCommand
{
public:
    explicit FAwaitActualPIE(TSharedRef<FState> In):State(In){}
    bool Update() override
    {
        if(Started==0)Started=FPlatformTime::Seconds();
        UWorld* W=GEditor?GEditor->PlayWorld.Get():nullptr;
        if(!W)
        {
            if(FPlatformTime::Seconds()-Started<20.)return false;
            State->Test->AddError(TEXT("Actual engine PIE did not start; this is not a delegate-only pass."));return true;
        }
        State->bActualPIEObserved=true;
        State->Test->TestTrue(TEXT("Actual world is PIE"),W->WorldType==EWorldType::PIE);
        State->Test->TestTrue(TEXT("Disposable override uses plain GameModeBase"),W->GetAuthGameMode()&&W->GetAuthGameMode()->GetClass()==AGameModeBase::StaticClass());
        State->Test->TestTrue(TEXT("Actual native PreBeginPIE was observed"),State->bPreBeginObserved);
        State->Test->TestTrue(TEXT("Runner released its world before native BeginPIE"),State->bReleasedBeforeBegin);
        State->Test->TestNull(TEXT("Runner has no world throughout PIE"),State->Runner->GetWorld());
        State->Test->TestFalse(TEXT("Initialize refuses to recreate a private GameInstance during PIE"),State->Runner->Initialize());
        State->Test->TestFalse(TEXT("Runner did not start playback"),State->Runner->IsRunning());
        return true;
    }
private:
    TSharedRef<FState> State;double Started=0;
};

class FAwaitActualShutdown final:public IAutomationLatentCommand
{
public:
    explicit FAwaitActualShutdown(TSharedRef<FState> In):State(In){}
    bool Update() override
    {
        if(Started==0)Started=FPlatformTime::Seconds();
        if(!GEditor||GEditor->PlayWorld||GEditor->IsPlaySessionRequestQueued()||GEditor->IsPlayingSessionInEditor()||!State->bShutdownObserved)
        {
            if(FPlatformTime::Seconds()-Started<20.)return false;
            State->Test->AddError(TEXT("Actual EndPlayMap/ShutdownPIE did not finish."));return true;
        }
        State->Test->TestTrue(TEXT("PIE really existed before its actual shutdown"),State->bActualPIEObserved);
        // ShutdownPIE follows PlayLevel.cpp's all-UGameInstance garbage mark,
        // mandatory CollectGarbage and leak/fatal checks. No manual GC or
        // delegate broadcast substitutes for that engine path in this test.
        State->Test->TestFalse(TEXT("Old private GameInstance no longer survives actual PIE GC"),State->BeforeInstance.IsValid());
        State->Test->TestFalse(TEXT("Old private world no longer survives actual PIE GC"),State->BeforeWorld.IsValid());
        State->Test->TestNull(TEXT("Shutdown does not automatically recreate the preview world"),State->Runner->GetWorld());
        State->Test->TestFalse(TEXT("Shutdown does not automatically restart playback"),State->Runner->IsRunning());
        const bool Recreated=State->Runner->Initialize();
        State->Test->TestTrue(TEXT("Explicit Initialize succeeds after actual shutdown and GC"),Recreated);
        if(Recreated)
        {
            auto* New=State->Runner->GetWorld();
            State->Test->TestTrue(TEXT("Recreated preview uses its intended isolated Game world"),New&&New->WorldType==EWorldType::Game);
            State->Test->TestTrue(TEXT("Recreated instance has a new UObject weak identity"),New&&TWeakObjectPtr<UGameInstance>(New->GetGameInstance())!=State->BeforeInstance);
            State->Runner->Tick(.02f);
            State->Test->TestFalse(TEXT("Explicit recreation alone still does not play an action"),State->Runner->IsRunning());
        }
        return true;
    }
private:
    TSharedRef<FState> State;double Started=0;
};

class FRestoreEditor final:public IAutomationLatentCommand
{
public:
    explicit FRestoreEditor(TSharedRef<FState> In):State(In){}
    bool Update() override
    {
        if(!GEditor||GEditor->PlayWorld||GEditor->IsPlaySessionRequestQueued()||GEditor->IsPlayingSessionInEditor())
        {State->RestoreSettings();return true;} // Fail closed: voice remains off, explicit cleanup-needed report.
        State->Runner.Reset();
        if(!State->RestoreSettings())return true;
        // This package was created only by this test and was never saved.
        // Avoid prompting to save its temporary PlayerStart/GameMode override.
        if(auto* Owned=State->FixtureWorld.Get())
            if(GEditor&&GEditor->GetEditorWorldContext().World()==Owned)Owned->GetOutermost()->SetDirtyFlag(false);
        State->Test->TestTrue(TEXT("Original saved Editor map restored without saving the fixture"),FEditorFileUtils::LoadMap(State->OriginalMapFilename,false,false));
        return true;
    }
private:TSharedRef<FState> State;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCComposerRealPIEGarbageCollection,
    "MessControl.VFX.Preview.RealPIEGarbageCollection",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter|EAutomationTestFlags::NonNullRHI)

bool FMCComposerRealPIEGarbageCollection::RunTest(const FString&)
{
    // Run this one named test only in the owner's clean Editor. It changes the
    // active Editor map temporarily and intentionally exercises real PIE.
    if(!GEditor||GEditor->PlayWorld||GEditor->IsPlaySessionRequestQueued())
    {AddError(TEXT("A clean idle Editor without queued/active PIE is required."));return false;}
    TArray<UPackage*> DirtyMaps,DirtyContent;
    UEditorLoadingAndSavingUtils::GetDirtyMapPackages(DirtyMaps);
    UEditorLoadingAndSavingUtils::GetDirtyContentPackages(DirtyContent);
    if(!DirtyMaps.IsEmpty()||!DirtyContent.IsEmpty()){AddError(TEXT("Preserve unsaved packages before this explicit map-switching regression."));return false;}
    auto* EditorWorld=GEditor->GetEditorWorldContext().World();
    if(!EditorWorld){AddError(TEXT("No current Editor world to restore."));return false;}
    FString Restore;
    if(!FPackageName::DoesPackageExist(EditorWorld->GetOutermost()->GetName(),&Restore)||!IFileManager::Get().FileExists(*Restore))
    {AddError(TEXT("The original Editor map must already be saved; no automatic save is allowed."));return false;}
    auto State=MakeShared<MCComposerRealPIETest::FState>();State->Test=this;State->OriginalMapFilename=Restore;
    auto* Preferences=UMCGameUserSettings::Get();auto* Play=GetMutableDefault<ULevelEditorPlaySettings>();
    if(!Preferences||!Play){AddError(TEXT("Actual settings objects are unavailable."));return false;}
    State->PreferencesObject=Preferences;State->OldVoice=Preferences->GetVoiceEnabled();Play->GetPlayNetMode(State->OldNetMode);
    Play->GetPlayNumberOfClients(State->OldClientCount);Play->GetRunUnderOneProcess(State->OldOneProcess);State->bSettingsCaptured=true;
    Preferences->SetVoiceEnabled(false);Play->SetPlayNetMode(PIE_Standalone);Play->SetPlayNumberOfClients(1);Play->SetRunUnderOneProcess(true);
    auto* Fixture=FAutomationEditorCommonUtils::CreateNewMap();
    if(!Fixture){AddError(TEXT("Could not create disposable empty Editor map."));return false;}
    State->FixtureWorld=Fixture;Fixture->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();
    Fixture->SpawnActor<APlayerStart>();
    State->Runner=MakeUnique<FMCPerkPreviewRunner>();
    if(!State->Runner->Initialize())
    {
        AddError(State->Runner->GetStatus());
        MCComposerRealPIETest::FRestoreEditor(State).Update();return false;
    }
    State->BeforeWorld=State->Runner->GetWorld();State->BeforeInstance=State->Runner->GetWorld()->GetGameInstance();
    TestTrue(TEXT("Initialized preview really owns a private GameInstance before PIE"),State->BeforeInstance.IsValid());
    const TWeakPtr<MCComposerRealPIETest::FState> Weak=State;
    // Multicast delegates do not promise subscriber order. Observe release
    // at the next real engine phase, after every PreBeginPIE subscriber ran.
    State->BeforeHandle=FEditorDelegates::PreBeginPIE.AddLambda([Weak](bool)
    {if(auto S=Weak.Pin())S->bPreBeginObserved=true;});
    State->BeginHandle=FEditorDelegates::BeginPIE.AddLambda([Weak](bool)
    {if(auto S=Weak.Pin())S->bReleasedBeforeBegin=S->Runner&&S->Runner->GetWorld()==nullptr;});
    State->ShutdownHandle=FEditorDelegates::ShutdownPIE.AddLambda([Weak](bool)
    {if(auto S=Weak.Pin())S->bShutdownObserved=true;});
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(MCComposerRealPIETest::FAwaitActualPIE(State));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    ADD_LATENT_AUTOMATION_COMMAND(MCComposerRealPIETest::FAwaitActualShutdown(State));
    ADD_LATENT_AUTOMATION_COMMAND(MCComposerRealPIETest::FRestoreEditor(State));
    return true;
}
#endif
