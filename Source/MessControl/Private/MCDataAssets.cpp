#include "MCDataAssets.h"
#include "MCWorldPresentationComponent.h"
#include "MCPlayerController.h"
#include "AudioDevice.h"
#include "AudioParameter.h"
#include "Audio/ActorSoundParameterInterface.h"
#include "Components/ActorComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"

namespace
{
    TAutoConsoleVariable<int32> CVarAudioPaletteLog(TEXT("mc.Audio.ExperimentLog"), 0,
        TEXT("Log gameplay palette events, chosen assets and normalized Intensity/Speed inputs."));
}

UMCSoundPalette::UMCSoundPalette()
{
    // Authoring places only: never assign a source or modify the existing SFX bank.
    for (const TCHAR* Scene : {TEXT("MainMenu"),TEXT("Hub"),TEXT("Relax"),TEXT("Gameplay"),
        TEXT("NutEncounter"),TEXT("IceEvent"),TEXT("FogBrawl"),TEXT("CataclysmRafts"),
        TEXT("CataclysmJellies"),TEXT("ColdCola"),TEXT("FinalBoss")})
        Scenes.FindOrAdd(FName(Scene));
}

bool UMCSoundPalette::GetScene(FName Scene, FMCSoundScene& Settings) const
{
    const FMCSoundScene* Found = Scenes.Find(Scene);
    Settings = Found ? *Found : FMCSoundScene();
    return Found != nullptr;
}

const FMCSoundVariation* UMCSoundPalette::ResolveEvent(FName Event, FName* ResolvedEvent) const
{
    FName Key = Event;
    const FMCSoundVariation* Entry = Events.Find(Key);
    if (!Entry)
    {
        if (Event == TEXT("KnifeSwing") || Event == TEXT("PickaxeSwing")) Key = TEXT("Whoosh");
        else if (Event == TEXT("KnifeHit") || Event == TEXT("PickaxeHit")) Key = TEXT("Hit");
        Entry = Events.Find(Key);
    }
    if (ResolvedEvent) *ResolvedEvent = Entry ? Key : NAME_None;
    return Entry;
}

void UMCSoundPalette::Play(UObject* WorldContext, FName Event, FVector Location) const
{
    PlayWithParameters(WorldContext, Event, Location);
}

bool UMCSoundPalette::PlayWithParameters(UObject* WorldContext, FName Event, FVector Location, float Intensity, float Speed,
    FName PresentationSourceID,FName PresentationEvent,AActor* PresentationPolicyOwner) const
{
    FName ResolvedEvent;
    const FMCSoundVariation* Entry = ResolveEvent(Event, &ResolvedEvent);
    if (!Entry) return false;
    UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
    if (!World || World->GetNetMode() == NM_DedicatedServer) return true;

    TArray<USoundBase*, TInlineAllocator<8>> ValidSounds;
    for (const auto& Candidate : Entry->Sounds) if (IsValid(Candidate.Get())) ValidSounds.Add(Candidate.Get());
    if (ValidSounds.IsEmpty() || Entry->Volume <= 0.f) return true;
    USoundBase* Sound = ValidSounds[FMath::RandRange(0, ValidSounds.Num() - 1)];
    const float Pitch = FMath::FRandRange(Entry->PitchMin, FMath::Max(Entry->PitchMin, Entry->PitchMax));
    const float NormalizedIntensity = FMath::Clamp(Intensity, 0.f, 1.f);
    const float NormalizedSpeed = FMath::Clamp(Speed, 0.f, 1.f);
    AActor* SoundOwner = Cast<AActor>(WorldContext);
    if (!SoundOwner) if (const auto* Component = Cast<UActorComponent>(WorldContext)) SoundOwner = Component->GetOwner();
    if (!SoundOwner) SoundOwner = WorldContext->GetTypedOuter<AActor>();
    // Sound selection and pitch are still resolved exactly once even when the
    // source's audio-only Composer policy suppresses this original output.
    const auto* PolicyController=IsValid(PresentationPolicyOwner)?Cast<AMCPlayerController>(PresentationPolicyOwner):nullptr;
    const bool bExplicitPolicyValid=!PresentationPolicyOwner||(IsValid(PolicyController)
        && PolicyController->GetWorld()==World&&PolicyController->HasLocalPresentationOwner());
    const AActor* PolicyOwner=PresentationPolicyOwner?PolicyController:SoundOwner;
    // UI policy ownership is separate from the legacy widget sound context.
    // Invalid/cross-world/nonlocal explicit owners always retain original audio.
    const bool bNativeAudio=PresentationSourceID.IsNone()||PresentationEvent.IsNone()||!bExplicitPolicyValid
        ||MCWorldPresentation::UsesOriginalAudio(PolicyOwner,PresentationSourceID,PresentationEvent);
    if (bNativeAudio && GEngine && GEngine->UseSound() && World->bAllowAudioPlayback)
    {
        if (FAudioDeviceHandle AudioDevice = World->GetAudioDevice())
        {
            // Startup-loaded palettes can be disregarded by GC. Keep runtime parameters
            // owned by the active sound instead of caching a UObject on the data asset.
            TArray<FAudioParameter> Parameters;
            Parameters.Reserve(2);
            Parameters.Emplace(FName(TEXT("Intensity")), NormalizedIntensity);
            Parameters.Emplace(FName(TEXT("Speed")), NormalizedSpeed);
            UActorSoundParameterInterface::Fill(SoundOwner, Parameters);
            AudioDevice->PlaySoundAtLocation(Sound, World, Entry->Volume, Pitch, 0.f,
                Location, FRotator::ZeroRotator, nullptr, nullptr, MoveTemp(Parameters), SoundOwner);
        }
    }
    if (CVarAudioPaletteLog.GetValueOnGameThread() != 0)
        UE_LOG(LogTemp, Display, TEXT("MC_AUDIO_PALETTE event=%s key=%s palette=%s path=%s class=%s volume=%.3f pitch=%.3f intensity=%.3f speed=%.3f"),
            *Event.ToString(), *ResolvedEvent.ToString(), *GetPathName(), *Sound->GetPathName(), *Sound->GetClass()->GetName(),
            Entry->Volume, Pitch, NormalizedIntensity, NormalizedSpeed);
    return true;
}
