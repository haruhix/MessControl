#include "MCHubReadyZone.h"
#include "MCHubGameState.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "MCHub"

AMCHubReadyZone::AMCHubReadyZone()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    SetReplicateMovement(true);
    PrimaryActorTick.bCanEverTick = false;
    Zone = CreateDefaultSubobject<UBoxComponent>(TEXT("ReadyVolume"));
    SetRootComponent(Zone);
    Zone->SetBoxExtent(FVector(300.f,250.f,160.f));
    Zone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Zone->SetCollisionResponseToAllChannels(ECR_Ignore);
    Zone->SetCollisionResponseToChannel(ECC_Pawn,ECR_Overlap);
    Zone->SetGenerateOverlapEvents(true);
    Zone->SetCanEverAffectNavigation(false);

    FloorMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FloorMarker"));
    FloorMarker->SetupAttachment(Zone);
    BorderMarkers.Add(FloorMarker);
    for (const FName Name : {FName(TEXT("SouthBorder")),FName(TEXT("EastBorder")),FName(TEXT("WestBorder"))})
    {
        auto* Edge = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Edge->SetupAttachment(Zone);
        BorderMarkers.Add(Edge);
    }
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    for (UStaticMeshComponent* Edge : BorderMarkers)
    {
        Edge->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Edge->SetCastShadow(false);
        Edge->SetCanEverAffectNavigation(false);
        if (Cube.Succeeded()) Edge->SetStaticMesh(Cube.Object);
        if (Material.Succeeded()) Edge->SetMaterial(0,Material.Object);
    }

    // An optional separate mesh slot for a future mouth/portal illustration.
    MouthVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MouthVisual"));
    MouthVisual->SetupAttachment(Zone);
    MouthVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    MouthVisual->SetCanEverAffectNavigation(false);

    Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("ReadyLabel"));
    Label->SetupAttachment(Zone);
    Label->SetHorizontalAlignment(EHTA_Center);
    Label->SetVerticalAlignment(EVRTA_TextCenter);
    Label->SetWorldSize(55.f);
    Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Label->SetCastShadow(false);
    RefreshGeometry();
}

void AMCHubReadyZone::RefreshGeometry()
{
    if (!Zone || !FloorMarker || !Label) return;
    const FVector Extent = Zone->GetUnscaledBoxExtent();
    const float Width = 6.f;
    const FVector Positions[] = {FVector(0,Extent.Y-Width*.5f,-Extent.Z+3.f), FVector(0,-Extent.Y+Width*.5f,-Extent.Z+3.f),
        FVector(Extent.X-Width*.5f,0,-Extent.Z+3.f), FVector(-Extent.X+Width*.5f,0,-Extent.Z+3.f)};
    for (int32 Index = 0; Index < FMath::Min(BorderMarkers.Num(),4); ++Index)
    {
        UStaticMeshComponent* Edge = BorderMarkers[Index];
        if (!Edge) continue;
        Edge->SetRelativeLocation(Positions[Index]);
        Edge->SetRelativeScale3D(Index < 2 ? FVector(Extent.X*.02f,Width*.01f,.025f)
            : FVector(Width*.01f,Extent.Y*.02f,.025f));
        Edge->SetVisibility(bShowFloorMarker && bEnabled);
    }
    Label->SetRelativeLocation(FVector(0,0,-Extent.Z+7.f));
    Label->SetRelativeRotation(FRotator(90.f,180.f,0));
    Label->SetTextRenderColor(WaitingColor.ToFColor(true));
    Label->SetText(LOCTEXT("ReadyZoneInitial", "ГОТОВНОСТЬ"));
}

void AMCHubReadyZone::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RefreshGeometry();
}

void AMCHubReadyZone::SetZoneExtent(FVector HalfExtent)
{
    if (!Zone || HalfExtent.ContainsNaN()) return;
    Zone->SetBoxExtent(HalfExtent.GetAbs().ComponentMax(FVector(25.f)));
    RefreshGeometry();
}

bool AMCHubReadyZone::ContainsPawn(const APawn* Pawn) const
{
    if (!bEnabled || !Zone || !IsValid(Pawn) || Pawn->GetWorld() != GetWorld()) return false;
    // Use the actual oriented volume and authoritative pawn center. This also
    // handles initial overlaps and teleports, without stale overlap counters.
    const FVector Local = Zone->GetComponentTransform().InverseTransformPosition(Pawn->GetActorLocation());
    const FVector Extent = Zone->GetUnscaledBoxExtent();
    return !Local.ContainsNaN() && FMath::Abs(Local.X) <= Extent.X
        && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}

void AMCHubReadyZone::BeginPlay()
{
    Super::BeginPlay();
    for (UStaticMeshComponent* Edge : BorderMarkers)
        if (Edge) if (auto* Material = Edge->CreateDynamicMaterialInstance(0))
        {
            Material->SetVectorParameterValue(TEXT("Color"),WaitingColor);
            Material->SetScalarParameterValue(TEXT("Roughness"),.75f);
            MarkerMaterials.Add(Material);
        }
    BoundState = GetWorld()->GetGameState<AMCHubGameState>();
    if (BoundState) BoundState->OnHubStateChanged.AddDynamic(this,&AMCHubReadyZone::RefreshLabel);
    RefreshLabel();
}

void AMCHubReadyZone::EndPlay(const EEndPlayReason::Type Reason)
{
    if (IsValid(BoundState)) BoundState->OnHubStateChanged.RemoveDynamic(this,&AMCHubReadyZone::RefreshLabel);
    BoundState = nullptr;
    Super::EndPlay(Reason);
}

void AMCHubReadyZone::RefreshLabel()
{
    if (!Label || !BoundState) return;
    Label->SetText(FText::Format(LOCTEXT("ReadyZoneCount", "ГОТОВНОСТЬ\n{0}/{1}"),
        FText::AsNumber(BoundState->GetReadyCount()), FText::AsNumber(BoundState->GetParticipantCount())));
    Label->SetTextRenderColor((BoundState->AreAllReady()?ReadyColor:WaitingColor).ToFColor(true));
    for (UMaterialInstanceDynamic* Material : MarkerMaterials)
        if (Material) Material->SetVectorParameterValue(TEXT("Color"),BoundState->AreAllReady()?ReadyColor:WaitingColor);
}

#undef LOCTEXT_NAMESPACE
