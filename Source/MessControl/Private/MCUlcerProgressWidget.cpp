#include "MCUlcerProgressWidget.h"

#include "MCGameplayHUD.h"
#include "MCMouthSurface.h"
#include "MCToothpick.h"
#include "MCUIDataLibrary.h"
#include "MCUIVisualStyle.h"
#include "MCWorldUIShapes.h"
#include "MCCareWidgetPresentation.h"
#include "Components/WidgetComponent.h"
#include "Engine/World.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WidgetSwitcherSlot.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Widgets/SWidget.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"

#define LOCTEXT_NAMESPACE "MCUlcerProgress"

UMCUlcerProgressWidget::UMCUlcerProgressWidget() {bUseVersionedWorldPresentation=true;}

void UMCUlcerProgressWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(false);SetVisibility(ESlateVisibility::HitTestInvisible);
    BuildPresentationTree();
    ApplyTheme();
}

void UMCUlcerProgressWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(ProgressPanel) {BuildWorldPresentationTree();return;}
    if(WidgetTree->RootWidget)
    {
        ProgressPanel=Cast<UCanvasPanel>(WidgetTree->FindWidget(TEXT("TreatmentPanel")));
        ProgressBackground=Cast<UBorder>(WidgetTree->FindWidget(TEXT("TreatmentBackground")));
        ToolIcon=Cast<UMCHUDIcon>(WidgetTree->FindWidget(TEXT("TreatmentTool")));
        PullIcon=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("TreatmentPull")));
        ProgressText=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("TreatmentValue")));
        RingSegments.Reset();
        for(int32 I=0;I<40;++I) RingSegments.Add(Cast<UImage>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("TreatmentRing%02d"),I)))));
        BuildWorldPresentationTree();return;
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
    BuildWorldPresentationTree();
}

void UMCUlcerProgressWidget::BuildWorldPresentationTree()
{
    if(!bUseVersionedWorldPresentation || !WidgetTree) return;
    auto* NewRoot=WidgetTree->FindWidget<USizeBox>(TEXT("WorldCareV2Root"));
    if(!NewRoot) {
        NewRoot=MCWorldUIShapes::Bounds(WidgetTree,TEXT("WorldCareV2Root"),110,104);
        auto* Stack=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("WorldCareV2Stack"));NewRoot->SetContent(Stack);
        // Balance the caption below the source ring with transparent desired space above it.
        // This retains the optical ring center at the existing default centered component pivot.
        auto* Balance=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("WorldCareV2CaptionBalance"));
        Balance->SetHeightOverride(0);Stack->AddChildToVerticalBox(Balance);
        auto* RingBounds=MCWorldUIShapes::Bounds(WidgetTree,TEXT("WorldCareV2RingBounds"),84,84);RingBounds->SetHeightOverride(84);Stack->AddChildToVerticalBox(RingBounds)->SetHorizontalAlignment(HAlign_Center);
        auto* Plate=MCWorldUIShapes::Panel(WidgetTree,TEXT("WorldCareV2Plate"),GetNewWorldPresentationTheme(),EMCUIColorToken::Cream,FMargin(0),42);RingBounds->SetContent(Plate);
        auto* Stage=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("WorldCareV2Stage"));Plate->SetContentForSlot(TEXT("Content"),Stage);
        for(int32 Index=0;Index<40;++Index) {
            auto* Segment=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*FString::Printf(TEXT("WorldCareV2Ring%02d"),Index)));
            const float Angle=-HALF_PI+2.f*PI*(Index+.5f)/40.f;
            auto* SegmentCell=Stage->AddChildToCanvas(Segment);SegmentCell->SetPosition(FVector2D(42,42)+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*34.f);
            SegmentCell->SetAlignment(FVector2D(.5f));SegmentCell->SetSize(FVector2D(4.3f,7));
            Segment->SetRenderTransformPivot(FVector2D(.5f));Segment->SetRenderTransformAngle(FMath::RadiansToDegrees(Angle)+90.f);
            Segment->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,1.5f));
        }
        auto* Tool=WidgetTree->ConstructWidget<UMCHUDIcon>(UMCHUDIcon::StaticClass(),TEXT("WorldCareV2Tool"));
        auto* ToolCell=Stage->AddChildToCanvas(Tool);ToolCell->SetPosition(FVector2D(24,23));ToolCell->SetSize(FVector2D(36));
        auto* Pull=MCWorldUIShapes::Text(WidgetTree,TEXT("WorldCareV2Pull"),38);Pull->SetText(FText::AsCultureInvariant(TEXT("↑")));
        auto* PullCell=Stage->AddChildToCanvas(Pull);PullCell->SetPosition(FVector2D(23,20));PullCell->SetSize(FVector2D(38,44));
        auto* CaptionPlate=MCWorldUIShapes::Panel(WidgetTree,TEXT("WorldCareV2CaptionPlate"),GetNewWorldPresentationTheme(),EMCUIColorToken::Cream,FMargin(10,6),12);
        CaptionPlate->LabelKey=NAME_None;CaptionPlate->ShadowOffset=FVector2D::ZeroVector;
        Stack->AddChildToVerticalBox(CaptionPlate)->SetPadding(FMargin(0,4,0,0));
        auto* Value=MCWorldUIShapes::Text(WidgetTree,TEXT("WorldCareV2Value"),90);Value->SetAutoWrapText(false);
        CaptionPlate->SetContentForSlot(TEXT("Content"),Value);
    }
    InstallWorldPresentation(NewRoot);
    // Care's legacy root originally filled its fixed 84/96 component allocation.
    // The shared helper's center/bottom slot would otherwise shrink the 96px
    // toothpick Current branch back to its 84px design size.
    if(auto* Versions=WidgetTree->FindWidget<UWidgetSwitcher>(TEXT("WorldPresentationVersions")))
        if(auto* CurrentRoot=Versions->GetWidgetAtIndex(0)) if(auto* CareCell=Cast<UWidgetSwitcherSlot>(CurrentRoot->Slot)) {
            CareCell->SetHorizontalAlignment(HAlign_Fill);CareCell->SetVerticalAlignment(VAlign_Fill);
        }
    NewCareCaptionBalance=WidgetTree->FindWidget<USizeBox>(TEXT("WorldCareV2CaptionBalance"));
    NewCareRoot=NewRoot;NewCareTool=WidgetTree->FindWidget<UMCHUDIcon>(TEXT("WorldCareV2Tool"));NewCarePull=WidgetTree->FindWidget<UTextBlock>(TEXT("WorldCareV2Pull"));NewCareValue=WidgetTree->FindWidget<UTextBlock>(TEXT("WorldCareV2Value"));NewCareSegments.Reset();
    for(int32 Index=0;Index<40;++Index) NewCareSegments.Add(WidgetTree->FindWidget<UImage>(FName(*FString::Printf(TEXT("WorldCareV2Ring%02d"),Index))));
}

