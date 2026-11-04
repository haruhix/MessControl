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
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetComponent.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WidgetSwitcherSlot.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Widgets/SWidget.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Kismet/GameplayStatics.h"

#define LOCTEXT_NAMESPACE "MCHubGrillHologram"

namespace
{
float ReelFrontV(float PhysicalAngle)
{
    // SM_Roulette_1 has 22 flat facets and a padded atlas seam, not a full
    // 0..1 cylindrical UV. Intersect the visible +Y ray with the actual facet.
    constexpr float V[]={.00560379028f,.05053710938f,.09552001953f,.14038085938f,
        .18542480469f,.23034667969f,.275390625f,.3203125f,.365234375f,.41015625f,
        .455078125f,.5f,.544921875f,.58984375f,.634765625f,.6796875f,.724609375f,
        .76953125f,.814453125f,.859375f,.904296875f,.94970703125f,.99462890625f};
    const float Wrapped=FMath::Fmod(FMath::Fmod(PhysicalAngle,360.f)+360.f,360.f);
    const float Facet=(1.f-Wrapped/360.f)*22.f;
    const int32 I=FMath::Clamp(FMath::FloorToInt(Facet),0,21);
    const float F=Facet-I,Step=UE_TWO_PI/22.f;
    const float A=FMath::Sin(F*Step),B=FMath::Sin((1.f-F)*Step);
    return FMath::Lerp(V[I],V[I+1],A/FMath::Max(A+B,UE_SMALL_NUMBER));
}
}

