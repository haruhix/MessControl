#include "MCPhysicsTypes.h"
void FMCPhysicsSettings::Sanitize()
{
    auto Safe=[](float V,float Min,float Max,float Default) { return FMath::IsFinite(V)?FMath::Clamp(V,Min,Max):Default; };
    Knockback=Safe(Knockback,150,1100,650); Lift=Safe(Lift,80,650,340); FallThreshold=Safe(FallThreshold,100,600,240);
    RagdollSeconds=Safe(RagdollSeconds,0.5f,6,2.2f); GetUpSeconds=Safe(GetUpSeconds,0.25f,2,0.75f);
    MuscleStrength=Safe(MuscleStrength,2,35,14); Damping=Safe(Damping,0.3f,2,0.9f); Mass=Safe(Mass,3,20,8);
}
