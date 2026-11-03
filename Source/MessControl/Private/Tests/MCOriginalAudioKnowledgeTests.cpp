#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "MCVFXSourceContracts.h"
#include "MCEventSFX.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCOriginalAudioKnowledgeTest,
    "MessControl.Audio.Composer.OriginalAudioKnowledgeIsNotSilence",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCOriginalAudioKnowledgeTest::RunTest(const FString&)
{
    using K=EMCVFXOriginalAudioKnowledge;
    const FName ID(TEXT("Tool.Brush")),Event(TEXT("Contact"));
    const auto* Definition=MCVFXSourceContracts::Find(ID);if(!TestNotNull(TEXT("Actual Brush source exists"),Definition))return false;
    struct FRestore {FMCVFXSourceContract Definition;~FRestore(){MCVFXSourceContracts::Register(MoveTemp(Definition));}} Restore{*Definition};
    auto Candidate=*Definition;auto* Audio=Candidate.OriginalAudio.Find(Event);if(!TestNotNull(TEXT("Actual Brush selector exists"),Audio))return false;
    const auto Known=MCVFXSourceContracts::GetOriginalAudioStatus(ID,Event);
    TestTrue(TEXT("Native declaration is distinct from proven silence"),Known.Knowledge==K::DeclaredSource&&Known.bIndependentNativeGate);
    // A wired flag with no resource is incomplete discovery, never evidence of silence.
    Audio->Resource.Reset();Audio->bSuppressionWired=true;MCVFXSourceContracts::Register(Candidate);
    const auto Missing=MCVFXSourceContracts::GetOriginalAudioStatus(ID,Event);
    TestTrue(TEXT("Missing mapping stays unknown"),Missing.Knowledge==K::Unknown);
    TestFalse(TEXT("A dangling gate cannot mute unknown native producers"),MCVFXSourceContracts::CanSuppressOriginalSFX(ID,Event));
    FMCEventSFXSlot Slot;FString Error;Slot.Mode=EMCEventSFXMode::Add;
    TestTrue(TEXT("Independent added layers stay available for unknown originals"),MCEventSFX::ValidateNativePolicy(Slot,false,Error));
    Slot.Mode=EMCEventSFXMode::Disabled;
    TestFalse(TEXT("Unknown original cannot be declared disabled by a profile"),MCEventSFX::ValidateNativePolicy(Slot,MCVFXSourceContracts::CanSuppressOriginalSFX(ID,Event),Error));
    const auto Absent=MCVFXSourceContracts::GetOriginalAudioStatus(TEXT("Boss.NutMage.Melee"),TEXT("Tell"));
    TestTrue(TEXT("The exact audited event is proven absent without an invented source"),Absent.Knowledge==K::ConfirmedAbsent&&!Absent.bIndependentNativeGate&&!Absent.Evidence.IsEmpty());
    TestTrue(TEXT("Confirmed absence permits explicit sound policy"),MCEventSFX::ValidateNativePolicy(Slot,MCVFXSourceContracts::CanSuppressOriginalSFX(TEXT("Boss.NutMage.Melee"),TEXT("Tell")),Error));
    TestTrue(TEXT("A neighbouring unaudited event remains unknown"),MCVFXSourceContracts::GetOriginalAudioStatus(TEXT("Boss.NutKnight.Melee"),TEXT("Slam")).Knowledge==K::Unknown);
    return true;
}
#endif
