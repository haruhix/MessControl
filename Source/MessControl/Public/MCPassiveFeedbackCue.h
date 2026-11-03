#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCVFXNativePresentationSettings.h"
#include "MCPassiveFeedbackCue.generated.h"

UENUM()
enum class EMCPassiveFeedbackKind : uint8 { Heal,Enamel,Shield,Wind,Speed,Vanish,Return,Neighbor,PowerHit,Billiards };
enum class EMCVFXPresentationResult : uint8;
class AMCToothCharacter;

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
    /** One already-accepted gameplay event. Hero is the profile owner; Subject may be the struck enemy. */
    static AMCPassiveFeedbackCue* SpawnAccepted(AMCToothCharacter* SourceHero,FName SourceID,FName Event,
        EMCVFXPresentationResult Result,AActor* InSubject,EMCPassiveFeedbackKind Type,float Intensity=1.f,FVector InDirection=FVector::ZeroVector);
    static void SpawnLinkAccepted(AMCToothCharacter* SourceHero,FName SourceID,FName Event,
        EMCVFXPresentationResult Result,FVector From,FVector To);
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
    UPROPERTY(Replicated,BlueprintReadOnly) FName SourcePerkID;
    UPROPERTY(Replicated,BlueprintReadOnly) FName AcceptedEvent;
    UPROPERTY(Replicated) TObjectPtr<AMCToothCharacter> PresentationOwner;
    /** Only the saved source token is transported; local prepared resources never replicate. */
    UPROPERTY(ReplicatedUsing=OnRep_NativePresentation) FMCVFXNativePresentationRef NativePresentation;
    UPROPERTY(Replicated) FGuid PresentationEpoch;
    const FMCVFXPreparedNativePresentation& GetPreparedNativePresentation()const{return PreparedNative;}
private:
    static AMCPassiveFeedbackCue* SpawnInternal(AActor* Subject,EMCPassiveFeedbackKind Type,float Intensity,FVector Direction,
        AMCToothCharacter* SourceHero,FName SourceID,FName Event,const FMCVFXPreparedNativePresentation* Prepared);
    bool ResolveNativePresentation();
    void ApplyPreparedMaterials();
    float CosmeticTimeScale() const;
    UFUNCTION() void OnRep_NativePresentation();
    UPROPERTY(Transient) FMCVFXPreparedNativePresentation PreparedNative;
    UPROPERTY(Transient) TArray<TObjectPtr<class UMaterialInstanceDynamic>> RetainedMaterials;
    double CueTime() const;
    float Duration() const;
};
