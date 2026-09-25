#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "MCFoodActor.generated.h"
class UBoxComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class AMCToothCharacter;

USTRUCT(BlueprintType)
struct FMCFoodSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Drop") float DropHeight=650;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Drop") float Mass=9;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Impact") float ImpactSpeed=180;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Impact") float DamagePerSpeed=0.035f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Impact") float MaxDamage=40;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Impact") float Knockback=560;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Impact") float HitCooldown=0.75f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grab") float GrabReach=145;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grab") float BreakDistance=340;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grab") float Spring=14;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grab") float Damping=6;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pull") float PullSeconds=3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pull") float PullConeDegrees=35;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pull") float CooperationMultiplier=1.5f;
    void Sanitize();
};
UCLASS(BlueprintType)
class MESSCONTROL_API UMCFoodProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Food") FMCFoodSettings Settings;
};
UENUM(BlueprintType)
enum class EMCFoodPhase : uint8 { Falling, Stuck, Free, Disposed };

/** Server-simulated rigid food. Clients receive motion and interaction state. */
UCLASS(Blueprintable)
class MESSCONTROL_API AMCFoodActor : public AActor
{
    GENERATED_BODY()
public:
    AMCFoodActor();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    void Initialize(bool bJam,FVector ExtractionDirection);
    bool TryGrab(AMCToothCharacter* Hero);
    void Release(AMCToothCharacter* Hero);
    void Dispose();
    bool IsDisposed() const { return Phase==EMCFoodPhase::Disposed; }
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UBoxComponent> Body;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UTextRenderComponent> Label;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Food") TSoftObjectPtr<UMCFoodProfile> Profile;
    UPROPERTY(Replicated, BlueprintReadOnly) FMCFoodSettings Settings;
    UPROPERTY(ReplicatedUsing=OnRep_Phase, BlueprintReadOnly) EMCFoodPhase Phase=EMCFoodPhase::Falling;
    UPROPERTY(Replicated, BlueprintReadOnly) float PullProgress=0;
    UPROPERTY(Replicated, BlueprintReadOnly) FVector PullDirection=FVector(0,1,0);
    UPROPERTY(Replicated, BlueprintReadOnly) TArray<TObjectPtr<AMCToothCharacter>> Holders;
    int32 ConfirmedImpacts=0;
private:
    UFUNCTION() void OnRep_Phase();
    UFUNCTION() void OnHit(UPrimitiveComponent* Component,AActor* Other,UPrimitiveComponent* OtherComponent,FVector Impulse,const FHitResult& Hit);
    bool bJamOnLanding=false;
    bool bLandingPending=false;
    float LastPullTime=0;
    FVector PreviousVelocity=FVector::ZeroVector;
    TMap<TWeakObjectPtr<AActor>,double> LastHit;
    TMap<TWeakObjectPtr<AMCToothCharacter>,FVector> GripOffsets;
};

/** Replaceable level marker: ordinary food is disposed towards the throat. */
UCLASS()
class MESSCONTROL_API AMCFoodDisposal : public AActor
{
    GENERATED_BODY()
public:
    AMCFoodDisposal();
    virtual void Tick(float Dt) override;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Volume;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Label;
};
