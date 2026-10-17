#include "MCPerkCardWidget.h"

#include "MCGameplayUITheme.h"
#include "MCGameplayHUD.h"
#include "MCUIVisualStyle.h"
#include "MCPerkChoiceWidget.h"
#include "MCUIDataLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/RichTextBlock.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"

#define LOCTEXT_NAMESPACE "MCPerkCard"

namespace MCPerkCardPresentationPrivate
{
    bool LowerIsBetter(EMCRogueStat Stat)
    {
        switch(Stat) {
        case EMCRogueStat::DashCooldown:case EMCRogueStat::DirtyDuration:case EMCRogueStat::CarryPenalty:
        case EMCRogueStat::SpoilRate:case EMCRogueStat::FoodScatter:case EMCRogueStat::FallingNutDamage:
        case EMCRogueStat::FreezeRate:case EMCRogueStat::LegIceHealth:case EMCRogueStat::ChainsawCrashDamage:
        case EMCRogueStat::BraceDamage:case EMCRogueStat::RagdollDuration:case EMCRogueStat::IncomingDamage:case EMCRogueStat::WaveResistance:return true;
        default:return false;
        }
    }
    FString Markup(const FMCPerkCardPresentation& Data)
    {
        FString Value=Data.Description.ToString().Replace(TEXT("<br>"),TEXT("\n"));
        Value.ReplaceInline(TEXT("&"),TEXT("&amp;"));
        Value.ReplaceInline(TEXT("<"),TEXT("&lt;"));
        Value.ReplaceInline(TEXT(">"),TEXT("&gt;"));
        TArray<FString> Tokens=Data.BenefitTokens;
        for(const auto& Token:Data.CostTokens) Tokens.AddUnique(Token);
        Tokens.Sort([](const FString& A,const FString& B){return A.Len()>B.Len();});
        FString Result;
        // Match complete fragments once. In particular +8 must never colour part of +80.
        for(int32 At=0;At<Value.Len();) {
            bool Matched=false;
            for(const FString& Token:Tokens) {
                if(Token.IsEmpty() || Data.BenefitTokens.Contains(Token)==Data.CostTokens.Contains(Token) || Value.Mid(At,Token.Len())!=Token) continue;
                const int32 Next=At+Token.Len();
                // A sentence comma/full stop after a complete percentage is a
                // boundary, not a continuation of the number being matched.
                const bool DecimalContinues=!Token.EndsWith(TEXT("%")) && Next+1<Value.Len()
                    && (Value[Next]==TEXT('.') || Value[Next]==TEXT(',')) && FChar::IsDigit(Value[Next+1]);
                if(Next<Value.Len() && (FChar::IsDigit(Value[Next]) || Value[Next]==TEXT('%') || DecimalContinues)) continue;
                Result+=(Data.BenefitTokens.Contains(Token)?TEXT("<benefit>"):TEXT("<cost>"))+Token+TEXT("</>");At=Next;Matched=true;break;
            }
            if(!Matched) Result.AppendChar(Value[At++]);
        }
        return Result;
    }
}

