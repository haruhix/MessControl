#include "MCVFXRecipeComposer.h"
#include "MCVFXActionWorkspace.h"

#include "MCVFXRecipePreset.h"
#include "MCVFXComposerBuilderLibrary.h"
#include "MCPerkVFXProfile.h"
#include "MCPerkActionPreviewPanel.h"
#include "MCPerkPreviewRunner.h"
#include "SMCVFXComposerViewport.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetThumbnail.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "ContentBrowserModule.h"
#include "ContentBrowserDelegates.h"
#include "Containers/Ticker.h"
#include "DetailsViewArgs.h"
#include "Editor.h"
#include "EditorUndoClient.h"
#include "FileHelpers.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "IContentBrowserSingleton.h"
#include "IStructureDetailsView.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "NiagaraSystem.h"
#include "PropertyEditorModule.h"
#include "Rendering/DrawElementTypes.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
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
#include "Widgets/SNullWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STileView.h"
#include "Widgets/Views/STableRow.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "MCVFXRecipeComposer"

namespace
{
    const FName RecipeTab(TEXT("MCVFXRecipeComposer"));
    const FLinearColor Mint(.34f,.78f,.66f),PanelInk(.035f,.075f,.085f),DisabledButton(.18f,.20f,.22f),DisabledInk(.66f,.69f,.72f);
    const FButtonStyle& SolidButtonStyle()
    {
        // AppStyle's textured button multiplies an already dark brush by our tint.
        // A white solid base makes the chosen mint/gray colours intentional and stable.
        static const FButtonStyle Style=FButtonStyle()
            .SetNormal(FSlateRoundedBoxBrush(FLinearColor::White,6.f))
            .SetHovered(FSlateRoundedBoxBrush(FLinearColor(1.08f,1.08f,1.08f,1.f),6.f))
            .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.86f,.86f,.86f,1.f),6.f))
            .SetDisabled(FSlateRoundedBoxBrush(FLinearColor::White,6.f))
            .SetDisabledForeground(DisabledInk).SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
        return Style;
    }
    using FRecipeLibraryItem=TSharedPtr<FAssetData>;
    const FVector2D NodeSize(235,165),OutputPosition(760,180),OutputSize(160,160);
    int32 ConnectedLayerCount(const UMCVFXRecipePreset* D)
    {int32 Count=0;if(D)for(const auto& L:D->Layers)if(L.bEnabled)++Count;return Count;}
    FVector2D ResultPort(const UMCVFXRecipePreset* D,int32 LayerIndex)
    {
        int32 Rank=0;if(D)for(int32 I=0;I<LayerIndex&&I<D->Layers.Num();++I)if(D->Layers[I].bEnabled)++Rank;
        const int32 Count=ConnectedLayerCount(D);
        // Each enabled layer owns one distinct pin. Small recipes fan along the left
        // arc; larger ones use the circumference without stacking pins at one point.
        const double Step=FMath::Min(.45,2.*PI/FMath::Max(2,Count+1));
        const double Angle=PI+(Rank-(Count-1)*.5)*Step;
        return OutputPosition+OutputSize*.5+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*(OutputSize.X*.5-2.);
    }
    struct FRecipeContext
    {
        FName PerkID,Stage;
        TWeakObjectPtr<UMCPerkVFXProfile> Profile;
        FMCVFXRecipeApplied OnApply;
    };
    FRecipeContext PendingContext;
    bool bHasPendingContext=false;
    FLinearColor NodeTint(const UMCVFXRecipePreset* D,int32 I)
    {
        if(!D || !D->Nodes.IsValidIndex(I))return FLinearColor(.12f,.30f,.33f);
        const auto* G=D->Groups.FindByPredicate([&](const FMCVFXRecipeGroup& V){return V.ID==D->Nodes[I].Group;});
        return G?G->Color:FLinearColor(.12f,.30f,.33f);
    }
    FString ControlLabel(FName Name)
    {
        const FString S=Name.ToString().Replace(TEXT("User."),TEXT(""));
        if(S==TEXT("Color"))return TEXT("Цвет");
        if(S==TEXT("SpawnCount"))return TEXT("Количество частиц");
        if(S==TEXT("Lifetime"))return TEXT("Время жизни частицы");
        if(S==TEXT("ParticleScale"))return TEXT("Масштаб частиц");
        if(S==TEXT("EmissiveScale"))return TEXT("Яркость свечения");
        if(S==TEXT("TemperatureScale"))return TEXT("Температура");
        if(S==TEXT("Intensity"))return TEXT("Интенсивность");
        if(S==TEXT("Duration"))return TEXT("Длительность Niagara");
        if(S==TEXT("Radius"))return TEXT("Радиус Niagara");
        return S;
    }
    TSharedRef<SWidget> Text(const FString& Value,int32 Size=12)
    {return SNew(STextBlock).Text(FText::FromString(Value)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),Size)).AutoWrapText(true);}
    FString EffectLabel(const FString& Name)
    {
        static const TMap<FString,FString> Known={
            {TEXT("NS_Rocket_Start"),TEXT("Ракета · выстрел")},{TEXT("NS_Rocket_Loop"),TEXT("Ракета · след")},{TEXT("NS_Rocket_Impact"),TEXT("Ракета · попадание")},
            {TEXT("NS_Sponge_Start"),TEXT("Губка · старт")},{TEXT("NS_Sponge_Loop"),TEXT("Губка · движение")},{TEXT("NS_Sponge_End"),TEXT("Губка · завершение")},{TEXT("NS_Sponge_Trail"),TEXT("Губка · мыльный след")},
            {TEXT("NS_Hook_Start"),TEXT("Хук · бросок")},{TEXT("NS_Hook_Loop"),TEXT("Хук · полёт")},{TEXT("NS_Hook_Impact"),TEXT("Хук · попадание")},{TEXT("NS_Hook_End"),TEXT("Хук · возврат")},
            {TEXT("NS_Grenade_Start"),TEXT("Граната · бросок")},{TEXT("NS_Grenade_Loop"),TEXT("Граната · полёт")},{TEXT("NS_Grenade_Impact"),TEXT("Граната · взрыв")},{TEXT("NS_Grenade_Warning"),TEXT("Граната · предупреждение")},
            {TEXT("NS_Air_Loop"),TEXT("Воздухан · всасывание")},{TEXT("NS_Air_Impact"),TEXT("Воздухан · выдувание")},{TEXT("NS_Air_Rupture"),TEXT("Воздухан · перегрузка")},{TEXT("NS_Air_Warning"),TEXT("Воздухан · предупреждение")},
            {TEXT("NS_Driver_Start"),TEXT("Машина · старт")},{TEXT("NS_Driver_Loop"),TEXT("Машина · движение")},{TEXT("NS_Driver_End"),TEXT("Машина · завершение")}};
        if(const auto* Label=Known.Find(Name))return *Label;return Name;
    }

    class SRecipeResultHub final : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SRecipeResultHub){}SLATE_ATTRIBUTE(UMCVFXRecipePreset*,Document)SLATE_ATTRIBUTE(bool,Selected)SLATE_EVENT(FSimpleDelegate,OnClicked)SLATE_END_ARGS()
        void Construct(const FArguments& A)
        {
            Document=A._Document;Selected=A._Selected;OnClicked=A._OnClicked;
            SetToolTipText(LOCTEXT("ResultHubHint","Открыть общие настройки композиции. Порты и линии показывают включённые эффекты."));
            ChildSlot.HAlign(HAlign_Center).VAlign(VAlign_Center)
                [SNew(SVerticalBox).Visibility(EVisibility::HitTestInvisible)
                    +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("ResultHubTitle","Результат")).Justification(ETextJustify::Center).ColorAndOpacity(FLinearColor(.94f,.97f,.98f)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),18))]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,7)[SNew(STextBlock).Text_Lambda([this]{return FText::AsNumber(ConnectedLayerCount(Document.Get()));}).Justification(ETextJustify::Center).ColorAndOpacity(Mint).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),25))]
                    +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("ResultHubCount","эффектов включено")).Justification(ETextJustify::Center).ColorAndOpacity(FLinearColor(.70f,.83f,.86f)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10))]];
        }
        virtual bool SupportsKeyboardFocus()const override{return true;}
        virtual FReply OnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)override
        {
            const FVector2D Local=G.AbsoluteToLocal(E.GetScreenSpacePosition());
            if(E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();
            if((Local-G.GetLocalSize()*.5).SizeSquared()>FMath::Square(G.GetLocalSize().X*.5))return FReply::Handled();
            OnClicked.ExecuteIfBound();return FReply::Handled().SetUserFocus(SharedThis(this));
        }
        virtual FReply OnKeyDown(const FGeometry&,const FKeyEvent& E)override
        {if(E.GetKey()==EKeys::Enter||E.GetKey()==EKeys::SpaceBar){OnClicked.ExecuteIfBound();return FReply::Handled();}return FReply::Unhandled();}
        virtual int32 OnPaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& C,FSlateWindowElementList& O,int32 L,const FWidgetStyle& W,bool Enabled)const override
        {
            static const FSlateRoundedBoxBrush Circle(FLinearColor(.055f,.12f,.15f),80.f,FLinearColor(.28f,.67f,.68f),2.f);
            static const FSlateRoundedBoxBrush Chosen(FLinearColor(.065f,.16f,.18f),80.f,Mint,4.f);
            static const FSlateRoundedBoxBrush Inner(FLinearColor::Transparent,66.f,FLinearColor(.22f,.42f,.47f),1.f);
            static const FSlateRoundedBoxBrush Pin(FLinearColor::White,5.f,FLinearColor(.025f,.045f,.055f),1.5f);
            const FSlateBrush* Background=Selected.Get()?&Chosen:&Circle;
            FSlateDrawElement::MakeBox(O,L,G.ToPaintGeometry(),Background,ESlateDrawEffect::None,Background->GetTint(W));
            FSlateDrawElement::MakeBox(O,L+1,G.ToPaintGeometry(FVector2f(132,132),FSlateLayoutTransform(FVector2f(14,14))),&Inner,ESlateDrawEffect::None,Inner.GetTint(W));
            const int32 ContentLayer=SCompoundWidget::OnPaint(A,G,C,O,L+2,W,Enabled);
            const auto* D=Document.Get();if(D)for(int32 I=0;I<D->Layers.Num();++I)if(D->Layers[I].bEnabled)
            {
                const FVector2D P=ResultPort(D,I)-OutputPosition-FVector2D(5,5);
                FSlateDrawElement::MakeBox(O,ContentLayer+1,G.ToPaintGeometry(FVector2f(10,10),FSlateLayoutTransform(FVector2f(P))),&Pin,ESlateDrawEffect::None,Pin.GetTint(W)*NodeTint(D,I)*1.8f);
            }
            return ContentLayer+1;
        }
    private:
        TAttribute<UMCVFXRecipePreset*> Document;TAttribute<bool> Selected;FSimpleDelegate OnClicked;
    };

    class SRecipeGraph;
    class SRecipeWires final : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SRecipeWires){}SLATE_ARGUMENT(TWeakPtr<SRecipeGraph>,Graph)SLATE_END_ARGS()
        void Construct(const FArguments& A){Graph=A._Graph;SetVisibility(EVisibility::HitTestInvisible);}
        virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(700,500);}
        virtual int32 OnPaint(const FPaintArgs&,const FGeometry&,const FSlateRect&,FSlateWindowElementList&,int32,const FWidgetStyle&,bool)const override;
        TWeakPtr<SRecipeGraph> Graph;
    };
    DECLARE_DELEGATE_TwoParams(FRecipeSelect,FGuid,bool);
    DECLARE_DELEGATE_OneParam(FRecipeBeginMove,FGuid);
    DECLARE_DELEGATE_OneParam(FRecipeMove,FVector2D);
    DECLARE_DELEGATE_TwoParams(FRecipeToggle,FGuid,bool);
    class SRecipeGraph final : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SRecipeGraph){} SLATE_ATTRIBUTE(UMCVFXRecipePreset*,Document) SLATE_ATTRIBUTE(TSet<FGuid>,Selection) SLATE_ATTRIBUTE(bool,ResultSelected)
            SLATE_EVENT(FRecipeSelect,OnSelect) SLATE_EVENT(FRecipeBeginMove,OnBeginMove) SLATE_EVENT(FRecipeMove,OnMove)
            SLATE_EVENT(FSimpleDelegate,OnEndMove) SLATE_EVENT(FRecipeToggle,OnEnable) SLATE_EVENT(FRecipeToggle,OnSolo)
            SLATE_EVENT(FSimpleDelegate,OnDelete) SLATE_EVENT(FSimpleDelegate,OnDuplicate) SLATE_EVENT(FSimpleDelegate,OnGroup) SLATE_EVENT(FSimpleDelegate,OnSelectAll)
        SLATE_END_ARGS()
        void Construct(const FArguments& A)
        {
            Document=A._Document;Selection=A._Selection;ResultSelected=A._ResultSelected;OnSelect=A._OnSelect;OnBeginMove=A._OnBeginMove;OnMove=A._OnMove;
            OnEndMove=A._OnEndMove;OnEnable=A._OnEnable;OnSolo=A._OnSolo;OnDelete=A._OnDelete;OnDuplicate=A._OnDuplicate;OnGroup=A._OnGroup;OnSelectAll=A._OnSelectAll;
            Pool=MakeShared<FAssetThumbnailPool>(40);Build();
        }
        virtual bool SupportsKeyboardFocus()const override{return true;}
        void Build()
        {
            Thumbnails.Reset();auto* D=Document.Get();
            TSharedRef<SConstraintCanvas> Canvas=SNew(SConstraintCanvas).RenderTransform_Lambda([this]{return FSlateRenderTransform(FScale2D(Zoom),Pan);}).RenderTransformPivot(FVector2D::ZeroVector);
            if(D)for(int32 I=0;I<D->Nodes.Num() && I<D->Layers.Num();++I)
            {
                const FGuid ID=D->Nodes[I].ID;const auto& L=D->Layers[I];
                auto Thumb=MakeShared<FAssetThumbnail>(FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetByObjectPath(L.System.ToSoftObjectPath()),72,72,Pool);Thumbnails.Add(Thumb);
                Canvas->AddSlot().Offset_Lambda([this,ID]{auto* V=Document.Get();const int32 N=V?V->FindNode(ID):INDEX_NONE;const FVector2D P=N!=INDEX_NONE?V->Nodes[N].Position:FVector2D::ZeroVector;return FMargin(P.X,P.Y,NodeSize.X,NodeSize.Y);}).Alignment(FVector2D::ZeroVector)
                [SNew(SBorder).Padding(2).BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                    .BorderBackgroundColor_Lambda([this,ID]{auto* V=Document.Get();const int32 N=V?V->FindNode(ID):INDEX_NONE;return Selection.Get().Contains(ID)?FLinearColor(.55f,.62f,.28f):NodeTint(V,N);})
                    [SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight()[SNew(SBorder).Padding(7).BorderBackgroundColor(NodeTint(D,I))
                            .OnMouseButtonDown_Lambda([this,ID](const FGeometry&,const FPointerEvent& E)
                            {
                                if(E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();
                                // Ctrl-click changes membership only. A plain drag of an already selected
                                // header preserves the whole selected set instead of collapsing it to one node.
                                if(E.IsControlDown()){OnSelect.ExecuteIfBound(ID,true);return FReply::Handled().SetUserFocus(SharedThis(this));}
                                if(!Selection.Get().Contains(ID))OnSelect.ExecuteIfBound(ID,false);
                                OnBeginMove.ExecuteIfBound(ID);bNodeDragging=true;bAutoFit=false;
                                return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this));
                            })
                            [SNew(STextBlock).Text_Lambda([this,ID]{auto* V=Document.Get();const int32 N=V?V->FindNode(ID):INDEX_NONE;return N!=INDEX_NONE?FText::FromString(EffectLabel(V->Layers[N].Name.ToString())):FText::GetEmpty();}).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),12))]]
                        +SVerticalBox::Slot().FillHeight(1).Padding(5)[SNew(SBorder).Padding(0).BorderImage(FAppStyle::GetBrush(TEXT("NoBrush")))
                            .OnMouseButtonDown_Lambda([this,ID](const FGeometry&,const FPointerEvent& E){if(E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();OnSelect.ExecuteIfBound(ID,E.IsControlDown());return FReply::Handled().SetUserFocus(SharedThis(this));})
                            [SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(70).HeightOverride(70)[Thumb->MakeThumbnailWidget()]]
                                +SHorizontalBox::Slot().FillWidth(1).Padding(7,0)[SNew(SVerticalBox)
                                    +SVerticalBox::Slot().AutoHeight()[Text(L.System.GetAssetName())]
                                    +SVerticalBox::Slot().AutoHeight().Padding(0,5)[SNew(STextBlock).Text_Lambda([this,ID]{auto* V=Document.Get();const int32 N=V?V->FindNode(ID):INDEX_NONE;if(N==INDEX_NONE)return FText::GetEmpty();return FText::FromString(FString::Printf(TEXT("Старт %.2f с\n%s"),V->Layers[N].StartSeconds,V->Layers[N].bEnabled?TEXT("Подключён к результату"):TEXT("Выключен")));}).AutoWrapText(true)]]]]
                        +SVerticalBox::Slot().AutoHeight().Padding(5)[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1)[SNew(SCheckBox).IsChecked_Lambda([this,ID]{auto* V=Document.Get();const int32 N=V?V->FindNode(ID):INDEX_NONE;return N!=INDEX_NONE&&V->Layers[N].bEnabled?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                                .OnCheckStateChanged_Lambda([this,ID](ECheckBoxState S){OnEnable.ExecuteIfBound(ID,S==ECheckBoxState::Checked);})[Text(TEXT("Включён"))]]
                            +SHorizontalBox::Slot().FillWidth(1)[SNew(SCheckBox).IsChecked_Lambda([this,ID]{auto* V=Document.Get();const int32 N=V?V->FindNode(ID):INDEX_NONE;return N!=INDEX_NONE&&V->Layers[N].bSolo?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                                .OnCheckStateChanged_Lambda([this,ID](ECheckBoxState S){OnSolo.ExecuteIfBound(ID,S==ECheckBoxState::Checked);})[Text(TEXT("Solo"))]]]]];
            }
            if(D&&!D->Layers.IsEmpty())Canvas->AddSlot().Offset(FMargin(OutputPosition.X,OutputPosition.Y,OutputSize.X,OutputSize.Y)).Alignment(FVector2D::ZeroVector)
            [SNew(SRecipeResultHub).Document(Document).Selected(ResultSelected).OnClicked_Lambda([this]{OnSelect.ExecuteIfBound(FGuid(),false);})];
            ChildSlot[SNew(SBorder).Padding(0).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(.025f,.032f,.042f)).Clipping(EWidgetClipping::ClipToBounds)
                [SNew(SOverlay)+SOverlay::Slot()[SNew(SRecipeWires).Graph(SharedThis(this))]+SOverlay::Slot()[Canvas]]];
        }
        void FrameAll(){bAutoFit=true;bNeedFit=true;Invalidate(EInvalidateWidgetReason::Layout);}
        virtual void Tick(const FGeometry& G,double T,float Dt)override
        {
            SCompoundWidget::Tick(G,T,Dt);const auto S=G.GetLocalSize();
            if(!bAutoFit || S.X<2 || S.Y<2 || (!bNeedFit && S.Equals(LastFit,1.f)))return;
            FBox2D B(OutputPosition,OutputPosition+OutputSize);if(auto* D=Document.Get())for(const auto& N:D->Nodes){B+=N.Position-FVector2D(16,34);B+=N.Position+NodeSize+FVector2D(16,16);}
            const FVector2D Ext=B.GetSize();Zoom=float(FMath::Clamp(FMath::Min((S.X-32)/FMath::Max(1.,Ext.X),(S.Y-32)/FMath::Max(1.,Ext.Y)),.01,1.2));
            Pan=(S-Ext*Zoom)*.5-B.Min*Zoom;LastFit=S;bNeedFit=false;Invalidate(EInvalidateWidgetReason::Layout);
        }
        virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent& E)override
        {
            if(E.GetEffectingButton()==EKeys::RightMouseButton||E.GetEffectingButton()==EKeys::MiddleMouseButton){bPanning=true;bAutoFit=false;return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this));}
            if(E.GetEffectingButton()==EKeys::LeftMouseButton){OnSelect.ExecuteIfBound(FGuid(),false);return FReply::Handled().SetUserFocus(SharedThis(this));}
            return FReply::Unhandled();
        }
        virtual FReply OnMouseMove(const FGeometry&,const FPointerEvent& E)override
        {
            if(bNodeDragging){OnMove.ExecuteIfBound(E.GetCursorDelta()/Zoom);return FReply::Handled();}
            if(bPanning){Pan+=E.GetCursorDelta();Invalidate(EInvalidateWidgetReason::Layout);return FReply::Handled();}return FReply::Unhandled();
        }
        virtual FReply OnMouseButtonUp(const FGeometry&,const FPointerEvent&)override
        {if(bNodeDragging){bNodeDragging=false;OnEndMove.ExecuteIfBound();return FReply::Handled().ReleaseMouseCapture();}if(bPanning){bPanning=false;return FReply::Handled().ReleaseMouseCapture();}return FReply::Unhandled();}
        virtual void OnMouseCaptureLost(const FCaptureLostEvent&)override{if(bNodeDragging){bNodeDragging=false;OnEndMove.ExecuteIfBound();}bPanning=false;}
        virtual FReply OnMouseWheel(const FGeometry& G,const FPointerEvent& E)override
        {bAutoFit=false;const auto P=G.AbsoluteToLocal(E.GetScreenSpacePosition());const float Z=FMath::Clamp(Zoom*FMath::Pow(1.12f,E.GetWheelDelta()),.1f,2.f);Pan=P-(P-Pan)*(Z/Zoom);Zoom=Z;Invalidate(EInvalidateWidgetReason::Layout);return FReply::Handled();}
        virtual FReply OnKeyDown(const FGeometry&,const FKeyEvent& E)override
        {
            if(E.IsControlDown()&&E.GetKey()==EKeys::A){OnSelectAll.ExecuteIfBound();return FReply::Handled();}
            if(E.GetKey()==EKeys::Delete){OnDelete.ExecuteIfBound();return FReply::Handled();}
            if(E.IsControlDown()&&E.GetKey()==EKeys::D){OnDuplicate.ExecuteIfBound();return FReply::Handled();}
            if(E.IsControlDown()&&E.GetKey()==EKeys::G){OnGroup.ExecuteIfBound();return FReply::Handled();}
            if(E.GetKey()==EKeys::F){FrameAll();return FReply::Handled();}return FReply::Unhandled();
        }
        TAttribute<UMCVFXRecipePreset*> Document;FVector2D Pan=FVector2D::ZeroVector;float Zoom=.65f;
    private:
        TAttribute<TSet<FGuid>> Selection;TAttribute<bool> ResultSelected;
        FRecipeSelect OnSelect;FRecipeBeginMove OnBeginMove;FRecipeMove OnMove;FSimpleDelegate OnEndMove,OnDelete,OnDuplicate,OnGroup,OnSelectAll;FRecipeToggle OnEnable,OnSolo;
        TSharedPtr<FAssetThumbnailPool> Pool;TArray<TSharedPtr<FAssetThumbnail>> Thumbnails;
        bool bPanning=false,bNodeDragging=false,bAutoFit=true,bNeedFit=true;FVector2D LastFit=FVector2D::ZeroVector;
    };
    int32 SRecipeWires::OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& O,int32 L,const FWidgetStyle&,bool)const
    {
        auto P=Graph.Pin();auto* D=P?P->Document.Get():nullptr;if(!P||!D)return L;
        const auto Size=G.GetLocalSize();const float Step=FMath::Max(8.f,24*P->Zoom);
        for(float X=FMath::Fmod(P->Pan.X,Step);X<Size.X;X+=Step)FSlateDrawElement::MakeLines(O,L,G.ToPaintGeometry(),{FVector2D(X,0),FVector2D(X,Size.Y)},ESlateDrawEffect::None,FLinearColor(.035f,.045f,.055f),false,1);
        for(float Y=FMath::Fmod(P->Pan.Y,Step);Y<Size.Y;Y+=Step)FSlateDrawElement::MakeLines(O,L,G.ToPaintGeometry(),{FVector2D(0,Y),FVector2D(Size.X,Y)},ESlateDrawEffect::None,FLinearColor(.035f,.045f,.055f),false,1);
        for(const auto& Group:D->Groups)
        {
            FBox2D B(ForceInit);for(const auto& N:D->Nodes)if(N.Group==Group.ID){B+=N.Position-FVector2D(16,36);B+=N.Position+NodeSize+FVector2D(16,16);}
            if(!B.bIsValid)continue;const FVector2D Pos=P->Pan+B.Min*P->Zoom,Ext=B.GetSize()*P->Zoom;
            FLinearColor C=Group.Color;C.A=.32f;
            FSlateDrawElement::MakeBox(O,L+1,G.ToPaintGeometry(FVector2f(Ext),FSlateLayoutTransform(FVector2f(Pos))),FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),ESlateDrawEffect::None,C);
            FSlateDrawElement::MakeText(O,L+2,G.ToPaintGeometry(FVector2f(1,1),FSlateLayoutTransform(FVector2f(Pos+FVector2D(8,5)))),Group.Label,FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),10),ESlateDrawEffect::None,FLinearColor(.85f,.91f,.93f));
        }
        for(int32 I=0;I<D->Nodes.Num()&&I<D->Layers.Num();++I)
        {
            if(!D->Layers[I].bEnabled)continue;
            const FVector2D Source=D->Nodes[I].Position+FVector2D(NodeSize.X+5,NodeSize.Y*.5),Port=ResultPort(D,I);
            const FVector2D A=G.LocalToAbsolute(P->Pan+Source*P->Zoom),B=G.LocalToAbsolute(P->Pan+Port*P->Zoom);
            const FVector2D EndTangent=(OutputPosition+OutputSize*.5-Port).GetSafeNormal()*110*P->Zoom;
            FSlateDrawElement::MakeDrawSpaceSpline(O,L+3,A,FVector2D(110,0)*P->Zoom,B,EndTangent,2,ESlateDrawEffect::None,NodeTint(D,I)*1.8f);
            static const FSlateRoundedBoxBrush Pin(FLinearColor::White,5.f);
            FSlateDrawElement::MakeBox(O,L+4,G.ToPaintGeometry(FVector2f(FVector2D(10,10)*P->Zoom),FSlateLayoutTransform(FVector2f(P->Pan+(Source-FVector2D(5,5))*P->Zoom))),&Pin,ESlateDrawEffect::None,NodeTint(D,I)*1.8f);
        }
        return L+4;
    }

    class SRecipeComposer final : public SCompoundWidget,public FEditorUndoClient
    {
    public:
        SLATE_BEGIN_ARGS(SRecipeComposer){}SLATE_END_ARGS()
        void Construct(const FArguments&)
        {
            Working.Reset(NewObject<UMCVFXRecipePreset>(GetTransientPackage(),NAME_None,RF_Transient|RF_Transactional));
            FDetailsViewArgs A;A.bUpdatesFromSelection=false;A.bLockable=false;A.NameAreaSettings=FDetailsViewArgs::HideNameArea;
            FStructureDetailsViewArgs B;B.bShowObjects=true;
            Advanced=FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateStructureDetailView(A,B,nullptr);
            Advanced->GetOnFinishedChangingPropertiesDelegate().AddSP(this,&SRecipeComposer::AdvancedChanged);
            if(GEditor)GEditor->RegisterForUndo(this);
            DetailPool=MakeShared<FAssetThumbnailPool>(8);
            LibraryPool=MakeShared<FAssetThumbnailPool>(32);
            FilterLibrary(TEXT("/Game/Gameplay/ActiveSkills/VFX"));
            TSharedRef<SWidget> Library=SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().Padding(4)[SAssignNew(LibrarySearchBox,SEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14)).Padding(FMargin(10,8)).HintText(LOCTEXT("LibrarySearch","Найти эффект: название или файл…"))
                    .OnTextChanged_Lambda([this](const FText& Value){LibrarySearch=Value.ToString();RefreshLibrary();})]
                +SVerticalBox::Slot().FillHeight(1)[SAssignNew(LibraryView,STileView<FRecipeLibraryItem>)
                    .ListItemsSource(&LibraryItems).SelectionMode(ESelectionMode::Single).ItemWidth(180.f).ItemHeight(214.f)
                    .OnGenerateTile_Lambda([this](FRecipeLibraryItem Item,const TSharedRef<STableViewBase>& Owner)->TSharedRef<ITableRow>
                    {
                        auto Thumbnail=MakeShared<FAssetThumbnail>(*Item,152,112,LibraryPool);
                        FAssetThumbnailConfig Config;Config.bAllowRealTimeOnHovered=false;Config.bAllowHintText=false;
                        return SNew(STableRow<FRecipeLibraryItem>,Owner).Padding(5)
                            [SNew(SBorder).Padding(7).BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                                .BorderBackgroundColor_Lambda([this,Item]{return bLibrarySelection&&LibraryAsset.PackageName==Item->PackageName?Mint:FLinearColor(.10f,.14f,.17f);})
                                [SNew(SVerticalBox).ToolTipText(FText::FromString(Item->GetSoftObjectPath().ToString()))
                                    +SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(112)[Thumbnail->MakeThumbnailWidget(Config)]]
                                    +SVerticalBox::Slot().AutoHeight().Padding(2,6)[Text(EffectLabel(Item->AssetName.ToString()),14)]
                                    +SVerticalBox::Slot().AutoHeight().Padding(2,0)[SNew(STextBlock).Text_Lambda([this,Item]{return FText::FromString(bLibrarySelection&&LibraryAsset.PackageName==Item->PackageName?TEXT("● Выбрано для просмотра"):TEXT("Клик — посмотреть"));}).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10)).AutoWrapText(true)]]];
                    })
                    .OnSelectionChanged_Lambda([this](FRecipeLibraryItem Item,ESelectInfo::Type){if(Item.IsValid())SelectLibrary(*Item);})]
                +SVerticalBox::Slot().AutoHeight().Padding(12)[SNew(SVerticalBox).Visibility_Lambda([this]{return LibraryItems.IsEmpty()?EVisibility::Visible:EVisibility::Collapsed;})
                    +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[Text(TEXT("Ничего не найдено. Измените запрос или выберите другую категорию."),14)]
                    +SVerticalBox::Slot().AutoHeight()[Button(TEXT("Очистить поиск"),[this]{if(LibrarySearchBox)LibrarySearchBox->SetText(FText::GetEmpty());})]]
                +SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(FString::Printf(TEXT("%d NS · %s"),LibraryItems.Num(),*LibraryPath));}).AutoWrapText(true)];
            ChildSlot[SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().Padding(10,8)[SNew(SBorder).Padding(10,8).BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                    [SNew(SHorizontalBox)
                        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(185)[Button(TEXT("БИБЛИОТЕКА"),[this]{ShowLibrary();})]]
                        +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(14,0)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(ContextCaption());}).Justification(ETextJustify::Center).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),18)).AutoWrapText(true)]
                        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(185)[PrimaryButton(TEXT("+ ДОБАВИТЬ"),[this]{ShowLibrary();})]]]]
                +SVerticalBox::Slot().AutoHeight().Padding(10,0,10,8)[SNew(SBorder).Padding(10,6).BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                    [SNew(SHorizontalBox)
                        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2,0,10,0)[Text(TEXT("ПРЕСЕТ"),11)]
                        +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0,0,10,0)[SNew(SEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12)).Padding(FMargin(8,7))
                            .HintText(LOCTEXT("PresetNameHint","Название композиции"))
                            .Text_Lambda([this]{return FText::FromString(Working->PresetName);})
                            .OnTextCommitted_Lambda([this](const FText& Value,ETextCommit::Type){const FString Name=Value.ToString();if(Name==Working->PresetName)return;const FScopedTransaction Tx(LOCTEXT("RenamePresetHeader","VFX: название пресета"));Working->Modify();Working->PresetName=Name;bDirty=true;})]
                        +SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)[SNew(SButton).ContentPadding(FMargin(12,8)).IsEnabled_Lambda([this]{return HasEffects();})
                            .ToolTipText(LOCTEXT("SavePresetHeaderHint","Сохранить новую копию пресета. Существующие пресеты не перезаписываются; сначала добавьте эффект."))
                            .OnClicked_Lambda([this]{SaveNew();return FReply::Handled();})[Text(TEXT("Сохранить…"),12)]]
                        +SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)[Button(TEXT("Открыть…"),[this]{OpenSelected();})]
                        +SHorizontalBox::Slot().AutoWidth().Padding(0,0,10,0)[Button(TEXT("Новый"),[this]{NewRecipe();})]
                        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(bDirty?TEXT("● Не сохранён"):SourcePath.IsEmpty()?TEXT("Рабочая копия"):TEXT("✓ Сохранён"));}).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11)).ColorAndOpacity_Lambda([this]{return bDirty?FLinearColor(.94f,.73f,.38f):Mint;})]]]
                +SVerticalBox::Slot().AutoHeight().Padding(10,0,10,8)[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(10,6)).Visibility_Lambda([this]{return HasEffects()&&!bLibrary?EVisibility::Visible:EVisibility::Collapsed;})
                    +SWrapBox::Slot()[SNew(SButton)
                        .ContentPadding(FMargin(12,10)).IsEnabled_Lambda([this]{return HasEnabledEffects()&&!bDirty&&!SourcePath.IsEmpty();})
                        .ToolTipText_Lambda([this]{return FText::FromString(!HasEnabledEffects()?TEXT("Сначала добавьте и включите эффект."):(bDirty||SourcePath.IsEmpty()?TEXT("Сначала сохраните текущую композицию как новый пресет."):TEXT("Создать новую Niagara System. Исходные эффекты не перезаписываются.")));})
                        .OnClicked_Lambda([this]{ExportDialog();return FReply::Handled();})[Text(TEXT("Экспортировать эффект…"),12)]]
                    +SWrapBox::Slot()[SNew(SButton).Visibility_Lambda([this]{return !Context.Stage.IsNone()?EVisibility::Visible:EVisibility::Collapsed;})
                        .ContentPadding(FMargin(12,10)).IsEnabled_Lambda([this]{return LastExport.IsValid()&&Context.Profile.IsValid();})
                        .ToolTipText(LOCTEXT("ApplyWorkflow","После экспорта назначьте результат рабочей копии события. Профиль и DT сохраняются отдельно."))
                        .OnClicked_Lambda([this]{FString E;if(!Apply(E))Status=E;return FReply::Handled();})[Text(TEXT("Применить к событию"),12)]]]
                +SVerticalBox::Slot().FillHeight(1).Padding(8,0)[SNew(SSplitter)
                    +SSplitter::Slot().Value(.30f)[SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight().Padding(8,6)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(bPreviewingGallery?TEXT("Смотрим: ")+EffectLabel(LibraryAsset.AssetName.ToString()):TEXT("Предпросмотр композиции"));}).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),14)).AutoWrapText(true)]
                        +SVerticalBox::Slot().AutoHeight().Padding(8,0,8,8)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(FString::Printf(TEXT("В композиции: %d  ·  %s"),Working->Layers.Num(),bPreviewingGallery?TEXT("просмотр элемента не добавляет его"):bUseActionPreview?TEXT("полное действие зубика"):TEXT("свободный просмотр")));}).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11)).AutoWrapText(true)]
                        +SVerticalBox::Slot().FillHeight(1)[SNew(SWidgetSwitcher).WidgetIndex_Lambda([this]{return bUseActionPreview?1:0;})
                            +SWidgetSwitcher::Slot()[SAssignNew(Viewport,SMCVFXComposerViewport)]
                            +SWidgetSwitcher::Slot()[SAssignNew(ContextPreviewBox,SBox)]]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,6)[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).ButtonStyle(&SolidButtonStyle()).ContentPadding(FMargin(10,10))
                                .ButtonColorAndOpacity_Lambda([this]{return CanPlay()?Mint:DisabledButton;}).ForegroundColor_Lambda([this]{return FSlateColor(CanPlay()?PanelInk:DisabledInk);})
                                .IsEnabled_Lambda([this]{return CanPlay();}).ToolTipText_Lambda([this]{return FText::FromString(CanPlay()?TEXT("Проиграть выбранный эффект или композицию."):TEXT("Выберите эффект в библиотеке или добавьте его в композицию."));})
                                .OnClicked_Lambda([this]{if(bLibrary&&bLibrarySelection&&LibraryAsset.IsValid())PreviewLibrary();else PrepareAndPlay();return FReply::Handled();})[Text(TEXT("▶ Просмотреть"),14)]]
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Пауза"),[this]{PausePlayback();})]
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Стоп"),[this]{StopPlayback();})]]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,3)[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(12,3))
                            +SWrapBox::Slot()[SNew(SCheckBox).IsChecked_Lambda([this]{return bRepeat?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                                .OnCheckStateChanged_Lambda([this](ECheckBoxState S){bRepeat=S==ECheckBoxState::Checked;RepeatAt=0;})[Text(TEXT("Повтор"))]]
                            +SWrapBox::Slot()[SNew(SCheckBox).IsChecked_Lambda([this]{return bLiveGallery?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                                .OnCheckStateChanged_Lambda([this](ECheckBoxState S){bLiveGallery=S==ECheckBoxState::Checked;if(bLiveGallery&&bLibrary&&LibraryAsset.IsValid())PreviewLibrary();else if(!bLiveGallery&&bPreviewingGallery)StopPlayback();})[Text(TEXT("Live Gallery"))]]]
                        +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox).Visibility_Lambda([this]{return bUseActionPreview?EVisibility::Collapsed:EVisibility::Visible;})
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Вписать эффект"),[this]{Viewport->Focus();})]
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Спереди"),[this]{Viewport->SetViewPreset(0);})]
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Сбоку"),[this]{Viewport->SetViewPreset(1);})]]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,6)[SNew(SCheckBox).Visibility_Lambda([this]{return bUseActionPreview?EVisibility::Collapsed:EVisibility::Visible;}).OnCheckStateChanged_Lambda([this](ECheckBoxState S){Viewport->SetMotionEnabled(S==ECheckBoxState::Checked);})[Text(TEXT("Движение зубика (проверка следа)"))]]
                        +SVerticalBox::Slot().AutoHeight()[SNew(SSlider).Visibility_Lambda([this]{return bUseActionPreview?EVisibility::Collapsed:EVisibility::Visible;}).Value_Lambda([this]{return FMath::Clamp((bScrubbing?SeekTime:Viewport->GetPlaybackTime())/EndTime(),0.f,1.f);})
                            .OnMouseCaptureBegin_Lambda([this]{bScrubbing=true;SeekTime=Viewport->GetPlaybackTime();PausePlayback();})
                            .OnValueChanged_Lambda([this](float V){SeekTime=V*EndTime();})
                            .OnMouseCaptureEnd_Lambda([this]{bScrubbing=false;Viewport->Seek(SeekTime);})]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,5)[SNew(STextBlock).Text_Lambda([this]{if(bUseActionPreview){auto R=MCPerkActionPreviewPanel::GetActiveRunner();return FText::FromString(R?R->GetStatus():TEXT("Песочница действия закрыта"));}return FText::FromString(FString::Printf(TEXT("%.2f / %.2f с"),Viewport->GetPlaybackTime(),EndTime()));}).AutoWrapText(true)]
                        +SVerticalBox::Slot().AutoHeight()[SNew(SExpandableArea).InitiallyCollapsed(true).HeaderContent()[Text(TEXT("Диагностика preview"))].BodyContent()[SNew(STextBlock).Text_Lambda([this]{if(bUseActionPreview){auto R=MCPerkActionPreviewPanel::GetActiveRunner();return FText::FromString(R?R->GetEventLog():TEXT("Нет активного runner"));}return FText::FromString(Viewport->GetDiagnostics());}).AutoWrapText(true)]]]
                    +SSplitter::Slot().Value(.47f)[SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight().Padding(6,0,6,10)[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)[SNew(SButton).ButtonStyle(&SolidButtonStyle()).ContentPadding(FMargin(16,12))
                                .ButtonColorAndOpacity_Lambda([this]{return bLibrary?Mint:FLinearColor(.13f,.18f,.21f);}).ForegroundColor_Lambda([this]{return FSlateColor(bLibrary?PanelInk:FLinearColor::White);})
                                .OnClicked_Lambda([this]{ShowLibrary();return FReply::Handled();})[Text(TEXT("Библиотека"),14)]]
                            +SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).ButtonStyle(&SolidButtonStyle()).ContentPadding(FMargin(16,12))
                                .ButtonColorAndOpacity_Lambda([this]{return !bLibrary?Mint:FLinearColor(.13f,.18f,.21f);}).ForegroundColor_Lambda([this]{return FSlateColor(!bLibrary?PanelInk:FLinearColor::White);})
                                .OnClicked_Lambda([this]{ShowComposition();return FReply::Handled();})[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(FString::Printf(TEXT("Композиция (%d)"),Working->Layers.Num()));}).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),14))]]]
                        +SVerticalBox::Slot().FillHeight(1)[SNew(SWidgetSwitcher).WidgetIndex_Lambda([this]{return bLibrary?1:0;})
                            +SWidgetSwitcher::Slot()[SNew(SOverlay)
                                +SOverlay::Slot()[SAssignNew(Graph,SRecipeGraph).Document_Lambda([this]{return Working.Get();}).Selection_Lambda([this]{return Selection;}).ResultSelected_Lambda([this]{return !Primary.IsValid();})
                                    .Visibility_Lambda([this]{return HasEffects()?EVisibility::Visible:EVisibility::Collapsed;})
                                    .OnSelect_Lambda([this](FGuid ID,bool Ctrl){Select(ID,Ctrl);}).OnBeginMove_Lambda([this](FGuid ID){BeginMove(ID);}).OnMove_Lambda([this](FVector2D Delta){Move(Delta);}).OnEndMove_Lambda([this]{EndMove();})
                                    .OnEnable_Lambda([this](FGuid ID,bool V){Toggle(ID,V,false);}).OnSolo_Lambda([this](FGuid ID,bool V){Toggle(ID,V,true);})
                                    .OnDelete_Lambda([this]{Delete();}).OnDuplicate_Lambda([this]{Duplicate();}).OnGroup_Lambda([this]{GroupSelection();}).OnSelectAll_Lambda([this]{SelectAll();})]
                                +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[SNew(SBox).WidthOverride(360)
                                    .Visibility_Lambda([this]{return HasEffects()?EVisibility::Collapsed:EVisibility::Visible;})
                                    [SNew(SBorder).Padding(26).BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                                        [SNew(SVerticalBox)
                                            +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[Text(TEXT("Композиция пуста"),20)]
                                            +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,20)[Text(TEXT("Выберите эффект в библиотеке, посмотрите его и добавьте в композицию. Здесь появятся его узел и настройки."),14)]
                                            +SVerticalBox::Slot().AutoHeight()[PrimaryButton(TEXT("Выбрать первый эффект"),[this]{ShowLibrary();})]]]]]
                            +SWidgetSwitcher::Slot()[SNew(SVerticalBox)
                                +SVerticalBox::Slot().AutoHeight().Padding(10,2,10,10)[Text(TEXT("Клик — посмотреть. «Добавить» — включить в композицию."),14)]
                                +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                                    +SHorizontalBox::Slot().FillWidth(1).Padding(4)[Button(TEXT("Эффекты игры"),[this]{FilterLibrary(TEXT("/Game/Gameplay/ActiveSkills/VFX"));})]
                                    +SHorizontalBox::Slot().FillWidth(1).Padding(4)[Button(TEXT("Учебные"),[this]{FilterLibrary(TEXT("/Game/Tests/VFXPractice/Effects"));})]
                                    +SHorizontalBox::Slot().FillWidth(1).Padding(4)[Button(TEXT("Все эффекты"),[this]{FilterLibrary(TEXT("/Game"));})]]
                                +SVerticalBox::Slot().FillHeight(1)[Library]
                                +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SButton).ButtonStyle(&SolidButtonStyle()).ContentPadding(FMargin(18,13))
                                    .ButtonColorAndOpacity_Lambda([this]{return bLibrarySelection&&LibraryAsset.IsValid()?Mint:DisabledButton;})
                                    .ForegroundColor_Lambda([this]{return FSlateColor(bLibrarySelection&&LibraryAsset.IsValid()?PanelInk:DisabledInk);})
                                    .IsEnabled_Lambda([this]{return bLibrarySelection&&LibraryAsset.IsValid();})
                                    .ToolTipText(LOCTEXT("AddLibraryHint","Выберите карточку эффекта. Эта кнопка добавит отдельный настраиваемый элемент в композицию."))
                                    .OnClicked_Lambda([this]{AddLibrary();return FReply::Handled();})
                                    [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(bLibrarySelection&&LibraryAsset.IsValid()?TEXT("＋ Добавить: ")+EffectLabel(LibraryAsset.AssetName.ToString()):TEXT("Выберите карточку эффекта"));}).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),14)).AutoWrapText(true)]]]]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(SHorizontalBox).Visibility_Lambda([this]{return !bLibrary&&HasEffects()?EVisibility::Visible:EVisibility::Collapsed;})
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Дубликат · Ctrl+D"),[this]{Duplicate();})]
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Удалить · Del"),[this]{Delete();})]
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Группа · Ctrl+G"),[this]{GroupSelection();})]
                            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Вписать · F"),[this]{Graph->FrameAll();})]]
                        +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).Visibility_Lambda([this]{return !bLibrary&&HasEffects()?EVisibility::Visible:EVisibility::Collapsed;})
                            .Text(LOCTEXT("GraphHelp","Перетаскивайте заголовки узлов. Ctrl+клик — выбрать несколько; ПКМ — панорама; колесо — масштаб."))
                            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11)).AutoWrapText(true)]]
                    +SSplitter::Slot().Value(.23f)[SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Section(TEXT("Настройки"))]
                        +SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(Details,SVerticalBox)]]]]
                +SVerticalBox::Slot().AutoHeight().Padding(8,5)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Status);}).AutoWrapText(true)]
                +SVerticalBox::Slot().AutoHeight().Padding(12,0,12,8)[Text(TEXT("Работаете с копией. Исходные эффекты и игровой профиль меняются только через явное сохранение и применение."),10)]];
            RefreshDetails();
            PreviewTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(this,&SRecipeComposer::TickPlayback),.05f);
        }
        virtual ~SRecipeComposer()override{FTSTicker::GetCoreTicker().RemoveTicker(PreviewTicker);StopPlayback();MoveTransaction.Reset();if(GEditor)GEditor->UnregisterForUndo(this);}
        void SetOwnerTab(const TSharedRef<SDockTab>& Tab){OwnerTab=Tab;}
        void Closed(){StopPlayback();}
        virtual void PostUndo(bool Success)override{if(Success){StopPlayback();bDirty=true;LastExport.Reset();Selection.Reset();Primary.Invalidate();Refresh(true);}}
        virtual void PostRedo(bool Success)override{PostUndo(Success);}
        bool CanClose(){if(!bDirty)return true;auto C=FMessageDialog::Open(EAppMsgType::YesNoCancel,LOCTEXT("Unsaved","Сохранить рабочую композицию в новый пресет?\nДа — сохранить; Нет — закрыть без сохранения; Отмена — вернуться."));return C==EAppReturnType::No || (C==EAppReturnType::Yes&&SaveNew());}
        UMCVFXRecipePreset* GetWorking()const{return Working.Get();}
        void SetContext(const FRecipeContext& C)
        {
            if(!CanClose())return;Context=C;NewWorking();
            bUseActionPreview=C.Profile.IsValid() && C.Profile.Get()==MCPerkActionPreviewPanel::GetWorkingProfile();
            if(ContextPreviewBox){if(bUseActionPreview)ContextPreviewBox->SetContent(MCPerkActionPreviewPanel::MakeSharedActionViewport());else ContextPreviewBox->SetContent(SNullWidget::NullWidget);}
            const FMCPerkVFXSlot* Slot=C.Profile.IsValid()?C.Profile->Slots.Find(C.Stage):nullptr;
            if(Slot && Slot->Mode==EMCPerkVFXSlotMode::Replace && !Slot->System.IsNull())
            {
                if(UNiagaraSystem* NS=Slot->System.LoadSynchronous())
                {
                    const FString MasterPath=NS->GetOutermost()->GetMetaData().GetValue(NS,TEXT("MCComposerSourceRecipe"));
                    if(!MasterPath.IsEmpty())
                    {
                        auto* Master=MasterPath.StartsWith(TEXT("/Game/"))?LoadObject<UMCVFXRecipePreset>(nullptr,*MasterPath):nullptr;
                        FString Error;
                        if(!Master || Master->HasAnyFlags(RF_Transient) || Master->GetOutermost()->IsDirty() || !Master->ValidateLayout(Error))
                        {
                            Refresh(true);Status=TEXT("Не удалось восстановить сохранённый исходник композиции: ")+MasterPath+TEXT(". Экспортированная NS не заменена плоским узлом; профиль не изменён. ")+Error;return;
                        }
                        // Reopen the authoring master, not its generated output as an opaque source node.
                        if(!Load(Master))return;
                        EventOuterSettings=*Slot;bPreserveEventOuterSettings=true;
                        LastExport=NS;RefreshDetails();
                        Status=TEXT("Восстановлен исходник композиции: ")+MasterPath+TEXT(". Внешний transform/User overrides события сохранены отдельно и имеют приоритет в действии.");return;
                    }
                }
                FMCVFXCompositionLayer L=UMCPerkVFXProfile::AsLayer(*Slot);L.Name=FName(*Slot->System.GetAssetName());Working->Layers.Add(L);AddLayout();bDirty=false;
            }
            else if(C.Profile.IsValid() && C.Profile.Get()==MCPerkActionPreviewPanel::GetWorkingProfile())
                if(UNiagaraSystem* System=MCPerkActionPreviewPanel::ResolveWorkingStageSystem(C.Stage)){AddSystem(System);bDirty=false;}
            bLibrary=!HasEffects();Refresh(true);Status=TEXT("Открыто событие перка. Выберите элемент, чтобы настроить его, или добавьте новый из библиотеки.");
        }
        bool Load(UMCVFXComposition* Source)
        {
            if(!IsValid(Source)||!CanClose())return false;
            StopPlayback();
            if(const auto* Recipe=Cast<UMCVFXRecipePreset>(Source))
            {
                FString Error;if(!Recipe->ValidateLayout(Error)){Status=Error;return false;}
                Working.Reset(DuplicateObject<UMCVFXRecipePreset>(Recipe,GetTransientPackage(),MakeUniqueObjectName(GetTransientPackage(),UMCVFXRecipePreset::StaticClass(),TEXT("WorkingRecipe"))));
            }
            else
            {
                NewWorking();Working->Layers=Source->Layers;Working->DurationSeconds=Source->DurationSeconds;Working->TailSeconds=Source->TailSeconds;
                for(int32 I=0;I<Working->Layers.Num();++I)AddLayout();
                Working->PresetName=Source->GetName();
            }
            Working->ClearFlags(RF_Public|RF_Standalone);Working->SetFlags(RF_Transient|RF_Transactional);
            const bool ImportedLegacy=!Source->IsA<UMCVFXRecipePreset>();
            bDirty=ImportedLegacy;bLibrary=!HasEffects();bLibrarySelection=false;bPreviewingGallery=false;LibraryAsset=FAssetData();LastExport.Reset();SourcePath=ImportedLegacy?FString():Source->GetPathName();Selection.Reset();Primary.Invalidate();Refresh(true);
            Status=ImportedLegacy?TEXT("Старая композиция импортирована с новыми идентификаторами узлов. Перед экспортом сохраните новый исходник пресета."):TEXT("Открыта независимая копия: ")+SourcePath;return true;
        }
        bool AddSystem(UNiagaraSystem* System)
        {
            if(!System || Working->Layers.Num()>=32){Status=TEXT("Выберите NS. Максимум композиции — 32 элемента.");return false;}
            StopPlayback();
            const FScopedTransaction Tx(LOCTEXT("Add","VFX: добавить элемент"));Working->Modify();
            FMCVFXCompositionLayer L;L.Name=FName(*System->GetName());L.System=System;Working->Layers.Add(L);AddLayout();
            FString DiscoverError;Working->DiscoverParameters(Working->Layers.Num()-1,DiscoverError);
            Primary=Working->Nodes.Last().ID;Selection={Primary};bLibrary=false;Changed();Graph->FrameAll();
            Status=TEXT("Добавлен ")+System->GetName()+TEXT(". Настройте эффект справа и нажмите «Просмотреть».");
            if(!DiscoverError.IsEmpty())Status+=TEXT(" Параметры: ")+DiscoverError;return true;
        }
        UNiagaraSystem* Export(const FString& LongPackageName,FString& Error)
        {
            if(!HasEnabledEffects()){Error=TEXT("Композиция пуста или все эффекты выключены. Сначала добавьте эффект из библиотеки.");Status=Error;return nullptr;}
            auto* SavedSource=GetSavedSource(Error);if(!SavedSource){Status=Error;return nullptr;}
            UNiagaraSystem* NS=UMCVFXComposerBuilderLibrary::ExportNew(SavedSource,LongPackageName,Error);
            if(NS){LastExport=NS;Status=TEXT("Сохранена новая единая NS: ")+NS->GetPathName();RefreshDetails();}
            else Status=TEXT("Экспорт не выполнен: ")+Error;
            return NS;
        }
        bool Apply(FString& Error)
        {
            Error.Reset();
            if(Context.Stage.IsNone()||!Context.Profile.IsValid()){Error=TEXT("Откройте Composer из VFX-события перка.");return false;}
            if(!LastExport.IsValid()||LastExport->HasAnyFlags(RF_Transient)||LastExport->GetOutermost()->IsDirty())
            {Error=TEXT("Сначала экспортируйте текущую композицию в новую сохранённую NS.");return false;}
            FMCPerkVFXSlot Slot;if(!MakeEventSlot(LastExport.Get(),Slot,Error))return false;
            if(Context.OnApply.IsBound())Context.OnApply.Execute(Slot);
            else
            {
                if(Context.Profile.Get()!=MCPerkActionPreviewPanel::GetWorkingProfile())
                {Error=TEXT("Целевая рабочая копия профиля уже изменилась. Откройте Composer из события заново.");return false;}
                if(!MCPerkActionPreviewPanel::SetWorkingSlot(Context.Stage,Slot,Error))return false;
            }
            if(bPreserveEventOuterSettings)EventOuterSettings=Slot;
            Status=TEXT("Экспортированная NS назначена событию в рабочей копии профиля. DT не изменён; вернитесь к полному действию и повторите сценарий.");return true;
        }
        UMCVFXRecipePreset* SaveAs(const FString& Path,FString& Error)
        {
            Error.Reset();
            if(!Working->ValidateLayout(Error)||!Working->Validate(Error)){Status=Error;return nullptr;}
            for(const auto& L:Working->Layers)if(L.bEnabled){auto* NS=L.System.LoadSynchronous();if(!NS||!UMCVFXComposition::ValidateParameters(L,NS,Error)){if(!NS)Error=TEXT("Источник NS недоступен.");Status=Error;return nullptr;}}
            const FString PackageName=FPackageName::ObjectPathToPackageName(Path);
            if(!FPackageName::IsValidObjectPath(Path)||!PackageName.StartsWith(TEXT("/Game/"))||FPackageName::DoesPackageExist(PackageName)||FindObject<UPackage>(nullptr,*PackageName))
            {Error=TEXT("Выберите новое имя в /Game/. Существующие пакеты не перезаписываются.");Status=Error;return nullptr;}
            UPackage* P=CreatePackage(*PackageName);auto* Saved=DuplicateObject<UMCVFXRecipePreset>(Working.Get(),P,*FPackageName::ObjectPathToObjectName(Path));Saved->ClearFlags(RF_Transient);Saved->SetFlags(RF_Public|RF_Standalone|RF_Transactional);FAssetRegistryModule::AssetCreated(Saved);P->MarkPackageDirty();
            if(!UEditorLoadingAndSavingUtils::SavePackages({P},true)){Error=TEXT("Не удалось сохранить. Рабочая копия сохранена в окне.");Status=Error;return nullptr;}
            SourcePath=Saved->GetPathName();bDirty=false;Status=TEXT("Новый пресет сохранён вместе с узлами и группами: ")+SourcePath;
            FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().SyncBrowserToAssets(TArray<UObject*>{Saved});return Saved;
        }
    private:
        TStrongObjectPtr<UMCVFXRecipePreset> Working;
        TSharedPtr<SMCVFXComposerViewport> Viewport;TSharedPtr<SRecipeGraph> Graph;TSharedPtr<SVerticalBox> Details;
        TSharedPtr<SBox> ContextPreviewBox;
        TSharedPtr<IStructureDetailsView> Advanced;TSharedPtr<FStructOnScope> LayerProxy;
        TSharedPtr<FAssetThumbnailPool> DetailPool;TArray<TSharedPtr<FAssetThumbnail>> DetailThumbnails;
        TStrongObjectPtr<UMCVFXComposition> LibraryPreview;
        TStrongObjectPtr<UNiagaraSystem> AuditionSystem;
        /** Last effective visual, independent of document layout/grouping and of the action's own snapshot. */
        TStrongObjectPtr<UMCVFXComposition> AuditionRecipe;
        TStrongObjectPtr<UNiagaraSystem> CachedAuditionSystem;
        TArray<FGuid> CachedAuditionNodeIDs;
        TUniquePtr<FScopedTransaction> MoveTransaction;
        FAssetData LibraryAsset;
        TSharedPtr<FAssetThumbnailPool> LibraryPool;
        TSharedPtr<STileView<FRecipeLibraryItem>> LibraryView;
        TSharedPtr<SEditableTextBox> LibrarySearchBox;
        TArray<FAssetData> LibraryCatalog;
        TArray<FRecipeLibraryItem> LibraryItems;
        FString LibraryPath,LibrarySearch;
        FMCPerkVFXSlot EventOuterSettings;bool bPreserveEventOuterSettings=false;
        TWeakObjectPtr<UNiagaraSystem> LastExport;
        TSet<FGuid> Selection;FGuid Primary;FRecipeContext Context;
        FString SourcePath,Status=TEXT("Кликните карточку эффекта, чтобы посмотреть его. Затем нажмите «Добавить», чтобы настроить свою композицию.");
        bool bDirty=false,bLibrary=true,bLibrarySelection=false,bRefresh=false,bScrubbing=false,bUseActionPreview=false;float SeekTime=0;
        TWeakPtr<SDockTab> OwnerTab;
        FTSTicker::FDelegateHandle PreviewTicker;
        bool bRepeat=true,bLiveGallery=true,bPlaybackRequested=false,bObservedActive=false,bPreviewingGallery=false,bOwnsPreview=false;
        double RepeatAt=0;
        float EndTime()const{return FMath::Max(.05f,Working->DurationSeconds+Working->TailSeconds);}
        int32 Index()const{return Working->FindNode(Primary);}
        bool HasEffects()const{return Working.IsValid()&&!Working->Layers.IsEmpty();}
        bool HasEnabledEffects()const{if(Working.IsValid())for(const auto& L:Working->Layers)if(L.bEnabled&&!L.System.IsNull())return true;return false;}
        bool CanPlay()const{return (bLibrary&&bLibrarySelection&&LibraryAsset.IsValid())||HasEnabledEffects();}
        FString ContextCaption()const
        {
            if(Context.Stage.IsNone())return TEXT("Свободная композиция");
            FString Stage=Context.Stage.ToString();
            if(Context.Stage==TEXT("Start"))Stage=TEXT("Выстрел");else if(Context.Stage==TEXT("Loop"))Stage=TEXT("След ракеты");
            else if(Context.Stage==TEXT("Impact"))Stage=TEXT("Попадание");else if(Context.Stage==TEXT("End"))Stage=TEXT("Завершение");else if(Context.Stage==TEXT("Warning"))Stage=TEXT("Предупреждение");
            return (Context.PerkID==TEXT("Consumable_Rocket")?FString(TEXT("Ракетница")):Context.PerkID.ToString())+TEXT("  →  ")+Stage;
        }
        void ShowLibrary(){bLibrary=true;RefreshDetails();if(bLiveGallery&&LibraryAsset.IsValid())PreviewLibrary();}
        void ShowComposition()
        {
            const bool bWasGallery=bPreviewingGallery;
            bLibrary=false;bLibrarySelection=false;
            if(bWasGallery&&HasEnabledEffects())PrepareAndPlay();
            else {StopPlayback();bPreviewingGallery=false;}
            RefreshDetails();
        }
        TSharedRef<SWidget> Button(const FString& S,TFunction<void()> F){return SNew(SButton).ContentPadding(FMargin(12,8)).OnClicked_Lambda([F=MoveTemp(F)]{F();return FReply::Handled();})[Text(S,12)];}
        TSharedRef<SWidget> PrimaryButton(const FString& S,TFunction<void()> F){return SNew(SButton).ButtonStyle(&SolidButtonStyle()).ContentPadding(FMargin(16,11)).ButtonColorAndOpacity(Mint).ForegroundColor(PanelInk).OnClicked_Lambda([F=MoveTemp(F)]{F();return FReply::Handled();})[Text(S,14)];}
        TSharedRef<SWidget> Section(const FString& S){return SNew(SBorder).Padding(12).BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))[Text(S,14)];}
        void NewWorking(){StopPlayback();bLibrary=true;bLibrarySelection=false;bPreviewingGallery=false;LibraryAsset=FAssetData();if(LibraryView)LibraryView->ClearSelection();Working.Reset(NewObject<UMCVFXRecipePreset>(GetTransientPackage(),NAME_None,RF_Transient|RF_Transactional));LastExport.Reset();SourcePath.Reset();Selection.Reset();Primary.Invalidate();bDirty=false;bPreserveEventOuterSettings=false;EventOuterSettings=FMCPerkVFXSlot();SeekTime=0;bScrubbing=false;}
        void NewRecipe(){if(CanClose()){StopPlayback();Context=FRecipeContext();bUseActionPreview=false;NewWorking();Refresh(true);Status=TEXT("Выберите карточку эффекта в библиотеке. Сначала можно посмотреть, затем добавить в композицию.");}}
        void AddLayout()
        {
            const int32 I=Working->Nodes.Num();FMCVFXRecipeNodeLayout N;N.ID=FGuid::NewGuid();N.Position=FVector2D(50+(I%2)*300,60+(I/2)*220);Working->Nodes.Add(N);
        }
        void Changed(){StopPlayback();bPreviewingGallery=false;if(!HasEffects())bLibrary=true;bDirty=true;bLibrarySelection=false;LastExport.Reset();Refresh(false);Status=TEXT("Композиция изменена. Нажмите «Просмотреть», чтобы оценить результат.");}
        void Refresh(bool bFit)
        {
            if(Graph){Graph->Build();if(bFit)Graph->FrameAll();}
            RefreshDetails();
            if(Viewport)Viewport->SetComposition(Working->Layers.IsEmpty()?nullptr:Working.Get());
            if(bUseActionPreview && Working->Layers.IsEmpty())MCPerkActionPreviewPanel::ResetPreview();
        }
        void Select(FGuid ID,bool Ctrl)
        {
            bLibrarySelection=false;if(!Ctrl)Selection.Reset();if(ID.IsValid()){if(Ctrl&&Selection.Contains(ID))Selection.Remove(ID);else Selection.Add(ID);}Primary=ID;RefreshDetails();
        }
        void SelectAll()
        {Selection.Reset();for(const auto& N:Working->Nodes)Selection.Add(N.ID);Primary=Working->Nodes.IsEmpty()?FGuid():Working->Nodes.Last().ID;bLibrarySelection=false;RefreshDetails();}
        void BeginMove(FGuid ID)
        {
            if(!Selection.Contains(ID))Selection={ID};Primary=ID;RefreshDetails();
            MoveTransaction=MakeUnique<FScopedTransaction>(LOCTEXT("Move","VFX: переместить узлы"));Working->Modify();
        }
        void Move(FVector2D Delta){for(auto& N:Working->Nodes)if(Selection.Contains(N.ID))N.Position+=Delta;bDirty=true;Graph->Invalidate(EInvalidateWidgetReason::Layout);}
        void EndMove(){MoveTransaction.Reset();}
        void Toggle(FGuid ID,bool V,bool Solo)
        {const int32 I=Working->FindNode(ID);if(I==INDEX_NONE)return;const FScopedTransaction Tx(LOCTEXT("Toggle","VFX: включение / Solo"));Working->Modify();if(Solo)Working->Layers[I].bSolo=V;else Working->Layers[I].bEnabled=V;Changed();}
        void Duplicate()
        {
            if(Selection.IsEmpty() || Working->Layers.Num()+Selection.Num()>32)return;
            const FScopedTransaction Tx(LOCTEXT("Duplicate","VFX: дублировать элементы"));Working->Modify();const TSet<FGuid> Old=Selection;Selection.Reset();
            const int32 Count=Working->Layers.Num();for(int32 I=0;I<Count;++I)if(Old.Contains(Working->Nodes[I].ID))
            {FMCVFXCompositionLayer L=Working->Layers[I];L.Name=FName(*(L.Name.ToString()+TEXT("_Copy")));FMCVFXRecipeNodeLayout N=Working->Nodes[I];N.ID=FGuid::NewGuid();N.Position+=FVector2D(45,45);Working->Layers.Add(L);Working->Nodes.Add(N);Selection.Add(N.ID);Primary=N.ID;}
            Changed();
        }
        void Delete()
        {
            if(Selection.IsEmpty())return;const FScopedTransaction Tx(LOCTEXT("Delete","VFX: удалить элементы"));Working->Modify();
            for(int32 I=Working->Nodes.Num()-1;I>=0;--I)if(Selection.Contains(Working->Nodes[I].ID)){Working->Nodes.RemoveAt(I);Working->Layers.RemoveAt(I);}
            Selection.Reset();Primary.Invalidate();Changed();
        }
        void GroupSelection()
        {
            if(Selection.IsEmpty())return;const FScopedTransaction Tx(LOCTEXT("Group","VFX: сгруппировать элементы"));Working->Modify();FMCVFXRecipeGroup G;G.ID=FGuid::NewGuid();G.Label=FString::Printf(TEXT("Группа %d"),Working->Groups.Num()+1);Working->Groups.Add(G);
            for(auto& N:Working->Nodes)if(Selection.Contains(N.ID))N.Group=G.ID;Changed();
        }
        void FilterLibrary(const TCHAR* Path)
        {
            StopPlayback();LibraryAsset=FAssetData();bLibrarySelection=false;LibraryPath=Path;
            FARFilter Filter;Filter.ClassPaths.Add(UNiagaraSystem::StaticClass()->GetClassPathName());Filter.bRecursiveClasses=true;
            Filter.PackagePaths.Add(FName(Path));Filter.bRecursivePaths=true;LibraryCatalog.Reset();
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssets(Filter,LibraryCatalog);
            LibraryCatalog.Sort([](const FAssetData& A,const FAssetData& B){return A.AssetName.LexicalLess(B.AssetName);});
            RefreshLibrary();RefreshDetails();
        }
        void RefreshLibrary()
        {
            StopPlayback();LibraryAsset=FAssetData();bLibrarySelection=false;bPreviewingGallery=false;
            if(LibraryView)LibraryView->ClearSelection();
            LibraryItems.Reset();
            for(const auto& Asset:LibraryCatalog)
                if(LibrarySearch.IsEmpty() || EffectLabel(Asset.AssetName.ToString()).Contains(LibrarySearch) || Asset.AssetName.ToString().Contains(LibrarySearch) || Asset.PackageName.ToString().Contains(LibrarySearch))
                    LibraryItems.Add(MakeShared<FAssetData>(Asset));
            if(LibraryView)LibraryView->RequestListRefresh();
            Status=LibraryItems.IsEmpty()?TEXT("Ничего не найдено. Измените запрос или очистите поиск."):TEXT("Выберите карточку для просмотра. «Добавить» включит её в композицию.");
            RefreshDetails();
        }
        void SelectLibrary(const FAssetData& A)
        {
            // Selection is an audition, never an implicit graph edit. Cancel the previous
            // component/action before a new resource can wait on shader readiness.
            StopPlayback();LibraryAsset=A;bLibrarySelection=true;RefreshDetails();
            if(bLiveGallery&&bLibrary&&LibraryAsset.IsValid())PreviewLibrary();
        }
        void AddLibrary(){if(auto* NS=Cast<UNiagaraSystem>(LibraryAsset.GetAsset()))AddSystem(NS);else Status=TEXT("Выберите Niagara System в библиотеке.");}
        void StopPlayback()
        {
            bPlaybackRequested=false;bObservedActive=false;RepeatAt=0;
            if(Viewport)Viewport->SetComposition(nullptr);
            if(bOwnsPreview&&bUseActionPreview)MCPerkActionPreviewPanel::ResetPreview();
            bOwnsPreview=false;
        }
        void PausePlayback()
        {
            bPlaybackRequested=false;bObservedActive=false;RepeatAt=0;
            if(bUseActionPreview){if(bOwnsPreview)if(auto R=MCPerkActionPreviewPanel::GetActiveRunner())R->SetPaused(true);}
            else if(Viewport)Viewport->Pause();
            Status=TEXT("Пауза. Автоповтор остановлен; Play начинает новый просмотр.");
        }
        void PlaybackStarted(bool bGallery)
        {bPreviewingGallery=bGallery;bPlaybackRequested=true;bObservedActive=false;bOwnsPreview=true;RepeatAt=0;}
        bool TickPlayback(float)
        {
            if(!bPlaybackRequested)return true;
            const auto Tab=OwnerTab.Pin();
            const auto Window=FSlateApplication::Get().FindWidgetWindow(AsShared());
            if(!Tab || !Tab->IsForeground() || !Window || !Window->IsVisible() || Window->IsWindowMinimized())
            {
                StopPlayback();Status=TEXT("Предпросмотр остановлен: вкладка скрыта. Нажмите Play или выберите элемент библиотеки.");return true;
            }
            bool Active=false;
            if(bUseActionPreview)
            {
                auto R=MCPerkActionPreviewPanel::GetActiveRunner();
                if(!R || Context.Profile.Get()!=MCPerkActionPreviewPanel::GetWorkingProfile()){StopPlayback();return true;}
                if(R->IsPaused()){bPlaybackRequested=false;RepeatAt=0;return true;}
                Active=R->IsRunning();
                // A failed preparation is not a completed action to repeat forever.
                if(!Active&&R->GetEvents().IsEmpty()){bPlaybackRequested=false;Status=R->GetStatus();return true;}
            }
            else Active=Viewport&&Viewport->IsPlaying();
            if(Active){bObservedActive=true;RepeatAt=0;return true;}
            // IsPlaying may be false while Niagara is still compiling. Never enqueue
            // another run until this one has actually been seen running and finished.
            if(!bObservedActive)return true;
            if(!bRepeat){bPlaybackRequested=false;RepeatAt=0;return true;}
            const double Now=FPlatformTime::Seconds();
            if(RepeatAt==0){RepeatAt=Now+.6;return true;}
            if(Now>=RepeatAt){if(bPreviewingGallery)PreviewLibrary();else PrepareAndPlay();}
            return true;
        }
        void PreviewLibrary()
        {
            StopPlayback();bPreviewingGallery=true;
            auto* NS=Cast<UNiagaraSystem>(LibraryAsset.GetAsset());
            if(!NS){Status=TEXT("Выберите Niagara System в библиотеке.");return;}
            if(bUseActionPreview)
            {
                if(Context.Profile.Get()!=MCPerkActionPreviewPanel::GetWorkingProfile()){Status=TEXT("Контекст перка устарел. Откройте событие заново.");return;}
                // A single gallery NS needs no merge/compilation. Preserve the event's
                // actual outer transform and typed overrides; incompatible ones reject.
                FMCPerkVFXSlot Slot;
                if(const auto* Current=Context.Profile->Slots.Find(Context.Stage))
                    if(Current->Mode==EMCPerkVFXSlotMode::Replace)Slot=*Current;
                Slot.Mode=EMCPerkVFXSlotMode::Replace;Slot.System=NS;
                FString Error;
                if(!UMCVFXComposition::ValidateParameters(UMCPerkVFXProfile::AsLayer(Slot),NS,Error))
                {Status=TEXT("Элемент не подходит параметрам события: ")+Error;return;}
                AuditionSystem.Reset(NS);
                if(!MCPerkActionPreviewPanel::AuditionWorkingSlot(Context.Stage,Slot,Error)){Status=Error;return;}
            }
            else
            {
                LibraryPreview.Reset(NewObject<UMCVFXComposition>(GetTransientPackage(),NAME_None,RF_Transient));
                FMCVFXCompositionLayer L;L.Name=NS->GetFName();L.System=NS;LibraryPreview->Layers.Add(L);
                Viewport->SetComposition(LibraryPreview.Get());Viewport->Play();
            }
            PlaybackStarted(true);
            Status=TEXT("Галерея: ")+NS->GetName()+TEXT(". Один реальный элемент; граф, профиль и исходная NS не изменены. «ГРАФ» возвращает композицию.");
        }
        void PrepareAndPlay()
        {
            StopPlayback();bPreviewingGallery=false;
            if(!HasEnabledEffects()){Status=TEXT("Сначала добавьте эффект из библиотеки или включите существующий элемент.");return;}
            FString Error;if(!Working->ValidateLayout(Error)||!Working->Validate(Error)){Status=Error;return;}
            if(!Context.Stage.IsNone())
            {
                if(!bUseActionPreview || Context.Profile.Get()!=MCPerkActionPreviewPanel::GetWorkingProfile()){Status=TEXT("Контекст события больше не активен. Откройте Composer из перка заново.");return;}
                UNiagaraSystem* Result=GetOrBuildAudition(Working.Get(),Error);
                if(!Result){Status=TEXT("Предпросмотр в действии не выполнен: ")+Error;return;}
                AuditionSystem.Reset(Result);FMCPerkVFXSlot Slot;if(!MakeEventSlot(Result,Slot,Error)){Status=Error;return;}
                if(!MCPerkActionPreviewPanel::AuditionWorkingSlot(Context.Stage,Slot,Error)){Status=Error;return;}
                PlaybackStarted(false);
                Status=TEXT("Настоящее действие с временной композицией выбранного события. Рабочий профиль и DT не изменены.");return;
            }
            Viewport->SetComposition(Working.Get());Viewport->Play();PlaybackStarted(false);Status=TEXT("Реальное preview самостоятельных NS. Имена controls — из поддержанных User inputs.");
        }
        void Edit(TFunction<void(FMCVFXCompositionLayer&)> F)
        {EditNode(Primary,MoveTemp(F));}
        void EditNode(FGuid ID,TFunction<void(FMCVFXCompositionLayer&)> F)
        {const int32 I=Working->FindNode(ID);if(I==INDEX_NONE)return;const FScopedTransaction Tx(LOCTEXT("Edit","VFX: изменить элемент"));Working->Modify();F(Working->Layers[I]);Changed();}
        void CommitColor(FLinearColor Value,FGuid ID,int32 Parameter)
        {EditNode(ID,[Parameter,Value](FMCVFXCompositionLayer& L){if(L.Colors.IsValidIndex(Parameter)){L.Colors[Parameter].Value=Value;L.Colors[Parameter].bOverride=true;}});}
        void Discover()
        {
            const int32 I=Index();if(I==INDEX_NONE)return;FString Error;const FScopedTransaction Tx(LOCTEXT("Discover","VFX: прочитать настоящие параметры"));Working->Modify();const bool OK=Working->DiscoverParameters(I,Error);Changed();Status=Error.IsEmpty()?(OK?TEXT("Параметры прочитаны из исходной NS. Отдельные типы доступны в расширенных настройках."):TEXT("Не удалось прочитать параметры.")):Error;
        }
        void RefreshDetails();
        UNiagaraSystem* GetOrBuildAudition(const UMCVFXComposition* Source,FString& Error)
        {
            Error.Reset();if(!Source){Error=TEXT("Нет композиции для предпросмотра.");return nullptr;}
            const auto* Preset=Cast<UMCVFXRecipePreset>(Source);
            if(Preset && !Preset->ValidateLayout(Error))return nullptr;
            bool AnySolo=false;for(const auto& L:Source->Layers)AnySolo|=L.bEnabled&&L.bSolo;
            TArray<FMCVFXCompositionLayer> EffectiveLayers;
            TArray<FMCVFXRecipeNodeLayout> EffectiveNodes;
            TArray<FGuid> EffectiveIDs;
            for(int32 I=0;I<Source->Layers.Num();++I)
            {
                const auto& L=Source->Layers[I];if(!L.bEnabled || (AnySolo&&!L.bSolo))continue;
                FMCVFXCompositionLayer Copy=L;Copy.bSolo=false;EffectiveLayers.Add(MoveTemp(Copy));
                if(Preset){EffectiveNodes.Add(Preset->Nodes[I]);EffectiveIDs.Add(Preset->Nodes[I].ID);}
                else EffectiveIDs.Add(FGuid(0,0,0,uint32(I+1)));
            }
            bool Same=AuditionRecipe.IsValid() && CachedAuditionSystem.IsValid()
                && CachedAuditionNodeIDs==EffectiveIDs && AuditionRecipe->Layers.Num()==EffectiveLayers.Num();
            for(int32 I=0;Same&&I<EffectiveLayers.Num();++I)
                Same=FMCVFXCompositionLayer::StaticStruct()->CompareScriptStruct(&AuditionRecipe->Layers[I],&EffectiveLayers[I],0);
            // Reuse the same object even while Niagara compilation is pending. The runner owns readiness.
            if(Same)return CachedAuditionSystem.Get();

            TStrongObjectPtr<UMCVFXComposition> Snapshot(DuplicateObject<UMCVFXComposition>(Source,GetTransientPackage(),
                MakeUniqueObjectName(GetTransientPackage(),Source->GetClass(),TEXT("RecipeAudition"))));
            Snapshot->ClearFlags(RF_Public|RF_Standalone|RF_Transactional);Snapshot->SetFlags(RF_Transient);
            Snapshot->Layers=MoveTemp(EffectiveLayers);
            if(auto* SnapshotPreset=Cast<UMCVFXRecipePreset>(Snapshot.Get()))SnapshotPreset->Nodes=MoveTemp(EffectiveNodes);
            UNiagaraSystem* Result=UMCVFXComposerBuilderLibrary::BuildTransient(Snapshot.Get(),Error);
            if(!Result)return nullptr;
            AuditionRecipe.Reset(Snapshot.Get());CachedAuditionSystem.Reset(Result);CachedAuditionNodeIDs=MoveTemp(EffectiveIDs);
            return Result;
        }
        UMCVFXRecipePreset* GetSavedSource(FString& Error)
        {
            Error.Reset();
            if(bDirty || SourcePath.IsEmpty())
            {Error=TEXT("Перед экспортом сохраните текущую композицию в новый исходник пресета. Экспорт не сохраняет и не перезаписывает исходник автоматически.");return nullptr;}
            auto* Saved=LoadObject<UMCVFXRecipePreset>(nullptr,*SourcePath);
            if(!Saved || Saved->HasAnyFlags(RF_Transient) || !SourcePath.StartsWith(TEXT("/Game/")) || Saved->GetOutermost()->IsDirty()
                || !FPackageName::DoesPackageExist(Saved->GetOutermost()->GetName()) || !Saved->ValidateLayout(Error))
            {if(Error.IsEmpty())Error=TEXT("Исходник должен быть постоянным чистым сохранённым RecipePreset. Сохраните новую версию пресета.");return nullptr;}
            bool Same=Saved->PresetName==Working->PresetName && Saved->DurationSeconds==Working->DurationSeconds && Saved->TailSeconds==Working->TailSeconds
                && Saved->Layers.Num()==Working->Layers.Num() && Saved->Nodes.Num()==Working->Nodes.Num() && Saved->Groups.Num()==Working->Groups.Num();
            for(int32 I=0;Same && I<Saved->Layers.Num();++I)
                Same=FMCVFXCompositionLayer::StaticStruct()->CompareScriptStruct(&Saved->Layers[I],&Working->Layers[I],0)
                    && FMCVFXRecipeNodeLayout::StaticStruct()->CompareScriptStruct(&Saved->Nodes[I],&Working->Nodes[I],0);
            for(int32 I=0;Same && I<Saved->Groups.Num();++I)Same=FMCVFXRecipeGroup::StaticStruct()->CompareScriptStruct(&Saved->Groups[I],&Working->Groups[I],0);
            if(!Same){bDirty=true;Error=TEXT("Рабочая копия отличается от сохранённого исходника. Сначала сохраните новый пресет.");return nullptr;}
            return Saved;
        }
        bool MakeEventSlot(UNiagaraSystem* System,FMCPerkVFXSlot& Slot,FString& Error)
        {
            Error.Reset();if(!System){Error=TEXT("Нет готовой NS для события.");return false;}
            if(bPreserveEventOuterSettings && Context.Profile.IsValid())
            {
                const auto* Current=Context.Profile->Slots.Find(Context.Stage);
                if(!Current || !FMCPerkVFXSlot::StaticStruct()->CompareScriptStruct(Current,&EventOuterSettings,0))
                {Error=TEXT("Настройки события изменились после открытия Composer. Откройте событие заново, чтобы не потерять его новые transform/overrides.");return false;}
            }
            Slot=bPreserveEventOuterSettings?EventOuterSettings:FMCPerkVFXSlot();Slot.Mode=EMCPerkVFXSlotMode::Replace;Slot.System=System;
            if(!UMCVFXComposition::ValidateParameters(UMCPerkVFXProfile::AsLayer(Slot),System,Error))
            {Error=TEXT("Внешние overrides события несовместимы с новым результатом; они не удалены автоматически. ")+Error;return false;}
            return true;
        }
        void ExportDialog()
        {
            if(!HasEnabledEffects()){Status=TEXT("Сначала добавьте и включите эффект из библиотеки.");return;}
            FString Error;
            auto* Saved=GetSavedSource(Error);if(!Saved){Status=Error;return;}
            if(!UMCVFXComposerBuilderLibrary::ValidateRecipe(Saved,Error)){Status=TEXT("Single-NS экспорт недоступен для этого рецепта: ")+Error;return;}
            FSaveAssetDialogConfig C;C.DialogTitleOverride=LOCTEXT("ExportTitle","Экспортировать поддержанную композицию в новую Niagara System");C.DefaultPath=TEXT("/Game/Tests/VFXPractice/Generated");C.DefaultAssetName=TEXT("NS_ComposedEffect");C.AssetClassNames.Add(UNiagaraSystem::StaticClass()->GetClassPathName());C.ExistingAssetPolicy=ESaveAssetDialogExistingAssetPolicy::Disallow;
            const FString Path=FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().CreateModalSaveAssetDialog(C);if(Path.IsEmpty())return;
            if(UNiagaraSystem* NS=Export(FPackageName::ObjectPathToPackageName(Path),Error))FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().SyncBrowserToAssets(TArray<UObject*>{NS});
        }
        void AdvancedChanged(const FPropertyChangedEvent&)
        {if(bRefresh||!LayerProxy||!LayerProxy->IsValid())return;const auto Copy=*reinterpret_cast<const FMCVFXCompositionLayer*>(LayerProxy->GetStructMemory());Edit([&Copy](FMCVFXCompositionLayer& L){L=Copy;});}
        void OpenSelected()
        {
            FOpenAssetDialogConfig Config;Config.DialogTitleOverride=LOCTEXT("OpenPreset","Открыть пресет композиции");Config.DefaultPath=TEXT("/Game/Tests/VFXPractice/Recipes");
            Config.AssetClassNames.Add(UMCVFXRecipePreset::StaticClass()->GetClassPathName());Config.AssetClassNames.Add(UMCVFXComposition::StaticClass()->GetClassPathName());Config.bAllowMultipleSelection=false;
            const auto Assets=FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().CreateModalOpenAssetDialog(Config);
            if(!Assets.IsEmpty())if(auto* Recipe=Cast<UMCVFXComposition>(Assets[0].GetAsset()))Load(Recipe);
        }
        bool SaveNew()
        {
            FSaveAssetDialogConfig Config;Config.DialogTitleOverride=LOCTEXT("Save","Сохранить новый исходник композиции");Config.DefaultPath=TEXT("/Game/Tests/VFXPractice/Recipes");Config.DefaultAssetName=TEXT("DA_VFXRecipe");Config.AssetClassNames.Add(UMCVFXRecipePreset::StaticClass()->GetClassPathName());Config.ExistingAssetPolicy=ESaveAssetDialogExistingAssetPolicy::Disallow;
            const FString Path=FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().CreateModalSaveAssetDialog(Config);if(Path.IsEmpty())return false;FString Error;return SaveAs(Path,Error)!=nullptr;
        }
    };

    void SRecipeComposer::RefreshDetails()
    {
        if(!Details)return;TGuardValue<bool> Guard(bRefresh,true);Details->ClearChildren();DetailThumbnails.Reset();
        auto Add=[this](const TSharedRef<SWidget>& W){Details->AddSlot().AutoHeight().Padding(9,5)[W];};
        if(bLibrarySelection && LibraryAsset.IsValid())
        {
            Add(Text(TEXT("Выбранный эффект"),16));
            auto Thumb=MakeShared<FAssetThumbnail>(LibraryAsset,160,160,DetailPool);DetailThumbnails.Add(Thumb);
            Add(SNew(SBox).HeightOverride(160)[Thumb->MakeThumbnailWidget()]);
            Add(Text(EffectLabel(LibraryAsset.AssetName.ToString()),14));
            Add(Text(TEXT("Сейчас вы только смотрите этот эффект. Добавьте его, чтобы менять цвет, размер и время появления."),14));
            Add(PrimaryButton(TEXT("＋ Добавить: ")+EffectLabel(LibraryAsset.AssetName.ToString()),[this]{AddLibrary();}));
            Add(Button(TEXT("Посмотреть ещё раз"),[this]{PreviewLibrary();}));
            Add(SNew(SExpandableArea).InitiallyCollapsed(true).HeaderContent()[Text(TEXT("Источник эффекта"))].BodyContent()[Text(LibraryAsset.GetSoftObjectPath().ToString(),10)]);return;
        }
        if(!HasEffects())
        {
            Add(Text(TEXT("Соберите первый эффект"),18));
            Add(Text(TEXT("1. Выберите карточку в библиотеке — эффект появится слева."),14));
            Add(Text(TEXT("2. Нажмите «Добавить», чтобы включить его в композицию."),14));
            Add(Text(TEXT("3. Настройте результат и сохраните свой пресет."),14));
            if(!bLibrary)Add(PrimaryButton(TEXT("Выбрать эффект"),[this]{ShowLibrary();}));
            Add(Text(TEXT("Можно сочетать несколько эффектов. Оригиналы останутся целыми."),12));return;
        }
        if(bLibrary)
        {
            Add(Text(TEXT("Выберите ещё один эффект"),18));
            Add(Text(TEXT("Кликните карточку, чтобы посмотреть эффект слева. Кнопка «Добавить» появится после выбора."),14));
            Add(Text(FString::Printf(TEXT("В вашей композиции уже %d эффектов."),Working->Layers.Num()),14));
            Add(Button(TEXT("Вернуться к композиции"),[this]{ShowComposition();}));return;
        }
        const int32 N=Index();
        if(N==INDEX_NONE)
        {
            Add(Text(TEXT("Общий результат"),16));
            Add(Text(FString::Printf(TEXT("Эффектов в композиции: %d"),Working->Layers.Num()),14));
            Add(Text(TEXT("Название пресета"),14));
            Add(SNew(SEditableTextBox).Text(FText::FromString(Working->PresetName)).OnTextCommitted_Lambda([this](const FText& V,ETextCommit::Type)
            {const FScopedTransaction Tx(LOCTEXT("RenamePreset","VFX: имя пресета"));Working->Modify();Working->PresetName=V.ToString();bDirty=true;}));
            Add(Text(TEXT("Длительность предпросмотра, с"),14));
            Add(SNew(SSpinBox<float>).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12)).MinValue(.05f).MaxValue(30.f).Value(Working->DurationSeconds).IsEnabled(!bUseActionPreview).OnValueCommitted_Lambda([this](float V,ETextCommit::Type)
            {const FScopedTransaction Tx(LOCTEXT("Duration","VFX: длительность композиции"));Working->Modify();Working->DurationSeconds=V;Changed();}));
            Add(Text(TEXT("Дать частицам погаснуть, с"),14));
            Add(SNew(SSpinBox<float>).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12)).MinValue(0.f).MaxValue(10.f).Value(Working->TailSeconds).IsEnabled(!bUseActionPreview).OnValueCommitted_Lambda([this](float V,ETextCommit::Type)
            {const FScopedTransaction Tx(LOCTEXT("Tail","VFX: хвост композиции"));Working->Modify();Working->TailSeconds=V;Changed();}));
            Add(Text(TEXT("Выберите узел в композиции, чтобы настроить отдельный эффект. Сохранение и экспорт — в верхней строке."),14));
            Add(SNew(SExpandableArea).InitiallyCollapsed(true).HeaderContent()[Text(TEXT("Дополнительно: экспорт и ограничения"))]
                .BodyContent()[SNew(SVerticalBox)
                    +SVerticalBox::Slot().AutoHeight().Padding(0,6)[Text(UMCVFXComposerBuilderLibrary::GetExportNotes(),11)]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,6)[Text(TEXT("Сначала сохраните новый пресет. Экспорт хранит ссылку на него и восстанавливает элементы и группы при повторном открытии. Внешние transform и User overrides события сохраняются отдельно."),11)]
                    +SVerticalBox::Slot().AutoHeight()[Button(TEXT("Проверить возможность экспорта"),[this]{FString E;Status=UMCVFXComposerBuilderLibrary::ValidateRecipe(Working.Get(),E)?TEXT("Композицию можно экспортировать в одну Niagara System."):TEXT("Экспорт недоступен: ")+E;})]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,6)[Text(SourcePath.IsEmpty()?TEXT("Пресет ещё не сохранён."):TEXT("Пресет: ")+SourcePath,10)]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,6)[Text(LastExport.IsValid()?TEXT("Экспорт: ")+LastExport->GetPathName():TEXT("Экспорт ещё не создан."),10)]]);
            return;
        }

        const FGuid ID=Primary;const FMCVFXCompositionLayer& L=Working->Layers[N];
        Add(Text(TEXT("Настройки эффекта"),16));
        Add(SNew(SEditableTextBox).Text(FText::FromName(L.Name)).OnTextCommitted_Lambda([this,ID](const FText& T,ETextCommit::Type){EditNode(ID,[T](FMCVFXCompositionLayer& V){V.Name=FName(*T.ToString());});}));
        auto Thumb=MakeShared<FAssetThumbnail>(FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetByObjectPath(L.System.ToSoftObjectPath()),120,120,DetailPool);DetailThumbnails.Add(Thumb);
        Add(SNew(SBox).HeightOverride(120)[Thumb->MakeThumbnailWidget()]);Add(Text(L.System.GetAssetName()));
        Add(Text(TEXT("Появиться через, с"),14));
        Add(SNew(SSpinBox<float>).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12)).MinValue(0.f).MaxValue(FMath::Max(0.f,Working->DurationSeconds-.001f)).Value(L.StartSeconds)
            .OnValueCommitted_Lambda([this,ID](float V,ETextCommit::Type){EditNode(ID,[V](FMCVFXCompositionLayer& X){X.StartSeconds=V;});}));
        Add(Text(TEXT("Эмиссия, с · 0 = до конца композиции")));
        Add(SNew(SSpinBox<float>).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12)).MinValue(0.f).MaxValue(30.f).Value(L.EmissionSeconds)
            .OnValueCommitted_Lambda([this,ID](float V,ETextCommit::Type){EditNode(ID,[V](FMCVFXCompositionLayer& X){X.EmissionSeconds=V;});}));
        Add(Text(TEXT("Масштаб всего элемента (не ParticleScale)")));
        Add(SNew(SSpinBox<float>).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12)).MinValue(.01f).MaxValue(50.f).Value(float(L.Transform.GetScale3D().X))
            .OnValueCommitted_Lambda([this,ID](float V,ETextCommit::Type){EditNode(ID,[V](FMCVFXCompositionLayer& X){X.Transform.SetScale3D(FVector(V));});}));
        if(L.Floats.IsEmpty() && L.Colors.IsEmpty() && L.Bools.IsEmpty() && L.Vectors.IsEmpty())Add(Text(TEXT("У этого эффекта нет доступных цветовых или числовых параметров. Можно изменить время появления и расположение."),12));
        for(int32 I=0;I<L.Floats.Num();++I)
        {
            const auto& P=L.Floats[I];
            Add(SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth()[SNew(SCheckBox).IsChecked(P.bOverride?ECheckBoxState::Checked:ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([this,ID,I](ECheckBoxState S){EditNode(ID,[I,S](FMCVFXCompositionLayer& V){if(V.Floats.IsValidIndex(I))V.Floats[I].bOverride=S==ECheckBoxState::Checked;});})]
                +SHorizontalBox::Slot().FillWidth(1).Padding(6,0)[SNew(STextBlock).Text(FText::FromString(ControlLabel(P.Name))).ToolTipText(FText::FromName(P.Name)).AutoWrapText(true)]);
            Add(SNew(SSpinBox<float>).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12)).Value(P.Value).MinValue(TOptional<float>()).MaxValue(TOptional<float>())
                .OnValueCommitted_Lambda([this,ID,I](float V,ETextCommit::Type){EditNode(ID,[I,V](FMCVFXCompositionLayer& X){if(X.Floats.IsValidIndex(I)){X.Floats[I].Value=V;X.Floats[I].bOverride=true;}});}));
        }
        for(int32 I=0;I<L.Colors.Num();++I)
        {
            const auto& P=L.Colors[I];Add(SNew(STextBlock).Text(FText::FromString(ControlLabel(P.Name))).ToolTipText(FText::FromName(P.Name)));
            Add(SNew(SColorBlock).Color(P.Value).Size(FVector2D(130,24)).OnMouseButtonDown_Lambda([this,ID,I](const FGeometry&,const FPointerEvent&)
            {
                const int32 J=Working->FindNode(ID);if(J==INDEX_NONE||!Working->Layers[J].Colors.IsValidIndex(I))return FReply::Handled();
                FColorPickerArgs A;A.InitialColor=Working->Layers[J].Colors[I].Value;A.bUseAlpha=true;A.bOnlyRefreshOnOk=true;A.bExpandAdvancedSection=false;
                A.OnColorCommitted=FOnLinearColorValueChanged::CreateSP(this,&SRecipeComposer::CommitColor,ID,I);OpenColorPicker(A);return FReply::Handled();
            }));
        }
        for(int32 I=0;I<L.Bools.Num();++I)
        {
            const auto& P=L.Bools[I];
            Add(SNew(SCheckBox).IsChecked(P.Value?ECheckBoxState::Checked:ECheckBoxState::Unchecked).ToolTipText(FText::FromName(P.Name))
                .OnCheckStateChanged_Lambda([this,ID,I](ECheckBoxState S){EditNode(ID,[I,S](FMCVFXCompositionLayer& X){if(X.Bools.IsValidIndex(I)){X.Bools[I].Value=S==ECheckBoxState::Checked;X.Bools[I].bOverride=true;}});})[Text(ControlLabel(P.Name))]);
        }
        const FGuid GroupID=Working->Nodes[N].Group;
        const int32 GroupIndex=Working->Groups.IndexOfByPredicate([GroupID](const FMCVFXRecipeGroup& V){return V.ID==GroupID;});
        if(GroupIndex!=INDEX_NONE)
        {
            Add(Text(TEXT("Комментарий группы")));
            Add(SNew(SEditableTextBox).Text(FText::FromString(Working->Groups[GroupIndex].Label)).OnTextCommitted_Lambda([this,GroupID](const FText& T,ETextCommit::Type)
            {const int32 I=Working->Groups.IndexOfByPredicate([GroupID](const FMCVFXRecipeGroup& V){return V.ID==GroupID;});if(I==INDEX_NONE)return;const FScopedTransaction Tx(LOCTEXT("RenameGroup","VFX: имя группы"));Working->Modify();Working->Groups[I].Label=T.ToString();Changed();}));
            Add(Button(TEXT("Убрать выбранные узлы из группы"),[this]{const FScopedTransaction Tx(LOCTEXT("Ungroup","VFX: разгруппировать"));Working->Modify();for(auto& V:Working->Nodes)if(Selection.Contains(V.ID))V.Group.Invalidate();Changed();}));
        }
        LayerProxy=MakeShared<FStructOnScope>(FMCVFXCompositionLayer::StaticStruct());FMCVFXCompositionLayer::StaticStruct()->CopyScriptStruct(LayerProxy->GetStructMemory(),&L);Advanced->SetStructureData(LayerProxy);
        Add(SNew(SExpandableArea).InitiallyCollapsed(true).HeaderContent()[Text(TEXT("Дополнительно: расположение и параметры Niagara"))]
            .BodyContent()[SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().Padding(0,6)[Button(TEXT("Обновить параметры из источника"),[this]{Discover();})]
                +SVerticalBox::Slot().AutoHeight()[Advanced->GetWidget().ToSharedRef()]]);
    }
    TWeakPtr<SRecipeComposer> ActiveRecipe;
}

