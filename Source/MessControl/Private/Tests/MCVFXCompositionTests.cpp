#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCVFXComposition.h"
#include "MCVFXCompositionActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "NiagaraSystem.h"
#include "NiagaraParameterStore.h"
#include "UObject/Package.h"

namespace MCVFXCompositionTests
{
    UNiagaraSystem* MakeSource()
    {
        auto* System=NewObject<UNiagaraSystem>();
        System->GetExposedParameters().SetParameterValue(2.f,FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.Width")),true);
        System->GetExposedParameters().SetParameterValue(FLinearColor(.2f,.8f,.7f,.5f),FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(),TEXT("User.Tint")),true);
        return System;
    }
    UMCVFXComposition* MakeRecipe(UNiagaraSystem* System)
    {
        auto* Recipe=NewObject<UMCVFXComposition>();
        FMCVFXCompositionLayer Layer;Layer.System=System;Recipe->Layers.Add(Layer);
        return Recipe;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXCompositionValidationTest,"MessControl.VFX.Composer.Validation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXCompositionValidationTest::RunTest(const FString&)
{
    using namespace MCVFXCompositionTests;
    auto* Recipe=MakeRecipe(MakeSource());FString Error;
    TestTrue(TEXT("Valid simple recipe"),Recipe->Validate(Error));
    Recipe->Layers[0].StartSeconds=Recipe->DurationSeconds;
    TestFalse(TEXT("An effect cannot start after the emission deadline"),Recipe->Validate(Error));
    Recipe->Layers[0].StartSeconds=0;
    Recipe->Layers[0].Transform.SetScale3D(FVector(0,1,1));
    TestFalse(TEXT("Invisible zero-scale transform rejected"),Recipe->Validate(Error));
    Recipe->Layers[0].Transform=FTransform::Identity;
    Recipe->Layers[0].Socket=TEXT("missing_socket");
    TestFalse(TEXT("Sockets require Subject binding"),Recipe->Validate(Error));
    Recipe->Layers[0].Socket=NAME_None;
    Recipe->Layers[0].bEnabled=false;
    TestFalse(TEXT("Silent all-disabled composition rejected"),Recipe->Validate(Error));
    Recipe->Layers[0].bEnabled=true;
    FMCVFXFloatOverride Float;Float.Name=TEXT("User.Width");Float.Value=4;Float.bOverride=true;
    Recipe->Layers[0].Floats.Add(Float);
    FMCVFXColorOverride Color;Color.Name=Float.Name;Color.bOverride=true;
    Recipe->Layers[0].Colors.Add(Color);
    TestFalse(TEXT("Same User name cannot be driven by two types"),Recipe->Validate(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXCompositionParameterTest,"MessControl.VFX.Composer.ParameterIsolation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXCompositionParameterTest::RunTest(const FString&)
{
    using namespace MCVFXCompositionTests;
    auto* Source=MakeSource();auto* Recipe=MakeRecipe(Source);FString Error;
    FMCVFXFloatOverride Existing;Existing.Name=TEXT("User.Width");Existing.Value=9;Existing.bOverride=true;
    Recipe->Layers[0].Floats.Add(Existing);
    TestTrue(TEXT("Discover reads supported defaults"),Recipe->DiscoverParameters(0,Error));
    TestEqual(TEXT("Discovery keeps an explicitly authored override"),Recipe->Layers[0].Floats[0].Value,9.f);
    TestEqual(TEXT("Source value remains unchanged"),Source->GetExposedParameters().GetParameterValue<float>(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.Width"))),2.f);
    TestEqual(TEXT("Color default discovered"),Recipe->Layers[0].Colors.Num(),1);
    TestFalse(TEXT("Discovered defaults do not silently activate overrides"),Recipe->Layers[0].Colors[0].bOverride);
    TestTrue(TEXT("Matching float override accepted"),UMCVFXComposition::ValidateParameters(Recipe->Layers[0],Source,Error));
    const FMCVFXCompositionLayer SecondLayer=Recipe->Layers[0];
    Recipe->Layers.Add(SecondLayer);Recipe->Layers[1].Floats[0].Value=30.f;
    TestTrue(TEXT("Identical User names in separate layers stay independent"),Recipe->Validate(Error));
    TestEqual(TEXT("Second layer does not change first"),Recipe->Layers[0].Floats[0].Value,9.f);
    Recipe->Layers[0].Floats[0].Name=TEXT("User.Tint");
    TestFalse(TEXT("Existing name with wrong type rejected"),UMCVFXComposition::ValidateParameters(Recipe->Layers[0],Source,Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXCompositionScheduleTest,"MessControl.VFX.Composer.ScheduleAndSnapshot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXCompositionScheduleTest::RunTest(const FString&)
{
    using namespace MCVFXCompositionTests;
    UWorld* World=UWorld::CreateWorld(EWorldType::EditorPreview,false);
    if(!TestNotNull(TEXT("Owned preview fixture world"),World)) return false;
    UPackage* OwnedPackage=World->GetOutermost();
    GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
    auto* Actor=World->SpawnActor<AMCVFXCompositionActor>();
    if(!Actor) {GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return false;}
    Actor->SetExternallyTicked(true);
    auto* Recipe=MakeRecipe(MakeSource());Recipe->Layers[0].StartSeconds=1.f;
    FString Error;
    TestTrue(TEXT("Explicitly prepare before Play"),Actor->PrepareComposition(Recipe,Error));
    TestTrue(TEXT("Starts schedule without prematurely instantiating delayed source"),Actor->PlayPrepared());
    TestEqual(TEXT("No delayed layer component at time zero"),Actor->GetLiveLayerCount(),0);
    Recipe->Layers.Reset();
    Actor->PauseComposition(true);Actor->AdvanceComposition(.5f);
    TestEqual(TEXT("Pause preserves schedule time"),Actor->GetPlaybackTime(),0.f);
    Actor->PauseComposition(false);Actor->AdvanceComposition(.5f);
    TestEqual(TEXT("Resume advances the prepared snapshot"),Actor->GetPlaybackTime(),.5f);
    // Skip the entire emission window. An old burst must not flash after a long frame.
    Actor->AdvanceComposition(3.f);
    TestFalse(TEXT("Completed empty tail releases running ownership"),Actor->IsRunning());
    TestEqual(TEXT("Skipped emission was never instantiated"),Actor->GetLiveLayerCount(),0);
    Actor->StopComposition(true);Actor->Destroy();
    GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    if(IsValid(OwnedPackage) && OwnedPackage->GetName().StartsWith(TEXT("/Temp/"))) OwnedPackage->SetDirtyFlag(false);
    return true;
}
#endif
