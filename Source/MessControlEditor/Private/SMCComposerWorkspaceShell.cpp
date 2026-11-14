#include "SMCComposerWorkspaceShell.h"

#include "MCComposerStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ConfigCacheIni.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MCComposerWorkspaceShell"

using namespace MCComposerStyle;

void SMCComposerWorkspaceShell::Construct(const FArguments& Args)
{
    CatalogueWidget=Args._Catalogue.Widget;
    PreviewWidget=Args._Preview.Widget;
    GraphWidget=Args._Graph.Widget;
    InspectorWidget=Args._Inspector.Widget;
    LibraryWidget=Args._Library.Widget;
    SettingsSection=TEXT("MessControl.ComposerUI.")+Args._SettingsKey.ToString();
    int32 SavedDensity=0, SavedLibrary=0, SavedCenter=2;
    GConfig->GetInt(*SettingsSection,TEXT("Density"),SavedDensity,GEditorPerProjectIni);
    GConfig->GetInt(*SettingsSection,TEXT("Library"),SavedLibrary,GEditorPerProjectIni);
    GConfig->GetInt(*SettingsSection,TEXT("Center"),SavedCenter,GEditorPerProjectIni);
    GConfig->GetFloat(*SettingsSection,TEXT("DockFraction"),DockFraction,GEditorPerProjectIni);
    Density=static_cast<EDensity>(FMath::Clamp(SavedDensity,0,2));
    LibraryMode=static_cast<ELibrary>(FMath::Clamp(SavedLibrary,0,2));
    Center=static_cast<ECenter>(FMath::Clamp(SavedCenter,0,2));
    DockFraction=FMath::Clamp(DockFraction,.20f,.55f);
    bCompact=Density==EDensity::Compact;
    SetCanTick(true);
    ChildSlot[SNew(SBorder).Padding(0).BorderImage(&PanelBrush()).BorderBackgroundColor(AppFill)
        [SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[Args._Header.Widget]
            +SVerticalBox::Slot().AutoHeight().Padding(8,0,8,6)
                [SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth()[SNew(SComboButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(9,5))
                        .OnGetMenuContent(this,&SMCComposerWorkspaceShell::DensityMenu)
                        .ToolTipText(LOCTEXT("DensityHint","Авто выбирает компактный вид для небольшого окна. Переключение меняет только раскладку, сохраняя черновик и превью."))
                        .ButtonContent()[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(GetDensityName());}).ColorAndOpacity(TextColor)]]
                    +SHorizontalBox::Slot().AutoWidth().Padding(12,0,3,0)[CenterButton(LOCTEXT("Preview","Превью"),ECenter::Preview)]
                    +SHorizontalBox::Slot().AutoWidth().Padding(0,0,3,0)[CenterButton(LOCTEXT("Graph","Схема"),ECenter::Graph)]
                    +SHorizontalBox::Slot().AutoWidth()[CenterButton(LOCTEXT("Both","Рядом"),ECenter::Both)]
                    +SHorizontalBox::Slot().FillWidth(1)
                    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8,0)[Label(TEXT("БИБЛИОТЕКА"),10,Muted)]
                    +SHorizontalBox::Slot().AutoWidth().Padding(0,0,3,0)[LibraryButton(LOCTEXT("Dock","Снизу"),ELibrary::Dock)]
                    +SHorizontalBox::Slot().AutoWidth().Padding(0,0,3,0)[LibraryButton(LOCTEXT("Expand","Развернуть"),ELibrary::Expanded)]
                    +SHorizontalBox::Slot().AutoWidth()[LibraryButton(LOCTEXT("Hide","Скрыть"),ELibrary::Hidden)]]
            +SVerticalBox::Slot().FillHeight(1).Padding(8,0)[SAssignNew(BodyHost,SBox)]
            +SVerticalBox::Slot().AutoHeight().Padding(8,5)[Args._Transport.Widget]
            +SVerticalBox::Slot().AutoHeight()[Args._Footer.Widget]]];
    RebuildBody();
}

void SMCComposerWorkspaceShell::Tick(const FGeometry& Geometry,double CurrentTime,float DeltaTime)
{
    SCompoundWidget::Tick(Geometry,CurrentTime,DeltaTime);
    const FVector2D Size=Geometry.GetLocalSize();
    if(Size.X<=0 || Size.Y<=0)return;
    // Hysteresis prevents switching repeatedly at the edge while resizing a splitter/window.
    const bool bNext=Density==EDensity::Compact || (Density==EDensity::Auto &&
        (bCompact ? Size.X<1640 || Size.Y<860 : Size.X<1580 || Size.Y<800));
    if(bNext!=bCompact){bCompact=bNext;RebuildBody();}
}

