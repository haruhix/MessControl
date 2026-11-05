#include "MCUnifiedTeamWidget.h"

#include "MCGameplayUIFactory.h"
#include "MCUIDataLibrary.h"
#include "MCUniversalUIWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "InputCoreTypes.h"
#include "GameFramework/PlayerState.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SLeafWidget.h"

#define LOCTEXT_NAMESPACE "MCUnifiedTeam"

namespace
{
    FLinearColor MCUnifiedTeamPrivate_Color(const TCHAR* Hex) {return FLinearColor(FColor::FromHex(Hex));}
    bool MCUnifiedTeamPrivate_OwnVisible(const UWidget* Widget)
    {
        return Widget && Widget->GetVisibility()!=ESlateVisibility::Collapsed && Widget->GetVisibility()!=ESlateVisibility::Hidden;
    }
}

class SMCUnifiedMicrophone final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SMCUnifiedMicrophone) {}
        SLATE_ARGUMENT(TWeakObjectPtr<UMCUnifiedMicrophone>,Owner)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args) {Owner=Args._Owner;}
    virtual FVector2D ComputeDesiredSize(float) const override {return FVector2D(28,34);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& Geometry,const FSlateRect&,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bParentEnabled) const override
    {
        const auto* Widget=Owner.Get();if(!Widget) return Layer;
        const FVector2D Size=Geometry.GetLocalSize();
        const FVector2D Scale(Size.X/28.f,Size.Y/34.f);
        FLinearColor Tint=(Widget->bMuted?Widget->MutedTint:Widget->Tint)*Style.GetColorAndOpacityTint();
        const ESlateDrawEffect Effect=bParentEnabled?ESlateDrawEffect::None:ESlateDrawEffect::DisabledEffect;
        const FSlateRoundedBoxBrush Body(Tint,6.f);
        FSlateDrawElement::MakeBox(Elements,Layer,Geometry.ToPaintGeometry(FVector2D(12,20)*Scale,FSlateLayoutTransform(FVector2D(8,1)*Scale)),&Body,Effect,Tint);
        auto Stroke=[&](TArray<FVector2D> Points) {
            for(auto& Point:Points) Point*=Scale;
            FSlateDrawElement::MakeLines(Elements,Layer+1,Geometry.ToPaintGeometry(),Points,Effect,Tint,true,2.f*FMath::Min(Scale.X,Scale.Y));
        };
        TArray<FVector2D> Cup;for(int32 I=0;I<=12;++I) {const float Angle=PI*I/12.f;Cup.Add(FVector2D(14+10*FMath::Cos(Angle),15+12*FMath::Sin(Angle)));}
        Stroke(Cup);Stroke({{14,27},{14,32}});Stroke({{8,32},{20,32}});
        if(Widget->bMuted) Stroke({{2,2},{26,30}});
        return Layer+1;
    }
private:
    TWeakObjectPtr<UMCUnifiedMicrophone> Owner;
};

void UMCUnifiedMicrophone::SetMuted(bool Muted) {if(bMuted!=Muted) {bMuted=Muted;InvalidateLayoutAndVolatility();}}
TSharedRef<SWidget> UMCUnifiedMicrophone::RebuildWidget() {return SNew(SMCUnifiedMicrophone).Owner(this);}

void UMCUnifiedPartyButton::SetBackend(UMCScoreboardWidget* InBackend)
{
    Backend=InBackend;OnClicked.AddUniqueDynamic(this,&ThisClass::Forward);
}
void UMCUnifiedPartyButton::Forward()
{
    if(!Backend.IsValid() || !Backend->WidgetTree || Backend->IsDesignTime()) return;
    if(auto* Source=Backend->WidgetTree->FindWidget<UButton>(SourceControl);Source && Source->GetIsEnabled() && MCUnifiedTeamPrivate_OwnVisible(Source))
        Source->OnClicked.Broadcast();
}

