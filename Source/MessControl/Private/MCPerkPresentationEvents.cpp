#include "MCPerkPresentationEvents.h"
#include "MCPerkComponent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCFoodCollectionComponent.h"
#include "MCInventoryComponent.h"
#include "MCAirSkillAction.h"
#include "MCVFXPresentationComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include <initializer_list>

namespace
{
struct FObservedState
{
    bool bCarry=false,bCleanBuff=false,bDeliveryBuff=false;
    FGuid CarryEpisode,CleanEpisode,DeliveryEpisode;
};
TMap<TWeakObjectPtr<AMCToothCharacter>,FObservedState> Observed;
double PresentationNow(const UWorld* World)
{return World?(World->GetGameState()?World->GetGameState()->GetServerWorldTimeSeconds():World->GetTimeSeconds()):0.;}

const TMap<FName,TArray<FName>>& Contracts()
{
    static const TMap<FName,TArray<FName>> Values=[]
    {
        TMap<FName,TArray<FName>> Result;
        auto Add=[&](const TCHAR* ID,std::initializer_list<const TCHAR*> Extra)
        {
            TArray<FName> Events={TEXT("Equipped"),TEXT("State")};
            for(const TCHAR* Event:Extra)Events.Add(FName(Event));
            Result.Add(FName(ID),MoveTemp(Events));
        };
        Add(TEXT("B1_HeavyHit"),{TEXT("Hit")});
        Add(TEXT("B1_LightFoot"),{});Add(TEXT("B1_DeepBreath"),{});
        Add(TEXT("B1_BigPortion"),{TEXT("Pickup")});
        Add(TEXT("B1_ConfidentThrow"),{TEXT("Throw")});
        Add(TEXT("B1_FreshDelivery"),{TEXT("CarryStart"),TEXT("CarryEnd")});
        Add(TEXT("B1_GreedyHands"),{TEXT("Pickup")});
        Add(TEXT("B1_WideBristles"),{TEXT("Contact")});
        Add(TEXT("B1_UnderPressure"),{TEXT("SprayStart"),TEXT("SprayEnd")});
        Add(TEXT("B2_ReturnTrip"),{TEXT("Proc"),TEXT("End")});
        Add(TEXT("B2_CleanBreath"),{TEXT("Proc")});
        Add(TEXT("B2_NeatFreak"),{TEXT("Link")});
        Add(TEXT("B2_LastStroke"),{TEXT("FinishContact")});
        Add(TEXT("B2_CleanAcceleration"),{TEXT("Proc"),TEXT("End")});
        Add(TEXT("B2_RefreshingWork"),{TEXT("Proc")});
        Add(TEXT("B2_ThickEnamel"),{TEXT("Proc")});Add(TEXT("B2_Shield"),{TEXT("Proc")});
        Add(TEXT("B2_SecondWind"),{TEXT("Proc")});
        Add(TEXT("B2_HoudiniDash"),{TEXT("Vanish"),TEXT("Return")});
        Add(TEXT("B2_MightyTooth"),{TEXT("Cleave")});Add(TEXT("B2_StartFromDash"),{TEXT("Proc")});
        Add(TEXT("B2_ThunderGod"),{TEXT("Link")});
        Add(TEXT("B2_EchoSwing"),{TEXT("Windup"),TEXT("Echo")});
        Add(TEXT("B2_Billiards"),{TEXT("Launch"),TEXT("Impact")});
        Add(TEXT("B2_Softener"),{TEXT("Coat"),TEXT("End")});
        Add(TEXT("B2_Antiseptic"),{TEXT("SprayStart"),TEXT("SprayEnd"),TEXT("Clean")});
        Add(TEXT("B2_LastingFormula"),{TEXT("Coat"),TEXT("Refresh"),TEXT("End"),TEXT("Damage")});
        return Result;
    }();
    return Values;
}
}

const TArray<FName>& MCPassivePresentation::GetAllowedEvents(FName PerkID)
{
    if(const auto* Events=Contracts().Find(PerkID))return *Events;
    static const TArray<FName> Empty;return Empty;
}

EMCVFXPresentationResult MCPassivePresentation::Play(AMCToothCharacter* Hero,FName PerkID,FName Event,
    const FTransform& Frame,const FMCPerkVFXGameplayValues& Values)
{
    const auto* Perks=IsValid(Hero)?UMCPerkComponent::ForActor(Hero):nullptr;
    if(!IsValid(Hero) || !Hero->HasAuthority() || !Perks || Perks->GetStacks(PerkID)<=0
        || !GetAllowedEvents(PerkID).Contains(Event) || !Hero->PassivePresentation
        || !Hero->PassivePresentation->IsSourcePrepared(PerkID))
        return EMCVFXPresentationResult::UseDefault;
    // The dispatcher owns prepared source snapshots. This route never loads
    // assets, changes a stat, rerolls a proc or blocks a gameplay outcome.
    FMCPerkVFXGameplayValues Resolved=Values;
    if(Event==TEXT("Proc") && (PerkID==TEXT("B2_CleanAcceleration") || PerkID==TEXT("B2_ReturnTrip")))
    {
        auto& State=Observed.FindOrAdd(Hero);
        FGuid& Previous=PerkID==TEXT("B2_CleanAcceleration")?State.CleanEpisode:State.DeliveryEpisode;
        if(!Resolved.EpisodeID.IsValid())Resolved.EpisodeID=FGuid::NewGuid();
        if(Previous.IsValid() && Previous!=Resolved.EpisodeID)StopEpisode(Hero,PerkID,Previous,false);
        Previous.Invalidate();
        (PerkID==TEXT("B2_CleanAcceleration")?State.bCleanBuff:State.bDeliveryBuff)=true;
        const auto Result=Hero->PassivePresentation->PlaySourceEvent(PerkID,Event,Frame,Resolved);
        // Audio-only Add preserves the native VFX result but can own a live or
        // pending player. Retain its episode for the real expiration edge too.
        if(Result==EMCVFXPresentationResult::Played || Hero->PassivePresentation->IsSourceEpisodeRunning(PerkID,Resolved.EpisodeID)) {
            auto& Current=Observed.FindOrAdd(Hero);
            (PerkID==TEXT("B2_CleanAcceleration")?Current.CleanEpisode:Current.DeliveryEpisode)=Resolved.EpisodeID;
        }
        return Result;
    }
    return Hero->PassivePresentation->PlaySourceEvent(PerkID,Event,Frame,Resolved);
}

