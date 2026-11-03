#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCVFXPresentationComponent.h"
#include "MCEventSFXPlayer.h"
#include "MCGameState.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Sound/SoundWave.h"

namespace MCVFXStateAudioLaneTests
{
bool HasPrerequisite(AMCEventSFXPlayer* Player,UObject* Object)
{return Player&&Player->PrimaryActorTick.GetPrerequisites().ContainsByPredicate([Object](const auto& P){return P.PrerequisiteObject.Get()==Object;});}
AMCEventSFXPlayer* FindAudio(UWorld* World,FName Event)
{for(TActorIterator<AMCEventSFXPlayer> It(World);It;++It)if(It->IsRunning()&&It->GetContext().Event==Event)return *It;return nullptr;}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXPersistentAudioLaneOwnership,
    "MessControl.Audio.Presentation.PersistentStateAudioLaneOwnership",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXPersistentAudioLaneOwnership::RunTest(const FString&)
{
    using namespace MCVFXStateAudioLaneTests;
    for(const auto& Context:GEngine->GetWorldContexts())if(Context.WorldType==EWorldType::PIE) {
        AddError(TEXT("Run outside PIE; only a disposable silent scheduling world is used."));return false;
    }
    UWorld* World=UWorld::CreateWorld(EWorldType::EditorPreview,false);
    GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);World->bAllowAudioPlayback=false;
    struct FOwnedCleanup {UWorld* W;~FOwnedCleanup(){W->DestroyWorld(false);GEngine->DestroyWorldContext(W);}} Cleanup{World};
    auto* Owner=World->SpawnActor<AActor>();auto* Component=NewObject<UMCVFXPresentationComponent>(Owner);
    Owner->AddInstanceComponent(Component);Component->RegisterComponent();
    const FName ID(TEXT("EditorOnly.PersistentLane")),Event(TEXT("State")),OtherEvent(TEXT("State.Other"));
    auto* Profile=NewObject<UMCPerkVFXProfile>();
    for(const FName Entry:{Event,OtherEvent}) {
        FMCEventSFXSlot Slot;Slot.Mode=EMCEventSFXMode::Add;Slot.MaxDurationSeconds=5.f;
        auto* Wave=NewObject<USoundWave>();Wave->Duration=1.f;
        FMCEventSFXLayer Layer;Layer.Sound=Wave;Layer.DelaySeconds=2.f;Slot.Layers.Add(Layer);Profile->SFXSlots.Add(Entry,Slot);
    }
    FString Error;if(!Component->PrepareSourceProfile(ID,{Event,OtherEvent},Profile,Error,false)){AddError(Error);return false;}
    auto* FirstFrame=NewObject<USceneComponent>(Owner);FirstFrame->PrimaryComponentTick.bCanEverTick=true;
    Owner->AddInstanceComponent(FirstFrame);FirstFrame->RegisterComponent();
    auto* NextFrame=NewObject<USceneComponent>(Owner);NextFrame->PrimaryComponentTick.bCanEverTick=true;
    Owner->AddInstanceComponent(NextFrame);NextFrame->RegisterComponent();
    FMCPerkVFXGameplayValues Values;Values.EpisodeID=FGuid::NewGuid();Values.LocalStateAgeSeconds=.25f;
    TestTrue(TEXT("A visual-only owner accepts prepared state without scheduling its audio"),
        Component->UpdateLocalSourceState(ID,Event,true,FTransform::Identity,Values,FirstFrame,Error,false));
    TestNull(TEXT("Suppressed state lane has no pending voice"),FindAudio(World,Event));
    TestEqual(TEXT("Its native episode clock is retained without a fake player"),Component->GetLocalSourceStateCount(),1);
    TestTrue(TEXT("Default trailing argument preserves ordinary audio ownership"),
        Component->UpdateLocalSourceState(ID,Event,true,FTransform::Identity,Values,FirstFrame,Error));
    auto* First=FindAudio(World,Event);
    if(!TestNotNull(TEXT("Enabling the lane creates exactly its own delayed player"),First))return false;
    TestTrue(TEXT("Lane activation uses accepted native age"),FMath::IsNearlyEqual(First->GetPlaybackTime(),.25f));
    TestTrue(TEXT("Initial native tick prerequisite is retained"),HasPrerequisite(First,FirstFrame));
    Component->UpdateLocalSourceState(ID,Event,true,FTransform::Identity,Values,NextFrame,Error);
    TestEqual(TEXT("Repeated state updates do not replay a one-shot"),FindAudio(World,Event),First);
    TestFalse(TEXT("Valid frame-source replacement removes the old prerequisite"),HasPrerequisite(First,FirstFrame));
    TestTrue(TEXT("Valid frame-source replacement binds the actual new prerequisite"),HasPrerequisite(First,NextFrame));
    Component->UpdateLocalSourceState(ID,OtherEvent,true,FTransform::Identity,Values,nullptr,Error);
    auto* Other=FindAudio(World,OtherEvent);TestNotNull(TEXT("Another typed event key has its own audio"),Other);
    Component->UpdateLocalSourceState(ID,Event,true,FTransform::Identity,Values,NextFrame,Error,false);
    TestFalse(TEXT("Disabling a live lane immediately cancels its pending audio"),First->IsRunning());
    TestNull(TEXT("Disabled lane retains no audio handle/voice"),FindAudio(World,Event));
    TestEqual(TEXT("Another event with the same episode is not stopped"),FindAudio(World,OtherEvent),Other);
    Values.LocalStateAgeSeconds=1.25f;
    Component->UpdateLocalSourceState(ID,Event,true,FTransform::Identity,Values,NextFrame,Error,true);
    auto* Rebound=FindAudio(World,Event);
    if(!TestNotNull(TEXT("Explicitly reacquiring audio uses a new player"),Rebound))return false;
    TestTrue(TEXT("Reacquisition preserves current native age, not an onset restart"),FMath::IsNearlyEqual(Rebound->GetPlaybackTime(),1.25f));
    const FGuid PreviousEpisode=Values.EpisodeID;Values.EpisodeID=FGuid::NewGuid();Values.LocalStateAgeSeconds=0.f;
    Component->UpdateLocalSourceState(ID,Event,true,FTransform::Identity,Values,NextFrame,Error,false);
    TestFalse(TEXT("Changing native episode retires the old generation's audio"),Rebound->IsRunning());
    TestEqual(TEXT("Other typed event keeps its original generation"),Other->GetContext().EpisodeID,PreviousEpisode);
    Component->UpdateLocalSourceState(ID,Event,false,FTransform::Identity,Values,nullptr,Error,false);
    TestEqual(TEXT("Explicit end removes only its named state"),Component->GetLocalSourceStateCount(),1);
    auto* State=World->SpawnActor<AMCGameState>();World->SetGameState(State);State->GameplayLoadId=FGuid::NewGuid();
    Component->UpdateLocalSourceState(ID,OtherEvent,true,FTransform::Identity,Values,nullptr,Error);
    TestFalse(TEXT("Run epoch transition stops previous-epoch audio"),Other->IsRunning());
    auto* Current=FindAudio(World,OtherEvent);
    TestTrue(TEXT("Replacement audio is bound to the actual new epoch"),Current&&Current->GetContext().GameplayEpoch==State->GameplayLoadId);
    Component->StopLocalSourceStates();TestNull(TEXT("Reset cancels all remaining state-owned audio"),FindAudio(World,OtherEvent));
    return !HasAnyErrors();
}
#endif
