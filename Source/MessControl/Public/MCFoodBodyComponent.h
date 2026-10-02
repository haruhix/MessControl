#pragma once
#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "MCFoodBodyComponent.generated.h"

class UStaticMesh;

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
