#include "MCGameplayUIWidget.h"

#include "MCUIDataLibrary.h"
#include "MCGameplayUIFactory.h"
#include "MCUIVisualStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/DataTable.h"
#include "Engine/Texture.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "MCGameplayUI"

UMCGameplayUITheme* UMCGameplayUIWidget::GetResolvedTheme() const
{
    if (IsValid(Theme)) return Theme;
    // A parent override remains inherited; it must not be copied into a child's independent override.
    if (const auto* Parent=GetTypedOuter<UMCGameplayUIWidget>()) return Parent->GetResolvedTheme();
    return UMCGameplayUIFactory::GetMasterTheme();
}

FText UMCGameplayUIWidget::Localized(FName Key,const FText& Fallback) const
{
    const auto* Resolved=GetResolvedTheme();
    UDataTable* Table=Resolved?Resolved->LocalizationTable.LoadSynchronous():nullptr;
    if (!Table) Table=UMCUIDataLibrary::GetDefaultLocalizationTable();
    return UMCUIDataLibrary::ResolveText(Table,Key,Fallback,IsDesignTime()?PreviewCulture:FString());
}

void UMCGameplayUIWidget::BuildPresentationTree()
{
    if (WidgetTree && !WidgetTree->RootWidget) WidgetTree->RootWidget=WidgetTree->ConstructWidget<UCanvasPanel>();
}

TSharedRef<SWidget> UMCGameplayUIWidget::RebuildWidget()
{
    Initialize();
    BuildPresentationTree();
    ObserveTheme();
    return Super::RebuildWidget();
}

void UMCGameplayUIWidget::NativePreConstruct()
{
    BuildPresentationTree();
    ObserveTheme();
    Super::NativePreConstruct();
    // The parent owns fixtures for a composed screen. A late child PreConstruct
    // must not replace the snapshot the parent has already supplied.
    if (IsDesignTime() && !GetTypedOuter<UMCGameplayUIWidget>()) ApplyEditorPreview(PreviewTime);
    else ApplyTheme();
}

void UMCGameplayUIWidget::NativeConstruct()
{
    Super::NativeConstruct();
    ObserveTheme();
    ApplyTheme();
}

void UMCGameplayUIWidget::NativeDestruct()
{
    if (auto* Previous=ObservedTheme.Get()) Previous->OnThemeChanged.Remove(ThemeHandle);
    ObservedTheme.Reset();
    ThemeHandle.Reset();
    Super::NativeDestruct();
}

void UMCGameplayUIWidget::BeginDestroy()
{
    if (auto* Previous=ObservedTheme.Get()) Previous->OnThemeChanged.Remove(ThemeHandle);
    ObservedTheme.Reset();
    ThemeHandle.Reset();
    Super::BeginDestroy();
}

void UMCGameplayUIWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    BrushMetricsElapsed+=FMath::Max(0.f,DeltaSeconds);
    if(BrushMetricsElapsed>=.1f)
    {
        BrushMetricsElapsed=0.f;
        RefreshPresentationGeometry();
    }
}

void UMCGameplayUIWidget::RefreshPresentationGeometry()
{
    MCUIVisualStyle::RefreshGeometry(WidgetTree);
}

void UMCGameplayUIWidget::ObserveTheme()
{
    auto* Resolved=GetResolvedTheme();
    if (ObservedTheme.Get()==Resolved && ThemeHandle.IsValid()) return;
    if (auto* Previous=ObservedTheme.Get()) Previous->OnThemeChanged.Remove(ThemeHandle);
    ObservedTheme=Resolved;
    ThemeHandle.Reset();
    if (Resolved)
        ThemeHandle=Resolved->OnThemeChanged.AddWeakLambda(this,[this]
        {
            ApplyTheme();
            InvalidateLayoutAndVolatility();
        });
}

void UMCGameplayUIWidget::BindStyle(FName Name,EMCUIColorToken Color,EMCUIStyleTarget Target,int32 Size,bool bBold)
{
    auto* Binding=StyleBindings.FindByPredicate([Name,Target](const FMCUIStyleBinding& Entry)
    {return Entry.WidgetName==Name && Entry.Target==Target;});
    if (!Binding) Binding=&StyleBindings.AddDefaulted_GetRef();
    Binding->WidgetName=Name;
    Binding->Color=Color;
    Binding->Target=Target;
    Binding->FontSize=Size;
    Binding->bBold=bBold;
}

