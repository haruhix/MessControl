#include "MCBossHealthWidget.h"
#include "MCUIDataLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/Images/SImage.h"
#include "RenderingThread.h"
#include "UObject/StrongObjectPtr.h"

#define LOCTEXT_NAMESPACE "MCBossHealth"

namespace MCBossHealthPrivate
{
    constexpr int32 MaskResolution=256;
    constexpr float MaskPadding=24.f;
    struct FPreparedMask
    {
        TWeakObjectPtr<UTexture2D> Source;
        const FTextureResource* Resource=nullptr;
        FVector2D Size;
        FVector2D Padding;
        TStrongObjectPtr<UTextureRenderTarget2D> Field;
    };

    // A distance field is derived from the current PNG on demand. It is never
    // an authored frame asset. Cache it so normal HUD updates use one lookup.
    UTextureRenderTarget2D* PrepareMask(UTexture2D* Source,UMaterialInterface* Material,const FVector2D& DrawSize)
    {
        if(!Source || !Source->GetResource() || !Material || !FSlateApplication::IsInitialized()) return nullptr;
        static TArray<FPreparedMask> Cache;
        FLinearColor PaddingColor(12.f,6.f,0.f,0.f);
        Material->GetVectorParameterValue(FHashedMaterialParameterInfo(FName(TEXT("IconPadding"))),PaddingColor);
        const FVector2D Padding(FMath::Max(.5f,PaddingColor.R),FMath::Max(.5f,PaddingColor.G));
        Cache.RemoveAll([](const FPreparedMask& Entry){return !Entry.Source.IsValid();});
        for(const auto& Entry:Cache)
            if(Entry.Source.Get()==Source && Entry.Resource==Source->GetResource() && Entry.Size.Equals(DrawSize,.01) && Entry.Padding.Equals(Padding,.01)) return Entry.Field.Get();
        Source->WaitForPendingInitOrStreaming();
        Material->EnsureIsComplete();
        auto MakeTarget=[]()
        {
            TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>());
            Target->ClearColor=FLinearColor::Transparent;Target->TargetGamma=1.f;Target->Filter=TF_Nearest;
            Target->InitCustomFormat(MaskResolution,MaskResolution,PF_FloatRGBA,true);Target->UpdateResourceImmediate(true);
            return Target;
        };
        auto Ping=MakeTarget(),Pong=MakeTarget();
        TStrongObjectPtr<UMaterialInstanceDynamic> Seed(UMaterialInstanceDynamic::Create(Material,GetTransientPackage()));
        TStrongObjectPtr<UMaterialInstanceDynamic> Jump(UMaterialInstanceDynamic::Create(Material,GetTransientPackage()));
        const FLinearColor FieldSize(DrawSize.X+MaskPadding*2.f,DrawSize.Y+MaskPadding*2.f,0.f,0.f);
        for(auto* Pass:{Seed.Get(),Jump.Get()})
        {
            Pass->SetVectorParameterValue(TEXT("FieldSize"),FieldSize);
            Pass->SetScalarParameterValue(TEXT("MaskPadding"),MaskPadding);
            Pass->SetScalarParameterValue(TEXT("FieldResolution"),MaskResolution);
            Pass->SetTextureParameterValue(TEXT("IconMask"),Source);
            Pass->SetVectorParameterValue(TEXT("IconPadding"),FLinearColor(Padding.X,Padding.Y,0.f,0.f));
        }
        Seed->SetScalarParameterValue(TEXT("Operation"),1.f);Jump->SetScalarParameterValue(TEXT("Operation"),2.f);
        auto* Renderer=new FWidgetRenderer(false,true);
        FSlateBrush Brush;Brush.DrawAs=ESlateBrushDrawType::Image;Brush.ImageSize=FVector2D(MaskResolution);Brush.TintColor=FLinearColor::White;
        auto Draw=[&](UTextureRenderTarget2D* Target,UMaterialInstanceDynamic* Pass)
        {
            Brush.SetResourceObject(Pass);
            Renderer->DrawWidget(Target,SNew(SImage).Image(&Brush),FVector2D(MaskResolution),0.f,false);
            // Each jump consumes the preceding pass; keep UI data linear and
            // complete before reusing the material's input render target.
            FlushRenderingCommands();
        };
        Draw(Ping.Get(),Seed.Get());
        Jump->SetScalarParameterValue(TEXT("MetricMode"),1.f);
        for(int32 Step=MaskResolution/2;Step>=1;Step/=2)
        {
            Jump->SetTextureParameterValue(TEXT("DistanceMask"),Ping.Get());Jump->SetScalarParameterValue(TEXT("FieldStep"),Step);
            Draw(Pong.Get(),Jump.Get());Swap(Ping,Pong);
        }
        // Seed the expanded black recess in its elliptical metric, then compute
        // Euclidean distances to that recess. This keeps the metal width smooth
        // at nearest-point boundaries and avoids copied alpha-ring scallops.
        Seed->SetScalarParameterValue(TEXT("Operation"),3.f);Seed->SetTextureParameterValue(TEXT("DistanceMask"),Ping.Get());
        Draw(Pong.Get(),Seed.Get());Swap(Ping,Pong);
        Jump->SetScalarParameterValue(TEXT("MetricMode"),0.f);
        for(int32 Step=MaskResolution/2;Step>=1;Step/=2)
        {
            Jump->SetTextureParameterValue(TEXT("DistanceMask"),Ping.Get());Jump->SetScalarParameterValue(TEXT("FieldStep"),Step);
            Draw(Pong.Get(),Jump.Get());Swap(Ping,Pong);
        }
        BeginCleanup(Renderer);
        if(Cache.Num()>=16) Cache.RemoveAt(0);
        auto& Entry=Cache.AddDefaulted_GetRef();Entry.Source=Source;Entry.Resource=Source->GetResource();Entry.Size=DrawSize;Entry.Padding=Padding;Entry.Field=MoveTemp(Ping);
        UE_LOG(LogTemp,Display,TEXT("MC_BOSS_MASK_CACHE %s %dx%d size=%.2fx%.2f passes=18"),*Source->GetName(),MaskResolution,MaskResolution,DrawSize.X,DrawSize.Y);
        return Entry.Field.Get();
    }
    FName Name(int32 Index,const TCHAR* Part) {return FName(*FString::Printf(TEXT("Boss%d%s"),Index+1,Part));}
    USizeBox* Width(UWidgetTree* Tree,UWidget* Child,float Value) {auto* Box=Tree->ConstructWidget<USizeBox>();Box->SetWidthOverride(Value);Box->SetContent(Child);return Box;}
}

void UMCBossHealthWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();SetIsFocusable(false);BuildPresentationTree();SetVisibility(ESlateVisibility::HitTestInvisible);
    // Warm the small stock portrait set when gameplay UI is created, before
    // an encounter needs its first bar. Artist replacements follow the same path.
    if(!IsDesignTime()) if(auto* Material=FrameMaterial.LoadSynchronous())
    {
        int32 Count=0;
        for(const auto& Portrait:GetResolvedTheme()->BossPortraits)
        {
            if(Count++>=3) break;
            if(auto* Texture=Portrait.Value.LoadSynchronous())
            {
                const FVector2D Size(Texture->GetSizeX(),Texture->GetSizeY());
                const float Fit=FMath::Min(PortraitBoxSize.X/FMath::Max(1.,Size.X),PortraitBoxSize.Y/FMath::Max(1.,Size.Y));
                MCBossHealthPrivate::PrepareMask(Texture,Material,Size*Fit);
            }
        }
    }
}

void UMCBossHealthWidget::BuildPresentationTree()
{
    if(!WidgetTree || HealthPanel) return;
    if(WidgetTree->RootWidget)
    {
        HealthPanel=Cast<UVerticalBox>(WidgetTree->FindWidget(TEXT("BossStack")));
        TopBar=Cast<UBorder>(WidgetTree->FindWidget(TEXT("BossLetterboxTop")));BottomBar=Cast<UBorder>(WidgetTree->FindWidget(TEXT("BossLetterboxBottom")));
        for(int32 Index=0;Index<3;++Index)
        {
            auto& Row=Rows.AddDefaulted_GetRef();
            Row.Panel=Cast<UBorder>(WidgetTree->FindWidget(MCBossHealthPrivate::Name(Index,TEXT("Panel"))));
            Row.Name=Cast<UTextBlock>(WidgetTree->FindWidget(MCBossHealthPrivate::Name(Index,TEXT("Name"))));
            Row.Value=Cast<UTextBlock>(WidgetTree->FindWidget(MCBossHealthPrivate::Name(Index,TEXT("Value"))));
            Row.Current=Cast<UProgressBar>(WidgetTree->FindWidget(MCBossHealthPrivate::Name(Index,TEXT("Health"))));
            Row.Loss=Cast<UProgressBar>(WidgetTree->FindWidget(MCBossHealthPrivate::Name(Index,TEXT("Loss"))));
            Row.ShieldPanel=Cast<UBorder>(WidgetTree->FindWidget(MCBossHealthPrivate::Name(Index,TEXT("ShieldPanel"))));
            Row.ShieldValue=Cast<UTextBlock>(WidgetTree->FindWidget(MCBossHealthPrivate::Name(Index,TEXT("ShieldValue"))));
            Row.ShieldBar=Cast<UProgressBar>(WidgetTree->FindWidget(MCBossHealthPrivate::Name(Index,TEXT("Shield"))));
            Row.Portrait=Cast<UImage>(WidgetTree->FindWidget(MCBossHealthPrivate::Name(Index,TEXT("Portrait"))));
            Row.PortraitFallback=Cast<UTextBlock>(WidgetTree->FindWidget(MCBossHealthPrivate::Name(Index,TEXT("Fallback"))));
            Row.Frame=Cast<UImage>(WidgetTree->FindWidget(MCBossHealthPrivate::Name(Index,TEXT("Frame"))));
            Row.ShieldIcon=Cast<UImage>(WidgetTree->FindWidget(MCBossHealthPrivate::Name(Index,TEXT("ShieldIcon"))));
            Row.Tail=Cast<UImage>(WidgetTree->FindWidget(MCBossHealthPrivate::Name(Index,TEXT("Tail"))));
        }
        return;
    }
    // The viewport supplies project DPI; keep the stack anchored to the true
    // viewport centre rather than a centred fixed-size reference canvas.
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("BossDesignRoot"));WidgetTree->RootWidget=Root;
    HealthPanel=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("BossStack"));
    auto* PanelSlot=Root->AddChildToCanvas(HealthPanel);PanelSlot->SetAnchors(FAnchors(.5f,0));PanelSlot->SetAlignment(FVector2D(.5f,0));PanelSlot->SetPosition(FVector2D(0,18));PanelSlot->SetSize(FVector2D(816,212));
    for(int32 Index=0;Index<3;++Index)
    {
        auto& Row=Rows.AddDefaulted_GetRef();
        Row.Panel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),MCBossHealthPrivate::Name(Index,TEXT("Panel")));Row.Panel->SetPadding(FMargin(0));
        auto* Height=WidgetTree->ConstructWidget<USizeBox>();Height->SetHeightOverride(64.3f);Height->SetContent(Row.Panel);
        HealthPanel->AddChildToVerticalBox(Height);
        auto* Content=WidgetTree->ConstructWidget<UCanvasPanel>();Row.Panel->SetContent(Content);
        auto Place=[Content](UWidget* Child,FVector2D Position,FVector2D Size,int32 Z=0)
        {auto* Slot=Content->AddChildToCanvas(Child);Slot->SetPosition(Position);Slot->SetSize(Size);Slot->SetZOrder(Z);return Slot;};
        Row.Frame=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),MCBossHealthPrivate::Name(Index,TEXT("Frame")));
        Place(Row.Frame,FVector2D(-24,-12),FVector2D(840,108));
        Row.Portrait=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),MCBossHealthPrivate::Name(Index,TEXT("Portrait")));
        auto* PortraitFit=WidgetTree->ConstructWidget<UScaleBox>();PortraitFit->SetStretch(EStretch::ScaleToFit);PortraitFit->SetContent(Row.Portrait);
        Place(PortraitFit,FVector2D(8.03f,5.74f),FVector2D(73.45f,73.45f),2);
        Row.PortraitFallback=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),MCBossHealthPrivate::Name(Index,TEXT("Fallback")));Row.PortraitFallback->SetText(FText::AsCultureInvariant(TEXT("!")));Row.PortraitFallback->SetJustification(ETextJustify::Center);
        Place(Row.PortraitFallback,FVector2D(12,12),FVector2D(58,52),3);
        Row.Name=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),MCBossHealthPrivate::Name(Index,TEXT("Name")));Row.Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);Row.Name->SetClipping(EWidgetClipping::ClipToBounds);
        Place(Row.Name,FVector2D(97,13),FVector2D(230,47),1);
        auto* Layers=WidgetTree->ConstructWidget<UOverlay>();
        Place(Layers,FVector2D(340,25),FVector2D(Index==0?368:196,32),1);
        Row.Loss=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),MCBossHealthPrivate::Name(Index,TEXT("Loss")));auto* LossSlot=Layers->AddChildToOverlay(Row.Loss);LossSlot->SetHorizontalAlignment(HAlign_Fill);LossSlot->SetVerticalAlignment(VAlign_Fill);
        Row.Current=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),MCBossHealthPrivate::Name(Index,TEXT("Health")));auto* CurrentSlot=Layers->AddChildToOverlay(Row.Current);CurrentSlot->SetHorizontalAlignment(HAlign_Fill);CurrentSlot->SetVerticalAlignment(VAlign_Fill);
        Row.Value=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),MCBossHealthPrivate::Name(Index,TEXT("Value")));Row.Value->SetJustification(ETextJustify::Right);
        Place(Row.Value,FVector2D(Index==0?724:552,10),FVector2D(74,52),1);
        Row.Tail=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),MCBossHealthPrivate::Name(Index,TEXT("Tail")));
        Place(Row.Tail,FVector2D(636,25),FVector2D(164,32),1);
        Row.Tail->SetVisibility(ESlateVisibility::Collapsed);
        Row.ShieldPanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),MCBossHealthPrivate::Name(Index,TEXT("ShieldPanel")));Row.ShieldPanel->SetPadding(FMargin(0));
        Place(Row.ShieldPanel,FVector2D(636,13),FVector2D(164,48),1);
        auto* ShieldCanvas=WidgetTree->ConstructWidget<UCanvasPanel>();Row.ShieldPanel->SetContent(ShieldCanvas);
        Row.ShieldIcon=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),MCBossHealthPrivate::Name(Index,TEXT("ShieldIcon")));
        auto* IconSlot=ShieldCanvas->AddChildToCanvas(Row.ShieldIcon);IconSlot->SetPosition(FVector2D(0,2));IconSlot->SetSize(FVector2D(33,41));
        Row.ShieldValue=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),MCBossHealthPrivate::Name(Index,TEXT("ShieldValue")));
        auto* ShieldTextSlot=ShieldCanvas->AddChildToCanvas(Row.ShieldValue);ShieldTextSlot->SetPosition(FVector2D(54,0));ShieldTextSlot->SetSize(FVector2D(110,23));
        Row.ShieldBar=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),MCBossHealthPrivate::Name(Index,TEXT("Shield")));
        auto* ShieldBarSlot=ShieldCanvas->AddChildToCanvas(Row.ShieldBar);ShieldBarSlot->SetPosition(FVector2D(45,25));ShieldBarSlot->SetSize(FVector2D(119,15));
    }
    for(int32 Index=0;Index<2;++Index)
    {
        auto* Bar=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),Index==0?TEXT("BossLetterboxTop"):TEXT("BossLetterboxBottom"));Bar->SetBrushColor(FLinearColor::Black);
        auto* BarSlot=Root->AddChildToCanvas(Bar);BarSlot->SetAnchors(Index==0?FAnchors(0,0,1,.12f):FAnchors(0,.88f,1,1));BarSlot->SetOffsets(FMargin(0));BarSlot->SetZOrder(5);Bar->SetVisibility(ESlateVisibility::Collapsed);
        if(Index==0) TopBar=Bar;else BottomBar=Bar;
    }
    HealthPanel->SetVisibility(ESlateVisibility::Collapsed);ApplyTheme();
}

