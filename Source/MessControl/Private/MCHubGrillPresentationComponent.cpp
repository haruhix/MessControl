#include "MCHubGrillPresentationComponent.h"
#include "MCHubGrillStation.h"
#include "MCHubEconomyComponent.h"
#include "MCPlayerState.h"
#include "MCGameplayUIFactory.h"
#include "MCUIVisualStyle.h"
#include "MCWorldUIShapes.h"
#include "MCToothCharacter.h"
#include "MCFishingRodComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetComponent.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WidgetSwitcherSlot.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Layout/Clipping.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SWidget.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"

#define LOCTEXT_NAMESPACE "MCHubGrillHologram"

namespace MCGrillStrip
{
constexpr float Width=680.f;
constexpr float Height=133.f;
constexpr float ShellWidth=Width+30.f;
constexpr float CurrentHeight=226.f;
constexpr float CardWidth=116.f;
constexpr float CardGap=9.f;
constexpr int32 FirstCard=3;
constexpr int32 WinningCard=27;
constexpr int32 CardCount=WinningCard+4;

const FSlateRoundedBoxBrush Rail{FLinearColor::White,10.f};
const FSlateRoundedBoxBrush CardFace{FLinearColor::White,8.f};
const FSlateRoundedBoxBrush Marker{FLinearColor::White,1.f};
const FLinearColor Background{FColor::FromHex(TEXT("151F40"))};
const FLinearColor Paper{FColor::FromHex(TEXT("FFFDFD"))};
const FLinearColor Gold{FColor::FromHex(TEXT("FFDA49"))};

USizeBox* InstallSlot(UWidgetTree* Tree,UHorizontalBox* Row,FName Name)
{
    if(!Row) return nullptr;
    if(auto* Existing=Cast<USizeBox>(Tree->FindWidget(Name));Existing && Existing->GetParent()==Row) return Existing;
    Row->ClearChildren();
    auto* Slot=Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),Name);
    Slot->SetWidthOverride(Width);Slot->SetHeightOverride(Height);
    Slot->SetContent(Tree->ConstructWidget<USpacer>());
    Slot->SetVisibility(ESlateVisibility::HitTestInvisible);
    Row->AddChildToHorizontalBox(Slot);
    return Slot;
}

FLinearColor RarityColor(EMCHubItemRarity Rarity,bool bTrash)
{
    if(bTrash) return FLinearColor(FColor::FromHex(TEXT("8E98AE")));
    switch(Rarity) {
    case EMCHubItemRarity::Rare: return FLinearColor(FColor::FromHex(TEXT("5586FF")));
    case EMCHubItemRarity::Epic: return FLinearColor(FColor::FromHex(TEXT("B763FD")));
    case EMCHubItemRarity::Legendary: return FLinearColor(FColor::FromHex(TEXT("FFC14F")));
    default: return FLinearColor(FColor::FromHex(TEXT("7EC3E4")));
    }
}
}

