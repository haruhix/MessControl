#include "MCRewardChest.h"
#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "MCPlayerController.h"
#include "MCToothCharacter.h"

bool AMCRewardChest::PrepareRogueLoot(UMCPerkComponent* Perks)
{
    if(!HasAuthority() || !Perks) return false;
    const auto* PS=Cast<AMCPlayerState>(Perks->GetOwner());
    if(!PS || PS->PersonalChoicesTaken<2) return false;
    const int32 Seed=RollSeed^GetTypeHash(PS->GetPlayerId());
    if(RogueKind==EMCRogueRewardKind::RiskyAltar) {
        Polarity=EMCPerkPolarity::Risky;
        return Perks->BuildRogueOffer(Polarity,Seed,LootIDs);
    }
    if(RogueKind!=EMCRogueRewardKind::CursedContract) return false;
    if(!Perks->BuildRogueOffer(EMCPerkPolarity::Negative,Seed,ContractCurses)
        || !Perks->BuildRogueOffer(EMCPerkPolarity::Positive,Seed^0x52455744,ContractRewards,false,true)) return false;
    ContractCurse=ContractReward=NAME_None;ContractStep=0;LootIDs=ContractCurses;Polarity=EMCPerkPolarity::Negative;
    return true;
}
FMCRogueChestOffer AMCRewardChest::GetRogueOffer() const
{
    FMCRogueChestOffer Offer;Offer.Kind=RogueKind;Offer.IDs=LootIDs;Offer.RewardIDs=ContractRewards;
    Offer.Curse=ContractCurse;Offer.Reward=ContractReward;Offer.Step=ContractStep;return Offer;
}
void AMCRewardChest::PublishRogueOffer()
{
    if(HasAuthority() && OpeningPlayer) if(auto* PC=Cast<AMCPlayerController>(OpeningPlayer->GetController()))
        PC->ClientShowRogueChoices(this,GetRogueOffer());
}
bool AMCRewardChest::ConfirmRogueContract(AMCToothCharacter* Player)
{
    if(!HasAuthority() || bClaimInProgress || Stage!=EMCRewardChestStage::Open || RogueKind!=EMCRogueRewardKind::CursedContract
        || ContractStep!=2 || Player!=OpeningPlayer || !OpenerCanContinue()) return false;
    auto* PS=Player->GetPlayerState<AMCPlayerState>();
    if(!PS || !PS->Perks || !ContractCurses.Contains(ContractCurse) || !ContractRewards.Contains(ContractReward)) return false;
    TGuardValue<bool> Guard(bClaimInProgress,true);
    // One validated batch: callbacks see both final stacks and cannot apply half a deal.
    if(!PS->Perks->ServerGrantPerks({ContractCurse,ContractReward})) return false;
    ClaimedMask=7;PS->RecordChestRewardChoice();ForceNetUpdate();FinishReward(false);return true;
}
bool AMCRewardChest::DeclineRogueReward(AMCToothCharacter* Player)
{
    if(!HasAuthority() || bClaimInProgress || Stage!=EMCRewardChestStage::Open || RogueKind==EMCRogueRewardKind::Ordinary
        || Player!=OpeningPlayer || !OpenerCanContinue()) return false;
    // Declining consumes this optional opportunity, so it cannot block the reward queue forever.
    FinishReward(false);return true;
}
