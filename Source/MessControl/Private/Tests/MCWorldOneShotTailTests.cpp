#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCWorldPresentationComponent.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXComposition.h"
#include "MCEventSFXPlayer.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "MCVFXTestWorld.h"
#include "EngineUtils.h"
#include "NiagaraSystem.h"
#include "Sound/SoundWave.h"

namespace MCWorldOneShotTailTests
{
struct FWorld
{
    UWorld* World=MCVFXTestWorld::Create(EWorldType::Game,false);
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->bAllowAudioPlayback=false;World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
    }
    ~FWorld(){World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCWorldAcceptedOneShotTailOwnership,
    "MessControl.VFX.Presentation.WorldAdapterAcceptedOneShotTailOwnership",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCWorldAcceptedOneShotTailOwnership::RunTest(const FString&)
{
    using namespace MCWorldOneShotTailTests;FWorld T;FString Error;
    const FName ID(TEXT("World.FrostNova")),Event(TEXT("Burst"));
    auto* Owner=T.World->SpawnActor<AActor>();
    auto* Wrapper=NewObject<UMCWorldPresentationComponent>(Owner);Wrapper->Sources={ID};
    Owner->AddInstanceComponent(Wrapper);Wrapper->RegisterComponent();
    if(!TestTrue(TEXT("Real Game owner and adapter actually began play before destruction"),Owner->HasActorBegunPlay()&&Wrapper->HasBegunPlay()))return false;
    if(!TestTrue(TEXT("Real shared World adapter prepares its declared existing source"),Wrapper->PrepareSources(Error,true))) {AddError(Error);return false;}
    auto* Channel=Wrapper->GetSource(ID);
    if(!TestNotNull(TEXT("Prepared channel belongs to the actual World adapter"),Channel))return false;
    auto* Profile=NewObject<UMCPerkVFXProfile>();auto* Recipe=NewObject<UMCVFXComposition>();Recipe->DurationSeconds=10.f;
    // A bare UNiagaraSystem has no executable source. Prepare the real saved
    // gallery asset before exercising accepted-event ownership and delayed tails.
    auto* System=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Rocket_Start.NS_Rocket_Start"));
    if(!TestNotNull(TEXT("Actual saved ordinary source exists"),System))return false;
#if WITH_EDITOR
    System->WaitForCompilationComplete(false,false);
#endif
    if(!TestTrue(TEXT("Actual ordinary source compiled before profile preparation"),System->IsReadyToRun()))return false;
    FMCVFXCompositionLayer Visual;Visual.System=System;Visual.StartSeconds=5.f;Recipe->Layers.Add(Visual);
    FMCPerkVFXSlot Slot;Slot.Mode=EMCPerkVFXSlotMode::Composition;Slot.Composition=Recipe;Profile->Slots.Add(Event,Slot);
    FMCEventSFXSlot Audio;Audio.Mode=EMCEventSFXMode::Add;Audio.MaxDurationSeconds=10.f;
    auto* Sound=NewObject<USoundWave>();Sound->Duration=1.f;
    FMCEventSFXLayer Layer;Layer.Sound=Sound;Layer.DelaySeconds=2.f;Audio.Layers.Add(Layer);Profile->SFXSlots.Add(Event,Audio);
    if(!TestTrue(TEXT("Private VFX and SFX schedules prepare through ordinary existing API"),Channel->PrepareSourceProfile(ID,{TEXT("Wave"),Event},Profile,Error,false))) {AddError(Error);return false;}
    int32 Accepted=0;Channel->OnSourceEventAccepted.AddLambda([&](FName Source,FName Name,EMCVFXPresentationResult Result){
        if(Source==ID&&Name==Event&&Result==EMCVFXPresentationResult::Played)++Accepted;
    });
    FMCPerkVFXGameplayValues Values;Values.EpisodeID=FGuid::NewGuid();Values.LocalStateAgeSeconds=0.f;
    TestEqual(TEXT("Actual local accepted source event creates its custom one-shot"),Channel->PlayLocalSourceEvent(ID,Event,FTransform::Identity,Values),EMCVFXPresentationResult::Played);
    TestEqual(TEXT("Exactly one real source receipt is accepted"),Accepted,1);
    AMCPerkVFXCompositionPlayer* VisualPlayer=nullptr;AMCEventSFXPlayer* AudioPlayer=nullptr;int32 VisualCount=0,AudioCount=0;
    for(TActorIterator<AMCPerkVFXCompositionPlayer> It(T.World);It;++It)if(It->IsRunning()){VisualPlayer=*It;++VisualCount;}
    for(TActorIterator<AMCEventSFXPlayer> It(T.World);It;++It)if(It->IsRunning()&&It->GetContext().SourceID==ID&&It->GetContext().EpisodeID==Values.EpisodeID){AudioPlayer=*It;++AudioCount;}
    if(!TestTrue(TEXT("Actual accepted source owns one visual player and one silent audio schedule"),VisualCount==1&&AudioCount==1&&VisualPlayer&&AudioPlayer))return false;
    TestEqual(TEXT("Real delayed visual layer remains pending before any rendering"),VisualPlayer->GetPendingLayerCount(),1);
    TestEqual(TEXT("Real delayed sound remains pending; this test does not claim audibility"),AudioPlayer->GetPendingLayerCount(),1);
    Owner->Destroy();
    TestFalse(TEXT("Normal actor destruction actually routed adapter EndPlay"),Wrapper->HasBegunPlay());
    TestTrue(TEXT("Accepted world-owned visual survives normal source destruction"),VisualPlayer->IsRunning());
    TestTrue(TEXT("Accepted world-owned sound schedule survives normal source destruction"),AudioPlayer->IsRunning());
    UMCVFXPresentationComponent::StopWorldPresentations(T.World);
    TestFalse(TEXT("Explicit reset clears visual even after adapter/owner is gone"),VisualPlayer->IsRunning());
    TestFalse(TEXT("Explicit reset clears accepted audio even after adapter/owner is gone"),AudioPlayer->IsRunning());
    TestEqual(TEXT("Reset cannot leave a future visual onset behind"),VisualPlayer->GetPendingLayerCount(),0);
    TestEqual(TEXT("Reset cannot leave a future sound onset behind"),AudioPlayer->GetPendingLayerCount(),0);
    return !HasAnyErrors();
}
#endif
