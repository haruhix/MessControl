#include "MCInventoryComponent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCFoodActor.h"
#include "MCMouthSurface.h"
#include "MCGripComponent.h"
#include "MCExpressionComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/GameStateBase.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

UMCInventoryComponent::UMCInventoryComponent()
{
    PrimaryComponentTick.bCanEverTick=true; SetIsReplicatedByDefault(true);
    Profile=TSoftObjectPtr<UMCEquipmentProfile>(FSoftObjectPath(TEXT("/Game/Data/DA_Equipment.DA_Equipment")));
}
double UMCInventoryComponent::Now() const
{ const auto* GS=GetWorld()->GetGameState(); return GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds(); }
void UMCInventoryComponent::BeginPlay()
{
    Super::BeginPlay(); Hero=Cast<AMCToothCharacter>(GetOwner()); Settings=Profile.LoadSynchronous();
    if(!Settings) Settings=NewObject<UMCEquipmentProfile>(this);
    if(!Hero || GetNetMode()==NM_DedicatedServer) return;
    auto Part=[&](const TCHAR* Name) {
        auto* Mesh=NewObject<UStaticMeshComponent>(Hero,Name); Mesh->SetupAttachment(Hero->BrushPivot);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->SetCanEverAffectNavigation(false);
        Hero->AddInstanceComponent(Mesh); Mesh->RegisterComponent(); return Mesh;
    };
    Tool=Part(TEXT("InventoryTool")); Detail=Part(TEXT("InventoryDetail"));
    PrimaryComponentTick.AddPrerequisite(Hero,Hero->PrimaryActorTick); RefreshMesh();
}
void UMCInventoryComponent::ServerSelect_Implementation(EMCToolSlot Slot)
{
    if(uint8(Slot)>uint8(EMCToolSlot::Spray) || !Hero || !Hero->CanWork() || !Hero->CanSwitchTool()) return;
    if(Selected==Slot) return;
    Hero->ServerSetPrimary(false); Hero->ResetContact(); Hero->bSelfCare=false;
    Selected=Slot; GetOwner()->ForceNetUpdate();
}
void UMCInventoryComponent::UnlockWaterJet()
{ if(GetOwner()->HasAuthority()) { bWaterJetUnlocked=true; GetOwner()->ForceNetUpdate(); } }
float UMCInventoryComponent::CooldownSeconds() const { return FMath::Max(1.f,Settings?Settings->SprayCooldown:8.f); }
float UMCInventoryComponent::SpraySecondsLeft() const { return FMath::Max(0.f,float(SprayReadyAt-Now())); }
bool UMCInventoryComponent::CanBreak(const AMCFoodActor* Food) const
{
    return IsValid(Food) && !Food->bBrushTool && !Food->IsDisposed()
        && (Selected==EMCToolSlot::Pickaxe?Food->IsHardFood():Selected==EMCToolSlot::Knife && !Food->IsHardFood());
}
float UMCInventoryComponent::Damage() const
{ return FMath::Max(1.f,Selected==EMCToolSlot::Pickaxe?(Settings?Settings->PickaxeDamage:40.f):(Settings?Settings->KnifeDamage:25.f)); }
float UMCInventoryComponent::SwingDuration() const { return Selected==EMCToolSlot::Pickaxe?1.05f:Selected==EMCToolSlot::Knife?.50f:.85f; }
float UMCInventoryComponent::SwingContactTime() const { return Selected==EMCToolSlot::Pickaxe?.38f:Selected==EMCToolSlot::Knife?.16f:.16f; }
float UMCInventoryComponent::SwingAngle(EMCToolSlot Slot,float T)
{
    const float Wind=Slot==EMCToolSlot::Pickaxe?.30f:.11f;
    const float Hit=Slot==EMCToolSlot::Pickaxe?.44f:.22f;
    const float End=Slot==EMCToolSlot::Pickaxe?.95f:.44f;
    const float Back=Slot==EMCToolSlot::Pickaxe?115.f:-55.f,Front=Slot==EMCToolSlot::Pickaxe?-105.f:65.f;
    if(T<0 || T>=End) return -12;
    if(T<Wind) return FMath::Lerp(-12.f,Back,FMath::SmoothStep(0.f,Wind,T));
    if(T<Hit) return FMath::Lerp(Back,Front,FMath::SmoothStep(Wind,Hit,T));
    return FMath::Lerp(Front,-12.f,FMath::SmoothStep(Hit,End,T));
}
FVector UMCInventoryComponent::SwingOffset(EMCToolSlot Slot,float T)
{
    if(Slot!=EMCToolSlot::Pickaxe || T<0 || T>=.95f) return FVector::ZeroVector;
    const FVector Wind(-35,12,90),Strike(75,-10,-6);
    if(T<.30f) return FMath::Lerp(FVector::ZeroVector,Wind,FMath::SmoothStep(0.f,.30f,T));
    if(T<.44f) return FMath::Lerp(Wind,Strike,FMath::SmoothStep(.30f,.44f,T));
    return FMath::Lerp(Strike,FVector::ZeroVector,FMath::SmoothStep(.44f,.95f,T));
}
void UMCInventoryComponent::ServerSpray_Implementation()
{
    if(!Hero || !Hero->CanWork() || Hero->bInCoffee || Selected!=EMCToolSlot::Spray || Now()<SprayReadyAt) return;
    AMCMouthSurface* Best=nullptr; float Distance=FMath::Square(FMath::Max(10.f,Settings?Settings->SprayReach:235.f));
    for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) {
        if(!It->bUlcer || It->IsNumb()) continue;
        const FVector D=It->GetActorLocation()-Hero->GetActorLocation();
        if(D.SizeSquared()>Distance || FVector::DotProduct(D.GetSafeNormal2D(),Hero->GetActorForwardVector())<.15f) continue;
        FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(MCSpray),false,Hero); Q.AddIgnoredActor(*It);
        if(GetWorld()->LineTraceSingleByChannel(Hit,Hero->GetActorLocation(),It->GetActorLocation()+FVector(0,0,12),ECC_Visibility,Q)) continue;
        Best=*It; Distance=D.SizeSquared();
    }
    if(!Best || !Best->ApplyAnesthetic(FMath::Max(1.f,Settings?Settings->NumbSeconds:10.f))) return;
    LastSprayAt=Now(); SprayReadyAt=LastSprayAt+CooldownSeconds(); Hero->NotifyTaskFeedback(true);
    if(Hero->SoundPalette) Hero->SoundPalette->Play(this,TEXT("Brush"),Best->GetActorLocation());
    GetOwner()->ForceNetUpdate();
}
FString UMCInventoryComponent::ToolName() const
{
    switch(Selected) {
    case EMCToolSlot::Pickaxe:return TEXT("КИРКА");
    case EMCToolSlot::Knife:return TEXT("НОЖ");
    case EMCToolSlot::Spray:return TEXT("СПРЕЙ");
    default:return bWaterJetUnlocked?TEXT("ВОДОМЁТ"):TEXT("ЩЁТКА"); }
}
void UMCInventoryComponent::RefreshMesh()
{
    if(!Tool || !Detail) return;
    Presented=Selected; bPresentedUpgrade=bWaterJetUnlocked; Detail->SetVisibility(false);
    UStaticMesh* Mesh=nullptr; FTransform Transform=FTransform::Identity;
    if(Selected==EMCToolSlot::Pickaxe) { Mesh=Settings->PickaxeMesh.LoadSynchronous(); Transform=Settings->PickaxeTransform; }
    if(Selected==EMCToolSlot::Knife) { Mesh=Settings->KnifeMesh.LoadSynchronous(); Transform=Settings->KnifeTransform; }
    if(Selected==EMCToolSlot::Spray) { Mesh=Settings->SprayMesh.LoadSynchronous(); Transform=Settings->SprayTransform; }
    if(Selected==EMCToolSlot::Brush && bWaterJetUnlocked) { Mesh=Settings->WaterJetMesh.LoadSynchronous(); Transform=Settings->WaterJetTransform; }
    if(!Mesh && Selected==EMCToolSlot::Pickaxe) {
        Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Art/Meshes/Equipments/SM_Pick.SM_Pick"));
        Transform=FTransform(FRotator(0,0,90),FVector(25,0,0),FVector(.65));
    }
    if(!Mesh && (Selected==EMCToolSlot::Knife || Selected==EMCToolSlot::Spray)) {
        Mesh=LoadObject<UStaticMesh>(nullptr,Selected==EMCToolSlot::Knife?TEXT("/Engine/BasicShapes/Cube.Cube"):TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
        Transform=Selected==EMCToolSlot::Knife?FTransform(FRotator::ZeroRotator,FVector(32,0,0),FVector(.58,.035,.11))
            :FTransform(FRotator::ZeroRotator,FVector(15,0,12),FVector(.19,.19,.32));
        Detail->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Detail->SetRelativeTransform(Selected==EMCToolSlot::Knife?FTransform(FRotator::ZeroRotator,FVector(-5,0,0),FVector(.24,.09,.10))
            :FTransform(FRotator::ZeroRotator,FVector(22,0,31),FVector(.23,.08,.08)));
    }
    Tool->SetStaticMesh(Mesh); Tool->SetRelativeTransform(Transform);
}
void UMCInventoryComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Dt,Type,Tick); if(!Hero || !Tool) return;
    if(Presented!=Selected || bPresentedUpgrade!=bWaterJetUnlocked) RefreshMesh();
    const bool Visible=Hero->Status->IsAlive() && !Hero->HeldFood && !Hero->OrderJumpTarget && !Hero->SwallowedBy
        && Hero->AnimationOrderPress<.05f && Hero->AnimationOrderFlight<.05f && Hero->Grip->Blend()<.05f && Hero->Expression->BodyAlpha()<.01f;
    const bool Custom=Tool->GetStaticMesh()!=nullptr;
    Tool->SetVisibility(Visible && Custom); Detail->SetVisibility(Visible && Custom && (Selected==EMCToolSlot::Knife?Settings->KnifeMesh.IsNull():Selected==EMCToolSlot::Spray && Settings->SprayMesh.IsNull()));
    if(Selected!=EMCToolSlot::Brush || Custom) Hero->Brush->SetVisibility(false);
}
void UMCInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UMCInventoryComponent,Selected);
    DOREPLIFETIME(UMCInventoryComponent,bWaterJetUnlocked); DOREPLIFETIME(UMCInventoryComponent,SprayReadyAt); DOREPLIFETIME(UMCInventoryComponent,LastSprayAt);
}
