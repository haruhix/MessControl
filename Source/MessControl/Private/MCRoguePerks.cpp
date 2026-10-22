#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "MCGameState.h"
#include "MCGameDirector.h"
#include "MCSingleDayDirector.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCFoodCollectionComponent.h"
#include "MCInventoryComponent.h"
#include "MCNutEnemy.h"
#include "MCNutBoss.h"
#include "MCFoodActor.h"
#include "MCIceEvent.h"
#include "MCFogBrawlEvent.h"
#include "MCArenaTooth.h"
#include "MCRogueFoamPatch.h"
#include "Components/PointLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
    double RogueNow(const UWorld* World)
    { return World?(World->GetGameState()?World->GetGameState()->GetServerWorldTimeSeconds():World->GetTimeSeconds()):0; }
    AMCToothCharacter* HeroFor(const UMCPerkComponent* Perks)
    { const auto* PS=Perks?Cast<AMCPlayerState>(Perks->GetOwner()):nullptr;return PS?Cast<AMCToothCharacter>(PS->GetPawn()):nullptr; }
    FVector2D StatLimits(EMCRogueStat Stat)
    {
        switch(Stat) {
        case EMCRogueStat::NormalSpeed:case EMCRogueStat::JumpHeight:return { .7,1.4 };
        case EMCRogueStat::MaxStamina:case EMCRogueStat::DashCooldown:case EMCRogueStat::RagdollDuration:return { .7,1.6 };
        case EMCRogueStat::WeaponDamage:case EMCRogueStat::PickaxeDamage:case EMCRogueStat::ChainsawDamage:return { .6,2.0 };
        case EMCRogueStat::PickaxeReach:return { .8,1.3 };
        case EMCRogueStat::MaxHealth:return { .5,2.0 };
        case EMCRogueStat::FreezeRate:return { .65,1.6 };
        case EMCRogueStat::BrushPower:return { .6,1.8 };
        case EMCRogueStat::CarryPenalty:return { .5,1.6 };
        case EMCRogueStat::SpoilRate:return { .5,1.8 };
        case EMCRogueStat::ThrowImpulse:return { .6,1.4 };
        case EMCRogueStat::FoodScatter:return { .4,1.8 };
        case EMCRogueStat::DeliveryXP:return { 1,1.2 };
        case EMCRogueStat::ShieldDamage:return { 1,1.6 };
        case EMCRogueStat::FallingNutDamage:case EMCRogueStat::BraceDamage:case EMCRogueStat::IncomingDamage:return { .6,1.5 };
        case EMCRogueStat::WaveResistance:return { .6,1.0 };
        default:return { .6,1.6 };
        }
    }
}

