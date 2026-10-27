#pragma once
#include "CoreMinimal.h"

/** Cosmetic clip sample only; never contributes root motion or movement state. */
struct FMCGlideAnimationSample
{
    float Alpha=0.f;
    float Time=0.f;
    // Reserved for an actual fall transition; balance recovery never selects it.
    bool bStrong=false;
};

/** One authored balance reaction per surface/skid onset, with a quiet cooldown. */
struct FMCGlideAnimationState
{
    FMCGlideAnimationSample Sample;
    float Elapsed=-1.f,Duration=0.f,Cooldown=0.f;
    float ReleaseAge=-1.f,ReleaseAlpha=0.f;
    bool bWasSlippery=false,bWasSkidding=false,bWasMoving=false;

    void Update(float Dt,bool bAllowed,bool bSlippery,float Speed,float Skid,float Deceleration,
        float BalanceLength)
    {
        Dt=FMath::IsFinite(Dt)?FMath::Max(0.f,Dt):0.f;
        Cooldown=FMath::Max(0.f,Cooldown-Dt);
        const bool Skidding=Speed>120.f && (Skid>.38f || Deceleration>220.f);
        const bool Onset=(!bWasSlippery && bSlippery) || (!bWasSkidding && Skidding) || (!bWasMoving && Speed>120.f);
        bWasSlippery=bSlippery; bWasSkidding=Skidding; bWasMoving=Speed>120.f;
        // Hurt, frozen legs, death, traversal and hand contacts take over immediately.
        if(!bAllowed || BalanceLength<=KINDA_SMALL_NUMBER)
        {
            Elapsed=-1.f; ReleaseAge=-1.f; Sample={}; return;
        }
        if(Elapsed<0.f && Cooldown<=0.f && bSlippery && Speed>120.f && Onset)
        {
            // Glide1 is the complete attempt to recover balance. Glide2 is a
            // failed balance and fall, and requires a real fall transition.
            Sample.bStrong=false;
            Duration=BalanceLength;
            Elapsed=0.f; ReleaseAge=-1.f; Cooldown=Duration+.85f;
        }
        if(Elapsed<0.f) {Sample={};return;}
        Elapsed+=Dt;
        Sample.Time=FMath::Min(Elapsed,Duration);
        const float Envelope=FMath::SmoothStep(0.f,.12f,Elapsed)
            *(1-FMath::SmoothStep(FMath::Max(.12f,Duration-.24f),Duration,Elapsed));
        if((!bSlippery || Speed<70.f) && ReleaseAge<0.f) {ReleaseAge=0.f;ReleaseAlpha=Sample.Alpha;}
        if(ReleaseAge>=0.f) {ReleaseAge+=Dt;Sample.Alpha=FMath::Min(Envelope,ReleaseAlpha*(1-FMath::SmoothStep(0.f,.16f,ReleaseAge)));}
        else Sample.Alpha=Envelope;
        if(Elapsed>=Duration || (ReleaseAge>=.16f)) {Elapsed=-1.f;ReleaseAge=-1.f;Sample={};}
    }
};

/** Authority-only escalation: balance first, then a sustained severe skid. */
struct FMCSlipperyFallGate
{
    float BalanceAge=0.f,SevereAge=0.f,Cooldown=0.f;
    bool Update(float Dt,bool Allowed,bool Slippery,float Speed,float Skid,float Deceleration)
    {
        Dt=FMath::IsFinite(Dt)?FMath::Max(0.f,Dt):0.f;
        Cooldown=FMath::Max(0.f,Cooldown-Dt);
        if(!Allowed || !Slippery || Speed<=120.f) {BalanceAge=SevereAge=0.f;return false;}
        BalanceAge+=Dt;
        const bool Severe=Speed>=260.f && (Skid>.6f || Deceleration>450.f);
        SevereAge=Severe?SevereAge+Dt:FMath::Max(0.f,SevereAge-Dt*2.f);
        if(Cooldown>0.f || BalanceAge<.55f || SevereAge<.30f) return false;
        BalanceAge=SevereAge=0.f;Cooldown=6.5f;return true;
    }
};
