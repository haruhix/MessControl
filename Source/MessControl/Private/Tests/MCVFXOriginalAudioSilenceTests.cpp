#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "MCVFXSourceContracts.h"
#include "MCEventSFX.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXOriginalAudioConfirmedSilence,
    "MessControl.VFX.OriginalAudio.ConfirmedSilenceIsExactAndSeparate",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXOriginalAudioConfirmedSilence::RunTest(const FString&)
{
    struct FKnown {FName Source;FName Event;};
    const FKnown Known[]={{TEXT("Boss.NutKnight.Melee"),TEXT("Tell")},
        {TEXT("Boss.NutMage.Melee"),TEXT("Tell")},{TEXT("Boss.NutMage.Melee"),TEXT("Slash")}};
    for(const auto& Row:Known) {
        FString Evidence,Error;
        TestTrue(TEXT("Exact audited event has separate silence provenance"),MCVFXSourceContracts::IsOriginalSFXConfirmedSilent(Row.Source,Row.Event,&Evidence));
        TestFalse(TEXT("Silence provenance is explicit"),Evidence.IsEmpty());
        const auto* Contract=MCVFXSourceContracts::Find(Row.Source);
        TestTrue(TEXT("Silence never fabricates an original SoundBase row"),!Contract||!Contract->OriginalAudio.Contains(Row.Event));
        for(const auto Mode:{EMCEventSFXMode::Replace,EMCEventSFXMode::Disabled}) {
            FMCEventSFXSlot Slot;Slot.Mode=Mode;
            TestTrue(TEXT("Exact silent event can apply independent audio policy"),MCEventSFX::ValidateNativePolicy(Slot,MCVFXSourceContracts::CanSuppressOriginalSFX(Row.Source,Row.Event),Error));
        }
    }
    const FKnown Unknown[]={{TEXT("Boss.NutKnight.Melee"),TEXT("Slash")},
        {TEXT("Boss.NutKnight.Melee"),TEXT("Slam")},{TEXT("Boss.NutMage.Fireball"),TEXT("Charge")},
        {TEXT("Boss.NutMage.Melee"),TEXT("Impact")},{TEXT("Tests.Unknown"),TEXT("Tell")}};
    for(const auto& Row:Unknown) {
        FString Evidence=TEXT("stale"),Error;
        TestFalse(TEXT("No inference to another event/source/donor"),MCVFXSourceContracts::IsOriginalSFXConfirmedSilent(Row.Source,Row.Event,&Evidence));
        TestTrue(TEXT("Unknown query clears stale evidence"),Evidence.IsEmpty());
        FMCEventSFXSlot Slot;Slot.Mode=EMCEventSFXMode::Disabled;
        TestFalse(TEXT("Unknown native audio remains unsuppressible"),MCEventSFX::ValidateNativePolicy(Slot,MCVFXSourceContracts::CanSuppressOriginalSFX(Row.Source,Row.Event),Error));
    }
    TestTrue(TEXT("Mapped Brush sound still uses its real audio gate"),MCVFXSourceContracts::CanSuppressOriginalSFX(TEXT("Tool.Brush"),TEXT("Contact")));
    TestFalse(TEXT("Mapped Brush sound is not labelled silent"),MCVFXSourceContracts::IsOriginalSFXConfirmedSilent(TEXT("Tool.Brush"),TEXT("Contact")));
    return !HasAnyErrors();
}

#endif
