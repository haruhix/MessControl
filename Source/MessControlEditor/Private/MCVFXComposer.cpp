#include "MCVFXComposer.h"
#include "MCVFXActionWorkspace.h"
#include "MCVFXRecipeComposer.h"
#include "MCVFXActionWorkspace.h"
#include "MCPerkActionPreviewPanel.h"

#include "MCVFXComposition.h"
#include "SMCVFXComposerViewport.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "DetailsViewArgs.h"
#include "Editor.h"
#include "EditorUndoClient.h"
#include "FileHelpers.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "IContentBrowserSingleton.h"
#include "IDetailsView.h"
#include "IStructureDetailsView.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "NiagaraSystem.h"
#include "PropertyEditorModule.h"
#include "Rendering/DrawElementTypes.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "MCVFXComposer"

namespace
{
    const FName ComposerTabName(TEXT("MCVFXComposer"));
    const FString CompositionFolder(TEXT("/Game/Tests/VFXPractice/Compositions"));
    struct FComposerLayerItem { int32 Index = INDEX_NONE; };

    /** A read-only timing overview. It does not claim to expose Niagara's internal emitter timeline. */
    class SMCVFXComposerTimeline final : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SMCVFXComposerTimeline) {}
            SLATE_ATTRIBUTE(UMCVFXComposition*, Composition)
            SLATE_ATTRIBUTE(float, Time)
            SLATE_ATTRIBUTE(int32, SelectedIndex)
        SLATE_END_ARGS()

        void Construct(const FArguments& Args)
        {
            Composition=Args._Composition;Time=Args._Time;SelectedIndex=Args._SelectedIndex;
            SetToolTipText(LOCTEXT("TimelineHint", "Полоски — время эмиссии слоёв. Серая область — хвост уже рождённых частиц. Белая линия — текущее время превью. Здесь нет редактирования внутренних emitter-модулей Niagara."));
        }
        void Refresh() { Invalidate(EInvalidateWidgetReason::Layout); }
        virtual FVector2D ComputeDesiredSize(float) const override
        {
            const UMCVFXComposition* Data=Composition.Get();
            return FVector2D(400,30.f+22.f*FMath::Max(1,Data?Data->Layers.Num():0));
        }
        virtual bool ComputeVolatility() const override { return true; }
        virtual int32 OnPaint(const FPaintArgs&,const FGeometry& Geometry,const FSlateRect&,
            FSlateWindowElementList& Elements,int32 LayerId,const FWidgetStyle&,bool) const override
        {
            const UMCVFXComposition* Data=Composition.Get();
            if(!Data)return LayerId;
            const FVector2f Size(Geometry.GetLocalSize());
            const float LabelWidth=FMath::Min(145.f,Size.X*.3f);
            const float TrackWidth=FMath::Max(1.f,Size.X-LabelWidth-12.f);
            const float Duration=FMath::IsFinite(Data->DurationSeconds)?FMath::Clamp(Data->DurationSeconds,.05f,30.f):3.f;
            const float Tail=FMath::IsFinite(Data->TailSeconds)?FMath::Clamp(Data->TailSeconds,0.f,10.f):0.f;
            const float Total=Duration+Tail;
            const FSlateBrush* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
            const FSlateFontInfo Font=FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9);
            auto Box=[&](float X,float Y,float Width,float Height,FLinearColor Color)
            {
                FSlateDrawElement::MakeBox(Elements,LayerId,Geometry.ToPaintGeometry(FVector2f(Width,Height),FSlateLayoutTransform(FVector2f(X,Y))),White,ESlateDrawEffect::None,Color);
            };
            auto Text=[&](float X,float Y,const FString& Value,FLinearColor Color)
            {
                FSlateDrawElement::MakeText(Elements,LayerId+1,Geometry.ToPaintGeometry(FVector2f(1,1),FSlateLayoutTransform(FVector2f(X,Y))),Value,Font,ESlateDrawEffect::None,Color);
            };
            Box(0,0,Size.X,Size.Y,FLinearColor(.025f,.03f,.04f,1));
            const float TailX=LabelWidth+TrackWidth*Duration/Total;
            Box(TailX,22.f,FMath::Max(0.f,Size.X-TailX),FMath::Max(0.f,Size.Y-22.f),FLinearColor(.08f,.085f,.10f,1));
            for(int32 Tick=0;Tick<=4;++Tick)
            {
                const float X=LabelWidth+TrackWidth*Tick*.25f;
                Box(X,20.f,1.f,FMath::Max(0.f,Size.Y-20.f),FLinearColor(.14f,.17f,.19f,1));
                Text(FMath::Min(X,Size.X-38.f),3.f,FString::Printf(TEXT("%.1f с"),Total*Tick*.25f),FLinearColor(.65f,.70f,.74f,1));
            }
            if(Data->Layers.IsEmpty())Text(8,28,TEXT("Добавьте Niagara-слой"),FLinearColor(.6f,.65f,.7f,1));
            for(int32 Index=0;Index<Data->Layers.Num();++Index)
            {
                const auto& Layer=Data->Layers[Index];
                const float Y=27.f+22.f*Index;
                if(Index==SelectedIndex.Get())Box(0,Y-2.f,Size.X,21.f,FLinearColor(.07f,.16f,.19f,1));
                FString Name=Layer.Name.ToString();if(Name.Len()>18)Name=Name.Left(17)+TEXT("…");
                Text(8,Y,FString::Printf(TEXT("%02d %s"),Index+1,*Name),Layer.bEnabled?FLinearColor(.84f,.89f,.91f,1):FLinearColor(.4f,.43f,.46f,1));
                const float Start=FMath::IsFinite(Layer.StartSeconds)?FMath::Clamp(Layer.StartSeconds,0.f,Duration):0.f;
                const float End=FMath::Clamp(FMath::IsFinite(Layer.EmissionSeconds)&&Layer.EmissionSeconds>0?Start+Layer.EmissionSeconds:Duration,Start,Duration);
                FLinearColor Tint=Layer.bEnabled?FLinearColor(.18f,.64f,.64f,1):FLinearColor(.22f,.26f,.28f,1);
                if(Layer.bSolo&&Layer.bEnabled)Tint=FLinearColor(.76f,.65f,.24f,1);
                Box(LabelWidth+TrackWidth*Start/Total,Y+2.f,FMath::Max(2.f,TrackWidth*(End-Start)/Total),12.f,Tint);
            }
            const float PlayX=LabelWidth+TrackWidth*FMath::Clamp(Time.Get()/Total,0.f,1.f);
            FSlateDrawElement::MakeBox(Elements,LayerId+2,Geometry.ToPaintGeometry(FVector2f(2.f,FMath::Max(0.f,Size.Y-20.f)),FSlateLayoutTransform(FVector2f(PlayX,20.f))),White,ESlateDrawEffect::None,FLinearColor(.95f,.98f,1,1));
            return LayerId+2;
        }
    private:
        TAttribute<UMCVFXComposition*> Composition;
        TAttribute<float> Time;
        TAttribute<int32> SelectedIndex;
    };

    class SMCVFXComposerPanel final : public SCompoundWidget, public FEditorUndoClient
    {
    public:
        SLATE_BEGIN_ARGS(SMCVFXComposerPanel) {} SLATE_END_ARGS()

        void Construct(const FArguments&)
        {
            Working.Reset(NewObject<UMCVFXComposition>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional));
            FDetailsViewArgs Args;
            Args.bUpdatesFromSelection = false;
            Args.bLockable = false;
            Args.bAllowSearch = true;
            Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
            FStructureDetailsViewArgs StructureArgs;
            StructureArgs.bShowObjects=true;
            LayerDetails=FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateStructureDetailView(Args,StructureArgs,TSharedPtr<FStructOnScope>());
            LayerDetails->GetOnFinishedChangingPropertiesDelegate().AddSP(this,&SMCVFXComposerPanel::OnDetailsChanged);
            if (GEditor) GEditor->RegisterForUndo(this);

            ChildSlot
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(8)
                [
                    SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6,6))
                    + SWrapBox::Slot()
                    [Button(LOCTEXT("PerkActions", "Действия перков"), LOCTEXT("PerkActionsHint", "Открыть предпросмотр полного действия и его VFX-слотов."), []{ MCPerkActionPreviewPanel::Open(); return FReply::Handled(); })]
                    + SWrapBox::Slot()
                    [Button(LOCTEXT("New", "Новая"), LOCTEXT("NewHint", "Начать новую рабочую копию."), [this]{ return NewComposition(); })]
                    + SWrapBox::Slot()
                    [Button(LOCTEXT("Open", "Открыть выбранную DA"), LOCTEXT("OpenHint", "Выберите композицию в Content Browser. Оригинал останется без изменений."), [this]{ return OpenSelected(); })]
                    + SWrapBox::Slot()
                    [Button(LOCTEXT("Add", "+ Выбранные Niagara"), LOCTEXT("AddHint", "Добавить Niagara Systems из выделения Content Browser отдельными слоями."), [this]{ return AddSelected(); })]
                    + SWrapBox::Slot()
                    [Button(LOCTEXT("SaveNew", "Сохранить новую DA…"), LOCTEXT("SaveHint", "Сохранить композицию как новый ассет. Существующие DA и исходные NS не перезаписываются."), [this]{ SaveNew(); return FReply::Handled(); })]
                    + SWrapBox::Slot().VAlign(VAlign_Center)
                    [SNew(STextBlock).Text_Lambda([this]{ return FText::FromString(bDirty ? TEXT("● Есть несохранённые изменения") : TEXT("Рабочая копия")); })]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(8,0,8,8)
                [SNew(STextBlock).Text_Lambda([this]{ return FText::FromString(SourcePath.IsEmpty() ? TEXT("Редактируется временная композиция. Каждый слой — самостоятельная Niagara System.") : FString::Printf(TEXT("Копия: %s. Исходный ассет не изменяется."), *SourcePath)); }).AutoWrapText(true)]
                + SVerticalBox::Slot().AutoHeight().Padding(8,0,8,8)
                [
                    SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(14,6))
                    + SWrapBox::Slot().VAlign(VAlign_Center)
                    [SNew(STextBlock).Text(LOCTEXT("DurationLabel", "Композиция, с"))]
                    + SWrapBox::Slot()
                    [SNew(SBox).WidthOverride(85)[SNew(SSpinBox<float>).MinValue(.05f).MaxValue(30.f)
                        .ToolTipText(LOCTEXT("DurationHint", "Длительность основной части. После её конца эмиссия останавливается, уже рождённые частицы могут дожить в хвосте."))
                        .Value_Lambda([this]{return Working->DurationSeconds;})
                        .OnValueCommitted_Lambda([this](float Value,ETextCommit::Type){SetCompositionDuration(Value,false);})]]
                    + SWrapBox::Slot().VAlign(VAlign_Center)
                    [SNew(STextBlock).Text(LOCTEXT("TailLabel", "Хвост, с"))]
                    + SWrapBox::Slot()
                    [SNew(SBox).WidthOverride(85)[SNew(SSpinBox<float>).MinValue(0.f).MaxValue(10.f)
                        .ToolTipText(LOCTEXT("TailHint", "Максимальное время после конца эмиссии для затухания оставшихся частиц. Затем компоненты очищаются."))
                        .Value_Lambda([this]{return Working->TailSeconds;})
                        .OnValueCommitted_Lambda([this](float Value,ETextCommit::Type){SetCompositionDuration(Value,true);})]]
                    + SWrapBox::Slot().VAlign(VAlign_Center)
                    [SNew(STextBlock).Text(LOCTEXT("CompositionTimingHint", "Слои и параметры справа редактируются по одному."))]
                ]
                + SVerticalBox::Slot().FillHeight(1).Padding(8,0)
                [
                    SNew(SSplitter)
                    + SSplitter::Slot().Value(.22f).MinSize(230)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
                        [SNew(STextBlock).Text(LOCTEXT("Layers", "СЛОИ / ПОРЯДОК КОМПОЗИЦИИ"))]
                        + SVerticalBox::Slot().FillHeight(1)
                        [
                            SAssignNew(LayerList, SListView<TSharedPtr<FComposerLayerItem>>)
                            .ListItemsSource(&LayerItems)
                            .SelectionMode(ESelectionMode::Single)
                            .OnGenerateRow(this, &SMCVFXComposerPanel::MakeLayerRow)
                            .OnSelectionChanged(this, &SMCVFXComposerPanel::SelectLayer)
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)
                            [LayerButton(LOCTEXT("Duplicate", "Дубликат"), [this]{ DuplicateLayer(); })]
                            + SHorizontalBox::Slot().FillWidth(1)
                            [LayerButton(LOCTEXT("Remove", "Удалить"), [this]{ RemoveLayer(); })]
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0,5,0,0)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)
                            [LayerButton(LOCTEXT("Up", "↑ Выше"), [this]{ MoveLayer(-1); })]
                            + SHorizontalBox::Slot().FillWidth(1)
                            [LayerButton(LOCTEXT("Down", "↓ Ниже"), [this]{ MoveLayer(1); })]
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0,5,0,0)
                        [LayerButton(LOCTEXT("Discover", "Найти User-параметры слоя"), [this]{ DiscoverSelected(); })]
                        + SVerticalBox::Slot().AutoHeight().Padding(0,5,0,0)
                        [LayerButton(LOCTEXT("OpenNS", "Открыть исходную Niagara"), [this]{ OpenSourceSystem(); })]
                        + SVerticalBox::Slot().AutoHeight().Padding(0,10,0,0)
                        [SNew(STextBlock).Text(LOCTEXT("LayerHelp", "Выберите слой — его параметры появятся справа. Включение и Solo — слева. Порядок слоёв не задаёт порядок прозрачного рендера.")).AutoWrapText(true)]
                    ]
                    + SSplitter::Slot().Value(.48f).MinSize(320)
                    [SAssignNew(Viewport, SMCVFXComposerViewport)]
                    + SSplitter::Slot().Value(.30f).MinSize(280)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().Padding(8,0,0,6)
                        [SNew(STextBlock).Text_Lambda([this]{return IsSelectedValid()?FText::FromString(TEXT("НАСТРОЙКИ СЛОЯ · ")+Working->Layers[SelectedIndex].Name.ToString()):LOCTEXT("NoSelectedLayer", "ВЫБЕРИТЕ СЛОЙ СЛЕВА");}).AutoWrapText(true)]
                        + SVerticalBox::Slot().FillHeight(1).Padding(8,0,0,0)
                        [LayerDetails->GetWidget().ToSharedRef()]
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(8,8,8,0)
                [SNew(STextBlock).Text(LOCTEXT("TimelineTitle", "ТАЙМЛАЙН · эмиссия слоёв и текущее время"))]
                + SVerticalBox::Slot().AutoHeight().Padding(8,4,8,0)
                [
                    SNew(SBox).HeightOverride(105)
                    [SNew(SScrollBox)
                        + SScrollBox::Slot()
                        [SAssignNew(Timeline,SMCVFXComposerTimeline)
                            .Composition_Lambda([this]{return Working.Get();})
                            .Time_Lambda([this]{return Viewport.IsValid()?Viewport->GetPlaybackTime():0.f;})
                            .SelectedIndex_Lambda([this]{return SelectedIndex;})]]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(8)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().Padding(0,0,5,0)
                    [Button(LOCTEXT("Play", "▶ Старт"), LOCTEXT("PlayHint", "Продолжить воспроизведение."), [this]{ Viewport->Play(); return FReply::Handled(); })]
                    + SHorizontalBox::Slot().AutoWidth().Padding(0,0,5,0)
                    [Button(LOCTEXT("Pause", "Ⅱ Пауза"), LOCTEXT("PauseHint", "Приостановить превью."), [this]{ Viewport->Pause(); return FReply::Handled(); })]
                    + SHorizontalBox::Slot().AutoWidth().Padding(0,0,10,0)
                    [Button(LOCTEXT("Reset", "↺ Сначала"), LOCTEXT("ResetHint", "Сбросить симуляцию и запустить с начала."), [this]{ Viewport->Reset(); return FReply::Handled(); })]
                    + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0,0,8,0)
                    [SNew(SSlider).ToolTipText(LOCTEXT("ScrubHint", "Перетащите и отпустите для пересимуляции. GPU/history-эффекты могут отличаться при поиске времени."))
                        .Value_Lambda([this]{ return FMath::Clamp((bScrubbing?PendingSeek:Viewport->GetPlaybackTime())/GetEndTime(), 0.f, 1.f); })
                        .OnMouseCaptureBegin_Lambda([this]{ PendingSeek=Viewport->GetPlaybackTime();bScrubbing=true;Viewport->Pause(); })
                        .OnMouseCaptureEnd_Lambda([this]{ bScrubbing=false;Viewport->Seek(PendingSeek); })
                        .OnValueChanged_Lambda([this](float Value){ PendingSeek=Value*GetEndTime();if(!bScrubbing)Viewport->Seek(PendingSeek); })]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,12,0)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth()
                        [SNew(SBox).WidthOverride(75)[SNew(SSpinBox<float>).MinValue(0.f).MaxValue_Lambda([this]{return GetEndTime();})
                            .Value_Lambda([this]{return bScrubbing?PendingSeek:Viewport->GetPlaybackTime();})
                            .OnValueCommitted_Lambda([this](float Value,ETextCommit::Type){Viewport->Seek(FMath::Clamp(Value,0.f,GetEndTime()));})]]
                        + SHorizontalBox::Slot().AutoWidth().Padding(5,0,0,0).VAlign(VAlign_Center)
                        [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(FString::Printf(TEXT("/ %.2f с"),GetEndTime()));})]
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(8,0,8,8)
                [
                    SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(12,6))
                    + SWrapBox::Slot()
                    [SNew(SCheckBox).IsChecked(ECheckBoxState::Unchecked).OnCheckStateChanged_Lambda([this](ECheckBoxState State){ Viewport->SetMotionEnabled(State==ECheckBoxState::Checked); })[SNew(STextBlock).Text(LOCTEXT("Motion", "Движение"))]]
                    + SWrapBox::Slot()
                    [SNew(SCheckBox).IsChecked(ECheckBoxState::Checked).OnCheckStateChanged_Lambda([this](ECheckBoxState State){ Viewport->SetGroundVisible(State==ECheckBoxState::Checked); })[SNew(STextBlock).Text(LOCTEXT("Ground", "Пол"))]]
                    + SWrapBox::Slot()
                    [SNew(SCheckBox).IsChecked(ECheckBoxState::Unchecked).OnCheckStateChanged_Lambda([this](ECheckBoxState State){ Viewport->SetBoundsVisible(State==ECheckBoxState::Checked); })[SNew(STextBlock).Text(LOCTEXT("Bounds", "Границы"))]]
                    + SWrapBox::Slot()
                    [Button(LOCTEXT("Focus", "Фокус"), LOCTEXT("FocusHint", "Поместить композицию в кадр."), [this]{ Viewport->Focus(); return FReply::Handled(); })]
                    + SWrapBox::Slot()
                    [Button(LOCTEXT("FrontView", "Спереди"), LOCTEXT("FrontViewHint", "Проверить читаемость эффекта и лица спереди."), [this]{ Viewport->SetViewPreset(0); return FReply::Handled(); })]
                    + SWrapBox::Slot()
                    [Button(LOCTEXT("SideView", "Сбоку"), LOCTEXT("SideViewHint", "Проверить глубину и пересечения сбоку."), [this]{ Viewport->SetViewPreset(1); return FReply::Handled(); })]
                    + SWrapBox::Slot()
                    [Button(LOCTEXT("BackView", "Сзади"), LOCTEXT("BackViewHint", "Проверить эффект со стороны спины."), [this]{ Viewport->SetViewPreset(2); return FReply::Handled(); })]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(8,0,8,8)
                [
                    SNew(SBorder).Padding(8)
                    [SNew(STextBlock).Text_Lambda([this]{ return FText::FromString(Status + TEXT("\n") + (Viewport.IsValid() ? Viewport->GetDiagnostics() : FString())); }).AutoWrapText(true)]
                ]
            ];
            Refresh();
        }

        virtual ~SMCVFXComposerPanel() override
        {
            if (GEditor) GEditor->UnregisterForUndo(this);
        }

        virtual void PostUndo(bool bSuccess) override { if (bSuccess) { bDirty=true; Refresh(); } }
        virtual void PostRedo(bool bSuccess) override { PostUndo(bSuccess); }

        bool CanClose()
        {
            if (!bDirty) return true;
            const EAppReturnType::Type Choice=FMessageDialog::Open(EAppMsgType::YesNoCancel,
                LOCTEXT("Unsaved", "Сохранить рабочую композицию в новую DA?\nДа — сохранить, Нет — закрыть без сохранения, Отмена — вернуться."));
            return Choice==EAppReturnType::No || (Choice==EAppReturnType::Yes && SaveNew());
        }

    private:
        TStrongObjectPtr<UMCVFXComposition> Working;
        TSharedPtr<IStructureDetailsView> LayerDetails;
        TSharedPtr<FStructOnScope> LayerProxy;
        TSharedPtr<SMCVFXComposerTimeline> Timeline;
        TSharedPtr<SMCVFXComposerViewport> Viewport;
        TSharedPtr<SListView<TSharedPtr<FComposerLayerItem>>> LayerList;
        TArray<TSharedPtr<FComposerLayerItem>> LayerItems;
        int32 SelectedIndex=INDEX_NONE;
        bool bDirty=false;
        bool bScrubbing=false;
        bool bUpdatingProxy=false;
        float PendingSeek=0;
        FString SourcePath;
        FString Status=TEXT("Выберите Niagara Systems в Content Browser и нажмите «+ Выбранные Niagara». Экспорт в единый NS в эту версию не входит.");

        TSharedRef<SWidget> Button(const FText& Label, const FText& Hint, TFunction<FReply()> Action)
        {
            return SNew(SButton).Text(Label).ToolTipText(Hint).OnClicked_Lambda([Action=MoveTemp(Action)]{ return Action(); });
        }

        TSharedRef<SWidget> LayerButton(const FText& Label, TFunction<void()> Action)
        {
            return SNew(SButton).Text(Label).IsEnabled_Lambda([this]{ return IsSelectedValid(); })
                .OnClicked_Lambda([Action=MoveTemp(Action)]{ Action(); return FReply::Handled(); });
        }

        bool IsSelectedValid() const { return Working.IsValid() && Working->Layers.IsValidIndex(SelectedIndex); }
        float GetEndTime() const { return Working.IsValid() ? FMath::Max(.01f,Working->DurationSeconds+Working->TailSeconds) : 3.f; }

        void Changed()
        {
            bDirty=true;
            Refresh();
        }

        void Refresh()
        {
            LayerItems.Reset();
            for (int32 Index=0; Index<Working->Layers.Num(); ++Index)
            {
                TSharedPtr<FComposerLayerItem> Item=MakeShared<FComposerLayerItem>();
                Item->Index=Index;
                LayerItems.Add(Item);
            }
            if (!Working->Layers.IsValidIndex(SelectedIndex)) SelectedIndex=Working->Layers.IsEmpty()?INDEX_NONE:0;
            LayerList->RequestListRefresh();
            if (LayerItems.IsValidIndex(SelectedIndex)) LayerList->SetSelection(LayerItems[SelectedIndex]);
            RefreshSelectedProxy();
            if(Timeline.IsValid())Timeline->Refresh();
            if (Viewport.IsValid())
            {
                Viewport->SetComposition(Working.Get());
                if (!Working->Layers.IsEmpty()) Viewport->Play();
            }
        }

        void OnDetailsChanged(const FPropertyChangedEvent&)
        {
            if(bUpdatingProxy || !IsSelectedValid() || !LayerProxy.IsValid() || !LayerProxy->IsValid())return;
            const FMCVFXCompositionLayer Edited=*reinterpret_cast<const FMCVFXCompositionLayer*>(LayerProxy->GetStructMemory());
            const FScopedTransaction Tx(LOCTEXT("EditSelectedLayer", "VFX: изменить выбранный слой"));
            Working->Modify();
            Working->Layers[SelectedIndex]=Edited;
            Changed();
        }

        void RefreshSelectedProxy()
        {
            TGuardValue<bool> Guard(bUpdatingProxy,true);
            if(IsSelectedValid())
            {
                LayerProxy=MakeShared<FStructOnScope>(FMCVFXCompositionLayer::StaticStruct());
                FMCVFXCompositionLayer::StaticStruct()->CopyScriptStruct(LayerProxy->GetStructMemory(),&Working->Layers[SelectedIndex]);
            }
            else LayerProxy.Reset();
            LayerDetails->SetStructureData(LayerProxy);
        }

        void SetCompositionDuration(float Value,bool bTail)
        {
            if(!FMath::IsFinite(Value))return;
            const float Clamped=FMath::Clamp(Value,bTail?0.f:.05f,bTail?10.f:30.f);
            float& Destination=bTail?Working->TailSeconds:Working->DurationSeconds;
            if(FMath::IsNearlyEqual(Destination,Clamped))return;
            const FScopedTransaction Tx(LOCTEXT("EditCompositionDuration", "VFX: изменить длительность композиции"));
            Working->Modify();Destination=Clamped;Changed();
        }

        TSharedRef<ITableRow> MakeLayerRow(TSharedPtr<FComposerLayerItem> Item, const TSharedRef<STableViewBase>& Owner)
        {
            const int32 Index=Item->Index;
            return SNew(STableRow<TSharedPtr<FComposerLayerItem>>,Owner).Padding(4)
            [
                SNew(SBorder).Padding(6).BorderImage(FAppStyle::GetBrush(TEXT("WhiteBrush")))
                .BorderBackgroundColor_Lambda([this,Index]{return Index==SelectedIndex?FLinearColor(.055f,.19f,.22f,1):FLinearColor(.045f,.055f,.07f,1);})
                [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0,0,5,0)
                [SNew(SCheckBox).ToolTipText(LOCTEXT("Enabled", "Включить слой"))
                    .IsChecked_Lambda([this,Index]{return Working->Layers.IsValidIndex(Index)&&Working->Layers[Index].bEnabled?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                    .OnCheckStateChanged_Lambda([this,Index](ECheckBoxState State){if(Working->Layers.IsValidIndex(Index)){const FScopedTransaction Tx(LOCTEXT("ToggleLayer", "VFX: включение слоя"));Working->Modify();Working->Layers[Index].bEnabled=State==ECheckBoxState::Checked;Changed();}})]
                + SHorizontalBox::Slot().AutoWidth().Padding(0,0,7,0)
                [SNew(SCheckBox).ToolTipText(LOCTEXT("SoloHint", "Показывать только Solo-слои. Отключённый слой остаётся отключённым."))
                    .IsChecked_Lambda([this,Index]{return Working->Layers.IsValidIndex(Index)&&Working->Layers[Index].bSolo?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                    .OnCheckStateChanged_Lambda([this,Index](ECheckBoxState State){if(Working->Layers.IsValidIndex(Index)){const FScopedTransaction Tx(LOCTEXT("SoloLayer", "VFX: Solo слоя"));Working->Modify();Working->Layers[Index].bSolo=State==ECheckBoxState::Checked;Changed();}})
                    [SNew(STextBlock).Text(FText::FromString(TEXT("S")))]]
                + SHorizontalBox::Slot().FillWidth(1)
                [SNew(STextBlock).Text_Lambda([this,Index]{if(!Working->Layers.IsValidIndex(Index))return FText::GetEmpty();const auto& Layer=Working->Layers[Index];return FText::FromString(FString::Printf(TEXT("%02d  %s\n%s"),Index+1,*Layer.Name.ToString(),*Layer.System.GetAssetName()));}).AutoWrapText(true)]
                ]
            ];
        }

        void SelectLayer(TSharedPtr<FComposerLayerItem> Item, ESelectInfo::Type)
        {
            SelectedIndex=Item.IsValid()?Item->Index:INDEX_NONE;
            RefreshSelectedProxy();
        }

        TArray<FAssetData> SelectedAssets() const
        {
            TArray<FAssetData> Assets;
            FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().GetSelectedAssets(Assets);
            return Assets;
        }

        FReply NewComposition()
        {
            if (!CanClose()) return FReply::Handled();
            Working.Reset(NewObject<UMCVFXComposition>(GetTransientPackage(),NAME_None,RF_Transient|RF_Transactional));
            SourcePath.Reset(); bDirty=false; SelectedIndex=INDEX_NONE;
            Status=TEXT("Новая композиция. Добавьте Niagara Systems из Content Browser."); Refresh();
            return FReply::Handled();
        }

        FReply OpenSelected()
        {
            UMCVFXComposition* Source=nullptr;
            for (const auto& Asset:SelectedAssets()) if ((Source=Cast<UMCVFXComposition>(Asset.GetAsset()))) break;
            if (!Source) {Status=TEXT("Выберите DA композиции в Content Browser.");return FReply::Handled();}
            if (!CanClose()) return FReply::Handled();
            Working.Reset(DuplicateObject<UMCVFXComposition>(Source,GetTransientPackage(),MakeUniqueObjectName(GetTransientPackage(),UMCVFXComposition::StaticClass(),TEXT("VFXComposerWorking"))));
            Working->ClearFlags(RF_Public|RF_Standalone); Working->SetFlags(RF_Transient|RF_Transactional);
            SourcePath=Source->GetPathName(); bDirty=false; SelectedIndex=INDEX_NONE;
            Status=TEXT("Открыта независимая рабочая копия. «Сохранить новую DA» создаст новую версию."); Refresh();
            return FReply::Handled();
        }

        FReply AddSelected()
        {
            TArray<UNiagaraSystem*> Systems;
            for (const auto& Asset:SelectedAssets()) if (UNiagaraSystem* System=Cast<UNiagaraSystem>(Asset.GetAsset())) Systems.Add(System);
            if (Systems.IsEmpty()) {Status=TEXT("Выберите одну или несколько Niagara Systems в Content Browser.");return FReply::Handled();}
            if (Working->Layers.Num()+Systems.Num()>32) {Status=TEXT("В композиции максимум 32 слоя. Уменьшите выделение или удалите лишние слои.");return FReply::Handled();}
            const FScopedTransaction Tx(LOCTEXT("AddLayers", "VFX: добавить слои")); Working->Modify();
            for (UNiagaraSystem* System:Systems)
            {
                FMCVFXCompositionLayer Layer; Layer.Name=System->GetFName();Layer.System=System;
                SelectedIndex=Working->Layers.Add(MoveTemp(Layer));
            }
            Status=FString::Printf(TEXT("Добавлено слоёв: %d. Для доступных ручек нажмите «Найти User-параметры слоя»."),Systems.Num());Changed();
            return FReply::Handled();
        }

        void DuplicateLayer()
        {
            if(!IsSelectedValid())return;
            if(Working->Layers.Num()>=32){Status=TEXT("В композиции максимум 32 слоя. Удалите лишний слой перед дублированием.");return;}
            const FScopedTransaction Tx(LOCTEXT("DuplicateLayer", "VFX: дубликат слоя"));Working->Modify();
            FMCVFXCompositionLayer Copy=Working->Layers[SelectedIndex];Copy.Name=FName(*(Copy.Name.ToString()+TEXT("_Copy")));
            Working->Layers.Insert(MoveTemp(Copy),++SelectedIndex);Changed();
        }
        void RemoveLayer()
        {
            if(!IsSelectedValid())return; const FScopedTransaction Tx(LOCTEXT("DeleteLayer", "VFX: удалить слой"));Working->Modify();Working->Layers.RemoveAt(SelectedIndex);Changed();
        }
        void MoveLayer(int32 Direction)
        {
            if(!IsSelectedValid()||!Working->Layers.IsValidIndex(SelectedIndex+Direction))return;
            const FScopedTransaction Tx(LOCTEXT("MoveLayer", "VFX: изменить порядок слоёв"));Working->Modify();Working->Layers.Swap(SelectedIndex,SelectedIndex+Direction);SelectedIndex+=Direction;Changed();
        }
        void DiscoverSelected()
        {
            if(!IsSelectedValid())return;
            const FScopedTransaction Tx(LOCTEXT("DiscoverParams", "VFX: обнаружить User-параметры"));Working->Modify();
            FString Error;
            if(!Working->DiscoverParameters(SelectedIndex,Error)){Status=Error;return;}
            Status=TEXT("Найдены поддерживаемые User-параметры. Включите bOverride справа, чтобы переопределить значение только для этого слоя. Произвольные ручки не создаются автоматически: они должны уже существовать в исходной NS с тем же именем и типом.");
            if(!Error.IsEmpty())Status+=TEXT("\n")+Error;
            Changed();
        }
        void OpenSourceSystem()
        {
            if(!IsSelectedValid()||!GEditor)return;
            if(UNiagaraSystem* Source=Working->Layers[SelectedIndex].System.LoadSynchronous())
                if(auto* Editors=GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())Editors->OpenEditorForAsset(Source);
        }

        bool SaveNew()
        {
            FString Error;
            if(!Working->Validate(Error)) {Status=TEXT("Сохранение отменено: ")+Error;return false;}
            for(const FMCVFXCompositionLayer& Layer:Working->Layers)
            {
                if(!Layer.bEnabled)continue;
                UNiagaraSystem* System=Layer.System.LoadSynchronous();
                if(!System || !UMCVFXComposition::ValidateParameters(Layer,System,Error))
                {
                    Status=TEXT("Сохранение отменено: ")+(System?Error:FString::Printf(TEXT("не удалось загрузить Niagara слоя %s"),*Layer.Name.ToString()));
                    return false;
                }
            }
            FSaveAssetDialogConfig Config;Config.DialogTitleOverride=LOCTEXT("SaveDialog", "Сохранить новую VFX композицию");
            Config.DefaultPath=CompositionFolder;Config.DefaultAssetName=TEXT("DA_VFXComposition");
            Config.AssetClassNames.Add(UMCVFXComposition::StaticClass()->GetClassPathName());
            Config.ExistingAssetPolicy=ESaveAssetDialogExistingAssetPolicy::Disallow;
            const FString ObjectPath=FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().CreateModalSaveAssetDialog(Config);
            if(ObjectPath.IsEmpty())return false;
            if(!FPackageName::IsValidObjectPath(ObjectPath)) {Status=TEXT("Некорректный путь ассета.");return false;}
            const FString PackageName=FPackageName::ObjectPathToPackageName(ObjectPath);
            if(!PackageName.StartsWith(TEXT("/Game/"))) {Status=TEXT("Сохраните композицию внутри /Game/.");return false;}
            if(FPackageName::DoesPackageExist(PackageName)||FindObject<UPackage>(nullptr,*PackageName))
            {Status=TEXT("Такой package уже существует. Выберите новое имя: оригиналы не перезаписываются.");return false;}
            UPackage* Package=CreatePackage(*PackageName);
            UMCVFXComposition* Saved=DuplicateObject<UMCVFXComposition>(Working.Get(),Package,*FPackageName::ObjectPathToObjectName(ObjectPath));
            Saved->ClearFlags(RF_Transient);Saved->SetFlags(RF_Public|RF_Standalone|RF_Transactional);
            FAssetRegistryModule::AssetCreated(Saved);Package->MarkPackageDirty();
            if(!UEditorLoadingAndSavingUtils::SavePackages({Package},true))
            {Status=TEXT("Не удалось сохранить новую DA. Рабочая копия сохранена в окне; повторите с новым именем. Несохранённый package остаётся в Content Browser.");return false;}
            bDirty=false;SourcePath=Saved->GetPathName();
            Status=TEXT("Сохранена новая DA: ")+SourcePath+TEXT(". Исходные Niagara не изменены. Runtime-вызов: PrepareComposition → PlayPrepared; подключение к DT/VFX-профилям — отдельная интеграция.");
            FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().SyncBrowserToAssets(TArray<UObject*>{Saved});
            return true;
        }
    };
}

void MCVFXComposer::Register()
{
    if(IsRunningCommandlet()||FGlobalTabmanager::Get()->HasTabSpawner(ComposerTabName))return;
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(ComposerTabName,FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
    {
        TSharedRef<SMCVFXComposerPanel> Panel=SNew(SMCVFXComposerPanel);
        return SNew(SDockTab).TabRole(ETabRole::NomadTab)
            .OnCanCloseTab(SDockTab::FCanCloseTab::CreateSP(Panel,&SMCVFXComposerPanel::CanClose))
            [Panel];
    }))
    .SetDisplayName(LOCTEXT("TabTitle", "VFX Composer · Wash n Smash"))
    .SetTooltipText(LOCTEXT("TabHint", "Собрать эффект из самостоятельных Niagara-слоёв с таймингом и User-параметрами."))
    .SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
}

