#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothMovementComponent.h"
#include "MCInventoryComponent.h"
#include "MCFoodActor.h"
#include "MCNutEnemy.h"
#include "MCBossCharacter.h"
#include "MCMouthSurface.h"
#include "MCArenaTooth.h"
#include "MCReactionVFX.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/GameStateBase.h"
#include "EngineUtils.h"

namespace
{
double BoosterNow(const UWorld* World) {return World->GetGameState()?World->GetGameState()->GetServerWorldTimeSeconds():World->GetTimeSeconds();}
AMCToothCharacter* BoosterHero(const UMCPerkComponent* Perks) {const auto* State=Cast<AMCPlayerState>(Perks->GetOwner());return State?Cast<AMCToothCharacter>(State->GetPawn()):nullptr;}
}
void UMCPerkComponent::AddBoosterCharge(FName ID)
{if(HasAuthority()) {BoosterQueue.Add(ID);GetOwner()->ForceNetUpdate();}}
bool UMCPerkComponent::IsFuryActive() const {return GetWorld() && BoosterNow(GetWorld())<FuryUntil;}
bool UMCPerkComponent::ActivateBooster()
{
    auto* Hero=BoosterHero(this);
    if(!HasAuthority() || !Hero || !Hero->CanWork() || Hero->HasFrozenLegs() || Hero->Inventory->bChainsawRunning || IsFuryActive()) return false;
    int32 Index=INDEX_NONE;
    for(int32 I=0;I<BoosterQueue.Num();++I) if(BoosterQueue[I]!=TEXT("Fallback_Grenade") || !bGrenadeArmed) {Index=I;break;}
    if(Index==INDEX_NONE) return false;
    const FName ID=BoosterQueue[Index];BoosterQueue.RemoveAt(Index);
    if(ID==TEXT("Fallback_Grenade")) bGrenadeArmed=true;
    else if(ID==TEXT("Fallback_BrushFury")) {
        Hero->CancelGameplayInput();
        FuryUntil=BoosterNow(GetWorld())+5;FuryDirection=Hero->GetActorForwardVector().GetSafeNormal2D();
        CastChecked<UMCToothMovementComponent>(Hero->GetCharacterMovement())->CancelDash();
    } else RogueBurst(false);
    GetOwner()->ForceNetUpdate();return true;
}
void UMCPerkComponent::RogueBurst(bool Grenade)
{
    auto* Hero=BoosterHero(this);if(!HasAuthority() || !Hero) return;
    const FVector Center=Hero->GetActorLocation();const float Radius=Grenade?450.f:400.f,Damage=Grenade?120.f:35.f;
    auto Visible=[&](AActor* Target) {
        if(FVector::DistSquared(Center,Target->GetActorLocation())>FMath::Square(Radius)) return false;
        FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCRogueBurst),false,Hero);Query.AddIgnoredActor(Target);
        return !GetWorld()->LineTraceSingleByChannel(Hit,Center,Target->GetActorLocation(),ECC_Visibility,Query);
    };
    TGuardValue<bool> Extra(bRogueExtraHit,true);
    for(TActorIterator<AMCNutEnemy> It(GetWorld());It;++It) if(Visible(*It)) It->ReceiveToolDamage(Damage,Hero);
    for(TActorIterator<AMCBossCharacter> It(GetWorld());It;++It) if(Visible(*It)) It->ReceiveBossDamage(Damage,Hero);
    for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if(Visible(*It) && !It->bBrushTool)
        It->HitFood(Damage,(It->GetActorLocation()-Center).GetSafeNormal2D(),Hero);
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(It->Status->IsAlive() && Visible(*It)) {
        const FVector Direction=(It->GetActorLocation()-Center).GetSafeNormal2D(UE_SMALL_NUMBER,Hero->GetActorForwardVector());
        if(Grenade) It->Status->Damage(60,Direction,GetOwner());
        else It->Status->ClearCoffeeForBooster();
        if(*It!=Hero) It->ToothPhysics->ApplyHit(Direction*(Grenade?500:650)+FVector(0,0,100),It->GetActorLocation());
    }
    if(!Grenade) {
        for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if(Visible(*It)) It->ClearLiquidForBooster();
        for(TActorIterator<AMCArenaTooth> It(GetWorld());It;++It) if(Visible(*It)) It->ClearGrimeForBooster();
    }
    AMCReactionVFX::Spawn(GetWorld(),Center,Grenade?EMCReactionEffect::Impact:EMCReactionEffect::WaterImpact,.65f,Radius);
}
void UMCPerkComponent::OnRogueKnockdown()
{
    if(!HasAuthority() || !bGrenadeArmed) return;
    bGrenadeArmed=false;GetOwner()->ForceNetUpdate();RogueBurst(true);
}
void UMCPerkComponent::TickRogueBoosters(float Dt)
{
    auto* Hero=BoosterHero(this);
    if(GetNetMode()!=NM_DedicatedServer && Hero && Hero->Status->IsAlive() && bGrenadeArmed) {
        if(!GrenadeCue.IsValid() || GrenadeCue->GetOwner()!=Hero) {
            if(GrenadeCue.IsValid()) GrenadeCue->DestroyComponent();
            auto* Cue=NewObject<UTextRenderComponent>(Hero);Cue->SetupAttachment(Hero->GetRootComponent());
            Cue->SetRelativeLocation(FVector(0,0,115));Cue->SetText(FText::FromString(TEXT("ГРАНАТА")));
            Cue->SetWorldSize(15);Cue->SetTextRenderColor(FColor(255,70,70));Cue->SetHorizontalAlignment(EHTA_Center);
            Hero->AddInstanceComponent(Cue);Cue->RegisterComponent();GrenadeCue=Cue;
        }
    } else if(GrenadeCue.IsValid()) {GrenadeCue->DestroyComponent();GrenadeCue.Reset();}
    if(!Hero) return;
    auto* Move=Hero->GetCharacterMovement();
    const bool Running=IsFuryActive() && Hero->Status->IsAlive() && Hero->ToothPhysics->CanAct() && !Hero->HasFrozenLegs() && !Hero->IsMimicCaptured();
    if(Hero->HasAuthority() || Hero->IsLocallyControlled()) {
        if(Running) {
            const FVector Steer=Move->GetCurrentAcceleration().GetSafeNormal2D();
            FuryDirection=(FuryDirection+Steer*FMath::Min(Dt,.1f)*.4f).GetSafeNormal2D();
            auto Current=bFuryMotion?Move->GetRootMotionSourceByID(FuryMotionID):nullptr;
            if(Current.IsValid()) static_cast<FMCLocomotionRootMotionSource*>(Current.Get())->Force=FuryDirection*700;
            else {
                auto Motion=MakeShared<FMCLocomotionRootMotionSource>();Motion->InstanceName=TEXT("MCRogueFury");Motion->Priority=140;
                Motion->AccumulateMode=ERootMotionAccumulateMode::Override;Motion->Duration=-1;Motion->Force=FuryDirection*700;
                Motion->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
                Motion->FinishVelocityParams.Mode=ERootMotionFinishVelocityMode::SetVelocity;Motion->FinishVelocityParams.SetVelocity=FVector::ZeroVector;
                FuryMotionID=Move->ApplyRootMotionSource(Motion);bFuryMotion=true;
            }
        } else if(bFuryMotion) {Move->RemoveRootMotionSourceByID(FuryMotionID);bFuryMotion=false;}
    }
    if(!HasAuthority() || !Running || BoosterNow(GetWorld())<NextFuryHitAt) return;
    NextFuryHitAt=BoosterNow(GetWorld())+.2;
    const FVector Center=Hero->GetActorLocation();
    auto Close=[&](AActor* Actor) {
        if(FVector::DistSquared(Center,Actor->GetActorLocation())>FMath::Square(180.f)) return false;
        FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(MCRogueFuryContact),false,Hero);Q.AddIgnoredActor(Actor);
        return !GetWorld()->LineTraceSingleByChannel(Hit,Center,Actor->GetActorLocation(),ECC_Visibility,Q);
    };
    for(TActorIterator<AMCNutEnemy> It(GetWorld());It;++It) if(Close(*It)) It->ReceiveToolDamage(Hero->Inventory->Damage(),Hero);
    for(TActorIterator<AMCBossCharacter> It(GetWorld());It;++It) if(Close(*It)) It->ReceiveBossDamage(Hero->Inventory->Damage(),Hero);
    for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if(Close(*It) && Hero->Inventory->CanBreak(*It)) It->HitFood(Hero->Inventory->Damage(),FuryDirection,Hero);
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(*It!=Hero && It->Status->IsAlive() && Close(*It))
        It->ToothPhysics->ApplyHit((It->GetActorLocation()-Center).GetSafeNormal2D()*500+FVector(0,0,100),It->GetActorLocation());
}
