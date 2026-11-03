#include "MCUnifiedShopWidget.h"

#include "MCHubShopWidget.h"
#include "MCCosmeticItemWidget.h"
#include "MCGameplayUIFactory.h"
#include "MCPerkComponent.h"
#include "MCUniversalUIWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "InputCoreTypes.h"

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
    if(WidgetTree->RootWidget) {BindStaticControls();return;}
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
    Hint->SetText(Localized(TEXT("UI.Shop.NextRunHint"),LOCTEXT("NextRun","Бустеры — на следующий забег")));
    TextBindings.Add(Hint->GetFName(),TEXT("UI.Shop.NextRunHint"));Column->AddChildToVerticalBox(Hint)->SetPadding(FMargin(4,4,4,18));
    auto* Body=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("ShopV2Body"));Column->AddChildToVerticalBox(Body);
    auto* GridHeight=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ShopV2GridBounds"));GridHeight->SetHeightOverride(620);
    auto* GridScroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("ShopV2CatalogScroll"));GridHeight->SetContent(GridScroll);
    auto* Grid=WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(),TEXT("ShopV2CatalogGrid"));
    Grid->SetMinDesiredSlotWidth(210);Grid->SetSlotPadding(FMargin(4,0,12,16));GridScroll->AddChild(Grid);
    auto* GridCell=Body->AddChildToHorizontalBox(GridHeight);GridCell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));GridCell->SetPadding(FMargin(0,0,14,0));
    auto* DetailWidth=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ShopV2DetailBounds"));DetailWidth->SetMinDesiredWidth(400);DetailWidth->SetMaxDesiredWidth(410);
    auto* Detail=MakeShape(TEXT("ShopV2Detail"),MCUnifiedShopPrivate_Dark,{10,14});DetailWidth->SetContent(Detail);
    auto* DetailCell=Body->AddChildToHorizontalBox(DetailWidth);DetailCell->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));DetailCell->SetPadding(FMargin(8,0,10,16));
    auto* DetailColumn=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("ShopV2DetailContent"));Detail->SetContentForSlot(TEXT("Content"),DetailColumn);
    auto* DetailScroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("ShopV2DetailScroll"));
    auto* DetailsHeight=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ShopV2DetailsHeight"));DetailsHeight->SetHeightOverride(450);DetailsHeight->SetContent(DetailScroll);
    DetailColumn->AddChildToVerticalBox(DetailsHeight)->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
    auto* DescriptionColumn=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("ShopV2DescriptionColumn"));DetailScroll->AddChild(DescriptionColumn);
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
    BindStaticControls();
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
        if(auto* Host=Card.Price->GetParent()) Host->SetVisibility(Product.Price.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible);
        if(auto* Host=Card.State->GetParent()) Host->SetVisibility(Product.State.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible);
        Card.Icon->SetBrush(Product.Icon);Card.Icon->SetVisibility(Product.Icon.GetResourceObject()?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
        Card.Surface->SetIsEnabled(Product.bEnabled);Card.Surface->CustomFill=Product.ItemID==Selected?MCUnifiedShopPrivate_Pink:MCUnifiedShopPrivate_Dark;Card.Surface->ApplyTheme();
    }
    // Newly generated labels use the same composite font as the surrounding root.
    Super::ApplyTheme();
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
    UpdateHeaderContour();
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
void UMCUnifiedShopWidget::UpdateHeaderContour()
{
    if(!bAdaptHeaderContour || !WidgetTree) return;
    auto* Frame=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("ShopV2Frame"));
    auto* Header=WidgetTree->FindWidget(TEXT("ShopV2HeaderSize"));if(!Frame || !Header) return;
    const FVector2D Area=Frame->GetPaintSpaceGeometry().GetLocalSize();if(Area.X<=0 || Area.Y<=0) return;
    const FVector2D Title=Header->GetDesiredSize();
    const float X=FMath::Clamp(float((Title.X+68)/Area.X),.35f,.95f),Y=FMath::Clamp(float((Title.Y+68)/Area.Y),.08f,.4f);
    const TArray<FVector2D> Points={{0,0},{X,0},{X,Y},{1,Y},{1,1},{0,1}};
    if(Frame->ContourPoints!=Points) {Frame->ContourPoints=Points;Frame->ApplyTheme();}
}
void UMCUnifiedShopWidget::ApplyTheme() {Super::ApplyTheme();UpdateHeaderContour();}
void UMCUnifiedShopWidget::ApplyEditorPreview(float Time) {BuildPresentationTree();Super::ApplyEditorPreview(Time);PresentPreview();UpdateHeaderContour();}
void UMCUnifiedShopWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);if(IsDesignTime()) return;
    RefreshElapsed+=DeltaSeconds;if(RefreshElapsed>=.15f) {RefreshElapsed=0;RefreshFromBackend();}
}
FReply UMCUnifiedShopWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if((Event.GetKey()==EKeys::Escape || Event.GetKey()==EKeys::Gamepad_FaceButton_Right) && !Event.IsRepeat()) {RequestBack();return FReply::Handled();}
    return Super::NativeOnPreviewKeyDown(Geometry,Event);
}

#undef LOCTEXT_NAMESPACE

