#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "MCToothStatusComponent.generated.h"

USTRUCT(BlueprintType)
struct FMCToothCareSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Contact", meta=(ClampMin="0.1")) float ContactSeconds=0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Contact", meta=(ClampMin="10")) float Reach=115.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Coffee", meta=(ClampMin="1")) int32 CoffeeContacts=4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Care", meta=(ClampMin="1")) int32 RepairContacts=4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Care", meta=(ClampMin="1")) float HealPerContact=25.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Care", meta=(ClampMin="0",ClampMax="1")) float LooseHealthFraction=0.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player", meta=(ClampMin="1")) float PlayerHealth=100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player") bool bAllowSelfCare=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Respawn", meta=(ClampMin="0.1")) float RespawnSeconds=3.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Respawn") bool bInheritReserveStatus=true;
    void Sanitize();
};

UCLASS(BlueprintType)
class MESSCONTROL_API UMCToothCareProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tooth Care") FMCToothCareSettings Settings;
};

USTRUCT(BlueprintType)
struct FMCToothStatus
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) float Health=100.f;
    UPROPERTY(BlueprintReadOnly) float MaxHealth=100.f;
    UPROPERTY(BlueprintReadOnly) int32 CoffeeLeft=0;
    UPROPERTY(BlueprintReadOnly) int32 CoffeeTotal=4;
    UPROPERTY(BlueprintReadOnly) int32 RepairLeft=0;
    UPROPERTY(BlueprintReadOnly) double ReactionAt=-100.;
    UPROPERTY(BlueprintReadOnly) bool bCareReaction=false;
};

/** The single authoritative status store, shared by a hero and a large arena tooth. */
UCLASS(ClassGroup=(MessControl), meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCToothStatusComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCToothStatusComponent();
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tooth Care") TSoftObjectPtr<UMCToothCareProfile> Profile;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Tooth Care") FMCToothCareSettings Settings;
    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Tooth Care") FMCToothStatus State;
    UFUNCTION(BlueprintPure, Category="Tooth Care") bool IsAlive() const { return State.Health>0; }
    UFUNCTION(BlueprintPure, Category="Tooth Care") bool IsLoose() const { return IsAlive() && State.RepairLeft>0; }
    float CoffeeAmount() const { return float(State.CoffeeLeft)/FMath::Max(1,State.CoffeeTotal); }
    bool NeedsCare(bool bBrush) const;
    void Initialize(float MaxHealth);
    void Restore(const FMCToothStatus& Source);
    void ApplyCoffee(float Amount=1.f);
    void Loosen();
    bool Damage(float Amount,FVector Direction=FVector::ForwardVector);
    // Called only after the worker validates a complete continuous tool contact on the server.
    bool CareContact(bool bBrush);
    FString Summary() const;
    FVector LastDamageDirection=FVector::ForwardVector;
private:
    UFUNCTION() void OnRep_State();
    void Changed(bool bCare);
    bool bInitialized=false;
};
