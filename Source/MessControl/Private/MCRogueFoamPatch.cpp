#include "MCRogueFoamPatch.h"
#include "MCNutEnemy.h"
#include "MCNutBoss.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"
AMCRogueFoamPatch::AMCRogueFoamPatch()
{
    bReplicates=true;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickInterval=.1f;
    Foam=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Foam"));SetRootComponent(Foam);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    if(Shape.Succeeded()) Foam->SetStaticMesh(Shape.Object);
    Foam->SetRelativeScale3D(FVector(2,2,.025));Foam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Foam->SetCanEverAffectNavigation(false);Foam->SetCastShadow(false);
}
void AMCRogueFoamPatch::BeginPlay()
{
    Super::BeginPlay();
    if(auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/Roguelike/M_RogueFoam.M_RogueFoam"))) Foam->SetMaterial(0,Material);
    if(HasAuthority()) SetLifeSpan(3);
}
void AMCRogueFoamPatch::Tick(float Dt)
{
    Super::Tick(Dt);if(!HasAuthority()) return;
    for(TActorIterator<AMCNutEnemy> It(GetWorld());It;++It)
        if(!Cast<AMCNutBoss>(*It) && It->CanReceiveToolHit() && FMath::Abs(It->GetActorLocation().Z-GetActorLocation().Z)<120
            && FVector::DistSquared2D(GetActorLocation(),It->GetActorLocation())<=FMath::Square(100.f))
            It->ApplyRogueSlow(.8f,.2f);
}
