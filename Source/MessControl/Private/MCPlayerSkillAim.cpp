#include "MCPlayerController.h"

#include "MCActiveSkillPerkEffect.h"
#include "MCGrenadeSkillAction.h"
#include "MCHookSkillAction.h"
#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "MCRocketSkillAction.h"
#include "MCToothCharacter.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

bool AMCPlayerController::RequiresSkillAim(FName PerkID)
{
    return PerkID==TEXT("Consumable_Rocket") || PerkID==TEXT("Active_Hook");
}

FMCActiveSkillInputHandle AMCPlayerController::MakeSkillInputHandle(int32 SlotIndex,FGuid InstanceID)
{
    if(++NextSkillPressSequence==0) ++NextSkillPressSequence;
    FMCActiveSkillInputHandle Handle;
    Handle.SlotIndex=SlotIndex;
    Handle.SlotInstanceID=InstanceID;
    Handle.PressSequence=NextSkillPressSequence;
    return Handle;
}

bool AMCPlayerController::IsAimSlotUsable(int32 SlotIndex,FGuid InstanceID) const
{
    const auto* State=GetPlayerState<AMCPlayerState>();
    const auto* Perks=State?State->Perks.Get():nullptr;
    if(!InstanceID.IsValid() || !Perks || !Perks->ActiveSkillSlots.IsValidIndex(SlotIndex)
        || bLoadingInputBlocked || bPauseMenuOpen || IsRewardInteractionActive()) return false;
    const auto& Slot=Perks->ActiveSkillSlots[SlotIndex];
    const auto* Definition=Perks->FindDefinition(Slot.PerkID);
    return Slot.InstanceID==InstanceID && RequiresSkillAim(Slot.PerkID)
        && Definition && UMCPerkComponent::IsDefinitionUsable(*Definition) && Perks->CanUseSkillActor();
}

void AMCPlayerController::SelectAimedSkill(int32 SlotIndex,FGuid InstanceID)
{
    const bool bToggleOff=SelectedAimSlot==SlotIndex && SelectedAimInstance==InstanceID && SelectedAimPawn.Get()==GetPawn();
    ClearAimedSkillSelection();
    auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    if(bToggleOff || !Hero || Hero->GetActiveSkillPresentation() || !IsAimSlotUsable(SlotIndex,InstanceID)) return;
    Hero->SuspendToolInputForSkillAim();
    SelectedAimSlot=SlotIndex;
    SelectedAimInstance=InstanceID;
    SelectedAimPawn=Hero;
}

void AMCPlayerController::EndLocalSkillAim()
{
    if(!LocalSkillAim.IsValid()) return;
    const auto EndedAim=LocalSkillAim;
    LocalSkillAim={};
    if(IsLocalController() && GetWorld() && !GetWorld()->bIsTearingDown)
        ServerSetSkillAimHeld(EndedAim,false);
}

void AMCPlayerController::ClearAimedSkillSelection()
{
    EndLocalSkillAim();
    SelectedAimSlot=INDEX_NONE;
    SelectedAimInstance.Invalidate();
    SelectedAimPawn.Reset();
}

bool AMCPlayerController::IsAimedSkillSelected() const
{
    return IsLocalController() && SelectedAimPawn.IsValid() && SelectedAimPawn.Get()==GetPawn()
        && IsAimSlotUsable(SelectedAimSlot,SelectedAimInstance);
}

bool AMCPlayerController::IsSkillAimHeld() const
{
    return LocalSkillAim.IsValid() && IsAimedSkillSelected();
}

FName AMCPlayerController::GetAimedSkillID() const
{
    return IsAimedSkillSelected()?GetPlayerState<AMCPlayerState>()->Perks->ActiveSkillSlots[SelectedAimSlot].PerkID:NAME_None;
}

float AMCPlayerController::GetAimedSkillCooldown() const
{
    return IsAimedSkillSelected()?GetPlayerState<AMCPlayerState>()->Perks->GetSkillCooldownRemaining(SelectedAimSlot):0.f;
}

const AMCActiveSkillAction* AMCPlayerController::SelectedAimDefaults(const FMCPerkDefinition*& OutDefinition) const
{
    const auto* Perks=GetPlayerState<AMCPlayerState>()->Perks.Get();
    OutDefinition=Perks->FindDefinition(Perks->ActiveSkillSlots[SelectedAimSlot].PerkID);
    const auto* Factory=OutDefinition?Cast<UMCActiveSkillPerkEffect>(OutDefinition->EffectClass.GetDefaultObject()):nullptr;
    return Factory?Factory->ActionClass.GetDefaultObject():nullptr;
}

bool AMCPlayerController::ReadSkillAimPose(UStaticMesh*& OutMesh,FMCActiveSkillAimPose& OutPose) const
{
    const auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    if(!Hero || !IsSkillAimHeld() || GetViewTarget()!=Hero) return false;
    const FMCPerkDefinition* Definition=nullptr;
    const auto* Defaults=SelectedAimDefaults(Definition);
    // Both props are retained by world preload.
    if(!Defaults || !Definition || !(OutMesh=Defaults->PropAsset.Get())) return false;
    if(const auto* Rocket=Cast<AMCRocketSkillAction>(Defaults))
        return Rocket->BuildReadyAimPose(*Hero,Definition->Parameters,OutPose);
    if(const auto* Hook=Cast<AMCHookSkillAction>(Defaults)) {
        Hook->BuildReadyPose(Definition->Parameters,OutPose.HandsLocal[1],OutPose.PropLocal);
        return true;
    }
    return false;
}

