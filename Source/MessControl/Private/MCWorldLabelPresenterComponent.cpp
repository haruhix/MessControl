#include "MCWorldLabelPresenterComponent.h"

#include "MCGameplayUIFactory.h"
#include "MCGameplayUITheme.h"
#include "MCWorldLabelWidget.h"
#include "MCWorldLabelLocalization.h"
#include "Engine/DataTable.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Components/TextRenderComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Camera/PlayerCameraManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "CollisionQueryParams.h"
#include "Widgets/SWidget.h"

UMCWorldLabelPresenterComponent::UMCWorldLabelPresenterComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bStartWithTickEnabled=true;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork;bTickInEditor=false;
    SetWidgetSpace(EWidgetSpace::Screen);SetDrawAtDesiredSize(true);SetDrawSize(FVector2D(1,1));
    SetWidgetClass(UMCWorldLabelWidget::StaticClass());SetTickMode(ETickMode::Enabled);
    SetPivot(FVector2D(.5,.5));SetWindowFocusable(false);
    SetCollisionEnabled(ECollisionEnabled::NoCollision);SetCastShadow(false);SetCanEverAffectNavigation(false);
    SetVisibility(false);
    PresentationTheme=TSoftObjectPtr<UMCGameplayUITheme>(FSoftObjectPath(TEXT("/Game/Gameplay/UI/V2/Theme/DA_UITheme_V2.DA_UITheme_V2")));
}

bool UMCWorldLabelPresenterComponent::CanMirrorSource() const
{
    const auto* World=GetWorld();const auto* Source=SourceText.Get();
    return World && World->IsGameWorld() && !World->bIsTearingDown && World->GetNetMode()!=NM_DedicatedServer
        && GetWidgetSpace()==EWidgetSpace::Screen && IsValid(Source) && Source->IsRegistered()
        && Source->GetWorld()==World && Source->GetOwner()==GetOwner();
}

void UMCWorldLabelPresenterComponent::InitWidget()
{
    bResourcesReady=false;
    if(!CanMirrorSource() || !FSlateApplication::IsInitialized()) return;
    auto* World=GetWorld();auto* Controller=World->GetFirstPlayerController();
    if(!IsValid(Controller) || !Controller->IsLocalController() || !Controller->GetLocalPlayer()) return;
    auto* ResidentTheme=PresentationTheme.Get();
    if(!UMCWorldLabelWidget::AreThemeResourcesResident(ResidentTheme)) return;
    UClass* RequestedClass=GetWidgetClass();
    if(const auto* Role=ResidentTheme->WidgetClasses.Find(TEXT("WorldLabel"));Role && !Role->IsNull()) {
        RequestedClass=Role->Get();if(!RequestedClass) return;
    }
    constexpr EClassFlags Rejected=CLASS_Abstract|CLASS_Deprecated|CLASS_NewerVersionExists;
    if(!RequestedClass || !RequestedClass->IsChildOf(UMCWorldLabelWidget::StaticClass()) || RequestedClass->HasAnyClassFlags(Rejected)) return;
    const auto* Defaults=RequestedClass->GetDefaultObject<UMCWorldLabelWidget>();
    if(!Defaults || !UMCWorldLabelWidget::AreThemeResourcesResident(ResidentTheme,Defaults->TranslationTable.Get())) return;
    SetOwnerPlayer(Controller->GetLocalPlayer());
    if(!GetUserWidgetObject() || GetUserWidgetObject()->GetClass()!=RequestedClass) {
        RestoreSourceRendering();SetVisibility(false);
        auto* CreatedView=CreateWidget<UMCWorldLabelWidget>(Controller,RequestedClass);
        if(!CreatedView) return;
        // Before SetWidget takes Slate: all forms resolve the resident New theme immediately.
        CreatedView->Theme=ResidentTheme;CreatedView->SetVisibility(ESlateVisibility::HitTestInvisible);
        CreatedView->BuildPresentationTree();CreatedView->ApplyTheme();SetWidget(CreatedView);
    }
    if(auto* ExistingView=Cast<UMCWorldLabelWidget>(GetUserWidgetObject())) {
        if(ExistingView->Theme!=ResidentTheme) {ExistingView->Theme=ResidentTheme;ExistingView->ApplyTheme();}
        // UWidget caches weak Slate handles; retain this hidden view so preparation survives until first show.
        PreparedViewSlate=ExistingView->TakeWidget();
        bResourcesReady=PreparedViewSlate.IsValid();
    }
}

