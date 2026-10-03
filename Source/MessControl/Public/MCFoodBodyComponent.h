#pragma once
#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "MCFoodBodyComponent.generated.h"

class UStaticMesh;

/** Detailed hand/tool queries without a second copy of the food's compound hulls. */
UCLASS()
class MESSCONTROL_API UMCFoodGripComponent : public UStaticMeshComponent
{
    GENERATED_BODY()
public:
    virtual UBodySetup* GetBodySetup() override;
private:
    UPROPERTY(Transient) TObjectPtr<UBodySetup> QueryBodySetup;
    TWeakObjectPtr<UBodySetup> QuerySource;
};

/** Compound mesh collision. BoxExtent is retained only for bounds and legacy size queries. */
UCLASS(ClassGroup=Collision, meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCFoodBodyComponent : public UBoxComponent
{
    GENERATED_BODY()
public:
    UMCFoodBodyComponent();
    void SetCollisionMesh(UStaticMesh* Mesh, FVector ItemScale);
    virtual void UpdateBodySetup() override;
    virtual FPrimitiveSceneProxy* CreateSceneProxy() override { return nullptr; }
    bool HasMeshCollision() const;
private:
    UPROPERTY(Transient) TObjectPtr<UStaticMesh> CollisionMesh;
    FVector CollisionScale=FVector::OneVector;
};
