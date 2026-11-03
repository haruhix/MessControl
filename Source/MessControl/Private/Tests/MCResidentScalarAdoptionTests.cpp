#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "MCNiagaraMaterialContextComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "MCVFXTestWorld.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraComponentRendererProperties.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCResidentScalarIdentityAdoption,
    "MessControl.VFX.MaterialContext.ResidentScalarIdentityAdoption",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FMCResidentScalarIdentityAdoption::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
    UWorld* World=MCVFXTestWorld::Create(EWorldType::EditorPreview,false,NAME_None,nullptr,false,GMaxRHIFeatureLevel,&Init);
    if(!TestNotNull(TEXT("Isolated world"),World))return false;
    GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
    struct FScope {UWorld* World;~FScope(){GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}} Scope{World};
    auto* Owner=World->SpawnActor<AActor>();
    auto* Geometry=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto* Parent=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/VFX/M_CombatHitOverlay.M_CombatHitOverlay"));
    if(!Geometry||!Parent){AddError(TEXT("Resident fixture assets missing."));return false;}
    auto MakeMesh=[&](UMaterialInstanceDynamic* MID){auto* Mesh=NewObject<UStaticMeshComponent>(Owner,NAME_None,RF_Transient);
        Mesh->SetStaticMesh(Geometry);Mesh->SetMaterial(0,MID);Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCanEverAffectNavigation(false);Mesh->RegisterComponentWithWorld(World);return Mesh;};
    auto* FirstMID=UMaterialInstanceDynamic::Create(Parent,Owner);FirstMID->SetScalarParameterValue(TEXT("Opacity"),.125f);
    auto* First=MakeMesh(FirstMID);
    auto* Resources=NewObject<UMCNiagaraMaterialContextResources>(World,NAME_None,RF_Transient);FString Error;
    auto* Emitter=LoadObject<UNiagaraEmitter>(nullptr,TEXT("/Niagara/DefaultAssets/Templates/Emitters/UpwardMeshBurst.UpwardMeshBurst"));
    if(!Emitter){AddError(TEXT("Actual stateful stock emitter fixture missing."));return false;}
    auto* System=NewObject<UNiagaraSystem>(GetTransientPackage(),NAME_None,RF_Transient);
    auto Handle=System->AddEmitterHandle(*Emitter,TEXT("ResidentScalar"),Emitter->GetExposedVersion().VersionGuid);
    const auto Instance=Handle.GetInstance();
    if(!Instance.Emitter||!Instance.Emitter->IsIn(System)||!Instance.Emitter->GetEmitterData(Instance.Version))
    {AddError(TEXT("Actual selected stateful fixture version unavailable."));return false;}
    Instance.Emitter->GetEmitterData(Instance.Version)->SimTarget=ENiagaraSimTarget::CPUSim;
    auto* Renderer=NewObject<UNiagaraComponentRendererProperties>(Instance.Emitter,NAME_None,RF_Transient);
    Renderer->ComponentType=UMCNiagaraMaterialContextComponent::StaticClass();Renderer->ComponentCountLimit=1;
    auto* Template=NewObject<UMCNiagaraMaterialContextComponent>(Renderer,NAME_None,RF_Transient);
    Template->ContextID=TEXT("Status");Template->Operation=EMCNiagaraMaterialContextOperation::ExistingMIDScalar;
    Template->ScalarParameter=TEXT("Opacity");Renderer->TemplateComponent=Template;Instance.Emitter->AddRenderer(Renderer,Instance.Version);
    auto& Store=System->GetExposedParameters();
    Store.AddParameter(FNiagaraVariable(FNiagaraTypeDefinition(USceneComponent::StaticClass()),TEXT("User.VisualSubject")));
    Store.AddParameter(FNiagaraVariable(FNiagaraTypeDefinition(UMCNiagaraMaterialContextResources::StaticClass()),TEXT("User.MaterialContextResources")));
    Store.AddParameter(FNiagaraVariable(FNiagaraTypeDefinition(UMaterialInstanceDynamic::StaticClass()),TEXT("User.MaterialTarget")));
    if(!Resources->Initialize(World,FGuid::NewGuid(),Error)||!Resources->PrepareSystem(System,2,Error)
        ||!Resources->RegisterPreparedTarget(First,Error)||!Resources->RegisterExistingScalarTarget(First,FirstMID,TEXT("Opacity"),Error)
        ||!Resources->SealResources(Error)){AddError(Error);return false;}
    auto* LateMID=UMaterialInstanceDynamic::Create(Parent,Owner);LateMID->SetScalarParameterValue(TEXT("Opacity"),.425f);
    auto* Late=MakeMesh(LateMID);
    TestTrue(TEXT("Existing factory signature admits later native component identity"),Resources->IsTargetPrepared(Late));
    TestFalse(TEXT("Ordinary registration retains its preparation-only contract"),Resources->RegisterExistingScalarTarget(Late,LateMID,TEXT("Opacity"),Error));
    TArray<UObject*> Before;Resources->GetPreparedResources(Before);
    if(!TestTrue(TEXT("Explicit handoff adopts exact existing native identity"),Resources->AdoptPreparedScalarTarget(Late,LateMID,TEXT("Opacity"),Error)))
    {AddError(Error);return false;}
    TArray<UObject*> After;Resources->GetPreparedResources(After);
    TestEqual(TEXT("Only existing native MID reference is added"),After.Num(),Before.Num()+1);
    for(UObject* Object:After)TestTrue(TEXT("No replacement rendering resource was created"),Before.Contains(Object)||Object==LateMID);
    TestEqual(TEXT("Enrollment does not change native scalar"),LateMID->K2_GetScalarParameterValue(TEXT("Opacity")),.425f);
    Late->SetMaterial(0,FirstMID);
    TestFalse(TEXT("Prepared MID cannot be stolen from another subject"),Resources->AdoptPreparedScalarTarget(Late,FirstMID,TEXT("Opacity"),Error));
    Late->SetMaterial(0,LateMID);
    auto* NoOverride=UMaterialInstanceDynamic::Create(Parent,Owner);auto* Unknown=MakeMesh(NoOverride);
    TestFalse(TEXT("Inherited scalar is not a native own-override admission"),Resources->AdoptPreparedScalarTarget(Unknown,NoOverride,TEXT("Opacity"),Error));
    auto* C=NewObject<UNiagaraComponent>(Owner,NAME_None,RF_Transient);C->SetAutoActivate(false);C->SetAsset(System);
    C->GetOverrideParameters().SetUObject(Late,FNiagaraVariable(FNiagaraTypeDefinition(USceneComponent::StaticClass()),TEXT("User.VisualSubject")));
    if(!Resources->BindPreparedScalarTarget(C,Error)||!Resources->ReserveActivation(C,Error)){AddError(Error);return false;}
    auto* Client=DuplicateObject<UMCNiagaraMaterialContextComponent>(Template,Owner);Client->SetupAttachment(C);Client->SetVisibility(true);
    Client->SetScalarValue(.7f);Client->Activate();
    TestEqual(TEXT("Actual context writes the later native MID"),LateMID->K2_GetScalarParameterValue(TEXT("Opacity")),.7f);
    Client->Deactivate();Resources->ReleaseActivation(C);
    TestEqual(TEXT("Actual release restores that identity's baseline"),LateMID->K2_GetScalarParameterValue(TEXT("Opacity")),.425f);
    Resources->Reset();
    TestFalse(TEXT("Retired scope cannot admit another identity"),Resources->AdoptPreparedScalarTarget(Late,LateMID,TEXT("Opacity"),Error));
    // Actual typed binding/lease behavior only, not Niagara simulation, first-use
    // shader/PSO coverage, a respawn gameplay test, or captured-body readiness.
    return true;
}
#endif