UMCHubGrillHologramWidget::UMCHubGrillHologramWidget(const FObjectInitializer& Initializer):Super(Initializer)
{
    bUseVersionedWorldPresentation=true;
}
void UMCHubGrillHologramWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(WidgetTree->RootWidget) {ResolveControls();BuildWorldPresentationTree();return;}
    auto* Size=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("HologramDesignSize"));
    Size->SetWidthOverride(MCGrillStrip::ShellWidth);Size->SetHeightOverride(MCGrillStrip::CurrentHeight);WidgetTree->RootWidget=Size;
    HologramPanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("HologramPanel"));
    HologramPanel->SetPadding(FMargin(13,8));Size->SetContent(HologramPanel);
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("HologramContent"));HologramPanel->SetContent(Column);
    HologramTitle=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("HologramTitle"));
    HologramTitle->SetText(LOCTEXT("Idle","1 РЫБА · 1 ПОПЫТКА"));HologramTitle->SetJustification(ETextJustify::Center);
    Column->AddChildToVerticalBox(HologramTitle)->SetPadding(FMargin(0,0,0,5));
    auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("HologramReels"));
    Column->AddChildToVerticalBox(Row)->SetHorizontalAlignment(HAlign_Center);
    HologramReward=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("HologramReward"));
    HologramReward->SetJustification(ETextJustify::Center);HologramReward->SetAutoWrapText(true);
    Column->AddChildToVerticalBox(HologramReward)->SetPadding(FMargin(0,5,0,0));
    ResolveControls();BuildWorldPresentationTree();ApplyTheme();
}
void UMCHubGrillHologramWidget::ResolveControls()
{
    if(!WidgetTree) return;
    HologramPanel=Cast<UBorder>(WidgetTree->FindWidget(TEXT("HologramPanel")));
    HologramTitle=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HologramTitle")));
    HologramReward=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HologramReward")));
    if(!HologramReelSlot) {
        if(auto* Size=WidgetTree->FindWidget<USizeBox>(TEXT("HologramDesignSize"))) {
            Size->SetWidthOverride(MCGrillStrip::ShellWidth);Size->SetHeightOverride(MCGrillStrip::CurrentHeight);
        }
        HologramReelSlot=MCGrillStrip::InstallSlot(WidgetTree,WidgetTree->FindWidget<UHorizontalBox>(TEXT("HologramReels")),TEXT("HologramReelSlot"));
    }
}
void UMCHubGrillHologramWidget::SetStation(AMCHubGrillStation* Station)
{Source=Station;BuildPresentationTree();ApplyTheme();TickPresentation(0);}
void UMCHubGrillHologramWidget::ApplyTheme()
{
    BuildPresentationTree();ResolveControls();Super::ApplyTheme();const auto* ResolvedTheme=GetResolvedTheme();if(!ResolvedTheme || !WidgetTree) return;
    const auto& P=ResolvedTheme->Palette;MCUIVisualStyle::Panel(HologramPanel,ResolvedTheme,P.Backdrop.CopyWithNewOpacity(.77f));
    for(auto* Label:{HologramTitle.Get(),HologramReward.Get()}) if(Label) {
        Label->SetFont(ResolvedTheme->MakeGameplayFont(18,true));Label->SetColorAndOpacity(P.Cream);
    }
    const auto* NewTheme=GetNewWorldPresentationTheme();
    MCWorldUIShapes::TextStyle(NewHologramTitle,NewTheme,20,EMCUIColorToken::Accent);
    MCWorldUIShapes::TextStyle(NewHologramReward,NewTheme,18,EMCUIColorToken::Marine);
    if(NewHologramTitle) NewHologramTitle->SetColorAndOpacity(FLinearColor(FColor::FromHex(TEXT("AE4C6C"))));
    if(NewHologramReward) NewHologramReward->SetColorAndOpacity(FLinearColor(FColor::FromHex(TEXT("16226B"))));
    CardFont=ResolvedTheme->MakeGameplayFont(14,true);
    PreparedEconomy.Reset();bHasNewCaptionMetrics=false;bWorldFontsPrepared=false;PrimeWorldPresentationFonts();RefreshWorldPresentation();
}
void UMCHubGrillHologramWidget::TickPresentation(float DeltaSeconds)
{
    RefreshWorldPresentationSelection();PrimeWorldPresentationFonts();
    const auto* Grill=Source.Get();if(!Grill) {RefreshWorldPresentation();return;}
    const auto* PS=GetOwningPlayer()?GetOwningPlayer()->GetPlayerState<AMCPlayerState>():nullptr;
    const auto* Economy=PS?PS->Economy.Get():nullptr;
    RefreshReel(*Grill,Economy);
    if(HologramTitle) HologramTitle->SetText(Grill->Stage==EMCHubGrillStage::Rolling?Localized(TEXT("UI.Grill.Rolling"),LOCTEXT("Rolling","КРУТИМ…")):
        Grill->Stage==EMCHubGrillStage::Closing?Localized(TEXT("UI.Grill.Accepted"),LOCTEXT("Cooking","РЫБА ПРИНЯТА")):
        bResultCentered?GrillText(TEXT("UI.Grill.Result"),TEXT("ВАШ РЕЗУЛЬТАТ"),TEXT("YOUR RESULT")):
            Localized(TEXT("UI.Grill.Idle"),LOCTEXT("Idle","1 РЫБА · 1 ПОПЫТКА")));
    if(HologramReward) {
        const FMCGrillReward& Reward=Grill->CurrentReward;
        const bool bTrash=Reward.Kind==EMCGrillRewardKind::Trash || (Reward.Kind==EMCGrillRewardKind::Coins && Reward.Coins<=0 && !Reward.bConvertedDuplicate);
        FText Result;
        if(bResultCentered) {
            const FText Name=ReelCards.IsValidIndex(MCGrillStrip::WinningCard)?ReelCards[MCGrillStrip::WinningCard].Caption:FText::FromName(Reward.CatalogItem);
            if(Reward.bConvertedDuplicate) Result=FText::Format(GrillText(TEXT("UI.Grill.Duplicate"),TEXT("ДУБЛИКАТ · {0} · +{1} монет"),TEXT("DUPLICATE · {0} · +{1} coins")),
                Name,FText::AsNumber(Reward.Coins));
            else if(bTrash) Result=GrillText(TEXT("UI.Grill.Trash"),TEXT("МУСОР · БЕЗ НАГРАДЫ"),TEXT("TRASH · NO REWARD"));
            else if(Reward.Kind==EMCGrillRewardKind::Coins)
                Result=FText::Format(Localized(TEXT("UI.Grill.Coins"),LOCTEXT("Coins","+{0} монет")),FText::AsNumber(Reward.Coins));
            else Result=FText::Format(Localized(TEXT("UI.Grill.Item"),LOCTEXT("Item","{0} ×{1}")),Name,FText::AsNumber(Reward.Quantity));
        }
        if(!HologramReward->GetText().EqualTo(Result)) HologramReward->SetText(Result);
        if(const auto* ResolvedTheme=GetResolvedTheme()) HologramReward->SetColorAndOpacity(bTrash?ResolvedTheme->Palette.Warning:ResolvedTheme->Palette.Mint);
    }
    RefreshWorldPresentation();
}
void UMCHubGrillHologramWidget::ApplyEditorPreview(float Time)
{
    if(!IsDesignTime()) {BuildPresentationTree();ApplyTheme();TickPresentation(0);return;}
    BuildPresentationTree();Super::ApplyEditorPreview(Time);
    FReelCard PreviewCards[]={
        {GrillText(TEXT("UI.Grill.Card.Coins"),TEXT("МОНЕТЫ"),TEXT("COINS")),{},ECardIcon::Coin,EMCHubItemRarity::Rare},
        {GrillText(TEXT("UI.Grill.Card.Clothing"),TEXT("ОДЕЖДА"),TEXT("CLOTHING")),{},ECardIcon::Clothing,EMCHubItemRarity::Epic},
        {GrillText(TEXT("UI.Grill.Card.Perk"),TEXT("ПЕРК"),TEXT("PERK")),{},ECardIcon::Perk,EMCHubItemRarity::Rare},
        {GrillText(TEXT("UI.Grill.Card.Booster"),TEXT("БУСТЕР"),TEXT("BOOSTER")),{},ECardIcon::Booster,EMCHubItemRarity::Legendary},
        {GrillText(TEXT("UI.Grill.Card.Trash"),TEXT("МУСОР"),TEXT("TRASH")),{},ECardIcon::Trash}
    };
    for(auto& Card:PreviewCards) PrepareCardText(Card);
    ReelCards.Reset(MCGrillStrip::CardCount);
    for(int32 Index=0;Index<MCGrillStrip::CardCount;++Index) ReelCards.Add(PreviewCards[Index%UE_ARRAY_COUNT(PreviewCards)]);
    bResultCentered=PreviewState==TEXT("NoPrize") || PreviewState==TEXT("Duplicate");
    ReelCursor=bResultCentered?float(MCGrillStrip::WinningCard):
        FMath::Lerp(float(MCGrillStrip::FirstCard),float(MCGrillStrip::WinningCard),1.f-FMath::Pow(1.f-FMath::Clamp(Time/2.f,0.f,1.f),3.f));
    if(HologramTitle) HologramTitle->SetText(Localized(TEXT("UI.Grill.Rolling"),LOCTEXT("Rolling","КРУТИМ…")));
    if(HologramReward) HologramReward->SetText(FText::GetEmpty());
    if(PreviewState==TEXT("Idle")) {
        ReelCursor=MCGrillStrip::FirstCard;
        if(HologramTitle) HologramTitle->SetText(Localized(TEXT("UI.Grill.Idle"),LOCTEXT("Idle","1 РЫБА · 1 ПОПЫТКА")));
    } else if(bResultCentered) {
        const bool bTrash=PreviewState==TEXT("NoPrize");
        ReelCards[MCGrillStrip::WinningCard]=PreviewCards[bTrash?4:1];
        if(HologramTitle) HologramTitle->SetText(GrillText(TEXT("UI.Grill.Result"),TEXT("ВАШ РЕЗУЛЬТАТ"),TEXT("YOUR RESULT")));
        if(HologramReward) HologramReward->SetText(bTrash?GrillText(TEXT("UI.Grill.Trash"),TEXT("МУСОР · БЕЗ НАГРАДЫ"),TEXT("TRASH · NO REWARD")):
            FText::Format(GrillText(TEXT("UI.Grill.Duplicate"),TEXT("ДУБЛИКАТ · {0} · +{1} монет"),TEXT("DUPLICATE · {0} · +{1} coins")),
                PreviewCards[1].Caption,FText::AsNumber(4)));
    }
    Invalidate(EInvalidateWidgetReason::Paint);
    RefreshWorldPresentation();
}

