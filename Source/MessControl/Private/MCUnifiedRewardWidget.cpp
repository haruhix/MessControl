#include "MCUnifiedRewardWidget.h"

#include "MCGameplayUIFactory.h"
#include "MCPerkComponent.h"
#include "MCUIDataLibrary.h"
#include "MCUniversalUIWidget.h"
#include "MCUIGridScrollBox.h"
#include "UObject/UObjectGlobals.h"
#include "Blueprint/WidgetTree.h"
#include "Components/OverlaySlot.h"
#include "Components/Overlay.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "InputCoreTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"
#include "Internationalization/BreakIterator.h"

#define LOCTEXT_NAMESPACE "MCUnifiedReward"

namespace
{
    // Outer bounds measured on the Reward composition in 13_PERK.png.
    const FVector2D RewardDesignViewport(1778,1080);
    const FVector2D RewardDesignSize(1197,920);
    const FVector2D RewardCardSurfaceSize(320,435);
    const FVector2D RewardArtworkSize(253,225);
    constexpr float RewardCardShadowRight=16.f;
    constexpr float RewardCardGap=27.f;
    constexpr float RewardArtworkGap=10.f;
    constexpr int32 RewardTitleFontSize=50; // 49 px capitals in the PSD export.
    constexpr int32 RewardDescriptionFontSize=13; // 20 px line pitch in the PSD export.

    bool UsesOrdinaryOfferLayout(const TArray<FMCUnifiedRewardCard>& Cards,bool bShowingReplacement,bool bShowingContract)
    {
        if(bShowingReplacement || bShowingContract) return false;
        int32 VisibleCards=0;
        for(const auto& Card:Cards)
            if(Card.Host && Card.Host->GetVisibility()!=ESlateVisibility::Collapsed) ++VisibleCards;
        return VisibleCards>0 && VisibleCards<=3;
    }

    void InstallRewardModalScale(UWidgetTree* Tree)
    {
        if(Tree->FindWidget(TEXT("RewardV2ModalScale"))) return;
        auto* Bounds=Tree->FindWidget<USizeBox>(TEXT("RewardV2Size"));
        if(!Bounds) return;

        auto* AlignmentRoot=Cast<UOverlay>(Bounds->GetParent());
        if(Tree->RootWidget==Bounds) {
            AlignmentRoot=Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("RewardV2AlignmentRoot"));
            Tree->RootWidget=AlignmentRoot;
        }
        if(!AlignmentRoot) return;

        // Install before Slate takes the tree. Moving a live SizeBox can retain
        // the old Slate branch for a paint and draw a second modal underneath.
        Bounds->RemoveFromParent();
        auto* Scale=Tree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),TEXT("RewardV2ModalScale"));
        Scale->SetStretch(EStretch::UserSpecified);
        Scale->SetContent(Bounds);
        auto* ModalSlot=AlignmentRoot->AddChildToOverlay(Scale);
        ModalSlot->SetHorizontalAlignment(HAlign_Center);
        ModalSlot->SetVerticalAlignment(VAlign_Center);
    }

    FLinearColor MCUnifiedRewardPrivate_Color(const TCHAR* Hex) { return FLinearColor(FColor::FromHex(Hex)); }
    const FLinearColor MCUnifiedRewardPrivate_Blue=MCUnifiedRewardPrivate_Color(TEXT("4C76AE")),MCUnifiedRewardPrivate_Paper=MCUnifiedRewardPrivate_Color(TEXT("FAFCFB")),MCUnifiedRewardPrivate_Ink=MCUnifiedRewardPrivate_Color(TEXT("44232C"));
    const FLinearColor MCUnifiedRewardPrivate_Dark=MCUnifiedRewardPrivate_Color(TEXT("404243")),MCUnifiedRewardPrivate_Pink=MCUnifiedRewardPrivate_Color(TEXT("AE4C6C")),MCUnifiedRewardPrivate_Red=MCUnifiedRewardPrivate_Color(TEXT("B21414"));
    const FLinearColor MCUnifiedRewardPrivate_Purple=MCUnifiedRewardPrivate_Color(TEXT("A05EFB")),MCUnifiedRewardPrivate_Yellow=MCUnifiedRewardPrivate_Color(TEXT("FFD907"));
    FName MCUnifiedRewardPrivate_Part(const TCHAR* Prefix,int32 Index) { return FName(*FString::Printf(TEXT("RewardV2%s%d"),Prefix,Index)); }
    struct MCUnifiedRewardPrivate_TextMetrics {float Word=0,Height=0;};
    MCUnifiedRewardPrivate_TextMetrics MCUnifiedRewardPrivate_MeasureText(const FText& Text,const FSlateFontInfo& Font,float WrapWidth=0)
    {
        MCUnifiedRewardPrivate_TextMetrics Result;
        if(Text.IsEmpty() || !FSlateApplication::IsInitialized()) return Result;
        const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        const float LineHeight=Measure->Measure(FText::FromString(TEXT("Ag")),Font).Y;
        TArray<FString> Paragraphs;Text.ToString().ParseIntoArray(Paragraphs,TEXT("\n"),false);
        auto Breaks=FBreakIterator::CreateLineBreakIterator();int32 LineCount=0;
        for(const FString& Paragraph:Paragraphs) {
            Breaks->SetStringRef(Paragraph);int32 Start=Breaks->ResetToBeginning();float LineWidth=0;++LineCount;
            for(int32 End=Breaks->MoveToNext();End!=INDEX_NONE;Start=End,End=Breaks->MoveToNext()) {
                const FString Span=Paragraph.Mid(Start,End-Start);const FString Word=Span.TrimStartAndEnd();
                Result.Word=FMath::Max(Result.Word,float(Measure->Measure(FText::FromString(Word),Font).X));
                if(WrapWidth>0) {
                    const float Width=Measure->Measure(FText::FromString(Span),Font).X;
                    if(LineWidth>0 && LineWidth+Width>WrapWidth) {++LineCount;LineWidth=0;}
                    LineCount+=FMath::Max(0,FMath::CeilToInt(Width/WrapWidth)-1);LineWidth=Width>WrapWidth?FMath::Max(1.f,FMath::Fmod(Width,WrapWidth)):LineWidth+Width;
                }
            }
        }
        Result.Height=LineCount*LineHeight;return Result;
    }
    UButton* MCUnifiedRewardPrivate_InnerButton(UMCUniversalUIWidget* Shape)
    { return Shape && Shape->WidgetTree?Shape->WidgetTree->FindWidget<UButton>(TEXT("UniversalSurface")):nullptr; }
    void MCUnifiedRewardPrivate_SetReadableButton(UMCUniversalUIWidget* Shape,bool bActive)
    {
        // Pending and contract cards retain readable content without activating a source action.
        if(auto* Button=MCUnifiedRewardPrivate_InnerButton(Shape)) {
            Button->SetVisibility(bActive?ESlateVisibility::Visible:ESlateVisibility::SelfHitTestInvisible);
            PRAGMA_DISABLE_DEPRECATION_WARNINGS
            Button->IsFocusable=bActive;
            PRAGMA_ENABLE_DEPRECATION_WARNINGS
        }
    }
}

