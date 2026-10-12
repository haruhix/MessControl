#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCRogueFoamPatch.generated.h"
class UStaticMeshComponent;
UCLASS()
class MESSCONTROL_API AMCRogueFoamPatch : public AActor
{
    GENERATED_BODY()
public:
    AMCRogueFoamPatch();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Foam;
};
