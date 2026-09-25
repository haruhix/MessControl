#include "MCArenaToothSocket.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AMCArenaToothSocket::AMCArenaToothSocket()
{
    Preview=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ToothPlacement")); SetRootComponent(Preview);
    Preview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Preview->SetHiddenInGame(true); // Hidden on every machine, including late-joining clients.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Game/Art/Meshes/SM_ToothProp"));
    if (Mesh.Succeeded()) Preview->SetStaticMesh(Mesh.Object);
}
