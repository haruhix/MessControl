#include "MCUnifiedWardrobeWidget.h"

#include "MCHubWardrobeWidget.h"
#include "MCCosmeticItemWidget.h"
#include "MCCosmeticCatalog.h"
#include "MCHubEconomyComponent.h"
#include "MCGameplayUIFactory.h"
#include "MCUIDataLibrary.h"
#include "MCUniversalUIWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ExpandableArea.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Engine/Texture2D.h"
#include "InputCoreTypes.h"

#define LOCTEXT_NAMESPACE "MCUnifiedWardrobe"

namespace
{
    FLinearColor MCUnifiedWardrobePrivate_C(const TCHAR* Hex) {return FLinearColor(FColor::FromHex(Hex));}
    FText MCUnifiedWardrobePrivate_SourceText(UMCCosmeticItemWidget* Source,FName Name)
    {
        auto* Text=Source && Source->WidgetTree?Source->WidgetTree->FindWidget<UTextBlock>(Name):nullptr;
        return Text?Text->GetText():FText::GetEmpty();
    }
    UButton* MCUnifiedWardrobePrivate_Surface(UMCUniversalUIWidget* Shape) {return Shape && Shape->WidgetTree?Shape->WidgetTree->FindWidget<UButton>(TEXT("UniversalSurface")):nullptr;}
}
void UMCUnifiedWardrobeBinding::Pressed()
{
    bCaptured=true;PressedSlot=Slot;PressedItem=ItemID;PressedCatalog=CatalogID;
    if(Action==EMCUnifiedWardrobeAction::Primary && Owner.IsValid())
        if(const auto* Product=Owner->SelectedProduct()) {PressedSlot=Product->Slot;PressedItem=Product->ItemID;PressedCatalog=Product->CatalogID;bPressedOwned=Product->bOwned;}
}
void UMCUnifiedWardrobeBinding::Clicked()
{
    if(!Owner.IsValid()) return;
    switch(Action) {
    case EMCUnifiedWardrobeAction::Select:Owner->SelectProduct(Slot,ItemID,CatalogID);break;
    case EMCUnifiedWardrobeAction::Primary:
        if(!bCaptured) Pressed();
        Owner->ActivateProduct(PressedSlot,PressedItem,PressedCatalog,TOptional<bool>(bPressedOwned));break;
    case EMCUnifiedWardrobeAction::Apply:Owner->ForwardButton(TEXT("WardrobeApply"));break;
    case EMCUnifiedWardrobeAction::Back:Owner->RequestBack();break;
    }
    bCaptured=false;
}
UMCUnifiedWardrobeWidget::UMCUnifiedWardrobeWidget()
{
    bUseNativeLayout=false;bOverridePreviewUIVersion=true;SetIsFocusable(true);
}
void UMCUnifiedWardrobeWidget::BuildPresentationTree()
{
    if(!WidgetTree || WidgetTree->RootWidget) return;
    if(!Theme) Theme=UMCGameplayUIFactory::GetThemeForVersion(EMCUIVersion::New);
    auto* Root=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("WardrobeV2Root"));WidgetTree->RootWidget=Root;
    auto* DesignWidth=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("WardrobeV2DesignSize"));DesignWidth->SetWidthOverride(1450);
    auto* RootCell=Root->AddChildToOverlay(DesignWidth);RootCell->SetPadding(FMargin(36,24,36,32));RootCell->SetHorizontalAlignment(HAlign_Center);RootCell->SetVerticalAlignment(VAlign_Fill);
    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("WardrobeV2Scroll"));DesignWidth->SetContent(Scroll);
    auto* Columns=WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(),TEXT("WardrobeV2Columns"));Columns->SetExplicitWrapSize(true);Columns->SetWrapSize(1360);Columns->SetInnerSlotPadding(FVector2D(28,24));
    Scroll->AddChild(Columns);
    auto Shape=[&](FName Name,FLinearColor Fill,FLinearColor Edge,FLinearColor Shadow,FMargin ContentInset,EMCUniversalUIKind Kind=EMCUniversalUIKind::Panel) {
        auto* Result=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),Name);
        Result->Kind=Kind;Result->Label=FText::GetEmpty();Result->bCustomColors=true;Result->bUseThemeShape=false;Result->CornerRadius=24;Result->OutlineWidth=4;
        Result->CustomFill=Fill;Result->CustomOutline=Edge;Result->CustomText=MCUnifiedWardrobePrivate_C(TEXT("FFF9F8"));Result->CustomShadow=Shadow;Result->ContentPadding=ContentInset;Result->ShadowOffset=FVector2D(10,12);
        Result->Initialize();Result->BuildPresentationTree();return Result;
    };
    auto Text=[&](FName Name,FName Key,const FText& Copy,int32 Size,FLinearColor Tint) {
        auto* Result=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);Result->SetText(Localized(Key,Copy));Result->SetFont(GetResolvedTheme()->MakeFont(Size,true));
        Result->SetColorAndOpacity(Tint);Result->SetAutoWrapText(true);Result->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);Result->SetJustification(ETextJustify::Center);
        if(!Key.IsNone()) TextBindings.Add(Name,Key);return Result;
    };
    auto Action=[&](FName Name,FName Key,const FText& Copy,FLinearColor Fill) {
        auto* Result=Shape(Name,Fill,MCUnifiedWardrobePrivate_C(TEXT("16226B")),MCUnifiedWardrobePrivate_C(TEXT("16226B")),FMargin(28,13),EMCUniversalUIKind::Button);
        Result->LabelKey=Key;Result->Label=Copy;Result->FontSize=29;Result->ApplyTheme();return Result;
    };
    auto* LeftWidth=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("WardrobeV2LeftWidth"));LeftWidth->SetMinDesiredWidth(740);LeftWidth->SetMaxDesiredWidth(780);Columns->AddChildToWrapBox(LeftWidth)->SetVerticalAlignment(VAlign_Top);
    auto* Left=Shape(TEXT("WardrobeV2LeftPanel"),MCUnifiedWardrobePrivate_C(TEXT("4C76AE")),MCUnifiedWardrobePrivate_C(TEXT("16226B")),MCUnifiedWardrobePrivate_C(TEXT("16226B")),FMargin(30,20,30,28));
    Left->ContourPoints={{.1,0},{.88,0},{.88,.12},{1,.12},{1,1},{0,1},{0,.12},{.1,.12}};LeftWidth->SetContent(Left);
    auto* Body=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("WardrobeV2LeftContents"));Left->SetContentForSlot(TEXT("Content"),Body);
    auto* Header=Shape(TEXT("WardrobeV2Header"),MCUnifiedWardrobePrivate_C(TEXT("FFF9F8")),MCUnifiedWardrobePrivate_C(TEXT("16226B")),FLinearColor::Transparent,FMargin(25,10));Header->ShadowOffset=FVector2D::ZeroVector;
    auto* Title=Text(TEXT("WardrobeV2Title"),TEXT("UI.Wardrobe.Title"),LOCTEXT("Title","ГАРДЕРОБ"),48,MCUnifiedWardrobePrivate_C(TEXT("AE4C6C")));Header->SetContentForSlot(TEXT("Content"),Title);
    auto* HeaderBounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("WardrobeV2HeaderBounds"));HeaderBounds->SetMaxDesiredWidth(580);HeaderBounds->SetContent(Header);
    auto* HeaderCell=Body->AddChildToVerticalBox(HeaderBounds);HeaderCell->SetHorizontalAlignment(HAlign_Center);HeaderCell->SetPadding(FMargin(22,0,22,24));
    auto* Coins=Text(TEXT("WardrobeV2Coins"),NAME_None,FText::GetEmpty(),20,MCUnifiedWardrobePrivate_C(TEXT("FFF9F8")));Body->AddChildToVerticalBox(Coins)->SetPadding(FMargin(0,0,0,8));
    auto* GridViewport=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("WardrobeV2GridViewport"));GridViewport->SetHeightOverride(650);Body->AddChildToVerticalBox(GridViewport);
    auto* GridScroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("WardrobeV2GridScroll"));GridScroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);GridViewport->SetContent(GridScroll);
    auto* Grid=WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(),TEXT("WardrobeV2Products"));Grid->SetMinDesiredSlotWidth(194);Grid->SetMinDesiredSlotHeight(190);Grid->SetSlotPadding(FMargin(8,8,12,12));GridScroll->AddChild(Grid);
    auto* Notice=Text(TEXT("WardrobeV2Notice"),NAME_None,FText::GetEmpty(),20,MCUnifiedWardrobePrivate_C(TEXT("FFF9F8")));Body->AddChildToVerticalBox(Notice)->SetPadding(FMargin(10,10));
    auto* Colors=WidgetTree->ConstructWidget<UExpandableArea>(UExpandableArea::StaticClass(),TEXT("WardrobeV2Colors"));
    Colors->SetContentForSlot(TEXT("Header"),Text(TEXT("WardrobeV2ColorHeading"),TEXT("UI.Wardrobe.BagAndBelt"),LOCTEXT("BagAndBelt","Цвет сумки и пояса"),24,MCUnifiedWardrobePrivate_C(TEXT("FFF9F8"))));
    auto* ColorBody=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("WardrobeV2ColorContents"));Colors->SetContentForSlot(TEXT("Body"),ColorBody);Body->AddChildToVerticalBox(Colors)->SetPadding(FMargin(10,10,10,20));
    auto* Swatch=Shape(TEXT("WardrobeV2Swatch"),PreviewScenario==EMCGameplayUIPreviewScenario::Default?MCUnifiedWardrobePrivate_C(TEXT("3DA7FF")):MCUnifiedWardrobePrivate_C(TEXT("FF55AA")),MCUnifiedWardrobePrivate_C(TEXT("FFF9F8")),FLinearColor::Transparent,FMargin(0));Swatch->ShadowOffset=FVector2D::ZeroVector;
    auto* SwatchSize=WidgetTree->ConstructWidget<USizeBox>();SwatchSize->SetHeightOverride(18);SwatchSize->SetContent(Swatch);ColorBody->AddChildToVerticalBox(SwatchSize)->SetPadding(FMargin(0,8));
    auto* Palettes=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("WardrobeV2Palettes"));ColorBody->AddChildToVerticalBox(Palettes)->SetHorizontalAlignment(HAlign_Center);
    for(bool Hue:{false,true}) {
        auto* Palette=WidgetTree->ConstructWidget<UMCWardrobeColorPalette>(UMCWardrobeColorPalette::StaticClass(),Hue?TEXT("WardrobeV2Hue"):TEXT("WardrobeV2Palette"));
        Palette->Mode=Hue?EMCWardrobePaletteMode::Hue:EMCWardrobePaletteMode::SaturationValue;Palette->DesiredSize=FVector2D(Hue?28:216,216);
        auto* Bounds=WidgetTree->ConstructWidget<USizeBox>();Bounds->SetWidthOverride(Hue?28:216);Bounds->SetHeightOverride(216);Bounds->SetContent(Palette);
        Palettes->AddChildToHorizontalBox(Bounds)->SetPadding(FMargin(0,0,Hue?0:16,12));
    }
    ColorBody->AddChildToVerticalBox(Text(TEXT("WardrobeV2ColorHint"),TEXT("UI.Wardrobe.PaletteHint"),LOCTEXT("PaletteHint","Выбери оттенок на полоске и цвет на палитре."),20,MCUnifiedWardrobePrivate_C(TEXT("FFF9F8"))))->SetPadding(FMargin(0,4,0,12));
    auto* Apply=Action(TEXT("WardrobeV2Apply"),TEXT("UI.Wardrobe.Apply"),LOCTEXT("Apply","ПРИМЕНИТЬ"),MCUnifiedWardrobePrivate_C(TEXT("2AA85C")));
    auto* ApplyCell=ColorBody->AddChildToVerticalBox(Apply);ApplyCell->SetHorizontalAlignment(HAlign_Center);ApplyCell->SetPadding(FMargin(0,0,10,16));
    auto* Back=Action(TEXT("WardrobeV2Back"),TEXT("UI.Settings.Back"),LOCTEXT("Back","НАЗАД"),MCUnifiedWardrobePrivate_C(TEXT("A35173")));
    auto* BackCell=Body->AddChildToVerticalBox(Back);BackCell->SetHorizontalAlignment(HAlign_Center);BackCell->SetPadding(FMargin(0,18,10,10));
    auto* RightWidth=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("WardrobeV2DetailWidth"));RightWidth->SetMinDesiredWidth(440);RightWidth->SetMaxDesiredWidth(540);Columns->AddChildToWrapBox(RightWidth)->SetPadding(FMargin(0,145,14,14));
    auto* Detail=Shape(TEXT("WardrobeV2Detail"),MCUnifiedWardrobePrivate_C(TEXT("404243")),MCUnifiedWardrobePrivate_C(TEXT("FFF9F8")),MCUnifiedWardrobePrivate_C(TEXT("FFF9F8")),FMargin(26,32,26,28));RightWidth->SetContent(Detail);
    auto* Details=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("WardrobeV2DetailContents"));Detail->SetContentForSlot(TEXT("Content"),Details);
    auto* IconSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("WardrobeV2DetailIconSize"));IconSize->SetWidthOverride(240);IconSize->SetHeightOverride(220);
    auto* IconFit=WidgetTree->ConstructWidget<UScaleBox>();IconFit->SetStretch(EStretch::ScaleToFit);IconFit->SetStretchDirection(EStretchDirection::Both);
    auto* Icon=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("WardrobeV2DetailIcon"));IconFit->SetContent(Icon);IconSize->SetContent(IconFit);
    auto* IconCell=Details->AddChildToVerticalBox(IconSize);IconCell->SetHorizontalAlignment(HAlign_Center);IconCell->SetPadding(FMargin(0,0,0,14));
    Details->AddChildToVerticalBox(Text(TEXT("WardrobeV2DetailName"),NAME_None,FText::GetEmpty(),32,MCUnifiedWardrobePrivate_C(TEXT("FFF9F8"))))->SetPadding(FMargin(0,0,0,30));
    Details->AddChildToVerticalBox(Text(TEXT("WardrobeV2Description"),NAME_None,FText::GetEmpty(),27,MCUnifiedWardrobePrivate_C(TEXT("FFF9F8"))))->SetPadding(FMargin(0,0,0,32));
    Details->AddChildToVerticalBox(Text(TEXT("WardrobeV2DetailState"),NAME_None,FText::GetEmpty(),20,MCUnifiedWardrobePrivate_C(TEXT("FFF9F8"))))->SetPadding(FMargin(0,0,0,28));
    auto* Primary=Action(TEXT("WardrobeV2Primary"),NAME_None,LOCTEXT("Choose","ВЫБЕРИ АКСЕССУАР"),MCUnifiedWardrobePrivate_C(TEXT("2AA85C")));
    auto* PrimaryCell=Details->AddChildToVerticalBox(Primary);PrimaryCell->SetHorizontalAlignment(HAlign_Center);PrimaryCell->SetPadding(FMargin(0,12,10,10));
}
void UMCUnifiedWardrobeWidget::OverrideArt(FMCUnifiedWardrobeProduct& Product) const
{
    const auto* Art=CosmeticIconArt.Find(Product.ItemID);auto* Texture=Art?(IsDesignTime()?Art->LoadSynchronous():Art->Get()):nullptr;
    if(!Texture) return;Product.Icon=FSlateBrush();Product.Icon.DrawAs=ESlateBrushDrawType::Image;Product.Icon.SetResourceObject(Texture);Product.Icon.ImageSize=FVector2D(Texture->GetSizeX(),Texture->GetSizeY());
}
void UMCUnifiedWardrobeWidget::RebuildGrid()
{
    auto* Grid=WidgetTree?WidgetTree->FindWidget<UUniformGridPanel>(TEXT("WardrobeV2Products")):nullptr;if(!Grid) return;
    Grid->ClearChildren();Tiles.Reset();
    for(int32 Index=0;Index<Products.Num();++Index) {
        auto& View=Tiles.AddDefaulted_GetRef();const FString Prefix=FString::Printf(TEXT("WardrobeV2Product%d"),Index);
        View.Card=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),FName(Prefix));View.Card->Kind=EMCUniversalUIKind::Button;View.Card->Label=FText::GetEmpty();
        View.Card->bCustomColors=true;View.Card->bUseThemeShape=false;View.Card->CornerRadius=20;View.Card->OutlineWidth=0;View.Card->CustomFill=MCUnifiedWardrobePrivate_C(TEXT("404243"));View.Card->CustomOutline=FLinearColor::Transparent;
        View.Card->CustomShadow=MCUnifiedWardrobePrivate_C(TEXT("FFF9F8"));View.Card->ContentPadding=FMargin(12,14);View.Card->ShadowOffset=FVector2D(9,12);View.Card->Initialize();View.Card->BuildPresentationTree();
        auto* Contents=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),FName(Prefix+TEXT("Contents")));View.Card->SetContentForSlot(TEXT("Content"),Contents);
        auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetHeightOverride(125);Size->SetWidthOverride(125);
        auto* Fit=WidgetTree->ConstructWidget<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);
        View.Icon=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(Prefix+TEXT("Icon")));Fit->SetContent(View.Icon);Size->SetContent(Fit);Contents->AddChildToVerticalBox(Size)->SetHorizontalAlignment(HAlign_Center);
        auto Label=[&](FName Name,int32 FontSize) {auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);Text->SetFont(GetResolvedTheme()->MakeFont(FontSize,true));Text->SetColorAndOpacity(MCUnifiedWardrobePrivate_C(TEXT("FFF9F8")));Text->SetAutoWrapText(true);Text->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);Text->SetJustification(ETextJustify::Center);Contents->AddChildToVerticalBox(Text)->SetPadding(FMargin(0,8,0,0));return Text;};
        View.Fallback=Label(FName(Prefix+TEXT("Name")),20);View.Price=Label(FName(Prefix+TEXT("Price")),24);
        auto* GridCell=Grid->AddChildToUniformGrid(View.Card,Index/3,Index%3);GridCell->SetHorizontalAlignment(HAlign_Fill);GridCell->SetVerticalAlignment(VAlign_Fill);
    }
    if(!IsDesignTime()) BindActions();
}
void UMCUnifiedWardrobeWidget::BindActions()
{
    if(IsDesignTime() || !WidgetTree) return;
    TArray<UMCUniversalUIWidget*> Buttons;
    for(const TCHAR* Name:{TEXT("WardrobeV2Apply"),TEXT("WardrobeV2Back"),TEXT("WardrobeV2Primary")}) if(auto* Button=WidgetTree->FindWidget<UMCUniversalUIWidget>(FName(Name))) Buttons.Add(Button);
    for(const auto& Tile:Tiles) if(Tile.Card) Buttons.Add(Tile.Card);
    for(auto* Button:Buttons) for(UMCUnifiedWardrobeBinding* Old:Bindings) if(Old) {Button->OnClicked.RemoveAll(Old);if(auto* Native=MCUnifiedWardrobePrivate_Surface(Button)) Native->OnPressed.RemoveAll(Old);}
    Bindings.Reset();
    auto Bind=[&](UMCUniversalUIWidget* Button,EMCUnifiedWardrobeAction Action,const FMCUnifiedWardrobeProduct* Product) {
        if(!Button) return;auto* Binding=NewObject<UMCUnifiedWardrobeBinding>(this,NAME_None,RF_Transient);Binding->Owner=this;Binding->Action=Action;
        if(Product) {Binding->Slot=Product->Slot;Binding->ItemID=Product->ItemID;Binding->CatalogID=Product->CatalogID;}
        Button->OnClicked.AddUniqueDynamic(Binding,&UMCUnifiedWardrobeBinding::Clicked);if(auto* Native=MCUnifiedWardrobePrivate_Surface(Button)) Native->OnPressed.AddUniqueDynamic(Binding,&UMCUnifiedWardrobeBinding::Pressed);Bindings.Add(Binding);
    };
    Bind(WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("WardrobeV2Apply")),EMCUnifiedWardrobeAction::Apply,nullptr);
    Bind(WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("WardrobeV2Back")),EMCUnifiedWardrobeAction::Back,nullptr);
    Bind(WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("WardrobeV2Primary")),EMCUnifiedWardrobeAction::Primary,nullptr);
    for(int32 Index=0;Index<Tiles.Num() && Index<Products.Num();++Index) Bind(Tiles[Index].Card,EMCUnifiedWardrobeAction::Select,&Products[Index]);
    for(const TCHAR* Name:{TEXT("WardrobeV2Palette"),TEXT("WardrobeV2Hue")}) if(auto* Palette=WidgetTree->FindWidget<UMCWardrobeColorPalette>(FName(Name))) Palette->OnHSVChanged.AddUniqueDynamic(this,&ThisClass::PaletteChanged);
}
void UMCUnifiedWardrobeWidget::SetBackend(UMCHubWardrobeWidget* Source)
{
    if(Backend.Get()!=Source) {Backend=Source;SelectedSlot=NAME_None;SelectedItem=NAME_None;Products.Reset();Tiles.Reset();}
    BuildPresentationTree();if(!IsDesignTime()) {if(Bindings.IsEmpty()) BindActions();RefreshFromBackend();}
}
void UMCUnifiedWardrobeWidget::RefreshFromBackend()
{
    if(IsDesignTime() || !Backend.IsValid() || !WidgetTree) return;
    TArray<FMCUnifiedWardrobeProduct> Next;
    auto* Economy=Backend->GetUnifiedEconomy();
    for(UMCCosmeticItemWidget* Source:Backend->GetUnifiedCosmeticTiles()) if(Source) {
        auto& Entry=Next.AddDefaulted_GetRef();Entry.Slot=Source->GetItemSlot();Entry.ItemID=Source->GetItemId();Entry.CatalogID=Source->GetCatalogId();Entry.Source=Source;
        Entry.Name=MCUnifiedWardrobePrivate_SourceText(Source,TEXT("CosmeticName"));Entry.Description=MCUnifiedWardrobePrivate_SourceText(Source,TEXT("CosmeticDescription"));Entry.Price=MCUnifiedWardrobePrivate_SourceText(Source,TEXT("CosmeticPrice"));Entry.State=MCUnifiedWardrobePrivate_SourceText(Source,TEXT("CosmeticState"));
        Entry.bOwned=Entry.ItemID.IsNone() || (Economy && Economy->OwnsCosmetic(Entry.ItemID));FText Error;Entry.bAffordable=Economy && !Entry.CatalogID.IsNone() && Economy->CanPurchase(Entry.CatalogID,Error);
        Entry.bDraftSelected=Backend->GetDraftLoadout().ItemForSlot(Entry.Slot)==Entry.ItemID;
        if(auto* Image=Source->WidgetTree?Source->WidgetTree->FindWidget<UImage>(TEXT("CosmeticThumbnail")):nullptr) Entry.Icon=Image->GetBrush();
        OverrideArt(Entry);
    }
    bool Rebuild=Next.Num()!=Products.Num() || Tiles.Num()!=Next.Num();
    if(!Rebuild) for(int32 I=0;I<Next.Num();++I) if(Next[I].Slot!=Products[I].Slot || Next[I].ItemID!=Products[I].ItemID || Next[I].CatalogID!=Products[I].CatalogID) {Rebuild=true;break;}
    Products=MoveTemp(Next);if(!SelectedProduct() && Products.Num()) {const auto* Pick=Products.FindByPredicate([](const FMCUnifiedWardrobeProduct& P){return P.bDraftSelected && !P.ItemID.IsNone();});if(!Pick) Pick=&Products[0];SelectedSlot=Pick->Slot;SelectedItem=Pick->ItemID;}
    if(Rebuild) RebuildGrid();Present();
    auto Copy=[&](FName From,FName To) {auto* Source=Backend->WidgetTree?Backend->WidgetTree->FindWidget<UTextBlock>(From):nullptr;auto* View=WidgetTree->FindWidget<UTextBlock>(To);if(Source && View) {View->SetText(Source->GetText());View->SetVisibility(Source->GetText().IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);}};
    Copy(TEXT("WardrobeCoins"),TEXT("WardrobeV2Coins"));Copy(TEXT("WardrobeTransactionMessage"),TEXT("WardrobeV2Notice"));
    const FLinearColor Color=Backend->GetDraftColor();const FLinearColor HSV=Color.LinearRGBToHSV();
    for(const TCHAR* Name:{TEXT("WardrobeV2Palette"),TEXT("WardrobeV2Hue")}) if(auto* Palette=WidgetTree->FindWidget<UMCWardrobeColorPalette>(FName(Name))) Palette->SetHSV(FVector(HSV.R/360.f,HSV.G,HSV.B));
    if(auto* Swatch=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("WardrobeV2Swatch"))) {Swatch->CustomFill=Color;Swatch->ApplyTheme();}
}
const FMCUnifiedWardrobeProduct* UMCUnifiedWardrobeWidget::SelectedProduct() const
{return Products.FindByPredicate([this](const FMCUnifiedWardrobeProduct& Product){return Product.Slot==SelectedSlot && Product.ItemID==SelectedItem;});}
void UMCUnifiedWardrobeWidget::Present()
{
    if(!WidgetTree) return;const auto* Selected=SelectedProduct();
    for(int32 I=0;I<Tiles.Num() && I<Products.Num();++I) {
        const auto& Product=Products[I];auto& View=Tiles[I];const bool HasIcon=Product.Icon.GetResourceObject()!=nullptr;
        if(View.Icon) {View.Icon->SetBrush(Product.Icon);View.Icon->SetVisibility(HasIcon?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);}
        if(View.Fallback) {View.Fallback->SetText(Product.Name);View.Fallback->SetVisibility(HasIcon?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);}
        if(View.Price) View.Price->SetText(Product.Price.IsEmpty()?Product.State:Product.Price);
        if(View.Card) {View.Card->CustomFill=Selected==&Product?MCUnifiedWardrobePrivate_C(TEXT("AE4C6C")):MCUnifiedWardrobePrivate_C(TEXT("404243"));View.Card->SetToolTipText(Product.Description.IsEmpty()?Product.Name:FText::Format(FText::AsCultureInvariant(TEXT("{0}\n{1}")),Product.Name,Product.Description));View.Card->ApplyTheme();}
    }
    auto Set=[&](FName Name,const FText& Text) {if(auto* Target=WidgetTree->FindWidget<UTextBlock>(Name)) Target->SetText(Text);};
    Set(TEXT("WardrobeV2DetailName"),Selected?Selected->Name:FText::GetEmpty());Set(TEXT("WardrobeV2Description"),Selected?Selected->Description:FText::GetEmpty());Set(TEXT("WardrobeV2DetailState"),Selected?Selected->State:FText::GetEmpty());
    if(auto* Image=WidgetTree->FindWidget<UImage>(TEXT("WardrobeV2DetailIcon"))) {Image->SetBrush(Selected?Selected->Icon:FSlateBrush());Image->SetVisibility(Selected && Selected->Icon.GetResourceObject()?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);}
    if(auto* Primary=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("WardrobeV2Primary"))) {
        Primary->LabelKey=NAME_None;
        Primary->Label=!Selected?Localized(TEXT("UI.Wardrobe.NoAccessory"),LOCTEXT("NoAccessory","Без аксессуара")):
            Selected->bOwned?Localized(TEXT("UI.Wardrobe.Apply"),LOCTEXT("Apply","ПРИМЕНИТЬ")):
            !Selected->bAffordable?Localized(TEXT("UI.Product.Insufficient"),LOCTEXT("Insufficient","Мало монет")):
            FText::Format(FText::AsCultureInvariant(TEXT("{0} · {1}")),Localized(TEXT("UI.Product.Buy"),LOCTEXT("Buy","КУПИТЬ")),Selected->Price);
        Primary->SetIsEnabled(Selected && (Selected->bOwned || Selected->bAffordable));Primary->ApplyTheme();
        if(Primary->WidgetTree) if(auto* Caption=Primary->WidgetTree->FindWidget<UTextBlock>(TEXT("UniversalLabel"))) {
            Caption->SetAutoWrapText(false);Caption->SetWrapTextAt(340.f);
        }
    }
}
void UMCUnifiedWardrobeWidget::SelectProduct(FName InSlot,FName ItemID,FName CatalogID)
{
    if(IsDesignTime() || !Backend.IsValid()) return;
    auto* Product=Products.FindByPredicate([&](const FMCUnifiedWardrobeProduct& P){return P.Slot==InSlot && P.ItemID==ItemID && P.CatalogID==CatalogID;});
    if(!Product || !Product->Source.IsValid()) return;
    SelectedSlot=InSlot;SelectedItem=ItemID;Product->Source->OnPressed.Broadcast(InSlot,ItemID,CatalogID);RefreshFromBackend();
}
void UMCUnifiedWardrobeWidget::ActivateProduct(FName InSlot,FName ItemID,FName CatalogID,TOptional<bool> ExpectedOwned)
{
    if(IsDesignTime() || !Backend.IsValid()) return;
    const auto* Product=Products.FindByPredicate([&](const FMCUnifiedWardrobeProduct& P){return P.Slot==InSlot && P.ItemID==ItemID && P.CatalogID==CatalogID;});
    if(!Product || !Product->Source.IsValid() || (ExpectedOwned.IsSet() && Product->bOwned!=ExpectedOwned.GetValue())) return;
    if(Product->bOwned) {Product->Source->OnPressed.Broadcast(InSlot,ItemID,CatalogID);ForwardButton(TEXT("WardrobeApply"));}
    else if(Product->bAffordable) Product->Source->OnPurchaseRequested.Broadcast(CatalogID);
    RefreshFromBackend();
}
void UMCUnifiedWardrobeWidget::ForwardButton(FName Name)
{
    if(IsDesignTime() || !Backend.IsValid() || !Backend->WidgetTree) return;
    if(auto* Button=Backend->WidgetTree->FindWidget<UButton>(Name);Button && Button->GetIsEnabled()) Button->OnClicked.Broadcast();
}
void UMCUnifiedWardrobeWidget::RequestBack() {ForwardButton(TEXT("WardrobeCancel"));}
void UMCUnifiedWardrobeWidget::PaletteChanged(FVector HSV)
{
    if(IsDesignTime() || bSynchronizing || !Backend.IsValid() || !Backend->WidgetTree || HSV.ContainsNaN()) return;
    if(auto* Palette=Backend->WidgetTree->FindWidget<UMCWardrobeColorPalette>(TEXT("WardrobeColorPalette"))) Palette->OnHSVChanged.Broadcast(HSV);
    RefreshFromBackend();
}
void UMCUnifiedWardrobeWidget::ApplyEditorPreview(float Time)
{
    Super::ApplyEditorPreview(Time);BuildPresentationTree();if(!IsDesignTime()) return;
    Products.Reset();const auto Store=GetDefault<UMCHubEconomyComponent>()->GetCatalog();
    for(const auto& Item:Store) if(Item.Definition.Kind==EMCHubCatalogKind::Cosmetic) {
        const auto* Cosmetic=UMCCosmeticCatalog::Find(UMCCosmeticCatalog::GetDefaultCatalog(),Item.Definition.CosmeticID);if(!Cosmetic || !Cosmetic->bEnabled) continue;
        auto& Product=Products.AddDefaulted_GetRef();Product.Slot=Cosmetic->Slot;Product.ItemID=Item.Definition.CosmeticID;Product.CatalogID=Item.ItemID;
        Product.Name=Localized(Item.Definition.DisplayNameKey,Item.Definition.DisplayName.IsEmpty()?Cosmetic->DisplayName:Item.Definition.DisplayName);Product.Description=Localized(Item.Definition.DescriptionKey,Item.Definition.Description);
        Product.Price=FText::Format(Localized(TEXT("UI.Product.Coins"),LOCTEXT("Coins","{0} монет")),UMCUIDataLibrary::NumberForCulture(Item.Definition.Price,PreviewCulture));
        Product.bAffordable=PreviewScenario!=EMCGameplayUIPreviewScenario::Disabled;
        if(auto* Texture=Item.Definition.Icon.LoadSynchronous()) {Product.Icon.DrawAs=ESlateBrushDrawType::Image;Product.Icon.SetResourceObject(Texture);Product.Icon.ImageSize=FVector2D(Texture->GetSizeX(),Texture->GetSizeY());}
        OverrideArt(Product);
    }
    if(Products.Num()) {const auto* Pick=Products.FindByPredicate([this](const FMCUnifiedWardrobeProduct& P){return P.ItemID==PreviewCosmeticID;});if(!Pick) Pick=&Products[0];SelectedSlot=Pick->Slot;SelectedItem=Pick->ItemID;}
    if(Tiles.Num()!=Products.Num()) RebuildGrid();Present();RefreshHeaderGeometry();
}
UWidget* UMCUnifiedWardrobeWidget::GetDefaultFocusTarget() const
{
    for(const auto& Tile:Tiles) if(auto* Button=MCUnifiedWardrobePrivate_Surface(Tile.Card)) return Button;
    return WidgetTree?MCUnifiedWardrobePrivate_Surface(WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("WardrobeV2Back"))):nullptr;
}
void UMCUnifiedWardrobeWidget::NativeConstruct() {Super::NativeConstruct();BindActions();RefreshFromBackend();}
void UMCUnifiedWardrobeWidget::ApplyTheme() {Super::ApplyTheme();if(!IsDesignTime()) Present();}
void UMCUnifiedWardrobeWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds) {Super::NativeTick(Geometry,DeltaSeconds);RefreshHeaderGeometry();}
void UMCUnifiedWardrobeWidget::RefreshHeaderGeometry()
{
    if(!bAdaptHeaderContour || !WidgetTree) return;
    auto* Panel=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("WardrobeV2LeftPanel"));auto* Header=WidgetTree->FindWidget(TEXT("WardrobeV2Header"));if(!Panel || !Header) return;
    const auto& Geometry=Panel->GetPaintSpaceGeometry();const auto& HeaderGeometry=Header->GetPaintSpaceGeometry();const FVector2D Size=Geometry.GetLocalSize();if(Size.X<=1 || Size.Y<=1) return;
    const auto Min=Geometry.AbsoluteToLocal(HeaderGeometry.LocalToAbsolute(FVector2D::ZeroVector));
    const auto Max=Geometry.AbsoluteToLocal(HeaderGeometry.LocalToAbsolute(HeaderGeometry.GetLocalSize()));
    const double X=FMath::Clamp((Min.X-24.)/Size.X,.02,.4),Right=FMath::Clamp((Max.X+24.)/Size.X,.6,.98),Y=FMath::Clamp((Max.Y+26.)/Size.Y,.02,.8);
    const TArray<FVector2D> Points={{X,0},{Right,0},{Right,Y},{1,Y},{1,1},{0,1},{0,Y},{X,Y}};
    bool Changed=Panel->ContourPoints.Num()!=Points.Num();if(!Changed) for(int32 I=0;I<Points.Num();++I) if(!Panel->ContourPoints[I].Equals(Points[I],.001)) {Changed=true;break;}
    if(Changed) {Panel->ContourPoints=Points;Panel->ApplyTheme();}
}
FReply UMCUnifiedWardrobeWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if(Event.GetKey()==EKeys::Escape || Event.GetKey()==EKeys::Gamepad_FaceButton_Right) {RequestBack();return FReply::Handled();}
    return Super::NativeOnPreviewKeyDown(Geometry,Event);
}

#undef LOCTEXT_NAMESPACE
