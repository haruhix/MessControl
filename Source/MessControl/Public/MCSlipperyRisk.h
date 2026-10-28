#pragma once
#include "CoreMinimal.h"

/** A decision is produced once per elapsed MOVING exposure window, never per frame. */
enum class EMCSlipperyDecision : uint8 { None, Roll, CarryFall };

/** Authority-only policy; caller supplies one random sample only when Roll is returned. */
struct FMCSlipperyRisk
{
    float Exposure=0.f;
    float Cooldown=0.f;
    float RetryDelay=0.f;
    bool bRepeat=false;

    void ResetExposure() { Exposure=0.f;bRepeat=false; }
    void TickCooldown(float Dt)
    {
        Dt=FMath::IsFinite(Dt)?FMath::Max(0.f,Dt):0.f;
        Cooldown=FMath::Max(0.f,Cooldown-Dt);
        RetryDelay=FMath::Max(0.f,RetryDelay-Dt);
    }
    EMCSlipperyDecision Update(float Dt,bool Allowed,bool Slippery,bool Moving,bool Carrying,
        float FirstSeconds=1.5f,float RepeatSeconds=3.f)
    {
        Dt=FMath::IsFinite(Dt)?FMath::Max(0.f,Dt):0.f;
        TickCooldown(Dt);
        if(Dt<=0.f) return EMCSlipperyDecision::None;
        if(!Allowed) { ResetExposure();return EMCSlipperyDecision::None; }
        if(!Slippery) {
            // A foot flickering across a wet edge cannot instantly erase all risk.
            Exposure=FMath::Max(0.f,Exposure-Dt*3.f);
            if(Exposure<=0.f) bRepeat=false;
            return EMCSlipperyDecision::None;
        }
        if(Cooldown>0.f || RetryDelay>0.f || !Moving) return EMCSlipperyDecision::None;
        if(Carrying) { ResetExposure();return EMCSlipperyDecision::CarryFall; }
        const float Authored=bRepeat?RepeatSeconds:FirstSeconds;
        const float Limit=FMath::IsFinite(Authored)?FMath::Clamp(Authored,.25f,30.f):(bRepeat?3.f:1.5f);
        Exposure+=Dt;
        if(Exposure+KINDA_SMALL_NUMBER<Limit) return EMCSlipperyDecision::None;
        // Spend this opportunity even on a successful recovery or blocked footprint.
        // Discard hitch overshoot: one long frame cannot generate several random rolls.
        Exposure=0.f;bRepeat=true;
        return EMCSlipperyDecision::Roll;
    }
    void OnFall(float Seconds=6.5f)
    {
        ResetExposure();Cooldown=FMath::IsFinite(Seconds)?FMath::Max(0.f,Seconds):6.5f;RetryDelay=0.f;
    }
    void OnBlockedCarry() { RetryDelay=.25f; }
};
