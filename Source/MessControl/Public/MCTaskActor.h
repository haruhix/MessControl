#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCDataAssets.h"
#include "MCTaskActor.generated.h"
class UStaticMeshComponent;
class UTextRenderComponent;
class AMCToothCharacter;

UCLASS(Blueprintable)
class MESSCONTROL_API AMCTaskActor : public AActor
{
    GENERATED_BODY()
public:
    AMCTaskActor();
    virtual void Tick(float DeltaSeconds) override;
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    void Initialize(UMCDayEvent* Event);
    bool ApplyWork(AMCToothCharacter* Worker, bool bBrush, float DeltaSeconds);
    UPROPERTY(ReplicatedUsing=OnRep_Visuals, EditAnywhere, BlueprintReadOnly, Category="Task") EMCTaskKind Kind = EMCTaskKind::Coffee;
    UPROPERTY(ReplicatedUsing=OnRep_Visuals, BlueprintReadOnly, Category="Task") float Progress = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Task") TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Task") TObjectPtr<UTextRenderComponent> Label;
    UPROPERTY(EditDefaultsOnly, Category="Audio") TObjectPtr<UMCSoundPalette> SoundPalette;
protected:
    virtual void BeginPlay() override;
    UFUNCTION() void OnRep_Visuals();
    UFUNCTION(NetMulticast, Unreliable) void MulticastComplete();
private:
    float WorkSeconds = 3.f;
    bool bResolved = false;
    UPROPERTY() TObjectPtr<UStaticMesh> CoffeeMesh;
    UPROPERTY() TObjectPtr<UStaticMesh> FoodMesh;
    UPROPERTY() TObjectPtr<UStaticMesh> ToothMesh;
};
