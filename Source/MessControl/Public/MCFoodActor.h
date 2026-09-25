#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "MCDayPlan.h"
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
enum class EMCFoodPhase : uint8 { Falling, Stuck, Free, Disposed, Equipped };

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
    void ConfigureItem(FName Name,const FMCFoodRow& Row,FRandomStream& Random,bool Fragment=false);
    void ConfigureBrush();
    bool HitFood(float Damage,FVector Direction);
    void Throw(AMCToothCharacter* Hero);
    float DragSpeed() const;
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
    UPROPERTY(ReplicatedUsing=OnRep_Item, BlueprintReadOnly) TObjectPtr<UStaticMesh> ItemMesh;
    UPROPERTY(Replicated, BlueprintReadOnly) FMCFoodRow FoodData;
    UPROPERTY(Replicated, BlueprintReadOnly) FName ItemName;
    UPROPERTY(Replicated, BlueprintReadOnly) float Health=75;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bFragment=false;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bBrushTool=false;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bSpoiled=false;
    UPROPERTY(Replicated, BlueprintReadOnly) double SpoilAt=0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 Batch=0;
    UPROPERTY(Replicated, BlueprintReadOnly) TObjectPtr<AMCToothCharacter> EquippedBy;
    UPROPERTY(Replicated, BlueprintReadOnly) TObjectPtr<AActor> StuckTooth;
    int32 ConfirmedImpacts=0;
private:
    UFUNCTION() void OnRep_Phase();
    UFUNCTION() void OnRep_Item();
    UFUNCTION() void OnHit(UPrimitiveComponent* Component,AActor* Other,UPrimitiveComponent* OtherComponent,FVector Impulse,const FHitResult& Hit);
    bool bJamOnLanding=false;
    bool bLandingPending=false;
    float LastPullTime=0;
    // Sampled in the actor's PrePhysics tick, before contact impulses change velocity.
    FVector PrePhysicsVelocity=FVector::ZeroVector;
    TMap<TWeakObjectPtr<AActor>,double> LastHit;
    TMap<TWeakObjectPtr<AMCToothCharacter>,FVector> GripOffsets;
    void Spoil();
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
    UPROPERTY(EditAnywhere, Replicated) bool bBrushBin=false;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
private:
    bool bExitConfigured=false;
};
