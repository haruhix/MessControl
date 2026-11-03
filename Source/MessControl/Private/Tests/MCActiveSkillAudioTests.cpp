#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCActiveSkillTestTypes.h"
#include "MCEventSFXPlayer.h"
#include "MCGameState.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "MCVFXTestWorld.h"
#include "EngineUtils.h"
#include "NiagaraComponent.h"
#include "Sound/SoundWave.h"
#include "UObject/UnrealType.h"

namespace MCActiveSkillAudioTests
{
struct FWorld
{
    UWorld* W=MCVFXTestWorld::Create(EWorldType::Game,false);
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);W->bAllowAudioPlayback=false;
        W->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        W->SetGameMode(URL);W->InitializeActorsForPlay(URL);W->BeginPlay();
        // Actor::ProcessEvent suppresses reflected RPC wrappers in an
        // uninitialized EditorPreview world. Exercise the normal initialized
        // game callspace used by the actual isolated preview provider.
    }
    ~FWorld(){W->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(W);W->DestroyWorld(false);}
    int32 Voices() const {int32 N=0;for(TActorIterator<AMCEventSFXPlayer> It(W);It;++It)if(It->IsRunning())++N;return N;}
};
FMCEventSFXSlot Slot(float Delay=0,bool Repeat=false)
{
    auto* Wave=NewObject<USoundWave>();Wave->Duration=1.f;
    FMCEventSFXLayer L;L.Sound=Wave;L.Name=TEXT("NativeStageWave");L.DelaySeconds=Delay;L.bRepeatOnFinish=Repeat;
    FMCEventSFXSlot S;S.Mode=EMCEventSFXMode::Add;S.Layers.Add(L);S.MaxDurationSeconds=10.f;return S;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCActiveAudioEventClockTest,
    "MessControl.Audio.ActiveSkills.ActualStageClockAndVFXIndependence",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCActiveAudioEventClockTest::RunTest(const FString&)
{
    using namespace MCActiveSkillAudioTests;FWorld T;FString Error;
    if(!TestTrue(TEXT("Native fixture actors are initialized for the real RPC callspace"),T.W->AreActorsInitialized()))return false;
    auto* A=T.W->SpawnActor<AMCActiveSkillProtocolProbe>();
    auto* Profile=NewObject<UMCPerkVFXProfile>();Profile->SFXSlots.Add(TEXT("Start"),Slot());
    FMCPerkVFXSlot Disabled;Disabled.Mode=EMCPerkVFXSlotMode::Disabled;Profile->Slots.Add(TEXT("Start"),Disabled);
    if(!TestTrue(TEXT("The real standard profile entry prepares audio despite disabled VFX"),A->PrepareVFXProfile(Profile,Error)))
    {AddError(Error);return false;}
    A->PerkID=TEXT("ActiveAudioProbe");A->ActionID=FGuid::NewGuid();A->bCommitted=true;
    // The action has already existed for five seconds; this new actual stage
    // must start at its own accepted Now(), not at the action's StartedAt.
    A->StartedAt=A->Now()-5.;int32 NativeStages=0;
    A->OnVFXStage.AddLambda([&](FName){++NativeStages;});
    const FVector Point(40,50,60);A->PlaySkillCue(TEXT("Start"),Point,FVector::UpVector);
    TestEqual(TEXT("Actual generated stage RPC reached its native handler"),A->ReceivedCueSerial,uint32(1));
    if(!TestEqual(TEXT("One real SFX player starts through the existing stage RPC"),T.Voices(),1))return false;
    auto* Audio=A->AudioPlayers.IsEmpty()?nullptr:A->AudioPlayers[0].Get();
    if(!TestNotNull(TEXT("Actual stage owns a local audio schedule"),Audio))return false;
    TestTrue(TEXT("Stage audio age starts near zero instead of inheriting action age"),Audio->GetPlaybackTime()<.01f);
    TestEqual(TEXT("Original source begins at this actual onset in a silent world"),Audio->GetStartedLayerCount(),1);
    TestTrue(TEXT("No VFX offset is substituted for the accepted source frame"),Audio->GetActorLocation().Equals(Point));
    TestNull(TEXT("Audio did not re-enable the disabled native Niagara"),A->GetPreparedVFXSystem(TEXT("Start")));
    A->MulticastSkillCue_Implementation(TEXT("Start"),Point,FVector::UpVector,-1,A->Now(),1,FGuid(),false,false);
    TestEqual(TEXT("The same reliable serial cannot replay sound or its stage event"),T.Voices(),1);
    TestEqual(TEXT("Actual stage observer was invoked once"),NativeStages,1);
    A->Finish(EMCActiveSkillEndReason::InputCanceled);
    TestFalse(TEXT("Cancellation stops the action's owned audio immediately"),Audio->IsRunning());
    TestEqual(TEXT("Stop control does not fabricate another VFX stage"),NativeStages,1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCActiveAudioLoopSnapshotTest,
    "MessControl.Audio.ActiveSkills.LoopSnapshotStopAndTerminalTail",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCActiveAudioLoopSnapshotTest::RunTest(const FString&)
{
    using namespace MCActiveSkillAudioTests;FWorld T;FString Error;
    auto* A=T.W->SpawnActor<AMCActiveSkillProtocolProbe>();auto* Profile=NewObject<UMCPerkVFXProfile>();
    Profile->SFXSlots.Add(TEXT("Loop"),Slot(0,true));Profile->SFXSlots.Add(TEXT("Impact"),Slot(2.f));
    if(!A->PrepareVFXProfile(Profile,Error)){AddError(Error);return false;}
    A->PerkID=TEXT("ActiveLoopProbe");A->ActionID=FGuid::NewGuid();A->bCommitted=true;A->StartedAt=A->Now()-5.;
    A->StartLoop();auto* Loop=A->LoopAudioPlayer.Get();
    if(!TestNotNull(TEXT("An audio-only Loop starts on its real native boundary"),Loop))return false;
    TestTrue(TEXT("Loop has its own accepted start time rather than action start time"),A->LoopStartedAt==A->Now()&&A->LoopStartedAt!=A->StartedAt);
    const auto* Property=FindFProperty<FDoubleProperty>(AMCActiveSkillAction::StaticClass(),TEXT("LoopStartedAt"));
    TestTrue(TEXT("Late peers receive the exact loop timestamp in the replicated snapshot"),Property&&Property->HasAnyPropertyFlags(CPF_Net));
    A->OnRep_Loop();A->OnRep_Loop();
    TestTrue(TEXT("Repeated snapshots retain the same loop instance instead of replaying onset"),A->LoopAudioPlayer.Get()==Loop&&T.Voices()==1);
    A->StopLoop();Loop->Advance(.2f);
    TestFalse(TEXT("Native LoopStop prevents later repetition and retires its finite fade"),Loop->IsRunning());
    A->PlaySkillCue(TEXT("Impact"),FVector(100,0,0),FVector::UpVector);
    AMCEventSFXPlayer* Tail=nullptr;
    for(TActorIterator<AMCEventSFXPlayer> It(T.W);It;++It)if(It->IsRunning()&&It->GetContext().Event==TEXT("Impact"))Tail=*It;
    if(!TestNotNull(TEXT("The actual terminal event owns its delayed sound"),Tail))return false;
    TestEqual(TEXT("Terminal source has not been started ahead of its authored delay"),Tail->GetPendingLayerCount(),1);
    A->Finish(EMCActiveSkillEndReason::Completed);
    TestTrue(TEXT("Completed action destruction preserves its world-owned terminal schedule"),Tail->IsRunning());
    Tail->Advance(2.05f);TestEqual(TEXT("The delayed terminal layer starts after the action actor is gone"),Tail->GetStartedLayerCount(),1);
    AMCActiveSkillAction::StopWorldAudio(T.W);
    TestFalse(TEXT("Explicit run/preview reset stops even the already ownerless terminal audio"),Tail->IsRunning());
    return true;
}
#endif
