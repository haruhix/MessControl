#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "MCToothCalculusComponent.generated.h"

class AMCToothCharacter;
class UProceduralMeshComponent;
class UMaterialInterface;
class UStaticMesh;

/** Surface anchors are retained alongside the seed: attachment never depends on a client's floor or physics state. */
USTRUCT()
struct FMCCalculusDepositAnchor
{
    GENERATED_BODY()
    UPROPERTY() FVector_NetQuantize10 Center=FVector::ZeroVector;
    UPROPERTY() FVector_NetQuantizeNormal Normal=FVector::ForwardVector;
    UPROPERTY() FVector_NetQuantizeNormal Up=FVector::UpVector;
    UPROPERTY() float Width=45.f;
    UPROPERTY() float Height=32.f;
};

USTRUCT(BlueprintType)
struct FMCCalculusState
{
    GENERATED_BODY()
    UPROPERTY() int32 Seed=1;
    UPROPERTY() int32 GrowthSerial=0;
    UPROPERTY() FVector_NetQuantize100 GrowthScale=FVector::OneVector;
    UPROPERTY() TArray<FMCCalculusDepositAnchor> Anchors;
    // Three stages per piece; zero permanently removes that piece on every peer, including late joins.
    UPROPERTY() TArray<uint8> Pieces;
    UPROPERTY() int32 HitSerial=0;
    UPROPERTY() FVector_NetQuantize10 HitPoint=FVector::ZeroVector;
    UPROPERTY() FVector_NetQuantizeNormal HitNormal=FVector::ForwardVector;
    UPROPERTY() double HitAt=-100;
};

/** Attached, contact-local calculus destruction. Rendering and short-lived debris use two batches and no Chaos bodies. */
UCLASS(ClassGroup=(MessControl), meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCToothCalculusComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCToothCalculusComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

    void RebuildForSurface();
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Care|Calculus") void GrowCalculus(int32 Seed=1,int32 PatchCount=3);
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Care|Calculus") void ClearCalculus();
    bool FindContact(AMCToothCharacter* Worker,FVector& Point,FVector& Normal) const;
    bool ApplyPickaxeHit(AMCToothCharacter* Worker,FVector Point,FVector Normal,float Damage);
    bool ApplyBufferHit(AMCToothCharacter* Worker,float Damage,FVector& HitPoint);
    UFUNCTION(BlueprintPure, Category="Care|Calculus") bool HasCalculus() const;
    UFUNCTION(BlueprintPure, Category="Care|Calculus") int32 RemainingPieces() const;
    UFUNCTION(BlueprintPure, Category="Care|Calculus") float RemainingFraction() const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Care|Calculus")
    TSoftObjectPtr<UMaterialInterface> CalculusMaterial;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Care|Calculus") TObjectPtr<UProceduralMeshComponent> Deposits;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Care|Calculus") TObjectPtr<UProceduralMeshComponent> Debris;
    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Care|Calculus") FMCCalculusState State;

private:
    struct FPiece
    {
        FVector Center=FVector::ZeroVector,Normal=FVector::ForwardVector;
        FVector Up=FVector::UpVector,Side=FVector::RightVector;
        TArray<FVector> Rim;
        float Depth=8.f,Radius=12.f;
        float Variation=.5f;
    };
    struct FShard
    {
        FVector Point=FVector::ZeroVector,Velocity=FVector::ZeroVector;
        FRotator Rotation=FRotator::ZeroRotator,Spin=FRotator::ZeroRotator;
        float Age=0,Lifetime=.7f,Size=3.f;
        FLinearColor Color=FLinearColor::White;
    };
    UFUNCTION() void OnRep_State();
    void EnsureMeshes();
    bool BuildSurfacePieces();
    void BuildDepositMesh();
    void EmitShards(FVector Point,FVector Normal,int32 Serial);
    void UpdateShardMesh();
    bool CanReachContact(const AMCToothCharacter* Worker,FVector Point,FVector Normal,bool bImpact) const;
    FVector PieceContact(int32 Index,FVector& Normal) const;
    TArray<FPiece> SurfacePieces;
    TArray<FShard> Shards;
    UPROPERTY(Transient) TObjectPtr<UStaticMesh> CachedMesh;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> LoadedMaterial;
    int32 CachedGrowth=INDEX_NONE,PlayedHit=0,RetryFrames=0;
    int32 PendingSeed=1,PendingPatches=0,PendingGrowFrames=0;
};
