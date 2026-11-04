#include "MCWorldLabelWidget.h"

#include "MCUniversalUIWidget.h"
#include "MCUIDataLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Rendering/SlateRenderer.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "MCWorldLabel"

UMCWorldLabelWidget::UMCWorldLabelWidget()
{
    bUseNativeLayout=false;
    SetIsFocusable(false);
    PresentationTheme=TSoftObjectPtr<UMCGameplayUITheme>(FSoftObjectPath(TEXT("/Game/Gameplay/UI/V2/Theme/DA_UITheme_V2.DA_UITheme_V2")));
}

bool UMCWorldLabelWidget::AreThemeResourcesResident(const UMCGameplayUITheme* Candidate,const UDataTable* CopyOverride)
{
    return IsValid(Candidate) && Candidate->MakeFont(22,true).HasValidFont()
        && (IsValid(CopyOverride) || Candidate->LocalizationTable.IsNull() || Candidate->LocalizationTable.Get()!=nullptr);
}

bool UMCWorldLabelWidget::ResolveResidentTheme()
{
    auto* Candidate=IsValid(Theme.Get())?Theme.Get():PresentationTheme.Get();
    if(!Candidate && IsDesignTime() && !PresentationTheme.IsNull()) Candidate=PresentationTheme.LoadSynchronous();
    if(IsDesignTime() && Candidate && !Candidate->LocalizationTable.IsNull()) Candidate->LocalizationTable.LoadSynchronous();
    if(!AreThemeResourcesResident(Candidate,TranslationTable.Get())) return false;
    Theme=Candidate;return true;
}

UDataTable* UMCWorldLabelWidget::GetResolvedTranslationTable() const
{
    if(IsValid(TranslationTable.Get())) return TranslationTable.Get();
    const auto* Candidate=IsValid(Theme.Get())?Theme.Get():PresentationTheme.Get();
    // Source labels already own their FText. Never introduce a first-interaction table load here.
    return Candidate?Candidate->LocalizationTable.Get():nullptr;
}

void UMCWorldLabelWidget::BuildPresentationTree()
{
    if(!WidgetTree || !ResolveResidentTheme()) return;
    if(WidgetTree->RootWidget) {
        LabelBounds=WidgetTree->FindWidget<USizeBox>(TEXT("WorldLabelBounds"));
        LabelFrame=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("WorldLabelFrame"));
        CaptionText=WidgetTree->FindWidget<UTextBlock>(TEXT("WorldLabelText"));
        return;
    }
    LabelBounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("WorldLabelBounds"));
    WidgetTree->RootWidget=LabelBounds;
    LabelFrame=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),TEXT("WorldLabelFrame"));
    LabelFrame->Theme=Theme;LabelFrame->Kind=EMCUniversalUIKind::Panel;
    LabelFrame->Label=FText::GetEmpty();LabelFrame->LabelKey=NAME_None;
    LabelFrame->bUseThemeShape=false;LabelFrame->CornerRadius=12.f;LabelFrame->OutlineWidth=2.f;
    LabelFrame->ContentPadding=FMargin(14,8);LabelFrame->ShadowOffset=FVector2D(0,3);
    LabelFrame->bCustomColors=true;LabelFrame->Initialize();LabelFrame->BuildPresentationTree();
    LabelFrame->SetIsFocusable(false);LabelFrame->SetVisibility(ESlateVisibility::HitTestInvisible);
    CaptionText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("WorldLabelText"));
    CaptionText->SetJustification(ETextJustify::Center);CaptionText->SetAutoWrapText(false);
    CaptionText->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
    CaptionText->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
    CaptionText->SetVisibility(ESlateVisibility::HitTestInvisible);
    LabelFrame->SetContentForSlot(TEXT("Content"),CaptionText);LabelBounds->SetContent(LabelFrame);
    SetVisibility(ESlateVisibility::HitTestInvisible);
    ApplyTheme();
}

void UMCWorldLabelWidget::ApplyTheme()
{
    if(!ResolveResidentTheme()) {bPrepared=false;return;}
    Super::ApplyTheme();
    if(!CaptionText || !LabelFrame) return;
    CaptionText->SetFont(Theme->MakeFont(FMath::Clamp(CaptionFontSize,8,48),true));
    LabelFrame->Theme=Theme;LabelFrame->Label=FText::GetEmpty();LabelFrame->LabelKey=NAME_None;
    LabelFrame->CustomFill=Theme->Palette.LCDBackground.CopyWithNewOpacity(.98f);
    LabelFrame->CustomShadow=Theme->Palette.Marine.CopyWithNewOpacity(.85f);
    LabelFrame->CustomOutline=SourceTint;LabelFrame->CustomText=SourceTint;LabelFrame->ApplyTheme();
    CaptionText->SetColorAndOpacity(SourceTint);
    bPrepared=false;RefreshCaption();
}

