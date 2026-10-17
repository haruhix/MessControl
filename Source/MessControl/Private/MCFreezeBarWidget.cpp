#include "MCFreezeBarWidget.h"
#include "MCUIDataLibrary.h"
#include "MCUIVisualStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"

#define LOCTEXT_NAMESPACE "MCFreezeBar"

void UMCFreezeBarWidget::BuildPresentationTree()
{
    if (!WidgetTree) return;
    SetIsFocusable(false); SetVisibility(ESlateVisibility::HitTestInvisible);
    if (WidgetTree->RootWidget) {
        FreezeBackground=Cast<UBorder>(WidgetTree->FindWidget(TEXT("FreezeBackground")));
        FreezeProgress=Cast<UProgressBar>(WidgetTree->FindWidget(TEXT("FreezeProgress")));
        FreezeCaption=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("FreezeCaption"))); return;
    }
    auto* Root=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("FreezeRoot"));
    Root->SetWidthOverride(240); Root->SetMinDesiredHeight(64); WidgetTree->RootWidget=Root;
    FreezeBackground=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("FreezeBackground"));
    FreezeBackground->SetPadding(FMargin(11,7)); Root->SetContent(FreezeBackground);
    auto* Box=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("FreezeContent")); FreezeBackground->SetContent(Box);
    FreezeCaption=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("FreezeCaption"));
    FreezeCaption->SetJustification(ETextJustify::Center); FreezeCaption->SetAutoWrapText(true);
    FreezeCaption->SetWrapTextAt(216);FreezeCaption->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);FreezeCaption->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
    Box->AddChildToVerticalBox(FreezeCaption)->SetPadding(FMargin(0,0,0,5));
    auto* Size=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("FreezeBarSize"));
    Size->SetHeightOverride(12); Box->AddChildToVerticalBox(Size);
    FreezeProgress=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),TEXT("FreezeProgress"));
    FreezeProgress->SetBarFillType(EProgressBarFillType::LeftToRight); FreezeProgress->SetBarFillStyle(EProgressBarFillStyle::Scale);
    Size->SetContent(FreezeProgress); ApplyFreeze();
}

void UMCFreezeBarWidget::SetFreeze(float Amount,bool bSafe)
{
    FreezeAmount=FMath::IsFinite(Amount)?FMath::Clamp(Amount,0.f,1.f):0.f;
    bInSafeZone=bSafe; ApplyFreeze();
}

void UMCFreezeBarWidget::ApplyFreeze()
{
    const FString Culture=bEditorFixture || IsDesignTime()?PreviewCulture:FString();
    const auto* Style=GetResolvedTheme();
    UDataTable* Table=Style?Style->LocalizationTable.LoadSynchronous():nullptr;
    if (!Table) Table=UMCUIDataLibrary::GetDefaultLocalizationTable();
    if (FreezeCaption) {
        const FText Pattern=bInSafeZone
            ? UMCUIDataLibrary::ResolveText(Table,TEXT("UI.Status.FreezeWarmingValue"),LOCTEXT("Warming","Заморозка {Percent} · Отогрев"),Culture)
            : UMCUIDataLibrary::ResolveText(Table,TEXT("UI.Status.FreezeValue"),LOCTEXT("Frozen","Заморозка {Percent}"),Culture);
        auto Target=FInternationalization::Get().GetCulture(Culture.IsEmpty()?FInternationalization::Get().GetCurrentLanguage()->GetName():Culture);
        FNumberFormattingOptions Options=FNumberFormattingOptions::DefaultNoGrouping(); Options.SetMinimumFractionalDigits(0).SetMaximumFractionalDigits(0);
        FreezeCaption->SetText(UMCUIDataLibrary::FormatTextForCulture(Pattern,FFormatNamedArguments{{TEXT("Percent"),FText::AsPercent(FreezeAmount,&Options,Target)}},Culture));
    }
    if (FreezeProgress) FreezeProgress->SetPercent(FreezeAmount);
    ApplyTheme();
}

void UMCFreezeBarWidget::ApplyTheme()
{
    Super::ApplyTheme();
    if (const auto* Style=GetResolvedTheme()) {
        const auto& P=Style->Palette;
        const FLinearColor Fill=bInSafeZone?P.Accent:FreezeAmount<=.7f?FMath::Lerp(P.Shield,P.Warning,FreezeAmount/.7f):FMath::Lerp(P.Warning,P.Cost,(FreezeAmount-.7f)/.3f);
        MCUIVisualStyle::Panel(FreezeBackground,Style,P.Marine);
        MCUIVisualStyle::Progress(FreezeProgress,Style,Fill);
        if (FreezeCaption) {FreezeCaption->SetFont(Style->MakeFont(16));FreezeCaption->SetColorAndOpacity(FreezeAmount>=.8f && !bInSafeZone?P.Cost:P.OnMarine);FreezeCaption->SetShadowOffset(FVector2D::ZeroVector);}
    }
}

void UMCFreezeBarWidget::ApplyEditorPreview(float Time)
{
    BuildPresentationTree();bEditorFixture=true;
    const bool Danger=PreviewState==TEXT("Danger") || PreviewScenario==EMCGameplayUIPreviewScenario::LowResources;
    SetFreeze(PreviewState==TEXT("Empty") || PreviewScenario==EMCGameplayUIPreviewScenario::Empty?0.f:Danger?.95f:PreviewFreezeAmount,bPreviewSafe || PreviewState==TEXT("Warming"));
    SetRenderOpacity(Time<=0?1.f:FMath::Clamp(Time*6.f,0.f,1.f));
}
void UMCFreezeBarWidget::ResetEditorPreview() {bEditorFixture=false;SetRenderOpacity(1);}
#undef LOCTEXT_NAMESPACE
