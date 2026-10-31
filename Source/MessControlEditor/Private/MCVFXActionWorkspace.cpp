#include "MCVFXActionWorkspace.h"

#include "MCVFXActionSession.h"
#include "MCPerkActionPreviewPanel.h"
#include "MCVFXRecipePreset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetThumbnail.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Containers/Ticker.h"
#include "ContentBrowserModule.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/MessageDialog.h"
#include "Misc/Crc.h"
#include "Misc/ObjectThumbnail.h"
#include "IContentBrowserSingleton.h"
#include "InputCoreTypes.h"
#include "NiagaraSystem.h"
#include "ObjectTools.h"
#include "Rendering/DrawElementTypes.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STileView.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "MCVFXActionWorkspace"

namespace
{
    const FName WorkspaceTab(TEXT("MCVFXActionWorkspace"));
    FLinearColor UIColor(const TCHAR* Hex){return FLinearColor::FromSRGBColor(FColor::FromHex(Hex));}
    const FLinearColor Mint=UIColor(TEXT("#8FE3CF")),Ink=UIColor(TEXT("#123136")),Surface=UIColor(TEXT("#1C2A36")),Muted=UIColor(TEXT("#9EB3C3"));
    const FLinearColor AppFill=UIColor(TEXT("#0F141B")),GraphFill=UIColor(TEXT("#151C25")),PanelFill=UIColor(TEXT("#171F2A")),HeadingFill=UIColor(TEXT("#1D2835"));
    const FLinearColor ButtonFill=UIColor(TEXT("#253443")),ButtonHover=UIColor(TEXT("#314859")),BorderFill=UIColor(TEXT("#324454")),TextColor=UIColor(TEXT("#EFF4F8"));
    const FSlateRoundedBoxBrush& PanelBrush()
    {static const FSlateRoundedBoxBrush Brush(FLinearColor::White,7.f);return Brush;}
    const FSlateRoundedBoxBrush& SelectedPanelBrush()
    {static const FSlateRoundedBoxBrush Brush(FLinearColor::White,7.f,Mint,1.2f);return Brush;}
    using FGalleryItem=TSharedPtr<FAssetData>;
    using FCatalogItem=TSharedPtr<FMCVFXActionCatalogueEntry>;
    const FVector2D HubPosition(234,210),HubSize(136,136),GraphExtent(588,548);
    FVector2D StageDimensions(const UMCVFXRecipePreset* Recipe)
    {
        const int32 Count=Recipe?Recipe->Layers.Num():0;
        return FVector2D(216,Count==0?132:FMath::Min(192,68+Count*32));
    }
    FVector2D StagePosition(int32 Index,const FVector2D& Size,int32 Count)
    {
        if(Count>4)
        {
            const double Angle=-PI*.5+2*PI*Index/Count;
            return FVector2D(425+310*FMath::Cos(Angle),425+295*FMath::Sin(Angle))-Size*.5;
        }
        const bool Right=Index>=2,Bottom=(Index%2)==1;
        return FVector2D(Right?364:8,Bottom?HubPosition.Y+HubSize.Y+12:HubPosition.Y-12-Size.Y);
    }
    FLinearColor StageColor(FName Stage)
    {
        if(Stage==TEXT("Start"))return FLinearColor(.83f,.56f,.25f);
        if(Stage==TEXT("Loop"))return FLinearColor(.28f,.71f,.73f);
        if(Stage==TEXT("Impact"))return FLinearColor(.90f,.43f,.39f);
        return FLinearColor(.49f,.57f,.86f);
    }
    TSharedRef<SWidget> Label(const FString& Value,int32 Size=12,FLinearColor Color=TextColor)
    {return SNew(STextBlock).Text(FText::FromString(Value)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),Size)).ColorAndOpacity(Color).AutoWrapText(true);}
    const FButtonStyle& SolidStyle()
    {
        static const FButtonStyle Style=FButtonStyle().SetNormal(FSlateRoundedBoxBrush(FLinearColor::White,6.f))
            .SetHovered(FSlateRoundedBoxBrush(FLinearColor(1.08f,1.08f,1.08f),6.f)).SetPressed(FSlateRoundedBoxBrush(FLinearColor(.87f,.87f,.87f),6.f))
            .SetDisabled(FSlateRoundedBoxBrush(FLinearColor::White,6.f)).SetDisabledForeground(Muted).SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
        return Style;
    }
    const FButtonStyle& NeutralStyle()
    {
        static const FButtonStyle Style=FButtonStyle().SetNormal(FSlateRoundedBoxBrush(ButtonFill,6.f)).SetHovered(FSlateRoundedBoxBrush(ButtonHover,6.f))
            .SetPressed(FSlateRoundedBoxBrush(HeadingFill,6.f)).SetDisabled(FSlateRoundedBoxBrush(HeadingFill,6.f))
            .SetNormalForeground(TextColor).SetHoveredForeground(TextColor).SetPressedForeground(TextColor).SetDisabledForeground(Muted)
            .SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));return Style;
    }
    TSharedRef<SWidget> Command(const FString& Text,TFunction<void()> Action,bool Primary=false)
    {
        return SNew(SButton).ButtonStyle(Primary?&SolidStyle():&NeutralStyle()).ContentPadding(FMargin(12,9)).ButtonColorAndOpacity(Primary?Mint:FLinearColor::White)
            .ForegroundColor(Primary?Ink:TextColor).OnClicked_Lambda([Action=MoveTemp(Action)]{Action();return FReply::Handled();})
            [SNew(STextBlock).Text(FText::FromString(Text)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),12)).AutoWrapText(true)];
    }
    TSharedRef<SWidget> Heading(const FString& Text)
    {return SNew(SBorder).Padding(10,7).BorderImage(&PanelBrush()).BorderBackgroundColor(HeadingFill)
        [SNew(STextBlock).Text(FText::FromString(Text)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),12)).ColorAndOpacity(TextColor)];}
    TSharedRef<SWidget> SmallCommand(const FString& Text,TFunction<void()> Action)
    {return SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(7,4)).ButtonColorAndOpacity(FLinearColor::White).ForegroundColor(TextColor)
        .OnClicked_Lambda([Action=MoveTemp(Action)]{Action();return FReply::Handled();})[SNew(STextBlock).Text(FText::FromString(Text)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10))];}
    FString ParameterLabel(FName Name)
    {
        const FString S=Name.ToString().Replace(TEXT("User."),TEXT(""));
        if(S==TEXT("Color"))return TEXT("Цвет");if(S==TEXT("Radius"))return TEXT("Радиус");if(S==TEXT("Intensity"))return TEXT("Интенсивность");
        if(S==TEXT("Tint"))return TEXT("Оттенок");if(S==TEXT("Size"))return TEXT("Размер");if(S==TEXT("SpawnRate"))return TEXT("Частиц/сек");
        if(S==TEXT("Direction"))return TEXT("Направление");if(S==TEXT("SurfaceNormal"))return TEXT("Нормаль поверхности");if(S==TEXT("Velocity"))return TEXT("Скорость");
        if(S==TEXT("Position"))return TEXT("Положение");if(S==TEXT("Width"))return TEXT("Ширина");if(S==TEXT("Fill"))return TEXT("Заполнение");if(S==TEXT("EmissiveScale"))return TEXT("Яркость свечения");
        if(S==TEXT("Duration"))return TEXT("Длительность");if(S==TEXT("SpawnCount"))return TEXT("Количество частиц");
        if(S==TEXT("Lifetime"))return TEXT("Время жизни частиц");if(S==TEXT("ParticleScale"))return TEXT("Масштаб частиц");return S;
    }
    FString EffectLabel(const FString& Name)
    {
        static const TMap<FString,FString> Known={
            {TEXT("NS_Rocket_Start"),TEXT("Ракета · выстрел")},{TEXT("NS_Rocket_Loop"),TEXT("Ракета · след")},{TEXT("NS_Rocket_Impact"),TEXT("Ракета · попадание")},
            {TEXT("NS_Sponge_Start"),TEXT("Губка · старт")},{TEXT("NS_Sponge_Loop"),TEXT("Губка · движение")},{TEXT("NS_Sponge_End"),TEXT("Губка · завершение")},{TEXT("NS_Sponge_Trail"),TEXT("Губка · мыльный след")},
            {TEXT("NS_Hook_Start"),TEXT("Хук · бросок")},{TEXT("NS_Hook_Loop"),TEXT("Хук · полёт")},{TEXT("NS_Hook_Impact"),TEXT("Хук · попадание")},{TEXT("NS_Hook_End"),TEXT("Хук · возврат")},
            {TEXT("NS_Grenade_Start"),TEXT("Граната · бросок")},{TEXT("NS_Grenade_Loop"),TEXT("Граната · полёт")},{TEXT("NS_Grenade_Impact"),TEXT("Граната · взрыв")},{TEXT("NS_Grenade_Warning"),TEXT("Граната · предупреждение")},
            {TEXT("NS_Air_Loop"),TEXT("Воздухан · всасывание")},{TEXT("NS_Air_Impact"),TEXT("Воздухан · выдувание")},{TEXT("NS_Air_Rupture"),TEXT("Воздухан · перегрузка")},{TEXT("NS_Air_Warning"),TEXT("Воздухан · предупреждение")},
            {TEXT("NS_Driver_Start"),TEXT("Машина · старт")},{TEXT("NS_Driver_Loop"),TEXT("Машина · движение")},{TEXT("NS_Driver_End"),TEXT("Машина · завершение")},
            {TEXT("NS_Template_Flash"),TEXT("Вспышка")},{TEXT("NS_Template_Smoke"),TEXT("Дым")},{TEXT("NS_Template_Trail"),TEXT("След")},{TEXT("NS_Template_Glow"),TEXT("Свечение")},
            {TEXT("NS_Practice_FoamBasics"),TEXT("Учебный · пена")},{TEXT("NS_Practice_Burst"),TEXT("Учебный · всплеск")},{TEXT("NS_Practice_Trail"),TEXT("Учебный · след")},
            {TEXT("NS_Practice_Impact"),TEXT("Учебный · попадание")},{TEXT("NS_Practice_HealSeed"),TEXT("Учебный · лечение")}};
        if(const auto* Label=Known.Find(Name))return *Label;return Name;
    }
    FString LayerLabel(const FMCVFXCompositionLayer& Layer)
    {
        const FString Name=Layer.Name.ToString(),System=Layer.System.GetAssetName();
        if(Layer.Name.IsNone()||Name==TEXT("Layer")||Name==System||Name==System.Replace(TEXT("NS_Template_"),TEXT("")))return EffectLabel(System);
        return EffectLabel(Name);
    }
    int32 EnabledLayers(const UMCVFXRecipePreset* Recipe)
    {int32 Count=0;if(Recipe)for(const auto& Layer:Recipe->Layers)if(Layer.bEnabled)++Count;return Count;}
    FString ProvenanceLabel(const FAssetData& Asset)
    {
        const FString Path=Asset.PackagePath.ToString(),Name=Asset.AssetName.ToString();
        if(Path.Contains(TEXT("/ActionCompositions/"))||Path.EndsWith(TEXT("/Generated"))||Path.Contains(TEXT("/Generated/")))return TEXT("Собранная версия");
        if(Name.StartsWith(TEXT("NS_Template_")))return TEXT("Стартовый шаблон");
        if(Name.StartsWith(TEXT("NS_Practice_")))return TEXT("Учебный эффект");
        return Path.StartsWith(TEXT("/Game/Gameplay/ActiveSkills/VFX"))?TEXT("Исходный эффект"):TEXT("Эффект проекта");
    }
    TPair<FString,FLinearColor> NeutralGroup(const FAssetData& Asset)
    {
        const FString Name=Asset.AssetName.ToString();
        if(Name.StartsWith(TEXT("NS_Rocket_")))return {TEXT("РАК"),FLinearColor(.37f,.48f,.67f)};
        if(Name.StartsWith(TEXT("NS_Sponge_")))return {TEXT("ГУБ"),FLinearColor(.31f,.62f,.57f)};
        if(Name.StartsWith(TEXT("NS_Hook_")))return {TEXT("ХУК"),FLinearColor(.50f,.48f,.66f)};
        if(Name.StartsWith(TEXT("NS_Grenade_")))return {TEXT("ГРН"),FLinearColor(.70f,.48f,.33f)};
        if(Name.StartsWith(TEXT("NS_Air_")))return {TEXT("ВОЗ"),FLinearColor(.34f,.61f,.71f)};
        if(Name.StartsWith(TEXT("NS_Driver_")))return {TEXT("МАШ"),FLinearColor(.58f,.58f,.38f)};
        if(Name.StartsWith(TEXT("NS_Template_")))return {EffectLabel(Name).Left(3).ToUpper(),Mint};
        if(Name.StartsWith(TEXT("NS_Practice_")))return {TEXT("УЧ"),FLinearColor(.48f,.57f,.63f)};
        return {TEXT("VFX"),Muted};
    }
    struct FGalleryThumbnailEvidence{bool bHasSavedImage=false;uint32 Hash=0;};
    class SWorkspaceLayerButton final : public SButton
    {
    public:
        SLATE_BEGIN_ARGS(SWorkspaceLayerButton){}SLATE_ARGUMENT(TSoftObjectPtr<UNiagaraSystem>,System)SLATE_ARGUMENT(FText,Label)
            SLATE_ATTRIBUTE(bool,Selected)SLATE_EVENT(FSimpleDelegate,OnSelected)SLATE_END_ARGS()
        void Construct(const FArguments& A)
        {
            System=A._System;Selected=A._Selected;OnSelected=A._OnSelected;
            SButton::Construct(SButton::FArguments().ButtonStyle(&SolidStyle()).ContentPadding(FMargin(7,6))
                .ButtonColorAndOpacity_Lambda([this]{return Selected.Get()?Mint:ButtonFill;})
                .ForegroundColor_Lambda([this]{return FSlateColor(Selected.Get()?Ink:TextColor);})
                .OnClicked_Lambda([this]{OnSelected.ExecuteIfBound();return FReply::Handled();})
                [SNew(STextBlock).Text(A._Label).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12)).AutoWrapText(true)]);
            SetToolTipText(FText::FromString(TEXT("Клик — выбрать слой. Двойной клик — открыть исходную Niagara System.\n")+System.ToSoftObjectPath().ToString()));
        }
        virtual FReply OnMouseButtonDoubleClick(const FGeometry& G,const FPointerEvent& E)override
        {
            if(E.GetEffectingButton()!=EKeys::LeftMouseButton)return SButton::OnMouseButtonDoubleClick(G,E);
            OnSelected.ExecuteIfBound();
            if(GEditor)if(auto* Source=System.LoadSynchronous())if(!Source->HasAnyFlags(RF_Transient))
                if(auto* Editors=GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())Editors->OpenEditorForAsset(Source);
            return FReply::Handled();
        }
    private:
        TSoftObjectPtr<UNiagaraSystem> System;TAttribute<bool> Selected;FSimpleDelegate OnSelected;
    };
    DECLARE_DELEGATE_TwoParams(FDropWorkspaceEffect,FName,FAssetData);
    class FWorkspaceEffectDrag final : public FDecoratedDragDropOp
    {
    public:
        DRAG_DROP_OPERATOR_TYPE(FWorkspaceEffectDrag,FDecoratedDragDropOp)
        FAssetData Asset;
        static TSharedRef<FWorkspaceEffectDrag> New(const FAssetData& InAsset)
        {
            auto Op=MakeShared<FWorkspaceEffectDrag>();Op->Asset=InAsset;
            Op->Reject();Op->SetupDefaults();Op->Construct();return Op;
        }
        void Reject()
        {SetToolTip(FText::FromString(EffectLabel(Asset.AssetName.ToString())+TEXT("  ·  Перетащите на карточку события. Esc — отмена.")),nullptr);CurrentIconColorAndOpacity=Muted;}
        void Target(const FText& StageLabel,bool Allowed,const FString& Reason)
        {SetToolTip(FText::FromString(EffectLabel(Asset.AssetName.ToString())+(Allowed?TEXT("  →  Добавить слой в «")+StageLabel.ToString()+TEXT("»"):TEXT("  →  ")+Reason)),nullptr);CurrentIconColorAndOpacity=Allowed?Mint:Muted;}
        virtual TSharedPtr<SWidget> GetDefaultDecorator()const override
        {
            return SNew(SBorder).Padding(12,9).BorderImage(&PanelBrush()).BorderBackgroundColor(HeadingFill)
                [SNew(STextBlock).Text_Lambda([this]{return GetHoverText();}).ColorAndOpacity_Lambda([this]{return GetIconColorAndOpacity();})
                    .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),12)).WrapTextAt(420.f)];
        }
    };
    class SWorkspaceEventDropZone final : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SWorkspaceEventDropZone){}SLATE_DEFAULT_SLOT(FArguments,Content)SLATE_ARGUMENT(FName,Stage)
            SLATE_ATTRIBUTE(bool,CanAccept)SLATE_ATTRIBUTE(FText,StageLabel)SLATE_ATTRIBUTE(FString,UnavailableReason)SLATE_ATTRIBUTE(bool,Selected)SLATE_EVENT(FDropWorkspaceEffect,OnEffectDropped)SLATE_END_ARGS()
        void Construct(const FArguments& A)
        {
            Stage=A._Stage;CanAccept=A._CanAccept;StageLabel=A._StageLabel;UnavailableReason=A._UnavailableReason;Selected=A._Selected;OnEffectDropped=A._OnEffectDropped;
            ChildSlot[SNew(SBorder).Padding(2).BorderImage(&PanelBrush()).BorderBackgroundColor_Lambda([this]
                {return bHovered&&HoverOperation.IsValid()&&FSlateApplication::Get().GetDragDroppingContent().Get()==HoverOperation.Pin().Get()?(CanAccept.Get()?Mint:FLinearColor(.63f,.35f,.30f)):
                    Selected.Get()?Mint:BorderFill;})[A._Content.Widget]];
        }
        virtual void OnDragEnter(const FGeometry&,const FDragDropEvent& E)override
        {if(const auto Op=E.GetOperationAs<FWorkspaceEffectDrag>()){bHovered=true;HoverOperation=Op;Op->Target(StageLabel.Get(),CanAccept.Get(),UnavailableReason.Get());Invalidate(EInvalidateWidgetReason::Paint);}}
        virtual void OnDragLeave(const FDragDropEvent& E)override
        {bHovered=false;if(const auto Op=E.GetOperationAs<FWorkspaceEffectDrag>())Op->Reject();Invalidate(EInvalidateWidgetReason::Paint);}
        virtual FReply OnDragOver(const FGeometry&,const FDragDropEvent& E)override
        {if(const auto Op=E.GetOperationAs<FWorkspaceEffectDrag>()){bHovered=true;HoverOperation=Op;Op->Target(StageLabel.Get(),CanAccept.Get(),UnavailableReason.Get());return FReply::Handled();}return FReply::Unhandled();}
        virtual FReply OnDrop(const FGeometry&,const FDragDropEvent& E)override
        {
            bHovered=false;const auto Op=E.GetOperationAs<FWorkspaceEffectDrag>();if(!Op)return FReply::Unhandled();
            if(CanAccept.Get()&&Op->Asset.AssetClassPath==UNiagaraSystem::StaticClass()->GetClassPathName())
            {const FDropWorkspaceEffect Callback=OnEffectDropped;Callback.ExecuteIfBound(Stage,Op->Asset);}
            return FReply::Handled();
        }
    private:
        FName Stage;TAttribute<FText> StageLabel;TAttribute<FString> UnavailableReason;TAttribute<bool> CanAccept,Selected;FDropWorkspaceEffect OnEffectDropped;bool bHovered=false;TWeakPtr<FWorkspaceEffectDrag> HoverOperation;
    };

    class SActionStageGraph;
    class SActionConnections final : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SActionConnections):_PinsOnly(false){}SLATE_ARGUMENT(TWeakPtr<SActionStageGraph>,Graph)SLATE_ARGUMENT(bool,PinsOnly)SLATE_END_ARGS()
        void Construct(const FArguments& A){Graph=A._Graph;bPinsOnly=A._PinsOnly;SetVisibility(EVisibility::HitTestInvisible);}
        virtual FVector2D ComputeDesiredSize(float)const override{return GraphExtent;}
        virtual int32 OnPaint(const FPaintArgs&,const FGeometry&,const FSlateRect&,FSlateWindowElementList&,int32,const FWidgetStyle&,bool)const override;
        TWeakPtr<SActionStageGraph> Graph;
        bool bPinsOnly=false;
    };
    DECLARE_DELEGATE_TwoParams(FSelectWorkspaceLayer,FName,FGuid);
    class SActionStageGraph final : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SActionStageGraph){}SLATE_ARGUMENT(TSharedPtr<FMCVFXActionSession>,Session)SLATE_ATTRIBUTE(FName,SelectedStage)
            SLATE_ATTRIBUTE(FGuid,SelectedLayer)SLATE_EVENT(FSelectWorkspaceLayer,OnSelect)SLATE_EVENT(FSimpleDelegate,OnHub)SLATE_EVENT(FDropWorkspaceEffect,OnEffectDropped)SLATE_END_ARGS()
        void Construct(const FArguments& A)
        {Session=A._Session;SelectedStage=A._SelectedStage;SelectedLayer=A._SelectedLayer;OnSelect=A._OnSelect;OnHub=A._OnHub;OnEffectDropped=A._OnEffectDropped;Rebuild();}
        void Rebuild()
        {
            TSharedRef<SConstraintCanvas> Canvas=SNew(SConstraintCanvas).RenderTransform_Lambda([this]{return FSlateRenderTransform(FScale2D(Zoom),Pan);}).RenderTransformPivot(FVector2D::ZeroVector);
            const auto Stages=Session->GetActionStages();
            for(int32 I=0;I<Stages.Num();++I)
            {
                const FName Stage=Stages[I];const auto* Recipe=Session->GetRecipe(Stage);const FVector2D Size=StageDimensions(Recipe),P=StagePosition(I,Size,Stages.Num());
                TSharedRef<SVerticalBox> Rows=SNew(SVerticalBox);
                if(Recipe)for(int32 L=0;L<Recipe->Layers.Num()&&Recipe->Nodes.IsValidIndex(L);++L)
                {
                    const FGuid ID=Recipe->Nodes[L].ID;const auto& Layer=Recipe->Layers[L];
                    Rows->AddSlot().AutoHeight().Padding(4,2)[SNew(SWorkspaceLayerButton).System(Layer.System)
                        .Selected_Lambda([this,Stage,ID]{return SelectedStage.Get()==Stage&&SelectedLayer.Get()==ID;})
                        .Label(FText::FromString((Layer.bEnabled?TEXT("● "):TEXT("○ "))+LayerLabel(Layer)))
                        .OnSelected_Lambda([this,Stage,ID]{OnSelect.ExecuteIfBound(Stage,ID);})];
                }
                if(!Recipe||Recipe->Layers.IsEmpty())Rows->AddSlot().AutoHeight().Padding(10,7)
                    [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(!Session->IsContextCurrent()?TEXT("Контекст изменился.\nВыберите действие\nзаново."):
                        Session->CanEditProfile()?TEXT("Перетащите эффект\nиз библиотеки."):TEXT("Просмотр механики.\nVFX-привязка ещё\nне подключена."));})
                        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11)).ColorAndOpacity(Muted).AutoWrapText(true)];
                Canvas->AddSlot().Offset(FMargin(P.X,P.Y,Size.X,Size.Y)).Alignment(FVector2D::ZeroVector)
                    [SNew(SWorkspaceEventDropZone).Stage(Stage).CanAccept_Lambda([this]{return Session&&Session->CanEditProfile();}).StageLabel_Lambda([this,Stage]{return Session->GetEventLabel(Stage);}).UnavailableReason_Lambda([this]{return Session->GetEditUnavailableReason();}).Selected_Lambda([this,Stage]{return SelectedStage.Get()==Stage;})
                        .OnEffectDropped_Lambda([this](FName Event,FAssetData Asset){OnEffectDropped.ExecuteIfBound(Event,Asset);})
                        [SNew(SBorder).Padding(1).BorderImage(&PanelBrush()).BorderBackgroundColor(Surface)
                        [SNew(SVerticalBox)
                            +SVerticalBox::Slot().AutoHeight()[SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(10,8)).ButtonColorAndOpacity(FLinearColor::White).ForegroundColor(TextColor)
                                .OnClicked_Lambda([this,Stage]{OnSelect.ExecuteIfBound(Stage,FGuid());return FReply::Handled();})
                                [SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[Label(Session->GetEventLabel(Stage).ToString(),14,StageColor(Stage))]
                                    +SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text_Lambda([this,Stage]{const auto* Current=Session->GetRecipe(Stage);return FText::FromString(!Session->IsContextCurrent()?TEXT("—"):
                                        Session->CanEditProfile()?FString::Printf(TEXT("%d/%d"),EnabledLayers(Current),Current?Current->Layers.Num():0):TEXT("Просмотр"));})
                                        .ToolTipText_Lambda([this]{return Session->CanEditProfile()?LOCTEXT("EnabledCountHint","Включено / всего слоёв"):FText::FromString(Session->GetEditUnavailableReason());})
                                        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11)).ColorAndOpacity(Muted)]]]
                            +SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[Rows]]]]];
            }
            static const FButtonStyle HubStyle=FButtonStyle().SetNormal(FSlateRoundedBoxBrush(UIColor(TEXT("#173337")),68.f,Mint,2.f))
                .SetHovered(FSlateRoundedBoxBrush(UIColor(TEXT("#20464A")),68.f,Mint,3.f)).SetPressed(FSlateRoundedBoxBrush(UIColor(TEXT("#16393D")),68.f,Mint,3.f));
            const FVector2D Hub=Stages.Num()>4?FVector2D(357,357):HubPosition;
            Canvas->AddSlot().Offset(FMargin(Hub.X,Hub.Y,HubSize.X,HubSize.Y)).Alignment(FVector2D::ZeroVector)
                [SNew(SButton).ButtonStyle(&HubStyle).ContentPadding(FMargin(17)).HAlign(HAlign_Center).VAlign(VAlign_Center).ForegroundColor(FLinearColor::White)
                    .OnClicked_Lambda([this]{OnHub.ExecuteIfBound();return FReply::Handled();})
                    [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Session->GetActionLabel()).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),14)).Justification(ETextJustify::Center)]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)[SNew(STextBlock).Text(LOCTEXT("ActionHub","ДЕЙСТВИЕ")).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9)).ColorAndOpacity(Mint).Justification(ETextJustify::Center)]]];
            ChildSlot[SNew(SBorder).Padding(0).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(GraphFill).Clipping(EWidgetClipping::ClipToBounds)
                [SNew(SOverlay)+SOverlay::Slot()[SNew(SActionConnections).Graph(SharedThis(this))]+SOverlay::Slot()[Canvas]
                    +SOverlay::Slot()[SNew(SActionConnections).Graph(SharedThis(this)).PinsOnly(true)]]];
        }
        void Frame(){bFit=true;LastSize=FVector2D::ZeroVector;}
        virtual void Tick(const FGeometry& G,double T,float Dt)override
        {
            SCompoundWidget::Tick(G,T,Dt);const auto S=G.GetLocalSize();
            if(!bFit||S.X<2||S.Y<2||S.Equals(LastSize,1.f))return;
            const FVector2D Extent=Session->GetActionStages().Num()>4?FVector2D(850,850):GraphExtent;
            Zoom=float(FMath::Clamp(FMath::Min((S.X-16)/Extent.X,(S.Y-16)/Extent.Y),.1,1.25));
            Pan=(S-Extent*Zoom)*.5;LastSize=S;Invalidate(EInvalidateWidgetReason::Layout);
        }
        virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent& E)override
        {if(E.GetEffectingButton()==EKeys::RightMouseButton||E.GetEffectingButton()==EKeys::MiddleMouseButton){bPanning=true;bFit=false;return FReply::Handled().CaptureMouse(SharedThis(this));}return FReply::Unhandled();}
        virtual FReply OnMouseMove(const FGeometry&,const FPointerEvent& E)override
        {if(!bPanning)return FReply::Unhandled();Pan+=E.GetCursorDelta();Invalidate(EInvalidateWidgetReason::Layout);return FReply::Handled();}
        virtual FReply OnMouseButtonUp(const FGeometry&,const FPointerEvent&)override
        {if(!bPanning)return FReply::Unhandled();bPanning=false;return FReply::Handled().ReleaseMouseCapture();}
        virtual void OnMouseCaptureLost(const FCaptureLostEvent&)override{bPanning=false;}
        virtual FReply OnMouseWheel(const FGeometry& G,const FPointerEvent& E)override
        {const auto P=G.AbsoluteToLocal(E.GetScreenSpacePosition());const float NewZoom=FMath::Clamp(Zoom*FMath::Pow(1.12f,E.GetWheelDelta()),.4f,1.6f);Pan=P-(P-Pan)*(NewZoom/Zoom);Zoom=NewZoom;bFit=false;Invalidate(EInvalidateWidgetReason::Layout);return FReply::Handled();}
        TSharedPtr<FMCVFXActionSession> Session;FVector2D Pan=FVector2D::ZeroVector;float Zoom=1;
        virtual FReply OnDragOver(const FGeometry&,const FDragDropEvent& E)override
        {if(const auto Op=E.GetOperationAs<FWorkspaceEffectDrag>()){Op->Reject();return FReply::Handled();}return FReply::Unhandled();}
        virtual FReply OnDrop(const FGeometry&,const FDragDropEvent& E)override
        {if(const auto Op=E.GetOperationAs<FWorkspaceEffectDrag>()){Op->Reject();return FReply::Handled();}return FReply::Unhandled();}
    private:
        TAttribute<FName> SelectedStage;TAttribute<FGuid> SelectedLayer;FSelectWorkspaceLayer OnSelect;FSimpleDelegate OnHub;FDropWorkspaceEffect OnEffectDropped;
        bool bPanning=false,bFit=true;FVector2D LastSize=FVector2D::ZeroVector;
    };
    int32 SActionConnections::OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& O,int32 L,const FWidgetStyle&,bool)const
    {
        const auto P=Graph.Pin();if(!P)return L;
        if(!bPinsOnly)
        {
            const auto Size=G.GetLocalSize();const float Step=FMath::Max(8.f,32.f*P->Zoom);const auto Grid=UIColor(TEXT("#23303C")).CopyWithNewOpacity(.55f);
            for(float X=FMath::Fmod(P->Pan.X,Step);X<Size.X;X+=Step)FSlateDrawElement::MakeLines(O,L,G.ToPaintGeometry(),{FVector2D(X,0),FVector2D(X,Size.Y)},ESlateDrawEffect::None,Grid,false,1.f);
            for(float Y=FMath::Fmod(P->Pan.Y,Step);Y<Size.Y;Y+=Step)FSlateDrawElement::MakeLines(O,L,G.ToPaintGeometry(),{FVector2D(0,Y),FVector2D(Size.X,Y)},ESlateDrawEffect::None,Grid,false,1.f);
        }
        const auto Stages=P->Session->GetActionStages();const FVector2D Center=(Stages.Num()>4?FVector2D(357,357):HubPosition)+HubSize*.5;
        for(int32 I=0;I<Stages.Num();++I)
        {
            const auto* Recipe=P->Session->GetRecipe(Stages[I]);bool Active=false;if(Recipe)for(const auto& Layer:Recipe->Layers)Active|=Layer.bEnabled;
            if(!Active)continue;
            const FVector2D Size=StageDimensions(Recipe),Node=StagePosition(I,Size,Stages.Num())+Size*.5,Direction=(Node-Center).GetSafeNormal();
            const double ToEdge=FMath::Min((Size.X*.5)/FMath::Max(.0001,FMath::Abs(Direction.X)),(Size.Y*.5)/FMath::Max(.0001,FMath::Abs(Direction.Y)));
            const FVector2D ALocal=P->Pan+(Center+Direction*(HubSize.X*.5))*P->Zoom,BLocal=P->Pan+(Node-Direction*ToEdge)*P->Zoom;
            if(bPinsOnly)
            {
                static const FSlateRoundedBoxBrush Pin(FLinearColor::White,4.f,FLinearColor(.025f,.045f,.055f),1.f);
                for(const FVector2D& Point:{ALocal,BLocal})FSlateDrawElement::MakeBox(O,L+1,
                    G.ToPaintGeometry(FVector2f(FVector2D(8,8)*P->Zoom),FSlateLayoutTransform(FVector2f(Point-FVector2D(4,4)*P->Zoom))),&Pin,ESlateDrawEffect::None,StageColor(Stages[I]));
            }
            else FSlateDrawElement::MakeDrawSpaceSpline(O,L+1,G.LocalToAbsolute(ALocal),Direction*70*P->Zoom,G.LocalToAbsolute(BLocal),Direction*70*P->Zoom,2.f,ESlateDrawEffect::None,StageColor(Stages[I]));
        }
        return L+1;
    }

    class SActionWorkspace final : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SActionWorkspace){}SLATE_END_ARGS()
        void Construct(const FArguments&);
        virtual ~SActionWorkspace()override;
        void SetOwnerTab(const TSharedRef<SDockTab>& Tab){OwnerTab=Tab;}
        bool CanClose();
        void Closed();
        FString Snapshot()const;
        TSharedPtr<FMCVFXActionSession> GetSession()const{return Session;}
        bool ImportSavedEvent(FName ActionID,FName Stage,UMCVFXComposition* Source,FString& Error);
    private:
        TSharedPtr<FMCVFXActionSession> Session;
        TSharedPtr<SWidget> NativePreview;
        TSharedPtr<SBox> PreviewHost,GraphHost;
        TSharedPtr<SActionStageGraph> Graph;
        TSharedPtr<SVerticalBox> Details;
        TSharedPtr<FAssetThumbnailPool> GalleryPool;
        TSharedPtr<STileView<FGalleryItem>> GalleryView;
        TSharedPtr<SEditableTextBox> GallerySearchBox;
        TArray<FAssetData> GalleryCatalog;
        TArray<FGalleryItem> GalleryItems;
        TArray<FCatalogItem> Catalogue;
        TSharedPtr<SListView<FCatalogItem>> CatalogueView;
        TWeakPtr<SDockTab> OwnerTab;
        FTSTicker::FDelegateHandle TickHandle;
        FAssetData GallerySelection;
        FName SelectedAction=TEXT("Consumable_Rocket"),SelectedStage=TEXT("Start");
        EMCPerkPreviewScenario SelectedScenario=EMCPerkPreviewScenario::Target;
        FGuid SelectedLayer;
        FString GalleryQuery,Status;
        TMap<FName,FGalleryThumbnailEvidence> GalleryThumbnailEvidence;
        TMap<uint32,TSet<FName>> GalleryThumbnailSources;
        uint64 SeenRevision=MAX_uint64;
        bool bUnsupported=false,bShowActionDetails=false,bLoop=true,bLive=true,bPlaying=false,bObserved=false,bPaused=false,bAudition=false,bPendingGalleryAudition=false;
        double RepeatAt=0;
        bool TickWorkspace(float);
        void SelectAction(FCatalogItem Item);
        void SelectLayer(FName Stage,FGuid ID);
        void SelectGallery(FGalleryItem Item);
        void Refresh();
        void RefreshDetails();
        void RefreshGallery(const TCHAR* Path=nullptr);
        TSharedRef<SWidget> GalleryImage(const FAssetData& Asset);
        void AddSelected();
        void DropEffect(FName Stage,const FAssetData& Asset);
        FReply BeginGalleryDrag(const FAssetData& Asset);
        void NewCompound();
        TSharedRef<SWidget> CompoundMenu();
        void ImportCompound(const UMCVFXRecipePreset* Recipe);
        void LoadStarter(const FString& Name);
        void OpenCompound();
        void Apply();
        void Play(bool Gallery=false);
        void Stop();
        void EditLayer(TFunction<void(FMCVFXCompositionLayer&)> Edit);
        void MutateLayer(FName Stage,FGuid ID,TFunction<void(FMCVFXCompositionLayer&)> Edit);
        void CommitColor(FLinearColor Value,FName Stage,FGuid ID,int32 Index);
        FString ActionTitle()const;
        void Fail(const FString& Error){Status=TEXT("Ошибка: ")+Error;}
        bool CanPlay()const{return !bUnsupported&&Session&&Session->IsContextCurrent();}
        bool CanEdit()const{return CanPlay()&&Session->CanEditProfile();}
        FString AuthoringUnavailableReason()const
        {if(bUnsupported)return TEXT("Для этого события пока доступен каталог метаданных; адаптер предпросмотра ещё не подключён.");
            return Session?Session->GetEditUnavailableReason():TEXT("Выберите действие, чтобы открыть его контекст.");}
        const FMCVFXCompositionLayer* CurrentLayer()const;
    };
    TWeakPtr<SActionWorkspace> ActiveWorkspace;
}

