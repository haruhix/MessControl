#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCVFXPresentationComponent.h"
#include "MCEventSFXPlayer.h"
#include "MCEventSFXReadiness.h"
#include "MCGameState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NiagaraSystem.h"
#include "Sound/SoundWave.h"

namespace MCEventAudioIntegrationTests
{
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::EditorPreview,false);
    AActor* Owner=nullptr;UMCVFXPresentationComponent* Component=nullptr;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
        World->bAllowAudioPlayback=false; // These tests prove real silent lifecycle, never audibility.
        Owner=World->SpawnActor<AActor>();Component=NewObject<UMCVFXPresentationComponent>(Owner);
        Component->SourceID=TEXT("EditorOnly.NutEnemyDeath");Component->Events={TEXT("Death")};
        Owner->AddInstanceComponent(Component);Component->RegisterComponent();
    }
    ~FWorld(){GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    int32 AudioCount() const
    {int32 N=0;for(TActorIterator<AMCEventSFXPlayer> It(World);It;++It)if(It->IsRunning())++N;return N;}
    int32 VisualCount() const
    {int32 N=0;for(TActorIterator<AMCPerkVFXCompositionPlayer> It(World);It;++It)if(It->IsRunning())++N;return N;}
};
FMCEventSFXSlot AudioSlot()
{
    auto* Wave=NewObject<USoundWave>();Wave->Duration=5.f;
    FMCEventSFXLayer L;L.Sound=Wave;L.Name=TEXT("SourceWave");L.DelaySeconds=2.f;
    FMCEventSFXSlot S;S.Mode=EMCEventSFXMode::Add;S.Layers.Add(L);S.MaxDurationSeconds=10.f;return S;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXAudioIndependentChannels,
    "MessControl.Audio.Presentation.DefaultAndDisabledVFXKeepIndependentAudio",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXAudioIndependentChannels::RunTest(const FString&)
{
    using namespace MCEventAudioIntegrationTests;FWorld T;FString Error;
    auto* Profile=NewObject<UMCPerkVFXProfile>();Profile->SFXSlots.Add(TEXT("Death"),AudioSlot());
    if(!TestTrue(TEXT("An audio-only Add source prepares with original default visuals"),T.Component->PrepareProfileOverride(Profile,Error)))
    {AddError(Error);return false;}
    int32 NativeCallbacks=0;T.Component->OnEventAccepted.AddLambda([&](FName,EMCVFXPresentationResult){++NativeCallbacks;});
    FMCPerkVFXGameplayValues Values;Values.EpisodeID=FGuid::NewGuid();
    const auto First=T.Component->PlayEvent(TEXT("Death"),FTransform(FVector(20,30,40)),Values);
    TestEqual(TEXT("Custom audio does not change the caller's UseDefault VFX decision"),First,EMCVFXPresentationResult::UseDefault);
    TestTrue(TEXT("One existing receipt carries audio without replaying the native default visuals"),
        T.Component->LastReceipt.bPlaySFX&&!T.Component->LastReceipt.bPlayVFX&&T.Component->LastReceipt.Serial==1);
    TestEqual(TEXT("One actual audio player is scheduled"),T.AudioCount(),1);
    TestEqual(TEXT("No replacement visual player was fabricated"),T.VisualCount(),0);
    TestTrue(TEXT("Audio-only native episodes are discoverable for proper End cleanup"),T.Component->IsSourceEpisodeRunning(T.Component->SourceID,Values.EpisodeID));
    T.Component->MulticastReceipt_Implementation(T.Component->LastReceipt);T.Component->OnRep_Receipt();
    TestEqual(TEXT("Reliable echo and late snapshot cannot double the audio onset"),T.AudioCount(),1);
    TestEqual(TEXT("Replay is not another accepted native event"),NativeCallbacks,1);
    FMCPerkVFXSlot Disabled;Disabled.Mode=EMCPerkVFXSlotMode::Disabled;Disabled.Transform.SetTranslation(FVector(900,0,0));
    Profile->Slots.Add(TEXT("Death"),Disabled);
    if(!T.Component->PrepareProfileOverride(Profile,Error)){AddError(Error);return false;}
    Values.EpisodeID=FGuid::NewGuid();const FTransform ActualFrame(FVector(70,80,90));
    TestEqual(TEXT("Audio does not turn Disabled visuals into a custom visual or fallback"),
        T.Component->PlayEvent(TEXT("Death"),ActualFrame,Values),EMCVFXPresentationResult::Suppressed);
    if(!TestTrue(TEXT("Second actual audio episode is tracked"),T.Component->Players.Contains(2)))return false;
    auto* Audio=T.Component->Players[2].AudioPlayer.Get();
    TestTrue(TEXT("Audio follows the actual event frame, not the VFX slot offset"),Audio&&Audio->GetActorLocation().Equals(ActualFrame.GetLocation()));
    TArray<UObject*> Resources;T.Component->GetPreparedAudioResources(Resources);
    TestTrue(TEXT("The original SoundBase is strongly retained before playback"),Resources.Contains(Profile->SFXSlots[TEXT("Death")].Layers[0].Sound.Get()));
    auto* Barrier=NewObject<UMCEventSFXReadiness>();TArray<FMCEventSFXSamplePlan> Plans;
    Barrier->Begin(T.World,Plans,Error);
    TestFalse(TEXT("No-device preparation cannot manufacture an audible readiness certificate"),T.Component->CertifyPreparedAudio(Barrier,Error));
    Profile->SFXSlots[TEXT("Death")].Mode=EMCEventSFXMode::Replace;
    TestFalse(TEXT("Unmapped original SFX suppression is refused before applying data"),T.Component->PrepareProfileOverride(Profile,Error));
    T.Component->StopPresentation(true);
    TestEqual(TEXT("The same component cancellation stops both audio-only episodes"),T.AudioCount(),0);
    TestEqual(TEXT("No cancellation manufactures a native event"),NativeCallbacks,2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXAudioOrderedCancellation,
    "MessControl.Audio.Presentation.OrderedEpochAudioAndScopedCancellation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXAudioOrderedCancellation::RunTest(const FString&)
{
    using namespace MCEventAudioIntegrationTests;FWorld T;FString Error;
    auto* State=T.World->SpawnActor<AMCGameState>();T.World->SetGameState(State);State->GameplayLoadId=FGuid::NewGuid();
    const FGuid Future=FGuid::NewGuid(),EpisodeA=FGuid::NewGuid(),EpisodeB=FGuid::NewGuid();
    FMCVFXPresentationReceipt R;R.SourceID=T.Component->SourceID;R.Event=TEXT("Death");R.bPlayVFX=false;R.bPlaySFX=true;
    R.SFXSlot=AudioSlot();R.GameplayEpoch=Future;R.EpisodeID=EpisodeA;R.Serial=1;
    if(!T.Component->ReceiveReceipt(R,nullptr,Error,true)){AddError(Error);return false;}
    R.Serial=2;R.EpisodeID=EpisodeB;R.SFXSlot.Layers[0].Volume=.25f;
    if(!T.Component->ReceiveReceipt(R,nullptr,Error,true)){AddError(Error);return false;}
    R.SFXSlot.Layers[0].Volume=.9f;
    TestEqual(TEXT("Both real audio receipts wait for their matching run epoch"),T.Component->GetPendingReceiptCount(),2);
    TestEqual(TEXT("Queued audio settings are frozen independently of later author edits"),T.Component->PendingReceipts[1].PreparedAudio.Slot.Layers[0].Volume,.25f);
    FMCVFXPresentationCancel Cancel;Cancel.SourceID=R.SourceID;Cancel.EpisodeID=EpisodeA;Cancel.ThroughSerial=1;Cancel.bImmediate=true;
    T.Component->ReceiveCancel(Cancel);
    TestEqual(TEXT("Ending one source episode retains a later independent audio episode"),T.Component->GetPendingReceiptCount(),1);
    State->GameplayLoadId=Future;T.Component->ProcessPendingReceipts();
    TestEqual(TEXT("Matching epoch consumes the remaining real scheduled audio receipt"),T.Component->GetPendingReceiptCount(),0);
    if(!TestTrue(TEXT("Later audio playback is owned by its original receipt serial"),T.Component->Players.Contains(2)))return false;
    auto* Later=T.Component->Players[2].AudioPlayer.Get();
    if(!TestNotNull(TEXT("Real audio lifecycle exists after epoch match"),Later))return false;
    T.Component->ReceiveCancel(Cancel);TestTrue(TEXT("Old cancellation cannot stop a later audio episode"),Later->IsRunning());
    T.Owner->Destroy();TestTrue(TEXT("Ordinary native source destruction does not truncate its world-space audio tail"),Later->IsRunning());
    UMCVFXPresentationComponent::StopWorldPresentations(T.World);
    TestFalse(TEXT("Explicit world reset stops owned audio even when its original component is gone"),Later->IsRunning());
    TestEqual(TEXT("Reset permanently cancels its delayed starts"),Later->GetPendingLayerCount(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXSharedGroupsPreparation,
    "MessControl.Audio.Presentation.SharedGroupsCompileWithoutMutatingSources",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXSharedGroupsPreparation::RunTest(const FString&)
{
    using namespace MCEventAudioIntegrationTests;FWorld T;FString Error;
    auto* Profile=NewObject<UMCPerkVFXProfile>();auto* Recipe=NewObject<UMCVFXComposition>();
    FMCEventPresentationGroup Off;Off.Name=TEXT("Disabled group");Off.bEnabled=false;
    FMCEventPresentationGroup Muted;Muted.Name=TEXT("Muted audio group");Muted.bMuteSFX=true;
    Profile->EventGroups.FindOrAdd(TEXT("Death")).Groups={Off,Muted};
    FMCVFXCompositionLayer A;A.GroupID=Off.ID;A.System=NewObject<UNiagaraSystem>();
    FMCVFXCompositionLayer B;B.GroupID=Muted.ID;B.System=NewObject<UNiagaraSystem>();Recipe->Layers={A,B};
    FMCPerkVFXSlot Visual;Visual.Mode=EMCPerkVFXSlotMode::Composition;Visual.Composition=Recipe;Profile->Slots.Add(TEXT("Death"),Visual);
    auto Audio=AudioSlot();Audio.Layers[0].GroupID=Off.ID;auto L=Audio.Layers[0];L.ID=FGuid::NewGuid();L.GroupID=Muted.ID;Audio.Layers.Add(L);
    Profile->SFXSlots.Add(TEXT("Death"),Audio);
    if(!TestTrue(TEXT("One actual shared group set prepares both channels"),T.Component->PrepareProfileOverride(Profile,Error)))
    {AddError(Error);return false;}
    const auto& Source=T.Component->PreparedSources.FindChecked(T.Component->SourceID);
    const auto& V=Source.Events.FindChecked(TEXT("Death"));const auto& S=Source.AudioEvents.FindChecked(TEXT("Death"));
    TestTrue(TEXT("Disabled group is flattened into the private VFX snapshot only"),!V.Snapshot->Layers[0].bEnabled&&V.Snapshot->Layers[1].bEnabled&&Recipe->Layers[0].bEnabled);
    TestTrue(TEXT("Disabled authored Niagara still has a retained original resource"),V.Systems[0]==A.System.Get());
    TestTrue(TEXT("Audio has the same disabled GUID and independent audio-mute GUID"),S.DisabledGroupIDs.Contains(Off.ID)&&S.MutedGroupIDs.Contains(Muted.ID));
    TestTrue(TEXT("Disabled/muted sounds retain original resources and authored child state"),S.Sounds.Num()==2&&S.Sounds[0]&&S.Sounds[1]&&S.Slot.Layers[0].bEnabled);
    auto* Snapshot=V.Snapshot.Get();Recipe->Layers[1].GroupID=FGuid::NewGuid();
    TestFalse(TEXT("An actual runtime composition cannot refer to an unknown event group"),T.Component->PrepareProfileOverride(Profile,Error));
    TestTrue(TEXT("Failed preparation preserves the prior combined immutable snapshot"),T.Component->PreparedSources.FindChecked(T.Component->SourceID).Events.FindChecked(TEXT("Death")).Snapshot==Snapshot);
    return true;
}
#endif
