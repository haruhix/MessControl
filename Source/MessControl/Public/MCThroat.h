#pragma once
#include "CoreMinimal.h"
#include "MCFoodActor.h"
#include "MCThroat.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USkeletalMeshComponent;

UENUM(BlueprintType)
enum class EMCThroatPhase : uint8 { Collecting, Anticipation, Swallowing, Recovering, Spasm, Vomiting };

/** A breathing throat with automatic food intake and a decorative uvula. */
UCLASS(Blueprintable)
class MESSCONTROL_API AMCThroat : public AMCFoodDisposal
{
    GENERATED_BODY()
public:
    AMCThroat();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UProceduralMeshComponent> Tissue;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<USkeletalMeshComponent> SculptedTissue;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<USkeletalMeshComponent> AuthoredMouth;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Uvula;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UBoxComponent> UvulaLanding;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> ClosedBarrier;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UProceduralMeshComponent> ZoneRing;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Throat|Art") TObjectPtr<UMaterialInterface> TissueMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Throat|Art") TObjectPtr<UMaterialInterface> RingMaterial;
    // The current artist's Open target collapses the aperture rather than opening it.
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Throat|Art") bool bReverseAuthoredOpen=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Throat|Shape") FVector GateCenter=FVector(150,0,-200);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Throat|Shape") FVector2D GateSize=FVector2D(500,500);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Throat|Shape") FVector UvulaTop=FVector(-120,0,430);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Throat|Shape") float UvulaLength=190;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Throat|Zone") FVector ZoneCenter=FVector(-280,0,-40);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Throat|Zone",meta=(ClampMin="80")) float ZoneRadius=290;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Throat|Zone",meta=(ClampMin="40")) float ZoneHeight=240;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Throat|Timing",meta=(ClampMin="0.1")) float PressSeconds=.48f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Throat|Timing",meta=(ClampMin="1")) float AnticipationSeconds=3.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Throat|Timing",meta=(ClampMin="0.5")) float SwallowSeconds=1.65f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Throat|Timing",meta=(ClampMin="0.2")) float RecoverySeconds=1.1f;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Throat") EMCThroatPhase ThroatPhase=EMCThroatPhase::Collecting;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Throat") double PhaseStartedAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Throat") float Weight=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Throat") int32 FoodInZone=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Throat") int32 SwallowCount=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Throat") int32 FoodSwallowed=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Throat") int32 SpasmCount=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Throat") int32 VomitCount=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Throat") int32 MealSequence=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Throat|Timing") float SpasmSeconds=1.2f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Throat|Timing",meta=(ClampMin="2")) float VomitSeconds=2.2f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Throat|Art") FVector VomitOrigin=FVector(-70,0,130);
    UFUNCTION(BlueprintPure,Category="Throat") bool ContainsFood(const AMCFoodActor* Food) const;
    UFUNCTION(BlueprintPure,Category="Throat") float OpenAmount() const;
    /** A point inside the visible artist aperture, above the tongue surface. */
    FVector VacuumInlet() const;
    UFUNCTION(CallInEditor,Category="Throat") void RebuildAppearance();
    void NotifyUvulaLanding(AMCToothCharacter* Hero,const FHitResult& Hit,float DownSpeed);
    bool ContainsPlayer(const AMCToothCharacter* Hero) const;
    bool CanOrderJump(const AMCToothCharacter* Hero) const;
    /** Conservative clearance from the authored uvula profile, including body margin. */
    float UvulaBodyClearance(FVector Center,float Radius,float HalfHeight) const;
    bool LaunchToUvula(AMCToothCharacter* Hero);
    void ResetSwallow();
private:
    double ServerNow() const;
    void SetPhase(EMCThroatPhase Phase,double At);
    void UpdateTissue(float Open,float Time,bool Rebuild=false);
    void UpdatePresentation(float Dt);
    void BuildRing();
    void CaptureMeal();
    void BeginVomit();
    bool OrderVelocity(const AMCToothCharacter* Hero,FVector& Velocity) const;
    void UpdateOrderJumps();
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RingMID;
    TArray<TWeakObjectPtr<AMCToothCharacter>> LandedPlayers;
    TArray<TWeakObjectPtr<AMCToothCharacter>> PreparingPlayers;
    struct FMealPiece { TWeakObjectPtr<AMCFoodActor> Food; FVector Start; FQuat Rotation; };
    TArray<FMealPiece> Meal;
    struct FSwallowedPlayer { TWeakObjectPtr<AMCToothCharacter> Hero; FVector Start; };
    TArray<FSwallowedPlayer> SwallowedPlayers;
    TWeakObjectPtr<class AMCVomitBurst> ActiveVomit;
    void SpitOut(bool Reset=false);
    float PressTime=0,VisualWeight=0,GeometryElapsed=0,RingElapsed=0;
    bool bPressConsumed=false;
};
