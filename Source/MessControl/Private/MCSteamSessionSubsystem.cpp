#include "MCSteamSessionSubsystem.h"
#include "MCGameState.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/OnlineExternalUIInterface.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

namespace
{
    const FName GameKey(TEXT("MC_GAME"));
    const FString GameValue(TEXT("MessControl_Coop_v1"));
    IOnlineSubsystem* SteamFor(const UMCSteamSessionSubsystem* Self)
    {
        const UWorld* World=Self->GetWorld();
        if (!World || World->WorldType==EWorldType::PIE || IsRunningCommandlet()) return nullptr;
        IOnlineSubsystem* OSS=Online::GetSubsystem(World);
        return OSS && OSS->GetSubsystemName()==FName(TEXT("STEAM")) ? OSS : nullptr;
    }
}

void UMCSteamSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Status=TEXT("Создай комнату или найди комнату друга.");
    if (GEngine)
    {
        NetworkHandle=GEngine->OnNetworkFailure().AddUObject(this,&ThisClass::NetworkFailed);
        TravelHandle=GEngine->OnTravelFailure().AddUObject(this,&ThisClass::TravelFailed);
    }
    GetSessions();
}

void UMCSteamSessionSubsystem::Deinitialize()
{
    if (Sessions)
    {
        Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
        Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
        Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
        Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
        Sessions->ClearOnSessionUserInviteAcceptedDelegate_Handle(InviteHandle);
    }
    if (GEngine)
    {
        GEngine->OnNetworkFailure().Remove(NetworkHandle);
        GEngine->OnTravelFailure().Remove(TravelHandle);
    }
    Search.Reset(); Sessions.Reset();
    Super::Deinitialize();
}

bool UMCSteamSessionSubsystem::CanUseSteam() const
{
    IOnlineSubsystem* OSS=SteamFor(this);
    const IOnlineIdentityPtr Identity=OSS?OSS->GetIdentityInterface():nullptr;
    return Identity && Identity->GetLoginStatus(0)==ELoginStatus::LoggedIn;
}

FString UMCSteamSessionSubsystem::GetPlayerName() const
{
    IOnlineSubsystem* OSS=SteamFor(this);
    const IOnlineIdentityPtr Identity=OSS?OSS->GetIdentityInterface():nullptr;
    return Identity?Identity->GetPlayerNickname(0):FString();
}

IOnlineSessionPtr UMCSteamSessionSubsystem::GetSessions()
{
    if (!Sessions && CanUseSteam())
    {
        Sessions=SteamFor(this)->GetSessionInterface();
        if (Sessions)
            InviteHandle=Sessions->AddOnSessionUserInviteAcceptedDelegate_Handle(
                FOnSessionUserInviteAcceptedDelegate::CreateUObject(this,&ThisClass::InviteAccepted));
    }
    return Sessions;
}

bool UMCSteamSessionSubsystem::HasRoom() const
{
    return Sessions && Sessions->GetNamedSession(NAME_GameSession)!=nullptr;
}

void UMCSteamSessionSubsystem::RefreshAvailability()
{
    GetSessions();
    if (CanUseSteam()) UE_LOG(LogTemp,Display,TEXT("MC_STEAM_READY"));
}

void UMCSteamSessionSubsystem::SetStatus(const FString& Text, bool Failed)
{
    Status=Text; bFailed=Failed;
    UE_LOG(LogTemp,Display,TEXT("MC_STEAM %s"),*Text);
}

bool UMCSteamSessionSubsystem::IsMessControlRoom(const FOnlineSessionSearchResult& Result)
{
    FString Value;
    return Result.IsValid() && Result.Session.SessionSettings.Get(GameKey,Value) && Value==GameValue;
}

void UMCSteamSessionSubsystem::HostRoom()
{
    if (IsBusy()) return;
    if (!GetSessions()) { SetStatus(TEXT("Запусти Steam, войди в аккаунт и перезапусти игру."),true); return; }
    if (HasRoom()) DestroyFor(EOperation::DestroyForHost);
    else CreateRoom();
}

