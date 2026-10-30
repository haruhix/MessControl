#pragma once

#include "CoreMinimal.h"
#include "MCActiveSkillAction.h"
#include "MCActiveSkillPerkEffect.h"
#include "MCPerkComponent.h"
#include "MCActiveSkillTestTypes.generated.h"

/** Inert native protocol probe; never registered in the production reward catalog. */
UCLASS(Transient,NotPlaceable)
class AMCActiveSkillProtocolProbe : public AMCActiveSkillAction
{
    GENERATED_BODY()
public:
    int32 Starts=0,Releases=0,Cancels=0,Finishes=0;
    bool bRegisteredAtStart=false;
    EMCActiveSkillEndReason LastReason=EMCActiveSkillEndReason::Completed;
    virtual bool CanStart() const override {return Super::CanStart() && Param(TEXT("Reject"),0.f)==0;}
    virtual void StartCommitted() override
    {Super::StartCommitted();++Starts;bRegisteredAtStart=Perks() && Perks()->GetActiveSkillActions().Contains(this);}
    virtual void ReleaseInput() override
    {++Releases;if(Param(TEXT("RepeatStops"),0.f)==0) ReleasePresentation();}
    virtual bool RepeatInput() override
    {if(Param(TEXT("RepeatStops"),0.f)==0) return false;Finish();return true;}
    virtual void Cancel(EMCActiveSkillEndReason Reason) override
    {++Cancels;Super::Cancel(Reason);}
protected:
    virtual void OnFinished(EMCActiveSkillEndReason Reason) override {++Finishes;LastReason=Reason;}
};

UCLASS(Transient)
class UMCActiveSkillProtocolProbeEffect : public UMCActiveSkillPerkEffect
{
    GENERATED_BODY()
public:
    UMCActiveSkillProtocolProbeEffect() {ActionClass=AMCActiveSkillProtocolProbe::StaticClass();}
};
