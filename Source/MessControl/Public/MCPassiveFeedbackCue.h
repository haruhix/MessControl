#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCPassiveFeedbackCue.generated.h"

UENUM()
enum class EMCPassiveFeedbackKind : uint8 { Heal,Enamel,Shield,Wind,Speed,Vanish,Return,Neighbor,PowerHit,Billiards };

/** Bounded cosmetic receipts. Never applies gameplay effects or blocks input. */
UCLASS()
class MESSCONTROL_API AMCPassiveFeedbackCue : public AActor
{
    GENERATED_BODY()
public:
    AMCPassiveFeedbackCue();
    /** Strength is normalized presentation intensity, not health/damage. */
    static AMCPassiveFeedbackCue* Spawn(AActor* InSubject,EMCPassiveFeedbackKind Type,float Intensity=1.f,FVector InDirection=FVector::ZeroVector);
    static void SpawnLink(AActor* InSubject,FVector From,FVector To);
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UProceduralMeshComponent> Mesh;
    UPROPERTY(EditDefaultsOnly) TObjectPtr<class UMaterialInterface> SoftMaterial;
    UPROPERTY(EditDefaultsOnly) TObjectPtr<class UMaterialInterface> GlowMaterial;
    /** Opaque lit volume for the cartoon primary; held by the loading manifest. */
    UPROPERTY(EditDefaultsOnly) TObjectPtr<class UMaterialInterface> SolidMaterial;
    UPROPERTY(Replicated,BlueprintReadOnly) EMCPassiveFeedbackKind Kind=EMCPassiveFeedbackKind::Heal;
    UPROPERTY(Replicated,BlueprintReadOnly) TObjectPtr<AActor> Subject;
    UPROPERTY(Replicated,BlueprintReadOnly) double StartedAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly) float Strength=1;
    UPROPERTY(Replicated) FVector_NetQuantizeNormal Direction=FVector::ForwardVector;
    UPROPERTY(Replicated) FVector_NetQuantize LinkEnd;
private:
    double CueTime() const;
    float Duration() const;
};