void UMCBossHealthWidget::ApplyTheme()
{
    Super::ApplyTheme();const auto* ThemeData=GetResolvedTheme();const auto& Palette=ThemeData->Palette;
    FProgressBarStyle Style;Style.SetBackgroundImage(FSlateRoundedBoxBrush(Palette.Ink,4));Style.SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White,4));Style.SetMarqueeImage(FSlateNoResource());
    auto* LEDMaterial=LEDFillMaterial.LoadSynchronous();
    if(LEDMaterial)
    {
        FSlateBrush LEDFill;
        LEDFill.DrawAs=ESlateBrushDrawType::Image;
        LEDFill.ImageSize=FVector2D(432.f,18.f);
        LEDFill.TintColor=FLinearColor::White;
        LEDFill.SetResourceObject(LEDMaterial);
        Style.SetFillImage(LEDFill);
    }
    auto ApplyBar=[&](UProgressBar* Bar,const FLinearColor& Color,bool bTrack,const FLinearColor& TrackColor)
    {
        if(!Bar) return;
        auto BarStyle=Style;
        auto GridBrush=[&](const FSlateBrush& Previous,const FLinearColor& ThemeColor,float Opacity,bool bEmpty=false)
        {
            FSlateBrush Brush=Style.FillImage;
            auto* Dynamic=Cast<UMaterialInstanceDynamic>(Previous.GetResourceObject());
            if(!Dynamic || Dynamic->GetMaterial()!=LEDMaterial->GetMaterial()) Dynamic=UMaterialInstanceDynamic::Create(LEDMaterial,Bar);
            Dynamic->SetVectorParameterValue(TEXT("ThemeColor"),ThemeColor.CopyWithNewOpacity(1.f));
            Dynamic->SetScalarParameterValue(TEXT("ThemeOpacity"),Opacity);
            const bool bShield=Bar->GetName().EndsWith(TEXT("Shield"));
            Dynamic->SetScalarParameterValue(TEXT("Columns"),bShield?8.f:Bar->GetName().StartsWith(TEXT("Boss1"))?58.f:31.f);
            Dynamic->SetScalarParameterValue(TEXT("Rows"),bShield?1.f:5.f);
            Dynamic->SetScalarParameterValue(TEXT("Segments"),bShield?1.f:0.f);
            Dynamic->SetScalarParameterValue(TEXT("Empty"),bEmpty?1.f:0.f);
            Brush.SetResourceObject(Dynamic);Brush.TintColor=FLinearColor::White;
            return Brush;
        };
        if(LEDMaterial)
        {
            BarStyle.SetFillImage(GridBrush(Bar->GetWidgetStyle().FillImage,Color,Color.A));
            const FLinearColor Dim(TrackColor.R*.16f,TrackColor.G*.16f,TrackColor.B*.16f,1.f);
            BarStyle.SetBackgroundImage(bTrack?GridBrush(Bar->GetWidgetStyle().BackgroundImage,Dim,TrackColor.A,true):FSlateNoResource());
        }
        else if(!bTrack) BarStyle.SetBackgroundImage(FSlateNoResource());
        Bar->SetWidgetStyle(BarStyle);Bar->SetBarFillStyle(EProgressBarFillStyle::Mask);
        Bar->SetFillColorAndOpacity(LEDMaterial?FLinearColor::White:Color);
    };
    for(auto& Row:Rows)
    {
        if(Row.Portrait)
        {
            // Bring existing authored starter trees onto the same aspect-fit path.
            auto* Fit=Cast<UScaleBox>(Row.Portrait->GetParent());
            if(auto* Layers=Cast<UOverlay>(Row.Portrait->GetParent()))
            {
                Row.Portrait->RemoveFromParent();Fit=WidgetTree->ConstructWidget<UScaleBox>();Fit->SetContent(Row.Portrait);
                auto* FitSlot=Layers->AddChildToOverlay(Fit);FitSlot->SetHorizontalAlignment(HAlign_Fill);FitSlot->SetVerticalAlignment(VAlign_Fill);
            }
            if(Fit)
            {
                Fit->SetStretch(EStretch::ScaleToFit);Fit->SetStretchDirection(EStretchDirection::DownOnly);
                if(auto* CanvasSlot=Cast<UCanvasPanelSlot>(Fit->Slot))
                {
                    Fit->SetStretchDirection(EStretchDirection::Both);
                    CanvasSlot->SetSize(FVector2D(FMath::Clamp(PortraitBoxSize.X,16.f,128.f),FMath::Clamp(PortraitBoxSize.Y,16.f,128.f)));
                }
                if(auto* Layers=Cast<UOverlay>(Fit->GetParent())) if(auto* Envelope=Cast<USizeBox>(Layers->GetParent()))
                {Envelope->SetWidthOverride(FMath::Clamp(PortraitBoxSize.X,16.f,64.f));Envelope->SetHeightOverride(FMath::Clamp(PortraitBoxSize.Y,16.f,64.f));}
            }
        }
        // Existing authored trees may still have the Overlay's default Left alignment.
        for(UProgressBar* Bar:{Row.Loss.Get(),Row.Current.Get()}) if(Bar) if(auto* FillSlot=Cast<UOverlaySlot>(Bar->Slot)) FillSlot->SetHorizontalAlignment(HAlign_Fill);
        if(Row.Panel) {Row.Panel->SetBrush(FSlateNoResource());Row.Panel->SetBrushColor(FLinearColor::Transparent);}
        if(Row.Frame) if(auto* Material=FrameMaterial.LoadSynchronous())
        {
            // A wide opaque icon may reach its fitted rectangle's left edge.
            // The procedural dilation needs its own transparent drawing margin.
            if(auto* FrameSlot=Cast<UCanvasPanelSlot>(Row.Frame->Slot)) {FrameSlot->SetPosition(FVector2D(-24,-12));FrameSlot->SetSize(FVector2D(840,108));}
            auto* Dynamic=Cast<UMaterialInstanceDynamic>(Row.Frame->GetBrush().GetResourceObject());
            if(!Dynamic || Dynamic->GetMaterial()!=Material->GetMaterial()) Dynamic=UMaterialInstanceDynamic::Create(Material,Row.Frame);
            Dynamic->SetVectorParameterValue(TEXT("PanelSize"),FLinearColor(840.f,108.f,0.f,0.f));
            Dynamic->SetVectorParameterValue(TEXT("IconRect"),FLinearColor(32.03f,17.74f,PortraitBoxSize.X,PortraitBoxSize.Y));
            Dynamic->SetVectorParameterValue(TEXT("BodyRect"),FLinearColor(98.f,22.f,742.f,64.f));
            Dynamic->SetVectorParameterValue(TEXT("SurfaceColor"),Palette.BossSurface);
            Dynamic->SetVectorParameterValue(TEXT("RimColor"),Palette.BossRim);
            Dynamic->SetVectorParameterValue(TEXT("HighlightColor"),Palette.BossRimHighlight);
            Dynamic->SetScalarParameterValue(TEXT("FrameOpacity"),1.f);
            Row.Frame->SetBrushFromMaterial(Dynamic);
        }
        auto NameFont=BossNameFont.HasValidFont()?BossNameFont:ThemeData->MakeFont(24,false);
        auto ValueFont=BossValueFont.HasValidFont()?BossValueFont:ThemeData->MakeFont(30,false);
        NameFont.OutlineSettings=FFontOutlineSettings(1,Palette.BossHealth.CopyWithNewOpacity(.22f*Palette.BossHealth.A));
        ValueFont.OutlineSettings=FFontOutlineSettings(1,Palette.BossHealth.CopyWithNewOpacity(.22f*Palette.BossHealth.A));
        if(Row.ShieldIcon) if(auto* Material=ShieldIconMaterial.LoadSynchronous())
        {
            auto* Dynamic=Cast<UMaterialInstanceDynamic>(Row.ShieldIcon->GetBrush().GetResourceObject());
            if(!Dynamic || Dynamic->GetMaterial()!=Material->GetMaterial()) Dynamic=UMaterialInstanceDynamic::Create(Material,Row.ShieldIcon);
            Dynamic->SetVectorParameterValue(TEXT("ThemeColor"),Palette.Shield.CopyWithNewOpacity(1.f));
            Dynamic->SetScalarParameterValue(TEXT("ThemeOpacity"),Palette.Shield.A);
            Row.ShieldIcon->SetBrushFromMaterial(Dynamic);
        }
        if(Row.Name) {Row.Name->SetFont(NameFont);Row.Name->SetColorAndOpacity(Palette.BossHealth);Row.Name->SetClipping(EWidgetClipping::ClipToBounds);}
        if(Row.Value) {Row.Value->SetFont(ValueFont);Row.Value->SetColorAndOpacity(Palette.BossHealth);}
        if(Row.PortraitFallback) {Row.PortraitFallback->SetFont(ThemeData->MakeFont(34));Row.PortraitFallback->SetColorAndOpacity(Palette.Cream);}
        if(Row.Tail && LEDMaterial)
        {
            auto* TailMID=Cast<UMaterialInstanceDynamic>(Row.Tail->GetBrush().GetResourceObject());
            if(!TailMID || TailMID->GetMaterial()!=LEDMaterial->GetMaterial()) TailMID=UMaterialInstanceDynamic::Create(LEDMaterial,Row.Tail);
            TailMID->SetVectorParameterValue(TEXT("ThemeColor"),FLinearColor(Palette.BossHealth.R*.16f,Palette.BossHealth.G*.16f,Palette.BossHealth.B*.16f,1.f));
            TailMID->SetScalarParameterValue(TEXT("ThemeOpacity"),Palette.BossHealth.A);TailMID->SetScalarParameterValue(TEXT("Columns"),26.f);TailMID->SetScalarParameterValue(TEXT("Rows"),5.f);TailMID->SetScalarParameterValue(TEXT("Empty"),1.f);
            Row.Tail->SetBrushFromMaterial(TailMID);
        }
        ApplyBar(Row.Loss,Palette.Warning,true,Palette.BossHealth);
        ApplyBar(Row.Current,Palette.BossHealth,false,Palette.BossHealth);
        ApplyBar(Row.ShieldBar,Palette.Shield,true,Palette.Shield);
        if(Row.ShieldPanel) Row.ShieldPanel->SetBrushColor(FLinearColor::Transparent);
        if(Row.ShieldValue) {Row.ShieldValue->SetFont(ThemeData->MakeFont(12));Row.ShieldValue->SetColorAndOpacity(Palette.Shield);}
    }
    CachedWidthLimit=-1.f;
    UpdateResponsiveLayout();
}

