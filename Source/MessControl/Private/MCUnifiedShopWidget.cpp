#include "MCUnifiedShopWidget.h"
#include "MCUIGridScrollBox.h"

#include "MCHubShopWidget.h"
#include "MCCosmeticItemWidget.h"
#include "MCGameplayUIFactory.h"
#include "MCPerkComponent.h"
#include "MCUniversalUIWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Button.h"
#include "Components/OverlaySlot.h"
#include "Components/Overlay.h"
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
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Internationalization/BreakIterator.h"
#include "Internationalization/IBreakIterator.h"
#include "InputCoreTypes.h"
#include "Rendering/SlateRenderer.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "MCUnifiedShop"

namespace
{
    FLinearColor MCUnifiedShopPrivate_Color(const TCHAR* Hex) {return FLinearColor(FColor::FromHex(Hex));}
    const FLinearColor MCUnifiedShopPrivate_Blue=MCUnifiedShopPrivate_Color(TEXT("4C76AE")),MCUnifiedShopPrivate_Navy=MCUnifiedShopPrivate_Color(TEXT("16226B")),MCUnifiedShopPrivate_Paper=MCUnifiedShopPrivate_Color(TEXT("FFFDFD"));
    const FLinearColor MCUnifiedShopPrivate_Dark=MCUnifiedShopPrivate_Color(TEXT("404243")),MCUnifiedShopPrivate_Pink=MCUnifiedShopPrivate_Color(TEXT("AE4C6C")),MCUnifiedShopPrivate_Green=MCUnifiedShopPrivate_Color(TEXT("2AA85C")),MCUnifiedShopPrivate_BackPink=MCUnifiedShopPrivate_Color(TEXT("A35173"));
    FText MCUnifiedShopPrivate_ReadText(const UUserWidget* Owner,FName Name)
    {
        auto* Text=Owner && Owner->WidgetTree?Owner->WidgetTree->FindWidget<UTextBlock>(Name):nullptr;
        return Text?Text->GetText():FText::GetEmpty();
    }
    void MCUnifiedShopPrivate_StyleUnavailableCard(UMCUniversalUIWidget* Card)
    {
        // The PSD's unavailable merchandise stays charcoal. Scope this to shop
        // cards; the shared disabled palette remains unchanged for other UI.
        if(!Card || Card->GetIsEnabled() || !Card->WidgetTree) return;
        if(auto* Button=Card->WidgetTree->FindWidget<UButton>(TEXT("UniversalSurface"))) {
            FButtonStyle Style=Button->GetStyle();
            const FSlateRoundedBoxBrush Brush(MCUnifiedShopPrivate_Dark,Card->CornerRadius,Card->CustomOutline,Card->OutlineWidth);
            Style.SetNormal(Brush);Style.SetDisabled(Brush);Button->SetStyle(Style);
        }
    }
}
struct FMCUnifiedShopProduct
{
    FName ItemID;
    FText Name,Description,Price,State;
    FSlateBrush Icon;
    bool bEnabled=true;
};