void UMCWorldLabelPresenterComponent::BeginPlay()
{
    Super::BeginPlay();
    if(!GetWorld() || GetWorld()->GetNetMode()==NM_DedicatedServer) {SetComponentTickEnabled(false);return;}
    if(GetOwner()) AddTickPrerequisiteActor(GetOwner());
    StartObservingPresentation();RefreshSourceLabel();
}

void UMCWorldLabelPresenterComponent::OnRegister()
{
    Super::OnRegister();
    if(HasBegunPlay()) {
        if(GetOwner()) AddTickPrerequisiteActor(GetOwner());
        StartObservingPresentation();RefreshSourceLabel();
    }
}

void UMCWorldLabelPresenterComponent::StartObservingPresentation()
{
    const auto* World=GetWorld();
    if(!PresentationChangedHandle.IsValid() && World && World->IsGameWorld() && !World->bIsTearingDown && World->GetNetMode()!=NM_DedicatedServer)
        PresentationChangedHandle=UMCGameplayUIFactory::OnPresentationChanged().AddWeakLambda(this,[this](){RefreshSourceLabel();});
}

void UMCWorldLabelPresenterComponent::StopObservingPresentation()
{
    UMCGameplayUIFactory::OnPresentationChanged().Remove(PresentationChangedHandle);PresentationChangedHandle.Reset();
}

void UMCWorldLabelPresenterComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    StopObservingPresentation();StopObservingSourceCopy();SourceCopyRefresh.Unbind();RestoreSourceRendering();Super::EndPlay(Reason);PreparedViewSlate.Reset();
}

void UMCWorldLabelPresenterComponent::OnUnregister()
{
    StopObservingPresentation();StopObservingSourceCopy();RestoreSourceRendering();Super::OnUnregister();PreparedViewSlate.Reset();
}

void UMCWorldLabelPresenterComponent::SetSourceCopyRefresh(const FSimpleDelegate& Callback)
{
    SourceCopyRefresh=Callback;bSourceCopyRefreshPending=true;
}
void UMCWorldLabelPresenterComponent::RecordNativeSourceCopy(const FText& ActualCopy)
{
    LastNativeSourceCopy=ActualCopy;bNativeSourceCopyOwned=true;
}
void UMCWorldLabelPresenterComponent::HandleSourceCopyTableChanged()
{
    bSourceCopyRefreshPending=true;
}
void UMCWorldLabelPresenterComponent::StopObservingSourceCopy()
{
    if(auto* Table=ObservedCopyTable.Get()) Table->OnDataTableChanged().Remove(CopyTableChangedHandle);
    CopyTableChangedHandle.Reset();ObservedCopyTable.Reset();ObservedCopyCulture.Reset();bSourceCopyRefreshPending=true;
}
void UMCWorldLabelPresenterComponent::RefreshSourceCopy()
{
    if(!SourceCopyRefresh.IsBound()) return;
    UDataTable* ResidentCopy=MCWorldLabelLocalization::ResidentTable(SourceText.Get());
    if(ObservedCopyTable.Get()!=ResidentCopy) {
        if(auto* Previous=ObservedCopyTable.Get()) Previous->OnDataTableChanged().Remove(CopyTableChangedHandle);
        CopyTableChangedHandle.Reset();ObservedCopyTable=ResidentCopy;bSourceCopyRefreshPending=true;
        if(ResidentCopy) CopyTableChangedHandle=ResidentCopy->OnDataTableChanged().AddUObject(this,&ThisClass::HandleSourceCopyTableChanged);
    }
    const FString CurrentLanguage=FInternationalization::Get().GetCurrentLanguage()->GetName();
    if(ObservedCopyCulture!=CurrentLanguage) {ObservedCopyCulture=CurrentLanguage;bSourceCopyRefreshPending=true;}
    if(bSourceCopyRefreshPending) {
        bSourceCopyRefreshPending=false;
        // An authored/BP caption written after the last native event remains authoritative.
        // Only the original next native Apply may reclaim that copy ownership.
        if(bNativeSourceCopyOwned && SourceText && SourceText->Text.IdenticalTo(LastNativeSourceCopy)) SourceCopyRefresh.ExecuteIfBound();
        else bNativeSourceCopyOwned=false;
    }
}

