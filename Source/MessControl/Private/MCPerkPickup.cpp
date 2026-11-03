#include "MCPerkPickup.h"
#include "MCWorldPresentationComponent.h"
#include "MCVFXPresentationComponent.h"
#include "MCRewardChest.h"
#include "MCToothCharacter.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

AMCPerkPickup::AMCPerkPickup()
{
    bReplicates=true; PrimaryActorTick.bCanEverTick=false;
    Trigger=CreateDefaultSubobject<USphereComponent>(TEXT("PickupContact")); SetRootComponent(Trigger);
    Trigger->InitSphereRadius(34); Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Trigger->SetCollisionResponseToAllChannels(ECR_Ignore); Trigger->SetCollisionResponseToChannel(ECC_Pawn,ECR_Overlap);
    Trigger->SetGenerateOverlapEvents(true);
    Visual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PerkOrb")); Visual->SetupAttachment(Trigger);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision); Visual->SetRelativeScale3D(FVector(.48)); Visual->SetCastShadow(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if(Mesh.Succeeded()) Visual->SetStaticMesh(Mesh.Object);
    if(Material.Succeeded()) Visual->SetMaterial(0,Material.Object);
    Label=CreateDefaultSubobject<UTextRenderComponent>(TEXT("PerkName")); Label->SetupAttachment(Trigger);
    Label->SetRelativeLocation(FVector(0,0,60)); Label->SetWorldSize(14); Label->SetHorizontalAlignment(EHTA_Center);
    Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DetailLabel=CreateDefaultSubobject<UTextRenderComponent>(TEXT("PerkDescription")); DetailLabel->SetupAttachment(Trigger);
    DetailLabel->SetRelativeLocation(FVector(0,0,40)); DetailLabel->SetWorldSize(10); DetailLabel->SetHorizontalAlignment(EHTA_Center);
    DetailLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AMCPerkPickup::InitializePickup(AMCRewardChest* Chest,int32 Index,FName ID,FText Name,FText Detail,EMCPerkPolarity Kind)
{
    RewardChest=Chest; LootIndex=Index; PerkID=ID; DisplayName=Name; Description=Detail; Polarity=Kind;
}

void AMCPerkPickup::BeginPlay()
{
    Super::BeginPlay();MCWorldPresentation::PrepareOwner(this,{TEXT("Reward.PerkPickup")});bPresentationBegun=true;RefreshAppearance();
    if(HasAuthority()) {
        Trigger->OnComponentBeginOverlap.AddDynamic(this,&AMCPerkPickup::Entered);
        Trigger->OnComponentEndOverlap.AddDynamic(this,&AMCPerkPickup::Left);
    }
}

void AMCPerkPickup::RefreshAppearance()
{
    const FLinearColor Color=Polarity==EMCPerkPolarity::Positive ? FLinearColor(.15f,1.f,.36f) : FLinearColor(.92f,.18f,.48f);
    Label->SetText(DisplayName.IsEmpty()?FText::FromName(PerkID):DisplayName); Label->SetTextRenderColor(Color.ToFColor(true));
    DetailLabel->SetText(Description); DetailLabel->SetTextRenderColor(FColor::White);
    if(auto* MID=Visual->CreateDynamicMaterialInstance(0)) MID->SetVectorParameterValue(TEXT("Color"),Color);
    SetActorHiddenInGame(bClaimed); SetActorEnableCollision(!bClaimed);
    if(bPresentationBegun) {
        auto* PresentationOwner=FindComponentByClass<UMCWorldPresentationComponent>();auto* Source=PresentationOwner?PresentationOwner->GetSource(TEXT("Reward.PerkPickup")):nullptr;
        FMCPerkVFXGameplayValues V;V.Radius=34;V.Color=Color;V.bHasColor=true;V.LocalStateAgeSeconds=0;
        // Native overlap/claim is unchanged; replicated appearance may repeat.
        // Once observed, neither a metadata refresh nor a late profile retries it.
        if(!bPresentationArrivalObserved) {bPresentationArrivalObserved=true;if(!bClaimed&&Source)Source->PlayLocalSourceEvent(TEXT("Reward.PerkPickup"),TEXT("Arrival"),GetActorTransform(),V);}
        if(bClaimed&&!bPresentationClaimObserved) {bPresentationClaimObserved=true;if(Source)Source->PlayLocalSourceEvent(TEXT("Reward.PerkPickup"),TEXT("Claim"),GetActorTransform(),V);}
    }
}

void AMCPerkPickup::Entered(UPrimitiveComponent*,AActor* Other,UPrimitiveComponent*,int32,bool,const FHitResult&)
{
    TryCollect(Cast<AMCToothCharacter>(Other));
}

void AMCPerkPickup::Left(UPrimitiveComponent*,AActor* Other,UPrimitiveComponent*,int32)
{
    if(HasAuthority() && !Trigger->IsOverlappingActor(Other)) MustReenter.Remove(Other);
}

void AMCPerkPickup::StartArming()
{
    if(HasAuthority() && !bClaimed && !bArmed)
        GetWorldTimerManager().SetTimer(ArmTimer,this,&AMCPerkPickup::Arm,.8f,false);
}

void AMCPerkPickup::Arm()
{
    if(!HasAuthority() || bClaimed) return;
    // Seeing the three choices never grants one. A player already here must leave and return.
    TArray<AActor*> Present; Trigger->GetOverlappingActors(Present,AMCToothCharacter::StaticClass());
    for(AActor* Actor:Present) MustReenter.Add(Actor);
    bArmed=true;
}

bool AMCPerkPickup::TryCollect(AMCToothCharacter* Player)
{
    return HasAuthority() && bArmed && !bClaimed && IsValid(Player) && !MustReenter.Contains(Player)
        && Trigger->IsOverlappingActor(Player) && IsValid(RewardChest) && RewardChest->TryClaim(this,Player);
}

void AMCPerkPickup::MarkClaimed()
{
    if(!HasAuthority() || bClaimed) return;
    bClaimed=true; RefreshAppearance(); ForceNetUpdate();
}

void AMCPerkPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCPerkPickup,PerkID); DOREPLIFETIME(AMCPerkPickup,DisplayName); DOREPLIFETIME(AMCPerkPickup,Description); DOREPLIFETIME(AMCPerkPickup,Polarity);
    DOREPLIFETIME(AMCPerkPickup,bClaimed); DOREPLIFETIME(AMCPerkPickup,LootIndex); DOREPLIFETIME(AMCPerkPickup,RewardChest);
}
