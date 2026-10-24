#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MCHubTypes.h"
#include "MCSessionTypes.h"
#include "MCHubGameMode.generated.h"

class AMCHubGameState;
class AMCToothCharacter;

/** A social hub and a shared selector. Deliberately does not inherit the level director GameMode. */
UCLASS()
class MESSCONTROL_API AMCHubGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AMCHubGameMode();
    virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
    virtual void InitGameState() override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
    virtual void PostLogin(APlayerController* NewPlayer) override;
    virtual void Logout(AController* Exiting) override;
    virtual void HandleSeamlessTravelPlayer(AController*& Controller) override;
    virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
    virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override;
    virtual void RestartPlayer(AController* Player) override;
    virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* Player, const FTransform& SpawnTransform) override;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Hub") bool RequestOpenSelection(APlayerController* Requester);
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Hub") bool RequestSelectDay(APlayerController* Requester, int32 Day);
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Hub") bool RequestLaunch(APlayerController* Requester);
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Hub") bool RequestReturnToHub(APlayerController* Requester);
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Hub|Room")
    bool RequestSetRoomPrivacy(APlayerController* Requester, EMCRoomPrivacy Privacy);
    void AcknowledgeSceneLoaded(APlayerController* Player, int32 Generation);
    /** Queue safe recovery outside the character's physics/FellOutOfWorld callback. */
    bool RecoverPlayer(AMCToothCharacter* Player);
    void RefreshReadiness();
    bool IsSelectionScene() const { return bSelectionScene; }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hub|Travel")
    TSoftObjectPtr<UWorld> HubMap;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hub|Travel")
    TSoftObjectPtr<UWorld> SelectionMap;
    /** Fallback when no PlayerStart exists. Authored hub starts also establish a recovery threshold. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hub|Safety") float RecoveryFloorZ = -10000.f;

private:
    bool bSelectionScene = false;
    bool bTravelPending = false;
    int32 InitialSelectedDay = 1;
    TMap<TWeakObjectPtr<APlayerController>, int32> LoadedPlayers;
    TMap<TWeakObjectPtr<APlayerController>, double> LastSelectionAt;
    TMap<TWeakObjectPtr<APlayerController>, double> RecoveryAt;
    TSet<TWeakObjectPtr<APlayerController>> ForcedRecovery;
    struct FSelectionPawnMovement
    {
        TWeakObjectPtr<AMCToothCharacter> Pawn;
        uint8 MovementMode = 0;
        uint8 CustomMovementMode = 0;
    };
    TMap<TWeakObjectPtr<APlayerController>, FSelectionPawnMovement> SelectionPawnMovement;

    void InitializeHubPlayer(APlayerController* Player, bool bPreserveAppearance);
    void SetHubPhase(EMCHubPhase NewPhase);
    void ApplySelectionPawnLock(APlayerController* Player);
    void RestoreSelectionPawnMovement();
    void RecoverPlayers();
    void RemoveOrphanedStartupPawns();
    float ArrivalYaw(const FVector& SpawnLocation, float FallbackYaw) const;
    bool IsParticipant(const APlayerController* Player) const;
    bool IsHostRequest(const APlayerController* Player) const;
    bool TravelTo(const FString& URL, EMCHubPhase RestorePhase);
};