void MCVFXComposer::Unregister()
{
    if(const TSharedPtr<SDockTab> Tab=FGlobalTabmanager::Get()->FindExistingLiveTab(ComposerTabName))
    {
        // Ordinary close preserves Save / Discard / Cancel. This module does not support hot unloading.
        if(!Tab->RequestCloseTab())return;
    }
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ComposerTabName);
}

void MCVFXComposer::Open()
{
    MCVFXActionWorkspace::Open();
}

static FAutoConsoleCommand OpenVFXComposerCommand(TEXT("MC.VFXComposer"),
    TEXT("Open the local Wash n Smash VFX Composer editor tab."),
    FConsoleCommandDelegate::CreateStatic(&MCVFXActionWorkspace::Open));
static FAutoConsoleCommand OpenVFXElementsCommand(TEXT("MC.VFXElements"),
    TEXT("Open the independent effect recipe and ingredient graph editor."),
    FConsoleCommandDelegate::CreateStatic(&MCVFXRecipeComposer::Open));
static FAutoConsoleCommand OpenVFXLayersCommand(TEXT("MC.VFXLayers"),
    TEXT("Open the original independent Niagara layers and timeline editor."),
    FConsoleCommandDelegate::CreateStatic(&MCVFXComposer::Open));

#undef LOCTEXT_NAMESPACE
