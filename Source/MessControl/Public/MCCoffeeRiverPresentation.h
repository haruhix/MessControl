#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/GCObject.h"
#include "NiagaraComponent.h"
#include "MCCoffeeRiverPresentation.generated.h"
class AActor;
class UWorld;
class AMCTongue;
class UNiagaraSystem;
class UNiagaraComponent;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class FMCOriginalCoffeeRiverVertexTextures;
class FRenderCommandFence;

/** Retains the existing native water policy: a PSO miss cannot hide the footprint. */
UCLASS()
class MESSCONTROL_API UMCCoffeeRiverNiagaraComponent final:public UNiagaraComponent
{
    GENERATED_BODY()
protected:
    bool UsePSOPrecacheRenderProxyDelay()const override{return false;}
};

/** One local retained surface; geometry is the native final draw data, never gameplay. */
class MESSCONTROL_API FMCCoffeeRiverPresentation final:public FGCObject
{
public:
    FMCCoffeeRiverPresentation();
    ~FMCCoffeeRiverPresentation();
    static bool Uses(const UNiagaraSystem* System);
    bool Prepare(UWorld* World,AMCTongue* Tongue,FGuid Epoch,const TArray<UNiagaraSystem*>& Systems,FString& Error);
    bool Claim(AActor* Source,UMaterialInterface* Original,FString& Error);
    void Retire(AActor* Source);
    bool Upload(AActor* Source,TConstArrayView<FVector> Vertices,TConstArrayView<FVector> Normals,UMaterialInstanceDynamic* Native,FString& Error);
    bool Validate(const UNiagaraSystem* System,const UWorld* World,FString& Error)const;
    bool Bind(UNiagaraComponent* Component,FString& Error)const;
    bool IsFor(const AMCTongue* Tongue)const;
    bool PollRendering(bool& Waiting,FString& Error);
    bool HasPendingUploads()const;
    UMaterialInstanceDynamic* GetNativeMaterial()const{return NativeMaterial.Get();}
    UMaterialInterface* GetOriginalMaterial()const{return ClaimedOriginal.Get();}
    void GetResources(TArray<UObject*>& Out)const;
    void GetWarmups(TArray<TPair<UNiagaraSystem*,UMaterialInterface*>>& Out)const;
    void AddReferencedObjects(FReferenceCollector& Collector)override;
    FString GetReferencerName()const override{return TEXT("FMCCoffeeRiverPresentation");}
private:
    struct FLayer {TObjectPtr<UNiagaraSystem> System=nullptr;TObjectPtr<UStaticMesh> Mesh=nullptr;TObjectPtr<UMaterialInterface> Original=nullptr;TObjectPtr<UMaterialInstanceDynamic> Material=nullptr;};
    struct FNativeVariant {TObjectPtr<UMaterialInterface> Original=nullptr;TObjectPtr<UMaterialInstanceDynamic> Material=nullptr;};
    TArray<FLayer> Layers;
    TArray<FNativeVariant> NativeVariants;
    TObjectPtr<UMaterialInterface> ClaimedOriginal=nullptr;
    TUniquePtr<FMCOriginalCoffeeRiverVertexTextures> Textures;
    TWeakObjectPtr<UWorld> PreparedWorld;
    TWeakObjectPtr<AMCTongue> PreparedTongue;
    TWeakObjectPtr<AActor> ClaimedSource;
    FGuid PreparedEpoch;
    TObjectPtr<UStaticMesh> SourceMesh=nullptr;
    TObjectPtr<UMaterialInstanceDynamic> NativeMaterial=nullptr;
    FBox Bounds=FBox(EForceInit::ForceInit);
    bool bConsumed=false;
    bool bWarmupSubmitted=false;
    TArray<TObjectPtr<UNiagaraComponent>> RenderWarmups;
    TUniquePtr<FRenderCommandFence> RenderFence;
};

/** No gameplay actor/timers. Only explicit Mouth/Tutorial/precommit preparation creates resources. */
UCLASS()
class MESSCONTROL_API UMCCoffeeRiverPresentationResources final:public UWorldSubsystem
{
    GENERATED_BODY()
public:
    bool PrepareLocation(const TArray<UObject*>& Loaded,FGuid Epoch,bool bPreview,FString& Error);
    TSharedPtr<FMCCoffeeRiverPresentation> Claim(AMCTongue* Tongue,AActor* Source,UMaterialInterface* Original,FString& Error);
    bool PollRendering(bool& Waiting,FString& Error);
    bool HasPendingUploads()const;
    void GetResources(TArray<UObject*>& Out)const;
    void GetWarmups(TArray<TPair<UNiagaraSystem*,UMaterialInterface*>>& Out)const;
    void ResetUnclaimed();
    void Deinitialize()override;
private:
    FGuid Scope;
    TArray<TSharedPtr<FMCCoffeeRiverPresentation>> Surfaces;
};