void UMCUlcerProgressWidget::RefreshWorldPresentation()
{
    if(!NewCareRoot) return;
    NewCareRoot->SetVisibility(bVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
    const auto* NewTheme=GetNewWorldPresentationTheme();if(!NewTheme) return;
    const auto& NewPalette=NewTheme->Palette;
    const FLinearColor ProgressTint=bOverrideProgressColor?ProgressColor:NewPalette.Mint;
    const FLinearColor IconTint=bToothpick?NewPalette.Accent:NewPalette.Marine;
    // The original presenter's actual phase is shared. Style changes never reset PulseTime.
    const float Pulse=.75f+.25f*FMath::Sin(PulseTime*5.f);
    for(int32 Index=0;Index<NewCareSegments.Num();++Index) if(NewCareSegments[Index]) {
        const float SegmentFill=FMath::Clamp(DisplayProgress*NewCareSegments.Num()-Index,0.f,1.f);
        NewCareSegments[Index]->SetColorAndOpacity(FMath::Lerp(NewPalette.SurfaceAlt,ProgressTint,SegmentFill));
    }
    if(NewCareTool) {
        NewCareTool->ToolSlot=bToothpick?1:3;NewCareTool->Tint=IconTint;NewCareTool->InkTint=NewPalette.Marine;
        NewCareTool->SetVisibility(bPull?ESlateVisibility::Hidden:ESlateVisibility::HitTestInvisible);
        NewCareTool->SetRenderOpacity(Pulse);NewCareTool->SetRenderScale(FVector2D(1.f+.1f*Pulse));NewCareTool->InvalidateLayoutAndVolatility();
    }
    if(NewCarePull) {NewCarePull->SetVisibility(bPull?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);NewCarePull->SetColorAndOpacity(IconTint);NewCarePull->SetRenderOpacity(Pulse);}
    // Keep the existing E/2/4 localization behavior in this slice; no new input/rebinding lookup.
    if(NewCareValue && ProgressText) {
        const bool LongFixture=IsDesignTime() && (PreviewState==TEXT("LongText") || PreviewScenario==EMCGameplayUIPreviewScenario::LongText);
        const FText Copy=IsDesignTime() && !PreviewCaptionOverride.IsEmpty()?PreviewCaptionOverride:LongFixture?
            Localized(TEXT("UI.World.LongCaptionFixture"),LOCTEXT("LongCaptionFixture","Длинная локализованная подпись для проверки адаптивной формы виджета")):ProgressText->GetText();
        NewCareValue->SetText(Copy);
    }
    RefreshWorldCaptionLayout();
}

void UMCUlcerProgressWidget::RefreshWorldCaptionLayout()
{
    auto* Bounds=Cast<USizeBox>(NewCareRoot);auto* Plate=WidgetTree?WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("WorldCareV2CaptionPlate")):nullptr;
    if(!Bounds || !Plate || !NewCareValue || !NewCareCaptionBalance || !FSlateApplication::IsInitialized()) return;
    const auto Renderer=FSlateApplication::Get().GetRenderer();if(!Renderer) return;
    const float Maximum=FMath::Max(110.f,MaximumNewCaptionWidth);
    const float FontScale=FMath::Max(.001f,GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale());
    const FSlateFontInfo Font=NewCareValue->GetFont();const FText Copy=NewCareValue->GetText();
    if(bHasNewCaptionMetrics && MeasuredNewCaption.EqualTo(Copy) && MeasuredNewFont==Font
        && FMath::IsNearlyEqual(MeasuredNewMaximum,Maximum,.1f) && FMath::IsNearlyEqual(MeasuredNewScale,FontScale,.001f)) return;
    const FMargin Inset=Plate->ContentPadding;
    const float Advance=Renderer->GetFontMeasureService()->Measure(Copy,Font,FontScale).X/FontScale;
    const float Width=FMath::Clamp(FMath::CeilToFloat(Advance+Inset.Left+Inset.Right+2.f),110.f,Maximum);
    Bounds->SetWidthOverride(Width);
    NewCareValue->SetAutoWrapText(false);NewCareValue->SetWrapTextAt(FMath::Max(32.f,Width-Inset.Left-Inset.Right));
    const auto SlatePlate=Plate->TakeWidget();SlatePlate->MarkPrepassAsDirty();SlatePlate->SlatePrepass(FontScale);
    NewCareCaptionBalance->SetHeightOverride(FMath::CeilToFloat(Plate->GetDesiredSize().Y)+4.f);
    MeasuredNewCaption=Copy;MeasuredNewFont=Font;MeasuredNewMaximum=Maximum;MeasuredNewScale=FontScale;bHasNewCaptionMetrics=true;
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
    if(const auto* NewTheme=GetNewWorldPresentationTheme()) {
        MCWorldUIShapes::TextStyle(NewCarePull,NewTheme,31,EMCUIColorToken::Marine);
        MCWorldUIShapes::TextStyle(NewCareValue,NewTheme,16,EMCUIColorToken::Marine);
        for(const auto& Segment:NewCareSegments) if(Segment) if(auto* SegmentCell=Cast<UCanvasPanelSlot>(Segment->Slot))
            SegmentCell->SetSize(FVector2D(4.3f,FMath::Clamp(RingThickness,2.f,12.f)));
    }
    bHasNewCaptionMetrics=false;RefreshPresentation();
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
    RefreshWorldPresentation();
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

void FMCCareWidgetPresentation::Restore()
{
    // Restore only a value this bridge wrote and still owns. DrawSize, pivot and anchors are never written.
    if(auto* Indicator=BoundIndicator.Get();Indicator && CurrentDrawAtDesiredSize.IsSet()
        && bChangedDrawAtDesiredSize && Indicator->GetDrawAtDesiredSize())
        Indicator->SetDrawAtDesiredSize(CurrentDrawAtDesiredSize.GetValue());
    BoundIndicator.Reset();CurrentDrawAtDesiredSize.Reset();bChangedDrawAtDesiredSize=false;
}
void FMCCareWidgetPresentation::Refresh(UWidgetComponent* Indicator)
{
    if(BoundIndicator.Get()!=Indicator) Restore();
    if(!IsValid(Indicator)) {Restore();return;}
    const auto* Surface=Cast<AMCMouthSurface>(Indicator->GetOwner());
    const auto* Pick=Cast<AMCToothpick>(Indicator->GetOwner());
    const bool KnownOwner=(Surface && Surface->TreatmentIndicator==Indicator) || (Pick && Pick->ActionIndicator==Indicator);
    const auto* Care=Cast<UMCUlcerProgressWidget>(Indicator->GetUserWidgetObject());
    const bool UseDesired=KnownOwner && Indicator->GetWorld() && Indicator->GetWorld()->IsGameWorld()
        && Indicator->GetWorld()->GetNetMode()!=NM_DedicatedServer && Indicator->GetWidgetSpace()==EWidgetSpace::Screen
        && Care && Care->GetWorld()==Indicator->GetWorld() && !Care->IsDesignTime() && Care->IsNewWorldPresentation();
    if(!UseDesired) {Restore();return;}
    if(!CurrentDrawAtDesiredSize.IsSet()) {BoundIndicator=Indicator;CurrentDrawAtDesiredSize=Indicator->GetDrawAtDesiredSize();}
    if(!Indicator->GetDrawAtDesiredSize()) {Indicator->SetDrawAtDesiredSize(true);bChangedDrawAtDesiredSize=true;}
}

#undef LOCTEXT_NAMESPACE
