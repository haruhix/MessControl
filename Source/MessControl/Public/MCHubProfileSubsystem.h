#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameFramework/SaveGame.h"
#include "MCHubEconomyComponent.h"
#include "MCHubProfileSubsystem.generated.h"

class AMCPlayerState;
class APlayerController;

UCLASS()
class MESSCONTROL_API UMCHubProfileSave : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY() int32 Version=1;
    UPROPERTY() FMCHubEconomySaveData Economy;
    UPROPERTY() int32 Fish=0;
};

/** Host-side test persistence, keyed by the authenticated identity, never by a client-supplied filename. */
UCLASS()
class MESSCONTROL_API UMCHubProfileSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    bool Restore(AMCPlayerState* State,APlayerController* Controller);
    void Store(AMCPlayerState* State,APlayerController* Controller,bool bSettlePending=false);
private:
    FString ProfilePath(const AMCPlayerState* State,const APlayerController* Controller) const;
    TMap<FString,TArray<uint8>> CachedBytes;
    TSet<FString> ProtectedProfiles;
};
