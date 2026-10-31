#include "MCPerkActionPreviewPanel.h"

#include "MCPerkPreviewRunner.h"
#include "MCPerkPreviewAdapter.h"
#include "MCActiveSkillPerkEffect.h"
#include "MCPerkPreviewEditorLibrary.h"
#include "MCVFXRecipeComposer.h"
#include "MCVFXActionWorkspace.h"
#include "MCPerkVFXProfile.h"
#include "MCPerkTypes.h"
#include "MCActiveSkillAction.h"
#include "MCRocketSkillAction.h"
#include "MCToothCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "AssetThumbnail.h"
#include "ContentBrowserModule.h"
#include "Containers/Ticker.h"
#include "DetailsViewArgs.h"
#include "Editor.h"
#include "EditorUndoClient.h"
#include "EditorViewportClient.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"
#include "FileHelpers.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "IContentBrowserSingleton.h"
#include "IStructureDetailsView.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "NiagaraSystem.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "Rendering/DrawElementTypes.h"
#include "ScopedTransaction.h"
#include "SceneView.h"
#include "SEditorViewport.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UnrealClient.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
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
#include "Widgets/Text/STextBlock.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "MCPerkActionPreview"

namespace
{
    const FName PerkPreviewTabName(TEXT("MCPerkActionPreview"));
    const TArray<FName> Stages={TEXT("Start"),TEXT("Loop"),TEXT("Impact"),TEXT("End")};
    FString StageLabel(FName Stage)
    {
        if(Stage==TEXT("Start"))return TEXT("ВЫСТРЕЛ");
        if(Stage==TEXT("Loop"))return TEXT("СЛЕД РАКЕТЫ");
        if(Stage==TEXT("Impact"))return TEXT("ПОПАДАНИЕ");
        return TEXT("ЗАВЕРШЕНИЕ");
    }
    FString StageContext(FName Stage)
    {
        if(Stage==TEXT("Start"))return TEXT("Вспышка у настоящего дула");
        if(Stage==TEXT("Loop"))return TEXT("Эмиссия на летящем снаряде");
        if(Stage==TEXT("Impact"))return TEXT("Точка столкновения и нормаль");
        return TEXT("Окончание действия / recovery");
    }
    FLinearColor StageColor(FName Stage)
    {
        if(Stage==TEXT("Start"))return FLinearColor(.62f,.34f,.08f);
        if(Stage==TEXT("Loop"))return FLinearColor(.12f,.40f,.40f);
        if(Stage==TEXT("Impact"))return FLinearColor(.53f,.15f,.13f);
        return FLinearColor(.28f,.22f,.44f);
    }
    TSoftObjectPtr<UNiagaraSystem> DefaultSystem(FName Stage)
    {
        const AMCRocketSkillAction* CDO=GetDefault<AMCRocketSkillAction>();
        if(Stage==TEXT("Start"))return CDO->StartVFX;
        if(Stage==TEXT("Loop"))return CDO->LoopVFX;
        if(Stage==TEXT("Impact"))return CDO->ImpactVFX;
        return CDO->EndVFX;
    }
    TSoftObjectPtr<UNiagaraSystem> EffectiveSystem(const UMCPerkVFXProfile* Profile,FName Stage)
    {
        const FMCPerkVFXSlot* Slot=Profile?Profile->Slots.Find(Stage):nullptr;
        if(!Slot || Slot->Mode==EMCPerkVFXSlotMode::UseDefault)return DefaultSystem(Stage);
        return Slot->Mode==EMCPerkVFXSlotMode::Replace?Slot->System:TSoftObjectPtr<UNiagaraSystem>();
    }
    FString ModeText(EMCPerkVFXSlotMode Mode)
    {
        if(Mode==EMCPerkVFXSlotMode::Replace)return TEXT("Своя Niagara");
        return Mode==EMCPerkVFXSlotMode::Disabled?TEXT("Выключен"):TEXT("По умолчанию");
    }
    FVector2D NodePosition(int32 Index)
    {return FVector2D(Index%2?355.f:35.f,Index/2?245.f:35.f);}
    const FVector2D GraphOutputPosition(222,455);
    const FVector2D GraphOutputSize(180,76);
    const FVector2D GraphBoundsMin(35,35);
    const FVector2D GraphBoundsMax(590,531);

