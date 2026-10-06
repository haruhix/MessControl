#pragma once
#include "CoreMinimal.h"
#include "MCPlaytestBotTypes.generated.h"

UENUM(BlueprintType)
enum class EMCPlaytestBotSkill : uint8 { Novice, Regular, Skilled };

/** Decision differences only. Movement, damage, contact and resources keep the player rules. */
struct FMCPlaytestBotTuning
{
    float DecisionSeconds=.45f;
    float SightRadius=1800.f;
    float CommitSeconds=2.f;
    float HesitationChance=.06f;
    float AimErrorDegrees=7.f;
    float HazardReactionSeconds=.5f;
    float CooperationPenalty=650.f;
    static FMCPlaytestBotTuning ForSkill(EMCPlaytestBotSkill Skill)
    {
        FMCPlaytestBotTuning T;
        if(Skill==EMCPlaytestBotSkill::Novice) {
            T.DecisionSeconds=.8f; T.SightRadius=1400; T.CommitSeconds=3;
            T.HesitationChance=.18f; T.AimErrorDegrees=18; T.HazardReactionSeconds=1.1f; T.CooperationPenalty=250;
        } else if(Skill==EMCPlaytestBotSkill::Skilled) {
            T.DecisionSeconds=.25f; T.SightRadius=2200; T.CommitSeconds=1.2f;
            T.HesitationChance=.01f; T.AimErrorDegrees=2; T.HazardReactionSeconds=.25f; T.CooperationPenalty=1000;
        }
        return T;
    }
};

inline const TCHAR* MCPlaytestSkillName(EMCPlaytestBotSkill Skill)
{
    return Skill==EMCPlaytestBotSkill::Novice?TEXT("novice"):Skill==EMCPlaytestBotSkill::Skilled?TEXT("skilled"):TEXT("regular");
}
