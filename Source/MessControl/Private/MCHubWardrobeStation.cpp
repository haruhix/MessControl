#include "MCHubWardrobeStation.h"

#include "MCGameplayUIFactory.h"
#include "MCHubInteractionPromptWidget.h"
#include "Components/BoxComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

AMCHubWardrobeStation::AMCHubWardrobeStation()
{
    PrimaryActorTick.bCanEverTick=false;
    InteractionVolume=CreateDefaultSubobject<UBoxComponent>(TEXT("WardrobeInteractionVolume"));
    SetRootComponent(InteractionVolume);
    InteractionVolume->SetBoxExtent(FVector(160.f,140.f,120.f));
    InteractionVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    InteractionVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
    InteractionVolume->SetGenerateOverlapEvents(false);
    InteractionVolume->SetCanEverAffectNavigation(false);
    InteractionVolume->SetHiddenInGame(true);

    InteractionPrompt=CreateDefaultSubobject<UWidgetComponent>(TEXT("WardrobeInteractionPrompt"));
    InteractionPrompt->SetupAttachment(InteractionVolume);
    InteractionPrompt->SetRelativeLocation(FVector(0.f,-285.f,160.f));
    InteractionPrompt->SetWidgetSpace(EWidgetSpace::Screen);
    InteractionPrompt->SetWidgetClass(UMCHubInteractionPromptWidget::StaticClass());
    InteractionPrompt->SetDrawSize(FVector2D(64.f,66.f));InteractionPrompt->SetDrawAtDesiredSize(true);
    InteractionPrompt->SetPivot(FVector2D(.5f,1.f));
    InteractionPrompt->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    InteractionPrompt->SetGenerateOverlapEvents(false);InteractionPrompt->SetCanEverAffectNavigation(false);
    InteractionPrompt->SetCastShadow(false);InteractionPrompt->SetVisibility(false);InteractionPrompt->SetHiddenInGame(true);
    InteractionPrompt->PrimaryComponentTick.bStartWithTickEnabled=false;
}

void AMCHubWardrobeStation::BeginPlay()
{
    Super::BeginPlay();
    if(GetNetMode()!=NM_DedicatedServer && InteractionPrompt->GetWidgetClass()==UMCHubInteractionPromptWidget::StaticClass())
        InteractionPrompt->SetWidgetClass(UMCGameplayUIFactory::ResolveClass(TEXT("HubInteractionPrompt"),UMCHubInteractionPromptWidget::StaticClass()));
    InteractionPrompt->SetVisibility(false);InteractionPrompt->SetHiddenInGame(true);InteractionPrompt->SetComponentTickEnabled(false);
}

void AMCHubWardrobeStation::SetLocalPromptVisible(APlayerController* LocalController,bool bVisible)
{
    if(!InteractionPrompt || !IsValid(LocalController) || !LocalController->IsLocalController()
        || LocalController->GetWorld()!=GetWorld() || !LocalController->GetLocalPlayer()) return;
    const bool bShow=bVisible && ContainsPawn(LocalController->GetPawn());
    if(bShow) {
        InteractionPrompt->SetOwnerPlayer(LocalController->GetLocalPlayer());
        InteractionPrompt->InitWidget();
        if(auto* Widget=InteractionPrompt->GetUserWidgetObject()) Widget->SetOwningPlayer(LocalController);
    }
    if(auto* Widget=InteractionPrompt->GetUserWidgetObject())
        Widget->SetVisibility(bShow?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    InteractionPrompt->SetVisibility(bShow);InteractionPrompt->SetHiddenInGame(!bShow);
    InteractionPrompt->SetComponentTickEnabled(bShow);
}

bool AMCHubWardrobeStation::ContainsPawn(const APawn* Pawn) const
{
    if(!bEnabled || !InteractionVolume || !IsValid(Pawn) || Pawn->GetWorld()!=GetWorld()) return false;
    const FVector Local=InteractionVolume->GetComponentTransform().InverseTransformPosition(Pawn->GetActorLocation());
    const FVector Extent=InteractionVolume->GetUnscaledBoxExtent();
    return !Local.ContainsNaN() && FMath::Abs(Local.X)<=Extent.X
        && FMath::Abs(Local.Y)<=Extent.Y && FMath::Abs(Local.Z)<=Extent.Z;
}
