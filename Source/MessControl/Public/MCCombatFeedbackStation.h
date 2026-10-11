#pragma once

#include "CoreMinimal.h"
#include "MCNutEnemy.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "MCCombatFeedbackStation.generated.h"

class AMCPlaytestBotController;
class AMCToothCharacter;
class AMCTongue;
class ASpectatorPawn;
class UMCNutRainProfile;
class UStaticMesh;
class UTextRenderComponent;
class AMCVFXLabBossStation;
class SWidget;
class UGameViewportClient;

/** Development fixture: ordinary nut hit presentation without autonomous attacks. */
UCLASS(Transient,NotBlueprintable)
class MESSCONTROL_API AMCCombatFeedbackNut : public AMCNutEnemy
{
    GENERATED_BODY()
public:
    virtual void Tick(float DeltaSeconds) override;
};

/** Opt-in A/B fixture. Calls the real accepted damage paths with identical timing. */
UCLASS(Blueprintable)
class MESSCONTROL_API AMCCombatFeedbackStation : public AActor
{
    GENERATED_BODY()
public:
    AMCCombatFeedbackStation();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat comparison") bool bLegacyFeedback=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat comparison") bool bAutoRun=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat comparison") TObjectPtr<AMCTongue> Floor;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat comparison") TSoftClassPtr<AMCToothCharacter> SubjectClass;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat comparison") TSoftObjectPtr<UMCNutRainProfile> NutProfile;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat comparison") TSoftObjectPtr<UStaticMesh> EnemyMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat comparison",meta=(ClampMin="3",ClampMax="30")) float RepeatSeconds=4;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat comparison",meta=(ClampMin="1",ClampMax="50")) float EnemyDamage=25;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat comparison",meta=(ClampMin="1",ClampMax="50")) float PlayerDamage=15;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat comparison") FVector SubjectOffset=FVector(-95,0,0);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat comparison") FVector EnemyOffset=FVector(95,0,0);
    UPROPERTY(Transient,BlueprintReadOnly,Category="Combat comparison") TObjectPtr<AMCToothCharacter> Subject;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Combat comparison") TObjectPtr<AMCNutEnemy> Enemy;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Combat comparison") bool bRunning=false;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Combat comparison") int32 Cycles=0;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Combat comparison") float LastEnemyDamage=0;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Combat comparison") float LastPlayerDamage=0;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Combat comparison") FString Status;
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Combat comparison") bool StrikeEnemy();
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Combat comparison") bool StrikePlayer();
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Combat comparison") void Reset();
    /** Pause keeps subjects available for manual possession and ordinary tool inputs. */
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Combat comparison") void SetRunning(bool bEnabled);
    /** Called only after the comparison player deliberately releases this subject. */
    void ResumeBotControl();
    void SelectScenario(int32 Index);
    bool ApplyOutgoing(float Damage);
    bool ApplyIncoming(float Damage);
    bool RunSelectedCase();
    AActor* GetComparisonEnemy() const;
    FString GetComparisonStatusText() const;
private:
    bool SpawnFixtures();
    bool StartCycle();
    bool GroundPoint(FVector Offset,float Radius,FVector& OutPoint) const;
    void ClearFixtures();
    void SetStatus(const FString& Text);
    UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Readout;
    UPROPERTY(Transient) TObjectPtr<AMCPlaytestBotController> Bot;
    UPROPERTY(Transient) TObjectPtr<AMCVFXLabBossStation> BossFixture;
    int32 ScenarioIndex=0;
    UPROPERTY(Transient) TObjectPtr<UStaticMesh> ResolvedEnemyMesh;
    FMCNutEnemySettings EnemySettings;
    FVector EnemyScale=FVector::OneVector;
    FTransform SubjectPose;
    double CycleStartedAt=0,NextCycleAt=0;
    bool bCycleActive=false,bEnemyStruck=false,bPlayerStruck=false;
};

UCLASS()
class MESSCONTROL_API AMCCombatFeedbackController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual bool InputKey(const FInputKeyEventArgs& Params) override;
    UFUNCTION(BlueprintCallable,Category="Combat comparison") void ViewLegacy();
    UFUNCTION(BlueprintCallable,Category="Combat comparison") void ViewCurrent();
    UFUNCTION(BlueprintCallable,Category="Combat comparison") void ViewOverview();
    UFUNCTION(BlueprintCallable,Category="Combat comparison") void ResetStations();
    UFUNCTION(BlueprintCallable,Category="Combat comparison") void ToggleRepeating();
    UFUNCTION(BlueprintCallable,Category="Combat comparison") void ToggleTestPanel();
    UFUNCTION(BlueprintCallable,Category="Combat comparison") void CloseTestPanel();
    UFUNCTION(BlueprintPure,Category="Combat comparison") bool IsTestPanelOpen() const;
    UFUNCTION(BlueprintCallable,Category="Combat comparison") void SelectScenario(int32 Index);
    UFUNCTION(BlueprintPure,Category="Combat comparison") int32 GetSelectedScenario() const {return SelectedScenario;}
    UFUNCTION(BlueprintPure,Category="Combat comparison") FString GetComparisonStatusText() const;
    UFUNCTION(BlueprintPure,Category="Combat comparison") FString GetScenarioTitle() const;
    UFUNCTION(BlueprintCallable,Category="Combat comparison") void StrikeOutgoing(float Damage);
    UFUNCTION(BlueprintCallable,Category="Combat comparison") void StrikeIncoming(float Damage);
    UFUNCTION(BlueprintCallable,Category="Combat comparison") void FinishEnemy();
    UFUNCTION(BlueprintCallable,Category="Combat comparison") void FinishPlayer();
    UFUNCTION(BlueprintCallable,Category="Combat comparison") void StrikeShield(float Damage);
    UFUNCTION(BlueprintPure,Category="Combat comparison") bool CanShieldTest() const;
    UFUNCTION(BlueprintCallable,Category="Combat comparison") void RunSelectedCase();
private:
    void ViewSubject(bool bLegacy);
    void ReleaseSubject();
    UPROPERTY(Transient) TObjectPtr<ASpectatorPawn> OverviewPawn;
    int32 SelectedScenario=0;
    TSharedPtr<SWidget> TestPanel;
    TWeakObjectPtr<UGameViewportClient> PanelViewport;
};

UCLASS()
class MESSCONTROL_API AMCCombatFeedbackHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};

/** Standalone development comparison; no day scheduler or production GameMode. */
UCLASS()
class MESSCONTROL_API AMCCombatFeedbackGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AMCCombatFeedbackGameMode();
};