void UMCUnifiedRewardBinding::Clicked()
{
    if(Owner.IsValid() && SourceButton.IsValid()) Owner->ActivateSourceButton(SourceButton.Get());
}
UMCUnifiedRewardWidget::UMCUnifiedRewardWidget()
{
    bUseNativeLayout=false;bOverridePreviewUIVersion=true;SetIsFocusable(true);
    PreviewPerkIDs={TEXT("B2_ReturnTrip"),TEXT("B1_WideBristles"),TEXT("B1_HeavyHit"),TEXT("B1_LightFoot")};
}
UMCUniversalUIWidget* UMCUnifiedRewardWidget::MakeShape(FName Name,FLinearColor Fill,bool bButton)
{
    auto* Shape=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),Name);
    Shape->Kind=bButton?EMCUniversalUIKind::Button:EMCUniversalUIKind::Panel;
    Shape->bUseNativeLayout=false;Shape->bUseThemeShape=false;Shape->bCustomColors=true;
    Shape->CustomFill=Fill;Shape->CustomText=MCUnifiedRewardPrivate_Ink;Shape->CustomOutline=MCUnifiedRewardPrivate_Ink;Shape->CustomShadow=MCUnifiedRewardPrivate_Ink;
    Shape->CornerRadius=18;Shape->OutlineWidth=3;Shape->ContentPadding=FMargin(14,10);Shape->ShadowOffset={5,7};
    Shape->Initialize();Shape->BuildPresentationTree();return Shape;
}
UTextBlock* UMCUnifiedRewardWidget::MakeText(FName Name,int32 FontSize,FLinearColor TextColor)
{
    auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
    Text->SetAutoWrapText(true);Text->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
    Text->SetJustification(ETextJustify::Center);Text->SetVisibility(ESlateVisibility::HitTestInvisible);
    Text->SetColorAndOpacity(TextColor);
    if(auto* T=GetResolvedTheme()) Text->SetFont(T->MakeGameplayFont(FontSize));
    BindStyle(Name,TextColor==MCUnifiedRewardPrivate_Paper?EMCUIColorToken::Cream:EMCUIColorToken::Ink,EMCUIStyleTarget::Text,FontSize,true);
    return Text;
}
void UMCUnifiedRewardWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(!Theme) Theme=UMCGameplayUIFactory::GetThemeForVersion(EMCUIVersion::New);
    if(WidgetTree->RootWidget) {
        InstallRewardModalScale(WidgetTree);
        ResolvePresentationTree();UpdateResponsiveLayout();return;
    }
    auto* Size=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("RewardV2Size"));
    Size->SetMinDesiredWidth(1040);Size->SetMaxDesiredWidth(1240);WidgetTree->RootWidget=Size;
    auto* Frame=MakeShape(TEXT("RewardV2Frame"),MCUnifiedRewardPrivate_Blue);Frame->CustomOutline=MCUnifiedRewardPrivate_Paper;Frame->OutlineWidth=4;
    Frame->ShadowOffset={8,10};Frame->ContentPadding=FMargin(24,18,24,22);
    Frame->ContourPoints={{0,.14},{.24,.14},{.24,0},{.76,0},{.76,.14},{1,.14},{1,1},{0,1}};Frame->ApplyTheme();Size->SetContent(Frame);
    if(auto* FrameSlot=Cast<USizeBoxSlot>(Frame->Slot)) FrameSlot->SetVerticalAlignment(VAlign_Top);
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("RewardV2Column"));
    Frame->SetContentForSlot(TEXT("Content"),Column);
    auto* Header=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("RewardV2HeaderRow"));Column->AddChildToVerticalBox(Header)->SetPadding(FMargin(0,0,0,12));
    auto* Plate=MakeShape(TEXT("RewardV2TitlePlate"),MCUnifiedRewardPrivate_Paper);Plate->CustomText=MCUnifiedRewardPrivate_Pink;Plate->CustomShadow=MCUnifiedRewardPrivate_Ink;
    Plate->LabelKey=TEXT("UI.Reward.Title");Plate->Label=LOCTEXT("Reward","REWARD");Plate->FontSize=36;Plate->ContentPadding=FMargin(30,8);Plate->ApplyTheme();
    auto* PlateWidth=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("RewardV2TitleBounds"));PlateWidth->SetMinDesiredWidth(360);PlateWidth->SetMaxDesiredWidth(640);PlateWidth->SetContent(Plate);
    auto* HeaderCell=Header->AddChildToHorizontalBox(PlateWidth);HeaderCell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));HeaderCell->SetHorizontalAlignment(HAlign_Center);
    auto* Timer=MakeShape(TEXT("RewardV2Countdown"),MCUnifiedRewardPrivate_Paper);Timer->CustomText=MCUnifiedRewardPrivate_Red;Timer->CustomShadow=MCUnifiedRewardPrivate_Ink;Timer->FontSize=20;Timer->ContentPadding=FMargin(16,10);Timer->ApplyTheme();
    auto* TimerWidth=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("RewardV2CountdownBounds"));TimerWidth->SetMinDesiredWidth(190);TimerWidth->SetMaxDesiredWidth(290);TimerWidth->SetContent(Timer);
    auto* Context=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("RewardV2ContextRow"));
    Column->AddChildToVerticalBox(Context)->SetPadding(FMargin(8,8,8,8));
    auto* TitleCell=Context->AddChildToHorizontalBox(MakeText(TEXT("RewardV2SourceTitle"),22,MCUnifiedRewardPrivate_Paper));
    TitleCell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));TitleCell->SetVerticalAlignment(VAlign_Center);TitleCell->SetPadding(FMargin(8,2,12,4));
    auto* TimerCell=Context->AddChildToHorizontalBox(TimerWidth);TimerCell->SetPadding(FMargin(12,0,0,0));TimerCell->SetVerticalAlignment(VAlign_Center);
    Column->AddChildToVerticalBox(MakeText(TEXT("RewardV2Subtitle"),18,MCUnifiedRewardPrivate_Paper))->SetPadding(FMargin(8,2,8,16));
    auto* Row=WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(),TEXT("RewardV2Cards"));Row->SetExplicitWrapSize(true);Row->SetWrapSize(1180);Row->SetInnerSlotPadding({18,16});
    auto* RowCell=Column->AddChildToVerticalBox(Row);RowCell->SetHorizontalAlignment(HAlign_Center);RowCell->SetPadding(FMargin(4,0,12,14));
    for(int32 I=0;I<4;++I) {
        auto* Width=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),MCUnifiedRewardPrivate_Part(TEXT("CardBounds"),I));Width->SetWidthOverride(310);
        auto* CardColumn=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),MCUnifiedRewardPrivate_Part(TEXT("CardColumn"),I));Width->SetContent(CardColumn);
        Row->AddChildToWrapBox(Width)->SetVerticalAlignment(VAlign_Top);
        auto* Type=MakeShape(MCUnifiedRewardPrivate_Part(TEXT("Type"),I),MCUnifiedRewardPrivate_Paper);Type->ShadowOffset={0,0};Type->FontSize=18;Type->ContentPadding=FMargin(12,7);Type->ApplyTheme();
        CardColumn->AddChildToVerticalBox(Type)->SetPadding(FMargin(12,0,12,9));
        auto* Surface=MakeShape(MCUnifiedRewardPrivate_Part(TEXT("Card"),I),MCUnifiedRewardPrivate_Paper,true);Surface->ContentPadding=FMargin(14);Surface->ShadowOffset={6,8};Surface->ApplyTheme();
        CardColumn->AddChildToVerticalBox(Surface)->SetPadding(FMargin(0,0,6,12));
        auto* Body=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),MCUnifiedRewardPrivate_Part(TEXT("CardBody"),I));Surface->SetContentForSlot(TEXT("Content"),Body);
        auto* IconPanel=MakeShape(MCUnifiedRewardPrivate_Part(TEXT("IconPanel"),I),MCUnifiedRewardPrivate_Dark);IconPanel->ShadowOffset={0,0};IconPanel->OutlineWidth=0;IconPanel->ContentPadding=FMargin(12);IconPanel->ApplyTheme();
        auto* IconSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),MCUnifiedRewardPrivate_Part(TEXT("IconBounds"),I));IconSize->SetWidthOverride(250);IconSize->SetHeightOverride(250);IconSize->SetContent(IconPanel);Body->AddChildToVerticalBox(IconSize)->SetPadding(FMargin(4,0,4,10));
        auto* Fit=WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),MCUnifiedRewardPrivate_Part(TEXT("IconFit"),I));Fit->SetStretch(EStretch::ScaleToFit);IconPanel->SetContentForSlot(TEXT("Content"),Fit);
        auto* Icon=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),MCUnifiedRewardPrivate_Part(TEXT("Icon"),I));Icon->SetVisibility(ESlateVisibility::Hidden);Fit->SetContent(Icon);
        auto* CopySize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),MCUnifiedRewardPrivate_Part(TEXT("CopyBounds"),I));Body->AddChildToVerticalBox(CopySize);
        auto* Copy=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),MCUnifiedRewardPrivate_Part(TEXT("CopyColumn"),I));CopySize->SetContent(Copy);
        const TCHAR* Suffixes[]={TEXT("Name"),TEXT("Description"),TEXT("Stats"),TEXT("Metadata")};const int32 Fonts[]={22,18,17,15};
        for(int32 L=0;L<4;++L) {
            auto* Text=MakeText(MCUnifiedRewardPrivate_Part(Suffixes[L],I),Fonts[L],MCUnifiedRewardPrivate_Ink);Text->SetAutoWrapText(false);Text->SetWrapTextAt(262);
            auto* Bounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),MCUnifiedRewardPrivate_Part(*FString::Printf(TEXT("%sBounds"),Suffixes[L]),I));Bounds->SetMaxDesiredWidth(262);Bounds->SetContent(Text);
            auto* Cell=Copy->AddChildToVerticalBox(Bounds);Cell->SetHorizontalAlignment(HAlign_Center);Cell->SetPadding(FMargin(0,L?5:0,0,7));
        }
        auto* Rarity=MakeShape(MCUnifiedRewardPrivate_Part(TEXT("Rarity"),I),MCUnifiedRewardPrivate_Paper);Rarity->FontSize=20;Rarity->ContentPadding=FMargin(12,7);Rarity->ApplyTheme();
        CardColumn->AddChildToVerticalBox(Rarity)->SetPadding(FMargin(12,0,12,0));
        if(I==3) Width->SetVisibility(ESlateVisibility::Collapsed);
    }
    auto* Actions=WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(),TEXT("RewardV2Actions"));Actions->SetExplicitWrapSize(true);Actions->SetWrapSize(1100);Actions->SetInnerSlotPadding({16,12});
    auto* ActionsCell=Column->AddChildToVerticalBox(Actions);ActionsCell->SetHorizontalAlignment(HAlign_Center);ActionsCell->SetPadding(FMargin(0,4,0,12));
    for(int32 I=0;I<4;++I) {
        auto* Action=MakeShape(MCUnifiedRewardPrivate_Part(TEXT("Action"),I),MCUnifiedRewardPrivate_Paper,true);Action->CustomText=MCUnifiedRewardPrivate_Red;Action->FontSize=23;Action->ContentPadding=FMargin(26,9);Action->ApplyTheme();
        auto* ActionSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),MCUnifiedRewardPrivate_Part(TEXT("ActionBounds"),I));ActionSize->SetMaxDesiredWidth(520);ActionSize->SetContent(Action);Actions->AddChildToWrapBox(ActionSize);
        if(I) ActionSize->SetVisibility(ESlateVisibility::Collapsed);
    }
    Column->AddChildToVerticalBox(MakeText(TEXT("RewardV2Hint"),16,MCUnifiedRewardPrivate_Paper))->SetPadding(FMargin(12,4,12,0));
    InstallRewardModalScale(WidgetTree);
    ResolvePresentationTree();
}

