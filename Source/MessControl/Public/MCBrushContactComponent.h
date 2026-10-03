#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "MCBrushContactComponent.generated.h"

class AMCArenaTooth;
class AMCToothCharacter;
struct FReferenceSkeleton;

/** One surface contact drives the cleaning mask, the hand and the foam on every peer. */
UCLASS()
class MESSCONTROL_API UMCBrushContactComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCBrushContactComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    void Contact(AActor* Surface,FVector Point,FVector Normal,const FVector* FacingPoint=nullptr);
    void Release();
    bool CanReach(FVector Point,FVector Normal,const AActor* Surface=nullptr) const;
    bool CanReachAfterFacing(FVector Point,FVector Normal,const AActor* Surface=nullptr) const;
    bool CanAcquireSurface(const AActor* Surface) const;
    bool CanBrushToward(FVector Point) const;
    bool WantsFacing(FVector& Direction) const;
    bool IsFacingContact() const;
    UPROPERTY(EditAnywhere, Category="Brush") float SurfaceReach=210.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Brush|VFX") TSoftObjectPtr<class UNiagaraSystem> FoamSystem;
    // World-space travel from the resting wrist, independent of bone lengths.
    UPROPERTY(EditAnywhere, Category="Brush", meta=(ClampMin="1")) float MaxHandTravel=135.f;
    UPROPERTY(EditAnywhere, Category="Brush", meta=(ClampMin="1")) float MaxHandVerticalTravel=250.f;
    FVector ClampHandOffset(FVector Offset) const;
    void BuildPose(TArray<FTransform>& Pose,const FReferenceSkeleton& Ref,float Dt) const;
    FVector ContactPoint() const;
    FVector ContactNormal() const;
    FVector BristlePoint() const;
    bool IsTouchingSurface() const;
    // Gameplay contact uses server time and geometry, independent of rendered bones.
    bool IsWorkReady() const;
    float Alpha() const { return Blend; }
    bool IsPresenting() const { return Blend>.001f || bHandPresented; }
    UPROPERTY(Replicated) TObjectPtr<AActor> Target;
    UPROPERTY(Replicated) FVector_NetQuantize10 LocalPoint=FVector::ZeroVector;
    UPROPERTY(Replicated) FVector_NetQuantize10 LocalFacingPoint=FVector::ZeroVector;
    UPROPERTY(Replicated) FVector_NetQuantizeNormal LocalNormal=FVector::UpVector;
    UPROPERTY(Replicated) double ContactAt=-100;
    UPROPERTY(Replicated) double ApproachStartedAt=-100;
private:
    FTransform HandGoal(FVector Point,FVector Normal,bool* Reachable=nullptr,const AActor* Surface=nullptr,const FTransform* FacingWorld=nullptr) const;
    FTransform SurfaceTransform() const;
    UPROPERTY() TObjectPtr<AMCToothCharacter> Hero;
    TWeakObjectPtr<class AMCTongue> Tongue;
    UPROPERTY() TObjectPtr<class UNiagaraComponent> Foam;
    bool bFoamEmitting=false;
    float Blend=0;
    mutable FTransform PresentedHand=FTransform::Identity;
    mutable FTransform PresentationBase=FTransform::Identity;
    mutable bool bHandPresented=false;
};
