#pragma once

#include "MCNutVFXCatalog.h"
#include "MCPerkVFXProfile.h"
#include "NiagaraSystem.h"
#include "UObject/Package.h"

namespace MCNutVFX
{
inline UMCPerkVFXProfile* WithWizardSpellDefaults(UMCPerkVFXProfile* Profile,const FMCNutVFXAction& Action)
{
    auto* Effective=NewObject<UMCPerkVFXProfile>(GetTransientPackage());
    if(Profile) {
        Effective->Slots=Profile->Slots;
        Effective->SFXSlots=Profile->SFXSlots;
        Effective->EventGroups=Profile->EventGroups;
        Effective->NativePresentation=Profile->NativePresentation;
    }
    for(const FName Event:Action.Events) {
        const TCHAR* Path=WizardSpellDefaultPath(Action.ID,Event);
        if(!Path) continue;
        auto& Slot=Effective->Slots.FindOrAdd(Event);
        if(Slot.Mode!=EMCPerkVFXSlotMode::UseDefault) continue;
        Slot=FMCPerkVFXSlot{};
        Slot.Mode=EMCPerkVFXSlotMode::Replace;
        Slot.System=TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(Path));
        if(Action.ID==TEXT("Boss.NutMage.Flash")) {
            Slot.Transform.SetScale3D(FVector(Event==TEXT("Departure")?.5f:.7f));
            continue;
        }
        const bool bLeft=Event==TEXT("ChargeLeft"),bRight=Event==TEXT("ChargeRight");
        Slot.Transform.SetScale3D(FVector(Event==TEXT("Merge")?.8f:(bLeft || bRight)?.38f:.5f));
        const FLinearColor Tint=bLeft?FLinearColor(1.f,.12f,.20f):bRight?FLinearColor(.12f,.42f,1.f):FLinearColor(.57f,.19f,1.f);
        for(const FName Name:{FName(TEXT("User.MagicTint")),FName(TEXT("User.FireTint")),FName(TEXT("User.CoreTint")),FName(TEXT("User.WakeTint"))}) {
            auto& Color=Slot.Colors.Emplace_GetRef();
            Color.Name=Name;Color.Value=Tint;Color.bOverride=true;
        }
    }
    return Effective;
}
}