bool UMCWorldLabelPresenterComponent::ShouldShowSource() const
{
    const auto* Source=SourceText.Get();const auto* Owner=GetOwner();
    if(!CanMirrorSource() || !Owner || Owner->IsHidden() || !Source->IsVisible() || Source->Text.IsEmpty() || Source->TextRenderColor.A==0) return false;
    const auto* Controller=GetWorld()->GetFirstPlayerController();
    if(!Controller || !Controller->PlayerCameraManager) return false;
    const auto* ViewTarget=Controller->GetViewTarget();
    const bool IsOwnerView=ViewTarget && (Owner==ViewTarget || Owner->IsOwnedBy(ViewTarget));
    if((Source->bOnlyOwnerSee && !IsOwnerView) || (Source->bOwnerNoSee && IsOwnerView)) return false;
    const float Distance=FVector::Distance(Controller->PlayerCameraManager->GetCameraLocation(),Source->GetComponentLocation());
    return Distance>=Source->GetMinDrawDistance() && (Source->CachedMaxDrawDistance<=0 || Distance<=Source->CachedMaxDrawDistance);
}

bool UMCWorldLabelPresenterComponent::UpdateProjectedPresentation(UMCWorldLabelWidget* NewView)
{
    bLastProjectionInView=false;LastPerspectiveScale=0.f;LastProjectedLinePixels=0.f;
    if(!NewView || !CanMirrorSource() || NewView->GetPreparedLineHeight()<=0.f) return false;
    const auto* Controller=GetWorld()->GetFirstPlayerController();if(!Controller || !Controller->PlayerCameraManager) return false;
    FVector Eye;FRotator ViewRotation;Controller->GetPlayerViewPoint(Eye,ViewRotation);
    const FVector Anchor=SourceText->GetComponentLocation();
    if(Anchor.ContainsNaN() || Eye.ContainsNaN() || ViewRotation.ContainsNaN()) return false;
    if(FVector::DotProduct(Anchor-Eye,ViewRotation.Vector())<=0.f) return false;
    const float WorldLineHeight=float(FMath::Abs(double(SourceText->WorldSize)*SourceText->YScale*SourceText->GetComponentScale().Z));
    if(!FMath::IsFinite(WorldLineHeight) || WorldLineHeight<=0.f) return false;
    // Preserve the original plane's projected height, including the ready-zone floor label at a grazing angle.
    const FVector SourceUp=SourceText->GetUpVector();
    const FVector TopWorld=Anchor+SourceUp*(WorldLineHeight*GetPivot().Y);
    const FVector BottomWorld=Anchor-SourceUp*(WorldLineHeight*(1.f-GetPivot().Y));
    FVector2D TopPixel,BottomPixel,AnchorPixel;
    if(!Controller->ProjectWorldLocationToScreen(TopWorld,TopPixel,false)
        || !Controller->ProjectWorldLocationToScreen(BottomWorld,BottomPixel,false)
        || !Controller->ProjectWorldLocationToScreen(Anchor,AnchorPixel,false)) return false;
    if(TopPixel.ContainsNaN() || BottomPixel.ContainsNaN() || AnchorPixel.ContainsNaN()) return false;
    LastProjectedLinePixels=float((BottomPixel-TopPixel).Size());
    if(!FMath::IsFinite(LastProjectedLinePixels) || LastProjectedLinePixels<FMath::Max(.001f,MinimumProjectedLinePixels)) return false;
    // Same pixel -> viewport-local conversion as SWorldWidgetScreenLayer. Do not mix desktop/tick coordinates.
    FVector2D TopLocal,BottomLocal,AnchorLocal;
    USlateBlueprintLibrary::ScreenToViewport(Controller,TopPixel,TopLocal);
    USlateBlueprintLibrary::ScreenToViewport(Controller,BottomPixel,BottomLocal);
    USlateBlueprintLibrary::ScreenToViewport(Controller,AnchorPixel,AnchorLocal);
    const FVector2D ViewportExtent=UWidgetLayoutLibrary::GetViewportWidgetGeometry(this).GetLocalSize();
    if(TopLocal.ContainsNaN() || BottomLocal.ContainsNaN() || AnchorLocal.ContainsNaN() || ViewportExtent.X<=1 || ViewportExtent.Y<=1) return false;
    const float PerspectiveScale=float((BottomLocal-TopLocal).Size())/NewView->GetPreparedLineHeight();
    if(!FMath::IsFinite(PerspectiveScale) || PerspectiveScale<=0.f) return false;
    const FVector2D LabelExtent=NewView->GetDesiredSize()*PerspectiveScale;
    const FVector2D RectMinimum=AnchorLocal-LabelExtent*GetPivot(),RectMaximum=RectMinimum+LabelExtent;
    if(LabelExtent.ContainsNaN() || LabelExtent.X<=0 || LabelExtent.Y<=0
        || RectMaximum.X<0 || RectMaximum.Y<0 || RectMinimum.X>ViewportExtent.X || RectMinimum.Y>ViewportExtent.Y) return false;
    // This is perspective, not a font-fit mechanism. All authored text/layout metrics stay fixed in local UMG units.
    NewView->SetRenderTransformPivot(GetPivot());NewView->SetRenderScale(FVector2D(PerspectiveScale,PerspectiveScale));
    LastPerspectiveScale=PerspectiveScale;bLastProjectionInView=true;return true;
}

