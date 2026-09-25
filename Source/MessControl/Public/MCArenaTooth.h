#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "MCArenaTooth.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UTextRenderComponent;
class UStaticMesh;

USTRUCT()
struct FMCArenaToothAppearance
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<UStaticMesh> Mesh;
    UPROPERTY() FVector MeshScale=FVector(2.15,2.15,2.45);
};

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCArenaToothSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Health", meta=(ClampMin="1")) float MaxHealth=100;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Health", meta=(ClampMin="0",ClampMax="1")) float LooseHealthFraction=0.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Health", meta=(ClampMin="0")) float BrushHitDamage=25;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation", meta=(ClampMin="0",ClampMax="0.4")) float HitSquash=0.18f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation", meta=(ClampMin="0",ClampMax="25")) float WobbleDegrees=12;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation", meta=(ClampMin="0.2",ClampMax="3")) float ReactionSeconds=0.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fall", meta=(ClampMin="0",ClampMax="1")) float FallAnticipation=0.25f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fall", meta=(ClampMin="0",ClampMax="1000")) float FallLift=310;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fall", meta=(ClampMin="0",ClampMax="1000")) float FallSpeed=220;
    void Sanitize();
};

/** Defaults for arena teeth only. Shared player statuses/cleaning are the next step. */
UCLASS(BlueprintType)
class MESSCONTROL_API UMCArenaToothProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena Tooth") FMCArenaToothSettings Settings;
};

/** One replicated snapshot also reconstructs visuals for a late-joining client. */
USTRUCT(BlueprintType)
struct FMCArenaToothState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) int32 ToothId=0;
    UPROPERTY(BlueprintReadOnly) float Health=100;
    UPROPERTY(BlueprintReadOnly) float Coffee=0;
    UPROPERTY(BlueprintReadOnly) bool bLost=false;
    UPROPERTY() double HitTime=-100;
    UPROPERTY() FVector HitDirection=FVector(0,1,0);
    UPROPERTY() int32 HitSerial=0;
};

/** Root box is the physical body; only the child mesh squashes/wobbles. */
UCLASS()
class MESSCONTROL_API AMCArenaTooth : public AActor
{
    GENERATED_BODY()
public:
    AMCArenaTooth();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    void Initialize(int32 Id,const FMCArenaToothSettings& Defaults);
    // Call before FinishSpawning. The actor root is the mesh bounds centre, independent of its asset pivot.
    void SetAppearance(UStaticMesh* Mesh,FVector Scale);
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Arena Tooth") bool ReceiveArenaHit(float Damage,FVector Direction);
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Arena Tooth") void SetCoffee(float Amount);
    UFUNCTION(BlueprintPure, Category="Arena Tooth") bool IsAvailable() const { return !State.bLost && State.Health>0; }
    UFUNCTION(BlueprintPure, Category="Arena Tooth") bool IsLoose() const { return IsAvailable() && State.Health<=Settings.MaxHealth*Settings.LooseHealthFraction; }
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Arena Tooth") FMCArenaToothState State;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Arena Tooth") FMCArenaToothSettings Settings;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UBoxComponent> Body;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UTextRenderComponent> Label;
private:
    UFUNCTION() void ApplyAppearance();
    UPROPERTY(ReplicatedUsing=ApplyAppearance) FMCArenaToothAppearance Appearance;
    UFUNCTION() void OnBodyHit(UPrimitiveComponent* Component,AActor* Other,UPrimitiveComponent* OtherComponent,FVector Impulse,const FHitResult& Hit);
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
    FVector MeshBaseLocation=FVector::ZeroVector;
    FVector MeshBaseScale=FVector::OneVector;
    double LastPhysicsHit=-100;
    bool bFallStarted=false;
};
