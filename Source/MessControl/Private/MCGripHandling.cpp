#include "MCGripComponent.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"

void UMCGripComponent::RefreshCanonicalHandling()
{
    auto* H=Cast<AMCToothCharacter>(GetOwner());if(!H || !H->HasAuthority()) return;
    FMCHandlingMoveSample S;
    S.LoadEpoch=H->GetSimulationFrame().LoadEpoch;
    uint32 Membership=GetTypeHash(S.LoadEpoch);
    TSet<const AMCFoodActor*> Seen;
    const auto AddFood=[&](const AMCFoodActor* Food,bool Stack) {
        if(!IsValid(Food) || Food->IsDisposed() || Seen.Contains(Food)) return;
        Seen.Add(Food);const float Mass=FMath::Clamp(Food->Settings.Mass,0.f,1000.f);
        S.LoadMass+=Mass;if(Stack) S.StackMass+=Mass;
        Membership=HashCombine(Membership,HashCombine(GetTypeHash(Food->GetUniqueID()),GetTypeHash(uint8(Food->Phase))));
    };
    if(H->FoodCollection) for(const auto& Food:H->FoodCollection->Pieces)
        if(IsValid(Food) && Food->StackCarrier==H) AddFood(Food,true);
    AddFood(Frame.Food,false);AddFood(Secondary.Food,false);
    AddFood(Cast<AMCFoodActor>(BraceTarget()),false);
    TSet<const AMCToothCharacter*> Players;
    for(const auto* Player:{GrabbedPlayer.Get(),Cast<AMCToothCharacter>(BraceTarget())}) if(IsValid(Player) && !Players.Contains(Player)) {
        Players.Add(Player);S.LoadMass+=FMath::Max(3.f,Player->ToothPhysics->Settings.Mass);
        Membership=HashCombine(Membership,GetTypeHash(Player->GetUniqueID()));
    }
    S.IncomingMass=IncomingChainMass();S.LoadMass+=S.IncomingMass;
    S.bPlayerPull=IsValid(GrabbedPlayer);
    S.bHeavyGrip=S.bPlayerPull || (IsValid(H->HeldFood) && !CanCarry(H->HeldFood));
    if(S.bHeavyGrip && IsValid(H->HeldFood)) S.DragSpeedLimit=H->HeldFood->DragSpeed();
    S.LoadMass=FMath::Clamp(S.LoadMass,0.f,10000.f);S.StackMass=FMath::Min(S.StackMass,S.LoadMass);
    S.IncomingMass=FMath::Clamp(S.IncomingMass,0.f,10000.f);S.Revision=AcceptedHandling.Revision;
    if(!S.Revision || Membership!=HandlingMembershipHash || !S.Equals(AcceptedHandling)) {
        if(!++S.Revision) ++S.Revision;
        AcceptedHandling=S;HandlingMembershipHash=Membership;H->ForceNetUpdate();
    }
}
FMCHandlingMoveSample UMCGripComponent::CaptureHandlingForMove() const
{
    const auto* H=Cast<AMCToothCharacter>(GetOwner());if(!H || !GetWorld()) return {};
    if(GetOwner()->HasAuthority()) const_cast<UMCGripComponent*>(this)->RefreshCanonicalHandling();
    const FGuid Epoch=H->GetSimulationFrame().LoadEpoch;
    if(AcceptedHandling.LoadEpoch!=Epoch) {
        FMCHandlingMoveSample Neutral;Neutral.LoadEpoch=Epoch;return Neutral;
    }
    return AcceptedHandling;
}
