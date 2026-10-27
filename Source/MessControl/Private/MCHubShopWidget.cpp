#include "MCHubShopWidget.h"

#include "MCHubEconomyComponent.h"
#include "MCCosmeticItemWidget.h"
#include "MCPlayerState.h"
#include "MCUIVisualStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
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
    if(WidgetTree->RootWidget) {ResolveControls();EnsureCatalogPresentation();return;}
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
    ResolveControls();EnsureCatalogPresentation();ApplyTheme();
}

void UMCHubShopSlotButton::Configure(UMCHubShopWidget* Owner,int32 SkillSlot)
{
    OwnerWidget=Owner;DestinationSlot=SkillSlot;OnClicked.AddUniqueDynamic(this,&ThisClass::Clicked);
}
void UMCHubShopSlotButton::Clicked() {if(OwnerWidget.IsValid()) OwnerWidget->SelectReplacementSlot(DestinationSlot);}

void UMCHubShopWidget::EnsureReplacementPresentation()
{
    if(!WidgetTree) return;
    ReplacementSlots=Cast<UHorizontalBox>(WidgetTree->FindWidget(TEXT("ShopReplacementSlots")));
    if(ReplacementSlots) {
        ReplacementButtons.Reset();
        for(int32 I=0;I<ReplacementSlots->GetChildrenCount();++I) if(auto* Button=Cast<UMCHubShopSlotButton>(ReplacementSlots->GetChildAt(I))) {Button->Configure(this,I);ReplacementButtons.Add(Button);}
        return;
    }
    auto* Content=Cast<UVerticalBox>(WidgetTree->FindWidget(TEXT("ShopContent")));
    if(!Content || !ShopBuy) return;
    UWidget* BuyHost=ShopBuy;
    while(BuyHost->GetParent() && BuyHost->GetParent()!=Content) BuyHost=BuyHost->GetParent();
    const int32 Index=Content->GetChildIndex(BuyHost);if(Index==INDEX_NONE) return;
    ReplacementSlots=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("ShopReplacementSlots"));
    ReplacementSlots->SetVisibility(ESlateVisibility::Collapsed);
    if(auto* Row=Cast<UVerticalBoxSlot>(Content->InsertChildAt(Index,ReplacementSlots))) Row->SetPadding(FMargin(0,0,0,6));
    for(int32 I=0;I<4;++I) {
        auto* Button=WidgetTree->ConstructWidget<UMCHubShopSlotButton>(UMCHubShopSlotButton::StaticClass(),FName(*FString::Printf(TEXT("ShopReplace%d"),I)));
        auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),FName(*FString::Printf(TEXT("ShopReplaceLabel%d"),I)));
        Label->SetJustification(ETextJustify::Center);Label->SetAutoWrapText(true);Button->SetContent(Label);Button->Configure(this,I);
        auto* Row=ReplacementSlots->AddChildToHorizontalBox(Button);Row->SetSize(FSlateChildSize(ESlateSizeRule::Fill));Row->SetPadding(FMargin(2));
        ReplacementButtons.Add(Button);
    }
}

void UMCHubShopItemButton::Configure(UMCHubShopWidget* Owner,FName ID)
{
    OwnerWidget=Owner;ItemID=ID;OnClicked.AddUniqueDynamic(this,&ThisClass::Clicked);
}
void UMCHubShopItemButton::Clicked() { if(OwnerWidget.IsValid()) OwnerWidget->SelectCatalogItem(ItemID); }