namespace
{
    void SActionWorkspace::Construct(const FArguments&)
    {
        Session=MakeShared<FMCVFXActionSession>();
        FString Error;const bool Ready=Session->Initialize(SelectedAction,Error);Status=Ready?TEXT("Выберите событие действия в центре. Клик по галерее — только просмотр; «Добавить» включает слой в черновик."):TEXT("Ошибка: ")+Error;
        NativePreview=Ready?Session->MakePreviewWidget():Label(Error,14);
        for(const auto& Entry:FMCVFXActionSession::GetCatalogue())Catalogue.Add(MakeShared<FMCVFXActionCatalogueEntry>(Entry));
        GalleryPool=MakeShared<FAssetThumbnailPool>(24);RefreshGallery(TEXT("/Game/Gameplay/ActiveSkills/VFX"));
        ChildSlot[SNew(SBorder).Padding(0).BorderImage(&PanelBrush()).BorderBackgroundColor(AppFill)[SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(10,8)[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(TEXT("VFX Composer  /  ")+ActionTitle());}).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),18))]
                +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SComboButton).ButtonStyle(&SolidStyle()).ContentPadding(FMargin(14,10))
                    .ButtonColorAndOpacity_Lambda([this]{return CanEdit()?Mint:HeadingFill;}).ForegroundColor_Lambda([this]{return FSlateColor(CanEdit()?Ink:Muted);})
                    .IsEnabled_Lambda([this]{return CanEdit();}).ToolTipText_Lambda([this]{return CanEdit()?LOCTEXT("NewCompoundHint","Создать композицию выбранного события: пустую или из готового шаблона. Ctrl+Z возвращает предыдущие слои."):FText::FromString(AuthoringUnavailableReason());})
                    .OnGetMenuContent_Lambda([this]{return CompoundMenu();}).ButtonContent()[SNew(STextBlock).Text(LOCTEXT("NewCompoundButton","＋ Новая композиция")).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),13))]]
                +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(&SolidStyle()).ContentPadding(FMargin(14,10))
                    .ButtonColorAndOpacity_Lambda([this]{return CanEdit()?ButtonFill:HeadingFill;})
                    .IsEnabled_Lambda([this]{return CanEdit();}).ToolTipText_Lambda([this]{return CanEdit()?LOCTEXT("ApplyAllHint","Сохранить композиции с независимыми Niagara-слоями и назначить новый VFX-профиль действию в игре. Исходные эффекты не перезаписываются."):FText::FromString(AuthoringUnavailableReason());})
                    .OnClicked_Lambda([this]{Apply();return FReply::Handled();})[Label(TEXT("Применить в игре"),13)]]]
            +SVerticalBox::Slot().AutoHeight().Padding(12,0,12,8)[SNew(STextBlock).Text_Lambda([this]{
                if(!CanPlay())return FText::FromString(bUnsupported?TEXT("Каталог событий"):TEXT("Контекст изменился · выберите действие заново"));
                return FText::FromString(FString(!CanEdit()?TEXT("Просмотр механики  ·  "):Session->IsDirty()?TEXT("● Черновик изменён  ·  "):TEXT("Рабочая копия  ·  "))+TEXT("Выбрано событие: ")+Session->GetEventLabel(SelectedStage).ToString());}).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12)).ColorAndOpacity(Muted)]
            +SVerticalBox::Slot().FillHeight(1).Padding(8,0)[SNew(SSplitter)
                +SSplitter::Slot().Value(.24f)[SNew(SSplitter).Orientation(Orient_Vertical)
                    +SSplitter::Slot().Value(.50f)[SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight()[SNew(SBorder).Padding(9,5).BorderImage(&PanelBrush()).BorderBackgroundColor(HeadingFill)
                            .ToolTipText_Lambda([this]{return CanEdit()?LOCTEXT("PreviewModeHint","Примерка временно добавляет эффект галереи к выбранному событию. В черновик он попадёт только после «Добавить»."):FText::FromString(AuthoringUnavailableReason());})
                            [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(!CanPlay()?TEXT("Предпросмотр недоступен"):!CanEdit()?TEXT("Механика · просмотр"):bAudition?TEXT("Примерка — не добавлено"):TEXT("Черновик события"));}).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),12)).ColorAndOpacity(Mint)]]
                        +SVerticalBox::Slot().FillHeight(1)[SAssignNew(PreviewHost,SBox)[NativePreview.ToSharedRef()]]
                        +SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)[SNew(SButton).ButtonStyle(&SolidStyle()).ContentPadding(FMargin(7,8)).ButtonColorAndOpacity_Lambda([this]{return CanPlay()?Mint:HeadingFill;})
                                .ForegroundColor_Lambda([this]{return FSlateColor(CanPlay()?Ink:Muted);}).IsEnabled_Lambda([this]{return CanPlay();})
                                .ToolTipText(LOCTEXT("PlayHint","Полностью выполнить настоящее действие со всеми черновиками событий."))
                                .OnClicked_Lambda([this]{Play();return FReply::Handled();})[SNew(STextBlock).Text(LOCTEXT("PlayAction","▶ Играть")).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),12))]]
                            +SHorizontalBox::Slot().AutoWidth().Padding(0,0,4,0)[Command(TEXT("Ⅱ"),[this]{if(!CanPlay())return;bPaused=!bPaused;Session->Pause(bPaused);})]
                            +SHorizontalBox::Slot().AutoWidth()[Command(TEXT("■"),[this]{Stop();})]]
                        +SVerticalBox::Slot().AutoHeight().Padding(4,0,4,5)[SNew(SComboButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(8,6))
                            .IsEnabled_Lambda([this]{return CanPlay();})
                            .OnGetMenuContent_Lambda([this]()->TSharedRef<SWidget>
                            {
                                auto Menu=SNew(SVerticalBox);
                                for(auto Scenario:Session->GetScenarios())Menu->AddSlot().AutoHeight()[Command(Session->GetScenarioLabel(Scenario).ToString(),[this,Scenario]{Stop();SelectedScenario=Scenario;FSlateApplication::Get().DismissAllMenus();})];
                                return Menu;
                            }).ButtonContent()[SNew(STextBlock).Text_Lambda([this]{return Session->GetScenarioLabel(SelectedScenario);}).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11)).ColorAndOpacity(TextColor)]]
                        +SVerticalBox::Slot().AutoHeight().Padding(6,0,6,6)[SNew(SCheckBox).IsChecked_Lambda([this]{return bLoop?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                            .OnCheckStateChanged_Lambda([this](ECheckBoxState S){bLoop=S==ECheckBoxState::Checked;RepeatAt=0;})[Label(TEXT("Повтор действия"),11)]]]
                    +SSplitter::Slot().Value(.50f)[SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight()[Heading(TEXT("ДЕЙСТВИЯ И СОБЫТИЯ"))]
                        +SVerticalBox::Slot().FillHeight(1)[SAssignNew(CatalogueView,SListView<FCatalogItem>).ListItemsSource(&Catalogue).SelectionMode(ESelectionMode::Single)
                            .OnGenerateRow_Lambda([this](FCatalogItem Item,const TSharedRef<STableViewBase>& Owner)->TSharedRef<ITableRow>
                            {return SNew(STableRow<FCatalogItem>,Owner).Padding(4)[SNew(SBorder).Padding(10).BorderImage_Lambda([this,Item]{return SelectedAction==Item->ID?&SelectedPanelBrush():&PanelBrush();})
                                .BorderBackgroundColor_Lambda([this,Item]{return SelectedAction==Item->ID?UIColor(TEXT("#1B3D3F")):Surface;})
                                [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Item->Label).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),12)).ColorAndOpacity(TextColor)]
                                    +SVerticalBox::Slot().AutoHeight().Padding(0,4)[Label(Item->bSupportsPreview?(Item->bSupportsProfileOverride?TEXT("Сценарий + редактирование VFX"):TEXT("Сценарий · VFX-привязка впереди")):TEXT("Метаданные · адаптер впереди"),10,Item->bSupportsPreview?Mint:Muted)]]];})
                            .OnSelectionChanged_Lambda([this](FCatalogItem Item,ESelectInfo::Type){if(Item)SelectAction(Item);})]]]
                +SSplitter::Slot().Value(.45f)[SNew(SVerticalBox)
                    +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                        +SHorizontalBox::Slot().FillWidth(1)[Heading(TEXT("ДЕЙСТВИЕ → СОБЫТИЯ → СЛОИ"))]
                        +SHorizontalBox::Slot().AutoWidth()[Command(TEXT("Вписать"),[this]{if(Graph)Graph->Frame();})]]
                    +SVerticalBox::Slot().FillHeight(1)[SAssignNew(GraphHost,SBox)
                        [SAssignNew(Graph,SActionStageGraph).Session(Session).SelectedStage_Lambda([this]{return SelectedStage;}).SelectedLayer_Lambda([this]{return SelectedLayer;})
                            .OnSelect_Lambda([this](FName Stage,FGuid ID){SelectLayer(Stage,ID);}).OnHub_Lambda([this]{bShowActionDetails=true;SelectedLayer.Invalidate();RefreshDetails();})
                            .OnEffectDropped_Lambda([this](FName Stage,FAssetData Asset){DropEffect(Stage,Asset);})]]
                    +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text_Lambda([this]{return CanEdit()?LOCTEXT("GraphNavigation","1 Событие → 2 Примерить → 3 Добавить → 4 Применить"):
                        FText::FromString(CanPlay()?TEXT("Реальный сценарий · Niagara-слои пока не редактируются"):TEXT("Выберите действие, чтобы восстановить контекст"));})
                        .ToolTipText_Lambda([this]{return CanEdit()?LOCTEXT("GraphMeaning","ПКМ — панорама; колесо — масштаб. Выберите событие, затем слой. Провода показывают события одного настоящего действия; они не создают новую игровую механику."):FText::FromString(AuthoringUnavailableReason());})
                        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10)).ColorAndOpacity(Muted)]]
                +SSplitter::Slot().Value(.31f)[SNew(SSplitter).Orientation(Orient_Vertical)
                    +SSplitter::Slot().Value(.45f)[SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight()[SNew(SBorder).Padding(9,5).BorderImage(&PanelBrush()).BorderBackgroundColor(HeadingFill)
                            .ToolTipText_Lambda([this]{return CanEdit()?LOCTEXT("GalleryContextHint","Галерея эффектов. Клик — примерка для выбранного события, кнопка «Добавить» — новый слой его черновика."):FText::FromString(AuthoringUnavailableReason());})
                            [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(TEXT("Для события: ")+Session->GetEventLabel(SelectedStage).ToString());}).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),13)).ColorAndOpacity(Mint)]]
                        +SVerticalBox::Slot().AutoHeight().Padding(3)[SAssignNew(GallerySearchBox,SEditableTextBox).HintText(LOCTEXT("Search","Найти эффект…")).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12)).Padding(FMargin(7,5))
                            .OnTextChanged_Lambda([this](const FText& T){GalleryQuery=T.ToString();RefreshGallery();})]
                        +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1).Padding(3)[SmallCommand(TEXT("Игра"),[this]{RefreshGallery(TEXT("/Game/Gameplay/ActiveSkills/VFX"));})]
                            +SHorizontalBox::Slot().FillWidth(1).Padding(3)[SmallCommand(TEXT("Учебные"),[this]{RefreshGallery(TEXT("/Game/Tests/VFXPractice/Effects"));})]
                            +SHorizontalBox::Slot().FillWidth(1).Padding(3)[SmallCommand(TEXT("Все"),[this]{RefreshGallery(TEXT("/Game"));})]]
                        +SVerticalBox::Slot().FillHeight(1)[SAssignNew(GalleryView,STileView<FGalleryItem>).ListItemsSource(&GalleryItems).SelectionMode(ESelectionMode::Single).ItemWidth(126.f).ItemHeight(126.f)
                            .OnGenerateTile_Lambda([this](FGalleryItem Item,const TSharedRef<STableViewBase>& Owner)->TSharedRef<ITableRow>
                            {return SNew(STableRow<FGalleryItem>,Owner).Padding(4).SignalSelectionMode(ETableRowSignalSelectionMode::Deferred)
                                .OnDragDetected_Lambda([this,Item](const FGeometry&,const FPointerEvent&){return BeginGalleryDrag(*Item);})
                                [SNew(SBorder).Padding(5).BorderImage_Lambda([this,Item]{return GallerySelection.PackageName==Item->PackageName?&SelectedPanelBrush():&PanelBrush();})
                                    .BorderBackgroundColor_Lambda([this,Item]{return GallerySelection.PackageName==Item->PackageName?UIColor(TEXT("#1B3D3F")):Surface;})
                                    .ToolTipText(FText::FromString(Item->GetSoftObjectPath().ToString()))
                                    [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(64)[GalleryImage(*Item)]]
                                        +SVerticalBox::Slot().AutoHeight().Padding(0,4)[SNew(SBox).HeightOverride(28)[Label(EffectLabel(Item->AssetName.ToString()),10)]]
                                        +SVerticalBox::Slot().AutoHeight()[Label(ProvenanceLabel(*Item),9,Muted)]]];})
                            .OnSelectionChanged_Lambda([this](FGalleryItem Item,ESelectInfo::Type){if(Item)SelectGallery(Item);})]
                        +SVerticalBox::Slot().AutoHeight().Padding(6,3)[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1)[SNew(SCheckBox).IsEnabled_Lambda([this]{return CanEdit();}).IsChecked_Lambda([this]{return bLive?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                                .OnCheckStateChanged_Lambda([this](ECheckBoxState S){if(!CanEdit()){Status=AuthoringUnavailableReason();return;}bLive=S==ECheckBoxState::Checked;if(bLive&&GallerySelection.IsValid())Play(true);else if(!bLive&&bAudition)Stop();})[Label(TEXT("Клик — просмотр"),10)]]
                            +SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(FString::Printf(TEXT("%d NS"),GalleryItems.Num()));}).ColorAndOpacity(Muted)]]
                        +SVerticalBox::Slot().AutoHeight().Padding(5)[SNew(SButton).ButtonStyle(&SolidStyle()).ContentPadding(FMargin(10,6))
                            .ButtonColorAndOpacity_Lambda([this]{return CanEdit()&&GallerySelection.IsValid()?Mint:HeadingFill;}).ForegroundColor_Lambda([this]{return FSlateColor(CanEdit()&&GallerySelection.IsValid()?Ink:Muted);})
                            .ToolTipText_Lambda([this]{return CanEdit()?FText::FromString(TEXT("Добавить выбранную Niagara новым слоем события.")):FText::FromString(AuthoringUnavailableReason());})
                            .IsEnabled_Lambda([this]{return CanEdit()&&GallerySelection.IsValid();}).OnClicked_Lambda([this]{AddSelected();return FReply::Handled();})
                            [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(!CanEdit()?(CanPlay()?TEXT("Niagara-привязка пока недоступна"):TEXT("Откройте контекст действия")):
                                GallerySelection.IsValid()?TEXT("＋ Добавить в «")+Session->GetEventLabel(SelectedStage).ToString()+TEXT("»"):TEXT("Выберите эффект в галерее"));}).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),12)).AutoWrapText(true)]]]
                    +SSplitter::Slot().Value(.55f)[SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight()[Heading(TEXT("НАСТРОЙКИ ВЫБРАННОГО СЛОЯ"))]
                        +SVerticalBox::Slot().FillHeight(1)[SNew(SBorder).Padding(0).BorderImage(&PanelBrush()).BorderBackgroundColor(PanelFill)
                            [SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(Details,SVerticalBox)]]]]]]
            +SVerticalBox::Slot().AutoHeight().Padding(10,7)[SNew(SBox).MaxDesiredHeight(58).Clipping(EWidgetClipping::ClipToBounds)
                [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Status);}).ToolTipText_Lambda([this]{return FText::FromString(Status);})
                    .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11)).AutoWrapText(true)
                    .WrapTextAt_Lambda([this]{return FMath::Max(200.f,float(GetCachedGeometry().GetLocalSize().X)-24.f);})
                    .ColorAndOpacity_Lambda([this]{return Status.StartsWith(TEXT("Ошибка:"))||Status.StartsWith(TEXT("Не применено:"))?FLinearColor(1.f,.56f,.47f):Muted;})]]]];
        Refresh();TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(this,&SActionWorkspace::TickWorkspace),.1f);
        if(CatalogueView)for(const auto& Entry:Catalogue)if(Entry->ID==SelectedAction){CatalogueView->SetSelection(Entry,ESelectInfo::Direct);break;}
    }

    SActionWorkspace::~SActionWorkspace(){FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);Stop();}
    bool SActionWorkspace::CanClose()
    {
        if(!Session||!Session->IsDirty())return true;
        return FMessageDialog::Open(EAppMsgType::YesNo,LOCTEXT("CloseDraft","Черновики событий ещё не применены в игре. Закрыть рабочую область без применения?"))==EAppReturnType::Yes;
    }
    void SActionWorkspace::Closed(){Stop();}
    FString SActionWorkspace::ActionTitle()const
    {for(const auto& Entry:Catalogue)if(Entry->ID==SelectedAction)return Entry->Label.ToString();return SelectedAction.ToString();}
    void SActionWorkspace::SelectAction(FCatalogItem Item)
    {
        if(Item && SelectedAction!=Item->ID && Session->IsDirty() && FMessageDialog::Open(EAppMsgType::YesNo,
            LOCTEXT("SwitchUnsavedAction","В текущем действии есть неприменённые слои. Перейти к другому действию и отбросить этот черновик?"))!=EAppReturnType::Yes)return;
        if(!Item)return;Stop();SelectedAction=Item->ID;bUnsupported=!Item->bSupportsPreview;bShowActionDetails=true;SelectedLayer.Invalidate();GallerySelection=FAssetData();
        if(bUnsupported)
        {
            Session->Clear();SelectedStage=NAME_None;
            PreviewHost->SetContent(Label(TEXT("Полный предпросмотр ещё не подключён для «")+Item->Label.ToString()+TEXT("».\n\nЭто реальные игровые данные; ракетница не подставляется вместо этого действия."),14,Muted));
            GraphHost->SetContent(SNew(SBorder).Padding(24)[SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,15)[Label(Item->Label.ToString(),20)]
                +SVerticalBox::Slot().AutoHeight()[Label(Item->Description.ToString(),14)]
                +SVerticalBox::Slot().AutoHeight().Padding(0,20)[Label(TEXT("Редактор событий и полный игровой сценарий появятся после подключения адаптера. Просмотр метаданных доступен; применение и запуск выключены."),12,Muted)]]);
            Status=TEXT("Каталог: ")+Item->Label.ToString()+TEXT(". Адаптер полного действия пока не подключён.");
        }
        else
        {
            if(Session->GetActionID()!=Item->ID||!Session->IsContextCurrent())
            {
                FString Error;if(!Session->Initialize(Item->ID,Error))
                {
                    Fail(Error);bUnsupported=true;
                    PreviewHost->SetContent(Label(Error,14,Muted));GraphHost->SetContent(Label(TEXT("Сценарий не открыт. ")+Error,14,Muted));
                    RefreshDetails();return;
                }
            }
            const auto Stages=Session->GetActionStages();if(!Stages.Contains(SelectedStage))SelectedStage=Stages.IsEmpty()?NAME_None:Stages[0];
            if(!Session->GetScenarios().Contains(SelectedScenario))SelectedScenario=EMCPerkPreviewScenario::Target;
            NativePreview=Session->MakePreviewWidget();PreviewHost->SetContent(NativePreview.ToSharedRef());GraphHost->SetContent(Graph.ToSharedRef());
            Graph->Frame();
            Status=Session->CanEditProfile()?TEXT("Перетащите эффект из галереи на карточку события. Он добавится новым слоем. «Применить» сохраняет результат в игру."):Session->GetEditUnavailableReason();
        }
        Refresh();
    }
    void SActionWorkspace::SelectLayer(FName Stage,FGuid ID)
    {SelectedStage=Stage;SelectedLayer=ID;bShowActionDetails=false;RefreshDetails();}
    void SActionWorkspace::SelectGallery(FGalleryItem Item)
    {
        if(!Item)return;Stop();GallerySelection=*Item;
        if(!CanEdit()){Status=TEXT("Выбран ")+EffectLabel(Item->AssetName.ToString())+TEXT(". ")+AuthoringUnavailableReason();return;}
        // Slate may signal deferred row selection immediately before OnDragDetected.
        // Audition only on the next tick; BeginGalleryDrag cancels this pending click.
        bPendingGalleryAudition=bLive&&CanEdit();
        Status=TEXT("Выбран ")+EffectLabel(Item->AssetName.ToString())+TEXT(". Клик — примерка, перетащите на событие или нажмите «Добавить».");
    }
    FReply SActionWorkspace::BeginGalleryDrag(const FAssetData& Asset)
    {
        Stop();GallerySelection=Asset;
        if(!CanEdit()){Status=AuthoringUnavailableReason();return FReply::Handled();}
        Status=TEXT("Перетащите «")+EffectLabel(Asset.AssetName.ToString())+TEXT("» на карточку события. Будет добавлен новый слой; Esc — отмена.");
        return FReply::Handled().BeginDragDrop(FWorkspaceEffectDrag::New(Asset));
    }
    void SActionWorkspace::DropEffect(FName Stage,const FAssetData& Asset)
    {
        if(!CanEdit())return;Stop();FString Error;
        const FGuid ID=Session->AddSystem(Stage,Cast<UNiagaraSystem>(Asset.GetAsset()),Error);
        if(!ID.IsValid()){Fail(Error);return;}
        SelectedStage=Stage;SelectedLayer=ID;bShowActionDetails=false;GallerySelection=Asset;Refresh();
        Status=TEXT("Добавлен слой в «")+Session->GetEventLabel(Stage).ToString()+TEXT("». Нажмите ▶ Играть для просмотра. Ctrl+Z — отмена; «Применить» — сохранить в игру.");
    }
    void SActionWorkspace::RefreshGallery(const TCHAR* Path)
    {
        Stop();GallerySelection=FAssetData();if(GalleryView)GalleryView->ClearSelection();
        if(Path)
        {FARFilter Filter;Filter.ClassPaths.Add(UNiagaraSystem::StaticClass()->GetClassPathName());Filter.bRecursiveClasses=true;Filter.PackagePaths.Add(FName(Path));Filter.bRecursivePaths=true;
            GalleryCatalog.Reset();FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssets(Filter,GalleryCatalog);
            GalleryCatalog.Sort([](const FAssetData& A,const FAssetData& B){return A.AssetName.LexicalLess(B.AssetName);});}
        GalleryItems.Reset();for(const auto& Asset:GalleryCatalog)if(GalleryQuery.IsEmpty()||EffectLabel(Asset.AssetName.ToString()).Contains(GalleryQuery)||Asset.AssetName.ToString().Contains(GalleryQuery)||Asset.PackageName.ToString().Contains(GalleryQuery))GalleryItems.Add(MakeShared<FAssetData>(Asset));
        if(GalleryView)GalleryView->RequestListRefresh();
    }
    TSharedRef<SWidget> SActionWorkspace::GalleryImage(const FAssetData& Asset)
    {
        FGalleryThumbnailEvidence* Evidence=GalleryThumbnailEvidence.Find(Asset.PackageName);
        if(!Evidence)
        {
            FGalleryThumbnailEvidence NewEvidence;FObjectThumbnail Saved;
            // Read thumbnail bytes for visible tiles only. Do not load every Niagara
            // system just to manufacture a preview from its default/template emitter.
            if(ThumbnailTools::LoadThumbnailFromPackage(Asset,Saved)&&!Saved.IsEmpty()&&Saved.HasValidImageData())
            {
                const auto& Pixels=Saved.GetUncompressedImageData();NewEvidence.bHasSavedImage=!Pixels.IsEmpty();
                if(NewEvidence.bHasSavedImage)NewEvidence.Hash=HashCombine(FCrc::MemCrc32(Pixels.GetData(),Pixels.Num()),GetTypeHash(Saved.GetImageWidth()));
            }
            Evidence=&GalleryThumbnailEvidence.Add(Asset.PackageName,NewEvidence);
            if(Evidence->bHasSavedImage)GalleryThumbnailSources.FindOrAdd(Evidence->Hash).Add(Asset.PackageName);
        }
        const bool HasImage=Evidence->bHasSavedImage;const uint32 Hash=Evidence->Hash;
        auto SharedImage=[this,HasImage,Hash]{const auto* Sources=GalleryThumbnailSources.Find(Hash);return HasImage&&Sources&&Sources->Num()>1;};
        const auto Group=NeutralGroup(Asset);static const FSlateRoundedBoxBrush Badge(FLinearColor::White,6.f);
        TSharedRef<SWidget> Main=Label(TEXT("")),Small=Label(TEXT(""));
        if(HasImage)
        {
            auto Thumbnail=MakeShared<FAssetThumbnail>(Asset,106,64,GalleryPool);FAssetThumbnailConfig Config;Config.bAllowRealTimeOnHovered=false;Config.bAllowHintText=false;
            Main=Thumbnail->MakeThumbnailWidget(Config);Small=Thumbnail->MakeThumbnailWidget(Config);
        }
        TSharedRef<SWidget> Identity=SNew(SOverlay)
            .ToolTipText(LOCTEXT("NeutralGalleryIcon","Нейтральный значок группы, а не изображение эффекта. Сохранённый кадр отсутствует или одинаков у нескольких ресурсов. Настоящий вид проверяйте кликом в предпросмотре; маленький кадр — исходный thumbnail."))
            +SOverlay::Slot()[SNew(SBorder).Padding(5).BorderImage(&Badge).BorderBackgroundColor(Group.Value*.22f).HAlign(HAlign_Center).VAlign(VAlign_Center)
                [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(Group.Key)).Justification(ETextJustify::Center).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),20)).ColorAndOpacity(Group.Value)]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,3)[SNew(STextBlock).Text(LOCTEXT("GroupIcon","группа VFX")).Justification(ETextJustify::Center).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),8)).ColorAndOpacity(Muted)]]]
            +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom)[SNew(SBox).WidthOverride(24).HeightOverride(24).Visibility_Lambda([SharedImage]{return SharedImage()?EVisibility::Visible:EVisibility::Collapsed;})[Small]];
        return SNew(SWidgetSwitcher).WidgetIndex_Lambda([HasImage,SharedImage]{return HasImage&&!SharedImage()?0:1;})
            +SWidgetSwitcher::Slot()[Main]+SWidgetSwitcher::Slot()[Identity];
    }
    void SActionWorkspace::Refresh(){SeenRevision=Session->GetRevision();if(Graph)Graph->Rebuild();RefreshDetails();}
    const FMCVFXCompositionLayer* SActionWorkspace::CurrentLayer()const
    {const auto* R=Session?Session->GetRecipe(SelectedStage):nullptr;const int32 I=R?R->FindNode(SelectedLayer):INDEX_NONE;return I!=INDEX_NONE?&R->Layers[I]:nullptr;}
    void SActionWorkspace::AddSelected()
    {
        if(!CanEdit()||!GallerySelection.IsValid())return;Stop();FString Error;
        auto* NS=Cast<UNiagaraSystem>(GallerySelection.GetAsset());const FGuid ID=Session->AddSystem(SelectedStage,NS,Error);
        if(!ID.IsValid()){Fail(Error);return;}SelectedLayer=ID;bShowActionDetails=false;Refresh();Status=TEXT("Слой добавлен в «")+Session->GetEventLabel(SelectedStage).ToString()+TEXT("». Это черновик; игра изменится только после «Применить в игре».");
    }
    void SActionWorkspace::NewCompound()
    {if(!CanEdit())return;Stop();FString Error;if(!Session->NewCompound(SelectedStage,Error)){Fail(Error);return;}SelectedLayer.Invalidate();bShowActionDetails=false;Refresh();Status=TEXT("Новая пустая композиция события. Добавляйте слои из галереи; Ctrl+Z вернёт предыдущий черновик.");}
    void SActionWorkspace::Apply()
    {if(!CanEdit())return;Stop();FString Error;const bool OK=Session->ApplyAll(Error);Refresh();Status=OK?Session->GetStatus():TEXT("Не применено: ")+Error;}
    void SActionWorkspace::Play(bool Gallery)
    {
        if(!CanPlay())return;if(Gallery&&!CanEdit()){Fail(Session->GetEditUnavailableReason());return;}const bool WasPlaying=bPlaying;Stop();FString Error;bool OK=false;
        if(!WasPlaying)Session->SetPreviewView(Gallery&&(SelectedStage==TEXT("Start")||SelectedStage==TEXT("End"))?1:0);
        if(Gallery&&GallerySelection.IsValid())OK=Session->AuditionNS(SelectedStage,Cast<UNiagaraSystem>(GallerySelection.GetAsset()),Error,SelectedScenario);
        else OK=Session->PlayFullAction(Error,SelectedScenario);
        if(!OK){Fail(Error);return;}bPlaying=true;bObserved=false;bAudition=Gallery;RepeatAt=0;
        Status=Gallery?TEXT("Примерка: ")+EffectLabel(GallerySelection.AssetName.ToString())+TEXT(". Нажмите «Добавить», чтобы оставить этот слой."):
            CanEdit()?TEXT("Проигрывается черновик события. Игровой профиль не изменён."):TEXT("Выполняется реальный сценарий с родным представлением механики. Редактирование Niagara-слоёв ещё не подключено.");
    }
    void SActionWorkspace::Stop(){bPlaying=false;bObserved=false;bPaused=false;bAudition=false;bPendingGalleryAudition=false;RepeatAt=0;if(Session)Session->Stop();}
    bool SActionWorkspace::TickWorkspace(float)
    {
        if(Session&&SeenRevision!=Session->GetRevision()){Status=Session->GetStatus();Refresh();}
        if(bPendingGalleryAudition)
        {
            bPendingGalleryAudition=false;const auto Tab=OwnerTab.Pin();const auto Window=FSlateApplication::Get().FindWidgetWindow(AsShared());
            if(!FSlateApplication::Get().IsDragDropping()&&Tab&&Tab->IsForeground()&&Window&&Window->IsVisible()&&!Window->IsWindowMinimized())Play(true);
        }
        if(!bPlaying&&!bPaused)return true;
        const auto Tab=OwnerTab.Pin();const auto Window=FSlateApplication::Get().FindWidgetWindow(AsShared());
        if(!Tab||!Tab->IsForeground()||!Window||!Window->IsVisible()||Window->IsWindowMinimized()||!CanPlay()){Stop();return true;}
        if(bPaused)return true;const auto Runner=MCPerkActionPreviewPanel::GetActiveRunner();if(!Runner){Stop();return true;}
        if(Runner->IsRunning()){bObserved=true;RepeatAt=0;return true;}
        const auto& Events=Runner->GetEvents();
        const bool Complete=Events.ContainsByPredicate([](const FMCPerkPreviewEvent& E){return E.Stage==TEXT("ActionFinished");});
        const bool SafetyStop=Events.ContainsByPredicate([](const FMCPerkPreviewEvent& E){return E.Stage==TEXT("PreviewTailSafetyLimit");});
        if(!Complete || SafetyStop){bPlaying=false;RepeatAt=0;Status=TEXT("Предпросмотр остановлен: ")+Runner->GetStatus();return true;}
        if(!bObserved)return true;if(!bLoop){bPlaying=false;return true;}
        const double Now=FPlatformTime::Seconds();if(RepeatAt==0){RepeatAt=Now+.6;return true;}if(Now>=RepeatAt)Play(bAudition);return true;
    }
    void SActionWorkspace::EditLayer(TFunction<void(FMCVFXCompositionLayer&)> Edit)
    {MutateLayer(SelectedStage,SelectedLayer,MoveTemp(Edit));}
    void SActionWorkspace::MutateLayer(FName Stage,FGuid ID,TFunction<void(FMCVFXCompositionLayer&)> Edit)
    {
        if(!CanEdit())return;const auto* Recipe=Session->GetRecipe(Stage);const int32 Index=Recipe?Recipe->FindNode(ID):INDEX_NONE;
        if(Index==INDEX_NONE)return;auto Copy=Recipe->Layers[Index];Edit(Copy);Stop();FString Error;
        if(!Session->SetLayer(Stage,ID,Copy,Error))Fail(Error);else Status=TEXT("Черновик слоя изменён. Проиграйте действие, чтобы оценить результат.");
    }
    void SActionWorkspace::CommitColor(FLinearColor Value,FName Stage,FGuid ID,int32 Index)
    {
        if(!CanEdit())return;const auto* Recipe=Session->GetRecipe(Stage);const int32 I=Recipe?Recipe->FindNode(ID):INDEX_NONE;
        if(I==INDEX_NONE||!Recipe->Layers[I].Colors.IsValidIndex(Index))return;auto Copy=Recipe->Layers[I];Copy.Colors[Index].Value=Value;Copy.Colors[Index].bOverride=true;
        FString Error;Stop();if(!Session->SetLayer(Stage,ID,Copy,Error))Fail(Error);
    }
    TSharedRef<SWidget> SActionWorkspace::CompoundMenu()
    {
        TSharedRef<SVerticalBox> Menu=SNew(SVerticalBox);
        Menu->AddSlot().AutoHeight().Padding(6)[Label(TEXT("Для события «")+Session->GetEventLabel(SelectedStage).ToString()+TEXT("»"),13)];
        Menu->AddSlot().AutoHeight().Padding(8,3,8,8)[SNew(SBox).WidthOverride(310)[Label(TEXT("Новая композиция заменит черновик этого события. Ctrl+Z — вернуть."),11,Muted)]];
        Menu->AddSlot().AutoHeight()[Command(TEXT("Пустая композиция"),[this]{FSlateApplication::Get().DismissAllMenus();NewCompound();})];
        const TPair<FString,FString> Templates[]={{TEXT("Flash"),TEXT("Вспышка")},{TEXT("Smoke"),TEXT("Дым")},{TEXT("Trail"),TEXT("След")},{TEXT("Glow"),TEXT("Свечение")}};
        for(const auto& Template:Templates)Menu->AddSlot().AutoHeight()[Command(Template.Value,[this,Name=Template.Key]{FSlateApplication::Get().DismissAllMenus();LoadStarter(Name);})];
        Menu->AddSlot().AutoHeight()[Command(TEXT("Из сохранённого пресета…"),[this]{FSlateApplication::Get().DismissAllMenus();OpenCompound();})];
        return Menu;
    }
    void SActionWorkspace::ImportCompound(const UMCVFXRecipePreset* Recipe)
    {
        if(!CanEdit()||!Recipe)return;Stop();FString Error;
        if(!Session->ImportRecipe(SelectedStage,Recipe,Error)){Fail(Error);return;}
        const auto* Result=Session->GetRecipe(SelectedStage);SelectedLayer=Result&&!Result->Nodes.IsEmpty()?Result->Nodes[0].ID:FGuid();
        bShowActionDetails=false;Refresh();Status=TEXT("Шаблон скопирован в черновик события. Исходник не изменён; добавляйте и настраивайте слои.");
    }
    bool SActionWorkspace::ImportSavedEvent(FName ActionID,FName Stage,UMCVFXComposition* Source,FString& Error)
    {
        const auto* Recipe=Cast<UMCVFXRecipePreset>(Source);
        if(!CanEdit() || Session->GetActionID()!=ActionID || !Session->GetActionStages().Contains(Stage))
        {Error=TEXT("Откройте нужное действие и его редактируемое событие перед импортом пресета.");return false;}
        if(!Recipe || Recipe->HasAnyFlags(RF_Transient) || Recipe->GetOutermost()->IsDirty() || !Recipe->GetPathName().StartsWith(TEXT("/Game/")))
        {Error=TEXT("Выберите сохранённый авторский пресет без несохранённых изменений.");return false;}
        Stop();if(!Session->ImportRecipe(Stage,Recipe,Error)){Fail(Error);return false;}
        SelectedStage=Stage;const auto* Result=Session->GetRecipe(Stage);
        SelectedLayer=Result&&!Result->Nodes.IsEmpty()?Result->Nodes[0].ID:FGuid();bShowActionDetails=false;
        Refresh();Status=TEXT("Сохранённый пресет открыт в черновике события. «Играть» — просмотр; «Применить» — сохранение в игру.");return true;
    }
    void SActionWorkspace::LoadStarter(const FString& Name)
    {
        const FString Asset=TEXT("DA_Template_")+Name;
        const FString Path=TEXT("/Game/Tests/VFXPractice/Templates/")+Asset+TEXT(".")+Asset;
        const auto* Recipe=LoadObject<UMCVFXRecipePreset>(nullptr,*Path);
        if(!Recipe){Fail(TEXT("Шаблон пока не сохранён в проекте: ")+Path);return;}ImportCompound(Recipe);
    }
    void SActionWorkspace::OpenCompound()
    {
        FOpenAssetDialogConfig Config;Config.DialogTitleOverride=LOCTEXT("ImportRecipe","Открыть композицию для выбранного события");Config.DefaultPath=TEXT("/Game/Tests/VFXPractice");
        Config.AssetClassNames.Add(UMCVFXRecipePreset::StaticClass()->GetClassPathName());Config.bAllowMultipleSelection=false;
        const auto Assets=FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().CreateModalOpenAssetDialog(Config);
        if(!Assets.IsEmpty())ImportCompound(Cast<UMCVFXRecipePreset>(Assets[0].GetAsset()));
    }

    void SActionWorkspace::RefreshDetails()
    {
        if(!Details)return;Details->ClearChildren();auto Add=[this](const TSharedRef<SWidget>& W){Details->AddSlot().AutoHeight().Padding(9,5)[W];};
        if(bUnsupported)
        {
            for(const auto& Entry:Catalogue)if(Entry->ID==SelectedAction)
            {Add(Label(Entry->Label.ToString(),17));Add(Label(Entry->Description.ToString(),13));Add(Label(Entry->UnsupportedReason.IsEmpty()?TEXT("Адаптер предпросмотра пока не подключён. Запуск и применение недоступны."):Entry->UnsupportedReason,12,Muted));break;}
            return;
        }
        if(!CanPlay())
        {Add(Label(TEXT("Контекст предпросмотра изменился"),16));Add(Label(AuthoringUnavailableReason(),13,Muted));return;}
        if(!Session->CanEditProfile())
        {
            Add(Label(bShowActionDetails?ActionTitle():Session->GetEventLabel(SelectedStage).ToString(),17));
            Add(Label(TEXT("Нажмите ▶ Играть, чтобы выполнить настоящий игровой сценарий с текущим родным представлением."),13));
            Add(Label(Session->GetEditUnavailableReason(),12,Muted));
            Add(Label(TEXT("Слои Niagara для этого события ещё не подключены к Composer. Случайные пассивки показывают эффект при фактическом срабатывании по текущему шансу."),12,Muted));
            return;
        }
        if(bShowActionDetails)
        {
            Add(Label(ActionTitle(),18));Add(Label(TEXT("Сценарий запускает настоящее игровое действие. Карточки показывают его события; редактируемые события поддерживают несколько слоёв эффекта."),13));
            if(!Session->CanEditProfile())Add(Label(Session->GetEditUnavailableReason(),12,Muted));
            for(FName Stage:Session->GetActionStages())Add(Command(Session->GetEventLabel(Stage).ToString()+FString::Printf(TEXT(" · включено %d/%d"),EnabledLayers(Session->GetRecipe(Stage)),Session->GetLayerCount(Stage)),[this,Stage]{SelectLayer(Stage,FGuid());}));
            Add(Label(TEXT("«Применить в игре» сохраняет композиции, готовые эффекты и профиль. До этой кнопки правки остаются черновиками."),12,Muted));return;
        }
        const auto* Layer=CurrentLayer();
        Add(Label(Session->GetEventLabel(SelectedStage).ToString(),16,StageColor(SelectedStage)));
        if(!Layer)
        {
            Add(Label(FString::Printf(TEXT("Включено слоёв: %d из %d"),EnabledLayers(Session->GetRecipe(SelectedStage)),Session->GetLayerCount(SelectedStage)),13));
            Add(Label(TEXT("Выберите слой в карточке события, чтобы изменить его параметры. Выберите эффект в галерее и нажмите «Добавить», чтобы включить новый слой."),13));
            Add(Label(TEXT("＋ Новая композиция — пустой черновик или стартовый шаблон для этого события."),12,Muted));return;
        }
        const FName Stage=SelectedStage;const FGuid ID=SelectedLayer;const auto Copy=*Layer;
        Add(SNew(SEditableTextBox).Text(FText::FromString(LayerLabel(Copy))).ToolTipText(FText::FromString(Copy.System.ToSoftObjectPath().ToString())).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),13)).Padding(FMargin(7,6))
            .OnTextCommitted_Lambda([this,Stage,ID,DisplayName=LayerLabel(Copy)](const FText& T,ETextCommit::Type){if(T.ToString()==DisplayName)return;MutateLayer(Stage,ID,[T](FMCVFXCompositionLayer& L){L.Name=FName(*T.ToString());});}));
        Add(SNew(SCheckBox).IsChecked(Copy.bEnabled?ECheckBoxState::Checked:ECheckBoxState::Unchecked)
            .OnCheckStateChanged_Lambda([this,Stage,ID](ECheckBoxState S){Stop();FString E;if(!Session->SetLayerEnabled(Stage,ID,S==ECheckBoxState::Checked,E))Fail(E);})[Label(TEXT("Слой включён"),12)]);
        Add(Label(TEXT("Появиться через, с"),12));
        const auto* Recipe=Session->GetRecipe(Stage);const float MaxStart=Recipe?FMath::Max(0.f,Recipe->DurationSeconds-.001f):2.999f;
        Add(SNew(SSpinBox<float>).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12)).Value(Copy.StartSeconds).MinValue(0.f).MaxValue(MaxStart)
            .OnValueCommitted_Lambda([this,Stage,ID](float Value,ETextCommit::Type){MutateLayer(Stage,ID,[Value](FMCVFXCompositionLayer& L){L.StartSeconds=Value;});}));
        for(int32 I=0;I<Copy.Floats.Num();++I)
        {
            const auto P=Copy.Floats[I];
            Add(SNew(SCheckBox).ToolTipText(FText::FromName(P.Name)).IsChecked(P.bOverride?ECheckBoxState::Checked:ECheckBoxState::Unchecked)
                .OnCheckStateChanged_Lambda([this,Stage,ID,I](ECheckBoxState S){MutateLayer(Stage,ID,[I,S](FMCVFXCompositionLayer& L){if(L.Floats.IsValidIndex(I))L.Floats[I].bOverride=S==ECheckBoxState::Checked;});})[Label(ParameterLabel(P.Name),12)]);
            Add(SNew(SSpinBox<float>).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12)).Value(P.Value).IsEnabled(P.bOverride).MinValue(TOptional<float>()).MaxValue(TOptional<float>())
                .OnValueCommitted_Lambda([this,Stage,ID,I](float Value,ETextCommit::Type){MutateLayer(Stage,ID,[I,Value](FMCVFXCompositionLayer& L){if(L.Floats.IsValidIndex(I)){L.Floats[I].Value=Value;L.Floats[I].bOverride=true;}});}));
        }
        for(int32 I=0;I<Copy.Colors.Num();++I)
        {
            const auto P=Copy.Colors[I];
            Add(SNew(SCheckBox).ToolTipText(FText::FromName(P.Name)).IsChecked(P.bOverride?ECheckBoxState::Checked:ECheckBoxState::Unchecked)
                .OnCheckStateChanged_Lambda([this,Stage,ID,I](ECheckBoxState S){MutateLayer(Stage,ID,[I,S](FMCVFXCompositionLayer& L){if(L.Colors.IsValidIndex(I))L.Colors[I].bOverride=S==ECheckBoxState::Checked;});})[Label(ParameterLabel(P.Name),12)]);
            Add(SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(76).HeightOverride(26)
                    [SNew(SColorBlock).Color(P.Value).Size(FVector2D(76,26)).OnMouseButtonDown_Lambda([this,Stage,ID,I,P](const FGeometry&,const FPointerEvent&)
                        {FColorPickerArgs A;A.InitialColor=P.Value;A.bUseAlpha=true;A.bOnlyRefreshOnOk=true;A.bExpandAdvancedSection=false;
                            A.OnColorCommitted=FOnLinearColorValueChanged::CreateSP(this,&SActionWorkspace::CommitColor,Stage,ID,I);OpenColorPicker(A);return FReply::Handled();})]]
                +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(10,0)[Label(TEXT("#")+P.Value.ToFColorSRGB().ToHex(),10,Muted)]);
        }
        for(int32 I=0;I<Copy.Bools.Num();++I)
        {
            const auto P=Copy.Bools[I];Add(SNew(SCheckBox).ToolTipText(FText::FromName(P.Name)).IsChecked(P.Value?ECheckBoxState::Checked:ECheckBoxState::Unchecked)
                .OnCheckStateChanged_Lambda([this,Stage,ID,I](ECheckBoxState S){MutateLayer(Stage,ID,[I,S](FMCVFXCompositionLayer& L){if(L.Bools.IsValidIndex(I)){L.Bools[I].Value=S==ECheckBoxState::Checked;L.Bools[I].bOverride=true;}});})[Label(ParameterLabel(P.Name),12)]);
        }
        for(int32 I=0;I<Copy.Vectors.Num();++I)
        {
            const auto P=Copy.Vectors[I];Add(Label(ParameterLabel(P.Name)+TEXT(" · X / Y / Z"),12));TSharedRef<SHorizontalBox> Components=SNew(SHorizontalBox);
            for(int32 Axis=0;Axis<3;++Axis)Components->AddSlot().FillWidth(1).Padding(2)[SNew(SSpinBox<double>).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11)).Value(P.Value[Axis]).MinValue(TOptional<double>()).MaxValue(TOptional<double>())
                .ToolTipText(FText::FromString(FString::Printf(TEXT("%s · %c"),*P.Name.ToString(),TCHAR('X'+Axis))))
                .OnValueCommitted_Lambda([this,Stage,ID,I,Axis](double Value,ETextCommit::Type){MutateLayer(Stage,ID,[I,Axis,Value](FMCVFXCompositionLayer& L){if(L.Vectors.IsValidIndex(I)){L.Vectors[I].Value[Axis]=Value;L.Vectors[I].bOverride=true;}});})];
            Add(Components);
        }
        if(Copy.Floats.IsEmpty()&&Copy.Colors.IsEmpty()&&Copy.Bools.IsEmpty()&&Copy.Vectors.IsEmpty())Add(Label(TEXT("У этого Niagara System нет доступных User-параметров. Время появления и расположение остаются настраиваемыми."),11,Muted));
        TSharedRef<SVerticalBox> Advanced=SNew(SVerticalBox);
        Advanced->AddSlot().AutoHeight().Padding(0,5)[Label(Copy.System.ToSoftObjectPath().ToString(),10,Muted)];
        Advanced->AddSlot().AutoHeight().Padding(0,5)[Label(TEXT("Масштаб всего слоя, ×"),12)];
        Advanced->AddSlot().AutoHeight()[SNew(SSpinBox<double>).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12)).Value(Copy.Transform.GetScale3D().X).MinValue(.01).MaxValue(50.)
            .OnValueCommitted_Lambda([this,Stage,ID](double Value,ETextCommit::Type){MutateLayer(Stage,ID,[Value](FMCVFXCompositionLayer& L){L.Transform.SetScale3D(FVector(Value));});})];
        Advanced->AddSlot().AutoHeight().Padding(0,5)[Label(TEXT("Смещение, см · X / Y / Z"),12)];
        TSharedRef<SHorizontalBox> Position=SNew(SHorizontalBox);for(int32 Axis=0;Axis<3;++Axis)Position->AddSlot().FillWidth(1).Padding(2)
            [SNew(SSpinBox<double>).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11)).Value(Copy.Transform.GetTranslation()[Axis]).MinValue(TOptional<double>()).MaxValue(TOptional<double>())
                .OnValueCommitted_Lambda([this,Stage,ID,Axis](double Value,ETextCommit::Type){MutateLayer(Stage,ID,[Value,Axis](FMCVFXCompositionLayer& L){FVector P=L.Transform.GetTranslation();P[Axis]=Value;L.Transform.SetTranslation(P);});})];
        Advanced->AddSlot().AutoHeight()[Position];
        Advanced->AddSlot().AutoHeight().Padding(0,6)[Label(TEXT("Поддержка преобразований и тайминга проверяется перед просмотром и применением. Неподдержанные настройки не отбрасываются молча."),10,Muted)];
        Add(SNew(SExpandableArea).InitiallyCollapsed(true).HeaderContent()[Label(TEXT("Дополнительно: источник и расположение"),12)].BodyContent()[Advanced]);
        Add(SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,5,0)[Command(TEXT("Дубликат"),[this,Stage,ID]{Stop();FString E;const FGuid NewID=Session->DuplicateLayer(Stage,ID,E);if(NewID.IsValid()){SelectedLayer=NewID;Refresh();}else Fail(E);})]
            +SHorizontalBox::Slot().FillWidth(1)[Command(TEXT("Удалить слой"),[this,Stage,ID]{Stop();FString E;if(Session->RemoveLayer(Stage,ID,E)){SelectedLayer.Invalidate();Refresh();}else Fail(E);})]);
    }

    FString SActionWorkspace::Snapshot()const
    {
        TSharedRef<FJsonObject> Root=MakeShared<FJsonObject>();Root->SetStringField(TEXT("selected_action"),SelectedAction.ToString());Root->SetStringField(TEXT("selected_stage"),SelectedStage.ToString());
        Root->SetStringField(TEXT("selected_layer"),SelectedLayer.ToString(EGuidFormats::DigitsWithHyphens));Root->SetStringField(TEXT("gallery_selection"),GallerySelection.GetSoftObjectPath().ToString());
        Root->SetBoolField(TEXT("adapter_supported"),!bUnsupported);Root->SetBoolField(TEXT("loop"),bLoop);Root->SetBoolField(TEXT("live_gallery"),bLive);Root->SetBoolField(TEXT("playing"),bPlaying);Root->SetBoolField(TEXT("paused"),bPaused);
        const auto Drag=FSlateApplication::Get().GetDragDroppingContent();Root->SetBoolField(TEXT("gallery_drag_active"),Drag&&Drag->IsOfType<FWorkspaceEffectDrag>());Root->SetBoolField(TEXT("audition_pending"),bPendingGalleryAudition);
        Root->SetNumberField(TEXT("gallery_count"),GalleryItems.Num());Root->SetStringField(TEXT("status"),Status);
        TSharedPtr<FJsonObject> Model;if(Session&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Session->GetSnapshotJson()),Model)&&Model)Root->SetObjectField(TEXT("session"),Model);
        FString Result;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Result));return Result;
    }
}