void SMCComposerWorkspaceShell::RebuildBody()
{
    // Detach all old parents before reusing slot widgets; never recreate their sessions or controls.
    BodyHost->SetContent(SNullWidget::NullWidget);
    VerticalSplitter.Reset();
    MainSplitter.Reset();
    CenterSplitter.Reset();
    const ECenter EffectiveCenter=bCompact&&Center==ECenter::Both?ECenter::Preview:Center;
    TSharedRef<SWidget> CenterWidget=PreviewWidget.ToSharedRef();
    if(EffectiveCenter==ECenter::Graph)CenterWidget=GraphWidget.ToSharedRef();
    else if(EffectiveCenter==ECenter::Both)
        CenterWidget=SAssignNew(CenterSplitter,SSplitter)
            .OnSplitterFinishedResizing_Lambda([this]{if(CenterSplitter)CenterWidths=FVector2D(CenterSplitter->SlotAt(0).GetSizeValue(),CenterSplitter->SlotAt(1).GetSizeValue());})
            +SSplitter::Slot().Value(CenterWidths.X).MinSize(300)[PreviewWidget.ToSharedRef()]
            +SSplitter::Slot().Value(CenterWidths.Y).MinSize(320)[GraphWidget.ToSharedRef()];
    const FVector Widths=bCompact?CompactWidths:FullWidths;
    const auto Main=SAssignNew(MainSplitter,SSplitter)
        .OnSplitterFinishedResizing_Lambda([this]{if(MainSplitter){
            const FVector Changed(MainSplitter->SlotAt(0).GetSizeValue(),MainSplitter->SlotAt(1).GetSizeValue(),MainSplitter->SlotAt(2).GetSizeValue());
            if(bCompact)CompactWidths=Changed;else FullWidths=Changed;}})
        +SSplitter::Slot().Value(Widths.X).MinSize(190)[CatalogueWidget.ToSharedRef()]
        +SSplitter::Slot().Value(Widths.Y).MinSize(300)[CenterWidget]
        +SSplitter::Slot().Value(Widths.Z).MinSize(280)[InspectorWidget.ToSharedRef()];
    if(LibraryMode==ELibrary::Hidden){BodyHost->SetContent(Main);return;}
    const float Fraction=LibraryMode==ELibrary::Expanded?.60f:DockFraction;
    BodyHost->SetContent(SAssignNew(VerticalSplitter,SSplitter).Orientation(Orient_Vertical)
        .OnSplitterFinishedResizing_Lambda([this]{
            if(LibraryMode==ELibrary::Dock&&VerticalSplitter){
                DockFraction=FMath::Clamp(VerticalSplitter->SlotAt(1).GetSizeValue(),.20f,.55f);SaveSettings();}})
        +SSplitter::Slot().Value(1-Fraction).MinSize(220)[Main]
        +SSplitter::Slot().Value(Fraction).MinSize(210)
            [SNew(SBorder).Padding(1).BorderImage(&PanelBrush()).BorderBackgroundColor(PanelFill)[LibraryWidget.ToSharedRef()]]);
}

TSharedRef<SWidget> SMCComposerWorkspaceShell::CenterButton(const FText& Text,ECenter Value)
{
    return SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(9,5))
        .Text(Text).IsEnabled_Lambda([this,Value]{return Value!=ECenter::Both||!bCompact;})
        .ForegroundColor_Lambda([this,Value]{const ECenter Effective=bCompact&&Center==ECenter::Both?ECenter::Preview:Center;return FSlateColor(Effective==Value?WorkspaceMint:TextColor);})
        .ToolTipText(Value==ECenter::Both?LOCTEXT("BothHint","Показать превью и схему рядом. Доступно в полном режиме."):LOCTEXT("CenterHint","Сфокусировать центральную область. Данные, выбранное событие и воспроизведение сохраняются."))
        .OnClicked_Lambda([this,Value]{SetCenter(Value);return FReply::Handled();});
}

TSharedRef<SWidget> SMCComposerWorkspaceShell::LibraryButton(const FText& Text,ELibrary Value)
{
    return SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(9,5)).Text(Text)
        .ForegroundColor_Lambda([this,Value]{return FSlateColor(LibraryMode==Value?WorkspaceMint:TextColor);})
        .OnClicked_Lambda([this,Value]{SetLibraryMode(Value);return FReply::Handled();});
}

TSharedRef<SWidget> SMCComposerWorkspaceShell::DensityMenu()
{
    const auto Menu=SNew(SVerticalBox);
    for(const EDensity Value:{EDensity::Auto,EDensity::Full,EDensity::Compact})
        Menu->AddSlot().AutoHeight()[Command(Value==EDensity::Auto?TEXT("Авто"):Value==EDensity::Full?TEXT("Полный"):TEXT("Компактный"),[this,Value]{SetDensity(Value);FSlateApplication::Get().DismissAllMenus();})];
    return Menu;
}

void SMCComposerWorkspaceShell::SetDensity(EDensity Value)
{
    Density=Value;
    const FVector2D Size=GetCachedGeometry().GetLocalSize();
    bCompact=Value==EDensity::Compact||(Value==EDensity::Auto&&(Size.X<1580||Size.Y<800));
    RebuildBody();SaveSettings();
}
void SMCComposerWorkspaceShell::SetCenter(ECenter Value){Center=Value;RebuildBody();SaveSettings();}
void SMCComposerWorkspaceShell::SetLibraryMode(ELibrary Value){LibraryMode=Value;RebuildBody();SaveSettings();}
void SMCComposerWorkspaceShell::SaveSettings() const
{
    GConfig->SetInt(*SettingsSection,TEXT("Density"),static_cast<int32>(Density),GEditorPerProjectIni);
    GConfig->SetInt(*SettingsSection,TEXT("Library"),static_cast<int32>(LibraryMode),GEditorPerProjectIni);
    GConfig->SetInt(*SettingsSection,TEXT("Center"),static_cast<int32>(Center),GEditorPerProjectIni);
    GConfig->SetFloat(*SettingsSection,TEXT("DockFraction"),DockFraction,GEditorPerProjectIni);
}
FString SMCComposerWorkspaceShell::GetDensityName() const
{
    if(Density==EDensity::Auto)return bCompact?TEXT("Авто · компактный"):TEXT("Авто · полный");
    return bCompact?TEXT("Компактный"):TEXT("Полный");
}
FString SMCComposerWorkspaceShell::GetLibraryModeName() const
{
    return LibraryMode==ELibrary::Hidden?TEXT("hidden"):LibraryMode==ELibrary::Expanded?TEXT("expanded"):TEXT("dock");
}

bool SMCComposerWorkspaceShell::IsGraphOnly() const { return Center==ECenter::Graph; }

#undef LOCTEXT_NAMESPACE
