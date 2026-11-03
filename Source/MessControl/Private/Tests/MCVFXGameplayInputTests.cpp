#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCVFXGameplayInputs.h"
#include "MCVFXPresentationComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXReadOnlyInputTypes,
    "MessControl.VFX.Context.ReadOnlyInputTypes",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXReadOnlyInputTypes::RunTest(const FString&)
{
    auto* Source=NewObject<UNiagaraSystem>();FString Error;
    Source->GetExposedParameters().SetParameterValue(30.f,
        FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.BodyRadius")),true);
    TestTrue(TEXT("Exact native body input type is accepted"),MCVFXGameplayInputs::ValidateSystem(Source,Error));
    TestTrue(TEXT("BodyRadius is reserved"),MCVFXGameplayInputs::IsReadOnly(TEXT("User.BodyRadius")));
    TestTrue(TEXT("Same FName under another display case remains reserved"),MCVFXGameplayInputs::IsReadOnly(TEXT("user.bodyradius")));
    TestTrue(TEXT("Typed native namespace is reserved"),MCVFXGameplayInputs::IsReadOnly(TEXT("user.native.CueAge")));
    TestFalse(TEXT("Similar artist name is not accidentally reserved"),MCVFXGameplayInputs::IsReadOnly(TEXT("User.BodyRadiusGlow")));
    TestFalse(TEXT("Existing artist strength remains editable"),MCVFXGameplayInputs::IsReadOnly(TEXT("User.Strength")));
    auto* Wrong=NewObject<UNiagaraSystem>();
    Wrong->GetExposedParameters().SetParameterValue(FVector3f(1,2,3),
        FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(),TEXT("User.ScreenOrientationLocal")),true);
    TestFalse(TEXT("Quaternion cannot silently masquerade as a vector"),MCVFXGameplayInputs::ValidateSystem(Wrong,Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXReadOnlyInputOverrides,
    "MessControl.VFX.Context.ReadOnlyInputDiscoveryAndOverrides",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXReadOnlyInputOverrides::RunTest(const FString&)
{
    auto* Source=NewObject<UNiagaraSystem>();FString Error;
    Source->GetExposedParameters().SetParameterValue(30.f,
        FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.BodyRadius")),true);
    Source->GetExposedParameters().SetParameterValue(2.f,
        FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.Strength")),true);
    auto* Recipe=NewObject<UMCVFXComposition>();FMCVFXCompositionLayer Layer;Layer.System=Source;Recipe->Layers.Add(Layer);
    if(!TestTrue(TEXT("Discover supported inputs"),Recipe->DiscoverParameters(0,Error))) {AddError(Error);return false;}
    TestEqual(TEXT("Only the actual artist parameter is exposed"),Recipe->Layers[0].Floats.Num(),1);
    TestEqual(TEXT("Artist parameter identity is preserved"),Recipe->Layers[0].Floats[0].Name,FName(TEXT("User.Strength")));
    FMCVFXFloatOverride Override;Override.Name=TEXT("User.BodyRadius");Override.bOverride=true;Override.Value=999.f;
    Recipe->Layers[0].Floats.Add(Override);
    TestFalse(TEXT("Persisted override cannot falsify body context"),Recipe->Validate(Error));
    TestFalse(TEXT("Direct layer preparation also rejects reserved overrides"),
        UMCVFXComposition::ValidateParameters(Recipe->Layers[0],Source,Error));
    Recipe->Layers[0].Floats.Last().Name=TEXT("user.bodyradius");
    TestFalse(TEXT("A display-case alias cannot override the same native slot"),
        UMCVFXComposition::ValidateParameters(Recipe->Layers[0],Source,Error));
    TestEqual(TEXT("Validation does not mutate the source default"),Source->GetExposedParameters().GetParameterValue<float>(
        FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.BodyRadius"))),30.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXLocalStateNativeModes,
    "MessControl.VFX.Context.LocalStateUsesPreparedNativeModes",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXLocalStateNativeModes::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::EditorPreview,false);
    GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
    struct FCleanup {UWorld* W;~FCleanup(){GEngine->DestroyWorldContext(W);W->DestroyWorld(false);}} Cleanup{World};
    auto* Owner=World->SpawnActor<AActor>();auto* Component=NewObject<UMCVFXPresentationComponent>(Owner);
    Owner->AddInstanceComponent(Component);Component->RegisterComponent();
    const FName ID(TEXT("EditorOnly.LocalState")),Event(TEXT("Durability"));FString Error;
    auto* Profile=NewObject<UMCPerkVFXProfile>();
    TestTrue(TEXT("Explicit default event prepares before playback"),Component->PrepareSourceProfile(ID,{Event},Profile,Error,false));
    FMCPerkVFXGameplayValues Values;Values.LocalStateAgeSeconds=120.f;
    TestTrue(TEXT("Default state is a successful native gate, not a fake player"),
        Component->UpdateLocalSourceState(ID,Event,true,FTransform::Identity,Values,nullptr,Error));
    TestEqual(TEXT("Default does not allocate a local state"),Component->GetLocalSourceStateCount(),0);
    TestFalse(TEXT("Undeclared state cannot load or fabricate a binding"),
        Component->UpdateLocalSourceState(ID,TEXT("Invented"),true,FTransform::Identity,Values,nullptr,Error));
    FMCPerkVFXSlot Disabled;Disabled.Mode=EMCPerkVFXSlotMode::Disabled;Profile->Slots.Add(Event,Disabled);
    TestTrue(TEXT("Disabled snapshot prepares"),Component->PrepareSourceProfile(ID,{Event},Profile,Error,false));
    EMCPerkVFXSlotMode Mode=EMCPerkVFXSlotMode::UseDefault;
    TestTrue(TEXT("Gate reads an immutable prepared event"),Component->GetPreparedEventMode(ID,Event,Mode));
    TestEqual(TEXT("Disabled state remains disabled"),uint8(Mode),uint8(EMCPerkVFXSlotMode::Disabled));
    TestTrue(TEXT("Disabled state is a successful no-op"),Component->UpdateLocalSourceState(ID,Event,true,FTransform::Identity,Values,nullptr,Error));
    TestTrue(TEXT("Explicit state stop is idempotent"),Component->UpdateLocalSourceState(ID,Event,false,FTransform::Identity,Values,nullptr,Error));
    return true;
}
#endif
