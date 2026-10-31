#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCPerkVFXCompositionPlayer.h"
#include "MCPreloadManifest.h"
#include "MCRocketSkillAction.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"
#include "UObject/Package.h"

namespace MCPerkCompositionTests
{
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::EditorPreview,false);
    AMCRocketSkillAction* Action=nullptr;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
        Action=World->SpawnActor<AMCRocketSkillAction>();
        // This fixture checks data/lifecycle contracts, not art/readiness.
        // Only its own uncommitted instance is changed; CDOs are untouched.
        Action->StartVFX.Reset();Action->LoopVFX.Reset();Action->ImpactVFX.Reset();Action->EndVFX.Reset();Action->WarningVFX.Reset();
    }
    ~FWorld()
    {
        if(World) {GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    }
};
UNiagaraSystem* ScalarSource()
{
    auto* NS=NewObject<UNiagaraSystem>();
    NS->GetExposedParameters().SetParameterValue(2.f,FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.Strength")),true);
    return NS;
}
FMCPerkVFXSlot Slot(UMCVFXComposition* Recipe)
{FMCPerkVFXSlot Result;Result.Mode=EMCPerkVFXSlotMode::Composition;Result.Composition=Recipe;return Result;}
UMCVFXComposition* Recipe(UNiagaraSystem* A,UNiagaraSystem* B=nullptr)
{
    auto* Result=NewObject<UMCVFXComposition>();
    FMCVFXCompositionLayer Layer;Layer.Name=TEXT("First");Layer.System=A;Result->Layers.Add(Layer);
    if(B) {Layer.Name=TEXT("Second");Layer.System=B;Layer.StartSeconds=.2f;Result->Layers.Add(Layer);}
    return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPerkCompositionSnapshot,
    "MessControl.VFX.PerkComposition.ImmutableIndependentSources",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPerkCompositionSnapshot::RunTest(const FString&)
{
    using namespace MCPerkCompositionTests;
    TestEqual(TEXT("Existing default enum serialization stays zero"),uint8(EMCPerkVFXSlotMode::UseDefault),uint8(0));
    TestEqual(TEXT("Existing single-System serialization stays one"),uint8(EMCPerkVFXSlotMode::Replace),uint8(1));
    TestEqual(TEXT("Existing disabled serialization stays two"),uint8(EMCPerkVFXSlotMode::Disabled),uint8(2));
    TestEqual(TEXT("Composition mode is appended"),uint8(EMCPerkVFXSlotMode::Composition),uint8(3));
    FWorld T;auto* First=ScalarSource();auto* Second=NewObject<UNiagaraSystem>();
    // The same User name deliberately has a different type/default in another
    // independent NS. This must work without namespacing or merging sources.
    Second->GetExposedParameters().SetParameterValue(FLinearColor::Green,
        FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(),TEXT("User.Strength")),true);
    auto* Authored=Recipe(First,Second);auto* Profile=NewObject<UMCPerkVFXProfile>();
    Profile->Slots.Add(TEXT("Impact"),Slot(Authored));FString Error;
    if(!TestTrue(TEXT("Different independent defaults/types prepare without graph merge"),T.Action->PrepareVFXProfile(Profile,Error)))
    {AddError(Error);return false;}
    TestNull(TEXT("Composition does not masquerade as a generated single NS"),T.Action->GetPreparedVFXSystem(TEXT("Impact")));
    TArray<UNiagaraSystem*> Ready;T.Action->GetPreparedVFXSystems(TEXT("Impact"),Ready);
    TestTrue(TEXT("Readiness receives BOTH original Niagara sources"),Ready.Num()==2 && Ready.Contains(First) && Ready.Contains(Second));
    auto* Prepared=T.Action->GetPreparedVFXComposition(TEXT("Impact"));
    TestTrue(TEXT("Runtime snapshot is native composition, independent of mutable authoring"),Prepared && Prepared!=Authored && Prepared->GetClass()==UMCVFXComposition::StaticClass());
    Authored->Layers[0].Transform.SetLocation(FVector(900,0,0));Authored->Layers[1].StartSeconds=1.5f;
    TestTrue(TEXT("Authored transform edit cannot move an accepted action's effect"),Prepared->Layers[0].Transform.GetLocation().IsNearlyZero());
    TestEqual(TEXT("Accepted layer scheduling is immutable"),Prepared->Layers[1].StartSeconds,.2f);
    auto& Outer=Profile->Slots[TEXT("Impact")];
    FMCVFXFloatOverride Wrong;Wrong.Name=TEXT("User.Strength");Wrong.bOverride=true;Wrong.Value=7;Outer.Floats.Add(Wrong);
    TestFalse(TEXT("A global override with conflicting per-System types fails before commit"),T.Action->PrepareVFXProfile(Profile,Error));
    TestTrue(TEXT("Failed replacement preserves the entire earlier valid snapshot"),T.Action->GetPreparedVFXComposition(TEXT("Impact"))==Prepared);
    TestEqual(TEXT("First original source value was not rewritten"),First->GetExposedParameters().GetParameterValue<float>(
        FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.Strength"))),2.f);
    TestEqual(TEXT("Second original source type/value was not rewritten"),Second->GetExposedParameters().GetParameterValue<FLinearColor>(
        FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(),TEXT("User.Strength"))),FLinearColor::Green);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPerkCompositionCancellation,
    "MessControl.VFX.PerkComposition.DelayedCancellationAndSubjectValidation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPerkCompositionCancellation::RunTest(const FString&)
{
    using namespace MCPerkCompositionTests;
    FWorld T;auto* Source=ScalarSource();auto* Authored=Recipe(Source);
    Authored->Layers[0].StartSeconds=.8f;Authored->TailSeconds=.2f;
    FMCPreparedPerkVFXComposition Data;Data.Snapshot=Authored;Data.Systems.Add(Source);Data.Outer=Slot(Authored);
    auto* Player=T.World->SpawnActor<AMCPerkVFXCompositionPlayer>();FString Error;
    if(!TestTrue(TEXT("Prepared delayed playback accepts original source without loading it on a cue"),
        Player->StartPrepared(Data,FTransform::Identity,nullptr,nullptr,false,FMCPerkVFXGameplayValues(),Error))) return false;
    TestEqual(TEXT("Delayed effect is pending, with no prematurely visible layer"),Player->GetPendingLayerCount(),1);
    TestEqual(TEXT("Nothing is active before the real scheduled start"),Player->GetLiveLayerCount(),0);
    Player->Tick(.25f);
    TestEqual(TEXT("Advancing native player before delay still does not spawn"),Player->GetPendingLayerCount(),1);
    Player->StopEmission(false);
    TestEqual(TEXT("Cancellation removes all future scheduled starts"),Player->GetPendingLayerCount(),0);
    Player->Tick(1.f);
    TestFalse(TEXT("Canceled delayed cue cannot flash later or leave a running player"),Player->IsRunning());
    Data.Snapshot=Recipe(Source);Data.Snapshot->Layers[0].Binding=EMCVFXCompositionBinding::Subject;
    Data.Snapshot->Layers[0].Socket=TEXT("MissingHandSocket");
    auto* MissingSubject=T.World->SpawnActor<AMCPerkVFXCompositionPlayer>();
    TestFalse(TEXT("Missing subject/socket rejects playback rather than silently moving the effect to origin"),
        MissingSubject->StartPrepared(Data,FTransform::Identity,nullptr,nullptr,false,FMCPerkVFXGameplayValues(),Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPerkCompositionManifest,
    "MessControl.VFX.PerkComposition.ManifestLoadWaves",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPerkCompositionManifest::RunTest(const FString&)
{
    auto* Profile=NewObject<UMCPerkVFXProfile>();
    const FSoftObjectPath CompositionPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/Compositions/DA_Example.DA_Example"));
    FMCPerkVFXSlot Slot;Slot.Mode=EMCPerkVFXSlotMode::Composition;Slot.Composition=CompositionPath;
    Profile->Slots.Add(TEXT("Impact"),Slot);TArray<FSoftObjectPath> Wave;
    FMCPreloadManifest::CollectSoftReferences(Profile,Wave);
    TestTrue(TEXT("Reflected profile map discovers runtime composition in the first load wave"),Wave.Contains(CompositionPath));
    auto* Composition=NewObject<UMCVFXComposition>();
    const FSoftObjectPath ArbitraryNS(TEXT("/Game/SomeImportedPack/Effects/FreeMagicHit2.FreeMagicHit2"));
    FMCVFXCompositionLayer Layer;Layer.System=ArbitraryNS;Composition->Layers.Add(Layer);Wave.Reset();
    FMCPreloadManifest::CollectSoftReferences(Composition,Wave);
    TestTrue(TEXT("Loaded native composition discovers independent NS outside the curated exporter folders"),Wave.Contains(ArbitraryNS));
    TestTrue(TEXT("Manifest traversal does not first-load fixtures or depend on Editor recipe payloads"),!CompositionPath.ResolveObject() && !ArbitraryNS.ResolveObject());
    return true;
}
#endif
