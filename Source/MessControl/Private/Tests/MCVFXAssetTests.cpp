#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "NiagaraSystem.h"
#include "NiagaraMeshRendererProperties.h"
#include "Stateless/NiagaraStatelessEmitter.h"
#include "Stateless/Modules/NiagaraStatelessModule_InitializeParticle.h"
#include "Stateless/Modules/NiagaraStatelessModule_AddVelocity.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXMeshSlotsTest,"MessControl.VFX.SavedParticleMeshSlots",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXMeshSlotsTest::RunTest(const FString&)
{
    for(const TCHAR* Name:{TEXT("NS_BrushFoam"),TEXT("NS_SprayMist"),TEXT("NS_IceShatter")}) {
        const FString Path=FString::Printf(TEXT("/Game/Gameplay/VFX/%s.%s"),Name,Name);
        auto* System=LoadObject<UNiagaraSystem>(nullptr,*Path);
        if(!TestNotNull(Path,System)) continue;
        int32 MeshRenderers=0;
        for(int32 I=0;I<System->GetNumEmitters();++I) {
            auto* Emitter=System->GetEmitterHandle(I).GetStatelessEmitter();if(!Emitter) continue;
            for(auto* Base:Emitter->GetRenderers()) if(auto* Renderer=Cast<UNiagaraMeshRendererProperties>(Base)) {
                ++MeshRenderers;
                if(TestTrue(Path+TEXT(" has mesh slot zero"),Renderer->Meshes.Num()>0))
                    TestNotNull(Path+TEXT(" default particle mesh is renderable"),Renderer->Meshes[0].Mesh.Get());
            }
        }
        TestTrue(Path+TEXT(" contains a mesh particle renderer"),MeshRenderers>0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSprayAssetTest,"MessControl.VFX.SprayContinuousCone",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSprayAssetTest::RunTest(const FString&)
{
    auto* System=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Gameplay/VFX/NS_SprayMist.NS_SprayMist"));
    if(!TestNotNull(TEXT("saved treatment spray"),System)) return true;
    if(!TestTrue(TEXT("spray emitter exists"),System->GetNumEmitters()>0)) return true;
    auto* Emitter=System->GetEmitterHandle(0).GetStatelessEmitter();
    if(!TestNotNull(TEXT("spray uses the lightweight emitter"),Emitter)) return true;
    bool ContinuousRate=false;
    for(int32 I=0;I<Emitter->GetNumSpawnInfos();++I) {
        const auto* Spawn=Emitter->GetSpawnInfoByIndex(I);
        ContinuousRate|=Spawn->bEnabled && Spawn->Type==ENiagaraStatelessSpawnInfoType::Rate
            && Spawn->Rate.CalculateRange().Min>0 && !Spawn->bLoopCountLimitEnabled;
    }
    TestTrue(TEXT("spray emits throughout held treatment"),ContinuousRate);
    const auto* Init=Emitter->GetModule<UNiagaraStatelessModule_InitializeParticle>();
    if(TestNotNull(TEXT("particle initialization"),Init)) {
        const auto Lifetime=Init->LifetimeDistribution.CalculateRange();
        const auto Scale=Init->MeshScaleDistribution.CalculateRange();
        TestTrue(TEXT("visible finite droplet lifetime"),Lifetime.Min>0 && Lifetime.Max<=.5f);
        TestTrue(TEXT("nonzero droplet scale"),Scale.Min.GetMin()>0 && Scale.Max.GetMax()<.1f);
    }
    const auto* Velocity=Emitter->GetModule<UNiagaraStatelessModule_AddVelocity>();
    if(TestNotNull(TEXT("spray cone velocity"),Velocity)) {
        TestTrue(TEXT("enabled outward cone"),Velocity->IsModuleEnabled() && Velocity->VelocityType==ENSM_VelocityType::InCone
            && Velocity->ConeVelocityDistribution.CalculateRange().Min>0 && Velocity->ConeAngle>=10);
        TestTrue(TEXT("cone follows nozzle aim"),Velocity->CoordinateSpace==ENiagaraCoordinateSpace::Local
            && Velocity->ConeDirection.CalculateRange().Min.Equals(FVector3f::ZAxisVector));
    }
    TestTrue(TEXT("full spray fits fixed system bounds"),System->bFixedBounds && System->GetFixedBounds().Max.Z>=320);
    return true;
}
#endif