void MCVFXRecipeComposer::Register()
{
    if(IsRunningCommandlet()||FGlobalTabmanager::Get()->HasTabSpawner(RecipeTab))return;
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(RecipeTab,FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
    {
        TSharedRef<SRecipeComposer> Panel=SNew(SRecipeComposer);ActiveRecipe=Panel;
        if(bHasPendingContext){Panel->SetContext(PendingContext);bHasPendingContext=false;}
        TSharedRef<SDockTab> Tab=SNew(SDockTab).TabRole(ETabRole::NomadTab)
            .OnCanCloseTab(SDockTab::FCanCloseTab::CreateSP(Panel,&SRecipeComposer::CanClose))
            .OnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda([WeakPanel=TWeakPtr<SRecipeComposer>(Panel)](TSharedRef<SDockTab>){if(auto P=WeakPanel.Pin())P->Closed();}))[Panel];
        Panel->SetOwnerTab(Tab);return Tab;
    })).SetDisplayName(LOCTEXT("TabTitle","VFX Composer · Элементы"))
        .SetTooltipText(LOCTEXT("TabHint","Произвольные элементы, библиотека, граф, настоящие controls и сохранение композиции."))
        .SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
}
void MCVFXRecipeComposer::Unregister()
{
    if(auto Tab=FGlobalTabmanager::Get()->FindExistingLiveTab(RecipeTab))if(!Tab->RequestCloseTab())return;
    ActiveRecipe.Reset();FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(RecipeTab);
}
void MCVFXRecipeComposer::Open(){MCVFXActionWorkspace::Open();}
void MCVFXRecipeComposer::OpenForSlot(FName PerkID,FName Stage,UMCPerkVFXProfile* Profile,FMCVFXRecipeApplied OnApply)
{
    FRecipeContext C;C.PerkID=PerkID;C.Stage=Stage;C.Profile=Profile;C.OnApply=MoveTemp(OnApply);
    if(auto Panel=ActiveRecipe.Pin()){Panel->SetContext(C);Open();}
    else{PendingContext=MoveTemp(C);bHasPendingContext=true;Open();}
}
UMCVFXRecipePreset* MCVFXRecipeComposer::GetWorkingRecipe(){auto P=ActiveRecipe.Pin();return P?P->GetWorking():nullptr;}
bool MCVFXRecipeComposer::LoadComposition(UMCVFXComposition* Source){auto P=ActiveRecipe.Pin();return P&&P->Load(Source);}
bool MCVFXRecipeComposer::AddSystem(UNiagaraSystem* Source){auto P=ActiveRecipe.Pin();return P&&P->AddSystem(Source);}
UMCVFXRecipePreset* MCVFXRecipeComposer::SaveWorkingRecipeAs(const FString& Path,FString& Error){auto P=ActiveRecipe.Pin();if(!P){Error=TEXT("Open the visible Composer first.");return nullptr;}return P->SaveAs(Path,Error);}
UNiagaraSystem* MCVFXRecipeComposer::ExportWorkingRecipe(const FString& Path,FString& Error){auto P=ActiveRecipe.Pin();if(!P){Error=TEXT("Open the visible Composer first.");return nullptr;}return P->Export(Path,Error);}
bool MCVFXRecipeComposer::ApplyToContext(FString& Error){auto P=ActiveRecipe.Pin();if(!P){Error=TEXT("Open the visible Composer first.");return false;}return P->Apply(Error);}

#undef LOCTEXT_NAMESPACE
