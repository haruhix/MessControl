#if !UE_BUILD_SHIPPING
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCInventoryComponent.h"
#include "MCGameState.h"
#include "MCDayDirector.h"
#include "MCFoodActor.h"
#include "MCHazardWave.h"
#include "MCMouthSurface.h"
#include "MCTongue.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

// A real remote owner attacks another client; all four peers observe server health.
void MCTickWeaponPlayerNetworkValidation(UWorld* W) {
    struct FRun {
        TWeakObjectPtr<UWorld> W;
        float Age=0;int32 Selected=-1,Sent=-1,Checked=-1;uint8 Seen=0;
        bool Setup=false,Failed=false;
    };static FRun R;if(R.W!=W) {R=FRun();R.W=W;}
    R.Age+=W->GetDeltaSeconds();
    auto* GS=W->GetGameState<AMCGameState>();auto* PC=W->GetFirstPlayerController();
    if(!GS || !PC) return;
    const bool Host=W->GetNetMode()!=NM_Client;
    const double Now=GS->GetServerWorldTimeSeconds(),T=GS->TasksTotal==5434?Now-GS->DayStartedAt:-1;
    auto Finish=[&]() {
        const bool Pass=!R.Failed && R.Seen==15;
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s WEAPON_PVP net=%d observed=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),int32(W->GetNetMode()),R.Seen);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    };
    if(T>(Host?14:13) || R.Age>110) {Finish();return;}
    TArray<AMCToothCharacter*> Heroes;
    for(TActorIterator<AMCToothCharacter> It(W);It;++It) if(It->GetPlayerState()) Heroes.Add(*It);
    Heroes.Sort([](const AMCToothCharacter& A,const AMCToothCharacter& B){return A.GetPlayerState()->GetPlayerId()<B.GetPlayerState()->GetPlayerId();});
    if(Heroes.Num()!=4) return;
    const auto* Profile=LoadObject<UMCEquipmentProfile>(nullptr,TEXT("/Game/Data/DA_Equipment.DA_Equipment"));
    const float Damage[4]={25,FMath::Max(1.f,Profile?Profile->PickaxeDamage:40),FMath::Max(1.f,Profile?Profile->KnifeDamage:25),0};
    const float MaxHP=100+Damage[0]+Damage[1]+Damage[2];
    if(Host && !R.Setup && R.Age>8) {
        bool Ready=GS->PlayerArray.Num()==4;
        for(FConstPlayerControllerIterator It=W->GetPlayerControllerIterator();It;++It)
            Ready&=It->Get()->GetPawn() && (It->Get()->IsLocalController() || It->Get()->AcknowledgedPawn==It->Get()->GetPawn());
        if(!Ready) return;
        if(auto* Mode=W->GetAuthGameMode()) Mode->SetActorTickEnabled(false);
        for(TActorIterator<AMCDayDirector> It(W);It;++It) It->SetActorTickEnabled(false);
        for(TActorIterator<AMCFoodActor> It(W);It;++It) It->Dispose();
        for(TActorIterator<AMCHazardWave> It(W);It;++It) It->Destroy();
        for(TActorIterator<AMCMouthSurface> It(W);It;++It) if(It->bUlcer) It->Destroy();
        for(TActorIterator<AMCTongue> It(W);It;++It) {It->bAutomaticYawns=false;It->ResetPain();It->Settings.bAutomaticJolts=false;It->ForceNetUpdate();}
        // Isolate contacts from the mouth geometry while keeping participants
        // within the normal replication radius of the bounded game camera.
        const FVector Positions[4]={{10000,-600,4000},{10000,0,4000},{10125,0,4000},{10000,600,4000}};
        for(int32 I=0;I<4;++I) {
            auto* H=Heroes[I];H->CancelGameplayInput();H->Status->Initialize(MaxHP);
            H->SetActorLocationAndRotation(Positions[I],FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
            H->GetCharacterMovement()->StopMovementImmediately();H->GetCharacterMovement()->DisableMovement();
            // Keep the four stage contacts in place. Automation separately checks real knockdowns.
            H->ToothPhysics->Settings.Knockback=0;H->ToothPhysics->Settings.Lift=0;
            H->ValidatedSwingCount=H->ConfirmedHitCount=0;H->ForceNetUpdate();
        }
        GS->bDevManualEvents=true;GS->bPhysicalBrushes=false;GS->Phase=EMCShiftPhase::Working;GS->PhaseEndsAt=0;
        GS->TasksTotal=5434;GS->DayStartedAt=Now+1;GS->ForceNetUpdate();R.Setup=true;return;
    }
    if(T<0) return;
    const int32 Stage=FMath::Min(3,int32(T/3));const double StageAge=T-Stage*3;
    const EMCToolSlot Slots[4]={EMCToolSlot::Brush,EMCToolSlot::Pickaxe,EMCToolSlot::Knife,EMCToolSlot::Spray};
    auto* Own=Cast<AMCToothCharacter>(PC->GetPawn());
    if(Own==Heroes[1]) {
        if(R.Selected!=Stage) {Own->Inventory->ServerSelect(Slots[Stage]);R.Selected=Stage;}
        if(StageAge>.8 && R.Sent!=Stage && Own->Inventory->Selected==Slots[Stage]) {
            Own->SwingBrush();Own->SwingBrush();Own->SwingBrush();R.Sent=Stage;
        }
    }
    if(StageAge>2.2 && R.Checked!=Stage) {
        float Expected=MaxHP;for(int32 I=0;I<=Stage;++I) Expected-=Damage[I];
        bool OK=FMath::IsNearlyEqual(Heroes[2]->Status->State.Health,Expected,.01f);
        for(int32 I:{0,1,3}) OK&=FMath::IsNearlyEqual(Heroes[I]->Status->State.Health,MaxHP,.01f);
        if(Host) OK&=Heroes[1]->ConfirmedHitCount==FMath::Min(3,Stage+1) && Heroes[1]->ValidatedSwingCount==FMath::Min(3,Stage+1);
        R.Failed|=!OK;R.Checked=Stage;if(OK) R.Seen|=uint8(1<<Stage);
        UE_LOG(LogTemp,Display,TEXT("MC_WEAPON_PVP_CHECK net=%d %s stage=%d hp=%.1f expected=%.1f server_hits=%d"),int32(W->GetNetMode()),OK?TEXT("PASS"):TEXT("FAIL"),Stage,Heroes[2]->Status->State.Health,Expected,Host?Heroes[1]->ConfirmedHitCount:-1);
    }
}
#endif
