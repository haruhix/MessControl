#include "MCHubGrillPresentationComponent.h"
#include "MCHubGrillStation.h"
#include "MCHubEconomyComponent.h"
#include "MCPlayerState.h"
#include "MCGameplayUIFactory.h"
#include "MCUIVisualStyle.h"
#include "MCWorldUIShapes.h"
#include "MCToothCharacter.h"
#include "MCFishingRodComponent.h"
#include "MCVFXPresentationComponent.h"
#include "MCWorldPresentationComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/AudioComponent.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Curves/CurveEvaluation.h"
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
#include "Materials/MaterialInterface.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

#define LOCTEXT_NAMESPACE "MCHubGrillHologram"

namespace MCGrillStrip
{
constexpr float Width=480.f;
constexpr float Height=133.f;
constexpr float ShellWidth=Width+30.f;
constexpr float CurrentHeight=226.f;
constexpr float ProjectionHeight=245.f;
constexpr float ProjectionScale=.49f;
constexpr double RareArrivalSeconds=.62;
constexpr double ResetSeconds=.62;
constexpr float ResetCollapseEnd=.29f;
constexpr float ResetBootStart=.43f;
constexpr float RailRadius=10.f;
constexpr const TCHAR* ProjectionMaterial=TEXT("/Game/Gameplay/Hub/Economy/M_CaseHologramProjection.M_CaseHologramProjection");
constexpr const TCHAR* RareReactionAtlas=TEXT("/Game/Gameplay/Hub/Economy/T_CaseRareToothReaction.T_CaseRareToothReaction");
constexpr const TCHAR* RareFanfareAsset=TEXT("/Game/Gameplay/Hub/Economy/SFX/SW_CaseRareFanfare.SW_CaseRareFanfare");
constexpr const TCHAR* ResetSoundAsset=TEXT("/Game/Gameplay/Hub/Economy/SFX/SW_CaseHologramReset.SW_CaseHologramReset");
const FName ComposerSource(TEXT("Hub.Grill"));
const FName RareWinEvent(TEXT("RareWin"));
const FName ResetEvent(TEXT("Reset"));
constexpr float CardWidth=116.f;
constexpr float CardGap=9.f;
constexpr double IdleCardsPerSecond=5./19.;
constexpr int32 FirstCard=3;
constexpr int32 WinningCard=27;
constexpr int32 CardCount=WinningCard+4;

const FSlateRoundedBoxBrush Rail{FLinearColor::White,RailRadius};
const FSlateRoundedBoxBrush CardFace{FLinearColor::White,8.f};
const FSlateRoundedBoxBrush Marker{FLinearColor::White,1.f};
const FLinearColor Background{FColor::FromHex(TEXT("151F40"))};
const FLinearColor Paper{FColor::FromHex(TEXT("FFFDFD"))};
const FLinearColor Gold{FColor::FromHex(TEXT("FFDA49"))};
const FLinearColor HologramBlue{FColor::FromHex(TEXT("4C76AE"))};
const FLinearColor HologramNavy{FColor::FromHex(TEXT("16226B"))};
const FLinearColor HologramRose{FColor::FromHex(TEXT("AE4C6C"))};
const FSlateRoundedBoxBrush RareShell{FLinearColor::White,13.f,HologramNavy,2.f};
const FSlateRoundedBoxBrush RareShellShadow{FLinearColor::White,13.f};
const FSlateRoundedBoxBrush RareCaptionPlate{FLinearColor::White,6.f,HologramNavy,1.5f};

bool IsRareReveal(const AMCHubGrillStation& Grill)
{
    const auto& Reward=Grill.CurrentReward;
    return Grill.Stage==EMCHubGrillStage::Revealing && Reward.RollID.IsValid()
        && Reward.Kind==EMCGrillRewardKind::CatalogItem && Reward.Quantity>0 && !Reward.bConvertedDuplicate
        && (Reward.Rarity==EMCHubItemRarity::Epic || Reward.Rarity==EMCHubItemRarity::Legendary);
}

double PresentationTime(const AMCHubGrillStation& Grill)
{
    const auto* World=Grill.GetWorld();
    if(const auto* State=World?World->GetGameState():nullptr) return State->GetServerWorldTimeSeconds();
    return World?World->GetTimeSeconds():Grill.StageStartedAt;
}

float ResetProgressAt(const AMCHubGrillStation& Grill,double Now)
{
    if(Grill.Stage!=EMCHubGrillStage::Idle || !Grill.CurrentReward.RollID.IsValid()) return 1.f;
    return float(FMath::Clamp((Now-Grill.StageStartedAt)/ResetSeconds,0.,1.));
}

FWidgetTransform ResetDisplayTransform(float Progress)
{
    const float Collapse=FMath::SmoothStep(0.f,ResetCollapseEnd,Progress);
    const float Boot=FMath::SmoothStep(ResetBootStart,.87f,Progress);
    const float Overshoot=.032f*FMath::SmoothStep(.70f,.86f,Progress)*(1.f-FMath::SmoothStep(.87f,1.f,Progress));
    const float Tear=FMath::Sin(Progress*10.f*PI)*Collapse*(1.f-FMath::SmoothStep(.20f,.34f,Progress));
    FWidgetTransform Transform;
    Transform.Scale.Y=1.f-.998f*Collapse+.998f*Boot+Overshoot;
    Transform.Translation.X=16.f*Tear;
    return Transform;
}

float RareArrival(double Seconds)
{
    const float Elapsed=float(Seconds/RareArrivalSeconds);
    if(Elapsed<=0.f || Elapsed>=1.f) return FMath::Clamp(Elapsed,0.f,1.f);

    // Match the approved preview's cubic-bezier(.22, .75, .25, 1).
    float Low=0.f,High=1.f;
    for(int32 Step=0;Step<10;++Step) {
        const float T=(Low+High)*.5f;
        if(UE::Curves::BezierInterp(0.f,.22f,.25f,1.f,T)<Elapsed) Low=T;
        else High=T;
    }
    return UE::Curves::BezierInterp(0.f,.75f,1.f,1.f,(Low+High)*.5f);
}

FTransform RareWinCabinetFrame(const AMCHubGrillStation& Grill,const FVector& HologramOffset,float& OutRadius)
{
    const FVector LocalFront=FVector(-HologramOffset.X,-HologramOffset.Y,0.).GetSafeNormal(UE_SMALL_NUMBER,FVector::YAxisVector);
    const FTransform StationTransform=Grill.GetActorTransform();
    const FQuat Facing=StationTransform.TransformVectorNoScale(LocalFront).ToOrientationQuat();
    if(const auto* Lid=IsValid(Grill.LidActor)?Grill.LidActor->FindComponentByClass<UStaticMeshComponent>():nullptr;
        Lid && Lid->GetStaticMesh()) {
        const FTransform Frame(Facing,Lid->GetComponentLocation());
        const auto Bounds=Lid->CalcBounds(Lid->GetComponentTransform().GetRelativeTransform(Frame));
        OutRadius=float(Bounds.BoxExtent.Y+14.);
        const FVector Shoulders=Bounds.Origin+FVector(Bounds.BoxExtent.X*.5,0.,Bounds.BoxExtent.Z*.42);
        return FTransform(Facing,Frame.TransformPosition(Shoulders));
    }

    // The existing station offset is measured from the interaction volume to the cabinet.
    OutRadius=105.f;
    const FVector Shoulders=FVector(HologramOffset.X,HologramOffset.Y,144.)+LocalFront*42.;
    return FTransform(Facing,StationTransform.TransformPosition(Shoulders));
}

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
void UMCHubGrillHologramWidget::SetRareReactionTexture(UTexture2D* Texture)
{
    RareReactionTexture=Texture;
    Invalidate(EInvalidateWidgetReason::Paint);
}
void UMCHubGrillHologramWidget::StartRareReaction(const FGuid& Roll,double StartTime)
{
    RareReactionRoll=Roll;
    RareReactionStartedAt=StartTime;
}
void UMCHubGrillHologramWidget::UseFixedProjectionLayout()
{
    bFixedProjectionLayout=true;
    if(auto* CurrentRoot=WidgetTree?WidgetTree->FindWidget<USizeBox>(TEXT("HologramDesignSize")):nullptr) {
        CurrentRoot->SetWidthOverride(MCGrillStrip::ShellWidth);
        CurrentRoot->SetHeightOverride(MCGrillStrip::ProjectionHeight);
    }
    if(NewHologramRoot) {
        NewHologramRoot->SetWidthOverride(MCGrillStrip::ShellWidth);
        NewHologramRoot->SetHeightOverride(MCGrillStrip::ProjectionHeight);
    }
    for(auto* ReelSlot:{HologramReelSlot.Get(),NewHologramReelSlot.Get()}) if(ReelSlot) ReelSlot->SetWidthOverride(MCGrillStrip::Width);
    RefreshWorldPresentation();
}
void UMCHubGrillHologramWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    if(bFixedProjectionLayout) TickPresentation(DeltaSeconds);
}
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
    const auto* Grill=Source.Get();
    if(!Grill) {
        UpdateResetVisual(1.f);
        UpdateRareReaction(0.f,0.f);
        RefreshWorldPresentation();
        return;
    }
    const auto* PS=GetOwningPlayer()?GetOwningPlayer()->GetPlayerState<AMCPlayerState>():nullptr;
    const auto* Economy=PS?PS->Economy.Get():nullptr;
    const double Now=MCGrillStrip::PresentationTime(*Grill);
    UpdateResetVisual(MCGrillStrip::ResetProgressAt(*Grill,Now));
    RefreshReel(*Grill,Economy,Now);
    const bool bRareReveal=MCGrillStrip::IsRareReveal(*Grill);
    // The last frame of the celebration is the picture that the CRT switches off.
    const bool bShowRareReaction=RareReactionTexture && RareReactionRoll.IsValid()
        && RareReactionRoll==Grill->CurrentReward.RollID
        && (bRareReveal || Grill->Stage==EMCHubGrillStage::Opening || ResetProgress<MCGrillStrip::ResetBootStart);
    UpdateRareReaction(bShowRareReaction?MCGrillStrip::RareArrival(Now-RareReactionStartedAt):0.f,bShowRareReaction?1.f:0.f);
    if(HologramTitle) {
        const FText Title=Grill->Stage==EMCHubGrillStage::Rolling?Localized(TEXT("UI.Grill.Rolling"),LOCTEXT("Rolling","КРУТИМ…")):
            Grill->Stage==EMCHubGrillStage::Closing?Localized(TEXT("UI.Grill.Accepted"),LOCTEXT("Cooking","РЫБА ПРИНЯТА")):
            (bRareReveal || bShowRareReaction)?GrillText(TEXT("UI.Grill.RareWin"),TEXT("ЦЕННЫЙ ПРИЗ"),TEXT("BIG WIN")):
            bResultCentered?GrillText(TEXT("UI.Grill.Result"),TEXT("ВАШ РЕЗУЛЬТАТ"),TEXT("YOUR RESULT")):
                bIdleReel?GrillText(TEXT("UI.Grill.IdleTeaser"),TEXT("ЧТО ВЫПАДЕТ?"),TEXT("WHAT WILL DROP?")):
                    Localized(TEXT("UI.Grill.Idle"),LOCTEXT("Idle","1 РЫБА · 1 ПОПЫТКА"));
        if(!HologramTitle->GetText().EqualTo(Title)) HologramTitle->SetText(Title);
    }
    if(HologramReward) {
        const FMCGrillReward& Reward=Grill->CurrentReward;
        const bool bTrash=Reward.Kind==EMCGrillRewardKind::Trash || (Reward.Kind==EMCGrillRewardKind::Coins && Reward.Coins<=0 && !Reward.bConvertedDuplicate);
        FText Result=bIdleReel?Localized(TEXT("UI.Grill.Idle"),LOCTEXT("Idle","1 РЫБА · 1 ПОПЫТКА")):FText::GetEmpty();
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

void UMCHubGrillHologramWidget::UpdateRareReaction(float Arrival,float Opacity)
{
    if(RareReactionArrival==Arrival && RareReactionOpacity==Opacity) return;
    if(auto* Base=WidgetTree?WidgetTree->RootWidget.Get():nullptr) {
        if(Opacity>0.f && !HiddenBaseOpacity.IsSet()) {
            HiddenBaseOpacity=Base->GetRenderOpacity();
            Base->SetRenderOpacity(0.f);
        } else if(Opacity<=0.f && HiddenBaseOpacity.IsSet()) {
            Base->SetRenderOpacity(HiddenBaseOpacity.GetValue());
            HiddenBaseOpacity.Reset();
        }
    }
    RareReactionArrival=Arrival;
    RareReactionOpacity=Opacity;
    Invalidate(EInvalidateWidgetReason::Paint);
}

void UMCHubGrillHologramWidget::UpdateResetVisual(float Progress)
{
    using namespace MCGrillStrip;
    auto* Base=WidgetTree?WidgetTree->RootWidget.Get():nullptr;
    if(!Base || Progress==ResetProgress) return;
    ResetProgress=Progress;
    if(Progress>=1.f) {
        if(BeforeResetTransform.IsSet()) Base->SetRenderTransform(BeforeResetTransform.GetValue());
        BeforeResetTransform.Reset();
    } else {
        if(!BeforeResetTransform.IsSet()) BeforeResetTransform=Base->GetRenderTransform();
        FWidgetTransform Transform=BeforeResetTransform.GetValue();
        const FWidgetTransform Reset=ResetDisplayTransform(Progress);
        Transform.Scale*=Reset.Scale;
        Transform.Translation+=Reset.Translation;
        Base->SetRenderTransform(Transform);
    }
    Invalidate(EInvalidateWidgetReason::Paint);
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
    const bool bPreviewRareReset=PreviewState==TEXT("RareReset");
    const bool bPreviewReset=bPreviewRareReset || PreviewState==TEXT("Reset");
    UpdateResetVisual(bPreviewReset?FMath::Clamp(float(Time/MCGrillStrip::ResetSeconds),0.f,1.f):1.f);
    bResultCentered=PreviewState==TEXT("NoPrize") || PreviewState==TEXT("Duplicate")
        || (bPreviewReset && ResetProgress<MCGrillStrip::ResetBootStart);
    bIdleReel=PreviewState==TEXT("Idle") || (bPreviewReset && !bResultCentered);
    if((bIdleReel || bPreviewRareReset) && !RareReactionTexture)
        SetRareReactionTexture(LoadObject<UTexture2D>(nullptr,MCGrillStrip::RareReactionAtlas));
    const bool bShowRareReaction=bPreviewRareReset && bResultCentered && RareReactionTexture;
    UpdateRareReaction(bShowRareReaction?1.f:0.f,bShowRareReaction?1.f:0.f);
    ReelCursor=bResultCentered?float(MCGrillStrip::WinningCard):
        FMath::Lerp(float(MCGrillStrip::FirstCard),float(MCGrillStrip::WinningCard),1.f-FMath::Pow(1.f-FMath::Clamp(Time/2.f,0.f,1.f),3.f));
    if(HologramTitle) HologramTitle->SetText(Localized(TEXT("UI.Grill.Rolling"),LOCTEXT("Rolling","КРУТИМ…")));
    if(HologramReward) HologramReward->SetText(FText::GetEmpty());
    if(bIdleReel) {
        FillerCards.Reset(UE_ARRAY_COUNT(PreviewCards));
        FillerCards.Append(PreviewCards,UE_ARRAY_COUNT(PreviewCards));
        ReelCursor=float(FMath::Fmod(FMath::Max(0.,double(Time))*MCGrillStrip::IdleCardsPerSecond,double(FillerCards.Num())));
        if(HologramTitle) HologramTitle->SetText(GrillText(TEXT("UI.Grill.IdleTeaser"),TEXT("ЧТО ВЫПАДЕТ?"),TEXT("WHAT WILL DROP?")));
        if(HologramReward) HologramReward->SetText(Localized(TEXT("UI.Grill.Idle"),LOCTEXT("Idle","1 РЫБА · 1 ПОПЫТКА")));
    } else if(bShowRareReaction) {
        ReelCards[MCGrillStrip::WinningCard]=PreviewCards[1];
        if(HologramTitle) HologramTitle->SetText(GrillText(TEXT("UI.Grill.RareWin"),TEXT("ЦЕННЫЙ ПРИЗ"),TEXT("BIG WIN")));
        if(HologramReward) HologramReward->SetText(FText::Format(Localized(TEXT("UI.Grill.Item"),LOCTEXT("Item","{0} ×{1}")),
            PreviewCards[1].Caption,FText::AsNumber(1)));
    } else if(bResultCentered) {
        const bool bTrash=PreviewState==TEXT("NoPrize") || bPreviewReset;
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
    if(NewHologramRewardCard) NewHologramRewardCard->SetVisibility(RewardCopy.IsEmpty()
        ? (bFixedProjectionLayout?ESlateVisibility::Hidden:ESlateVisibility::Collapsed) : ESlateVisibility::HitTestInvisible);
    if(!bFixedProjectionLayout) RefreshWorldCaptionLayout();
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
        GrillText(TEXT("UI.Grill.IdleTeaser"),TEXT("ЧТО ВЫПАДЕТ?"),TEXT("WHAT WILL DROP?")),
        GrillText(TEXT("UI.Grill.Result"),TEXT("ВАШ РЕЗУЛЬТАТ"),TEXT("YOUR RESULT")),
        GrillText(TEXT("UI.Grill.RareWin"),TEXT("ЦЕННЫЙ ПРИЗ"),TEXT("BIG WIN")),
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

void UMCHubGrillHologramWidget::RefreshReel(const AMCHubGrillStation& Grill,const UMCHubEconomyComponent* Economy,double Now)
{
    using namespace MCGrillStrip;
    const FMCGrillReward& Reward=Grill.CurrentReward;
    const bool bNewPool=Economy && PreparedEconomy.Get()!=Economy && Economy->CatalogTable.Get()
        && Economy->GrillRewardsTable.Get() && Economy->RulesTable.Get();
    if(bNewPool) {
        FillerCards.Reset();
        TSet<FName> ShownItems;
        TSet<EMCGrillRewardKind> ShownGenericRewards;
        for(const auto& Candidate:Economy->GetGrillPreviewPool()) {
            bool bAlreadyShown=false;
            if(Candidate.Kind==EMCGrillRewardKind::CatalogItem) ShownItems.Add(Candidate.CatalogItem,&bAlreadyShown);
            else ShownGenericRewards.Add(Candidate.Kind,&bAlreadyShown);
            if(!bAlreadyShown) FillerCards.Add(MakeCard(Candidate,Economy));
        }
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
    const bool bWasIdle=bIdleReel;
    const bool bCollapsingResult=ResetProgress<ResetBootStart;
    bIdleReel=Grill.Stage==EMCHubGrillStage::Idle && !bCollapsingResult;
    bResultCentered=Reward.RollID.IsValid() && (Grill.Stage==EMCHubGrillStage::Revealing
        || Grill.Stage==EMCHubGrillStage::Opening || bCollapsingResult);
    if(bResultCentered && !bWasCentered && !bNewRoll) ReelCards[WinningCard]=MakeCard(Reward,Economy);
    float NextReelPosition=float(bResultCentered?WinningCard:FirstCard);
    if(bIdleReel) {
        const int32 IdleCardCount=FillerCards.IsEmpty()?ReelCards.Num():FillerCards.Num();
        const double Elapsed=FMath::Max(0.,Now-Grill.StageStartedAt);
        NextReelPosition=float(FMath::Fmod(Elapsed*IdleCardsPerSecond,double(IdleCardCount)));
    } else if(Reward.RollID.IsValid() && Grill.Stage==EMCHubGrillStage::Rolling) {
        const float Alpha=float(FMath::Clamp((Now-Grill.StageStartedAt)/FMath::Max(.1,Grill.StageEndsAt-Grill.StageStartedAt),0.,1.));
        NextReelPosition=FMath::Lerp(float(FirstCard),float(WinningCard),1.f-FMath::Pow(1.f-Alpha,3.f));
    }
    if(bNewPool || bNewRoll || bWasCentered!=bResultCentered || bWasIdle!=bIdleReel || ReelCursor!=NextReelPosition)
        Invalidate(EInvalidateWidgetReason::Paint);
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

void UMCHubGrillHologramWidget::DrawReelCard(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& RailGeometry,
    double Left,const FReelCard& Card,const FLinearColor& Tint,bool bWinning) const
{
    using namespace MCGrillStrip;
    const double RailHeight=RailGeometry.GetLocalSize().Y;
    const double TileHeight=RailHeight-20.;
    const FLinearColor Accent=RarityColor(Card.Rarity,Card.Shape==ECardIcon::Trash);
    if(bWinning) FSlateDrawElement::MakeBox(Elements,Layer+2,
        RailGeometry.ToPaintGeometry(FVector2D(CardWidth+4.,TileHeight+4.),FSlateLayoutTransform(FVector2D(Left-2.,8))),
        &CardFace,ESlateDrawEffect::None,Gold*Tint);
    const FPaintGeometry Tile=RailGeometry.ToPaintGeometry(FVector2D(CardWidth,TileHeight),FSlateLayoutTransform(FVector2D(Left,10)));
    const FLinearColor Light=FMath::Lerp(Background,Accent,.23f),Dark=FMath::Lerp(Background,Accent,.43f);
    FSlateDrawElement::MakeGradient(Elements,Layer+3,Tile,
        {FSlateGradientStop(FVector2D(0,0),Light*Tint),FSlateGradientStop(FVector2D(0,TileHeight),Dark*Tint)},
        Orient_Vertical,ESlateDrawEffect::None,FVector4f(8.f));
    FSlateDrawElement::MakeBox(Elements,Layer+4,
        RailGeometry.ToPaintGeometry(FVector2D(CardWidth-10.,4.),FSlateLayoutTransform(FVector2D(Left+5.,RailHeight-17.))),
        &Marker,ESlateDrawEffect::None,Accent*Tint);
    DrawCardIcon(Elements,Layer+5,RailGeometry.MakeChild(FVector2D(78,77),
        FSlateLayoutTransform(FVector2D(Left+(CardWidth-78.)*.5,14.))),Card,Paper*Tint);
    if(!Card.RailCaption.IsEmpty()) {
        const FSlateFontInfo Font=CardFont.HasValidFont()?CardFont:FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),14);
        const double X=Left+FMath::Max(0.,(CardWidth-Card.RailCaptionWidth)*.5);
        const auto LabelGeometry=RailGeometry.ToPaintGeometry(FVector2D(CardWidth,18),FSlateLayoutTransform(FVector2D(X,RailHeight-39.)));
        FSlateDrawElement::MakeText(Elements,Layer+6,LabelGeometry,Card.RailCaption,Font,ESlateDrawEffect::None,Paper*Tint);
    }
}

int32 UMCHubGrillHologramWidget::PaintRareWinScene(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,
    const FLinearColor& Tint,const FReelCard& Prize) const
{
    using namespace MCGrillStrip;
    const FVector2D Size(ShellWidth,ProjectionHeight);
    const FVector2D Available=Geometry.GetLocalSize();
    const float Scale=float(FMath::Min(Available.X/Size.X,Available.Y/Size.Y));
    if(Scale<=0.f) return Layer;
    const FGeometry Scene=Geometry.MakeChild(Size,FSlateLayoutTransform(Scale,(Available-Size*Scale)*.5));
    auto Box=[&](int32 Depth,FVector2D Position,FVector2D Extent,const FSlateBrush& Brush,FLinearColor Color) {
        FSlateDrawElement::MakeBox(Elements,Layer+Depth,Scene.ToPaintGeometry(Extent,FSlateLayoutTransform(Position)),
            &Brush,ESlateDrawEffect::None,Color*Tint);
    };
    Elements.PushClip(FSlateClippingZone(Scene));
    Box(1,{5,6},{504,238},RareShellShadow,HologramNavy);
    Box(2,{1,1},{504,238},RareShell,HologramBlue);
    Box(3,{15,11},{Width,33},RareCaptionPlate,Paper);
    Box(3,{15,196},{Width,32},RareCaptionPlate,Paper);
    const FVector2D RailOrigin((ShellWidth-Width)*.5,53.);
    const FVector2D RailSize(Width,Height);
    const FGeometry RailGeometry=Scene.MakeChild(RailSize,FSlateLayoutTransform(RailOrigin));
    Box(3,RailOrigin,RailSize,Rail,Background);
    Elements.PushClip(FSlateClippingZone(RailGeometry));
    DrawReelCard(Elements,Layer+3,RailGeometry,(Width-CardWidth)*.5,Prize,Tint,true);

    FSlateBrush Portrait;
    Portrait.DrawAs=ESlateBrushDrawType::Image;
    Portrait.SetResourceObject(RareReactionTexture);
    // Resting atlas bounds from the approved 510 × 245 composition.
    constexpr double LeftSize=227.25,RightSize=193.14;
    const double Arrival=RareReactionArrival;
    const FLinearColor PortraitTint=FLinearColor::White.CopyWithNewOpacity(RareReactionOpacity);
    Portrait.SetUVRegion(FBox2f(FVector2f(0,0),FVector2f(.5f,1)));
    Box(10,{FMath::Lerp(RailOrigin.X-LeftSize,-22.,Arrival),28.24},{LeftSize,LeftSize},Portrait,PortraitTint);
    Portrait.SetUVRegion(FBox2f(FVector2f(.5f,0),FVector2f(1,1)));
    Box(10,{FMath::Lerp(RailOrigin.X+Width,319.03,Arrival),27.51},{RightSize,RightSize},Portrait,PortraitTint);

    const auto Renderer=FSlateApplication::Get().GetRenderer();
    // Slate's clip is rectangular. Cover its four corners with the shell color.
    const auto White=Renderer->GetResourceHandle(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
    const auto RailTransform=RailGeometry.GetAccumulatedRenderTransform();
    const FColor CornerColor=(HologramBlue*Tint).ToFColorSRGB();
    TArray<FSlateVertex> Corners;
    TArray<SlateIndex> CornerIndices;
    constexpr int32 CornerSegments=6;
    Corners.Reserve(4*(CornerSegments+2));
    CornerIndices.Reserve(4*CornerSegments*3);
    for(const FVector2f Direction:{FVector2f(1,1),FVector2f(-1,1),FVector2f(-1,-1),FVector2f(1,-1)}) {
        const FVector2f Corner(Direction.X<0.f?Width:0.f,Direction.Y<0.f?Height:0.f);
        const SlateIndex First=SlateIndex(Corners.Num());
        Corners.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RailTransform,Corner,FVector2f(.5f,.5f),CornerColor));
        for(int32 Segment=0;Segment<=CornerSegments;++Segment) {
            float Sin,Cos;
            FMath::SinCos(&Sin,&Cos,HALF_PI*Segment/CornerSegments);
            const FVector2f Point=Corner+Direction*FVector2f(1.f-Cos,1.f-Sin)*RailRadius;
            Corners.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RailTransform,Point,FVector2f(.5f,.5f),CornerColor));
            if(Segment>0) CornerIndices.Append({First,SlateIndex(First+Segment),SlateIndex(First+Segment+1)});
        }
    }
    FSlateDrawElement::MakeCustomVerts(Elements,Layer+11,White,Corners,CornerIndices,nullptr,0,0);
    Elements.PopClip();

    const auto FontMeasure=Renderer->GetFontMeasureService();
    auto Label=[&](const UTextBlock* TextBlock,double CenterY,FLinearColor Color) {
        if(!TextBlock || TextBlock->GetText().IsEmpty()) return;
        const FText& Text=TextBlock->GetText();
        FSlateFontInfo Font=TextBlock->GetFont();
        const float WidthLimit=478.f;
        const float Advance=FontMeasure->Measure(Text,Font).X;
        if(Advance>WidthLimit) Font.Size*=WidthLimit/Advance;
        const FVector2D Extent=FontMeasure->Measure(Text,Font);
        FSlateDrawElement::MakeText(Elements,Layer+11,
            Scene.ToPaintGeometry(Extent,FSlateLayoutTransform(FVector2D((Size.X-Extent.X)*.5,CenterY-Extent.Y*.5))),
            Text,Font,ESlateDrawEffect::None,Color*Tint);
    };
    Label(NewHologramTitle?NewHologramTitle.Get():HologramTitle.Get(),27.5,HologramRose);
    Label(NewHologramReward?NewHologramReward.Get():HologramReward.Get(),212.,HologramNavy);
    Elements.PopClip();
    return Layer+11;
}

