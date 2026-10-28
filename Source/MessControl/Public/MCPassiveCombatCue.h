#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCPassiveCombatCue.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

UENUM()
enum class EMCPassiveCombatCueKind : uint8 { Lightning,EchoTool };

/** Geometry and contact pose are copied from the actual visible tool, before a later switch. */
USTRUCT()
struct FMCEchoToolPart
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(VisibleAnywhere) FTransform ContactPose=FTransform::Identity;
};

/** Short cosmetic links/ghost tools. Shared immutable materials are prepared by the arena manifest. */
UCLASS()
class MESSCONTROL_API AMCPassiveCombatCue : public AActor
{
    GENERATED_BODY()
public:
    AMCPassiveCombatCue();
    static void Spawn(UWorld* World,FVector From,FVector To,uint32 Seed);
    static AMCPassiveCombatCue* SpawnEcho(UWorld* World,FVector Origin,FVector Forward,const TArray<FMCEchoToolPart>& Parts,bool bOverhead,double ContactAt);
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Mesh;
    UPROPERTY(EditDefaultsOnly) TObjectPtr<UMaterialInterface> LinkMaterial;
    UPROPERTY(EditDefaultsOnly) TObjectPtr<UMaterialInterface> GhostMaterial;
    UPROPERTY(ReplicatedUsing=RefreshGhost,VisibleInstanceOnly) EMCPassiveCombatCueKind Kind=EMCPassiveCombatCueKind::Lightning;
    UPROPERTY(ReplicatedUsing=RefreshGhost,VisibleInstanceOnly) TArray<FMCEchoToolPart> ToolParts;
    UPROPERTY(Replicated,VisibleInstanceOnly) bool bEchoOverhead=false;
    UPROPERTY(VisibleAnywhere,Transient) TArray<TObjectPtr<UStaticMeshComponent>> GhostMeshes;
    UPROPERTY(Replicated,VisibleInstanceOnly) FVector_NetQuantize LinkEnd;
    UPROPERTY(Replicated,VisibleInstanceOnly) double StartedAt=0;
    UPROPERTY(Replicated,VisibleInstanceOnly) uint32 NoiseSeed=0;
private:
    UFUNCTION() void RefreshGhost();
    void TickGhost(float Age);
};
