#include "MCStudioSplashWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"

bool UMCStudioSplashSubsystem::ClaimStartupPresentation()
{
    if(bPresented) return false;
    bPresented=true;
    return true;
}

UMCStudioSplashWidget::UMCStudioSplashWidget(const FObjectInitializer& Initializer):Super(Initializer)
{
    SetIsFocusable(true);
    PreviewDuration=3.f;
}

float UMCStudioSplashWidget::RevealAt(float Time,float Start,float End)
{
    if(!FMath::IsFinite(Time) || !FMath::IsFinite(Start) || !FMath::IsFinite(End)) return 0.f;
    const float T=FMath::Clamp((Time-Start)/FMath::Max(.001f,End-Start),0.f,1.f);
    return T*T*(3.f-2.f*T);
}

float UMCStudioSplashWidget::CubicRevealAt(float Time,float Start,float End)
{
    if(!FMath::IsFinite(Time) || !FMath::IsFinite(Start) || !FMath::IsFinite(End)) return 0.f;
    const float T=1.f-FMath::Clamp((Time-Start)/FMath::Max(.001f,End-Start),0.f,1.f);
    return 1.f-T*T*T;
}

void UMCStudioSplashWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(WidgetTree->RootWidget) {BindPresentation();return;}
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("StudioSplashRoot"));
    WidgetTree->RootWidget=Root;
    auto* Backdrop=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("StudioBackdrop"));
    Backdrop->SetBrush(FSlateColorBrush(FLinearColor::White));
    auto* BackSlot=Root->AddChildToCanvas(Backdrop);BackSlot->SetAnchors(FAnchors(0,0,1,1));BackSlot->SetOffsets(FMargin(0));
    auto* Scale=WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),TEXT("StudioLogoFit"));
    Scale->SetStretch(EStretch::ScaleToFit);Scale->SetStretchDirection(EStretchDirection::Both);
    auto* FitSlot=Root->AddChildToCanvas(Scale);FitSlot->SetAnchors(FAnchors(.12f,.18f,.88f,.82f));FitSlot->SetOffsets(FMargin(0));
    auto* Size=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("StudioLogoSize"));
    Size->SetWidthOverride(1024);Size->SetHeightOverride(384);Scale->AddChild(Size);
    auto* Layers=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("StudioLogoLayers"));Size->AddChild(Layers);
    for(const TCHAR* Name:{TEXT("StudioWordmark"),TEXT("StudioBranch"),TEXT("StudioLeaves")})
    {
        auto* Layer=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),Name);
        Layer->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* LayerSlot=Layers->AddChildToCanvas(Layer);LayerSlot->SetSize(FVector2D(1024,384));
    }
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("StudioFallback"));
    Label->SetText(FText::AsCultureInvariant(TEXT("BRANCH9")));Label->SetFont(GetResolvedTheme()->MakeGameplayFont(100,true));
    Label->SetColorAndOpacity(LogoColor);
    auto* LabelSlot=Layers->AddChildToCanvas(Label);LabelSlot->SetAnchors(FAnchors(.5f,.5f));
    LabelSlot->SetAlignment(FVector2D(.5f,.5f));LabelSlot->SetAutoSize(true);
    BindPresentation();
}

void UMCStudioSplashWidget::ConfigureLayer(UImage* Image,UTexture2D* Texture,TObjectPtr<UMaterialInstanceDynamic>& Material)
{
    if(!Image) return;
    Image->SetRenderTransformPivot(FVector2D(.5f,.5f));
    if(!Texture) {Material=nullptr;Image->SetVisibility(ESlateVisibility::Collapsed);return;}
    Image->SetVisibility(ESlateVisibility::HitTestInvisible);
    if(RevealMaterial)
    {
        Material=UMaterialInstanceDynamic::Create(RevealMaterial,this);
        Material->SetTextureParameterValue(TEXT("LogoTexture"),Texture);
        Material->SetVectorParameterValue(TEXT("LogoColor"),LogoColor);
        Image->SetBrushFromMaterial(Material);
    }
    else {Material=nullptr;Image->SetBrushFromTexture(Texture);}
}