bool UMCWorldLabelPresenterComponent::RefreshOcclusion()
{
    auto* World=GetWorld();const auto* Controller=World?World->GetFirstPlayerController():nullptr;
    if(!World || !World->GetPhysicsScene() || !Controller || !CanMirrorSource()) {bOcclusionCacheValid=false;return false;}
    auto* CurrentViewTarget=Controller->GetViewTarget();
    if(OcclusionSource.Get()!=SourceText.Get() || OcclusionViewTarget.Get()!=CurrentViewTarget) {
        bOcclusionCacheValid=false;OcclusionSource=SourceText;OcclusionViewTarget=CurrentViewTarget;
    }
    const double Now=World->GetRealTimeSeconds();
    if(Now>=NextOcclusionTestAt) {
        FVector Eye;FRotator ViewRotation;Controller->GetPlayerViewPoint(Eye,ViewRotation);
        FCollisionQueryParams LabelQuery(SCENE_QUERY_STAT(MCWorldLabelOcclusion),true,SourceText->GetOwner());
        LabelQuery.AddIgnoredComponent(SourceText.Get());
        if(APawn* ViewPawn=Controller->GetPawn()) LabelQuery.AddIgnoredActor(ViewPawn);
        if(IsValid(CurrentViewTarget)) LabelQuery.AddIgnoredActor(CurrentViewTarget);
        // Query only. Preserve the exact native anchor as the endpoint; no damage/target/range state is changed.
        bLastOccluded=World->LineTraceTestByChannel(Eye,SourceText->GetComponentLocation(),ECC_Visibility,LabelQuery);
        bOcclusionCacheValid=true;NextOcclusionTestAt=Now+FMath::Clamp(OcclusionRefreshSeconds,.1f,.2f);
    }
    return bOcclusionCacheValid;
}

