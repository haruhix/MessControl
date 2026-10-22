#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "MCGameState.h"
#include "MCSingleDayDirector.h"
#include "MCGameDirector.h"
#include "MCGameDirectorProfile.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCInventoryComponent.h"
#include "MCToothMovementComponent.h"
#include "MCFoodCollectionComponent.h"
#include "MCIceEvent.h"
#include "MCFogBrawlEvent.h"
#include "MCNutRainEvent.h"
#include "MCRewardChest.h"
#include "MCRoguelikeDirector.h"
#include "EngineUtils.h"

namespace
{
bool HasRemainingOpeningCoffee(const AMCSingleDayDirector* Sequence)
{
    if(!Sequence || Sequence->IsLegacyTimedFinale() || Sequence->MealPortionCount<2) return false;
    switch(Sequence->Stage) {
    case EMCSingleDayStage::Training:
    case EMCSingleDayStage::FirstPerk:
    case EMCSingleDayStage::OpeningPause:return true;
    case EMCSingleDayStage::FirstMeal:return Sequence->MealPortionNumber<Sequence->MealPortionCount;
    case EMCSingleDayStage::MealCoffeeWave:return true;
    case EMCSingleDayStage::BetweenMealsPause:return Sequence->MealPortionNumber+1<Sequence->MealPortionCount;
    default:return false;
    }
}
FName NextRogueEvent(const UWorld* World)
{
    const auto* GS=World?World->GetGameState<AMCGameState>():nullptr;
    const auto* Sequence=GS?GS->SingleDayDirector.Get():nullptr;
    const auto* Profile=Sequence?Sequence->GetRunProfile():nullptr;
    if(!Profile || Sequence->Stage==EMCSingleDayStage::Complete) return NAME_None;
    if(Sequence->Stage==EMCSingleDayStage::PostNutMeal || Sequence->Stage==EMCSingleDayStage::PostNutMint) return TEXT("CoffeeFlood");
    if(HasRemainingOpeningCoffee(Sequence)) return TEXT("CoffeeWave");
    if(!Profile->KeyEvents.IsValidIndex(Sequence->KeyEventIndex)) return NAME_None;
    switch(Profile->KeyEvents[Sequence->KeyEventIndex].Kind) {
    case EMCSingleDayKeyEventKind::NutEncounter:return TEXT("NutEncounter");
    case EMCSingleDayKeyEventKind::IceEvent:return TEXT("IceEvent");
    case EMCSingleDayKeyEventKind::FogBrawl:return TEXT("FogBrawl");
    case EMCSingleDayKeyEventKind::Jellies:return TEXT("CoffeeFlood");
    }
    return NAME_None;
}
}

