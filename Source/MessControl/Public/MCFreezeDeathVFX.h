#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCFreezeDeathVFX.generated.h"
class AMCToothCharacter;
class UProceduralMeshComponent;
class UMaterialInterface;

/** Local geometry driven by the character's replicated fatal-freeze timestamp. */
UCLASS()
class MESSCONTROL_API AMCFreezeDeathVFX : public AActor
{
    GENERATED_BODY()
public:
    AMCFreezeDeathVFX();
    static AMCFreezeDeathVFX* SpawnLocal(AMCToothCharacter* Hero,double StartedAt);
    virtual void Tick(float DeltaSeconds) override;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UProceduralMeshComponent> IceCube;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<TObjectPtr<UProceduralMeshComponent>> BodyFragments;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<TObjectPtr<UProceduralMeshComponent>> IceFragments;
private:
    void Capture(AMCToothCharacter* Hero);
    UProceduralMeshComponent* MakePart(FName Name);
    UPROPERTY() TObjectPtr<UMaterialInterface> GlassMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> BodyMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> BagMaterial;
    UPROPERTY() TObjectPtr<AMCToothCharacter> Source;
    struct FShard { FVector Home,Velocity,Spin; float Floor=0; };
    TArray<FShard> Motion;
    double StartedAt=0;
    float HalfSize=100;
    bool bShattered=false;
};
