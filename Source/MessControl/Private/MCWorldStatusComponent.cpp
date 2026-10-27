#include "MCWorldStatusComponent.h"
#include "MCFoodActor.h"
#include "MCArenaTooth.h"
#include "MCToothStatusComponent.h"
#include "MCFoodBodyComponent.h"
#include "MCNutEnemy.h"
#include "MCNutBoss.h"
#include "MCNutKamikaze.h"
#include "MCGameplayUIFactory.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicsEngine/BodySetup.h"

UMCWorldStatusComponent::UMCWorldStatusComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickGroup=TG_PostPhysics;
    PrimaryComponentTick.TickInterval=.12f;SetIsReplicatedByDefault(false);
}

bool UMCWorldStatusComponent::SupportsOwner() const
{
    return IsValid(GetOwner()) && (Cast<AMCFoodActor>(GetOwner()) || Cast<AMCArenaTooth>(GetOwner())
        || (Cast<AMCNutEnemy>(GetOwner()) && !GetOwner()->IsA<AMCNutBoss>()));
}

void UMCWorldStatusComponent::BeginPlay()
{
    Super::BeginPlay();
    if(GetNetMode()==NM_DedicatedServer || !SupportsOwner()) {SetComponentTickEnabled(false);return;}
    // The polling component stays alive when its UMG root is collapsed. It can
    // wake it after distance/occlusion changes without hidden widget ticking.
    HideLegacyLabel();
    if(Cast<AMCArenaTooth>(GetOwner())) EnsureWidget();
}

void UMCWorldStatusComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if(Screen) {Screen->SetWidget(nullptr);Screen->DestroyComponent();}
    StatusWidget=nullptr;Screen=nullptr;Super::EndPlay(Reason);
}

void UMCWorldStatusComponent::HideLegacyLabel()
{
    UTextRenderComponent* Label=nullptr;
    bool bUseCompact=bEnabled;
    if(auto* Food=Cast<AMCFoodActor>(GetOwner())) {Label=Food->Label;bUseCompact&=!Food->bBrushTool && !Food->ItemName.IsNone();}
    else if(auto* Enemy=Cast<AMCNutEnemy>(GetOwner())) Label=Enemy->Label;
    // HiddenInGame persists even when an inherited enemy Tick calls SetVisibility.
    if(Label) Label->SetHiddenInGame(bUseCompact);
}

