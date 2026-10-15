#include "MCDataAssets.h"
#include "AudioParameter.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

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

bool UMCSoundPalette::PlayWithParameters(UObject* WorldContext, FName Event, FVector Location, float Intensity, float Speed) const
{
    FName ResolvedEvent;
    const FMCSoundVariation* Entry = ResolveEvent(Event, &ResolvedEvent);
    if (!Entry) return false;
    if (!WorldContext || !WorldContext->GetWorld() || WorldContext->GetWorld()->GetNetMode() == NM_DedicatedServer) return true;

    TArray<USoundBase*, TInlineAllocator<8>> ValidSounds;
    for (const auto& Candidate : Entry->Sounds) if (IsValid(Candidate.Get())) ValidSounds.Add(Candidate.Get());
    if (ValidSounds.IsEmpty() || Entry->Volume <= 0.f) return true;
    USoundBase* Sound = ValidSounds[FMath::RandRange(0, ValidSounds.Num() - 1)];
    const float Pitch = FMath::FRandRange(Entry->PitchMin, FMath::Max(Entry->PitchMin, Entry->PitchMax));
    const float NormalizedIntensity = FMath::Clamp(Intensity, 0.f, 1.f);
    const float NormalizedSpeed = FMath::Clamp(Speed, 0.f, 1.f);
    if (!InitialParameters) InitialParameters = NewObject<UInitialActiveSoundParams>(const_cast<UMCSoundPalette*>(this));
    InitialParameters->Reset(2);
    InitialParameters->AudioParams.Emplace(FName(TEXT("Intensity")), NormalizedIntensity);
    InitialParameters->AudioParams.Emplace(FName(TEXT("Speed")), NormalizedSpeed);
    // UE copies the initial parameters during this call; the shared palette can reuse them for the next actor.
    AActor* SoundOwner = Cast<AActor>(WorldContext);
    if (!SoundOwner) if (const auto* Component = Cast<UActorComponent>(WorldContext)) SoundOwner = Component->GetOwner();
    UGameplayStatics::PlaySoundAtLocation(WorldContext, Sound, Location, FRotator::ZeroRotator,
        Entry->Volume, Pitch, 0.f, nullptr, nullptr, SoundOwner, InitialParameters);
    if (CVarAudioPaletteLog.GetValueOnGameThread() != 0)
        UE_LOG(LogTemp, Display, TEXT("MC_AUDIO_PALETTE event=%s key=%s palette=%s path=%s class=%s volume=%.3f pitch=%.3f intensity=%.3f speed=%.3f"),
            *Event.ToString(), *ResolvedEvent.ToString(), *GetPathName(), *Sound->GetPathName(), *Sound->GetClass()->GetName(),
            Entry->Volume, Pitch, NormalizedIntensity, NormalizedSpeed);
    return true;
}