void UMCHubShopWidget::EnsureCatalogPresentation()
{
    if(!WidgetTree) return;
    if(ShopCatalogGrid) {EnsureReplacementPresentation();return;}
    auto* Content=Cast<UVerticalBox>(WidgetTree->FindWidget(TEXT("ShopContent")));
    auto* Previous=Cast<UUniformGridPanel>(WidgetTree->FindWidget(TEXT("ShopEmptySlots")));
    // Migrate only the known empty placeholder. An artist-authored catalog host
    // remains untouched, and all other panel/close-button layout is preserved.
    if(!Content || !Previous || Previous->GetParent()!=Content) return;
    int32 Index=Content->GetChildIndex(Previous); Previous->RemoveFromParent();
    ShopBalance=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("ShopBalance"));
    ShopBalance->SetText(LOCTEXT("BalancePlaceholder","Монеты: 0"));
    if(auto* RowSlot=Cast<UVerticalBoxSlot>(Content->InsertChildAt(Index++,ShopBalance))) RowSlot->SetPadding(FMargin(0,0,0,8));
    auto* Size=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ShopCatalogHost"));Size->SetHeightOverride(308);
    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("ShopCatalogScroll"));Size->SetContent(Scroll);
    ShopCatalogGrid=WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(),TEXT("ShopCatalogGrid"));
    ShopCatalogGrid->SetMinDesiredSlotWidth(184);ShopCatalogGrid->SetMinDesiredSlotHeight(180);ShopCatalogGrid->SetSlotPadding(FMargin(4));
    Scroll->AddChild(ShopCatalogGrid);
    if(auto* RowSlot=Cast<UVerticalBoxSlot>(Content->InsertChildAt(Index++,Size))) RowSlot->SetPadding(FMargin(-3,0,-3,8));
    auto AddText=[this,Content,&Index](FName Name,int32 Height)->UTextBlock* {
        auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);Text->SetAutoWrapText(true);
        auto* Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetHeightOverride(Height);Box->SetClipping(EWidgetClipping::ClipToBounds);Box->SetContent(Text);
        Content->InsertChildAt(Index++,Box);return Text;
    };
    ShopSelectedName=AddText(TEXT("ShopSelectedName"),26);
    ShopSelectedDescription=AddText(TEXT("ShopSelectedDescription"),44);
    ShopTransactionMessage=AddText(TEXT("ShopTransactionMessage"),24);
    ShopBuy=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("ShopBuy"));
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("ShopBuyLabel"));
    Label->SetText(LOCTEXT("Buy","Купить"));Label->SetJustification(ETextJustify::Center);ShopBuy->SetContent(Label);
    auto* BuySize=WidgetTree->ConstructWidget<USizeBox>();BuySize->SetHeightOverride(42);BuySize->SetContent(ShopBuy);
    if(auto* RowSlot=Cast<UVerticalBoxSlot>(Content->InsertChildAt(Index++,BuySize))) RowSlot->SetPadding(FMargin(0,0,0,8));
    if(auto* Footer=Cast<USizeBox>(WidgetTree->FindWidget(TEXT("ShopFooterSpace")))) Footer->SetMinDesiredHeight(0);
    if(auto* Hint=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("ShopEmptyMessage")))) {
        Hint->SetText(LOCTEXT("NextRunHint","Бустеры — на следующий забег"));
        TextBindings.Add(Hint->GetFName(),TEXT("UI.Shop.NextRunHint"));
        if(auto* RowSlot=Cast<UVerticalBoxSlot>(Hint->Slot)) RowSlot->SetPadding(FMargin(0,0,0,8));
    }
    ResolveControls();EnsureReplacementPresentation();
}