void UMCGameplayUIWidget::ApplyTheme()
{
    if (!WidgetTree) return;
    ObserveTheme();
    const auto* Resolved=GetResolvedTheme();
    if (!Resolved) return;
    for (const auto& Binding:StyleBindings)
    {
        auto* Widget=WidgetTree->FindWidget(Binding.WidgetName);
        if (!Widget) continue;
        const FLinearColor Color=Resolved->GetColor(Binding.Color);
        switch(Binding.Target)
        {
        case EMCUIStyleTarget::Background:
            if (auto* Border=Cast<UBorder>(Widget))
            {
                if(Binding.Color==EMCUIColorToken::Backdrop) {Border->SetBrush(FSlateRoundedBoxBrush(Color,0.f));Border->SetBrushColor(FLinearColor::White);}
                else MCUIVisualStyle::Panel(Border,Resolved,Color);
            }
            break;
        case EMCUIStyleTarget::Text:
            if (auto* Text=Cast<UTextBlock>(Widget))
            {
                Text->SetColorAndOpacity(FSlateColor(Color));
                const int32 Size=Binding.FontSize>0?Binding.FontSize:FMath::RoundToInt(Text->GetFont().Size);
                Text->SetFont(Resolved->MakeFont(Size,Binding.bBold));
            }
            break;
        case EMCUIStyleTarget::Image:
            if (auto* Image=Cast<UImage>(Widget))
            {
                if(Cast<UTexture>(Image->GetBrush().GetResourceObject())) Image->SetColorAndOpacity(Color);
                else MCUIVisualStyle::Image(Image,Resolved,Color);
            }
            break;
        case EMCUIStyleTarget::Progress:
            if (auto* Progress=Cast<UProgressBar>(Widget))
            {
                MCUIVisualStyle::Progress(Progress,Resolved,Color);
            }
            break;
        case EMCUIStyleTarget::Button:
            if (auto* Button=Cast<UButton>(Widget))
            {
                MCUIVisualStyle::Button(Button,Resolved,Color);
            }
            break;
        }
    }
    WidgetTree->ForEachWidget([](UWidget* Widget)
    {
        if (auto* Child=Cast<UMCGameplayUIWidget>(Widget)) Child->ApplyTheme();
    });
}

void UMCGameplayUIWidget::ApplyEditorPreview(float Time)
{
    PreviewTime=FMath::IsFinite(Time)?FMath::Max(0.f,Time):0.f;
    ApplyTheme();
}

void UMCGameplayUIWidget::ResetEditorPreview()
{
    PreviewTime=0.f;
    ApplyEditorPreview(0.f);
}