    class SMCPerkPreviewViewport;
    class FMCPerkPreviewViewportClient final : public FEditorViewportClient
    {
    public:
        FMCPerkPreviewViewportClient(const TSharedRef<FMCPerkPreviewRunner>& InRunner,const TSharedRef<SMCPerkPreviewViewport>& Widget);
        virtual UWorld* GetWorld() const override {return Runner->GetWorld();}
        virtual FLinearColor GetBackgroundColor() const override {return FLinearColor(.035f,.043f,.052f);}
        virtual void Tick(float Dt) override
        {
            // The shared service owns the private world's clock. Rendering a
            // small/hidden/background viewport must not pause an action or fuse.
            FEditorViewportClient::Tick(Dt);
            const FIntPoint Size=Viewport?Viewport->GetSizeXY():FIntPoint::ZeroValue;
            if(Size.X>1 && Size.Y>1 && Size!=LastFramedSize){LastFramedSize=Size;Preset(CurrentPreset);}
        }
        virtual void OverridePostProcessSettings(FSceneView& View) override
        {
            FPostProcessSettings Settings;
            Settings.bOverride_AutoExposureMethod=true;Settings.AutoExposureMethod=AEM_Manual;
            Settings.bOverride_AutoExposureApplyPhysicalCameraExposure=true;Settings.AutoExposureApplyPhysicalCameraExposure=false;
            Settings.bOverride_BloomIntensity=true;Settings.BloomIntensity=.2f;
            View.OverridePostProcessSettings(Settings,1.f);
        }
        void Preset(int32 Index)
        {
            CurrentPreset=Index;
            const FVector Hero=Runner->GetHero()?Runner->GetHero()->GetActorLocation():FVector(0,0,90);
            const FIntPoint Size=Viewport?Viewport->GetSizeXY():FIntPoint(420,500);
            const double Aspect=FMath::Max(.1,double(Size.X)/FMath::Max(1,Size.Y));
            // Keep a useful vertical field of view when the preview is narrow/tall or resized.
            // A fixed horizontal 55/90 degrees expands to an unusably tall vertical view in a portrait panel.
            const double WantedTanV=FMath::Tan(FMath::DegreesToRadians(Index==0?25.:19.));
            ViewFOV=float(FMath::Clamp(FMath::RadiansToDegrees(2*FMath::Atan(WantedTanV*Aspect)),10.,120.));
            FOVAngle=ViewFOV;
            FVector Target,Camera;
            if(Index==0)
            {
                // Frame both the complete tooth and its target. In a portrait
                // preview a fixed over-shoulder camera clipped the left hand
                // and half the body; compute the distance from actual corners.
                const float TargetX=Runner->GetScenario()==EMCPerkPreviewScenario::NearWall?250.f:900.f;
                const FBox Box(Hero+FVector(-85,-150,-65),FVector(TargetX+35,160,295));
                Target=Box.GetCenter();const FVector Back=FVector(-1,.55f,.34f).GetSafeNormal();
                const FVector Forward=-Back;
                const FVector Right=FVector::CrossProduct(Forward,FVector::UpVector).GetSafeNormal();
                const FVector Up=FVector::CrossProduct(Right,Forward).GetSafeNormal();
                const double TanH=FMath::Tan(FMath::DegreesToRadians(double(ViewFOV)*.5));
                const double TanV=TanH/Aspect;double Distance=600;
                for(int32 Corner=0;Corner<8;++Corner)
                {
                    const FVector Q=FVector(Corner&1?Box.Max.X:Box.Min.X,Corner&2?Box.Max.Y:Box.Min.Y,Corner&4?Box.Max.Z:Box.Min.Z)-Target;
                    const double Depth=FVector::DotProduct(Q,Forward);
                    Distance=FMath::Max(Distance,FMath::Abs(FVector::DotProduct(Q,Right))/(TanH*.86)-Depth);
                    Distance=FMath::Max(Distance,FMath::Abs(FVector::DotProduct(Q,Up))/(TanV*.86)-Depth);
                }
                Camera=Target+Back*Distance;
            }
            else
            {
                // Include the ordinary body, its evaluated mesh and room for the authored held launcher.
                // Actor-wide bounds would also include screen-space nameplates and unrelated effects.
                FBox Box(Hero+FVector(-55,-70,-75),Hero+FVector(160,70,110));
                if(auto* H=Runner->GetHero())if(auto* Mesh=H->GetMesh())
                    if(Mesh->Bounds.GetBox().IsValid && Mesh->Bounds.BoxExtent.Size()<500.f)Box+=Mesh->Bounds.GetBox();
                if(auto* Action=Runner->GetAction())if(auto* Prop=Action->PropMesh.Get())
                    if(Prop->IsVisible() && Prop->Bounds.GetBox().IsValid && FVector::Dist(Prop->Bounds.Origin,Hero)<400.f)Box+=Prop->Bounds.GetBox();
                Box=Box.ExpandBy(10.f);Target=Box.GetCenter();
                const FVector Back=(Index==1?FVector(1,0,.18f):FVector(0,1,.18f)).GetSafeNormal();
                const FVector Forward=-Back;
                const FVector Right=FVector::CrossProduct(Forward,FVector::UpVector).GetSafeNormal();
                const FVector Up=FVector::CrossProduct(Right,Forward).GetSafeNormal();
                const double TanH=FMath::Tan(FMath::DegreesToRadians(double(ViewFOV)*.5));
                const double TanV=TanH/Aspect;
                double Distance=140.;
                for(int32 Corner=0;Corner<8;++Corner)
                {
                    const FVector Q=FVector(Corner&1?Box.Max.X:Box.Min.X,Corner&2?Box.Max.Y:Box.Min.Y,Corner&4?Box.Max.Z:Box.Min.Z)-Target;
                    const double Depth=FVector::DotProduct(Q,Forward);
                    Distance=FMath::Max(Distance,FMath::Abs(FVector::DotProduct(Q,Right))/(TanH*.84)-Depth);
                    Distance=FMath::Max(Distance,FMath::Abs(FVector::DotProduct(Q,Up))/(TanV*.84)-Depth);
                }
                Camera=Target+Back*Distance;
            }
            ToggleOrbitCamera(false);GetViewTransform().TransitionToLocation(Camera,TWeakPtr<SWidget>(),true);
            SetViewRotation((Target-Camera).Rotation());
            Invalidate();
        }
    private:
        TSharedRef<FMCPerkPreviewRunner> Runner;
        FIntPoint LastFramedSize=FIntPoint::ZeroValue;
        int32 CurrentPreset=0;
    };
    class SMCPerkPreviewViewport final : public SEditorViewport
    {
    public:
        SLATE_BEGIN_ARGS(SMCPerkPreviewViewport){} SLATE_ARGUMENT(TSharedPtr<FMCPerkPreviewRunner>,Runner) SLATE_END_ARGS()
        void Construct(const FArguments& Args){Runner=Args._Runner;SEditorViewport::Construct(SEditorViewport::FArguments());}
        void Preset(int32 Index){if(Client)Client->Preset(Index);}
    protected:
        virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override
        {Client=MakeShared<FMCPerkPreviewViewportClient>(Runner.ToSharedRef(),StaticCastSharedRef<SMCPerkPreviewViewport>(AsShared()));return Client.ToSharedRef();}
    private:
        TSharedPtr<FMCPerkPreviewRunner> Runner;
        TSharedPtr<FMCPerkPreviewViewportClient> Client;
    };
    FMCPerkPreviewViewportClient::FMCPerkPreviewViewportClient(const TSharedRef<FMCPerkPreviewRunner>& InRunner,const TSharedRef<SMCPerkPreviewViewport>& Widget)
        :FEditorViewportClient(nullptr,nullptr,Widget),Runner(InRunner)
    {
        SetViewMode(VMI_Lit);SetRealtime(true);ShowWidget(false);bIsSimulateInEditorViewport=true;
        DrawHelper.bDrawPivot=false;DrawHelper.bDrawWorldBox=false;DrawHelper.bDrawGrid=true;
        OverrideNearClipPlane(1.f);EngineShowFlags.SetSelectionOutline(false);
        ExposureSettings.bFixed=true;ExposureSettings.FixedEV100=1.f;Preset(0);
    }

