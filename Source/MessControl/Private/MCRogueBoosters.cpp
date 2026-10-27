#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "MCGameState.h"
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
#include "GameFramework/Controller.h"
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
bool UMCPerkComponent::IsTeamStageBoosterID(FName ID)
{
    return ID==TEXT("Fallback_Mouthwash") || ID==TEXT("Fallback_BrushFury") || ID==TEXT("Fallback_Grenade");
}

bool UMCPerkComponent::ActivatePersonalStageBooster(FName ID)
{
    auto* Hero=BoosterHero(this);
    const auto* Definition=FindDefinition(ID);
    const auto* Game=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    if(!HasAuthority() || !IsTeamStageBoosterID(ID) || !Definition || !Definition->bConsumableBooster
        || !Game || Game->bLobbyWaiting || Game->Phase==EMCShiftPhase::Lost || Game->Phase==EMCShiftPhase::Won
        || !IsValid(Hero) || Hero->IsActorBeingDestroyed() || !Hero->Status || !Hero->Status->IsAlive()
        || !Hero->ToothPhysics->CanAct() || Hero->HasFrozenLegs() || Hero->IsMimicCaptured() || Hero->SwallowedBy
        || !Hero->Inventory || Hero->Inventory->bChainsawRunning || IsFuryActive()) return false;
    if(ID==TEXT("Fallback_Mouthwash")) {
        // An area effect may touch nearby world objects, but originates only
        // from the player who chose it. It never arms or activates teammates.
        RogueBurst(false,{Hero});
    } else if(ID==TEXT("Fallback_Grenade")) bGrenadeArmed=true;
    else {
        Hero->CancelGameplayInput();
        FuryUntil=BoosterNow(GetWorld())+5;
        FuryDirection=Hero->GetActorForwardVector().GetSafeNormal2D();
        CastChecked<UMCToothMovementComponent>(Hero->GetCharacterMovement())->CancelDash();
    }
    GetOwner()->ForceNetUpdate();
    UE_LOG(LogTemp,Display,TEXT("MC_STAGE_PERSONAL_BOOSTER player=%s booster=%s"),*GetOwner()->GetName(),*ID.ToString());
    return true;
}

bool UMCPerkComponent::AcceptTimedStageBooster(FName ID)
{
    auto* Game=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    const auto* Definition=FindDefinition(ID);
    if(!HasAuthority() || !IsTeamStageBoosterID(ID) || !Definition || !Definition->bConsumableBooster
        || Definition->Polarity!=EMCPerkPolarity::Positive || !Game || Game->bLobbyWaiting
        || Game->Phase==EMCShiftPhase::Lost || Game->Phase==EMCShiftPhase::Won) return false;
    if(ActivatePersonalStageBooster(ID)) return true;
    // The reward belongs to PlayerState. Do not hold the whole stage hostage to
    // an absent/recovering pawn, or force an effect onto a frozen/ragdoll body.
    if(PendingStageGame.Get()!=Game || PendingStageLoadId!=Game->GameplayLoadId) PendingStageBoosters.Reset();
    PendingStageGame=Game;PendingStageLoadId=Game->GameplayLoadId;
    PendingStageBoosters.Add(ID);
    return true;
}

bool UMCPerkComponent::ActivateTeamStageBooster(FName ID)
{
    if(!HasAuthority() || !IsTeamStageBoosterID(ID)) return false;
    const auto* Definition=FindDefinition(ID);
    if(!Definition || !Definition->bConsumableBooster) return false;
    const auto* Game=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    if(!Game || Game->bLobbyWaiting || Game->Phase==EMCShiftPhase::Lost || Game->Phase==EMCShiftPhase::Won) return false;
    TArray<AMCToothCharacter*> Team;
    TArray<UMCPerkComponent*> Recipients;
    for(TActorIterator<AMCPlayerState> It(GetWorld());It;++It) {
        auto* Hero=Cast<AMCToothCharacter>(It->GetPawn());
        if(It->IsActorBeingDestroyed() || It->IsSpectator() || It->IsOnlyASpectator() || !It->Perks
            || !IsValid(Hero) || Hero->IsActorBeingDestroyed() || !Hero->GetController() || !Hero->Status
            || Hero->GetController()->IsActorBeingDestroyed() || Hero->GetPlayerState<AMCPlayerState>()!=*It
            || !Hero->Status->IsAlive()) continue;
        Team.Add(Hero); Recipients.Add(It->Perks);
    }
    if(Team.IsEmpty()) return false;
    // The mouthwash's shared area effects run once over the union of team
    // radii. Overlapping players do not multiply damage/cleaning/knockback.
    if(ID==TEXT("Fallback_Mouthwash")) RogueBurst(false,Team);
    else for(int32 I=0;I<Team.Num();++I) {
        auto* Perks=Recipients[I]; auto* Hero=Team[I];
        if(ID==TEXT("Fallback_Grenade")) Perks->bGrenadeArmed=true;
        else {
            Perks->FuryUntil=BoosterNow(GetWorld())+5;
            Perks->FuryDirection=Hero->GetActorForwardVector().GetSafeNormal2D();
            CastChecked<UMCToothMovementComponent>(Hero->GetCharacterMovement())->CancelDash();
        }
        Perks->GetOwner()->ForceNetUpdate();
    }
    UE_LOG(LogTemp,Display,TEXT("MC_STAGE_BOOSTER_APPLY booster=%s recipients=%d"),*ID.ToString(),Team.Num());
    return true;
}

