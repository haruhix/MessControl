#if WITH_DEV_AUTOMATION_TESTS
#include "MCDataAssets.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Sound/SoundWave.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSoundPalettePlaybackGCTest, "MessControl.Audio.PalettePlaybackGCSafety",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCSoundPalettePlaybackGCTest::RunTest(const FString& Parameters)
{
    if (!TestNotNull(TEXT("Engine for the fixture world"), GEngine)) return false;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Fixture world"), World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->bAllowAudioPlayback = false;

    auto* Palette = NewObject<UMCSoundPalette>();
    Palette->AddToRoot();
    ON_SCOPE_EXIT
    {
        Palette->RemoveFromRoot();
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    };

    // The palette is the only GC owner of the authored sound. No saved bank is touched.
    TWeakObjectPtr<USoundWave> AuthoredSound = NewObject<USoundWave>();
    Palette->Events.FindOrAdd(TEXT("Hit")).Sounds.Add(AuthoredSound.Get());
    for (int32 Cycle = 0; Cycle < 2; ++Cycle)
    {
        for (int32 Playback = 0; Playback < 3; ++Playback)
            TestTrue(TEXT("Configured playback stays handled when world audio is disabled"),
                Palette->PlayWithParameters(World, TEXT("KnifeHit"), FVector::ZeroVector, .7f, .3f));

        TArray<UObject*> RuntimeChildren;
        GetObjectsWithOuter(Palette, RuntimeChildren);
        TestTrue(TEXT("Playback leaves the data asset free of runtime UObject children"), RuntimeChildren.IsEmpty());

        CollectGarbage(RF_NoFlags, true);
        TestTrue(TEXT("Authored sound remains alive through the palette after GC"), AuthoredSound.IsValid());
        FName ResolvedEvent;
        const auto* Entry = Palette->ResolveEvent(TEXT("KnifeHit"), &ResolvedEvent);
        TestEqual(TEXT("Authored routing survives GC"), ResolvedEvent, FName(TEXT("Hit")));
        TestTrue(TEXT("The authored sound pool survives GC unchanged"),
            Entry && Entry->Sounds.Num() == 1 && Entry->Sounds[0].Get() == AuthoredSound.Get());
    }

    // Editor GC cannot certify the startup disregard-for-GC assumptions of a packaged game.
    return true;
}
#endif
