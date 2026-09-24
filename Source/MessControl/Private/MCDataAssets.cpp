#include "MCDataAssets.h"
#include "Kismet/GameplayStatics.h"

void UMCSoundPalette::Play(UObject* WorldContext, FName Event, FVector Location) const
{
    if (!WorldContext || !WorldContext->GetWorld() || WorldContext->GetWorld()->GetNetMode() == NM_DedicatedServer) return;
    const FMCSoundVariation* Entry = Events.Find(Event);
    if (!Entry || Entry->Sounds.IsEmpty()) return;
    USoundBase* Sound = Entry->Sounds[FMath::RandRange(0, Entry->Sounds.Num() - 1)];
    if (Sound) UGameplayStatics::PlaySoundAtLocation(WorldContext, Sound, Location, Entry->Volume,
        FMath::FRandRange(Entry->PitchMin, FMath::Max(Entry->PitchMin, Entry->PitchMax)));
}
