#if WITH_DEV_AUTOMATION_TESTS
#include "MCDataAssets.h"
#include "Misc/AutomationTest.h"
#include "Sound/SoundWave.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSoundPaletteRoutingTest, "MessControl.Audio.PaletteRouting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCSoundPaletteRoutingTest::RunTest(const FString& Parameters)
{
    auto* Palette = NewObject<UMCSoundPalette>();
    auto* WhooshSound = NewObject<USoundWave>(Palette);
    auto* HitSound = NewObject<USoundWave>(Palette);
    auto* KnifeSound = NewObject<USoundWave>(Palette);
    Palette->Events.FindOrAdd(TEXT("Whoosh")).Sounds.Add(WhooshSound);
    Palette->Events.FindOrAdd(TEXT("Hit")).Sounds.Add(HitSound);
    Palette->Events.FindOrAdd(TEXT("Brush")).Sounds.Add(HitSound);

    FName Key;
    const FName Swings[] = {TEXT("KnifeSwing"), TEXT("PickaxeSwing")};
    for (FName Event : Swings)
    {
        const auto* Entry = Palette->ResolveEvent(Event, &Key);
        TestEqual(TEXT("Absent tool swing uses the designer Whoosh pool"), Key, FName(TEXT("Whoosh")));
        TestTrue(TEXT("Swing alias preserves the existing pool"), Entry && Entry->Sounds[0] == WhooshSound);
    }
    const FName Hits[] = {TEXT("KnifeHit"), TEXT("PickaxeHit")};
    for (FName Event : Hits)
    {
        const auto* Entry = Palette->ResolveEvent(Event, &Key);
        TestEqual(TEXT("Absent tool hit uses the designer Hit pool"), Key, FName(TEXT("Hit")));
        TestTrue(TEXT("Hit alias preserves the existing pool"), Entry && Entry->Sounds[0] == HitSound);
    }

    auto& Knife = Palette->Events.FindOrAdd(TEXT("KnifeHit"));
    Knife.Sounds.Add(KnifeSound);
    Knife.Volume = .31f; Knife.PitchMin = .87f; Knife.PitchMax = 1.13f;
    const auto* Exact = Palette->ResolveEvent(TEXT("KnifeHit"), &Key);
    TestEqual(TEXT("Explicit tool event overrides its alias"), Key, FName(TEXT("KnifeHit")));
    TestTrue(TEXT("Exact event retains designer sound and playback settings"),
        Exact && Exact->Sounds[0] == KnifeSound && Exact->Volume == .31f && Exact->PitchMin == .87f && Exact->PitchMax == 1.13f);

    Palette->Events.FindOrAdd(TEXT("KnifeHit")).Sounds.Reset();
    const auto* Silent = Palette->ResolveEvent(TEXT("KnifeHit"), &Key);
    TestEqual(TEXT("Explicit empty event keeps its key instead of falling through to Hit"), Key, FName(TEXT("KnifeHit")));
    TestTrue(TEXT("Explicit empty event is handled as silence"), Silent && Silent->Sounds.IsEmpty()
        && Palette->PlayWithParameters(nullptr, TEXT("KnifeHit"), FVector::ZeroVector));
    Palette->Events.FindOrAdd(TEXT("PickaxeSwing")).Sounds.Add(nullptr);
    const auto* NullOnly = Palette->ResolveEvent(TEXT("PickaxeSwing"), &Key);
    TestEqual(TEXT("Explicit null-only pool keeps its key"), Key, FName(TEXT("PickaxeSwing")));
    TestTrue(TEXT("Null-only pool stays handled instead of using Whoosh"), NullOnly && NullOnly->Sounds.Num() == 1
        && !NullOnly->Sounds[0] && Palette->PlayWithParameters(nullptr, TEXT("PickaxeSwing"), FVector::ZeroVector));
    TestNull(TEXT("Unconfigured event has no implicit sound source"), Palette->ResolveEvent(TEXT("Unknown"), &Key));
    TestEqual(TEXT("Unconfigured event reports no resolved key"), Key, NAME_None);
    TestFalse(TEXT("Unconfigured playback is unhandled"), Palette->PlayWithParameters(nullptr, TEXT("Unknown"), FVector::ZeroVector));

    Palette->Events.Remove(TEXT("KnifeHit"));
    TestTrue(TEXT("Deleting the override immediately restores the Hit alias"),
        Palette->ResolveEvent(TEXT("KnifeHit")) == Palette->Events.Find(TEXT("Hit")));
    Palette->Events.FindOrAdd(TEXT("Brush")).Sounds[0] = KnifeSound;
    TestTrue(TEXT("Next resolution observes a designer replacement without a cached selection"),
        Palette->ResolveEvent(TEXT("Brush"))->Sounds[0] == KnifeSound);

    return true;
}
#endif
