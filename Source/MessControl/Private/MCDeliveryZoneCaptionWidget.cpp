#include "MCDeliveryZoneCaptionWidget.h"
#include "MCUIVisualStyle.h"
#include "MCWorldUIShapes.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

#define LOCTEXT_NAMESPACE "MCDeliveryZones"

UMCDeliveryZoneCaptionWidget::UMCDeliveryZoneCaptionWidget() {bUseVersionedWorldPresentation=true;}

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
    if(!WidgetTree) return;
    if(Background && Title && Description) {BuildWorldPresentationTree();return;}
    if(WidgetTree->RootWidget)
    {
        Background=Cast<UBorder>(WidgetTree->FindWidget(TEXT("DeliveryBackground")));
        IconBackground=Cast<UBorder>(WidgetTree->FindWidget(TEXT("DeliveryIconBackground")));
        DirectionIcon=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("DeliveryDirection")));
        Title=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("DeliveryTitle")));
        Description=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("DeliveryDescription")));
        BuildWorldPresentationTree();return;
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
    BuildWorldPresentationTree();
}

void UMCDeliveryZoneCaptionWidget::BuildWorldPresentationTree()
{
    if(!bUseVersionedWorldPresentation || !WidgetTree) return;
    auto* NewRoot=WidgetTree->FindWidget<USizeBox>(TEXT("WorldDeliveryV2Root"));
    if(!NewRoot) {
        auto* NewTheme=GetNewWorldPresentationTheme();NewRoot=MCWorldUIShapes::Bounds(WidgetTree,TEXT("WorldDeliveryV2Root"),320,64);
        auto* Plate=MCWorldUIShapes::Panel(WidgetTree,TEXT("WorldDeliveryV2Plate"),NewTheme,EMCUIColorToken::Surface,FMargin(12,9));NewRoot->SetContent(Plate);
        auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("WorldDeliveryV2Row"));Plate->SetContentForSlot(TEXT("Content"),Row);
        auto* DirectionBounds=MCWorldUIShapes::Bounds(WidgetTree,TEXT("WorldDeliveryV2DirectionBounds"),40,40);
        auto* Badge=MCWorldUIShapes::Panel(WidgetTree,TEXT("WorldDeliveryV2DirectionShape"),NewTheme,EMCUIColorToken::Cream,FMargin(4,2),9);DirectionBounds->SetContent(Badge);
        auto* Arrow=MCWorldUIShapes::Text(WidgetTree,TEXT("WorldDeliveryV2Direction"),32);Badge->SetContentForSlot(TEXT("Content"),Arrow);
        auto* DirectionCell=Row->AddChildToHorizontalBox(DirectionBounds);DirectionCell->SetVerticalAlignment(VAlign_Center);DirectionCell->SetPadding(FMargin(0,0,8,0));
        auto* Stack=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("WorldDeliveryV2Stack"));auto* StackCell=Row->AddChildToHorizontalBox(Stack);StackCell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));StackCell->SetVerticalAlignment(VAlign_Center);
        auto* Heading=MCWorldUIShapes::Text(WidgetTree,TEXT("WorldDeliveryV2Title"),248);Stack->AddChildToVerticalBox(Heading)->SetPadding(FMargin(0,0,0,3));
        auto* Copy=MCWorldUIShapes::Text(WidgetTree,TEXT("WorldDeliveryV2Copy"),248);Stack->AddChildToVerticalBox(Copy);
    }
    InstallWorldPresentation(NewRoot);
    NewDirectionShape=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("WorldDeliveryV2DirectionShape"));NewDirection=WidgetTree->FindWidget<UTextBlock>(TEXT("WorldDeliveryV2Direction"));
    NewTitle=WidgetTree->FindWidget<UTextBlock>(TEXT("WorldDeliveryV2Title"));NewDescription=WidgetTree->FindWidget<UTextBlock>(TEXT("WorldDeliveryV2Copy"));
}

void UMCDeliveryZoneCaptionWidget::RefreshWorldPresentation()
{
    const auto* NewTheme=GetNewWorldPresentationTheme();if(!NewTheme) return;
    if(NewDirectionShape) {NewDirectionShape->Fill=bErrorCaption?EMCUIColorToken::Warning:EMCUIColorToken::Cream;NewDirectionShape->ApplyTheme();}
    MCWorldUIShapes::TextStyle(NewDirection,NewTheme,24,EMCUIColorToken::Marine);
    MCWorldUIShapes::TextStyle(NewTitle,NewTheme,18,EMCUIColorToken::OnMarine);
    MCWorldUIShapes::TextStyle(NewDescription,NewTheme,16,bErrorCaption?EMCUIColorToken::Warning:EMCUIColorToken::OnMarine);
    if(NewDirection) NewDirection->SetText(DirectionIcon?DirectionIcon->GetText():FText::GetEmpty());
    if(NewTitle) NewTitle->SetText(Title?Title->GetText():FText::GetEmpty());
    if(NewDescription) NewDescription->SetText(Description?Description->GetText():FText::GetEmpty());
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
    RefreshWorldPresentation();
}

void UMCDeliveryZoneCaptionWidget::ApplyEditorPreview(float Time)
{
    bExitCaption=PreviewState==TEXT("Exit") || PreviewState==TEXT("SortingError");
    bErrorCaption=PreviewState==TEXT("SortingError");
    Super::ApplyEditorPreview(Time);
    SetVisibility(PreviewScenario==EMCGameplayUIPreviewScenario::Empty?ESlateVisibility::Hidden:ESlateVisibility::HitTestInvisible);
}

#undef LOCTEXT_NAMESPACE
