#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MCDataAssets.h"
#include "MCToothCharacter.generated.h"
class UCameraComponent;
class USpringArmComponent;
class UInputAction;
class UInputMappingContext;
class UMCToothPhysicsComponent;
class UPhysicsControlComponent;
class UMCToothStatusComponent;
class UMCGazeComponent;
class AMCFoodActor;
class AMCArenaTooth;
class UMaterialInstanceDynamic;
class UEnhancedInputLocalPlayerSubsystem;
struct FInputActionValue;

UCLASS(Blueprintable)
class MESSCONTROL_API AMCToothCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    AMCToothCharacter();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostInitializeComponents() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual void PawnClientRestart() override;
    virtual void Landed(const FHitResult& Hit) override;
    virtual void FellOutOfWorld(const UDamageType& DamageType) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Physics") TObjectPtr<UMCToothPhysicsComponent> ToothPhysics;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Care") TObjectPtr<UMCToothStatusComponent> Status;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Gaze") TObjectPtr<UMCGazeComponent> Gaze;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Care") bool bSelfCare=false;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Care") TObjectPtr<AActor> CareTarget;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Care") float ContactProgress=0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Food") TObjectPtr<AMCFoodActor> HeldFood;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Tools") TObjectPtr<AMCFoodActor> EquippedBrush;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Coffee") bool bInCoffee=false;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Coffee") TObjectPtr<AMCArenaTooth> ClingTooth;
    bool bWantsCling=false;
    FVector ClingPoint=FVector::ZeroVector;
    FVector2D PaddleInput=FVector2D::ZeroVector;
    bool HasBrush() const;
    UFUNCTION(BlueprintCallable, Category="Tools") void ThrowItem();
    UFUNCTION(Server,Reliable) void ServerThrowItem();
    UFUNCTION(Server,Unreliable) void ServerPaddle(FVector2D Direction);
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Life") double RespawnAt=0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Life") int32 RespawnSourceId=0;
    bool CanWork() const;
    void StatusChanged();
    void DropFood();
    void CancelGameplayInput();
    bool CanContact(AActor* Target) const;
    UMCToothStatusComponent* FindCareTarget(bool bBrush) const;
    void AdvanceCare(float Dt);
    void ResetContact();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Physics") TObjectPtr<UPhysicsControlComponent> Muscles;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Visuals") TObjectPtr<USceneComponent> BrushPivot;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Visuals") TObjectPtr<UStaticMeshComponent> Brush;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Visuals") TObjectPtr<UMCPlayerAppearance> Appearance;
    FName RigBone(FName BoneRole) const;
    FTransform StandingMeshTransform() const;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Camera") TObjectPtr<USpringArmComponent> CameraBoom;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Camera") TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation") TObjectPtr<UMCAnimationProfile> AnimationProfile;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio") TObjectPtr<UMCSoundPalette> SoundPalette;
    UPROPERTY(BlueprintReadWrite, Category="Animation") FMCAnimationSettings AnimationSettings;
    UPROPERTY(BlueprintReadWrite, Category="Animation") bool bPreviewAnimation = false;
    UPROPERTY(ReplicatedUsing=OnRep_Working, BlueprintReadOnly, Category="Action") bool bBrushing = false;
    UPROPERTY(ReplicatedUsing=OnRep_Working, BlueprintReadOnly, Category="Action") bool bHandling = false;
    UFUNCTION(BlueprintCallable, Category="Animation") void SaveTuning();
    UFUNCTION(BlueprintCallable, Category="Animation") void ResetTuning();
    UFUNCTION(BlueprintCallable, Category="Action") void SwingBrush();
    UFUNCTION(BlueprintCallable, Category="Physics") void SpawnPracticeTooth();
    float AnimationGait=0.f, AnimationSpeed=0.f, AnimationBob=0.f, AnimationPitch=0.f, AnimationBrushAngle=0.f;
    int32 ValidatedSwingCount=0, ConfirmedHitCount=0;
    int32 SuccessfulBrushContacts=0;
protected:
    virtual void BeginPlay() override;
private:
    friend class UMCValidationSubsystem;
    friend class AMCCoreScenario;
    friend class AMCDayOneScenario;
    void BuildInput();
    void ApplyAppearance();
    void MoveForward(const FInputActionValue& Value);
    void MoveRight(const FInputActionValue& Value);
    void StartJump(); void StopJump();
    void StartBrush(); void StopBrush(); void StartHandle(); void StopHandle();
    void TogglePanel(); void ToggleConnection(); void RestartRun();
    UFUNCTION(Server, Reliable) void ServerSetWorking(bool bBrush, bool bActive);
    void ToggleSelfCare();
    UFUNCTION(Server, Reliable) void ServerToggleSelfCare();
    UFUNCTION() void OnRep_Working();
    void FindWork(float DeltaSeconds);
    UFUNCTION(Server,Reliable) void ServerSwingBrush();
    UFUNCTION(NetMulticast,Reliable) void MulticastSwing();
    UFUNCTION(NetMulticast,Unreliable) void MulticastHitSound(FVector Location);
    UFUNCTION() void OnBodyHit(UPrimitiveComponent* HitComponent,AActor* OtherActor,UPrimitiveComponent* OtherComponent,FVector NormalImpulse,const FHitResult& Hit);
    void ResolveSwing();
    UPROPERTY() TObjectPtr<UInputMappingContext> InputMap;
    UPROPERTY() TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> AppliedInputSubsystem;
    UPROPERTY() TObjectPtr<UInputAction> ForwardAction;
    UPROPERTY() TObjectPtr<UInputAction> RightAction;
    UPROPERTY() TObjectPtr<UInputAction> JumpAction;
    UPROPERTY() TObjectPtr<UInputAction> BrushAction;
    UPROPERTY() TObjectPtr<UInputAction> HandleAction;
    UPROPERTY() TObjectPtr<UInputAction> PanelAction;
    UPROPERTY() TObjectPtr<UInputAction> ConnectionAction;
    UPROPERTY() TObjectPtr<UInputAction> RestartAction;
    UPROPERTY() TObjectPtr<UInputAction> SwingAction;
    UPROPERTY() TObjectPtr<UInputAction> SelfCareAction;
    UPROPERTY() TObjectPtr<UInputAction> ThrowAction;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> StatusMaterial;
    UPROPERTY() TObjectPtr<AMCToothCharacter> PracticeTooth;
    float NextSwingTime=0.f;
    float SwingStartedAt=-10.f;
    float LastEnvironmentHit=-10.f;
    FTimerHandle SwingTimer;
    float Gait = 0.f;
    float LandingImpulse = 0.f;
    float WorkStartedAt = -10.f;
    float SoundAccumulator = 0.f;
    float BrushAngle = 0.f;
    bool bLoadedLocalTuning = false;
    bool bDeathReported=false;
    bool bLastContactBrush=false;
    float ContactElapsed=0;
    FVector2D LocalPaddle=FVector2D::ZeroVector;
    float PaddleSendElapsed=0;
    double LastPaddleAt=0;
};
