#pragma once
#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "MCNiagaraProceduralSurfaceComponent.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UMaterialInstanceDynamic;

/** One real CPU Niagara ComponentRenderer draw, with native geometry as read-only input.
 * The original LocalVF, UVs, tangent frame, Substrate and WPO remain unchanged.
 * Only explicit prebarrier priming may allocate sections/material variants. */
UCLASS(ClassGroup=(Presentation),NotBlueprintable,meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCNiagaraProceduralSurfaceComponent final:public UProceduralMeshComponent
{
    GENERATED_BODY()
public:
    UMCNiagaraProceduralSurfaceComponent(const FObjectInitializer& ObjectInitializer);
    UPROPERTY(EditAnywhere,Category="Surface") FName SourceID;
    UPROPERTY(EditAnywhere,Category="Surface") TArray<TObjectPtr<UMaterialInterface>> NativeOriginalMaterials;
    UPROPERTY(EditAnywhere,Category="Surface") TArray<TObjectPtr<UMaterialInterface>> AuthoredMaterials;
    UPROPERTY(EditAnywhere,Category="Surface") TArray<FName> ScalarParameters;
    UPROPERTY(EditAnywhere,Category="Surface") TArray<FName> VectorParameters;
    UPROPERTY(EditAnywhere,Category="Surface") TArray<FName> TextureParameters;
    UPROPERTY(EditAnywhere,Category="Surface",meta=(ClampMin="1",ClampMax="131072")) int32 MaximumTriangles=32768;
    /** Concurrent scoped capacity, not a gameplay actor count limit. Exhaustion rejects the new lease. */
    UPROPERTY(EditAnywhere,Category="Preparation",meta=(ClampMin="1",ClampMax="64")) int32 SmallPreparedInstances=1;
    UPROPERTY(EditAnywhere,Category="Preparation",meta=(ClampMin="0",ClampMax="32")) int32 LargePreparedInstances=0;
    bool IsDrawPrepared()const{return bDrawPrepared;}
    int32 GetPreparedTriangleCapacity()const{return TriangleCapacity;}
    bool BindPreparedSubject(UProceduralMeshComponent* Subject,FString& Error);
    bool CanDrawSubject(UProceduralMeshComponent* Subject,FString& Error)const;
    bool UpdateNativeDraw(FString& Error);
    void SetRendererEligibility(bool Enabled);
    bool IsRendererEligible()const;
    void ReleasePreparedSubject();
    void GetPreparedMaterials(TArray<UMaterialInterface*>& Out)const;
    virtual void OnRegister()override;
    virtual void TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* TickFunction)override;
    virtual void OnUnregister()override;
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld)const override;
    const FString& GetSurfaceError()const{return SurfaceError;}
private:
    static constexpr int32 TrianglesPerSection=2048;
    bool PrepareDraw(FString& Error);
    bool ReadControl(UNiagaraComponent* Parent,bool& Preparing,int32& Capacity,FString& Error)const;
    UNiagaraComponent* DirectNiagaraParent()const;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> PreparedMaterials;
    UPROPERTY(Transient) TWeakObjectPtr<UProceduralMeshComponent> NativeSubject;
    TArray<FVector> Positions,Normals;
    TArray<FVector2D> UVs;
    TArray<FColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    int32 TriangleCapacity=0,VerticesPerSection=0;
    bool bDrawPrepared=false,bRendererEligible=false;
    uint64 LastRendererFrame=0;
    FString SurfaceError;
};