void UMCPerkCardWidget::ApplyDefinitionMetadata(FMCPerkCardPresentation& Data,const FMCPerkDefinition& Definition,
    UDataTable* Localization,const FString& CultureName,bool bPersonal,int32 Before)
{
    Data.Polarity=Definition.Polarity;Data.BenefitTokens.Reset();Data.CostTokens.Reset();
    auto Copy=[&](FName Key,const FText& Fallback){return UMCUIDataLibrary::ResolveText(Localization,Key,Fallback,CultureName);};
    auto Format=[&](const FText& Pattern,const FFormatNamedArguments& Arguments){return UMCUIDataLibrary::FormatTextForCulture(Pattern,Arguments,CultureName);};
    const FCulturePtr Culture=FInternationalization::Get().GetCulture(CultureName);
    const FText Polarity=Definition.Polarity==EMCPerkPolarity::Risky?Copy(TEXT("UI.Reward.Badge.Risky"),LOCTEXT("Risky","С ЦЕНОЙ")):
        Definition.Polarity==EMCPerkPolarity::Positive?Copy(TEXT("UI.Reward.Badge.Positive"),LOCTEXT("Positive","ПОЛОЖИТЕЛЬНЫЙ")):Copy(TEXT("UI.Reward.Badge.Negative"),LOCTEXT("Negative","ПРОКЛЯТИЕ"));
    const FText Rarity=Definition.Rarity==EMCPerkRarity::Legendary?Copy(TEXT("UI.Reward.Rarity.Legendary"),LOCTEXT("Legendary","ЛЕГЕНДАРНЫЙ")):
        Definition.Rarity==EMCPerkRarity::Rare?Copy(TEXT("UI.Reward.Rarity.Rare"),LOCTEXT("Rare","РЕДКИЙ")):Copy(TEXT("UI.Reward.Rarity.Standard"),LOCTEXT("Standard","ОБЫЧНЫЙ"));
    const FText Scope=!bPersonal && Definition.Rarity==EMCPerkRarity::Legendary && Definition.ToolUpgrade!=EMCToolUpgrade::None?
        Copy(TEXT("UI.Reward.Scope.Team"),LOCTEXT("Team","КОМАНДА")):Copy(TEXT("UI.Reward.Scope.Personal"),LOCTEXT("Personal","ВАМ"));
    Data.Badge=Format(Copy(TEXT("UI.Reward.BadgeFormat"),LOCTEXT("BadgeFormat","{Polarity} · {Rarity} · {Scope}")),FFormatNamedArguments{{TEXT("Polarity"),Polarity},{TEXT("Rarity"),Rarity},{TEXT("Scope"),Scope}});
    auto Number=[&](double V){return FText::AsNumber(V,nullptr,Culture);};
    Data.StackPreview=Format(Copy(TEXT("UI.Reward.StackFormat"),LOCTEXT("StackFormat","Стак {Before} → {After} / {Maximum}")),FFormatNamedArguments{{TEXT("Before"),Number(Before)},{TEXT("After"),Number(Before+1)},{TEXT("Maximum"),Number(Definition.MaxStacks)}});
    for(const auto& M:Definition.Modifiers) if(!FMath::IsNearlyZero(M.Value)) {
        const bool Benefit=(M.Value<0)==MCPerkCardPresentationPrivate::LowerIsBetter(M.Stat);
        // Percentage multiplication can produce 30.000002 from a float .3.
        // Use display precision and both decimal separators for authored copy.
        FNumberFormattingOptions TokenNumber=FNumberFormattingOptions::DefaultNoGrouping();TokenNumber.SetMinimumFractionalDigits(0);TokenNumber.SetMaximumFractionalDigits(3);
        const FString Magnitude=FText::AsNumber(FMath::Abs(M.bFlat?M.Value:M.Value*100.f),&TokenNumber,Culture).ToString();
        TArray<FString> Magnitudes={Magnitude};Magnitudes.AddUnique(Magnitude.Replace(TEXT(","),TEXT(".")));Magnitudes.AddUnique(Magnitude.Replace(TEXT("."),TEXT(",")));
        const FString SourceMagnitude=FText::AsNumber(FMath::Abs(M.bFlat?M.Value:M.Value*100.f),&TokenNumber,FInternationalization::Get().GetCulture(TEXT("en"))).ToString();
        Magnitudes.AddUnique(SourceMagnitude);Magnitudes.AddUnique(SourceMagnitude.Replace(TEXT("."),TEXT(",")));
        const FString Suffix=M.bFlat?TEXT(""):TEXT("%");
        auto& Tokens=Benefit?Data.BenefitTokens:Data.CostTokens;
        for(const auto& Value:Magnitudes) if(M.Value<0) {Tokens.AddUnique(TEXT("−")+Value+Suffix);Tokens.AddUnique(TEXT("-")+Value+Suffix);}
        else Tokens.AddUnique(TEXT("+")+Value+Suffix);
    }
}