void UMCBossHealthWidget::ShowBosses(const TArray<FMCBossHealthPresentation>& Bosses,float DeltaSeconds)
{
    BuildPresentationTree();
    TArray<FMCBossHealthPresentation> Ordered;
    for(const auto& Previous:ActiveBosses)
        if(const auto* Found=Bosses.FindByPredicate([&](const auto& Entry){return Entry.BossID==Previous.BossID;})) Ordered.Add(*Found);
    for(const auto& Entry:Bosses)
        if(Ordered.Num()<3 && !Ordered.ContainsByPredicate([&](const auto& Existing){return Existing.BossID==Entry.BossID;})) Ordered.Add(Entry);
    ActiveBosses=MoveTemp(Ordered);bShowingHealth=!ActiveBosses.IsEmpty();
    PresentBosses(FMath::Max(0.f,DeltaSeconds));
}

void UMCBossHealthWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    const float Width=Geometry.GetLocalSize().X;
    if(Width>1.f && !FMath::IsNearlyEqual(LayoutViewportWidth,Width,.5f))
    {
        LayoutViewportWidth=Width;
        UpdateResponsiveLayout();
    }
}

void UMCBossHealthWidget::UpdateResponsiveLayout()
{
    if(!HealthPanel || ActiveBosses.IsEmpty() || !FSlateApplication::IsInitialized()) return;
    // Geometry is already in logical UMG units. Leave room for the corner HUD
    // in composed screens; an isolated designer preview uses its whole canvas.
    const float WidthLimit=FMath::Clamp(LayoutViewportWidth>=1400.f?LayoutViewportWidth-720.f:LayoutViewportWidth-32.f,816.f,FMath::Max(816.f,MaximumStackWidth));
    auto Font=BossNameFont.HasValidFont()?BossNameFont:GetResolvedTheme()->MakeFont(24,false);
    FString Signature;
    for(const auto& Entry:ActiveBosses) Signature+=Entry.Name.ToUpper().ToString()+TEXT("\x1f")+(Entry.bHasShield && Entry.MaxShield>0?TEXT("1"):TEXT("0"));
    if(CachedLayoutNames==Signature && CachedLayoutFont==Font && FMath::IsNearlyEqual(CachedWidthLimit,WidthLimit,.1f)) return;
    CachedLayoutNames=Signature;CachedLayoutFont=Font;CachedWidthLimit=WidthLimit;
    float LongestName=230.f;
    for(int32 Index=0;Index<Rows.Num() && Index<ActiveBosses.Num();++Index)
    {
        auto* Name=Rows[Index].Name.Get();
        if(!Name) continue;
        auto NameFont=Font;
        NameFont.OutlineSettings=FFontOutlineSettings(1,GetResolvedTheme()->Palette.BossHealth.CopyWithNewOpacity(.22f*GetResolvedTheme()->Palette.BossHealth.A));
        Name->SetFont(NameFont);Name->SetText(ActiveBosses[Index].Name.ToUpper());
        Name->SetAutoWrapText(false);Name->SetWrapTextAt(0.f);
        Name->TakeWidget()->SlatePrepass();
        // Measure the very same shaped Slate text used for painting. A raw
        // font-measure call can disagree with fallback shaping and outline size.
        LongestName=FMath::Max(LongestName,Name->GetDesiredSize().X+8.f);
    }
    const float StackWidth=FMath::Min(WidthLimit,816.f+LongestName-230.f);
    const float Extra=StackWidth-816.f,NameWidth=230.f+Extra;
    float StackHeight=0.f;
    for(int32 Index=0;Index<Rows.Num() && Index<ActiveBosses.Num();++Index)
    {
        auto& Row=Rows[Index];const auto& Entry=ActiveBosses[Index];
        if(!Row.Name) continue;
        Row.Name->SetText(Entry.Name.ToUpper());Row.Name->SetToolTipText(Entry.Name);
        auto NameFont=Font;
        NameFont.OutlineSettings=FFontOutlineSettings(1,GetResolvedTheme()->Palette.BossHealth.CopyWithNewOpacity(.22f*GetResolvedTheme()->Palette.BossHealth.A));
        Row.Name->SetFont(NameFont);
        Row.Name->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
        Row.Name->SetAutoWrapText(false);Row.Name->SetWrapTextAt(NameWidth);
        Row.Name->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
        // Explicit wrap width lets Slate shape/measure complete localized text,
        // including emergency breaks for compounds, before sizing the frame.
        Row.Name->TakeWidget()->SlatePrepass();
        const float TextHeight=Row.Name->GetDesiredSize().Y;
        const float BodyHeight=FMath::Max(64.f,TextHeight+20.f);
        Row.VerticalOffset=(BodyHeight-64.f)*.5f;
        const float Offset=Row.VerticalOffset;
        auto Place=[](UWidget* Widget,FVector2D Position,FVector2D Size)
        {if(auto* Slot=Widget?Cast<UCanvasPanelSlot>(Widget->Slot):nullptr) {Slot->SetPosition(Position);Slot->SetSize(Size);}};
        Place(Row.Name,FVector2D(97.f,10.f+(BodyHeight-TextHeight)*.5f),FVector2D(NameWidth,TextHeight+1.f));
        if(auto* Wrapper=Row.Panel?Cast<USizeBox>(Row.Panel->GetParent()):nullptr) Wrapper->SetHeightOverride(BodyHeight+.3f);
        Place(Row.Frame,FVector2D(-24.f,-12.f),FVector2D(840.f+Extra,108.f+BodyHeight-64.f));
        if(Row.Frame) if(auto* MID=Cast<UMaterialInstanceDynamic>(Row.Frame->GetBrush().GetResourceObject()))
        {
            MID->SetVectorParameterValue(TEXT("PanelSize"),FLinearColor(840.f+Extra,108.f+BodyHeight-64.f,0.f,0.f));
            MID->SetVectorParameterValue(TEXT("BodyRect"),FLinearColor(98.f,22.f,742.f+Extra,BodyHeight));
        }
        if(Row.Portrait) Place(Row.Portrait->GetParent(),FVector2D(8.03f,5.74f+Offset),PortraitBoxSize);
        Place(Row.PortraitFallback,FVector2D(12.f,12.f+Offset),FVector2D(58.f,52.f));
        const bool Shield=Entry.bHasShield && Entry.MaxShield>0;
        const bool FullBar=Index==0 && !Shield;
        if(Row.Current) Place(Row.Current->GetParent(),FVector2D(340.f+Extra,25.f+Offset),FVector2D(FullBar?368.f:196.f,32.f));
        Place(Row.Value,FVector2D((FullBar?724.f:552.f)+Extra,10.f+Offset),FVector2D(74.f,52.f));
        Place(Row.Tail,FVector2D(636.f+Extra,25.f+Offset),FVector2D(164.f,32.f));
        Place(Row.ShieldPanel,FVector2D(636.f+Extra,13.f+Offset),FVector2D(164.f,48.f));
        // Keep the cached alpha-derived icon lobe aligned when a title wraps.
        if(Row.Portrait && Row.Frame) if(auto* Texture=Cast<UTexture2D>(Row.Portrait->GetBrush().GetResourceObject()))
            if(auto* MID=Cast<UMaterialInstanceDynamic>(Row.Frame->GetBrush().GetResourceObject()))
            {
                const FVector2D SourceSize(Texture->GetSizeX(),Texture->GetSizeY());
                const float* Optical=PortraitScaleByBoss.Find(Entry.BossID);
                const float Scale=Optical && FMath::IsFinite(*Optical)?FMath::Clamp(*Optical,.5f,1.5f):1.f;
                const float Fit=FMath::Min(PortraitBoxSize.X/FMath::Max(1.,SourceSize.X),PortraitBoxSize.Y/FMath::Max(1.,SourceSize.Y));
                const FVector2D DrawSize=SourceSize*Fit*Scale;
                const FVector2D Position=FVector2D(32.03f,17.74f+Offset)+(PortraitBoxSize-DrawSize)*.5f;
                MID->SetVectorParameterValue(TEXT("IconRect"),FLinearColor(Position.X,Position.Y,DrawSize.X,DrawSize.Y));
            }
        StackHeight+=BodyHeight+.3f;
    }
    // Reserve the last frame's complete painted allocation, including a fully
    // opaque replacement icon's rim rather than the stock PNG's transparent bounds.
    const auto& LastRow=Rows[ActiveBosses.Num()-1];
    const auto* FrameSlot=LastRow.Frame?Cast<UCanvasPanelSlot>(LastRow.Frame->Slot):nullptr;
    const float LastRowHeight=64.3f+LastRow.VerticalOffset*2.f;
    const float Overhang=FrameSlot?FrameSlot->GetPosition().Y+FrameSlot->GetSize().Y-LastRowHeight:15.f;
    DisplayedStackHeight=StackHeight+FMath::Max(0.f,Overhang);
    if(auto* StackSlot=Cast<UCanvasPanelSlot>(HealthPanel->Slot)) StackSlot->SetSize(FVector2D(StackWidth,DisplayedStackHeight));
}