UMCUnifiedTeamWidget::UMCUnifiedTeamWidget()
{
    bUseNativeLayout=false;bOverridePreviewUIVersion=true;SetIsFocusable(true);
}
void UMCUnifiedTeamWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(!Theme) Theme=UMCGameplayUIFactory::GetThemeForVersion(EMCUIVersion::New);
    if(WidgetTree->RootWidget) {EnsureRowsViewport();CacheControls();return;}
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("TeamRoot"));WidgetTree->RootWidget=Root;
    // The card's measured text/content determines height. There is no font scaling.
    auto* Width=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("TeamDesignSize"));
    Width->SetMaxDesiredWidth(1100);
    auto* RootSlot=Root->AddChildToCanvas(Width);RootSlot->SetAnchors(FAnchors(.5f,.5f));
    RootSlot->SetAlignment(FVector2D(.5f,.5f));RootSlot->SetAutoSize(true);RootSlot->SetPosition(FVector2D::ZeroVector);
    auto* Stack=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("TeamStack"));Width->SetContent(Stack);
    auto Shape=[&](FName Name,FLinearColor Fill,float Radius,float Border,FVector2D Shadow,FMargin InPadding) {
        auto* Panel=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),Name);
        Panel->Kind=EMCUniversalUIKind::Panel;Panel->Label=FText::GetEmpty();Panel->bCustomColors=true;Panel->bUseThemeShape=false;
        Panel->CustomFill=Fill;Panel->CustomOutline=MCUnifiedTeamPrivate_Color(TEXT("2B2B83"));Panel->CustomShadow=MCUnifiedTeamPrivate_Color(TEXT("29296C"));
        Panel->CornerRadius=Radius;Panel->OutlineWidth=Border;Panel->ShadowOffset=Shadow;Panel->ContentPadding=InPadding;
        Panel->Initialize();Panel->BuildPresentationTree();return Panel;
    };
    auto Label=[&](FName Name,FText Text,int32 Size,FLinearColor Tint,bool Wrap=true) {
        auto* Value=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);Value->SetText(Text);
        Value->SetAutoWrapText(Wrap);Value->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
        Value->SetClipping(EWidgetClipping::Inherit);Value->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
        Value->SetFont(GetResolvedTheme()->MakeFont(Size,true));Value->SetColorAndOpacity(Tint);return Value;
    };
    auto* Card=Shape(TEXT("TeamCard"),MCUnifiedTeamPrivate_Color(TEXT("4C79AC")),35,3,{7,10},FMargin(42,38,42,42));
    auto* CardSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("TeamCardSize"));CardSize->SetContent(Card);
    Stack->AddChildToVerticalBox(CardSize)->SetPadding(FMargin(0,0,10,12));
    auto* Body=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("TeamCardContents"));Card->SetContentForSlot(TEXT("Content"),Body);
    auto* Header=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("TeamHeader"));
    Body->AddChildToVerticalBox(Header)->SetPadding(FMargin(0,0,10,30));
    for(int32 Index=0;Index<2;++Index)
    {
        const bool Title=Index==0;
        auto* Badge=Shape(Title?TEXT("TeamTitleBadge"):TEXT("TeamTabBadge"),MCUnifiedTeamPrivate_Color(TEXT("FFF9F8")),22,3,{5,8},FMargin(22,7));
        auto* Caption=Label(Title?TEXT("TeamTitle"):TEXT("TeamTabKey"),
            Title?Localized(TEXT("UI.Scoreboard.Title"),LOCTEXT("Team","КОМАНДА")):FText::AsCultureInvariant(TEXT("TAB")),48,
            MCUnifiedTeamPrivate_Color(Title?TEXT("AE466D"):TEXT("BF151C")),Title);
        Caption->SetJustification(ETextJustify::Center);Badge->SetContentForSlot(TEXT("Content"),Caption);
        UWidget* BadgeContents=Badge;
        if(Title) {
            auto* BadgeBounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("TeamTitleBounds"));
            BadgeBounds->SetMaxDesiredWidth(620);BadgeBounds->SetContent(Badge);BadgeContents=BadgeBounds;
        }
        auto* Cell=Header->AddChildToHorizontalBox(BadgeContents);Cell->SetVerticalAlignment(VAlign_Center);
        if(Title) {
            auto* Spacer=WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(),TEXT("TeamHeaderSpacer"));
            Header->AddChildToHorizontalBox(Spacer)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        }
    }
    TextBindings.Add(TEXT("TeamTitle"),TEXT("UI.Scoreboard.Title"));
    for(int32 I=0;I<4;++I)
    {
        const FString Prefix=FString::Printf(TEXT("TeamPlayer%d"),I+1);
        auto* Row=Shape(FName(Prefix),MCUnifiedTeamPrivate_Color(TEXT("FFF9F8")),17,3,{0,7},FMargin(38,10));
        auto* Minimum=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(Prefix+TEXT("Size")));Minimum->SetMinDesiredHeight(78);Minimum->SetContent(Row);
        Body->AddChildToVerticalBox(Minimum)->SetPadding(FMargin(0,0,0,I<3?12:0));
        auto* Contents=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),FName(Prefix+TEXT("Contents")));Row->SetContentForSlot(TEXT("Content"),Contents);
        auto* Name=Label(FName(Prefix+TEXT("Name")),FText::AsCultureInvariant(TEXT("HASBULADO")),25,MCUnifiedTeamPrivate_Color(TEXT("2B2B83")));
        auto* NameCell=Contents->AddChildToHorizontalBox(Name);NameCell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));NameCell->SetVerticalAlignment(VAlign_Center);NameCell->SetPadding(FMargin(0,0,16,0));
        auto* Mic=WidgetTree->ConstructWidget<UMCUnifiedMicrophone>(UMCUnifiedMicrophone::StaticClass(),FName(Prefix+TEXT("Microphone")));Mic->Tint=MCUnifiedTeamPrivate_Color(TEXT("2B2B83"));Mic->MutedTint=MCUnifiedTeamPrivate_Color(TEXT("B13E68"));
        auto* MicSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(Prefix+TEXT("MicrophoneSize")));MicSize->SetWidthOverride(28);MicSize->SetHeightOverride(34);MicSize->SetContent(Mic);
        auto* Mute=WidgetTree->ConstructWidget<UMCScoreboardMuteButton>(UMCScoreboardMuteButton::StaticClass(),FName(Prefix+TEXT("Mute")));Mute->SetContent(MicSize);
        auto* MicSlot=CastChecked<UButtonSlot>(MicSize->Slot);MicSlot->SetPadding(FMargin(7,3));MicSlot->SetHorizontalAlignment(HAlign_Center);MicSlot->SetVerticalAlignment(VAlign_Center);
        FButtonStyle Style;Style.SetNormal(FSlateNoResource());Style.SetHovered(FSlateRoundedBoxBrush(MCUnifiedTeamPrivate_Color(TEXT("DCE5FD")),10.f));Style.SetPressed(FSlateRoundedBoxBrush(MCUnifiedTeamPrivate_Color(TEXT("BDCFF0")),10.f));Style.SetDisabled(FSlateNoResource());Mute->SetStyle(Style);
        auto* MicCell=Contents->AddChildToHorizontalBox(Mute);MicCell->SetVerticalAlignment(VAlign_Center);MicCell->SetPadding(FMargin(0,0,20,0));
        auto* Points=Label(FName(Prefix+TEXT("Points")),FText::AsNumber(300),25,MCUnifiedTeamPrivate_Color(TEXT("2B2B83")));Points->SetJustification(ETextJustify::Center);
        auto* PointsSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(Prefix+TEXT("PointsSize")));PointsSize->SetMaxDesiredWidth(200);PointsSize->SetContent(Points);
        auto* PointsCell=Contents->AddChildToHorizontalBox(PointsSize);PointsCell->SetVerticalAlignment(VAlign_Center);PointsCell->SetPadding(FMargin(16,0,28,0));
        auto* Ping=Label(FName(Prefix+TEXT("Ping")),FText::AsCultureInvariant(TEXT("0 MS")),25,MCUnifiedTeamPrivate_Color(TEXT("2B2B83")));Ping->SetJustification(ETextJustify::Right);
        auto* PingSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(Prefix+TEXT("PingSize")));PingSize->SetMaxDesiredWidth(220);PingSize->SetContent(Ping);
        auto* PingCell=Contents->AddChildToHorizontalBox(PingSize);PingCell->SetVerticalAlignment(VAlign_Center);PingCell->SetPadding(FMargin(20,0,0,0));
    }
    // Hub controls keep source permissions/actions, with content-sized buttons flowing onto new lines.
    auto* Hub=Shape(TEXT("TeamHubControls"),MCUnifiedTeamPrivate_Color(TEXT("4C79AC")),24,3,{7,8},FMargin(22,16));
    auto* HubSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("TeamHubSize"));HubSize->SetContent(Hub);
    Stack->AddChildToVerticalBox(HubSize)->SetPadding(FMargin(0,4,10,10));
    auto* HubBody=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("TeamHubContents"));Hub->SetContentForSlot(TEXT("Content"),HubBody);
    auto* Summary=Label(TEXT("TeamHubSummary"),FText::GetEmpty(),22,MCUnifiedTeamPrivate_Color(TEXT("FFF9F8")));HubBody->AddChildToVerticalBox(Summary)->SetPadding(FMargin(0,0,0,12));
    auto* Actions=WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(),TEXT("TeamHubActions"));Actions->SetInnerSlotPadding(FVector2D(10,10));Actions->SetExplicitWrapSize(false);
    HubBody->AddChildToVerticalBox(Actions)->SetPadding(FMargin(0,0,0,12));
    const TCHAR* Sources[]={TEXT("HubPartyInvite"),TEXT("HubPartyPublic"),TEXT("HubPartyFriends"),TEXT("HubPartyInviteOnly"),TEXT("HubPartyClose")};
    for(int32 I=0;I<5;++I)
    {
        auto* Button=WidgetTree->ConstructWidget<UMCUnifiedPartyButton>(UMCUnifiedPartyButton::StaticClass(),FName(Sources[I]));Button->SourceControl=Sources[I];
        FButtonStyle Style;Style.SetNormal(FSlateRoundedBoxBrush(MCUnifiedTeamPrivate_Color(TEXT("FFF9F8")),13.f,MCUnifiedTeamPrivate_Color(TEXT("2B2B83")),2.f));Style.SetHovered(FSlateRoundedBoxBrush(MCUnifiedTeamPrivate_Color(TEXT("E4EDFF")),13.f,MCUnifiedTeamPrivate_Color(TEXT("2B2B83")),2.f));Style.SetPressed(FSlateRoundedBoxBrush(MCUnifiedTeamPrivate_Color(TEXT("CCDDF7")),13.f,MCUnifiedTeamPrivate_Color(TEXT("2B2B83")),2.f));Style.SetDisabled(Style.Normal);Button->SetStyle(Style);
        const FName CopyName(*(FString(Sources[I])+TEXT("Caption")));
        auto* Caption=Label(CopyName,FText::GetEmpty(),19,MCUnifiedTeamPrivate_Color(TEXT("2B2B83")));Caption->SetJustification(ETextJustify::Center);Button->SetContent(Caption);
        auto* CaptionSlot=CastChecked<UButtonSlot>(Caption->Slot);CaptionSlot->SetPadding(FMargin(18,10));CaptionSlot->SetVerticalAlignment(VAlign_Center);CaptionSlot->SetHorizontalAlignment(HAlign_Center);
        auto* Bounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(FString(Sources[I])+TEXT("Size")));
        Bounds->SetMinDesiredHeight(48);Bounds->SetMaxDesiredWidth(760);Bounds->SetContent(Button);
        Actions->AddChildToWrapBox(Bounds)->SetVerticalAlignment(VAlign_Center);
    }
    auto* Status=Label(TEXT("TeamHubStatus"),FText::GetEmpty(),19,MCUnifiedTeamPrivate_Color(TEXT("FFF9F8")));HubBody->AddChildToVerticalBox(Status);
    EnsureRowsViewport();CacheControls();ApplyTheme();
}
void UMCUnifiedTeamWidget::EnsureRowsViewport()
{
    if(!WidgetTree) return;
    auto* Body=WidgetTree->FindWidget<UVerticalBox>(TEXT("TeamCardContents"));
    auto* Header=WidgetTree->FindWidget<UHorizontalBox>(TEXT("TeamHeader"));
    if(!Body || !Header || Header->GetParent()!=Body) return;
    if(WidgetTree->FindWidget(TEXT("TeamRowsViewport")) || WidgetTree->FindWidget(TEXT("TeamRowsScroll")) || WidgetTree->FindWidget(TEXT("TeamRows"))) return;
    USizeBox* PlayerBounds[4];FMargin Insets[4];
    if(Body->GetChildrenCount()!=5) return;
    for(int32 I=0;I<4;++I) {
        PlayerBounds[I]=WidgetTree->FindWidget<USizeBox>(FName(*FString::Printf(TEXT("TeamPlayer%dSize"),I+1)));
        if(!PlayerBounds[I] || PlayerBounds[I]->GetParent()!=Body) return;
        const auto* Cell=Cast<UVerticalBoxSlot>(PlayerBounds[I]->Slot);Insets[I]=Cell?Cell->GetPadding():FMargin(0,0,0,12);
    }
    auto* Viewport=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("TeamRowsViewport"));
    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("TeamRowsScroll"));Scroll->SetAllowOverscroll(false);Scroll->SetScrollbarThickness(FVector2D(5,5));Scroll->SetScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll);
    auto* PlayerRows=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("TeamRows"));Viewport->SetContent(Scroll);Scroll->AddChild(PlayerRows);
    for(int32 I=0;I<4;++I) {PlayerBounds[I]->RemoveFromParent();PlayerRows->AddChildToVerticalBox(PlayerBounds[I])->SetPadding(Insets[I]);}
    Body->AddChildToVerticalBox(Viewport);
}
void UMCUnifiedTeamWidget::UpdateLayout()
{
    if(!WidgetTree) return;
    const FVector2D Area=GetPaintSpaceGeometry().GetLocalSize();if(Area.X<=1 || Area.Y<=1) return;LastLayoutViewport=Area;
    auto* WidthBounds=WidgetTree->FindWidget<USizeBox>(TEXT("TeamDesignSize"));
    auto* CardBounds=WidgetTree->FindWidget<USizeBox>(TEXT("TeamCardSize"));
    auto* Card=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("TeamCard"));
    auto* Viewport=WidgetTree->FindWidget<USizeBox>(TEXT("TeamRowsViewport"));
    auto* PlayerRows=WidgetTree->FindWidget<UVerticalBox>(TEXT("TeamRows"));
    auto* HubBounds=WidgetTree->FindWidget<USizeBox>(TEXT("TeamHubSize"));
    if(!WidthBounds || !CardBounds || !Card || !Viewport || !PlayerRows) return;
    // Retain the PSD's vertical breathing room as well as its overall width.
    // A saved compact tree receives the same authored proportions in place.
    if(Card->ContentPadding!=FMargin(42,38,42,42)) {Card->ContentPadding=FMargin(42,38,42,42);Card->ApplyTheme();}
    if(auto* Header=WidgetTree->FindWidget(TEXT("TeamHeader"))) if(auto* Cell=Cast<UVerticalBoxSlot>(Header->Slot)) Cell->SetPadding(FMargin(0,0,10,30));
    const float Width=FMath::Max(1.f,FMath::Min(1100.f,float(Area.X)-16.f));
    const float InnerWidth=FMath::Max(1.f,Width-10.f-Card->ContentPadding.Left-Card->ContentPadding.Right);
    WidthBounds->ClearMinDesiredWidth();WidthBounds->SetMaxDesiredWidth(Width);WidthBounds->SetWidthOverride(Width);
    WidthBounds->ClearHeightOverride();WidthBounds->ClearMinDesiredHeight();WidthBounds->ClearMaxDesiredHeight();
    CardBounds->ClearHeightOverride();
    if(auto* Cell=Cast<UCanvasPanelSlot>(WidthBounds->Slot)) Cell->SetAutoSize(true);
    if(auto* Cell=Cast<UVerticalBoxSlot>(CardBounds->Slot)) Cell->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
    const float HubInnerWidth=FMath::Max(1.f,Width-54.f);
    if(auto* Actions=WidgetTree->FindWidget<UWrapBox>(TEXT("TeamHubActions"))) {Actions->SetExplicitWrapSize(true);Actions->SetWrapSize(HubInnerWidth);}
    for(const TCHAR* Name:{TEXT("TeamHubSummary"),TEXT("TeamHubStatus")}) if(auto* Caption=WidgetTree->FindWidget<UTextBlock>(Name)) {Caption->SetAutoWrapText(false);Caption->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);Caption->SetWrapTextAt(HubInnerWidth);}
    for(const TCHAR* Name:{TEXT("HubPartyInvite"),TEXT("HubPartyPublic"),TEXT("HubPartyFriends"),TEXT("HubPartyInviteOnly"),TEXT("HubPartyClose")})
        if(auto* Button=WidgetTree->FindWidget<UMCUnifiedPartyButton>(Name)) {
            if(auto* Bounds=Cast<USizeBox>(Button->GetParent())) Bounds->SetMaxDesiredWidth(HubInnerWidth);
            if(auto* Caption=Cast<UTextBlock>(Button->GetContent())) {Caption->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);Caption->SetAutoWrapText(false);Caption->SetWrapTextAt(FMath::Max(1.f,HubInnerWidth-36.f));}
        }
    // Apply the display face only to TEAM and the input badge. Player data uses
    // the existing composite body face, with its unchanged localized fallbacks.
    for(const TCHAR* Name:{TEXT("TeamTitle"),TEXT("TeamTabKey")}) if(auto* Caption=WidgetTree->FindWidget<UTextBlock>(Name)) {
        FSlateFontInfo Font=TeamTitleFont.HasValidFont()?TeamTitleFont:GetResolvedTheme()->MakeFont(48,true);
        Font.Size=48;Caption->SetFont(Font);
        Caption->SetAutoWrapText(false);Caption->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);Caption->SetWrapTextAt(FMath::Max(1.f,InnerWidth-300.f));
    }
    if(auto* TitleBounds=WidgetTree->FindWidget<USizeBox>(TEXT("TeamTitleBounds"))) {TitleBounds->SetMinDesiredWidth(FMath::Min(260.f,InnerWidth*.45f));TitleBounds->SetMaxDesiredWidth(FMath::Max(1.f,InnerWidth-270.f));}
    if(auto* Badge=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("TeamTabBadge"))) {
        auto* Bounds=WidgetTree->FindWidget<USizeBox>(TEXT("TeamTabBounds"));
        if(!Bounds) if(auto* Header=Cast<UHorizontalBox>(Badge->GetParent())) {
            Badge->RemoveFromParent();Bounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("TeamTabBounds"));
            Bounds->SetContent(Badge);Header->AddChildToHorizontalBox(Bounds)->SetVerticalAlignment(VAlign_Center);
        }
        if(Bounds) Bounds->SetWidthOverride(FMath::Min(260.f,InnerWidth*.3f));
    }
    const float RowWidth=FMath::Max(1.f,InnerWidth-76.f);
    constexpr float NameShare=.65f,PointsShare=.15f,PingShare=.20f;
    const float NameWidth=FMath::Max(1.f,(RowWidth-60.f)*NameShare-16.f);
    for(int32 I=0;I<4;++I) {
        const FString Prefix=FString::Printf(TEXT("TeamPlayer%d"),I+1);
        if(auto* Bounds=WidgetTree->FindWidget<USizeBox>(FName(Prefix+TEXT("Size")))) Bounds->SetMinDesiredHeight(78);
        if(auto* Row=WidgetTree->FindWidget<UMCUniversalUIWidget>(FName(Prefix));Row && Row->ContentPadding!=FMargin(38,10)) {Row->ContentPadding=FMargin(38,10);Row->ApplyTheme();}
        if(auto* Name=WidgetTree->FindWidget<UTextBlock>(FName(Prefix+TEXT("Name")))) {
            // User nicknames can contain no Unicode word breaks. Wrap their
            // complete glyphs at the name column; never truncate or scale them.
            Name->SetAutoWrapText(false);Name->SetWrapTextAt(NameWidth);Name->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
            if(auto* Cell=Cast<UHorizontalBoxSlot>(Name->Slot)) {FSlateChildSize Size(ESlateSizeRule::Fill);Size.Value=NameShare;Cell->SetSize(Size);Cell->SetPadding(FMargin(0,0,16,0));}
        }
        if(auto* Mic=WidgetTree->FindWidget<USizeBox>(FName(Prefix+TEXT("MicrophoneSize")))) {Mic->SetWidthOverride(34);Mic->SetHeightOverride(44);}
        if(auto* Mute=WidgetTree->FindWidget(FName(Prefix+TEXT("Mute")))) if(auto* Cell=Cast<UHorizontalBoxSlot>(Mute->Slot)) {Cell->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));Cell->SetPadding(FMargin(0,0,12,0));}
        for(bool Ping:{false,true}) if(auto* Bounds=WidgetTree->FindWidget<USizeBox>(FName(Prefix+(Ping?TEXT("PingSize"):TEXT("PointsSize"))))) {
            Bounds->ClearWidthOverride();Bounds->ClearMinDesiredWidth();Bounds->ClearMaxDesiredWidth();
            const float Share=Ping?PingShare:PointsShare;
            if(auto* Cell=Cast<UHorizontalBoxSlot>(Bounds->Slot)) {FSlateChildSize Size(ESlateSizeRule::Fill);Size.Value=Share;Cell->SetSize(Size);Cell->SetPadding(FMargin(0));}
            if(auto* Caption=Cast<UTextBlock>(Bounds->GetContent())) {Caption->SetAutoWrapText(false);Caption->SetWrapTextAt(FMath::Max(1.f,(RowWidth-60.f)*Share));Caption->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);}
        }
    }
    const float Scale=FMath::Max(.001f,GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale());
    auto MeasuredHeight=[&](UWidget* Widget) {
        if(!Widget || !Widget->IsVisible()) return 0.f;
        const auto Slate=Widget->TakeWidget();Slate->MarkPrepassAsDirty();Slate->SlatePrepass(Scale);
        float Height=FMath::CeilToFloat(Widget->GetDesiredSize().Y);
        if(const auto* Cell=Cast<UVerticalBoxSlot>(Widget->Slot)) Height+=Cell->GetPadding().Top+Cell->GetPadding().Bottom;
        return Height;
    };
    const float HeaderHeight=MeasuredHeight(WidgetTree->FindWidget(TEXT("TeamHeader")));
    const float FramePadding=Card->ContentPadding.Top+Card->ContentPadding.Bottom;
    float RowPadding=0.f;
    if(auto* Cell=Cast<UVerticalBoxSlot>(Viewport->Slot)) {
        Cell->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));Cell->SetVerticalAlignment(VAlign_Top);
        RowPadding=Cell->GetPadding().Top+Cell->GetPadding().Bottom;
    }
    // Measure actual rows rather than the scroll child's previous aggregate allocation.
    // Their authored minimum, wrapped name and microphone sizes remain unchanged.
    float NaturalRowsHeight=0.f,LastRowShadowHeight=0.f;
    for(UWidget* PlayerRow:PlayerRows->GetAllChildren()) {
        if(!PlayerRow || !PlayerRow->IsVisible()) continue;
        NaturalRowsHeight+=MeasuredHeight(PlayerRow);
        if(const auto* RowBounds=Cast<USizeBox>(PlayerRow))
            if(const auto* RowShape=Cast<UMCUniversalUIWidget>(RowBounds->GetContent()))
                LastRowShadowHeight=FMath::Max(0.f,float(RowShape->ShadowOffset.Y));
    }
    // The last row's authored offset shadow paints below its layout bounds.
    // Reserve that extent in both scroll content and its measured viewport.
    // Use a minimum, not an additive update, so saved/repeated passes cannot grow it.
    if(auto* ScrollCell=Cast<UScrollBoxSlot>(PlayerRows->Slot)) {
        FMargin Inset=ScrollCell->GetPadding();Inset.Bottom=FMath::Max(Inset.Bottom,LastRowShadowHeight);
        ScrollCell->SetPadding(Inset);NaturalRowsHeight+=Inset.Top+Inset.Bottom;
    }
    float CardPadding=0.f;
    if(auto* Cell=Cast<UVerticalBoxSlot>(CardBounds->Slot)) {
        Cell->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));Cell->SetVerticalAlignment(VAlign_Top);
        CardPadding=Cell->GetPadding().Top+Cell->GetPadding().Bottom;
    }
    const float Reserved=MeasuredHeight(HubBounds)+HeaderHeight+FramePadding+RowPadding+CardPadding+12.f;
    const float RowsHeight=FMath::Min(NaturalRowsHeight,FMath::Max(0.f,float(Area.Y)-Reserved));
    Viewport->ClearMinDesiredHeight();Viewport->ClearMaxDesiredHeight();Viewport->SetHeightOverride(RowsHeight);
    if(auto* Scroll=WidgetTree->FindWidget<UScrollBox>(TEXT("TeamRowsScroll")))
        Scroll->SetScrollBarVisibility(NaturalRowsHeight>RowsHeight+.5f?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    CardBounds->ClearMinDesiredHeight();CardBounds->ClearMaxDesiredHeight();
    // Retained saved bounds cannot stretch the frame beyond the actual content.
    CardBounds->SetHeightOverride(HeaderHeight+RowsHeight+RowPadding+FramePadding);
}
void UMCUnifiedTeamWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    if(!LastLayoutViewport.Equals(Geometry.GetLocalSize(),.5f)) UpdateLayout();
}
void UMCUnifiedTeamWidget::CacheControls()
{
    Rows.Reset();if(!WidgetTree) return;
    for(int32 I=0;I<4;++I) {
        const FString Prefix=FString::Printf(TEXT("TeamPlayer%d"),I+1);auto& Row=Rows.AddDefaulted_GetRef();
        Row.Surface=WidgetTree->FindWidget<UMCUniversalUIWidget>(FName(Prefix));
        Row.Name=WidgetTree->FindWidget<UTextBlock>(FName(Prefix+TEXT("Name")));Row.Points=WidgetTree->FindWidget<UTextBlock>(FName(Prefix+TEXT("Points")));Row.Ping=WidgetTree->FindWidget<UTextBlock>(FName(Prefix+TEXT("Ping")));
        Row.Microphone=WidgetTree->FindWidget<UMCUnifiedMicrophone>(FName(Prefix+TEXT("Microphone")));Row.MuteButton=WidgetTree->FindWidget<UMCScoreboardMuteButton>(FName(Prefix+TEXT("Mute")));
    }
}
void UMCUnifiedTeamWidget::SetBackend(UMCScoreboardWidget* InBackend)
{
    if(Backend.Get()!=InBackend) if(auto* Previous=Backend.Get()) Previous->ClearUnifiedFocusOwner(this);
    if(InBackend) InBackend->SetUnifiedFocusOwner(this);
    if(Backend.Get()==InBackend) {if(!IsDesignTime()) RefreshFromBackend();return;}
    Backend=InBackend;CacheControls();
    if(WidgetTree) WidgetTree->ForEachWidget([InBackend](UWidget* Widget){if(auto* Button=Cast<UMCUnifiedPartyButton>(Widget)) Button->SetBackend(InBackend);});
    if(!IsDesignTime()) RefreshFromBackend();
}
void UMCUnifiedTeamWidget::ApplyTheme()
{
    Super::ApplyTheme();if(!WidgetTree || !GetResolvedTheme()) return;
    for(int32 I=0;I<4;++I) {
        const FString Prefix=FString::Printf(TEXT("TeamPlayer%d"),I+1);
        for(const TCHAR* Suffix:{TEXT("Name"),TEXT("Points"),TEXT("Ping")}) if(auto* Text=WidgetTree->FindWidget<UTextBlock>(FName(Prefix+Suffix)))
            Text->SetFont(GetResolvedTheme()->MakeFont(Text->GetFont().Size,true));
    }
    if(!IsDesignTime() && Backend.IsValid()) RefreshFromBackend();
    UpdateLayout();
}
void UMCUnifiedTeamWidget::ApplyEditorPreview(float Time)
{
    Super::ApplyEditorPreview(Time);BuildPresentationTree();
    TArray<FMCScoreboardEntry> Entries;
    const int32 Count=PreviewScenario==EMCGameplayUIPreviewScenario::Empty?0:4;
    for(int32 I=0;I<Count;++I) {auto& Entry=Entries.AddDefaulted_GetRef();Entry.Nickname=I==0 && PreviewScenario==EMCGameplayUIPreviewScenario::LongText?TEXT("HASBULADO_WITH_A_VERY_LONG_PLAYER_NAME"):TEXT("HASBULADO");Entry.Points=300;Entry.bVoiceControlEnabled=true;Entry.bVoiceMuted=I==2;Entry.bHost=I==0;}
    PresentEntries(Entries,false);UpdateLayout();
}
void UMCUnifiedTeamWidget::PresentEntries(const TArray<FMCScoreboardEntry>& Entries,bool bHub)
{
    const FString Culture=IsDesignTime()?PreviewCulture:FString();TWeakObjectPtr<APlayerState> Focused;
    for(const auto& Row:Rows) if(Row.MuteButton && Row.MuteButton->HasUserFocus(GetOwningPlayer())) Focused=Row.MuteButton->GetActionTarget();
    for(int32 I=0;I<Rows.Num();++I) {
        auto& Row=Rows[I];const bool Available=Entries.IsValidIndex(I);
        if(Row.Surface) {
            Row.Surface->SetVisibility(Available?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
            if(auto* Minimum=Cast<USizeBox>(Row.Surface->GetParent())) Minimum->SetVisibility(Available?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
        }
        if(!Available) {if(Row.MuteButton) Row.MuteButton->Configure(Backend.Get(),nullptr);continue;}
        const auto& Entry=Entries[I];
        if(Row.Name) {Row.Name->SetText(FText::AsCultureInvariant(Entry.Nickname));Row.Name->SetToolTipText(Entry.bSpectating?UMCUIDataLibrary::FormatTextForCulture(Localized(TEXT("UI.Scoreboard.ObserverName"),LOCTEXT("ObserverName","{Name} · наблюдает")),{{TEXT("Name"),FText::AsCultureInvariant(Entry.Nickname)}},Culture):FText::AsCultureInvariant(Entry.Nickname));}
        if(Row.Points) {Row.Points->SetText(UMCUIDataLibrary::NumberForCulture(Entry.Points,Culture));Row.Points->SetVisibility(bHub?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);}
        if(Row.Ping) Row.Ping->SetText(UMCUIDataLibrary::FormatTextForCulture(Localized(TEXT("UI.Scoreboard.PingValue"),LOCTEXT("PingValue","{Ping} мс")),{{TEXT("Ping"),UMCUIDataLibrary::NumberForCulture(FMath::RoundToInt(Entry.PingMilliseconds),Culture)}},Culture));
        if(Row.Microphone) Row.Microphone->SetMuted(Entry.bVoiceMuted);
        if(Row.MuteButton) {Row.MuteButton->Configure(Backend.Get(),Entry.PlayerState);Row.MuteButton->SetIsEnabled(Entry.bVoiceControlEnabled);Row.MuteButton->SetToolTipText(Entry.bLocalPlayer?Localized(TEXT("UI.Scoreboard.SelfMuteHint"),LOCTEXT("SelfMuteHint","Включить или выключить свой микрофон")):Localized(TEXT("UI.Scoreboard.MuteHint"),LOCTEXT("MuteHint","Отключить голос этого игрока только для себя")));}
    }
    if(WidgetTree) if(auto* Hub=WidgetTree->FindWidget(TEXT("TeamHubSize"))) Hub->SetVisibility(bHub?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    if(Focused.IsValid()) for(int32 I=0;I<Entries.Num() && I<Rows.Num();++I)
        if(Entries[I].PlayerState==Focused.Get() && Rows[I].MuteButton && Rows[I].MuteButton->GetIsEnabled() && !Rows[I].MuteButton->HasUserFocus(GetOwningPlayer())) {Rows[I].MuteButton->SetUserFocus(GetOwningPlayer());break;}
}
void UMCUnifiedTeamWidget::RefreshFromBackend()
{
    if(IsDesignTime() || !Backend.IsValid()) return;
    if(Rows.Num()!=4) CacheControls();
    PresentEntries(Backend->PlayerEntries,Backend->IsUnifiedHubParty());RefreshHubControls();UpdateLayout();
}
void UMCUnifiedTeamWidget::RefreshHubControls()
{
    if(!Backend.IsValid() || !Backend->WidgetTree || !WidgetTree) return;
    auto Copy=[&](FName Source,FName Target) {auto* From=Backend->WidgetTree->FindWidget<UTextBlock>(Source);auto* To=WidgetTree->FindWidget<UTextBlock>(Target);if(From && To) To->SetText(From->GetText());};
    Copy(TEXT("HubPartySummary"),TEXT("TeamHubSummary"));Copy(TEXT("HubPartyStatus"),TEXT("TeamHubStatus"));
    for(const TCHAR* Source:{TEXT("HubPartyInvite"),TEXT("HubPartyPublic"),TEXT("HubPartyFriends"),TEXT("HubPartyInviteOnly"),TEXT("HubPartyClose")}) {
        auto* Original=Backend->WidgetTree->FindWidget<UButton>(FName(Source));auto* View=WidgetTree->FindWidget<UMCUnifiedPartyButton>(FName(Source));if(!View) continue;
        View->SetBackend(Backend.Get());View->SetIsEnabled(Original && Original->GetIsEnabled());
        bool Visible=MCUnifiedTeamPrivate_OwnVisible(Original);
        if(FName(Source)==TEXT("HubPartyPublic") || FName(Source)==TEXT("HubPartyFriends") || FName(Source)==TEXT("HubPartyInviteOnly"))
            Visible=Visible && MCUnifiedTeamPrivate_OwnVisible(Backend->WidgetTree->FindWidget(TEXT("HubPrivacyControls")));
        View->SetVisibility(Visible?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
        if(auto* Bounds=Cast<USizeBox>(View->GetParent())) Bounds->SetVisibility(Visible?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
        if(Original) if(auto* From=Cast<UTextBlock>(Original->GetContent())) if(auto* To=Cast<UTextBlock>(View->GetContent())) To->SetText(From->GetText());
    }
}
UWidget* UMCUnifiedTeamWidget::GetDefaultFocusTarget() const
{
    if(Backend.IsValid() && Backend->IsUnifiedHubParty() && WidgetTree)
        if(auto* Invite=WidgetTree->FindWidget<UMCUnifiedPartyButton>(TEXT("HubPartyInvite"));Invite && Invite->GetIsEnabled() && MCUnifiedTeamPrivate_OwnVisible(Invite)) return Invite;
    for(const auto& Row:Rows) if(Row.MuteButton && Row.Surface && MCUnifiedTeamPrivate_OwnVisible(Row.Surface) && Row.MuteButton->GetIsEnabled()) return Row.MuteButton;
    return const_cast<UMCUnifiedTeamWidget*>(this);
}
void UMCUnifiedTeamWidget::FocusVoiceControls() {if(auto* Target=GetDefaultFocusTarget()) Target->SetUserFocus(GetOwningPlayer());}
void UMCUnifiedTeamWidget::RequestClose() {if(!IsDesignTime() && Backend.IsValid()) Backend->RequestUnifiedClose();}
void UMCUnifiedTeamWidget::MoveVoiceFocus(int32 Direction)
{
    TArray<UWidget*> Targets;int32 Current=INDEX_NONE;
    if(Backend.IsValid() && Backend->IsUnifiedHubParty() && WidgetTree) for(const TCHAR* Name:{TEXT("HubPartyInvite"),TEXT("HubPartyPublic"),TEXT("HubPartyFriends"),TEXT("HubPartyInviteOnly"),TEXT("HubPartyClose")})
        if(auto* Target=WidgetTree->FindWidget(FName(Name));Target && MCUnifiedTeamPrivate_OwnVisible(Target) && Target->GetIsEnabled()) Targets.Add(Target);
    for(const auto& Row:Rows) if(Row.MuteButton && Row.Surface && MCUnifiedTeamPrivate_OwnVisible(Row.Surface) && Row.MuteButton->GetIsEnabled()) Targets.Add(Row.MuteButton);
    for(int32 I=0;I<Targets.Num();++I) if(Targets[I]->HasUserFocus(GetOwningPlayer())) Current=I;
    if(!Targets.IsEmpty()) Targets[Current==INDEX_NONE?0:(Current+Direction+Targets.Num())%Targets.Num()]->SetUserFocus(GetOwningPlayer());
}
FReply UMCUnifiedTeamWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    const FKey Key=Event.GetKey();
    if(Key==EKeys::Escape || Key==EKeys::Gamepad_FaceButton_Right) {RequestClose();return FReply::Handled();}
    if(Key==EKeys::Tab || Key==EKeys::Gamepad_Special_Left) {if(Backend.IsValid() && Backend->IsUnifiedHubParty() && !Event.IsRepeat()) RequestClose();return FReply::Handled();}
    if(Key==EKeys::Up || Key==EKeys::Down || Key==EKeys::Gamepad_DPad_Up || Key==EKeys::Gamepad_DPad_Down) {MoveVoiceFocus(Key==EKeys::Up || Key==EKeys::Gamepad_DPad_Up?-1:1);return FReply::Handled();}
    return Super::NativeOnPreviewKeyDown(Geometry,Event);
}
FReply UMCUnifiedTeamWidget::NativeOnKeyUp(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if(Event.GetKey()==EKeys::Tab || Event.GetKey()==EKeys::Gamepad_Special_Left) {if(Backend.IsValid() && !Backend->IsUnifiedHubParty()) RequestClose();return FReply::Handled();}
    return Super::NativeOnKeyUp(Geometry,Event);
}
void UMCUnifiedTeamWidget::OnLocalizationChanged() {Super::OnLocalizationChanged();if(!IsDesignTime()) RefreshFromBackend();}

#undef LOCTEXT_NAMESPACE