    class SMCPerkSlotGraph;
    /** Lines show the slots actually routed to the next action. Disabled/empty slots have no output connection. */
    class SMCPerkGraphLines final : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SMCPerkGraphLines){} SLATE_ARGUMENT(TWeakPtr<SMCPerkSlotGraph>,Graph) SLATE_END_ARGS()
        void Construct(const FArguments& Args){Graph=Args._Graph;SetVisibility(EVisibility::HitTestInvisible);}
        virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(650,700);}
        virtual int32 OnPaint(const FPaintArgs&,const FGeometry&,const FSlateRect&,FSlateWindowElementList&,int32,const FWidgetStyle&,bool)const override;
        TWeakPtr<SMCPerkSlotGraph> Graph;
    };
    DECLARE_DELEGATE_OneParam(FMCSelectPreviewSlot,FName);
    class SMCPerkSlotGraph final : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SMCPerkSlotGraph){} SLATE_ATTRIBUTE(UMCPerkVFXProfile*,Profile) SLATE_ATTRIBUTE(FName,Selected) SLATE_EVENT(FMCSelectPreviewSlot,OnSelected) SLATE_END_ARGS()
        void Construct(const FArguments& Args)
        {
            Profile=Args._Profile;Selected=Args._Selected;OnSelected=Args._OnSelected;
            Pool=MakeShared<FAssetThumbnailPool>(16);Build();
        }
        void Build()
        {
            Thumbnails.Reset();
            TSharedRef<SConstraintCanvas> Canvas=SNew(SConstraintCanvas)
                .RenderTransform_Lambda([this]{return FSlateRenderTransform(FScale2D(Zoom),FVector2D(Pan));})
                .RenderTransformPivot(FVector2D::ZeroVector);
            for(int32 Index=0;Index<Stages.Num();++Index)
            {
                const FName Stage=Stages[Index];const FVector2D Pos=NodePosition(Index);
                const FAssetData Data(FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetByObjectPath(EffectiveSystem(Profile.Get(),Stage).ToSoftObjectPath()));
                TSharedRef<FAssetThumbnail> Thumb=MakeShared<FAssetThumbnail>(Data,72,72,Pool);Thumbnails.Add(Thumb);
                Canvas->AddSlot().Offset(FMargin(Pos.X,Pos.Y,235,165)).Alignment(FVector2D::ZeroVector)
                [SNew(SButton).ButtonColorAndOpacity_Lambda([this,Stage]{return Selected.Get()==Stage?StageColor(Stage)*1.8f:StageColor(Stage);})
                    .OnClicked_Lambda([this,Stage]{OnSelected.ExecuteIfBound(Stage);return FReply::Handled();})
                    [SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight().Padding(5)[SNew(STextBlock).Text(FText::FromString(StageLabel(Stage))).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),12))]
                        +SVerticalBox::Slot().FillHeight(1).Padding(5)[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(66).HeightOverride(66)[Thumb->MakeThumbnailWidget()]]
                            +SHorizontalBox::Slot().FillWidth(1).Padding(9,0)[SNew(SVerticalBox)
                                +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(StageContext(Stage))).AutoWrapText(true)]
                                +SVerticalBox::Slot().AutoHeight().Padding(0,6)[SNew(STextBlock).Text_Lambda([this,Stage]{return FText::FromString(EffectiveSystem(Profile.Get(),Stage).GetAssetName());}).AutoWrapText(true)]]]
                        +SVerticalBox::Slot().AutoHeight().Padding(5)[SNew(STextBlock).Text_Lambda([this,Stage]{const auto* S=Profile.Get()?Profile.Get()->Slots.Find(Stage):nullptr;return FText::FromString(ModeText(S?S->Mode:EMCPerkVFXSlotMode::UseDefault));})]]];
            }
            Canvas->AddSlot().Offset(FMargin(GraphOutputPosition.X,GraphOutputPosition.Y,GraphOutputSize.X,GraphOutputSize.Y)).Alignment(FVector2D::ZeroVector)
            [SNew(SBorder).BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder"))).BorderBackgroundColor(FLinearColor(.06f,.32f,.36f)).Padding(10)
                [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("Output","РАКЕТНИЦА · OUTPUT")).Justification(ETextJustify::Center)]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,5)[SNew(STextBlock).Text(LOCTEXT("OutputHint","Слоты одного настоящего действия")).AutoWrapText(true).Justification(ETextJustify::Center)]]];
            ChildSlot[SNew(SBorder).Padding(0).Clipping(EWidgetClipping::ClipToBounds).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(.025f,.03f,.04f))
                [SNew(SOverlay)+SOverlay::Slot()[SNew(SMCPerkGraphLines).Graph(SharedThis(this))]+SOverlay::Slot()[Canvas]]];
        }
        virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent& Event)override
        {
            if(Event.GetEffectingButton()==EKeys::RightMouseButton || Event.GetEffectingButton()==EKeys::MiddleMouseButton){bAutoFit=false;bPanning=true;return FReply::Handled().CaptureMouse(SharedThis(this));}
            return FReply::Unhandled();
        }
        virtual FReply OnMouseButtonUp(const FGeometry&,const FPointerEvent&)override
        {if(bPanning){bPanning=false;return FReply::Handled().ReleaseMouseCapture();}return FReply::Unhandled();}
        virtual FReply OnMouseMove(const FGeometry&,const FPointerEvent& Event)override
        {if(bPanning){Pan+=Event.GetCursorDelta();Invalidate(EInvalidateWidgetReason::Layout);return FReply::Handled();}return FReply::Unhandled();}
        virtual FReply OnMouseWheel(const FGeometry& Geo,const FPointerEvent& Event)override
        {
            bAutoFit=false;
            const FVector2D Cursor=Geo.AbsoluteToLocal(Event.GetScreenSpacePosition());
            const float NewZoom=FMath::Clamp(Zoom*FMath::Pow(1.12f,Event.GetWheelDelta()),.12f,1.5f);
            Pan=Cursor-(Cursor-Pan)*(NewZoom/Zoom);Zoom=NewZoom;Invalidate(EInvalidateWidgetReason::Layout);return FReply::Handled();
        }
        virtual void Tick(const FGeometry& Geometry,const double CurrentTime,const float DeltaTime)override
        {
            SCompoundWidget::Tick(Geometry,CurrentTime,DeltaTime);
            const FVector2D Size=Geometry.GetLocalSize();
            if(bAutoFit && Size.X>1 && Size.Y>1 && (bNeedsFit || !Size.Equals(LastFitSize,1.f)))Fit(Size);
        }
        void Home(){bAutoFit=true;bNeedsFit=true;Invalidate(EInvalidateWidgetReason::Layout);}
        FVector2D Pan=FVector2D::ZeroVector;float Zoom=.5f;
        TAttribute<UMCPerkVFXProfile*> Profile;
    private:
        void Fit(const FVector2D& Size)
        {
            const FVector2D Content=GraphBoundsMax-GraphBoundsMin;
            const double Padding=16.;
            Zoom=float(FMath::Clamp(FMath::Min(FMath::Max(1.,Size.X-2*Padding)/Content.X,FMath::Max(1.,Size.Y-2*Padding)/Content.Y),.01,1.2));
            Pan=(Size-Content*Zoom)*.5-GraphBoundsMin*Zoom;
            LastFitSize=Size;bNeedsFit=false;Invalidate(EInvalidateWidgetReason::Layout);
        }
        TAttribute<FName> Selected;FMCSelectPreviewSlot OnSelected;
        TSharedPtr<FAssetThumbnailPool> Pool;TArray<TSharedPtr<FAssetThumbnail>> Thumbnails;bool bPanning=false;
        bool bAutoFit=true,bNeedsFit=true;
        FVector2D LastFitSize=FVector2D::ZeroVector;
    };
    int32 SMCPerkGraphLines::OnPaint(const FPaintArgs&,const FGeometry& Geo,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle&,bool)const
    {
        auto P=Graph.Pin();if(!P)return Layer;
        const FVector2D Size=Geo.GetLocalSize();
        for(float X=FMath::Fmod(P->Pan.X,24.f*P->Zoom);X<Size.X;X+=24.f*P->Zoom)
            FSlateDrawElement::MakeLines(Out,Layer,Geo.ToPaintGeometry(),{FVector2D(X,0),FVector2D(X,Size.Y)},ESlateDrawEffect::None,FLinearColor(.035f,.04f,.05f),false,1.f);
        for(float Y=FMath::Fmod(P->Pan.Y,24.f*P->Zoom);Y<Size.Y;Y+=24.f*P->Zoom)
            FSlateDrawElement::MakeLines(Out,Layer,Geo.ToPaintGeometry(),{FVector2D(0,Y),FVector2D(Size.X,Y)},ESlateDrawEffect::None,FLinearColor(.035f,.04f,.05f),false,1.f);
        for(int32 I=0;I<Stages.Num();++I)
        {
            if(EffectiveSystem(P->Profile.Get(),Stages[I]).IsNull())continue;
            const FVector2D A=Geo.LocalToAbsolute(P->Pan+(NodePosition(I)+FVector2D(118,165))*P->Zoom);
            const FVector2D B=Geo.LocalToAbsolute(P->Pan+(GraphOutputPosition+FVector2D(GraphOutputSize.X*.5,0))*P->Zoom);
            FSlateDrawElement::MakeDrawSpaceSpline(Out,Layer+1,A,FVector2D(0,100)*P->Zoom,B,FVector2D(0,100)*P->Zoom,2.f,ESlateDrawEffect::None,StageColor(Stages[I])*1.6f);
        }
        return Layer+1;
    }

    class SMCPerkActionPreviewPanel final : public SCompoundWidget,public FEditorUndoClient
    {
    public:
        SLATE_BEGIN_ARGS(SMCPerkActionPreviewPanel){}SLATE_END_ARGS()
        void Construct(const FArguments&)
        {
            Runner=MakeShared<FMCPerkPreviewRunner>();Runner->Initialize();
            Working.Reset(NewObject<UMCPerkVFXProfile>(GetTransientPackage(),NAME_None,RF_Transient|RF_Transactional));
            // Reopening a perk starts from its assigned profile, not unrelated defaults.
            if(auto* Catalog=LoadObject<UDataTable>(nullptr,TEXT("/Game/Gameplay/Roguelike/DT_Perks.DT_Perks")))
                if(Catalog->GetRowStruct()==FMCPerkDefinition::StaticStruct())
                    if(const auto* Row=Catalog->FindRow<FMCPerkDefinition>(TEXT("Consumable_Rocket"),TEXT("OpenPerkPreview"),false))
                        if(auto* Source=Row->VFXProfile.LoadSynchronous())
                        {
                            Working.Reset(DuplicateObject<UMCPerkVFXProfile>(Source,GetTransientPackage(),MakeUniqueObjectName(GetTransientPackage(),UMCPerkVFXProfile::StaticClass(),TEXT("RocketVFXWorking"))));
                            Working->ClearFlags(RF_Public|RF_Standalone);Working->SetFlags(RF_Transient|RF_Transactional);
                            SourcePath=Source->GetPathName();Status=TEXT("Открыта независимая копия назначенного профиля: ")+SourcePath;
                        }
            for(FName Stage:Stages)Working->Slots.FindOrAdd(Stage);
            ThumbnailPool=MakeShared<FAssetThumbnailPool>(16);
            FDetailsViewArgs D;D.bUpdatesFromSelection=false;D.bLockable=false;D.bAllowSearch=false;D.NameAreaSettings=FDetailsViewArgs::HideNameArea;
            FStructureDetailsViewArgs S;S.bShowObjects=true;
            AdvancedDetails=FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateStructureDetailView(D,S,nullptr);
            AdvancedDetails->GetOnFinishedChangingPropertiesDelegate().AddSP(this,&SMCPerkActionPreviewPanel::OnAdvancedChanged);
            if(GEditor)GEditor->RegisterForUndo(this);

            FAssetPickerConfig Picker;Picker.Filter.ClassPaths.Add(UNiagaraSystem::StaticClass()->GetClassPathName());Picker.Filter.bRecursiveClasses=true;
            Picker.InitialAssetViewType=EAssetViewType::Tile;Picker.bAllowNullSelection=false;Picker.bFocusSearchBoxWhenOpened=false;
            Picker.OnAssetSelected=FOnAssetSelected::CreateSP(this,&SMCPerkActionPreviewPanel::OnLibrarySelected);
            TSharedRef<SWidget> Library=FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().CreateAssetPicker(Picker);
            ChildSlot[SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8,6))
                    +SWrapBox::Slot()[SNew(STextBlock).Text(LOCTEXT("Title","ПРЕДПРОСМОТР ПЕРКА  /  РАКЕТНИЦА")).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),16))]
                    +SWrapBox::Slot()[Button(TEXT("Открыть профиль…"),[this]{OpenProfile();})]
                    +SWrapBox::Slot()[Button(TEXT("Сохранить новую DA…"),[this]{SaveNew();})]
                    +SWrapBox::Slot()[Button(TEXT("Назначить сохранённый профиль перку"),[this]{AssignSavedProfile();})]
                    +SWrapBox::Slot()[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(bDirty?TEXT("● Рабочая копия изменена"):TEXT("Рабочая копия"));})]]
                +SVerticalBox::Slot().AutoHeight().Padding(8,0,8,8)[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8,6))
                    +SWrapBox::Slot()[SNew(STextBlock).Text(LOCTEXT("Scenario","Сценарий:"))]
                    +SWrapBox::Slot()[ScenarioButton(TEXT("Мишень"),EMCPerkPreviewScenario::Target)]
                    +SWrapBox::Slot()[ScenarioButton(TEXT("Близкая стена"),EMCPerkPreviewScenario::NearWall)]
                    +SWrapBox::Slot()[ScenarioButton(TEXT("Полёт до таймаута"),EMCPerkPreviewScenario::Timeout)]
                    +SWrapBox::Slot()[Button(TEXT("▶ Полное действие"),[this]{Run(Scenario);})]
                    +SWrapBox::Slot()[Button(TEXT("Пауза / продолжить"),[this]{Runner->SetPaused(!Runner->IsPaused());})]
                    +SWrapBox::Slot()[Button(TEXT("Сбросить песочницу"),[this]{Runner->Reset();})]]
                +SVerticalBox::Slot().FillHeight(1).Padding(8,0,8,6)[SNew(SSplitter)
                    +SSplitter::Slot().Value(.30f)[SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight()[Header(TEXT("ПОЛНОЕ ДЕЙСТВИЕ"))]
                        +SVerticalBox::Slot().FillHeight(1)[SAssignNew(Viewport,SMCPerkPreviewViewport).Runner(Runner)]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,5)[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Общий"),[this]{Viewport->Preset(0);})]
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Спереди"),[this]{Viewport->Preset(1);})]
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Сбоку"),[this]{Viewport->Preset(2);})]]
                        +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Runner->GetStatus());}).AutoWrapText(true)]
                        +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(FString::Printf(TEXT("Время: %.2f с · %s · %s"),Runner->GetElapsed(),Runner->IsPaused()?TEXT("Пауза"):Runner->IsRunning()?TEXT("Действие"):TEXT("Готово"),*Runner->GetPhaseName()));}).AutoWrapText(true)]
                        +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(SExpandableArea).InitiallyCollapsed(true).HeaderContent()[SNew(STextBlock).Text(LOCTEXT("RealEvents","Фактические события действия"))].BodyContent()[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Runner->GetEventLog());}).AutoWrapText(true)]]]
                    +SSplitter::Slot().Value(.47f)[SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("ГРАФ ДЕЙСТВИЯ"),[this]{bLibrary=false;})]
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("+ БИБЛИОТЕКА NIAGARA"),[this]{bLibrary=true;})]
                            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("⌂"),[this]{Graph->Home();})]]
                        +SVerticalBox::Slot().FillHeight(1)[SNew(SWidgetSwitcher).WidgetIndex_Lambda([this]{return bLibrary?1:0;})
                            +SWidgetSwitcher::Slot()[SAssignNew(Graph,SMCPerkSlotGraph).Profile_Lambda([this]{return Working.Get();}).Selected_Lambda([this]{return SelectedStage;}).OnSelected_Lambda([this](FName Stage){SelectedStage=Stage;RefreshDetails();})]
                            +SWidgetSwitcher::Slot()[SNew(SVerticalBox)
                                +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text(LOCTEXT("LibraryHint","Выберите настоящую Niagara System; «Назначить слоту» заменит только текущий VFX-слот рабочей копии.")).AutoWrapText(true)]
                                +SVerticalBox::Slot().FillHeight(1)[Library]
                                +SVerticalBox::Slot().AutoHeight().Padding(8)[Button(TEXT("Назначить выбранную Niagara слоту"),[this]{AssignLibrary();})]]]
                        +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).Text(LOCTEXT("GraphHint","ПКМ / средняя кнопка — перемещение · Колесо — масштаб. Провода показывают включённые VFX-слоты. Подготовка, хват и recovery исполняются игровым действием.")).AutoWrapText(true)]]
                    +SSplitter::Slot().Value(.23f)[SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight()[Header(TEXT("НАСТРОЙКИ СЛОТА"))]
                        +SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(DetailsBox,SVerticalBox)]]]]
                +SVerticalBox::Slot().AutoHeight().Padding(8,0,8,4)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Status);}).AutoWrapText(true)]
                +SVerticalBox::Slot().AutoHeight().Padding(8,0,8,8)[SNew(STextBlock).Text(LOCTEXT("Scope","Первый сценарий: ракетница. Профиль меняет только VFX. Назначение в DT_Perks — отдельная явная кнопка после сохранения DA. Это граф слотов NS, не внутренний Niagara graph и не экспорт эмиттеров.")).AutoWrapText(true)]];
            RefreshDetails();
        }
        virtual ~SMCPerkActionPreviewPanel()override{if(GEditor)GEditor->UnregisterForUndo(this);if(Runner)Runner->Reset();}
        virtual void PostUndo(bool bSuccess)override{if(bSuccess){bDirty=true;Refresh();}}
        virtual void PostRedo(bool bSuccess)override{PostUndo(bSuccess);}
        bool CanClose()
        {
            if(!bDirty)return true;
            const auto Result=FMessageDialog::Open(EAppMsgType::YesNoCancel,LOCTEXT("Unsaved","Сохранить рабочий VFX-профиль в новую DA?\nДа — сохранить; Нет — закрыть без сохранения; Отмена — вернуться."));
            return Result==EAppReturnType::No || (Result==EAppReturnType::Yes && SaveNew());
        }
        TSharedPtr<FMCPerkPreviewRunner> GetRunner()const{return Runner;}
        UMCPerkVFXProfile* GetWorking()const{return Working.Get();}
        bool SetWorkingSlot(FName Stage,const FMCPerkVFXSlot& Slot,FString& OutError)
        {
            OutError.Reset();
            if(!Stages.Contains(Stage)){OutError=TEXT("Это событие не поддерживается предпросмотром ракетницы.");return false;}
            TStrongObjectPtr<UMCPerkVFXProfile> Candidate(DuplicateObject<UMCPerkVFXProfile>(Working.Get(),GetTransientPackage()));
            Candidate->Slots.FindOrAdd(Stage)=Slot;
            if(!Candidate->Validate(OutError))return false;
            if(Slot.Mode==EMCPerkVFXSlotMode::Replace)
            {
                auto* System=Slot.System.LoadSynchronous();
                if(!System || !UMCVFXComposition::ValidateParameters(UMCPerkVFXProfile::AsLayer(Slot),System,OutError))
                {if(OutError.IsEmpty())OutError=TEXT("Niagara System недоступна.");return false;}
            }
            const FScopedTransaction Tx(LOCTEXT("ApplyComposition","Применить композицию к событию перка"));
            Working->Modify();Working->Slots.FindOrAdd(Stage)=Slot;SelectedStage=Stage;Changed();
            return true;
        }
        bool Run(EMCPerkPreviewScenario InScenario)
        {
            Scenario=InScenario;FString Error;
            if(!Working->Validate(Error)){Status=TEXT("Профиль не запущен: ")+Error;return false;}
            PreviewProfile.Reset(DuplicateObject<UMCPerkVFXProfile>(Working.Get(),GetTransientPackage(),MakeUniqueObjectName(GetTransientPackage(),UMCPerkVFXProfile::StaticClass(),TEXT("PerkPreviewSnapshot"))));
            PreviewProfile->ClearFlags(RF_Public|RF_Standalone);PreviewProfile->SetFlags(RF_Transient);
            if(bSolo)for(FName Stage:Stages)if(Stage!=SelectedStage)PreviewProfile->Slots.FindOrAdd(Stage).Mode=EMCPerkVFXSlotMode::Disabled;
            const bool bResult=Runner->StartRocket(Scenario,PreviewProfile.Get());
            Status=bResult?(bSolo?TEXT("Предпросмотр выбранного слота Solo в полном действии."):TEXT("Запущено настоящее действие с копией рабочего профиля.")):Runner->GetStatus();
            return bResult;
        }
        bool Audition(FName Stage,const FMCPerkVFXSlot& Slot,FString& OutError)
        {
            OutError.Reset();
            if(!Stages.Contains(Stage)){OutError=TEXT("Это событие не поддерживается.");return false;}
            PreviewProfile.Reset(DuplicateObject<UMCPerkVFXProfile>(Working.Get(),GetTransientPackage(),MakeUniqueObjectName(GetTransientPackage(),UMCPerkVFXProfile::StaticClass(),TEXT("ComposerAudition"))));
            PreviewProfile->ClearFlags(RF_Public|RF_Standalone);PreviewProfile->SetFlags(RF_Transient);
            PreviewProfile->Slots.FindOrAdd(Stage)=Slot;
            if(!PreviewProfile->Validate(OutError))return false;
            const bool OK=Runner->StartRocket(Scenario,PreviewProfile.Get());
            if(!OK)OutError=Runner->GetStatus();
            Status=OK?TEXT("Composer: проба события на копии профиля. Рабочий профиль не изменён."):OutError;
            return OK;
        }
    private:
        TSharedPtr<FMCPerkPreviewRunner> Runner;
        TStrongObjectPtr<UMCPerkVFXProfile> Working,PreviewProfile;
        TSharedPtr<SMCPerkPreviewViewport> Viewport;
        TSharedPtr<SMCPerkSlotGraph> Graph;
        TSharedPtr<SVerticalBox> DetailsBox;
        TSharedPtr<IStructureDetailsView> AdvancedDetails;
        TSharedPtr<FStructOnScope> SlotProxy;
        TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
        TArray<TSharedPtr<FAssetThumbnail>> DetailsThumbnails;
        FAssetData LibrarySelection;
        FName SelectedStage=TEXT("Start");EMCPerkPreviewScenario Scenario=EMCPerkPreviewScenario::Target;
        bool bDirty=false,bSolo=false,bLibrary=false,bRefreshing=false;
        FString SourcePath,Status=TEXT("Выберите сценарий и нажмите «Полное действие». Редактирование применяется к следующему запуску.");

        TSharedRef<SWidget> Header(const FString& Text)
        {return SNew(SBorder).BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder"))).Padding(9)[SNew(STextBlock).Text(FText::FromString(Text)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),11))];}
        TSharedRef<SWidget> Button(const FString& Text,TFunction<void()> Action)
        {return SNew(SButton).Text(FText::FromString(Text)).OnClicked_Lambda([Action=MoveTemp(Action)]{Action();return FReply::Handled();});}
        TSharedRef<SWidget> ScenarioButton(const FString& Text,EMCPerkPreviewScenario Value)
        {return SNew(SButton).Text(FText::FromString(Text)).ButtonColorAndOpacity_Lambda([this,Value]{return Scenario==Value?FLinearColor(.11f,.43f,.40f):FLinearColor(.15f,.17f,.20f);}).OnClicked_Lambda([this,Value]{Scenario=Value;return FReply::Handled();});}
        void Refresh(){if(Graph)Graph->Build();RefreshDetails();}
        void Changed(){bDirty=true;Status=TEXT("Рабочий профиль изменён. Повторите действие, чтобы увидеть изменения.");Refresh();}
        void Edit(TFunction<void(FMCPerkVFXSlot&)> Change)
        {const FScopedTransaction Tx(LOCTEXT("EditSlot","VFX: изменить слот действия"));Working->Modify();Change(Working->Slots.FindOrAdd(SelectedStage));Changed();}
        void SetMode(EMCPerkVFXSlotMode Mode)
        {Edit([this,Mode](FMCPerkVFXSlot& Slot){if(Mode==EMCPerkVFXSlotMode::Replace && Slot.System.IsNull())Slot.System=DefaultSystem(SelectedStage);Slot.Mode=Mode;});}
        void OnLibrarySelected(const FAssetData& Asset){LibrarySelection=Asset;}
        void AssignLibrary()
        {
            if(!LibrarySelection.IsValid() || LibrarySelection.AssetClassPath!=UNiagaraSystem::StaticClass()->GetClassPathName()){Status=TEXT("Сначала выберите Niagara System в библиотеке.");return;}
            Edit([this](FMCPerkVFXSlot& Slot){Slot.Mode=EMCPerkVFXSlotMode::Replace;Slot.System=TSoftObjectPtr<UNiagaraSystem>(LibrarySelection.GetSoftObjectPath());Slot.Floats.Reset();Slot.Colors.Reset();Slot.Vectors.Reset();Slot.Bools.Reset();});bLibrary=false;
        }
        void Discover()
        {
            FString Error;const FScopedTransaction Tx(LOCTEXT("Discover","VFX: найти параметры слота"));Working->Modify();
            auto& Slot=Working->Slots.FindOrAdd(SelectedStage);
            if(Slot.Mode!=EMCPerkVFXSlotMode::Replace){Slot.System=DefaultSystem(SelectedStage);Slot.Mode=EMCPerkVFXSlotMode::Replace;}
            const bool bOK=Working->DiscoverParameters(SelectedStage,Error);Changed();Status=bOK?TEXT("Показаны параметры, действительно открытые исходной Niagara."):Error;
        }
        void RefreshDetails()
        {
            if(!DetailsBox)return;TGuardValue<bool> Guard(bRefreshing,true);DetailsBox->ClearChildren();DetailsThumbnails.Reset();
            const FMCPerkVFXSlot& Slot=Working->Slots.FindOrAdd(SelectedStage);
            DetailsBox->AddSlot().AutoHeight().Padding(9)[SNew(STextBlock).Text(FText::FromString(StageLabel(SelectedStage))).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),16))];
            DetailsBox->AddSlot().AutoHeight().Padding(9,0,9,9)[SNew(STextBlock).Text(FText::FromString(StageContext(SelectedStage))).AutoWrapText(true)];
            DetailsBox->AddSlot().AutoHeight().Padding(9)[Button(TEXT("Собрать эффект в Composer…"),[this]
            {MCVFXRecipeComposer::OpenForSlot(TEXT("Consumable_Rocket"),SelectedStage,Working.Get());})];
            auto Thumb=MakeShared<FAssetThumbnail>(FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetByObjectPath(EffectiveSystem(Working.Get(),SelectedStage).ToSoftObjectPath()),100,100,ThumbnailPool);DetailsThumbnails.Add(Thumb);
            DetailsBox->AddSlot().AutoHeight().Padding(9)[SNew(SBox).HeightOverride(100).WidthOverride(100)[Thumb->MakeThumbnailWidget()]];
            DetailsBox->AddSlot().AutoHeight().Padding(9)[SNew(SComboButton).ButtonContent()[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(ModeText(Working->Slots.FindOrAdd(SelectedStage).Mode));})]
                .MenuContent()[SNew(SVerticalBox)
                    +SVerticalBox::Slot().AutoHeight()[Button(TEXT("По умолчанию"),[this]{SetMode(EMCPerkVFXSlotMode::UseDefault);})]
                    +SVerticalBox::Slot().AutoHeight()[Button(TEXT("Своя Niagara"),[this]{SetMode(EMCPerkVFXSlotMode::Replace);})]
                    +SVerticalBox::Slot().AutoHeight()[Button(TEXT("Выключить"),[this]{SetMode(EMCPerkVFXSlotMode::Disabled);})]]];
            DetailsBox->AddSlot().AutoHeight().Padding(9)[SNew(SCheckBox).IsChecked_Lambda([this]{return bSolo?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}).OnCheckStateChanged_Lambda([this](ECheckBoxState State){bSolo=State==ECheckBoxState::Checked;})[SNew(STextBlock).Text(LOCTEXT("Solo","Solo выбранного слота (только preview)"))]];
            DetailsBox->AddSlot().AutoHeight().Padding(9)[SNew(SObjectPropertyEntryBox).AllowedClass(UNiagaraSystem::StaticClass()).ThumbnailPool(ThumbnailPool)
                .ObjectPath_Lambda([this]{return EffectiveSystem(Working.Get(),SelectedStage).ToSoftObjectPath().ToString();})
                .OnObjectChanged_Lambda([this](const FAssetData& Asset){Edit([&Asset](FMCPerkVFXSlot& S){S.Mode=EMCPerkVFXSlotMode::Replace;S.System=TSoftObjectPtr<UNiagaraSystem>(Asset.GetSoftObjectPath());S.Floats.Reset();S.Colors.Reset();S.Vectors.Reset();S.Bools.Reset();});})];
            DetailsBox->AddSlot().AutoHeight().Padding(9)[Button(TEXT("Выбрать из библиотеки"),[this]{bLibrary=true;})];
            DetailsBox->AddSlot().AutoHeight().Padding(9)[Button(TEXT("Показать параметры Niagara"),[this]{Discover();})];
            if(Slot.Mode==EMCPerkVFXSlotMode::Replace)
            {
                for(int32 I=0;I<Slot.Floats.Num();++I)
                {
                    const auto& P=Slot.Floats[I];
                    DetailsBox->AddSlot().AutoHeight().Padding(9,3)[SNew(STextBlock).Text(FText::FromName(P.Name)).AutoWrapText(true)];
                    DetailsBox->AddSlot().AutoHeight().Padding(9,0,9,6)[SNew(SHorizontalBox)
                        +SHorizontalBox::Slot().AutoWidth()[SNew(SCheckBox).IsChecked(P.bOverride?ECheckBoxState::Checked:ECheckBoxState::Unchecked).OnCheckStateChanged_Lambda([this,I](ECheckBoxState S){Edit([I,S](FMCPerkVFXSlot& V){if(V.Floats.IsValidIndex(I))V.Floats[I].bOverride=S==ECheckBoxState::Checked;});})]
                        +SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<float>).Value(P.Value).MinValue(TOptional<float>()).MaxValue(TOptional<float>()).OnValueCommitted_Lambda([this,I](float V,ETextCommit::Type){Edit([I,V](FMCPerkVFXSlot& S){if(S.Floats.IsValidIndex(I)){S.Floats[I].Value=V;S.Floats[I].bOverride=true;}});})]];
                }
                for(int32 I=0;I<Slot.Colors.Num();++I)
                {
                    const auto& P=Slot.Colors[I];const FName Stage=SelectedStage;
                    DetailsBox->AddSlot().AutoHeight().Padding(9,3)[SNew(STextBlock).Text(FText::FromName(P.Name)).AutoWrapText(true)];
                    DetailsBox->AddSlot().AutoHeight().Padding(9,0,9,6)[SNew(SColorBlock).Color(P.Value).Size(FVector2D(120,24)).OnMouseButtonDown_Lambda([this,I,Stage](const FGeometry&,const FPointerEvent&)
                    {
                        if(!Working->Slots.FindOrAdd(Stage).Colors.IsValidIndex(I))return FReply::Handled();
                        FColorPickerArgs Args;Args.InitialColor=Working->Slots.FindOrAdd(Stage).Colors[I].Value;Args.bUseAlpha=true;Args.bOnlyRefreshOnOk=true;Args.bExpandAdvancedSection=false;
                        Args.OnColorCommitted=FOnLinearColorValueChanged::CreateSP(this,&SMCPerkActionPreviewPanel::SetColor,Stage,I);OpenColorPicker(Args);return FReply::Handled();
                    })];
                }
            }
            SlotProxy=MakeShared<FStructOnScope>(FMCPerkVFXSlot::StaticStruct());FMCPerkVFXSlot::StaticStruct()->CopyScriptStruct(SlotProxy->GetStructMemory(),&Slot);AdvancedDetails->SetStructureData(SlotProxy);
            DetailsBox->AddSlot().AutoHeight().Padding(9)[SNew(SExpandableArea).InitiallyCollapsed(true).HeaderContent()[SNew(STextBlock).Text(LOCTEXT("Advanced","Дополнительно: transform и типы параметров"))].BodyContent()[AdvancedDetails->GetWidget().ToSharedRef()]];
            DetailsBox->AddSlot().AutoHeight().Padding(9)[SNew(STextBlock).Text(LOCTEXT("SlotLimits","У ракетницы подготовка/хват/recovery — настоящая анимация, без отдельного VFX-события подготовки. Warning в этом сценарии не выдаётся.")).AutoWrapText(true)];
        }
        void SetColor(FLinearColor Value,FName Stage,int32 I)
        {
            auto* S=Working->Slots.Find(Stage);if(!S || !S->Colors.IsValidIndex(I))return;
            const FScopedTransaction Tx(LOCTEXT("Color","VFX: цвет параметра"));Working->Modify();S->Colors[I].Value=Value;S->Colors[I].bOverride=true;Changed();
        }
        void OnAdvancedChanged(const FPropertyChangedEvent&)
        {
            if(bRefreshing || !SlotProxy || !SlotProxy->IsValid())return;
            const auto Copy=*reinterpret_cast<const FMCPerkVFXSlot*>(SlotProxy->GetStructMemory());
            Edit([&Copy](FMCPerkVFXSlot& S){S=Copy;});
        }
        void OpenProfile()
        {
            if(!CanClose())return;
            TArray<FAssetData> Assets;FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().GetSelectedAssets(Assets);
            for(const FAssetData& Asset:Assets)if(auto* Source=Cast<UMCPerkVFXProfile>(Asset.GetAsset()))
            {
                Working.Reset(DuplicateObject<UMCPerkVFXProfile>(Source,GetTransientPackage(),MakeUniqueObjectName(GetTransientPackage(),UMCPerkVFXProfile::StaticClass(),TEXT("PerkVFXWorking"))));
                Working->ClearFlags(RF_Public|RF_Standalone);Working->SetFlags(RF_Transient|RF_Transactional);SourcePath=Source->GetPathName();bDirty=false;Refresh();Status=TEXT("Открыта независимая копия: ")+SourcePath;return;
            }
            Status=TEXT("Выберите VFXProfile DA в Content Browser, затем нажмите «Открыть профиль».");
        }
        void AssignSavedProfile()
        {
            if(bDirty || SourcePath.IsEmpty())
            {Status=TEXT("Сначала сохраните текущую рабочую копию в новую DA. Назначается только сохранённый профиль.");return;}
            UMCPerkVFXProfile* Saved=LoadObject<UMCPerkVFXProfile>(nullptr,*SourcePath);
            if(!Saved || Saved->HasAnyFlags(RF_Transient) || Saved->GetOutermost()==GetTransientPackage())
            {Status=TEXT("Не удалось найти сохранённый постоянный профиль. Сначала сохраните новую DA.");return;}
            Status=UMCPerkPreviewEditorLibrary::AssignSavedProfileToRocket(Saved);
        }
        bool SaveNew()
        {
            FString Error;if(!Working->Validate(Error)){Status=TEXT("Сохранение отменено: ")+Error;return false;}
            FSaveAssetDialogConfig Config;Config.DialogTitleOverride=LOCTEXT("SaveTitle","Сохранить новый VFX-профиль ракетницы");Config.DefaultPath=TEXT("/Game/Tests/VFXPractice/Profiles");Config.DefaultAssetName=TEXT("DA_VFX_Rocket");
            Config.AssetClassNames.Add(UMCPerkVFXProfile::StaticClass()->GetClassPathName());Config.ExistingAssetPolicy=ESaveAssetDialogExistingAssetPolicy::Disallow;
            const FString ObjectPath=FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().CreateModalSaveAssetDialog(Config);
            if(ObjectPath.IsEmpty())return false;
            const FString PackageName=FPackageName::ObjectPathToPackageName(ObjectPath);
            if(!FPackageName::IsValidObjectPath(ObjectPath)||!PackageName.StartsWith(TEXT("/Game/"))||FPackageName::DoesPackageExist(PackageName)||FindObject<UPackage>(nullptr,*PackageName)){Status=TEXT("Выберите новое корректное имя в /Game/: существующие пакеты не перезаписываются.");return false;}
            auto* Saved=UMCPerkPreviewEditorLibrary::SaveWorkingProfileAs(ObjectPath,Error);
            if(!Saved){Status=Error;return false;}
            bDirty=false;SourcePath=Saved->GetPathName();Status=TEXT("Сохранена новая DA: ")+SourcePath+TEXT(". Назначение в DT_Perks — отдельное явное действие.");
            FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().SyncBrowserToAssets(TArray<UObject*>{Saved});return true;
        }
    };
    TWeakPtr<SMCPerkActionPreviewPanel> ActivePanel;
    // The current workspace owns no hidden legacy tab. These are headless
    // authoring services shared by its view and the compatibility API.
    TSharedPtr<FMCPerkPreviewRunner> SharedRunner;
    TStrongObjectPtr<UMCPerkVFXProfile> SharedWorking;
    FName SharedPerkID;
    FTSTicker::FDelegateHandle SharedClockHandle;

    void EnsureSharedClock()
    {
        if(SharedClockHandle.IsValid())return;
        SharedClockHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float DeltaSeconds) {
            // No hidden idle-world simulation: close/Stop resets IsRunning.
            // Warmup is included by the native runner's IsRunning contract.
            if(SharedRunner && SharedRunner->IsRunning() && !SharedRunner->IsPaused())SharedRunner->Tick(DeltaSeconds);
            return true;
        }));
    }

    TSoftObjectPtr<UNiagaraSystem> SharedDefaultSystem(FName Stage)
    {
        const auto* Entry=MCPerkPreviewAdapters::Find(SharedPerkID);
        if(Entry)if(const auto* Value=Entry->DefaultSystems.Find(Stage))return *Value;
        if(!Entry || (Entry->Kind==EMCPerkPreviewAdapterKind::Context && Entry->SourceRowID.IsNone()))return {};
        const FName RowID=Entry && !Entry->SourceRowID.IsNone()?Entry->SourceRowID:SharedPerkID;
        const auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Gameplay/Roguelike/DT_Perks.DT_Perks"));
        const auto* Row=Table && Table->GetRowStruct()==FMCPerkDefinition::StaticStruct()?Table->FindRow<FMCPerkDefinition>(RowID,TEXT("PreviewDefault"),false):nullptr;
        const auto* Effect=Row && Row->EffectClass?Cast<UMCActiveSkillPerkEffect>(Row->EffectClass->GetDefaultObject()):nullptr;
        const auto* Action=Effect && Effect->ActionClass?Effect->ActionClass->GetDefaultObject<AMCActiveSkillAction>():nullptr;
        if(!Action)return {};
        if(Stage==TEXT("Start"))return Action->StartVFX;
        if(Stage==TEXT("Loop"))return Action->LoopVFX;
        if(Stage==TEXT("Impact"))return Action->ImpactVFX;
        if(Stage==TEXT("Warning"))return Action->WarningVFX;
        if(Stage==TEXT("End"))return Action->EndVFX;
        return {};
    }
}