bool UMCWorldLabelPresenterComponent::AcquireSourceRendering()
{
    auto* Source=SourceText.Get();if(!IsValid(Source) || bLeaseConflict) return false;
    if(LeasedSource.Get()!=Source) {
        RestoreSourceRendering();
        // An authored non-rendering source must not become a visible New card.
        if(!Source->bRenderInMainPass) return false;
        LeasedSource=Source;bSavedMainPass=Source->bRenderInMainPass;bSavedDepthPass=Source->bRenderInDepthPass;
        bSavedCustomDepth=Source->bRenderCustomDepth;bSavedShadow=Source->CastShadow;
        if(Source->bRenderInMainPass) Source->SetRenderInMainPass(false);
        if(Source->bRenderInDepthPass) Source->SetRenderInDepthPass(false);
        if(Source->bRenderCustomDepth) Source->SetRenderCustomDepth(false);
        if(Source->CastShadow) Source->SetCastShadow(false);
    } else if(Source->bRenderInMainPass || Source->bRenderInDepthPass || Source->bRenderCustomDepth || Source->CastShadow) {
        // Another owner changed a leased rendering flag. Yield rather than overwrite it every tick.
        RestoreSourceRendering();bLeaseConflict=true;return false;
    }
    return LeasedSource.Get()==Source;
}

void UMCWorldLabelPresenterComponent::RestoreSourceRendering()
{
    if(auto* Previous=LeasedSource.Get()) {
        if(bSavedMainPass && !Previous->bRenderInMainPass) Previous->SetRenderInMainPass(true);
        if(bSavedDepthPass && !Previous->bRenderInDepthPass) Previous->SetRenderInDepthPass(true);
        if(bSavedCustomDepth && !Previous->bRenderCustomDepth) Previous->SetRenderCustomDepth(true);
        if(bSavedShadow && !Previous->CastShadow) Previous->SetCastShadow(true);
    }
    LeasedSource.Reset();bSavedMainPass=bSavedDepthPass=bSavedCustomDepth=bSavedShadow=false;
}

bool UMCWorldLabelPresenterComponent::HasPreparedPresentation() const
{
    const auto* NewView=Cast<UMCWorldLabelWidget>(GetUserWidgetObject());
    return bResourcesReady && NewView && NewView->HasPreparedPresentation();
}

void UMCWorldLabelPresenterComponent::RefreshSourceLabel()
{
    if(!CanMirrorSource()) {bResourcesReady=false;bOcclusionCacheValid=false;bLastProjectionInView=false;RestoreSourceRendering();SetVisibility(false);return;}
    bSourceSeen=true;InitWidget();RefreshSourceCopy();
    auto* NewView=Cast<UMCWorldLabelWidget>(GetUserWidgetObject());
    if(NewView) {
        NewView->SetPresentation(SourceText->Text,FLinearColor(SourceText->TextRenderColor));
        NewView->PreparePresentation();
    }
    if(UMCGameplayUIFactory::GetPresentationVersion()!=EMCUIVersion::New) {
        RestoreSourceRendering();bLeaseConflict=false;bOcclusionCacheValid=false;bLastProjectionInView=false;SetVisibility(false);return;
    }
    if(!HasPreparedPresentation()) {RestoreSourceRendering();SetVisibility(false);return;}
    const auto HAlign=SourceText->HorizontalAlignment;const auto VAlign=SourceText->VerticalAlignment;
    const FVector2D LabelPivot(HAlign==EHTA_Left?0.f:(HAlign==EHTA_Right?1.f:.5f),
        VAlign==EVRTA_TextCenter?.5f:((VAlign==EVRTA_TextTop || VAlign==EVRTA_QuadTop)?0.f:1.f));
    if(GetPivot()!=LabelPivot) SetPivot(LabelPivot);
    // Source Visible/Hidden are untouched. Projection and occlusion affect only this cosmetic view.
    const bool Candidate=ShouldShowSource() && UpdateProjectedPresentation(NewView);
    if(!Candidate) {bOcclusionCacheValid=false;bLastProjectionInView=false;}
    else if(!RefreshOcclusion()) {RestoreSourceRendering();SetVisibility(false);return;} // native depth fallback while untested/unavailable
    if(!AcquireSourceRendering()) {RestoreSourceRendering();SetVisibility(false);return;}
    const bool Show=Candidate && bOcclusionCacheValid && !bLastOccluded;if(IsVisible()!=Show) SetVisibility(Show);
}

void UMCWorldLabelPresenterComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    if(bDestroyWhenSourceGone && bSourceSeen && !IsValid(SourceText.Get())) {DestroyComponent();return;}
    RefreshSourceLabel();Super::TickComponent(DeltaTime,TickType,TickFunction);
}
