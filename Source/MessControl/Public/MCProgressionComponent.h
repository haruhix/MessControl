#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCProgressionComponent.generated.h"

class AMCPlayerState;

/** Shared task experience; every earned level gives each player their own choice. */
UCLASS(ClassGroup=(MessControl), meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCProgressionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCProgressionComponent();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Progression")
    int32 AddExperience(int32 Amount);
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Progression")
    void ResetProgression();
    UFUNCTION(BlueprintPure, Category="Progression")
    int32 GetExperienceToNextLevel() const;
    UFUNCTION(BlueprintPure, Category="Progression")
    bool HasPendingChoices() const;
    void SynchronizePlayer(AMCPlayerState* Player);

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Progression", meta=(ClampMin="1"))
    int32 FirstLevelExperience=50;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Progression", meta=(ClampMin="0"))
    int32 ExperienceGrowthPerLevel=25;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Progression", meta=(ClampMin="2",ClampMax="1000"))
    int32 MaxTeamLevel=100;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Progression") int32 TeamLevel=1;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Progression") int64 ExperienceInLevel=0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Progression") int64 TotalExperience=0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Progression") int32 ExperienceToNextLevel=50;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    void SynchronizePlayers();
    FTimerHandle SynchronizeTimer;
};
