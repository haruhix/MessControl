#include "MCKatanaEnemyContact.h"

#include "MCBossCharacter.h"
#include "MCCombatPassiveSubsystem.h"
#include "MCNutBoss.h"
#include "MCNutEnemy.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"

namespace MCKatanaEnemyContact
{
UPrimitiveComponent* BodyFor(const AActor& Target)
{
    UPrimitiveComponent* Body=nullptr;
    if(const auto* Nut=Cast<AMCNutEnemy>(&Target); Nut && Nut->CanReceiveToolHit())
        Body=Nut->Body.Get();
    else if(const auto* Boss=Cast<AMCBossCharacter>(&Target); Boss && Boss->CanReceiveWeaponHit())
        Body=Boss->BodyHitbox.Get();
    else if(const auto* Tooth=Cast<AMCToothCharacter>(&Target);
        Tooth && Tooth->Status && Tooth->Status->IsAlive() && Tooth->ToothPhysics
        && Tooth->ToothPhysics->GetBodyState()!=EMCBodyState::Recovering
        && !Tooth->SwallowedBy && !Tooth->IsMimicCaptured())
    {
        if(Tooth->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll) Body=Tooth->GetMesh();
        else Body=Tooth->GetCapsuleComponent();
    }
    return Body && Body->IsQueryCollisionEnabled()?Body:nullptr;
}

bool Apply(AMCToothCharacter& Hero,AActor& Target,const FVector& ImpactPoint,float Damage)
{
    if(!Hero.HasAuthority() || &Hero==&Target || Hero.GetWorld()!=Target.GetWorld()
        || !Hero.CanWork() || !BodyFor(Target) || ImpactPoint.ContainsNaN()
        || !FMath::IsFinite(Damage) || Damage<=0.f) return false;

    if(auto* Nut=Cast<AMCNutEnemy>(&Target))
    {
        const auto* Knight=Cast<AMCNutBoss>(Nut);
        const float ShieldBefore=Knight?Knight->ShieldHealth:0.f;
        const float Applied=Nut->ReceiveToolDamage(Damage,&Hero);
        return Applied>0.f || (Knight && Knight->ShieldHealth<ShieldBefore);
    }
    if(auto* Boss=Cast<AMCBossCharacter>(&Target))
        return Boss->ReceiveBossDamage(Damage,&Hero)>0.f;

    // Ordinary tool hits on another tooth are knockback contacts, not direct
    // HP damage. Keep those rules while using the blade's point for the impulse.
    auto* Tooth=CastChecked<AMCToothCharacter>(&Target);
    const auto& Tuning=Hero.ToothPhysics->Settings;
    const FVector Direction=(Tooth->GetActorLocation()-Hero.GetActorLocation())
        .GetSafeNormal2D(UE_SMALL_NUMBER,Hero.GetActorForwardVector());
    Tooth->ToothPhysics->ApplyHit(Direction*Tuning.Knockback+FVector(0,0,Tuning.Lift),ImpactPoint);
    UMCCombatPassiveSubsystem::NotifyPhysicalHit(&Hero,Tooth,Damage);
    return true;
}
}
