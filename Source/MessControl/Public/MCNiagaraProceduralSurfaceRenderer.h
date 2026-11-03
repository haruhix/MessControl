#pragma once
#include "CoreMinimal.h"
#include "NiagaraRendererProperties.h"
#include "MCNiagaraProceduralSurfaceRenderer.generated.h"

class UMCNiagaraProceduralSurfaceComponent;

/** Editable original surface template; location resources own the actual draw.
 * Renderer/proxy recreation never allocates or retires the prepared component. */
UCLASS(EditInlineNew,meta=(DisplayName="Original Procedural Surface Renderer"))
class MESSCONTROL_API UMCNiagaraProceduralSurfaceRendererProperties final:public UNiagaraRendererProperties
{
    GENERATED_BODY()
public:
    UMCNiagaraProceduralSurfaceRendererProperties();
    virtual void PostInitProperties()override;
    virtual FNiagaraRenderer* CreateEmitterRenderer(ERHIFeatureLevel::Type FeatureLevel,const FNiagaraEmitterInstance* Emitter,const FNiagaraSystemInstanceController& Controller)override;
    virtual FNiagaraBoundsCalculator* CreateBoundsCalculator()override{return nullptr;}
    virtual bool IsSimTargetSupported(ENiagaraSimTarget Target)const override{return Target==ENiagaraSimTarget::CPUSim;}
    virtual void GetUsedMaterials(const FNiagaraEmitterInstance*,TArray<UMaterialInterface*>& Out)const override;
    virtual bool NeedsSystemPostTick()const override{return true;}
    virtual bool NeedsSystemCompletion()const override{return true;}
#if WITH_EDITORONLY_DATA
    virtual TArray<FNiagaraVariable> GetBoundAttributes()const override;
    virtual void GetRendererTooltipWidgets(const FNiagaraEmitterInstance*,TArray<TSharedPtr<SWidget>>&,TSharedPtr<FAssetThumbnailPool>)const override{}
#endif
    UPROPERTY(Export,Instanced,EditAnywhere,Category="Surface") TObjectPtr<UMCNiagaraProceduralSurfaceComponent> TemplateComponent;
    UPROPERTY(EditAnywhere,Category="Bindings") FNiagaraVariableAttributeBinding EnabledBinding;
    UPROPERTY(EditAnywhere,Category="Bindings") FNiagaraVariableAttributeBinding RendererVisibilityTagBinding;
    UPROPERTY(EditAnywhere,Category="Rendering") int32 RendererVisibility=0;
    /** Preserved stock one-draw compatibility policy; no renderer-owned pool. */
    UPROPERTY(VisibleAnywhere,AdvancedDisplay,Category="Surface") uint32 ComponentCountLimit=1;
    UPROPERTY(VisibleAnywhere,AdvancedDisplay,Category="Surface") bool bAssignComponentsOnParticleID=true;
    UPROPERTY(VisibleAnywhere,AdvancedDisplay,Category="Surface") bool bCreateComponentFirstParticleFrame=false;
    UPROPERTY(VisibleAnywhere,AdvancedDisplay,Category="Surface") bool bOnlyActivateNewlyAquiredComponents=true;
#if WITH_EDITORONLY_DATA
    UPROPERTY(VisibleAnywhere,AdvancedDisplay,Category="Surface") bool bVisualizeComponents=true;
#endif
};