bool MCPassivePresentation::UsesNativeFallback(EMCVFXPresentationResult Result)
{return Result==EMCVFXPresentationResult::UseDefault || Result==EMCVFXPresentationResult::Rejected;}

void MCPassivePresentation::StopEpisode(AMCToothCharacter* Hero,FName PerkID,FGuid EpisodeID,bool bImmediate)
{
    if(IsValid(Hero) && Hero->HasAuthority() && Hero->PassivePresentation && EpisodeID.IsValid())
        Hero->PassivePresentation->StopSourceEpisode(PerkID,EpisodeID,bImmediate);
}

bool MCPassivePresentation::UpdateEpisode(AMCToothCharacter* Hero,FName PerkID,FGuid EpisodeID,
    const FTransform& Frame,const FMCPerkVFXGameplayValues& Values)
{
    return IsValid(Hero) && Hero->HasAuthority() && Hero->PassivePresentation && EpisodeID.IsValid()
        && Hero->PassivePresentation->UpdateSourceEpisode(PerkID,EpisodeID,Frame,Values);
}

void MCPassivePresentation::TickStates(AMCToothCharacter* Hero)
{
    for(auto It=Observed.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
    if(!IsValid(Hero) || !Hero->HasAuthority())return;
    if(!Hero->Status || !Hero->Status->IsAlive()){Observed.Remove(Hero);return;}
    const auto* Perks=UMCPerkComponent::ForActor(Hero);
    if(!Perks || !Hero->PassivePresentation)return;
    auto& State=Observed.FindOrAdd(Hero);
    const FTransform Frame(Hero->GetActorRotation(),Hero->GetActorLocation());
    const FName Fresh(TEXT("B1_FreshDelivery"));
    if(Hero->PassivePresentation->IsSourcePrepared(Fresh))
    {
        const auto* Air=Cast<AMCAirSkillAction>(Hero->GetActiveSkillPresentation());
        const bool Carry=Perks->GetStacks(Fresh)>0 && (Hero->HeldFood
            || (Hero->FoodCollection && !Hero->FoodCollection->Pieces.IsEmpty()) || (Air && Air->StoredCount>0));
        if(Carry!=State.bCarry)
        {
            if(Carry){State.CarryEpisode=FGuid::NewGuid();FMCPerkVFXGameplayValues Values;Values.EpisodeID=State.CarryEpisode;
                const auto Result=Play(Hero,Fresh,TEXT("CarryStart"),Frame,Values);
                if(Result!=EMCVFXPresentationResult::Played && !Hero->PassivePresentation->IsSourceEpisodeRunning(Fresh,State.CarryEpisode))State.CarryEpisode.Invalidate();}
            else{StopEpisode(Hero,Fresh,State.CarryEpisode);Play(Hero,Fresh,TEXT("CarryEnd"),Frame);State.CarryEpisode.Invalidate();}
            State.bCarry=Carry;
        }
    }
    const double Now=PresentationNow(Hero->GetWorld());
    auto Expire=[&](FName ID,double Until,bool& WasActive,FGuid& Episode)
    {
        if(!Hero->PassivePresentation->IsSourcePrepared(ID))return;
        const bool Active=Perks->GetStacks(ID)>0 && Until>Now;
        if(WasActive && !Active){StopEpisode(Hero,ID,Episode,false);Play(Hero,ID,TEXT("End"),Frame);Episode.Invalidate();}
        WasActive=Active;
    };
    Expire(TEXT("B2_CleanAcceleration"),Perks->CleanSpeedUntil,State.bCleanBuff,State.CleanEpisode);
    Expire(TEXT("B2_ReturnTrip"),Perks->DeliverySpeedUntil,State.bDeliveryBuff,State.DeliveryEpisode);
}

void MCPassivePresentation::ForgetSourceState(AMCToothCharacter* Hero,FName PerkID)
{
    if(!IsValid(Hero))return;
    if(auto* State=Observed.Find(Hero)) {
        if(PerkID==TEXT("B1_FreshDelivery")){State->bCarry=false;State->CarryEpisode.Invalidate();}
        else if(PerkID==TEXT("B2_CleanAcceleration")){State->bCleanBuff=false;State->CleanEpisode.Invalidate();}
        else if(PerkID==TEXT("B2_ReturnTrip")){State->bDeliveryBuff=false;State->DeliveryEpisode.Invalidate();}
    }
    if(Hero->Inventory)Hero->Inventory->ResetPassivePresentationSource(PerkID);
}
