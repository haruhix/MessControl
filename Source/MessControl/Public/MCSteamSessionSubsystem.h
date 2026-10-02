#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/EngineBaseTypes.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "MCSteamSessionSubsystem.generated.h"

/** Steam rooms survive map travel; editor/local tests keep the existing IP path. */
UCLASS()
class MESSCONTROL_API UMCSteamSessionSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    bool CanUseSteam() const;
    bool IsBusy() const { return Operation != EOperation::Idle; }
    bool HasRoom() const;
    bool HadFailure() const { return bFailed; }
    FString GetPlayerName() const;
    const FString& GetStatus() const { return Status; }
    const TArray<FString>& GetRoomLabels() const { return RoomLabels; }
    int32 GetSearchRevision() const { return SearchRevision; }
    void HostRoom();
    void FindRooms();
    void JoinRoom(int32 Index);
    void InviteFriends();
    void RefreshAvailability();
    static bool IsMessControlRoom(const FOnlineSessionSearchResult& Result);
private:
    enum class EOperation : uint8 { Idle, Creating, Finding, Joining, DestroyForHost, DestroyForJoin };
    IOnlineSessionPtr GetSessions();
    void SetStatus(const FString& Text, bool Failed=false);
    void CreateRoom();
    void BeginJoin(const FOnlineSessionSearchResult& Result);
    void DestroyFor(EOperation Next);
    void Created(FName Name, bool Success);
    void Found(bool Success);
    void Joined(FName Name, EOnJoinSessionCompleteResult::Type Result);
    void Destroyed(FName Name, bool Success);
    void InviteAccepted(bool Success, int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& Result);
    void NetworkFailed(UWorld* World, class UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error);
    void TravelFailed(UWorld* World, ETravelFailure::Type Type, const FString& Error);
    IOnlineSessionPtr Sessions;
    TSharedPtr<FOnlineSessionSearch> Search;
    TArray<FOnlineSessionSearchResult> Rooms;
    TArray<FString> RoomLabels;
    FOnlineSessionSearchResult PendingJoin;
    FDelegateHandle CreateHandle, FindHandle, JoinHandle, DestroyHandle, InviteHandle;
    FDelegateHandle NetworkHandle, TravelHandle;
    EOperation Operation=EOperation::Idle;
    FString Status;
    int32 SearchRevision=0;
    bool bFailed=false;
};
