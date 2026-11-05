#include "MCUnifiedEmoteWidget.h"
#include "MCExpressionComponent.h"
#include "MCGameplayUIFactory.h"
#include "MCUIDataLibrary.h"
#include "MCUniversalUIWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateNoResource.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Rendering/SlateRenderer.h"

#define LOCTEXT_NAMESPACE "MCUnifiedEmote"
namespace MCUnifiedEmotePrivate
{
    FLinearColor Color(const TCHAR* Hex) {return FLinearColor(FColor::FromHex(Hex));}
    FName Indexed(const TCHAR* Prefix,int32 Index) {return FName(*FString::Printf(TEXT("%s%d"),Prefix,Index));}
    const TCHAR* Labels[]={TEXT("NewEmoteHeading"),TEXT("NewEmoteHint"),TEXT("NewEmoteFooter"),TEXT("NewEmoteColorHeading"),TEXT("NewEmoteSector0"),TEXT("NewEmoteSector1"),TEXT("NewEmoteSector2"),TEXT("NewEmoteCenterKey")};
    const int32 Sizes[]={26,20,18,20,22,22,22,26};
    const FLinearColor PlayerColors[]={FLinearColor(.24f,.65f,1),FLinearColor(1,.27f,.31f),FLinearColor(.26f,.91f,.71f),FLinearColor(.75f,.45f,1)};
}
void UMCUnifiedEmoteBinding::Clicked() {if(auto* View=Owner.Get()) View->ForwardChoice(this,false);}
void UMCUnifiedEmoteBinding::Hovered() {if(auto* View=Owner.Get()) View->ForwardChoice(this,true);}
UMCUnifiedEmoteChoiceButton::UMCUnifiedEmoteChoiceButton() {InitIsFocusable(false);}
UMCUnifiedEmoteWidget::UMCUnifiedEmoteWidget() {bUseNativeLayout=false;bOverridePreviewUIVersion=true;PreviewUIVersion=EMCUIVersion::New;SetIsFocusable(true);}
UMCUniversalUIWidget* UMCUnifiedEmoteWidget::MakeShape(FName Name,FLinearColor FillColor)
{
    auto* View=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),Name);
    View->Label=FText::GetEmpty();View->LabelKey=NAME_None;View->Kind=EMCUniversalUIKind::Panel;
    View->bUseNativeLayout=false;View->bUseThemeShape=false;View->bCustomColors=true;View->CustomFill=FillColor;View->CustomText=MCUnifiedEmotePrivate::Color(TEXT("FFF9F8"));
    View->CustomOutline=MCUnifiedEmotePrivate::Color(TEXT("FFF9F8"));View->CustomShadow=MCUnifiedEmotePrivate::Color(TEXT("243A52"));
    View->CornerRadius=14;View->OutlineWidth=3;View->ContentPadding=FMargin(14,10);View->ShadowOffset=FVector2D(6,7);
    View->SetVisibility(ESlateVisibility::SelfHitTestInvisible);View->Initialize();View->BuildPresentationTree();View->ApplyTheme();return View;
}
UTextBlock* UMCUnifiedEmoteWidget::MakeLabel(FName Name,int32 Size)
{
    auto* Caption=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);Caption->SetAutoWrapText(false);Caption->SetWrapTextAt(440);
    Caption->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);Caption->SetColorAndOpacity(MCUnifiedEmotePrivate::Color(TEXT("FFF9F8")));Caption->SetVisibility(ESlateVisibility::HitTestInvisible);
    if(auto* T=GetResolvedTheme()) Caption->SetFont(T->MakeFont(Size,true));return Caption;
}
void UMCUnifiedEmoteWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;if(!Theme) Theme=UMCGameplayUIFactory::GetThemeForVersion(EMCUIVersion::New);if(WidgetTree->RootWidget) {
        auto* Bounds=WidgetTree->FindWidget<USizeBox>(TEXT("NewEmoteBounds"));auto* Scroll=WidgetTree->FindWidget<UScrollBox>(TEXT("NewEmoteBodyScroll"));auto* Body=WidgetTree->FindWidget<UVerticalBox>(TEXT("NewEmoteBody"));
        if(Bounds && Scroll && Body && Scroll->GetParent()==Bounds && Body->GetParent()==Scroll && Scroll->GetChildrenCount()==1) {Body->RemoveFromParent();Bounds->SetContent(Body);}
        return;
    }
    auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("NewEmoteRoot"));WidgetTree->RootWidget=Canvas;
    auto* Backdrop=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("NewEmoteBackdrop"));Backdrop->SetBrushColor(FLinearColor(0,0,0,.65f));Backdrop->SetPadding(FMargin(0));
    auto* BackdropCell=Canvas->AddChildToCanvas(Backdrop);BackdropCell->SetAnchors(FAnchors(0,0,1,1));BackdropCell->SetOffsets(FMargin(0));
    auto* Bounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("NewEmoteBounds"));Bounds->SetWidthOverride(1080);Bounds->SetMaxDesiredHeight(700);
    auto* BodyCell=Canvas->AddChildToCanvas(Bounds);BodyCell->SetAnchors(FAnchors(.5,.5));BodyCell->SetAlignment(FVector2D(.5,.5));BodyCell->SetAutoSize(true);
    auto* Body=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("NewEmoteBody"));Bounds->SetContent(Body);
    auto* MainRow=WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(),TEXT("NewEmoteMainRow"));MainRow->SetExplicitWrapSize(true);MainRow->SetInnerSlotPadding(FVector2D(20,20));Body->AddChildToVerticalBox(MainRow);
    auto* WheelBounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("NewEmoteWheelBounds"));WheelBounds->SetWidthOverride(428);WheelBounds->SetHeightOverride(428);MainRow->AddChildToWrapBox(WheelBounds);
    auto* WheelCanvas=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("NewEmoteWheelCanvas"));WheelBounds->SetContent(WheelCanvas);
    for(int32 Index=0;Index<3;++Index) {
        auto* Sector=MakeShape(MCUnifiedEmotePrivate::Indexed(TEXT("NewEmoteSectorShape"),Index),MCUnifiedEmotePrivate::Color(TEXT("374F56")));
        Sector->ContentPadding=FMargin(0);Sector->CornerRadius=0;Sector->ShadowOffset=FVector2D(4,5);
        const float Start=-148.f+Index*120,End=Start+116;
        for(int32 Step=0;Step<=24;++Step) {const float Angle=FMath::DegreesToRadians(FMath::Lerp(Start,End,float(Step)/24));Sector->ContourPoints.Add((FVector2D(214,214)+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*210)/428);}
        for(int32 Step=24;Step>=0;--Step) {const float Angle=FMath::DegreesToRadians(FMath::Lerp(Start,End,float(Step)/24));Sector->ContourPoints.Add((FVector2D(214,214)+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*65)/428);}
        Sector->SetVisibility(ESlateVisibility::HitTestInvisible);Sector->ApplyTheme();auto* ShapeCell=WheelCanvas->AddChildToCanvas(Sector);ShapeCell->SetAnchors(FAnchors(0,0,1,1));ShapeCell->SetOffsets(FMargin(0));
        auto* Caption=MakeLabel(MCUnifiedEmotePrivate::Indexed(TEXT("NewEmoteSector"),Index),22);Caption->SetJustification(ETextJustify::Center);
        auto* CaptionCell=WheelCanvas->AddChildToCanvas(Caption);CaptionCell->SetAnchors(FAnchors(.5,.5));CaptionCell->SetAlignment(FVector2D(.5,.5));CaptionCell->SetAutoSize(true);
    }
    auto* CenterKey=MakeLabel(TEXT("NewEmoteCenterKey"),26);CenterKey->SetText(FText::AsCultureInvariant(TEXT("T")));CenterKey->SetJustification(ETextJustify::Center);
    auto* KeyCell=WheelCanvas->AddChildToCanvas(CenterKey);KeyCell->SetAnchors(FAnchors(.5,.5));KeyCell->SetAlignment(FVector2D(.5,.5));KeyCell->SetSize(FVector2D(50,40));
    auto* ListBounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("NewEmoteListBounds"));ListBounds->SetWidthOverride(560);ListBounds->SetHeightOverride(428);MainRow->AddChildToWrapBox(ListBounds);
    auto* ListFrame=MakeShape(TEXT("NewEmoteListFrame"),MCUnifiedEmotePrivate::Color(TEXT("374F56")));ListBounds->SetContent(ListFrame);
    auto* ListBody=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("NewEmoteListBody"));ListFrame->SetContentForSlot(TEXT("Content"),ListBody);
    auto* Header=MakeShape(TEXT("NewEmoteHeaderFrame"),MCUnifiedEmotePrivate::Color(TEXT("4C76AE")));ListBody->AddChildToVerticalBox(Header)->SetPadding(FMargin(0,0,0,10));Header->SetContentForSlot(TEXT("Content"),MakeLabel(TEXT("NewEmoteHeading"),26));
    auto* ListScroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("NewEmoteListScroll"));ListScroll->SetAllowOverscroll(false);ListScroll->SetScrollbarThickness(FVector2D(6,6));ListBody->AddChildToVerticalBox(ListScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto* Items=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("NewEmoteItems"));ListScroll->AddChild(Items);ListBody->AddChildToVerticalBox(MakeLabel(TEXT("NewEmoteHint"),20))->SetPadding(FMargin(0,12,0,0));
    auto* ColorFrame=MakeShape(TEXT("NewEmoteColorFrame"),MCUnifiedEmotePrivate::Color(TEXT("374F56")));Body->AddChildToVerticalBox(ColorFrame)->SetPadding(FMargin(0,16,8,0));
    auto* ColorBody=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("NewEmoteColorBody"));ColorFrame->SetContentForSlot(TEXT("Content"),ColorBody);ColorBody->AddChildToVerticalBox(MakeLabel(TEXT("NewEmoteColorHeading"),20))->SetPadding(FMargin(0,0,0,8));
    auto* ColorRow=WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(),TEXT("NewEmoteColors"));ColorRow->SetExplicitWrapSize(true);ColorRow->SetInnerSlotPadding(FVector2D(10,10));ColorBody->AddChildToVerticalBox(ColorRow);
    Body->AddChildToVerticalBox(MakeLabel(TEXT("NewEmoteFooter"),18))->SetPadding(FMargin(8,14,8,8));UpdateLayout();
}
void UMCUnifiedEmoteWidget::UpdateChoices(const FMCEmotePresentationSnapshot& Snapshot)
{
    auto* Items=WidgetTree->FindWidget<UVerticalBox>(TEXT("NewEmoteItems"));auto* Colors=WidgetTree->FindWidget<UWrapBox>(TEXT("NewEmoteColors"));if(!Items || !Colors) return;
    auto Fill=[&](const TArray<FMCEmotePresentationChoice>& Tokens,TArray<TObjectPtr<UMCUniversalUIWidget>>& Views,bool bColor) {
        for(int32 Index=0;Index<Tokens.Num();++Index) {
            if(!Views.IsValidIndex(Index)) {
                const FName FrameName=MCUnifiedEmotePrivate::Indexed(bColor?TEXT("NewEmoteColorChoice"):TEXT("NewEmoteChoice"),Index);
                auto* Frame=WidgetTree->FindWidget<UMCUniversalUIWidget>(FrameName);if(!Frame) Frame=MakeShape(FrameName,MCUnifiedEmotePrivate::Color(TEXT("4C76AE")));
                Frame->Kind=EMCUniversalUIKind::Panel;Frame->SetVisibility(ESlateVisibility::HitTestInvisible);
                if(!Frame->GetContentForSlot(TEXT("Content"))) Frame->SetContentForSlot(TEXT("Content"),MakeLabel(MCUnifiedEmotePrivate::Indexed(bColor?TEXT("NewEmoteColorLabel"):TEXT("NewEmoteChoiceLabel"),Index),22));
                const FName ButtonName=MCUnifiedEmotePrivate::Indexed(bColor?TEXT("NewEmoteColorButton"):TEXT("NewEmoteChoiceButton"),Index);
                auto* Proxy=WidgetTree->FindWidget<UMCUnifiedEmoteChoiceButton>(ButtonName);if(!Proxy) Proxy=WidgetTree->ConstructWidget<UMCUnifiedEmoteChoiceButton>(UMCUnifiedEmoteChoiceButton::StaticClass(),ButtonName);
                FButtonStyle ProxyStyle;ProxyStyle.SetNormal(FSlateNoResource());ProxyStyle.SetHovered(FSlateNoResource());ProxyStyle.SetPressed(FSlateNoResource());ProxyStyle.SetDisabled(FSlateNoResource());
                ProxyStyle.SetNormalPadding(FMargin(0));ProxyStyle.SetPressedPadding(FMargin(0));Proxy->SetStyle(ProxyStyle);
                if(Proxy->GetContent()!=Frame) {Frame->RemoveFromParent();Proxy->SetContent(Frame);}
                if(auto* Cell=Cast<UButtonSlot>(Proxy->GetContentSlot())) {Cell->SetHorizontalAlignment(HAlign_Fill);Cell->SetVerticalAlignment(VAlign_Fill);Cell->SetPadding(FMargin(0));}
                if(!Proxy->GetParent()) {if(bColor) Colors->AddChildToWrapBox(Proxy);else Items->AddChildToVerticalBox(Proxy)->SetPadding(FMargin(0,0,8,10));}
                Views.Add(Frame);auto* Binding=NewObject<UMCUnifiedEmoteBinding>(this);Binding->Owner=this;Binding->Frame=Frame;Bindings.Add(Binding);
                Proxy->OnClicked.AddUniqueDynamic(Binding,&UMCUnifiedEmoteBinding::Clicked);Proxy->OnHovered.AddUniqueDynamic(Binding,&UMCUnifiedEmoteBinding::Hovered);
            }
            auto* Frame=Views[Index].Get();const auto& Token=Tokens[Index];for(const auto& Binding:Bindings) if(Binding && Binding->Frame.Get()==Frame) {Binding->Token=Token;break;}
            Frame->SetVisibility(ESlateVisibility::HitTestInvisible);Frame->bSelected=Token.bSelected;Frame->CustomFill=bColor?Token.Color:MCUnifiedEmotePrivate::Color(Token.bSelected?TEXT("AD4672"):TEXT("4C76AE"));Frame->SetIsEnabled(Token.bEnabled);Frame->ApplyTheme();
            if(auto* Proxy=Cast<UMCUnifiedEmoteChoiceButton>(Frame->GetParent())) {Proxy->SetVisibility(ESlateVisibility::Visible);Proxy->SetIsEnabled(Token.bEnabled);}
            if(auto* Caption=Cast<UTextBlock>(Frame->GetContentForSlot(TEXT("Content")))) {Caption->SetText(Token.Label);Caption->SetColorAndOpacity(MCUnifiedEmotePrivate::Color(bColor?TEXT("132939"):TEXT("FFF9F8")));}
        }
        for(int32 Index=Tokens.Num();Index<Views.Num();++Index) if(auto* Proxy=Views[Index]->GetParent()) Proxy->SetVisibility(ESlateVisibility::Collapsed);
    };
    Fill(Snapshot.Choices,ChoiceViews,false);Fill(Snapshot.Colors,ColorViews,true);
}