void UMCUnifiedRewardWidget::ResolvePresentationTree()
{
    if(!WidgetTree) return;
    Cards.SetNum(4);ActionViews.SetNum(4);
    for(int32 I=0;I<4;++I) {
        auto& Card=Cards[I];
        Card.Host=WidgetTree->FindWidget(MCUnifiedRewardPrivate_Part(TEXT("CardBounds"),I));Card.Width=Cast<USizeBox>(Card.Host);
        Card.Surface=WidgetTree->FindWidget<UMCUniversalUIWidget>(MCUnifiedRewardPrivate_Part(TEXT("Card"),I));
        Card.Type=WidgetTree->FindWidget<UMCUniversalUIWidget>(MCUnifiedRewardPrivate_Part(TEXT("Type"),I));Card.Rarity=WidgetTree->FindWidget<UMCUniversalUIWidget>(MCUnifiedRewardPrivate_Part(TEXT("Rarity"),I));
        Card.Icon=WidgetTree->FindWidget<UImage>(MCUnifiedRewardPrivate_Part(TEXT("Icon"),I));Card.Name=WidgetTree->FindWidget<UTextBlock>(MCUnifiedRewardPrivate_Part(TEXT("Name"),I));
        Card.Description=WidgetTree->FindWidget<UTextBlock>(MCUnifiedRewardPrivate_Part(TEXT("Description"),I));Card.Stats=WidgetTree->FindWidget<UTextBlock>(MCUnifiedRewardPrivate_Part(TEXT("Stats"),I));
        Card.Metadata=WidgetTree->FindWidget<UTextBlock>(MCUnifiedRewardPrivate_Part(TEXT("Metadata"),I));Card.BodyScroll=WidgetTree->FindWidget<UScrollBox>(MCUnifiedRewardPrivate_Part(TEXT("CopyScroll"),I));
        ActionViews[I]=WidgetTree->FindWidget<UMCUniversalUIWidget>(MCUnifiedRewardPrivate_Part(TEXT("Action"),I));
    }
}
void UMCUnifiedRewardWidget::BindSourceControl(UMCUniversalUIWidget* View,UMCPerkCardButton* Source)
{
    if(!View || IsDesignTime() || !Backend.IsValid()) return;
    if(auto* Existing=Bindings.FindByPredicate([View](const auto& Binding){return Binding && Binding->View==View;})) {
        (*Existing)->SourceButton=Source;return;
    }
    auto* Binding=NewObject<UMCUnifiedRewardBinding>(this);Binding->Owner=this;Binding->SourceButton=Source;Binding->View=View;
    Bindings.Add(Binding);View->OnClicked.AddUniqueDynamic(Binding,&UMCUnifiedRewardBinding::Clicked);
}
FText UMCUnifiedRewardWidget::TypeLabel(EMCPerkKind Kind) const
{
    auto Copy=[this](FName Key,const FText& Fallback){return Backend.IsValid()?Backend->Localized(Key,Fallback):UMCUIDataLibrary::ResolveText(GetResolvedTranslationTable(),Key,Fallback,PreviewCulture);};
    switch(Kind) {
    case EMCPerkKind::Active:return Copy(TEXT("UI.Reward.Type.Active"),LOCTEXT("Active","ACTIVE SKILL"));
    case EMCPerkKind::Consumable:return Copy(TEXT("UI.Reward.Type.Consumable"),LOCTEXT("Consumable","CONSUMABLE"));
    case EMCPerkKind::ToolUpgrade:return Copy(TEXT("UI.Reward.Type.Tool"),LOCTEXT("Tool","TOOL UPGRADE"));
    default:return Copy(TEXT("UI.Reward.Type.Passive"),LOCTEXT("Passive","PASSIVE SKILL"));
    }
}
FText UMCUnifiedRewardWidget::RarityLabel(EMCPerkRarity Rarity) const
{
    auto Copy=[this](FName Key,const FText& Fallback){return Backend.IsValid()?Backend->Localized(Key,Fallback):UMCUIDataLibrary::ResolveText(GetResolvedTranslationTable(),Key,Fallback,PreviewCulture);};
    switch(Rarity) {
    case EMCPerkRarity::Rare:return Copy(TEXT("UI.Reward.Rarity.Rare"),LOCTEXT("Rare","RARE"));
    case EMCPerkRarity::Legendary:return Copy(TEXT("UI.Reward.Rarity.Legendary"),LOCTEXT("Legendary","LEGENDARY"));
    default:return Copy(TEXT("UI.Reward.Rarity.Standard"),LOCTEXT("Common","COMMON"));
    }
}
void UMCUnifiedRewardWidget::Present(const FMCPerkChoicePresentationSnapshot& Snapshot)
{
    if(!WidgetTree) return;ResolvePresentationTree();SelectedSource=Snapshot.Selected;bShowingContract=Snapshot.bConfirming;
    if(bShowingReplacement!=Snapshot.bReplacing)
    {
        bReplacementStickLatched=false;
        if(ReplacementGridScroll) ReplacementGridScroll->ScrollToCell(0);
    }
    bShowingReplacement=Snapshot.bReplacing;
    auto Copy=[this](FName Name,const FText& Text) {if(auto* Label=WidgetTree->FindWidget<UTextBlock>(Name)) Label->SetText(Text);};
    Copy(TEXT("RewardV2SourceTitle"),Snapshot.Title);Copy(TEXT("RewardV2Subtitle"),Snapshot.Subtitle);Copy(TEXT("RewardV2Hint"),Snapshot.Hint);
    // The PSD has one instruction and no repeated stage heading or input legend.
    // Status/contract states retain their meaningful source heading and hint.
    const bool HasContext=Snapshot.bWaiting || Snapshot.bConfirming || Snapshot.bReplacing;
    if(auto* Heading=WidgetTree->FindWidget(TEXT("RewardV2SourceTitle"))) Heading->SetVisibility(HasContext?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(auto* Context=WidgetTree->FindWidget(TEXT("RewardV2ContextRow"))) Context->SetVisibility(HasContext?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    if(auto* Hint=WidgetTree->FindWidget(TEXT("RewardV2Hint"))) Hint->SetVisibility((Snapshot.bWaiting || Snapshot.bPending || Snapshot.bConfirming || Snapshot.bReplacing)?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(auto* Timer=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("RewardV2Countdown"))) {
        Timer->SetLabel(NAME_None,Snapshot.Countdown);
        FString Prefix=Snapshot.Countdown.ToString(),Value;int32 Number=INDEX_NONE;
        for(int32 C=0;C<Prefix.Len();++C) if(FChar::IsDigit(Prefix[C])) {Number=C;break;}
        if(Number!=INDEX_NONE) {Value=Prefix.Mid(Number).TrimStartAndEnd();Prefix=Prefix.Left(Number).TrimStartAndEnd();}
        Copy(TEXT("RewardV2CountdownPrefix"),FText::FromString(Prefix));Copy(TEXT("RewardV2CountdownValue"),FText::FromString(Value));
        if(auto* Label=WidgetTree->FindWidget(TEXT("RewardV2CountdownValue"))) Label->SetVisibility(Value.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    }
    if(auto* Bounds=WidgetTree->FindWidget(TEXT("RewardV2CountdownBounds"))) Bounds->SetVisibility(Snapshot.bCountdownVisible?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    PreferredCardWidth=AdaptiveCardWidth(Snapshot);
    const float CardWidth=ResponsiveViewportSize.X>1?FMath::Min(PreferredCardWidth,FMath::Max(1.f,float(ResponsiveViewportSize.X)-78.f)):PreferredCardWidth;
    const float RowWidth=FMath::Min(1516.f,Snapshot.Cards.Num()*CardWidth+FMath::Max(0,Snapshot.Cards.Num()-1)*18.f);
    if(auto* Row=WidgetTree->FindWidget<UWrapBox>(TEXT("RewardV2Cards"))) Row->SetWrapSize(FMath::Max(CardWidth,RowWidth));

    for(int32 I=0;I<Cards.Num();++I) {
        auto& Card=Cards[I];const auto* Data=Snapshot.Cards.IsValidIndex(I)?&Snapshot.Cards[I]:nullptr;
        if(Card.Host) Card.Host->SetVisibility(Data?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
        BindSourceControl(Card.Surface,Data?Data->Button.Get():nullptr);
        if(Data) Card.SourceButton=Data->Button;else Card.SourceButton.Reset();
        if(!Data) continue;
        if(Card.Width) Card.Width->SetWidthOverride(CardWidth);
        const bool Changed=Card.PerkID!=Data->Data.GameplayID;Card.PerkID=Data->Data.GameplayID;
        if(Changed && Card.BodyScroll) Card.BodyScroll->SetScrollOffset(0);
        if(Card.Type) Card.Type->SetLabel(NAME_None,TypeLabel(Data->Kind));
        if(Card.Name) Card.Name->SetText(Data->Data.DisplayName);
        if(Card.Description) Card.Description->SetText(Data->Data.Description);
        if(Card.Stats) Card.Stats->SetText(Data->Data.StackPreview);
        if(Card.Metadata) Card.Metadata->SetText(Data->Data.Badge);
        if(Card.Surface) Card.Surface->SetToolTipText(FText::Join(FText::FromString(TEXT("\n")),TArray<FText>{Data->Data.DisplayName,Data->Data.Description,Data->Data.StackPreview,Data->Data.Badge}));
        for(UTextBlock* Label:{Card.Name.Get(),Card.Description.Get(),Card.Stats.Get(),Card.Metadata.Get()}) if(Label) {
            Label->SetWrapTextAt(CardWidth-56);if(auto* Bounds=Cast<USizeBox>(Label->GetParent())) Bounds->SetMaxDesiredWidth(CardWidth-56);
            const bool ContextCopy=(Label==Card.Stats && (Snapshot.bReplacing || Snapshot.bConfirming)) || (Label==Card.Metadata && Snapshot.bConfirming);
            const bool OrdinaryCopy=Label==Card.Name || Label==Card.Description;
            if(auto* Host=Label->GetParent()) Host->SetVisibility((OrdinaryCopy || ContextCopy) && !Label->GetText().IsEmpty()?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
        }
        if(Card.Icon) {
            // Runtime resources are prewarmed by the existing perk catalog; never
            // replace an empty canonical icon with a legacy/private lookup.
            UTexture2D* Texture=Data->Data.Image.Get();
            if(!Texture && !Backend.IsValid()) Texture=Data->Data.Image.LoadSynchronous();
            Card.Icon->SetBrushFromTexture(Texture,true);Card.Icon->SetVisibility(Texture?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
        }
        if(Card.Rarity) {
            Card.Rarity->CustomFill=Data->Rarity==EMCPerkRarity::Legendary?MCUnifiedRewardPrivate_Yellow:Data->Rarity==EMCPerkRarity::Rare?MCUnifiedRewardPrivate_Purple:MCUnifiedRewardPrivate_Paper;
            Card.Rarity->CustomText=MCUnifiedRewardPrivate_Ink;Card.Rarity->SetLabel(NAME_None,RarityLabel(Data->Rarity));
        }
        if(Card.Surface) {
            Card.Surface->CustomFill=Snapshot.bNavigationHighlight && Data->Button==Snapshot.Selected?MCUnifiedRewardPrivate_Color(TEXT("FFE2ED")):MCUnifiedRewardPrivate_Paper;
            Card.Surface->ApplyTheme();MCUnifiedRewardPrivate_SetReadableButton(Card.Surface,Data->bEnabled && Backend.IsValid());
        }
        // Narrow four-slot cards still grow their labels vertically; no font scale.
        for(UMCUniversalUIWidget* Badge:{Card.Type.Get(),Card.Rarity.Get()}) if(Badge && Badge->WidgetTree)
            if(auto* Label=Badge->WidgetTree->FindWidget<UTextBlock>(TEXT("UniversalLabel"))) {Label->SetAutoWrapText(false);Label->SetWrapTextAt(CardWidth-56);}
    }
    for(int32 I=0;I<ActionViews.Num();++I) if(UMCUniversalUIWidget* View=ActionViews[I]) {
        const auto* Action=Snapshot.Actions.IsValidIndex(I)?&Snapshot.Actions[I]:nullptr;
        if(auto* Host=View->GetParent()) Host->SetVisibility(Action?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
        BindSourceControl(View,Action?Action->Button.Get():nullptr);
        if(Action) {
            View->SetLabel(NAME_None,Action->Label);View->ApplyTheme();MCUnifiedRewardPrivate_SetReadableButton(View,Action->bEnabled && Backend.IsValid());
            if(View->WidgetTree) if(auto* Label=View->WidgetTree->FindWidget<UTextBlock>(TEXT("UniversalLabel"))) {Label->SetAutoWrapText(false);Label->SetWrapTextAt(450);}
        }
    }
    UpdateResponsiveLayout();UpdateHeaderContour();RefreshTextBounds();
}
void UMCUnifiedRewardWidget::SetBackend(UMCPerkChoiceWidget* Source)
{
    if(Backend.Get()!=Source) {
        Backend=Source;SelectedSource.Reset();
        for(UMCUnifiedRewardBinding* Binding:Bindings) if(Binding) Binding->SourceButton.Reset();
    }
    if(Source) RefreshFromBackend();
}
void UMCUnifiedRewardWidget::RefreshFromBackend()
{
    if(IsDesignTime() || !Backend.IsValid()) return;
    BuildPresentationTree();Present(Backend->GetPresentationSnapshot());
}
UWidget* UMCUnifiedRewardWidget::GetDefaultFocusTarget() const
{
    for(const UMCUnifiedRewardBinding* Binding:Bindings) if(Binding && Binding->SourceButton.IsValid() && Binding->SourceButton==SelectedSource && Binding->View)
        if(auto* Button=MCUnifiedRewardPrivate_InnerButton(Binding->View)) if(Button->GetVisibility()==ESlateVisibility::Visible) return Button;
    for(const UMCUnifiedRewardBinding* Binding:Bindings) if(Binding && Binding->SourceButton.IsValid() && Binding->SourceButton->GetIsEnabled() && Binding->SourceButton->IsInteractionAllowed())
        if(auto* Button=MCUnifiedRewardPrivate_InnerButton(Binding->View)) if(Button->GetVisibility()==ESlateVisibility::Visible) return Button;
    return const_cast<UMCUnifiedRewardWidget*>(this);
}
void UMCUnifiedRewardWidget::ActivateSourceButton(UMCPerkCardButton* Button)
{
    if(!IsDesignTime() && Backend.IsValid()) {Backend->TryActivatePresentationButton(Button);RefreshFromBackend();}
}
void UMCUnifiedRewardWidget::RequestBack()
{
    RouteSourceKey(FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));
}
void UMCUnifiedRewardWidget::RefreshFocusFromSource()
{
    if(auto* Target=GetDefaultFocusTarget()) if(auto* PC=GetOwningPlayer()) Target->SetUserFocus(PC);
    // Only the explicit replacement-list navigation moves its viewport. A
    // periodic source refresh must never fight the player's wheel position.
    if(bShowingReplacement && ReplacementGridScroll && ReplacementGridScroll->GetParent())
        for(int32 Index=0;Index<Cards.Num();++Index) if(Cards[Index].Host && Cards[Index].SourceButton==SelectedSource && Cards[Index].Host->IsVisible()) {
            ReplacementGridScroll->ScrollToCell(Index);break;
        }
}
bool UMCUnifiedRewardWidget::RouteSourceKey(const FKeyEvent& Event)
{
    if(IsDesignTime() || !Backend.IsValid()) return false;
    // Slate Tab/arrow navigation can focus a visible proxy without touching the
    // source. Transfer only that control identity before its existing key policy.
    for(const UMCUnifiedRewardBinding* Binding:Bindings) if(Binding && Binding->View &&
        (Binding->View->HasUserFocus(GetOwningPlayer()) || Binding->View->HasUserFocusedDescendants(GetOwningPlayer()))) {
        Backend->SetPresentationFocusButton(Binding->SourceButton.Get());break;
    }
    const bool Handled=Backend->RoutePresentationKey(Event);RefreshFromBackend();
    const FKey Key=Event.GetKey();
    if(Handled && (Key==EKeys::Gamepad_DPad_Left || Key==EKeys::Gamepad_DPad_Right || Key==EKeys::Gamepad_DPad_Up || Key==EKeys::Gamepad_DPad_Down)) RefreshFocusFromSource();
    return Handled;
}
bool UMCUnifiedRewardWidget::RouteSourceAnalog(const FAnalogInputEvent& Event)
{
    if(IsDesignTime() || !Backend.IsValid()) return false;
    if(bShowingReplacement && Event.GetKey()==EKeys::Gamepad_RightY && ReplacementGridScroll && ReplacementGridScroll->GetParent()) {
        const float Value=Event.GetAnalogValue();
        if(FMath::Abs(Value)<.25f) bReplacementStickLatched=false;
        else if(FMath::Abs(Value)>.5f && !bReplacementStickLatched && ReplacementCardGrid) {
            bReplacementStickLatched=true;
            const int32 NextRow=ReplacementGridScroll->GetFirstVisibleRow()+(Value<0.f?1:-1);
            const int32 Cell=FMath::Clamp(NextRow*2,0,FMath::Max(0,ReplacementCardGrid->GetChildrenCount()-1));
            ReplacementGridScroll->ScrollToCell(Cell);
        }
        return true;
    }
    const bool Handled=Backend->RoutePresentationAnalog(Event);RefreshFromBackend();
    if(Handled && (Event.GetKey()==EKeys::Gamepad_LeftX || Event.GetKey()==EKeys::Gamepad_LeftY)) RefreshFocusFromSource();
    return Handled;
}

void UMCUnifiedRewardWidget::PresentPreview()
{
    if(Backend.IsValid()) return;
    FMCPerkChoicePresentationSnapshot Snapshot;Snapshot.bShowing=true;
    auto Copy=[this](FName Key,const FText& Fallback){return UMCUIDataLibrary::ResolveText(GetResolvedTranslationTable(),Key,Fallback,PreviewCulture);};
    Snapshot.Title=Copy(TEXT("UI.Reward.TeamBoosterTitle"),LOCTEXT("PreviewTitle","YOUR STAGE REWARD"));
    Snapshot.Subtitle=Copy(TEXT("UI.Reward.TeamBoosterSubtitle"),LOCTEXT("PreviewSubtitle","Choose your personal reward."));
    Snapshot.Hint=Copy(TEXT("UI.Reward.ChoiceHint"),LOCTEXT("PreviewHint","Choose: mouse / 1 / 2 / 3 · gamepad: D-pad + A"));
    Snapshot.Countdown=UMCUIDataLibrary::FormatTextForCulture(Copy(TEXT("UI.Reward.AutoPickIn"),LOCTEXT("PreviewAutoPickIn","AUTO-PICK IN {Seconds} SEC")),
        FFormatNamedArguments{{TEXT("Seconds"),UMCUIDataLibrary::NumberForCulture(38,PreviewCulture)}},PreviewCulture);Snapshot.bCountdownVisible=true;
    if(PreviewState==TEXT("AutoPicking")) Snapshot.Countdown=Copy(TEXT("UI.Reward.AutoPicking"),LOCTEXT("PreviewAutoPicking","AUTO-PICKING…"));
    const auto* Defaults=GetDefault<UMCPerkComponent>();UDataTable* Table=Defaults?Defaults->PerkTable.LoadSynchronous():nullptr;
    const bool Replacing=PreviewState==TEXT("Replacement");const bool Contract=PreviewState==TEXT("Contract");
    const int32 Count=Replacing?4:Contract?2:3;Snapshot.bReplacing=Replacing;Snapshot.bConfirming=Contract;
    if(Replacing) {
        Snapshot.Title=Copy(TEXT("UI.Skill.ReplaceTitle"),LOCTEXT("ReplaceTitle","FOUR SLOTS OCCUPIED"));
        Snapshot.Subtitle=Copy(TEXT("UI.Skill.ReplaceSubtitle"),LOCTEXT("ReplaceSubtitle","Choose a skill to replace or skip the reward. The timer keeps your old loadout."));
        Snapshot.Hint=Copy(TEXT("UI.Skill.ReplaceHint"),LOCTEXT("ReplaceHint","Slot: 1 / 2 / 3 / 4 · cancel replacement: Backspace · skip: Esc"));
    }
    for(int32 I=0;I<Count;++I) {
        FMCPerkChoicePresentationCard Card;const FName ID=PreviewPerkIDs.IsValidIndex(I)?PreviewPerkIDs[I]:FName(*FString::Printf(TEXT("P%02d"),I+1));
        const auto* Row=Table?Table->FindRow<FMCPerkDefinition>(ID,TEXT("Reward PSD designer fixture"),false):nullptr;
        Card.Data.GameplayID=ID;Card.Data.DisplayName=FText::FromName(ID);
        if(Row) {
            Card.Data.DisplayName=Copy(Row->DisplayNameKey,Row->DisplayName);Card.Data.Description=Copy(Row->DescriptionKey,Row->Description);
            Card.Data.Image=Row->Icon;Card.Kind=UMCPerkComponent::ResolvePerkKind(*Row,ID);Card.Rarity=Row->Rarity;
            UMCPerkCardWidget::ApplyDefinitionMetadata(Card.Data,*Row,GetResolvedTranslationTable(),PreviewCulture,false,0);
        }
        if(PreviewScenario==EMCGameplayUIPreviewScenario::LongText) {
            const FString Original=Card.Data.Description.ToString();Card.Data.Description=FText::FromString(Original+TEXT("\n\n")+Original+TEXT("\n\n")+Original);
        }
        if(Replacing) Card.Data.StackPreview=FText::Format(Copy(TEXT("UI.Skill.ReplaceSlot"),LOCTEXT("ReplaceSlot","Replace slot {Slot}")),FFormatNamedArguments{{TEXT("Slot"),FText::AsNumber(I+1)}});
        Snapshot.Cards.Add(MoveTemp(Card));
    }
    FMCPerkChoicePresentationAction Action;
    if(Replacing) {Action.Label=Copy(TEXT("UI.Skill.Back"),LOCTEXT("BackSkill","BACK TO REWARDS"));Snapshot.Actions.Add(Action);}
    Action.Label=Copy(Contract?TEXT("UI.Reward.AcceptContract"):TEXT("UI.Skill.Skip"),Contract?LOCTEXT("Accept","ACCEPT CONTRACT [ENTER]"):LOCTEXT("Skip","SKIP [ESC]"));Snapshot.Actions.Add(Action);
    if(Contract) {Action.Label=Copy(TEXT("UI.Reward.Decline"),LOCTEXT("Decline","DECLINE [ESC]"));Snapshot.Actions.Add(Action);}
    if(PreviewState==TEXT("WaitingForTeam") || PreviewScenario==EMCGameplayUIPreviewScenario::Waiting) {
        Snapshot.bWaiting=true;Snapshot.bShowing=false;Snapshot.bCountdownVisible=false;Snapshot.Cards.Reset();Snapshot.Actions.Reset();
        Snapshot.Title=Copy(TEXT("UI.Reward.WaitingTitle"),LOCTEXT("Waiting","WAITING FOR TEAM"));
        Snapshot.Subtitle=Copy(TEXT("UI.Reward.WaitingSubtitle"),LOCTEXT("WaitingSubtitle","You are ready. Others are choosing their perks."));
        Snapshot.Hint=FText::Format(Copy(TEXT("UI.Reward.TeamReady"),LOCTEXT("TeamReady","Ready: {Ready} / {Total}")),FFormatNamedArguments{{TEXT("Ready"),FText::AsNumber(1)},{TEXT("Total"),FText::AsNumber(4)}});
    } else if(PreviewState==TEXT("Pending") || PreviewScenario==EMCGameplayUIPreviewScenario::Pending) {
        Snapshot.bPending=true;Snapshot.Hint=Copy(TEXT("UI.Reward.SelectionPending"),LOCTEXT("Pending","Confirming your selection…"));
    }
    Present(Snapshot);
}
void UMCUnifiedRewardWidget::ApplyEditorPreview(float Time)
{
    Super::ApplyEditorPreview(Time);BuildPresentationTree();
    if(Backend.IsValid() && !IsDesignTime()) RefreshFromBackend();else PresentPreview();UpdateResponsiveLayout();
}
void UMCUnifiedRewardWidget::ApplyTheme()
{
    Super::ApplyTheme();
    if(!WidgetTree) return;
    ResolvePresentationTree();
    for(auto& Card:Cards) {
        for(UTextBlock* Label:{Card.Name.Get(),Card.Description.Get(),Card.Stats.Get(),Card.Metadata.Get()}) if(Label) Label->SetColorAndOpacity(MCUnifiedRewardPrivate_Ink);
        const auto* Source=Card.SourceButton.Get();MCUnifiedRewardPrivate_SetReadableButton(Card.Surface,Source && Source->GetIsEnabled() && Source->IsInteractionAllowed());
    }
    for(const UMCUnifiedRewardBinding* Binding:Bindings) if(Binding && Binding->View) {
        const auto* Source=Binding->SourceButton.Get();MCUnifiedRewardPrivate_SetReadableButton(Binding->View,Source && Source->GetIsEnabled() && Source->IsInteractionAllowed());
    }
    UpdateResponsiveLayout();UpdateHeaderContour();
}
float UMCUnifiedRewardWidget::AdaptiveCardWidth(const FMCPerkChoicePresentationSnapshot& Snapshot) const
{
    float Width=FMath::Max(240.f,MinimumCardWidth);if(!bAdaptCardLayout) return Width;
    for(int32 I=0;I<Snapshot.Cards.Num();++I) {
        const auto& Data=Snapshot.Cards[I].Data;
        const FText Texts[]={Data.DisplayName,Data.Description,Data.StackPreview,Data.Badge};
        const UTextBlock* Labels[]={Cards.IsValidIndex(I)?Cards[I].Name.Get():nullptr,Cards.IsValidIndex(I)?Cards[I].Description.Get():nullptr,Cards.IsValidIndex(I)?Cards[I].Stats.Get():nullptr,Cards.IsValidIndex(I)?Cards[I].Metadata.Get():nullptr};
        const int32 Fonts[]={22,18,17,15};
        const int32 TextCount=Snapshot.bConfirming?4:Snapshot.bReplacing?3:2;
        for(int32 L=0;L<TextCount;++L) {
            const auto Font=Labels[L]?Labels[L]->GetFont():GetResolvedTheme()->MakeGameplayFont(Fonts[L]);
            Width=FMath::Max(Width,MCUnifiedRewardPrivate_MeasureText(Texts[L],Font).Word+60.f);
        }
    }
    return FMath::CeilToFloat(FMath::Min(Width,FMath::Max(MinimumCardWidth,MaximumCardWidth)));
}
void UMCUnifiedRewardWidget::UpdateCardCopyHeight()
{
    if(!WidgetTree) return;
    const bool OrdinaryOffers=UsesOrdinaryOfferLayout(Cards,bShowingReplacement,bShowingContract);
    const float Scale=FMath::Max(.01f,GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale());
    float NaturalHeight=0;
    for(int32 Index=0;Index<Cards.Num();++Index) {
        auto& Card=Cards[Index];if(!Card.Host || Card.Host->GetVisibility()==ESlateVisibility::Collapsed) continue;
        auto* Bounds=WidgetTree->FindWidget<USizeBox>(MCUnifiedRewardPrivate_Part(TEXT("CopyBounds"),Index));
        auto* Copy=WidgetTree->FindWidget<UVerticalBox>(MCUnifiedRewardPrivate_Part(TEXT("CopyColumn"),Index));
        if(!Bounds || !Copy) continue;
        Bounds->ClearHeightOverride();Bounds->ClearMaxDesiredHeight();Bounds->ClearMinDesiredHeight();
        if(const auto Slate=Copy->GetCachedWidget()) {Slate->MarkPrepassAsDirty();Slate->SlatePrepass(Scale);}
        const float Width=FMath::Max(1.f,(Card.Width?Card.Width->GetWidthOverride():MinimumCardWidth)-60.f);
        float FallbackHeight=0;
        for(const UTextBlock* Label:{Card.Name.Get(),Card.Description.Get(),Card.Stats.Get(),Card.Metadata.Get()}) if(Label && Label->GetParent() && Label->GetParent()->IsVisible() && !Label->GetText().IsEmpty())
            FallbackHeight+=MCUnifiedRewardPrivate_MeasureText(Label->GetText(),Label->GetFont(),Width).Height+12.f;
        NaturalHeight=FMath::Max(NaturalHeight,FMath::Max(float(Copy->GetDesiredSize().Y),FallbackHeight));
    }
    for(int32 Index=0;Index<Cards.Num();++Index) {
        auto* Bounds=WidgetTree->FindWidget<USizeBox>(MCUnifiedRewardPrivate_Part(TEXT("CopyBounds"),Index));
        if(!Bounds) continue;
        const auto* Surface=Cards[Index].Surface.Get();
        if(OrdinaryOffers && Surface) {
            // Keep the PSD card boundary fixed. Longer copy can use space from
            // the illustration, without changing the type size or adding scroll.
            const float BodyHeight=RewardCardSurfaceSize.Y-Surface->ContentPadding.Top-Surface->ContentPadding.Bottom;
            const float ArtworkHeight=FMath::Clamp(BodyHeight-RewardArtworkGap-FMath::CeilToFloat(NaturalHeight),0.f,float(RewardArtworkSize.Y));
            if(auto* Artwork=WidgetTree->FindWidget<USizeBox>(MCUnifiedRewardPrivate_Part(TEXT("IconBounds"),Index))) {
                Artwork->SetWidthOverride(ArtworkHeight*RewardArtworkSize.X/RewardArtworkSize.Y);
                Artwork->SetHeightOverride(ArtworkHeight);
                Artwork->SetVisibility(ArtworkHeight>0 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
            }
            Bounds->SetHeightOverride(BodyHeight-(ArtworkHeight>0 ? ArtworkHeight+RewardArtworkGap : 0.f));
        } else {
            if(auto* Artwork=WidgetTree->FindWidget<USizeBox>(MCUnifiedRewardPrivate_Part(TEXT("IconBounds"),Index)))
                Artwork->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
            Bounds->SetHeightOverride(FMath::CeilToFloat(NaturalHeight));
        }
    }
}
void UMCUnifiedRewardWidget::UpdateResponsiveLayout(FVector2D AvailableSize)
{
    if(!WidgetTree) return;
    auto* RootBounds=WidgetTree->FindWidget<USizeBox>(TEXT("RewardV2Size"));
    auto* Frame=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("RewardV2Frame"));
    auto* Column=WidgetTree->FindWidget<UVerticalBox>(TEXT("RewardV2Column"));
    auto* CardRow=OrdinaryCardRow.Get();
    auto* Header=WidgetTree->FindWidget<UHorizontalBox>(TEXT("RewardV2HeaderRow"));
    if(!RootBounds || !Frame || !Column || !Header) return;
    if(!CardRow) CardRow=WidgetTree->FindWidget<UWrapBox>(TEXT("RewardV2Cards"));
    if(!CardRow) CardRow=FindObject<UWrapBox>(WidgetTree,TEXT("RewardV2Cards"));
    if(!CardRow) {
        // A replacement-only tree saved by a preview can omit the detached
        // ordinary container. Recreate just that empty container after proving
        // the known active hierarchy and all four existing card identities.
        auto* ExistingViewport=WidgetTree->FindWidget<USizeBox>(TEXT("RewardV2CardsViewport"));
        auto* ExistingGrid=WidgetTree->FindWidget<UUniformGridPanel>(TEXT("RewardV2ReplacementGrid"));
        auto* ExistingScroll=WidgetTree->FindWidget<UMCUIGridScrollBox>(TEXT("RewardV2CardsScroll"));
        if(!ExistingViewport || !ExistingGrid || !ExistingScroll || ExistingViewport->GetParent()!=Column || ExistingGrid->GetParent()!=ExistingScroll
            || ExistingViewport->GetContent()!=ExistingScroll || ExistingScroll->GetChildrenCount()!=1 || ExistingScroll->GetChildAt(0)!=ExistingGrid || Cards.Num()!=4) return;
        if(FindObject<UWidget>(WidgetTree,TEXT("RewardV2Cards"))) return; // Preserve an incompatible authored object.
        for(int32 Index=0;Index<Cards.Num();++Index)
            if(!Cards[Index].Host || Cards[Index].Host->GetFName()!=MCUnifiedRewardPrivate_Part(TEXT("CardBounds"),Index) || Cards[Index].Host->GetParent()!=ExistingGrid) return;
        CardRow=WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(),TEXT("RewardV2Cards"));
        CardRow->SetExplicitWrapSize(true);CardRow->SetWrapSize(1180);CardRow->SetInnerSlotPadding({18,16});
        ReplacementCardGrid=ExistingGrid;ReplacementGridScroll=ExistingScroll;
    }
    OrdinaryCardRow=CardRow;
    // Upgrade the saved tree in place: keep its named controls and action bindings,
    // while removing the former outer and per-card scroll boxes from presentation.
    auto* CardViewport=WidgetTree->FindWidget<USizeBox>(TEXT("RewardV2CardsViewport"));
    if(!CardViewport) {
        UWidget* ExistingBranch=CardRow;
        if(CardRow->GetParent()!=Column) {
            auto* LegacyScroll=Cast<UScrollBox>(CardRow->GetParent());
            if(!LegacyScroll || LegacyScroll->GetChildrenCount()!=1 || LegacyScroll->GetChildAt(0)!=CardRow) return;
            ExistingBranch=LegacyScroll;
            if(LegacyScroll->GetParent()!=Column) {
                auto* LegacyBounds=Cast<USizeBox>(LegacyScroll->GetParent());
                if(!LegacyBounds || LegacyBounds->GetContent()!=LegacyScroll || LegacyBounds->GetParent()!=Column) return;
                ExistingBranch=LegacyBounds;
            }
        }
        const auto Existing=Column->GetAllChildren();
        struct FCell {FMargin Padding;FSlateChildSize Size;EHorizontalAlignment H=HAlign_Fill;EVerticalAlignment V=VAlign_Fill;};
        TArray<FCell> Cells;
        for(UWidget* Child:Existing) {FCell Saved;if(auto* Cell=Cast<UVerticalBoxSlot>(Child->Slot)) {Saved.Padding=Cell->GetPadding();Saved.Size=Cell->GetSize();Saved.H=Cell->GetHorizontalAlignment();Saved.V=Cell->GetVerticalAlignment();}Cells.Add(Saved);}
        CardViewport=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("RewardV2CardsViewport"));
        CardRow->RemoveFromParent();Column->ClearChildren();CardViewport->SetContent(CardRow);
        for(int32 I=0;I<Existing.Num();++I) {
            auto* Cell=Column->AddChildToVerticalBox(Existing[I]==ExistingBranch?static_cast<UWidget*>(CardViewport):Existing[I]);
            Cell->SetPadding(Cells[I].Padding);Cell->SetSize(Cells[I].Size);Cell->SetHorizontalAlignment(Cells[I].H);Cell->SetVerticalAlignment(Cells[I].V);
        }
    }
    if(!bShowingReplacement && CardViewport->GetContent()!=CardRow) {
        // Saved-tree migration has no arranged viewport yet. Persist the
        // ordinary PSD composition without retaining the former outer scroll.
        // Restore hosts before detaching the active replacement tree so the
        // next source refresh still finds their named controls with zero geometry.
        for(auto& Card:Cards) if(auto* Host=Card.Host.Get();Host && Host->GetParent()!=CardRow) {
            Host->RemoveFromParent();CardRow->AddChildToWrapBox(Host)->SetVerticalAlignment(VAlign_Top);
        }
        CardRow->RemoveFromParent();CardViewport->SetContent(CardRow);
    }
    if(auto* Cell=Cast<UVerticalBoxSlot>(CardViewport->Slot)) {Cell->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));Cell->SetHorizontalAlignment(HAlign_Center);Cell->SetPadding(FMargin(30,0,30,22));}
    if(Frame->ContentPadding!=FMargin(24,12,24,20) || Frame->CornerRadius!=52 || !Frame->ShadowOffset.IsNearlyZero()) {
        Frame->ContentPadding=FMargin(24,12,24,20);Frame->CornerRadius=52;Frame->ShadowOffset={0,0};Frame->ApplyTheme();
    }
    if(auto* Cell=Cast<USizeBoxSlot>(Frame->Slot)) {Cell->SetHorizontalAlignment(HAlign_Fill);Cell->SetVerticalAlignment(VAlign_Fill);}
    auto* TimerBounds=WidgetTree->FindWidget<USizeBox>(TEXT("RewardV2CountdownBounds"));
    if(TimerBounds && TimerBounds->GetParent()!=Header) {
        TimerBounds->RemoveFromParent();auto* Cell=Header->AddChildToHorizontalBox(TimerBounds);Cell->SetVerticalAlignment(VAlign_Top);Cell->SetPadding(FMargin(14,0,0,0));
    }
    auto* Spacer=WidgetTree->FindWidget<USizeBox>(TEXT("RewardV2HeaderSpacer"));
    if(!Spacer) {
        // Rebuild this one row once. InsertChildAt alone does not rebuild a live
        // Slate box, so it can leave the timer and title in their former order.
        const auto Children=Header->GetAllChildren();TArray<FMargin> Insets;TArray<FSlateChildSize> Sizing;
        TArray<EHorizontalAlignment> Horizontal;TArray<EVerticalAlignment> Vertical;
        for(UWidget* Child:Children) {auto* Cell=CastChecked<UHorizontalBoxSlot>(Child->Slot);Insets.Add(Cell->GetPadding());Sizing.Add(Cell->GetSize());Horizontal.Add(Cell->GetHorizontalAlignment());Vertical.Add(Cell->GetVerticalAlignment());}
        Header->ClearChildren();Spacer=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("RewardV2HeaderSpacer"));Header->AddChildToHorizontalBox(Spacer);
        for(int32 I=0;I<Children.Num();++I) {auto* Cell=Header->AddChildToHorizontalBox(Children[I]);Cell->SetPadding(Insets[I]);Cell->SetSize(Sizing[I]);Cell->SetHorizontalAlignment(Horizontal[I]);Cell->SetVerticalAlignment(Vertical[I]);}
    }
    const bool OrdinaryOffers=UsesOrdinaryOfferLayout(Cards,bShowingReplacement,bShowingContract);
    if(auto* Plate=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("RewardV2TitlePlate"))) {
        const int32 TitleSize=OrdinaryOffers ? RewardTitleFontSize : 64;
        if(Plate->FontSize!=TitleSize || Plate->CornerRadius!=20 || Plate->ContentPadding!=FMargin(20,4) || !Plate->ShadowOffset.IsNearlyZero()) {
            Plate->FontSize=TitleSize;Plate->CornerRadius=20;Plate->ContentPadding=FMargin(20,4);Plate->ShadowOffset={0,0};Plate->ApplyTheme();
        }
        if(RewardTitleFont.HasValidFont() && Plate->WidgetTree)
            if(auto* Label=Plate->WidgetTree->FindWidget<UTextBlock>(TEXT("UniversalLabel"))) {
                auto Font=RewardTitleFont;Font.Size=Plate->FontSize;Label->SetFont(Font);
            }
    }
    if(auto* Timer=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("RewardV2Countdown"))) {
        if(Timer->CornerRadius!=20 || Timer->ContentPadding!=FMargin(10,6) || !Timer->ShadowOffset.IsNearlyZero()) {
            Timer->CornerRadius=20;Timer->ContentPadding=FMargin(10,6);Timer->ShadowOffset={0,0};Timer->ApplyTheme();
        }
        if(!WidgetTree->FindWidget(TEXT("RewardV2CountdownCopy"))) {
            auto* Copy=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("RewardV2CountdownCopy"));Timer->SetContentForSlot(TEXT("Content"),Copy);
            auto* PrefixLabel=MakeText(TEXT("RewardV2CountdownPrefix"),20,MCUnifiedRewardPrivate_Red);
            auto* ValueLabel=MakeText(TEXT("RewardV2CountdownValue"),38,MCUnifiedRewardPrivate_Red);
            Copy->AddChildToVerticalBox(PrefixLabel);Copy->AddChildToVerticalBox(ValueLabel);
            FString Prefix=Timer->Label.ToString(),Value;int32 Number=INDEX_NONE;
            for(int32 C=0;C<Prefix.Len();++C) if(FChar::IsDigit(Prefix[C])) {Number=C;break;}
            if(Number!=INDEX_NONE) {Value=Prefix.Mid(Number).TrimStartAndEnd();Prefix=Prefix.Left(Number).TrimStartAndEnd();}
            PrefixLabel->SetText(FText::FromString(Prefix));ValueLabel->SetText(FText::FromString(Value));
            ValueLabel->SetVisibility(Value.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
        }
        for(const TCHAR* Name:{TEXT("RewardV2CountdownPrefix"),TEXT("RewardV2CountdownValue")}) if(auto* Label=WidgetTree->FindWidget<UTextBlock>(Name)) Label->SetColorAndOpacity(MCUnifiedRewardPrivate_Red);
    }
    for(int32 I=0;I<Cards.Num();++I) {
        auto& Card=Cards[I];
        auto* CopyBounds=WidgetTree->FindWidget<USizeBox>(MCUnifiedRewardPrivate_Part(TEXT("CopyBounds"),I));
        auto* Copy=WidgetTree->FindWidget<UVerticalBox>(MCUnifiedRewardPrivate_Part(TEXT("CopyColumn"),I));
        if(CopyBounds && Copy && CopyBounds->GetContent()!=Copy) {Copy->RemoveFromParent();CopyBounds->SetContent(Copy);}
        for(UTextBlock* Extra:{Card.Stats.Get(),Card.Metadata.Get()}) if(Extra && Extra->GetParent()) {
            const bool ContextCopy=Extra==Card.Stats?bShowingReplacement || bShowingContract:bShowingContract;
            Extra->GetParent()->SetVisibility(ContextCopy && !Extra->GetText().IsEmpty()?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
        }
        for(UMCUniversalUIWidget* Badge:{Card.Type.Get(),Card.Rarity.Get()}) if(Badge && (Badge->CornerRadius!=0 || !Badge->ShadowOffset.IsNearlyZero() || Badge->OutlineWidth!=3)) {Badge->CornerRadius=0;Badge->ShadowOffset={0,0};Badge->OutlineWidth=3;Badge->ApplyTheme();}
        if(Card.Surface) {
            if(Card.Surface->CustomShadow!=MCUnifiedRewardPrivate_Dark || Card.Surface->ShadowOffset!=FVector2D(16,22) || Card.Surface->CornerRadius!=16) {Card.Surface->CustomShadow=MCUnifiedRewardPrivate_Dark;Card.Surface->ShadowOffset={16,22};Card.Surface->CornerRadius=16;Card.Surface->ApplyTheme();}
            if(auto* Cell=Cast<UVerticalBoxSlot>(Card.Surface->Slot)) Cell->SetPadding(FMargin(0,0,16,26));
        }
        if(auto* IconBounds=WidgetTree->FindWidget<USizeBox>(MCUnifiedRewardPrivate_Part(TEXT("IconBounds"),I))) if(auto* Cell=Cast<UVerticalBoxSlot>(IconBounds->Slot)) {Cell->SetHorizontalAlignment(HAlign_Center);Cell->SetPadding(FMargin(0,0,0,10));}
    }
    if(AvailableSize.X<=1 || AvailableSize.Y<=1) AvailableSize=GetPaintSpaceGeometry().GetLocalSize();
    if(AvailableSize.X<=1 || AvailableSize.Y<=1) return;
    ResponsiveViewportSize=AvailableSize;
    int32 Count=0;for(const auto& Card:Cards) if(Card.Host && Card.Host->GetVisibility()!=ESlateVisibility::Collapsed) ++Count;
    const float MaxWidth=FMath::Max(1.f,float(AvailableSize.X)-14.f),MaxHeight=FMath::Max(1.f,float(AvailableSize.Y)-16.f);
    auto* ModalScale=WidgetTree->FindWidget<UScaleBox>(TEXT("RewardV2ModalScale"));
    if(ModalScale) {
        const float Scale=OrdinaryOffers ? FMath::Min(AvailableSize.X/RewardDesignViewport.X,AvailableSize.Y/RewardDesignViewport.Y) : 1.f;
        ModalScale->SetUserSpecifiedScale(Scale);
    }
    const float Chrome=Frame->ContentPadding.Left+Frame->ContentPadding.Right+60.f;
    // Three ordinary offers always retain the PSD row. Four existing skill
    // slots are a different interaction: give their text two usable columns
    // when four natural-width cards would not fit the viewport.
    const bool ReplacementGrid=bShowingReplacement && Count>3 && MaxWidth<Count*FMath::Max(MinimumCardWidth,PreferredCardWidth)+(Count-1)*18.f+Chrome;
    const int32 Columns=ReplacementGrid?2:FMath::Max(1,Count);
    float ViewWidth=OrdinaryOffers ? RewardDesignSize.X : FMath::Min(MaxWidth,Columns*FMath::Max(310.f,PreferredCardWidth)+(Columns-1)*18.f+Chrome);
    if(ReplacementGrid) ViewWidth=FMath::Min(MaxWidth,FMath::Max(ViewWidth,3.f*MinimumCardWidth+36.f+Chrome));
    auto* ListScroll=ReplacementGridScroll.Get();
    auto* Grid=ReplacementCardGrid.Get();
    if(ReplacementGrid) {
        if(!ListScroll) {
            auto* Previous=FindObject<UScrollBox>(WidgetTree,TEXT("RewardV2CardsScroll"));
            ListScroll=Cast<UMCUIGridScrollBox>(Previous);
            if(Previous && !ListScroll) {
                // A pre-snap saved/transient tree can retain this former list
                // object even after ordinary presentation detached it.
                CardRow->RemoveFromParent();Previous->RemoveFromParent();
                const FName Retired=MakeUniqueObjectName(WidgetTree,Previous->GetClass(),TEXT("RewardV2LegacyCardsScroll"));
                Previous->Rename(*Retired.ToString(),WidgetTree,REN_DontCreateRedirectors|REN_NonTransactional);
            }
            if(!ListScroll) ListScroll=WidgetTree->ConstructWidget<UMCUIGridScrollBox>(UMCUIGridScrollBox::StaticClass(),TEXT("RewardV2CardsScroll"));
            ListScroll->SetScrollbarThickness({5,5});ListScroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
            ReplacementGridScroll=ListScroll;
        }
        if(!Grid) {
            Grid=WidgetTree->FindWidget<UUniformGridPanel>(TEXT("RewardV2ReplacementGrid"));
            if(!Grid) Grid=WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(),TEXT("RewardV2ReplacementGrid"));
            Grid->SetSlotPadding(FMargin(0,0,18,16));ReplacementCardGrid=Grid;
        }
        if(Grid->GetParent()!=ListScroll) {Grid->RemoveFromParent();ListScroll->AddChild(Grid);}
        for(int32 Index=0;Index<Cards.Num();++Index) if(auto* Host=Cards[Index].Host.Get();Host && Host->GetParent()!=Grid) {
            Host->RemoveFromParent();auto* Cell=Grid->AddChildToUniformGrid(Host,Index/Columns,Index%Columns);
            Cell->SetHorizontalAlignment(HAlign_Fill);Cell->SetVerticalAlignment(VAlign_Top);
        }
        CardRow->RemoveFromParent();
        if(CardViewport->GetContent()!=ListScroll) {ListScroll->RemoveFromParent();CardViewport->SetContent(ListScroll);}
    } else {
        for(auto& Card:Cards) if(auto* Host=Card.Host.Get();Host && Host->GetParent()!=CardRow) {
            Host->RemoveFromParent();CardRow->AddChildToWrapBox(Host)->SetVerticalAlignment(VAlign_Top);
        }
        if(CardViewport->GetContent()!=CardRow) {CardRow->RemoveFromParent();CardViewport->SetContent(CardRow);}
    }
    CardViewport->ClearHeightOverride();CardViewport->ClearMaxDesiredHeight();CardViewport->ClearMinDesiredHeight();
    const float Scale=FMath::Max(.01f,GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale());
    auto Measure=[Scale](UWidget* Widget) {if(const auto Slate=Widget?Widget->GetCachedWidget():nullptr) {Slate->MarkPrepassAsDirty();Slate->SlatePrepass(Scale);}return Widget?Widget->GetDesiredSize():FVector2D::ZeroVector;};
    auto Layout=[&](float Width,float IconLimit) {
        const float Inner=FMath::Max(1.f,Width-Frame->ContentPadding.Left-Frame->ContentPadding.Right);
        const float CardWidthWithShadow=RewardCardSurfaceSize.X+RewardCardShadowRight;
        const float RowWidth=OrdinaryOffers ? Columns*CardWidthWithShadow+(Columns-1)*RewardCardGap : FMath::Max(1.f,Inner-60.f);
        const float ContentWidth=FMath::Max(1.f,RowWidth-(ReplacementGrid?12.f:0.f));
        const float CardWidth=OrdinaryOffers ? CardWidthWithShadow : FMath::Max(1.f,(ContentWidth-(ReplacementGrid?Columns:Columns-1)*18.f)/Columns);
        RootBounds->ClearMaxDesiredHeight();RootBounds->ClearHeightOverride();RootBounds->ClearMinDesiredHeight();RootBounds->ClearMinDesiredWidth();RootBounds->SetWidthOverride(Width);RootBounds->SetMaxDesiredWidth(Width);
        CardViewport->SetWidthOverride(RowWidth);
        if(!ReplacementGrid) {
            CardRow->SetInnerSlotPadding({OrdinaryOffers ? RewardCardGap : 18.f,16.f});
            CardRow->SetWrapSize(ContentWidth+2.f);CardRow->SetHorizontalAlignment(HAlign_Center);
        }
        else if(Grid) {Grid->SetMinDesiredSlotWidth(0);Grid->SetMinDesiredSlotHeight(0);}
        const float IconSize=FMath::Max(1.f,FMath::Min3(CardWidth-44.f,IconLimit,ReplacementGrid?160.f:TNumericLimits<float>::Max()));
        for(int32 I=0;I<Cards.Num();++I) {
            auto& Card=Cards[I];if(Card.Width) Card.Width->SetWidthOverride(CardWidth);
            if(Card.Description) {
                auto Font=Card.Description->GetFont();
                const int32 Size=OrdinaryOffers ? RewardDescriptionFontSize : 18;
                if(Font.Size!=Size) {Font.Size=Size;Card.Description->SetFont(Font);}
            }
            for(UTextBlock* Label:{Card.Name.Get(),Card.Description.Get(),Card.Stats.Get(),Card.Metadata.Get()}) if(Label) {Label->SetAutoWrapText(false);Label->SetWrapTextAt(FMath::Max(1.f,CardWidth-60.f));Label->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);if(auto* Bounds=Cast<USizeBox>(Label->GetParent())) Bounds->SetMaxDesiredWidth(FMath::Max(1.f,CardWidth-60.f));}
            if(!OrdinaryOffers)
                if(auto* IconBounds=WidgetTree->FindWidget<USizeBox>(MCUnifiedRewardPrivate_Part(TEXT("IconBounds"),I))) {IconBounds->SetWidthOverride(IconSize);IconBounds->SetHeightOverride(IconSize);}
            // Saved low-resolution perk art must fill the existing PSD square.
            // Keep the card/row height budget; only normalize the image's
            // intrinsic size and allow its scale box to grow canonical artwork.
            if(auto* Fit=WidgetTree->FindWidget<UScaleBox>(MCUnifiedRewardPrivate_Part(TEXT("IconFit"),I))) {
                if(Fit->GetStretch()!=EStretch::ScaleToFit) Fit->SetStretch(EStretch::ScaleToFit);
                if(Fit->GetStretchDirection()!=EStretchDirection::Both) Fit->SetStretchDirection(EStretchDirection::Both);
            }
            if(Card.Icon) {
                const auto* Panel=WidgetTree->FindWidget<UMCUniversalUIWidget>(MCUnifiedRewardPrivate_Part(TEXT("IconPanel"),I));
                const float Inset=Panel?FMath::Max(Panel->ContentPadding.Left+Panel->ContentPadding.Right,Panel->ContentPadding.Top+Panel->ContentPadding.Bottom):24.f;
                Card.Icon->SetDesiredSizeOverride(FVector2D(FMath::Max(1.f,IconSize-Inset)));
            }
        }
        const float TimerWidth=FMath::Min(Inner*.23f,310.f);
        if(TimerBounds) {TimerBounds->SetWidthOverride(TimerWidth);TimerBounds->SetMinDesiredWidth(TimerWidth);TimerBounds->SetMaxDesiredWidth(TimerWidth);}
        const bool TimerVisible=TimerBounds && TimerBounds->GetVisibility()!=ESlateVisibility::Collapsed;
        const float TimerExtent=TimerVisible?TimerWidth+14.f:0.f;
        float NaturalTitleWidth=0.f;
        if(const auto* Plate=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("RewardV2TitlePlate"));Plate && Plate->WidgetTree)
            if(const auto* Label=Plate->WidgetTree->FindWidget<UTextBlock>(TEXT("UniversalLabel"));Label && FSlateApplication::IsInitialized())
                if(const auto Renderer=FSlateApplication::Get().GetRenderer())
                    NaturalTitleWidth=float(Renderer->GetFontMeasureService()->Measure(Label->GetText(),Label->GetFont()).X)+Plate->ContentPadding.Left+Plate->ContentPadding.Right+8.f;
        // The PSD's centered plate is the baseline, not a text-width ceiling.
        // A longer translated word first consumes the empty left spacer; the
        // fixed-size title stays centered whenever the right timer permits it.
        const float PlateWidth=FMath::Max(1.f,FMath::Min(FMath::Max(Inner*.51f,NaturalTitleWidth),Inner-TimerExtent));
        if(Spacer) Spacer->SetWidthOverride(FMath::Max(0.f,FMath::Min(TimerExtent,Inner-PlateWidth-TimerExtent)));
        if(auto* PlateBounds=WidgetTree->FindWidget<USizeBox>(TEXT("RewardV2TitleBounds"))) {PlateBounds->SetWidthOverride(PlateWidth);PlateBounds->SetMinDesiredWidth(PlateWidth);PlateBounds->SetMaxDesiredWidth(PlateWidth);}
        for(const TCHAR* Name:{TEXT("RewardV2Subtitle"),TEXT("RewardV2SourceTitle"),TEXT("RewardV2Hint")}) if(auto* Label=WidgetTree->FindWidget<UTextBlock>(Name)) {Label->SetAutoWrapText(false);Label->SetWrapTextAt(FMath::Max(1.f,Inner-24.f));Label->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);}
        for(const TCHAR* Name:{TEXT("RewardV2CountdownPrefix"),TEXT("RewardV2CountdownValue")}) if(auto* Label=WidgetTree->FindWidget<UTextBlock>(Name)) {Label->SetAutoWrapText(false);Label->SetWrapTextAt(FMath::Max(1.f,TimerWidth-24.f));Label->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);}
        if(auto* Actions=WidgetTree->FindWidget<UWrapBox>(TEXT("RewardV2Actions"))) Actions->SetWrapSize(Inner);
        for(int32 I=0;I<4;++I) if(auto* Bounds=WidgetTree->FindWidget<USizeBox>(MCUnifiedRewardPrivate_Part(TEXT("ActionBounds"),I))) Bounds->SetMaxDesiredWidth(FMath::Min(520.f,Inner));
        RefreshTextBounds();UpdateCardCopyHeight();
        if(ReplacementGrid) CardViewport->SetHeightOverride(Measure(Grid).Y);
        return Measure(Frame).Y;
    };
    float Height=Layout(ViewWidth,TNumericLimits<float>::Max());
    if(OrdinaryOffers) {
        RootBounds->SetHeightOverride(RewardDesignSize.Y);
        RootBounds->SetMaxDesiredHeight(RewardDesignSize.Y);
        return;
    }
    if(ReplacementGrid) {
        // Each stopped viewport contains whole uniform rows, including badges
        // and shadows. Text retains its natural height and never scrolls alone.
        const int32 TotalRows=FMath::Max(1,FMath::DivideAndRoundUp(Count,Columns));
        float ListHeight=Measure(Grid).Y;
        float Reserved=FMath::Max(0.f,Height-ListHeight);
        float RowStride=FMath::Max(1.f,ListHeight/TotalRows);
        if(RowStride>MaxHeight-Reserved) {
            Height=Layout(ViewWidth,64.f);ListHeight=Measure(Grid).Y;
            Reserved=FMath::Max(0.f,Height-ListHeight);RowStride=FMath::Max(1.f,ListHeight/TotalRows);
        }
        if(RowStride>MaxHeight-Reserved && ViewWidth<MaxWidth) {
            ViewWidth=MaxWidth;Height=Layout(ViewWidth,64.f);ListHeight=Measure(Grid).Y;
            Reserved=FMath::Max(0.f,Height-ListHeight);RowStride=FMath::Max(1.f,ListHeight/TotalRows);
        }
        const int32 VisibleRows=FMath::Clamp(FMath::FloorToInt(FMath::Max(1.f,MaxHeight-Reserved)/RowStride),1,TotalRows);
        const float VisibleListHeight=VisibleRows*RowStride;
        CardViewport->SetHeightOverride(VisibleListHeight);CardViewport->SetMaxDesiredHeight(VisibleListHeight);
        ListScroll->ConfigureGrid(Grid,Columns,VisibleRows,RowStride);
        Height=Measure(Frame).Y;
        RootBounds->SetHeightOverride(FMath::CeilToFloat(Height));RootBounds->SetMaxDesiredHeight(FMath::CeilToFloat(Height));return;
    }
    if(Height>MaxHeight && ViewWidth<MaxWidth) {ViewWidth=MaxWidth;Height=Layout(ViewWidth,TNumericLimits<float>::Max());}
    if(Height>MaxHeight && Count>0) {
        const float IconWidth=FMath::Max(64.f,(ViewWidth-Chrome-(Columns-1)*18.f)/Columns-44.f-float(Height-MaxHeight));
        Height=Layout(ViewWidth,IconWidth);
    }
    RootBounds->SetHeightOverride(FMath::CeilToFloat(Height));RootBounds->SetMaxDesiredHeight(FMath::CeilToFloat(Height));
}
void UMCUnifiedRewardWidget::RefreshTextBounds()
{
    if(!WidgetTree) return;
    auto Caption=[](UMCUniversalUIWidget* Shape,float Width) {
        if(Shape && Shape->WidgetTree) if(auto* Label=Shape->WidgetTree->FindWidget<UTextBlock>(TEXT("UniversalLabel"))) {
            Label->SetJustification(ETextJustify::Center);Label->SetAutoWrapText(false);Label->SetWrapTextAt(FMath::Max(1.f,Width));Label->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
        }
    };
    const auto* PanelBounds=WidgetTree->FindWidget<USizeBox>(TEXT("RewardV2Size"));
    const float InnerWidth=PanelBounds && PanelBounds->GetWidthOverride()>1?FMath::Max(1.f,PanelBounds->GetWidthOverride()-64.f):1180.f;
    const auto* TitleBounds=WidgetTree->FindWidget<USizeBox>(TEXT("RewardV2TitleBounds"));
    Caption(WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("RewardV2TitlePlate")),TitleBounds?FMath::Max(1.f,TitleBounds->GetWidthOverride()-40.f):InnerWidth*.5f);
    Caption(WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("RewardV2Countdown")),FMath::Min(230.f,InnerWidth*.4f-32.f));
    for(const auto& Card:Cards) {
        const float Width=FMath::Max(1.f,(Card.Width?Card.Width->GetWidthOverride():MinimumCardWidth)-48.f);
        Caption(Card.Type,Width);Caption(Card.Rarity,Width);
    }
    for(UMCUniversalUIWidget* Action:ActionViews) Caption(Action,FMath::Min(450.f,InnerWidth-52.f));
}
void UMCUnifiedRewardWidget::UpdateHeaderContour()
{
    RefreshTextBounds();
    if(!bAdaptHeaderContour || !WidgetTree) return;
    auto* Frame=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("RewardV2Frame"));auto* Plate=WidgetTree->FindWidget(TEXT("RewardV2TitleBounds"));auto* Header=WidgetTree->FindWidget(TEXT("RewardV2HeaderRow"));
    if(!Frame || !Plate || !Header) return;
    const auto& Geometry=Frame->GetPaintSpaceGeometry();const FVector2D Size=Geometry.GetLocalSize();if(Size.X<1 || Size.Y<1) return;
    const auto& PlateGeometry=Plate->GetPaintSpaceGeometry();const auto& HeaderGeometry=Header->GetPaintSpaceGeometry();
    const FVector2D Left=Geometry.AbsoluteToLocal(PlateGeometry.LocalToAbsolute(FVector2D::ZeroVector));
    const FVector2D Right=Geometry.AbsoluteToLocal(PlateGeometry.LocalToAbsolute(PlateGeometry.GetLocalSize()));
    const FVector2D HeaderBottom=Geometry.AbsoluteToLocal(HeaderGeometry.LocalToAbsolute(FVector2D(0,HeaderGeometry.GetLocalSize().Y)));
    const float StepLeft=FMath::Clamp(float((Left.X-20)/Size.X),.015f,.48f);
    const float StepRight=FMath::Clamp(float((Right.X+20)/Size.X),.52f,.985f);
    const float StepY=FMath::Clamp(float((HeaderBottom.Y+4)/Size.Y),.04f,.4f);
    const TArray<FVector2D> Points={{0,StepY},{StepLeft,StepY},{StepLeft,0},{StepRight,0},{StepRight,StepY},{1,StepY},{1,1},{0,1}};
    if(Frame->ContourPoints!=Points) {Frame->ContourPoints=Points;Frame->ApplyTheme();RefreshTextBounds();}
}
void UMCUnifiedRewardWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);UpdateResponsiveLayout(Geometry.GetLocalSize());RefreshElapsed+=DeltaSeconds;
    if(RefreshElapsed>=.1f) {RefreshElapsed=0;RefreshFromBackend();UpdateHeaderContour();}
}
FReply UMCUnifiedRewardWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    return RouteSourceKey(Event)?FReply::Handled():Super::NativeOnPreviewKeyDown(Geometry,Event);
}
FReply UMCUnifiedRewardWidget::NativeOnAnalogValueChanged(const FGeometry& Geometry,const FAnalogInputEvent& Event)
{
    return RouteSourceAnalog(Event)?FReply::Handled():Super::NativeOnAnalogValueChanged(Geometry,Event);
}
void UMCUnifiedRewardWidget::OnLocalizationChanged()
{
    if(Backend.IsValid() && !IsDesignTime()) RefreshFromBackend();else if(IsDesignTime()) PresentPreview();
}

#undef LOCTEXT_NAMESPACE
