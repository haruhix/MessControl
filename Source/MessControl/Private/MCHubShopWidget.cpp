#include "MCHubShopWidget.h"

#include "MCUIVisualStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"

#define LOCTEXT_NAMESPACE "MCHubShop"

void UMCHubShopWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(WidgetTree->RootWidget) {ResolveControls();return;}
    auto* Scale=WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),TEXT("ShopScale"));
    Scale->SetStretch(EStretch::ScaleToFit);WidgetTree->RootWidget=Scale;
    auto* Size=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ShopDesignSize"));
    Size->SetWidthOverride(1280);Size->SetHeightOverride(720);Scale->SetContent(Size);
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("ShopRoot"));Size->SetContent(Root);
    ShopPanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("ShopPanel"));ShopPanel->SetPadding(FMargin(24,20));
    auto* PanelSlot=Root->AddChildToCanvas(ShopPanel);
    PanelSlot->SetAnchors(FAnchors(1.f,.5f));PanelSlot->SetAlignment(FVector2D(1.f,.5f));
    PanelSlot->SetPosition(FVector2D(-36,0));PanelSlot->SetSize(FVector2D(460,648));
    auto* Content=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("ShopContent"));ShopPanel->SetContent(Content);
    auto AddText=[this,Content](FName Name,FName Key,const FText& Copy,FMargin RowPadding) {
        auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);Label->SetText(Copy);Label->SetAutoWrapText(true);
        Content->AddChildToVerticalBox(Label)->SetPadding(RowPadding);TextBindings.Add(Name,Key);return Label;
    };
    AddText(TEXT("ShopTitle"),TEXT("UI.Shop.Title"),LOCTEXT("Title","МАГАЗИН"),FMargin(0,0,0,14));
    AddText(TEXT("ShopEmptyMessage"),TEXT("UI.Shop.Empty"),LOCTEXT("Empty","Ассортимент пока пуст"),FMargin(0,0,0,24));
    auto* Slots=WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(),TEXT("ShopEmptySlots"));
    Slots->SetMinDesiredSlotWidth(130);Slots->SetMinDesiredSlotHeight(112);Slots->SetSlotPadding(FMargin(3));
    Content->AddChildToVerticalBox(Slots)->SetPadding(FMargin(-3,0,-3,0));
    for(int32 Index=0;Index<6;++Index) {
        auto* Card=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),FName(*FString::Printf(TEXT("ShopEmptySlot%d"),Index)));
        Card->SetIsEnabled(false);Card->SetPadding(FMargin(12));Card->SetVerticalAlignment(VAlign_Center);
        auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),FName(*FString::Printf(TEXT("ShopEmptyLabel%d"),Index)));
        Label->SetText(LOCTEXT("EmptySlot","Пусто"));Label->SetJustification(ETextJustify::Center);Card->SetContent(Label);
        TextBindings.Add(Label->GetFName(),TEXT("UI.Shop.EmptySlot"));
        auto* GridSlot=Slots->AddChildToUniformGrid(Card,Index/3,Index%3);
        GridSlot->SetHorizontalAlignment(HAlign_Fill);GridSlot->SetVerticalAlignment(VAlign_Fill);
    }
    auto* Footer=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ShopFooterSpace"));Footer->SetMinDesiredHeight(16);
    Content->AddChildToVerticalBox(Footer)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    ShopClose=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("ShopClose"));
    auto* CloseLabel=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("ShopCloseLabel"));
    CloseLabel->SetText(LOCTEXT("Close","Назад"));CloseLabel->SetJustification(ETextJustify::Center);ShopClose->SetContent(CloseLabel);
    TextBindings.Add(TEXT("ShopCloseLabel"),TEXT("UI.Shop.Close"));
    if(auto* ButtonSlot=Cast<UButtonSlot>(ShopClose->GetContentSlot())) {
        ButtonSlot->SetHorizontalAlignment(HAlign_Fill);ButtonSlot->SetVerticalAlignment(VAlign_Center);
    }
    auto* CloseSize=WidgetTree->ConstructWidget<USizeBox>();CloseSize->SetHeightOverride(44);CloseSize->SetContent(ShopClose);
    Content->AddChildToVerticalBox(CloseSize);
    ResolveControls();ApplyTheme();
}

void UMCHubShopWidget::ResolveControls()
{
    if(!WidgetTree) return;
    ShopPanel=Cast<UBorder>(WidgetTree->FindWidget(TEXT("ShopPanel")));
    ShopClose=Cast<UButton>(WidgetTree->FindWidget(TEXT("ShopClose")));
}

void UMCHubShopWidget::NativeOnInitialized() {Super::NativeOnInitialized();SetIsFocusable(true);}
void UMCHubShopWidget::NativeConstruct()
{
    Super::NativeConstruct();ResolveControls();
    if(ShopClose) ShopClose->OnClicked.AddUniqueDynamic(this,&ThisClass::CloseClicked);
}
void UMCHubShopWidget::NativeDestruct()
{
    if(ShopClose) ShopClose->OnClicked.RemoveAll(this);
    Super::NativeDestruct();
}
void UMCHubShopWidget::CloseClicked() {if(!bEditorFixture && !IsDesignTime()) OnClose.Broadcast();}
FReply UMCHubShopWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if((Event.GetKey()==EKeys::Escape || Event.GetKey()==EKeys::Gamepad_FaceButton_Right) && !Event.IsRepeat()) {
        CloseClicked();return FReply::Handled();
    }
    return Super::NativeOnPreviewKeyDown(Geometry,Event);
}
void UMCHubShopWidget::ApplyTheme()
{
    ResolveControls();Super::ApplyTheme();
    const auto* Style=GetResolvedTheme();if(!Style || !WidgetTree) return;
    const auto& P=Style->Palette;
    MCUIVisualStyle::Panel(ShopPanel,Style,P.Backdrop.CopyWithNewOpacity(.94f));
    auto Text=[this,Style](FName Name,int32 Size,FLinearColor Color,bool Bold=true) {
        if(auto* Label=Cast<UTextBlock>(WidgetTree->FindWidget(Name))) {
            Label->SetFont(Style->MakeGameplayFont(Size,Bold));Label->SetColorAndOpacity(Color);
        }
    };
    Text(TEXT("ShopTitle"),40,P.Cream);Text(TEXT("ShopEmptyMessage"),19,P.OnMarine,false);
    for(int32 Index=0;Index<6;++Index) {
        if(auto* Card=Cast<UBorder>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("ShopEmptySlot%d"),Index))))) {
            MCUIVisualStyle::Panel(Card,Style,P.Marine.CopyWithNewOpacity(.45f));Card->SetIsEnabled(false);
        }
        Text(FName(*FString::Printf(TEXT("ShopEmptyLabel%d"),Index)),14,P.OnMarine,false);
    }
    MCUIVisualStyle::Button(ShopClose,Style,P.Mint);Text(TEXT("ShopCloseLabel"),18,P.ButtonText);
}
void UMCHubShopWidget::ApplyEditorPreview(float Time)
{
    bEditorFixture=true;BuildPresentationTree();Super::ApplyEditorPreview(Time);
}
void UMCHubShopWidget::ResetEditorPreview() {Super::ResetEditorPreview();}

#undef LOCTEXT_NAMESPACE
