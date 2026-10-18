#include "MCToothStatusComponent.h"
#include "MCPerkComponent.h"
#include "MCArenaTooth.h"
#include "MCToothCharacter.h"
#include "Components/CapsuleComponent.h"
#include "MCFoodCollectionComponent.h"
#include "MCCombatFeedbackComponent.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCTutorialDirector.h"
#include "MCMouthSurface.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

void FMCToothCareSettings::Sanitize()
{
    auto Safe=[](float V,float D,float L,float H){return FMath::IsFinite(V)?FMath::Clamp(V,L,H):D;};
    ContactSeconds=Safe(ContactSeconds,.5f,.1f,10); Reach=Safe(Reach,115,10,250);
    CoffeeContacts=FMath::Clamp(CoffeeContacts,1,100); RepairContacts=FMath::Clamp(RepairContacts,1,100);
    HealPerContact=Safe(HealPerContact,25,1,10000); PlayerHealth=Safe(PlayerHealth,100,1,10000);
    LooseHealthFraction=Safe(LooseHealthFraction,.4f,0,1); RespawnSeconds=Safe(RespawnSeconds,3,.1f,60);
}
UMCToothStatusComponent::UMCToothStatusComponent()
{
    SetIsReplicatedByDefault(true);
    Profile=TSoftObjectPtr<UMCToothCareProfile>(FSoftObjectPath(TEXT("/Game/Data/DA_ToothCare.DA_ToothCare")));
}
void UMCToothStatusComponent::BeginPlay()
{
    Super::BeginPlay();
    if (GetOwner()->HasAuthority())
    {
        if (const auto* Data=Profile.LoadSynchronous()) Settings=Data->Settings;
        Settings.Sanitize();
        if (!bInitialized) Initialize(Settings.PlayerHealth);
    }
}
void UMCToothStatusComponent::Initialize(float MaxHealth)
{
    if (!GetOwner()->HasAuthority()) return;
    State=FMCToothStatus(); State.MaxHealth=FMath::IsFinite(MaxHealth)?FMath::Max(1.f,MaxHealth):100;
    BaseMaxHealth=State.MaxHealth;State.Health=State.MaxHealth; State.CoffeeTotal=Settings.CoffeeContacts; bInitialized=true;
    OnRep_State(); GetOwner()->ForceNetUpdate();
}
void UMCToothStatusComponent::Restore(const FMCToothStatus& Source)
{
    if (!GetOwner()->HasAuthority()) return;
    State.Health=FMath::Clamp(Source.Health/FMath::Max(1.f,Source.MaxHealth)*State.MaxHealth,1.f,State.MaxHealth);
    State.CoffeeLeft=Source.CoffeeLeft; State.CoffeeTotal=Source.CoffeeTotal; State.RepairLeft=Source.RepairLeft;
    Changed(true);
}
void UMCToothStatusComponent::Changed(bool bCare)
{
    const auto* GS=GetWorld()->GetGameState();
    State.ReactionAt=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds(); State.bCareReaction=bCare;
    OnRep_State(); GetOwner()->ForceNetUpdate();
}
void UMCToothStatusComponent::OnRep_State()
{
    if (auto* Arena=Cast<AMCArenaTooth>(GetOwner())) Arena->StatusChanged();
    if (auto* Hero=Cast<AMCToothCharacter>(GetOwner())) Hero->StatusChanged();
}
bool UMCToothStatusComponent::NeedsCare(bool bBrush) const
{
    if(const auto* Hero=Cast<AMCToothCharacter>(GetOwner());Hero && Hero->IsFreezingToDeath()) return false;
    return IsAlive() && (bBrush?State.CoffeeLeft>0:State.Health<State.MaxHealth || State.RepairLeft>0);
}
void UMCToothStatusComponent::ApplyCoffee(float Amount)
{
    if (!GetOwner()->HasAuthority() || !IsAlive() || !FMath::IsFinite(Amount)) return;
    State.CoffeeTotal=Settings.CoffeeContacts;
    if (auto* Surface=Cast<AMCMouthSurface>(GetOwner())) Surface->ResetLiquid();
    State.CoffeeLeft=FMath::CeilToInt(FMath::Clamp(Amount,0.f,1.f)*State.CoffeeTotal); Changed(true);
    if (auto* Arena=Cast<AMCArenaTooth>(GetOwner())) Arena->ResetGrime();
}
void UMCToothStatusComponent::Loosen()
{
    if (!GetOwner()->HasAuthority() || !IsAlive()) return;
    State.RepairLeft=Settings.RepairContacts; Changed(false);
}
bool UMCToothStatusComponent::Damage(float Amount,FVector Direction,AActor* Source,EMCRogueDamageKind Kind,bool bScaleWithPerks)
{
    if(const auto* Hero=Cast<AMCToothCharacter>(GetOwner());Hero && Hero->IsFreezingToDeath()
        && Hero->FreezeDeathAge()<AMCToothCharacter::FreezeDeathHoldSeconds) return false;
    if (const auto* Game=GetWorld()->GetGameState<AMCGameState>(); Game && (Game->bTutorialActive || Game->bLobbyWaiting)) return false;
    if (!GetOwner()->HasAuthority() || !IsAlive() || !FMath::IsFinite(Amount) || Amount<=0 || Direction.ContainsNaN()
        || (IsValid(Source) && Source->GetWorld()!=GetWorld())) return false;
    if (const auto* Hero=Cast<AMCToothCharacter>(GetOwner()); Hero && Hero->MimicCaptor) return false;
    // Native tools attribute hits to the wielder. Chili attributes damage to
    // its hazard actor, so its deliberate all-target exception still applies.
    if (const auto* Hero=Cast<AMCToothCharacter>(GetOwner()); Hero && Cast<AMCToothCharacter>(Source)) return false;
    if(bScaleWithPerks) if(const auto* Perks=UMCPerkComponent::ForActor(GetOwner())) Amount=Perks->IncomingDamage(Amount,Kind);
    const float HealthBefore=State.Health;
    LastDamageDirection=Direction.GetSafeNormal(); State.Health=FMath::Max(0.f,State.Health-Amount);
    const auto* GS=GetWorld()->GetGameState();
    State.DamageAt=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    if(auto* Hero=Cast<AMCToothCharacter>(GetOwner())) {
        Hero->FoodCollection->Spill(LastDamageDirection*180+FVector(0,0,50));
        if(!Hero->IsFreezingToDeath()) {
            const bool Legacy=Hero->ActorHasTag(TEXT("MC.LegacyCombatFeedback"));
            const float ContactRadius=Legacy?30.f:Hero->GetCapsuleComponent()->GetScaledCapsuleRadius()+8.f;
            Hero->Feedback->ReportAcceptedHit(Source,Hero->GetActorLocation()-LastDamageDirection*ContactRadius+FVector(0,0,Legacy?35:25),
                LastDamageDirection,HealthBefore-State.Health,0.f,false,State.Health<=0.f);
        }
    }
    if (State.Health<=State.MaxHealth*Settings.LooseHealthFraction) State.RepairLeft=FMath::Max(State.RepairLeft,Settings.RepairContacts);
    Changed(false); return true;
}
void UMCToothStatusComponent::Heal(float Amount)
{
    if(!GetOwner()->HasAuthority() || !IsAlive() || !FMath::IsFinite(Amount) || Amount<=0) return;
    State.Health=FMath::Min(State.MaxHealth,State.Health+Amount);Changed(true);
}
void UMCToothStatusComponent::ClearCoffeeForBooster()
{if(GetOwner()->HasAuthority()) {State.CoffeeLeft=0;Changed(true);}}
void UMCToothStatusComponent::SetPerkMaxHealth(float Maximum)
{
    if(!GetOwner()->HasAuthority() || !bInitialized || !FMath::IsFinite(Maximum) || FMath::IsNearlyEqual(Maximum,State.MaxHealth)) return;
    State.MaxHealth=FMath::Max(1.f,Maximum);State.Health=FMath::Min(State.Health,State.MaxHealth);Changed(true);
}
float UMCToothStatusComponent::PersonalCoffeeCareRate() const
{
    const auto* Perks=UMCPerkComponent::ForActor(GetOwner());
    return Perks?1.f/FMath::Max(.1f,Perks->RogueValue(EMCRogueStat::DirtyDuration)):1.f;
}
float UMCToothStatusComponent::PainAlpha() const
{
    const auto* GS=GetWorld()->GetGameState();
    const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    const float Age=Now-State.DamageAt;
    // Let the authored pain pose become fully visible before it relaxes.
    return Age>=0 && Age<1.1f?1-FMath::SmoothStep(.35f,1.1f,Age):0.f;
}
bool UMCToothStatusComponent::CareContact(bool bBrush, AMCToothCharacter* Worker)
{
    if (!bBrush && Cast<AMCArenaTooth>(GetOwner())) return false;
    if (!GetOwner()->HasAuthority() || !NeedsCare(bBrush)) return false;
    if (bBrush) {--State.CoffeeLeft;if(auto* Perks=UMCPerkComponent::ForActor(Worker)) Perks->NotifyCleaned(GetOwner()->GetActorLocation());}
    else { State.Health=FMath::Min(State.MaxHealth,State.Health+Settings.HealPerContact); State.RepairLeft=FMath::Max(0,State.RepairLeft-1); }
    Changed(true);
    if (!NeedsCare(bBrush) && IsValid(Worker))
    {
        if (bBrush) if (auto* Tutorial=AMCTutorialDirector::Find(GetWorld()))
            Tutorial->NotifyAction(Worker,Cast<AMCMouthSurface>(GetOwner())?EMCTutorialAction::BrushTongue:EMCTutorialAction::BrushTooth,GetOwner());
        if (auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>()) Mode->AwardTask(Worker,bBrush?EMCScoreTask::Coffee:EMCScoreTask::Repair);
    }
    return true;
}
bool UMCToothStatusComponent::RepairWithTool(float Amount, AMCToothCharacter* Worker)
{
    const auto* Arena=Cast<AMCArenaTooth>(GetOwner());
    if (!Arena || !Arena->HasAuthority() || !Arena->IsAvailable() || !IsValid(Worker)
        || Worker->GetWorld()!=GetWorld() || !Worker->CanWork() || !NeedsCare(false)
        || !FMath::IsFinite(Amount) || Amount<=0) return false;
    State.Health=FMath::Min(State.MaxHealth,State.Health+Amount);
    if (State.Health>=State.MaxHealth-KINDA_SMALL_NUMBER) {
        State.Health=State.MaxHealth; State.RepairLeft=0;
    } else if (State.RepairLeft>0) {
        State.RepairLeft=FMath::Min(State.RepairLeft,FMath::CeilToInt(Settings.RepairContacts*(1-State.Health/State.MaxHealth)));
    }
    Changed(true);
    if (!NeedsCare(false))
        if (auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>()) Mode->AwardTask(Worker,EMCScoreTask::Repair);
    return true;
}
FString UMCToothStatusComponent::Summary() const
{
    if (!IsAlive()) return TEXT("DOWN");
    return FString::Printf(TEXT("HP %.0f/%.0f  |  COFFEE %d/%d  |  REPAIR %d"),State.Health,State.MaxHealth,State.CoffeeLeft,State.CoffeeTotal,State.RepairLeft);
}
void UMCToothStatusComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UMCToothStatusComponent,State); DOREPLIFETIME(UMCToothStatusComponent,Settings);
}
