#include "MCHubFishingZone.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "MCToothCharacter.h"
#include "MCFishingRodComponent.h"
#include "MCHubInteractionPromptWidget.h"
#include "MCGameplayUIFactory.h"
#include "Components/WidgetComponent.h"
#include "Kismet/GameplayStatics.h"

AMCHubFishingZone::AMCHubFishingZone()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    SetReplicateMovement(true);
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval=.1f;
    Zone = CreateDefaultSubobject<UBoxComponent>(TEXT("PierFishingVolume"));
    SetRootComponent(Zone);
    Zone->SetBoxExtent(FVector(250.f, 140.f, 140.f));
    // Position containment handles initial overlaps, teleporting and recovery.
    // This volume neither blocks movement nor generates physics/overlap work.
    Zone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Zone->SetGenerateOverlapEvents(false);
    Zone->SetCanEverAffectNavigation(false);
    Zone->SetHiddenInGame(true);
    Zone->ShapeColor = FColor(48, 210, 215);
    InteractionPrompt=CreateDefaultSubobject<UWidgetComponent>(TEXT("FishingInteractionPrompt"));
    InteractionPrompt->SetupAttachment(Zone);InteractionPrompt->SetRelativeLocation(FVector(0,0,130));
    InteractionPrompt->SetWidgetSpace(EWidgetSpace::Screen);
    InteractionPrompt->SetWidgetClass(UMCHubInteractionPromptWidget::StaticClass());
    InteractionPrompt->SetDrawSize(FVector2D(64,66));InteractionPrompt->SetDrawAtDesiredSize(true);
    InteractionPrompt->SetPivot(FVector2D(.5f,1.f));InteractionPrompt->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    InteractionPrompt->SetGenerateOverlapEvents(false);InteractionPrompt->SetCanEverAffectNavigation(false);
    InteractionPrompt->SetVisibility(false);InteractionPrompt->SetHiddenInGame(true);InteractionPrompt->SetCastShadow(false);
}

void AMCHubFishingZone::BeginPlay()
{
    Super::BeginPlay();
    if(GetNetMode()==NM_DedicatedServer) {SetActorTickEnabled(false);return;}
    InteractionPrompt->SetWidgetClass(UMCGameplayUIFactory::ResolveClass(TEXT("HubInteractionPrompt"),UMCHubInteractionPromptWidget::StaticClass()));
}

void AMCHubFishingZone::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    auto* PC=UGameplayStatics::GetPlayerController(this,0);
    const auto* Hero=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    const bool Show=PC && PC->IsLocalController() && Hero && ContainsPawn(Hero)
        && !PC->IsMoveInputIgnored() && Hero->FishingRod && Hero->FishingRod->CanBeginFishing();
    if(Show && PC->GetLocalPlayer()) {
        InteractionPrompt->SetOwnerPlayer(PC->GetLocalPlayer());InteractionPrompt->InitWidget();
        if(auto* Widget=InteractionPrompt->GetUserWidgetObject()) Widget->SetOwningPlayer(PC);
    }
    InteractionPrompt->SetVisibility(Show);InteractionPrompt->SetHiddenInGame(!Show);
    InteractionPrompt->SetComponentTickEnabled(Show);
}

bool AMCHubFishingZone::ContainsPawn(const APawn* Pawn) const
{
    if (!bEnabled || !Zone || !IsValid(Pawn) || Pawn->GetWorld() != GetWorld()) return false;
    const FVector Local = Zone->GetComponentTransform().InverseTransformPosition(Pawn->GetActorLocation());
    const FVector Extent = Zone->GetUnscaledBoxExtent();
    return !Local.ContainsNaN() && FMath::Abs(Local.X) <= Extent.X
        && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}

void AMCHubFishingZone::SetZoneExtent(FVector HalfExtent)
{
    if (!Zone || HalfExtent.ContainsNaN()) return;
    Zone->SetBoxExtent(HalfExtent.GetAbs().ComponentMax(FVector(25.f)));
}