void UMCSteamSessionSubsystem::CreateRoom()
{
    Operation=EOperation::Creating;
    SetStatus(TEXT("Создаём комнату Steam…"));
    FOnlineSessionSettings Settings;
    const auto* State=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    Settings.NumPublicConnections=State?FMath::Clamp(State->RunSettings.MaxPlayers,2,4):4;
    Settings.bIsLANMatch=false;
    Settings.bIsDedicated=false;
    Settings.bShouldAdvertise=true;
    Settings.bAllowJoinInProgress=true;
    Settings.bAllowInvites=true;
    Settings.bUsesPresence=true;
    Settings.bUseLobbiesIfAvailable=true;
    Settings.bAllowJoinViaPresence=true;
    Settings.Set(GameKey,GameValue,EOnlineDataAdvertisementType::ViaOnlineService);
    CreateHandle=Sessions->AddOnCreateSessionCompleteDelegate_Handle(
        FOnCreateSessionCompleteDelegate::CreateUObject(this,&ThisClass::Created));
    if (!Sessions->CreateSession(0,NAME_GameSession,Settings) && Operation==EOperation::Creating)
        Created(NAME_GameSession,false);
}

void UMCSteamSessionSubsystem::Created(FName Name, bool Success)
{
    if (Name!=NAME_GameSession || Operation!=EOperation::Creating) return;
    Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
    Operation=EOperation::Idle;
    if (!Success) { SetStatus(TEXT("Не удалось создать комнату. Проверь подключение Steam и попробуй снова."),true); return; }
    SetStatus(TEXT("Комната создана. Друг может найти её по твоему имени Steam или принять приглашение."));
    UE_LOG(LogTemp,Display,TEXT("MC_STEAM_ROOM_CREATED"));
    UGameplayStatics::OpenLevel(GetGameInstance(),FName(TEXT("/Game/Maps/L_Mouth")),true,TEXT("listen"));
}

void UMCSteamSessionSubsystem::FindRooms()
{
    if (IsBusy()) return;
    if (!GetSessions()) { SetStatus(TEXT("Запусти Steam, войди в аккаунт и перезапусти игру."),true); return; }
    Operation=EOperation::Finding;
    Rooms.Empty(); RoomLabels.Empty(); ++SearchRevision;
    SetStatus(TEXT("Ищем комнаты MessControl…"));
    Search=MakeShared<FOnlineSessionSearch>();
    Search->bIsLanQuery=false;
    Search->MaxSearchResults=100;
    Search->TimeoutInSeconds=25;
    Search->QuerySettings.Set(SEARCH_LOBBIES,true,EOnlineComparisonOp::Equals);
    Search->QuerySettings.Set(GameKey,GameValue,EOnlineComparisonOp::Equals);
    FindHandle=Sessions->AddOnFindSessionsCompleteDelegate_Handle(
        FOnFindSessionsCompleteDelegate::CreateUObject(this,&ThisClass::Found));
    if (!Sessions->FindSessions(0,Search.ToSharedRef()) && Operation==EOperation::Finding) Found(false);
}

void UMCSteamSessionSubsystem::Found(bool Success)
{
    if (Operation!=EOperation::Finding) return;
    Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
    Operation=EOperation::Idle;
    if (Success && Search)
    {
        for (const auto& Result:Search->SearchResults)
        {
            if (!IsMessControlRoom(Result) || Result.Session.NumOpenPublicConnections<=0) continue;
            Rooms.Add(Result);
            const int32 Capacity=Result.Session.SessionSettings.NumPublicConnections;
            RoomLabels.Add(FString::Printf(TEXT("%s · %d/%d"),*Result.Session.OwningUserName,
                Capacity-Result.Session.NumOpenPublicConnections,Capacity));
        }
    }
    ++SearchRevision;
    SetStatus(!Success?TEXT("Поиск не удался. Проверь Steam и попробуй снова."):
        Rooms.IsEmpty()?TEXT("Комнат пока нет. Повтори поиск или попроси приглашение через Steam."):
        TEXT("Выбери комнату друга и нажми «Подключиться»."),!Success);
    UE_LOG(LogTemp,Display,TEXT("MC_STEAM_ROOMS_FOUND count=%d success=%d"),Rooms.Num(),Success);
}

void UMCSteamSessionSubsystem::JoinRoom(int32 Index)
{
    if (IsBusy() || !Rooms.IsValidIndex(Index)) return;
    BeginJoin(Rooms[Index]);
}

