#include "MCEventSFX.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventSFXPolicyTest,"MessControl.Audio.Composer.Policy",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventSFXPolicyTest::RunTest(const FString&)
{
    FString Error;FMCEventSFXSlot Slot;
    TestTrue(TEXT("An absent/empty audio slot preserves original audio"),Slot.UsesNativeAudio());
    TestFalse(TEXT("Default audio has no custom layers"),Slot.HasCustomLayers());
    TestTrue(TEXT("Default audio does not require a replacement adapter"),MCEventSFX::ValidateNativePolicy(Slot,false,Error));
    FMCEventSFXLayer Layer;Layer.Sound=FSoftObjectPath(TEXT("/Game/Audio/S_Hit.S_Hit"));Slot.Layers.Add(Layer);
    Slot.Mode=EMCEventSFXMode::Add;
    TestTrue(TEXT("Add retains original audio"),Slot.UsesNativeAudio());
    TestTrue(TEXT("Add has custom layers"),Slot.HasCustomLayers());
    TestTrue(TEXT("Add does not need to suppress originals"),MCEventSFX::ValidateNativePolicy(Slot,false,Error));
    Slot.Mode=EMCEventSFXMode::Replace;
    TestFalse(TEXT("Replace suppresses original AUDIO only"),Slot.UsesNativeAudio());
    TestFalse(TEXT("An unmapped native source cannot claim replacement"),MCEventSFX::ValidateNativePolicy(Slot,false,Error));
    TestTrue(TEXT("A mapped native audio gate permits replacement"),MCEventSFX::ValidateNativePolicy(Slot,true,Error));
    Slot.Mode=EMCEventSFXMode::Disabled;
    TestFalse(TEXT("Disabled has no custom audio"),Slot.HasCustomLayers());
    TestFalse(TEXT("Disabled cannot mute an unmapped native source"),MCEventSFX::ValidateNativePolicy(Slot,false,Error));
    TestTrue(TEXT("Explicit mapped silence is valid"),MCEventSFX::ValidateNativePolicy(Slot,true,Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventSFXIdentityTest,"MessControl.Audio.Composer.IdentityAndBounds",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventSFXIdentityTest::RunTest(const FString&)
{
    FString Error;FMCEventSFXSlot Slot;Slot.Mode=EMCEventSFXMode::Add;
    FMCEventSFXLayer A;A.Name=TEXT("Impact");A.Sound=FSoftObjectPath(TEXT("/Game/Audio/S_Hit.S_Hit"));
    A.GroupID=FGuid::NewGuid();FMCEventSFXLayer B=A;B.ID=FGuid::NewGuid();B.Name=TEXT("Accent");
    B.Sound=FSoftObjectPath(TEXT("/Game/Audio/S_Whoosh.S_Whoosh"));Slot.Layers={A,B};
    TestTrue(TEXT("Several independent sounds in a shared simultaneous group are valid"),Slot.Validate(Error));
    Slot.Layers[1].ID=A.ID;
    TestFalse(TEXT("Duplicating a layer must assign a fresh identity"),Slot.Validate(Error));
    Slot.Layers[1].ID=FGuid::NewGuid();Slot.Layers[1].DelaySeconds=Slot.MaxDurationSeconds;
    TestFalse(TEXT("A delayed layer cannot start after its event horizon"),Slot.Validate(Error));
    Slot.Layers[1].DelaySeconds=0;Slot.Layers[1].Space=EMCEventSFXSpace::SourceSocket;
    TestFalse(TEXT("Socket mode cannot silently substitute an unnamed binding"),Slot.Validate(Error));
    Slot.Layers[1].Socket=TEXT("AudioGrip");
    TestTrue(TEXT("An explicit socket can be resolved by the real source provider"),Slot.Validate(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventSFXResidentTest,"MessControl.Audio.Composer.ResidentOnly",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventSFXResidentTest::RunTest(const FString&)
{
    FString Error;FMCPreparedEventSFX Prepared;FMCEventSFXSlot Slot;Slot.Mode=EMCEventSFXMode::Add;
    FMCEventSFXLayer Layer;Layer.Sound=FSoftObjectPath(TEXT("/Game/Audio/ComposerTests_Missing_Source.ComposerTests_Missing_Source"));
    Slot.Layers.Add(Layer);
    TestFalse(TEXT("Playback preparation never silently loads an unavailable source"),MCEventSFX::Prepare(Slot,false,Prepared,Error));
    TestTrue(TEXT("The failure identifies a residency requirement"),Error.Contains(TEXT("not resident")));
    TestTrue(TEXT("A failed candidate does not replace the previous prepared snapshot"),Prepared.Sounds.IsEmpty());
    Slot.Mode=EMCEventSFXMode::UseDefault;
    TestTrue(TEXT("Default policy does not load inactive authoring references"),MCEventSFX::Prepare(Slot,false,Prepared,Error));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventSFXSharedGroupsTest,"MessControl.Audio.Composer.SharedGroupPolicy",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventSFXSharedGroupsTest::RunTest(const FString&)
{
    FString Error;FMCPreparedEventSFX Prepared;const FGuid A=FGuid::NewGuid(),B=FGuid::NewGuid();
    FMCEventSFXLayer Layer;Layer.GroupID=A;Prepared.Slot.Layers.Add(Layer);
    TestTrue(TEXT("The same shared event GUIDs compile into independent audio policies"),MCEventSFX::CompileSharedGroupPolicy({A,B},{B},{A},Prepared,Error));
    TestTrue(TEXT("Disabled and muted are separate policies"),Prepared.DisabledGroupIDs.Contains(B) && Prepared.MutedGroupIDs.Contains(A));
    const FGuid Unknown=FGuid::NewGuid();
    TestFalse(TEXT("Another event's group cannot silently enter this audio snapshot"),MCEventSFX::CompileSharedGroupPolicy({A,B},{Unknown},{},Prepared,Error));
    TestTrue(TEXT("Failed compile retains the previous shared policy atomically"),Prepared.DisabledGroupIDs.Contains(B) && Prepared.MutedGroupIDs.Contains(A));
    TestFalse(TEXT("Shared group identity collisions are rejected"),MCEventSFX::CompileSharedGroupPolicy({A,A},{},{},Prepared,Error));
    Prepared.Slot.Layers[0].GroupID.Invalidate();
    TestTrue(TEXT("Old ungrouped layers remain in the implicit Default group"),MCEventSFX::CompileSharedGroupPolicy({}, {}, {},Prepared,Error));
    return true;
}
#endif
