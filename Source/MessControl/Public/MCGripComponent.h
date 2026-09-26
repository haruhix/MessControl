#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "Engine/NetSerialization.h"
#include "MCGripComponent.generated.h"
class AMCToothCharacter;
class AMCFoodActor;
struct FReferenceSkeleton;

UENUM(BlueprintType)
enum class EMCGripPose : uint8 { FrontPull, Push, LeftHand, RightHand, RearPull, Carry };

USTRUCT(BlueprintType)
struct FMCGripSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing",meta=(ClampMin="0.12",ClampMax="0.6")) float ReachSeconds=.28f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing",meta=(ClampMin="0.1",ClampMax="0.6")) float ReleaseSeconds=.22f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reach",meta=(ClampMin="0",ClampMax="24")) float BodyReach=18;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reach",meta=(ClampMin="0",ClampMax="30")) float CrouchDepth=25;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reach",meta=(ClampMin="0",ClampMax="10")) float ContactTolerance=6;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reach",meta=(ClampMin="1",ClampMax="1.15")) float MaxArmStretch=1.08f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reach",meta=(ClampMin="2",ClampMax="20")) float BreakSlack=14;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reach",meta=(ClampMin="1",ClampMax="2")) float DragDistanceScale=1.7f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Carry",meta=(ClampMin="0",ClampMax="20")) float CarryMaxMass=6;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Carry",meta=(ClampMin="10",ClampMax="50")) float CarryMaxHalfExtent=30;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hands",meta=(ClampMin="2",ClampMax="12")) float PalmLength=7;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hands",meta=(ClampMin="0",ClampMax="6")) float PalmThickness=2.5f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Angles",meta=(ClampMin="30",ClampMax="70")) float FrontAngle=55;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Angles",meta=(ClampMin="110",ClampMax="160")) float RearAngle=130;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Angles",meta=(ClampMin="2",ClampMax="15")) float AngleHysteresis=8;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Effort",meta=(ClampMin="1000",ClampMax="40000")) float DriveForce=17000;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Effort",meta=(ClampMin="100",ClampMax="1500")) float Spring=750;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Effort",meta=(ClampMin="10",ClampMax="250")) float Damping=95;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Effort",meta=(ClampMin="0",ClampMax="20")) float Lean=10;
    void Sanitize();
};
UCLASS(BlueprintType)
class MESSCONTROL_API UMCGripProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Grip") FMCGripSettings Settings;
};

USTRUCT(BlueprintType)
struct FMCGripFrame
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) TObjectPtr<AMCFoodActor> Food;
    UPROPERTY(BlueprintReadOnly) EMCGripPose Pose=EMCGripPose::FrontPull;
    UPROPERTY() FVector_NetQuantize10 LeftPoint=FVector::ZeroVector;
    UPROPERTY() FVector_NetQuantize10 RightPoint=FVector::ZeroVector;
    UPROPERTY() FVector_NetQuantizeNormal LeftNormal=FVector::ForwardVector;
    UPROPERTY() FVector_NetQuantizeNormal RightNormal=FVector::ForwardVector;
    UPROPERTY() FVector_NetQuantize10 RestOffset=FVector::ZeroVector;
    UPROPERTY() double StartedAt=0;
    UPROPERTY(BlueprintReadOnly) bool bContact=false;
    UPROPERTY() int32 Serial=0;
};

/** Replicated contact anchors; arm IK is evaluated locally from those same anchors. */
UCLASS(ClassGroup=(MessControl),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCGripComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCGripComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* TickFunction) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Grip") TSoftObjectPtr<UMCGripProfile> Profile;
    UPROPERTY(ReplicatedUsing=OnRep_Settings,BlueprintReadOnly,Category="Grip") FMCGripSettings Settings;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Grip") FMCGripFrame Frame;
    bool BeginGrip(AMCFoodActor* Food);
    void EndGrip();
    void BuildPose(TArray<FTransform>& Pose,const FReferenceSkeleton& Ref,float Dt);
    FVector InputDirection() const;
    FVector DriveForce() const;
    FVector ContactPoint(bool Left) const;
    FVector PalmPoint(bool Left) const;
    bool IsReady() const { return Frame.Food && Frame.bContact; }
    bool CanCarry(const AMCFoodActor* Food) const;
    FVector CarryLocation() const;
    float Blend() const { return FMath::Max(HandAlpha[0],HandAlpha[1]); }
    float ContactError() const;
    FString DebugFailure;
    static EMCGripPose SelectPose(EMCGripPose Previous,float Angle,float Approach,const FMCGripSettings& S);
    static bool UsesHand(EMCGripPose Pose,bool Left);
private:
    UPROPERTY() TObjectPtr<AMCToothCharacter> Tooth;
    struct FArm
    {
        int32 Upper=INDEX_NONE,Lower=INDEX_NONE,Hand=INDEX_NONE;
        FVector Shoulder=FVector::ZeroVector,PalmLocal=FVector::ZeroVector;
        FVector FingerLocal=FVector::ForwardVector,NormalLocal=FVector::UpVector;
        float UpperLength=0,LowerLength=0;
    };
    FArm Arms[2];
    float HandAlpha[2]={0,0};
    FVector Targets[2],Normals[2];
    FVector ReachOffset=FVector::ZeroVector;
    bool bRigReady=false,bConstraining=false;
    float LostContact=0,NextAttemptAt=0;
    double NextModeAt=0;
    EMCGripPose PresentationPose=EMCGripPose::FrontPull;
    bool FindAnchors(AMCFoodActor* Food,EMCGripPose Mode,FMCGripFrame& Result);
    FVector ShoulderPoint(int32 Index) const;
    FVector ReachFor(const AMCFoodActor* Food) const;
    UFUNCTION() void OnRep_Settings();
    FQuat HandRotation(int32 Index,FVector Normal) const;
    float Now() const;
    void CacheRig();
    UFUNCTION() void ConstrainMovement(float Dt,FVector OldLocation,FVector OldVelocity);
};