UMCPerkComponent* UMCPerkComponent::ForActor(const AActor* Actor)
{
    if(const auto* PS=Cast<AMCPlayerState>(Actor)) return PS->Perks;
    const auto* Hero=Cast<AMCToothCharacter>(Actor);
    if(!Hero && Actor) Hero=Cast<AMCToothCharacter>(Actor->GetInstigator());
    const auto* PS=Hero?Hero->GetPlayerState<AMCPlayerState>():nullptr;
    return PS?PS->Perks.Get():nullptr;
}
bool UMCPerkComponent::IsNightActive() const
{ const auto* GS=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;return GS && GS->bRogueNightActive; }
bool UMCPerkComponent::HasNearbyAlly(float Radius) const
{
    const auto* Hero=HeroFor(this);const auto* GS=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    if(!Hero || !GS) return false;
    for(const auto& State:GS->PlayerArray) if(State!=GetOwner())
        if(const auto* Other=Cast<AMCToothCharacter>(State->GetPawn());Other && Other->Status->IsAlive()
            && FVector::DistSquared(Other->GetActorLocation(),Hero->GetActorLocation())<=FMath::Square(Radius)) return true;
    return false;
}
bool UMCPerkComponent::HasRecentAllyHit(AActor* Target) const
{
    if(!IsValid(Target) || Cast<AMCFoodActor>(Target)) return false;
    const auto* GS=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    if(!GS) return false;
    for(const auto& State:GS->PlayerArray) if(State!=GetOwner())
        if(const auto* Other=Cast<AMCPlayerState>(State);Other && Other->Perks)
            if(const double* At=Other->Perks->RecentWeaponHits.Find(Target);At && RogueNow(GetWorld())-*At<=2) return true;
    return false;
}
bool UMCPerkComponent::ActivationMatches(EMCRogueActivation Condition,AActor* Target) const
{
    const auto* Hero=HeroFor(this);if(!Hero) return Condition==EMCRogueActivation::Always;
    switch(Condition) {
    case EMCRogueActivation::Always:return true;
    case EMCRogueActivation::CarryingFood:return Hero->HeldFood || (Hero->FoodCollection && !Hero->FoodCollection->Pieces.IsEmpty());
    case EMCRogueActivation::HighFreeze:
        for(TActorIterator<AMCIceEvent> It(GetWorld());It;++It) if(It->IsActive() && It->FreezeAmount(Hero)>=.5f) return true;
        return false;
    case EMCRogueActivation::Night:return IsNightActive();
    case EMCRogueActivation::RecentAllyHit:return HasRecentAllyHit(Target);
    case EMCRogueActivation::NoRecentAllyHit:return IsValid(Target) && !Cast<AMCFoodActor>(Target) && !HasRecentAllyHit(Target);
    case EMCRogueActivation::CounterReady:return RogueNow(GetWorld())<CounterReadyUntil;
    case EMCRogueActivation::TeamStrikeReady:return RogueNow(GetWorld())>=NextTeamStrikeAt && HasRecentAllyHit(Target);
    case EMCRogueActivation::WatergunFiring:return Hero->Inventory && Hero->Inventory->bPressureMode && Hero->Inventory->IsUsingWatergun();
    case EMCRogueActivation::NearbyAlly:return HasNearbyAlly(200);
    case EMCRogueActivation::PickaxeSelected:return Hero->Inventory && Hero->Inventory->Selected==EMCToolSlot::Pickaxe;
    case EMCRogueActivation::WatergunSelected:return Hero->Inventory && Hero->Inventory->Selected==EMCToolSlot::Spray;
    }
    return false;
}
float UMCPerkComponent::RogueValue(EMCRogueStat Stat,float Base,AActor* Target,FName ExtraStack) const
{
    if(!FMath::IsFinite(Base)) return 0;
    float Flat=0,Percent=0;
    auto Add=[&](FName ID,int32 Stacks) {
        if(const auto* Row=FindDefinition(ID);Row && Row->bRoguePerk && Stacks>0)
            for(const auto& M:Row->Modifiers) if(M.Stat==Stat && FMath::IsFinite(M.Value) && ActivationMatches(M.Condition,Target))
                (M.bFlat?Flat:Percent)+=M.Value*Stacks;
    };
    bool ExtraSeen=false;
    for(const auto& Active:ActivePerks) {Add(Active.PerkID,Active.Stacks+(Active.PerkID==ExtraStack?1:0));ExtraSeen|=Active.PerkID==ExtraStack;}
    if(!ExtraStack.IsNone() && !ExtraSeen) Add(ExtraStack,1);
    if(Stat==EMCRogueStat::NormalSpeed) {
        if(GetStacks(TEXT("P17"))>0 && RogueNow(GetWorld())<CleanSpeedUntil) Percent+=.1f;
        if(GetStacks(TEXT("N24"))>0 && RogueNow(GetWorld())<DeliverySlowUntil) Percent-=.1f;
    }
    const float Result=(Base+Flat)*(1+Percent);
    if(Stat==EMCRogueStat::CarryCapacity) return FMath::Clamp(Result,1.f,10.f);
    if(Stat==EMCRogueStat::WarmDuration) return FMath::Clamp(Result,1.f,FMath::Max(6.f,Base+6.f));
    if(Stat==EMCRogueStat::ChainsawCrashDamage) return FMath::Max(10.f,Result);
    const auto Limits=StatLimits(Stat);
    return FMath::Clamp(Result,Base*float(Limits.X),Base*float(Limits.Y));
}
float UMCPerkComponent::WeaponDamage(float Base,AActor* Target) const
{
    if(bRogueExtraHit) return Base;
    float Bonus=RogueValue(EMCRogueStat::WeaponDamage,1,Target)-1;
    if(const auto* Hero=HeroFor(this);Hero && Hero->Inventory) {
        if(Hero->Inventory->Selected==EMCToolSlot::Pickaxe) Bonus+=RogueValue(EMCRogueStat::PickaxeDamage,1,Target)-1;
        if(Hero->Inventory->Selected==EMCToolSlot::Knife && Hero->Inventory->HasUpgrade(EMCToolUpgrade::Chainsaw)) Bonus+=RogueValue(EMCRogueStat::ChainsawDamage,1,Target)-1;
    }
    return Base*FMath::Clamp(1+Bonus,.6f,2.f);
}
float UMCPerkComponent::IncomingDamage(float Base,EMCRogueDamageKind Kind) const
{
    if(Kind==EMCRogueDamageKind::Chainsaw) return Base;
    float Bonus=RogueValue(EMCRogueStat::IncomingDamage)-1;
    if(Kind==EMCRogueDamageKind::FallingNut) Bonus+=RogueValue(EMCRogueStat::FallingNutDamage)-1;
    if(Kind==EMCRogueDamageKind::Brace) Bonus+=RogueValue(EMCRogueStat::BraceDamage)-1;
    return Base*FMath::Clamp(1+Bonus,.6f,1.5f);
}
void UMCPerkComponent::NotifyWeaponHit(AActor* Target)
{
    if(!HasAuthority() || bRogueExtraHit || !IsValid(Target)) return;
    const bool Team=ActivationMatches(EMCRogueActivation::TeamStrikeReady,Target);
    RecentWeaponHits.Add(Target,RogueNow(GetWorld()));CounterReadyUntil=-100;
    if(Team && GetStacks(TEXT("P35"))>0) NextTeamStrikeAt=RogueNow(GetWorld())+3;
    GetOwner()->ForceNetUpdate();
}
void UMCPerkComponent::NotifyDodgedAttack()
{
    if(!HasAuthority() || GetStacks(TEXT("P09"))<=0 || RogueNow(GetWorld())<NextCounterAt) return;
    const auto* Hero=HeroFor(this);if(!Hero || !Hero->IsDashing()) return;
    CounterReadyUntil=RogueNow(GetWorld())+2;NextCounterAt=RogueNow(GetWorld())+6;GetOwner()->ForceNetUpdate();
}
void UMCPerkComponent::NotifyCleaned(FVector Point)
{
    if(!HasAuthority()) return;
    auto* Hero=HeroFor(this);if(!Hero || !Hero->Status->IsAlive()) return;
    const double Time=RogueNow(GetWorld());
    if(GetStacks(TEXT("P17"))>0) CleanSpeedUntil=Time+3;
    if(GetStacks(TEXT("P30"))>0 && Time>=NextCleanHealAt) {Hero->Status->Heal(3);NextCleanHealAt=Time+15;}
    if(GetStacks(TEXT("P15"))>0 && Time>=NextFoamAt) {
        FActorSpawnParameters Params;Params.Owner=GetOwner();
        const auto* Move=Hero->GetCharacterMovement();
        if(Move->IsMovingOnGround() && Move->CurrentFloor.IsWalkableFloor()) {
            const auto& Floor=Move->CurrentFloor.HitResult;
            if(auto* Patch=GetWorld()->SpawnActor<AMCRogueFoamPatch>(Floor.ImpactPoint+Floor.ImpactNormal*4,FRotationMatrix::MakeFromZ(Floor.ImpactNormal).Rotator(),Params)) RogueActors.Add(Patch);
            NextFoamAt=Time+3;
        }
    }
    GetOwner()->ForceNetUpdate();
}
void UMCPerkComponent::NotifyFoodDelivered()
{
    if(HasAuthority() && GetStacks(TEXT("N24"))>0) {DeliverySlowUntil=RogueNow(GetWorld())+3;GetOwner()->ForceNetUpdate();}
}
int32 UMCPerkComponent::FoodExperienceBonus(int32 Base)
{
    return 0; // Saved perk definitions cannot re-enable the retired XP economy.
}

