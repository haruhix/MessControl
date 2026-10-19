#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "MCHubTypes.h"
#include "MCHubGameState.generated.h"

class APlayerController;
class UDataTable;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMCOnHubStateChanged);

/** Hub membership and shared selection, independent of level objectives and progression. */
UCLASS()
class MESSCONTROL_API AMCHubGameState : public AGameStateBase
{
    GENERATED_BODY()
public:
    AMCHubGameState();
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(ReplicatedUsing=OnRep_HubState, BlueprintReadOnly, Category="Hub")
    EMCHubPhase Phase = EMCHubPhase::Gathering;
    UPROPERTY(ReplicatedUsing=OnRep_HubState, BlueprintReadOnly, Category="Hub") int32 SelectedDay = 1;
    UPROPERTY(ReplicatedUsing=OnRep_HubState, BlueprintReadOnly, Category="Hub") TArray<FMCHubPlayerReadiness> Players;
    UPROPERTY(ReplicatedUsing=OnRep_HubState, BlueprintReadOnly, Category="Hub") FText Notice;
    UPROPERTY(EditAnywhere, ReplicatedUsing=OnRep_DestinationTable, BlueprintReadOnly, Category="Hub")
    TSoftObjectPtr<UDataTable> DestinationTable;
    UPROPERTY(BlueprintAssignable, Category="Hub") FMCOnHubStateChanged OnHubStateChanged;

    UFUNCTION(BlueprintPure, Category="Hub") int32 GetReadyCount() const;
    UFUNCTION(BlueprintPure, Category="Hub") int32 GetParticipantCount() const;
    UFUNCTION(BlueprintPure, Category="Hub") bool AreAllReady() const;
    UFUNCTION(BlueprintPure, Category="Hub") bool AreAllLoaded() const;
    UFUNCTION(BlueprintPure, Category="Hub") bool IsHost(const APlayerController* Player) const;
    UFUNCTION(BlueprintPure, Category="Hub") bool HasHost() const;
    UFUNCTION(BlueprintPure, Category="Hub") bool GetDestination(int32 Day, FMCHubDestinationRow& Destination) const;
    UFUNCTION(BlueprintPure, Category="Hub") TArray<FMCHubDestinationRow> GetDestinations() const;

    const FMCHubDestinationRow* FindDestination(int32 Day) const;
    /** Used by the destination GameMode after travel; resolves the same authored row and safety fallbacks. */
    static bool ResolveAuthoredDestination(int32 Day, FMCHubDestinationRow& Destination);
    void ReloadDestinations();

    // Native authority-only mutations. Clients only read the replicated snapshot.
    void SetPhase(EMCHubPhase NewPhase);
    void SetSelectedDay(int32 Day);
    void SetReadiness(const TArray<FMCHubPlayerReadiness>& NewPlayers);
    void SetNotice(const FText& NewNotice);

private:
    UPROPERTY(Transient) TObjectPtr<UDataTable> LoadedDestinationTable;
    UPROPERTY(Transient) TMap<int32, FMCHubDestinationRow> Destinations;
    bool bDefinitionsLoaded = false;
    void NotifyChanged();
    UFUNCTION() void OnRep_HubState();
    UFUNCTION() void OnRep_DestinationTable();
};