void UMCPerkCardWidget::BindPresentationTree()
{
    ChoiceButton=Cast<UMCPerkCardButton>(WidgetTree->FindWidget(TEXT("CardButton")));
    BadgeBorder=Cast<UBorder>(WidgetTree->FindWidget(TEXT("BadgeBorder")));
    StatsBorder=Cast<UBorder>(WidgetTree->FindWidget(TEXT("StatsBorder")));
    Illustration=Cast<UImage>(WidgetTree->FindWidget(TEXT("Illustration")));
    IconFallback=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("IconFallback")));
    NativeIcon=Cast<UMCHUDIcon>(WidgetTree->FindWidget(TEXT("CardNativeIcon")));
    BodyScroll=Cast<UScrollBox>(WidgetTree->FindWidget(TEXT("CardBodyScroll")));
    NameLabel=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("CardName")));
    BadgeLabel=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("CardBadge")));
    StatsLabel=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("CardStats")));
    DescriptionLabel=Cast<URichTextBlock>(WidgetTree->FindWidget(TEXT("CardDescription")));
    // Normalize known layout properties without replacing an artist-authored tree.
    if(NameLabel) {NameLabel->SetAutoWrapText(true);NameLabel->SetWrapTextAt(0);NameLabel->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);}
    if(DescriptionLabel) {DescriptionLabel->SetAutoWrapText(true);DescriptionLabel->SetWrapTextAt(0);DescriptionLabel->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);}
    if(BadgeLabel) BadgeLabel->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
    if(StatsLabel) StatsLabel->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
    if(Illustration) if(auto* Fit=Cast<UScaleBox>(Illustration->GetParent())) if(auto* FitSlot=Cast<UOverlaySlot>(Fit->Slot)) FitSlot->SetVerticalAlignment(VAlign_Fill);
    BindStyle(TEXT("BadgeBorder"),EMCUIColorToken::Cream,EMCUIStyleTarget::Background);
    BindStyle(TEXT("StatsBorder"),EMCUIColorToken::LCDBackground,EMCUIStyleTarget::Background);
    BindStyle(TEXT("CardName"),EMCUIColorToken::Ink,EMCUIStyleTarget::Text,21,true);
    BindStyle(TEXT("CardBadge"),EMCUIColorToken::Ink,EMCUIStyleTarget::Text,11,true);
    BindStyle(TEXT("CardStats"),EMCUIColorToken::LCDInk,EMCUIStyleTarget::Text,14,true);
}

