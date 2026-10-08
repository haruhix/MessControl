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

/** First playable sequence. Variant profiles change a slot while its order stays authored. */
UCLASS(BlueprintType)
class MESSCONTROL_API UMCSingleDayProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequence") TArray<TSoftObjectPtr<UMCNutRainProfile>> NutRainVariants;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequence", meta=(ClampMin="5", ClampMax="300", Units="s")) float DirectorSeconds=30;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequence", meta=(ClampMin="0")) int32 NutEventExperience=25;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequence") TSoftClassPtr<AMCBossCharacter> FinalBossClass;
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
    void Stop();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Sequence") EMCSingleDayStage Stage=EMCSingleDayStage::Training;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Sequence") TObjectPtr<AMCNutRainEvent> NutEvent;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Sequence") TObjectPtr<AMCBossCharacter> FinalBoss;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Sequence") int32 VariantIndex=0;
private:
    void BeginNuts();
    void BeginDirector();
    void BeginBoss();
    void Fail(const FString& Reason);
    void Publish(const FString& Title, const FString& Instruction, int32 Left=0, int32 Total=0);
    UPROPERTY() TObjectPtr<UMCDayPlan> Mechanics;
    UPROPERTY() TObjectPtr<UMCSingleDayProfile> Settings;
    UPROPERTY() TObjectPtr<UMCGameDirectorProfile> DirectorProfile;
    UPROPERTY() TObjectPtr<AMCGameDirector> Interlude;
    double InterludeEndsAt=0;
    bool bStopped=false;
};
