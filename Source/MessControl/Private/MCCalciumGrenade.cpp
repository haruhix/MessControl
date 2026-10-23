#include "MCCalciumGrenade.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCPlayerState.h"
#include "MCHubEconomyComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AMCCalciumGrenade::AMCCalciumGrenade()
{
    bReplicates=true;PrimaryActorTick.bCanEverTick=true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("CalciumRoot")));
    Capsule=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CalciumCapsule"));Capsule->SetupAttachment(RootComponent);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
    if(Sphere.Succeeded()) Capsule->SetStaticMesh(Sphere.Object);
    Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);Capsule->SetGenerateOverlapEvents(false);
    Capsule->SetCanEverAffectNavigation(false);Capsule->SetRelativeScale3D(FVector(.14,.14,.23));
    Mist=CreateDefaultSubobject<UNiagaraComponent>(TEXT("CalciumMist"));Mist->SetupAttachment(RootComponent);
    Mist->SetAutoActivate(false);Mist->SetAutoDestroy(false);Mist->SetRelativeRotation(FRotator(90,0,0));
    MistSystem=TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NS_SprayMist.NS_SprayMist")));
    CapsuleMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Gameplay/Hub/Economy/M_CalciumCapsule.M_CalciumCapsule")));
}
double AMCCalciumGrenade::Now() const
{
    const auto* GS=GetWorld()?GetWorld()->GetGameState():nullptr;
    return GS?GS->GetServerWorldTimeSeconds():(GetWorld()?GetWorld()->GetTimeSeconds():0.);
}
void AMCCalciumGrenade::BeginPlay()
{
    Super::BeginPlay();
    const auto* Hero=Cast<AMCToothCharacter>(GetOwner());
    const auto* Player=Hero?Hero->GetPlayerState<AMCPlayerState>():nullptr;
    if (!Player || !Player->Economy || !Player->Economy->IsCalciumEnabled())
    {
        SetActorHiddenInGame(true); SetActorTickEnabled(false);
        if (HasAuthority()) Destroy();
        return;
    }
    if(HasAuthority()) {StartedAt=Now();SetLifeSpan(1.6f);ForceNetUpdate();}
    if(GetNetMode()!=NM_DedicatedServer) {
        Capsule->SetMaterial(0,CapsuleMaterial.LoadSynchronous());
        Mist->SetAsset(MistSystem.LoadSynchronous());
    }
}
void AMCCalciumGrenade::Detonate()
{
    if(!HasAuthority() || bDetonated) return;
    bDetonated=true;
    auto* Hero=Cast<AMCToothCharacter>(GetOwner());
    const auto* Player=IsValid(Hero)?Hero->GetPlayerState<AMCPlayerState>():nullptr;
    if(Player && Player->Economy && Player->Economy->IsCalciumEnabled() && Hero->Status && Hero->Status->IsAlive()
        && FVector::DistSquared(Hero->GetActorLocation(),GetActorLocation())<=FMath::Square(200.f))
        Hero->Status->Heal(FMath::Clamp(HealAmount,1.f,100.f));
    ForceNetUpdate();
}
void AMCCalciumGrenade::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const double Age=FMath::Max(0.,Now()-StartedAt);
    if(HasAuthority() && Age>=.45) Detonate();
    if(GetNetMode()==NM_DedicatedServer) return;
    const float Toss=FMath::Clamp(float(Age/.45),0.f,1.f);
    Capsule->SetRelativeLocation(FVector(0,0,12.f+FMath::Sin(Toss*PI)*48.f));
    Capsule->SetRelativeRotation(FRotator(0,0,Toss*170.f));
    Capsule->SetVisibility(Age<.45);
    if(Age>=.45 && !bMistPresented) {bMistPresented=true;Mist->Activate(true);}
    if(Age>=.7) Mist->Deactivate();
}
void AMCCalciumGrenade::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCCalciumGrenade,StartedAt);DOREPLIFETIME(AMCCalciumGrenade,HealAmount);
    DOREPLIFETIME(AMCCalciumGrenade,bDetonated);
}
