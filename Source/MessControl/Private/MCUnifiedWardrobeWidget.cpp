#include "MCUnifiedWardrobeWidget.h"

#include "MCHubWardrobeWidget.h"
#include "MCCosmeticItemWidget.h"
#include "MCCosmeticCatalog.h"
#include "MCHubEconomyComponent.h"
#include "MCGameplayUIFactory.h"
#include "MCUIDataLibrary.h"
#include "MCUniversalUIWidget.h"
#include "MCUIGridScrollBox.h"
#include "UObject/UObjectGlobals.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Widgets/SWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"
#include "Internationalization/BreakIterator.h"
#include "Internationalization/IBreakIterator.h"
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
#include "Components/SizeBoxSlot.h"
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
    float MCUnifiedWardrobePrivate_BalancedCaptionWidth(const UTextBlock* Label,float Available,const FSlateFontMeasure& Measure)
    {
        if(!Label || Available<=1.f) return Available;
        const FString Copy=Label->GetText().ToString();
        const float Natural=float(Measure.Measure(Copy,Label->GetFont()).X);
        if(Copy.IsEmpty() || Copy.Len()>64 || Natural<=Available) return Available;
        const auto Iterator=FBreakIterator::CreateLineBreakIterator();Iterator->SetString(Copy);
        TArray<int32> Ends;Iterator->ResetToBeginning();
        for(int32 End=Iterator->MoveToNext();End!=INDEX_NONE;End=Iterator->MoveToNext()) if(End>0) Ends.Add(End);
        if(Ends.IsEmpty() || Ends.Num()>32) return Available;
        TArray<float> Candidates;Candidates.Add(Available);
        for(int32 End:Ends) {
            const FString Prefix=Copy.Left(End).TrimStartAndEnd();
            const float Width=float(Measure.Measure(Prefix,Label->GetFont()).X)+1.f;
            if(Width>1.f && Width<Available) Candidates.Add(Width);
        }
        const int32 MinimumLines=FMath::Max(2,FMath::CeilToInt(Natural/Available));
        float BestWidth=Available;double BestScore=TNumericLimits<double>::Max();
        for(float Width:Candidates) {
            int32 Start=0,LineCount=0,Orphans=0;float Shortest=Available,Longest=0.f;bool Valid=true;
            while(Start<Copy.Len()) {
                int32 LineEnd=INDEX_NONE;float LineWidth=0.f;
                for(int32 End:Ends) if(End>Start) {
                    const FString Line=Copy.Mid(Start,End-Start).TrimStartAndEnd();
                    const float Extent=float(Measure.Measure(Line,Label->GetFont()).X);
                    if(Extent>Width) break;
                    LineEnd=End;LineWidth=Extent;
                }
                if(LineEnd==INDEX_NONE) {Valid=false;break;}
                const FString Line=Copy.Mid(Start,LineEnd-Start).TrimStartAndEnd();
                if(!Line.IsEmpty()) {++LineCount;Orphans+=Line.Len()==1?1:0;Shortest=FMath::Min(Shortest,LineWidth);Longest=FMath::Max(Longest,LineWidth);}
                Start=LineEnd;
            }
            if(!Valid || LineCount==0) continue;
            const double Unit=double(Available)*Available;
            const double Score=FMath::Square(double(Longest-Shortest))+double(FMath::Max(0,LineCount-MinimumLines))*Unit*2.+double(Orphans)*Unit*16.;
            if(Score<BestScore || (FMath::IsNearlyEqual(Score,BestScore) && Width>BestWidth)) {BestScore=Score;BestWidth=Width;}
        }
        // Only the layout width changes; the source FText and fixed font remain intact.
        return BestWidth;
    }
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
        Result->SetColorAndOpacity(Tint);Result->SetAutoWrapText(true);Result->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);Result->SetJustification(ETextJustify::Center);
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
    auto* GridScroll=WidgetTree->ConstructWidget<UMCUIGridScrollBox>(UMCUIGridScrollBox::StaticClass(),TEXT("WardrobeV2GridScroll"));GridScroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);GridViewport->SetContent(GridScroll);
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
    // Migrated rows own their mandatory icon. PSD art stays the legacy/fixture fallback only.
    const auto* ClothingTable=FindObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_CharacterCosmetics.DT_CharacterCosmetics"));
    const auto* Clothing=UMCCosmeticCatalog::Find(ClothingTable,Product.ItemID);
    const bool Canonical=Clothing && Clothing->bUseCanonicalCommerce;
    const auto* Art=Canonical?&Clothing->Icon:CosmeticIconArt.Find(Product.ItemID);
    auto* Texture=Art?(IsDesignTime()?Art->LoadSynchronous():Art->Get()):nullptr;
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
        auto* Size=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(Prefix+TEXT("IconBounds")));View.IconBounds=Size;Size->SetHeightOverride(125);Size->SetWidthOverride(125);
        auto* Fit=WidgetTree->ConstructWidget<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);
        View.Icon=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(Prefix+TEXT("Icon")));Fit->SetContent(View.Icon);Size->SetContent(Fit);Contents->AddChildToVerticalBox(Size)->SetHorizontalAlignment(HAlign_Center);
        auto Label=[&](FName Name,int32 FontSize) {auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);Text->SetFont(GetResolvedTheme()->MakeFont(FontSize,true));Text->SetColorAndOpacity(MCUnifiedWardrobePrivate_C(TEXT("FFF9F8")));Text->SetAutoWrapText(true);Text->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);Text->SetJustification(ETextJustify::Center);Contents->AddChildToVerticalBox(Text)->SetPadding(FMargin(0,8,0,0));return Text;};
        View.Fallback=Label(FName(Prefix+TEXT("Name")),20);View.Price=Label(FName(Prefix+TEXT("Price")),24);
        View.Bounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(Prefix+TEXT("Bounds")));View.Bounds->SetContent(View.Card);
        auto* GridCell=Grid->AddChildToUniformGrid(View.Bounds,Index/3,Index%3);GridCell->SetHorizontalAlignment(HAlign_Fill);GridCell->SetVerticalAlignment(VAlign_Fill);
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
        if(View.IconBounds) View.IconBounds->SetVisibility(HasIcon?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
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
            const auto* Width=WidgetTree->FindWidget<USizeBox>(TEXT("WardrobeV2DetailWidth"));
            const float Available=Width?Width->GetWidthOverride():440.f;
            Caption->SetAutoWrapText(false);Caption->SetWrapTextAt(FMath::Max(60.f,Available-Primary->ContentPadding.Left-Primary->ContentPadding.Right-56.f));
            Caption->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
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
    if(Tiles.Num()!=Products.Num()) RebuildGrid();Present();RefreshAdaptiveLayout();RefreshHeaderGeometry();
}
UWidget* UMCUnifiedWardrobeWidget::GetDefaultFocusTarget() const
{
    for(const auto& Tile:Tiles) if(auto* Button=MCUnifiedWardrobePrivate_Surface(Tile.Card)) return Button;
    return WidgetTree?MCUnifiedWardrobePrivate_Surface(WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("WardrobeV2Back"))):nullptr;
}
TSharedRef<SWidget> UMCUnifiedWardrobeWidget::RebuildWidget()
{
    // Upgrade saved named containers before Super creates their Slate slots.
    // Late Designer reparenting may leave old SVerticalBox slots painting ghosts.
    if(!WidgetTree) Initialize();BuildPresentationTree();EnsureAdaptiveTree();
    return Super::RebuildWidget();
}
void UMCUnifiedWardrobeWidget::NativeConstruct() {Super::NativeConstruct();BindActions();RefreshFromBackend();RefreshAdaptiveLayout();}
void UMCUnifiedWardrobeWidget::ApplyTheme() {Super::ApplyTheme();if(!IsDesignTime()) Present();LastAdaptiveSize=FVector2D::ZeroVector;RefreshAdaptiveLayout();}
void UMCUnifiedWardrobeWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds) {Super::NativeTick(Geometry,DeltaSeconds);TickLayoutSize=Geometry.GetLocalSize();RefreshAdaptiveLayout();RefreshHeaderGeometry();}
bool UMCUnifiedWardrobeWidget::EnsureAdaptiveTree()
{
    if(bAdaptiveTreeReady) return true;
    auto* Root=WidgetTree?Cast<UOverlay>(WidgetTree->RootWidget):nullptr;
    auto* LeftWidth=WidgetTree?WidgetTree->FindWidget<USizeBox>(TEXT("WardrobeV2LeftWidth")):nullptr;
    auto* DetailWidth=WidgetTree?WidgetTree->FindWidget<USizeBox>(TEXT("WardrobeV2DetailWidth")):nullptr;
    auto* LeftContents=WidgetTree?WidgetTree->FindWidget<UVerticalBox>(TEXT("WardrobeV2LeftContents")):nullptr;
    auto* Details=WidgetTree?WidgetTree->FindWidget<UVerticalBox>(TEXT("WardrobeV2DetailContents")):nullptr;
    auto* Primary=WidgetTree?WidgetTree->FindWidget(TEXT("WardrobeV2Primary")):nullptr;
    auto* Colors=WidgetTree?WidgetTree->FindWidget<UExpandableArea>(TEXT("WardrobeV2Colors")):nullptr;
    auto* ColorBody=WidgetTree?WidgetTree->FindWidget<UVerticalBox>(TEXT("WardrobeV2ColorContents")):nullptr;
    if(!Root || !LeftWidth || !DetailWidth || !LeftContents || !Details || !Primary || !Colors || !ColorBody) return false;
    auto* Canvas=WidgetTree->FindWidget<UCanvasPanel>(TEXT("WardrobeV2AdaptiveCanvas"));
    if(!Canvas) {
        if(WidgetTree->FindWidget(TEXT("WardrobeV2AdaptiveCanvas"))) return false;
        Canvas=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("WardrobeV2AdaptiveCanvas"));
        auto* Cell=Root->AddChildToOverlay(Canvas);Cell->SetHorizontalAlignment(HAlign_Fill);Cell->SetVerticalAlignment(VAlign_Fill);
    }
    // Reparent only this task's named containers, retaining source controls/graphs and identities.
    for(auto* Width:{LeftWidth,DetailWidth}) if(Width->GetParent()!=Canvas) {
        Width->RemoveFromParent();Canvas->AddChildToCanvas(Width);
        Width->SetMinDesiredWidth(0);Width->ClearMaxDesiredWidth();
    }
    if(auto* OldFrame=WidgetTree->FindWidget(TEXT("WardrobeV2DesignSize"))) OldFrame->SetVisibility(ESlateVisibility::Collapsed);
    if(auto* GridViewport=WidgetTree->FindWidget<USizeBox>(TEXT("WardrobeV2GridViewport"))) {
        auto* OldScroll=Cast<UScrollBox>(GridViewport->GetContent());
        if(OldScroll && !OldScroll->IsA<UMCUIGridScrollBox>()) {
            auto* Grid=WidgetTree->FindWidget<UUniformGridPanel>(TEXT("WardrobeV2Products"));
            if(!Grid || Grid->GetParent()!=OldScroll) return false;
            OldScroll->RemoveChild(Grid);GridViewport->SetContent(nullptr);
            const FName RetiredName=MakeUniqueObjectName(WidgetTree,UScrollBox::StaticClass(),TEXT("WardrobeV2LegacyGridScroll"));
            OldScroll->Rename(*RetiredName.ToString(),WidgetTree,REN_DontCreateRedirectors|REN_NonTransactional);
            auto* Snap=WidgetTree->ConstructWidget<UMCUIGridScrollBox>(UMCUIGridScrollBox::StaticClass(),TEXT("WardrobeV2GridScroll"));
            Snap->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);Snap->SetScrollbarThickness(FVector2D(5,5));
            Snap->AddChild(Grid);GridViewport->SetContent(Snap);
        }
        GridViewport->ClearHeightOverride();
        if(auto* Cell=Cast<UVerticalBoxSlot>(GridViewport->Slot)) Cell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }
    if(!WidgetTree->FindWidget(TEXT("WardrobeV2DetailViewport"))) {
        Primary->RemoveFromParent();
        if(auto* OldSpacer=WidgetTree->FindWidget(TEXT("WardrobeV2DetailSpacer"))) OldSpacer->RemoveFromParent();
        auto* DetailViewport=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("WardrobeV2DetailViewport"));
        auto* DetailColumn=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("WardrobeV2DetailTextColumn"));
        DetailViewport->SetContent(DetailColumn);
        for(const TCHAR* Name:{TEXT("WardrobeV2DetailIconSize"),TEXT("WardrobeV2DetailName"),TEXT("WardrobeV2Description"),TEXT("WardrobeV2DetailState")})
            if(auto* Child=WidgetTree->FindWidget(Name)) {Child->RemoveFromParent();DetailColumn->AddChildToVerticalBox(Child)->SetPadding(FMargin(0,0,0,12));}
        Details->AddChildToVerticalBox(DetailViewport)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* Cell=Details->AddChildToVerticalBox(Primary);Cell->SetHorizontalAlignment(HAlign_Center);Cell->SetPadding(FMargin(0,8,0,0));
    }
    if(auto* DetailViewport=WidgetTree->FindWidget<USizeBox>(TEXT("WardrobeV2DetailViewport"))) {
        if(auto* OldScroll=Cast<UScrollBox>(DetailViewport->GetContent());OldScroll && OldScroll->GetChildrenCount()==1)
            if(auto* DetailColumn=Cast<UVerticalBox>(OldScroll->GetChildAt(0))) {
                // Keep the complete authored description, without a text-scroll
                // input target, scrollbar or inherited Slate overflow shadows.
                DetailColumn->RemoveFromParent();DetailViewport->SetContent(DetailColumn);
            }
        if(auto* DetailColumn=Cast<UVerticalBox>(DetailViewport->GetContent()))
            if(auto* Cell=Cast<USizeBoxSlot>(DetailColumn->Slot)) {Cell->SetHorizontalAlignment(HAlign_Fill);Cell->SetVerticalAlignment(VAlign_Top);}
    }
    auto* Popup=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("WardrobeV2ColorPopup"));
    if(!Popup) {
        if(WidgetTree->FindWidget(TEXT("WardrobeV2ColorPopup"))) return false;
        Colors->SetContentForSlot(TEXT("Body"),nullptr);ColorBody->RemoveFromParent();
        Popup=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),TEXT("WardrobeV2ColorPopup"));
        Popup->Theme=GetResolvedTheme();Popup->Kind=EMCUniversalUIKind::Panel;Popup->bCustomColors=true;Popup->bUseThemeShape=false;
        Popup->CustomFill=MCUnifiedWardrobePrivate_C(TEXT("404243"));Popup->CustomOutline=MCUnifiedWardrobePrivate_C(TEXT("FFF9F8"));
        Popup->CustomShadow=MCUnifiedWardrobePrivate_C(TEXT("16226B"));Popup->ContentPadding=FMargin(16);Popup->CornerRadius=20;
        Popup->Initialize();Popup->BuildPresentationTree();Popup->SetContentForSlot(TEXT("Content"),ColorBody);Popup->ApplyTheme();
        Canvas->AddChildToCanvas(Popup)->SetZOrder(2);Popup->SetVisibility(ESlateVisibility::Collapsed);
    }
    bAdaptiveTreeReady=true;return true;
}
void UMCUnifiedWardrobeWidget::RefreshAdaptiveLayout()
{
    if(bRefreshingAdaptiveLayout || !WidgetTree) return;
    TGuardValue<bool> LayoutGuard(bRefreshingAdaptiveLayout,true);
    if(!EnsureAdaptiveTree()) return;
    FVector2D Area=GetPaintSpaceGeometry().GetLocalSize();if(Area.X<=1 || Area.Y<=1) Area=TickLayoutSize;
    if(Area.X<=1 || Area.Y<=1) return;
    auto* Title=WidgetTree->FindWidget<UTextBlock>(TEXT("WardrobeV2Title"));
    if(Title && WardrobeTitleFont.HasValidFont()) Title->SetFont(WardrobeTitleFont);
    auto* Colors=WidgetTree->FindWidget<UExpandableArea>(TEXT("WardrobeV2Colors"));
    auto* Primary=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("WardrobeV2Primary"));
    FString Copy;
    for(const TCHAR* Name:{TEXT("WardrobeV2Title"),TEXT("WardrobeV2DetailName"),TEXT("WardrobeV2Description"),TEXT("WardrobeV2DetailState"),TEXT("WardrobeV2Notice")})
        if(const auto* Text=WidgetTree->FindWidget<UTextBlock>(Name)) Copy+=Text->GetText().ToString()+TEXT("|");
    if(Primary) Copy+=Primary->Label.ToString();
    for(const auto& Tile:Tiles) {if(Tile.Fallback) Copy+=Tile.Fallback->GetText().ToString()+TEXT("|");if(Tile.Price) Copy+=Tile.Price->GetText().ToString()+TEXT("|");}
    const bool Expanded=Colors && Colors->GetIsExpanded();
    const auto* GridViewport=WidgetTree->FindWidget<USizeBox>(TEXT("WardrobeV2GridViewport"));
    const FVector2D GridArea=GridViewport?GridViewport->GetPaintSpaceGeometry().GetLocalSize():FVector2D::ZeroVector;
    if(Area.Equals(LastAdaptiveSize,.5) && GridArea.Equals(LastAdaptiveGridSize,.5) && Copy==LastAdaptiveCopy && Tiles.Num()==LastAdaptiveTileCount && Expanded==bLastPaletteExpanded) {
        if(auto* Snap=WidgetTree->FindWidget<UMCUIGridScrollBox>(TEXT("WardrobeV2GridScroll")))
            Snap->ConfigureGrid(WidgetTree->FindWidget<UUniformGridPanel>(TEXT("WardrobeV2Products")),3,3,GridArea.Y>1?float(GridArea.Y)/3.f:0.f);
        return;
    }
    LastAdaptiveSize=Area;LastAdaptiveGridSize=GridArea;LastAdaptiveCopy=Copy;LastAdaptiveTileCount=Tiles.Num();bLastPaletteExpanded=Expanded;
    const float Scale=FMath::Max(.01f,GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale());
    auto Measure=[Scale](UWidget* Widget){if(const auto Slate=Widget?Widget->GetCachedWidget():nullptr) {Slate->MarkPrepassAsDirty();Slate->SlatePrepass(Scale);}return Widget?Widget->GetDesiredSize():FVector2D::ZeroVector;};
    float TitleWidth=0;
    if(Title) {Title->SetAutoWrapText(false);Title->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);Title->SetWrapTextAt(0);TitleWidth=float(Measure(Title).X);}
    // Measure native line-break segments at the actual fixed font. An ordinary
    // German compound must widen the detail card instead of leaving one letter
    // on another line. CJK retains its normal Unicode line-break opportunities.
    float WidestDetailSegment=0.f,WidestTilePrice=0.f,PreferredDetailCopy=0.f;
    if(FSlateApplication::IsInitialized()) if(const auto Renderer=FSlateApplication::Get().GetRenderer()) {
        const auto FontMeasure=Renderer->GetFontMeasureService();
        // Price is a short number/unit caption. Give the three-column grid the
        // natural width when available, so CJK does not consume half a unit on
        // the amount line and leave one character on the following line.
        for(const auto& Tile:Tiles) {
            if(Tile.Price) WidestTilePrice=FMath::Max(WidestTilePrice,float(FontMeasure->Measure(Tile.Price->GetText(),Tile.Price->GetFont()).X));
            if(Tile.Fallback && Tile.Fallback->GetVisibility()!=ESlateVisibility::Collapsed)
                WidestTilePrice=FMath::Max(WidestTilePrice,float(FontMeasure->Measure(Tile.Fallback->GetText(),Tile.Fallback->GetFont()).X));
        }
        if(const auto* Name=WidgetTree->FindWidget<UTextBlock>(TEXT("WardrobeV2DetailName"))) {
            const float Natural=float(FontMeasure->Measure(Name->GetText(),Name->GetFont()).X)+60.f;
            if(Natural<float(Area.X)*.45f) PreferredDetailCopy=Natural;
        }
        if(Primary && Primary->WidgetTree) if(const auto* Caption=Primary->WidgetTree->FindWidget<UTextBlock>(TEXT("UniversalLabel")))
            PreferredDetailCopy=FMath::Max(PreferredDetailCopy,float(FontMeasure->Measure(Caption->GetText(),Caption->GetFont()).X)+Primary->ContentPadding.Left+Primary->ContentPadding.Right+60.f);
        for(const TCHAR* Name:{TEXT("WardrobeV2DetailName"),TEXT("WardrobeV2Description"),TEXT("WardrobeV2DetailState")})
            if(const auto* Text=WidgetTree->FindWidget<UTextBlock>(Name)) {
                const FString FullCopy=Text->GetText().ToString();
                const auto Breaks=FBreakIterator::CreateLineBreakIterator();Breaks->SetString(FullCopy);
                int32 SegmentStart=Breaks->ResetToBeginning();
                for(int32 SegmentEnd=Breaks->MoveToNext();SegmentEnd!=INDEX_NONE;SegmentEnd=Breaks->MoveToNext()) {
                    const FString Segment=FullCopy.Mid(SegmentStart,SegmentEnd-SegmentStart).TrimStartAndEnd();
                    if(!Segment.IsEmpty()) WidestDetailSegment=FMath::Max(WidestDetailSegment,float(FontMeasure->Measure(Segment,Text->GetFont()).X));
                    SegmentStart=SegmentEnd;
                }
            }
    }
    const float Gap=16.f;
    const float MinimumLeft=FMath::Min(float(Area.X)-32.f,TitleWidth+72.f);
    // The PSD's detached description is about two thirds of the catalog width.
    // Let complete words/actions widen it when a translation needs more space.
    const float DetailReferenceWidth=FMath::Min(530.f,FMath::Max(220.f,FMath::Min(760.f,float(Area.X)*.42f)*.68f));
    const float DesiredDetail=FMath::Max3(DetailReferenceWidth,WidestDetailSegment+60.f,PreferredDetailCopy);
    const float PreviewReserve=FMath::Max(160.f,float(Area.X)*.18f);
    const float PriceGridWidth=(WidestTilePrice+16.f+17.f)*3.f+40.f;
    const float PreferredLeft=FMath::Min(float(Area.X)-32.f,FMath::Max3(MinimumLeft,PriceGridWidth,FMath::Min(760.f,float(Area.X)*.42f)));
    const float LeftWidth=FMath::Max(MinimumLeft,FMath::Min(PreferredLeft,float(Area.X)-DesiredDetail-Gap-PreviewReserve));
    const float DetailWidth=FMath::Max(220.f,FMath::Min(DesiredDetail,float(Area.X)-LeftWidth-Gap-PreviewReserve));
    const float PanelHeight=FMath::Max(120.f,float(Area.Y)-12.f);
    auto Position=[&](FName Name,FVector2D At,FVector2D Size) {
        if(auto* Width=WidgetTree->FindWidget<USizeBox>(Name)) {
            Width->SetWidthOverride(Size.X);Width->SetHeightOverride(Size.Y);
            if(auto* Cell=Cast<UCanvasPanelSlot>(Width->Slot)) {Cell->SetAnchors(FAnchors(0,0));Cell->SetAlignment(FVector2D::ZeroVector);Cell->SetPosition(At);Cell->SetSize(Size);}
        }
    };
    Position(TEXT("WardrobeV2LeftWidth"),FVector2D(0,0),FVector2D(LeftWidth,PanelHeight));
    for(const TCHAR* Name:{TEXT("WardrobeV2LeftPanel"),TEXT("WardrobeV2Detail")}) if(auto* Plate=WidgetTree->FindWidget<UMCUniversalUIWidget>(Name)) {
        const FMargin Inset=FString(Name)==TEXT("WardrobeV2LeftPanel")?FMargin(16,12,16,12):FMargin(18);
        if(Plate->ContentPadding!=Inset) {Plate->ContentPadding=Inset;Plate->ApplyTheme();}
    }
    if(auto* Header=WidgetTree->FindWidget<USizeBox>(TEXT("WardrobeV2HeaderBounds"))) {Header->ClearMaxDesiredWidth();Header->SetWidthOverride(TitleWidth+56.f);}
    if(auto* Header=WidgetTree->FindWidget(TEXT("WardrobeV2HeaderBounds"))) if(auto* Cell=Cast<UVerticalBoxSlot>(Header->Slot)) Cell->SetPadding(FMargin(0,0,0,10));
    if(auto* Back=WidgetTree->FindWidget(TEXT("WardrobeV2Back"))) if(auto* Cell=Cast<UVerticalBoxSlot>(Back->Slot)) {Cell->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));Cell->SetPadding(FMargin(0,8,8,0));}
    if(Colors) if(auto* Cell=Cast<UVerticalBoxSlot>(Colors->Slot)) Cell->SetPadding(FMargin(4,6,4,4));
    if(auto* Grid=WidgetTree->FindWidget<UUniformGridPanel>(TEXT("WardrobeV2Products"))) {
        Grid->SetMinDesiredSlotWidth(0);Grid->SetMinDesiredSlotHeight(0);Grid->SetSlotPadding(FMargin(5,5,12,12));
        Grid->SetIsEnabled(!Expanded);
    }
    const float GridEstimate=FMath::Max(80.f,PanelHeight-float(Measure(WidgetTree->FindWidget(TEXT("WardrobeV2HeaderBounds"))).Y)-150.f);
    const float RowStride=(GridArea.Y>1?float(GridArea.Y):GridEstimate)/3.f;
    const auto* ProductGrid=WidgetTree->FindWidget<UUniformGridPanel>(TEXT("WardrobeV2Products"));
    const float GridWidth=ProductGrid && ProductGrid->GetPaintSpaceGeometry().GetLocalSize().X>1?float(ProductGrid->GetPaintSpaceGeometry().GetLocalSize().X):LeftWidth-38.f;
    const float CellWidth=FMath::Max(1.f,GridWidth/3.f-17.f),CellHeight=FMath::Max(1.f,RowStride-17.f);
    for(auto& Tile:Tiles) {
        if(Tile.Bounds) {Tile.Bounds->SetWidthOverride(CellWidth);Tile.Bounds->SetHeightOverride(CellHeight);}
        if(Tile.Card && Tile.Card->ContentPadding!=FMargin(8)) {Tile.Card->ContentPadding=FMargin(8);Tile.Card->ApplyTheme();}
        float CopyHeight=0;
        for(auto* Text:{Tile.Fallback.Get(),Tile.Price.Get()}) if(Text) {
            float CopyWidth=FMath::Max(1.f,CellWidth-16.f);
            if(Text==Tile.Fallback.Get() && Text->GetVisibility()!=ESlateVisibility::Collapsed && FSlateApplication::IsInitialized())
                if(const auto Renderer=FSlateApplication::Get().GetRenderer()) CopyWidth=MCUnifiedWardrobePrivate_BalancedCaptionWidth(Text,CopyWidth,*Renderer->GetFontMeasureService());
            Text->SetAutoWrapText(false);Text->SetWrapTextAt(CopyWidth);
            Text->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
            if(Text->GetVisibility()!=ESlateVisibility::Collapsed) CopyHeight+=float(Measure(Text).Y)+8.f;
        }
        const float IconSize=FMath::Max(1.f,FMath::Min(CellHeight-CopyHeight-20.f,CellWidth-16.f));
        if(Tile.IconBounds) {Tile.IconBounds->SetWidthOverride(FMath::Min(IconSize,FMath::Max(1.f,CellWidth-16.f)));Tile.IconBounds->SetHeightOverride(IconSize);}
    }
    if(auto* Snap=WidgetTree->FindWidget<UMCUIGridScrollBox>(TEXT("WardrobeV2GridScroll")))
        Snap->ConfigureGrid(WidgetTree->FindWidget<UUniformGridPanel>(TEXT("WardrobeV2Products")),3,3,RowStride);
    float TextHeight=0;
    for(const TCHAR* Name:{TEXT("WardrobeV2DetailName"),TEXT("WardrobeV2Description"),TEXT("WardrobeV2DetailState")})
        if(auto* Text=WidgetTree->FindWidget<UTextBlock>(Name)) {Text->SetWrapTextAt(FMath::Max(60.f,DetailWidth-52.f));Text->SetAutoWrapText(false);TextHeight+=float(Measure(Text).Y);
            if(auto* Cell=Cast<UVerticalBoxSlot>(Text->Slot)) Cell->SetPadding(FMargin(0,0,0,12));}
    if(Primary && Primary->WidgetTree) if(auto* Caption=Primary->WidgetTree->FindWidget<UTextBlock>(TEXT("UniversalLabel"))) {Caption->SetAutoWrapText(false);Caption->SetWrapTextAt(FMath::Max(60.f,DetailWidth-Primary->ContentPadding.Left-Primary->ContentPadding.Right-56.f));}
    // The detail card starts beside the top catalog row and ends independently
    // of the catalog's fixed Back/color controls. Ordinary copy keeps the short
    // PSD card; complete longer copy can grow the card within the viewport.
    const float DetailTop=FMath::Min(PanelHeight*.28f,FMath::Max(100.f,float(Measure(WidgetTree->FindWidget(TEXT("WardrobeV2HeaderBounds"))).Y)+28.f));
    const float PrimaryHeight=float(Measure(Primary).Y);
    const float DesiredIconSize=FMath::Min(220.f,FMath::Max(64.f,DetailWidth-52.f));
    const float NaturalDetailHeight=DesiredIconSize+TextHeight+PrimaryHeight+112.f;
    const float DetailHeight=FMath::Max(120.f,FMath::Min(PanelHeight-DetailTop,FMath::Max(560.f,NaturalDetailHeight)));
    Position(TEXT("WardrobeV2DetailWidth"),FVector2D(LeftWidth+Gap,DetailTop),FVector2D(DetailWidth,DetailHeight));
    const float DetailIconSize=FMath::Clamp(DetailHeight-TextHeight-PrimaryHeight-100.f,64.f,DesiredIconSize);
    if(auto* Icon=WidgetTree->FindWidget<USizeBox>(TEXT("WardrobeV2DetailIconSize"))) {
        const auto* Product=SelectedProduct();const bool Show=Product && Product->Icon.GetResourceObject() && DetailIconSize>=40.f;
        Icon->SetVisibility(Show?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
        Icon->ClearMinDesiredWidth();Icon->ClearMaxDesiredWidth();Icon->ClearMinDesiredHeight();Icon->ClearMaxDesiredHeight();
        Icon->SetWidthOverride(DetailIconSize);Icon->SetHeightOverride(DetailIconSize);
        if(auto* Cell=Cast<UVerticalBoxSlot>(Icon->Slot)) {Cell->SetHorizontalAlignment(HAlign_Center);Cell->SetPadding(FMargin(0,0,0,8));}
    }
    if(auto* Popup=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("WardrobeV2ColorPopup"))) {
        Popup->SetVisibility(Expanded?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
        const float PopupHeight=FMath::Min(420.f,FMath::Max(100.f,PanelHeight-200.f));
        if(auto* Cell=Cast<UCanvasPanelSlot>(Popup->Slot)) {
            Cell->SetPosition(FVector2D(16,FMath::Max(92.f,PanelHeight-128.f-PopupHeight)));Cell->SetSize(FVector2D(LeftWidth-32.f,PopupHeight));
        }
        auto* Hint=WidgetTree->FindWidget<UTextBlock>(TEXT("WardrobeV2ColorHint"));if(Hint) Hint->SetWrapTextAt(FMath::Max(80.f,LeftWidth-84.f));
        const float PaletteHeight=FMath::Clamp(PopupHeight-float(Measure(Hint).Y)-float(Measure(WidgetTree->FindWidget(TEXT("WardrobeV2Apply"))).Y)-136.f,60.f,216.f);
        for(const TCHAR* Name:{TEXT("WardrobeV2Palette"),TEXT("WardrobeV2Hue")}) if(auto* Palette=WidgetTree->FindWidget<UMCWardrobeColorPalette>(Name)) {
            const bool Hue=FString(Name)==TEXT("WardrobeV2Hue");Palette->DesiredSize=FVector2D(Hue?28.f:PaletteHeight,PaletteHeight);
            if(auto* Bounds=Cast<USizeBox>(Palette->GetParent())) {Bounds->SetWidthOverride(Hue?28.f:PaletteHeight);Bounds->SetHeightOverride(PaletteHeight);}
        }
    }
}
bool UMCUnifiedWardrobeWidget::GetPreviewViewportRect(const FGeometry& ViewportPaint,FBox2D& OutRect) const
{
    OutRect=FBox2D(ForceInit);const FVector2D Size=ViewportPaint.GetLocalSize();
    if(!WidgetTree || !bAdaptiveTreeReady || Size.X<=1 || Size.Y<=1 || !IsVisible()) return false;
    double Right=0;bool Found=false;
    for(const TCHAR* Name:{TEXT("WardrobeV2LeftPanel"),TEXT("WardrobeV2Detail")}) if(const auto* Panel=WidgetTree->FindWidget(Name)) {
        const auto& Pose=Panel->GetPaintSpaceGeometry();if(Pose.GetLocalSize().X<=1 || Pose.GetLocalSize().Y<=1) return false;
        const FVector2D End=ViewportPaint.AbsoluteToLocal(Pose.LocalToAbsolute(Pose.GetLocalSize()));
        Right=FMath::Max(Right,End.X/Size.X);Found=true;
    }
    if(!Found || !FMath::IsFinite(Right) || Right>=.98) return false;
    OutRect=FBox2D(FVector2D(FMath::Clamp(Right+12./Size.X,0.,.99),.04),FVector2D(.985,.96));
    return OutRect.GetSize().X>.015 && OutRect.GetSize().Y>.1;
}
void UMCUnifiedWardrobeWidget::RefreshHeaderGeometry()
{
    if(!bAdaptHeaderContour || !WidgetTree || !bAdaptiveTreeReady || LastAdaptiveSize.X<=1 || LastAdaptiveSize.Y<=1) return;
    auto* Panel=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("WardrobeV2LeftPanel"));auto* Header=WidgetTree->FindWidget(TEXT("WardrobeV2Header"));if(!Panel || !Header) return;
    const auto& Geometry=Panel->GetPaintSpaceGeometry();const auto& HeaderGeometry=Header->GetPaintSpaceGeometry();const FVector2D Size=Geometry.GetLocalSize();
    const auto* Width=WidgetTree->FindWidget<USizeBox>(TEXT("WardrobeV2LeftWidth"));
    if(Size.X<=1 || Size.Y<=1 || HeaderGeometry.GetLocalSize().GetMin()<=1 || !Width
        || !Size.Equals(FVector2D(Width->GetWidthOverride(),Width->GetHeightOverride()),1.)) return;
    const auto Min=Geometry.AbsoluteToLocal(HeaderGeometry.LocalToAbsolute(FVector2D::ZeroVector));
    const auto Max=Geometry.AbsoluteToLocal(HeaderGeometry.LocalToAbsolute(HeaderGeometry.GetLocalSize()));
    // Ignore a stale pre-migration header pose instead of clamping it into a tall stem.
    if(Min.Y< -1 || Max.Y<=Min.Y || Max.Y>Size.Y*.45 || Max.X<=Min.X) return;
    const double X=FMath::Clamp((Min.X-24.)/Size.X,.02,.4),Right=FMath::Clamp((Max.X+24.)/Size.X,.6,.98),Y=FMath::Clamp((Max.Y+18.)/Size.Y,.02,.45);
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