bool UMCWorldStatusComponent::ReadSnapshot(FMCWorldStatusSnapshot& Value,FVector& Anchor) const
{
    const auto* World=GetWorld();if(!World || !bEnabled || !SupportsOwner() || GetOwner()->IsHidden()) return false;
    const auto* State=World->GetGameState();const double Now=State?State->GetServerWorldTimeSeconds():World->GetTimeSeconds();
    if(const auto* Food=Cast<AMCFoodActor>(GetOwner())) {
        if(Food->bBrushTool || Food->ItemName.IsNone() || Food->Health<=0 || Food->IsDisposed()
            || Food->Phase==EMCFoodPhase::Absorbing || Food->Phase==EMCFoodPhase::Swallowing
            || Food->Phase==EMCFoodPhase::Equipped || Food->Phase==EMCFoodPhase::Carried || Food->StackCarrier) return false;
        Value.HealthFraction=Food->Health/FMath::Max(1.f,Food->FoodData.Health);
        Value.bShowHealth=!Food->bFragment;
        Value.bHasTimer=Food->FoodData.Kind==EMCFoodKind::Food || Food->FoodData.Kind==EMCFoodKind::Spicy;
        Value.bDrainTimer=Food->FoodData.Kind==EMCFoodKind::Food;
        // Foreign-object fragments have neither a spoil/fuse timer nor a visible
        // HP treatment. Do not create/show an empty status widget for them.
        if(!Value.bShowHealth && !Value.bHasTimer) return false;
        Value.bSpoiled=Food->bSpoiled;
        if(Food->FoodData.Kind==EMCFoodKind::Food) {
            // Carrier preservation perks move the replicated deadline itself.
            // Measure consumed budget from it; applying a perk again here would
            // count preservation twice and actor age would reset cut fragments.
            Value.TimerFraction=Food->SpoilAt>0?FMath::Clamp(1.f-float(FMath::Max(0.,Food->SpoilAt-Now))
                /FMath::Max(.1f,Food->FoodData.SpoilSeconds),0.f,1.f):0.f;
        } else if(Food->FoodData.Kind==EMCFoodKind::Spicy) {
            // Match ArmSpicy's actual finite/clamped fuse duration.
            const float FuseDuration=FMath::IsFinite(Food->FoodData.FuseSeconds)?FMath::Clamp(Food->FoodData.FuseSeconds,1.f,60.f):10.f;
            Value.TimerFraction=Food->FuseEndsAt>0 || Food->bFusePaused
                ?FMath::Clamp(1.f-Food->FuseRemaining()/FuseDuration,0.f,1.f):0.f;
        }
        // Display the replicated deadline in server time, without waiting for
        // a later bSpoiled packet or changing authoritative food state.
        if(Food->FoodData.Kind==EMCFoodKind::Food && Food->SpoilAt>0 && Now>=Food->SpoilAt) Value.bSpoiled=true;
        // Preserve the visual centre in XY. Its rotated local bounding box can
        // overestimate the top of round food, so prefer the authored hull
        // points already retained by FoodBody (no render CPU mesh access).
        Anchor=Food->Visual?Food->Visual->Bounds.Origin+FVector(0,0,Food->Visual->Bounds.BoxExtent.Z+HeightPadding)
            :Food->GetActorLocation()+FVector(0,0,90.f);
        if(auto* MeshBody=Cast<UMCFoodBodyComponent>(Food->Body.Get());MeshBody && MeshBody->HasMeshCollision()) {
            if(const auto* Setup=MeshBody->GetBodySetup()) {
                const FTransform BodyWorld=MeshBody->GetComponentTransform();
                double TopZ=0.;bool bHasPoint=false;
                for(const FKConvexElem& Hull:Setup->AggGeom.ConvexElems) {
                    const FTransform HullLocal=Hull.GetTransform();
                    for(const FVector& Point:Hull.VertexData) {
                        // FoodBody baked mesh pivot and item scale into these
                        // points once. Apply hull pose and actor scale once.
                        const double WorldZ=BodyWorld.TransformPosition(HullLocal.TransformPosition(Point)).Z;
                        if(!FMath::IsFinite(WorldZ)) continue;
                        TopZ=bHasPoint?FMath::Max(TopZ,WorldZ):WorldZ;bHasPoint=true;
                    }
                }
                if(bHasPoint) Anchor.Z=TopZ+HeightPadding;
            }
        }
        return true;
    }
    if(const auto* Tooth=Cast<AMCArenaTooth>(GetOwner())) {
        if(!Tooth->IsAvailable() || !Tooth->Status) return false;
        const auto& Health=Tooth->Status->State;
        if(Health.Health<=0 || Health.Health>=Health.MaxHealth-KINDA_SMALL_NUMBER) return false;
        Value.HealthFraction=FMath::Clamp(Health.Health/FMath::Max(1.f,Health.MaxHealth),0.f,1.f);
        Value.bShowHealth=true;Value.bHasTimer=false;
        Anchor=Tooth->Visual?Tooth->Visual->Bounds.Origin+FVector(0,0,Tooth->Visual->Bounds.BoxExtent.Z+HeightPadding)
            :Tooth->GetActorLocation()+FVector(0,0,120.f+HeightPadding);
        return true;
    }
    if(const auto* Enemy=Cast<AMCNutEnemy>(GetOwner())) {
        if(!Enemy->IsEncounterAlive() || Enemy->Health<=0) return false;
        Value.HealthFraction=Enemy->Health/FMath::Max(1.f,Enemy->Settings.MaxHealth);
        if(const auto* Kamikaze=Cast<AMCNutKamikaze>(Enemy);Kamikaze && Kamikaze->bArmed && !Kamikaze->bExploded) {
            Value.bHasTimer=true;Value.TimerFraction=FMath::Clamp(1.f-Kamikaze->GetFuseSecondsRemaining()
                /FMath::Max(.1f,float(Kamikaze->FuseExplodesAt-Kamikaze->FuseStartedAt)),0.f,1.f);
        }
        Anchor=Enemy->Label?Enemy->Label->GetComponentLocation()+FVector(0,0,10.f)
            :Enemy->GetActorLocation()+FVector(0,0,Enemy->Settings.BodyRadius+HeightPadding);
        return true;
    }
    return false;
}

