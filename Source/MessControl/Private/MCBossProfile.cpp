#include "MCBossProfile.h"

void FMCBossAttackDefinition::Sanitize()
{
    auto Safe=[](float Value,float Default,float Min,float Max)
    { return FMath::IsFinite(Value)?FMath::Clamp(Value,Min,Max):Default; };
    MinimumPhase=FMath::Clamp(MinimumPhase,0,32);
    SelectionWeight=Safe(SelectionWeight,1.f,0.f,10000.f);
    Damage=Safe(Damage,20.f,0.f,10000.f);
    Range=Safe(Range,250.f,1.f,10000.f);
    VerticalReach=Safe(VerticalReach,160.f,1.f,10000.f);
    HalfAngleDegrees=Safe(HalfAngleDegrees,60.f,1.f,180.f);
    ClotCount=FMath::Clamp(ClotCount,1,64);
    ClotDamage=Safe(ClotDamage,4.f,0.f,10000.f);
    WindPushAcceleration=Safe(WindPushAcceleration,260.f,0.f,500.f);
    StartDelaySeconds=Safe(StartDelaySeconds,0.f,0.f,60.f);
    WindupSeconds=Safe(WindupSeconds,.8f,.05f,60.f);
    ActiveSeconds=Safe(ActiveSeconds,.15f,.05f,60.f);
    RecoverySeconds=Safe(RecoverySeconds,.7f,.05f,60.f);
    CooldownSeconds=Safe(CooldownSeconds,2.f,0.f,300.f);
}
