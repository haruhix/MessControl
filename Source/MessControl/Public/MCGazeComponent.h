#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "MCGazeComponent.generated.h"
class AMCToothCharacter;
struct FReferenceSkeleton;

UENUM(BlueprintType)
enum class EMCGazeInterest : uint8 { None, Player, Food, Work, Danger };

USTRUCT(BlueprintType)
struct FMCGazeSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Attention") float Radius=850;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Attention") float ViewHalfAngle=80;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Attention") float ScanInterval=.15f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Attention") float HoldSeconds=1.2f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Eyes") float YawLimit=35;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Eyes") float PitchLimit=25;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Eyes") float TurnSpeed=18;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Eyes") float ReactionDelay=.12f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Blink") float BlinkMin=2.8f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Blink") float BlinkMax=5.2f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Blink") float BlinkSeconds=.22f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pupils") float PupilRest=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pupils") float PupilDanger=1.75f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pupils") float PupilPain=1.5f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pupils") float PupilFocus=.8f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pupils") float PupilReactSpeed=12.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pupils") float PupilRecoverSpeed=2.4f;
    void Sanitize();
};

UCLASS(BlueprintType)
class MESSCONTROL_API UMCGazeProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gaze") FMCGazeSettings Settings;
};

USTRUCT(BlueprintType)
struct FMCGazeTarget
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) TObjectPtr<AActor> Actor;
    // World point is also a fallback while a referenced actor is being resolved on a client.
    UPROPERTY(BlueprintReadOnly) FVector_NetQuantize10 Point=FVector::ZeroVector;
    UPROPERTY() FVector_NetQuantize10 Offset=FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) EMCGazeInterest Interest=EMCGazeInterest::None;
    UPROPERTY() double ChangedAt=0;
    UPROPERTY() int32 Serial=0;
};

/** Server chooses attention; each peer poses only the cosmetic face bones. */
UCLASS(ClassGroup=(MessControl),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCGazeComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCGazeComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* TickFunction) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Gaze") TSoftObjectPtr<UMCGazeProfile> Profile;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Gaze") FMCGazeSettings Settings;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Gaze") FMCGazeTarget Target;
    UPROPERTY(Replicated) double BlinkStartedAt=-100;
    UPROPERTY(BlueprintReadOnly,Category="Gaze") float Blink=0;
    UPROPERTY(BlueprintReadOnly,Category="Gaze") float PupilScale=1.f;
    UPROPERTY(BlueprintReadOnly,Category="Gaze") FVector2D LeftAngles=FVector2D::ZeroVector;
    UPROPERTY(BlueprintReadOnly,Category="Gaze") FVector2D RightAngles=FVector2D::ZeroVector;
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Gaze") bool NoticePoint(FVector WorldPoint,float HoldSeconds=1);
    FVector TargetPoint() const;
    void BuildPose(TArray<FTransform>& Pose,const FReferenceSkeleton& Ref,float Dt);
    // F1 previews only the local face presentation; attention stays server-owned.
    FMCGazeSettings VisualSettings() const;
    void SetVisualPreview(float Yaw,float Pitch,float Speed,float Delay);
    void SavePreview();
    void ResetPreview();
private:
    void SelectTarget();
    void SetTarget(AActor* Actor,FVector Point,EMCGazeInterest Interest);
    bool CanSee(AActor* Actor,FVector Point) const;
    FVector EyePosition() const;
    float ServerTime() const;
    UPROPERTY() TObjectPtr<AMCToothCharacter> Tooth;
    float ScanLeft=0;
    double NextBlink=0,HoldUntil=0,NoticeUntil=0;
    bool bPreview=false,bPreviewLoaded=false;
    FVector4 Preview=FVector4(35,25,18,.12f);
};
