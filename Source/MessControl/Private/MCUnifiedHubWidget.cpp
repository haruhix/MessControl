#include "MCUnifiedHubWidget.h"
#include "MCUnifiedReticleWidget.h"
#include "CoreGlobals.h"
#include "Components/ScrollBox.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Rendering/SlateRenderer.h"
#include "Widgets/SWidget.h"

#include "MCGameplayUIFactory.h"
#include "MCHubFishingPresentation.h"
#include "MCHubGameState.h"
#include "MCUIDataLibrary.h"
#include "MCUniversalUIWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "MCUnifiedHub"

namespace
{
    FLinearColor MCUnifiedHubPrivate_Color(const TCHAR* Hex) {return FLinearColor(FColor::FromHex(Hex));}
    UButton* MCUnifiedHubPrivate_Surface(UMCUniversalUIWidget* View) {return View && View->WidgetTree?View->WidgetTree->FindWidget<UButton>(TEXT("UniversalSurface")):nullptr;}
}
void UMCUnifiedHubBinding::Clicked() {if(Owner.IsValid()) Owner->ForwardAction(Action);}
UMCUnifiedHubWidget::UMCUnifiedHubWidget()
{
    bUseNativeLayout=false;bOverridePreviewUIVersion=true;SetIsFocusable(true);SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}
void UMCUnifiedHubWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(WidgetTree->RootWidget) {EnsureControlsPresentation();return;}
    if(!Theme) Theme=UMCGameplayUIFactory::GetThemeForVersion(EMCUIVersion::New);
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("HubV2Root"));Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);WidgetTree->RootWidget=Root;
    auto Shape=[&](FName Name,FLinearColor Fill,FLinearColor Outline,FLinearColor Shadow,FMargin Inset,EMCUniversalUIKind Kind=EMCUniversalUIKind::Panel) {
        auto* View=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),Name);
        View->Kind=Kind;View->Label=FText::GetEmpty();View->bCustomColors=true;View->bUseThemeShape=false;View->CornerRadius=22;View->OutlineWidth=4;
        View->CustomFill=Fill;View->CustomOutline=Outline;View->CustomShadow=Shadow;View->CustomText=MCUnifiedHubPrivate_Color(TEXT("FFF9F8"));View->ContentPadding=Inset;View->ShadowOffset=FVector2D(10,12);
        View->SetVisibility(Kind==EMCUniversalUIKind::Button?ESlateVisibility::Visible:ESlateVisibility::SelfHitTestInvisible);View->Initialize();View->BuildPresentationTree();return View;
    };
    auto Text=[&](FName Name,FName Key,const FText& Copy,int32 FontSize) {
        auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);Label->SetText(Localized(Key,Copy));Label->SetFont(GetResolvedTheme()->MakeFont(FontSize,true));
        Label->SetColorAndOpacity(MCUnifiedHubPrivate_Color(TEXT("FFF9F8")));Label->SetAutoWrapText(true);Label->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);Label->SetVisibility(ESlateVisibility::HitTestInvisible);
        if(!Key.IsNone()) TextBindings.Add(Name,Key);return Label;
    };
    auto Bounded=[&](FName Name,UWidget* Contents,float Minimum,float Maximum) {
        auto* Bounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),Name);Bounds->SetMinDesiredWidth(Minimum);Bounds->SetMaxDesiredWidth(Maximum);Bounds->SetContent(Contents);Bounds->SetVisibility(ESlateVisibility::SelfHitTestInvisible);return Bounds;
    };
    auto Action=[&](FName Name,FLinearColor Fill,int32 FontSize) {
        auto* Button=Shape(Name,Fill,MCUnifiedHubPrivate_Color(TEXT("FFF9F8")),FLinearColor::Transparent,FMargin(22,11),EMCUniversalUIKind::Button);
        Button->ShadowOffset=FVector2D::ZeroVector;Button->FontSize=FontSize;return Button;
    };
    auto* Top=WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(),TEXT("HubV2Top"));Top->SetExplicitWrapSize(true);Top->SetWrapSize(1500);Top->SetInnerSlotPadding(FVector2D(24,20));Top->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    auto* TopWidth=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("HubV2TopWidth"));TopWidth->SetWidthOverride(1500);TopWidth->SetContent(Top);TopWidth->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    auto* TopCell=Root->AddChildToCanvas(TopWidth);TopCell->SetAnchors(FAnchors(0,0));TopCell->SetPosition(FVector2D(18,16));TopCell->SetAutoSize(true);
    auto* Ready=Shape(TEXT("HubV2Readiness"),MCUnifiedHubPrivate_Color(TEXT("374F56")),MCUnifiedHubPrivate_Color(TEXT("D8DCDC")),MCUnifiedHubPrivate_Color(TEXT("C7C7C7")),FMargin(15,12));
    auto* ReadyBody=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("HubV2ReadinessContents"));ReadyBody->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Ready->SetContentForSlot(TEXT("Content"),ReadyBody);
    ReadyBody->AddChildToVerticalBox(Text(TEXT("HubV2ReadinessTitle"),TEXT("hub.title"),LOCTEXT("Title","СБОР КОМАНДЫ"),29));
    ReadyBody->AddChildToVerticalBox(Text(TEXT("HubV2ReadinessCount"),NAME_None,FText::GetEmpty(),27))->SetPadding(FMargin(0,2,0,8));
    ReadyBody->AddChildToVerticalBox(Text(TEXT("HubV2ReadinessHint"),NAME_None,FText::GetEmpty(),19))->SetPadding(FMargin(0,0,0,12));
    auto* Open=Action(TEXT("HubV2OpenSelection"),MCUnifiedHubPrivate_Color(TEXT("E8EEEE")),42);Open->CustomText=MCUnifiedHubPrivate_Color(TEXT("AE4C6C"));Open->Label=FText::AsCultureInvariant(TEXT("ENTER"));Open->ApplyTheme();
    ReadyBody->AddChildToVerticalBox(Open);
    Top->AddChildToWrapBox(Bounded(TEXT("HubV2ReadinessWidth"),Ready,330,400))->SetVerticalAlignment(VAlign_Top);
    auto* ProfileColumn=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("HubV2ProfileContents"));ProfileColumn->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    auto* Profile=Shape(TEXT("HubV2Profile"),MCUnifiedHubPrivate_Color(TEXT("194651")),MCUnifiedHubPrivate_Color(TEXT("A9FAF2")),FLinearColor::Transparent,FMargin(16,8));Profile->ShadowOffset=FVector2D::ZeroVector;
    auto* Name=Text(TEXT("HubV2ProfileName"),NAME_None,FText::GetEmpty(),30);Name->SetJustification(ETextJustify::Center);Profile->SetContentForSlot(TEXT("Content"),Name);
    ProfileColumn->AddChildToVerticalBox(Profile)->SetPadding(FMargin(0,0,0,10));
    // This PSD plate has no defined gameplay value; it intentionally remains decorative.
    auto* Plate=Shape(TEXT("HubV2DecorativePlate"),MCUnifiedHubPrivate_Color(TEXT("A35173")),MCUnifiedHubPrivate_Color(TEXT("FFF9F8")),FLinearColor::Transparent,FMargin(0));Plate->ShadowOffset=FVector2D::ZeroVector;Plate->CornerRadius=15;
    auto* PlateHeight=WidgetTree->ConstructWidget<USizeBox>();PlateHeight->SetMinDesiredHeight(36);PlateHeight->SetContent(Plate);PlateHeight->SetVisibility(ESlateVisibility::SelfHitTestInvisible);ProfileColumn->AddChildToVerticalBox(PlateHeight);
    Top->AddChildToWrapBox(Bounded(TEXT("HubV2ProfileWidth"),ProfileColumn,280,420))->SetVerticalAlignment(VAlign_Top);
    auto* SocialColumn=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("HubV2SocialContents"));SocialColumn->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    auto* Wallet=Shape(TEXT("HubV2WalletCard"),MCUnifiedHubPrivate_Color(TEXT("A35173")),MCUnifiedHubPrivate_Color(TEXT("FFF9F8")),FLinearColor::Transparent,FMargin(14,8));Wallet->ShadowOffset=FVector2D::ZeroVector;
    auto* CurrencyRow=WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(),TEXT("HubV2CurrencyRow"));CurrencyRow->SetExplicitWrapSize(true);CurrencyRow->SetWrapSize(520);CurrencyRow->SetInnerSlotPadding(FVector2D(20,8));CurrencyRow->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Wallet->SetContentForSlot(TEXT("Content"),CurrencyRow);
    for(bool Fish:{false,true}) {
        auto* Pair=WidgetTree->ConstructWidget<UHorizontalBox>();Pair->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        auto* Icon=WidgetTree->ConstructWidget<UMCHubCurrencyIcon>(UMCHubCurrencyIcon::StaticClass(),Fish?TEXT("HubV2FishIcon"):TEXT("HubV2CoinIcon"));Icon->bFish=Fish;Icon->Tint=MCUnifiedHubPrivate_Color(TEXT("FFF9F8"));Icon->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* IconSize=WidgetTree->ConstructWidget<USizeBox>();IconSize->SetWidthOverride(Fish?40:30);IconSize->SetHeightOverride(30);IconSize->SetContent(Icon);IconSize->SetVisibility(ESlateVisibility::HitTestInvisible);
        Pair->AddChildToHorizontalBox(IconSize)->SetVerticalAlignment(VAlign_Center);
        auto* Count=Text(Fish?TEXT("HubV2FishCount"):TEXT("HubV2WalletCount"),NAME_None,FText::GetEmpty(),30);Count->SetAutoWrapText(false);
        auto* CountCell=Pair->AddChildToHorizontalBox(Count);CountCell->SetVerticalAlignment(VAlign_Center);CountCell->SetPadding(FMargin(8,0,0,0));
        CurrencyRow->AddChildToWrapBox(Pair)->SetVerticalAlignment(VAlign_Center);
    }
    SocialColumn->AddChildToVerticalBox(Wallet)->SetHorizontalAlignment(HAlign_Left);
    auto* Party=Shape(TEXT("HubV2PartyCard"),MCUnifiedHubPrivate_Color(TEXT("AE4C6C")),MCUnifiedHubPrivate_Color(TEXT("FFF9F8")),FLinearColor::Transparent,FMargin(16,8));Party->ShadowOffset=FVector2D::ZeroVector;
    auto* PartyText=Text(TEXT("HubV2PartyHint"),NAME_None,FText::GetEmpty(),29);PartyText->SetJustification(ETextJustify::Center);Party->SetContentForSlot(TEXT("Content"),PartyText);
    SocialColumn->AddChildToVerticalBox(Party)->SetPadding(FMargin(0,16,0,0));
    Top->AddChildToWrapBox(Bounded(TEXT("HubV2SocialWidth"),SocialColumn,340,600))->SetVerticalAlignment(VAlign_Top);
    auto* Footer=Shape(TEXT("HubV2SelectionFooter"),MCUnifiedHubPrivate_Color(TEXT("374F56")),MCUnifiedHubPrivate_Color(TEXT("FFF9F8")),MCUnifiedHubPrivate_Color(TEXT("C7C7C7")),FMargin(20,14));
    auto* FooterWidth=Bounded(TEXT("HubV2SelectionWidth"),Footer,740,1400);FooterWidth->SetWidthOverride(1400);
    auto* FooterCell=Root->AddChildToCanvas(FooterWidth);FooterCell->SetAnchors(FAnchors(.5,1));FooterCell->SetAlignment(FVector2D(.5,1));FooterCell->SetPosition(FVector2D(0,-24));FooterCell->SetAutoSize(true);
    auto* FooterBody=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("HubV2SelectionContents"));FooterBody->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Footer->SetContentForSlot(TEXT("Content"),FooterBody);
    FooterBody->AddChildToVerticalBox(Text(TEXT("HubV2SelectionTitle"),NAME_None,FText::GetEmpty(),27));
    FooterBody->AddChildToVerticalBox(Text(TEXT("HubV2SelectionStatus"),NAME_None,FText::GetEmpty(),21))->SetPadding(FMargin(0,6,0,14));
    auto* FooterActions=WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(),TEXT("HubV2SelectionActions"));FooterActions->SetExplicitWrapSize(true);FooterActions->SetWrapSize(1300);FooterActions->SetInnerSlotPadding(FVector2D(20,10));FooterActions->SetHorizontalAlignment(HAlign_Center);FooterActions->SetVisibility(ESlateVisibility::SelfHitTestInvisible);FooterBody->AddChildToVerticalBox(FooterActions)->SetHorizontalAlignment(HAlign_Center);
    auto* Launch=Action(TEXT("HubV2Launch"),MCUnifiedHubPrivate_Color(TEXT("2AA85C")),26);Launch->Label=Localized(TEXT("hub.selection.start"),LOCTEXT("Launch","ENTER · НАЧАТЬ"));Launch->ApplyTheme();FooterActions->AddChildToWrapBox(Launch);
    auto* Back=Action(TEXT("HubV2Return"),MCUnifiedHubPrivate_Color(TEXT("A35173")),26);Back->Label=Localized(TEXT("hub.selection.cancel"),LOCTEXT("Return","ESC · ВЕРНУТЬСЯ В ХАБ"));Back->ApplyTheme();FooterActions->AddChildToWrapBox(Back);
    ShowMode(true);EnsureControlsPresentation();
}
void UMCUnifiedHubWidget::BindActions()
{
    if(IsDesignTime() || !GetWorld() || !GetWorld()->IsGameWorld() || !WidgetTree) return;
    const FName Names[]={TEXT("HubV2OpenSelection"),TEXT("HubV2Launch"),TEXT("HubV2Return")};
    for(FName Name:Names) if(auto* Button=WidgetTree->FindWidget<UMCUniversalUIWidget>(Name)) for(UMCUnifiedHubBinding* Old:Bindings) if(Old) Button->OnClicked.RemoveAll(Old);
    Bindings.Reset();
    const EMCHubMenuAction Actions[]={EMCHubMenuAction::OpenSelection,EMCHubMenuAction::Launch,EMCHubMenuAction::Return};
    for(int32 I=0;I<3;++I) if(auto* Button=WidgetTree->FindWidget<UMCUniversalUIWidget>(Names[I])) {
        auto* Binding=NewObject<UMCUnifiedHubBinding>(this,NAME_None,RF_Transient);Binding->Owner=this;Binding->Action=Actions[I];Button->OnClicked.AddUniqueDynamic(Binding,&UMCUnifiedHubBinding::Clicked);Bindings.Add(Binding);
    }
}
void UMCUnifiedHubWidget::SetBackend(UMCHubWidget* Source)
{
    auto* Candidate=Source && Source->GetWorld()==GetWorld() && Source->GetOwningPlayer()==GetOwningPlayer()?Source:nullptr;
    if(Backend.Get()!=Candidate) {UnbindControlsNotification();ClearControlsPresentation(Backend.Get());Backend=Candidate;bControlsPrepared=false;}
    BuildPresentationTree();BindControlsNotification();PrepareControlsPresentation();if(Bindings.IsEmpty()) BindActions();RefreshFromBackend();
}
void UMCUnifiedHubWidget::ShowMode(bool bGathering)
{
    if(!WidgetTree) return;
    if(auto* Top=WidgetTree->FindWidget(TEXT("HubV2TopWidth"))) Top->SetVisibility(bGathering?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    if(auto* Footer=WidgetTree->FindWidget(TEXT("HubV2SelectionWidth"))) Footer->SetVisibility(bGathering?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible);
}
void UMCUnifiedHubWidget::RefreshFromBackend()
{
    if(IsDesignTime() || !Backend.IsValid() || !WidgetTree || !Backend->WidgetTree) return;
    const auto* Hub=Backend->GetUnifiedState();if(!Hub) return;ShowMode(Hub->Phase==EMCHubPhase::Gathering);
    RefreshControlsPresentation();
    auto Copy=[&](FName SourceName,FName TargetName) {
        auto* Source=Backend->WidgetTree->FindWidget<UTextBlock>(SourceName);auto* Target=WidgetTree->FindWidget<UTextBlock>(TargetName);
        if(Source && Target && !Target->GetText().EqualTo(Source->GetText())) Target->SetText(Source->GetText());
    };
    Copy(TEXT("HubReadinessTitle"),TEXT("HubV2ReadinessTitle"));Copy(TEXT("HubReadinessCount"),TEXT("HubV2ReadinessCount"));Copy(TEXT("HubReadinessHint"),TEXT("HubV2ReadinessHint"));
    Copy(TEXT("HubProfileName"),TEXT("HubV2ProfileName"));Copy(TEXT("HubWalletCount"),TEXT("HubV2WalletCount"));Copy(TEXT("HubBottomFishCount"),TEXT("HubV2FishCount"));Copy(TEXT("HubPartyHint"),TEXT("HubV2PartyHint"));
    Copy(TEXT("HubSelectionTitle"),TEXT("HubV2SelectionTitle"));Copy(TEXT("HubSelectionStatus"),TEXT("HubV2SelectionStatus"));
    const FName Sources[]={TEXT("HubOpenSelection"),TEXT("HubLaunch"),TEXT("HubReturn")},Targets[]={TEXT("HubV2OpenSelection"),TEXT("HubV2Launch"),TEXT("HubV2Return")};
    for(int32 I=0;I<3;++I) if(auto* Target=WidgetTree->FindWidget<UMCUniversalUIWidget>(Targets[I])) {
        auto* Source=Backend->WidgetTree->FindWidget<UButton>(Sources[I]);const bool Visible=Source && Source->GetVisibility()!=ESlateVisibility::Collapsed && Source->GetVisibility()!=ESlateVisibility::Hidden;
        Target->SetVisibility(Visible?ESlateVisibility::Visible:ESlateVisibility::Collapsed);Target->SetIsEnabled(Source && Source->GetIsEnabled());
        if(I>0) if(auto* Caption=Backend->WidgetTree->FindWidget<UTextBlock>(FName(Sources[I].ToString()+TEXT("Label")))) Target->Label=Caption->GetText();
        Target->ApplyTheme();
    }
}
bool UMCUnifiedHubWidget::RequiresMenuInput() const {return Backend.IsValid() && Backend->RequiresMenuInput();}
UWidget* UMCUnifiedHubWidget::GetDefaultFocusTarget() const
{
    if(WidgetTree) for(const TCHAR* Name:RequiresMenuInput()?TArray<const TCHAR*>{TEXT("HubV2Launch"),TEXT("HubV2Return")}:TArray<const TCHAR*>{TEXT("HubV2OpenSelection")})
        if(auto* View=WidgetTree->FindWidget<UMCUniversalUIWidget>(FName(Name));View && View->GetIsEnabled() && View->GetVisibility()==ESlateVisibility::Visible) if(auto* Button=MCUnifiedHubPrivate_Surface(View)) return Button;
    return const_cast<UMCUnifiedHubWidget*>(this);
}
UWidget* UMCUnifiedHubWidget::GetFishCounterIcon() const {return WidgetTree?WidgetTree->FindWidget(TEXT("HubV2FishIcon")):nullptr;}
UWidget* UMCUnifiedHubWidget::GetFishCounterCard() const {return WidgetTree?WidgetTree->FindWidget(TEXT("HubV2WalletCard")):nullptr;}
void UMCUnifiedHubWidget::ForwardAction(EMCHubMenuAction Action)
{
    if(!IsDesignTime() && Backend.IsValid() && Backend->GetWorld()==GetWorld()) Backend->HandleAction(Action);
}
void UMCUnifiedHubWidget::RequestBack() {if(!IsDesignTime() && Backend.IsValid()) Backend->RequestUnifiedBack();}
bool UMCUnifiedHubWidget::RouteSourceKey(const FKeyEvent& Event) {return !IsDesignTime() && RequiresMenuInput() && Backend->RouteUnifiedKey(Event);}
bool UMCUnifiedHubWidget::RouteSourceMouseWheel(const FPointerEvent& Event) {return !IsDesignTime() && RequiresMenuInput() && Backend->RouteUnifiedMouseWheel(Event);}
void UMCUnifiedHubWidget::NativeConstruct() {Super::NativeConstruct();BindActions();BindControlsNotification();PrepareControlsPresentation();RefreshFromBackend();}
void UMCUnifiedHubWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);RefreshViewportLayout();PrepareControlsPresentation();RefreshControlsPresentation();
}
void UMCUnifiedHubWidget::RefreshReadinessTitleWidth(float AvailableTopWidth)
{
    if(!WidgetTree) return;
    auto* Title=WidgetTree->FindWidget<UTextBlock>(TEXT("HubV2ReadinessTitle"));
    auto* ReadyPanel=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("HubV2Readiness"));
    auto* ReadyBounds=WidgetTree->FindWidget<USizeBox>(TEXT("HubV2ReadinessWidth"));
    if(!Title || !ReadyPanel || !ReadyBounds) return;
    const float Available=FMath::Max(1.f,AvailableTopWidth);
    const auto TitleSlate=Title->GetCachedWidget();
    if(TitleSlate.IsValid()) {
        const float ActualScale=FMath::Max(.001f,TitleSlate->GetTickSpaceGeometry().GetAccumulatedLayoutTransform().GetScale());
        const FText ActualCopy=Title->GetText();const FSlateFontInfo ActualFont=Title->GetFont();
        if(!bReadinessTitleMeasured || MeasuredReadinessTitle.Get()!=Title || !MeasuredReadinessCopy.EqualTo(ActualCopy)
            || !(MeasuredReadinessFont==ActualFont) || !FMath::IsNearlyEqual(MeasuredReadinessScale,ActualScale,.001f)
            || !FMath::IsNearlyEqual(MeasuredReadinessTopWidth,Available,.5f)) {
            const bool WasAutoWrap=Title->GetAutoWrapText();const float WasWrapAt=Title->GetWrapTextAt();
            Title->SetAutoWrapText(false);Title->SetWrapTextAt(0.f);
            TitleSlate->MarkPrepassAsDirty();TitleSlate->SlatePrepass(ActualScale);
            const float NaturalWidth=float(TitleSlate->GetDesiredSize().X); // Native shaped Slate size is already in local layout units.
            Title->SetAutoWrapText(WasAutoWrap);Title->SetWrapTextAt(WasWrapAt);
            TitleSlate->MarkPrepassAsDirty();TitleSlate->SlatePrepass(ActualScale);
            if(FMath::IsFinite(NaturalWidth) && NaturalWidth>=0.f) {
                ReadinessTitleNaturalWidth=NaturalWidth;MeasuredReadinessTitle=Title;MeasuredReadinessCopy=ActualCopy;
                MeasuredReadinessFont=ActualFont;MeasuredReadinessScale=ActualScale;MeasuredReadinessTopWidth=Available;bReadinessTitleMeasured=true;
            } else bReadinessTitleMeasured=false;
        }
    }
    const FMargin ContentInset=ReadyPanel->ContentPadding;
    float Required=0.f;
    if(bReadinessTitleMeasured) {
        // Two physical pixels cover fractional glyph/slot rounding; this never changes font size or text.
        const float CeilGutter=2.f/FMath::Max(.001f,MeasuredReadinessScale);
        Required=FMath::CeilToFloat(ReadinessTitleNaturalWidth+ContentInset.Left+ContentInset.Right+CeilGutter);
    }
    const float Maximum=FMath::Min(Available,FMath::Max(400.f,Required));
    // A long word also needs this content-derived minimum: restored AutoWrap may still cache the old narrower allotted width.
    const float Minimum=FMath::Min(Available,Required>400.f?FMath::Max(330.f,Required):330.f);
    if(!FMath::IsNearlyEqual(ReadyBounds->GetMinDesiredWidth(),Minimum,.5f)) ReadyBounds->SetMinDesiredWidth(Minimum);
    if(!FMath::IsNearlyEqual(ReadyBounds->GetMaxDesiredWidth(),Maximum,.5f)) ReadyBounds->SetMaxDesiredWidth(Maximum);
}
void UMCUnifiedHubWidget::RefreshViewportLayout()
{
    if(!bFitViewportColumns || !WidgetTree) return;
    const double Width=GetPaintSpaceGeometry().GetLocalSize().X;if(Width<=1) return;
    const float TopWidth=float(FMath::Clamp(Width-36.,320.,1500.));
    const float FooterWidth=float(FMath::Clamp(Width-48.,320.,1400.));
    RefreshReadinessTitleWidth(TopWidth);
    // Match the nameplate to the real nickname before allowing a long-name wrap.
    // Normal names remain on one line; exceptional names use balanced lines.
    if(auto* Name=WidgetTree->FindWidget<UTextBlock>(TEXT("HubV2ProfileName")))
        if(auto* Bounds=WidgetTree->FindWidget<USizeBox>(TEXT("HubV2ProfileWidth"))) {
            const auto* Surface=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("HubV2Profile"));
            const float NameInsetWidth=Surface?Surface->ContentPadding.Left+Surface->ContentPadding.Right:32.f;
            const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
            const float Natural=float(Measure->Measure(Name->GetText(),Name->GetFont()).X)+8.f;
            const float Maximum=FMath::Max(120.f,FMath::Min(760.f,TopWidth*.60f));
            const float NameWidth=FMath::Clamp(Natural+NameInsetWidth,FMath::Min(280.f,Maximum),Maximum);
            Bounds->SetMinDesiredWidth(NameWidth);Bounds->SetMaxDesiredWidth(NameWidth);Bounds->SetWidthOverride(NameWidth);
            const float Inner=FMath::Max(60.f,NameWidth-NameInsetWidth);
            const int32 Lines=FMath::Max(1,FMath::CeilToInt(Natural/Inner));
            Name->SetAutoWrapText(false);
            Name->SetWrappingPolicy(Lines>1?ETextWrappingPolicy::AllowPerCharacterWrapping:ETextWrappingPolicy::DefaultWrapping);
            Name->SetWrapTextAt(Lines>1?FMath::Min(Inner,FMath::CeilToFloat(Natural/Lines)+4.f):0.f);
        }
    if(auto* Bounds=WidgetTree->FindWidget<USizeBox>(TEXT("HubV2TopWidth")))
        if(!FMath::IsNearlyEqual(Bounds->GetWidthOverride(),TopWidth,.5f)) Bounds->SetWidthOverride(TopWidth);
    if(auto* Wrap=WidgetTree->FindWidget<UWrapBox>(TEXT("HubV2Top")))
        if(!FMath::IsNearlyEqual(Wrap->GetWrapSize(),TopWidth,.5f)) Wrap->SetWrapSize(TopWidth);
    if(auto* Bounds=WidgetTree->FindWidget<USizeBox>(TEXT("HubV2SelectionWidth"))) {
        Bounds->SetMinDesiredWidth(FMath::Min(740.f,FooterWidth));
        if(!FMath::IsNearlyEqual(Bounds->GetWidthOverride(),FooterWidth,.5f)) Bounds->SetWidthOverride(FooterWidth);
    }
    if(auto* Wrap=WidgetTree->FindWidget<UWrapBox>(TEXT("HubV2SelectionActions")))
        if(!FMath::IsNearlyEqual(Wrap->GetWrapSize(),FooterWidth-60,.5f)) Wrap->SetWrapSize(FooterWidth-60);
}
void UMCUnifiedHubWidget::ApplyTheme() {bReadinessTitleMeasured=false;Super::ApplyTheme();bControlsPrepared=false;PrepareControlsPresentation();if(!IsDesignTime()) RefreshFromBackend();}
void UMCUnifiedHubWidget::EnsureControlsPresentation()
{
    if(!WidgetTree) return;auto* Canvas=Cast<UCanvasPanel>(WidgetTree->RootWidget);if(!Canvas) return;
    if(!WidgetTree->FindWidget(TEXT("HubV2ControlsReticle"))) {
        auto* Class=UMCGameplayUIFactory::ResolveClassForTheme(TEXT("UnifiedReticle"),UMCUnifiedReticleWidget::StaticClass(),GetResolvedTheme());
        auto* Reticle=WidgetTree->ConstructWidget<UMCUnifiedReticleWidget>(Class,TEXT("HubV2ControlsReticle"));Reticle->PreparedContourCount=128;
        if(!Reticle->Theme) Reticle->Theme=GetResolvedTheme();Reticle->Initialize();Reticle->BuildPresentationTree();
        auto* Cell=Canvas->AddChildToCanvas(Reticle);Cell->SetAnchors(FAnchors(0,0,1,1));Cell->SetOffsets(FMargin(0));Cell->SetZOrder(20);
        Reticle->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    if(!WidgetTree->FindWidget(TEXT("HubV2ControlsBounds"))) {
        auto* Bounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("HubV2ControlsBounds"));Bounds->SetWidthOverride(1000);
        auto* Cell=Canvas->AddChildToCanvas(Bounds);Cell->SetAnchors(FAnchors(.5,1));Cell->SetAlignment(FVector2D(.5,1));Cell->SetPosition(FVector2D(0,-116));Cell->SetAutoSize(true);Cell->SetZOrder(21);
        auto* Plate=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),TEXT("HubV2ControlsPlate"));
        Plate->Theme=GetResolvedTheme();Plate->Kind=EMCUniversalUIKind::Panel;Plate->Label=FText::GetEmpty();Plate->LabelKey=NAME_None;
        Plate->bUseThemeShape=false;Plate->bCustomColors=true;Plate->CustomFill=MCUnifiedHubPrivate_Color(TEXT("374F56"));
        Plate->CustomOutline=MCUnifiedHubPrivate_Color(TEXT("FFF9F8"));Plate->CustomShadow=MCUnifiedHubPrivate_Color(TEXT("C7C7C7"));
        Plate->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        Plate->ContentPadding=FMargin(16,11);Plate->CornerRadius=18;Plate->OutlineWidth=3;Plate->ShadowOffset=FVector2D(4,5);
        Plate->Initialize();Plate->BuildPresentationTree();Plate->ApplyTheme();Bounds->SetContent(Plate);
        auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("HubV2ControlsScroll"));Scroll->SetAllowOverscroll(false);
        Plate->SetContentForSlot(TEXT("Content"),Scroll);
        auto* Hint=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("HubV2ControlsText"));
        Hint->SetVisibility(ESlateVisibility::HitTestInvisible);
        Hint->SetFont(GetResolvedTheme()->MakeFont(20,true));Hint->SetColorAndOpacity(MCUnifiedHubPrivate_Color(TEXT("FFF9F8")));Hint->SetAutoWrapText(false);
        Hint->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);Hint->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);Hint->SetJustification(ETextJustify::Center);
        Scroll->AddChild(Hint);Bounds->SetVisibility(ESlateVisibility::Collapsed);
    }
}
void UMCUnifiedHubWidget::PrepareControlsPresentation()
{
    if(bPreparingControls) return;TGuardValue<bool> Guard(bPreparingControls,true);EnsureControlsPresentation();
    if(!WidgetTree || !GetResolvedTheme() || !FSlateApplication::IsInitialized()) return;
    auto* Reticle=WidgetTree->FindWidget<UMCUnifiedReticleWidget>(TEXT("HubV2ControlsReticle"));
    auto* Hint=WidgetTree->FindWidget<UTextBlock>(TEXT("HubV2ControlsText"));
    if(!Reticle || !Hint) {bControlsPrepared=false;return;}Reticle->PreparePresentation();
    const auto Renderer=FSlateApplication::Get().GetRenderer();if(!Renderer) {bControlsPrepared=false;return;}
    const FString Culture=IsDesignTime()?PreviewCulture:FInternationalization::Get().GetCurrentLanguage()->GetName();
    UDataTable* Table=Backend.IsValid()?Backend->GetResolvedTranslationTable():GetResolvedTranslationTable();
    TArray<FText> Warm;const FName Keys[]={TEXT("hub.basketball.controls"),TEXT("hub.basketball.controls.gamepad"),TEXT("hub.basketball.pickup"),TEXT("hub.basketball.pickup.gamepad"),
        TEXT("hub.fishing.water_required"),TEXT("hub.fishing.water_required.gamepad"),TEXT("hub.fishing.hooked")};
    bool Complete=true;
    for(FName Key:Keys) {const FText Copy=UMCUIDataLibrary::ResolveText(Table,Key,FText::GetEmpty(),IsDesignTime()?PreviewCulture:FString());Complete&=!Copy.IsEmpty();Warm.Add(Copy);}
    MCHubFishingPresentation::AppendHints(Warm,Table,IsDesignTime()?PreviewCulture:FString());
    if(auto* Source=Backend.Get()) Warm.Add(Source->GetUnifiedControlsSnapshot().Hint);
    const FText Corpus=FText::Join(FText::AsCultureInvariant(TEXT(" ")),Warm);
    const float Scale=FMath::Max(.001f,GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale());
    const bool SourcePrimed=IsDesignTime() || (Backend.IsValid() && Backend->HasPreparedControlsGlyphs());
    if(bControlsPrepared && Complete && SourcePrimed && PreparedControlsTable.Get()==Table && PreparedControlsCulture==Culture && PreparedControlsCopy.EqualTo(Corpus)
        && PreparedControlsFont==Hint->GetFont() && FMath::IsNearlyEqual(PreparedControlsScale,Scale,.001f)) return;
    Hint->SetFont(GetResolvedTheme()->MakeFont(FMath::Max(8,FMath::RoundToInt(Hint->GetFont().Size)),true));
    Renderer->GetFontMeasureService()->Measure(Corpus,Hint->GetFont(),Scale);
    // Warm the actual original '+' font before its first aimed frame; New draws two ordinary UMG strokes.
    if(auto* Source=Backend.Get()) Renderer->GetFontMeasureService()->Measure(FText::AsCultureInvariant(TEXT("+")),Source->GetControlsReticleFont(),1.f);
    PreparedControlsTable=Table;PreparedControlsCulture=Culture;PreparedControlsCopy=Corpus;PreparedControlsFont=Hint->GetFont();PreparedControlsScale=Scale;
    bControlsPrepared=Complete && SourcePrimed && Reticle->IsPresentationPrepared() && Hint->GetFont().HasValidFont();
}
void UMCUnifiedHubWidget::BindControlsNotification()
{
    if(!ControlsHandle.IsValid()) if(auto* Source=Backend.Get()) ControlsHandle=Source->OnControlsPresentationUpdated().AddUObject(this,&ThisClass::OnControlsSnapshot);
}
void UMCUnifiedHubWidget::UnbindControlsNotification()
{
    if(auto* Source=Backend.Get()) {Source->OnControlsPresentationUpdated().Remove(ControlsHandle);Source->ClearUnifiedControlsView(this);}ControlsHandle.Reset();
}
void UMCUnifiedHubWidget::ClearControlsPresentation(const UMCHubWidget* Source)
{
    if(!WidgetTree) return;
    if(auto* Reticle=WidgetTree->FindWidget<UMCUnifiedReticleWidget>(TEXT("HubV2ControlsReticle"))) Reticle->ClearPresentationFor(Source);
    if(auto* Bounds=WidgetTree->FindWidget(TEXT("HubV2ControlsBounds"))) Bounds->SetVisibility(ESlateVisibility::Collapsed);
}
bool UMCUnifiedHubWidget::HasControlsPresentationFrame(const UMCHubWidget* Source,uint64 SourceFrame) const
{
    if(!bControlsPrepared || !Source || Backend.Get()!=Source || !WidgetTree) return false;
    const auto* Reticle=WidgetTree->FindWidget<UMCUnifiedReticleWidget>(TEXT("HubV2ControlsReticle"));
    return Reticle && Reticle->HasPresentedSourceFrame(Source,SourceFrame);
}
void UMCUnifiedHubWidget::OnControlsSnapshot(const FMCHubControlsPresentation& Snapshot)
{
    auto* Source=Backend.Get();if(!Source || Snapshot.Reticle.Source.Get()!=Source || !WidgetTree) return;
    auto* Reticle=WidgetTree->FindWidget<UMCUnifiedReticleWidget>(TEXT("HubV2ControlsReticle"));
    auto* Bounds=WidgetTree->FindWidget<USizeBox>(TEXT("HubV2ControlsBounds"));auto* Hint=WidgetTree->FindWidget<UTextBlock>(TEXT("HubV2ControlsText"));
    if(!bControlsPrepared || !Reticle || !Bounds || !Hint || !Reticle->ConsumePresentation(Snapshot.Reticle,Source->GetUnifiedControlsPaintGeometry())) {
        Source->ClearUnifiedControlsView(this);ClearControlsPresentation(Source);return;
    }
    const bool NewCopy=!Hint->GetText().EqualTo(Snapshot.Hint);if(NewCopy) Hint->SetText(Snapshot.Hint);
    const FVector2D Area=GetPaintSpaceGeometry().GetLocalSize();const float Width=FMath::Max(240.f,FMath::Min(1100.f,float(Area.X)-48));
    Bounds->SetWidthOverride(Width);Bounds->SetMaxDesiredHeight(FMath::Max(120.f,float(Area.Y)*.45f));Hint->SetWrapTextAt(FMath::Max(100.f,Width-44));
    Bounds->SetVisibility(Snapshot.bHintVisible && !Snapshot.Hint.IsEmpty()?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    if(NewCopy) if(auto* Scroll=WidgetTree->FindWidget<UScrollBox>(TEXT("HubV2ControlsScroll"))) Scroll->ScrollToStart();
    if(!Source->CommitUnifiedControlsView(this,Snapshot.Reticle.SourceFrame)) {Source->ClearUnifiedControlsView(this);ClearControlsPresentation(Source);}
}
void UMCUnifiedHubWidget::RefreshControlsPresentation()
{
    if(IsDesignTime()) return;if(auto* Source=Backend.Get()) OnControlsSnapshot(Source->GetUnifiedControlsSnapshot());else ClearControlsPresentation(nullptr);
}
void UMCUnifiedHubWidget::PresentControlsPreview()
{
    if(!IsDesignTime() || !WidgetTree) return;PrepareControlsPresentation();
    auto* Bounds=WidgetTree->FindWidget<USizeBox>(TEXT("HubV2ControlsBounds"));auto* Hint=WidgetTree->FindWidget<UTextBlock>(TEXT("HubV2ControlsText"));
    if(!Bounds || !Hint) return;
    using MCHubFishingPresentation::EHint;
    FText Copy;
    if(PreviewState==TEXT("FishingControls") || PreviewState==TEXT("ControlsLongText"))
        Copy=MCHubFishingPresentation::ResolveHint(GetResolvedTranslationTable(),EHint::Cast,PreviewCulture);
    else if(PreviewState==TEXT("FishingWaiting"))
        Copy=MCHubFishingPresentation::ResolveHint(GetResolvedTranslationTable(),EHint::Wait,PreviewCulture);
    else if(PreviewState==TEXT("FishingHooked")) Copy=Localized(TEXT("hub.fishing.hooked"),FText::GetEmpty());
    else if(PreviewState==TEXT("BasketballControls")) Copy=Localized(TEXT("hub.basketball.controls"),FText::GetEmpty());
    else if(PreviewState==TEXT("BasketballPickup")) Copy=Localized(TEXT("hub.basketball.pickup"),FText::GetEmpty());
    else {Bounds->SetVisibility(ESlateVisibility::Collapsed);return;}
    if(PreviewState==TEXT("ControlsLongText")) Copy=FText::Format(FText::AsCultureInvariant(TEXT("{0}\n{0}\n{0}")),Copy);
    Hint->SetText(Copy);FVector2D Area=GetPaintSpaceGeometry().GetLocalSize();if(Area.X<1 || Area.Y<1) Area=FVector2D(1280,720);
    const float Width=FMath::Max(240.f,FMath::Min(1100.f,float(Area.X)-48));Bounds->SetWidthOverride(Width);Bounds->SetMaxDesiredHeight(FMath::Max(120.f,float(Area.Y)*.45f));
    Hint->SetWrapTextAt(FMath::Max(100.f,Width-44));Bounds->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}
void UMCUnifiedHubWidget::NativeDestruct() {UnbindControlsNotification();ClearControlsPresentation(Backend.Get());Backend.Reset();bControlsPrepared=false;bReadinessTitleMeasured=false;MeasuredReadinessTitle.Reset();Super::NativeDestruct();}
void UMCUnifiedHubWidget::OnLocalizationChanged() {Super::OnLocalizationChanged();bControlsPrepared=false;PrepareControlsPresentation();RefreshControlsPresentation();}
void UMCUnifiedHubWidget::ApplyEditorPreview(float Time)
{
    Super::ApplyEditorPreview(Time);BuildPresentationTree();if(!IsDesignTime() || !WidgetTree) return;
    const bool Selecting=PreviewState==TEXT("Selecting") || PreviewState==TEXT("Selection") || PreviewState==TEXT("Travelling") || PreviewState==TEXT("SelectingGuest") || PreviewScenario==EMCGameplayUIPreviewScenario::Selected;
    const bool Travelling=PreviewState==TEXT("Travelling"),Guest=PreviewState==TEXT("Guest") || PreviewState==TEXT("SelectingGuest"),Waiting=PreviewScenario==EMCGameplayUIPreviewScenario::Waiting;
    ShowMode(!Selecting);
    auto Set=[&](FName Name,const FText& Copy) {if(auto* Target=WidgetTree->FindWidget<UTextBlock>(Name)) if(!Target->GetText().EqualTo(Copy)) Target->SetText(Copy);};
    Set(TEXT("HubV2ReadinessCount"),UMCUIDataLibrary::FormatTextForCulture(Localized(TEXT("hub.ready.count"),LOCTEXT("Ready","ГОТОВЫ · {Ready} / {Total}")),{{TEXT("Ready"),Waiting?3:4},{TEXT("Total"),4}},PreviewCulture));
    Set(TEXT("HubV2ReadinessHint"),Localized(TEXT("hub.ready.flow"),LOCTEXT("Flow","Чтобы начать игру, всей командой встаньте в подсвеченную READY-зону. Затем хост нажимает Enter и выбирает локацию.")));
    Set(TEXT("HubV2ProfileName"),FText::AsCultureInvariant(PreviewScenario==EMCGameplayUIPreviewScenario::LongText?TEXT("HASBULADO_WITH_A_VERY_LONG_PLAYER_NAME"):TEXT("HASBULADO")));
    Set(TEXT("HubV2WalletCount"),UMCUIDataLibrary::NumberForCulture(PreviewCoins,PreviewCulture));Set(TEXT("HubV2FishCount"),UMCUIDataLibrary::NumberForCulture(PreviewFish,PreviewCulture));
    Set(TEXT("HubV2PartyHint"),UMCUIDataLibrary::FormatTextForCulture(Localized(TEXT("hub.bottom.party"),LOCTEXT("Party","Команда {Count}/4 · {Key}")),{{TEXT("Count"),4},{TEXT("Key"),FText::AsCultureInvariant(TEXT("[Tab]"))}},PreviewCulture));
    Set(TEXT("HubV2SelectionTitle"),Localized(TEXT("hub.selection.title"),LOCTEXT("Destination","ВЫБОР ЛОКАЦИИ")));
    Set(TEXT("HubV2SelectionStatus"),Localized(Travelling?TEXT("hub.loading"):Waiting?TEXT("hub.loading.players"):Guest?TEXT("hub.guest.hint"):TEXT("hub.selection.hint"),
        Travelling?LOCTEXT("Travelling","ЗАГРУЖАЕМ ДЕНЬ…"):Waiting?LOCTEXT("Waiting","Ждём загрузку остальных игроков…"):Guest?LOCTEXT("Guest","Хост выбирает день. ESC — меню."):LOCTEXT("Host","Выберите локацию. Вход — клик по выбранному рту или Enter. Назад — Esc.")));
    const FName Names[]={TEXT("HubV2OpenSelection"),TEXT("HubV2Launch"),TEXT("HubV2Return")};
    for(int32 I=0;I<3;++I) if(auto* Button=WidgetTree->FindWidget<UMCUniversalUIWidget>(Names[I])) {
        if(I==1) Button->Label=Localized(TEXT("hub.selection.start"),LOCTEXT("Launch","ENTER · НАЧАТЬ"));
        else if(I==2) Button->Label=Localized(TEXT("hub.selection.cancel"),LOCTEXT("Return","ESC · ВЕРНУТЬСЯ В ХАБ"));
        Button->SetVisibility(Guest?ESlateVisibility::Collapsed:ESlateVisibility::Visible);Button->SetIsEnabled(!Travelling && (!Waiting || I==2));Button->ApplyTheme();
    }
    RefreshViewportLayout();PresentControlsPreview();
}
#undef LOCTEXT_NAMESPACE
