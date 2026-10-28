#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameFramework/Actor.h"
#include "MCDefenseSprayPassives.generated.h"

class AMCToothCharacter;
class UProceduralMeshComponent;
class UMaterialInterface;
class UMCPerkComponent;

/** Visual only. The world subsystem owns the bounded surface treatment and damage clock. */
UCLASS()
class MESSCONTROL_API AMCSprayCoating : public AActor
{
    GENERATED_BODY()
public:
    AMCSprayCoating();
    virtual void BeginPlay() override;
    void SetRadius(float InRadius);
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Surface;
    UPROPERTY(EditDefaultsOnly) TObjectPtr<UMaterialInterface> CoatingMaterial;
private:
    UPROPERTY(ReplicatedUsing=OnRep_Radius,VisibleInstanceOnly) float Radius=40.f;
    UFUNCTION() void OnRep_Radius();
};

/** Transient run effects stay with their actual pawn and world, never in copied PlayerState data. */
UCLASS()
class MESSCONTROL_API UMCDefenseSprayPassives : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    static UMCDefenseSprayPassives* ForWorld(const UObject* Context);
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickable() const override;
    virtual void Deinitialize() override;
    static float ResolveIncomingDamage(AMCToothCharacter* Hero,float Amount);
    static void RegisterThreat(AMCToothCharacter* Hero,AActor* Source,uint64 AttackID);
    static void NotifyDashStarted(AMCToothCharacter* Hero);
    static void NotifyIncomingHit(AMCToothCharacter* Hero,AActor* Source);
    static void NotifyDodgedAttack(AMCToothCharacter* Hero,AActor* Source,uint64 AttackID);
    static void NotifyAttackFinished(AActor* Source,uint64 AttackID,bool bCanceled=false);
    static void UpdateDashVisibility(AMCToothCharacter* Hero);
    static float BrushMultiplier(const AActor* Target,FVector Point);
    static void ApplySpray(AMCToothCharacter* Hero,FVector Origin,FVector Direction,float Reach,float Seconds);
    static void PruneOwner(const UMCPerkComponent* Owner);
    static void ResetOwner(const UMCPerkComponent* Owner);
    /** Static geometry predicate shared by the visual disk and its standing-actor hazard. */
    static bool IsStandingInCoating(const AActor* Target,FVector Point,FVector Normal,float Radius);
private:
    struct FThreat
    {
        TWeakObjectPtr<AMCToothCharacter> Hero;
        TWeakObjectPtr<UMCPerkComponent> Owner;
        TWeakObjectPtr<AActor> Source;
        uint64 AttackID=0;
        double StartedAt=0,ExpiresAt=0;
        bool bDashed=false,bResolved=false;
    };
    struct FSoftened
    {
        TWeakObjectPtr<AMCToothCharacter> Worker;
        TWeakObjectPtr<UMCPerkComponent> Owner;
        TWeakObjectPtr<AActor> Target;
        FVector LocalPoint=FVector::ZeroVector;
        float Radius=0;
        double Until=0;
    };
    struct FCoating
    {
        TWeakObjectPtr<AMCToothCharacter> Worker;
        TWeakObjectPtr<UMCPerkComponent> Owner;
        TWeakObjectPtr<AActor> Target;
        TWeakObjectPtr<AMCSprayCoating> Visual;
        FVector LocalPoint=FVector::ZeroVector,LocalNormal=FVector::UpVector;
        FVector Point=FVector::ZeroVector,Normal=FVector::UpVector;
        float Radius=0;
        double CleanUntil=0,Until=0,LastCleanAt=0;
    };
    struct FHiddenOverride { TWeakObjectPtr<UMCPerkComponent> Owner;bool bWasHidden=false; };
    TArray<FThreat> Threats;
    TArray<FSoftened> Softened;
    TArray<FCoating> Coatings;
    TMap<TWeakObjectPtr<AMCToothCharacter>,FHiddenOverride> HiddenOverrides;
    TMap<TWeakObjectPtr<AMCToothCharacter>,double> NextSprayAt;
    TMap<TWeakObjectPtr<AMCToothCharacter>,float> SpraySeconds;
    double NextTreatmentAt=0,LastTreatmentAt=0,NextDamageAt=0,LastDamageAt=0;
    void ResolveThreat(FThreat& Threat,bool bDodged);
    void StampTreatment(AMCToothCharacter* Hero,AActor* Target,FVector Point,FVector Normal,float Radius);
    void DestroyCoating(FCoating& Coating);
};