void UMCBossHealthWidget::PresentBosses(float DeltaSeconds)
{
    UpdateResponsiveLayout();
    const FString Culture=IsDesignTime()?PreviewCulture:FString();
    for(int32 Index=0;Index<Rows.Num();++Index)
    {
        auto& Row=Rows[Index];const bool Available=ActiveBosses.IsValidIndex(Index);
        if(Row.Panel) Row.Panel->SetVisibility(Available?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        // Hide the fixed row wrapper as well, so absent rows do not reserve space.
        if(auto* Wrapper=Row.Panel?Cast<USizeBox>(Row.Panel->GetParent()):nullptr) Wrapper->SetVisibility(Available?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        if(!Available) continue;
        const auto& Entry=ActiveBosses[Index];const float Fraction=FMath::Clamp(Entry.Health/FMath::Max(1.f,Entry.MaxHealth),0.f,1.f);
        if(Row.BossID!=Entry.BossID || Fraction>Row.LastFraction) {Row.DelayedFraction=Fraction;Row.LossHold=0;Row.BossID=Entry.BossID;}
        else if(Fraction<Row.LastFraction-KINDA_SMALL_NUMBER) Row.LossHold=.45f;
        else if(Row.LossHold>0) Row.LossHold=FMath::Max(0.f,Row.LossHold-DeltaSeconds);
        else Row.DelayedFraction=FMath::FInterpConstantTo(Row.DelayedFraction,Fraction,DeltaSeconds,.7f);
        Row.LastFraction=Fraction;
        if(Row.Value) Row.Value->SetText(UMCUIDataLibrary::FormatTextForCulture(Localized(TEXT("UI.Boss.Percent"),LOCTEXT("Percent","{Value}%")),FFormatNamedArguments{{TEXT("Value"),UMCUIDataLibrary::NumberForCulture(FMath::RoundToInt(Fraction*100),Culture)}},Culture));
        if(Row.Current) Row.Current->SetPercent(Fraction);
        // A settled bar must not expose the warning layer through themed alpha.
        if(Row.Loss) Row.Loss->SetPercent(Row.DelayedFraction>Fraction+KINDA_SMALL_NUMBER?Row.DelayedFraction:0.f);
        const bool Shield=Entry.bHasShield && Entry.MaxShield>0;
        if(Row.ShieldPanel) Row.ShieldPanel->SetVisibility(Shield?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        if(auto* Wrapper=Row.ShieldPanel?Cast<USizeBox>(Row.ShieldPanel->GetParent()):nullptr) Wrapper->SetVisibility(Shield?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        if(Row.Tail) Row.Tail->SetVisibility(Index>0 && !Shield?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        for(UProgressBar* Bar:{Row.Current.Get(),Row.Loss.Get()}) if(Bar)
            for(const FSlateBrush* Brush:{&Bar->GetWidgetStyle().FillImage,&Bar->GetWidgetStyle().BackgroundImage})
                if(auto* Material=Cast<UMaterialInstanceDynamic>(Brush->GetResourceObject()))
                    Material->SetScalarParameterValue(TEXT("Columns"),Index==0 && !Shield?58.f:31.f);
        const float ShieldFraction=Shield?FMath::Clamp(Entry.Shield/Entry.MaxShield,0.f,1.f):0;
        if(Row.ShieldBar) Row.ShieldBar->SetPercent(ShieldFraction);
        if(Row.ShieldValue) Row.ShieldValue->SetText(UMCUIDataLibrary::FormatTextForCulture(Localized(TEXT("UI.Boss.Shield"),LOCTEXT("Shield","Щит {Value}%")),FFormatNamedArguments{{TEXT("Value"),UMCUIDataLibrary::NumberForCulture(FMath::RoundToInt(ShieldFraction*100),Culture)}},Culture));
        const bool HasPortrait=!Entry.Portrait.IsNull();
        if(Row.Portrait)
        {
            const float* OpticalScale=PortraitScaleByBoss.Find(Entry.BossID);
            const float Scale=OpticalScale && FMath::IsFinite(*OpticalScale)?FMath::Clamp(*OpticalScale,.5f,1.5f):1.f;
            Row.Portrait->SetRenderTransformPivot(FVector2D(.5f,.5f));Row.Portrait->SetRenderScale(FVector2D(Scale));
            Row.Portrait->SetVisibility(HasPortrait?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
            if(HasPortrait)
            {
                if(auto* Texture=IsDesignTime()?Entry.Portrait.LoadSynchronous():Entry.Portrait.Get())
                {
                    Row.Portrait->SetBrushFromTexture(Texture,true);
                    if(Row.Frame) if(auto* FrameMID=Cast<UMaterialInstanceDynamic>(Row.Frame->GetBrush().GetResourceObject()))
                    {
                        FrameMID->SetTextureParameterValue(TEXT("IconMask"),Texture);
                        FrameMID->SetScalarParameterValue(TEXT("HasIcon"),1.f);
                        const FVector2D SourceSize(Texture->GetSizeX(),Texture->GetSizeY());
                        const float Fit=FMath::Min(PortraitBoxSize.X/FMath::Max(1.,SourceSize.X),PortraitBoxSize.Y/FMath::Max(1.,SourceSize.Y));
                        const FVector2D DrawSize=SourceSize*Fit*Scale;
                        const FVector2D DrawPosition=FVector2D(32.03f,17.74f+Row.VerticalOffset)+(PortraitBoxSize-DrawSize)*.5f;
                        FrameMID->SetVectorParameterValue(TEXT("IconRect"),FLinearColor(DrawPosition.X,DrawPosition.Y,DrawSize.X,DrawSize.Y));
                        if(auto* Mask=MCBossHealthPrivate::PrepareMask(Texture,FrameMaterial.Get(),DrawSize))
                        {
                            FrameMID->SetTextureParameterValue(TEXT("DistanceMask"),Mask);
                            FrameMID->SetVectorParameterValue(TEXT("FieldSize"),FLinearColor(DrawSize.X+48.f,DrawSize.Y+48.f,0.f,0.f));
                            FrameMID->SetScalarParameterValue(TEXT("MaskPadding"),24.f);FrameMID->SetScalarParameterValue(TEXT("DistanceReady"),1.f);
                        }
                    }
                }
                else Row.Portrait->SetBrushFromSoftTexture(Entry.Portrait,true);
            }
        }
        if(!HasPortrait && Row.Frame) if(auto* FrameMID=Cast<UMaterialInstanceDynamic>(Row.Frame->GetBrush().GetResourceObject())) FrameMID->SetScalarParameterValue(TEXT("HasIcon"),0.f);
        if(Row.PortraitFallback) Row.PortraitFallback->SetVisibility(HasPortrait?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    }
    if(HealthPanel) HealthPanel->SetVisibility(bShowingHealth && !bLetterbox?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
}

void UMCBossHealthWidget::ShowHealth(float Health,float MaxHealth,float DeltaSeconds)
{
    FMCBossHealthPresentation Entry;Entry.BossID=TEXT("RottenGuardian");Entry.Name=Localized(TEXT("UI.Boss.Zombie.Name"),LOCTEXT("ZombieName","Гнилой страж"));Entry.Health=Health;Entry.MaxHealth=MaxHealth;ShowBosses({Entry},DeltaSeconds);
}
void UMCBossHealthWidget::HideHealth() {bShowingHealth=false;ActiveBosses.Reset();if(HealthPanel) HealthPanel->SetVisibility(ESlateVisibility::Collapsed);}
void UMCBossHealthWidget::SetLetterbox(bool bShow)
{
    BuildPresentationTree();bLetterbox=bShow;
    if(TopBar) TopBar->SetVisibility(bShow?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(BottomBar) BottomBar->SetVisibility(bShow?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(HealthPanel) HealthPanel->SetVisibility(!bShow && bShowingHealth?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
}
void UMCBossHealthWidget::ApplyEditorPreview(float Time)
{
    PreviewTime=FMath::Max(0.f,Time);BuildPresentationTree();ApplyTheme();bLetterbox=PreviewState==TEXT("Letterbox");
    const int32 Count=PreviewState==TEXT("ZeroBosses")?0:PreviewState==TEXT("ShieldedLeader")?1:PreviewScenario==EMCGameplayUIPreviewScenario::Empty?0:PreviewScenario==EMCGameplayUIPreviewScenario::OneBoss?1:PreviewScenario==EMCGameplayUIPreviewScenario::TwoBosses?2:3;
    TArray<FMCBossHealthPresentation> Samples;
    const FName IDs[]={TEXT("RottenGuardian"),TEXT("NutKnight"),TEXT("NutWizard")};
    const FText Names[]={Localized(TEXT("UI.Boss.Zombie.Name"),LOCTEXT("ZombieName","Гнилой страж")),Localized(TEXT("UI.Boss.NutTank.Name"),LOCTEXT("NutTank","Ореховый рыцарь")),Localized(TEXT("UI.Boss.NutMage.Name"),LOCTEXT("NutMage","Ореховый колдун"))};
    for(int32 Index=0;Index<Count;++Index)
    {
        const int32 SampleIndex=PreviewState==TEXT("ShieldedLeader")?1:Index;
        auto& Entry=Samples.AddDefaulted_GetRef();Entry.BossID=IDs[SampleIndex];Entry.Name=PreviewScenario==EMCGameplayUIPreviewScenario::LongText?Localized(TEXT("UI.Boss.LongName"),LOCTEXT("LongName","Очень длинное локализованное имя опасного босса")):Names[SampleIndex];
        Entry.Portrait=GetResolvedTheme()->BossPortraits.FindRef(IDs[SampleIndex]);
        Entry.MaxHealth=100;Entry.Health=PreviewScenario==EMCGameplayUIPreviewScenario::LowResources?8.f:PreviewState==TEXT("ZeroHealth")?0.f:PreviewState==TEXT("FullHealth")?100.f:(SampleIndex==0?68.f:SampleIndex==1?45.f:40.f);
        Entry.bHasShield=SampleIndex==1;Entry.MaxShield=100;Entry.Shield=PreviewState==TEXT("BrokenShield")?0.f:60.f;
    }
    ShowBosses(Samples,.1f);SetLetterbox(bLetterbox);
}

#undef LOCTEXT_NAMESPACE
