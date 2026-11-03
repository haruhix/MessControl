#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCVFXPresentationComponent.h"
#include "MCEventSFXPlayer.h"
#include "MCPerkVFXProfile.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "MCVFXTestWorld.h"
#include "EngineUtils.h"
#include "Sound/SoundWave.h"

namespace MCFreezeAudioLifecycleTests
{
struct FWorld
{
    UWorld* W=MCVFXTestWorld::Create(EWorldType::Game,false);
    AActor* Owner=nullptr;UMCVFXPresentationComponent* Source=nullptr;
    FWorld() {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);W->bAllowAudioPlayback=false;
        W->SetGameInstance(NewObject<UGameInstance>(GEngine));FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        W->SetGameMode(URL);W->InitializeActorsForPlay(URL);W->BeginPlay();
        Owner=W->SpawnActor<AActor>();Source=NewObject<UMCVFXPresentationComponent>(Owner);
        Owner->AddInstanceComponent(Source);Source->RegisterComponent();
    }
    ~FWorld(){UMCVFXPresentationComponent::StopWorldPresentations(W);W->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(W);W->DestroyWorld(false);}
    UMCPerkVFXProfile* Profile() {
        auto* P=NewObject<UMCPerkVFXProfile>();
        auto& Slot=P->SFXSlots.FindOrAdd(TEXT("Hold"));Slot.Mode=EMCEventSFXMode::Add;Slot.MaxDurationSeconds=10;Slot.TailSeconds=3;
        auto* Wave=NewObject<USoundWave>();Wave->Duration=4;
        FMCEventSFXLayer One;One.Sound=Wave;One.bAllowNaturalTail=true;Slot.Layers.Add(One);
        auto Loop=One;Loop.ID=FGuid::NewGuid();Loop.bRepeatOnFinish=true;Loop.FadeOutSeconds=.1f;Slot.Layers.Add(Loop);
        auto Pending=One;Pending.ID=FGuid::NewGuid();Pending.DelaySeconds=5;Slot.Layers.Add(Pending);
        return P;
    }
    AMCEventSFXPlayer* Find(FName ID) {
        for(TActorIterator<AMCEventSFXPlayer> It(W);It;++It)if(It->IsRunning()&&It->GetContext().SourceID==ID)return *It;
        return nullptr;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFreezeAcceptedAudioDeathTail,
    "MessControl.Audio.Freeze.AcceptedFiniteTailStopsLoopsAndSurvivesRetiredOwner",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFreezeAcceptedAudioDeathTail::RunTest(const FString&)
{
    using namespace MCFreezeAudioLifecycleTests;FWorld F;FString Error;const FName ID(TEXT("Character.FreezeDeath"));
    auto* P=F.Profile();
    if(!F.Source->PrepareSourceProfile(ID,{TEXT("Hold"),TEXT("Shatter")},P,Error,false)){AddError(Error);return false;}
    if(!F.Source->PrepareSourceProfile(TEXT("Other.Test"),{TEXT("Hold")},P,Error,false)){AddError(Error);return false;}
    int32 Accepted=0;F.Source->OnSourceEventAccepted.AddLambda([&](FName I,FName E,EMCVFXPresentationResult){if(I==ID&&E==TEXT("Hold"))++Accepted;});
    FMCPerkVFXGameplayValues V;V.LocalStateAgeSeconds=0;
    F.Source->PlayLocalSourceEvent(ID,TEXT("Hold"),FTransform::Identity,V,false);
    F.Source->PlayLocalSourceEvent(TEXT("Other.Test"),TEXT("Hold"),FTransform::Identity,V,false);
    auto* Audio=F.Find(ID);auto* Other=F.Find(TEXT("Other.Test"));
    if(!TestNotNull(TEXT("Real accepted Hold audio player"),Audio)||!TestNotNull(TEXT("Independent actual source"),Other))return false;
    TestEqual(TEXT("The exact accepted source had one onset, not a replay"),Accepted,1);
    Audio->Advance(1.6f);
    F.Source->RetireLocalSourceAudioEvent(ID,TEXT("Hold"));
    F.Source->StopPresentationWithLocalSourceAudioTails(ID);
    TestFalse(TEXT("Unrelated source is stopped immediately at death"),Other->IsRunning());
    TestEqual(TEXT("Future/loop repeats are not new death tails"),Audio->GetPendingLayerCount(),0);
    Audio->Advance(.2f);
    TestEqual(TEXT("One finite authored natural tail remains after the loop fade"),Audio->GetVoiceComponents().Num(),1);
    TestTrue(TEXT("Long accepted Hold remains after the native1.6s boundary"),Audio->IsRunning());
    F.Owner->Destroy();Audio->Advance(.1f);
    TestTrue(TEXT("Actual world-owned finite tail survives ordinary owner destruction"),Audio->IsRunning());
    TestEqual(TEXT("Retirement never replays accepted audio"),Accepted,1);
    UMCVFXPresentationComponent::StopWorldPresentations(F.W);
    TestFalse(TEXT("Explicit reset still clears the retained tail"),Audio->IsRunning());
    return true; // Silent lifecycle only; no audible-output claim.
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFreezeSnapshotForgetKeepsOverrideAndTail,
    "MessControl.VFX.FreezeCapture.SnapshotReleaseKeepsScopedOverrideAndAcceptedTail",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFreezeSnapshotForgetKeepsOverrideAndTail::RunTest(const FString&)
{
    using namespace MCFreezeAudioLifecycleTests;FWorld F;FString Error;const FName ID(TEXT("Character.FreezeDeath"));
    auto* P=F.Profile();
    if(!F.Source->PrepareScopedOverride(ID,{TEXT("Hold"),TEXT("Shatter")},P,Error)){AddError(Error);return false;}
    FMCPerkVFXGameplayValues V;V.LocalStateAgeSeconds=0;
    F.Source->PlayLocalSourceEvent(ID,TEXT("Hold"),FTransform::Identity,V,false);auto* Audio=F.Find(ID);
    if(!TestNotNull(TEXT("Actual accepted immutable source player"),Audio))return false;
    F.Source->ForgetPreparedSourceSnapshot(ID);
    TestFalse(TEXT("Old per-load source snapshot is genuinely released"),F.Source->IsSourcePrepared(ID));
    TestTrue(TEXT("Snapshot release does not cancel accepted world tail"),Audio->IsRunning());
    // Null canonical profile would have no custom audio. The retained scoped
    // override must reinstall its independent immutable user choice instead.
    if(!F.Source->PrepareSourceProfile(ID,{TEXT("Hold"),TEXT("Shatter")},nullptr,Error,false)){AddError(Error);return false;}
    F.Source->PlayLocalSourceEvent(ID,TEXT("Hold"),FTransform::Identity,V,false);
    int32 Count=0;for(TActorIterator<AMCEventSFXPlayer> It(F.W);It;++It)if(It->IsRunning()&&It->GetContext().SourceID==ID)++Count;
    TestEqual(TEXT("Explicit reprepare retained the scoped override without replaying old receipt"),Count,2);
    F.Source->StopPresentation(true);TestFalse(TEXT("Explicit component reset owns all retained handles"),Audio->IsRunning());
    return true;
}
#endif