void UMCHubGrillHologramWidget::BuildWorldPresentationTree()
{
    if(!bUseVersionedWorldPresentation || !WidgetTree) return;
    auto* NewRoot=WidgetTree->FindWidget<USizeBox>(TEXT("WorldGrillV2Root"));
    if(!NewRoot && WidgetTree->FindWidget(TEXT("WorldGrillV2Root"))) return;
    if(!NewRoot) {
        auto* NewTheme=GetNewWorldPresentationTheme();
        auto Shape=[&](FName ShapeName,FLinearColor FillColor,FMargin Inset,FLinearColor OutlineColor,FVector2D ShadowOffset) {
            auto* NewPanel=MCWorldUIShapes::Panel(WidgetTree,ShapeName,NewTheme,EMCUIColorToken::Marine,Inset,12.f);
            NewPanel->bCustomColors=true;NewPanel->CustomFill=FillColor;NewPanel->CustomText=FLinearColor::White;
            NewPanel->CustomOutline=OutlineColor;NewPanel->CustomShadow=OutlineColor;NewPanel->ShadowOffset=ShadowOffset;NewPanel->ApplyTheme();return NewPanel;
        };
        const FLinearColor Blue(FColor::FromHex(TEXT("4C76AE"))),Navy(FColor::FromHex(TEXT("16226B")));
        const FLinearColor Paper(FColor::FromHex(TEXT("FFFDFD")));
        NewRoot=MCWorldUIShapes::Bounds(WidgetTree,TEXT("WorldGrillV2Root"),MCGrillStrip::ShellWidth,190);
        auto* Shell=Shape(TEXT("WorldGrillV2Panel"),Blue,FMargin(14,10,14,14),Navy,FVector2D(4,5));NewRoot->SetContent(Shell);
        auto* Body=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("WorldGrillV2Body"));Shell->SetContentForSlot(TEXT("Content"),Body);
        auto* HeadingPlate=Shape(TEXT("WorldGrillV2TitlePlate"),Paper,FMargin(10,6),Navy,FVector2D::ZeroVector);
        Body->AddChildToVerticalBox(HeadingPlate)->SetPadding(FMargin(0,0,0,9));
        auto* Heading=MCWorldUIShapes::Text(WidgetTree,TEXT("WorldGrillV2Title"),342);Heading->SetAutoWrapText(false);HeadingPlate->SetContentForSlot(TEXT("Content"),Heading);
        auto* ReelRow=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("WorldGrillV2Reels"));
        Body->AddChildToVerticalBox(ReelRow)->SetHorizontalAlignment(HAlign_Center);
        auto* RewardPlate=Shape(TEXT("WorldGrillV2RewardPlate"),Paper,FMargin(10,7),Navy,FVector2D::ZeroVector);
        Body->AddChildToVerticalBox(RewardPlate)->SetPadding(FMargin(0,10,0,0));
        auto* RewardLabel=MCWorldUIShapes::Text(WidgetTree,TEXT("WorldGrillV2Reward"),342);RewardLabel->SetAutoWrapText(false);RewardPlate->SetContentForSlot(TEXT("Content"),RewardLabel);
    }
    ResolveWorldControls(NewRoot);
    const bool Complete=NewHologramRoot && NewHologramTitle && NewHologramReward && NewHologramRewardCard && NewHologramReelSlot;
    if(!Complete) return; // Preserve a malformed authored source tree; never switch to an incomplete replacement.
    InstallWorldPresentation(NewRoot);
    if(auto* Versions=WidgetTree->FindWidget<UWidgetSwitcher>(TEXT("WorldPresentationVersions")))
        if(auto* CurrentRoot=Versions->GetWidgetAtIndex(0)) if(auto* CurrentCell=Cast<UWidgetSwitcherSlot>(CurrentRoot->Slot)) {
            CurrentCell->SetHorizontalAlignment(HAlign_Fill);CurrentCell->SetVerticalAlignment(VAlign_Fill);
        }
}
void UMCHubGrillHologramWidget::ResolveWorldControls(UWidget* SearchRoot)
{
    if(!WidgetTree) return;
    // A newly authored branch is detached until it is validated. FindWidget
    // searches only the attached tree, so inspect this branch explicitly first.
    auto Find=[&](FName Name)->UWidget* {
        if(!SearchRoot) return WidgetTree->FindWidget(Name);
        UWidget* Found=SearchRoot->GetFName()==Name?SearchRoot:nullptr;
        UWidgetTree::ForWidgetAndChildren(SearchRoot,[&](UWidget* Child) {
            if(Child->GetFName()==Name) Found=Child;
        });
        return Found;
    };
    NewHologramRoot=Cast<USizeBox>(Find(TEXT("WorldGrillV2Root")));
    NewHologramTitle=Cast<UTextBlock>(Find(TEXT("WorldGrillV2Title")));
    NewHologramReward=Cast<UTextBlock>(Find(TEXT("WorldGrillV2Reward")));
    NewHologramRewardCard=Cast<UMCUniversalUIWidget>(Find(TEXT("WorldGrillV2RewardPlate")));
    NewHologramReelSlot=Cast<USizeBox>(Find(TEXT("WorldGrillV2ReelSlot")));
    if(!NewHologramReelSlot) NewHologramReelSlot=MCGrillStrip::InstallSlot(WidgetTree,Cast<UHorizontalBox>(Find(TEXT("WorldGrillV2Reels"))),TEXT("WorldGrillV2ReelSlot"));
}
void UMCHubGrillHologramWidget::RefreshWorldPresentation()
{
    if(!NewHologramTitle || !NewHologramReward) return;
    // Dynamic runtime captions remain source-owned and translation-table aware.
    const FText TitleCopy=IsDesignTime() && !PreviewTitleOverride.IsEmpty()?PreviewTitleOverride:HologramTitle?HologramTitle->GetText():FText::GetEmpty();
    const FText RewardCopy=IsDesignTime() && !PreviewRewardOverride.IsEmpty()?PreviewRewardOverride:HologramReward?HologramReward->GetText():FText::GetEmpty();
    if(!NewHologramTitle->GetText().EqualTo(TitleCopy)) NewHologramTitle->SetText(TitleCopy);
    if(!NewHologramReward->GetText().EqualTo(RewardCopy)) NewHologramReward->SetText(RewardCopy);
    if(NewHologramRewardCard) NewHologramRewardCard->SetVisibility(RewardCopy.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    RefreshWorldCaptionLayout();
    // Keep Current until the New tree and its fonts are prepared.
    if(!IsDesignTime() && GetWorldPresentationVersion()==EMCUIVersion::New && !HasPreparedNewPresentation())
        if(auto* Versions=WidgetTree->FindWidget<UWidgetSwitcher>(TEXT("WorldPresentationVersions"))) Versions->SetActiveWidgetIndex(0);
}
void UMCHubGrillHologramWidget::RefreshWorldCaptionLayout()
{
    if(!NewHologramRoot || !NewHologramTitle || !NewHologramReward || !FSlateApplication::IsInitialized()) return;
    const auto Renderer=FSlateApplication::Get().GetRenderer();if(!Renderer) return;
    const auto* OuterPlate=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("WorldGrillV2Panel"));
    const auto* HeadingPlate=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("WorldGrillV2TitlePlate"));
    const FMargin OuterPadding=OuterPlate?OuterPlate->ContentPadding:FMargin(14,10,14,14);
    const FMargin HeadingPadding=HeadingPlate?HeadingPlate->ContentPadding:FMargin(10,6);
    const FMargin RewardPadding=NewHologramRewardCard?NewHologramRewardCard->ContentPadding:FMargin(10,7);
    const float Maximum=FMath::Max(MCGrillStrip::ShellWidth,MaximumNewCaptionWidth);
    const float PaintScale=GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale();
    const float FontScale=FMath::IsFinite(PaintScale) && PaintScale>UE_SMALL_NUMBER?PaintScale:1.f;
    const FText TitleCopy=NewHologramTitle->GetText(),RewardCopy=NewHologramReward->GetText();
    const FSlateFontInfo TitleFont=NewHologramTitle->GetFont(),RewardFont=NewHologramReward->GetFont();
    if(bHasNewCaptionMetrics && MeasuredNewTitle.EqualTo(TitleCopy) && MeasuredNewReward.EqualTo(RewardCopy)
        && MeasuredNewTitleFont==TitleFont && MeasuredNewRewardFont==RewardFont
        && MeasuredNewOuterPadding==OuterPadding && MeasuredNewHeadingPadding==HeadingPadding && MeasuredNewRewardPadding==RewardPadding
        && FMath::IsNearlyEqual(MeasuredNewCaptionMaximum,Maximum,.1f) && FMath::IsNearlyEqual(MeasuredNewCaptionScale,FontScale,.001f)) return;
    const auto Measure=Renderer->GetFontMeasureService();
    const float HeadingInset=HeadingPadding.Left+HeadingPadding.Right,RewardInset=RewardPadding.Left+RewardPadding.Right;
    const float CaptionAdvance=FMath::Max(Measure->Measure(TitleCopy,TitleFont,FontScale).X/FontScale+HeadingInset,
        Measure->Measure(RewardCopy,RewardFont,FontScale).X/FontScale+RewardInset);
    const float OuterInset=OuterPadding.Left+OuterPadding.Right;
    const float DesiredWidth=FMath::Clamp(FMath::CeilToFloat(CaptionAdvance+OuterInset+2.f),MCGrillStrip::ShellWidth,Maximum);
    NewHologramRoot->SetWidthOverride(DesiredWidth);
    NewHologramTitle->SetAutoWrapText(false);NewHologramTitle->SetWrapTextAt(FMath::Max(32.f,DesiredWidth-OuterInset-HeadingInset));
    NewHologramReward->SetAutoWrapText(false);NewHologramReward->SetWrapTextAt(FMath::Max(32.f,DesiredWidth-OuterInset-RewardInset));
    // No height limit or font reduction: real wrapped text determines the card's
    // desired height. The screen host keeps its original bottom-aligned pivot.
    const auto SlateRoot=NewHologramRoot->TakeWidget();SlateRoot->MarkPrepassAsDirty();SlateRoot->SlatePrepass(FontScale);
    MeasuredNewTitle=TitleCopy;MeasuredNewReward=RewardCopy;MeasuredNewTitleFont=TitleFont;MeasuredNewRewardFont=RewardFont;
    MeasuredNewOuterPadding=OuterPadding;MeasuredNewHeadingPadding=HeadingPadding;MeasuredNewRewardPadding=RewardPadding;
    MeasuredNewCaptionMaximum=Maximum;MeasuredNewCaptionScale=FontScale;bHasNewCaptionMetrics=true;
}
void UMCHubGrillHologramWidget::PrimeWorldPresentationFonts()
{
    if(bWorldFontsPrepared || !NewHologramTitle || !NewHologramReward || !FSlateApplication::IsInitialized()) return;
    const auto Renderer=FSlateApplication::Get().GetRenderer();if(!Renderer) return;
    const auto Measure=Renderer->GetFontMeasureService();
    const FText Corpus[]={
        Localized(TEXT("UI.Grill.Rolling"),LOCTEXT("Rolling","КРУТИМ…")),
        Localized(TEXT("UI.Grill.Accepted"),LOCTEXT("Cooking","РЫБА ПРИНЯТА")),
        Localized(TEXT("UI.Grill.Idle"),LOCTEXT("Idle","1 РЫБА · 1 ПОПЫТКА")),
        GrillText(TEXT("UI.Grill.Result"),TEXT("ВАШ РЕЗУЛЬТАТ"),TEXT("YOUR RESULT")),
        GrillText(TEXT("UI.Grill.Duplicate"),TEXT("ДУБЛИКАТ · {0} · +{1} монет"),TEXT("DUPLICATE · {0} · +{1} coins")),
        GrillText(TEXT("UI.Grill.Trash"),TEXT("МУСОР · БЕЗ НАГРАДЫ"),TEXT("TRASH · NO REWARD")),
        FText::AsCultureInvariant(TEXT("0123456789 +×%…"))
    };
    for(const FText& Copy:Corpus) {
        Measure->Measure(Copy,NewHologramTitle->GetFont(),1.f);Measure->Measure(Copy,NewHologramReward->GetFont(),1.f);
    }
    bWorldFontsPrepared=true;
}
bool UMCHubGrillHologramWidget::HasPreparedNewPresentation() const
{
    return NewHologramRoot && NewHologramTitle && NewHologramReward && NewHologramRewardCard && NewHologramReelSlot && bWorldFontsPrepared;
}

