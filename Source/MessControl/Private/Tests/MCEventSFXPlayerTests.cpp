#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCEventSFXPlayer.h"
#include "MCEventSFXReadiness.h"
#include "MCEventSFXTestTypes.h"
#include "MCGameState.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundWaveProcedural.h"

namespace MCEventSFXPlayerTests
{
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::EditorPreview,false);
    FWorld(){GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);World->bAllowAudioPlayback=false;}
    ~FWorld(){if(World){GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}}
    FMCEventSFXContext Context() const
    {FMCEventSFXContext C;C.SourceID=TEXT("EditorOnly.ComposerAudio");C.Event=TEXT("ActualImpact");C.EpisodeID=FGuid::NewGuid();return C;}
    AMCEventSFXPlayer* Spawn(const FMCPreparedEventSFX& Data,FString& Error,float Elapsed=0.f,
        USceneComponent* Subject=nullptr,FName Tag=TEXT("MC.Test.OwnedSFX"))
    {return AMCEventSFXPlayer::SpawnPrepared(World,Data,FTransform::Identity,Subject,Context(),Tag,Error,Elapsed);}
};
FMCPreparedEventSFX Data(float Duration=1.f,float Delay=0.f,bool bRepeat=false)
{
    auto* Wave=NewObject<USoundWave>();Wave->Duration=Duration;
    FMCEventSFXLayer Layer;Layer.Sound=Wave;Layer.Name=TEXT("OriginalWave");Layer.DelaySeconds=Delay;Layer.bRepeatOnFinish=bRepeat;
    FMCPreparedEventSFX P;P.Slot.Mode=EMCEventSFXMode::Add;P.Slot.Layers.Add(Layer);P.Slot.MaxDurationSeconds=10.f;
    P.Sounds.Add(Wave);P.Attenuations.Add(nullptr);P.Concurrencies.Add(nullptr);return P;
}
void AddCopy(FMCPreparedEventSFX& P,FGuid Group,float Delay=0.f)
{
    auto L=P.Slot.Layers[0];L.ID=FGuid::NewGuid();L.GroupID=Group;L.DelaySeconds=Delay;
    const auto Sound=P.Sounds[0];
    P.Slot.Layers.Add(L);P.Sounds.Add(Sound);P.Attenuations.Add(nullptr);P.Concurrencies.Add(nullptr);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventSFXDelayCancel,"MessControl.Audio.Composer.DelayedCancellation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventSFXDelayCancel::RunTest(const FString&)
{
    using namespace MCEventSFXPlayerTests;FWorld W;FString Error;auto P=Data(.5f,.75f);
    auto* Player=W.Spawn(P,Error);if(!TestNotNull(TEXT("A real silent-world player is allocated"),Player)){AddError(Error);return false;}
    TestEqual(TEXT("Delayed sound has no allocated voice before its due time"),Player->GetVoiceComponents().Num(),0);
    TestEqual(TEXT("Its accepted schedule is pending"),Player->GetPendingLayerCount(),1);
    Player->Advance(.5f);TestEqual(TEXT("Delay is not rounded down"),Player->GetVoiceComponents().Num(),0);
    Player->Stop(true);Player->Advance(2.f);
    TestFalse(TEXT("Reset retires the player"),Player->IsRunning());
    TestEqual(TEXT("Reset cancels delayed starts permanently"),Player->GetPendingLayerCount(),0);
    TestEqual(TEXT("Advancing cannot resurrect a cancelled sound"),Player->GetVoiceComponents().Num(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventSFXLoopTail,"MessControl.Audio.Composer.RepeatAndGracefulTail",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventSFXLoopTail::RunTest(const FString&)
{
    using namespace MCEventSFXPlayerTests;FWorld W;FString Error;auto P=Data(.2f,0.f,true);P.Slot.TailSeconds=.5f;
    P.Slot.Layers[0].FadeOutSeconds=.1f;auto* Loop=W.Spawn(P,Error);
    if(!TestNotNull(TEXT("Repeat owns a real AudioComponent even with audio disabled"),Loop)){AddError(Error);return false;}
    if(!TestEqual(TEXT("Repeat has one tracked component"),Loop->GetVoiceComponents().Num(),1))return false;
    auto* Voice=Loop->GetVoiceComponents()[0];Loop->Advance(.25f);
    TestTrue(TEXT("Finite repetition reuses its existing component"),Loop->GetVoiceComponents().Num()==1 && Loop->GetVoiceComponents()[0]==Voice);
    Loop->Stop(false);Loop->Advance(.11f);Loop->Advance(1.f);
    TestFalse(TEXT("Graceful cancellation prevents every later repeat"),Loop->IsRunning());
    TestEqual(TEXT("Loop component is destroyed after its bounded fade"),Loop->GetVoiceComponents().Num(),0);
    P=Data(1.f);P.Slot.TailSeconds=.3f;auto* Tail=W.Spawn(P,Error);
    if(!TestNotNull(TEXT("One-shot player starts"),Tail)){AddError(Error);return false;}
    Tail->Stop(false);Tail->Advance(.29f);
    TestEqual(TEXT("An authored finite tail may finish naturally"),Tail->GetVoiceComponents().Num(),1);
    Tail->Advance(.02f);TestEqual(TEXT("Tail deadline destroys even a source which has not finished"),Tail->GetVoiceComponents().Num(),0);
    TestFalse(TEXT("Tail deadline retires the player"),Tail->IsRunning());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventSFXFrames,"MessControl.Audio.Composer.RealFrameAndSocketOwnership",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventSFXFrames::RunTest(const FString&)
{
    using namespace MCEventSFXPlayerTests;FWorld W;FString Error;auto P=Data(5.f);
    auto* Source=W.World->SpawnActor<AActor>();auto* MeshComponent=NewObject<UStaticMeshComponent>(Source);
    auto* Mesh=NewObject<UStaticMesh>();auto* Socket=NewObject<UStaticMeshSocket>(Mesh);
    Socket->SocketName=TEXT("AudioGrip");Socket->RelativeLocation=FVector(4,5,6);Mesh->AddSocket(Socket);
    Source->SetRootComponent(MeshComponent);Source->AddInstanceComponent(MeshComponent);
    MeshComponent->SetMobility(EComponentMobility::Movable);MeshComponent->SetStaticMesh(Mesh);
    MeshComponent->RegisterComponent();Source->SetActorLocation(FVector(100,0,0));
    const FGuid Group=FGuid::NewGuid();P.Slot.Layers[0].Offset.SetTranslation(FVector(10,0,0));
    AddCopy(P,Group);P.Slot.Layers[1].Space=EMCEventSFXSpace::FollowEventFrame;
    AddCopy(P,Group);P.Slot.Layers[2].Space=EMCEventSFXSpace::SourceSocket;P.Slot.Layers[2].Socket=Socket->SocketName;
    const FTransform First(FVector(20,30,40)),Next(FVector(60,70,80));
    auto* Player=AMCEventSFXPlayer::SpawnPrepared(W.World,P,First,MeshComponent,W.Context(),TEXT("MC.Test.FrameSFX"),Error);
    if(!TestNotNull(TEXT("The exact native socket can be resolved"),Player)){AddError(Error);return false;}
    const auto Voices=Player->GetVoiceComponents();if(!TestEqual(TEXT("Three placement modes are independent real voices"),Voices.Num(),3))return false;
    TestTrue(TEXT("World-space layer is placed at the accepted event"),Voices[0]->GetComponentLocation().Equals(FVector(30,30,40)));
    TestTrue(TEXT("Socket source is the actual native component"),Voices[2]->GetAttachParent()==MeshComponent && Voices[2]->GetAttachSocketName()==Socket->SocketName);
    Player->UpdateEventFrame(Next);Player->Advance(0.f);
    TestTrue(TEXT("World-space layer stays at its original accepted location"),Voices[0]->GetComponentLocation().Equals(FVector(30,30,40)));
    TestTrue(TEXT("Follow-frame layer follows the new native event frame"),Voices[1]->GetComponentLocation().Equals(FVector(70,70,80)));
    TestTrue(TEXT("Player is world-owned rather than owned by the short-lived source"),Player->GetOwner()==W.World->GetWorldSettings());
    Source->Destroy();Player->Advance(0.f);
    TestEqual(TEXT("Source destruction cancels only the source-attached voice"),Player->GetVoiceComponents().Num(),2);
    TestTrue(TEXT("World-space/follow event presentation remains owned until its actual cancel"),Player->IsRunning());
    Player->Stop(true);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventSFXGroups,"MessControl.Audio.Composer.SimultaneousGroupsMuteSoloCancel",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventSFXGroups::RunTest(const FString&)
{
    using namespace MCEventSFXPlayerTests;FWorld W;FString Error;auto P=Data(3.f);
    const FGuid A=FGuid::NewGuid(),B=FGuid::NewGuid();P.Slot.Layers[0].GroupID=A;AddCopy(P,B);AddCopy(P,A,.5f);
    auto* Player=W.Spawn(P,Error);if(!TestNotNull(TEXT("Multiple groups can start together"),Player)){AddError(Error);return false;}
    TestEqual(TEXT("Both groups are simultaneous; they are not exclusive A/B variants"),Player->GetVoiceComponents().Num(),2);
    TestEqual(TEXT("One independent accent remains delayed"),Player->GetPendingLayerCount(),1);
    FMCEventSFXPreviewFilter Filter;Filter.OutputGain=.25f;Filter.MutedGroupIDs={A};Player->SetPreviewFilter(Filter);
    auto Voices=Player->GetVoiceComponents();TestEqual(TEXT("Group A mute affects only its sound"),Voices[0]->VolumeMultiplier,0.f);
    TestEqual(TEXT("Group B retains the scoped preview gain"),Voices[1]->VolumeMultiplier,.25f);
    Filter.MutedGroupIDs.Reset();Filter.SoloGroupIDs={A};Player->SetPreviewFilter(Filter);
    TestEqual(TEXT("Solo keeps the chosen group's gain"),Voices[0]->VolumeMultiplier,.25f);
    TestEqual(TEXT("Solo filters other groups locally"),Voices[1]->VolumeMultiplier,0.f);
    Player->SetPreviewFilter(FMCEventSFXPreviewFilter());
    TestEqual(TEXT("Clearing preview filters restores the retained authored gain"),Voices[1]->VolumeMultiplier,1.f);
    Player->StopGroup(A,true);Player->Advance(.6f);
    TestEqual(TEXT("Group cancellation stops its voice and future accent only"),Player->GetVoiceComponents().Num(),1);
    TestEqual(TEXT("Cancelled group accent never starts"),Player->GetPendingLayerCount(),0);
    TestTrue(TEXT("Other simultaneous group keeps running"),Player->IsRunning());Player->Stop(true);
    P.DisabledGroupIDs={A};P.MutedGroupIDs={B};Player=W.Spawn(P,Error);
    if(!TestNotNull(TEXT("Shared persistent policies compile separately from local preview"),Player)){AddError(Error);return false;}
    Voices=Player->GetVoiceComponents();TestEqual(TEXT("Disabled group has no voice or delay"),Voices.Num(),1);
    TestEqual(TEXT("Persistent MuteSFX preserves source/context with zero per-instance gain"),Voices[0]->VolumeMultiplier,0.f);
    Player->Stop(true);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventSFXLateAndEpoch,"MessControl.Audio.Composer.LateReceiptAndEpochReset",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventSFXLateAndEpoch::RunTest(const FString&)
{
    using namespace MCEventSFXPlayerTests;FWorld W;FString Error;auto P=Data(.5f);auto* Expired=W.Spawn(P,Error,1.f);
    if(!TestNotNull(TEXT("Old accepted one-shot is consumed"),Expired)){AddError(Error);return false;}
    TestEqual(TEXT("An expired onset never replays as a fresh sound"),Expired->GetVoiceComponents().Num(),0);
    TestEqual(TEXT("Expired layer is explicitly counted"),Expired->GetSkippedLateLayerCount(),1);
    auto* Procedural=NewObject<USoundWaveProcedural>();Procedural->Duration=.5f;P.Sounds[0]=Procedural;P.Slot.Layers[0].Sound=Procedural;
    auto* Unseekable=W.Spawn(P,Error,.2f);if(!TestNotNull(TEXT("Procedural late source is safely consumed"),Unseekable)){AddError(Error);return false;}
    TestEqual(TEXT("Unsupported historical seek is not silently replayed"),Unseekable->GetSkippedLateLayerCount(),1);
    auto* State=W.World->SpawnActor<AMCGameState>();W.World->SetGameState(State);State->GameplayLoadId=FGuid::NewGuid();
    auto C=W.Context();C.GameplayEpoch=State->GameplayLoadId;P=Data(1.f,.5f);
    auto* Player=AMCEventSFXPlayer::SpawnPrepared(W.World,P,FTransform::Identity,nullptr,C,TEXT("MC.Test.EpochSFX"),Error);
    if(!TestNotNull(TEXT("The player binds to the accepted run epoch"),Player)){AddError(Error);return false;}
    State->GameplayLoadId=FGuid::NewGuid();Player->Advance(0.f);
    TestFalse(TEXT("Run replacement stops pending and active audio"),Player->IsRunning());
    TestEqual(TEXT("A retired epoch cannot later start its delayed sound"),Player->GetPendingLayerCount(),0);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventSFXResetProvenance,"MessControl.Audio.Composer.WorldResetProvenance",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventSFXResetProvenance::RunTest(const FString&)
{
    using namespace MCEventSFXPlayerTests;FWorld W;FString Error;auto P=Data(1.f,.5f);
    auto* Owned=W.Spawn(P,Error,0.f,nullptr,TEXT("MC.Generic.SFX"));auto* Other=W.Spawn(P,Error,0.f,nullptr,TEXT("MC.Active.SFX"));
    if(!TestNotNull(TEXT("Generic player allocated"),Owned) || !TestNotNull(TEXT("Independent active player allocated"),Other)){AddError(Error);return false;}
    AMCEventSFXPlayer::StopWorldPlayers(W.World,TEXT("MC.Generic.SFX"));
    TestFalse(TEXT("Reset stops only the tagged factory's owned players"),Owned->IsRunning());
    TestTrue(TEXT("A separate active owner remains intact"),Other->IsRunning());
    TestEqual(TEXT("The separate owner's pending schedule is preserved"),Other->GetPendingLayerCount(),1);
    Other->Stop(true);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventSFXOrdinaryUnseekable,"MessControl.Audio.Composer.OrdinaryUnseekableLateOnset",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventSFXOrdinaryUnseekable::RunTest(const FString&)
{
    using namespace MCEventSFXPlayerTests;FWorld W;FString Error;auto P=Data(1.f);
    auto* NonSeekable=NewObject<UMCNonSeekableSoundWaveProbe>();NonSeekable->Duration=1.f;
    P.Sounds[0]=NonSeekable;P.Slot.Layers[0].Sound=NonSeekable;
    const auto* Wave=Cast<USoundWave>(P.Sounds[0].Get());
    if(!TestNotNull(TEXT("The fixture is an ordinary Wave"),Wave))return false;
    TestFalse(TEXT("The regression source is not procedural"),Wave->IsProcedurallyGenerated());
    if(!TestFalse(TEXT("The ordinary source explicitly reports an unseekable decoder"),Wave->IsSeekable()))return false;
    auto* Player=W.Spawn(P,Error,.2f);
    if(!TestNotNull(TEXT("The late accepted ordinary source is safely consumed"),Player)){AddError(Error);return false;}
    TestEqual(TEXT("A nonprocedural but unseekable onset is explicitly skipped"),Player->GetSkippedLateLayerCount(),1);
    TestEqual(TEXT("No voice replays its onset under an aged completion deadline"),Player->GetVoiceComponents().Num(),0);
    TestFalse(TEXT("No stale onset remains pending or running"),Player->IsRunning());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventSFXOutputTransition,"MessControl.Audio.Composer.UncertifiedOutputTransitionStaysSilent",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventSFXOutputTransition::RunTest(const FString&)
{
    using namespace MCEventSFXPlayerTests;FWorld W;FString Error;auto P=Data(.2f,0.f,true);
    auto* Player=W.Spawn(P,Error);
    if(!TestNotNull(TEXT("A no-output repeat is available for silent lifecycle inspection"),Player)){AddError(Error);return false;}
    if(!TestEqual(TEXT("The source owns its real component"),Player->GetVoiceComponents().Num(),1))return false;
    TestFalse(TEXT("The no-output source snapshot has no playback-resource certificate"),Player->Data.bPlaybackResourcesPrepared);
    TestEqual(TEXT("Silent start made no native playback request"),Player->PlaybackRequests,0);
    Player->Advance(.25f);
    // Exercise the production start/repeat dispatch with output now available.
    // Its uncertified guard must avoid FadeIn; no real device, preferences or world policy is enabled here.
    Player->PlayVoiceWithOutputState(0,0.f,true);
    TestTrue(TEXT("The existing uncertified repeat remains on its silent clock"),Player->Voices[0].bSilentClock);
    TestEqual(TEXT("Output recovery made zero playback requests"),Player->PlaybackRequests,0);
    TestTrue(TEXT("The diagnostic requires a rebuilt certified snapshot"),Player->GetOutputStatus().Contains(TEXT("Rebuild and certify")));
    Player->Advance(.25f);Player->PlayVoiceWithOutputState(0,0.f,true);
    TestEqual(TEXT("Every later repeat still refuses promotion to audible playback"),Player->PlaybackRequests,0);
    Player->Stop(true);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventSFXUnavailableReadiness,"MessControl.Audio.Composer.UnavailableOutputNeverCertifiesPlaybackResources",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventSFXUnavailableReadiness::RunTest(const FString&)
{
    using namespace MCEventSFXPlayerTests;FWorld W;FString Error;auto P=Data(1.f);
    auto* Barrier=NewObject<UMCEventSFXReadiness>();FMCEventSFXSamplePlan Plan;Plan.Source=P.Sounds[0];
    TestTrue(TEXT("Silent functional mode is explicit and does not block visual inspection"),Barrier->Begin(W.World,{Plan},Error));
    TestTrue(TEXT("Its state is output unavailable"),Barrier->GetState()==EMCEventSFXReadiness::OutputUnavailable);
    TestFalse(TEXT("No-device inspection is never a playback-resource readiness result"),Barrier->IsReady());
    TestFalse(TEXT("An unavailable device cannot issue a playback-resource certificate"),Barrier->CertifyPrepared(P,Error));
    TestFalse(TEXT("The prepared snapshot stays uncertified"),P.bPlaybackResourcesPrepared);
    Barrier->Reset();return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventSFXFrameSource,"MessControl.Audio.Composer.NativeFrameAnchorBeforeAdvance",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventSFXFrameSource::RunTest(const FString&)
{
    using namespace MCEventSFXPlayerTests;FWorld W,OtherWorld;FString Error;auto P=Data(3.f,.1f);
    P.Slot.Layers[0].Space=EMCEventSFXSpace::FollowEventFrame;
    auto* Native=W.World->SpawnActor<AActor>();auto* Anchor=NewObject<USceneComponent>(Native);
    Native->SetRootComponent(Anchor);Native->AddInstanceComponent(Anchor);Anchor->RegisterComponent();
    auto* Foreign=OtherWorld.World->SpawnActor<AActor>();auto* ForeignAnchor=NewObject<USceneComponent>(Foreign);
    Foreign->SetRootComponent(ForeignAnchor);Foreign->AddInstanceComponent(ForeignAnchor);ForeignAnchor->RegisterComponent();
    auto* Player=W.Spawn(P,Error);if(!TestNotNull(TEXT("Delayed followed audio has a real owned player"),Player)){AddError(Error);return false;}
    TestTrue(TEXT("Actual same-world native frame source can be bound"),Player->BindEventFrameSource(Anchor));
    TestFalse(TEXT("A different world cannot replace the accepted native anchor"),Player->BindEventFrameSource(ForeignAnchor));
    Anchor->SetWorldLocation(FVector(120,30,80));Player->Tick(.1f);
    const auto Voices=Player->GetVoiceComponents();if(!TestEqual(TEXT("The delayed layer starts after reading its actual native frame"),Voices.Num(),1))return false;
    TestTrue(TEXT("Its first position is the current native frame, not a stale spawn frame"),Voices[0]->GetComponentLocation().Equals(FVector(120,30,80)));
    Player->BindEventFrameSource(nullptr);Player->UpdateEventFrame(FTransform(FVector(20,40,60)));
    Anchor->SetWorldLocation(FVector(300,300,300));Player->Tick(0.f);
    TestTrue(TEXT("Null binding restores the generic explicit frame update path"),Voices[0]->GetComponentLocation().Equals(FVector(20,40,60)));
    Player->BindEventFrameSource(Anchor);Player->Stop(false);Anchor->SetWorldLocation(FVector(900,900,900));Player->Tick(0.f);
    TestTrue(TEXT("Graceful stop clears native follow and retains the tail's last frame"),Voices[0]->GetComponentLocation().Equals(FVector(20,40,60)));
    Player->Stop(true);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventSFXResourceStateInvalidation,"MessControl.Audio.Readiness.SameWaveObjectResourceResetInvalidatesReady",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventSFXResourceStateInvalidation::RunTest(const FString&)
{
    using namespace MCEventSFXPlayerTests;FWorld W;
    auto* Source=LoadObject<USoundWave>(nullptr,TEXT("/Game/Audio/Rewards/S_TaskCoins.S_TaskCoins"));
    if(!TestNotNull(TEXT("A valid saved sound source exists for resource-proxy validation"),Source))return false;
    const auto SourcePrecache=Source->GetPrecacheState();const auto SourceDecompression=Source->DecompressionType;
    auto* Wave=DuplicateObject<USoundWave>(Source,GetTransientPackage());
    if(!TestNotNull(TEXT("Resource mutation uses an isolated transient source copy"),Wave))return false;
    Wave->ClearFlags(RF_Public|RF_Standalone);Wave->SetFlags(RF_Transient);
    FMCPreparedEventSFX P;P.Slot.Mode=EMCEventSFXMode::Add;
    FMCEventSFXLayer Layer;Layer.Sound=Wave;P.Slot.Layers.Add(Layer);
    P.Sounds.Add(Wave);P.Attenuations.Add(nullptr);P.Concurrencies.Add(nullptr);
    auto* Ready=NewObject<UMCEventSFXReadiness>();Ready->PreparedWorld=W.World;
    FMCEventSFXSamplePlan Plan;Plan.Source=Wave;Plan.SampleWaves.Add(Wave);Ready->RetainedPlans.Add(Plan);Ready->RetainedWaves.Add(Wave);
    // Only the duplicate's resource state is changed. Its real source payload lets
    // the engine build a valid proxy without malformed synthetic-audio errors.
    Wave->DecompressionType=DTYPE_RealTime;Wave->SetPrecacheState(ESoundWavePrecacheState::Done);
    UMCEventSFXReadiness::FWaveWork Work;Work.Wave=Wave;Work.bReady=true;Work.WaveData=Wave->GetSoundWaveProxy()->GetSoundWaveDataRef();
    Ready->Work.Add(MoveTemp(Work));Ready->State=EMCEventSFXReadiness::Ready;
    TestTrue(TEXT("The baseline completed resource state is current"),Ready->IsReady());FString Error;
    TestTrue(TEXT("A current baseline can certify its exact source"),Ready->CertifyPrepared(P,Error));
    const uint32 Identity=Wave->GetUniqueID();Wave->SetPrecacheState(ESoundWavePrecacheState::NotStarted);
    TestEqual(TEXT("In-place edit/reimport preserves the UObject identity"),Wave->GetUniqueID(),Identity);
    TestFalse(TEXT("That same object cannot reuse its stale Ready state after resource reset"),Ready->IsReady());
    TestFalse(TEXT("A stale resource state cannot certify another accepted event"),Ready->CertifyPrepared(P,Error));
    TestFalse(TEXT("A failed recertification clears the prepared flag"),P.bPlaybackResourcesPrepared);
    Ready->State=EMCEventSFXReadiness::Preparing;
    TestTrue(TEXT("A genuinely preparing operation remains Preparing and is not restarted by the pure query"),Ready->GetState()==EMCEventSFXReadiness::Preparing && !Ready->IsReady());
    Ready->Reset();
    TestTrue(TEXT("Resource-reset fixture preserves the saved source's live resource state"),
        Source->GetPrecacheState()==SourcePrecache && Source->DecompressionType==SourceDecompression);
    return true;
}
#endif
