#include "MCToothCharacter.h"
#include "MCPlayerState.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCFoodActor.h"
#include "MCFoodBodyComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

bool AMCToothCharacter::AllowsPeerMovementPassThrough() const
{
    const auto* Identity=GetPlayerState<AMCPlayerState>();
    const auto* Move=GetCharacterMovement();
    return bPeerMovementCollisionStarted && !IsActorBeingDestroyed() && !bPreviewAnimation
        && !ActorHasTag(TEXT("MC_VFXLabRuntime")) && Identity && Identity->GetPawn()==this && !Identity->IsOnlyASpectator()
        && Status && Status->IsAlive() && ToothPhysics && ToothPhysics->CanAct()
        && !SwallowedBy && !IsMimicCaptured() && !IsSeatedOnRaft() && !OrderJumpTarget
        && Move && (Move->IsMovingOnGround() || Move->IsFalling());
}

void AMCToothCharacter::SetPeerMovementIgnored(AMCToothCharacter* Peer,bool Ignore)
{
    if(!Peer || Peer==this) return;
    SetMovementContactReason(Peer,EMCMovementContactReason::PeerWalking,Ignore);
}

bool AMCToothCharacter::AllowsStableFoodMovementPassThrough() const
{
    const auto* Identity=GetPlayerState<AMCPlayerState>();
    const auto* Move=GetCharacterMovement();
    return bPeerMovementCollisionStarted && !IsActorBeingDestroyed() && !bPreviewAnimation
        && !ActorHasTag(TEXT("MC_VFXLabRuntime")) && Identity && Identity->GetPawn()==this && !Identity->IsOnlyASpectator()
        && Status && Status->IsAlive() && ToothPhysics && ToothPhysics->CanAct()
        && !SwallowedBy && !IsMimicCaptured() && !IsSeatedOnRaft() && !OrderJumpTarget
        && Move && Move->MovementMode!=MOVE_None;
}

void AMCToothCharacter::SetMovementContactReason(AActor* Other,EMCMovementContactReason Reason,bool Ignore)
{
    MovementContacts.Set(GetCapsuleComponent(),Other,Reason,Ignore);
}

void AMCToothCharacter::RefreshFoodMovementContact(AMCFoodActor* Food)
{
    if(!Food || Food->GetWorld()!=GetWorld()) return;
    const bool Eligible=AllowsStableFoodMovementPassThrough() && !Food->IsDisposed();
    const bool Stable=Eligible && !Food->HasWalkingObstacle();
    SetMovementContactReason(Food,EMCMovementContactReason::StableFood,Stable);
    const bool Carried=Food->StackCarrier==this || (Food->Phase==EMCFoodPhase::Carried && Food->Holders.Contains(this));
    SetMovementContactReason(Food,EMCMovementContactReason::CarriedFood,Carried);
    {
        auto* Move=GetCharacterMovement();
        const auto* Base=GetMovementBase();
        // Simulated food never owns character movement. A fixed query-only
        // blocker is stable support; world and raft bases retain ownership.
        const bool FixedBase=Food->HasWalkingObstacle() && Base==Food->WalkingBlocker;
        const bool FoodBase=Base && Base->GetOwner()==Food && !FixedBase;
        if(FoodBase) SetBase(static_cast<FMovementBaseInterfaceData*>(nullptr));
        const bool PhysicalFloor=Move->CurrentFloor.HitResult.GetActor()==Food
            && (!Food->HasWalkingObstacle() || Move->CurrentFloor.HitResult.GetComponent()!=Food->WalkingBlocker);
        if(FoodBase || PhysicalFloor) {
            Move->CurrentFloor.Clear();Move->bForceNextFloorCheck=true;
        }
    }
}

void AMCToothCharacter::ForgetFoodMovementContact(AMCFoodActor* Food)
{
    MovementContacts.Forget(GetCapsuleComponent(),Food);
    if(!Food)return;
    auto* Move=GetCharacterMovement();
    const auto* Base=GetMovementBase();
    const bool FoodBase=Base && Base->GetOwner()==Food;
    if(FoodBase)SetBase(static_cast<FMovementBaseInterfaceData*>(nullptr));
    if(FoodBase || Move->CurrentFloor.HitResult.GetActor()==Food) {
        Move->CurrentFloor.Clear();Move->bForceNextFloorCheck=true;
    }
}

void AMCToothCharacter::NotifyFoodMovementContact(AMCFoodActor* Food,bool Removed)
{
    if(!Food || !Food->GetWorld()) return;
    for(TActorIterator<AMCToothCharacter> It(Food->GetWorld());It;++It) {
        if(Removed) It->ForgetFoodMovementContact(Food);
        else It->RefreshFoodMovementContact(Food);
    }
}

void AMCToothCharacter::RefreshNetworkMovementContacts(bool Force)
{
    RefreshPeerMovementCollision(Force);
}

void AMCToothCharacter::RefreshPeerMovementCollision(bool Force)
{
    if(!bPeerMovementCollisionStarted || !GetWorld()) return;
    const bool Eligible=AllowsPeerMovementPassThrough();
    const bool StableFood=AllowsStableFoodMovementPassThrough();
    const bool PeerChanged=Force || Eligible!=bPeerMovementEligible;
    const bool FoodChanged=Force || StableFood!=bStableFoodMovementEligible;
    if(!PeerChanged && !FoodChanged) return;
    bPeerMovementEligible=Eligible;
    bStableFoodMovementEligible=StableFood;
    // At most a transition/identity scan, never a world scan per movement tick.
    // Apply on every peer: autonomous and authority sweeps use the same policy.
    if(PeerChanged) for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(*It!=this) {
        const bool Ignore=Eligible && It->AllowsPeerMovementPassThrough();
        SetPeerMovementIgnored(*It,Ignore);
        It->SetPeerMovementIgnored(this,Ignore);
    }
    if(FoodChanged) for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) RefreshFoodMovementContact(*It);
}

void AMCToothCharacter::ClearPeerMovementCollision()
{
    MovementContacts.Release();
    PeerMovementIgnores.Reset();bPeerMovementEligible=false;bStableFoodMovementEligible=false;
    // The other capsule may own its entry even if ours predated this policy.
    if(GetWorld()) for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        if(*It!=this) It->SetPeerMovementIgnored(this,false);
}

void AMCToothCharacter::OnPlayerStateChanged(APlayerState* NewState,APlayerState* OldState)
{
    Super::OnPlayerStateChanged(NewState,OldState);
    // Native SetPlayerState and OnRep_PlayerState both reach this hook.
    RefreshPeerMovementCollision(true);
}

void AMCToothCharacter::UnPossessed()
{
    Super::UnPossessed();
    RefreshPeerMovementCollision(true);
}
