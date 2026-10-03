#pragma once
#include "CoreMinimal.h"
#include "NiagaraActor.h"
#include "MCAmbientParticles.generated.h"

class AMCToothCharacter;

/** Local presentation: player movement and the replicated mouth drive Niagara air. */
UCLASS(Blueprintable)
class MESSCONTROL_API AMCAmbientParticles : public ANiagaraActor
{
    GENERATED_BODY()
public:
    AMCAmbientParticles(const FObjectInitializer& ObjectInitializer=FObjectInitializer::Get());
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Particles|Player Reaction",meta=(ClampMin="1",Units="cm"))
    float ReactionRadius=260.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Particles|Player Reaction",meta=(ClampMin="0",Units="cm/s"))
    float RunPushSpeed=95.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Particles|Player Reaction",meta=(ClampMin="0",Units="cm/s"))
    float MinimumMoveSpeed=60.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Particles|Player Reaction",meta=(ClampMin="1",Units="cm/s"))
    float FullReactionSpeed=450.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Particles|Player Reaction",meta=(ClampMin="0.1"))
    float ReactionSmoothing=8.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Particles|Mouth Airflow",meta=(ClampMin="0",Units="cm/s"))
    float OpenMouthAirSpeed=95.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Particles|Mouth Airflow",meta=(ClampMin="1"))
    float OpenMouthSpeedMultiplier=4.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Particles|Mouth Airflow",meta=(ClampMin="0.1"))
    float MouthSmoothing=6.f;

    UPROPERTY(Transient,VisibleInstanceOnly,BlueprintReadOnly,Category="Particles|Runtime")
    float MouthOpenAmount=0;
    UPROPERTY(Transient,VisibleInstanceOnly,BlueprintReadOnly,Category="Particles|Runtime")
    FVector CurrentAirVelocity=FVector(4,-2,5);
    UPROPERTY(Transient,VisibleInstanceOnly,BlueprintReadOnly,Category="Particles|Runtime")
    int32 ActiveInteractionCount=0;

private:
    static constexpr int32 MaxPlayers=8;
    TWeakObjectPtr<AMCToothCharacter> PlayerSlots[MaxPlayers];
    FVector SmoothedVelocities[MaxPlayers]{};
};
