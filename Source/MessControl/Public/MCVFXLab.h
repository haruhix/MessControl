#pragma once

#include "CoreMinimal.h"
#include "MCTongue.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "Engine/NetSerialization.h"
#include "MCVFXLab.generated.h"

class UNiagaraComponent;
class SWidget;
class UGameViewportClient;

/** Static support for real gameplay queries; intentionally has no tongue motion. */
UCLASS()
class MESSCONTROL_API AMCVFXLabFloor : public AMCTongue
{
    GENERATED_BODY()
public:
    AMCVFXLabFloor();
    virtual void BeginPlay() override;
};

USTRUCT(BlueprintType)
struct FMCVFXLabResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) TObjectPtr<AActor> Station;
    UPROPERTY(BlueprintReadOnly) FString Name;
    UPROPERTY(BlueprintReadOnly) FString Status;
    UPROPERTY(BlueprintReadOnly) int32 Passed=0;
    UPROPERTY(BlueprintReadOnly) int32 Failed=0;
    UPROPERTY(BlueprintReadOnly) int32 Cycles=0;
    UPROPERTY(BlueprintReadOnly) bool bRunning=false;
};

/** Opt-in lab catalogue and replicated results; never starts the day scheduler. */
UCLASS()
class MESSCONTROL_API AMCVFXLab : public AActor
{
    GENERATED_BODY()
public:
    AMCVFXLab();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(Replicated,VisibleAnywhere,BlueprintReadOnly,Category="VFX Lab") TArray<FMCVFXLabResult> Results;
    UPROPERTY(Replicated,VisibleAnywhere,BlueprintReadOnly,Category="VFX Lab") bool bRunning=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VFX Lab",meta=(ClampMin="1")) float SampleRepeatSeconds=4;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VFX Lab") bool bProximityActivation=true;
    /** Distance in cm from the edge of the station's platform. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VFX Lab",meta=(ClampMin="0",Units="cm")) float ActivationDistance=3000;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VFX Lab",meta=(ClampMin="0",Units="cm")) float ReleaseDistance=3600;
    UFUNCTION(BlueprintCallable,Category="VFX Lab") void SetRunning(bool bEnabled);
    UFUNCTION(BlueprintCallable,Category="VFX Lab") void ResetStation(int32 Index);
    static AMCVFXLab* Find(const UWorld* World);
private:
    void UpdateStations();
    void UpdateResults();
    TArray<TWeakObjectPtr<AActor>> Stations;
    struct FSample
    {
        TWeakObjectPtr<UNiagaraComponent> Component;
        bool bAutoRepeat=false;
        bool bNearby=false;
        double NextRepeatAt=0;
    };
    TArray<FSample> Samples;
};

UCLASS()
class MESSCONTROL_API AMCVFXLabPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void PlayerTick(float DeltaSeconds) override;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="VFX Lab") int32 StationIndex=0;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UFUNCTION(Server,Reliable) void ServerNavigate(int32 Direction);
    /** INDEX_NONE opens the Fab reference gallery; other indices select a station. */
    UFUNCTION(Server,Reliable) void ServerSelectStation(int32 Index);
    UFUNCTION(Server,Reliable) void ServerToggleRunning();
    UFUNCTION(Server,Reliable) void ServerResetCurrent();
    UFUNCTION(Server,Unreliable) void ServerReportViewerPosition(FVector_NetQuantize10 Location);
    UFUNCTION(BlueprintPure,Category="VFX Lab") FVector GetLabViewerLocation() const;
    UFUNCTION(BlueprintCallable,Category="VFX Lab") void ToggleLabNavigation();
    UFUNCTION(BlueprintPure,Category="VFX Lab") bool IsLabNavigationOpen() const;
protected:
    virtual void SetupInputComponent() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    void GotoStation(int32 Index);
    void CloseLabNavigation();
    TSharedPtr<SWidget> NavigationWidget;
    TWeakObjectPtr<UGameViewportClient> NavigationViewport;
    bool bInitialFrame=false;
    double NextViewerReportAt=0;
    double ViewerReportedAt=-100;
    FVector ReportedViewerLocation=FVector::ZeroVector;
};

UCLASS()
class MESSCONTROL_API AMCVFXLabHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};

UCLASS()
class MESSCONTROL_API AMCVFXLabGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AMCVFXLabGameMode();
    virtual void StartPlay() override;
};