void UMCUIThemePreviewWidget::BuildPresentationTree()
{
    if (!WidgetTree || WidgetTree->RootWidget) return;
    auto* Scale=WidgetTree->ConstructWidget<UScaleBox>();
    Scale->SetStretch(EStretch::ScaleToFit);
    Scale->SetStretchDirection(EStretchDirection::DownOnly);
    WidgetTree->RootWidget=Scale;
    auto* Size=WidgetTree->ConstructWidget<USizeBox>();
    Size->SetWidthOverride(720.f);Size->SetHeightOverride(590.f);Scale->SetContent(Size);
    auto* Frame=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("ThemeFrame"));
    Frame->SetPadding(FMargin(20));Size->SetContent(Frame);BindStyle(TEXT("ThemeFrame"),EMCUIColorToken::Marine,EMCUIStyleTarget::Background);
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();Frame->SetContent(Column);

    auto AddText=[&](UVerticalBox* Parent,FName Name,FName Key,const FText& Fallback,EMCUIColorToken Token,int32 FontSize,bool bBold=false)
    {
        auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
        Text->SetText(Localized(Key,Fallback));Text->SetAutoWrapText(true);
        Parent->AddChildToVerticalBox(Text)->SetPadding(FMargin(0,0,0,9));
        BindStyle(Name,Token,EMCUIStyleTarget::Text,FontSize,bBold);
        return Text;
    };
    AddText(Column,TEXT("ThemeHeading"),TEXT("UI.Theme.Heading"),LOCTEXT("ThemeHeading","MESS CONTROL · ИГРОВОЙ ИНТЕРФЕЙС"),EMCUIColorToken::OnMarine,25,true);
    AddText(Column,TEXT("ThemeCaption"),TEXT("UI.Theme.Caption"),LOCTEXT("ThemeCaption","Общая палитра · текст и значения редактируются в UMG"),EMCUIColorToken::OnMarine,15);

    auto* Body=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("ThemeSurface"));
    Body->SetPadding(FMargin(18));Column->AddChildToVerticalBox(Body)->SetPadding(FMargin(0,2,0,12));
    BindStyle(TEXT("ThemeSurface"),EMCUIColorToken::Surface,EMCUIStyleTarget::Background);
    auto* BodyColumn=WidgetTree->ConstructWidget<UVerticalBox>();Body->SetContent(BodyColumn);
    AddText(BodyColumn,TEXT("ThemeBodyTitle"),TEXT("UI.Theme.CardTitle"),LOCTEXT("CardTitle","Готов к новому событию"),EMCUIColorToken::Ink,22,true);
    AddText(BodyColumn,TEXT("ThemeBody"),TEXT("UI.Theme.Body"),LOCTEXT("Body","Чёткие подсказки, спокойные панели и заметные изменения состояния."),EMCUIColorToken::Ink,16);
    AddText(BodyColumn,TEXT("ThemeBenefit"),TEXT("UI.Theme.Benefit"),LOCTEXT("Benefit","Польза: +20% к запасу сил"),EMCUIColorToken::Benefit,17,true);
    AddText(BodyColumn,TEXT("ThemeCost"),TEXT("UI.Theme.Cost"),LOCTEXT("Cost","Цена: −10% к скорости"),EMCUIColorToken::Cost,17,true);

    auto* LCD=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("ThemeLCD"));
    LCD->SetPadding(FMargin(14,10));BodyColumn->AddChildToVerticalBox(LCD)->SetPadding(FMargin(0,4,0,8));
    BindStyle(TEXT("ThemeLCD"),EMCUIColorToken::LCDBackground,EMCUIStyleTarget::Background);
    auto* LCDColumn=WidgetTree->ConstructWidget<UVerticalBox>();LCD->SetContent(LCDColumn);
    AddText(LCDColumn,TEXT("ThemeLCDTitle"),TEXT("UI.Theme.Timer"),LOCTEXT("Timer","ДО СЛЕДУЮЩЕГО СОБЫТИЯ"),EMCUIColorToken::LCDInk,13,true);
    AddText(LCDColumn,TEXT("ThemeTime"),NAME_None,FText::AsCultureInvariant(TEXT("00:28")),EMCUIColorToken::LCDInk,30,true);

    auto* Buttons=WidgetTree->ConstructWidget<UHorizontalBox>();Column->AddChildToVerticalBox(Buttons)->SetPadding(FMargin(0,0,0,12));
    const FName ButtonNames[]={TEXT("ThemePrimary"),TEXT("ThemeSecondary"),TEXT("ThemeDisabled")};
    const FName ButtonTextNames[]={TEXT("ThemePrimaryText"),TEXT("ThemeSecondaryText"),TEXT("ThemeDisabledText")};
    const FName ButtonKeys[]={TEXT("UI.Theme.Primary"),TEXT("UI.Theme.Secondary"),TEXT("UI.Theme.Disabled")};
    const FText ButtonCopy[]={LOCTEXT("Primary","ПРОДОЛЖИТЬ"),LOCTEXT("Secondary","НАЗАД"),LOCTEXT("Disabled","НЕДОСТУПНО")};
    for (int32 Index=0;Index<3;++Index)
    {
        auto* Button=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),ButtonNames[Index]);
        auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),ButtonTextNames[Index]);
        Label->SetText(Localized(ButtonKeys[Index],ButtonCopy[Index]));Label->SetJustification(ETextJustify::Center);Button->SetContent(Label);
        auto* ContentSlot=Cast<UButtonSlot>(Button->GetContentSlot());
        if (ContentSlot) ContentSlot->SetPadding(FMargin(10,12));
        auto* ButtonSlot=Buttons->AddChildToHorizontalBox(Button);ButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));ButtonSlot->SetPadding(FMargin(3,0));
        Button->SetIsEnabled(Index!=2);
        BindStyle(ButtonNames[Index],Index==0?EMCUIColorToken::Mint:EMCUIColorToken::Button,EMCUIStyleTarget::Button);
        BindStyle(ButtonTextNames[Index],EMCUIColorToken::ButtonText,EMCUIStyleTarget::Text,14,true);
    }

    struct FSample {FName Name;FName Key;FText Text;EMCUIColorToken Color;float Value;};
    const FSample Samples[]={
        {TEXT("ThemeHealth"),TEXT("UI.Theme.Health"),LOCTEXT("Health","ЗДОРОВЬЕ БОССА"),EMCUIColorToken::BossHealth,.66f},
        {TEXT("ThemeShield"),TEXT("UI.Theme.Shield"),LOCTEXT("Shield","ЩИТ"),EMCUIColorToken::Shield,.4f},
        {TEXT("ThemeStamina"),TEXT("UI.Theme.Stamina"),LOCTEXT("Stamina","ВЫНОСЛИВОСТЬ"),EMCUIColorToken::Stamina,.8f}
    };
    for (const auto& Sample:Samples)
    {
        const FName LabelName(*FString::Printf(TEXT("%sLabel"),*Sample.Name.ToString()));
        AddText(Column,LabelName,Sample.Key,Sample.Text,EMCUIColorToken::OnMarine,12,true);
        auto* Height=WidgetTree->ConstructWidget<USizeBox>();Height->SetHeightOverride(10.f);
        auto* Bar=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),Sample.Name);Bar->SetPercent(Sample.Value);Height->SetContent(Bar);
        Column->AddChildToVerticalBox(Height)->SetPadding(FMargin(0,0,0,11));BindStyle(Sample.Name,Sample.Color,EMCUIStyleTarget::Progress);
    }
    ApplyTheme();
}