FText UMCHubGrillHologramWidget::GrillText(FName Key,const TCHAR* Russian,const TCHAR* English) const
{
    const bool bRussian=IsDesignTime() && !PreviewCulture.IsEmpty()?PreviewCulture.StartsWith(TEXT("ru")):
        FInternationalization::Get().GetCurrentLanguage()->GetTwoLetterISOLanguageName()==TEXT("ru");
    return Localized(Key,FText::AsCultureInvariant(bRussian?Russian:English));
}

UMCHubGrillHologramWidget::FReelCard UMCHubGrillHologramWidget::MakeCard(const FMCGrillReward& Reward,const UMCHubEconomyComponent* Economy) const
{
    FReelCard Card;
    Card.Rarity=Reward.Rarity;
    if(Reward.Kind==EMCGrillRewardKind::CatalogItem || (Reward.bConvertedDuplicate && !Reward.CatalogItem.IsNone())) {
        FMCHubEconomyCatalogEntry Entry;
        if(Economy && Economy->FindCatalogEntry(Reward.CatalogItem,Entry)) {
            const auto& Definition=Entry.Definition;
            Card.Caption=Localized(Definition.DisplayNameKey,Definition.DisplayName);
            // The Hub already retains catalog and perk icons before readiness.
            // No interaction-time load is needed, including for prize thumbnails.
            Card.Icon=Definition.Icon;
            Card.Shape=Definition.Kind==EMCHubCatalogKind::Cosmetic?ECardIcon::Clothing:
                Entry.PerkKind==EMCPerkKind::Passive?ECardIcon::Perk:ECardIcon::Booster;
        }
        if(Card.Caption.IsEmpty()) Card.Caption=FText::FromName(Reward.CatalogItem);
    } else if(Reward.Kind==EMCGrillRewardKind::Trash || (!Reward.bConvertedDuplicate && Reward.Coins<=0)) {
        Card.Caption=GrillText(TEXT("UI.Grill.Card.Trash"),TEXT("МУСОР"),TEXT("TRASH"));
        Card.Shape=ECardIcon::Trash;
    } else {
        Card.Caption=GrillText(TEXT("UI.Grill.Card.Coins"),TEXT("МОНЕТЫ"),TEXT("COINS"));
        Card.Shape=ECardIcon::Coin;
    }
    PrepareCardText(Card);
    return Card;
}