void UMCUnifiedShopBinding::Clicked()
{
    if(!Owner.IsValid()) return;
    if(!ItemID.IsNone()) Owner->ForwardItem(ItemID);
    else Owner->ForwardControl(SourceControl);
}
UMCUnifiedShopWidget::UMCUnifiedShopWidget()
{
    bUseNativeLayout=false;bOverridePreviewUIVersion=true;SetIsFocusable(true);
}
UMCUniversalUIWidget* UMCUnifiedShopWidget::MakeShape(FName Name,FLinearColor Fill,FVector2D Shadow,bool bButton)
{
    auto* Panel=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),Name);
    Panel->Kind=bButton?EMCUniversalUIKind::Button:EMCUniversalUIKind::Panel;Panel->bCustomColors=true;Panel->bUseThemeShape=false;
    Panel->CustomFill=Fill;Panel->CustomText=MCUnifiedShopPrivate_Paper;Panel->CustomOutline=MCUnifiedShopPrivate_Navy;Panel->CustomShadow=Shadow.IsNearlyZero()?MCUnifiedShopPrivate_Navy:MCUnifiedShopPrivate_Paper;
    Panel->CornerRadius=20;Panel->OutlineWidth=0;Panel->ShadowOffset=Shadow;Panel->ContentPadding=FMargin(14);
    Panel->Label=FText::GetEmpty();Panel->Initialize();Panel->BuildPresentationTree();return Panel;
}
UTextBlock* UMCUnifiedShopWidget::MakeText(FName Name,int32 FontSize)
{
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
    Label->SetAutoWrapText(true);Label->SetJustification(ETextJustify::Center);Label->SetVisibility(ESlateVisibility::HitTestInvisible);
    BindStyle(Name,EMCUIColorToken::Cream,EMCUIStyleTarget::Text,FontSize,true);return Label;
}
void UMCUnifiedShopWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(!Theme) Theme=UMCGameplayUIFactory::GetThemeForVersion(EMCUIVersion::New);
    if(WidgetTree->RootWidget) {UpdateResponsiveLayout();BindStaticControls();return;}
    auto* Size=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ShopV2Size"));
    Size->SetMinDesiredWidth(1000);Size->SetMaxDesiredWidth(1350);WidgetTree->RootWidget=Size;
    auto* Frame=MakeShape(TEXT("ShopV2Frame"),MCUnifiedShopPrivate_Blue,{10,14});Frame->CustomOutline=MCUnifiedShopPrivate_Navy;Frame->CustomShadow=MCUnifiedShopPrivate_Navy;Frame->OutlineWidth=5;
    Frame->ContentPadding=FMargin(24);Frame->ContourPoints={{0,0},{.62,0},{.62,.15},{1,.15},{1,1},{0,1}};
    Frame->ApplyTheme();Size->SetContent(Frame);
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("ShopV2Content"));Frame->SetContentForSlot(TEXT("Content"),Column);
    auto* Header=MakeShape(TEXT("ShopV2Header"),MCUnifiedShopPrivate_Paper,{0,0});Header->CustomText=MCUnifiedShopPrivate_Pink;Header->OutlineWidth=5;
    Header->LabelKey=TEXT("UI.Shop.Title");Header->Label=LOCTEXT("Shop","МАГАЗИН");Header->FontSize=44;Header->ContentPadding=FMargin(30,12);Header->ApplyTheme();
    auto* HeaderWidth=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ShopV2HeaderSize"));
    HeaderWidth->SetMinDesiredWidth(360);HeaderWidth->SetMaxDesiredWidth(540);HeaderWidth->SetContent(Header);
    Column->AddChildToVerticalBox(HeaderWidth)->SetHorizontalAlignment(HAlign_Left);
    auto* Balance=MakeText(TEXT("ShopV2Balance"),18);
    auto* BalanceCell=Column->AddChildToVerticalBox(Balance);BalanceCell->SetHorizontalAlignment(HAlign_Right);BalanceCell->SetPadding(FMargin(0,8,0,4));
    auto* Hint=MakeText(TEXT("ShopV2Hint"),20);Hint->SetJustification(ETextJustify::Left);
    Hint->SetText(Localized(TEXT("UI.Shop.RaidStockHint"),LOCTEXT("RaidStockHint","Активные и пассивные перки пополняют запас.")));
    TextBindings.Add(Hint->GetFName(),TEXT("UI.Shop.RaidStockHint"));Column->AddChildToVerticalBox(Hint)->SetPadding(FMargin(4,4,4,18));
    auto* Body=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("ShopV2Body"));Column->AddChildToVerticalBox(Body);
    auto* GridHeight=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ShopV2GridBounds"));GridHeight->SetHeightOverride(620);
    auto* GridScroll=WidgetTree->ConstructWidget<UMCUIGridScrollBox>(UMCUIGridScrollBox::StaticClass(),TEXT("ShopV2CatalogScroll"));GridHeight->SetContent(GridScroll);
    auto* Grid=WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(),TEXT("ShopV2CatalogGrid"));
    Grid->SetMinDesiredSlotWidth(210);Grid->SetSlotPadding(FMargin(4,0,12,16));GridScroll->AddChild(Grid);
    auto* GridCell=Body->AddChildToHorizontalBox(GridHeight);GridCell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));GridCell->SetPadding(FMargin(0,0,14,0));
    auto* DetailWidth=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ShopV2DetailBounds"));DetailWidth->SetMinDesiredWidth(400);DetailWidth->SetMaxDesiredWidth(410);
    auto* Detail=MakeShape(TEXT("ShopV2Detail"),MCUnifiedShopPrivate_Dark,{10,14});DetailWidth->SetContent(Detail);
    auto* DetailCell=Body->AddChildToHorizontalBox(DetailWidth);DetailCell->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));DetailCell->SetPadding(FMargin(8,0,10,16));
    auto* DetailColumn=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("ShopV2DetailContent"));Detail->SetContentForSlot(TEXT("Content"),DetailColumn);
    auto* DetailsHeight=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ShopV2DetailsHeight"));
    DetailColumn->AddChildToVerticalBox(DetailsHeight)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto* DescriptionColumn=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("ShopV2DescriptionColumn"));DetailsHeight->SetContent(DescriptionColumn);
    auto* IconSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ShopV2SelectedIconBounds"));IconSize->SetHeightOverride(120);
    auto* Fit=WidgetTree->ConstructWidget<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);IconSize->SetContent(Fit);
    auto* Icon=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("ShopV2SelectedIcon"));Icon->SetVisibility(ESlateVisibility::Hidden);Fit->SetContent(Icon);
    DescriptionColumn->AddChildToVerticalBox(IconSize)->SetPadding(FMargin(12,4,12,12));
    auto* SelectedName=MakeText(TEXT("ShopV2SelectedName"),26);SelectedName->SetWrapTextAt(350);
    DescriptionColumn->AddChildToVerticalBox(SelectedName)->SetPadding(FMargin(2,0,2,14));
    auto* Description=MakeText(TEXT("ShopV2SelectedDescription"),18);Description->SetWrapTextAt(350);
    auto* DescriptionWidth=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ShopV2DescriptionBounds"));DescriptionWidth->SetMaxDesiredWidth(350);DescriptionWidth->SetContent(Description);
    auto* DescriptionCell=DescriptionColumn->AddChildToVerticalBox(DescriptionWidth);DescriptionCell->SetHorizontalAlignment(HAlign_Center);DescriptionCell->SetPadding(FMargin(2,0,2,14));
    DescriptionColumn->AddChildToVerticalBox(MakeText(TEXT("ShopV2Transaction"),17))->SetPadding(FMargin(2,4,2,12));
    auto* Replace=WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(),TEXT("ShopV2ReplacementSlots"));
    Replace->SetExplicitWrapSize(true);Replace->SetWrapSize(380);Replace->SetInnerSlotPadding({8,8});DetailColumn->AddChildToVerticalBox(Replace)->SetPadding(FMargin(0,8));
    for(int32 I=0;I<4;++I) {
        auto* Button=MakeShape(FName(*FString::Printf(TEXT("ShopV2Replace%d"),I)),MCUnifiedShopPrivate_Blue,{0,0},true);Button->FontSize=18;Button->ContentPadding=FMargin(14,10);Replace->AddChildToWrapBox(Button);
    }
    Replace->SetVisibility(ESlateVisibility::Collapsed);
    auto* Buy=MakeShape(TEXT("ShopV2Buy"),MCUnifiedShopPrivate_Green,{0,0},true);Buy->FontSize=26;Buy->ContentPadding=FMargin(22,12);Buy->Label=Localized(TEXT("UI.Shop.Buy"),LOCTEXT("Buy","Купить"));Buy->ApplyTheme();
    auto* BuyCell=DetailColumn->AddChildToVerticalBox(Buy);BuyCell->SetHorizontalAlignment(HAlign_Center);BuyCell->SetPadding(FMargin(8,14,8,8));
    auto* Close=MakeShape(TEXT("ShopV2Back"),MCUnifiedShopPrivate_BackPink,{0,0},true);Close->LabelKey=TEXT("UI.Settings.Back");Close->Label=LOCTEXT("Back","НАЗАД");Close->FontSize=28;
    Close->OutlineWidth=5;Close->ContentPadding=FMargin(56,10);Close->ApplyTheme();
    auto* CloseCell=Column->AddChildToVerticalBox(Close);CloseCell->SetHorizontalAlignment(HAlign_Center);CloseCell->SetPadding(FMargin(8,16,8,4));
    UpdateResponsiveLayout();BindStaticControls();
}
void UMCUnifiedShopWidget::BindStaticControls()
{
    if(!WidgetTree || IsDesignTime() || !Backend.IsValid()) return;
    for(UMCUnifiedShopBinding* Binding:StaticBindings) if(Binding) {
        const FName View=Binding->SourceControl==TEXT("ShopBuy")?FName(TEXT("ShopV2Buy")):
            Binding->SourceControl==TEXT("ShopClose")?FName(TEXT("ShopV2Back")):
            FName(*Binding->SourceControl.ToString().Replace(TEXT("ShopReplace"),TEXT("ShopV2Replace")));
        if(auto* Button=WidgetTree->FindWidget<UMCUniversalUIWidget>(View)) Button->OnClicked.RemoveDynamic(Binding,&UMCUnifiedShopBinding::Clicked);
    }
    StaticBindings.Reset();
    auto Bind=[this](FName View,FName Original) {
        if(auto* Button=WidgetTree->FindWidget<UMCUniversalUIWidget>(View)) {
            auto* Binding=NewObject<UMCUnifiedShopBinding>(this);Binding->Owner=this;Binding->SourceControl=Original;StaticBindings.Add(Binding);
            Button->OnClicked.AddUniqueDynamic(Binding,&UMCUnifiedShopBinding::Clicked);
        }
    };
    Bind(TEXT("ShopV2Buy"),TEXT("ShopBuy"));Bind(TEXT("ShopV2Back"),TEXT("ShopClose"));
    for(int32 I=0;I<4;++I) Bind(FName(*FString::Printf(TEXT("ShopV2Replace%d"),I)),FName(*FString::Printf(TEXT("ShopReplace%d"),I)));
}
UWidget* UMCUnifiedShopWidget::SourceControl(FName Name) const {return Backend.IsValid() && Backend->WidgetTree?Backend->WidgetTree->FindWidget(Name):nullptr;}
void UMCUnifiedShopWidget::SetBackend(UMCHubShopWidget* Source)
{
    if(Backend.Get()==Source) return;
    Backend=Source;if(Source) {BindStaticControls();RefreshFromBackend();}
}
void UMCUnifiedShopWidget::PresentProducts(const TArray<FMCUnifiedShopProduct>& Products,FName Selected)
{
    if(!WidgetTree) return;auto* Grid=WidgetTree->FindWidget<UUniformGridPanel>(TEXT("ShopV2CatalogGrid"));if(!Grid) return;
    bool Rebuild=Cards.Num()!=Products.Num();
    for(int32 I=0;!Rebuild && I<Products.Num();++I) Rebuild=Cards[I].ItemID!=Products[I].ItemID || !Cards[I].Surface;
    if(Rebuild) {
        for(int32 I=0;I<Cards.Num() && I<CardBindings.Num();++I)
            if(Cards[I].Surface && CardBindings[I]) Cards[I].Surface->OnClicked.RemoveDynamic(CardBindings[I],&UMCUnifiedShopBinding::Clicked);
        TSet<FName> RetiredLabels;
        for(const auto& Old:Cards) {
            if(Old.Name) RetiredLabels.Add(Old.Name->GetFName());
            if(Old.Price) RetiredLabels.Add(Old.Price->GetFName());
            if(Old.State) RetiredLabels.Add(Old.State->GetFName());
        }
        StyleBindings.RemoveAll([&RetiredLabels](const FMCUIStyleBinding& Binding){return RetiredLabels.Contains(Binding.WidgetName);});
        Grid->ClearChildren();Cards.Reset();CardBindings.Reset();
        for(int32 I=0;I<Products.Num();++I) {
            FMCUnifiedShopCard Card;Card.ItemID=Products[I].ItemID;
            Card.Surface=MakeShape(MakeUniqueObjectName(WidgetTree,UMCUniversalUIWidget::StaticClass(),FName(*FString::Printf(TEXT("ShopV2Card%d"),I))),MCUnifiedShopPrivate_Dark,{8,12},true);
            Card.Surface->ContentPadding=FMargin(10);
            auto* CardBody=WidgetTree->ConstructWidget<UVerticalBox>();Card.Surface->SetContentForSlot(TEXT("Content"),CardBody);
            auto* IconSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),MakeUniqueObjectName(WidgetTree,USizeBox::StaticClass(),TEXT("ShopV2CardIconBounds")));IconSize->SetHeightOverride(100);
            auto* Fit=WidgetTree->ConstructWidget<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);IconSize->SetContent(Fit);
            Card.Icon=WidgetTree->ConstructWidget<UImage>();Card.Icon->SetVisibility(ESlateVisibility::HitTestInvisible);Fit->SetContent(Card.Icon);
            CardBody->AddChildToVerticalBox(IconSize)->SetPadding(FMargin(3,0,3,6));
            auto Line=[&](UTextBlock* Label,const TCHAR* BoundName,float Minimum) {
                Label->SetWrapTextAt(190);
                auto* Bounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),MakeUniqueObjectName(WidgetTree,USizeBox::StaticClass(),FName(BoundName)));
                Bounds->SetMinDesiredHeight(Minimum);Bounds->SetMaxDesiredWidth(190);Bounds->SetContent(Label);
                auto* Cell=CardBody->AddChildToVerticalBox(Bounds);Cell->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));Cell->SetHorizontalAlignment(HAlign_Center);
            };
            Card.Name=MakeText(MakeUniqueObjectName(WidgetTree,UTextBlock::StaticClass(),FName(*FString::Printf(TEXT("ShopV2CardName%d"),I))),20);Line(Card.Name,TEXT("ShopV2CardNameBounds"),28);
            Card.Price=MakeText(MakeUniqueObjectName(WidgetTree,UTextBlock::StaticClass(),FName(*FString::Printf(TEXT("ShopV2CardPrice%d"),I))),24);Line(Card.Price,TEXT("ShopV2CardPriceBounds"),30);
            Card.State=MakeText(MakeUniqueObjectName(WidgetTree,UTextBlock::StaticClass(),FName(*FString::Printf(TEXT("ShopV2CardState%d"),I))),14);Line(Card.State,TEXT("ShopV2CardStateBounds"),20);
            auto* Width=WidgetTree->ConstructWidget<USizeBox>();Width->SetMinDesiredWidth(210);Width->SetMaxDesiredWidth(340);Width->SetContent(Card.Surface);
            auto* Cell=Grid->AddChildToUniformGrid(Width,I/2,I%2);Cell->SetHorizontalAlignment(HAlign_Fill);Cell->SetVerticalAlignment(VAlign_Fill);
            if(!IsDesignTime() && Backend.IsValid()) {
                auto* Binding=NewObject<UMCUnifiedShopBinding>(this);Binding->Owner=this;Binding->ItemID=Card.ItemID;CardBindings.Add(Binding);
                Card.Surface->OnClicked.AddUniqueDynamic(Binding,&UMCUnifiedShopBinding::Clicked);
            }
            Cards.Add(Card);
        }
    }
    for(int32 I=0;I<Products.Num();++I) {
        const auto& Product=Products[I];auto& Card=Cards[I];
        Card.Name->SetText(Product.Name);Card.Price->SetText(Product.Price);Card.State->SetText(Product.State);
        Card.Surface->SetToolTipText(Product.State.IsEmpty()?Product.Name:FText::Format(FText::AsCultureInvariant(TEXT("{0}\n{1}")),Product.Name,Product.State));
        if(auto* Host=Card.Price->GetParent()) Host->SetVisibility(Product.Price.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible);
        if(auto* Host=Card.State->GetParent()) Host->SetVisibility(Product.State.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible);
        Card.Icon->SetBrush(Product.Icon);Card.Icon->SetVisibility(Product.Icon.GetResourceObject()?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
        Card.Surface->SetIsEnabled(Product.bEnabled);Card.Surface->CustomFill=Product.ItemID==Selected?MCUnifiedShopPrivate_Pink:MCUnifiedShopPrivate_Dark;Card.Surface->ApplyTheme();
    }
    // Newly generated labels use the same composite font as the surrounding root.
    Super::ApplyTheme();
    for(const auto& Card:Cards) MCUnifiedShopPrivate_StyleUnavailableCard(Card.Surface);
}
void UMCUnifiedShopWidget::RefreshFromBackend()
{
    if(IsDesignTime() || !Backend.IsValid() || !WidgetTree) return;
    TArray<FMCUnifiedShopProduct> Products;
    if(auto* Grid=Cast<UUniformGridPanel>(SourceControl(TEXT("ShopCatalogGrid"))))
        for(UWidget* Child:Grid->GetAllChildren()) if(auto* Original=Cast<UMCCosmeticItemWidget>(Child)) {
            FMCUnifiedShopProduct Product;Product.ItemID=Original->GetCatalogId();
            Product.Name=MCUnifiedShopPrivate_ReadText(Original,TEXT("CosmeticName"));Product.Description=MCUnifiedShopPrivate_ReadText(Original,TEXT("CosmeticDescription"));
            Product.Price=MCUnifiedShopPrivate_ReadText(Original,TEXT("CosmeticPrice"));Product.State=MCUnifiedShopPrivate_ReadText(Original,TEXT("CosmeticState"));
            if(auto* Image=Original->WidgetTree?Original->WidgetTree->FindWidget<UImage>(TEXT("CosmeticThumbnail")):nullptr) Product.Icon=Image->GetBrush();
            Products.Add(MoveTemp(Product));
        }
    const FName Selected=Backend->GetSelectedPresentationItemID();PresentProducts(Products,Selected);
    auto Copy=[this](FName Target,FName Source) {
        if(auto* To=WidgetTree->FindWidget<UTextBlock>(Target)) if(auto* From=Cast<UTextBlock>(SourceControl(Source))) To->SetText(From->GetText());
    };
    Copy(TEXT("ShopV2Balance"),TEXT("ShopBalance"));Copy(TEXT("ShopV2SelectedName"),TEXT("ShopSelectedName"));
    Copy(TEXT("ShopV2SelectedDescription"),TEXT("ShopSelectedDescription"));Copy(TEXT("ShopV2Transaction"),TEXT("ShopTransactionMessage"));
    if(auto* Icon=WidgetTree->FindWidget<UImage>(TEXT("ShopV2SelectedIcon"))) {
        const auto* Product=Products.FindByPredicate([Selected](const auto& Item){return Item.ItemID==Selected;});
        Icon->SetBrush(Product?Product->Icon:FSlateBrush());Icon->SetVisibility(Product && Product->Icon.GetResourceObject()?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
    }
    if(auto* Buy=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("ShopV2Buy"))) {
        auto* Original=Cast<UButton>(SourceControl(TEXT("ShopBuy")));Buy->SetIsEnabled(Original && Original->GetIsEnabled());
        Buy->SetLabel(NAME_None,MCUnifiedShopPrivate_ReadText(Backend.Get(),TEXT("ShopBuyLabel")));
        // Bound this action's text inside the narrow detail column. Keep the
        // authored font; longer translated states grow to multiple lines.
        if(Buy->WidgetTree) if(auto* Caption=Buy->WidgetTree->FindWidget<UTextBlock>(TEXT("UniversalLabel"))) {
            Caption->SetAutoWrapText(false);Caption->SetWrapTextAt(280.f);
        }
    }
    auto* OriginalSlots=SourceControl(TEXT("ShopReplacementSlots"));
    if(auto* Slots=WidgetTree->FindWidget(TEXT("ShopV2ReplacementSlots"))) Slots->SetVisibility(OriginalSlots && OriginalSlots->IsVisible()?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    for(int32 I=0;I<4;++I) if(auto* Button=WidgetTree->FindWidget<UMCUniversalUIWidget>(FName(*FString::Printf(TEXT("ShopV2Replace%d"),I)))) {
        auto* Original=Cast<UButton>(SourceControl(FName(*FString::Printf(TEXT("ShopReplace%d"),I))));
        Button->SetIsEnabled(Original && Original->GetIsEnabled());Button->SetLabel(NAME_None,MCUnifiedShopPrivate_ReadText(Backend.Get(),FName(*FString::Printf(TEXT("ShopReplaceLabel%d"),I))));
    }
    if(auto* State=WidgetTree->FindWidget<UTextBlock>(TEXT("ShopV2SelectedState"))) {
        const auto* Product=Products.FindByPredicate([Selected](const auto& Item){return Item.ItemID==Selected;});
        State->SetText(Product?Product->State:FText::GetEmpty());State->SetVisibility(Product && !Product->State.IsEmpty()?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    }
    UpdateResponsiveLayout();UpdateHeaderContour();
}
void UMCUnifiedShopWidget::PresentPreview()
{
    if(!WidgetTree) return;
    TArray<FMCUnifiedShopProduct> Products;
    auto* Table=GetDefault<UMCPerkComponent>()->PerkTable.LoadSynchronous();
    const TCHAR* IDs[]={TEXT("Consumable_Grenade"),TEXT(""),TEXT("Active_Sponge"),TEXT("Consumable_Driver"),TEXT("Active_Hook"),TEXT("Consumable_Rocket")};
    for(int32 I=0;I<6;++I) {
        FMCUnifiedShopProduct Product;Product.ItemID=FName(*FString::Printf(TEXT("DesignerFixture%d"),I));
        const auto* Definition=Table && *IDs[I]?Table->FindRow<FMCPerkDefinition>(FName(IDs[I]),TEXT("PSD shop fixture"),false):nullptr;
        if(Definition) {
            Product.Name=Localized(Definition->DisplayNameKey,Definition->DisplayName);Product.Description=Localized(Definition->DescriptionKey,Definition->Description);
            Product.Icon.SetResourceObject(Definition->Icon.LoadSynchronous());
        } else Product.Name=Localized(TEXT("UI.V2.Shop.ComingSoon"),LOCTEXT("ComingSoon","СКОРО"));
        Product.Price=I==1?FText::GetEmpty():FText::AsNumber(I==0?60:600);
        Product.bEnabled=I!=1;Products.Add(MoveTemp(Product));
    }
    PresentProducts(Products,Products[0].ItemID);
    if(auto* Name=WidgetTree->FindWidget<UTextBlock>(TEXT("ShopV2SelectedName"))) Name->SetText(Products[0].Name);
    if(auto* Copy=WidgetTree->FindWidget<UTextBlock>(TEXT("ShopV2SelectedDescription"))) Copy->SetText(Products[0].Description);
    if(auto* Icon=WidgetTree->FindWidget<UImage>(TEXT("ShopV2SelectedIcon"))) {Icon->SetBrush(Products[0].Icon);Icon->SetVisibility(Products[0].Icon.GetResourceObject()?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);}
    if(auto* Buy=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("ShopV2Buy"))) {Buy->SetIsEnabled(true);Buy->SetLabel(NAME_None,Products[0].Price);}
    if(auto* Balance=WidgetTree->FindWidget<UTextBlock>(TEXT("ShopV2Balance"))) Balance->SetText(FText::Format(Localized(TEXT("UI.Shop.Balance"),LOCTEXT("Balance","Монеты: {0}")),FText::AsNumber(800)));
}
void UMCUnifiedShopWidget::ForwardItem(FName ItemID) {if(!IsDesignTime() && Backend.IsValid()) {Backend->SelectCatalogItem(ItemID);RefreshFromBackend();}}
void UMCUnifiedShopWidget::ForwardControl(FName Name)
{
    if(IsDesignTime() || !Backend.IsValid()) return;
    if(auto* Original=Cast<UButton>(SourceControl(Name));Original && Original->GetIsEnabled()) Original->OnClicked.Broadcast();
    RefreshFromBackend();
}
void UMCUnifiedShopWidget::RequestBack() {if(!IsDesignTime() && Backend.IsValid()) {Backend->RequestPresentationBack();RefreshFromBackend();}}
UWidget* UMCUnifiedShopWidget::GetDefaultFocusTarget() const
{
    if(!WidgetTree) return nullptr;
    if(auto* Slots=WidgetTree->FindWidget(TEXT("ShopV2ReplacementSlots"));Slots && Slots->IsVisible())
        for(int32 I=0;I<4;++I)
            if(auto* Button=WidgetTree->FindWidget<UMCUniversalUIWidget>(FName(*FString::Printf(TEXT("ShopV2Replace%d"),I)));Button && Button->GetIsEnabled() && Button->WidgetTree)
                return Button->WidgetTree->FindWidget(TEXT("UniversalSurface"));
    const FName Selected=Backend.IsValid()?Backend->GetSelectedPresentationItemID():NAME_None;
    for(const auto& Card:Cards) if(Card.ItemID==Selected && Card.Surface && Card.Surface->GetIsEnabled() && Card.Surface->WidgetTree)
        return Card.Surface->WidgetTree->FindWidget(TEXT("UniversalSurface"));
    for(const auto& Card:Cards) if(Card.Surface && Card.Surface->GetIsEnabled() && Card.Surface->WidgetTree)
        return Card.Surface->WidgetTree->FindWidget(TEXT("UniversalSurface"));
    if(auto* Back=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("ShopV2Back"))) return Back->WidgetTree?Back->WidgetTree->FindWidget(TEXT("UniversalSurface")):Back;
    return nullptr;
}
void UMCUnifiedShopWidget::UpdateResponsiveLayout(FVector2D AvailableSize)
{
    if(!WidgetTree) return;
    auto* RootBounds=WidgetTree->FindWidget<USizeBox>(TEXT("ShopV2Size"));
    auto* Frame=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("ShopV2Frame"));
    auto* Body=WidgetTree->FindWidget<UHorizontalBox>(TEXT("ShopV2Body"));
    if(!RootBounds || !Frame || !Body) return;
    if(WidgetTree->RootWidget==RootBounds) {
        auto* AlignmentRoot=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("ShopV2AlignmentRoot"));
        WidgetTree->RootWidget=AlignmentRoot;auto* AlignedCell=AlignmentRoot->AddChildToOverlay(RootBounds);
        AlignedCell->SetHorizontalAlignment(HAlign_Left);AlignedCell->SetVerticalAlignment(VAlign_Center);
    }
    auto* Grid=WidgetTree->FindWidget<UUniformGridPanel>(TEXT("ShopV2CatalogGrid"));
    auto* Detail=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("ShopV2Detail"));
    auto* DetailBounds=WidgetTree->FindWidget<USizeBox>(TEXT("ShopV2DetailBounds"));
    if(!DetailBounds && Detail)
        if(auto* Existing=Cast<USizeBox>(Detail->GetParent());Existing && Existing->GetContent()==Detail && Existing->GetParent()==Body) DetailBounds=Existing;
    auto* DetailScroll=WidgetTree->FindWidget<UScrollBox>(TEXT("ShopV2DetailScroll"));
    auto* DetailContent=WidgetTree->FindWidget<UVerticalBox>(TEXT("ShopV2DetailContent"));
    auto* DescriptionColumn=WidgetTree->FindWidget<UVerticalBox>(TEXT("ShopV2DescriptionColumn"));
    if(!DescriptionColumn) if(auto* Caption=WidgetTree->FindWidget<UTextBlock>(TEXT("ShopV2SelectedName"))) DescriptionColumn=Cast<UVerticalBox>(Caption->GetParent());
    if(!DescriptionColumn && DetailScroll && DetailScroll->GetChildrenCount()==1) DescriptionColumn=Cast<UVerticalBox>(DetailScroll->GetChildAt(0));
    auto* DetailsViewport=WidgetTree->FindWidget<USizeBox>(TEXT("ShopV2DetailsHeight"));
    if(!DetailsViewport && DescriptionColumn)
        if(auto* Existing=Cast<USizeBox>(DescriptionColumn->GetParent());Existing && Existing->GetContent()==DescriptionColumn && Existing->GetParent()==DetailContent) DetailsViewport=Existing;
    if(!DetailsViewport && DetailScroll)
        if(auto* Existing=Cast<USizeBox>(DetailScroll->GetParent());Existing && Existing->GetContent()==DetailScroll && Existing->GetParent()==DetailContent) DetailsViewport=Existing;
    if(DetailsViewport && DescriptionColumn && DetailScroll && DetailsViewport->GetContent()==DetailScroll && DescriptionColumn->GetParent()==DetailScroll) {
        // Descriptions are a complete fixed composition. Only merchandise grids
        // scroll. Retain the authored column, box and controls, removing the old
        // text scroll's input, scrollbar and Slate overflow-shadow layer.
        DescriptionColumn->RemoveFromParent();DetailsViewport->SetContent(DescriptionColumn);
    }
    if(DescriptionColumn && DetailsViewport && DetailsViewport->GetContent()==DescriptionColumn)
        if(auto* Cell=Cast<USizeBoxSlot>(DescriptionColumn->Slot)) {Cell->SetHorizontalAlignment(HAlign_Fill);Cell->SetVerticalAlignment(VAlign_Top);}
    auto* SelectedIcon=WidgetTree->FindWidget<USizeBox>(TEXT("ShopV2SelectedIconBounds"));
    if(!SelectedIcon) if(auto* Icon=WidgetTree->FindWidget<UImage>(TEXT("ShopV2SelectedIcon"));Icon && Icon->GetParent())
        if(auto* Existing=Cast<USizeBox>(Icon->GetParent()->GetParent());Existing && Existing->GetContent()==Icon->GetParent() && Existing->GetParent()==DescriptionColumn) SelectedIcon=Existing;
    auto* GridViewport=WidgetTree->FindWidget<USizeBox>(TEXT("ShopV2GridBounds"));
    if(!GridViewport) if(auto* ExistingScroll=WidgetTree->FindWidget<UScrollBox>(TEXT("ShopV2CatalogScroll")))
        if(auto* ExistingViewport=Cast<USizeBox>(ExistingScroll->GetParent());ExistingViewport && ExistingViewport->GetContent()==ExistingScroll && ExistingViewport->GetParent()==Body) {
            // Saved PSD class uses an unnamed SizeBox here. Resolve this exact
            // proven parent/content relation, keeping that authored object/slot.
            GridViewport=ExistingViewport;
        }
    if(!GridViewport) if(auto* ExistingScroll=WidgetTree->FindWidget<UScrollBox>(TEXT("ShopV2CatalogScroll"));ExistingScroll && ExistingScroll->GetParent()==Body) {
        // Earlier saved PSD trees put this exact scroll directly in the HBox.
        // Add only its missing viewport wrapper, preserving all siblings/slots.
        const auto Children=Body->GetAllChildren();TArray<FMargin> Insets;TArray<FSlateChildSize> Sizing;TArray<EHorizontalAlignment> Horizontal;TArray<EVerticalAlignment> Vertical;
        for(UWidget* Child:Children) {auto* Cell=Cast<UHorizontalBoxSlot>(Child->Slot);Insets.Add(Cell?Cell->GetPadding():FMargin(0));Sizing.Add(Cell?Cell->GetSize():FSlateChildSize(ESlateSizeRule::Automatic));Horizontal.Add(Cell?Cell->GetHorizontalAlignment():HAlign_Fill);Vertical.Add(Cell?Cell->GetVerticalAlignment():VAlign_Fill);}
        GridViewport=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ShopV2GridBounds"));
        Body->ClearChildren();GridViewport->SetContent(ExistingScroll);
        for(int32 Index=0;Index<Children.Num();++Index) {
            auto* Cell=Body->AddChildToHorizontalBox(Children[Index]==ExistingScroll?static_cast<UWidget*>(GridViewport):Children[Index]);
            Cell->SetPadding(Insets[Index]);Cell->SetSize(Sizing[Index]);Cell->SetHorizontalAlignment(Horizontal[Index]);Cell->SetVerticalAlignment(Vertical[Index]);
        }
    }
    auto* GridScroll=WidgetTree->FindWidget<UMCUIGridScrollBox>(TEXT("ShopV2CatalogScroll"));
    if(!GridScroll && Grid && GridViewport) {
        if(auto* Previous=Cast<UScrollBox>(GridViewport->GetContent());Previous && Grid->GetParent()==Previous) {
            const float Offset=Previous->GetScrollOffset();
            const FName Retired=MakeUniqueObjectName(WidgetTree,Previous->GetClass(),TEXT("ShopV2CatalogScrollRetired"));
            Previous->Rename(*Retired.ToString(),WidgetTree,REN_DontCreateRedirectors|REN_NonTransactional);
            GridScroll=WidgetTree->ConstructWidget<UMCUIGridScrollBox>(UMCUIGridScrollBox::StaticClass(),TEXT("ShopV2CatalogScroll"));
            GridScroll->SetWidgetStyle(Previous->GetWidgetStyle());GridScroll->SetWidgetBarStyle(Previous->GetWidgetBarStyle());
            GridScroll->SetScrollbarThickness(Previous->GetScrollbarThickness());GridScroll->SetScrollbarPadding(Previous->GetScrollbarPadding());
            GridScroll->SetScrollBarVisibility(Previous->GetScrollBarVisibility());GridScroll->SetConsumeMouseWheel(Previous->GetConsumeMouseWheel());
            Grid->RemoveFromParent();GridScroll->AddChild(Grid);GridViewport->SetContent(GridScroll);GridScroll->SetScrollOffset(Offset);
        }
    }
    // Keep the PSD two-column cells and reserve more height by sharing the top
    // row. Existing named header/balance/hint objects retain their identity.
    if(!WidgetTree->FindWidget(TEXT("ShopV2TopRow"))) {
        auto* Column=WidgetTree->FindWidget<UVerticalBox>(TEXT("ShopV2Content"));
        auto* Header=WidgetTree->FindWidget(TEXT("ShopV2HeaderSize"));auto* Balance=WidgetTree->FindWidget(TEXT("ShopV2Balance"));auto* Hint=WidgetTree->FindWidget(TEXT("ShopV2Hint"));
        if(Column && Header && Balance && Hint) {
            const auto Children=Column->GetAllChildren();TArray<FMargin> Insets;TArray<FSlateChildSize> Sizing;TArray<EHorizontalAlignment> Horizontal;TArray<EVerticalAlignment> Vertical;
            for(UWidget* Child:Children) {auto* Cell=Cast<UVerticalBoxSlot>(Child->Slot);Insets.Add(Cell?Cell->GetPadding():FMargin(0));Sizing.Add(Cell?Cell->GetSize():FSlateChildSize(ESlateSizeRule::Automatic));Horizontal.Add(Cell?Cell->GetHorizontalAlignment():HAlign_Fill);Vertical.Add(Cell?Cell->GetVerticalAlignment():VAlign_Fill);}
            auto* Top=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("ShopV2TopRow"));
            auto* HeadingColumn=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("ShopV2TopHeading"));
            Column->ClearChildren();Top->AddChildToHorizontalBox(HeadingColumn)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            HeadingColumn->AddChildToVerticalBox(Header)->SetHorizontalAlignment(HAlign_Left);HeadingColumn->AddChildToVerticalBox(Hint)->SetPadding(FMargin(4,6,4,8));
            auto* BalanceCell=Top->AddChildToHorizontalBox(Balance);BalanceCell->SetVerticalAlignment(VAlign_Top);BalanceCell->SetPadding(FMargin(16,8,0,0));
            for(int32 Index=0;Index<Children.Num();++Index) {
                if(Children[Index]==Balance || Children[Index]==Hint) continue;
                auto* Cell=Column->AddChildToVerticalBox(Children[Index]==Header?static_cast<UWidget*>(Top):Children[Index]);
                Cell->SetPadding(Children[Index]==Header?FMargin(0,0,0,8):Insets[Index]);Cell->SetSize(Sizing[Index]);Cell->SetHorizontalAlignment(Horizontal[Index]);Cell->SetVerticalAlignment(Vertical[Index]);
            }
        }
    }
    if(!WidgetTree->FindWidget(TEXT("ShopV2InfoRow"))) {
        auto* Column=WidgetTree->FindWidget<UVerticalBox>(TEXT("ShopV2Content"));
        auto* Top=WidgetTree->FindWidget(TEXT("ShopV2TopRow"));
        auto* Hint=WidgetTree->FindWidget(TEXT("ShopV2Hint"));
        auto* Balance=WidgetTree->FindWidget(TEXT("ShopV2Balance"));
        if(Column && Top && Top->GetParent()==Column && Hint && Balance) {
            // Only the title belongs in the raised cap. Wallet and explanation
            // share a fixed row below it, rather than stretching the notch over
            // the selected-product panel. Reuse all existing source-bound nodes.
            const auto Children=Column->GetAllChildren();
            struct FCell {FMargin Padding;FSlateChildSize Size;EHorizontalAlignment H=HAlign_Fill;EVerticalAlignment V=VAlign_Fill;};
            TArray<FCell> Cells;
            for(UWidget* Child:Children) {FCell Saved;if(auto* Cell=Cast<UVerticalBoxSlot>(Child->Slot)) {Saved.Padding=Cell->GetPadding();Saved.Size=Cell->GetSize();Saved.H=Cell->GetHorizontalAlignment();Saved.V=Cell->GetVerticalAlignment();}Cells.Add(Saved);}
            auto* Info=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("ShopV2InfoRow"));
            Hint->RemoveFromParent();Balance->RemoveFromParent();
            auto* HintCell=Info->AddChildToHorizontalBox(Hint);HintCell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));HintCell->SetVerticalAlignment(VAlign_Center);
            auto* BalanceCell=Info->AddChildToHorizontalBox(Balance);BalanceCell->SetVerticalAlignment(VAlign_Bottom);BalanceCell->SetPadding(FMargin(12,0,0,0));
            Column->ClearChildren();
            for(int32 I=0;I<Children.Num();++I) {
                auto* Cell=Column->AddChildToVerticalBox(Children[I]);Cell->SetPadding(Cells[I].Padding);Cell->SetSize(Cells[I].Size);Cell->SetHorizontalAlignment(Cells[I].H);Cell->SetVerticalAlignment(Cells[I].V);
                if(Children[I]==Top) {Cell->SetPadding(FMargin(0,0,0,4));auto* InfoCell=Column->AddChildToVerticalBox(Info);InfoCell->SetPadding(FMargin(4,0,4,10));}
            }
        }
    }
    if(!WidgetTree->FindWidget(TEXT("ShopV2SelectedState")) && DescriptionColumn) {
        auto* State=MakeText(TEXT("ShopV2SelectedState"),17);DescriptionColumn->AddChildToVerticalBox(State)->SetPadding(FMargin(2,4,2,10));
    }
    // Existing saved nodes keep their identity. Only the catalog scrolls;
    // complete detail copy and actions stay in their fixed column slots.
    RootBounds->ClearMinDesiredWidth();RootBounds->ClearMinDesiredHeight();
    if(auto* FrameCell=Cast<USizeBoxSlot>(Frame->Slot)) FrameCell->SetVerticalAlignment(VAlign_Fill);
    if(auto* BodyCell=Cast<UVerticalBoxSlot>(Body->Slot)) BodyCell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    if(GridViewport) {
        GridViewport->ClearHeightOverride();GridViewport->ClearMinDesiredHeight();GridViewport->ClearMaxDesiredHeight();
    }
    if(DetailsViewport) {
        DetailsViewport->ClearHeightOverride();DetailsViewport->ClearMinDesiredHeight();DetailsViewport->ClearMaxDesiredHeight();
        if(auto* DetailBodyCell=Cast<UVerticalBoxSlot>(DetailsViewport->Slot)) DetailBodyCell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }
    if(AvailableSize.X<=1 || AvailableSize.Y<=1) AvailableSize=GetPaintSpaceGeometry().GetLocalSize();
    if(AvailableSize.X<=1 || AvailableSize.Y<=1) return;
    ResponsiveViewportSize=AvailableSize;
    const int32 Columns=2;
    const FMargin GridInset=Grid?Grid->GetSlotPadding():FMargin(4,0,12,16);
    const auto* GridBodySlot=GridViewport?Cast<UHorizontalBoxSlot>(GridViewport->Slot):nullptr;
    const auto* DetailBodySlot=DetailBounds?Cast<UHorizontalBoxSlot>(DetailBounds->Slot):nullptr;
    const FMargin GridBodyInset=GridBodySlot?GridBodySlot->GetPadding():FMargin(0,0,14,0);
    const FMargin DetailBodyInset=DetailBodySlot?DetailBodySlot->GetPadding():FMargin(8,0,10,16);
    const float BodyInsets=GridBodyInset.Left+GridBodyInset.Right+DetailBodyInset.Left+DetailBodyInset.Right;
    const float FrameInsets=Frame->ContentPadding.Left+Frame->ContentPadding.Right;
    const float ScrollbarGutter=GridScroll && GridScroll->GetScrollBarVisibility()!=ESlateVisibility::Collapsed?
        GridScroll->GetScrollbarThickness().X+GridScroll->GetScrollbarPadding().Left+GridScroll->GetScrollbarPadding().Right:0.f;
    float MinimumCardCopy=0.f;
    // Keep ordinary words intact at their authored font. Only a locale whose
    // natural segment cannot fit a PSD cell is allowed to widen the shop.
    if(FSlateApplication::IsInitialized()) if(const auto Renderer=FSlateApplication::Get().GetRenderer()) {
        const auto FontMeasure=Renderer->GetFontMeasureService();
        for(const auto& Card:Cards) for(const auto* Caption:{Card.Name.Get(),Card.Price.Get()}) if(Caption) {
            const FString Copy=Caption->GetText().ToString();
            const auto Breaks=FBreakIterator::CreateLineBreakIterator();Breaks->SetString(Copy);
            int32 Start=Breaks->ResetToBeginning();
            for(int32 End=Breaks->MoveToNext();End!=INDEX_NONE;End=Breaks->MoveToNext()) {
                const FString Segment=Copy.Mid(Start,End-Start).TrimStartAndEnd();
                if(!Segment.IsEmpty()) MinimumCardCopy=FMath::Max(MinimumCardCopy,float(FontMeasure->Measure(Segment,Caption->GetFont()).X));
                Start=End;
            }
        }
    }
    const float ReferenceInner=FMath::Max(1.f,FMath::Min(820.f,float(AvailableSize.X)-14.f)-FrameInsets);
    const float DetailWidth=FMath::Min(330.f,FMath::Max(1.f,ReferenceInner*.42f));
    const float NaturalWidth=FrameInsets+DetailWidth+BodyInsets+ScrollbarGutter+Columns*(GridInset.Left+GridInset.Right+MinimumCardCopy+14.f);
    const float ViewWidth=FMath::Max(1.f,FMath::Min(FMath::Max(820.f,NaturalWidth),float(AvailableSize.X)-14.f));
    const float ViewHeight=FMath::Max(1.f,float(AvailableSize.Y)-16.f);
    RootBounds->SetWidthOverride(ViewWidth);RootBounds->SetMaxDesiredWidth(ViewWidth);
    RootBounds->SetHeightOverride(ViewHeight);RootBounds->SetMaxDesiredHeight(ViewHeight);
    const float InnerWidth=FMath::Max(1.f,ViewWidth-FrameInsets);
    const float GridWidth=FMath::Max(1.f,InnerWidth-DetailWidth-BodyInsets);
    const float CardWidth=FMath::Max(1.f,(GridWidth-ScrollbarGutter)/Columns-GridInset.Left-GridInset.Right);
    if(DetailBounds) {
        DetailBounds->SetMinDesiredWidth(0);DetailBounds->SetWidthOverride(DetailWidth);DetailBounds->SetMaxDesiredWidth(DetailWidth);
        if(auto* Cell=Cast<UHorizontalBoxSlot>(DetailBounds->Slot)) Cell->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
    }
    if(GridViewport) {
        GridViewport->ClearMinDesiredWidth();GridViewport->SetWidthOverride(GridWidth);GridViewport->SetMaxDesiredWidth(GridWidth);
        if(auto* Cell=Cast<UHorizontalBoxSlot>(GridViewport->Slot)) Cell->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
    }
    if(Grid) {
        Grid->SetMinDesiredSlotWidth(FMath::Min(210.f,CardWidth));
        for(int32 Index=0;Index<Grid->GetChildrenCount();++Index) if(auto* CardBounds=Cast<USizeBox>(Grid->GetChildAt(Index))) {
            CardBounds->ClearMinDesiredWidth();CardBounds->SetWidthOverride(CardWidth);CardBounds->SetMaxDesiredWidth(CardWidth);
            if(auto* GridCell=Cast<UUniformGridSlot>(CardBounds->Slot)) {GridCell->SetRow(Index/Columns);GridCell->SetColumn(Index%Columns);}
        }
    }
    // Use the actual fill-body allocation. A saved tree may have no old
    // viewport wrapper, and the previous grid desired height can exceed the
    // viewport; neither is the available space for three complete rows.
    float GridHeight=float(Body->GetPaintSpaceGeometry().GetLocalSize().Y);
    if(GridHeight<=1.f && GridScroll) GridHeight=float(GridScroll->GetPaintSpaceGeometry().GetLocalSize().Y);
    if(GridHeight<=1.f) GridHeight=FMath::Max(1.f,ViewHeight-230.f);
    const float RowStride=FMath::Max(1.f,FMath::FloorToFloat(GridHeight/3.f));
    const float CellHeight=FMath::Max(1.f,RowStride-GridInset.Top-GridInset.Bottom);
    if(GridViewport) {GridViewport->SetHeightOverride(RowStride*3.f);GridViewport->SetMaxDesiredHeight(RowStride*3.f);if(auto* GridViewportCell=Cast<UHorizontalBoxSlot>(GridViewport->Slot)) GridViewportCell->SetVerticalAlignment(VAlign_Top);}
    if(Grid) {
        Grid->SetMinDesiredSlotHeight(CellHeight);
        for(int32 Index=0;Index<Grid->GetChildrenCount();++Index) if(auto* CardBounds=Cast<USizeBox>(Grid->GetChildAt(Index))) {
            CardBounds->ClearMinDesiredHeight();CardBounds->SetHeightOverride(CellHeight);CardBounds->SetMaxDesiredHeight(CellHeight);
        }
    }
    for(const auto& Card:Cards) {
        if(Card.Surface) Card.Surface->ContentPadding=FMargin(6);
        if(Card.State) if(auto* StateBounds=Card.State->GetParent()) StateBounds->SetVisibility(ESlateVisibility::Collapsed);
        const float NameHeight=Card.Name?FMath::Max(28.f,float(Card.Name->GetDesiredSize().Y)):28.f;
        const float PriceHeight=Card.Price && !Card.Price->GetText().IsEmpty()?FMath::Max(30.f,float(Card.Price->GetDesiredSize().Y)):0.f;
        if(Card.Icon && Card.Icon->GetParent()) if(auto* IconBounds=Cast<USizeBox>(Card.Icon->GetParent()->GetParent())) {
            const float IconExtent=FMath::Max(24.f,FMath::Min(CardWidth-20.f,CellHeight-NameHeight-PriceHeight-18.f));
            IconBounds->SetHeightOverride(IconExtent);IconBounds->SetWidthOverride(IconExtent);
            if(auto* IconCell=Cast<UVerticalBoxSlot>(IconBounds->Slot)) IconCell->SetHorizontalAlignment(HAlign_Center);
        }
    }
    if(GridScroll && Grid) GridScroll->ConfigureGrid(Grid,2,3,RowStride);
    for(const auto& Card:Cards) for(UTextBlock* Caption:{Card.Name.Get(),Card.Price.Get(),Card.State.Get()}) if(Caption) {
        const float CopyWidth=FMath::Max(1.f,CardWidth-14.f);Caption->SetAutoWrapText(false);Caption->SetWrapTextAt(CopyWidth);
        Caption->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
        if(auto* CopyBounds=Cast<USizeBox>(Caption->GetParent())) {CopyBounds->ClearMinDesiredWidth();CopyBounds->SetWidthOverride(CopyWidth);CopyBounds->SetMaxDesiredWidth(CopyWidth);}
    }
    if(auto* HeaderBounds=WidgetTree->FindWidget<USizeBox>(TEXT("ShopV2HeaderSize"))) {
        HeaderBounds->SetMinDesiredWidth(FMath::Min(360.f,InnerWidth));HeaderBounds->SetMaxDesiredWidth(InnerWidth);
    }
    if(auto* Hint=WidgetTree->FindWidget<UTextBlock>(TEXT("ShopV2Hint"))) {Hint->SetAutoWrapText(false);Hint->SetWrapTextAt(GridWidth);Hint->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);}
    if(auto* Balance=WidgetTree->FindWidget<UTextBlock>(TEXT("ShopV2Balance"))) {Balance->SetAutoWrapText(false);Balance->SetWrapTextAt(FMath::Max(1.f,DetailWidth-28.f));Balance->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);}
    if(auto* DescriptionBounds=WidgetTree->FindWidget<USizeBox>(TEXT("ShopV2DescriptionBounds"))) {
        DescriptionBounds->ClearMinDesiredWidth();DescriptionBounds->SetWidthOverride(FMath::Max(1.f,DetailWidth-48.f));DescriptionBounds->SetMaxDesiredWidth(FMath::Max(1.f,DetailWidth-48.f));
    }
    for(const TCHAR* Name:{TEXT("ShopV2SelectedName"),TEXT("ShopV2SelectedDescription"),TEXT("ShopV2Transaction"),TEXT("ShopV2SelectedState")})
        if(auto* Caption=WidgetTree->FindWidget<UTextBlock>(FName(Name))) {Caption->SetAutoWrapText(false);Caption->SetWrapTextAt(FMath::Max(1.f,DetailWidth-48.f));Caption->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);Caption->SetVisibility(Caption->GetText().IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);}
    if(auto* Replacement=WidgetTree->FindWidget<UWrapBox>(TEXT("ShopV2ReplacementSlots"))) Replacement->SetWrapSize(FMath::Max(1.f,DetailWidth-28.f));
    for(const TCHAR* Name:{TEXT("ShopV2Header"),TEXT("ShopV2Back"),TEXT("ShopV2Buy"),TEXT("ShopV2Replace0"),TEXT("ShopV2Replace1"),TEXT("ShopV2Replace2"),TEXT("ShopV2Replace3")})
        if(auto* Shape=WidgetTree->FindWidget<UMCUniversalUIWidget>(FName(Name));Shape && Shape->WidgetTree)
            if(auto* Caption=Shape->WidgetTree->FindWidget<UTextBlock>(TEXT("UniversalLabel"))) {
                const bool DetailAction=Shape->GetFName()!=TEXT("ShopV2Header") && Shape->GetFName()!=TEXT("ShopV2Back");
                const float CopyWidth=FMath::Max(1.f,(DetailAction?DetailWidth-28.f:InnerWidth)-Shape->ContentPadding.Left-Shape->ContentPadding.Right-12.f);
                Caption->SetAutoWrapText(false);Caption->SetWrapTextAt(CopyWidth);Caption->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
                if(Shape->GetFName()==TEXT("ShopV2Header")) {Caption->SetJustification(ETextJustify::Center);if(ShopTitleFont.HasValidFont()) Caption->SetFont(ShopTitleFont);}
            }
    if(SelectedIcon && DescriptionColumn && DetailContent) {
        const float Scale=FMath::Max(.01f,GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale());
        auto Measure=[Scale](UWidget* Widget) {if(const auto Slate=Widget?Widget->GetCachedWidget():nullptr) {Slate->MarkPrepassAsDirty();Slate->SlatePrepass(Scale);}return Widget?Widget->GetDesiredSize():FVector2D::ZeroVector;};
        float FixedHeight=0.f,TextHeight=0.f;
        for(UWidget* Child:DetailContent->GetAllChildren()) if(Child!=DetailsViewport && Child->GetVisibility()!=ESlateVisibility::Collapsed) {
            const auto* Cell=Cast<UVerticalBoxSlot>(Child->Slot);const FMargin ChildInset=Cell?Cell->GetPadding():FMargin(0);
            FixedHeight+=float(Measure(Child).Y)+ChildInset.Top+ChildInset.Bottom;
        }
        for(UWidget* Child:DescriptionColumn->GetAllChildren()) if(Child!=SelectedIcon && Child->GetVisibility()!=ESlateVisibility::Collapsed) {
            const auto* Cell=Cast<UVerticalBoxSlot>(Child->Slot);const FMargin ChildInset=Cell?Cell->GetPadding():FMargin(0);
            TextHeight+=float(Measure(Child).Y)+ChildInset.Top+ChildInset.Bottom;
        }
        const auto* IconCell=Cast<UVerticalBoxSlot>(SelectedIcon->Slot);const FMargin IconPadding=IconCell?IconCell->GetPadding():FMargin(0);
        const auto* ViewportCell=DetailsViewport?Cast<UVerticalBoxSlot>(DetailsViewport->Slot):nullptr;
        const FMargin ViewportPadding=ViewportCell?ViewportCell->GetPadding():FMargin(0);
        const float DetailInsets=Detail?Detail->ContentPadding.Top+Detail->ContentPadding.Bottom:36.f;
        const float CopyHeight=GridHeight-DetailBodyInset.Top-DetailBodyInset.Bottom-DetailInsets-FixedHeight-ViewportPadding.Top-ViewportPadding.Bottom;
        // Spend only the remaining height on the icon; the full semantic copy
        // keeps its authored font and has no text scrolling or elision.
        const float IconExtent=FMath::Max(24.f,FMath::Min3(190.f,DetailWidth-48.f,CopyHeight-TextHeight-IconPadding.Top-IconPadding.Bottom-2.f));
        SelectedIcon->ClearMinDesiredWidth();SelectedIcon->ClearMaxDesiredWidth();SelectedIcon->ClearMinDesiredHeight();SelectedIcon->ClearMaxDesiredHeight();
        SelectedIcon->SetWidthOverride(IconExtent);SelectedIcon->SetHeightOverride(IconExtent);
        if(auto* Cell=Cast<UVerticalBoxSlot>(SelectedIcon->Slot)) Cell->SetHorizontalAlignment(HAlign_Center);
    }
    for(const auto& Card:Cards) MCUnifiedShopPrivate_StyleUnavailableCard(Card.Surface);
}
void UMCUnifiedShopWidget::UpdateHeaderContour()
{
    if(!bAdaptHeaderContour || !WidgetTree) return;
    auto* Frame=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("ShopV2Frame"));
    auto* Header=WidgetTree->FindWidget(TEXT("ShopV2HeaderSize"));if(!Frame || !Header) return;
    const FVector2D Area=Frame->GetPaintSpaceGeometry().GetLocalSize();if(Area.X<=0 || Area.Y<=0) return;
    const FVector2D Title=Header->GetDesiredSize();
    FVector2D CapEnd(Title.X+54.f,Title.Y+34.f);
    const auto& HeaderPaint=Header->GetPaintSpaceGeometry();
    if(HeaderPaint.GetLocalSize().X>1 && HeaderPaint.GetLocalSize().Y>1)
        CapEnd=Frame->GetPaintSpaceGeometry().AbsoluteToLocal(HeaderPaint.LocalToAbsolute(HeaderPaint.GetLocalSize()))+FVector2D(20.f,0.f);
    const float X=FMath::Clamp(float(CapEnd.X/Area.X),.35f,.95f),Y=FMath::Clamp(float(CapEnd.Y/Area.Y),.08f,.6f);
    const TArray<FVector2D> Points={{0,0},{X,0},{X,Y},{1,Y},{1,1},{0,1}};
    if(Frame->ContourPoints!=Points) {Frame->ContourPoints=Points;Frame->ApplyTheme();}
    if(auto* TitleShape=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("ShopV2Header"));TitleShape && TitleShape->WidgetTree)
        if(auto* Caption=TitleShape->WidgetTree->FindWidget<UTextBlock>(TEXT("UniversalLabel"))) {Caption->SetJustification(ETextJustify::Center);if(ShopTitleFont.HasValidFont()) Caption->SetFont(ShopTitleFont);}
    for(const auto& Card:Cards) MCUnifiedShopPrivate_StyleUnavailableCard(Card.Surface);
}
void UMCUnifiedShopWidget::ApplyTheme()
{
    if(WidgetTree && TextBindings.FindRef(TEXT("ShopV2Hint"))==TEXT("UI.Shop.NextRunHint"))
        if(auto* Hint=WidgetTree->FindWidget<UTextBlock>(TEXT("ShopV2Hint")))
        {
            TextBindings.Add(Hint->GetFName(),TEXT("UI.Shop.RaidStockHint"));
            Hint->SetText(LOCTEXT("RaidStockHint","Активные и пассивные перки пополняют запас."));
        }
    Super::ApplyTheme();UpdateResponsiveLayout();UpdateHeaderContour();
}
void UMCUnifiedShopWidget::ApplyEditorPreview(float Time) {BuildPresentationTree();Super::ApplyEditorPreview(Time);PresentPreview();UpdateResponsiveLayout();UpdateHeaderContour();}
void UMCUnifiedShopWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);UpdateResponsiveLayout(Geometry.GetLocalSize());if(IsDesignTime()) return;
    RefreshElapsed+=DeltaSeconds;if(RefreshElapsed>=.15f) {RefreshElapsed=0;RefreshFromBackend();}
}
FReply UMCUnifiedShopWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if((Event.GetKey()==EKeys::Escape || Event.GetKey()==EKeys::Gamepad_FaceButton_Right) && !Event.IsRepeat()) {RequestBack();return FReply::Handled();}
    return Super::NativeOnPreviewKeyDown(Geometry,Event);
}

#undef LOCTEXT_NAMESPACE

