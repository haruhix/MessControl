#include "MCHubProfileSubsystem.h"
#include "MCPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

FString UMCHubProfileSubsystem::ProfilePath(const AMCPlayerState* State,const APlayerController* Controller) const
{
    if(!State || !Controller || !State->HasAuthority() || !GetWorld()) return {};
    // Owned solo validation never seeds or overwrites the user's profile.
    if(FParse::Param(FCommandLine::Get(),TEXT("MCHubTransientProfile"))) return {};
    FString Identity;
    if(State->GetUniqueId().IsValid()) Identity=State->GetUniqueId()->ToString();
    else if(Controller->IsLocalController()) Identity=TEXT("LocalProfile");
    // An unauthenticated remote guest has session inventory on PlayerState,
    // but must not share another guest's persistent wallet by display name.
    else return {};
    const FString Key=FSHA1::HashBuffer(TCHAR_TO_UTF8(*Identity),FTCHARToUTF8(*Identity).Length()).ToString();
    return FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("HubProfiles"),
        GetWorld()->WorldType==EWorldType::PIE?TEXT("PIE"):TEXT("Players"),Key+TEXT(".sav"));
}

bool UMCHubProfileSubsystem::Restore(AMCPlayerState* State,APlayerController* Controller)
{
    if(!State || !State->HasAuthority() || !State->Economy) return false;
    if(State->bHubProfileLoaded) return true;
    State->bHubProfileLoaded=true;
    const FString Path=ProfilePath(State,Controller);
    if(Path.IsEmpty()) return true;
    TArray<uint8> Bytes;
    if(const auto* Cached=CachedBytes.Find(Path)) Bytes=*Cached;
    else if(!FFileHelper::LoadFileToArray(Bytes,*Path)) {
        if(IFileManager::Get().FileExists(*Path)) {
            ProtectedProfiles.Add(Path);
            UE_LOG(LogTemp,Warning,TEXT("Hub profile could not be read; the existing save is preserved."));
        }
        return true;
    }
    const auto* Save=Cast<UMCHubProfileSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if(!Save || Save->Version!=1) {
        ProtectedProfiles.Add(Path);
        UE_LOG(LogTemp,Warning,TEXT("Hub profile version is unreadable; the existing save is preserved."));return true;
    }
    FText Error;
    if(!State->Economy->ImportSave(Save->Economy,Error)) {
        ProtectedProfiles.Add(Path);
        UE_LOG(LogTemp,Warning,TEXT("Hub profile was not applied: %s"),*Error.ToString());
        return true;
    }
    // Keep the original bytes before the first additive economy migration or quarantine save.
    if(Save->Economy.Version<2 || !State->Economy->MigrationNotice.IsEmpty()) {
        const FString Backup=Path+(Save->Economy.Version<2?TEXT(".pre-v2.bak"):TEXT(".quarantine.bak"));
        if(!IFileManager::Get().FileExists(*Backup) && !FFileHelper::SaveArrayToFile(Bytes,*Backup)) {
            ProtectedProfiles.Add(Path);
            UE_LOG(LogTemp,Warning,TEXT("Hub profile migration backup failed; the original save is protected from overwrite."));
        }
    }
    if(!Error.IsEmpty()) UE_LOG(LogTemp,Warning,TEXT("Hub profile retained compatibility data: %s"),*Error.ToString());
    State->FishCaught=FMath::Clamp(Save->Fish,0,1000000);State->ForceNetUpdate();
    CachedBytes.Add(Path,MoveTemp(Bytes));return true;
}

void UMCHubProfileSubsystem::Store(AMCPlayerState* State,APlayerController* Controller,bool bSettlePending)
{
    if(!State || !State->HasAuthority() || !State->bHubProfileLoaded || !State->Economy) return;
    if(!bSettlePending && State->Economy->HasPendingGrillRoll()) return;
    const FString Path=ProfilePath(State,Controller);
    if(Path.IsEmpty() || ProtectedProfiles.Contains(Path)) return;
    auto* Save=NewObject<UMCHubProfileSave>();
    Save->Economy=State->Economy->ExportSave();Save->Fish=State->FishCaught;
    TArray<uint8> Bytes;
    if(!UGameplayStatics::SaveGameToMemory(Save,Bytes)) return;
    if(const auto* Cached=CachedBytes.Find(Path);Cached && *Cached==Bytes) return;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    const FString Temporary=Path+TEXT(".tmp");
    if(!FFileHelper::SaveArrayToFile(Bytes,*Temporary) || !IFileManager::Get().Move(*Path,*Temporary,true,true)) {
        UE_LOG(LogTemp,Warning,TEXT("Could not save the hub profile."));return;
    }
    CachedBytes.Add(Path,MoveTemp(Bytes));
}

bool UMCHubProfileSubsystem::EnsureShopStockForPlayer(AMCPlayerState* State)
{
    if(!State || !State->HasAuthority() || !State->bHubProfileLoaded || !State->Economy) return false;
    if(!ShopRotationId.IsValid()) {
        ShopRotationId=FGuid::NewGuid();
        ShopRotationSeed=int32(ShopRotationId.A^ShopRotationId.B^ShopRotationId.C^ShopRotationId.D);
    }
    return State->Economy->EnsureShopStock(ShopRotationId,ShopRotationSeed);
}

void UMCHubProfileSubsystem::MarkShopRunCompleted(const FGuid& RunId)
{
    if(!GetWorld() || GetWorld()->GetNetMode()==NM_Client || !RunId.IsValid() || LastCompletedShopRunId==RunId) return;
    LastCompletedShopRunId=RunId;ShopRotationId.Invalidate();ShopRotationSeed=0;
}