void UMCWorldLabelWidget::SetPresentation(const FText& ActualText,FLinearColor ActualTint)
{
    const bool CopyChanged=!SourceCaption.EqualTo(ActualText);
    const bool TintChanged=!SourceTint.Equals(ActualTint);
    SourceCaption=ActualText;SourceTint=ActualTint;
    if(!CaptionText || !LabelFrame || !LabelBounds) BuildPresentationTree();
    if(CopyChanged) {bPrepared=false;RefreshCaption();}
    if(TintChanged && CaptionText && LabelFrame) {
        CaptionText->SetColorAndOpacity(SourceTint);LabelFrame->CustomOutline=SourceTint;
        LabelFrame->CustomText=SourceTint;LabelFrame->ApplyTheme();
    }
}

void UMCWorldLabelWidget::RefreshCaption()
{
    if(CaptionText) CaptionText->SetText(SourceCaption);
}

bool UMCWorldLabelWidget::PreparePresentation()
{
    if(!ResolveResidentTheme() || !FSlateApplication::IsInitialized()) return bPrepared=false;
    BuildPresentationTree();
    if(!LabelBounds || !LabelFrame || !CaptionText) return bPrepared=false;
    const auto Renderer=FSlateApplication::Get().GetRenderer();if(!Renderer) return bPrepared=false;
    const float ActualScale=FMath::Max(.001f,GetTickSpaceGeometry().GetAccumulatedLayoutTransform().GetScale());
    const FString ActualCopy=SourceCaption.ToString();
    const FSlateFontInfo ActualFont=Theme->MakeFont(FMath::Clamp(CaptionFontSize,8,48),true);
    const FString Culture=IsDesignTime()?PreviewCulture:FInternationalization::Get().GetCurrentLanguage()->GetName();
    UDataTable* ResidentCopy=GetResolvedTranslationTable();
    if(!PreparationKeys.IsEmpty() && !IsValid(ResidentCopy)) return bPrepared=false;
    FString KeyCopy(TEXT("0123456789 %.,/+-{}\n"));
    for(int32 Digit=0;Digit<10;++Digit) {KeyCopy+=UMCUIDataLibrary::NumberForCulture(Digit,Culture).ToString();KeyCopy+=TEXT(" ");}
    for(const FName Key:PreparationKeys) {
        if(Key.IsNone()) continue;
        // ResolveText never loads a table; an unresolved configured key is not a completed prewarm.
        const FText Resolved=UMCUIDataLibrary::ResolveText(ResidentCopy,Key,FText::GetEmpty(),Culture);
        if(Resolved.IsEmpty()) return bPrepared=false;
        KeyCopy+=TEXT("\n");KeyCopy+=Resolved.ToString();
    }
    const FMargin Insets=LabelFrame->ContentPadding;
    if(bPrepared && PreparedCaption==ActualCopy && PreparedFont==ActualFont && PreparedInsets==Insets
        && PreparedMinimumWidth==MinimumWidth && PreparedMaximumWidth==MaximumWidth
        && PreparedKeys==PreparationKeys && PreparedCopyTable.Get()==ResidentCopy && PreparedCulture==Culture && PreparedKeyCopy==KeyCopy
        && FMath::IsNearlyEqual(PreparedScale,ActualScale,.001f)) return true;
    if(!ActualFont.HasValidFont()) return bPrepared=false;
    CaptionText->SetFont(ActualFont);RefreshCaption();
    const auto Measure=Renderer->GetFontMeasureService();
    PreparedLineHeight=float(Measure->GetMaxCharacterHeight(ActualFont,ActualScale))/ActualScale;
    if(!FMath::IsFinite(PreparedLineHeight) || PreparedLineHeight<=0.f) return bPrepared=false;
    // Warm every configured future label and numeric glyph at the same fixed font used by the actual caption.
    // Keys are reflected authored UI data, not a private list of mechanics; no owner/profile is required.
    Measure->Measure(FText::AsCultureInvariant(KeyCopy),ActualFont,ActualScale);
    // Ask the actual shaped Slate text layout for its unwrapped width. Raw
    // character advances can undercount shaped glyphs at fractional DPI and
    // make even READY break before its last letter.
    const bool bAuthoredAutoWrap=CaptionText->GetAutoWrapText();
    CaptionText->SetAutoWrapText(false);CaptionText->SetWrapTextAt(0.f);
    const auto CaptionSlate=CaptionText->TakeWidget();CaptionSlate->MarkPrepassAsDirty();CaptionSlate->SlatePrepass(ActualScale);
    const FVector2D NaturalSize=CaptionSlate->GetDesiredSize();
    CaptionText->SetAutoWrapText(bAuthoredAutoWrap);
    const float MinWidth=FMath::Max(32.f,MinimumWidth);
    const float MaxWidth=FMath::Max(MinWidth,MaximumWidth);
    const float FrameWidth=FMath::Clamp(FMath::CeilToFloat(float(NaturalSize.X))+Insets.Left+Insets.Right+1.f,MinWidth,MaxWidth);
    LabelBounds->SetWidthOverride(FrameWidth);
    CaptionText->SetWrapTextAt(FMath::Max(16.f,FrameWidth-Insets.Left-Insets.Right));
    ForceLayoutPrepass();
    PreparedCaption=ActualCopy;PreparedFont=ActualFont;PreparedScale=ActualScale;
    PreparedInsets=Insets;PreparedMinimumWidth=MinimumWidth;PreparedMaximumWidth=MaximumWidth;
    PreparedKeys=PreparationKeys;PreparedCopyTable=ResidentCopy;PreparedCulture=Culture;PreparedKeyCopy=MoveTemp(KeyCopy);
    return bPrepared=true;
}

