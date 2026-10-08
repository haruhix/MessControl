#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCFogBrawlEvent.generated.h"

class AMCArenaTooth;
class AMCToothCharacter;
class UProceduralMeshComponent;
class UMaterialInterface;
class UMCFogBrawlPresentationComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UENUM(BlueprintType)
enum class EMCFogBrawlStage : uint8 { Idle, SmokeIn, Warning, Recovery, Complete };

/** Server-owned strikes against the existing reserve teeth. Clients reconstruct local tells from this state. */
UCLASS()
class MESSCONTROL_API AMCFogBrawlEvent : public AActor
{
    GENERATED_BODY()
public:
    AMCFogBrawlEvent();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Fog Brawl") void Start();
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Fog Brawl") void Stop();
    UFUNCTION(BlueprintPure, Category="Fog Brawl") bool IsActive() const;
    UFUNCTION(BlueprintPure, Category="Fog Brawl") bool IsComplete() const { return Stage==EMCFogBrawlStage::Complete && !bFailed; }
    UFUNCTION(BlueprintPure, Category="Fog Brawl") bool IsGuarding(const AMCToothCharacter* Hero) const;
    UFUNCTION(BlueprintPure, Category="Fog Brawl") bool ShouldRevealMarker(const AMCToothCharacter* Hero) const;
    UFUNCTION(BlueprintPure, Category="Fog Brawl") float WarningRemaining() const;

    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Fog Brawl") EMCFogBrawlStage Stage=EMCFogBrawlStage::Idle;
    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Fog Brawl") TObjectPtr<AMCArenaTooth> ActiveTarget;
    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Fog Brawl") double StartedAt=0;
    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Fog Brawl") double StageEndsAt=0;
    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Fog Brawl") double WarningEndsAt=0;
    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Fog Brawl") int32 StrikesResolved=0;
    UPROPERTY(EditAnywhere, ReplicatedUsing=OnRep_State, BlueprintReadWrite, Category="Fog Brawl", meta=(ClampMin="1",ClampMax="20")) int32 TotalStrikes=5;
    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Fog Brawl") int32 GuardCount=0;
    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Fog Brawl") int32 LastGuardCount=0;
    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Fog Brawl") int32 LastLivingTeamCount=0;
    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Fog Brawl") float LastDamagePerGuard=0;
    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Fog Brawl") float LastImpulse=0;
    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Fog Brawl") bool bLastToothSaved=false;
    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Fog Brawl") bool bFailed=false;
    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Fog Brawl") double LastImpactAt=-100;
    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Fog Brawl") FVector LastImpactLocation=FVector::ZeroVector;

    UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category="Fog Brawl", meta=(ClampMin="0.1")) float SmokeInSeconds=3;
    UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category="Fog Brawl", meta=(ClampMin="1")) float WarningSeconds=4.8f;
    UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category="Fog Brawl", meta=(ClampMin="0.5")) float RecoverySeconds=3;
    UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category="Fog Brawl", meta=(ClampMin="80")) float GuardRadius=260;
    UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category="Fog Brawl", meta=(ClampMin="100")) float MarkerRevealDistance=700;
    UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category="Fog Brawl", meta=(ClampMin="1")) float ReachSeconds=5;
    UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category="Fog Brawl", meta=(ClampMin="0")) float DamagePerLivingPlayer=15;
    UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category="Fog Brawl", meta=(ClampMin="0")) float FullTeamImpulse=280;
    UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category="Fog Brawl", meta=(ClampMin="0")) float MaxImpulse=1100;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fog Brawl|Presentation") TObjectPtr<UProceduralMeshComponent> Pulse;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fog Brawl|Presentation") TObjectPtr<UStaticMeshComponent> TargetGlow;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fog Brawl|Presentation") TObjectPtr<UMCFogBrawlPresentationComponent> SmokePresentation;

private:
    UFUNCTION() void OnRep_State();
    double Now() const;
    void BeginWarning();
    void ResolveStrike();
    void Finish(bool bFailure);
    void UpdatePresentation();
    void ClearPresentation();
    void PublishManualHUD();
    bool HasClearApproach(const AMCToothCharacter* Hero,const AMCArenaTooth* Target) const;
    FVector ClosestTargetPoint(const AMCToothCharacter* Hero,const AMCArenaTooth* Target) const;
    bool IsLivingTeamMember(const AMCToothCharacter* Hero) const;
    void FindGuards(TArray<AMCToothCharacter*>& Out) const;
    UPROPERTY() TObjectPtr<UMaterialInterface> PulseMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> ToothPulseMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ToothPulseMID;
    FRandomStream Random;
};