void UMCSteamSessionSubsystem::BeginJoin(const FOnlineSessionSearchResult& Result)
{
    if (IsBusy()) return;
    if (!GetSessions() || !IsMessControlRoom(Result)) { SetStatus(TEXT("Эта комната недоступна для MessControl."),true); return; }
    PendingJoin=Result;
    if (HasRoom()) { DestroyFor(EOperation::DestroyForJoin); return; }
    Operation=EOperation::Joining;
    SetStatus(TEXT("Подключаемся к другу…"));
    JoinHandle=Sessions->AddOnJoinSessionCompleteDelegate_Handle(
        FOnJoinSessionCompleteDelegate::CreateUObject(this,&ThisClass::Joined));
    // Steam treats these flags as equivalent, including results received by invitation.
    PendingJoin.Session.SessionSettings.bUsesPresence=true;
    PendingJoin.Session.SessionSettings.bUseLobbiesIfAvailable=true;
    if (!Sessions->JoinSession(0,NAME_GameSession,PendingJoin) && Operation==EOperation::Joining)
        Joined(NAME_GameSession,EOnJoinSessionCompleteResult::UnknownError);
}

void UMCSteamSessionSubsystem::Joined(FName Name, EOnJoinSessionCompleteResult::Type Result)
{
    if (Name!=NAME_GameSession || Operation!=EOperation::Joining) return;
    Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
    Operation=EOperation::Idle;
    FString Address;
    if (Result!=EOnJoinSessionCompleteResult::Success || !Sessions->GetResolvedConnectString(Name,Address))
    {
        SetStatus(Result==EOnJoinSessionCompleteResult::SessionIsFull?TEXT("Комната заполнена."):
            TEXT("Не удалось подключиться. Обнови список и проверь, что у вас одинаковый билд."),true);
        return;
    }
    if (APlayerController* PC=GetGameInstance()->GetFirstLocalPlayerController())
    {
        SetStatus(TEXT("Загружаем комнату друга…"));
        UE_LOG(LogTemp,Display,TEXT("MC_STEAM_JOIN_TRAVEL"));
        PC->ClientTravel(Address,TRAVEL_Absolute);
    }
    else SetStatus(TEXT("Не найден локальный игрок для подключения."),true);
}

void UMCSteamSessionSubsystem::DestroyFor(EOperation Next)
{
    Operation=Next;
    SetStatus(TEXT("Выходим из предыдущей комнаты…"));
    DestroyHandle=Sessions->AddOnDestroySessionCompleteDelegate_Handle(
        FOnDestroySessionCompleteDelegate::CreateUObject(this,&ThisClass::Destroyed));
    if (!Sessions->DestroySession(NAME_GameSession) && Operation==Next) Destroyed(NAME_GameSession,false);
}

void UMCSteamSessionSubsystem::Destroyed(FName Name, bool Success)
{
    if (Name!=NAME_GameSession || (Operation!=EOperation::DestroyForHost && Operation!=EOperation::DestroyForJoin)) return;
    Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
    const EOperation Next=Operation; Operation=EOperation::Idle;
    if (!Success) { SetStatus(TEXT("Не удалось выйти из комнаты. Перезапусти игру."),true); return; }
    if (Next==EOperation::DestroyForHost) CreateRoom();
    else BeginJoin(PendingJoin);
}

void UMCSteamSessionSubsystem::InviteAccepted(bool Success, int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& Result)
{
    if (Success && ControllerId==0 && !IsBusy()) BeginJoin(Result);
}

void UMCSteamSessionSubsystem::InviteFriends()
{
    IOnlineSubsystem* OSS=SteamFor(this);
    const IOnlineExternalUIPtr UI=OSS?OSS->GetExternalUIInterface():nullptr;
    if (!HasRoom()) { SetStatus(TEXT("Сначала создай комнату."),true); return; }
    if (!UI || !UI->ShowInviteUI(0,NAME_GameSession))
        SetStatus(TEXT("Открой друзей Steam через Shift+Tab и отправь приглашение."));
}

void UMCSteamSessionSubsystem::NetworkFailed(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error)
{
    if (!World || World->GetGameInstance()!=GetGameInstance() || !CanUseSteam()) return;
    SetStatus(TEXT("Соединение потеряно. Хост мог выйти из игры. Открой F2, чтобы подключиться снова."),true);
}

void UMCSteamSessionSubsystem::TravelFailed(UWorld* World, ETravelFailure::Type Type, const FString& Error)
{
    if (!World || World->GetGameInstance()!=GetGameInstance() || !CanUseSteam()) return;
    SetStatus(TEXT("Не удалось загрузить комнату. Проверь одинаковую версию игры на обоих ПК."),true);
}
