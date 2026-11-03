#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCVFXNativePresentationSettings.h"
#include "MCVFXCapturedMesh.h"
#include "MCPassiveCombatCue.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class AMCToothCharacter;
enum class EMCVFXPresentationResult:uint8;

UENUM()
enum class EMCPassiveCombatCueKind : uint8 { Lightning,EchoTool };

/** Short cosmetic links/ghost tools. Shared immutable materials are prepared by the arena manifest. */
UCLASS()
class MESSCONTROL_API AMCPassiveCombatCue : public AActor
{
    GENERATED_BODY()
public:
    AMCPassiveCombatCue();
    static void Spawn(UWorld* World,FVector From,FVector To,uint32 Seed);
    static AMCPassiveCombatCue* SpawnLightningAccepted(AMCToothCharacter* Hero,FName SourceID,FName Event,
        EMCVFXPresentationResult Result,FVector From,FVector To,uint32 Seed);
    static AMCPassiveCombatCue* SpawnEcho(UWorld* World,FVector Origin,FVector Forward,const TArray<FMCEchoToolPart>& Parts,bool bOverhead,double ContactAt);
    /** Same native ghost, with the original accepted mechanic retained on its
     * existing replicated actor. Custom/Disabled never spawn a second fallback.
     */
    static AMCPassiveCombatCue* SpawnEchoAccepted(AMCToothCharacter* Hero,FName SourceID,FName Event,
        EMCVFXPresentationResult Result,FVector Origin,FVector Forward,const TArray<FMCEchoToolPart>& Parts,bool bOverhead,double ContactAt);
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
    UPROPERTY(Replicated,VisibleInstanceOnly) FName SourcePerkID;
    UPROPERTY(Replicated,VisibleInstanceOnly) FName AcceptedEvent;
    UPROPERTY(Replicated) TObjectPtr<AMCToothCharacter> PresentationOwner;
    UPROPERTY(Replicated) FGuid PresentationEpoch;
    UPROPERTY(ReplicatedUsing=OnRep_NativeStyle) FMCVFXNativePresentationRef NativeStyle;
    const FMCVFXPreparedNativePresentation& GetPreparedNativeStyle()const{return PreparedNative;}
private:
    UFUNCTION() void OnRep_NativeStyle();
    void ApplyNativeStyle();
    UPROPERTY(Transient) FMCVFXPreparedNativePresentation PreparedNative;
    UFUNCTION() void RefreshGhost();
    void TickGhost(float Age);
};
