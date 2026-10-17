#include "MCDeliveryZoneCaptionWidget.h"
#include "MCUIVisualStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

#define LOCTEXT_NAMESPACE "MCDeliveryZones"

void UMCDeliveryZoneCaptionWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(false);
    SetVisibility(ESlateVisibility::HitTestInvisible);
    BuildPresentationTree();
    ApplyTheme();
}

void UMCDeliveryZoneCaptionWidget::BuildPresentationTree()
{
    if(!WidgetTree || (Background && Title && Description)) return;
    if(WidgetTree->RootWidget)
    {
        Background=Cast<UBorder>(WidgetTree->FindWidget(TEXT("DeliveryBackground")));
        IconBackground=Cast<UBorder>(WidgetTree->FindWidget(TEXT("DeliveryIconBackground")));
        DirectionIcon=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("DeliveryDirection")));
        Title=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("DeliveryTitle")));
        Description=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("DeliveryDescription")));
        return;
    }
    auto* Size=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("DeliverySize"));
    Size->SetWidthOverride(320);Size->SetMinDesiredHeight(64);WidgetTree->RootWidget=Size;
    Background=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("DeliveryBackground"));
    Background->SetPadding(FMargin(12,9));Size->SetContent(Background);
    auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Background->SetContent(Row);
    auto* IconSize=WidgetTree->ConstructWidget<USizeBox>();
    IconSize->SetWidthOverride(32);IconSize->SetHeightOverride(32);
    IconBackground=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("DeliveryIconBackground"));
    IconBackground->SetPadding(FMargin(0));IconSize->SetContent(IconBackground);
    DirectionIcon=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("DeliveryDirection"));
    DirectionIcon->SetJustification(ETextJustify::Center);DirectionIcon->SetAutoWrapText(false);
    IconBackground->SetHorizontalAlignment(HAlign_Center);IconBackground->SetVerticalAlignment(VAlign_Center);IconBackground->SetContent(DirectionIcon);
    auto* IconSlot=Row->AddChildToHorizontalBox(IconSize);
    IconSlot->SetVerticalAlignment(VAlign_Center);IconSlot->SetPadding(FMargin(0,0,8,0));
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();
    auto* ColumnSlot=Row->AddChildToHorizontalBox(Column);
    ColumnSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));ColumnSlot->SetVerticalAlignment(VAlign_Center);
    Title=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("DeliveryTitle"));
    Title->SetAutoWrapText(true);Title->SetWrapTextAt(252);Title->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);Title->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
    Column->AddChildToVerticalBox(Title)->SetPadding(FMargin(0,0,0,3));

    // Keep a bounded world-label width and let translated descriptions grow down.
    // Shrinking the entire sentence to a fixed single line makes it unreadable.
    Description=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("DeliveryDescription"));
    Description->SetAutoWrapText(true);Description->SetWrapTextAt(252);Description->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);Description->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
    Column->AddChildToVerticalBox(Description);
}

void UMCDeliveryZoneCaptionWidget::SetDeliveryCaption(bool bExit,bool bSortingError)
{
    if(bExitCaption==bExit && bErrorCaption==bSortingError && Title) return;
    bExitCaption=bExit;bErrorCaption=bSortingError;
    BuildPresentationTree();
    RefreshCaption();
}

void UMCDeliveryZoneCaptionWidget::ApplyTheme()
{
    BuildPresentationTree();
    Super::ApplyTheme();
    RefreshCaption();
}

void UMCDeliveryZoneCaptionWidget::RefreshCaption()
{
    if(!Title || !Description || !Background || !DirectionIcon || !IconBackground) return;
    const auto* Resolved=GetResolvedTheme();const auto& P=Resolved->Palette;
    const FLinearColor Accent=bErrorCaption?P.Warning:bExitCaption?P.Cost:P.Accent;
    MCUIVisualStyle::Panel(Background,Resolved,P.Marine);
    MCUIVisualStyle::Panel(IconBackground,Resolved,Accent);
    DirectionIcon->SetFont(Resolved->MakeFont(24));DirectionIcon->SetColorAndOpacity(P.Ink);
    DirectionIcon->SetText(FText::AsCultureInvariant(bExitCaption?TEXT("»"):TEXT("«")));
    Title->SetFont(Resolved->MakeFont(18));Title->SetColorAndOpacity(Accent);
    Title->SetText(bExitCaption?Localized(TEXT("UI.Delivery.ExitTitle"),LOCTEXT("ExitTitle","ВЫХОД")):Localized(TEXT("UI.Delivery.EntryTitle"),LOCTEXT("EntryTitle","ВХОД")));
    Description->SetFont(Resolved->MakeFont(16,false));Description->SetColorAndOpacity(bErrorCaption?P.Warning:P.OnMarine);
    Description->SetText(bErrorCaption?Localized(TEXT("UI.Delivery.SortingError"),LOCTEXT("SortingError","ОШИБКА СОРТИРОВКИ"))
        :bExitCaption?Localized(TEXT("UI.Delivery.ExitDescription"),LOCTEXT("ExitDescription","МУСОР · ИСПОРЧЕННОЕ · ЩЁТКИ"))
        :Localized(TEXT("UI.Delivery.EntryDescription"),LOCTEXT("EntryDescription","СВЕЖАЯ ЕДА")));
}

void UMCDeliveryZoneCaptionWidget::ApplyEditorPreview(float Time)
{
    bExitCaption=PreviewState==TEXT("Exit") || PreviewState==TEXT("SortingError");
    bErrorCaption=PreviewState==TEXT("SortingError");
    Super::ApplyEditorPreview(Time);
    SetVisibility(PreviewScenario==EMCGameplayUIPreviewScenario::Empty?ESlateVisibility::Hidden:ESlateVisibility::HitTestInvisible);
}

#undef LOCTEXT_NAMESPACE