int32 UMCHubGrillHologramWidget::PaintResetBeam(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,
    const FLinearColor& Tint) const
{
    using namespace MCGrillStrip;
    if(ResetProgress>=1.f) return Layer;
    const FVector2D Bounds=Geometry.GetLocalSize();
    const FVector2D Center=Bounds*.5;
    const float CollapseGlow=FMath::SmoothStep(.16f,.28f,ResetProgress)*(1.f-FMath::SmoothStep(.29f,.40f,ResetProgress));
    const float BootGlow=FMath::SmoothStep(.46f,.56f,ResetProgress)*(1.f-FMath::SmoothStep(.66f,.91f,ResetProgress));
    const float Strength=CollapseGlow+.6f*BootGlow;
    if(Strength<=UE_SMALL_NUMBER) return Layer;

    const float FadeOut=FMath::SmoothStep(.29f,.40f,ResetProgress);
    const float Scan=FMath::SmoothStep(.43f,.91f,ResetProgress);
    const double HalfWidth=Center.X*(BootGlow>0.f?.91:FMath::Lerp(.94,.06,FadeOut));
    const double Y=Center.Y+(BootGlow>0.f?Bounds.Y*.36*Scan:0.);
    auto DrawBeam=[&](int32 Depth,double Thickness,FLinearColor Color) {
        const auto Paint=Geometry.ToPaintGeometry({HalfWidth*2.,Thickness},FSlateLayoutTransform(FVector2D(Center.X-HalfWidth,Y-Thickness*.5)));
        FSlateDrawElement::MakeBox(Elements,Layer+Depth,Paint,&Marker,ESlateDrawEffect::None,Color*Tint);
    };
    DrawBeam(1,10.,FLinearColor(.10f,.46f,.93f,Strength*.30f));
    DrawBeam(2,4.,FLinearColor(.27f,.81f,1.f,Strength*.68f));
    DrawBeam(3,1.6,FLinearColor(.68f,.96f,1.f,Strength));
    return Layer+3;
}