void UMCHubShopWidget::Configure(UMCHubEconomyComponent* Economy)
{
    if(auto* Previous=ObservedEconomy.Get()) Previous->OnEconomyChanged.RemoveAll(this);
    ObservedEconomy=Economy;SelectedItemID=NAME_None;bChoosingReplacement=false;
    if(Economy) Economy->OnEconomyChanged.AddUniqueDynamic(this,&ThisClass::RefreshEconomy);
    if(WidgetTree && WidgetTree->RootWidget) RefreshEconomy();
}
void UMCHubShopWidget::SelectCatalogItem(FName ItemID)
{
    if(bEditorFixture || IsDesignTime()) return;
    auto* Economy=ObservedEconomy.Get();
    if(!Economy || !Economy->FindCatalogItem(ItemID)) return;
    SelectedItemID=ItemID;bChoosingReplacement=false;SetTransactionMessage(FText::GetEmpty());RefreshEconomy();
}
void UMCHubShopWidget::SetTransactionMessage(const FText& Message)
{
    if(ShopTransactionMessage) ShopTransactionMessage->SetText(Message);
}
void UMCHubShopWidget::RefreshEconomy()
{
    ResolveControls();EnsureReplacementPresentation();if(!ShopCatalogGrid) return;
    auto* Economy=ObservedEconomy.Get();
    const bool Preview=bEditorFixture || IsDesignTime();
    auto Entries=Economy?Economy->GetShopCatalog():TArray<FMCHubEconomyCatalogEntry>();
    Entries.RemoveAll([](const FMCHubEconomyCatalogEntry& Entry) {return Entry.Definition.Kind==EMCHubCatalogKind::Cosmetic || !Entry.Definition.bEnabled || !Entry.Definition.bInShop;});
    if(Preview && Entries.IsEmpty()) {
        const TCHAR* Names[]={TEXT("Суперщётка"),TEXT("Запас сил"),TEXT("Быстрые ноги"),TEXT("Сильные руки")};
        for(int32 I=0;I<4;++I) {FMCHubEconomyCatalogEntry Entry;Entry.ItemID=FName(*FString::Printf(TEXT("Preview%d"),I));
            Entry.Definition.DisplayName=FText::FromString(Names[I]);Entry.Definition.Description=LOCTEXT("PreviewCardDescription","Улучшение на следующий забег.");Entry.Definition.Price=12+I*4;
            Entry.Definition.Rarity=I==0?EMCHubItemRarity::Rare:EMCHubItemRarity::Common;Entries.Add(Entry);}
    }
    bool Rebuild=CatalogCards.Num()!=Entries.Num() || !CatalogButtons.IsEmpty();
    for(int32 I=0;!Rebuild && I<Entries.Num();++I) Rebuild=!CatalogCards[I] || CatalogCards[I]->GetCatalogId()!=Entries[I].ItemID;
    if(Rebuild) {ShopCatalogGrid->ClearChildren();CatalogButtons.Reset();CatalogCards.Reset();}
    if(!Entries.ContainsByPredicate([this](const FMCHubEconomyCatalogEntry& E) {return E.ItemID==SelectedItemID;})) SelectedItemID=Entries.IsEmpty()?NAME_None:Entries[0].ItemID;
    if(ShopBalance) ShopBalance->SetText(FText::Format(Localized(TEXT("UI.Shop.Balance"),LOCTEXT("Balance","Монеты: {0}")),FText::AsNumber(Economy?Economy->Coins:Preview?80:0)));
    TSubclassOf<UMCCosmeticItemWidget> CardClass=ProductCardClass;
    if(!CardClass) CardClass=LoadClass<UMCCosmeticItemWidget>(nullptr,TEXT("/Game/Gameplay/UI/WBP_WardrobeCosmeticTile.WBP_WardrobeCosmeticTile_C"));
    if(!CardClass) CardClass=UMCCosmeticItemWidget::StaticClass();
    for(int32 I=0;I<Entries.Num();++I) {
        const auto& Entry=Entries[I];
        auto* Card=Rebuild?CreateWidget<UMCCosmeticItemWidget>(this,CardClass):CatalogCards[I].Get();if(!Card) continue;
        Card->Theme=GetResolvedTheme();Card->SetTranslationTable(GetResolvedTranslationTable());
        const int32 Queued=Economy?Economy->QueuedStacks(Entry.ItemID):0;
        const bool Affordable=Preview || (Economy && Economy->Coins>=Entry.Definition.Price);
        FText Status=Queued>0?FText::Format(Localized(TEXT("UI.Shop.Queued"),LOCTEXT("Queued","На следующий забег: {0}")),FText::AsNumber(Queued)):
            Entry.ItemID==SelectedItemID?Localized(TEXT("UI.Product.Selected"),LOCTEXT("SelectedCard","Выбрано")):
            !Affordable?Localized(TEXT("UI.Product.Insufficient"),LOCTEXT("InsufficientCard","Мало монет")):Localized(TEXT("UI.Product.Details"),LOCTEXT("DetailsCard","Подробнее"));
        Card->Configure(NAME_None,Entry.ItemID,Entry.ItemID,Localized(Entry.Definition.DisplayNameKey,Entry.Definition.DisplayName),Queued>0,Entry.ItemID==SelectedItemID,Entry.Definition.Price,Affordable);
        Card->SetProductPresentation(Entry.Definition.Icon.Get(),Localized(Entry.Definition.DescriptionKey,Entry.Definition.Description),Entry.Definition.Rarity,false,Status);
        Card->OnPressed.AddUniqueDynamic(this,&ThisClass::CardSelected);
        if(Rebuild) {auto* S=ShopCatalogGrid->AddChildToUniformGrid(Card,I/2,I%2);S->SetHorizontalAlignment(HAlign_Fill);S->SetVerticalAlignment(VAlign_Fill);CatalogCards.Add(Card);}
    }
    FMCHubEconomyCatalogEntry SelectedEntry;
    const FMCHubCatalogRow* Selected=Economy && Economy->FindCatalogEntry(SelectedItemID,SelectedEntry)?&SelectedEntry.Definition:nullptr;
    const auto Planned=Economy?Economy->GetPendingActiveSlots():TArray<FMCActiveSkillSlot>();
    const bool UsesSlot=SelectedEntry.PerkKind==EMCPerkKind::Active || SelectedEntry.PerkKind==EMCPerkKind::Consumable;
    const bool Full=Planned.Num()==4 && !Planned.ContainsByPredicate([](const auto& SkillSlot){return SkillSlot.PerkID.IsNone();});
    if(!Selected || !UsesSlot || !Full) bChoosingReplacement=false;
    if(auto* Host=Cast<USizeBox>(WidgetTree->FindWidget(TEXT("ShopCatalogHost")))) Host->SetHeightOverride(bChoosingReplacement?166.f:220.f);
    if(ReplacementSlots) ReplacementSlots->SetVisibility(bChoosingReplacement?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    for(int32 I=0;I<ReplacementButtons.Num();++I) if(auto* Label=Cast<UTextBlock>(ReplacementButtons[I]->GetContent())) {
        const FName ID=Planned.IsValidIndex(I)?Planned[I].PerkID:NAME_None;
        FText Name=FText::FromName(ID);
        if(Economy) for(const auto& Entry:Economy->GetCatalog()) if(Entry.Definition.PerkID==ID) {Name=Localized(Entry.Definition.DisplayNameKey,Entry.Definition.DisplayName);break;}
        Label->SetText(FText::Format(Localized(TEXT("UI.Shop.ReplaceSlot"),LOCTEXT("ReplaceSlot","{0} · {1}")),FText::AsNumber(I+1),Name));
    }
    if(ShopTransactionMessage && ShopTransactionMessage->GetText().IsEmpty() && Economy && !Economy->MigrationNotice.IsEmpty()) ShopTransactionMessage->SetText(Economy->MigrationNotice);
    if(ShopSelectedName) ShopSelectedName->SetText(Selected?Localized(Selected->DisplayNameKey,Selected->DisplayName):Preview?LOCTEXT("PreviewSelected","Суперщётка"):
        Localized(TEXT("UI.Shop.NoItems"),LOCTEXT("NoItems","Ассортимент пока недоступен")));
    if(ShopSelectedDescription) ShopSelectedDescription->SetText(Selected?Localized(Selected->DescriptionKey,Selected->Description):Preview?LOCTEXT("PreviewDescription","Покупка действует в следующем забеге."):FText::GetEmpty());
    FText Error;const bool CanBuy=Economy && Selected && Economy->CanPurchase(SelectedItemID,Error);
    if(ShopBuy) ShopBuy->SetIsEnabled(CanBuy || Preview);
    if(auto* Label=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("ShopBuyLabel"))))
        Label->SetText(Selected && Economy && Economy->Coins<Selected->Price?Localized(TEXT("UI.Product.Insufficient"),LOCTEXT("BuyInsufficient","Мало монет")):
            Selected && UsesSlot && Full?Localized(TEXT("UI.Shop.ChooseReplacement"),LOCTEXT("ChooseReplacement","Выбрать заменяемый слот")):
            Selected?FText::Format(Localized(TEXT("UI.Shop.BuyPrice"),LOCTEXT("BuyPrice","Купить · {0}")),FText::AsNumber(Selected->Price)):
            Localized(TEXT("UI.Shop.Buy"),LOCTEXT("Buy","Купить")));
    ApplyTheme();
}
void UMCHubShopWidget::CardSelected(FName InSlot,FName ItemID,FName CatalogID) {SelectCatalogItem(CatalogID);}
void UMCHubShopWidget::BuyClicked()
{
    if(bEditorFixture || IsDesignTime()) return;
    FText Error;auto* Economy=ObservedEconomy.Get();
    if(!Economy || !Economy->CanPurchase(SelectedItemID,Error)) {SetTransactionMessage(Error);return;}
    FMCHubEconomyCatalogEntry Entry;
    if(!Economy->FindCatalogEntry(SelectedItemID,Entry)) return;
    const auto Slots=Economy->GetPendingActiveSlots();
    const bool UsesSlot=Entry.PerkKind==EMCPerkKind::Active || Entry.PerkKind==EMCPerkKind::Consumable;
    if(UsesSlot && Slots.Num()==4 && !Slots.ContainsByPredicate([](const auto& S){return S.PerkID.IsNone();})) {
        bChoosingReplacement=true;RefreshEconomy();SetTransactionMessage(Localized(TEXT("UI.Shop.ReplacePrompt"),LOCTEXT("ReplacePrompt","Выбери слот для замены.")));return;
    }
    RequestPurchase(INDEX_NONE,NAME_None,FGuid());
}

