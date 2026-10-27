#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ProceduralMeshComponent.h"
#include "MCTaskCoinPresentationComponent.generated.h"

class AMCPlayerController;
class APawn;
class UAudioComponent;
class UMaterialInterface;
class USoundBase;

/** Local cosmetic receipt only. Neither its mesh count nor completion grants currency. */
UCLASS(ClassGroup=(Presentation),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCTaskCoinPresentationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCTaskCoinPresentationComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaSeconds,ELevelTick Tick,FActorComponentTickFunction* Function) override;

    /** Call under the location readiness barrier. PresentCoins never loads or creates resources. */
    UFUNCTION(BlueprintCallable,Category="Rewards|Presentation") void PreparePresentation();
    UFUNCTION(BlueprintPure,Category="Rewards|Presentation") bool HasPreparedPresentation() const;
    UFUNCTION(BlueprintCallable,Category="Rewards|Presentation") void PresentCoins(int32 Amount,int32 WalletAfter);
    /** Clears motion/audio while retaining the prepared pool for this world. */
    UFUNCTION(BlueprintCallable,Category="Rewards|Presentation") void ResetPresentation();

    UPROPERTY(EditDefaultsOnly,Category="Rewards|Presentation") TSoftObjectPtr<UMaterialInterface> CoinMaterial;
    UPROPERTY(EditDefaultsOnly,Category="Rewards|Presentation") TSoftObjectPtr<USoundBase> CoinSound;
    UPROPERTY(Transient,VisibleInstanceOnly,BlueprintReadOnly,Category="Rewards|Presentation") TObjectPtr<UProceduralMeshComponent> CoinMesh;
private:
    static constexpr int32 PoolSize=5;
    struct FCoinVertex {FVector Position,Normal,Tangent;FVector2D UV;FColor Color;};
    struct FCoinMotion
    {
        FVector Start=FVector::ZeroVector,Landing=FVector::ZeroVector,LandingNormal=FVector::UpVector;
        FVector LandingLocal=FVector::ZeroVector,NormalLocal=FVector::UpVector;
        TWeakObjectPtr<UPrimitiveComponent> Surface;
        FQuat InitialRotation=FQuat::Identity;
        float Delay=0,Spin=0;
        bool bContact=false;
    };
    bool CanPresent(AMCPlayerController* PC,APawn* Pawn) const;
    void BuildCoinGeometry(TArray<int32>& Indices);
    void UpdateMesh(float Age);
    void ReleasePreparedResources();
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> RetainedMaterial;
    UPROPERTY(Transient) TObjectPtr<USoundBase> RetainedSound;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> CoinAudio;
    UPROPERTY(Transient) TObjectPtr<AActor> VisualHost;
    TWeakObjectPtr<UWorld> PreparedWorld;
    TWeakObjectPtr<APawn> ActivePawn;
    TWeakObjectPtr<APawn> VisibilityPawn;
    FCoinMotion Coins[PoolSize];
    TArray<FCoinVertex> Template;
    TArray<FVector> Positions,Normals;
    TArray<FVector2D> UV;
    TArray<FColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    FVector BurstOrigin=FVector::ZeroVector;
    float Age=0;
    int32 ActiveCount=0;
};