void MCPerkActionPreviewPanel::Register()
{
    if(IsRunningCommandlet()||FGlobalTabmanager::Get()->HasTabSpawner(PerkPreviewTabName))return;
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(PerkPreviewTabName,FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
    {
        TSharedRef<SMCPerkActionPreviewPanel> Panel=SNew(SMCPerkActionPreviewPanel);ActivePanel=Panel;
        return SNew(SDockTab).TabRole(ETabRole::NomadTab).OnCanCloseTab(SDockTab::FCanCloseTab::CreateSP(Panel,&SMCPerkActionPreviewPanel::CanClose))[Panel];
    })).SetDisplayName(LOCTEXT("Tab","Действия перков · VFX"))
      .SetTooltipText(LOCTEXT("TabTip","Полное действие ракетницы в изолированной песочнице и VFX-слоты реальных событий."))
      .SetMenuType(ETabSpawnerMenuType::Hidden)
      .SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
}
void MCPerkActionPreviewPanel::Unregister()
{
    if(auto Tab=FGlobalTabmanager::Get()->FindExistingLiveTab(PerkPreviewTabName))if(!Tab->RequestCloseTab())return;
    ActivePanel.Reset();FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PerkPreviewTabName);
    FTSTicker::GetCoreTicker().RemoveTicker(SharedClockHandle);SharedClockHandle.Reset();
    SharedWorking.Reset();SharedRunner.Reset();SharedPerkID=NAME_None;
}
void MCPerkActionPreviewPanel::Open(){MCVFXActionWorkspace::Open();}
bool MCPerkActionPreviewPanel::EnsureContext(FName PerkID,FString& OutError)
{
    OutError.Reset();const auto* Entry=MCPerkPreviewAdapters::Find(PerkID);
    if(!Entry){OutError=TEXT("Для выбранного действия ещё нет контекстного адаптера.");return false;}
    if(!SharedRunner){SharedRunner=MakeShared<FMCPerkPreviewRunner>();if(!SharedRunner->Initialize()){OutError=SharedRunner->GetStatus();SharedRunner.Reset();return false;}}
    EnsureSharedClock();
    if(SharedPerkID==PerkID && SharedWorking.IsValid())return true;
    SharedRunner->Reset();SharedPerkID=PerkID;
    const bool NeedsRow=Entry->Kind!=EMCPerkPreviewAdapterKind::Context || !Entry->SourceRowID.IsNone();
    const auto* Table=NeedsRow?LoadObject<UDataTable>(nullptr,TEXT("/Game/Gameplay/Roguelike/DT_Perks.DT_Perks")):nullptr;
    const FName RowID=Entry->SourceRowID.IsNone()?PerkID:Entry->SourceRowID;
    const auto* Row=Table && Table->GetRowStruct()==FMCPerkDefinition::StaticStruct()?Table->FindRow<FMCPerkDefinition>(RowID,TEXT("PreviewContext"),false):nullptr;
    if(auto* Source=Row?Row->VFXProfile.LoadSynchronous():nullptr)
        SharedWorking.Reset(DuplicateObject<UMCPerkVFXProfile>(Source,GetTransientPackage(),MakeUniqueObjectName(GetTransientPackage(),UMCPerkVFXProfile::StaticClass(),TEXT("VFXContextWorking"))));
    else SharedWorking.Reset(NewObject<UMCPerkVFXProfile>(GetTransientPackage(),NAME_None,RF_Transient));
    SharedWorking->ClearFlags(RF_Public|RF_Standalone);SharedWorking->SetFlags(RF_Transient|RF_Transactional);
    return true;
}
FName MCPerkActionPreviewPanel::GetContextPerkID(){return SharedPerkID;}
TSharedPtr<FMCPerkPreviewRunner> MCPerkActionPreviewPanel::GetActiveRunner(){return SharedRunner;}
UMCPerkVFXProfile* MCPerkActionPreviewPanel::GetWorkingProfile(){return SharedWorking.Get();}
bool MCPerkActionPreviewPanel::SetWorkingSlot(FName Stage,const FMCPerkVFXSlot& Slot,FString& OutError)
{
    OutError.Reset();const auto* Entry=MCPerkPreviewAdapters::Find(SharedPerkID);
    if(!SharedWorking.IsValid() || !Entry || !Entry->Stages.Contains(Stage) || !Entry->bSupportsProfileOverride) {
        OutError=TEXT("Текущий контекст не поддерживает замену этого VFX-события.");return false;
    }
    TStrongObjectPtr<UMCPerkVFXProfile> Candidate(DuplicateObject<UMCPerkVFXProfile>(SharedWorking.Get(),GetTransientPackage()));Candidate->Slots.FindOrAdd(Stage)=Slot;
    if(!Candidate->Validate(OutError))return false;
    if(Slot.Mode==EMCPerkVFXSlotMode::Replace) {
        auto* System=Slot.System.LoadSynchronous();
        if(!System || !UMCVFXComposition::ValidateParameters(UMCPerkVFXProfile::AsLayer(Slot),System,OutError))return false;
    }
    const FScopedTransaction Tx(LOCTEXT("SharedApply","Изменить рабочий VFX-контекст"));
    SharedWorking->Modify();SharedWorking->Slots.FindOrAdd(Stage)=Slot;return true;
}
UNiagaraSystem* MCPerkActionPreviewPanel::ResolveWorkingStageSystem(FName Stage)
{
    const auto* Slot=SharedWorking.IsValid()?SharedWorking->Slots.Find(Stage):nullptr;
    if(!Slot || Slot->Mode==EMCPerkVFXSlotMode::UseDefault)return SharedDefaultSystem(Stage).LoadSynchronous();
    return Slot->Mode==EMCPerkVFXSlotMode::Replace?Slot->System.LoadSynchronous():nullptr;
}
bool MCPerkActionPreviewPanel::AuditionWorkingSlot(FName Stage,const FMCPerkVFXSlot& Slot,FString& OutError)
{
    OutError.Reset();if(!SharedRunner || !SharedWorking.IsValid()){OutError=TEXT("Контекст предпросмотра недоступен.");return false;}
    TStrongObjectPtr<UMCPerkVFXProfile> Trial(DuplicateObject<UMCPerkVFXProfile>(SharedWorking.Get(),GetTransientPackage()));
    Trial->ClearFlags(RF_Public|RF_Standalone);Trial->SetFlags(RF_Transient);Trial->Slots.FindOrAdd(Stage)=Slot;
    if(!Trial->Validate(OutError))return false;
    if(!SharedRunner->StartAction(SharedPerkID,EMCPerkPreviewScenario::Target,Trial.Get())){OutError=SharedRunner->GetStatus();return false;}
    return true;
}
TSharedRef<SWidget> MCPerkActionPreviewPanel::MakeSharedActionViewport(TFunction<void(int32)>* OutSetView)
{
    auto Runner=GetActiveRunner();
    if(!Runner)return SNew(STextBlock).Text(LOCTEXT("NoSharedContext","Для выбранного действия ещё не подготовлен контекст предпросмотра."));
    TSharedPtr<SMCPerkPreviewViewport> SharedView;
    TSharedRef<SVerticalBox> Box=SNew(SVerticalBox)
        +SVerticalBox::Slot().FillHeight(1)[SAssignNew(SharedView,SMCPerkPreviewViewport).Runner(Runner)];
    SharedView->Preset(1);
    if(OutSetView)*OutSetView=[WeakView=TWeakPtr<SMCPerkPreviewViewport>(SharedView)](int32 Index)
    {if(auto View=WeakView.Pin())View->Preset(Index);};
    Box->AddSlot().AutoHeight()[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).Text(LOCTEXT("SharedOverview","Общий")).OnClicked_Lambda([SharedView]{SharedView->Preset(0);return FReply::Handled();})]
        +SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).Text(LOCTEXT("SharedFront","Спереди")).OnClicked_Lambda([SharedView]{SharedView->Preset(1);return FReply::Handled();})]
        +SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).Text(LOCTEXT("SharedSide","Сбоку")).OnClicked_Lambda([SharedView]{SharedView->Preset(2);return FReply::Handled();})]];
    Box->AddSlot().AutoHeight().Padding(6)[SNew(STextBlock).Text_Lambda([Runner]{return FText::FromString(Runner->GetStatus());}).AutoWrapText(true)];
    return Box;
}
bool MCPerkActionPreviewPanel::RunScenario(EMCPerkPreviewScenario Scenario)
{return SharedRunner && SharedRunner->StartAction(SharedPerkID,Scenario,SharedWorking.Get());}
void MCPerkActionPreviewPanel::ResetPreview(){if(auto Runner=GetActiveRunner())Runner->Reset();}

static FAutoConsoleCommand OpenPerkVFXPreview(TEXT("MC.PerkVFXPreview"),TEXT("Open the isolated perk action and VFX slot preview."),FConsoleCommandDelegate::CreateStatic(&MCPerkActionPreviewPanel::Open));

#undef LOCTEXT_NAMESPACE
