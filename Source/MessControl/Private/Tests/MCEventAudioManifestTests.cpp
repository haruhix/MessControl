#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "MCPreloadManifest.h"
#include "MCWorldPreloadSubsystem.h"
#include "MCEventSFXReadiness.h"
#include "MCPerkVFXProfile.h"
#include "MCPerkTypes.h"
#include "MCVFXPresentationLibrary.h"
#include "MCDataAssets.h"
#include "MCSceneAudioComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundWaveProcedural.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNodeMixer.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundNodeWaveParam.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundAttenuation.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace MCEventAudioManifestTests
{
struct FPackage
{
    UPackage* Package;
    TArray<UObject*> Assets;
    explicit FPackage(const TCHAR* Leaf=TEXT("Audio"),const FString& Prefix=TEXT("/Game/Tests/TransientAudio/"))
    {Package=CreatePackage(*(Prefix+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT("/")+Leaf));}
    template<class T> T* Asset(const TCHAR* Name)
    {auto* Object=NewObject<T>(Package,FName(Name),RF_Public|RF_Standalone);Assets.Add(Object);return Object;}
    ~FPackage(){for(UObject* Object:Assets)if(IsValid(Object))Object->ClearFlags(RF_Public|RF_Standalone);}
};
struct FWorld
{
    FPackage Package;
    UWorld* World;
    explicit FWorld(const TCHAR* Map=TEXT("AudioWorld")) : Package(Map)
    {
        const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
            .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
        World=UWorld::CreateWorld(EWorldType::EditorPreview,false,FName(Map),Package.Package,false,GMaxRHIFeatureLevel,&Values);
        GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
    }
    ~FWorld(){GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventAudioProfileManifest,"MessControl.Audio.Manifest.ProfileAndSoundResourceClosure",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventAudioProfileManifest::RunTest(const FString&)
{
    using namespace MCEventAudioManifestTests;FPackage P;
    auto* Profile=NewObject<UMCPerkVFXProfile>();FMCEventSFXSlot Slot;Slot.Mode=EMCEventSFXMode::Add;
    FMCEventSFXLayer L;L.bEnabled=false;
    L.Sound=FSoftObjectPath(TEXT("/Game/Tests/Audio/S_Unloaded.S_Unloaded"));
    L.Attenuation=FSoftObjectPath(TEXT("/Game/Tests/Audio/ATT_Unloaded.ATT_Unloaded"));
    L.Concurrency=FSoftObjectPath(TEXT("/Game/Tests/Audio/SC_Unloaded.SC_Unloaded"));Slot.Layers.Add(L);
    Profile->SFXSlots.Add(TEXT("Proc"),Slot);TArray<FSoftObjectPath> Paths;
    FMCPreloadManifest::CollectSoftReferences(Profile,Paths);
    TestTrue(TEXT("SoundBase soft refs inside actual event maps are reachable even for retained disabled layers"),Paths.Contains(L.Sound.ToSoftObjectPath()));
    TestTrue(TEXT("Custom attenuation is prepared with the source"),Paths.Contains(L.Attenuation.ToSoftObjectPath()));
    TestTrue(TEXT("Custom concurrency is prepared with the source"),Paths.Contains(L.Concurrency.ToSoftObjectPath()));
    TestNull(TEXT("Pure dependency discovery does not load an absent source"),L.Sound.Get());
    auto* Wave=P.Asset<USoundWave>(TEXT("Wave"));auto* Submix=P.Asset<USoundSubmix>(TEXT("Submix"));
    auto* Class=P.Asset<USoundClass>(TEXT("SoundClass"));auto* Att=P.Asset<USoundAttenuation>(TEXT("Attenuation"));
    auto* Concurrency=P.Asset<USoundConcurrency>(TEXT("Concurrency"));
    Wave->SoundSubmixObject=Submix;Wave->SoundClassObject=Class;Wave->AttenuationSettings=Att;Wave->ConcurrencySet.Add(Concurrency);
    Paths.Reset();FMCPreloadManifest::CollectSoftReferences(Wave,Paths);
    TestTrue(TEXT("Original source submix is retained"),Paths.Contains(FSoftObjectPath(Submix)));
    TestTrue(TEXT("Original source SoundClass is retained"),Paths.Contains(FSoftObjectPath(Class)));
    TestTrue(TEXT("Original source attenuation is retained"),Paths.Contains(FSoftObjectPath(Att)));
    TestTrue(TEXT("Original source concurrency is retained"),Paths.Contains(FSoftObjectPath(Concurrency)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventAudioStaticCue,"MessControl.Audio.Manifest.StaticCueActualWavePlayers",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventAudioStaticCue::RunTest(const FString&)
{
    using namespace MCEventAudioManifestTests;FPackage P;auto* Cue=P.Asset<USoundCue>(TEXT("Cue"));
    auto* A=P.Asset<USoundWave>(TEXT("WaveA"));auto* B=P.Asset<USoundWave>(TEXT("WaveB"));
    auto* Mixer=NewObject<USoundNodeMixer>(Cue);auto* PlayerA=NewObject<USoundNodeWavePlayer>(Cue);
    auto* PlayerB=NewObject<USoundNodeWavePlayer>(Cue);PlayerA->SetSoundWave(A);PlayerB->SetSoundWave(B);
    Mixer->ChildNodes={PlayerA,PlayerB,PlayerA};Cue->FirstNode=Mixer;FMCEventSFXSamplePlan Plan;FString Error;
    if(!TestTrue(TEXT("Static Cue closure follows its real authored graph"),FMCPreloadManifest::BuildEventAudioPlan(Cue,Plan,Error)))
    {AddError(Error);return false;}
    TestEqual(TEXT("Shared WavePlayer branches are deduplicated without choosing a random branch"),Plan.SampleWaves.Num(),2);
    TestTrue(TEXT("Actual Wave dependencies remain exact"),Plan.SampleWaves.Contains(A) && Plan.SampleWaves.Contains(B));
    TestTrue(TEXT("Only an audited static graph receives closure flags"),Plan.bDependencyAuditComplete && Plan.bGraphPrepared);
    TArray<FSoftObjectPath> Paths;FMCPreloadManifest::CollectSoftReferences(Cue,Paths);
    TestTrue(TEXT("Async manifest waves can discover the actual private authored wave reference"),Paths.Contains(FSoftObjectPath(A)) && Paths.Contains(FSoftObjectPath(B)));
    auto* Dynamic=NewObject<USoundNodeWaveParam>(Cue);Mixer->ChildNodes.Add(Dynamic);
    TestFalse(TEXT("Runtime-selected wave inputs cannot be falsely called a complete static closure"),FMCPreloadManifest::BuildEventAudioPlan(Cue,Plan,Error));
    TestTrue(TEXT("The unsupported node is named for author validation"),Error.Contains(TEXT("SoundNodeWaveParam")));
    TestEqual(TEXT("Failed audit leaves the previous output plan intact"),Plan.SampleWaves.Num(),2);
    Mixer->ChildNodes={Mixer};
    TestFalse(TEXT("Malformed cyclic sound graphs terminate explicitly"),FMCPreloadManifest::BuildEventAudioPlan(Cue,Plan,Error));
    TestTrue(TEXT("Cycle has an actionable reason"),Error.Contains(TEXT("cycle")));
    auto* Procedural=NewObject<USoundWaveProcedural>();
    TestFalse(TEXT("Procedural/MetaSound-style resource inputs require their actual provider audit"),FMCPreloadManifest::BuildEventAudioPlan(Procedural,Plan,Error));
    TestTrue(TEXT("Dynamic closure is explicitly unsupported"),Error.Contains(TEXT("Unsupported")));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventAudioLocationScope,"MessControl.Audio.Manifest.HubSelectedSceneAndPerkScope",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventAudioLocationScope::RunTest(const FString&)
{
    using namespace MCEventAudioManifestTests;FWorld Hub(TEXT("Hub")),Arena(TEXT("L_Mouth"));
    auto* Palette=NewObject<UMCSoundPalette>();Palette->Events.Reset();Palette->Scenes.Reset();
    const FSoftObjectPath HubSound(TEXT("/Game/Tests/Audio/S_HubOnly.S_HubOnly"));
    const FSoftObjectPath BossSound(TEXT("/Game/Tests/Audio/S_BossOnly.S_BossOnly"));
    FMCSoundScene HubScene,BossScene;HubScene.Music.Sound=HubSound;BossScene.Music.Sound=BossSound;
    Palette->Scenes.Add(TEXT("Hub"),HubScene);Palette->Scenes.Add(TEXT("NutEncounter"),BossScene);
    auto* Scene=NewObject<UMCSceneAudioComponent>();Scene->SoundPalette=Palette;Scene->SceneContext=TEXT("Hub");
    TArray<FSoftObjectPath> Paths;FMCPreloadManifest::CollectSoftReferences(Scene,Paths,Hub.World);
    TestTrue(TEXT("Hub retains its actual selected scene source"),Paths.Contains(HubSound));
    TestFalse(TEXT("Hub does not load every boss scene from the shared palette"),Paths.Contains(BossSound));
    FMCPreloadManifest::CollectSoftReferences(Palette,Paths,Hub.World);
    TestFalse(TEXT("Expanding the palette cannot bypass location scope"),Paths.Contains(BossSound));
    auto* Table=NewObject<UDataTable>();Table->RowStruct=FMCPerkDefinition::StaticStruct();
    FMCPerkDefinition Row;const FSoftObjectPath GameplayProfile(TEXT("/Game/Tests/Audio/DA_ArenaProfile.DA_ArenaProfile"));
    Row.VFXProfile=GameplayProfile;Table->AddRow(TEXT("PresentationProbe"),Row);Paths.Reset();
    FMCPreloadManifest::CollectSoftReferences(Table,Paths,Hub.World);
    TestFalse(TEXT("Hub UI perk metadata does not preload every gameplay presentation profile"),Paths.Contains(GameplayProfile));
    Paths.Reset();FMCPreloadManifest::CollectSoftReferences(Table,Paths,Arena.World);
    TestTrue(TEXT("Actual arena perk presentation profiles remain reachable"),Paths.Contains(GameplayProfile));
    Paths.Reset();FMCPreloadManifest::Collect(Hub.World,Paths);
    TestFalse(TEXT("Hub does not acquire the complete mouth boss presentation library"),Paths.Contains(UMCVFXPresentationLibrary::MouthLibraryPath()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventAudioWorldService,"MessControl.Audio.Readiness.SharedWorldServiceBeforeCommit",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventAudioWorldService::RunTest(const FString&)
{
    // Direct API fixture: this object is not in the world's initialized subsystem
    // collection. Exercise the production reset hook without a fake Deinitialize.
    using namespace MCEventAudioManifestTests;FWorld W;auto* Service=NewObject<UMCWorldPreloadSubsystem>(W.World);
    auto* Wave=NewObject<USoundWave>();Wave->Duration=1.f;FString Error;
    if(!TestTrue(TEXT("Composer can prepare resident selected sources in the same world service while visual phase is Idle"),Service->PrepareEventAudioSources({Wave,Wave},Error)))
    {AddError(Error);return false;}
    const auto* First=Service->GetEventAudioReadiness();if(!TestNotNull(TEXT("World service retains its actual readiness helper"),First))return false;
    TestTrue(TEXT("Disabled output is explicit and never a resource certificate"),First->GetState()==EMCEventSFXReadiness::OutputUnavailable && !First->IsReady());
    TestEqual(TEXT("Duplicate source collection does not inflate the location root set"),Service->EventAudioSources.Num(),1);
    Service->PollEventAudioReadiness();TestTrue(TEXT("Unchanged selected sources reuse the shared service"),Service->PrepareEventAudioSources({Wave},Error) && Service->GetEventAudioReadiness()==First);
    TestTrue(TEXT("Audio preparation does not advance or reset the visual phase"),Service->GetPhase()==EMCWorldPreloadPhase::Idle);
    Service->EventAudioDeviceId^=1u;
    TestNull(TEXT("A changed output device cannot reuse a prior certificate"),Service->GetEventAudioReadiness());
    TestTrue(TEXT("Explicit preparation restores the matching output resource state"),Service->PrepareEventAudioSources({Wave},Error));
    TestNotNull(TEXT("The matching service becomes discoverable again"),Service->GetEventAudioReadiness());
    Service->ResetPreparation();TestNull(TEXT("Preparation reset releases the retained audio service and startup handles"),Service->GetEventAudioReadiness());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventAudioLegacySafety,"MessControl.Audio.Readiness.EmptyComposerSlotsPreserveLegacyDynamicAudio",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventAudioLegacySafety::RunTest(const FString&)
{
    using namespace MCEventAudioManifestTests;FWorld W;auto* Service=NewObject<UMCWorldPreloadSubsystem>(W.World);
    auto* LegacyCue=NewObject<USoundCue>();LegacyCue->FirstNode=NewObject<USoundNodeWaveParam>(LegacyCue);
    auto* EmptyProfile=NewObject<UMCPerkVFXProfile>();
    FMCEventSFXSamplePlan Audit;FString AuditError;
    TestFalse(TEXT("The fixture really has unsupported dynamic resource closure"),FMCPreloadManifest::BuildEventAudioPlan(LegacyCue,Audit,AuditError));
    Service->RetainedAssets.Add(LegacyCue);Service->RetainedAssets.Add(EmptyProfile);
    TestTrue(TEXT("An unchanged legacy dynamic cue cannot hold or fail an empty Composer audio plan"),Service->TickEventAudioReadiness());
    TestFalse(TEXT("Legacy audio audit does not fail location preparation"),Service->HasFailed());
    TestEqual(TEXT("Legacy dynamic sources are not promoted to required authored SFX roots"),Service->EventAudioSources.Num(),0);
    TestTrue(TEXT("Its unmapped closure is audited once and remains explicit"),Service->AuditedLegacyAudioSources.Contains(TWeakObjectPtr<USoundBase>(LegacyCue)));
    TestTrue(TEXT("Original dynamic source remains unchanged"),LegacyCue->FirstNode->IsA<USoundNodeWaveParam>());
    // NewObject did not Initialize this tickable subsystem; only the collection
    // may pair Initialize/Deinitialize. Reset the same production-owned resources.
    Service->ResetPreparation();return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventAudioMountedDependencies,"MessControl.Audio.Manifest.ExplicitMountedRuntimeDependencies",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventAudioMountedDependencies::RunTest(const FString&)
{
    using namespace MCEventAudioManifestTests;
    const FString Root=TEXT("/MCComposerAudio_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT("/");
    const auto Mount=FPackageName::RegisterMountPoint(Root,FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Automation/AudioMount")));
    if(!TestTrue(TEXT("A dedicated explicit test content root is mounted"),bool(Mount)))return false;
    FPackage P(TEXT("Mounted"),Root);auto* Wave=P.Asset<USoundWave>(TEXT("Wave"));
    auto* Submix=P.Asset<USoundSubmix>(TEXT("Submix"));Wave->SoundSubmixObject=Submix;
    auto* Cue=P.Asset<USoundCue>(TEXT("Cue"));auto* Player=NewObject<USoundNodeWavePlayer>(Cue);Player->SetSoundWave(Wave);Cue->FirstNode=Player;
    auto* Profile=NewObject<UMCPerkVFXProfile>();FMCEventSFXSlot Slot;Slot.Mode=EMCEventSFXMode::Add;
    FMCEventSFXLayer L;L.Sound=Cue;Slot.Layers.Add(L);Profile->SFXSlots.Add(TEXT("Proc"),Slot);
    TArray<FSoftObjectPath> Paths;FMCPreloadManifest::CollectSoftReferences(Profile,Paths);
    TestTrue(TEXT("An explicitly referenced mounted runtime sound is not dropped by a Game-only filter"),Paths.Contains(FSoftObjectPath(Cue)));
    FMCPreloadManifest::CollectSoftReferences(Cue,Paths);
    TestTrue(TEXT("A Game or mounted Cue can retain a mounted child Wave"),Paths.Contains(FSoftObjectPath(Wave)));
    // The Cue's authored Wave is a soft reference. As in FinishLoadWave, expand
    // that discovered resource on the next pass before checking its own routing.
    FMCPreloadManifest::CollectSoftReferences(Wave,Paths);
    TestTrue(TEXT("Mounted source submix dependency is also reachable"),Paths.Contains(FSoftObjectPath(Submix)));
    FMCEventSFXSamplePlan Plan;FString Error;
    TestTrue(TEXT("Mounted runtime type and static resource closure are supported"),FMCPreloadManifest::BuildEventAudioPlan(Cue,Plan,Error));
    P.Package->SetPackageFlags(PKG_EditorOnly);
    TestFalse(TEXT("Editor-only audio packages are explicitly rejected before saved use"),FMCPreloadManifest::BuildEventAudioPlan(Cue,Plan,Error));
    P.Package->ClearPackageFlags(PKG_EditorOnly);
    FPackageName::UnRegisterMountPoint(Mount);
    TestFalse(TEXT("An unmounted runtime package cannot acquire a new authored audio plan"),FMCPreloadManifest::BuildEventAudioPlan(Cue,Plan,Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventAudioColdCueCache,"MessControl.Audio.Manifest.ColdCueRequiresExplicitNativeCachePreparation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventAudioColdCueCache::RunTest(const FString&)
{
    using namespace MCEventAudioManifestTests;FPackage P;auto* Wave=P.Asset<USoundWave>(TEXT("Wave"));
    auto* Cue=P.Asset<USoundCue>(TEXT("Cue"));auto* Player=NewObject<USoundNodeWavePlayer>(Cue);
    Player->SetSoundWave(Wave);Cue->FirstNode=Player;Player->ClearAssetReferences();
    TestNull(TEXT("The actual native WavePlayer cache is cold"),Player->GetSoundWave());
    FMCEventSFXSamplePlan Plan;FString Error;
    TestFalse(TEXT("Resident authored soft target alone cannot be declared graph-ready"),FMCPreloadManifest::BuildEventAudioPlan(Cue,Plan,Error));
    TestNull(TEXT("Readonly validation does not populate that cache"),Player->GetSoundWave());
    bool Waiting=false;
    if(!TestTrue(TEXT("Explicit author/prebarrier preparation binds the real native cache"),FMCPreloadManifest::PrepareEventAudioPlan(Cue,Plan,Error,Waiting,true)))
    {AddError(Error);return false;}
    TestTrue(TEXT("Native cache is exact and no async asset request is outstanding"),Player->GetSoundWave()==Wave && !Player->IsCurrentlyAsyncLoadingAsset() && !Waiting);
    TestTrue(TEXT("Readonly readiness succeeds only after actual cache preparation"),FMCPreloadManifest::BuildEventAudioPlan(Cue,Plan,Error));
    return true;
}
#endif
