#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/DataAsset.h"
#include "MCSingleDayDirector.generated.h"

class UMCDayPlan;
class UMCNutRainProfile;
class UMCGameDirectorProfile;
class AMCNutRainEvent;
class AMCGameDirector;
class AMCBossCharacter;

UENUM(BlueprintType)
enum class EMCSingleDayStage : uint8 { Training, FirstPerk, Nuts, Director, Boss, Complete };

UENUM(BlueprintType)
enum class EMCSingleDayKeyEventKind : uint8 { NutEncounter };

/** A saved position in the main sequence. Only authored event kinds can run. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCSingleDayKeyEvent
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sequence") FName EventId=TEXT("NutEncounter");
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sequence") EMCSingleDayKeyEventKind Kind=EMCSingleDayKeyEventKind::NutEncounter;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sequence") TArray<TSoftObjectPtr<UMCNutRainProfile>> NutRainVariants;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sequence",meta=(ClampMin="0")) int32 CompletionExperience=25;
    /** Support after this slot, when another authored slot exists. It never ends the run. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sequence",meta=(ClampMin="5",ClampMax="600",Units="s")) float DirectorSupportSeconds=120;
};

/** Extensible main sequence. The complete run target is an orientation, not a victory timer. */
UCLASS(BlueprintType)
class MESSCONTROL_API UMCSingleDayProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UMCSingleDayProfile();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sequence") TArray<FMCSingleDayKeyEvent> KeyEvents;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sequence",meta=(ClampMin="1",ClampMax="120",Units="min")) float RunTargetMinMinutes=25;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sequence",meta=(ClampMin="1",ClampMax="120",Units="min")) float RunTargetMaxMinutes=35;
    /** Explicit compatibility for the old short prototype, never enabled by new fragment authoring. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Legacy") bool bLegacyTimedFinale=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequence") TArray<TSoftObjectPtr<UMCNutRainProfile>> NutRainVariants;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Legacy", meta=(ClampMin="5", ClampMax="300", Units="s")) float DirectorSeconds=30;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Legacy", meta=(ClampMin="0")) int32 NutEventExperience=25;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Legacy") TSoftClassPtr<AMCBossCharacter> FinalBossClass;
};

/** Main events are ordered. The adaptive scheduler owns only the interval between them. */
UCLASS()
class MESSCONTROL_API AMCSingleDayDirector : public AActor
{
    GENERATED_BODY()
public:
    AMCSingleDayDirector();
    void Initialize(UMCDayPlan* Plan, UMCSingleDayProfile* Profile, UMCGameDirectorProfile* InterludeProfile);
    void BeginFirstPerk();
    int32 ChooseNutSeriesSize(int32 Minimum,int32 Maximum,int32 CompletedSeries) const;
    bool ShouldFinishNutRain(int32 CompletedSeries,int32 MinimumSeries,float RainAge,float TargetSeconds) const;
    bool IsLegacyTimedFinale() const { return Settings && Settings->bLegacyTimedFinale; }
    void Stop();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Sequence") EMCSingleDayStage Stage=EMCSingleDayStage::Training;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Sequence") TObjectPtr<AMCNutRainEvent> NutEvent;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Sequence") TObjectPtr<AMCBossCharacter> FinalBoss;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Sequence") int32 VariantIndex=0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Sequence") int32 KeyEventIndex=0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Sequence") bool bAuthoredFragmentComplete=false;
private:
    void BeginNuts();
    void BeginDirector();
    void BeginBoss();
    void Fail(const FString& Reason);
    void Publish(const FString& Title, const FString& Instruction, int32 Left=0, int32 Total=0);
    void Record(const FString& Decision);
    UPROPERTY() TObjectPtr<UMCDayPlan> Mechanics;
    UPROPERTY() TObjectPtr<UMCSingleDayProfile> Settings;
    UPROPERTY() TObjectPtr<UMCGameDirectorProfile> DirectorProfile;
    UPROPERTY() TObjectPtr<AMCGameDirector> Interlude;
    double InterludeEndsAt=0;
    double RunStartedAt=0;
    bool bStopped=false;
};
