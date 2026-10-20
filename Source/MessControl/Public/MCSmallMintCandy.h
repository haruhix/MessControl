#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCSmallMintCandy.generated.h"

class AMCTongue;
class AMCToothCharacter;
class AMCHazardWave;
class UStaticMeshComponent;
class UMCFrostNovaVFXComponent;

USTRUCT(BlueprintType)
struct FMCSmallMintCandySettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1")) float HealthPerPlayer=180.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="40",ClampMax="250",Units="cm")) float Diameter=135.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin=".5",ClampMax="5",Units="s")) float FallSeconds=1.4f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="200",ClampMax="2000",Units="cm")) float FallHeight=850.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="3",ClampMax="5",Units="s")) float FirstWaveMinSeconds=3.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="3",ClampMax="5",Units="s")) float FirstWaveMaxSeconds=5.f;
    /** Recovery after the previous front has crossed the arena. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",ClampMax="20",Units="s")) float WaveRestMinSeconds=3.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",ClampMax="20",Units="s")) float WaveRestMaxSeconds=5.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="200",ClampMax="6000",Units="cm")) float WaveRadius=2600.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",ClampMax="6",Units="s")) float WaveTravelSeconds=2.8f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",ClampMax="1000")) float FootIceHealth=80.f;
    void Sanitize();
};

UENUM(BlueprintType)
enum class EMCSmallMintStage : uint8 { Idle, Falling, Active, Complete, Failed };

/** Small breakable mint. Its only attack is a jumpable, non-damaging foot-freeze front. */
UCLASS()
class MESSCONTROL_API AMCSmallMintCandy : public AActor
{
    GENERATED_BODY()
public:
    AMCSmallMintCandy();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Events|Mint")
    bool Start(AMCTongue* Surface,const FMCSmallMintCandySettings& Config,int32 Seed);
    bool HitWithPickaxe(AMCToothCharacter* Worker,float Damage);
    FVector ContactPoint(FVector From) const;
    bool IsComplete() const { return Stage==EMCSmallMintStage::Complete; }
    bool HasFailed() const { return Stage==EMCSmallMintStage::Failed; }
    void Stop();
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Body;
    /** One local presenter is prepared during the fall and reused by non-overlapping pulses. */
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UMCFrostNovaVFXComponent> FrostNovaVFX;
    UPROPERTY(ReplicatedUsing=OnRep_State,BlueprintReadOnly) EMCSmallMintStage Stage=EMCSmallMintStage::Idle;
    UPROPERTY(Replicated,BlueprintReadOnly) FMCSmallMintCandySettings Settings;
    UPROPERTY(Replicated,BlueprintReadOnly) TObjectPtr<AMCTongue> Tongue;
    UPROPERTY(Replicated,BlueprintReadOnly) FVector Anchor=FVector::ZeroVector;
    UPROPERTY(Replicated,BlueprintReadOnly) FVector LandingPoint=FVector::ZeroVector;
    UPROPERTY(Replicated,BlueprintReadOnly) double StartedAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly) double LandedAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly) double NextWaveAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly) float Health=0;
    UPROPERTY(Replicated,BlueprintReadOnly) float MaxHealth=0;
    UPROPERTY(Replicated,BlueprintReadOnly) int32 WavesEmitted=0;
private:
    UFUNCTION() void OnRep_State();
    UFUNCTION(NetMulticast,Reliable) void Multicast_Shatter(FVector Point);
    void UpdatePresentation();
    void RecordFrozenHero(AMCToothCharacter* Hero);
    double Now() const;
    FRandomStream Random;
    TArray<TWeakObjectPtr<AMCHazardWave>> Waves;
    TSet<TWeakObjectPtr<AMCToothCharacter>> FrozenHeroes;
};