void UMCHubGrillHologramWidget::PrepareCardText(FReelCard& Card) const
{
    Card.RailCaption=Card.Caption;
    if(Card.Caption.IsEmpty() || !FSlateApplication::IsInitialized()) return;
    const auto Renderer=FSlateApplication::Get().GetRenderer();if(!Renderer) return;
    const auto Measure=Renderer->GetFontMeasureService();
    const FSlateFontInfo Font=CardFont.HasValidFont()?CardFont:FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),14);
    const float Available=MCGrillStrip::CardWidth-12.f;
    Card.RailCaptionWidth=Measure->Measure(Card.RailCaption,Font).X;
    if(Card.RailCaptionWidth>Available) {
        const FString Ellipsis(TEXT("…"));
        const int32 OffsetLimit=FMath::FloorToInt(Available-Measure->Measure(Ellipsis,Font).X);
        const int32 Last=Measure->FindLastWholeCharacterIndexBeforeOffset(Card.RailCaption,Font,OffsetLimit);
        Card.RailCaption=FText::FromString(Card.RailCaption.ToString().Left(FMath::Max(0,Last+1))+Ellipsis);
        Card.RailCaptionWidth=Measure->Measure(Card.RailCaption,Font).X;
    }
    for(const auto* RewardLabel:{HologramReward.Get(),NewHologramReward.Get()})
        if(RewardLabel) Measure->Measure(Card.Caption,RewardLabel->GetFont(),1.f);
}

void UMCHubGrillHologramWidget::RefreshReel(const AMCHubGrillStation& Grill,const UMCHubEconomyComponent* Economy)
{
    using namespace MCGrillStrip;
    const FMCGrillReward& Reward=Grill.CurrentReward;
    const bool bNewPool=Economy && PreparedEconomy.Get()!=Economy && Economy->CatalogTable.Get()
        && Economy->GrillRewardsTable.Get() && Economy->RulesTable.Get();
    if(bNewPool) {
        FillerCards.Reset();
        for(const auto& Candidate:Economy->GetGrillPreviewPool()) FillerCards.Add(MakeCard(Candidate,Economy));
        PreparedEconomy=Economy;
    }
    const bool bNewRoll=DisplayedRoll!=Reward.RollID;
    if(bNewPool || bNewRoll || ReelCards.IsEmpty()) {
        ReelCards.Reset(CardCount);
        FRandomStream CosmeticRandom(int32(GetTypeHash(Reward.RollID)));
        for(int32 Index=0;Index<CardCount;++Index)
            ReelCards.Add(FillerCards.IsEmpty()?FReelCard():FillerCards[CosmeticRandom.RandRange(0,FillerCards.Num()-1)]);
        DisplayedRoll=Reward.RollID;
        if(DisplayedRoll.IsValid()) ReelCards[WinningCard]=MakeCard(Reward,Economy);
    }
    const bool bWasCentered=bResultCentered;
    bResultCentered=Reward.RollID.IsValid() && (Grill.Stage==EMCHubGrillStage::Revealing || Grill.Stage==EMCHubGrillStage::Opening);
    if(bResultCentered && !bWasCentered && !bNewRoll) ReelCards[WinningCard]=MakeCard(Reward,Economy);
    float NextReelPosition=float(bResultCentered?WinningCard:FirstCard);
    if(Reward.RollID.IsValid() && Grill.Stage==EMCHubGrillStage::Rolling) {
        const auto* GS=GetWorld()?GetWorld()->GetGameState():nullptr;
        const double Now=GS?GS->GetServerWorldTimeSeconds():(GetWorld()?GetWorld()->GetTimeSeconds():Grill.StageStartedAt);
        const float Alpha=float(FMath::Clamp((Now-Grill.StageStartedAt)/FMath::Max(.1,Grill.StageEndsAt-Grill.StageStartedAt),0.,1.));
        NextReelPosition=FMath::Lerp(float(FirstCard),float(WinningCard),1.f-FMath::Pow(1.f-Alpha,3.f));
    }
    if(bNewPool || bNewRoll || bWasCentered!=bResultCentered || ReelCursor!=NextReelPosition) Invalidate(EInvalidateWidgetReason::Paint);
    ReelCursor=NextReelPosition;
}