int32 UMCHubGrillHologramWidget::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
    FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const
{
    using namespace MCGrillStrip;
    const int32 Top=Super::NativePaint(Args,Geometry,CullingRect,Elements,Layer,Style,ParentEnabled);
    if(RareReactionTexture && RareReactionOpacity>0.f && ReelCards.IsValidIndex(WinningCard)) {
        const FGeometry Picture=ResetProgress<1.f?Geometry.MakeChild(ResetDisplayTransform(ResetProgress).ToSlateRenderTransform()):Geometry;
        const FLinearColor Tint=Style.GetColorAndOpacityTint();
        const int32 PictureLayer=PaintRareWinScene(Elements,Top,Picture,Tint,ReelCards[WinningCard]);
        return PaintResetBeam(Elements,PictureLayer,Geometry,Tint);
    }
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
    const auto& Cards=bIdleReel && !FillerCards.IsEmpty()?FillerCards:ReelCards;
    const int32 FirstVisible=FMath::FloorToInt(ReelCursor-(Center+CardWidth)/Step);
    const int32 LastVisible=FMath::CeilToInt(ReelCursor+(Center+CardWidth)/Step);
    const int32 Start=bIdleReel?FirstVisible:FMath::Max(0,FirstVisible);
    const int32 End=bIdleReel?LastVisible:FMath::Min(Cards.Num()-1,LastVisible);
    for(int32 Index=Start;Index<=End;++Index) {
        const int32 CardIndex=bIdleReel?(Index%Cards.Num()+Cards.Num())%Cards.Num():Index;
        const FReelCard& Card=Cards[CardIndex];
        const double Offset=(Index-ReelCursor)*Step;
        const double Left=Center+Offset-CardWidth*.5;
        if(Left>=Bounds.X || Left+CardWidth<=0) continue;
        const float Fade=bIdleReel?1.f:1.f-.57f*FMath::SmoothStep(.25f,1.f,FMath::Clamp(float(FMath::Abs(Offset)/Center),0.f,1.f));
        const bool bWinning=bResultCentered && Index==WinningCard;
        DrawReelCard(Elements,Top,ReelGeometry,Left,Card,StyleTint.CopyWithNewOpacity(StyleTint.A*Fade),bWinning);
    }
    if(bIdleReel) {
        if(RareReactionTexture) {
            // Fade the reel into its own rail color before painting the mascot.
            const FLinearColor RailTint=Background*StyleTint;
            FSlateDrawElement::MakeGradient(Elements,Top+7,ReelGeometry.ToPaintGeometry(),
                {FSlateGradientStop(FVector2D(0.,0.),RailTint.CopyWithNewOpacity(0.f)),
                 FSlateGradientStop(FVector2D(Bounds.X*.68,0.),RailTint.CopyWithNewOpacity(0.f)),
                 FSlateGradientStop(FVector2D(Bounds.X*.77,0.),RailTint.CopyWithNewOpacity(RailTint.A*.58f)),
                 FSlateGradientStop(FVector2D(Bounds.X*.86,0.),RailTint),
                 FSlateGradientStop(FVector2D(Bounds.X,0.),RailTint)},
                Orient_Vertical,ESlateDrawEffect::None,FVector4f(RailRadius));
            FSlateBrush Portrait;
            Portrait.DrawAs=ESlateBrushDrawType::Image;
            Portrait.SetResourceObject(RareReactionTexture);
            Portrait.SetUVRegion(FBox2f(FVector2f(.5f,0),FVector2f(1,1)));
            const double PortraitSize=FMath::Min(Bounds.Y*.94,Bounds.X*.30);
            Box(8,Portrait,{Bounds.X*.982-PortraitSize,(Bounds.Y-PortraitSize)*.5},{PortraitSize,PortraitSize},FLinearColor::White);
        }
    } else {
        Box(8,Marker,{Center-2.,2},{4.,Bounds.Y-4.},Gold);
        Box(8,Marker,{Center-6.,2},{12.,4.},Gold);
        Box(8,Marker,{Center-6.,Bounds.Y-6.},{12.,4.},Gold);
    }
    Elements.PopClip();
    return PaintResetBeam(Elements,Top+8,Geometry,StyleTint);
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
    MCWorldPresentation::PrepareOwner(Station.Get(),{MCGrillStrip::ComposerSource});
    RareFanfare=LoadObject<USoundBase>(nullptr,MCGrillStrip::RareFanfareAsset);
    ResetSound=LoadObject<USoundBase>(nullptr,MCGrillStrip::ResetSoundAsset);
    if(ResetSound) {
        ResetSoundAttenuation=NewObject<USoundAttenuation>(this);
        ResetSoundAttenuation->Attenuation.AttenuationShapeExtents=FVector(680.f,0.f,0.f);
        ResetSoundAttenuation->Attenuation.FalloffDistance=1900.f;
    }
    Display=NewObject<UWidgetComponent>(GetOwner(),TEXT("GrillHologramDisplay"));
    Display->SetupAttachment(GetOwner()->GetRootComponent());
    // The saved screen-space anchor leads to the machine body. Seat the fixed
    // projection just above the lid's front edge to clear the overhead HUD.
    const FVector ToFront=FVector(-HologramLocalOffset.X,-HologramLocalOffset.Y,0.).GetSafeNormal();
    const FVector PromptOffset=HologramLocalOffset+ToFront*32.-FVector(0,0,94);
    const FVector ProjectionOffset=PromptOffset+FVector(0,0,68);
    Display->SetRelativeTransform(FTransform(ToFront.Rotation(),ProjectionOffset,FVector(MCGrillStrip::ProjectionScale)));
    if(auto* Prompt=Station->InteractionPrompt.Get()) Prompt->SetRelativeLocation(PromptOffset);
    Display->SetWidgetSpace(EWidgetSpace::World);
    Display->SetBlendMode(EWidgetBlendMode::Transparent);
    Display->SetDrawSize(FVector2D(MCGrillStrip::ShellWidth,MCGrillStrip::ProjectionHeight));
    Display->SetPivot(FVector2D(.5,1));
    Display->SetWidgetClass(UMCGameplayUIFactory::ResolveClass(TEXT("GrillHologram"),WidgetClass?WidgetClass.Get():UMCHubGrillHologramWidget::StaticClass()));
    Display->SetCollisionEnabled(ECollisionEnabled::NoCollision);Display->SetGenerateOverlapEvents(false);Display->SetCanEverAffectNavigation(false);
    Display->SetCastShadow(false);Display->SetVisibility(false);Display->SetHiddenInGame(true);
    if(auto* Material=LoadObject<UMaterialInterface>(nullptr,MCGrillStrip::ProjectionMaterial)) Display->SetMaterial(0,Material);
    Display->RegisterComponent();Display->InitWidget();
    if(auto* W=Cast<UMCHubGrillHologramWidget>(Display->GetUserWidgetObject())) {
        W->SetRareReactionTexture(LoadObject<UTexture2D>(nullptr,MCGrillStrip::RareReactionAtlas));
        W->SetStation(Station.Get());
        W->UseFixedProjectionLayout();
        W->SetVisibility(ESlateVisibility::HitTestInvisible);
        Display->UpdateRenderTarget(FIntPoint(MCGrillStrip::ShellWidth,MCGrillStrip::ProjectionHeight));
    }
    const auto* Fishing=GetDefault<UMCFishingRodComponent>();
    IntakeFish=NewObject<UStaticMeshComponent>(GetOwner(),TEXT("GrillIntakeFish"));
    IntakeFish->SetupAttachment(GetOwner()->GetRootComponent());IntakeFish->SetStaticMesh(Fishing->FishMesh);
    IntakeFish->SetMaterial(0,Fishing->FishMaterial);IntakeFish->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    IntakeFish->SetGenerateOverlapEvents(false);IntakeFish->SetCanEverAffectNavigation(false);
    IntakeFish->SetRelativeScale3D(FVector(.3));IntakeFish->SetVisibility(false);IntakeFish->RegisterComponent();
}
void UMCHubGrillPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if(IsValid(RareFanfarePlayback)) RareFanfarePlayback->Stop();
    if(IsValid(ResetSoundPlayback)) ResetSoundPlayback->Stop();
    if(Display) {Display->DestroyComponent();Display=nullptr;}
    if(IntakeFish) {IntakeFish->DestroyComponent();IntakeFish=nullptr;}
    Super::EndPlay(Reason);
}
void UMCHubGrillPresentationComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Dt,Type,Tick);auto* Grill=Station.Get();if(!Grill || !Display) return;
    if(ResetSoundPlayback && Grill->Stage!=EMCHubGrillStage::Idle) {
        if(IsValid(ResetSoundPlayback)) ResetSoundPlayback->Stop();
        ResetSoundPlayback=nullptr;
    }
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
    auto* PC=UGameplayStatics::GetPlayerController(this,0);
    const bool bHasLocalPlayer=IsValid(PC) && PC->IsLocalController() && PC->GetLocalPlayer();
    const auto* LocalPlayerState=bHasLocalPlayer?PC->GetPlayerState<AMCPlayerState>():nullptr;
    const bool bLocalRoll=LocalPlayerState && Grill->ActivePlayer==LocalPlayerState && Grill->Stage!=EMCHubGrillStage::Idle;
    auto* Widget=Cast<UMCHubGrillHologramWidget>(Display->GetUserWidgetObject());
    if(bHasLocalPlayer && MCGrillStrip::IsRareReveal(*Grill) && PresentedRareWinRoll!=Grill->CurrentReward.RollID) {
        using namespace MCGrillStrip;
        PresentedRareWinRoll=Grill->CurrentReward.RollID;
        const double ReactionStart=PresentationTime(*Grill);
        if(Widget) Widget->StartRareReaction(PresentedRareWinRoll,ReactionStart);
        if(bLocalRoll) {
            const auto* Adapter=Grill->FindComponentByClass<UMCWorldPresentationComponent>();
            auto* Channel=Adapter?Adapter->GetSource(ComposerSource):nullptr;
            FMCPerkVFXGameplayValues Values;
            Values.EpisodeID=PresentedRareWinRoll;
            Values.VisualSubject=Grill->GetRootComponent();
            Values.Color=RarityColor(Grill->CurrentReward.Rarity,false);
            Values.bHasColor=true;
            const FTransform Frame=RareWinCabinetFrame(*Grill,HologramLocalOffset,Values.Radius);
            const bool bPresented=Channel && Channel->PlayLocalSourceEvent(ComposerSource,RareWinEvent,Frame,Values)!=EMCVFXPresentationResult::Rejected;
            if(RareFanfare && (!bPresented || Adapter->UsesOriginalAudio(ComposerSource,RareWinEvent))
                && ReactionStart-Grill->StageStartedAt<RareFanfare->GetDuration()) {
                RareFanfarePlayback=UGameplayStatics::SpawnSound2D(this,RareFanfare,.85f);
                if(RareFanfarePlayback) LastFanfareRoll=PresentedRareWinRoll;
            }
        }
    }
    const APawn* Pawn=bHasLocalPlayer?PC->GetPawn().Get():nullptr;
    const bool bNearby=IsValid(Pawn) && FVector::Dist(Pawn->GetActorLocation(),Grill->GetActorLocation())<FMath::Clamp(VisibleDistance,300.f,5000.f);
    const bool bShow=bLocalRoll || bNearby;
    VisibilityAlpha=bHasLocalPlayer?FMath::FInterpTo(VisibilityAlpha,bShow?1.f:0.f,Dt,12.f):0.f;
    const bool bVisible=VisibilityAlpha>.01f;
    Display->SetVisibility(bVisible);Display->SetHiddenInGame(!bVisible);Display->SetComponentTickEnabled(bVisible);
    Display->SetTintColorAndOpacity(FLinearColor(1.f,1.f,1.f,VisibilityAlpha));
    using namespace MCGrillStrip;
    const double Now=PresentationTime(*Grill);
    if(bHasLocalPlayer && bVisible && PresentedResetRoll!=Grill->CurrentReward.RollID && ResetProgressAt(*Grill,Now)<1.f) {
        PresentedResetRoll=Grill->CurrentReward.RollID;
        const auto* Adapter=Grill->FindComponentByClass<UMCWorldPresentationComponent>();
        auto* Channel=Adapter?Adapter->GetSource(ComposerSource):nullptr;
        FMCPerkVFXGameplayValues Values;
        Values.EpisodeID=PresentedResetRoll;
        Values.VisualSubject=Grill->GetRootComponent();
        const bool bPresented=Channel && Channel->PlayLocalSourceEvent(ComposerSource,ResetEvent,Display->GetComponentTransform(),Values)
            !=EMCVFXPresentationResult::Rejected;
        const float Age=float(FMath::Max(0.,Now-Grill->StageStartedAt));
        if(ResetSound && Age<ResetSound->GetDuration() && (!bPresented || Adapter->UsesOriginalAudio(ComposerSource,ResetEvent))) {
            ResetSoundPlayback=UGameplayStatics::SpawnSoundAttached(ResetSound,Display,NAME_None,FVector::ZeroVector,
                EAttachLocation::SnapToTarget,true,1.f,1.f,Age,ResetSoundAttenuation);
            if(ResetSoundPlayback) LastResetSoundRoll=PresentedResetRoll;
        }
    }
    if(Widget && bHasLocalPlayer) {
        if(Widget->GetOwningPlayer()!=PC) Widget->SetOwningPlayer(PC);
        if(!Widget->HasPreparedReelCatalog()) Widget->TickPresentation(0);
    }
    Grill->SetLocalPromptVisible(PC,bVisible && !PC->IsMoveInputIgnored());
}
#undef LOCTEXT_NAMESPACE
