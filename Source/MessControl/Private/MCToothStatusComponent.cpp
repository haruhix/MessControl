#include "MCToothStatusComponent.h"
#include "MCArenaTooth.h"
#include "MCToothCharacter.h"
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
    State.Health=State.MaxHealth; State.CoffeeTotal=Settings.CoffeeContacts; bInitialized=true;
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
    return IsAlive() && (bBrush?State.CoffeeLeft>0:State.Health<State.MaxHealth || State.RepairLeft>0);
}
void UMCToothStatusComponent::ApplyCoffee(float Amount)
{
    if (!GetOwner()->HasAuthority() || !IsAlive() || !FMath::IsFinite(Amount)) return;
    State.CoffeeTotal=Settings.CoffeeContacts;
    State.CoffeeLeft=FMath::CeilToInt(FMath::Clamp(Amount,0.f,1.f)*State.CoffeeTotal); Changed(true);
}
void UMCToothStatusComponent::Loosen()
{
    if (!GetOwner()->HasAuthority() || !IsAlive()) return;
    State.RepairLeft=Settings.RepairContacts; Changed(false);
}
bool UMCToothStatusComponent::Damage(float Amount,FVector Direction)
{
    if (!GetOwner()->HasAuthority() || !IsAlive() || !FMath::IsFinite(Amount) || Amount<=0 || Direction.ContainsNaN()) return false;
    LastDamageDirection=Direction.GetSafeNormal(); State.Health=FMath::Max(0.f,State.Health-Amount);
    if (State.Health<=State.MaxHealth*Settings.LooseHealthFraction) State.RepairLeft=FMath::Max(State.RepairLeft,Settings.RepairContacts);
    Changed(false); return true;
}
bool UMCToothStatusComponent::CareContact(bool bBrush)
{
    if (!GetOwner()->HasAuthority() || !NeedsCare(bBrush)) return false;
    if (bBrush) --State.CoffeeLeft;
    else { State.Health=FMath::Min(State.MaxHealth,State.Health+Settings.HealPerContact); State.RepairLeft=FMath::Max(0,State.RepairLeft-1); }
    Changed(true); return true;
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