bool UMCWorldStatusComponent::CanSee(FVector Anchor) const
{
    auto* PC=UGameplayStatics::GetPlayerController(GetWorld(),0);if(!PC || !PC->IsLocalController() || !PC->PlayerCameraManager) return false;
    const FVector Eye=PC->PlayerCameraManager->GetCameraLocation();
    if(FVector::DistSquared(Eye,Anchor)>FMath::Square(FMath::Max(100.f,MaxDistance))) return false;
    FVector2D Pixel;int32 Width=0,Height=0;PC->GetViewportSize(Width,Height);
    if(Width<=0 || Height<=0 || !PC->ProjectWorldLocationToScreen(Anchor,Pixel,true)
        || Pixel.X<0 || Pixel.Y<0 || Pixel.X>Width || Pixel.Y>Height) return false;
    if(bOcclusionTest) {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MCWorldStatusVisibility),false,GetOwner());Query.AddIgnoredActor(PC->GetPawn());
        FHitResult Hit;if(GetWorld()->LineTraceSingleByChannel(Hit,Eye,Anchor,ECC_Visibility,Query)) return false;
    }
    return true;
}

void UMCWorldStatusComponent::EnsureWidget()
{
    if(StatusWidget || !GetOwner()->GetRootComponent()) return;
    auto* PC=UGameplayStatics::GetPlayerController(GetWorld(),0);if(!PC || !PC->IsLocalController()) return;
    StatusWidget=CreateWidget<UMCWorldStatusWidget>(PC,UMCGameplayUIFactory::ResolveClass(TEXT("WorldStatus"),UMCWorldStatusWidget::StaticClass()));
    if(!StatusWidget) return;
    StatusWidget->SetVisibility(ESlateVisibility::Collapsed);
    Screen=NewObject<UWidgetComponent>(GetOwner(),TEXT("WorldStatusScreen"));Screen->SetupAttachment(GetOwner()->GetRootComponent());
    Screen->SetWidgetSpace(EWidgetSpace::Screen);Screen->SetDrawSize(FVector2D(136,34));Screen->SetDrawAtDesiredSize(true);
    Screen->SetPivot(FVector2D(.5f,1.f));Screen->SetCollisionEnabled(ECollisionEnabled::NoCollision);Screen->SetGenerateOverlapEvents(false);
    Screen->SetCanEverAffectNavigation(false);Screen->SetOwnerPlayer(PC->GetLocalPlayer());
    Screen->SetWidget(StatusWidget);Screen->SetTickMode(ETickMode::Automatic);Screen->RegisterComponent();
}

void UMCWorldStatusComponent::TickComponent(float Dt,ELevelTick Tick,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Dt,Tick,Function);if(!SupportsOwner()) return;
    // Prepare the HP root under loading even while every arena tooth is healthy.
    if(Cast<AMCArenaTooth>(GetOwner())) EnsureWidget();
    const double Now=GetWorld()->GetTimeSeconds();
    if(Now>=NextPollAt) {
        NextPollAt=Now+FMath::Clamp(PollSeconds,.05f,1.f);HideLegacyLabel();
        FMCWorldStatusSnapshot Value;FVector Anchor;
        bSourceActive=ReadSnapshot(Value,Anchor) && CanSee(Anchor);
        if(bSourceActive) {CurrentAnchor=Anchor;EnsureWidget();if(StatusWidget) StatusWidget->SetSnapshot(Value);}
        if(StatusWidget) StatusWidget->SetDisplayActive(bSourceActive);
        // Visible anchors follow real physics each frame. Hidden indicators only
        // poll their bounded wake/cull test; UMG stops ticking after its short fade.
        SetComponentTickInterval(bSourceActive?0.f:FMath::Clamp(PollSeconds,.05f,1.f));
    }
    if(Screen && bSourceActive) {
        FMCWorldStatusSnapshot Value;FVector Anchor;
        if(ReadSnapshot(Value,Anchor)) Screen->SetWorldLocation(Anchor);
    }
}