void UMCHubGrillHologramWidget::OnLocalizationChanged()
{
    Super::OnLocalizationChanged();
    PreparedEconomy.Reset();bWorldFontsPrepared=false;
}

void UMCHubGrillHologramWidget::DrawCardIcon(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& IconGeometry,
    const FReelCard& Card,const FLinearColor& Tint) const
{
    if(auto* Texture=Card.Icon.Get()) {
        FSlateBrush Brush;
        Brush.SetResourceObject(Texture);Brush.DrawAs=ESlateBrushDrawType::Image;Brush.ImageType=ESlateBrushImageType::FullColor;
        const FVector2D SourceSize(FMath::Max(1,Texture->GetSizeX()),FMath::Max(1,Texture->GetSizeY()));
        const FVector2D Bounds=IconGeometry.GetLocalSize();
        const FVector2D Size=SourceSize*FMath::Min(Bounds.X/SourceSize.X,Bounds.Y/SourceSize.Y);
        FSlateDrawElement::MakeBox(Elements,Layer,IconGeometry.ToPaintGeometry(Size,FSlateLayoutTransform((Bounds-Size)*.5)),
            &Brush,ESlateDrawEffect::None,Tint);
        return;
    }
    const FGeometry Shape=IconGeometry.MakeChild(FVector2D(64,64),
        FSlateLayoutTransform((IconGeometry.GetLocalSize()-FVector2D(64,64))*.5));
    const float LineWidth=3.f*Shape.GetAccumulatedLayoutTransform().GetScale();
    auto Line=[&](const TArray<FVector2D>& Points) {
        FSlateDrawElement::MakeLines(Elements,Layer,Shape.ToPaintGeometry(),Points,ESlateDrawEffect::None,Tint,true,LineWidth);
    };
    switch(Card.Shape) {
    case ECardIcon::Clothing:
        Line({{21,12},{13,15},{4,27},{16,34},{22,27},{22,53},{42,53},{42,27},{48,34},{60,27},{51,15},{43,12},{37,18},{27,18},{21,12}});
        break;
    case ECardIcon::Booster:
        Line({{36,6},{14,35},{29,35},{24,59},{51,26},{35,26},{36,6}});
        break;
    case ECardIcon::Coin:
        FSlateDrawElement::MakeBox(Elements,Layer,Shape.ToPaintGeometry(FVector2D(41,41),FSlateLayoutTransform(FVector2D(12,12))),
            &MCGrillStrip::Rail,ESlateDrawEffect::None,MCGrillStrip::Gold.CopyWithNewOpacity(Tint.A));
        Line({{35,21},{27,25},{27,40},{35,44}});
        break;
    case ECardIcon::Trash:
        Line({{14,19},{51,19}});Line({{24,18},{24,11},{41,11},{41,18}});
        Line({{18,22},{22,52},{43,52},{48,22}});Line({{29,27},{29,45}});Line({{37,27},{37,45}});
        break;
    default:
        Line({{32,7},{39,25},{57,32},{39,39},{32,57},{25,39},{7,32},{25,25},{32,7}});
        break;
    }
}

