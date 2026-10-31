#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCPerkVFXProfile.h"
#include "MCRocketSkillAction.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "NiagaraSystem.h"
#include "NiagaraParameterStore.h"
#include "UObject/Package.h"

namespace MCPerkVFXProfileTests
{
UNiagaraSystem* Source()
{
    auto* System=NewObject<UNiagaraSystem>();
    System->GetExposedParameters().SetParameterValue(2.f,FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.Width")),true);
    System->GetExposedParameters().SetParameterValue(FLinearColor(.2f,.6f,.9f,1.f),FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(),TEXT("User.Tint")),true);
    return System;
}
FMCPerkVFXSlot Replacement(UNiagaraSystem* System)
{
    FMCPerkVFXSlot Slot;Slot.Mode=EMCPerkVFXSlotMode::Replace;Slot.System=System;return Slot;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPerkVFXProfileValidationTest,"MessControl.VFX.PerkProfile.Validation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPerkVFXProfileValidationTest::RunTest(const FString&)
{
    auto* Profile=NewObject<UMCPerkVFXProfile>();FString Error;
    TestTrue(TEXT("Empty profile keeps all action defaults"),Profile->Validate(Error));
    Profile->Slots.Add(TEXT("ImaginaryEvent"),FMCPerkVFXSlot());
    TestFalse(TEXT("Unknown event cannot silently create a gameplay trigger"),Profile->Validate(Error));
    Profile->Slots.Reset();
    FMCPerkVFXSlot Disabled;Disabled.Mode=EMCPerkVFXSlotMode::Disabled;
    Profile->Slots.Add(TEXT("Impact"),Disabled);
    TestTrue(TEXT("Explicitly disabling all art needs no replacement asset"),Profile->Validate(Error));
    auto& Slot=Profile->Slots[TEXT("Impact")];Slot.Mode=EMCPerkVFXSlotMode::Replace;
    TestFalse(TEXT("Replacement with no resource is an authoring error"),Profile->Validate(Error));
    Slot.System=MCPerkVFXProfileTests::Source();
    Slot.Transform.SetScale3D(FVector(0,1,1));
    TestFalse(TEXT("Invisible zero scale rejected"),Profile->Validate(Error));
    Slot.Transform=FTransform::Identity;
    FMCVFXFloatOverride Float;Float.bOverride=true;Float.Name=TEXT("User.Width");Float.Value=8;Slot.Floats.Add(Float);
    FMCVFXColorOverride Color;Color.bOverride=true;Color.Name=Float.Name;Slot.Colors.Add(Color);
    TestFalse(TEXT("Same parameter name cannot be driven as two types"),Profile->Validate(Error));
    Slot.Colors.Reset();
    TestTrue(TEXT("Valid typed slot structure accepted"),Profile->Validate(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPerkVFXProfileDiscoveryTest,"MessControl.VFX.PerkProfile.DiscoveryIsolation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPerkVFXProfileDiscoveryTest::RunTest(const FString&)
{
    auto* System=MCPerkVFXProfileTests::Source();auto* Profile=NewObject<UMCPerkVFXProfile>();
    auto Slot=MCPerkVFXProfileTests::Replacement(System);
    FMCVFXFloatOverride Float;Float.Name=TEXT("User.Width");Float.bOverride=true;Float.Value=8;Slot.Floats.Add(Float);
    Profile->Slots.Add(TEXT("Impact"),Slot);FString Error;
    TestTrue(TEXT("Read actual exposed parameter defaults"),Profile->DiscoverParameters(TEXT("Impact"),Error));
    const auto& Found=Profile->Slots[TEXT("Impact")];
    TestEqual(TEXT("Existing authored float retained"),Found.Floats[0].Value,8.f);
    TestEqual(TEXT("Color discovered"),Found.Colors.Num(),1);
    TestFalse(TEXT("Discovery does not enable new overrides"),Found.Colors[0].bOverride);
    TestEqual(TEXT("Source remains unchanged"),System->GetExposedParameters().GetParameterValue<float>(
        FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.Width"))),2.f);
    TestFalse(TEXT("Discovery of unknown phase rejected"),Profile->DiscoverParameters(TEXT("NotAnEvent"),Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPerkVFXProfileSnapshotTest,"MessControl.VFX.PerkProfile.ActionSnapshot",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPerkVFXProfileSnapshotTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::EditorPreview,false);
    if(!TestNotNull(TEXT("Owned isolated fixture world"),World)) return false;
    UPackage* OwnedPackage=World->GetOutermost();
    GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
    auto* Action=World->SpawnActor<AMCRocketSkillAction>();
    if(!Action) {GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return false;}
    // Only this uncommitted actor instance is changed. No CDO/art loading needed for the test.
    Action->StartVFX.Reset();Action->LoopVFX.Reset();Action->ImpactVFX.Reset();Action->EndVFX.Reset();Action->WarningVFX.Reset();
    auto* First=MCPerkVFXProfileTests::Source();auto* Second=MCPerkVFXProfileTests::Source();
    auto* Profile=NewObject<UMCPerkVFXProfile>();
    Profile->Slots.Add(TEXT("Impact"),MCPerkVFXProfileTests::Replacement(First));FString Error;
    TestTrue(TEXT("Prepare replacement before activation"),Action->PrepareVFXProfile(Profile,Error));
    TestTrue(TEXT("Prepared instance retains source"),Action->GetPreparedVFXSystem(TEXT("Impact"))==First);
    Profile->Slots[TEXT("Impact")].System=Second;
    TestTrue(TEXT("Editing authoring profile does not mutate a prepared action"),Action->GetPreparedVFXSystem(TEXT("Impact"))==First);
    TestTrue(TEXT("An explicit new preparation picks up the edited profile"),Action->PrepareVFXProfile(Profile,Error));
    TestTrue(TEXT("New snapshot now selects second source"),Action->GetPreparedVFXSystem(TEXT("Impact"))==Second);
    auto& Slot=Profile->Slots[TEXT("Impact")];
    FMCVFXFloatOverride Wrong;Wrong.bOverride=true;Wrong.Name=TEXT("User.Tint");Wrong.Value=1;Slot.Floats.Add(Wrong);
    TestFalse(TEXT("Exact name with wrong type rejected before cues"),Action->PrepareVFXProfile(Profile,Error));
    TestTrue(TEXT("Failed preparation is atomic and retains previous valid snapshot"),Action->GetPreparedVFXSystem(TEXT("Impact"))==Second);
    Slot=FMCPerkVFXSlot();Slot.Mode=EMCPerkVFXSlotMode::Disabled;
    TestTrue(TEXT("Explicit disabled slot prepares"),Action->PrepareVFXProfile(Profile,Error));
    TestNull(TEXT("Disabled no longer retains an active source for this stage"),Action->GetPreparedVFXSystem(TEXT("Impact")));
    TestTrue(TEXT("Absent profile restores this actor's defaults"),Action->PrepareVFXProfile(nullptr,Error));
    TestNull(TEXT("The actor's empty default was not replaced globally"),Action->GetPreparedVFXSystem(TEXT("Impact")));
    Action->Destroy();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    if(IsValid(OwnedPackage) && OwnedPackage->GetName().StartsWith(TEXT("/Temp/"))) OwnedPackage->SetDirtyFlag(false);
    return true;
}
#endif