void UMCPerkCardWidget::BuildPresentationTree()
{
    if(!WidgetTree || ChoiceButton) return;
    // A designer-authored tree is supported when it exposes these named components.
    if(WidgetTree->RootWidget) {
        BindPresentationTree();
        return;
    }
    ChoiceButton=WidgetTree->ConstructWidget<UMCPerkCardButton>(UMCPerkCardButton::StaticClass(),TEXT("CardButton"));
    WidgetTree->RootWidget=ChoiceButton;
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("CardColumn"));
    auto* CardContentSlot=Cast<UButtonSlot>(ChoiceButton->SetContent(Column));
    CardContentSlot->SetPadding(FMargin(16,14));CardContentSlot->SetHorizontalAlignment(HAlign_Fill);CardContentSlot->SetVerticalAlignment(VAlign_Fill);
    BadgeBorder=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("BadgeBorder"));
    BadgeBorder->SetPadding(FMargin(10,7));
    BadgeLabel=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("CardBadge"));
    BadgeLabel->SetAutoWrapText(true);BadgeLabel->SetJustification(ETextJustify::Center);
    BadgeBorder->SetContent(BadgeLabel);
    Column->AddChildToVerticalBox(BadgeBorder)->SetPadding(FMargin(0,0,0,10));

    BodyScroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("CardBodyScroll"));
    BodyScroll->SetAllowOverscroll(false);BodyScroll->SetAllowRightClickDragScrolling(false);
    BodyScroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
    BodyScroll->SetScrollbarThickness(FVector2D(5,5));BodyScroll->SetScrollbarPadding(FMargin(4,0,0,0));
    BodyScroll->SetAnimateWheelScrolling(true);BodyScroll->SetWheelScrollMultiplier(1.f);
    Column->AddChildToVerticalBox(BodyScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto* Body=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("CardBodyColumn"));
    BodyScroll->AddChild(Body);
    auto* IconSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("CardIllustrationSize"));
    IconSize->SetWidthOverride(160);IconSize->SetHeightOverride(116);
    auto* IconLayers=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("CardIllustrationLayers"));IconSize->SetContent(IconLayers);
    NativeIcon=WidgetTree->ConstructWidget<UMCHUDIcon>(UMCHUDIcon::StaticClass(),TEXT("CardNativeIcon"));NativeIcon->bTooth=true;NativeIcon->bHappy=true;
    auto* NativeSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("CardNativeIconSize"));NativeSize->SetWidthOverride(96);NativeSize->SetHeightOverride(108);NativeSize->SetContent(NativeIcon);
    auto* NativeSlot=IconLayers->AddChildToOverlay(NativeSize);NativeSlot->SetHorizontalAlignment(HAlign_Center);NativeSlot->SetVerticalAlignment(VAlign_Center);
    Illustration=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("Illustration"));
    auto* Fit=WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),TEXT("CardImageFit"));Fit->SetStretch(EStretch::ScaleToFit);Fit->SetContent(Illustration);
    auto* IllustrationSlot=IconLayers->AddChildToOverlay(Fit);IllustrationSlot->SetHorizontalAlignment(HAlign_Fill);IllustrationSlot->SetVerticalAlignment(VAlign_Fill);
    IconFallback=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("IconFallback"));
    IconFallback->SetJustification(ETextJustify::Center);
    auto* FallbackSlot=IconLayers->AddChildToOverlay(IconFallback);FallbackSlot->SetHorizontalAlignment(HAlign_Right);FallbackSlot->SetVerticalAlignment(VAlign_Bottom);
    auto* IconSlot=Body->AddChildToVerticalBox(IconSize);IconSlot->SetHorizontalAlignment(HAlign_Center);IconSlot->SetPadding(FMargin(0,0,0,10));

    NameLabel=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("CardName"));
    NameLabel->SetAutoWrapText(true);NameLabel->SetJustification(ETextJustify::Center);
    Body->AddChildToVerticalBox(NameLabel)->SetPadding(FMargin(0,0,0,12));
    DescriptionLabel=WidgetTree->ConstructWidget<URichTextBlock>(URichTextBlock::StaticClass(),TEXT("CardDescription"));
    DescriptionLabel->SetAutoWrapText(true);DescriptionLabel->SetJustification(ETextJustify::Center);
    Body->AddChildToVerticalBox(DescriptionLabel)->SetPadding(FMargin(2,0,6,12));
    StatsBorder=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("StatsBorder"));StatsBorder->SetPadding(FMargin(10,8));
    StatsLabel=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("CardStats"));
    StatsLabel->SetAutoWrapText(true);StatsLabel->SetJustification(ETextJustify::Center);StatsBorder->SetContent(StatsLabel);
    Column->AddChildToVerticalBox(StatsBorder)->SetPadding(FMargin(0,10,0,0));
    BindPresentationTree();
    ApplyTheme();
}

void UMCPerkCardWidget::Configure(const FMCPerkCardPresentation& Data,AMCPlayerController* Controller,int32 ChoiceIndex,bool bInInteractive)
{
    TGuardValue<bool> PreviewGuard(bApplyingPreview,Controller==nullptr);
    BuildPresentationTree();
    const bool NewCard=Presentation.GameplayID!=Data.GameplayID || !Presentation.DisplayName.IdenticalTo(Data.DisplayName) || !Presentation.Description.IdenticalTo(Data.Description);
    Presentation=Data;bInteractive=bInInteractive;bHasExternalPresentation=true;
    if(NewCard && BodyScroll) BodyScroll->SetScrollOffset(0);
    if(ChoiceButton) ChoiceButton->Configure(Controller,ChoiceIndex);
    RefreshPresentation();ApplyTheme();SetInteractionEnabled(bInInteractive);
}