int32 UMCHubGrillHologramWidget::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
    FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const
{
    using namespace MCGrillStrip;
    const int32 Top=Super::NativePaint(Args,Geometry,CullingRect,Elements,Layer,Style,ParentEnabled);
    const auto* ActiveReelSlot=IsNewWorldPresentation()?NewHologramReelSlot.Get():HologramReelSlot.Get();
    if(!ActiveReelSlot || ReelCards.IsEmpty()) return Top;
    const FGeometry& ReelGeometry=ActiveReelSlot->GetPaintSpaceGeometry();
    const FVector2D Bounds=ReelGeometry.GetLocalSize();
    if(Bounds.X<=0 || Bounds.Y<=0) return Top;
    const double Center=Bounds.X*.5;
    const double Step=CardWidth+CardGap;
    const FLinearColor StyleTint=Style.GetColorAndOpacityTint();
    auto Box=[&](int32 Depth,const FSlateBrush& Brush,FVector2D Position,FVector2D Size,FLinearColor Color) {
        FSlateDrawElement::MakeBox(Elements,Top+Depth,ReelGeometry.ToPaintGeometry(Size,FSlateLayoutTransform(Position)),
            &Brush,ESlateDrawEffect::None,Color*StyleTint);
    };
    Box(1,Rail,FVector2D::ZeroVector,Bounds,Background);
    Elements.PushClip(FSlateClippingZone(ReelGeometry));
    const FSlateFontInfo Font=CardFont.HasValidFont()?CardFont:FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),14);
    const int32 Start=FMath::Max(0,FMath::FloorToInt(ReelCursor-(Center+CardWidth)/Step));
    const int32 End=FMath::Min(ReelCards.Num()-1,FMath::CeilToInt(ReelCursor+(Center+CardWidth)/Step));
    for(int32 Index=Start;Index<=End;++Index) {
        const FReelCard& Card=ReelCards[Index];
        const double Offset=(Index-ReelCursor)*Step;
        const double Left=Center+Offset-CardWidth*.5;
        const double TileHeight=Bounds.Y-20.;
        if(Left>=Bounds.X || Left+CardWidth<=0) continue;
        const float Fade=1.f-.57f*FMath::SmoothStep(.25f,1.f,FMath::Clamp(float(FMath::Abs(Offset)/Center),0.f,1.f));
        const FLinearColor Accent=RarityColor(Card.Rarity,Card.Shape==ECardIcon::Trash);
        if(bResultCentered && Index==WinningCard) Box(2,CardFace,{Left-2.,8},{CardWidth+4.,TileHeight+4.},Gold);
        const FPaintGeometry Tile=ReelGeometry.ToPaintGeometry(FVector2D(CardWidth,TileHeight),FSlateLayoutTransform(FVector2D(Left,10)));
        const FLinearColor Light=FMath::Lerp(Background,Accent,.23f),Dark=FMath::Lerp(Background,Accent,.43f);
        FSlateDrawElement::MakeGradient(Elements,Top+3,Tile,
            {FSlateGradientStop(FVector2D(0,0),Light.CopyWithNewOpacity(Fade)*StyleTint),
             FSlateGradientStop(FVector2D(0,TileHeight),Dark.CopyWithNewOpacity(Fade)*StyleTint)},
            Orient_Vertical,ESlateDrawEffect::None,FVector4f(8.f));
        Box(4,Marker,{Left+5.,Bounds.Y-17.},{CardWidth-10.,4.},Accent.CopyWithNewOpacity(Fade));
        const FGeometry IconGeometry=ReelGeometry.MakeChild(FVector2D(78,77),FSlateLayoutTransform(FVector2D(Left+(CardWidth-78.)*.5,14.)));
        DrawCardIcon(Elements,Top+5,IconGeometry,Card,Paper.CopyWithNewOpacity(Fade)*StyleTint);
        if(!Card.RailCaption.IsEmpty()) {
            const double X=Left+FMath::Max(0.,(CardWidth-Card.RailCaptionWidth)*.5);
            const auto LabelGeometry=ReelGeometry.ToPaintGeometry(FVector2D(CardWidth,18),FSlateLayoutTransform(FVector2D(X,Bounds.Y-39.)));
            FSlateDrawElement::MakeText(Elements,Top+6,LabelGeometry,Card.RailCaption,Font,ESlateDrawEffect::None,Paper.CopyWithNewOpacity(Fade)*StyleTint);
        }
    }
    Box(7,Marker,{Center-2.,2},{4.,Bounds.Y-4.},Gold);
    Box(7,Marker,{Center-6.,2},{12.,4.},Gold);
    Box(7,Marker,{Center-6.,Bounds.Y-6.},{12.,4.},Gold);
    Elements.PopClip();
    return Top+7;
}