bool AMCPlayerController::ShouldUseSkillShoulderCamera() const
{
    if(IsSkillAimHeld()) return true;
    const auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    if(!Hero || !Hero->CanWork() || bShowMouseCursor || IsLookInputIgnored()) return false;
    const auto* Grenade=Cast<AMCGrenadeSkillAction>(Hero->GetActiveSkillPresentation());
    if(!Grenade || Grenade->Phase!=EMCGrenadeSkillPhase::Cooking) return false;
    if(Grenade->IsThrowStrokeActive()) return true;
    const auto* Held=HeldSkillInputs.Find(Grenade->GetSlotIndex());
    return Held && *Held==Grenade->GetInputHandle();
}

bool AMCPlayerController::RouteSkillAimInput(bool bPressed)
{
    if(!bPressed) {
        if(!bSkillAimGestureConsumed) return false;
        bSkillAimGestureConsumed=false;
        EndLocalSkillAim();
        return true;
    }
    if(bSkillAimGestureConsumed) return true;
    if(!IsAimedSkillSelected()) return false;
    bSkillAimGestureConsumed=true;
    LocalSkillAim=MakeSkillInputHandle(SelectedAimSlot,SelectedAimInstance);
    ServerSetSkillAimHeld(LocalSkillAim,true);
    return true;
}

bool AMCPlayerController::RouteSkillFireInput(bool bPressed)
{
    if(!bPressed) {
        const bool bConsumed=bSkillFireGestureConsumed;
        bSkillFireGestureConsumed=false;
        return bConsumed;
    }
    if(bSkillFireGestureConsumed) return true;
    if(!IsAimedSkillSelected()) return false;
    // A selected device owns the fire gesture even before aiming. Holding
    // fire before aim cannot queue a shot or start the underlying tool action.
    bSkillFireGestureConsumed=true;
    if(IsSkillAimHeld() && GetViewTarget()==GetPawn()) {
        FVector View;FRotator Rotation;
        GetPlayerViewPoint(View,Rotation);
        ServerBeginSkillInput(MakeSkillInputHandle(SelectedAimSlot,SelectedAimInstance),View,Rotation.Vector());
    }
    return true;
}

bool AMCPlayerController::SuppressesOrdinaryBraceInput(bool bMouseGesture) const
{
    return (bMouseGesture?bSkillAimGestureConsumed:bSkillFireGestureConsumed) || IsAimedSkillSelected();
}

void AMCPlayerController::ServerSetSkillAimHeld_Implementation(FMCActiveSkillInputHandle Handle,bool bHeld)
{
    if(!Handle.IsValid()) return;
    if(!bHeld) {
        if(ServerSkillAim==Handle) {
            ServerSkillAim={};
            ServerSkillAimPawn.Reset();
        }
        return;
    }
    if(int32(Handle.PressSequence-LastServerSkillPressSequence)<=0 || !IsAimSlotUsable(Handle.SlotIndex,Handle.SlotInstanceID)) return;
    LastServerSkillPressSequence=Handle.PressSequence;
    ServerSkillAim=Handle;
    ServerSkillAimPawn=GetPawn();
}

void AMCPlayerController::RefreshSkillAimState()
{
    if(SelectedAimSlot!=INDEX_NONE && !IsAimedSkillSelected()) {
        const bool bPawnChanged=SelectedAimPawn.Get()!=GetPawn();
        ClearAimedSkillSelection();
        if(bPawnChanged) {
            bSkillAimGestureConsumed=false;
            bSkillFireGestureConsumed=false;
        }
    }
    if(HasAuthority() && ServerSkillAim.IsValid()
        && (ServerSkillAimPawn.Get()!=GetPawn() || !IsAimSlotUsable(ServerSkillAim.SlotIndex,ServerSkillAim.SlotInstanceID))) {
        ServerSkillAim={};
        ServerSkillAimPawn.Reset();
    }
}

bool AMCPlayerController::BuildSkillAimPreview(FMCActiveSkillAimPreview& Out) const
{
    const auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    if(!IsLocalController() || !Hero || !GetWorld() || bShowMouseCursor || IsLookInputIgnored() || !Hero->CanWork()) return false;
    const auto* Grenade=Cast<AMCGrenadeSkillAction>(Hero->GetActiveSkillPresentation());
    const bool bCooking=Grenade && Grenade->Phase==EMCGrenadeSkillPhase::Cooking
        && !Grenade->IsThrowStrokeActive() && ShouldUseSkillShoulderCamera();
    if(!bCooking && !IsSkillAimHeld()) return false;

    const FGuid Identity=bCooking?Grenade->GetActionID():LocalSkillAim.SlotInstanceID;
    const uint32 Sequence=bCooking?0:LocalSkillAim.PressSequence;
    const double Now=GetWorld()->GetRealTimeSeconds();
    if(Identity!=CachedSkillAimIdentity || Sequence!=CachedSkillAimSequence || Now>=NextSkillAimSampleAt) {
        CachedSkillAimIdentity=Identity;
        CachedSkillAimSequence=Sequence;
        NextSkillAimSampleAt=Now+.05;
        CachedSkillAimPreview={};
        bCachedSkillAim=false;
        if(bCooking) {
            bCachedSkillAim=Grenade->BuildAimPreview(CachedSkillAimPreview);
        } else {
            const FMCPerkDefinition* Definition=nullptr;
            const auto* Defaults=SelectedAimDefaults(Definition);
            if(const auto* Rocket=Cast<AMCRocketSkillAction>(Defaults))
                bCachedSkillAim=Rocket->BuildAimPreview(*Hero,Definition->Parameters,CachedSkillAimPreview);
            else if(const auto* Hook=Cast<AMCHookSkillAction>(Defaults))
                bCachedSkillAim=Hook->BuildAimPreview(*Hero,Definition->Parameters,CachedSkillAimPreview);
        }
    }
    if(!bCachedSkillAim) return false;
    Out=CachedSkillAimPreview;
    return true;
}
