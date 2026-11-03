#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "MCNiagaraMaterialContextComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraComponentRendererProperties.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"

namespace MCNiagaraMaterialContextTests
{
// Real component/parameter/lease APIs, without activating a Niagara simulation.
// This model fixture is not a material/PSO readiness or artistic acceptance result.
struct FFixture
{
    UWorld* World=nullptr;
    AActor* Owner=nullptr;
    UStaticMeshComponent* Mesh=nullptr;
    UMaterialInterface* Material=nullptr;
    UMCNiagaraMaterialContextResources* Resources=nullptr;
    UNiagaraSystem* System=nullptr;
    UMCNiagaraMaterialContextComponent* Template=nullptr;
    FGuid Epoch=FGuid::NewGuid();
    FString Error;
    FFixture()
    {
        const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
            .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
        World=UWorld::CreateWorld(EWorldType::EditorPreview,false,NAME_None,nullptr,false,GMaxRHIFeatureLevel,&Values);
        GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
        Owner=World->SpawnActor<AActor>();
        Mesh=NewObject<UStaticMeshComponent>(Owner,NAME_None,RF_Transient);Owner->SetRootComponent(Mesh);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Mesh->RegisterComponentWithWorld(World);
        Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/VFX/M_CombatHitOverlay.M_CombatHitOverlay"));
        Resources=NewObject<UMCNiagaraMaterialContextResources>(World,NAME_None,RF_Transient);
    }
    ~FFixture()
    {if(Resources)Resources->Reset();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    bool Prepare(EMCNiagaraMaterialContextOperation Operation,int32 Capacity=2,UMaterialInstanceDynamic* ScalarMID=nullptr)
    {
        if(!Material||!Mesh->GetStaticMesh()||!Resources->Initialize(World,Epoch,Error))return false;
        auto* E=LoadObject<UNiagaraEmitter>(nullptr,TEXT("/Niagara/DefaultAssets/Templates/Emitters/Minimal.Minimal"));
        if(!E){Error=TEXT("Missing stock Minimal emitter test fixture.");return false;}
        System=NewObject<UNiagaraSystem>(GetTransientPackage(),NAME_None,RF_Transient);
        auto Handle=System->AddEmitterHandle(*E,TEXT("ContextFixture"),E->GetExposedVersion().VersionGuid);
        const auto Instance=Handle.GetInstance();
        if(!Instance.Emitter || !Instance.Emitter->IsIn(System))
        {Error=TEXT("Test emitter must be a separately owned system instance.");return false;}
        Instance.Emitter->GetLatestEmitterData()->SimTarget=ENiagaraSimTarget::CPUSim;
        auto* Renderer=NewObject<UNiagaraComponentRendererProperties>(Instance.Emitter,NAME_None,RF_Transient);
        Renderer->ComponentType=UMCNiagaraMaterialContextComponent::StaticClass();Renderer->ComponentCountLimit=1;
        Template=NewObject<UMCNiagaraMaterialContextComponent>(Renderer,NAME_None,RF_Transient);
        Template->ContextID=TEXT("Body");Template->Operation=Operation;Template->OverlayMaterial=Material;
        Template->ScalarParameter=TEXT("Opacity");Renderer->TemplateComponent=Template;
        Instance.Emitter->AddRenderer(Renderer,Instance.Version);
        auto& Exposed=System->GetExposedParameters();
        Exposed.AddParameter(FNiagaraVariable(FNiagaraTypeDefinition(USceneComponent::StaticClass()),TEXT("User.VisualSubject")));
        Exposed.AddParameter(FNiagaraVariable(FNiagaraTypeDefinition(UMCNiagaraMaterialContextResources::StaticClass()),TEXT("User.MaterialContextResources")));
        if(Operation==EMCNiagaraMaterialContextOperation::ExistingMIDScalar)
            Exposed.AddParameter(FNiagaraVariable(FNiagaraTypeDefinition(UMaterialInstanceDynamic::StaticClass()),TEXT("User.MaterialTarget")));
        if(!Resources->PrepareSystem(System,Capacity,Error)||!Resources->RegisterPreparedTarget(Mesh,Error))return false;
        if(ScalarMID&&!Resources->RegisterExistingScalarTarget(Mesh,ScalarMID,TEXT("Opacity"),Error))return false;
        return Resources->SealResources(Error);
    }
    struct FClient {UNiagaraComponent* Niagara=nullptr;UMCNiagaraMaterialContextComponent* Component=nullptr;};
    FClient Client(bool bReserve=true)
    {
        FClient C;C.Niagara=NewObject<UNiagaraComponent>(Owner,NAME_None,RF_Transient);
        C.Niagara->SetAutoActivate(false);C.Niagara->SetAsset(System);
        C.Niagara->GetOverrideParameters().SetUObject(Mesh,
            FNiagaraVariable(FNiagaraTypeDefinition(USceneComponent::StaticClass()),TEXT("User.VisualSubject")));
        C.Component=DuplicateObject<UMCNiagaraMaterialContextComponent>(Template,Owner);
        C.Component->SetupAttachment(C.Niagara);C.Component->SetVisibility(true);
        if(bReserve && (!Resources->BindPreparedScalarTarget(C.Niagara,Error)||!Resources->ReserveActivation(C.Niagara,Error)))C.Component=nullptr;
        return C;
    }
};
constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNiagaraOverlayLeaseModel,"MessControl.VFX.MaterialContext.OverlayLeaseOrderModel",MCNiagaraMaterialContextTests::Flags)
bool FMCNiagaraOverlayLeaseModel::RunTest(const FString&)
{
    using namespace MCNiagaraMaterialContextTests;FFixture F;
    if(!TestTrue(TEXT("Prepare isolated retained overlay pool"),F.Prepare(EMCNiagaraMaterialContextOperation::ExistingMeshOverlay)))
    {AddError(F.Error);return false;}
    auto A=F.Client(),B=F.Client();if(!A.Component||!B.Component){AddError(F.Error);return false;}
    A.Component->SetTint(FLinearColor::Red);A.Component->Activate();auto* First=F.Mesh->OverlayMaterial.Get();
    TestNotNull(TEXT("First lease installs a prepared MID"),First);
    B.Component->SetTint(FLinearColor::Blue);B.Component->Activate();auto* Second=F.Mesh->OverlayMaterial.Get();
    TestTrue(TEXT("Independent leases use separate prepared variants"),First&&Second&&First!=Second);
    A.Component->Deactivate();TestTrue(TEXT("Ending a nonwinner preserves the active winner"),F.Mesh->OverlayMaterial==Second);
    B.Component->Deactivate();TestNull(TEXT("Last release restores exact null baseline"),F.Mesh->OverlayMaterial.Get());
    A.Component->Activate();B.Component->Activate();B.Component->Deactivate();
    TestTrue(TEXT("Ending the winner resumes the other existing lease"),F.Mesh->OverlayMaterial==First);
    A.Component->Deactivate();TestNull(TEXT("Reverse release order also restores null"),F.Mesh->OverlayMaterial.Get());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNiagaraForeignOwnerModel,"MessControl.VFX.MaterialContext.ForeignWriterStickyModel",MCNiagaraMaterialContextTests::Flags)
bool FMCNiagaraForeignOwnerModel::RunTest(const FString&)
{
    using namespace MCNiagaraMaterialContextTests;FFixture F;
    if(!F.Prepare(EMCNiagaraMaterialContextOperation::ExistingMeshOverlay)){AddError(F.Error);return false;}
    auto A=F.Client();if(!A.Component){AddError(F.Error);return false;}A.Component->Activate();
    F.Mesh->SetOverlayMaterial(F.Material);
    // Match the engine's repeated Activate attempt on an inactive pooled component.
    for(int32 I=0;I<3;++I)A.Component->Activate();
    TestTrue(TEXT("Foreign pointer survives repeated engine-style activation attempts"),F.Mesh->OverlayMaterial==F.Material);
    F.Resources->ReleaseActivation(A.Niagara);
    if(!TestTrue(TEXT("A deliberate new activation can reserve again"),F.Resources->ReserveActivation(A.Niagara,F.Error)))return false;
    A.Component->Activate();TestTrue(TEXT("New activation installs its managed overlay"),F.Mesh->OverlayMaterial!=F.Material);
    A.Component->Deactivate();TestTrue(TEXT("New activation restores the actual foreign baseline"),F.Mesh->OverlayMaterial==F.Material);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNiagaraScalarLeaseModel,"MessControl.VFX.MaterialContext.ExistingMIDScalarRestoreModel",MCNiagaraMaterialContextTests::Flags)
bool FMCNiagaraScalarLeaseModel::RunTest(const FString&)
{
    using namespace MCNiagaraMaterialContextTests;FFixture F;
    auto* MID=UMaterialInstanceDynamic::Create(F.Material,F.Owner);
    if(!MID){AddError(TEXT("Missing isolated material fixture."));return false;}
    MID->SetScalarParameterValue(TEXT("Opacity"),.125f);MID->SetVectorParameterValue(TEXT("Color"),FLinearColor::Green);F.Mesh->SetMaterial(0,MID);
    if(!F.Prepare(EMCNiagaraMaterialContextOperation::ExistingMIDScalar,2,MID)){AddError(F.Error);return false;}
    auto A=F.Client(),B=F.Client();if(!A.Component||!B.Component){AddError(F.Error);return false;}
    A.Component->SetScalarValue(.3f);A.Component->Activate();B.Component->SetScalarValue(.7f);B.Component->Activate();
    TestEqual(TEXT("Latest equal-priority scalar wins"),MID->K2_GetScalarParameterValue(TEXT("Opacity")),.7f);
    B.Component->Deactivate();TestEqual(TEXT("Earlier scalar lease resumes"),MID->K2_GetScalarParameterValue(TEXT("Opacity")),.3f);
    A.Component->Deactivate();TestEqual(TEXT("Last release restores exact own-MID baseline"),MID->K2_GetScalarParameterValue(TEXT("Opacity")),.125f);
    TestTrue(TEXT("Unrelated vector parameter is preserved"),MID->K2_GetVectorParameterValue(TEXT("Color")).Equals(FLinearColor::Green));
    TestTrue(TEXT("The assigned base material was never replaced"),F.Mesh->GetMaterial(0)==MID);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNiagaraCapacityModel,"MessControl.VFX.MaterialContext.CapacityAndFactoryGuardModel",MCNiagaraMaterialContextTests::Flags)
bool FMCNiagaraCapacityModel::RunTest(const FString&)
{
    using namespace MCNiagaraMaterialContextTests;FFixture F;
    if(!F.Prepare(EMCNiagaraMaterialContextOperation::ExistingMeshOverlay,1)){AddError(F.Error);return false;}
    TestTrue(TEXT("An explicit private world without GameState can use its sealed scope"),F.Resources->IsReadyForPlayback(F.World));
    TestFalse(TEXT("A readiness scope never certifies another world"),F.Resources->IsReadyForPlayback(nullptr));
    auto A=F.Client(),B=F.Client(false);if(!A.Component){AddError(F.Error);return false;}
    TestFalse(TEXT("Capacity exhaustion is explicit before any B activation"),F.Resources->ReserveActivation(B.Niagara,F.Error));
    TestNull(TEXT("Reservation alone never installs an overlay"),F.Mesh->OverlayMaterial.Get());
    F.Resources->ReleaseActivation(A.Niagara);
    TestTrue(TEXT("Releasing a pending reservation makes existing capacity reusable"),F.Resources->ReserveActivation(B.Niagara,F.Error));
    auto* Instanced=NewObject<UInstancedStaticMeshComponent>(F.Owner);Instanced->SetStaticMesh(F.Mesh->GetStaticMesh());
    TestFalse(TEXT("Unverified instanced factory is explicitly rejected"),UMCNiagaraMaterialContextResources::ValidateTargetFactory(Instanced,F.Error));
    return true;
}
#endif
