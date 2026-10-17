#include "MCUlcerProgressWidget.h"

#include "MCGameplayHUD.h"
#include "MCMouthSurface.h"
#include "MCToothpick.h"
#include "MCUIDataLibrary.h"
#include "MCUIVisualStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"

#define LOCTEXT_NAMESPACE "MCUlcerProgress"

void UMCUlcerProgressWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(false);SetVisibility(ESlateVisibility::HitTestInvisible);
    BuildPresentationTree();
    ApplyTheme();
}

void UMCUlcerProgressWidget::BuildPresentationTree()
{
    if(!WidgetTree || ProgressPanel) return;
    if(WidgetTree->RootWidget)
    {
        ProgressPanel=Cast<UCanvasPanel>(WidgetTree->FindWidget(TEXT("TreatmentPanel")));
        ProgressBackground=Cast<UBorder>(WidgetTree->FindWidget(TEXT("TreatmentBackground")));
        ToolIcon=Cast<UMCHUDIcon>(WidgetTree->FindWidget(TEXT("TreatmentTool")));
        PullIcon=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("TreatmentPull")));
        ProgressText=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("TreatmentValue")));
        RingSegments.Reset();
        for(int32 I=0;I<40;++I) RingSegments.Add(Cast<UImage>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("TreatmentRing%02d"),I)))));
        return;
    }
    auto* Scale=WidgetTree->ConstructWidget<UScaleBox>();
    Scale->SetStretch(EStretch::ScaleToFit);WidgetTree->RootWidget=Scale;
    auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(84);Size->SetHeightOverride(84);Scale->SetContent(Size);
    ProgressPanel=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("TreatmentPanel"));Size->SetContent(ProgressPanel);
    auto Add=[&](UWidget* Child,FVector2D Position,FVector2D Dimensions)
    {
        auto* ChildSlot=ProgressPanel->AddChildToCanvas(Child);ChildSlot->SetPosition(Position);ChildSlot->SetSize(Dimensions);return ChildSlot;
    };
    ProgressBackground=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("TreatmentBackground"));
    ProgressBackground->SetPadding(FMargin(0));Add(ProgressBackground,FVector2D(9,9),FVector2D(66,66));
    for(int32 I=0;I<40;++I)
    {
        auto* Segment=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*FString::Printf(TEXT("TreatmentRing%02d"),I)));
        const float Angle=-HALF_PI+2.f*PI*(I+.5f)/40.f;
        const FVector2D Center=FVector2D(42,42)+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*34.f;
        auto* SegmentSlot=Add(Segment,Center,FVector2D(4.3f,7.f));SegmentSlot->SetAlignment(FVector2D(.5,.5));
        Segment->SetRenderTransformPivot(FVector2D(.5,.5));Segment->SetRenderTransformAngle(FMath::RadiansToDegrees(Angle)+90.f);
        RingSegments.Add(Segment);
    }
    ToolIcon=WidgetTree->ConstructWidget<UMCHUDIcon>(UMCHUDIcon::StaticClass(),TEXT("TreatmentTool"));
    Add(ToolIcon,FVector2D(26,17),FVector2D(32,32));
    PullIcon=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("TreatmentPull"));
    PullIcon->SetText(FText::AsCultureInvariant(TEXT("↑")));PullIcon->SetJustification(ETextJustify::Center);
    Add(PullIcon,FVector2D(23,10),FVector2D(38,38));
    ProgressText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("TreatmentValue"));
    ProgressText->SetJustification(ETextJustify::Center);ProgressText->SetAutoWrapText(false);
    Add(ProgressText,FVector2D(12,48),FVector2D(60,24));
}

void UMCUlcerProgressWidget::ApplyTheme()
{
    BuildPresentationTree();
    Super::ApplyTheme();
    const auto* Resolved=GetResolvedTheme();const auto& P=Resolved->Palette;
    if(ProgressBackground)
    {
        ProgressBackground->SetBrush(MCUIVisualStyle::Surface(ProgressBackground,ProgressBackground->Background,Resolved,P.Marine,
            MCUIVisualStyle::SizeFor(ProgressBackground,FVector2D(66,66)),33.f));
        ProgressBackground->SetBrushColor(FLinearColor::White);
    }
    if(PullIcon) PullIcon->SetFont(Resolved->MakeFont(31));
    if(ProgressText) {ProgressText->SetFont(Resolved->MakeFont(16));ProgressText->SetColorAndOpacity(P.OnMarine);}
    PercentPattern=Localized(TEXT("UI.World.ProgressPercent"),LOCTEXT("Percent","{Value}%"));
    PullKey=Localized(TEXT("UI.World.PullKey"),LOCTEXT("PullKey","E"));
    PickaxeKey=Localized(TEXT("UI.World.PickaxeKey"),LOCTEXT("PickaxeKey","2"));
    SprayKey=Localized(TEXT("UI.World.SprayKey"),LOCTEXT("SprayKey","4"));
    LastPercent=MIN_int32;LastPrompt=-1;
    for(const auto& Segment:RingSegments)
        if(Segment) if(auto* SegmentSlot=Cast<UCanvasPanelSlot>(Segment->Slot)) SegmentSlot->SetSize(FVector2D(4.3f,FMath::Clamp(RingThickness,2.f,12.f)));
    RefreshPresentation();
}

void UMCUlcerProgressWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    if(IsDesignTime()) return;
    PulseTime+=FMath::Max(0.f,DeltaSeconds);
    RefreshSource();
    RefreshPresentation();
}

void UMCUlcerProgressWidget::RefreshSource()
{
    const auto* Pick=ToothpickSource.Get();const auto* Patch=Source.Get();
    bToothpick=Pick!=nullptr;bPull=Pick && Pick->State==EMCToothpickState::Impaled;
    bVisible=Pick?(!Pick->IsBroken() && Pick->State!=EMCToothpickState::Falling):(Patch && Patch->bUlcer && !Patch->bTreatmentBlocked && !Patch->IsHealed());
    const float Progress=Pick?(bPull?Pick->PullProgress:0.f):Patch?Patch->Healing:0.f;
    DisplayProgress=FMath::IsFinite(Progress)?FMath::Clamp(Progress,0.f,1.f):0.f;
}

void UMCUlcerProgressWidget::RefreshPresentation()
{
    if(!ProgressPanel) return;
    // Leave the user widget ticking so an unblocked lesion can become visible again.
    ProgressPanel->SetVisibility(bVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
    const auto& P=GetResolvedTheme()->Palette;
    const FLinearColor ProgressTint=bOverrideProgressColor?ProgressColor:P.Accent;
    const FLinearColor IconTint=bToothpick?P.Warning:ProgressTint;
    const float Pulse=.75f+.25f*FMath::Sin(PulseTime*5.f);
    for(int32 I=0;I<RingSegments.Num();++I)
        if(auto* Segment=RingSegments[I].Get())
        {
            const float SegmentFill=FMath::Clamp(DisplayProgress*RingSegments.Num()-I,0.f,1.f);
            Segment->SetColorAndOpacity(FMath::Lerp(P.SurfaceAlt,ProgressTint,SegmentFill));
        }
    if(ToolIcon)
    {
        ToolIcon->ToolSlot=bToothpick?1:3;ToolIcon->Tint=IconTint;ToolIcon->InkTint=P.Ink;
        ToolIcon->SetVisibility(bPull?ESlateVisibility::Hidden:ESlateVisibility::HitTestInvisible);
        ToolIcon->SetRenderOpacity(Pulse);ToolIcon->SetRenderScale(FVector2D(1.f+.1f*Pulse));
        ToolIcon->InvalidateLayoutAndVolatility();
    }
    if(PullIcon) {PullIcon->SetVisibility(bPull?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);PullIcon->SetColorAndOpacity(IconTint);PullIcon->SetRenderOpacity(Pulse);}
    const int32 Percent=DisplayProgress>0.f?FMath::RoundToInt(DisplayProgress*100.f):-1;
    const int32 Prompt=bPull?0:bToothpick?1:2;
    if(ProgressText && (LastPercent!=Percent || LastPrompt!=Prompt))
    {
        LastPercent=Percent;LastPrompt=Prompt;
        const FString Culture=IsDesignTime()?PreviewCulture:FString();
        if(DisplayProgress>0.f)
            ProgressText->SetText(UMCUIDataLibrary::FormatTextForCulture(PercentPattern,
                FFormatNamedArguments{{TEXT("Value"),UMCUIDataLibrary::NumberForCulture(Percent,Culture)}},Culture));
        else ProgressText->SetText(bPull?PullKey:bToothpick?PickaxeKey:SprayKey);
    }
}

void UMCUlcerProgressWidget::ApplyEditorPreview(float Time)
{
    bVisible=PreviewScenario!=EMCGameplayUIPreviewScenario::Empty;
    bToothpick=PreviewState==TEXT("Pull") || PreviewState==TEXT("Pickaxe");
    bPull=PreviewState==TEXT("Pull");
    DisplayProgress=PreviewState==TEXT("Ready") || PreviewState==TEXT("Pickaxe")?0.f:PreviewScenario==EMCGameplayUIPreviewScenario::LowResources?.12f:.62f;
    if(Time>0.f && PreviewState!=TEXT("Ready") && PreviewState!=TEXT("Pickaxe"))
        DisplayProgress=FMath::Fmod(Time/FMath::Max(.1f,PreviewDuration),1.f);
    PulseTime=FMath::Max(0.f,Time);
    Super::ApplyEditorPreview(Time);
}

#undef LOCTEXT_NAMESPACE
