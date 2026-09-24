#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MCDataAssets.h"
#include "MCToothCharacter.generated.h"
class UCameraComponent;
class USpringArmComponent;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

UCLASS(Blueprintable)
class MESSCONTROL_API AMCToothCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    AMCToothCharacter();
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual void PawnClientRestart() override;
    virtual void Landed(const FHitResult& Hit) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Visuals") TObjectPtr<USceneComponent> BodyPivot;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Visuals") TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Visuals") TObjectPtr<USceneComponent> BrushPivot;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Visuals") TObjectPtr<UStaticMeshComponent> Brush;
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
protected:
    virtual void BeginPlay() override;
private:
    friend class UMCValidationSubsystem;
    void BuildInput();
    void MoveForward(const FInputActionValue& Value);
    void MoveRight(const FInputActionValue& Value);
    void StartJump(); void StopJump();
    void StartBrush(); void StopBrush(); void StartHandle(); void StopHandle();
    void TogglePanel(); void ToggleConnection(); void RestartRun();
    UFUNCTION(Server, Reliable) void ServerSetWorking(bool bBrush, bool bActive);
    UFUNCTION() void OnRep_Working();
    void FindWork(float DeltaSeconds);
    UPROPERTY() TObjectPtr<UInputMappingContext> InputMap;
    UPROPERTY() TObjectPtr<UInputAction> ForwardAction;
    UPROPERTY() TObjectPtr<UInputAction> RightAction;
    UPROPERTY() TObjectPtr<UInputAction> JumpAction;
    UPROPERTY() TObjectPtr<UInputAction> BrushAction;
    UPROPERTY() TObjectPtr<UInputAction> HandleAction;
    UPROPERTY() TObjectPtr<UInputAction> PanelAction;
    UPROPERTY() TObjectPtr<UInputAction> ConnectionAction;
    UPROPERTY() TObjectPtr<UInputAction> RestartAction;
    float Gait = 0.f;
    float LandingImpulse = 0.f;
    float WorkStartedAt = -10.f;
    float WorkAccumulator = 0.f;
    float SoundAccumulator = 0.f;
    float BrushAngle = 0.f;
    bool bLoadedLocalTuning = false;
};