bool UMCWorldLabelWidget::HasPreparedPresentation() const
{
    return bPrepared && LabelBounds && LabelFrame && CaptionText;
}

void UMCWorldLabelWidget::OnLocalizationChanged()
{
    bPrepared=false;RefreshCaption();
    // The retained hidden preparation view also observes culture/table edits.
    // Warm future source labels immediately, before their next visible use.
    PreparePresentation();
}

void UMCWorldLabelWidget::ApplyEditorPreview(float Time)
{
    Super::ApplyEditorPreview(Time);
    if(!IsDesignTime()) return;
    FText Fixture=PreviewCaption;FLinearColor FixtureTint=PreviewTint;
    if(Fixture.IsEmpty()) {
        auto ResolveFixture=[this](FName Key,const FText& Fallback) {
            return UMCUIDataLibrary::ResolveText(GetResolvedTranslationTable(),Key,Fallback,PreviewCulture);
        };
        auto FormatFixture=[&](FName Key,const FText& Fallback,const FFormatNamedArguments& Arguments) {
            return UMCUIDataLibrary::FormatTextForCulture(ResolveFixture(Key,Fallback),Arguments,PreviewCulture);
        };
        if(PreviewState==TEXT("Goal")) Fixture=ResolveFixture(TEXT("UI.Tutorial.Copy.YourTarget"),LOCTEXT("Goal","▼ ТВОЯ ЦЕЛЬ"));
        else if(PreviewState==TEXT("Danger")) {Fixture=ResolveFixture(TEXT("UI.World.GrenadeArmedLabel"),LOCTEXT("Danger","ГРАНАТА"));FixtureTint=FLinearColor(1.f,.2f,.2f);}
        else if(PreviewState==TEXT("Numeric")) Fixture=FormatFixture(TEXT("UI.World.HealthValue"),LOCTEXT("HealthValue","{Health} / {Maximum} HP"),
            {{TEXT("Health"),125},{TEXT("Maximum"),300}});
        else if(PreviewState==TEXT("Care")) Fixture=FormatFixture(TEXT("UI.World.ArenaToothCare"),LOCTEXT("ArenaCare","{Id} | HP {Health}\nBRUSH {CoffeeLeft}/{CoffeeTotal} | REPAIR {RepairLeft}"),
            {{TEXT("Id"),FText::AsCultureInvariant(TEXT("03"))},{TEXT("Health"),100},{TEXT("CoffeeLeft"),2},{TEXT("CoffeeTotal"),4},{TEXT("RepairLeft"),1}});
        else if(PreviewState==TEXT("LongText")) Fixture=ResolveFixture(TEXT("UI.World.LongCaptionFixture"),LOCTEXT("LongText","Ein sehr langes lokalisiertes Ziel und eine zusätzliche mehrzeilige Beschreibung"));
        else Fixture=FormatFixture(TEXT("UI.World.ReadyCount"),LOCTEXT("ReadyCount","ГОТОВНОСТЬ\n{0}/{1}"),{{TEXT("0"),1},{TEXT("1"),4}});
    }
    SetPresentation(Fixture,FixtureTint);PreparePresentation();
}

#undef LOCTEXT_NAMESPACE
