#pragma once

#include "CoreMinimal.h"

class UMCVFXPresentationComponent;
class UMCPerkVFXProfile;
class UNiagaraSystem;
class USoundBase;

/** Runtime-owned identities. Composer consumes this same contract, not a second private list. */
struct MESSCONTROL_API FMCNutVFXAction
{
    FName ID;
    FText Label;
    TArray<FName> Events;
    FName LabScenario;
};

/** The actual gameplay caller supplies identity; a shared visual cue never guesses its spell. */
struct MESSCONTROL_API FMCNutCuePresentation
{
    FName Source,Event,AfterWindupEvent;
    FMCNutCuePresentation()=default;
    FMCNutCuePresentation(FName InSource,FName InEvent,FName InAfterWindup=NAME_None)
        :Source(InSource),Event(InEvent),AfterWindupEvent(InAfterWindup){}
};

namespace MCNutVFX
{
    MESSCONTROL_API FName ChainSource();
    MESSCONTROL_API TSoftObjectPtr<UNiagaraSystem> ChainDefaultSystem(FName Event);
    MESSCONTROL_API TSoftObjectPtr<USoundBase> ChainDefaultSound(FName Event);
    MESSCONTROL_API bool PrepareChainOverride(UMCVFXPresentationComponent* Component,UMCPerkVFXProfile* Profile,FString& Error);
    MESSCONTROL_API TConstArrayView<FMCNutVFXAction> Actions();
    MESSCONTROL_API const FMCNutVFXAction* Find(FName Source);
    /** Explicit preparation only. Missing profiles retain native artwork; playback never loads. */
    MESSCONTROL_API bool Prepare(UMCVFXPresentationComponent* Component,FName Source,FString& Error,bool bAllowLoad=false);
}
