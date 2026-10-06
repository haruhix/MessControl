#include "MCDeliveryZoneCaptionWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"

void UMCDeliveryZoneCaptionWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(false);
    SetVisibility(ESlateVisibility::HitTestInvisible);

    auto* Size=WidgetTree->ConstructWidget<USizeBox>();
    Size->SetWidthOverride(280);Size->SetHeightOverride(56);
    WidgetTree->RootWidget=Size;
    Background=WidgetTree->ConstructWidget<UBorder>();
    Background->SetPadding(FMargin(10,6));
    Background->SetClipping(EWidgetClipping::ClipToBounds);
    Size->SetContent(Background);

    auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();
    Background->SetContent(Row);
    auto* IconSize=WidgetTree->ConstructWidget<USizeBox>();
    IconSize->SetWidthOverride(28);IconSize->SetHeightOverride(28);
    IconBackground=WidgetTree->ConstructWidget<UBorder>();
    IconBackground->SetPadding(FMargin(0));
    IconSize->SetContent(IconBackground);
    DirectionIcon=WidgetTree->ConstructWidget<UTextBlock>();
    DirectionIcon->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),20));
    DirectionIcon->SetJustification(ETextJustify::Center);
    DirectionIcon->SetAutoWrapText(false);
    IconBackground->SetHorizontalAlignment(HAlign_Center);
    IconBackground->SetVerticalAlignment(VAlign_Center);
    IconBackground->SetContent(DirectionIcon);
    auto* IconSlot=Row->AddChildToHorizontalBox(IconSize);
    IconSlot->SetVerticalAlignment(VAlign_Center);
    IconSlot->SetPadding(FMargin(0,0,8,0));

    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();
    auto* ColumnSlot=Row->AddChildToHorizontalBox(Column);
    ColumnSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    ColumnSlot->SetVerticalAlignment(VAlign_Center);
    Title=WidgetTree->ConstructWidget<UTextBlock>();
    Title->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),18));
    Title->SetAutoWrapText(false);
    Column->AddChildToVerticalBox(Title)->SetPadding(FMargin(0,0,0,1));

    // The complete Russian exit description remains on one line at small screen resolutions.
    auto* DescriptionHeight=WidgetTree->ConstructWidget<USizeBox>();
    DescriptionHeight->SetHeightOverride(18);
    auto* DescriptionScale=WidgetTree->ConstructWidget<UScaleBox>();
    DescriptionScale->SetStretch(EStretch::ScaleToFit);
    DescriptionScale->SetStretchDirection(EStretchDirection::DownOnly);
    DescriptionHeight->SetContent(DescriptionScale);
    Description=WidgetTree->ConstructWidget<UTextBlock>();
    Description->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),13));
    Description->SetAutoWrapText(false);
    DescriptionScale->SetContent(Description);
    auto* DescriptionSlot=CastChecked<UScaleBoxSlot>(Description->Slot);
    DescriptionSlot->SetHorizontalAlignment(HAlign_Left);
    DescriptionSlot->SetVerticalAlignment(VAlign_Center);
    Column->AddChildToVerticalBox(DescriptionHeight);
    RefreshCaption();
}

void UMCDeliveryZoneCaptionWidget::SetDeliveryCaption(bool bExit,bool bSortingError)
{
    if(bExitCaption==bExit && bErrorCaption==bSortingError && Title) return;
    bExitCaption=bExit;bErrorCaption=bSortingError;
    RefreshCaption();
}

void UMCDeliveryZoneCaptionWidget::RefreshCaption()
{
    if(!Title || !Description || !Background || !DirectionIcon || !IconBackground) return;
    const FLinearColor Accent=bExitCaption?FLinearColor(1.f,.22f,.18f):FLinearColor(.20f,1.f,.52f);
    Background->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.012f,.024f,.034f,.85f),10.f,Accent.CopyWithNewOpacity(.52f),1.f));
    IconBackground->SetBrush(FSlateRoundedBoxBrush(Accent.CopyWithNewOpacity(.14f),7.f));
    DirectionIcon->SetColorAndOpacity(FSlateColor(Accent));
    DirectionIcon->SetText(FText::FromString(bExitCaption?TEXT("»"):TEXT("«")));
    Title->SetColorAndOpacity(FSlateColor(Accent));
    Title->SetText(bExitCaption?NSLOCTEXT("DeliveryZones","ExitTitle","ВЫХОД"):NSLOCTEXT("DeliveryZones","EntryTitle","ВХОД"));
    Description->SetColorAndOpacity(FSlateColor(bErrorCaption?FLinearColor(1.f,.74f,.34f):FLinearColor(.92f,.96f,.97f)));
    Description->SetText(bErrorCaption?NSLOCTEXT("DeliveryZones","SortingError","ОШИБКА СОРТИРОВКИ")
        :bExitCaption?NSLOCTEXT("DeliveryZones","ExitDescription","МУСОР · ИСПОРЧЕННОЕ · ЩЁТКИ")
        :NSLOCTEXT("DeliveryZones","EntryDescription","СВЕЖАЯ ЕДА"));
}