void UMCPerkComponent::TryWaterRicochet(AActor* First,float BaseDamage,FVector HitPoint)
{
    if(!HasAuthority() || bRogueExtraHit || GetStacks(TEXT("P12"))<=0 || RogueNow(GetWorld())<NextRicochetAt) return;
    auto* Hero=HeroFor(this);if(!Hero) return;
    AMCNutEnemy* Best=nullptr;double Distance=FMath::Square(200.f);
    for(TActorIterator<AMCNutEnemy> It(GetWorld());It;++It) {
        if(*It==First || Cast<AMCNutBoss>(*It) || !It->CanReceiveToolHit()) continue;
        const double D=FVector::DistSquared(HitPoint,It->GetActorLocation());if(D>=Distance) continue;
        FHitResult Block;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCRogueRicochet),false,Hero);Query.AddIgnoredActor(First);Query.AddIgnoredActor(*It);
        if(GetWorld()->LineTraceSingleByChannel(Block,HitPoint,It->GetToolTargetPoint(HitPoint),ECC_Visibility,Query)) continue;
        Best=*It;Distance=D;
    }
    if(!Best) return;
    NextRicochetAt=RogueNow(GetWorld())+.5;
    TGuardValue<bool> Extra(bRogueExtraHit,true);Best->ReceiveToolDamage(BaseDamage*.3f,Hero);
}
void UMCPerkComponent::RefreshRogueTuning()
{
    auto* Hero=HeroFor(this);if(!Hero || !Hero->Status || !Hero->Status->IsAlive()) return;
    TunedHero=Hero;
    if(HasAuthority()) Hero->Status->SetPerkMaxHealth(RogueValue(EMCRogueStat::MaxHealth,Hero->Status->GetBaseMaxHealth()));
}
void UMCPerkComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Dt,Type,Tick);RefreshRogueTuning();
    TickRogueBoosters(Dt);
    auto* Hero=HeroFor(this);
    if(GetNetMode()!=NM_DedicatedServer && GetStacks(TEXT("P33"))>0 && IsNightActive() && Hero && Hero->Status->IsAlive()) {
        if(!NightLight.IsValid() || NightLight->GetOwner()!=Hero) {
            if(NightLight.IsValid()) NightLight->DestroyComponent();
            auto* Light=NewObject<UPointLightComponent>(Hero);Light->SetupAttachment(Hero->GetRootComponent());
            Light->SetIntensity(1400);Light->SetAttenuationRadius(250);Light->SetLightColor(FLinearColor(1,.9f,.65f));Light->SetCastShadows(false);
            Hero->AddInstanceComponent(Light);Light->RegisterComponent();NightLight=Light;
        }
    } else if(NightLight.IsValid()) {NightLight->DestroyComponent();NightLight.Reset();}
    if(HasAuthority()) {
        for(auto It=RecentWeaponHits.CreateIterator();It;++It) if(!It.Key().IsValid() || RogueNow(GetWorld())-It.Value()>3) It.RemoveCurrent();
        RogueActors.RemoveAll([](const auto& Actor){return !Actor.IsValid();});
    }
}
void UMCPerkComponent::ClearRogueRuntime()
{
    if(GrenadeCue.IsValid()) GrenadeCue->DestroyComponent();GrenadeCue.Reset();
    if(auto* Hero=HeroFor(this);Hero && bFuryMotion) Hero->GetCharacterMovement()->RemoveRootMotionSourceByID(FuryMotionID);
    bFuryMotion=false;FuryUntil=-100;bGrenadeArmed=false;BoosterQueue.Reset();
    if(NightLight.IsValid()) NightLight->DestroyComponent();NightLight.Reset();
    for(const auto& Actor:RogueActors) if(Actor.IsValid()) Actor->Destroy();RogueActors.Reset();RecentWeaponHits.Reset();
    CounterReadyUntil=CleanSpeedUntil=DeliverySlowUntil=-100;
    NextCounterAt=NextTeamStrikeAt=NextCleanHealAt=NextFoamAt=NextRicochetAt=-100;
    DeliveryXPResidual=0;
}
bool UMCPerkComponent::GetFogNavigation(FVector& Point) const
{
    if(GetStacks(TEXT("P34"))<=0 || FMath::Fmod(RogueNow(GetWorld()),6.)>=1) return false;
    for(TActorIterator<AMCFogBrawlEvent> It(GetWorld());It;++It) if(It->IsActive() && IsValid(It->ActiveTarget)) {Point=It->ActiveTarget->GetActorLocation();return true;}
    return false;
}