void UMCUnifiedEmoteWidget::UpdateLayout()
{
    if(!WidgetTree) return;const FVector2D Area=GetPaintSpaceGeometry().GetLocalSize();if(Area.X<=1 || Area.Y<=1) return;
    const float BodyWidth=FMath::Max(1.f,FMath::Min(1080.f,float(Area.X)-48.f));
    const float AvailableHeight=FMath::Max(1.f,float(Area.Y)-48.f);
    auto Wrap=[&](const TCHAR* Name,float Width) {if(auto* Caption=WidgetTree->FindWidget<UTextBlock>(Name)) {Caption->SetAutoWrapText(false);Caption->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);Caption->SetWrapTextAt(FMath::Max(1.f,Width));}};
    if(auto* Colors=WidgetTree->FindWidget<UWrapBox>(TEXT("NewEmoteColors"))) Colors->SetWrapSize(FMath::Max(1.f,BodyWidth-34.f));
    Wrap(TEXT("NewEmoteColorHeading"),BodyWidth-34.f);Wrap(TEXT("NewEmoteFooter"),BodyWidth-24.f);
    for(const auto& Frame:ColorViews) if(auto* Caption=Cast<UTextBlock>(Frame->GetContentForSlot(TEXT("Content")))) {Caption->SetAutoWrapText(false);Caption->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);Caption->SetWrapTextAt(FMath::Max(1.f,FMath::Min(240.f,BodyWidth-66.f)));}
    const float Scale=FMath::Max(.001f,GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale());
    auto MeasuredHeight=[&](UWidget* Widget) {
        if(!Widget || !Widget->IsVisible()) return 0.f;
        const auto Slate=Widget->TakeWidget();Slate->MarkPrepassAsDirty();Slate->SlatePrepass(Scale);
        float Height=FMath::CeilToFloat(Widget->GetDesiredSize().Y);
        if(const auto* Cell=Cast<UVerticalBoxSlot>(Widget->Slot)) Height+=Cell->GetPadding().Top+Cell->GetPadding().Bottom;
        return Height;
    };
    const float FixedHeight=MeasuredHeight(WidgetTree->FindWidget(TEXT("NewEmoteColorFrame")))+MeasuredHeight(WidgetTree->FindWidget(TEXT("NewEmoteFooter")));
    const float MainHeight=FMath::Max(1.f,AvailableHeight-FixedHeight);
    // The wheel and list stay beside each other; the wheel artwork alone can resize.
    // Text retains its authored font. Scrolling is confined to NewEmoteListScroll.
    const float Diameter=FMath::Max(1.f,FMath::Min(428.f,FMath::Min(MainHeight,BodyWidth*.42f)));
    const float ListWidth=FMath::Max(1.f,BodyWidth-Diameter-20.f);
    const float MainRowHeight=FMath::Min(428.f,MainHeight);
    if(auto* Bounds=WidgetTree->FindWidget<USizeBox>(TEXT("NewEmoteBounds"))) {Bounds->ClearMinDesiredHeight();Bounds->ClearMaxDesiredHeight();Bounds->SetWidthOverride(BodyWidth);Bounds->SetHeightOverride(MainRowHeight+FixedHeight);}
    if(auto* Row=WidgetTree->FindWidget<UWrapBox>(TEXT("NewEmoteMainRow"))) Row->SetWrapSize(BodyWidth+.5f);
    if(auto* Bounds=WidgetTree->FindWidget<USizeBox>(TEXT("NewEmoteWheelBounds"))) {Bounds->SetWidthOverride(Diameter);Bounds->SetHeightOverride(Diameter);}
    if(auto* Bounds=WidgetTree->FindWidget<USizeBox>(TEXT("NewEmoteListBounds"))) {Bounds->SetWidthOverride(ListWidth);Bounds->SetHeightOverride(MainRowHeight);}
    for(int32 Index=0;Index<3;++Index) if(auto* Caption=WidgetTree->FindWidget<UTextBlock>(MCUnifiedEmotePrivate::Indexed(TEXT("NewEmoteSector"),Index))) {
        const float Mid=FMath::DegreesToRadians(-90.f+Index*120);const float Width=Diameter*(Index==0?.64f:.39f);
        Caption->SetAutoWrapText(false);Caption->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);Caption->SetWrapTextAt(FMath::Max(1.f,Width));
        if(auto* Cell=Cast<UCanvasPanelSlot>(Caption->Slot)) {Cell->SetPosition(FVector2D(FMath::Cos(Mid),FMath::Sin(Mid))*Diameter*.318f);Cell->SetAutoSize(true);}
    }
    for(const auto& Frame:ChoiceViews) if(auto* Caption=Cast<UTextBlock>(Frame->GetContentForSlot(TEXT("Content")))) {Caption->SetAutoWrapText(false);Caption->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);Caption->SetWrapTextAt(FMath::Max(1.f,ListWidth-82.f));}
    Wrap(TEXT("NewEmoteHeading"),ListWidth-62.f);Wrap(TEXT("NewEmoteHint"),ListWidth-42.f);LastViewport=Area;
}
void UMCUnifiedEmoteWidget::Present(const FMCEmotePresentationSnapshot& Snapshot)
{
    if(!WidgetTree) return;const int32 OldCategory=LastSnapshot.SelectedCategory,OldChoice=LastSnapshot.SelectedChoice;LastSnapshot=Snapshot;
    auto Copy=[&](FName Name,const FText& Value) {if(auto* Caption=WidgetTree->FindWidget<UTextBlock>(Name)) Caption->SetText(Value);};
    Copy(TEXT("NewEmoteHeading"),Snapshot.Heading);Copy(TEXT("NewEmoteHint"),Snapshot.HintText);Copy(TEXT("NewEmoteFooter"),Snapshot.FooterText);Copy(TEXT("NewEmoteColorHeading"),Snapshot.ColorHeadingText);
    for(int32 Index=0;Index<3;++Index) {Copy(MCUnifiedEmotePrivate::Indexed(TEXT("NewEmoteSector"),Index),Snapshot.CategoryLabels[Index]);if(auto* Sector=WidgetTree->FindWidget<UMCUniversalUIWidget>(MCUnifiedEmotePrivate::Indexed(TEXT("NewEmoteSectorShape"),Index))) {Sector->CustomFill=MCUnifiedEmotePrivate::Color(Index==Snapshot.SelectedCategory?TEXT("AD4672"):TEXT("374F56"));Sector->ApplyTheme();}}
    UpdateChoices(Snapshot);UpdateLayout();
    if(OldCategory!=Snapshot.SelectedCategory || OldChoice!=Snapshot.SelectedChoice) if(auto* List=WidgetTree->FindWidget<UScrollBox>(TEXT("NewEmoteListScroll"))) {
        if(ChoiceViews.IsValidIndex(Snapshot.SelectedChoice)) List->ScrollWidgetIntoView(ChoiceViews[Snapshot.SelectedChoice],true,EDescendantScrollDestination::IntoView,8);else List->ScrollToStart();
    }
}
void UMCUnifiedEmoteWidget::PreparePresentation()
{
    if(bPrepared || bPreparing) return;TGuardValue<bool> Guard(bPreparing,true);BuildPresentationTree();if(!WidgetTree || !GetResolvedTheme() || !FSlateApplication::IsInitialized()) return;
    const auto Renderer=FSlateApplication::Get().GetRenderer();if(!Renderer) return;const auto Measure=Renderer->GetFontMeasureService();TArray<FText> Known;
    const FName Keys[]={TEXT("UI.Emote.Category.Face"),TEXT("UI.Emote.Category.Gesture"),TEXT("UI.Emote.Category.Signal"),TEXT("UI.Emote.CategoryShortcut"),TEXT("UI.Emote.Signal.Attention"),TEXT("UI.Emote.Signal.Help"),TEXT("UI.Emote.SignalHint"),TEXT("UI.Emote.GestureHint"),TEXT("UI.Emote.FaceHint"),TEXT("UI.Emote.PlayerColor"),TEXT("UI.Emote.Controls.Menu"),TEXT("UI.Emote.Controls.Hold"),TEXT("UI.Emote.Color.Blue"),TEXT("UI.Emote.Color.Coral"),TEXT("UI.Emote.Color.Mint"),TEXT("UI.Emote.Color.Lilac")};
    for(FName Key:Keys) Known.Add(Localized(Key,FText::GetEmpty()));if(auto* Source=Backend.Get()) {const auto Snapshot=Source->GetUnifiedPresentationSnapshot();Known.Append(Snapshot.KnownChoiceLabels);for(const auto& Token:Snapshot.Choices) Known.Add(Token.Label);}
    const FText AllCopy=FText::Join(FText::AsCultureInvariant(TEXT(" ")),Known);const float FontScale=FMath::Max(.001f,GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale());bool Ready=true;
    for(const TCHAR* Name:MCUnifiedEmotePrivate::Labels) {auto* Caption=WidgetTree->FindWidget<UTextBlock>(Name);Ready&=Caption && Caption->GetFont().HasValidFont();if(Caption) Measure->Measure(AllCopy,Caption->GetFont(),FontScale);}
    for(int32 Index=0;Index<3;++Index) Ready&=WidgetTree->FindWidget<UMCUniversalUIWidget>(MCUnifiedEmotePrivate::Indexed(TEXT("NewEmoteSectorShape"),Index))!=nullptr;
    Measure->Measure(AllCopy,GetResolvedTheme()->MakeFont(22,true),FontScale);Ready&=WidgetTree->FindWidget<UCanvasPanel>(TEXT("NewEmoteWheelCanvas")) && WidgetTree->FindWidget<UVerticalBox>(TEXT("NewEmoteItems")) && WidgetTree->FindWidget<UWrapBox>(TEXT("NewEmoteColors"));
    PreparedCulture=IsDesignTime()?PreviewCulture:FInternationalization::Get().GetCurrentLanguage()->GetName();PreparedFontScale=FontScale;bPrepared=Ready;
}
void UMCUnifiedEmoteWidget::SetBackend(UMCEmoteWidget* Source)
{
    auto* Candidate=Source && Source->GetWorld()==GetWorld() && Source->GetOwningPlayer()==GetOwningPlayer()?Source:nullptr;
    if(Backend.Get()!=Candidate) {if(auto* Previous=Backend.Get()) Previous->ClearUnifiedFocusView(this);bPrepared=false;}Backend=Candidate;if(!IsDesignTime()) {PreparePresentation();RefreshFromBackend();}
}
void UMCUnifiedEmoteWidget::RefreshFromBackend() {if(!IsDesignTime()) if(auto* Source=Backend.Get()) {PreparePresentation();Present(Source->GetUnifiedPresentationSnapshot());}}
bool UMCUnifiedEmoteWidget::CanForward() const {return !IsDesignTime() && bPrepared && IsVisible() && Backend.IsValid() && LastSnapshot.bVisible;}
void UMCUnifiedEmoteWidget::ForwardChoice(const UMCUnifiedEmoteBinding* Binding,bool bHoverOnly) {if(Binding && CanForward()) {Backend->TryActivateUnifiedChoice(Binding->Token,bHoverOnly);RefreshFromBackend();}}
bool UMCUnifiedEmoteWidget::RouteSourceKeyDown(const FKeyEvent& Event) {if(!CanForward()) return false;const bool Handled=Backend->RouteUnifiedKeyDown(Event).IsEventHandled();RefreshFromBackend();return Handled;}
bool UMCUnifiedEmoteWidget::RouteSourceKeyUp(const FKeyEvent& Event) {if(!CanForward()) return false;const bool Handled=Backend->RouteUnifiedKeyUp(Event).IsEventHandled();RefreshFromBackend();return Handled;}
FReply UMCUnifiedEmoteWidget::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) {return RouteSourceKeyDown(Event)?FReply::Handled():Super::NativeOnKeyDown(Geometry,Event);}
FReply UMCUnifiedEmoteWidget::NativeOnKeyUp(const FGeometry& Geometry,const FKeyEvent& Event) {return RouteSourceKeyUp(Event)?FReply::Handled():Super::NativeOnKeyUp(Geometry,Event);}
FReply UMCUnifiedEmoteWidget::NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if(CanForward() && LastSnapshot.bHasGeometry) if(auto* Wheel=WidgetTree->FindWidget<UCanvasPanel>(TEXT("NewEmoteWheelCanvas"))) {
        const auto& WheelGeometry=Wheel->GetTickSpaceGeometry();const float Radius=WheelGeometry.GetLocalSize().X*(210.f/428.f)*WheelGeometry.GetAccumulatedLayoutTransform().GetScale();
        if(Radius>1 && LastSnapshot.OuterRadiusAbsolute>0) {
            const FVector2D Center=WheelGeometry.LocalToAbsolute(WheelGeometry.GetLocalSize()*.5);const float Ratio=LastSnapshot.OuterRadiusAbsolute/Radius;
            const FVirtualPointerPosition Position(LastSnapshot.WheelCenterAbsolute+(FVector2D(Event.GetScreenSpacePosition())-Center)*Ratio,LastSnapshot.WheelCenterAbsolute+(FVector2D(Event.GetLastScreenSpacePosition())-Center)*Ratio);
            // Translate cosmetic space only; the original owner still selects the category.
            const auto Reply=Backend->RouteUnifiedPointerDown(FPointerEvent::MakeTranslatedEvent(Event,Position));if(Reply.IsEventHandled()) {RefreshFromBackend();return FReply::Handled();}
        }
    }
    return Super::NativeOnMouseButtonDown(Geometry,Event);
}
void UMCUnifiedEmoteWidget::NativeOnFocusLost(const FFocusEvent& Event) {Super::NativeOnFocusLost(Event);if(auto* Source=Backend.Get()) Source->NotifyUnifiedFocusLost(this);}
void UMCUnifiedEmoteWidget::ApplyTheme()
{
    Super::ApplyTheme();if(!WidgetTree || !GetResolvedTheme()) return;
    for(int32 Index=0;Index<UE_ARRAY_COUNT(MCUnifiedEmotePrivate::Labels);++Index) if(auto* Caption=WidgetTree->FindWidget<UTextBlock>(MCUnifiedEmotePrivate::Labels[Index])) Caption->SetFont(GetResolvedTheme()->MakeFont(MCUnifiedEmotePrivate::Sizes[Index],true));
    for(const auto& Frame:ChoiceViews) if(auto* Caption=Cast<UTextBlock>(Frame->GetContentForSlot(TEXT("Content")))) Caption->SetFont(GetResolvedTheme()->MakeFont(22,true));
    for(const auto& Frame:ColorViews) if(auto* Caption=Cast<UTextBlock>(Frame->GetContentForSlot(TEXT("Content")))) Caption->SetFont(GetResolvedTheme()->MakeFont(22,true));
    bPrepared=false;UpdateLayout();if(!bPreparing) PreparePresentation();
}
void UMCUnifiedEmoteWidget::NativeConstruct() {Super::NativeConstruct();PreparePresentation();RefreshFromBackend();}
void UMCUnifiedEmoteWidget::NativeDestruct() {if(auto* Source=Backend.Get()) Source->ClearUnifiedFocusView(this);bPrepared=false;Super::NativeDestruct();}
void UMCUnifiedEmoteWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);if(!LastViewport.Equals(Geometry.GetLocalSize(),.5f)) UpdateLayout();if(IsDesignTime()) return;const FString Culture=FInternationalization::Get().GetCurrentLanguage()->GetName();
    if(Culture!=PreparedCulture || !FMath::IsNearlyEqual(PreparedFontScale,Geometry.GetAccumulatedLayoutTransform().GetScale(),.001f)) bPrepared=false;
    PreparePresentation();ViewRefreshElapsed+=DeltaSeconds;if(ViewRefreshElapsed>=.1f) {ViewRefreshElapsed=0;RefreshFromBackend();}if(!LastViewport.Equals(Geometry.GetLocalSize(),.5)) UpdateLayout();
}
void UMCUnifiedEmoteWidget::OnLocalizationChanged() {Super::OnLocalizationChanged();bPrepared=false;PreparePresentation();if(!IsDesignTime()) RefreshFromBackend();}
void UMCUnifiedEmoteWidget::PresentPreview()
{
    if(!IsDesignTime()) return;FMCEmotePresentationSnapshot Sample;Sample.bVisible=true;Sample.SelectedCategory=FMath::Clamp(PreviewCategory,0,2);
    if(PreviewState==TEXT("Signals")) Sample.SelectedCategory=2;else if(PreviewState==TEXT("Gestures")) Sample.SelectedCategory=1;
    Sample.bHeld=PreviewState==TEXT("Held") || PreviewState==TEXT("HeldNoSelection");Sample.SelectedChoice=PreviewState==TEXT("HeldNoSelection")?INDEX_NONE:0;
    auto Copy=[&](FName Key,const FText& Fallback) {return UMCUIDataLibrary::ResolveText(GetResolvedTranslationTable(),Key,Fallback,PreviewCulture);};
    const FText Categories[]={Copy(TEXT("UI.Emote.Category.Face"),LOCTEXT("Face","Мимика")),Copy(TEXT("UI.Emote.Category.Gesture"),LOCTEXT("Gesture","Жесты")),Copy(TEXT("UI.Emote.Category.Signal"),LOCTEXT("Signal","Сигналы"))};
    for(int32 Index=0;Index<3;++Index) Sample.CategoryLabels[Index]=UMCUIDataLibrary::FormatTextForCulture(Copy(TEXT("UI.Emote.CategoryShortcut"),LOCTEXT("CategoryShortcut","{Shortcut}  {Category}")),FFormatNamedArguments{{TEXT("Shortcut"),Index+1},{TEXT("Category"),Categories[Index]}},PreviewCulture);
    Sample.Heading=Categories[Sample.SelectedCategory];Sample.ColorHeadingText=Copy(TEXT("UI.Emote.PlayerColor"),LOCTEXT("PlayerColor","Цвет игрока"));
    Sample.HintText=Sample.SelectedCategory==2?Copy(TEXT("UI.Emote.SignalHint"),LOCTEXT("SignalHint","Сигнал видят все игроки над твоим персонажем.")):Sample.SelectedCategory==1?Copy(TEXT("UI.Emote.GestureHint"),LOCTEXT("GestureHint","Жесты — стоя на земле. Движение, работа и боль прерывают жест.")):Copy(TEXT("UI.Emote.FaceHint"),LOCTEXT("FaceHint","Выбери выражение лица."));
    Sample.FooterText=Sample.bHeld?Copy(TEXT("UI.Emote.Controls.Hold"),LOCTEXT("HoldControls","Удерживай T — выбор · Отпусти T — подтвердить · 1 / 2 / 3 — раздел · ↑ / ↓ — выбор · Esc — отмена")):Copy(TEXT("UI.Emote.Controls.Menu"),LOCTEXT("MenuControls","1 / 2 / 3 — раздел · ↑ / ↓ — выбор · Enter — подтвердить · Esc — отмена"));
    auto Add=[&](FName Id,const FText& Caption,EMCPlayerAlarm Alarm) {FMCEmotePresentationChoice Token;Token.Index=Sample.Choices.Num();Token.ExpectedCategory=Sample.SelectedCategory;Token.Id=Id;Token.Alarm=Alarm;Token.Label=Caption;Token.bEnabled=bPreviewActionsAvailable && PreviewState!=TEXT("Disabled");Token.bSelected=Token.Index==Sample.SelectedChoice;Sample.Choices.Add(Token);};
    if(PreviewState!=TEXT("Empty")) {
        if(Sample.SelectedCategory==2) {Add(NAME_None,Copy(TEXT("UI.Emote.Signal.Attention"),LOCTEXT("Attention","!  Внимание")),EMCPlayerAlarm::Attention);Add(NAME_None,Copy(TEXT("UI.Emote.Signal.Help"),LOCTEXT("Help","+  Нужна помощь")),EMCPlayerAlarm::Help);}
        else {RetainedPreviewLibrary=PreviewLibrary.LoadSynchronous();if(RetainedPreviewLibrary) for(const auto& Entry:RetainedPreviewLibrary->Entries) if((Entry.bFaceOnly || !Entry.Animation)==(Sample.SelectedCategory==0)) Add(Entry.Id,Copy(FName(*(TEXT("UI.Emote.Entry.")+Entry.Id.ToString()+TEXT(".Label"))),Entry.Label),EMCPlayerAlarm::None);}
    }
    const FText ColorNames[]={Copy(TEXT("UI.Emote.Color.Blue"),LOCTEXT("Blue","Синий")),Copy(TEXT("UI.Emote.Color.Coral"),LOCTEXT("Coral","Коралл")),Copy(TEXT("UI.Emote.Color.Mint"),LOCTEXT("Mint","Мята")),Copy(TEXT("UI.Emote.Color.Lilac"),LOCTEXT("Lilac","Сирень"))};
    for(int32 Index=0;Index<4;++Index) {FMCEmotePresentationChoice Token;Token.Index=Index;Token.bColorChoice=true;Token.bEnabled=bPreviewActionsAvailable && PreviewState!=TEXT("Disabled");Token.Label=ColorNames[Index];Token.Color=MCUnifiedEmotePrivate::PlayerColors[Index];Sample.Colors.Add(Token);}
    if(PreviewState==TEXT("LongText") || PreviewScenario==EMCGameplayUIPreviewScenario::LongText) for(auto& Token:Sample.Choices) Token.Label=FText::Join(FText::AsCultureInvariant(TEXT(" ")),TArray<FText>{Token.Label,Token.Label,Token.Label});Present(Sample);
}
void UMCUnifiedEmoteWidget::ApplyEditorPreview(float Time) {if(!IsDesignTime()) {RefreshFromBackend();return;}BuildPresentationTree();PreparePresentation();PresentPreview();UpdateLayout();SetRenderOpacity(1);}
#undef LOCTEXT_NAMESPACE
