#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCVFXPresentationComponent.h"
#include "MCWorldPresentationComponent.h"
#include "MCVFXSourceContracts.h"
#include "MCEventSFXPlayer.h"
#include "MCDataAssets.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCVFXPresentationLibrary.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Sound/SoundWave.h"
#include "GameFramework/GameModeBase.h"
#include "UObject/UnrealType.h"

namespace MCCharacterMovementAudio { FName LandingEvent(AMCToothCharacter* Hero); }

namespace MCCharacterMovementAudioTests
{
bool IsPIEActive()
{for(const auto& Context:GEngine->GetWorldContexts())if(Context.WorldType==EWorldType::PIE)return true;return false;}
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::EditorPreview,false);
    AActor* Owner=nullptr;
    UMCVFXPresentationComponent* Component=nullptr;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
        World->bAllowAudioPlayback=false; // Actual silent scheduling/lifecycle, never an audible certificate.
        Owner=World->SpawnActor<AActor>();Owner->SetRole(ROLE_SimulatedProxy);
        Component=NewObject<UMCVFXPresentationComponent>(Owner);
        Component->SourceID=TEXT("Character.Movement");Component->Events=MCVFXSourceContracts::Find(Component->SourceID)->Events;
        Owner->AddInstanceComponent(Component);Component->RegisterComponent();
    }
    ~FWorld()
    {
        Component->StopPresentation(true);
        World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    int32 AudioCount() const
    {int32 Count=0;for(TActorIterator<AMCEventSFXPlayer> It(World);It;++It)if(It->IsRunning())++Count;return Count;}
};
FMCEventSFXSlot Slot(EMCEventSFXMode Mode=EMCEventSFXMode::Add)
{
    auto* Wave=NewObject<USoundWave>();Wave->Duration=1.f;
    FMCEventSFXLayer Layer;Layer.Sound=Wave;Layer.DelaySeconds=1.2f;Layer.Name=TEXT("DelayedOriginalArtistChoice");
    FMCEventSFXSlot Result;Result.Mode=Mode;Result.Layers.Add(Layer);Result.MaxDurationSeconds=5.f;return Result;
}
UMCWorldPresentationComponent* BindOwnedWrapper(AActor* Owner,UMCVFXPresentationComponent* Channel)
{
    auto* Wrapper=NewObject<UMCWorldPresentationComponent>(Owner);
    Owner->AddInstanceComponent(Wrapper);Wrapper->RegisterComponent();
    // Owned transient fixture only: avoid loading the saved Tool library or bypassing
    // public policy. Bind the already genuinely prepared channel into its reflected map.
    auto* Property=FindFProperty<FMapProperty>(UMCWorldPresentationComponent::StaticClass(),TEXT("Channels"));
    if(!Property)return nullptr;
    FScriptMapHelper Map(Property,Property->ContainerPtrToValuePtr<void>(Wrapper));
    const int32 Index=Map.AddDefaultValue_Invalid_NeedsRehash();
    CastFieldChecked<FNameProperty>(Property->KeyProp)->SetPropertyValue(Map.GetKeyPtr(Index),FName(TEXT("Character.Movement")));
    CastFieldChecked<FObjectPropertyBase>(Property->ValueProp)->SetObjectPropertyValue(Map.GetValuePtr(Index),Channel);
    Map.Rehash();return Wrapper;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCharacterMovementLocalAudio,
    "MessControl.Audio.Movement.LocalOneShotsIndependentOfRemoteLedger",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCharacterMovementLocalAudio::RunTest(const FString&)
{
    using namespace MCCharacterMovementAudioTests;
    if(IsPIEActive()){AddError(TEXT("Run outside PIE; these are owned silent lifecycle fixtures."));return false;}
    FWorld F;auto* C=F.Component;FString Error;
    auto* Profile=NewObject<UMCPerkVFXProfile>();Profile->SFXSlots.Add(TEXT("Step"),Slot());
    if(!TestTrue(TEXT("Resident custom-only movement profile prepares without an original palette"),
        C->PrepareSourceProfile(C->SourceID,C->Events,Profile,Error,false))){AddError(Error);return false;}
    C->IssuedSerial=11;C->ReceivedSerial=17;C->CancelledThroughSerial=9;
    C->LastReceipt.Serial=12;C->LastCancel.ThroughSerial=8;
    const int32 Scopes=C->ScopedCancels.Num();const int32 Pending=C->PendingReceipts.Num();
    FMCPerkVFXGameplayValues First;First.EpisodeID=FGuid::NewGuid();First.LocalStateAgeSeconds=0.f;
    TestFalse(TEXT("Fixture owner is not authority; local sound must not call the authority RPC path"),F.Owner->HasAuthority());
    TestEqual(TEXT("Custom SFX preserves native VFX return contract"),
        C->PlayLocalSourceEvent(C->SourceID,TEXT("Step"),FTransform(FVector(10,20,30)),First),EMCVFXPresentationResult::UseDefault);
    if(!TestEqual(TEXT("One independent local handle is retained"),C->LocalEventPlayers.Num(),1))return false;
    auto* FirstAudio=C->LocalEventPlayers[0].Handle.AudioPlayer.Get();
    if(!TestNotNull(TEXT("Real delayed audio player exists without native clip/palette"),FirstAudio))return false;
    FirstAudio->Advance(.2f);
    auto Second=First;Second.EpisodeID=FGuid::NewGuid();
    C->PlayLocalSourceEvent(C->SourceID,TEXT("Step"),FTransform(FVector(40,50,60)),Second);
    TestEqual(TEXT("Rapid steps overlap instead of replacing one persistent source state"),C->LocalEventPlayers.Num(),2);
    TestEqual(TEXT("Both independent pending artist sounds survive the native .13s step cooldown"),F.AudioCount(),2);
    TestTrue(TEXT("First exact episode remains owned after the second step"),C->IsSourceEpisodeRunning(C->SourceID,First.EpisodeID));
    FirstAudio->Advance(1.1f);
    TestEqual(TEXT("Artist delay beyond native cadence starts naturally"),FirstAudio->GetStartedLayerCount(),1);
    TestEqual(TEXT("Local dispatch never increments the issued network serial"),C->IssuedSerial,uint32(11));
    TestEqual(TEXT("Local dispatch never enters remote dedup serials"),C->ReceivedSerial,uint32(17));
    TestEqual(TEXT("Local dispatch leaves replicated snapshot untouched"),C->LastReceipt.Serial,uint32(12));
    TestEqual(TEXT("Local dispatch leaves global cancellation floor untouched"),C->CancelledThroughSerial,uint32(9));
    TestEqual(TEXT("Local dispatch leaves replicated cancellation untouched"),C->LastCancel.ThroughSerial,uint32(8));
    TestEqual(TEXT("No local event enters the scoped remote cancellation ledger"),C->ScopedCancels.Num(),Scopes);
    TestEqual(TEXT("No local event enters the delivery FIFO"),C->PendingReceipts.Num(),Pending);
    TestTrue(TEXT("Serial zero never overwrites the remote player map"),C->Players.IsEmpty());
    TestTrue(TEXT("Local one-shot does not create a persistent native source state"),C->LocalSourceStates.IsEmpty());

    auto Remote=C->MakePreparedEventReceipt(C->SourceID,TEXT("Step"),FTransform::Identity,Second,
        C->PreparedSources.FindChecked(C->SourceID),*C->FindPreparedEvent(C->SourceID,TEXT("Step")),true);
    Remote.Serial=18;
    const auto* PreparedAudio=C->PreparedSources.FindChecked(C->SourceID).AudioEvents.Find(TEXT("Step"));
    TestTrue(TEXT("Existing incoming receipt path remains usable alongside local events"),
        C->ReceiveReceipt(Remote,C->FindPreparedEvent(C->SourceID,TEXT("Step")),Error,false,PreparedAudio));
    TestEqual(TEXT("One remote audio onset is added"),F.AudioCount(),3);
    C->ReceiveReceipt(Remote,C->FindPreparedEvent(C->SourceID,TEXT("Step")),Error,false,PreparedAudio);
    TestEqual(TEXT("Existing duplicate remote serial still cannot replay an onset"),F.AudioCount(),3);
    C->StopSourceEpisode(C->SourceID,First.EpisodeID,true);
    TestFalse(TEXT("Explicit episode cancellation cleans its local sound"),C->IsSourceEpisodeRunning(C->SourceID,First.EpisodeID));
    TestTrue(TEXT("Another local/remote episode is not cancelled"),C->IsSourceEpisodeRunning(C->SourceID,Second.EpisodeID));
    C->StopPresentation(true);
    TestEqual(TEXT("Explicit reset cleans local and remote event players"),F.AudioCount(),0);

    auto* State=F.World->SpawnActor<AMCGameState>();F.World->SetGameState(State);State->GameplayLoadId=FGuid::NewGuid();
    First.LocalStateAgeSeconds=.5f;First.EpisodeID=FGuid::NewGuid();
    C->PlayLocalSourceEvent(C->SourceID,TEXT("Step"),FTransform::Identity,First);
    if(!TestEqual(TEXT("Fresh epoch local event is retained"),C->LocalEventPlayers.Num(),1))return false;
    auto* EpochAudio=C->LocalEventPlayers[0].Handle.AudioPlayer.Get();
    TestTrue(TEXT("Actual accepted boundary age is retained, not restarted at zero"),EpochAudio&&FMath::IsNearlyEqual(EpochAudio->GetPlaybackTime(),.5f));
    State->GameplayLoadId=FGuid::NewGuid();C->TickComponent(.01f,LEVELTICK_All,nullptr);
    TestFalse(TEXT("Run epoch retirement cleans independent local events"),C->IsSourceEpisodeRunning(C->SourceID,First.EpisodeID));
    TestEqual(TEXT("Epoch retirement is not a remote serial cancellation"),C->IssuedSerial,uint32(11));
    const auto BadFrame=FTransform(FQuat(0,0,0,0));
    TestEqual(TEXT("Unnormalized native frame is rejected before any player is allocated"),
        C->PlayLocalSourceEvent(C->SourceID,TEXT("Step"),BadFrame),EMCVFXPresentationResult::Rejected);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCharacterMovementOriginalPolicy,
    "MessControl.Audio.Movement.OriginalPalettePolicyAndLandingAlias",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCharacterMovementOriginalPolicy::RunTest(const FString&)
{
    using namespace MCCharacterMovementAudioTests;
    if(IsPIEActive()){AddError(TEXT("Run outside PIE; no native palette RNG is changed in a user's play world."));return false;}
    FWorld F;FString Error;
    const auto* Contract=MCVFXSourceContracts::Find(TEXT("Character.Movement"));
    TestTrue(TEXT("Movement retains the finite shared Tool resource root"),Contract&&Contract->Library==MCVFXSourceContracts::ToolLibraryPath());
    for(const FName Event:{FName(TEXT("Step")),FName(TEXT("Land")),FName(TEXT("Jump")),FName(TEXT("Fall")),FName(TEXT("StandUp")),FName(TEXT("Pull"))}) {
        const auto* Original=Contract?Contract->OriginalAudio.Find(Event):nullptr;
        TestTrue(TEXT("Every exact movement boundary has one wired palette selector, not a variation file list"),Original&&Original->bSuppressionWired
            &&Original->Resource==FSoftObjectPath(TEXT("/Game/Data/DA_MouthSounds.DA_MouthSounds")));
        if(Original)TestEqual(TEXT("Land and legacy Jump share the original landing selector only"),Original->PaletteEvent,Event==TEXT("Land")?FName(TEXT("Jump")):Event);
    }
    TestFalse(TEXT("Unknown movement events cannot suppress unrelated native sound"),MCVFXSourceContracts::CanSuppressOriginalSFX(TEXT("Character.Movement"),TEXT("Other")));
    auto* Wrapper=BindOwnedWrapper(F.Owner,F.Component);
    if(!TestNotNull(TEXT("Owned fixture channel is discoverable through the real source wrapper"),Wrapper))return false;
    auto* Palette=NewObject<UMCSoundPalette>();FMCSoundVariation Variation;
    auto* A=NewObject<USoundWave>();auto* B=NewObject<USoundWave>();A->Duration=B->Duration=1.f;
    Variation.Sounds={A,B};Variation.PitchMin=.9f;Variation.PitchMax=1.1f;Palette->Events.Add(TEXT("Step"),Variation);
    for(const auto Mode:{EMCEventSFXMode::UseDefault,EMCEventSFXMode::Add,EMCEventSFXMode::Replace,EMCEventSFXMode::Disabled}) {
        auto* Profile=NewObject<UMCPerkVFXProfile>();Profile->SFXSlots.Add(TEXT("Step"),Slot(Mode));
        if(!F.Component->PrepareSourceProfile(TEXT("Character.Movement"),Contract->Events,Profile,Error,false)){AddError(Error);return false;}
        F.Component->PlayLocalSourceEvent(TEXT("Character.Movement"),TEXT("Step"),FTransform::Identity);
        TestEqual(TEXT("Only Default/Add permit the original; custom layers alone never silence it"),Wrapper->UsesOriginalAudio(TEXT("Character.Movement"),TEXT("Step")),
            Mode==EMCEventSFXMode::UseDefault||Mode==EMCEventSFXMode::Add);
        TestEqual(TEXT("Only Add/Replace schedule a custom one-shot"),F.AudioCount(),Mode==EMCEventSFXMode::Add||Mode==EMCEventSFXMode::Replace?1:0);
        // Deliberately advance the two native variation/pitch draws before comparing state.
        FMath::RandInit(901);(void)FMath::RandRange(0,1);(void)FMath::FRandRange(.9f,1.1f);const int32 Expected=FMath::Rand();
        FMath::RandInit(901);
        TestTrue(TEXT("Original selector is resolved once even when output is intentionally suppressed"),
            Palette->PlayWithParameters(F.Owner,TEXT("Step"),FVector::ZeroVector,.65f,.5f,TEXT("Character.Movement"),TEXT("Step")));
        TestEqual(TEXT("Native variation/pitch RNG is not repeated or skipped by audio mode"),FMath::Rand(),Expected);
        F.Component->StopPresentation(true);
    }
    auto* Hero=F.World->SpawnActor<AMCToothCharacter>();Hero->SoundPalette=nullptr;Hero->SetRole(ROLE_SimulatedProxy);
    auto* Landing=NewObject<UMCVFXPresentationComponent>(Hero);Landing->SourceID=TEXT("Character.Movement");Landing->Events=Contract->Events;
    Hero->AddInstanceComponent(Landing);Landing->RegisterComponent();
    if(!TestNotNull(TEXT("Landing fixture retains its actual hero-owned channel"),BindOwnedWrapper(Hero,Landing)))return false;
    auto* Legacy=NewObject<UMCPerkVFXProfile>();Legacy->SFXSlots.Add(TEXT("Jump"),Slot());
    if(!Landing->PrepareSourceProfile(Landing->SourceID,Landing->Events,Legacy,Error,false)){AddError(Error);return false;}
    TestEqual(TEXT("Saved Jump override remains a landing compatibility alias"),MCCharacterMovementAudio::LandingEvent(Hero),FName(TEXT("Jump")));
    FName Observed;int32 Callbacks=0;
    Landing->OnEventAccepted.AddLambda([&](FName Event,EMCVFXPresentationResult){Observed=Event;++Callbacks;});
    Hero->Landed(FHitResult());
    TestEqual(TEXT("Actual native Landed callback dispatches only one alias without an original palette"),Callbacks,1);
    TestEqual(TEXT("Legacy landing uses the saved Jump event, not a fabricated takeoff"),Observed,FName(TEXT("Jump")));
    TestEqual(TEXT("Custom-only landing schedules its sound"),F.AudioCount(),1);
    Landing->StopPresentation(true);Legacy->SFXSlots.Add(TEXT("Land"),Slot(EMCEventSFXMode::Disabled));
    if(!Landing->PrepareSourceProfile(Landing->SourceID,Landing->Events,Legacy,Error,false)){AddError(Error);return false;}
    Hero->Landed(FHitResult());
    TestEqual(TEXT("Explicit Land wins and never also fires the Jump alias"),Callbacks,2);
    TestEqual(TEXT("Actual landing identity is Land when configured"),Observed,FName(TEXT("Land")));
    TestEqual(TEXT("Disabled Land is intentional silence, no generic fallback sound"),F.AudioCount(),0);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCharacterMovementInitialBodyState,
    "MessControl.Audio.Movement.InitialProxyBodyStateUsesPreparedChannel",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCharacterMovementInitialBodyState::RunTest(const FString&)
{
    using namespace MCCharacterMovementAudioTests;
    if(IsPIEActive()){AddError(TEXT("Run outside PIE; only an isolated owned Game world is initialized."));return false;}
    struct FContractScope
    {
        FMCVFXSourceContract Original=*MCVFXSourceContracts::Find(TEXT("Character.Movement"));
        ~FContractScope(){MCVFXSourceContracts::Register(Original);}
    } ContractScope;
    auto* Profile=NewObject<UMCPerkVFXProfile>();Profile->SFXSlots.Add(TEXT("Fall"),Slot());Profile->SFXSlots.Add(TEXT("StandUp"),Slot());
    auto* Library=NewObject<UMCVFXPresentationLibrary>();Library->Profiles.Add(TEXT("Character.Movement"),Profile);
    auto FixtureContract=ContractScope.Original;FixtureContract.Library=FSoftObjectPath(Library);
    MCVFXSourceContracts::Register(MoveTemp(FixtureContract)); // Transient test metadata only, always restored; no saved library mutation.
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);World->bAllowAudioPlayback=false;
    // SetGameMode dereferences World.GameInstance to create the actual mode.
    // Match the initialized private audio/Ice fixtures; no GameInstance::Init,
    // LocalPlayer, viewport/audio startup, network session or saved profile state.
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
    if(!TestTrue(TEXT("Owned silent Game world creates its actual base game mode"),World->SetGameMode(URL))) {
        World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return false;
    }
    World->InitializeActorsForPlay(URL);World->BeginPlay();
    for(const auto State:{EMCBodyState::Ragdoll,EMCBodyState::Recovering}) {
        auto* Hero=World->SpawnActorDeferred<AMCToothCharacter>(AMCToothCharacter::StaticClass(),FTransform::Identity,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!TestNotNull(TEXT("Owned deferred initial proxy"),Hero))break;
        Hero->SetRole(ROLE_SimulatedProxy);Hero->SoundPalette=nullptr;
        auto* Property=FindFProperty<FStructProperty>(UMCToothPhysicsComponent::StaticClass(),TEXT("Frame"));
        if(!TestNotNull(TEXT("Actual replicated body frame is available"),Property)){Hero->Destroy();break;}
        auto* Frame=Property->ContainerPtrToValuePtr<FMCRagdollFrame>(Hero->ToothPhysics);
        Frame->State=State;Frame->StateStartedAt=World->GetTimeSeconds()-.4f;
        Hero->FinishSpawning(FTransform::Identity);
        auto* Wrapper=Hero->FindComponentByClass<UMCWorldPresentationComponent>();
        auto* Channel=Wrapper?Wrapper->GetSource(TEXT("Character.Movement")):nullptr;
        TestTrue(TEXT("Actual initial component OnRep reaches a prepared movement channel"),Channel&&Channel->IsSourcePrepared(TEXT("Character.Movement")));
        TestEqual(TEXT("Actual initial proxy body state is applied without a new transport"),Hero->ToothPhysics->GetBodyState(),State);
        AMCEventSFXPlayer* Audio=nullptr;
        for(TActorIterator<AMCEventSFXPlayer> It(World);It;++It)if(It->IsRunning()) {Audio=*It;break;}
        TestTrue(TEXT("Initial accepted Fall/StandUp schedules its custom-only sound before any later transition"),Audio
            &&Audio->GetContext().Event==(State==EMCBodyState::Ragdoll?FName(TEXT("Fall")):FName(TEXT("StandUp"))));
        TestTrue(TEXT("Initial replicated boundary age is preserved through BeginPlay"),Audio&&FMath::IsNearlyEqual(Audio->GetPlaybackTime(),.4f,.01f));
        if(Channel)Channel->StopPresentation(true);Hero->Destroy();
    }
    World->EndPlay(EEndPlayReason::Quit);
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    return !HasAnyErrors();
}
#endif