UMCHubGrillHologramWidget::UMCHubGrillHologramWidget(const FObjectInitializer& Initializer):Super(Initializer)
{
    bUseVersionedWorldPresentation=true;
    ReelMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Gameplay/Hub/Economy/M_GrillHologramReel.M_GrillHologramReel")));
}
void UMCHubGrillHologramWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(WidgetTree->RootWidget) {ResolveControls();BuildWorldPresentationTree();return;}
    auto* Size=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("HologramDesignSize"));
    Size->SetWidthOverride(390);Size->SetHeightOverride(190);WidgetTree->RootWidget=Size;
    HologramPanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("HologramPanel"));
    HologramPanel->SetPadding(FMargin(13,8));Size->SetContent(HologramPanel);
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("HologramContent"));HologramPanel->SetContent(Column);
    HologramTitle=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("HologramTitle"));
    HologramTitle->SetText(LOCTEXT("Idle","1 РЫБА · 1 ПОПЫТКА"));HologramTitle->SetJustification(ETextJustify::Center);
    Column->AddChildToVerticalBox(HologramTitle)->SetPadding(FMargin(0,0,0,5));
    auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("HologramReels"));
    Column->AddChildToVerticalBox(Row)->SetHorizontalAlignment(HAlign_Center);
    for(int32 I=0;I<3;++I) {
        auto* Frame=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),FName(*FString::Printf(TEXT("HologramReelFrame%d"),I)));
        Frame->SetPadding(FMargin(3));auto* ReelSlot=Row->AddChildToHorizontalBox(Frame);ReelSlot->SetPadding(FMargin(4,0));
        auto* Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetWidthOverride(102);Box->SetHeightOverride(102);Frame->SetContent(Box);
        auto* Reel=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*FString::Printf(TEXT("HologramReel%d"),I)));Box->SetContent(Reel);
    }
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
    Reels.Reset();for(int32 I=0;I<3;++I) Reels.Add(Cast<UImage>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("HologramReel%d"),I)))));
}
void UMCHubGrillHologramWidget::PrepareMaterials()
{
    ResolveControls();if(ReelMaterials.Num()==3) {BindWorldReelBrushes();return;}
    auto* Material=ReelMaterial.LoadSynchronous();if(!Material) return;
    ReelMaterials.Reset();for(UImage* Reel:Reels) {
        auto* MID=UMaterialInstanceDynamic::Create(Material,this);ReelMaterials.Add(MID);
        MID->SetScalarParameterValue(TEXT("ScanlineStrength"),ScanlineStrength);
        MID->SetScalarParameterValue(TEXT("ColorSeparation"),ColorSeparation);
        if(Reel) Reel->SetBrushFromMaterial(MID);
    }
    BindWorldReelBrushes();
}
void UMCHubGrillHologramWidget::SetStation(AMCHubGrillStation* Station)
{Source=Station;bPreview=false;BuildPresentationTree();ApplyTheme();PrepareMaterials();TickPresentation(0);}
void UMCHubGrillHologramWidget::ApplyTheme()
{
    BuildPresentationTree();ResolveControls();Super::ApplyTheme();const auto* ResolvedTheme=GetResolvedTheme();if(!ResolvedTheme || !WidgetTree) return;
    const auto& P=ResolvedTheme->Palette;MCUIVisualStyle::Panel(HologramPanel,ResolvedTheme,P.Backdrop.CopyWithNewOpacity(.77f));
    for(int32 I=0;I<3;++I) if(auto* Frame=Cast<UBorder>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("HologramReelFrame%d"),I)))))
        MCUIVisualStyle::Panel(Frame,ResolvedTheme,P.Marine.CopyWithNewOpacity(.85f));
    for(auto* Label:{HologramTitle.Get(),HologramReward.Get()}) if(Label) {
        Label->SetFont(ResolvedTheme->MakeGameplayFont(18,true));Label->SetColorAndOpacity(P.Cream);
    }
    const auto* NewTheme=GetNewWorldPresentationTheme();
    MCWorldUIShapes::TextStyle(NewHologramTitle,NewTheme,20,EMCUIColorToken::Accent);
    MCWorldUIShapes::TextStyle(NewHologramReward,NewTheme,18,EMCUIColorToken::Marine);
    if(NewHologramTitle) NewHologramTitle->SetColorAndOpacity(FLinearColor(FColor::FromHex(TEXT("AE4C6C"))));
    if(NewHologramReward) NewHologramReward->SetColorAndOpacity(FLinearColor(FColor::FromHex(TEXT("16226B"))));
    bHasNewCaptionMetrics=false;bWorldFontsPrepared=false;bCatalogNamesPrepared=false;PrimeWorldPresentationFonts();RefreshWorldPresentation();
}
void UMCHubGrillHologramWidget::TickPresentation(float DeltaSeconds)
{
    RefreshWorldPresentationSelection();PrimeWorldPresentationFonts();PrepareMaterials();const auto* Grill=Source.Get();if(!Grill) {RefreshWorldPresentation();return;}
    const auto Angles=Grill->GetReelDisplayAngles();
    for(int32 I=0;I<3 && I<ReelMaterials.Num();++I) if(ReelMaterials[I]) {
        // The physical head's aperture is angled above +Y. Its measured
        // per-reel alignment is included in the physical stop; remove that
        // alignment to sample the same fixed window centre in the atlas.
        const float Alignment=Grill->ReelAngleOffsets.IsValidIndex(I)?Grill->ReelAngleOffsets[I]:0.f;
        const float V=ReelFrontV((Angles.IsValidIndex(I)?Angles[I]:0.f)-Alignment);
        ReelMaterials[I]->SetScalarParameterValue(TEXT("VCenter"),V);
    }
    if(HologramTitle) HologramTitle->SetText(Grill->Stage==EMCHubGrillStage::Rolling?Localized(TEXT("UI.Grill.Rolling"),LOCTEXT("Rolling","КРУТИМ…")):
        Grill->Stage==EMCHubGrillStage::Closing?Localized(TEXT("UI.Grill.Accepted"),LOCTEXT("Cooking","РЫБА ПРИНЯТА")):
        Grill->CurrentReward.RollID.IsValid()?Localized(TEXT("UI.Grill.Combination"),LOCTEXT("Combination","ВАША КОМБИНАЦИЯ")):
            Localized(TEXT("UI.Grill.Idle"),LOCTEXT("Idle","1 РЫБА · 1 ПОПЫТКА")));
    if(HologramReward) {
        const bool Finished=Grill->CurrentReward.RollID.IsValid() && Grill->Stage!=EMCHubGrillStage::Closing && Grill->Stage!=EMCHubGrillStage::Rolling;
        if(!Finished) HologramReward->SetText(FText::GetEmpty());
        else if(Grill->CurrentReward.Kind==EMCGrillRewardKind::Coins)
            HologramReward->SetText(Grill->CurrentReward.Coins>0?
                FText::Format(Localized(TEXT("UI.Grill.Coins"),LOCTEXT("Coins","+{0} монет")),FText::AsNumber(Grill->CurrentReward.Coins)):
                Localized(TEXT("UI.Grill.NoPrize"),LOCTEXT("NoPrize","Без выигрыша · попробуй ещё")));
        else {
            FText Name=FText::FromName(Grill->CurrentReward.CatalogItem);
            const auto* PS=GetOwningPlayer()?GetOwningPlayer()->GetPlayerState<AMCPlayerState>():nullptr;
            if(PS && PS->Economy) if(const auto* Item=PS->Economy->FindCatalogItem(Grill->CurrentReward.CatalogItem)) Name=Localized(Item->DisplayNameKey,Item->DisplayName);
            HologramReward->SetText(FText::Format(Localized(TEXT("UI.Grill.Item"),LOCTEXT("Item","{0} ×{1}")),Name,FText::AsNumber(Grill->CurrentReward.Quantity)));
        }
        if(const auto* ResolvedTheme=GetResolvedTheme()) HologramReward->SetColorAndOpacity(!Finished?ResolvedTheme->Palette.Cream:
            Grill->CurrentReward.Kind==EMCGrillRewardKind::Coins && Grill->CurrentReward.Coins==0?ResolvedTheme->Palette.Warning:ResolvedTheme->Palette.Mint);
    }
    RefreshWorldPresentation();
}
void UMCHubGrillHologramWidget::ApplyEditorPreview(float Time)
{
    if(!IsDesignTime()) {BuildPresentationTree();ApplyTheme();TickPresentation(0);return;}
    bPreview=true;BuildPresentationTree();Super::ApplyEditorPreview(Time);PrepareMaterials();
    for(int32 I=0;I<ReelMaterials.Num();++I) if(ReelMaterials[I]) ReelMaterials[I]->SetScalarParameterValue(TEXT("VCenter"),FMath::Frac(.06f+Time*.7f+I*.11f));
    if(HologramTitle) HologramTitle->SetText(Localized(TEXT("UI.Grill.Rolling"),LOCTEXT("Rolling","КРУТИМ…")));
    if(HologramReward) HologramReward->SetText(FText::Format(Localized(TEXT("UI.Grill.Coins"),LOCTEXT("Coins","+{0} монет")),FText::AsNumber(25)));
    if(PreviewState==TEXT("Idle")) {
        if(HologramTitle) HologramTitle->SetText(Localized(TEXT("UI.Grill.Idle"),LOCTEXT("Idle","1 РЫБА · 1 ПОПЫТКА")));
        if(HologramReward) HologramReward->SetText(FText::GetEmpty());
    } else if(PreviewState==TEXT("NoPrize") && HologramReward)
        HologramReward->SetText(Localized(TEXT("UI.Grill.NoPrize"),LOCTEXT("NoPrize","Без выигрыша · попробуй ещё")));
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
        const FLinearColor Paper(FColor::FromHex(TEXT("FFFDFD"))),Dark(FColor::FromHex(TEXT("404243")));
        NewRoot=MCWorldUIShapes::Bounds(WidgetTree,TEXT("WorldGrillV2Root"),390,190);
        auto* Shell=Shape(TEXT("WorldGrillV2Panel"),Blue,FMargin(14,10,14,14),Navy,FVector2D(4,5));NewRoot->SetContent(Shell);
        auto* Body=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("WorldGrillV2Body"));Shell->SetContentForSlot(TEXT("Content"),Body);
        auto* HeadingPlate=Shape(TEXT("WorldGrillV2TitlePlate"),Paper,FMargin(10,6),Navy,FVector2D::ZeroVector);
        Body->AddChildToVerticalBox(HeadingPlate)->SetPadding(FMargin(0,0,0,9));
        auto* Heading=MCWorldUIShapes::Text(WidgetTree,TEXT("WorldGrillV2Title"),342);Heading->SetAutoWrapText(false);HeadingPlate->SetContentForSlot(TEXT("Content"),Heading);
        auto* ReelRow=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("WorldGrillV2Reels"));
        Body->AddChildToVerticalBox(ReelRow)->SetHorizontalAlignment(HAlign_Center);
        for(int32 ReelIndex=0;ReelIndex<3;++ReelIndex) {
            auto* ReelFrame=Shape(FName(*FString::Printf(TEXT("WorldGrillV2ReelFrame%d"),ReelIndex)),Dark,FMargin(3),Paper,FVector2D(0,3));
            auto* ReelCell=ReelRow->AddChildToHorizontalBox(ReelFrame);ReelCell->SetPadding(FMargin(4,0));
            auto* ReelBounds=MCWorldUIShapes::Bounds(WidgetTree,FName(*FString::Printf(TEXT("WorldGrillV2ReelBounds%d"),ReelIndex)),102,102);ReelBounds->SetHeightOverride(102);
            ReelFrame->SetContentForSlot(TEXT("Content"),ReelBounds);
            auto* ReelImage=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*FString::Printf(TEXT("WorldGrillV2Reel%d"),ReelIndex)));ReelBounds->SetContent(ReelImage);
        }
        auto* RewardPlate=Shape(TEXT("WorldGrillV2RewardPlate"),Paper,FMargin(10,7),Navy,FVector2D::ZeroVector);
        Body->AddChildToVerticalBox(RewardPlate)->SetPadding(FMargin(0,10,0,0));
        auto* RewardLabel=MCWorldUIShapes::Text(WidgetTree,TEXT("WorldGrillV2Reward"),342);RewardLabel->SetAutoWrapText(false);RewardPlate->SetContentForSlot(TEXT("Content"),RewardLabel);
    }
    ResolveWorldControls(NewRoot);
    bool Complete=NewHologramRoot && NewHologramTitle && NewHologramReward && NewHologramRewardCard && NewHologramReels.Num()==3;
    for(const auto& ReelImage:NewHologramReels) Complete&=ReelImage!=nullptr;
    if(!Complete) return; // Preserve a malformed authored source tree; never switch to an incomplete replacement.
    InstallWorldPresentation(NewRoot);
    if(auto* Versions=WidgetTree->FindWidget<UWidgetSwitcher>(TEXT("WorldPresentationVersions")))
        if(auto* CurrentRoot=Versions->GetWidgetAtIndex(0)) if(auto* CurrentCell=Cast<UWidgetSwitcherSlot>(CurrentRoot->Slot)) {
            CurrentCell->SetHorizontalAlignment(HAlign_Fill);CurrentCell->SetVerticalAlignment(VAlign_Fill);
        }
    BindWorldReelBrushes();
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
    NewHologramReels.Reset();
    for(int32 ReelIndex=0;ReelIndex<3;++ReelIndex) NewHologramReels.Add(Cast<UImage>(Find(FName(*FString::Printf(TEXT("WorldGrillV2Reel%d"),ReelIndex)))));
}
void UMCHubGrillHologramWidget::BindWorldReelBrushes()
{
    // The original presenter owns and retains exactly three MIDs. New images read
    // those same brushes, including any authored resource/tint, with no variants.
    for(int32 ReelIndex=0;ReelIndex<NewHologramReels.Num() && ReelIndex<Reels.Num();++ReelIndex)
        if(NewHologramReels[ReelIndex] && Reels[ReelIndex]) {
            const FSlateBrush& SourceBrush=Reels[ReelIndex]->GetBrush();
            if(!(NewHologramReels[ReelIndex]->GetBrush()==SourceBrush)) NewHologramReels[ReelIndex]->SetBrush(SourceBrush);
        }
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
    BindWorldReelBrushes();RefreshWorldCaptionLayout();
    // Keep Current presentation until the resident reel/font resources are ready.
    // The same source widget and station remain alive; no second result/roll exists.
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
    const float Maximum=FMath::Max(390.f,MaximumNewCaptionWidth);
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
    const float DesiredWidth=FMath::Clamp(FMath::CeilToFloat(CaptionAdvance+OuterInset+2.f),390.f,Maximum);
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
    if(!NewHologramTitle || !NewHologramReward || !FSlateApplication::IsInitialized()) return;
    const auto Renderer=FSlateApplication::Get().GetRenderer();if(!Renderer) return;
    const auto Measure=Renderer->GetFontMeasureService();
    if(!bWorldFontsPrepared) {
        const FName Keys[]={TEXT("UI.Grill.Rolling"),TEXT("UI.Grill.Accepted"),TEXT("UI.Grill.Combination"),TEXT("UI.Grill.Idle"),TEXT("UI.Grill.Coins"),TEXT("UI.Grill.NoPrize"),TEXT("UI.Grill.Item")};
        const FText Fallbacks[]={LOCTEXT("Rolling","КРУТИМ…"),LOCTEXT("Cooking","РЫБА ПРИНЯТА"),LOCTEXT("Combination","ВАША КОМБИНАЦИЯ"),
            LOCTEXT("Idle","1 РЫБА · 1 ПОПЫТКА"),LOCTEXT("Coins","+{0} монет"),LOCTEXT("NoPrize","Без выигрыша · попробуй ещё"),LOCTEXT("Item","{0} ×{1}")};
        for(int32 KeyIndex=0;KeyIndex<UE_ARRAY_COUNT(Keys);++KeyIndex) {
            const FText Copy=Localized(Keys[KeyIndex],Fallbacks[KeyIndex]);
            Measure->Measure(Copy,NewHologramTitle->GetFont(),1.f);Measure->Measure(Copy,NewHologramReward->GetFont(),1.f);
        }
        const FText Digits=FText::AsCultureInvariant(TEXT("0123456789 +×%"));
        Measure->Measure(Digits,NewHologramTitle->GetFont(),1.f);Measure->Measure(Digits,NewHologramReward->GetFont(),1.f);
        bWorldFontsPrepared=true;
    }
    const auto* Identity=GetOwningPlayer()?GetOwningPlayer()->GetPlayerState<AMCPlayerState>():nullptr;
    const auto* Economy=Identity && Identity->Economy?Identity->Economy.Get():GetDefault<UMCHubEconomyComponent>();
    // Get only: the Hub manifest/economy owns catalog loading. No interaction-time
    // load or replacement catalog is introduced by this presentation.
    UDataTable* ResidentCatalog=Economy?Economy->CatalogTable.Get():nullptr;
    // A configured live catalog must become resident itself; never substitute
    // default names and incorrectly acknowledge its readiness.
    if(PreparedGrillCatalog.Get()!=ResidentCatalog) {PreparedGrillCatalog=ResidentCatalog;bCatalogNamesPrepared=false;}
    if(bCatalogNamesPrepared || !ResidentCatalog || ResidentCatalog->GetRowStruct()!=FMCHubCatalogRow::StaticStruct()) return;
    for(const auto& Entry:ResidentCatalog->GetRowMap()) {
        const auto* Item=reinterpret_cast<const FMCHubCatalogRow*>(Entry.Value);if(!Item) continue;
        const FText ItemName=Localized(Item->DisplayNameKey,Item->DisplayName);
        Measure->Measure(ItemName,NewHologramReward->GetFont(),1.f);
    }
    bCatalogNamesPrepared=true;
}
bool UMCHubGrillHologramWidget::HasPreparedNewPresentation() const
{
    if(!NewHologramRoot || !NewHologramTitle || !NewHologramReward || !NewHologramRewardCard || !bWorldFontsPrepared || !bCatalogNamesPrepared
        || NewHologramReels.Num()!=3 || Reels.Num()!=3 || ReelMaterials.Num()!=3) return false;
    for(int32 ReelIndex=0;ReelIndex<3;++ReelIndex)
        if(!NewHologramReels[ReelIndex] || !Reels[ReelIndex] || !ReelMaterials[ReelIndex]
            || NewHologramReels[ReelIndex]->GetBrush().GetResourceObject()!=Reels[ReelIndex]->GetBrush().GetResourceObject()) return false;
    return true;
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
    Display->SetWidgetSpace(EWidgetSpace::Screen);Display->SetDrawSize(FVector2D(390,190));Display->SetPivot(FVector2D(.5,1));
    Display->SetWidgetClass(UMCGameplayUIFactory::ResolveClass(TEXT("GrillHologram"),WidgetClass?WidgetClass.Get():UMCHubGrillHologramWidget::StaticClass()));
    Display->SetCollisionEnabled(ECollisionEnabled::NoCollision);Display->SetGenerateOverlapEvents(false);Display->SetCanEverAffectNavigation(false);
    Display->SetCastShadow(false);Display->RegisterComponent();Display->InitWidget();
    if(auto* W=Cast<UMCHubGrillHologramWidget>(Display->GetUserWidgetObject())) W->SetStation(Station.Get());
    // Create all material variants now, before the initial Hub readiness snapshot.
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
    const bool HasOwner=IsValid(PC) && PC->IsLocalController() && PC->GetLocalPlayer() && IsValid(Pawn);
    // A visible hologram can still be fading when its player loses/replaces a
    // Pawn. Do not carry that visibility into the distance/owner updates below.
    if(!HasOwner) {
        RestoreDisplaySizing();VisibilityAlpha=0.f;
        Display->SetVisibility(false);Display->SetHiddenInGame(true);Display->SetComponentTickEnabled(false);
        Grill->SetLocalPromptVisible(PC,false);
        return;
    }
    const bool Show=FVector::DistSquared(Pawn->GetActorLocation(),Grill->GetActorLocation())<FMath::Square(FMath::Clamp(VisibleDistance,300.f,5000.f));
    VisibilityAlpha=FMath::FInterpTo(VisibilityAlpha,Show?1.f:0.f,Dt,12.f);
    const bool Visible=VisibilityAlpha>.01f;
    Display->SetVisibility(Visible);Display->SetHiddenInGame(!Visible);Display->SetComponentTickEnabled(Visible);
    if(!Visible) {RestoreDisplaySizing();return;}
    Display->SetOwnerPlayer(PC->GetLocalPlayer());
    if(auto* W=Cast<UMCHubGrillHologramWidget>(Display->GetUserWidgetObject())) {
        W->SetOwningPlayer(PC);W->SetRenderOpacity(VisibilityAlpha*.95f);
        const float Distance=FVector::Distance(Pawn->GetActorLocation(),Grill->GetActorLocation());
        const float Scale=FMath::Clamp(800.f/FMath::Max(Distance,1.f),.55f,1.f);
        W->SetRenderScale(FVector2D(Scale*(.96f+.04f*VisibilityAlpha)));W->TickPresentation(Dt);
    }
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
