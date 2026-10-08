#include "MCRecoveryPerkEffect.h"
#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UMCRecoveryPerkEffect::OnApplied_Implementation(FName PerkID,const FMCPerkDefinition&,int32 Stacks)
{
    RecoveryID=PerkID; TargetStacks=Stacks; ApplyPendingRecovery();
}

void UMCRecoveryPerkEffect::OnStacksChanged_Implementation(FName PerkID,const FMCPerkDefinition&,int32 Stacks)
{
    RecoveryID=PerkID; TargetStacks=Stacks;
    if (auto* Player=OwnerComponent?Cast<AMCPlayerState>(OwnerComponent->GetOwner()):nullptr)
        if (Player->HasAuthority()) Player->ConsumedRecoveryStacks.FindOrAdd(RecoveryID)
            =FMath::Min(Player->ConsumedRecoveryStacks.FindRef(RecoveryID),Stacks);
    ApplyPendingRecovery();
}

void UMCRecoveryPerkEffect::ApplyPendingRecovery()
{
    auto* Player=OwnerComponent?Cast<AMCPlayerState>(OwnerComponent->GetOwner()):nullptr;
    if (!Player || !Player->HasAuthority() || !GetWorld()) return;
    const int32 Pending=TargetStacks-Player->ConsumedRecoveryStacks.FindRef(RecoveryID);
    if (Pending<=0) { GetWorld()->GetTimerManager().ClearTimer(RecoveryTimer); return; }
    bool Applied=false;
    if (RecoveryID==TEXT("RecoveryMouthHeal"))
    {
        if (auto* Game=GetWorld()->GetGameState<AMCGameState>())
        {
            Game->MouthHealth=FMath::Min(Game->RunSettings.MaxMouthHealth,Game->MouthHealth+Pending*10.f);
            Game->ForceNetUpdate(); Applied=true;
        }
    }
    else if (RecoveryID==TEXT("RecoverySelfHeal") || RecoveryID==TEXT("RecoveryCleanse"))
    {
        if (auto* Hero=Cast<AMCToothCharacter>(Player->GetPawn()); Hero && Hero->Status && Hero->Status->IsAlive())
        {
            FMCToothStatus Status=Hero->Status->State;
            if (RecoveryID==TEXT("RecoverySelfHeal")) Status.Health=FMath::Min(Status.MaxHealth,Status.Health+Pending*25.f);
            else { Status.CoffeeLeft=0; Status.RepairLeft=0; }
            Hero->Status->Restore(Status); Applied=true;
        }
    }
    else return;
    if (Applied)
    {
        Player->ConsumedRecoveryStacks.Add(RecoveryID,TargetStacks);
        GetWorld()->GetTimerManager().ClearTimer(RecoveryTimer);
    }
    else if (!GetWorld()->GetTimerManager().IsTimerActive(RecoveryTimer))
        GetWorld()->GetTimerManager().SetTimer(RecoveryTimer,this,&UMCRecoveryPerkEffect::ApplyPendingRecovery,.25f,true);
}

void UMCRecoveryPerkEffect::OnRemoved_Implementation(FName PerkID)
{
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(RecoveryTimer);
    if (auto* Player=OwnerComponent?Cast<AMCPlayerState>(OwnerComponent->GetOwner()):nullptr)
        if (Player->HasAuthority()) Player->ConsumedRecoveryStacks.Remove(PerkID);
    RecoveryID=NAME_None; TargetStacks=0;
}
