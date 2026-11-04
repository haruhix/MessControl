#include "MCUnifiedRewardWidget.h"

#include "MCGameplayUIFactory.h"
#include "MCPerkComponent.h"
#include "MCUIDataLibrary.h"
#include "MCUniversalUIWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
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
    FLinearColor MCUnifiedRewardPrivate_Color(const TCHAR* Hex) { return FLinearColor(FColor::FromHex(Hex)); }
    const FLinearColor MCUnifiedRewardPrivate_Blue=MCUnifiedRewardPrivate_Color(TEXT("4C76AE")),MCUnifiedRewardPrivate_Paper=MCUnifiedRewardPrivate_Color(TEXT("FFFDFD")),MCUnifiedRewardPrivate_Ink=MCUnifiedRewardPrivate_Color(TEXT("472630"));
    const FLinearColor MCUnifiedRewardPrivate_Dark=MCUnifiedRewardPrivate_Color(TEXT("404243")),MCUnifiedRewardPrivate_Pink=MCUnifiedRewardPrivate_Color(TEXT("AE4C6C")),MCUnifiedRewardPrivate_Red=MCUnifiedRewardPrivate_Color(TEXT("B21829"));
    const FLinearColor MCUnifiedRewardPrivate_Purple=MCUnifiedRewardPrivate_Color(TEXT("9A40EB")),MCUnifiedRewardPrivate_Yellow=MCUnifiedRewardPrivate_Color(TEXT("FFDD00"));
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
        // Inactive contract/pending cards keep their child scroll regions usable.
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
    PreviewPerkIDs={TEXT("P01"),TEXT("P02"),TEXT("P03")};
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
    Text->SetAutoWrapText(true);Text->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
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
    if(WidgetTree->RootWidget) {ResolvePresentationTree();return;}
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
        auto* IconSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),MCUnifiedRewardPrivate_Part(TEXT("IconBounds"),I));IconSize->SetHeightOverride(126);IconSize->SetContent(IconPanel);Body->AddChildToVerticalBox(IconSize)->SetPadding(FMargin(4,0,4,10));
        auto* Fit=WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),MCUnifiedRewardPrivate_Part(TEXT("IconFit"),I));Fit->SetStretch(EStretch::ScaleToFit);IconPanel->SetContentForSlot(TEXT("Content"),Fit);
        auto* Icon=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),MCUnifiedRewardPrivate_Part(TEXT("Icon"),I));Icon->SetVisibility(ESlateVisibility::Hidden);Fit->SetContent(Icon);
        auto* ScrollSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),MCUnifiedRewardPrivate_Part(TEXT("CopyBounds"),I));ScrollSize->SetMaxDesiredHeight(MaximumCardCopyHeight);
        auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),MCUnifiedRewardPrivate_Part(TEXT("CopyScroll"),I));Scroll->SetAllowOverscroll(false);Scroll->SetScrollbarThickness({5,5});ScrollSize->SetContent(Scroll);Body->AddChildToVerticalBox(ScrollSize);
        auto* Copy=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),MCUnifiedRewardPrivate_Part(TEXT("CopyColumn"),I));Scroll->AddChild(Copy);
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
    auto Copy=[this](FName Name,const FText& Text) {if(auto* Label=WidgetTree->FindWidget<UTextBlock>(Name)) Label->SetText(Text);};
    Copy(TEXT("RewardV2SourceTitle"),Snapshot.Title);Copy(TEXT("RewardV2Subtitle"),Snapshot.Subtitle);Copy(TEXT("RewardV2Hint"),Snapshot.Hint);
    if(auto* Timer=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("RewardV2Countdown"))) Timer->SetLabel(NAME_None,Snapshot.Countdown);
    if(auto* Bounds=WidgetTree->FindWidget(TEXT("RewardV2CountdownBounds"))) Bounds->SetVisibility(Snapshot.bCountdownVisible?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    const float CardWidth=AdaptiveCardWidth(Snapshot);
    const float RowWidth=FMath::Min(1516.f,Snapshot.Cards.Num()*CardWidth+FMath::Max(0,Snapshot.Cards.Num()-1)*18.f);
    if(auto* Row=WidgetTree->FindWidget<UWrapBox>(TEXT("RewardV2Cards"))) Row->SetWrapSize(FMath::Max(CardWidth,RowWidth));
    if(auto* Size=WidgetTree->FindWidget<USizeBox>(TEXT("RewardV2Size"))) Size->SetMaxDesiredWidth(FMath::Max(1040.f,RowWidth+64.f));
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
        for(UTextBlock* Label:{Card.Name.Get(),Card.Description.Get(),Card.Stats.Get(),Card.Metadata.Get()}) if(Label) {
            Label->SetWrapTextAt(CardWidth-56);if(auto* Bounds=Cast<USizeBox>(Label->GetParent())) Bounds->SetMaxDesiredWidth(CardWidth-56);
            if(auto* Host=Label->GetParent()) Host->SetVisibility(Label->GetText().IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible);
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
            Card.Rarity->CustomText=Data->Rarity==EMCPerkRarity::Rare?MCUnifiedRewardPrivate_Paper:MCUnifiedRewardPrivate_Ink;Card.Rarity->SetLabel(NAME_None,RarityLabel(Data->Rarity));
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
    UpdateCardCopyHeight();UpdateHeaderContour();RefreshTextBounds();
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
    if(Event.GetKey()==EKeys::Gamepad_RightY) {
        const float Delta=-Event.GetAnalogValue()*18.f;
        if(FMath::Abs(Event.GetAnalogValue())>.15f)
            for(auto& Card:Cards) if(Card.BodyScroll && Card.Host && Card.Host->IsVisible() && (bShowingContract || Card.SourceButton==SelectedSource))
                Card.BodyScroll->SetScrollOffset(Card.BodyScroll->GetScrollOffset()+Delta);
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
    if(Backend.IsValid() && !IsDesignTime()) RefreshFromBackend();else PresentPreview();
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
    UpdateCardCopyHeight();UpdateHeaderContour();
}
float UMCUnifiedRewardWidget::AdaptiveCardWidth(const FMCPerkChoicePresentationSnapshot& Snapshot) const
{
    float Width=FMath::Max(240.f,MinimumCardWidth);if(!bAdaptCardLayout) return Width;
    for(int32 I=0;I<Snapshot.Cards.Num();++I) {
        const auto& Data=Snapshot.Cards[I].Data;
        const FText Texts[]={Data.DisplayName,Data.Description,Data.StackPreview,Data.Badge};
        const UTextBlock* Labels[]={Cards.IsValidIndex(I)?Cards[I].Name.Get():nullptr,Cards.IsValidIndex(I)?Cards[I].Description.Get():nullptr,
            Cards.IsValidIndex(I)?Cards[I].Stats.Get():nullptr,Cards.IsValidIndex(I)?Cards[I].Metadata.Get():nullptr};
        const int32 Fonts[]={22,18,17,15};
        for(int32 L=0;L<4;++L) {
            const auto Font=Labels[L]?Labels[L]->GetFont():GetResolvedTheme()->MakeGameplayFont(Fonts[L]);
            // The glyph width includes fallback fonts. Real Unicode line-break
            // spans retain Cyrillic words without treating CJK paragraphs as one word.
            Width=FMath::Max(Width,MCUnifiedRewardPrivate_MeasureText(Texts[L],Font).Word+72.f);
        }
    }
    return FMath::CeilToFloat(FMath::Min(Width,FMath::Max(MinimumCardWidth,MaximumCardWidth)));
}
void UMCUnifiedRewardWidget::UpdateCardCopyHeight()
{
    if(!bAdaptCardLayout || !WidgetTree) return;
    float NaturalHeight=0;
    for(auto& Card:Cards) if(Card.Host && Card.Host->GetVisibility()!=ESlateVisibility::Collapsed) {
        const float Wrap=FMath::Max(184.f,(Card.Width?Card.Width->GetWidthOverride():MinimumCardWidth)-56.f);
        float Height=0;int32 Lines=0;
        for(UTextBlock* Label:{Card.Name.Get(),Card.Description.Get(),Card.Stats.Get(),Card.Metadata.Get()}) if(Label && !Label->GetText().IsEmpty()) {
            Height+=MCUnifiedRewardPrivate_MeasureText(Label->GetText(),Label->GetFont(),Wrap).Height+(Lines++?12.f:7.f);
        }
        NaturalHeight=FMath::Max(NaturalHeight,Height+12.f);
    }
    // Keep the actions and hint in a normal three-card viewport. The enclosing
    // Root owns whole-screen scrolling; four replacement cards may need it.
    float Maximum=FMath::Clamp(MaximumCardCopyHeight,240.f,360.f);
    int32 VisibleCards=0;float CopyPaintHeight=0;
    for(int32 I=0;I<Cards.Num();++I) if(Cards[I].Host && Cards[I].Host->GetVisibility()!=ESlateVisibility::Collapsed) {
        ++VisibleCards;
        if(auto* Bounds=WidgetTree->FindWidget<USizeBox>(MCUnifiedRewardPrivate_Part(TEXT("CopyBounds"),I)))
            CopyPaintHeight=FMath::Max(CopyPaintHeight,float(Bounds->GetPaintSpaceGeometry().GetLocalSize().Y));
    }
    const UMCGameplayUIWidget* ViewportOwner=this;
    while(const auto* Parent=ViewportOwner->GetTypedOuter<UMCGameplayUIWidget>()) ViewportOwner=Parent;
    const float AvailableHeight=ViewportOwner!=this?float(ViewportOwner->GetPaintSpaceGeometry().GetLocalSize().Y):0.f;
    if(VisibleCards>0 && VisibleCards<=3 && AvailableHeight>100.f) {
        float OtherContent=620.f;
        if(auto* Frame=WidgetTree->FindWidget(TEXT("RewardV2Frame"))) {
            const float FrameHeight=Frame->GetPaintSpaceGeometry().GetLocalSize().Y;
            if(FrameHeight>CopyPaintHeight+100.f && CopyPaintHeight>1.f) OtherContent=FrameHeight-CopyPaintHeight;
        }
        const float TallWindowGrowth=FMath::Max(0.f,AvailableHeight-1080.f);
        const float Budget=AvailableHeight-OtherContent-64.f;
        Maximum=FMath::Max(120.f,FMath::Min(Maximum+TallWindowGrowth,Budget));
    }
    const float CommonHeight=FMath::CeilToFloat(FMath::Min(NaturalHeight,Maximum));
    for(int32 I=0;I<Cards.Num();++I) {
        if(auto* Bounds=WidgetTree->FindWidget<USizeBox>(MCUnifiedRewardPrivate_Part(TEXT("CopyBounds"),I))) {
            Bounds->ClearHeightOverride();Bounds->SetMinDesiredHeight(CommonHeight);Bounds->SetMaxDesiredHeight(Maximum);
        }
        if(Cards[I].BodyScroll) {Cards[I].BodyScroll->SetAlwaysShowScrollbar(false);Cards[I].BodyScroll->SetScrollBarVisibility(ESlateVisibility::Visible);}
    }
}
void UMCUnifiedRewardWidget::RefreshTextBounds()
{
    if(!WidgetTree) return;
    auto Caption=[](UMCUniversalUIWidget* Shape,float Width) {
        if(Shape && Shape->WidgetTree) if(auto* Label=Shape->WidgetTree->FindWidget<UTextBlock>(TEXT("UniversalLabel"))) {
            Label->SetJustification(ETextJustify::Center);Label->SetAutoWrapText(false);Label->SetWrapTextAt(Width);
        }
    };
    Caption(WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("RewardV2TitlePlate")),540.f);
    Caption(WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("RewardV2Countdown")),230.f);
    for(const auto& Card:Cards) {
        const float Width=FMath::Max(184.f,(Card.Width?Card.Width->GetWidthOverride():MinimumCardWidth)-56.f);
        Caption(Card.Type,Width);Caption(Card.Rarity,Width);
    }
    for(UMCUniversalUIWidget* Action:ActionViews) Caption(Action,450.f);
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
    Super::NativeTick(Geometry,DeltaSeconds);RefreshElapsed+=DeltaSeconds;
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
