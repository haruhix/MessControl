#include "MCCosmeticItemWidget.h"
#include "MCUIVisualStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"

#define LOCTEXT_NAMESPACE "MCCosmeticItem"
void UMCCosmeticItemWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(WidgetTree->RootWidget) {Resolve();return;}
    auto* Size=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("CosmeticCardSize"));
    Size->SetMinDesiredWidth(184);Size->SetMinDesiredHeight(180);WidgetTree->RootWidget=Size;
    CosmeticCardSurface=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("CosmeticCardSurface"));
    CosmeticCardSurface->SetPadding(FMargin(8));Size->SetContent(CosmeticCardSurface);
    auto* Content=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("CosmeticCardContent"));CosmeticCardSurface->SetContent(Content);
    CosmeticButton=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("CosmeticButton"));Content->AddChildToVerticalBox(CosmeticButton);
    auto* Main=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("CosmeticSelectionContent"));CosmeticButton->SetContent(Main);
    if(auto* S=Cast<UButtonSlot>(CosmeticButton->GetContentSlot())) {S->SetPadding(FMargin(0));S->SetHorizontalAlignment(HAlign_Fill);S->SetVerticalAlignment(VAlign_Fill);}
    CosmeticThumbnailFrame=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("CosmeticThumbnailFrame"));
    CosmeticThumbnailFrame->SetPadding(FMargin(4));auto* ThumbSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("CosmeticThumbnailSize"));
    ThumbSize->SetHeightOverride(64);ThumbSize->SetContent(CosmeticThumbnailFrame);Main->AddChildToVerticalBox(ThumbSize)->SetPadding(FMargin(0,0,0,4));
    auto* Fit=WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),TEXT("CosmeticThumbnailFit"));Fit->SetStretch(EStretch::ScaleToFit);CosmeticThumbnailFrame->SetContent(Fit);
    CosmeticThumbnail=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("CosmeticThumbnail"));Fit->SetContent(CosmeticThumbnail);
    auto AddText=[this,Main](FName Name,float Height)->UTextBlock* {
        auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);Text->SetAutoWrapText(true);
        auto* Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetMinDesiredHeight(Height);Box->SetContent(Text);
        Main->AddChildToVerticalBox(Box)->SetPadding(FMargin(1,0,1,2));return Text;
    };
    CosmeticName=AddText(TEXT("CosmeticName"),20);
    CosmeticDescription=AddText(TEXT("CosmeticDescription"),30);
    if(auto* DescriptionSize=Cast<USizeBox>(CosmeticDescription->GetParent())) {DescriptionSize->SetHeightOverride(30);DescriptionSize->SetClipping(EWidgetClipping::ClipToBounds);}
    auto* Footer=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("CosmeticMeta"));Main->AddChildToVerticalBox(Footer)->SetPadding(FMargin(1,2,1,4));
    CosmeticRarity=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("CosmeticRarity"));Footer->AddChildToHorizontalBox(CosmeticRarity)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    CosmeticPrice=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("CosmeticPrice"));CosmeticPrice->SetJustification(ETextJustify::Right);Footer->AddChildToHorizontalBox(CosmeticPrice);
    CosmeticState=AddText(TEXT("CosmeticState"),16);
    CosmeticBuy=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("CosmeticBuy"));
    CosmeticBuyLabel=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("CosmeticBuyLabel"));CosmeticBuyLabel->SetJustification(ETextJustify::Center);CosmeticBuy->SetContent(CosmeticBuyLabel);
    auto* BuySize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("CosmeticBuySize"));BuySize->SetHeightOverride(28);BuySize->SetContent(CosmeticBuy);Content->AddChildToVerticalBox(BuySize)->SetPadding(FMargin(0,4,0,0));
    Refresh();
}
void UMCCosmeticItemWidget::Resolve()
{
    if(!WidgetTree) return;
    CosmeticButton=WidgetTree->FindWidget<UButton>(TEXT("CosmeticButton"));CosmeticBuy=WidgetTree->FindWidget<UButton>(TEXT("CosmeticBuy"));
    CosmeticCardSurface=WidgetTree->FindWidget<UBorder>(TEXT("CosmeticCardSurface"));CosmeticThumbnailFrame=WidgetTree->FindWidget<UBorder>(TEXT("CosmeticThumbnailFrame"));
    CosmeticThumbnail=WidgetTree->FindWidget<UImage>(TEXT("CosmeticThumbnail"));CosmeticName=WidgetTree->FindWidget<UTextBlock>(TEXT("CosmeticName"));
    CosmeticDescription=WidgetTree->FindWidget<UTextBlock>(TEXT("CosmeticDescription"));CosmeticRarity=WidgetTree->FindWidget<UTextBlock>(TEXT("CosmeticRarity"));
    CosmeticPrice=WidgetTree->FindWidget<UTextBlock>(TEXT("CosmeticPrice"));CosmeticState=WidgetTree->FindWidget<UTextBlock>(TEXT("CosmeticState"));CosmeticBuyLabel=WidgetTree->FindWidget<UTextBlock>(TEXT("CosmeticBuyLabel"));
}
void UMCCosmeticItemWidget::Configure(FName InSlot,FName InItem,FName InCatalog,FText InName,bool bInOwned,bool bInSelected,int32 InPrice,bool bInAffordable)
{
    ItemSlot=InSlot;ItemId=InItem;CatalogId=InCatalog;Label=InName;bOwned=bInOwned;bSelected=bInSelected;Price=FMath::Max(0,InPrice);bAffordable=bInAffordable;
    BuildPresentationTree();Refresh();ApplyTheme();
}
void UMCCosmeticItemWidget::SetProductPresentation(UTexture2D* InIcon,FText InDescription,EMCHubItemRarity InRarity,bool bInShowPurchase,FText InStatusOverride)
{ProductIcon=InIcon;Description=InDescription;ProductRarity=InRarity;bPurchaseVisible=bInShowPurchase;StateOverride=InStatusOverride;Refresh();ApplyTheme();}
void UMCCosmeticItemWidget::Refresh()
{
    Resolve();
    if(CosmeticName) CosmeticName->SetText(Label.IsEmpty()?Localized(TEXT("UI.Wardrobe.NoAccessory"),LOCTEXT("Default","Без аксессуара")):Label);
    if(CosmeticDescription) {
        CosmeticDescription->SetText(Description);CosmeticDescription->SetToolTipText(Description);
        // Clothing cards avoid repeating the same appearance-only sentence; inspection retains its full tooltip.
        if(auto* DescriptionBox=CosmeticDescription->GetParent()) DescriptionBox->SetVisibility(ItemSlot.IsNone()?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    }
    if(CosmeticButton) CosmeticButton->SetToolTipText(Description);
    if(CosmeticPrice) CosmeticPrice->SetText(CatalogId.IsNone()?FText::GetEmpty():Price==0
        ?Localized(TEXT("UI.Product.Free"),LOCTEXT("Free","Бесплатно"))
        :FText::Format(Localized(TEXT("UI.Product.Coins"),LOCTEXT("Coins","{0} монет")),FText::AsNumber(Price)));
    FText Rarity;
    switch(ProductRarity) {
    case EMCHubItemRarity::Rare:Rarity=Localized(TEXT("UI.Product.Rare"),LOCTEXT("Rare","Редкое"));break;
    case EMCHubItemRarity::Epic:Rarity=Localized(TEXT("UI.Product.Epic"),LOCTEXT("Epic","Эпическое"));break;
    case EMCHubItemRarity::Legendary:Rarity=Localized(TEXT("UI.Product.Legendary"),LOCTEXT("Legendary","Легендарное"));break;
    default:Rarity=Localized(TEXT("UI.Product.Common"),LOCTEXT("Common","Обычное"));break;}
    if(CosmeticRarity) CosmeticRarity->SetText(CatalogId.IsNone()?FText::GetEmpty():Rarity);
    FText State=StateOverride;
    if(State.IsEmpty()) State=bSelected?(bOwned?Localized(TEXT("UI.Wardrobe.Selected"),LOCTEXT("Selected","Выбрано")):Localized(TEXT("UI.Product.Trying"),LOCTEXT("Trying","На примерке"))):
        bOwned?Localized(TEXT("UI.Wardrobe.Owned"),LOCTEXT("Owned","В наличии")):Localized(TEXT("UI.Product.Try"),LOCTEXT("Try","Нажми, чтобы примерить"));
    if(CosmeticState) CosmeticState->SetText(State);
    // Affordability controls only the purchase action. Selection/inspection is always available.
    if(CosmeticButton) CosmeticButton->SetIsEnabled(true);
    const bool ShowBuy=bPurchaseVisible && !bOwned && !CatalogId.IsNone();
    if(auto* BuySize=WidgetTree?WidgetTree->FindWidget(TEXT("CosmeticBuySize")):nullptr) BuySize->SetVisibility(ShowBuy?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(CosmeticBuy) {CosmeticBuy->SetIsEnabled(bAffordable);CosmeticBuy->SetVisibility(ShowBuy?ESlateVisibility::Visible:ESlateVisibility::Collapsed);}
    if(CosmeticBuyLabel) CosmeticBuyLabel->SetText(bAffordable
        ?(Price==0?Localized(TEXT("UI.Product.Get"),LOCTEXT("Get","Получить")):Localized(TEXT("UI.Product.Buy"),LOCTEXT("Buy","Купить")))
        :Localized(TEXT("UI.Product.Insufficient"),LOCTEXT("Insufficient","Мало монет")));
    if(CosmeticThumbnail) {
        CosmeticThumbnail->SetBrushFromTexture(ProductIcon);
        if(ProductIcon) if(const auto* Region=ThumbnailUVRegions.Find(ProductIcon->GetFName())) {
            if(Region->X>=0 && Region->Y>=0 && Region->Z<=1 && Region->W<=1 && Region->Z>Region->X && Region->W>Region->Y) {
                auto Brush=CosmeticThumbnail->GetBrush();
                Brush.SetUVRegion(FBox2f(FVector2f(float(Region->X),float(Region->Y)),FVector2f(float(Region->Z),float(Region->W))));
                Brush.ImageSize=FVector2D(ProductIcon->GetSizeX()*(Region->Z-Region->X),ProductIcon->GetSizeY()*(Region->W-Region->Y));
                CosmeticThumbnail->SetBrush(Brush);
            }
        }
        CosmeticThumbnail->SetVisibility(ProductIcon?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
    }
    BindControls();
}
void UMCCosmeticItemWidget::ApplySurface()
{
    const auto* Style=GetResolvedTheme();if(!Style) return;const auto& P=Style->Palette;
    const auto Base=FMath::Lerp(P.Marine,P.Mint,.18f*SelectionAmount);
    auto Tint=[](UBorder* Border,FLinearColor Color) {
        if(!Border) return;
        if(auto* MID=Cast<UMaterialInstanceDynamic>(Border->Background.GetResourceObject())) MID->SetVectorParameterValue(TEXT("SurfaceColor"),Color);
    };
    Tint(CosmeticCardSurface,FMath::Lerp(Base,P.Mint,.08f*HoverAmount));
    Tint(CosmeticThumbnailFrame,FMath::Lerp(P.Cream,P.Mint,.14f+.06f*HoverAmount));
}
void UMCCosmeticItemWidget::ApplyTheme()
{
    Resolve();Super::ApplyTheme();const auto* Style=GetResolvedTheme();if(!Style) return;
    const auto& P=Style->Palette;
    MCUIVisualStyle::Panel(CosmeticCardSurface,Style,bSelected?FMath::Lerp(P.Marine,P.Mint,.18f):P.Marine);
    MCUIVisualStyle::Panel(CosmeticThumbnailFrame,Style,FMath::Lerp(P.Cream,P.Mint,.14f));
    // The transparent selection button retains keyboard focus and hit testing over the content.
    if(CosmeticButton) {FButtonStyle B=CosmeticButton->GetStyle();for(FSlateBrush* Brush:{&B.Normal,&B.Hovered,&B.Pressed,&B.Disabled}) Brush->DrawAs=ESlateBrushDrawType::NoDrawType;CosmeticButton->SetStyle(B);}
    MCUIVisualStyle::Button(CosmeticBuy,Style,P.Mint);
    auto Text=[Style](UTextBlock* W,int32 Size,FLinearColor Color,bool Bold) {if(W) {W->SetFont(Style->MakeGameplayFont(Size,Bold));W->SetColorAndOpacity(Color);}};
    Text(CosmeticName,15,P.Cream,true);Text(CosmeticDescription,11,P.OnMarine.CopyWithNewOpacity(.88f),false);
    // The shop reuses the saved wardrobe tile. Its old 20px title row cannot
    // contain a wrapped name; reserve two lines before the description preview.
    // Wardrobe cards always supply Face/Hat, including the no-accessory card.
    const bool bShopProduct=ItemSlot.IsNone() && !ItemId.IsNone() && !CatalogId.IsNone();
    if(bShopProduct && CosmeticName) {
        CosmeticName->SetAutoWrapText(true);
        if(auto* NameBox=Cast<USizeBox>(CosmeticName->GetParent())) {
            NameBox->ClearHeightOverride();NameBox->SetMinDesiredHeight(44);
            if(auto* NameRow=Cast<UVerticalBoxSlot>(NameBox->Slot)) NameRow->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
        }
        if(auto* CardSize=WidgetTree->FindWidget<USizeBox>(TEXT("CosmeticCardSize"))) {
            CardSize->ClearHeightOverride();CardSize->SetMinDesiredHeight(204);
        }
    }
    FLinearColor Rarity=P.OnMarine;
    if(ProductRarity==EMCHubItemRarity::Rare) Rarity=P.Accent;else if(ProductRarity==EMCHubItemRarity::Epic) Rarity=FMath::Lerp(P.Accent,P.Cost,.5f);else if(ProductRarity==EMCHubItemRarity::Legendary) Rarity=P.Warning;
    Text(CosmeticRarity,10,Rarity,true);Text(CosmeticPrice,11,bAffordable || bOwned?P.Cream:P.Warning,true);
    Text(CosmeticState,11,bSelected?P.Mint:P.OnMarine.CopyWithNewOpacity(.8f),false);Text(CosmeticBuyLabel,12,P.ButtonText,true);
    ApplySurface();
}
void UMCCosmeticItemWidget::BindControls()
{
    if(IsDesignTime()) return;
    if(CosmeticButton) {CosmeticButton->OnClicked.AddUniqueDynamic(this,&ThisClass::Pressed);CosmeticButton->OnHovered.AddUniqueDynamic(this,&ThisClass::Hovered);CosmeticButton->OnUnhovered.AddUniqueDynamic(this,&ThisClass::Unhovered);}
    if(CosmeticBuy) CosmeticBuy->OnClicked.AddUniqueDynamic(this,&ThisClass::BuyPressed);
}
void UMCCosmeticItemWidget::NativeConstruct() {Super::NativeConstruct();Resolve();BindControls();Refresh();ApplyTheme();}
void UMCCosmeticItemWidget::NativeDestruct()
{
    if(CosmeticButton) {CosmeticButton->OnClicked.RemoveAll(this);CosmeticButton->OnHovered.RemoveAll(this);CosmeticButton->OnUnhovered.RemoveAll(this);}
    if(CosmeticBuy) CosmeticBuy->OnClicked.RemoveAll(this);Super::NativeDestruct();
}
void UMCCosmeticItemWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);const float Target=bHovered?1.f:0.f,SelectedTarget=bSelected?1.f:0.f;
    if(!FMath::IsNearlyEqual(HoverAmount,Target,.002f) || !FMath::IsNearlyEqual(SelectionAmount,SelectedTarget,.002f)) {
        const float Ease=1.f-FMath::Exp(-DeltaSeconds/.14f);
        HoverAmount=FMath::Lerp(HoverAmount,Target,Ease);SelectionAmount=FMath::Lerp(SelectionAmount,SelectedTarget,Ease);ApplySurface();
    }
}
void UMCCosmeticItemWidget::Hovered() {bHovered=true;}
void UMCCosmeticItemWidget::Unhovered() {bHovered=false;}
void UMCCosmeticItemWidget::Pressed() {if(!bFixture && !IsDesignTime()) OnPressed.Broadcast(ItemSlot,ItemId,CatalogId);}
void UMCCosmeticItemWidget::BuyPressed() {if(!bFixture && !IsDesignTime() && bAffordable && !bOwned && !CatalogId.IsNone()) OnPurchaseRequested.Broadcast(CatalogId);}
void UMCCosmeticItemWidget::ApplyEditorPreview(float Time)
{
    bFixture=true;Super::ApplyEditorPreview(Time);Label=PreviewName.IsEmpty()?LOCTEXT("PreviewName","Рабочая кепка"):PreviewName;Price=PreviewPrice;ProductIcon=PreviewIcon;
    Description=LOCTEXT("PreviewDescription","Аксессуар. Только внешний вид.");CatalogId=TEXT("Preview");ProductRarity=EMCHubItemRarity::Rare;
    bOwned=PreviewScenario!=EMCGameplayUIPreviewScenario::Disabled;bAffordable=PreviewScenario!=EMCGameplayUIPreviewScenario::Disabled;bPurchaseVisible=!bOwned;bSelected=PreviewScenario==EMCGameplayUIPreviewScenario::Selected;
    BuildPresentationTree();Refresh();ApplyTheme();
}
#undef LOCTEXT_NAMESPACE