void UMCUIThemePreviewWidget::ApplyEditorPreview(float Time)
{
    PreviewTime=FMath::IsFinite(Time)?FMath::Max(0.f,Time):0.f;
    BuildPresentationTree();
    ApplyTheme();
    if (!WidgetTree) return;
    struct FSampleCopy {FName Widget;FName Key;FText Fallback;};
    const FSampleCopy Copy[]={
        {TEXT("ThemeHeading"),TEXT("UI.Theme.Heading"),LOCTEXT("ThemeHeading","MESS CONTROL · ИГРОВОЙ ИНТЕРФЕЙС")},
        {TEXT("ThemeCaption"),TEXT("UI.Theme.Caption"),LOCTEXT("ThemeCaption","Общая палитра · текст и значения редактируются в UMG")},
        {TEXT("ThemeBodyTitle"),TEXT("UI.Theme.CardTitle"),LOCTEXT("CardTitle","Готов к новому событию")},
        {TEXT("ThemeBody"),TEXT("UI.Theme.Body"),LOCTEXT("Body","Чёткие подсказки, спокойные панели и заметные изменения состояния.")},
        {TEXT("ThemeBenefit"),TEXT("UI.Theme.Benefit"),LOCTEXT("Benefit","Польза: +20% к запасу сил")},
        {TEXT("ThemeCost"),TEXT("UI.Theme.Cost"),LOCTEXT("Cost","Цена: −10% к скорости")},
        {TEXT("ThemeLCDTitle"),TEXT("UI.Theme.Timer"),LOCTEXT("Timer","ДО СЛЕДУЮЩЕГО СОБЫТИЯ")},
        {TEXT("ThemePrimaryText"),TEXT("UI.Theme.Primary"),LOCTEXT("Primary","ПРОДОЛЖИТЬ")},
        {TEXT("ThemeSecondaryText"),TEXT("UI.Theme.Secondary"),LOCTEXT("Secondary","НАЗАД")},
        {TEXT("ThemeDisabledText"),TEXT("UI.Theme.Disabled"),LOCTEXT("Disabled","НЕДОСТУПНО")},
        {TEXT("ThemeHealthLabel"),TEXT("UI.Theme.Health"),LOCTEXT("Health","ЗДОРОВЬЕ БОССА")},
        {TEXT("ThemeShieldLabel"),TEXT("UI.Theme.Shield"),LOCTEXT("Shield","ЩИТ")},
        {TEXT("ThemeStaminaLabel"),TEXT("UI.Theme.Stamina"),LOCTEXT("Stamina","ВЫНОСЛИВОСТЬ")}
    };
    for (const auto& Entry:Copy)
        if (auto* Text=Cast<UTextBlock>(WidgetTree->FindWidget(Entry.Widget))) Text->SetText(Localized(Entry.Key,Entry.Fallback));
    if (auto* Button=Cast<UButton>(WidgetTree->FindWidget(TEXT("ThemePrimary"))))
        Button->SetIsEnabled(PreviewScenario!=EMCGameplayUIPreviewScenario::Disabled);
    const bool Low=PreviewScenario==EMCGameplayUIPreviewScenario::LowResources;
    if (auto* Bar=Cast<UProgressBar>(WidgetTree->FindWidget(TEXT("ThemeStamina"))))
        Bar->SetPercent(Low?.14f:Time>0.f?.5f+.35f*FMath::Sin(Time*1.5f):.8f);
}

#undef LOCTEXT_NAMESPACE