UMCHubGrillPresentationComponent::UMCHubGrillPresentationComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickInterval=1.f/30.f;
    WidgetClass=UMCHubGrillHologramWidget::StaticClass();
}
void UMCHubGrillPresentationComponent::BeginPlay()
{
    Super::BeginPlay();Station=Cast<AMCHubGrillStation>(GetOwner());
    if(!Station.IsValid() || GetNetMode()==NM_DedicatedServer) {SetComponentTickEnabled(false);return;}
    Display=NewObject<UWidgetComponent>(GetOwner(),TEXT("GrillHologramDisplay"));
    Display->SetupAttachment(GetOwner()->GetRootComponent());Display->SetRelativeLocation(HologramLocalOffset);
    Display->SetWidgetSpace(EWidgetSpace::Screen);Display->SetDrawSize(FVector2D(MCGrillStrip::ShellWidth,MCGrillStrip::CurrentHeight));Display->SetPivot(FVector2D(.5,1));
    Display->SetInitialSharedLayerName(TEXT("GrillRollPresentation"));Display->SetInitialLayerZOrder(5);
    Display->SetWidgetClass(UMCGameplayUIFactory::ResolveClass(TEXT("GrillHologram"),WidgetClass?WidgetClass.Get():UMCHubGrillHologramWidget::StaticClass()));
    Display->SetCollisionEnabled(ECollisionEnabled::NoCollision);Display->SetGenerateOverlapEvents(false);Display->SetCanEverAffectNavigation(false);
    Display->SetCastShadow(false);Display->RegisterComponent();Display->InitWidget();
    if(auto* W=Cast<UMCHubGrillHologramWidget>(Display->GetUserWidgetObject())) W->SetStation(Station.Get());
    Display->SetVisibility(false);Display->SetHiddenInGame(true);
    const auto* Fishing=GetDefault<UMCFishingRodComponent>();
    IntakeFish=NewObject<UStaticMeshComponent>(GetOwner(),TEXT("GrillIntakeFish"));
    IntakeFish->SetupAttachment(GetOwner()->GetRootComponent());IntakeFish->SetStaticMesh(Fishing->FishMesh);
    IntakeFish->SetMaterial(0,Fishing->FishMaterial);IntakeFish->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    IntakeFish->SetGenerateOverlapEvents(false);IntakeFish->SetCanEverAffectNavigation(false);
    IntakeFish->SetRelativeScale3D(FVector(.3));IntakeFish->SetVisibility(false);IntakeFish->RegisterComponent();
}
void UMCHubGrillPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    RestoreDisplaySizing();
    if(Display) {Display->DestroyComponent();Display=nullptr;}
    if(IntakeFish) {IntakeFish->DestroyComponent();IntakeFish=nullptr;}
    Super::EndPlay(Reason);
}
void UMCHubGrillPresentationComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Dt,Type,Tick);auto* Grill=Station.Get();if(!Grill || !Display) {RestoreDisplaySizing();return;}
    if(IntakeFish) {
        const bool Taking=Grill->Stage==EMCHubGrillStage::Closing && Grill->CurrentReward.RollID.IsValid();
        IntakeFish->SetVisibility(Taking);
        if(Taking) {
            if(IntakeRoll!=Grill->CurrentReward.RollID) {
                IntakeRoll=Grill->CurrentReward.RollID;
                const auto* OwnerPawn=Grill->ActivePlayer?Grill->ActivePlayer->GetPawn():nullptr;
                IntakeStart=OwnerPawn?OwnerPawn->GetActorLocation()+FVector(0,0,25):Grill->GetActorTransform().TransformPosition(FVector(0,150,130));
            }
            const auto* GS=GetWorld()->GetGameState();const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
            const float A=float(FMath::Clamp((Now-Grill->StageStartedAt)/FMath::Max(.05,Grill->StageEndsAt-Grill->StageStartedAt),0.,1.));
            const FVector End=Grill->GetActorTransform().TransformPosition(FVector(HologramLocalOffset.X,HologramLocalOffset.Y,145));
            IntakeFish->SetWorldLocation(FMath::Lerp(IntakeStart,End,FMath::SmoothStep(0.f,1.f,A))+FVector(0,0,FMath::Sin(A*PI)*75));
            IntakeFish->SetWorldRotation(FRotator(A*160,Now*120,0));
        }
    }
    auto* PC=UGameplayStatics::GetPlayerController(this,0);APawn* Pawn=PC?PC->GetPawn().Get():nullptr;
    const auto* LocalPlayerState=PC?PC->GetPlayerState<AMCPlayerState>():nullptr;
    const bool bLocalRoll=LocalPlayerState && Grill->ActivePlayer==LocalPlayerState && Grill->Stage!=EMCHubGrillStage::Idle;
    const bool HasOwner=IsValid(PC) && PC->IsLocalController() && PC->GetLocalPlayer() && (bLocalRoll || IsValid(Pawn));
    // A visible hologram can still be fading when its player loses/replaces a
    // Pawn. Do not carry that visibility into the distance/owner updates below.
    if(!HasOwner) {
        RestoreDisplaySizing();VisibilityAlpha=0.f;
        Display->SetRelativeLocation(HologramLocalOffset);Display->SetPivot(FVector2D(.5,1));
        Display->SetVisibility(false);Display->SetHiddenInGame(true);Display->SetComponentTickEnabled(false);
        Grill->SetLocalPromptVisible(PC,false);
        return;
    }
    int32 ViewWidth=0,ViewHeight=0;PC->GetViewportSize(ViewWidth,ViewHeight);
    FVector ViewOrigin,ViewDirection;
    // Keep one screen widget and one result. Only the local player's active roll
    // follows the viewport so a nearby HUD or nameplate cannot cover the stop.
    const bool bCentered=bLocalRoll && ViewWidth>0 && ViewHeight>0
        && PC->DeprojectScreenPositionToWorld(ViewWidth*.5f,ViewHeight*.51f,ViewOrigin,ViewDirection);
    if(bCentered) {
        Display->SetWorldLocation(ViewOrigin+ViewDirection*150.);
        Display->SetPivot(FVector2D(.5,.5));
    } else {
        Display->SetRelativeLocation(HologramLocalOffset);
        FVector2D Projected;
        const bool bNearTop=ViewHeight>0 && PC->ProjectWorldLocationToScreen(Display->GetComponentLocation(),Projected)
            && Projected.Y<ViewHeight*.24;
        Display->SetPivot(FVector2D(.5,bNearTop?.35:1.));
    }
    const double PawnDistance=IsValid(Pawn)?FVector::Dist(Pawn->GetActorLocation(),Grill->GetActorLocation()):0.;
    const bool Show=bLocalRoll || PawnDistance<FMath::Clamp(VisibleDistance,300.f,5000.f);
    VisibilityAlpha=bLocalRoll?1.f:FMath::FInterpTo(VisibilityAlpha,Show?1.f:0.f,Dt,12.f);
    const bool Visible=VisibilityAlpha>.01f;
    Display->SetVisibility(Visible);Display->SetHiddenInGame(!Visible);Display->SetComponentTickEnabled(Visible);
    Display->SetOwnerPlayer(PC->GetLocalPlayer());
    if(auto* W=Cast<UMCHubGrillHologramWidget>(Display->GetUserWidgetObject())) {
        W->SetOwningPlayer(PC);
        if(Visible) {
            W->SetRenderOpacity(bCentered?1.f:VisibilityAlpha*.95f);
            const double Scale=bCentered?1.25:FMath::Clamp(800./FMath::Max(PawnDistance,1.),.55,1.)*(.96+.04*VisibilityAlpha);
            W->SetRenderScale(FVector2D(Scale));W->TickPresentation(Dt);
        } else if(!W->HasPreparedReelCatalog()) W->TickPresentation(0);
    }
    if(!Visible) {RestoreDisplaySizing();return;}
    RefreshDisplaySizing();
    Grill->SetLocalPromptVisible(PC,!PC->IsMoveInputIgnored());
}
void UMCHubGrillPresentationComponent::RestoreDisplaySizing()
{
    if(auto* BoundDisplay=DesiredSizeDisplay.Get();BoundDisplay && CurrentDisplayDrawAtDesiredSize.IsSet()
        && bChangedDisplayDrawAtDesiredSize && BoundDisplay->GetDrawAtDesiredSize())
        BoundDisplay->SetDrawAtDesiredSize(CurrentDisplayDrawAtDesiredSize.GetValue());
    DesiredSizeDisplay.Reset();CurrentDisplayDrawAtDesiredSize.Reset();bChangedDisplayDrawAtDesiredSize=false;
}
void UMCHubGrillPresentationComponent::RefreshDisplaySizing()
{
    if(DesiredSizeDisplay.Get()!=Display.Get()) RestoreDisplaySizing();
    const auto* Hologram=Display?Cast<UMCHubGrillHologramWidget>(Display->GetUserWidgetObject()):nullptr;
    const bool UseDesired=IsValid(Display) && Display->GetOwner()==GetOwner() && Display->GetWidgetSpace()==EWidgetSpace::Screen
        && Hologram && Hologram->GetWorld()==GetWorld() && !Hologram->IsDesignTime()
        && Hologram->IsNewWorldPresentation() && Hologram->HasPreparedNewPresentation();
    if(!UseDesired) {RestoreDisplaySizing();return;}
    if(!CurrentDisplayDrawAtDesiredSize.IsSet()) {DesiredSizeDisplay=Display;CurrentDisplayDrawAtDesiredSize=Display->GetDrawAtDesiredSize();}
    if(!Display->GetDrawAtDesiredSize()) {Display->SetDrawAtDesiredSize(true);bChangedDisplayDrawAtDesiredSize=true;}
}
#undef LOCTEXT_NAMESPACE