void MCVFXActionWorkspace::Register()
{
    if(IsRunningCommandlet()||FGlobalTabmanager::Get()->HasTabSpawner(WorkspaceTab))return;
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(WorkspaceTab,FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
    {
        TSharedRef<SActionWorkspace> Panel=SNew(SActionWorkspace);ActiveWorkspace=Panel;
        TSharedRef<SDockTab> Tab=SNew(SDockTab).TabRole(ETabRole::NomadTab).OnCanCloseTab(SDockTab::FCanCloseTab::CreateSP(Panel,&SActionWorkspace::CanClose))
            .OnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda([Weak=TWeakPtr<SActionWorkspace>(Panel)](TSharedRef<SDockTab>){if(auto P=Weak.Pin())P->Closed();}))[Panel];
        Panel->SetOwnerTab(Tab);return Tab;
    })).SetDisplayName(LOCTEXT("WorkspaceTab","VFX Composer · Действия"))
        .SetTooltipText(LOCTEXT("WorkspaceHint","Действие, его реальные события, несколько слоёв VFX, галерея и общий игровой предпросмотр."))
        .SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
}
void MCVFXActionWorkspace::Unregister()
{
    if(auto Tab=FGlobalTabmanager::Get()->FindExistingLiveTab(WorkspaceTab))if(!Tab->RequestCloseTab())return;
    ActiveWorkspace.Reset();FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(WorkspaceTab);
}
void MCVFXActionWorkspace::Open()
{
    Register();const auto Tab=FGlobalTabmanager::Get()->TryInvokeTab(WorkspaceTab);
    // Give the four-pane authoring layout room, but never resize/maximize the user's
    // main Editor window when the workspace is docked there.
    if(Tab)if(const auto Window=Tab->GetParentWindow())
        if(Window!=FGlobalTabmanager::Get()->GetRootWindow()&&Window->GetType()==EWindowType::Normal)
        {
            if(!Window->IsWindowMaximized())Window->Maximize();
            Window->BringToFront(true);
        }
}
TSharedPtr<FMCVFXActionSession> MCVFXActionWorkspace::GetActiveSession(){const auto Panel=ActiveWorkspace.Pin();return Panel?Panel->GetSession():nullptr;}
FString MCVFXActionWorkspace::GetSnapshotJson(){const auto Panel=ActiveWorkspace.Pin();return Panel?Panel->Snapshot():TEXT("{\"error\":\"workspace_closed\"}");}
bool MCVFXActionWorkspace::ImportEventRecipe(FName ActionID,FName Stage,UMCVFXComposition* Source,FString& Error)
{
    const auto Panel=ActiveWorkspace.Pin();if(!Panel){Error=TEXT("Сначала откройте VFX Composer.");return false;}
    return Panel->ImportSavedEvent(ActionID,Stage,Source,Error);
}

#undef LOCTEXT_NAMESPACE
