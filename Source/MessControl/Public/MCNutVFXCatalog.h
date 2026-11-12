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
    MESSCONTROL_API FName GravityWavesSource();
    MESSCONTROL_API TSoftObjectPtr<UNiagaraSystem> GravityWavesDefaultSystem(FName Event);
    MESSCONTROL_API bool PrepareGravityWavesOverride(UMCVFXPresentationComponent* Component,UMCPerkVFXProfile* Profile,FString& Error);
    inline bool IsWizardSpellSource(FName Source)
    {return Source==TEXT("Boss.NutMage.Flash") || Source==TEXT("Boss.NutMage.NutSphere");}

    inline const TCHAR* WizardSpellDefaultPath(FName Source,FName Event)
    {
        if(Source==TEXT("Boss.NutMage.Flash") && (Event==TEXT("Departure") || Event==TEXT("Arrival")))
            return TEXT("/Game/Gameplay/VFX/NutCombat/NS_NutArcaneBurst.NS_NutArcaneBurst");
        if(Source==TEXT("Boss.NutMage.NutSphere")) {
            if(Event==TEXT("ChargeLeft") || Event==TEXT("ChargeRight") || Event==TEXT("Merge"))
                return TEXT("/Game/Gameplay/VFX/NutCombat/FireballEditable/NS_Fireball_Charge.NS_Fireball_Charge");
            if(Event==TEXT("Shot"))
                return TEXT("/Game/Gameplay/VFX/NutCombat/FireballEditable/NS_Fireball_Release.NS_Fireball_Release");
        }
        return nullptr;
    }
    MESSCONTROL_API TConstArrayView<FMCNutVFXAction> Actions();
    MESSCONTROL_API const FMCNutVFXAction* Find(FName Source);
    /** Explicit preparation only. Missing profiles retain native artwork; playback never loads. */
    MESSCONTROL_API bool Prepare(UMCVFXPresentationComponent* Component,FName Source,FString& Error,bool bAllowLoad=false);
}
