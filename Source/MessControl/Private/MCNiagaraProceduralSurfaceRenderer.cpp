#include "MCNiagaraProceduralSurfaceRenderer.h"
#include "MCNiagaraProceduralSurfaceComponent.h"
#include "MCNiagaraProceduralSurfaceResources.h"
#include "MCWorldPreloadSubsystem.h"
#include "NiagaraRenderer.h"
#include "NiagaraComponent.h"
#include "NiagaraConstants.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraSystemInstance.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"

namespace MCPreparedSurfaceRenderer
{
class FRenderer final:public FNiagaraRenderer
{
public:
    FRenderer(ERHIFeatureLevel::Type Level,const UNiagaraRendererProperties* Properties,const FNiagaraEmitterInstance* Emitter)
        :FNiagaraRenderer(Level,Properties,Emitter){}
    void PostSystemTick_GameThread(const UNiagaraRendererProperties* Properties,const FNiagaraEmitterInstance* Emitter)override
    {
        const auto* Surface=Cast<UMCNiagaraProceduralSurfaceRendererProperties>(Properties);
        auto* Instance=Emitter?Emitter->GetParentSystemInstance():nullptr;
        auto* Parent=Instance?Cast<UNiagaraComponent>(Instance->GetAttachComponent()):nullptr;
        auto* Loading=Parent&&Parent->GetWorld()?Parent->GetWorld()->GetSubsystem<UMCWorldPreloadSubsystem>():nullptr;
        auto* Resources=Loading?Loading->GetProceduralSurfaceResources():nullptr;
        if(!Surface||!Resources||GetSimTarget()!=ENiagaraSimTarget::CPUSim)return;
        bool Enabled=false;
        bool RendererEnabled=IsRendererEnabled(Properties,Emitter);
#if WITH_EDITORONLY_DATA
        RendererEnabled&=!Emitter->IsDisabledFromIsolation();
#endif
        if(RendererEnabled) {
            const auto& Data=Emitter->GetParticleData();
            const auto& Particles=Data.GetCurrentDataChecked();
            auto Active=FNiagaraDataSetAccessor<FNiagaraBool>::CreateReader(Data,Surface->EnabledBinding.GetDataSetBindableVariable().GetName());
            auto Visibility=FNiagaraDataSetAccessor<int32>::CreateReader(Data,Surface->RendererVisibilityTagBinding.GetDataSetBindableVariable().GetName());
            for(uint32 I=0;I<Particles.GetNumInstances();++I)
                if(Active.GetSafe(I,true)&&(!Visibility.IsValid()||Visibility.GetSafe(I,0)==Surface->RendererVisibility)) {Enabled=true;break;}
        }
        Resources->UpdateRendererEligibility(Parent,Enabled);
    }
    void OnSystemComplete_GameThread(const UNiagaraRendererProperties*,const FNiagaraEmitterInstance* Emitter)override
    {
        auto* Instance=Emitter?Emitter->GetParentSystemInstance():nullptr;
        auto* Parent=Instance?Cast<UNiagaraComponent>(Instance->GetAttachComponent()):nullptr;
        if(auto* Loading=Parent&&Parent->GetWorld()?Parent->GetWorld()->GetSubsystem<UMCWorldPreloadSubsystem>():nullptr)
            if(auto* Resources=Loading->GetProceduralSurfaceResources())Resources->UpdateRendererEligibility(Parent,false);
    }
    // The base has no component pool. GC/proxy rebuild destroys only this
    // lightweight renderer; the existing scoped draw retains its resources.
};
}

UMCNiagaraProceduralSurfaceRendererProperties::UMCNiagaraProceduralSurfaceRendererProperties()
{
    AttributeBindings={&EnabledBinding,&RendererVisibilityTagBinding};
}
void UMCNiagaraProceduralSurfaceRendererProperties::PostInitProperties()
{
    Super::PostInitProperties();InitRendererBaseBindings();
    if(!EnabledBinding.IsValid())EnabledBinding=FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_COMPONENTS_ENABLED);
    if(!RendererVisibilityTagBinding.IsValid())RendererVisibilityTagBinding=FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_VISIBILITY_TAG);
}
FNiagaraRenderer* UMCNiagaraProceduralSurfaceRendererProperties::CreateEmitterRenderer(ERHIFeatureLevel::Type Level,const FNiagaraEmitterInstance* Emitter,const FNiagaraSystemInstanceController&)
{
    return new MCPreparedSurfaceRenderer::FRenderer(Level,this,Emitter);
}
void UMCNiagaraProceduralSurfaceRendererProperties::GetUsedMaterials(const FNiagaraEmitterInstance*,TArray<UMaterialInterface*>& Out)const
{
    if(TemplateComponent) {
        for(UMaterialInterface* M:TemplateComponent->NativeOriginalMaterials)if(M)Out.AddUnique(M);
        for(UMaterialInterface* M:TemplateComponent->AuthoredMaterials)if(M)Out.AddUnique(M);
    }
}
#if WITH_EDITORONLY_DATA
TArray<FNiagaraVariable> UMCNiagaraProceduralSurfaceRendererProperties::GetBoundAttributes()const
{
    TArray<FNiagaraVariable> Result=Super::GetBoundAttributes();
    Result.AddUnique(EnabledBinding.GetParamMapBindableVariable());
    Result.AddUnique(RendererVisibilityTagBinding.GetParamMapBindableVariable());return Result;
}
#endif