void UMCHubShopWidget::SelectReplacementSlot(int32 SlotIndex)
{
    auto* Economy=ObservedEconomy.Get();if(!bChoosingReplacement || !Economy) return;
    const auto Slots=Economy->GetPendingActiveSlots();if(!Slots.IsValidIndex(SlotIndex)) return;
    RequestPurchase(SlotIndex,Slots[SlotIndex].PerkID,Slots[SlotIndex].InstanceID);
}
void UMCHubShopWidget::RequestPurchase(int32 SkillSlot,FName ExpectedID,const FGuid& ExpectedInstance)
{
    FText Error;auto* Economy=ObservedEconomy.Get();
    if(!Economy || !Economy->CanPurchaseForSlot(SelectedItemID,SkillSlot,ExpectedID,ExpectedInstance,Error)) {SetTransactionMessage(Error);return;}
    if(OnPurchaseSlotRequested.IsBound()) OnPurchaseSlotRequested.Broadcast(SelectedItemID,SkillSlot,ExpectedID,ExpectedInstance);
    else if(SkillSlot==INDEX_NONE) OnPurchaseRequested.Broadcast(SelectedItemID);
    else {SetTransactionMessage(LOCTEXT("ReplacementNotConnected","Замена слота пока недоступна."));return;}
    bChoosingReplacement=false;RefreshEconomy();
}