void UMCPerkCardWidget::SetInteractionEnabled(bool bEnabled)
{
    bInteractive=bEnabled;
    if(ChoiceButton) {ChoiceButton->SetIsEnabled(true);ChoiceButton->SetInteractionAllowed(bEnabled);}
}

FButtonStyle UMCPerkCardWidget::GetBaseButtonStyle() const { return BaseButtonStyle; }

void UMCPerkCardWidget::ScrollContent(float Delta)
{
    if(BodyScroll) BodyScroll->SetScrollOffset(FMath::Clamp(BodyScroll->GetScrollOffset()+Delta,0.f,BodyScroll->GetScrollOffsetOfEnd()));
}

void UMCPerkCardWidget::RefreshPresentation()
{
    if(NameLabel) NameLabel->SetText(Presentation.DisplayName);
    if(BadgeLabel) BadgeLabel->SetText(Presentation.Badge);
    if(BadgeBorder) BadgeBorder->SetVisibility(Presentation.Badge.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible);
    if(DescriptionLabel) DescriptionLabel->SetText(FText::FromString(MCPerkCardPresentationPrivate::Markup(Presentation)));
    if(StatsLabel) StatsLabel->SetText(Presentation.StackPreview);
    if(StatsBorder) StatsBorder->SetVisibility(Presentation.StackPreview.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible);
    if(IconFallback) IconFallback->SetText(FText::AsCultureInvariant(Presentation.Polarity==EMCPerkPolarity::Negative?TEXT("−"):Presentation.Polarity==EMCPerkPolarity::Risky?TEXT("±"):TEXT("+")));
    if(NativeIcon) {NativeIcon->bHappy=Presentation.Polarity==EMCPerkPolarity::Positive;NativeIcon->bSad=Presentation.Polarity==EMCPerkPolarity::Negative;NativeIcon->InvalidateLayoutAndVolatility();}
    UpdateImage();
}