void UMCPerkComponent::RogueBurst(bool Grenade,const TArray<AMCToothCharacter*>& TeamOrigins)
{
    auto* Hero=BoosterHero(this);if(!HasAuthority() || !Hero) return;
    const FVector Center=Hero->GetActorLocation();const float Radius=Grenade?450.f:400.f,Damage=Grenade?120.f:35.f;
    auto Visible=[&](AActor* Target) {
        auto From=[&](AMCToothCharacter* Origin) {
            if(FVector::DistSquared(Origin->GetActorLocation(),Target->GetActorLocation())>FMath::Square(Radius)) return false;
            FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCRogueBurst),false,Origin);Query.AddIgnoredActor(Target);
            return !GetWorld()->LineTraceSingleByChannel(Hit,Origin->GetActorLocation(),Target->GetActorLocation(),ECC_Visibility,Query);
        };
        if(TeamOrigins.IsEmpty()) return From(Hero);
        for(auto* Origin:TeamOrigins) if(IsValid(Origin) && From(Origin)) return true;
        return false;
    };
    TGuardValue<bool> Extra(bRogueExtraHit,true);
    for(TActorIterator<AMCNutEnemy> It(GetWorld());It;++It) if(Visible(*It)) It->ReceiveToolDamage(Damage,Hero);
    for(TActorIterator<AMCBossCharacter> It(GetWorld());It;++It) if(Visible(*It)) It->ReceiveBossDamage(Damage,Hero);
    for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if(Visible(*It) && !It->bBrushTool
        && (TeamOrigins.IsEmpty() || (!It->StackCarrier && It->Holders.IsEmpty() && It->Phase!=EMCFoodPhase::Carried)))
        It->HitFood(Damage,(It->GetActorLocation()-Center).GetSafeNormal2D(),Hero);
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(It->Status->IsAlive() && Visible(*It)) {
        const FVector Direction=(It->GetActorLocation()-Center).GetSafeNormal2D(UE_SMALL_NUMBER,Hero->GetActorForwardVector());
        if(Grenade) It->Status->Damage(60,Direction,GetOwner());
        else It->Status->ClearCoffeeForBooster();
        if(*It!=Hero && TeamOrigins.IsEmpty()) It->ToothPhysics->ApplyHit(Direction*(Grenade?500:650)+FVector(0,0,100),It->GetActorLocation());
    }
    if(!Grenade) {
        for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if(Visible(*It)) It->ClearLiquidForBooster();
        for(TActorIterator<AMCArenaTooth> It(GetWorld());It;++It) if(Visible(*It)) It->ClearGrimeForBooster();
    }
    if(TeamOrigins.IsEmpty()) AMCReactionVFX::Spawn(GetWorld(),Center,Grenade?EMCReactionEffect::Impact:EMCReactionEffect::WaterImpact,.65f,Radius);
    else for(auto* Origin:TeamOrigins) AMCReactionVFX::Spawn(GetWorld(),Origin->GetActorLocation(),EMCReactionEffect::WaterImpact,.65f,Radius);
}
void UMCPerkComponent::OnRogueKnockdown()
{
    if(!HasAuthority() || !bGrenadeArmed) return;
    bGrenadeArmed=false;GetOwner()->ForceNetUpdate();RogueBurst(true);
}
void UMCPerkComponent::TickRogueBoosters(float Dt)
{
    if(HasAuthority() && !PendingStageBoosters.IsEmpty()) {
        const auto* Game=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
        if(!Game || PendingStageGame.Get()!=Game || PendingStageLoadId!=Game->GameplayLoadId || Game->bLobbyWaiting
            || Game->Phase==EMCShiftPhase::Lost || Game->Phase==EMCShiftPhase::Won) {
            PendingStageBoosters.Reset();PendingStageGame.Reset();PendingStageLoadId.Invalidate();
        } else if(ActivatePersonalStageBooster(PendingStageBoosters[0])) {
            PendingStageBoosters.RemoveAt(0);
            if(PendingStageBoosters.IsEmpty()) {PendingStageGame.Reset();PendingStageLoadId.Invalidate();}
        }
    }
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
