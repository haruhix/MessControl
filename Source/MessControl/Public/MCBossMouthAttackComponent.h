#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCBossProfile.h"
#include "MCBossMouthAttackComponent.generated.h"
class AMCBossCharacter;
class AMCBossClot;
class AMCMouthSurface;
class AMCReactionVFX;
class AMCToothCharacter;

/** Authority owns the salvo, damage budget and two persistent, treatable ulcer slots. */
UCLASS(ClassGroup=(MessControl),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCBossMouthAttackComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCBossMouthAttackComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* Tick) override;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Mouth") FName MouthBone=TEXT("c_jawbone_x");
    /** Offset in the actor's axes from the jaw, in centimetres. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Mouth") FVector MouthOffset=FVector(12,0,7);
    UFUNCTION(BlueprintPure,Category="Mouth") FVector GetMouthLocation() const;
    UFUNCTION(BlueprintPure,Category="Mouth") int32 GetActiveUlcerCount() const;
    void StartAttack(const FMCBossAttackDefinition& Attack);
    void StopAttack(bool bClearUlcers=false);
    void ResolveClotHit(int32 Serial,const FHitResult& Hit);
    bool IsSalvoValid(int32 Serial) const;
private:
    void EmitClot(int32 Index);
    void PushWind(float Dt);
    void ClearWind();
    TWeakObjectPtr<AMCBossCharacter> Boss;
    TWeakObjectPtr<AMCReactionVFX> Wind;
    TArray<TWeakObjectPtr<AMCBossClot>> Clots;
    TArray<TWeakObjectPtr<AMCMouthSurface>> Ulcers;
    TMap<TWeakObjectPtr<AMCToothCharacter>,float> DamageDealt;
    TMap<TWeakObjectPtr<AMCToothCharacter>,uint16> WindSources;
    FMCBossAttackDefinition Definition;
    double StartedAt=-1000;
    int32 SalvoSerial=INDEX_NONE,Emitted=0;
    bool bEmitting=false;
};