void UMCPerkCardWidget::ShowLoadedImage(UTexture2D* Texture)
{
    if(Illustration) {
        if(Illustration->GetBrush().GetResourceObject()!=Texture) Illustration->SetBrushFromTexture(Texture,true);
        Illustration->SetVisibility(Texture?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    }
    if(NativeIcon) NativeIcon->SetVisibility(Texture?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    if(IconFallback) IconFallback->SetVisibility(Texture?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
}

void UMCPerkCardWidget::UpdateImage()
{
    const auto Image=Presentation.Image;
    if(bApplyingPreview || IsDesignTime()) {
        if(ImageLoadHandle.IsValid()) {ImageLoadHandle->CancelHandle();ImageLoadHandle.Reset();}
        RequestedImage=Image;bImageRequestFinished=true;
        ShowLoadedImage(Image.IsNull()?nullptr:Image.LoadSynchronous());return;
    }
    if(RequestedImage==Image) {
        if(UTexture2D* Loaded=Image.Get()) {ShowLoadedImage(Loaded);return;}
        if(ImageLoadHandle.IsValid() || bImageRequestFinished) return;
    }
    if(ImageLoadHandle.IsValid()) {ImageLoadHandle->CancelHandle();ImageLoadHandle.Reset();}
    RequestedImage=Image;bImageRequestFinished=Image.IsNull();ShowLoadedImage(Image.Get());
    if(Image.IsNull() || Image.Get()) return;
    ImageLoadHandle=UAssetManager::GetStreamableManager().RequestAsyncLoad(Image.ToSoftObjectPath(),FStreamableDelegate::CreateWeakLambda(this,[this,Image]() {
        if(RequestedImage!=Image) return;
        bImageRequestFinished=true;ShowLoadedImage(Image.Get());ImageLoadHandle.Reset();
    }));
    if(!ImageLoadHandle.IsValid()) bImageRequestFinished=true;
}

void UMCPerkCardWidget::ApplyTheme()
{
    const auto* Style=GetResolvedTheme();if(!Style) return;
    const auto& P=Style->Palette;
    const FLinearColor Semantic=Presentation.Polarity==EMCPerkPolarity::Negative?P.Cost:Presentation.Polarity==EMCPerkPolarity::Positive?P.Benefit:P.Ink;
    Super::ApplyTheme();
    const FVector2D CardSize=MCUIVisualStyle::SizeFor(ChoiceButton,FVector2D(316,480));
    BaseButtonStyle.SetNormal(MCUIVisualStyle::Surface(ChoiceButton,BaseButtonStyle.Normal,Style,P.Surface,CardSize,12));
    BaseButtonStyle.SetHovered(MCUIVisualStyle::Surface(ChoiceButton,BaseButtonStyle.Hovered,Style,P.SurfaceAlt,CardSize,12,false,0,false,true));
    BaseButtonStyle.SetPressed(MCUIVisualStyle::Surface(ChoiceButton,BaseButtonStyle.Pressed,Style,P.Mint,CardSize,12,false,0,false,true));
    // Read-only contract cards keep their full readable ink rather than disabled greying.
    BaseButtonStyle.SetDisabled(MCUIVisualStyle::Surface(ChoiceButton,BaseButtonStyle.Disabled,Style,P.Surface,CardSize,12));
    BaseButtonStyle.SetNormalPadding(FMargin(0));BaseButtonStyle.SetPressedPadding(FMargin(0));
    if(ChoiceButton) {ChoiceButton->SetStyle(BaseButtonStyle);ChoiceButton->SetBackgroundColor(FLinearColor::White);}
    if(IconFallback) {IconFallback->SetFont(Style->MakeFont(NativeIcon?30:58));IconFallback->SetColorAndOpacity(Semantic);}
    if(NativeIcon) {NativeIcon->Tint=P.Cream;NativeIcon->InkTint=P.Ink;NativeIcon->HostTint=Semantic;NativeIcon->InvalidateLayoutAndVolatility();}
    if(Illustration) Illustration->SetColorAndOpacity(FLinearColor::White);
    if(DescriptionLabel) {
        if(!RichStyles) {RichStyles=NewObject<UDataTable>(this);RichStyles->RowStruct=FRichTextStyleRow::StaticStruct();}
        else RichStyles->EmptyTable();
        FTextBlockStyle TextStyle;TextStyle.SetFont(Style->MakeFont(16,false));TextStyle.SetColorAndOpacity(P.Ink);
        FRichTextStyleRow Default;Default.TextStyle=TextStyle;RichStyles->AddRow(TEXT("Default"),Default);
        FRichTextStyleRow Benefit=Default;Benefit.TextStyle.SetColorAndOpacity(P.Benefit);Benefit.TextStyle.SetFont(Style->MakeFont(16));RichStyles->AddRow(TEXT("benefit"),Benefit);
        FRichTextStyleRow Cost=Default;Cost.TextStyle.SetColorAndOpacity(P.Cost);Cost.TextStyle.SetFont(Style->MakeFont(16));RichStyles->AddRow(TEXT("cost"),Cost);
        DescriptionLabel->SetTextStyleSet(nullptr);
        DescriptionLabel->SetTextStyleSet(RichStyles);DescriptionLabel->SetDefaultTextStyle(TextStyle);
    }
}

void UMCPerkCardWidget::ApplyEditorPreview(float Time)
{
    BuildPresentationTree();
    // In a preview of the complete choice panel the parent already supplied this card's fixture.
    if(bHasExternalPresentation) return;
    TGuardValue<bool> PreviewGuard(bApplyingPreview,true);
    FMCPerkCardPresentation Data=SampleCard;
    const auto* Style=GetResolvedTheme();
    UDataTable* Cards=Style && !Style->CardTable.IsNull()?Style->CardTable.LoadSynchronous():UMCUIDataLibrary::GetDefaultCardTable();
    UDataTable* CopyTable=Style && !Style->LocalizationTable.IsNull()?Style->LocalizationTable.LoadSynchronous():UMCUIDataLibrary::GetDefaultLocalizationTable();
    // Explicit Details Preview calls ResetEditorPreview. Play reuses its fixture
    // until inputs change; no synchronous image loads or MID/style rebuilds per tick.
    if(bHasPreviewFixture && LastPreviewCardRow==PreviewCardRow && LastPreviewCulture==PreviewCulture
        && bLastPreviewReadOnly==bPreviewReadOnly && LastPreviewCardTable.Get()==Cards && LastPreviewCopyTable.Get()==CopyTable
        && FMCPerkCardPresentation::StaticStruct()->CompareScriptStruct(&LastPreviewSample,&SampleCard,0)) {
        SetRenderOpacity(Time<=0.f?1.f:FMath::Clamp(Time*6.f,0.f,1.f));return;
    }
    const bool HasSelectedRow=PreviewCardRow.DataTable || !PreviewCardRow.RowName.IsNone();
    bPreviewRowFound=false;bPreviewUsedFallback=false;
    if(HasSelectedRow || Data.DisplayName.IsEmpty()) {
        const auto Content=HasSelectedRow?UMCUIDataLibrary::ResolveCard(PreviewCardRow,PreviewCulture):UMCUIDataLibrary::FindCardForGameplayID(Cards,TEXT("P03"),PreviewCulture);
        bPreviewRowFound=Content.bFound;bPreviewUsedFallback=Content.bUsedFallback;
        Data.GameplayID=HasSelectedRow?NAME_None:FName(TEXT("P03"));
        if(HasSelectedRow && PreviewCardRow.DataTable && PreviewCardRow.DataTable->GetRowStruct()==FMCUICardRow::StaticStruct())
            if(const auto* Row=PreviewCardRow.DataTable->FindRow<FMCUICardRow>(PreviewCardRow.RowName,TEXT("Card preview"),false)) Data.GameplayID=Row->GameplayPerkID;
        // UI copy and image are authoritative even when deliberately empty.
        Data.DisplayName=Content.DisplayName;Data.Description=Content.Description;Data.Image=Content.Image;
        Data.Badge=FText::GetEmpty();Data.StackPreview=FText::GetEmpty();Data.BenefitTokens.Reset();Data.CostTokens.Reset();
        if(!PreviewDefinitions) PreviewDefinitions=LoadObject<UDataTable>(nullptr,TEXT("/Game/Gameplay/Roguelike/DT_Perks.DT_Perks"));
        const auto* Definition=PreviewDefinitions && PreviewDefinitions->GetRowStruct()==FMCPerkDefinition::StaticStruct()
            ?PreviewDefinitions->FindRow<FMCPerkDefinition>(Data.GameplayID,TEXT("Card preview metadata"),false):nullptr;
        if(Definition) {
            ApplyDefinitionMetadata(Data,*Definition,CopyTable,PreviewCulture,false,0);
            if(!Content.bFound) {Data.DisplayName=Definition->DisplayName;Data.Description=Definition->Description;Data.Image=Definition->Icon;}
        }
        if(Data.DisplayName.IsEmpty()) Data.DisplayName=FText::FromName(HasSelectedRow?PreviewCardRow.RowName:Data.GameplayID);
    }
    Presentation=Data;
    if(ChoiceButton) ChoiceButton->Configure(nullptr,0);
    RefreshPresentation();ApplyTheme();SetInteractionEnabled(!bPreviewReadOnly);
    LastPreviewSample=SampleCard;LastPreviewCardRow=PreviewCardRow;LastPreviewCulture=PreviewCulture;bLastPreviewReadOnly=bPreviewReadOnly;
    LastPreviewCardTable=Cards;LastPreviewCopyTable=CopyTable;bHasPreviewFixture=true;
    SetRenderOpacity(Time<=0.f?1.f:FMath::Clamp(Time*6.f,0.f,1.f));
}

void UMCPerkCardWidget::ResetEditorPreview() {bHasPreviewFixture=false;SetRenderOpacity(1.f);}

void UMCPerkCardWidget::NativeDestruct()
{
    if(ImageLoadHandle.IsValid()) {ImageLoadHandle->CancelHandle();ImageLoadHandle.Reset();}
    RequestedImage.Reset();bImageRequestFinished=false;
    Super::NativeDestruct();
}

#undef LOCTEXT_NAMESPACE