void UMCStudioSplashWidget::BindPresentation()
{
    if(!WidgetTree || !WidgetTree->RootWidget) return;
    const bool bNewRoot=BoundRoot.Get()!=WidgetTree->RootWidget;
    BoundRoot=WidgetTree->RootWidget;
    const TCHAR* Base=TEXT("/Game/Gameplay/UI/Brand/Branch9/");
    auto LoadTexture=[Base](const TCHAR* Name) {return LoadObject<UTexture2D>(nullptr,*(FString(Base)+Name+TEXT(".")+Name));};
    if(!WordmarkTexture) WordmarkTexture=LoadTexture(TEXT("T_Branch9_Wordmark"));
    if(!BranchTexture) BranchTexture=LoadTexture(TEXT("T_Branch9_Branch"));
    if(!LeavesTexture) LeavesTexture=LoadTexture(TEXT("T_Branch9_Leaves"));
    if(!RevealMaterial) RevealMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/UI/Materials/M_Branch9_Reveal.M_Branch9_Reveal"));
    if(auto* Backdrop=WidgetTree->FindWidget<UBorder>(TEXT("StudioBackdrop"))) Backdrop->SetBrushColor(BackgroundColor);
    const bool bChanged=bNewRoot || ConfiguredWordmark.Get()!=WordmarkTexture || ConfiguredBranch.Get()!=BranchTexture
        || ConfiguredLeaves.Get()!=LeavesTexture || ConfiguredRevealMaterial.Get()!=RevealMaterial || ConfiguredLogoColor!=LogoColor;
    if(!bChanged) return;
    Wordmark=WidgetTree->FindWidget<UImage>(TEXT("StudioWordmark"));
    Branch=WidgetTree->FindWidget<UImage>(TEXT("StudioBranch"));
    Leaves=WidgetTree->FindWidget<UImage>(TEXT("StudioLeaves"));
    Composition=WidgetTree->FindWidget(TEXT("StudioLogoLayers"));
    if(Composition) Composition->SetRenderTransformPivot(FVector2D(.5f,.5f));
    Fallback=WidgetTree->FindWidget<UTextBlock>(TEXT("StudioFallback"));
    ConfigureLayer(Wordmark,WordmarkTexture,WordmarkMID);ConfigureLayer(Branch,BranchTexture,BranchMID);ConfigureLayer(Leaves,LeavesTexture,LeavesMID);
    ConfiguredWordmark=WordmarkTexture;ConfiguredBranch=BranchTexture;ConfiguredLeaves=LeavesTexture;
    ConfiguredRevealMaterial=RevealMaterial;ConfiguredLogoColor=LogoColor;
    if(WordmarkMID) WordmarkMID->SetScalarParameterValue(TEXT("Softness"),.035416667f);
    if(BranchMID) BranchMID->SetScalarParameterValue(TEXT("Softness"),.022916667f);
    if(Leaves) Leaves->SetRenderTransformPivot(FVector2D(.66f,.52f));
    if(Fallback) Fallback->SetVisibility(WordmarkTexture?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    if(bUseNativeTimeline) SetTimelineTime(PreviewTime);else ApplyStaticPose();
}

void UMCStudioSplashWidget::SynchronizeProperties()
{
    Super::SynchronizeProperties();BindPresentation();
    if(IsDesignTime()) {if(bUseNativeTimeline) SetTimelineTime(PreviewTime);else ApplyStaticPose();}
}

void UMCStudioSplashWidget::ApplyStaticPose()
{
    for(UImage* Image:{Wordmark.Get(),Branch.Get(),Leaves.Get()}) if(Image)
    {Image->SetRenderOpacity(1.f);Image->SetRenderTranslation(FVector2D::ZeroVector);Image->SetRenderTransformAngle(0);}
    for(UMaterialInstanceDynamic* Material:{WordmarkMID.Get(),BranchMID.Get(),LeavesMID.Get()}) if(Material)
    {Material->SetScalarParameterValue(TEXT("Reveal"),1.f);Material->SetScalarParameterValue(TEXT("Opacity"),1.f);}
    if(Composition) {Composition->SetRenderScale(FVector2D(1.f));Composition->SetRenderTranslation(FVector2D::ZeroVector);}
    if(Fallback) Fallback->SetRenderOpacity(1.f);
}

void UMCStudioSplashWidget::SetTimelineTime(float Time)
{
    const float T=FMath::IsFinite(Time)?FMath::Max(0.f,Time):0.f;
    const float SafeDuration=FMath::IsFinite(Duration)?FMath::Max(.5f,Duration):3.f;
    const float Exit=RevealAt(T,SafeDuration-.02f-FMath::Clamp(FadeOutSeconds,.01f,1.f),SafeDuration-.02f);
    const float Fade=1.f-Exit;
    const float Scale=1.f+.012f*(1.f-CubicRevealAt(T,.10f,.68f));
    if(Composition) {Composition->SetRenderScale(FVector2D(Scale));Composition->SetRenderTranslation(FVector2D(19.2f*Exit,0));}
    auto Pose=[&](UImage* Image,UMaterialInstanceDynamic* Material,float Reveal,float Opacity)
    {
        if(!Image) return;
        if(Material) {Material->SetScalarParameterValue(TEXT("Reveal"),Reveal);Material->SetScalarParameterValue(TEXT("Opacity"),Opacity);}
        Image->SetRenderOpacity(Material?1.f:Reveal*Opacity);
    };
    Pose(Wordmark,WordmarkMID,CubicRevealAt(T,WordRevealStart,WordRevealEnd),Fade);
    Pose(Branch,BranchMID,CubicRevealAt(T,BranchRevealStart,BranchRevealEnd),Fade);
    Pose(Leaves,LeavesMID,1.f,RevealAt(T,LeavesRevealStart,LeavesRevealEnd)*Fade);
    if(Branch) Branch->SetRenderTranslation(FVector2D(-8.533333f,3.2f)*(1.f-CubicRevealAt(T,BranchRevealStart,BranchRevealStart+.42f)));
    if(Leaves) Leaves->SetRenderTransformAngle(-1.6f*(1.f-CubicRevealAt(T,LeavesRevealStart,LeavesRevealStart+.42f)));
    if(Fallback) Fallback->SetRenderOpacity(RevealAt(T,WordRevealStart,WordRevealEnd)*Fade);
}

void UMCStudioSplashWidget::NativeConstruct()
{
    Super::NativeConstruct();BindPresentation();StartedAt=-1.;bFinished=false;
    if(bUseNativeTimeline) SetTimelineTime(0.f);else ApplyStaticPose();
}

void UMCStudioSplashWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    if(IsDesignTime() || bFinished) return;
    const double Now=FPlatformTime::Seconds();
    if(StartedAt<0.) StartedAt=Now;
    const float Time=float(Now-StartedAt);
    if(bUseNativeTimeline) SetTimelineTime(Time);
    const float SafeDuration=FMath::IsFinite(Duration)?FMath::Max(.5f,Duration):3.f;
    if(Time>=SafeDuration) {bFinished=true;OnFinished.Broadcast();}
}

void UMCStudioSplashWidget::ApplyEditorPreview(float Time)
{Super::ApplyEditorPreview(Time);BindPresentation();if(bUseNativeTimeline) SetTimelineTime(Time);else ApplyStaticPose();}
void UMCStudioSplashWidget::ResetEditorPreview() {Super::ResetEditorPreview();if(bUseNativeTimeline) SetTimelineTime(1.f);else ApplyStaticPose();}
FReply UMCStudioSplashWidget::NativeOnKeyDown(const FGeometry&,const FKeyEvent&) {return FReply::Handled();}
FReply UMCStudioSplashWidget::NativeOnMouseButtonDown(const FGeometry&,const FPointerEvent&) {return FReply::Handled();}
FReply UMCStudioSplashWidget::NativeOnMouseButtonUp(const FGeometry&,const FPointerEvent&) {return FReply::Handled();}
