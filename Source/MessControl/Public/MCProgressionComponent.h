#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCProgressionComponent.generated.h"

class AMCPlayerState;

/** Inert saved-asset compatibility shell. Runtime rewards come only from chests. */
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

    // Serialized legacy tuning only; intentionally absent from designer controls.
    UPROPERTY(BlueprintReadOnly, Category="Legacy")
    int32 FirstLevelExperience=50;
    UPROPERTY(BlueprintReadOnly, Category="Legacy")
    int32 ExperienceGrowthPerLevel=25;
    UPROPERTY(BlueprintReadOnly, Category="Legacy")
    int32 MaxTeamLevel=100;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Progression") int32 TeamLevel=1;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Progression") int64 ExperienceInLevel=0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Progression") int64 TotalExperience=0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Progression") int32 ExperienceToNextLevel=0;

protected:
    virtual void BeginPlay() override;
private:
    void SynchronizePlayers();
};