bool UMCPerkComponent::HasRunEvent(FName Event) const
{
    if(Event.IsNone() || Event==TEXT("Any")) return true;
    if(Event==TEXT("Progression")) return false; // XP/level mechanics have been retired.
    const auto* GS=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;if(!GS) return false;
    if(GS->RogueOptionalEvents.Contains(Event)) return true;
    if(Event==TEXT("Food")) return GS->DayPlan!=nullptr;
    if(Event==TEXT("Mimic")) {
        for(TActorIterator<AMCRoguelikeDirector> It(GetWorld());It;++It) if(It->ChestClass && It->ChestClass.GetDefaultObject()->MimicChance>0) return true;
        for(TActorIterator<AMCRewardChest> It(GetWorld());It;++It) if(It->IsMimicActive() || It->MimicChance>0) return true;
        return false;
    }
    if(Event==TEXT("IceEvent")) for(TActorIterator<AMCIceEvent> It(GetWorld());It;++It) if(It->IsActive()) return true;
    if(Event==TEXT("FogBrawl")) for(TActorIterator<AMCFogBrawlEvent> It(GetWorld());It;++It) if(It->IsActive()) return true;
    if(Event==TEXT("NutEncounter") || Event==TEXT("NutBossKnight") || Event==TEXT("NutBossMage"))
        for(TActorIterator<AMCNutRainEvent> It(GetWorld());It;++It) if(It->Stage!=EMCNutRainStage::Idle && It->Stage!=EMCNutRainStage::Complete) return true;
    const auto* Sequence=GS->SingleDayDirector.Get();
    const auto* Profile=Sequence?Sequence->GetRunProfile():nullptr;
    if(Profile && Sequence->Stage!=EMCSingleDayStage::Complete) {
        const bool PastNuts=Sequence->Stage==EMCSingleDayStage::PostNutMeal || Sequence->Stage==EMCSingleDayStage::PostNutMint;
        for(int32 I=FMath::Max(0,Sequence->KeyEventIndex+(PastNuts?1:0));I<Profile->KeyEvents.Num();++I) {
            const auto Kind=Profile->KeyEvents[I].Kind;
            if(Kind==EMCSingleDayKeyEventKind::NutEncounter && (Event==TEXT("NutEncounter") || Event==TEXT("NutBossKnight") || Event==TEXT("NutBossMage"))) return true;
            if(Kind==EMCSingleDayKeyEventKind::IceEvent && Event==TEXT("IceEvent")) return true;
            if(Kind==EMCSingleDayKeyEventKind::FogBrawl && Event==TEXT("FogBrawl")) return true;
            if(Kind==EMCSingleDayKeyEventKind::Jellies && (Event==TEXT("CoffeeFlood") || Event==TEXT("CoffeeWave"))) return true;
        }
        if(Event==TEXT("CoffeeWave") && HasRemainingOpeningCoffee(Sequence)) return true;
    }
    const auto* Director=AMCGameDirector::Find(GetWorld());
    const UMCGameDirectorProfile* Support=Director?Director->Settings.Get():Sequence?Sequence->GetSupportProfile():nullptr;
    if(Support) for(const auto& Rule:Support->Events) if(Rule.Weight>0) {
        if(Event==TEXT("Food") && Rule.Kind==EMCGameDirectorEvent::Food) return true;
        if(Event==TEXT("CoffeeWave") && (Rule.Kind==EMCGameDirectorEvent::Coffee || Rule.Kind==EMCGameDirectorEvent::CoffeeFlood)) return true;
        if(Event==TEXT("CoffeeFlood") && Rule.Kind==EMCGameDirectorEvent::CoffeeFlood) return true;
        if(Event==TEXT("FinalBoss") && Rule.Kind==EMCGameDirectorEvent::Boss) return true;
    }
    return false;
}
int32 UMCPerkComponent::RoguePolarityStacks(EMCPerkPolarity Polarity) const
{
    int32 Count=0;
    for(const auto& Active:ActivePerks) if(const auto* Row=FindDefinition(Active.PerkID);Row && Row->bRoguePerk && Row->Polarity==Polarity) Count+=Active.Stacks;
    return Count;
}
bool UMCPerkComponent::HasMeaningfulRogueChange(FName ID) const
{
    const auto* Row=FindDefinition(ID);if(!Row || !Row->bRoguePerk) return true;
    // Triggered mechanics are useful in their eligible future context, even while inactive now.
    if(Row->Modifiers.IsEmpty()) return true;
    const auto* Hero=Cast<AMCPlayerState>(GetOwner());
    const auto* Pawn=Hero?Cast<AMCToothCharacter>(Hero->GetPawn()):nullptr;
    for(const auto& M:Row->Modifiers) {
        if(M.Condition!=EMCRogueActivation::Always) return true;
        float Base=1;
        switch(M.Stat) {
        case EMCRogueStat::MaxHealth:Base=Pawn && Pawn->Status?Pawn->Status->GetBaseMaxHealth():100;break;
        case EMCRogueStat::MaxStamina:Base=Pawn?CastChecked<UMCToothMovementComponent>(Pawn->GetCharacterMovement())->MaxStamina:100;break;
        case EMCRogueStat::CarryCapacity:Base=Pawn && Pawn->FoodCollection?Pawn->FoodCollection->MaxPieces:6;break;
        case EMCRogueStat::WarmDuration:Base=12;break;
        case EMCRogueStat::ChainsawCrashDamage:Base=30;break;
        default:break;
        }
        if(!FMath::IsNearlyEqual(RogueValue(M.Stat,Base),RogueValue(M.Stat,Base,nullptr,ID),.0001f)) return true;
    }
    return false;
}
bool UMCPerkComponent::IsRogueEligible(FName ID) const
{
    const auto* Row=FindDefinition(ID);if(!Row) return false;
    // Keep old DataTable rows compatible, but never offer XP-only modifiers
    // through a chest after removing the experience economy.
    if(Row->Modifiers.ContainsByPredicate([](const FMCRogueModifier& Modifier) {
        return Modifier.Stat==EMCRogueStat::DeliveryXP;
    })) return false;
    if(!Row->bRoguePerk) {
        const auto* PS=Cast<AMCPlayerState>(GetOwner());const auto* Hero=PS?Cast<AMCToothCharacter>(PS->GetPawn()):nullptr;
        const auto* GS=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
        if(ID==TEXT("RecoverySelfHeal")) return Hero && Hero->Status->IsAlive() && Hero->Status->State.Health<Hero->Status->State.MaxHealth;
        if(ID==TEXT("RecoveryMouthHeal")) return GS && GS->MouthHealth<GS->RunSettings.MaxMouthHealth;
        if(ID==TEXT("RecoveryCleanse")) return Hero && (Hero->Status->State.CoffeeLeft>0 || Hero->Status->State.RepairLeft>0);
        return true;
    }
    if(Row->RequiredTool!=EMCToolUpgrade::None && !HasToolUpgrade(Row->RequiredTool)) return false;
    if(Row->bRequiresTeam) {
        const auto* GS=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;TSet<const AMCPlayerState*> Players;
        if(GS) for(const auto& State:GS->PlayerArray) if(const auto* Player=Cast<AMCPlayerState>(State);Player && !Player->IsSpectator() && !Player->IsOnlyASpectator()) Players.Add(Player);
        if(Players.Num()<2) return false;
    }
    if(!Row->RequiredEvents.IsEmpty() && !Row->RequiredEvents.ContainsByPredicate([this](FName Event){return HasRunEvent(Event);})) return false;
    if(Row->Polarity==EMCPerkPolarity::Negative && RoguePolarityStacks(Row->Polarity)>=3) return false;
    if(Row->Polarity==EMCPerkPolarity::Risky && RoguePolarityStacks(Row->Polarity)>=3) return false;
    return HasMeaningfulRogueChange(ID);
}
bool UMCPerkComponent::BuildRogueOffer(EMCPerkPolarity Polarity,int32 Seed,TArray<FName>& Out,bool Early,bool RareOnly,bool GuaranteeRare,const TArray<FName>& Excluded,bool IncludeTools) const
{
    Out.Reset();if(!LoadedTable) return false;
    const bool RogueCatalog=LoadedTable->GetRowNames().ContainsByPredicate([this](FName ID){const auto* Row=FindDefinition(ID);return Row && Row->bRoguePerk;});
    TArray<FName> Full;
    for(FName ID:LoadedTable->GetRowNames()) if(const auto* Row=FindDefinition(ID);Row && Row->Polarity==Polarity
        && Row->bAvailableForLevelChoice && Row->Weight>0 && FMath::IsFinite(Row->Weight) && !Row->bConsumableBooster && CanGrantPerk(ID)) {
        if(Early && RogueCatalog && (!Row->bEarlyChoice || Row->Rarity!=EMCPerkRarity::Standard)) continue;
        if(!IncludeTools && Row->ToolUpgrade!=EMCToolUpgrade::None) continue;
        if(RareOnly && (!Row->bRoguePerk || Row->Rarity==EMCPerkRarity::Standard)) continue;
        if(Row->ToolUpgrade!=EMCToolUpgrade::None && HasToolUpgrade(Row->ToolUpgrade)) continue;
        Full.Add(ID);
    }
    Full.Sort([](FName A,FName B){return A.LexicalLess(B);});
    if(Full.Num()<3) return false;
    auto Family=[this](FName ID){const auto* Row=FindDefinition(ID);return Row && !Row->ChoiceFamily.IsNone()?Row->ChoiceFamily:ID;};
    for(int32 Attempt=0;Attempt<24;++Attempt) {
        FRandomStream Random(Seed+Attempt*104729);TArray<FName> Pool=Full;
        if(Attempt<12) Pool.RemoveAll([&](FName ID){return Excluded.Contains(ID);});
        bool Legendary=false;Out.Reset();
        for(int32 Card=0;Card<3;++Card) {
            TArray<FName> Tiers[3];
            for(FName ID:Pool) {const auto* Row=FindDefinition(ID);if(Row->Rarity!=EMCPerkRarity::Legendary || !Legendary) Tiers[uint8(Row->Rarity)].Add(ID);}
            int32 Tier=0;
            if(Card==0 && GuaranteeRare && !Tiers[1].IsEmpty()) Tier=1;
            else {
                const float Weights[3]={70,25,5};float Total=0;for(int32 I=0;I<3;++I) if(!Tiers[I].IsEmpty()) Total+=Weights[I];
                if(Total<=0) break;
                float Draw=Random.FRand()*Total;for(int32 I=0;I<3;++I) if(!Tiers[I].IsEmpty()) {Tier=I;Draw-=Weights[I];if(Draw<=0) break;}
            }
            if(Tiers[Tier].IsEmpty()) break;
            auto Weight=[&](FName ID) {
                const auto* Row=FindDefinition(ID);float Value=Row->Weight;
                if(GetStacks(ID)>0 || (Row->RequiredTool!=EMCToolUpgrade::None && HasToolUpgrade(Row->RequiredTool))) Value*=1.5f;
                const FName Next=NextRogueEvent(GetWorld());
                if(!Next.IsNone() && Row->RequiredEvents.Contains(Next)) Value*=1.5f;
                return Value;
            };
            float Total=0;for(FName ID:Tiers[Tier]) Total+=Weight(ID);
            float Draw=Random.FRand()*Total;FName Chosen=Tiers[Tier].Last();
            for(FName ID:Tiers[Tier]) {Draw-=Weight(ID);if(Draw<=0) {Chosen=ID;break;}}
            Out.Add(Chosen);Legendary|=FindDefinition(Chosen)->Rarity==EMCPerkRarity::Legendary;
            const FName SelectedFamily=Family(Chosen);Pool.RemoveAll([&](FName ID){return Family(ID)==SelectedFamily;});
        }
        if(Out.Num()==3) return true;
    }
    Out.Reset();return false;
}
FString UMCPerkComponent::RoguePreview(FName ID) const
{
    const auto* Row=FindDefinition(ID);if(!Row) return FString();
    FString Result=FString::Printf(TEXT("Стак %d → %d / %d"),GetStacks(ID),GetStacks(ID)+1,Row->MaxStacks);
    for(const auto& M:Row->Modifiers) if(M.Condition==EMCRogueActivation::Always) {
        float Base=1;FString Label;
        switch(M.Stat) {
        case EMCRogueStat::NormalSpeed:Label=TEXT("Скорость");break;
        case EMCRogueStat::MaxHealth: {
            Label=TEXT("Макс. HP");const auto* PS=Cast<AMCPlayerState>(GetOwner());
            const auto* Hero=PS?Cast<AMCToothCharacter>(PS->GetPawn()):nullptr;
            Base=Hero && Hero->Status?Hero->Status->GetBaseMaxHealth():100;break;
        }
        case EMCRogueStat::PickaxeDamage:case EMCRogueStat::ChainsawDamage:case EMCRogueStat::WeaponDamage:Label=TEXT("Урон");break;
        case EMCRogueStat::BrushPower:Label=TEXT("Чистка");break;
        case EMCRogueStat::CarryCapacity: {
            Label=TEXT("Куски");const auto* PS=Cast<AMCPlayerState>(GetOwner());
            const auto* Hero=PS?Cast<AMCToothCharacter>(PS->GetPawn()):nullptr;
            Base=Hero && Hero->FoodCollection?Hero->FoodCollection->MaxPieces:6;break;
        }
        default:continue;
        }
        const float Before=RogueValue(M.Stat,Base),After=RogueValue(M.Stat,Base,nullptr,ID);
        Result+=Base==1?FString::Printf(TEXT(" · %s %.0f%% → %.0f%%"),*Label,Before*100,After*100):FString::Printf(TEXT(" · %s %.0f → %.0f"),*Label,Before,After);
        break;
    }
    return Result;
}