void UMCHubShopWidget::ResolveControls()
{
    if(!WidgetTree) return;
    ShopPanel=Cast<UBorder>(WidgetTree->FindWidget(TEXT("ShopPanel")));
    ShopClose=Cast<UButton>(WidgetTree->FindWidget(TEXT("ShopClose")));
    ShopBuy=Cast<UButton>(WidgetTree->FindWidget(TEXT("ShopBuy")));
    ShopBalance=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("ShopBalance")));
    ShopSelectedName=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("ShopSelectedName")));
    ShopSelectedDescription=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("ShopSelectedDescription")));
    ShopTransactionMessage=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("ShopTransactionMessage")));
    ShopCatalogGrid=Cast<UUniformGridPanel>(WidgetTree->FindWidget(TEXT("ShopCatalogGrid")));
}

void UMCHubShopWidget::NativeOnInitialized() {Super::NativeOnInitialized();SetIsFocusable(true);}
void UMCHubShopWidget::NativeConstruct()
{
    Super::NativeConstruct();ResolveControls();
    if(ShopClose) ShopClose->OnClicked.AddUniqueDynamic(this,&ThisClass::CloseClicked);
    if(ShopBuy) ShopBuy->OnClicked.AddUniqueDynamic(this,&ThisClass::BuyClicked);
    if(!ObservedEconomy.IsValid()) if(auto* Player=GetOwningPlayerState<AMCPlayerState>()) ObservedEconomy=Player->FindComponentByClass<UMCHubEconomyComponent>();
    if(auto* Economy=ObservedEconomy.Get()) Economy->OnEconomyChanged.AddUniqueDynamic(this,&ThisClass::RefreshEconomy);
    RefreshEconomy();
}
void UMCHubShopWidget::NativeDestruct()
{
    if(ShopClose) ShopClose->OnClicked.RemoveAll(this);
    if(ShopBuy) ShopBuy->OnClicked.RemoveAll(this);
    if(auto* Economy=ObservedEconomy.Get()) Economy->OnEconomyChanged.RemoveAll(this);
    Super::NativeDestruct();
}
void UMCHubShopWidget::CloseClicked() {if(!bEditorFixture && !IsDesignTime()) OnClose.Broadcast();}
FReply UMCHubShopWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if((Event.GetKey()==EKeys::Escape || Event.GetKey()==EKeys::Gamepad_FaceButton_Right) && !Event.IsRepeat()) {
        if(bChoosingReplacement) {bChoosingReplacement=false;RefreshEconomy();SetTransactionMessage(FText::GetEmpty());}
        else CloseClicked();
        return FReply::Handled();
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
    Text(TEXT("ShopTitle"),30,P.Cream);Text(TEXT("ShopEmptyMessage"),15,P.OnMarine,false);
    Text(TEXT("ShopBalance"),18,P.Mint);Text(TEXT("ShopSelectedName"),18,P.Cream);
    Text(TEXT("ShopSelectedDescription"),14,P.OnMarine,false);Text(TEXT("ShopTransactionMessage"),13,P.Cream,false);
    for(int32 Index=0;Index<6;++Index) {
        if(auto* Card=Cast<UBorder>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("ShopEmptySlot%d"),Index))))) {
            MCUIVisualStyle::Panel(Card,Style,P.Marine.CopyWithNewOpacity(.45f));Card->SetIsEnabled(false);
        }
        Text(FName(*FString::Printf(TEXT("ShopEmptyLabel%d"),Index)),14,P.OnMarine,false);
    }
    MCUIVisualStyle::Button(ShopClose,Style,P.Mint);Text(TEXT("ShopCloseLabel"),18,P.ButtonText);
    MCUIVisualStyle::Button(ShopBuy,Style,P.Accent);Text(TEXT("ShopBuyLabel"),17,P.ButtonText);
    for(int32 I=0;I<ReplacementButtons.Num();++I) {
        MCUIVisualStyle::Button(ReplacementButtons[I],Style,P.Mint);
        Text(FName(*FString::Printf(TEXT("ShopReplaceLabel%d"),I)),12,P.ButtonText);
    }
    for(UMCCosmeticItemWidget* Card:CatalogCards) if(Card) Card->ApplyTheme();
}
void UMCHubShopWidget::ApplyEditorPreview(float Time)
{
    bEditorFixture=true;BuildPresentationTree();Super::ApplyEditorPreview(Time);RefreshEconomy();
}
void UMCHubShopWidget::ResetEditorPreview() {Super::ResetEditorPreview();}

#undef LOCTEXT_NAMESPACE
