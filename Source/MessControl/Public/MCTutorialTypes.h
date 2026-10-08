#pragma once

#include "CoreMinimal.h"
#include "MCTutorialTypes.generated.h"

class AMCPlayerState;
class AActor;

UENUM(BlueprintType)
enum class EMCTutorialStage : uint8
{
    Loading, Intro, BrushTooth, FoodCut, FreshSort, SpoiledSort, TrashSort,
    BreakfastRain, BreakfastCleanup, CoffeeWarning, CoffeeWaves, CoffeeCleanup,
    Calculus, FoamParty, Complete, Finished,
    ToothpickPull, ToothpickBreak, ToothpickHeal
};

/** Only authoritative gameplay code reports these events; clients never submit lesson credit. */
UENUM(BlueprintType)
enum class EMCTutorialAction : uint8
{
    BrushTooth, BrushTongue, FoodCut, FoodDelivered, TrashDiscarded,
    SpoiledDiscarded, CalculusCleared, Anchored
};

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCTutorialPlayerProgress
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) TObjectPtr<AMCPlayerState> PlayerState;
    UPROPERTY(BlueprintReadOnly) TObjectPtr<AActor> GoalTarget;
    UPROPERTY(BlueprintReadOnly) int32 CompletedActions=0;
    UPROPERTY(BlueprintReadOnly) int32 StageActions=0;
    UPROPERTY(BlueprintReadOnly) int32 StageProgress=0;
    UPROPERTY(BlueprintReadOnly) int32 StageRequired=0;
    UPROPERTY(BlueprintReadOnly) bool bLoaded=false;
    UPROPERTY(BlueprintReadOnly) bool bReady=false;
};

/** Shared, world-independent progression rules, also used by focused automation checks. */
struct MESSCONTROL_API FMCTutorialProgressRules
{
    static bool IsSharedStage(EMCTutorialStage Stage)
    { return Stage==EMCTutorialStage::ToothpickPull || Stage==EMCTutorialStage::ToothpickBreak || Stage==EMCTutorialStage::ToothpickHeal; }
    static EMCTutorialStage NextShortStage(EMCTutorialStage Stage)
    {
        switch(Stage) {
        case EMCTutorialStage::Intro: return EMCTutorialStage::BrushTooth;
        case EMCTutorialStage::BrushTooth: return EMCTutorialStage::FoodCut;
        case EMCTutorialStage::FoodCut: return EMCTutorialStage::FreshSort;
        case EMCTutorialStage::FreshSort: return EMCTutorialStage::CoffeeCleanup;
        case EMCTutorialStage::CoffeeCleanup: return EMCTutorialStage::ToothpickPull;
        case EMCTutorialStage::ToothpickPull: return EMCTutorialStage::ToothpickBreak;
        case EMCTutorialStage::ToothpickBreak: return EMCTutorialStage::ToothpickHeal;
        case EMCTutorialStage::ToothpickHeal: return EMCTutorialStage::Complete;
        default: return EMCTutorialStage::Complete;
        }
    }
    static int32 ActionBit(EMCTutorialAction Action)
    { const uint8 Index=static_cast<uint8>(Action); return Index<=static_cast<uint8>(EMCTutorialAction::Anchored)?1 << Index:0; }
    static int32 RequiredActions(EMCTutorialStage Stage)
    {
        switch (Stage)
        {
        case EMCTutorialStage::BrushTooth: return ActionBit(EMCTutorialAction::BrushTooth);
        case EMCTutorialStage::FoodCut: return ActionBit(EMCTutorialAction::FoodCut);
        case EMCTutorialStage::FreshSort: return ActionBit(EMCTutorialAction::FoodDelivered);
        case EMCTutorialStage::SpoiledSort: return ActionBit(EMCTutorialAction::SpoiledDiscarded);
        case EMCTutorialStage::CoffeeCleanup: return ActionBit(EMCTutorialAction::BrushTongue);
        case EMCTutorialStage::Calculus: return ActionBit(EMCTutorialAction::CalculusCleared);
        default: return 0;
        }
    }
    static bool AcceptsAction(EMCTutorialStage Stage,EMCTutorialAction Action)
    { return (RequiredActions(Stage) & ActionBit(Action))!=0; }
    static bool CreditAction(FMCTutorialPlayerProgress& Progress,EMCTutorialStage Stage,EMCTutorialAction Action,bool bOwnTarget)
    {
        const int32 Bit=ActionBit(Action);
        if (!bOwnTarget || !AcceptsAction(Stage,Action) || (Progress.StageActions & Bit)!=0) return false;
        Progress.StageActions|=Bit; Progress.CompletedActions|=Bit;
        Progress.StageRequired=1; Progress.StageProgress=1;
        return true;
    }
    static bool SetReady(FMCTutorialPlayerProgress& Progress,EMCTutorialStage Stage,bool bReady)
    {
        if (Stage!=EMCTutorialStage::Complete || Progress.bReady==bReady) return false;
        Progress.bReady=bReady; return true;
    }
    static bool AllLoaded(TConstArrayView<FMCTutorialPlayerProgress> Players)
    {
        if (Players.IsEmpty()) return false;
        for (const auto& Player:Players) if (!Player.bLoaded) return false;
        return true;
    }
    static bool AllReady(TConstArrayView<FMCTutorialPlayerProgress> Players)
    {
        if (Players.IsEmpty()) return false;
        for (const auto& Player:Players) if (!Player.bReady) return false;
        return true;
    }
    static int32 CompletedPlayers(TConstArrayView<FMCTutorialPlayerProgress> Players)
    {
        int32 Count=0;
        for (const auto& Player:Players) if (Player.StageRequired>0 && Player.StageProgress>=Player.StageRequired) ++Count;
        return Count;
    }
};
