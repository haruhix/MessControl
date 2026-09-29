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
    void Contact(AMCArenaTooth* Tooth,FVector Point,FVector Normal);
    void Release();
    bool CanReach(FVector Point,FVector Normal) const;
    bool CanAcquireSurface(const AMCArenaTooth* Tooth) const;
    UPROPERTY(EditAnywhere, Category="Brush") float SurfaceReach=170.f;
    // World-space travel from the resting wrist, independent of bone lengths.
    UPROPERTY(EditAnywhere, Category="Brush", meta=(ClampMin="0")) float MaxHandTravel=90.f;
    void BuildPose(TArray<FTransform>& Pose,const FReferenceSkeleton& Ref,float Dt) const;
    FVector ContactPoint() const;
    FVector ContactNormal() const;
    FVector BristlePoint() const;
    float Alpha() const { return Blend; }
    bool IsPresenting() const { return Blend>.001f || bHandPresented; }
    UPROPERTY(Replicated) TObjectPtr<AMCArenaTooth> Target;
    UPROPERTY(Replicated) FVector_NetQuantize10 LocalPoint=FVector::ZeroVector;
    UPROPERTY(Replicated) FVector_NetQuantizeNormal LocalNormal=FVector::UpVector;
    UPROPERTY(Replicated) double ContactAt=-100;
private:
    FTransform HandGoal(FVector Point,FVector Normal) const;
    UPROPERTY() TObjectPtr<AMCToothCharacter> Hero;
    UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> Foam;
    struct FBubble { FVector Position,Velocity; float Age=0,Life=1,Radius=1; };
    TArray<FBubble> Bubbles;
    float Blend=0,SpawnClock=0;
    mutable FTransform PresentedHand=FTransform::Identity;
    mutable FTransform PresentationBase=FTransform::Identity;
    mutable bool bHandPresented=false;
    FRandomStream Random{781};
};
