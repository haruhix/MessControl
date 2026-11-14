#include "MCSFXComposerWorkspace.h"

#include "MCComposerDocumentView.h"
#include "MCComposerDocuments.h"
#include "MCComposerReview.h"
#include "MCComposerSourceDrag.h"
#include "MCComposerSceneAudioSession.h"
#include "MCComposerStyle.h"
#include "MCDataAssets.h"
#include "MCVFXActionCatalog.h"
#include "MCVFXActionSession.h"
#include "MCVFXActionWorkspace.h"
#include "MCVFXCatalogueFilter.h"
#include "MCPerkPreviewRunner.h"
#include "SMCComposerSceneAudioPanel.h"
#include "SMCSFXGallery.h"
#include "SMCSFXLibrary.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "InputCoreTypes.h"
#include "PropertyCustomizationHelpers.h"
#include "Rendering/DrawElementTypes.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Sound/SoundBase.h"
#include "Styling/CoreStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "MCSFXComposerWorkspace"

namespace
{
    using namespace MCComposerStyle;
    const FName SFXWorkspaceTab(TEXT("MCSFXComposerWorkspace"));

    FText CatalogueEventLabel(FName Action,FName Event)
    {
        if(const auto* Adapter=MCPerkPreviewAdapters::Find(Action))
            if(const auto* Text=Adapter->StageLabels.Find(Event))return *Text;
        return FText::FromName(Event);
    }
    FString AudioPolicyLabel(EMCEventSFXMode Mode)
    {
        return Mode==EMCEventSFXMode::UseDefault?TEXT("Родной звук"):Mode==EMCEventSFXMode::Add?TEXT("Родной звук + слои"):
            Mode==EMCEventSFXMode::Replace?TEXT("Только добавленные слои"):TEXT("Звук выключен");
    }

    TSharedRef<SWidget> MakeSFXApprovalCheck(FName Action,FName Event,const FString& Destination,bool bCaption,TFunction<void(const FString&)> Feedback)
    {
        return SNew(SCheckBox).IsEnabled(!Action.IsNone()&&!Event.IsNone())
            .IsChecked_Lambda([Action,Event]{
                const auto Document=MCComposerDocuments::Find(Action);
                const auto State=MCComposerReview::Query(Action,Event,EMCComposerReviewLane::SFX,Document.Get());
                return State.bStale||(State.bApproved&&!State.bCurrentKnown)?ECheckBoxState::Undetermined:State.bApproved?ECheckBoxState::Checked:ECheckBoxState::Unchecked;
            })
            .ToolTipText_Lambda([Action,Event,Destination]{
                const auto Document=MCComposerDocuments::Find(Action);
                const auto State=MCComposerReview::Query(Action,Event,EMCComposerReviewLane::SFX,Document.Get());
                const FString DefaultDetail=State.bStale||(State.bApproved&&!State.bCurrentKnown)?TEXT("Требуется повторная проверка текущего аудио. Прослушайте событие и подтвердите заново."):
                    State.bApproved?TEXT("Актуальные настройки звука этого события одобрены."):TEXT("Отметьте после прослушивания звука этого события.");
                const FString Detail=State.Detail.IsEmpty()?DefaultDetail:State.Detail;
                return FText::FromString(TEXT("SFX одобрено · ")+Destination+TEXT("\n")+Detail+TEXT("\nОтметка проверки не включает, не выключает и не блокирует звук."));
            })
            .OnCheckStateChanged_Lambda([Action,Event,Destination,Feedback](ECheckBoxState Value){
                const bool Approved=Value==ECheckBoxState::Checked;FString Error;
                if(!MCComposerReview::SetApproved(Action,Event,EMCComposerReviewLane::SFX,Approved,Error)){if(Feedback)Feedback(Error);return;}
                if(Feedback)Feedback((Approved?TEXT("SFX одобрено: "):TEXT("Одобрение SFX снято: "))+Destination+TEXT(". Сохранена только отметка проверки редактора."));
            })
            [SNew(STextBlock).Text(LOCTEXT("SFXApproved","SFX одобрено")).Visibility(bCaption?EVisibility::Visible:EVisibility::Collapsed)
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12)).ColorAndOpacity(TextColor)];
    }

    struct FSFXEditScope
    {
        FName Action, Event;
        FGuid Token;
    };

    /** An internal drag cannot be silently redirected after its source selection changes. */
    class FSFXSourceDrag final : public FMCComposerSourceDrag
    {
    public:
        DRAG_DROP_OPERATOR_TYPE(FSFXSourceDrag, FMCComposerSourceDrag)
        FSFXEditScope Scope;
        static TSharedRef<FSFXSourceDrag> New(const FAssetData& Asset, FSFXEditScope InScope)
        {
            auto Operation=MakeShared<FSFXSourceDrag>();
            Operation->Scope=InScope;
            Operation->SourceDocument=MCComposerDocuments::Find(InScope.Action);
            Operation->SourceToken=InScope.Token;
            TArray<FAssetData> Assets; Assets.Add(Asset);
            Operation->Init(MoveTemp(Assets),TArray<FString>(),TScriptInterface<IAssetFactoryInterface>());
            Operation->Construct();
            return Operation;
        }
    };

    TOptional<FSFXEditScope> SoundDragOrigin(const TSharedPtr<FDragDropOperation>& Operation)
    {
        if(!Operation)return {};
        if(Operation->IsOfType<FSFXSourceDrag>())return StaticCastSharedPtr<FSFXSourceDrag>(Operation)->Scope;
        if(Operation->IsOfType<FMCComposerSourceDrag>())
        {
            const auto Source=StaticCastSharedPtr<FMCComposerSourceDrag>(Operation);
            const auto Document=Source->SourceDocument.Pin();
            return FSFXEditScope{Document?Document->GetActionID():NAME_None,NAME_None,Source->SourceToken};
        }
        return {};
    }

    class SSFXDropTarget final : public SCompoundWidget
    {
    public:
        /** Registry-only classification, shared by the tree and the inspector targets. */
        static FString SoundAssetUnavailableReason(const FAssetData& Asset)
        {
            TArray<FTopLevelAssetPath> Ancestors;
            if(const auto* Module=FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry")))Module->Get().GetAncestorClassNames(Asset.AssetClassPath,Ancestors);
            const FTopLevelAssetPath SoundBase(TEXT("/Script/Engine"),TEXT("SoundBase"));
            const bool SoundType=Asset.IsValid()&&(Asset.AssetClassPath==SoundBase||Ancestors.Contains(SoundBase)||
                Asset.AssetClassPath==FTopLevelAssetPath(TEXT("/Script/Engine"),TEXT("SoundWave"))||
                Asset.AssetClassPath==FTopLevelAssetPath(TEXT("/Script/Engine"),TEXT("SoundCue"))||
                Asset.AssetClassPath==FTopLevelAssetPath(TEXT("/Script/MetasoundEngine"),TEXT("MetaSoundSource")));
            if(!SoundType)return TEXT("Сюда можно назначить только Sound Wave, Sound Cue или MetaSound Source.");
            return SMCSFXGallery::GetAssetUnavailableReason(Asset).ToString();
        }
        SLATE_BEGIN_ARGS(SSFXDropTarget) : _Replacement(false),_Compact(false) {}
            SLATE_ATTRIBUTE(FText, Text)
            SLATE_ATTRIBUTE(FText, Destination)
            SLATE_ATTRIBUTE(FString, TargetKey)
            SLATE_ARGUMENT(TFunction<bool(const FAssetData&,const TOptional<FSFXEditScope>&)>, Drop)
            SLATE_ARGUMENT(TFunction<bool(const FAssetData&,const TOptional<FSFXEditScope>&,FString&)>, CanDrop)
            SLATE_ARGUMENT(TFunction<void(const FString&)>, Feedback)
            SLATE_ARGUMENT(TFunction<void()>, Activate)
            SLATE_ATTRIBUTE(bool, Enabled)
            SLATE_ARGUMENT(bool, Replacement)
            SLATE_ARGUMENT(bool, Compact)
            SLATE_DEFAULT_SLOT(FArguments, Content)
        SLATE_END_ARGS()
        void Construct(const FArguments& Args)
        {
            Drop=Args._Drop;CanDrop=Args._CanDrop;Feedback=Args._Feedback;
            Text=Args._Text;Destination=Args._Destination;TargetKey=Args._TargetKey;bReplacement=Args._Replacement;bCompact=Args._Compact;
            SetEnabled(Args._Enabled);
            SetToolTipText(TAttribute<FText>::CreateLambda([this]{return Destination.Get();}));
            ChildSlot[SNew(SBorder).BorderImage(&PanelBrush()).Padding(bCompact?FMargin(4,3):FMargin(9,8))
                .BorderBackgroundColor_Lambda([this]{return IsActiveHover()&&bEligible?(bReplacement?UIColor(TEXT("#4A3520")):UIColor(TEXT("#204C44"))):bAvailable?UIColor(TEXT("#1D3739")):GraphFill;})
                [SNew(SVerticalBox)
                    +SVerticalBox::Slot().AutoHeight()[SNew(SBox).MinDesiredHeight(16)
                        [SNew(STextBlock).Text_Lambda([this]{return bCompact&&!IsActiveHover()?LOCTEXT("RowDropHint","↓ На слой — заменить источник"):Text.Get();})
                            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),bCompact?11:12))
                            .ColorAndOpacity_Lambda([this]{return IsActiveHover()&&bEligible?Accent():bCompact?Muted:WorkspaceMint;}).AutoWrapText(true)]]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,3,0,0)[SNew(STextBlock).Visibility(bCompact?EVisibility::Collapsed:EVisibility::Visible)
                        .Text_Lambda([this]{return FText::FromString(IsActiveHover()&&!HoverMessage.IsEmpty()?HoverMessage:Destination.Get().ToString());})
                        .ColorAndOpacity_Lambda([this]{return IsActiveHover()&&bEligible?TextColor:Muted;}).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11))
                        .OverflowPolicy(ETextOverflowPolicy::Ellipsis)]
                    +SVerticalBox::Slot().FillHeight(1).Padding(0,bCompact?3:7,0,0)[Args._Content.Widget]
                    +SVerticalBox::Slot().AutoHeight()[SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(8,5))
                        .Visibility(Args._Activate?EVisibility::Visible:EVisibility::Collapsed)
                        .OnClicked_Lambda([Action=Args._Activate]{if(Action)Action();return FReply::Handled();})
                        [Label(Args._Replacement?TEXT("Заменить выбранным из библиотеки"):TEXT("Добавить выбранный из библиотеки"),11)]]]];
        }
        virtual void Tick(const FGeometry& Geometry,double CurrentTime,float DeltaTime) override
        {
            SCompoundWidget::Tick(Geometry,CurrentTime,DeltaTime);
            FAssetData Asset;TOptional<FSFXEditScope> Origin;FString Reason;
            const auto Operation=FSlateApplication::Get().GetDragDroppingContent();
            bAvailable=Validate(Operation,Asset,Origin,Reason);
            if(!Operation){bHovered=false;bEligible=false;HoverMessage.Reset();}
        }
        virtual void OnDragEnter(const FGeometry&,const FDragDropEvent& Event) override {Hover(Event);}
        virtual void OnDragLeave(const FDragDropEvent&) override
        {
            const bool WasActive=IsActiveHover();bHovered=false;bEligible=false;HoverMessage.Reset();
            if(WasActive)ActiveTarget().Reset();
        }
        virtual FReply OnDragOver(const FGeometry&,const FDragDropEvent& Event) override
        {
            Hover(Event);
            return Event.GetOperationAs<FAssetDragDropOp>()?FReply::Handled():FReply::Unhandled();
        }
        virtual FReply OnDrop(const FGeometry&,const FDragDropEvent& Event) override
        {
            if(!Event.GetOperationAs<FAssetDragDropOp>())return FReply::Unhandled();
            FAssetData Asset;TOptional<FSFXEditScope> Origin;FString Reason;
            const bool Valid=Validate(Event.GetOperation(),Asset,Origin,Reason);
            bHovered=false;bEligible=false;ActiveTarget().Reset();
            if(!Valid){if(Feedback)Feedback(Reason);return FReply::Handled();}
            if(Drop)Drop(Asset,Origin);
            // A rejected/stale replacement must never bubble into the surrounding Add target.
            return FReply::Handled();
        }
        virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Cull,FSlateWindowElementList& Elements,
            int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const override
        {
            const int32 Top=SCompoundWidget::OnPaint(Args,Geometry,Cull,Elements,Layer,Style,ParentEnabled);
            const FLinearColor Color=IsActiveHover()&&bEligible?Accent():bAvailable?WorkspaceMint:BorderFill;
            const FVector2f Size(Geometry.GetLocalSize());
            const auto Dash=[&](FVector2f Start,FVector2f End){
                const FVector2f Delta=End-Start;const float Length=Delta.Size();if(Length<1)return;
                const FVector2f Direction=Delta/Length;
                for(float At=0;At<Length;At+=10.f){TArray<FVector2f> Points;Points.Add(Start+Direction*At);Points.Add(Start+Direction*FMath::Min(At+6.f,Length));
                    FSlateDrawElement::MakeLines(Elements,Top+1,Geometry.ToPaintGeometry(),MoveTemp(Points),ESlateDrawEffect::None,Color,true,IsActiveHover()&&bEligible?2.f:1.f);}
            };
            Dash(FVector2f(1,1),FVector2f(Size.X-1,1));Dash(FVector2f(Size.X-1,1),FVector2f(Size.X-1,Size.Y-1));
            Dash(FVector2f(Size.X-1,Size.Y-1),FVector2f(1,Size.Y-1));Dash(FVector2f(1,Size.Y-1),FVector2f(1,1));
            return Top+1;
        }
    private:
        static TWeakPtr<SSFXDropTarget>& ActiveTarget(){static TWeakPtr<SSFXDropTarget> Active;return Active;}
        bool IsActiveHover() const {return bHovered&&ActiveTarget().Pin().Get()==this;}
        FLinearColor Accent() const {return bReplacement?UIColor(TEXT("#F5C078")):WorkspaceMint;}
        bool Validate(const TSharedPtr<FDragDropOperation>& Operation,FAssetData& Asset,TOptional<FSFXEditScope>& Origin,FString& Reason)
        {
            if(!Operation||!Operation->IsOfType<FAssetDragDropOp>())return false;
            const auto Op=StaticCastSharedPtr<FAssetDragDropOp>(Operation);
            if(Op->GetAssets().Num()!=1){Reason=TEXT("Перетащите один звуковой источник за раз.");return false;}
            Asset=Op->GetAssets()[0];
            if(Operation->IsOfType<FMCComposerSourceDrag>()&&!StaticCastSharedPtr<FMCComposerSourceDrag>(Operation)->IsSourceCurrent())
            {Reason=TEXT("Исходный документ перетаскивания устарел. Начните перенос звука заново.");return false;}
            if(Operation->IsOfType<FSFXSourceDrag>())Origin=StaticCastSharedPtr<FSFXSourceDrag>(Operation)->Scope;
            if(!IsEnabled()){Reason=TEXT("Выбранное событие сейчас недоступно для редактирования аудио.");return false;}
            if(CachedOperation.Pin()!=Operation)
            {
                CachedOperation=Operation;CachedTargetKey=TargetKey.Get();CachedTypeReason.Reset();
                CachedTypeReason=SoundAssetUnavailableReason(Asset);
            }
            if(CachedTargetKey!=TargetKey.Get()){Reason=TEXT("Назначение изменилось во время перетаскивания. Начните перенос заново.");return false;}
            if(!CachedTypeReason.IsEmpty()){Reason=CachedTypeReason;return false;}
            return CanDrop?CanDrop(Asset,Origin,Reason):true;
        }
        void Hover(const FDragDropEvent& Event)
        {
            FAssetData Asset;TOptional<FSFXEditScope> Origin;FString Reason;
            bHovered=true;ActiveTarget()=SharedThis(this);bEligible=Validate(Event.GetOperation(),Asset,Origin,Reason);
            HoverMessage=bEligible?Asset.AssetName.ToString()+TEXT(" → ")+Destination.Get().ToString():Reason;
            if(Feedback&&!HoverMessage.IsEmpty())Feedback(HoverMessage);
        }
        TAttribute<FText> Text,Destination;
        TAttribute<FString> TargetKey;
        bool bReplacement=false,bCompact=false,bHovered=false,bEligible=false,bAvailable=false;
        FString HoverMessage,CachedTypeReason,CachedTargetKey;
        TWeakPtr<FDragDropOperation> CachedOperation;
        TFunction<bool(const FAssetData&,const TOptional<FSFXEditScope>&,FString&)> CanDrop;
        TFunction<bool(const FAssetData&,const TOptional<FSFXEditScope>&)> Drop;
        TFunction<void(const FString&)> Feedback;
    };

    enum class ESFXTreeKind : uint8 { Section, Owner, Action, Event, Scene };
    struct FSFXTreeNode
    {
        FString Key;
        FText Text;
        ESFXTreeKind Kind=ESFXTreeKind::Section;
        FName Action, Event;
        bool bPreview=false, bEditable=false;
        FText Reason;
        TArray<TSharedPtr<FSFXTreeNode>> Children;
    };
    using FTreeItem=TSharedPtr<FSFXTreeNode>;

    struct FSFXEventDropState
    {
        TWeakPtr<FDragDropOperation> Operation;
        FGuid TargetToken;
        bool bTargetWasOpen=false;
        FString Reason;
    };

    bool ValidateSFXDragOrigin(const TOptional<FSFXEditScope>& Origin,FString& Error,bool bValidateCanonical=false)
    {
        if(!Origin.IsSet())return true; // Content Browser and scene-library assets have no document dependency.
        const auto& Scope=Origin.GetValue();const auto Source=MCComposerDocuments::Find(Scope.Action);
        if(!Source||!Scope.Token.IsValid()||Source->GetSFXDraftToken()!=Scope.Token||!Source->IsContextCurrent()||(!Scope.Event.IsNone()&&!Source->GetActionStages().Contains(Scope.Event)))
        {Error=TEXT("Исходный документ перетаскивания устарел. Начните перенос звука заново.");return false;}
        return !bValidateCanonical||Source->ValidateSourceCurrent(Error);
    }

    class SSFXComposerWorkspace final : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SSFXComposerWorkspace) {} SLATE_END_ARGS()
        void Construct(const FArguments&);
        virtual ~SSFXComposerWorkspace() override;
        virtual bool SupportsKeyboardFocus() const override {return true;}
        virtual FReply OnKeyDown(const FGeometry&,const FKeyEvent&) override;
        virtual void Tick(const FGeometry& Geometry,double CurrentTime,float DeltaTime) override;
        void SetOwnerTab(const TSharedRef<SDockTab>& Tab) {OwnerTab=Tab;}
        void Closed() {Stop();if(Library)Library->StopSourcePreview();}
        TSharedPtr<FMCVFXActionSession> GetSession() const {return Session;}
        FString Snapshot() const;
        bool SelectForReview(FName Action,FName Event,FGuid Layer,FString& Error);
        bool PlayForReview(EMCPerkPreviewScenario Scenario,bool Loop,FString& Error);
        void Stop() {bPlaying=false;bPaused=false;bAudition=false;RepeatAt=0;if(View)View->Stop();}
    private:
        TSharedPtr<FMCComposerDocumentView> View;
        TSharedPtr<FMCVFXActionSession> Session;
        TSharedPtr<FMCComposerSceneAudioSession> SceneSession;
        TSharedPtr<SMCSFXLibrary> Library;
        TSharedPtr<STreeView<FTreeItem>> Tree;
        TSharedPtr<SBox> BodyHost,PreviewHost,InspectorHost,EventHost,LayersHost,SceneHeaderHost;
        TSharedPtr<SWidget> CatalogueWidget,CenterWidget,InspectorWidget,LibraryWidget;
        TWeakPtr<SDockTab> OwnerTab;
        TArray<FMCVFXActionCatalogueEntry> Catalogue;
        TArray<FTreeItem> Roots;
        TSet<FString> Expanded;
        FName SelectedAction=TEXT("Boss.NutKnight.Roll"),SelectedEvent=NAME_None,SelectedScene=TEXT("NutEncounter");
        FGuid SelectedLayer;
        FString Query,Status;
        uint64 SeenRevision=MAX_uint64,SeenPlaybackRevision=MAX_uint64,SeenSceneRevision=MAX_uint64;
        FTSTicker::FDelegateHandle TickHandle;
        EMCPerkPreviewScenario Scenario=EMCPerkPreviewScenario::Target;
        int32 Density=0;
        bool bCompact=false,bLibraryFocus=false,bSceneMode=false,bSelecting=false,bPlaying=false,bPaused=false,bLoop=false,bAudition=false;
        bool bBackground=true,bMusic=true,bAmbience=true,bInspectorRefreshPending=false;
        double RepeatAt=0;
        FAssetData AuditionAsset;

        FSFXEditScope Scope() const {return {SelectedAction,SelectedEvent,Session?Session->GetSFXDraftToken():FGuid()};}
        bool CheckScope(FSFXEditScope In,FString& Error,bool bRequireEditable=true) const;
        bool CanEdit() const {return !bSceneMode&&Session&&Session->IsContextCurrent()&&Session->CanEditProfile();}
        bool CanPlay() const {return Session&&Session->IsContextCurrent()&&Session->CanPreview();}
        FText EventLabel(FName Event) const {return Session?Session->GetEventLabel(Event):CatalogueEventLabel(SelectedAction,Event);}
        FString TargetLabel() const;
        FText DraftLabel() const;
        const FMCEventSFXLayer* Selected() const;
        void Fail(const FString& Error) {Status=Error;}
        void BuildTree();
        void SelectTree(FTreeItem Item,ESelectInfo::Type Origin);
        TSharedRef<ITableRow> TreeRow(FTreeItem Item,const TSharedRef<STableViewBase>& Owner);
        bool Select(FName Action,FName Event,FGuid Layer,FString& Error);
        void Refresh(bool bPreview=false,bool bPreserveActiveEdit=false);
        void RefreshLibrary();
        void RefreshInspector(bool bPreserveActiveEdit=false);
        void RebuildBody();
        TSharedRef<SWidget> MakeHeader();
        TSharedRef<SWidget> MakeCatalogue();
        TSharedRef<SWidget> MakeCenter();
        TSharedRef<SWidget> MakeInspector();
        TSharedRef<SWidget> MakeLibrary();
        TSharedRef<SWidget> DensityMenu();
        TSharedRef<SWidget> ScenarioMenu();
        TSharedRef<SWidget> ModeMenu();
        TSharedRef<SWidget> LayerRow(const FMCEventSFXLayer& Layer);
        TSharedRef<SWidget> LayerParameters(const FMCEventSFXLayer& Layer);
        void Edit(FSFXEditScope In,FGuid ID,TFunction<void(FMCEventSFXLayer&)> Mutation);
        bool Assign(const FAssetData& Asset,FSFXEditScope In,FGuid Replace=FGuid());
        bool CanAppendSoundToEvent(const FAssetData& Asset,FName Action,FName Event,const TOptional<FSFXEditScope>& Origin,FString& Error) const;
        bool AppendSoundToEvent(const FAssetData& Asset,FName Action,FName Event,FGuid ExpectedToken,const TOptional<FSFXEditScope>& Origin,FString& Error);
        void AssignSelected(bool bReplace);
        void OpenSource(const FAssetData& Asset);
        void Audition(const FAssetData& Asset,FSFXEditScope In);
        bool PrepareScene(FString& Error);
        bool Play(FString& Error);
        void Apply();
        bool Poll(float Delta);
    };
    TWeakPtr<SSFXComposerWorkspace> ActiveSFXWorkspace;

    bool SSFXComposerWorkspace::CheckScope(FSFXEditScope In,FString& Error,bool bRequireEditable) const
    {
        if(bSceneMode||!Session||!Session->IsContextCurrent()||(bRequireEditable&&!CanEdit())||In.Action!=SelectedAction||In.Event!=SelectedEvent||In.Token!=Session->GetSFXDraftToken())
        {Error=TEXT("Назначение звука устарело. Выберите актуальное событие и повторите действие.");return false;}
        return true;
    }
    const FMCEventSFXLayer* SSFXComposerWorkspace::Selected() const
    {
        const auto* Slot=Session?Session->GetSFXSlot(SelectedEvent):nullptr;
        return Slot?Slot->Layers.FindByPredicate([this](const auto& Layer){return Layer.ID==SelectedLayer;}):nullptr;
    }
    FString SSFXComposerWorkspace::TargetLabel() const
    {
        if(bSceneMode)return TEXT("Музыка и атмосфера / ")+SelectedScene.ToString();
        const auto* Entry=Catalogue.FindByPredicate([this](const auto& E){return E.ID==SelectedAction;});
        FString Result=Entry?Entry->OwnerLabel.ToString():SelectedAction.ToString();
        if(Entry&&Entry->OwnerID!=Entry->ID)Result+=TEXT(" / ")+(Entry->ActionLabel.IsEmpty()?Entry->Label.ToString():Entry->ActionLabel.ToString());
        Result+=TEXT(" / ")+EventLabel(SelectedEvent).ToString();
        if(const auto* Layer=Selected())Result+=TEXT(" / ")+(Layer->Name.IsNone()?Layer->Sound.ToSoftObjectPath().GetAssetName():Layer->Name.ToString());
        return Result;
    }
    FText SSFXComposerWorkspace::DraftLabel() const
    {
        const bool Dirty=bSceneMode?SceneSession&&SceneSession->HasChanges():Session&&Session->IsAudioDirty();
        return FText::FromString(Dirty?TEXT("Общий черновик аудио · есть неприменённые изменения"):TEXT("Общий документ с VFX Composer · изменения аудио общие"));
    }

    void SSFXComposerWorkspace::Construct(const FArguments&)
    {
        View=MakeShared<FMCComposerDocumentView>();
        SceneSession=FMCComposerSceneAudioSession::Shared();
        if(!SceneSession->GetSource())
        {
            FString Error;
            if(auto* Palette=LoadObject<UMCSoundPalette>(nullptr,TEXT("/Game/Data/DA_MouthSounds.DA_MouthSounds")))SceneSession->Open(Palette,Error);
        }
        Catalogue=FMCVFXActionSession::GetCatalogue();
        FString Error;
        if(!Select(SelectedAction,SelectedEvent,FGuid(),Error))Status=Error;
        FString AudioError;if(!View->SetPreviewAudioEnabled(true,AudioError))Status=AudioError;
        SeenSceneRevision=SceneSession->GetRevision();
        CatalogueWidget=MakeCatalogue();CenterWidget=MakeCenter();InspectorWidget=MakeInspector();LibraryWidget=MakeLibrary();
        ChildSlot[SNew(SBorder).BorderImage(&PanelBrush()).BorderBackgroundColor(AppFill).Padding(0)
            [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[MakeHeader()]
                +SVerticalBox::Slot().AutoHeight()[SNew(SBorder).BorderImage(&PanelBrush()).BorderBackgroundColor(PanelFill).Padding(12,6)
                    [SNew(SHorizontalBox)
                        +SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text_Lambda([this]{return DraftLabel();}).ColorAndOpacity(WorkspaceMint).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12))]
                        +SHorizontalBox::Slot().AutoWidth()[Label(TEXT("Сохранение звука не меняет визуальные слои"),11,Muted)]]]
                +SVerticalBox::Slot().FillHeight(1)[SAssignNew(BodyHost,SBox)]
                +SVerticalBox::Slot().AutoHeight()[SNew(SBorder).BorderImage(&PanelBrush()).BorderBackgroundColor(AppFill).Padding(10,5)
                    [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Status);}).ToolTipText_Lambda([this]{return FText::FromString(Status);})
                        .OverflowPolicy(ETextOverflowPolicy::Ellipsis).ColorAndOpacity(Muted).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11))]]]];
        BuildTree();RebuildBody();Refresh(true);
        TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(this,&SSFXComposerWorkspace::Poll),.1f);
    }
    SSFXComposerWorkspace::~SSFXComposerWorkspace()
    {FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);Stop();if(Library)Library->StopSourcePreview();}

    TSharedRef<SWidget> SSFXComposerWorkspace::MakeHeader()
    {
        return SNew(SBorder).BorderImage(&PanelBrush()).BorderBackgroundColor(PanelFill).Padding(12,9)
            [SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,20,0)[Label(TEXT("SFX Composer"),17)]
                +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(TargetLabel());})
                    .ToolTipText_Lambda([this]{return FText::FromString(TargetLabel());}).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                    .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),13)).ColorAndOpacity(TextColor)]
                +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SNew(SComboButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(10,6))
                    .OnGetMenuContent(this,&SSFXComposerWorkspace::DensityMenu).ButtonContent()[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Density==0?TEXT("Авто"):Density==1?TEXT("Полный"):TEXT("Компактный"));}).ColorAndOpacity(TextColor)]]
                +SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)[SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(10,6))
                    .OnClicked_Lambda([this]{bLibraryFocus=!bLibraryFocus;RebuildBody();return FReply::Handled();})
                    [SNew(STextBlock).Text_Lambda([this]{return bLibraryFocus?LOCTEXT("ReturnPreview","Вернуть превью"):LOCTEXT("Library","Библиотека");}).ColorAndOpacity(TextColor)]]
                +SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)[SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(10,6))
                    .IsEnabled_Lambda([this]{return !bSceneMode&&Session.IsValid();})
                    .ToolTipText(LOCTEXT("OpenVFXTarget","Открыть это же событие и звуковой слой в VFX Composer. Черновик останется общим."))
                    .OnClicked_Lambda([this]{MCVFXActionWorkspace::Open();FString Error;if(!MCVFXActionWorkspace::SelectAudioTargetForReview(SelectedAction,SelectedEvent,SelectedLayer,Error))Fail(Error);return FReply::Handled();})
                    [Label(TEXT("Открыть VFX"),11)]]
                +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(&SolidStyle())
                    .ButtonColorAndOpacity_Lambda([this]{return (bSceneMode?SceneSession&&SceneSession->HasChanges():CanEdit()&&Session->IsAudioDirty())?WorkspaceMint:HeadingFill;})
                    .ForegroundColor(Ink).ContentPadding(FMargin(12,7))
                    .IsEnabled_Lambda([this]{return bSceneMode?SceneSession&&SceneSession->HasChanges():CanEdit()&&Session->IsAudioDirty();})
                    .OnClicked_Lambda([this]{Apply();return FReply::Handled();})[SNew(STextBlock).Text(LOCTEXT("ApplyAudio","Сохранить звук")).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),12))]]];
    }
    TSharedRef<SWidget> SSFXComposerWorkspace::DensityMenu()
    {
        auto Rows=SNew(SVerticalBox);
        for(int32 Index=0;Index<3;++Index)
            Rows->AddSlot().AutoHeight()[SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(12,7))
                .OnClicked_Lambda([this,Index]{Density=Index;FSlateApplication::Get().DismissAllMenus();RebuildBody();return FReply::Handled();})
                [Label(Index==0?TEXT("Авто по размеру окна"):Index==1?TEXT("Полный"):TEXT("Компактный"),12)]];
        return Rows;
    }
    void SSFXComposerWorkspace::RebuildBody()
    {
        if(!BodyHost||!LibraryWidget)return;
        BodyHost->SetContent(SNullWidget::NullWidget);
        if(bLibraryFocus){BodyHost->SetContent(LibraryWidget.ToSharedRef());return;}
        auto Work=SNew(SSplitter).Orientation(Orient_Horizontal).PhysicalSplitterHandleSize(4)
            +SSplitter::Slot().Value(bCompact?.16f:.14f).MinSize(190)[CatalogueWidget.ToSharedRef()]
            +SSplitter::Slot().Value(bCompact?.61f:.66f).MinSize(320)[CenterWidget.ToSharedRef()]
            +SSplitter::Slot().Value(bCompact?.23f:.20f).MinSize(280)[InspectorWidget.ToSharedRef()];
        BodyHost->SetContent(SNew(SSplitter).Orientation(Orient_Vertical).PhysicalSplitterHandleSize(5)
            +SSplitter::Slot().Value(.67f).MinSize(320)[Work]
            +SSplitter::Slot().Value(.33f).MinSize(235)[LibraryWidget.ToSharedRef()]);
    }
    void SSFXComposerWorkspace::Tick(const FGeometry& Geometry,double CurrentTime,float DeltaTime)
    {
        SCompoundWidget::Tick(Geometry,CurrentTime,DeltaTime);
        const bool Compact=Density==2||(Density==0&&(Geometry.GetLocalSize().X<1600||Geometry.GetLocalSize().Y<930));
        if(Compact!=bCompact){bCompact=Compact;RebuildBody();}
    }

    TSharedRef<SWidget> SSFXComposerWorkspace::MakeCatalogue()
    {
        return SNew(SBorder).BorderImage(&PanelBrush()).BorderBackgroundColor(PanelFill).Padding(9)
            [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().Padding(2,0,0,6)[Label(TEXT("ДЕЙСТВИЯ И СОБЫТИЯ"),11,Muted)]
                +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[SNew(SSearchBox).HintText(LOCTEXT("SearchActions","Шаг, каст, смерть…"))
                    .OnTextChanged_Lambda([this](const FText& Text){Query=Text.ToString();BuildTree();})]
                +SVerticalBox::Slot().AutoHeight().Padding(2,0,0,6)[SNew(STextBlock).Text(LOCTEXT("SFXReviewLegend","SFX одобрено · «−» нужна проверка"))
                    .ToolTipText(LOCTEXT("SFXReviewLegendHelp","Флажки у событий отмечают одобрение звука. Промежуточная отметка означает, что аудио изменилось и его нужно проверить повторно. Это не включение звука."))
                    .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10)).ColorAndOpacity(Muted).AutoWrapText(true)]
                +SVerticalBox::Slot().FillHeight(1)[SAssignNew(Tree,STreeView<FTreeItem>).TreeItemsSource(&Roots).SelectionMode(ESelectionMode::Single)
                    .OnGenerateRow(this,&SSFXComposerWorkspace::TreeRow)
                    .OnGetChildren_Lambda([](FTreeItem Item,TArray<FTreeItem>& Out){if(Item)Out=Item->Children;})
                    .OnSelectionChanged(this,&SSFXComposerWorkspace::SelectTree)
                    .OnExpansionChanged_Lambda([this](FTreeItem Item,bool Open){if(!Item)return;if(Open)Expanded.Add(Item->Key);else Expanded.Remove(Item->Key);})]];
    }
    void SSFXComposerWorkspace::BuildTree()
    {
        Roots.Reset();TMap<FString,FTreeItem> Sections,Owners;
        auto AddNode=[](TArray<FTreeItem>& Into,FString Key,FText Text,ESFXTreeKind Kind){auto Node=MakeShared<FSFXTreeNode>();Node->Key=MoveTemp(Key);Node->Text=MoveTemp(Text);Node->Kind=Kind;Into.Add(Node);return Node;};
        for(const auto& Entry:Catalogue)
        {
            FString Search=Entry.Label.ToString()+TEXT(" ")+Entry.OwnerLabel.ToString()+TEXT(" ")+Entry.ID.ToString();
            for(FName Stage:Entry.Stages)Search+=TEXT(" ")+Stage.ToString()+TEXT(" ")+CatalogueEventLabel(Entry.ID,Stage).ToString();
            if(!Query.IsEmpty()&&!Search.Contains(Query,ESearchCase::IgnoreCase))continue;
            const FString SectionKey=FString::FromInt(static_cast<int32>(Entry.Section));
            FTreeItem Section=Sections.FindRef(SectionKey);
            if(!Section){Section=AddNode(Roots,SectionKey,MCVFXCatalogueNavigation::Label(Entry.Section),ESFXTreeKind::Section);Sections.Add(SectionKey,Section);}
            const FName OwnerID=Entry.OwnerID.IsNone()?Entry.ID:Entry.OwnerID;
            const FText OwnerLabel=Entry.OwnerLabel.IsEmpty()?Entry.Label:Entry.OwnerLabel;
            FTreeItem Action;
            if(OwnerID==Entry.ID)Action=AddNode(Section->Children,Entry.ID.ToString(),OwnerLabel,ESFXTreeKind::Action);
            else
            {
                const FString OwnerKey=SectionKey+TEXT("/")+OwnerID.ToString();FTreeItem Owner=Owners.FindRef(OwnerKey);
                if(!Owner){Owner=AddNode(Section->Children,OwnerKey,OwnerLabel,ESFXTreeKind::Owner);Owners.Add(OwnerKey,Owner);}
                Action=AddNode(Owner->Children,Entry.ID.ToString(),Entry.ActionLabel.IsEmpty()?Entry.Label:Entry.ActionLabel,ESFXTreeKind::Action);
            }
            Action->Action=Entry.ID;Action->bPreview=Entry.bSupportsPreview;Action->bEditable=Entry.bSupportsProfileOverride;
            Action->Reason=FText::FromString(!Entry.UnsupportedReason.IsEmpty()?Entry.UnsupportedReason:Entry.ProfileOverrideUnsupportedReason);
            for(FName Stage:Entry.Stages)
            {
                auto Event=AddNode(Action->Children,Entry.ID.ToString()+TEXT("/")+Stage.ToString(),Entry.ID==SelectedAction?EventLabel(Stage):CatalogueEventLabel(Entry.ID,Stage),ESFXTreeKind::Event);
                Event->Action=Entry.ID;Event->Event=Stage;Event->bPreview=Action->bPreview;Event->bEditable=Action->bEditable;Event->Reason=Action->Reason;
            }
        }
        if(SceneSession)
        {
            auto SceneRoot=AddNode(Roots,TEXT("scene"),LOCTEXT("Scenes","МУЗЫКА И АТМОСФЕРА"),ESFXTreeKind::Section);
            for(FName Name:SceneSession->GetSceneNames())
            {
                if(!Query.IsEmpty()&&!Name.ToString().Contains(Query,ESearchCase::IgnoreCase))continue;
                auto Node=AddNode(SceneRoot->Children,TEXT("scene/")+Name.ToString(),FText::FromName(Name),ESFXTreeKind::Scene);Node->Event=Name;
            }
        }
        if(Tree)
        {
            Tree->RequestTreeRefresh();bSelecting=true;
            TFunction<void(FTreeItem)> Expand=[&](FTreeItem Item){
                const bool Current=Item->Action==SelectedAction;
                Tree->SetItemExpansion(Item,Expanded.Contains(Item->Key)||!Query.IsEmpty()||Item->Kind==ESFXTreeKind::Section||Item->Kind==ESFXTreeKind::Owner||Current);
                if((!bSceneMode&&Item->Kind==ESFXTreeKind::Event&&Item->Action==SelectedAction&&Item->Event==SelectedEvent)||(bSceneMode&&Item->Kind==ESFXTreeKind::Scene&&Item->Event==SelectedScene))
                {Tree->SetSelection(Item,ESelectInfo::Direct);Tree->RequestScrollIntoView(Item);}
                for(auto Child:Item->Children)Expand(Child);
            };
            for(auto Root:Roots)Expand(Root);bSelecting=false;
        }
    }
    TSharedRef<ITableRow> SSFXComposerWorkspace::TreeRow(FTreeItem Item,const TSharedRef<STableViewBase>& Owner)
    {
        const bool Section=Item->Kind==ESFXTreeKind::Section;
        const bool Metadata=(Item->Kind==ESFXTreeKind::Action||Item->Kind==ESFXTreeKind::Event)&&!Item->bPreview&&!Item->bEditable;
        if(Item->Kind!=ESFXTreeKind::Event)return SNew(STableRow<FTreeItem>,Owner).Padding(FMargin(3,5))
            .OnDrop_Lambda([Weak=TWeakPtr<SSFXComposerWorkspace>(SharedThis(this))](const FDragDropEvent& Drag){
                if(!Drag.GetOperationAs<FAssetDragDropOp>())return FReply::Unhandled();
                if(auto W=Weak.Pin())W->Fail(TEXT("Раскройте действие и перетащите звук на конкретное событие. Папка и владелец не выбирают событие автоматически."));
                return FReply::Handled();})
            .ToolTipText(Item->Reason)[SNew(STextBlock).Text(Metadata?FText::FromString(Item->Text.ToString()+TEXT(" · каталог")):Item->Text).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                .Font(FCoreStyle::GetDefaultFontStyle(Section||Item->Kind==ESFXTreeKind::Owner?TEXT("Bold"):TEXT("Regular"),Section?11:12))
                .ColorAndOpacity(Section||Metadata?Muted:Item->Kind==ESFXTreeKind::Event?WorkspaceMint:TextColor)];
        const FName TargetAction=Item->Action,TargetEvent=Item->Event;
        const auto* Entry=Catalogue.FindByPredicate([TargetAction](const auto& E){return E.ID==TargetAction;});
        FString Destination=Entry?(Entry->OwnerLabel.IsEmpty()?Entry->Label.ToString():Entry->OwnerLabel.ToString()):TargetAction.ToString();
        if(Entry&&Entry->OwnerID!=Entry->ID)Destination+=TEXT(" / ")+(Entry->ActionLabel.IsEmpty()?Entry->Label.ToString():Entry->ActionLabel.ToString());
        Destination+=TEXT(" / ")+Item->Text.ToString();
        const auto State=MakeShared<FSFXEventDropState>();
        const auto Weak=TWeakPtr<SSFXComposerWorkspace>(SharedThis(this));
        return SNew(STableRow<FTreeItem>,Owner).Padding(FMargin(3,5))
            .ToolTipText(FText::FromString(TEXT("Добавить звук → ")+Destination))
            .OnCanAcceptDrop_Lambda([Weak,TargetAction,TargetEvent,Destination,State](const FDragDropEvent& Drag,EItemDropZone,FTreeItem)->TOptional<EItemDropZone>{
                const auto W=Weak.Pin();if(!W)return {};
                const auto Assets=Drag.GetOperationAs<FAssetDragDropOp>();if(!Assets)return {};
                if(State->Operation.Pin()!=Drag.GetOperation())
                {
                    State->Operation=Drag.GetOperation();const auto Target=MCComposerDocuments::Find(TargetAction);
                    State->bTargetWasOpen=Target.IsValid();State->TargetToken=Target?Target->GetSFXDraftToken():FGuid();
                }
                const auto Origin=SoundDragOrigin(Drag.GetOperation());
                if(Assets->GetAssets().Num()!=1)State->Reason=TEXT("Перетащите один звуковой источник на конкретное событие.");
                else if(const auto Source=Drag.GetOperationAs<FMCComposerSourceDrag>();Source&&!Source->IsSourceCurrent())State->Reason=TEXT("Исходный документ перетаскивания устарел. Начните перенос звука заново.");
                else if(State->bTargetWasOpen&&(!MCComposerDocuments::Find(TargetAction)||MCComposerDocuments::Find(TargetAction)->GetSFXDraftToken()!=State->TargetToken))State->Reason=TEXT("Целевой документ изменился во время переноса. Начните перетаскивание заново.");
                else if(W->CanAppendSoundToEvent(Assets->GetAssets()[0],TargetAction,TargetEvent,Origin,State->Reason))
                {W->Status=TEXT("Добавить звук «")+Assets->GetAssets()[0].AssetName.ToString()+TEXT("» → ")+Destination;return EItemDropZone::OntoItem;}
                W->Status=State->Reason;return {};
            })
            .OnAcceptDrop_Lambda([Weak,TargetAction,TargetEvent,State](const FDragDropEvent& Drag,EItemDropZone,FTreeItem){
                if(auto W=Weak.Pin())
                {
                    const auto Assets=Drag.GetOperationAs<FAssetDragDropOp>();
                    const auto Origin=SoundDragOrigin(Drag.GetOperation());
                    FString Error;
                    if(!Assets||Assets->GetAssets().Num()!=1)W->Fail(TEXT("Перенос не содержит одного звукового источника."));
                    else if(!W->AppendSoundToEvent(Assets->GetAssets()[0],TargetAction,TargetEvent,State->bTargetWasOpen?State->TargetToken:FGuid(),Origin,Error))W->Fail(Error);
                }
                return FReply::Handled();
            })
            .OnDrop_Lambda([Weak,State](const FDragDropEvent& Drag){
                if(!Drag.GetOperationAs<FAssetDragDropOp>())return FReply::Unhandled();
                if(auto W=Weak.Pin())W->Fail(State->Reason.IsEmpty()?TEXT("Звук не может быть добавлен в это событие."):State->Reason);
                return FReply::Handled();
            })
            .OnPaintDropIndicator_Lambda([](EItemDropZone,const FPaintArgs&,const FGeometry& Geometry,const FSlateRect&,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle&,bool){
                FLinearColor Fill=WorkspaceMint;Fill.A=.22f;
                FSlateDrawElement::MakeBox(Elements,Layer+1,Geometry.ToPaintGeometry(),&PanelBrush(),ESlateDrawEffect::None,Fill);
                const FVector2f Size(Geometry.GetLocalSize());TArray<FVector2f> Outline;
                Outline.Add(FVector2f(1,1));Outline.Add(FVector2f(Size.X-1,1));Outline.Add(FVector2f(Size.X-1,Size.Y-1));Outline.Add(FVector2f(1,Size.Y-1));Outline.Add(FVector2f(1,1));
                FSlateDrawElement::MakeLines(Elements,Layer+2,Geometry.ToPaintGeometry(),MoveTemp(Outline),ESlateDrawEffect::None,WorkspaceMint,true,2.f);
                return Layer+2;
            })
            [SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(Metadata?FText::FromString(Item->Text.ToString()+TEXT(" · каталог")):Item->Text)
                    .OverflowPolicy(ETextOverflowPolicy::Ellipsis).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12)).ColorAndOpacity(Metadata?Muted:WorkspaceMint)]
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5,0,0,0)[SNew(STextBlock).Text_Lambda([TargetAction,TargetEvent]{
                    const auto Document=MCComposerDocuments::Find(TargetAction);const auto* Slot=Document?Document->GetSFXSlot(TargetEvent):nullptr;
                    return Slot&&!Slot->Layers.IsEmpty()?FText::FromString(FString::Printf(TEXT("%d сл."),Slot->Layers.Num())):FText::GetEmpty();})
                    .ColorAndOpacity(Muted).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10))]
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5,0,0,0)
                    [MakeSFXApprovalCheck(TargetAction,TargetEvent,Destination,false,[Weak](const FString& Message){if(auto W=Weak.Pin())W->Status=Message;})]];
    }
    void SSFXComposerWorkspace::SelectTree(FTreeItem Item,ESelectInfo::Type Origin)
    {
        if(!Item||bSelecting||Origin==ESelectInfo::Direct)return;
        if(Item->Kind==ESFXTreeKind::Section||Item->Kind==ESFXTreeKind::Owner){Tree->SetItemExpansion(Item,!Tree->IsItemExpanded(Item));return;}
        if(Item->Kind==ESFXTreeKind::Scene)
        {
            Stop();bSceneMode=true;SelectedScene=Item->Event;RefreshInspector();RefreshLibrary();return;
        }
        FString Error;if(!Select(Item->Action,Item->Event,FGuid(),Error))Fail(Error);
    }

    bool SSFXComposerWorkspace::Select(FName Action,FName Event,FGuid Layer,FString& Error)
    {
        const auto* Entry=Catalogue.FindByPredicate([&](const auto& E){return E.ID==Action;});
        if(!Entry){Error=TEXT("Действие отсутствует в общем каталоге Composer.");return false;}
        if(!Event.IsNone()&&!Entry->Stages.Contains(Event)){Error=TEXT("Событие не принадлежит выбранному действию.");return false;}
        Stop();
        if(!Entry->bSupportsPreview&&!Entry->bSupportsProfileOverride)
        {
            if(Layer.IsValid()){Error=TEXT("У записи каталога без адаптера нет редактируемых звуковых слоёв.");return false;}
            Session.Reset();SelectedAction=Action;SelectedEvent=Event.IsNone()?(Entry->Stages.IsEmpty()?NAME_None:Entry->Stages[0]):Event;
            SelectedLayer.Invalidate();bSceneMode=false;Status=Entry->UnsupportedReason.IsEmpty()?TEXT("Запись каталога. Контекстный сценарий и редактирование ещё не подключены."):Entry->UnsupportedReason;
            Refresh(true);BuildTree();return true;
        }
        if(!View->Select(Action,Error))return false;
        Session=View->GetDocument();SelectedAction=Action;bSceneMode=false;
        const auto Stages=Session?Session->GetActionStages():Entry->Stages;
        SelectedEvent=Event.IsNone()?(Stages.IsEmpty()?NAME_None:Stages[0]):Event;
        SelectedLayer=Layer;
        // Background context follows the same explicit source families as the existing Composer.
        const FString ActionID=Action.ToString();
        SelectedScene=(ActionID.StartsWith(TEXT("Hub."))||ActionID==TEXT("EditorOnly.FishingCatch"))?FName(TEXT("Hub")):
            (ActionID.StartsWith(TEXT("Boss.Nut"))||ActionID.StartsWith(TEXT("Enemy.Nut"))||ActionID==TEXT("Event.NutRain.Entry")||ActionID==TEXT("Food.NutThrow"))?FName(TEXT("NutEncounter")):FName(TEXT("Gameplay"));
        if(Layer.IsValid()&&!Selected()){Error=TEXT("Звуковой слой отсутствует в выбранном событии.");SelectedLayer.Invalidate();Refresh(true);return false;}
        if(Session&&CanPlay()&&!Session->GetScenarios().Contains(Scenario))
        {const auto Scenarios=Session->GetScenarios();if(!Scenarios.IsEmpty())Scenario=Scenarios[0];}
        Status=CanPlay()?TEXT("Выберите звук в библиотеке. Прослушивание источника не меняет черновик."):
            Session?Session->GetPreviewUnavailableReason():Entry->UnsupportedReason;
        Refresh(true);BuildTree();return true;
    }
    bool SSFXComposerWorkspace::SelectForReview(FName Action,FName Event,FGuid Layer,FString& Error)
    {return Select(Action,Event,Layer,Error);}

    TSharedRef<SWidget> SSFXComposerWorkspace::MakeCenter()
    {
        return SNew(SBorder).BorderImage(&PanelBrush()).BorderBackgroundColor(AppFill).Padding(8)
            [SNew(SVerticalBox)
                +SVerticalBox::Slot().FillHeight(1)[SNew(SBorder).BorderImage(&PanelBrush()).BorderBackgroundColor(GraphFill).Padding(5)
                    [SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight().Padding(5,3)[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(bSceneMode?TEXT("ФОН ")+SelectedScene.ToString()+TEXT(" · КОНТЕКСТ ")+(Session?Session->GetActionLabel().ToString():SelectedAction.ToString()):TEXT("КОНТЕКСТНОЕ ПРЕВЬЮ"));})
                                .OverflowPolicy(ETextOverflowPolicy::Ellipsis).ColorAndOpacity(Muted).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11))]
                            +SHorizontalBox::Slot().AutoWidth()[SNew(SComboButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(7,3))
                                .OnGetMenuContent_Lambda([this]{auto Menu=SNew(SVerticalBox);for(int32 Index=0;Index<3;++Index)Menu->AddSlot().AutoHeight()[SmallCommand(Index==0?TEXT("Общий вид"):Index==1?TEXT("Спереди"):TEXT("Сбоку"),[this,Index]{View->SetPreviewView(Index);FSlateApplication::Get().DismissAllMenus();})];return Menu;})
                                .ButtonContent()[Label(TEXT("Камера"),11)]]]
                        +SVerticalBox::Slot().FillHeight(1)[SAssignNew(PreviewHost,SBox)]]]
                +SVerticalBox::Slot().AutoHeight().Padding(0,6)[SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(&SolidStyle()).ButtonColorAndOpacity(WorkspaceMint).ForegroundColor(Ink).ContentPadding(FMargin(12,7))
                        .IsEnabled_Lambda([this]{return CanPlay();}).ToolTipText_Lambda([this]{return FText::FromString(CanPlay()?TEXT("Выполнить настоящее действие с текущими общими настройками аудио."):Session?Session->GetPreviewUnavailableReason():TEXT("Выберите действие"));})
                        .OnClicked_Lambda([this]{FString Error;if(!Play(Error))Fail(Error);return FReply::Handled();})[Label(TEXT("▶ Играть действие"),12,Ink)]]
                    +SHorizontalBox::Slot().AutoWidth().Padding(5,0)[SmallCommand(TEXT("Ⅱ"),[this]{if(View->OwnsPreview()){bPaused=!bPaused;View->Pause(bPaused);}})]
                    +SHorizontalBox::Slot().AutoWidth()[SmallCommand(TEXT("■"),[this]{Stop();})]
                    +SHorizontalBox::Slot().AutoWidth().Padding(9,0)[SNew(SCheckBox).IsChecked_Lambda([this]{return bLoop?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState State){bLoop=State==ECheckBoxState::Checked;})[Label(TEXT("Повтор"),11)]]
                    +SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Right).VAlign(VAlign_Center)[SNew(STextBlock).Text_Lambda([this]{const auto Runner=View->GetRunner();return FText::FromString(Runner?FString::Printf(TEXT("%.2f с · %s"),Runner->GetElapsed(),*Runner->GetPhaseName()):TEXT("Остановлено"));}).ColorAndOpacity(Muted).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11))]
                    +SHorizontalBox::Slot().AutoWidth().Padding(8,0,0,0)[SNew(SComboButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(8,5)).OnGetMenuContent(this,&SSFXComposerWorkspace::ScenarioMenu)
                        .ButtonContent()[Label(TEXT("Сценарий"),11)]]]
                +SVerticalBox::Slot().AutoHeight()[SAssignNew(EventHost,SBox)]
                +SVerticalBox::Slot().AutoHeight().Padding(0,5)[SNew(SBox).MaxDesiredHeight_Lambda([this]{return bCompact?65.f:100.f;})
                    [SNew(SBorder).BorderImage(&PanelBrush()).BorderBackgroundColor(PanelFill).Padding(8,5)
                        [SNew(SScrollBox)+SScrollBox::Slot()[SNew(STextBlock).Text_Lambda([this]{const auto Runner=View->GetRunner();if(!Runner)return LOCTEXT("RealEvents","Тайминг появится после запуска: отображаются реальные события механики.");FString Result;for(const auto& E:Runner->GetEvents())Result+=FString::Printf(TEXT("%.2f с   %s\n"),E.Seconds,*EventLabel(E.Stage).ToString());return FText::FromString(Result);})
                            .ColorAndOpacity(Muted).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11)).AutoWrapText(true)]]]]
                +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)[Label(TEXT("КОНТЕКСТ"),11,Muted)]
                    +SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)[SNew(SCheckBox).IsChecked_Lambda([this]{return bBackground?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState State){bBackground=State==ECheckBoxState::Checked;FString Error;if(!PrepareScene(Error))Fail(Error);})[Label(TEXT("С фоном"),11)]]
                    +SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)[SNew(SCheckBox).IsChecked_Lambda([this]{return View->IsPreviewAudioEnabled()?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                        .ToolTipText(LOCTEXT("PreviewAudioSwitch","Включает весь звук изолированного предпросмотра, включая выбранный музыкальный фон."))
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState State){FString Error;if(!View->SetPreviewAudioEnabled(State==ECheckBoxState::Checked,Error))Fail(Error);RefreshLibrary();})[Label(TEXT("Звук превью"),11)]]
                    +SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)[SNew(SCheckBox).IsChecked_Lambda([this]{return bMusic?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState State){bMusic=State==ECheckBoxState::Checked;FString Error;if(!PrepareScene(Error))Fail(Error);})[Label(TEXT("Музыка"),11)]]
                    +SHorizontalBox::Slot().AutoWidth()[SNew(SCheckBox).IsChecked_Lambda([this]{return bAmbience?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState State){bAmbience=State==ECheckBoxState::Checked;FString Error;if(!PrepareScene(Error))Fail(Error);})[Label(TEXT("Атмосфера"),11)]]]];
    }
    TSharedRef<SWidget> SSFXComposerWorkspace::ScenarioMenu()
    {
        auto Rows=SNew(SVerticalBox);
        if(Session)for(auto Item:Session->GetScenarios())Rows->AddSlot().AutoHeight()[SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(10,6))
            .OnClicked_Lambda([this,Item]{Stop();Scenario=Item;FSlateApplication::Get().DismissAllMenus();return FReply::Handled();})[SNew(STextBlock).Text(Session->GetScenarioLabel(Item)).ColorAndOpacity(TextColor)]];
        return Rows;
    }

    TSharedRef<SWidget> SSFXComposerWorkspace::MakeInspector()
    {
        return SNew(SBorder).BorderImage(&PanelBrush()).BorderBackgroundColor(PanelFill).Padding(9)
            [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text_Lambda([this]{return bSceneMode?LOCTEXT("SceneTitle","МУЗЫКА И АТМОСФЕРА"):EventLabel(SelectedEvent);})
                        .OverflowPolicy(ETextOverflowPolicy::Ellipsis).ToolTipText_Lambda([this]{return EventLabel(SelectedEvent);})
                        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),12)).ColorAndOpacity(TextColor)]
                    +SHorizontalBox::Slot().AutoWidth()[Label(TEXT("Общее аудио"),11,WorkspaceMint)]]
                +SVerticalBox::Slot().FillHeight(.48f)[SAssignNew(LayersHost,SBox)]
                +SVerticalBox::Slot().AutoHeight().Padding(0,6)[SAssignNew(SceneHeaderHost,SBox)]
                +SVerticalBox::Slot().FillHeight(.52f)[SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(InspectorHost,SBox)]]];
    }
    TSharedRef<SWidget> SSFXComposerWorkspace::LayerRow(const FMCEventSFXLayer& Layer)
    {
        const auto In=Scope();const FGuid ID=Layer.ID;
        const FString Name=Layer.Name.IsNone()?Layer.Sound.ToSoftObjectPath().GetAssetName():Layer.Name.ToString();
        const auto Weak=TWeakPtr<SSFXComposerWorkspace>(SharedThis(this));
        const auto* Entry=Catalogue.FindByPredicate([this](const auto& E){return E.ID==SelectedAction;});
        FString EventPath=Entry?(Entry->OwnerLabel.IsEmpty()?Entry->Label.ToString():Entry->OwnerLabel.ToString()):SelectedAction.ToString();
        if(Entry&&Entry->OwnerID!=Entry->ID)EventPath+=TEXT(" / ")+(Entry->ActionLabel.IsEmpty()?Entry->Label.ToString():Entry->ActionLabel.ToString());
        EventPath+=TEXT(" / ")+EventLabel(In.Event).ToString();
        const auto Content=SNew(SBorder).BorderImage_Lambda([this,ID]{return SelectedLayer==ID?&SelectedPanelBrush():&PanelBrush();})
            .BorderBackgroundColor_Lambda([this,ID]{return SelectedLayer==ID?UIColor(TEXT("#203E3E")):Surface;}).Padding(7,5)
            [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,5,0)[SNew(SCheckBox).IsEnabled(CanEdit()).IsChecked(Layer.bEnabled?ECheckBoxState::Checked:ECheckBoxState::Unchecked)
                        .OnCheckStateChanged_Lambda([Weak,In,ID](ECheckBoxState Value){if(auto W=Weak.Pin())W->Edit(In,ID,[Value](auto& L){L.bEnabled=Value==ECheckBoxState::Checked;});})]
                    +SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(2,2))
                        .OnClicked_Lambda([Weak,In,ID]{if(auto W=Weak.Pin()){FString Error;if(W->CheckScope(In,Error,false)){W->SelectedLayer=ID;W->RefreshInspector();W->RefreshLibrary();}else W->Fail(Error);}return FReply::Handled();})
                        [SNew(STextBlock).Text(FText::FromString(Name)).OverflowPolicy(ETextOverflowPolicy::Ellipsis).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),12)).ColorAndOpacity(TextColor)]]
                    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Label(Layer.Volume>0?FString::Printf(TEXT("%.1f дБ"),20.f*FMath::LogX(10.f,Layer.Volume)):TEXT("−∞ дБ"),11,Muted)]]
                +SVerticalBox::Slot().AutoHeight().Padding(0,3,0,0)[SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Layer.Sound.ToSoftObjectPath().GetAssetName()))
                        .ToolTipText(FText::FromString(Layer.Sound.ToSoftObjectPath().ToString())).OverflowPolicy(ETextOverflowPolicy::Ellipsis).ColorAndOpacity(Muted).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11))]
                    +SHorizontalBox::Slot().AutoWidth().Padding(3,0)[SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(5,2)).ToolTipText(LOCTEXT("DuplicateLayer","Дублировать звуковой слой"))
                        .IsEnabled(CanEdit()).OnClicked_Lambda([Weak,In,ID]{if(auto W=Weak.Pin()){FString Error;if(!W->CheckScope(In,Error))W->Fail(Error);else{const FGuid NewID=W->Session->DuplicateSFXLayer(In.Token,In.Event,ID,Error);if(NewID.IsValid()){W->SelectedLayer=NewID;W->Refresh();}else W->Fail(Error);}}return FReply::Handled();})[Label(TEXT("⧉"),11)]]
                    +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(5,2)).ToolTipText(LOCTEXT("RemoveLayer","Удалить звуковой слой"))
                        .IsEnabled(CanEdit()).OnClicked_Lambda([Weak,In,ID]{if(auto W=Weak.Pin()){FString Error;if(!W->CheckScope(In,Error)||!W->Session->RemoveSFXLayer(In.Token,In.Event,ID,Error))W->Fail(Error);else W->Refresh();}return FReply::Handled();})[Label(TEXT("×"),12)]]]];
        return SNew(SSFXDropTarget).Text(LOCTEXT("ReplaceLayerDrop","Заменить звук этого слоя"))
            .Destination(FText::FromString(EventPath+TEXT(" / ")+Name)).Replacement(true).Compact(true)
            .Enabled_Lambda([Weak]{const auto W=Weak.Pin();return W&&W->CanEdit();})
            .CanDrop([Weak,In,ID](const FAssetData&,const TOptional<FSFXEditScope>& Origin,FString& Error){
                const auto W=Weak.Pin();if(!W)return false;
                if(!W->CheckScope(In,Error)||(Origin.IsSet()&&!W->CheckScope(Origin.GetValue(),Error)))return false;
                const auto* Slot=W->Session->GetSFXSlot(In.Event);
                if(!Slot||!Slot->Layers.ContainsByPredicate([ID](const auto& L){return L.ID==ID;})){Error=TEXT("Этот звуковой слой больше не существует. Обновите список и повторите перенос.");return false;}
                return true;})
            .Feedback([Weak](const FString& Message){if(auto W=Weak.Pin())W->Status=Message;})
            .Drop([Weak,In,ID](const FAssetData& Asset,const TOptional<FSFXEditScope>&){if(auto W=Weak.Pin())W->Assign(Asset,In,ID);return true;})
            [Content];
    }
    TSharedRef<SWidget> SSFXComposerWorkspace::ModeMenu()
    {
        const auto In=Scope();auto Rows=SNew(SVerticalBox);
        const auto Capability=Session?Session->GetSFXCapability(SelectedEvent):FMCComposerSFXCapabilities();
        for(auto Mode:{EMCEventSFXMode::UseDefault,EMCEventSFXMode::Add,EMCEventSFXMode::Replace,EMCEventSFXMode::Disabled})
        {
            const FString Name=AudioPolicyLabel(Mode);
            const bool Available=CanEdit()&&(Mode==EMCEventSFXMode::UseDefault||Mode==EMCEventSFXMode::Add||Capability.bCanReplaceNativeAudio);
            Rows->AddSlot().AutoHeight()[SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(10,6)).IsEnabled(Available)
                .ToolTipText(FText::FromString(Capability.UnavailableReason)).OnClicked_Lambda([this,In,Mode]{FString Error;if(!CheckScope(In,Error)||!Session->SetSFXMode(In.Token,In.Event,Mode,Error))Fail(Error);else Refresh();FSlateApplication::Get().DismissAllMenus();return FReply::Handled();})[Label(Name,12)]];
        }
        return Rows;
    }
    void SSFXComposerWorkspace::RefreshInspector(bool bPreserveActiveEdit)
    {
        if(!InspectorHost||!LayersHost||!SceneHeaderHost)return;
        const bool bDeferParameters=bPreserveActiveEdit&&InspectorHost->HasFocusedDescendants();
        bInspectorRefreshPending=bDeferParameters;
        if(bSceneMode)
        {
            LayersHost->SetContent(SNew(SMCComposerSceneAudioPanel).Session(SceneSession).Scene_Lambda([this]{return SelectedScene;})
                .Section(EMCComposerSceneAudioPanelSection::Header).CanPreview_Lambda([this]{return CanPlay();})
                .OnSceneSelected_Lambda([this](FName Scene){Stop();SelectedScene=Scene;RefreshInspector();RefreshLibrary();})
                .OnChanged_Lambda([this]{RefreshInspector();}).OnPlay_Lambda([this]{FString Error;if(!Play(Error))Fail(Error);}).OnStop_Lambda([this]{Stop();}));
            SceneHeaderHost->SetContent(SNullWidget::NullWidget);
            if(bDeferParameters)return;
            InspectorHost->SetContent(SNew(SMCComposerSceneAudioPanel).Session(SceneSession).Scene_Lambda([this]{return SelectedScene;})
                .Section(EMCComposerSceneAudioPanelSection::Layers).CanPreview_Lambda([this]{return CanPlay();}).OnChanged_Lambda([this]{RefreshInspector();})
                .OnPlay_Lambda([this]{FString Error;if(!Play(Error))Fail(Error);}).OnStop_Lambda([this]{Stop();}));
            return;
        }
        const auto In=Scope();const auto Weak=TWeakPtr<SSFXComposerWorkspace>(SharedThis(this));
        const auto* Entry=Catalogue.FindByPredicate([this](const auto& E){return E.ID==SelectedAction;});
        FString EventPath=Entry?(Entry->OwnerLabel.IsEmpty()?Entry->Label.ToString():Entry->OwnerLabel.ToString()):SelectedAction.ToString();
        if(Entry&&Entry->OwnerID!=Entry->ID)EventPath+=TEXT(" / ")+(Entry->ActionLabel.IsEmpty()?Entry->Label.ToString():Entry->ActionLabel.ToString());
        EventPath+=TEXT(" / ")+EventLabel(In.Event).ToString();
        auto Rows=SNew(SVerticalBox);
        Rows->AddSlot().AutoHeight().Padding(0,0,0,5)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1)[Label(TEXT("Слои вместе"),12)]
            +SHorizontalBox::Slot().AutoWidth()[SNew(SComboButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(7,3))
                .OnGetMenuContent(this,&SSFXComposerWorkspace::ModeMenu).ButtonContent()[SNew(STextBlock)
                    .Text_Lambda([this]{const auto* S=Session?Session->GetSFXSlot(SelectedEvent):nullptr;return FText::FromString(AudioPolicyLabel(S?S->Mode:EMCEventSFXMode::UseDefault));})
                    .ColorAndOpacity(Muted).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10))]]];
        auto List=SNew(SVerticalBox);
        const auto* Slot=Session?Session->GetSFXSlot(SelectedEvent):nullptr;
        if(Slot)for(const auto& Layer:Slot->Layers)List->AddSlot().AutoHeight().Padding(0,0,0,5)[LayerRow(Layer)];
        if(!Slot||Slot->Layers.IsEmpty())List->AddSlot().AutoHeight().Padding(4,9)[SNew(STextBlock).Text(LOCTEXT("NoLayers","Добавленных слоёв пока нет. Родной звук задаётся политикой события.")).AutoWrapText(true).ColorAndOpacity(Muted).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12))];
        Rows->AddSlot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[List]];
        LayersHost->SetContent(SNew(SSFXDropTarget).Text(LOCTEXT("NewSoundLayerDrop","Перетащите звук сюда — новый слой"))
            .Destination(FText::FromString(EventPath)).Enabled_Lambda([Weak]{const auto W=Weak.Pin();return W&&W->CanEdit();})
            .CanDrop([Weak,In](const FAssetData&,const TOptional<FSFXEditScope>& Origin,FString& Error){const auto W=Weak.Pin();return W&&W->CheckScope(In,Error)&&(!Origin.IsSet()||W->CheckScope(Origin.GetValue(),Error));})
            .Feedback([Weak](const FString& Message){if(auto W=Weak.Pin())W->Status=Message;})
            .Drop([Weak,In](const FAssetData& Asset,const TOptional<FSFXEditScope>&){if(auto W=Weak.Pin())W->Assign(Asset,In);return true;})[Rows]);
        SceneHeaderHost->SetContent(SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(8,7)).IsEnabled_Lambda([this]{return CanEdit();})
                .ToolTipText(FText::FromString(EventPath)).OnClicked_Lambda([Weak]{if(auto W=Weak.Pin())W->AssignSelected(false);return FReply::Handled();})
                [Label(TEXT("Добавить выбранный из библиотеки"),11)]]
            +SVerticalBox::Slot().AutoHeight().Padding(3,6,0,2)
                [MakeSFXApprovalCheck(In.Action,In.Event,EventPath,true,[Weak](const FString& Message){if(auto W=Weak.Pin())W->Status=Message;})]);
        if(bDeferParameters)return;
        if(const auto* Layer=Selected())InspectorHost->SetContent(LayerParameters(*Layer));
        else
        {
            auto Detail=SNew(SVerticalBox);
            Detail->AddSlot().AutoHeight().Padding(0,8)[SNew(STextBlock).Text(LOCTEXT("SelectSoundLayer","Выберите слой выше, чтобы настроить громкость, высоту тона и задержку.")).AutoWrapText(true).ColorAndOpacity(Muted)];
            if(Session)
            {
                const auto Capability=Session->GetSFXCapability(SelectedEvent);
                const FString DetailText=Capability.bOriginalSourceKnown?TEXT("Родной источник зарегистрирован. Добавленные слои редактируются независимо."):
                    Capability.bOriginalAudioConfirmedSilent?TEXT("Отсутствие родного звука подтверждено источником."):TEXT("Родной звук этого события ещё не полностью описан. Подавление недоступно без проверенной привязки.");
                Detail->AddSlot().AutoHeight().Padding(0,5)[SNew(STextBlock).Text(FText::FromString(DetailText)).AutoWrapText(true).ColorAndOpacity(Muted).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11))];
                if(!CanEdit())Detail->AddSlot().AutoHeight().Padding(0,6)[SNew(STextBlock).Text(FText::FromString(Session->GetEditUnavailableReason())).AutoWrapText(true).ColorAndOpacity(Muted)];
            }
            InspectorHost->SetContent(Detail);
        }
    }
    TSharedRef<SWidget> SSFXComposerWorkspace::LayerParameters(const FMCEventSFXLayer& Layer)
    {
        const FSFXEditScope In=Scope();const FGuid ID=Layer.ID;const auto Weak=TWeakPtr<SSFXComposerWorkspace>(SharedThis(this));
        auto Rows=SNew(SVerticalBox);
        Rows->AddSlot().AutoHeight().Padding(0,0,0,7)[SNew(STextBlock).Text(FText::FromString(Layer.Name.IsNone()?Layer.Sound.ToSoftObjectPath().GetAssetName():Layer.Name.ToString()))
            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),13)).ColorAndOpacity(TextColor).OverflowPolicy(ETextOverflowPolicy::Ellipsis)];
        auto Number=[&](const FString& Name,float Value,float Min,float Max,TFunction<void(FMCEventSFXLayer&,float)> Setter)
        {
            auto Pending=MakeShared<float>(Value);
            Rows->AddSlot().AutoHeight().Padding(0,4)[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Label(Name,12)]
                +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(86)[SNew(SSpinBox<float>).IsEnabled(CanEdit()).Value(Value).MinValue(Min).MaxValue(Max).Delta(.01f)
                    .OnValueCommitted_Lambda([Weak,In,ID,Setter,Value](float NewValue,ETextCommit::Type){if(FMath::IsNearlyEqual(NewValue,Value))return;if(auto W=Weak.Pin())W->Edit(In,ID,[Setter,NewValue](auto& L){Setter(L,NewValue);});})]]];
            if(Name!=TEXT("Задержка, с"))Rows->AddSlot().AutoHeight().Padding(0,0,0,5)[SNew(SSlider).IsEnabled(CanEdit()).MinValue(Min).MaxValue(Max).Value_Lambda([Pending]{return *Pending;})
                .SliderBarColor(BorderFill).SliderHandleColor(WorkspaceMint).OnValueChanged_Lambda([Pending](float V){*Pending=V;})
                .OnMouseCaptureEnd_Lambda([Weak,In,ID,Setter,Pending,Value]{if(FMath::IsNearlyEqual(*Pending,Value))return;if(auto W=Weak.Pin())W->Edit(In,ID,[Setter,Pending](auto& L){Setter(L,*Pending);});})
                .OnControllerCaptureEnd_Lambda([Weak,In,ID,Setter,Pending,Value]{if(FMath::IsNearlyEqual(*Pending,Value))return;if(auto W=Weak.Pin())W->Edit(In,ID,[Setter,Pending](auto& L){Setter(L,*Pending);});})];
        };
        Number(TEXT("Громкость, дБ"),20.f*FMath::LogX(10.f,FMath::Max(.001f,Layer.Volume)),-60.f,12.04f,
            [](auto& L,float V){L.Volume=FMath::Clamp(FMath::Pow(10.f,V/20.f),0.f,4.f);});
        Number(TEXT("Высота тона ×"),Layer.Pitch,.125f,4.f,[](auto& L,float V){L.Pitch=V;});
        Number(TEXT("Задержка, с"),Layer.DelaySeconds,0.f,60.f,[](auto& L,float V){L.DelaySeconds=V;});
        Rows->AddSlot().AutoHeight().Padding(0,6)[SNew(SExpandableArea).InitiallyCollapsed(true)
            .HeaderContent()[Label(TEXT("Время и пространство"),12)]
            .BodyContent()[SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().Padding(0,7)[SNew(SCheckBox).IsEnabled(CanEdit()).IsChecked(Layer.bRepeatOnFinish?ECheckBoxState::Checked:ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([Weak,In,ID](ECheckBoxState Value){if(auto W=Weak.Pin())W->Edit(In,ID,[Value](auto& L){L.bRepeatOnFinish=Value==ECheckBoxState::Checked;});})[Label(TEXT("Повторять конечный источник"),11)]]
                +SVerticalBox::Slot().AutoHeight()[Label(FString::Printf(TEXT("Пространство: %s"),Layer.Space==EMCEventSFXSpace::NonSpatial2D?TEXT("2D"):Layer.Space==EMCEventSFXSpace::EventLocation?TEXT("Точка события"):Layer.Space==EMCEventSFXSpace::FollowEventFrame?TEXT("Следовать событию"):TEXT("Сокет источника")),11,Muted)]
                +SVerticalBox::Slot().AutoHeight().Padding(0,5)[SNew(STextBlock).Text(LOCTEXT("PlacementNote","Позиция и контекст принадлежат настоящему событию. Источник сохраняет настройки пространственного звука.")).AutoWrapText(true).ColorAndOpacity(Muted).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11))]]];
        Rows->AddSlot().AutoHeight().Padding(0,4)[SNew(SObjectPropertyEntryBox).AllowedClass(USoundBase::StaticClass()).ObjectPath(Layer.Sound.ToSoftObjectPath().ToString()).IsEnabled(CanEdit())
            .OnObjectChanged_Lambda([Weak,In,ID](const FAssetData& Asset){if(auto W=Weak.Pin())W->Assign(Asset,In,ID);})];
        Rows->AddSlot().AutoHeight().Padding(0,5)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)[SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(6,5)).IsEnabled(CanEdit())
                .OnClicked_Lambda([this]{AssignSelected(true);return FReply::Handled();})[Label(TEXT("Заменить из библиотеки"),10)]]
            +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(6,5)).IsEnabled(!Layer.Sound.IsNull())
                .OnClicked_Lambda([Weak,In,ID]{if(auto W=Weak.Pin()){FString Error;if(!W->CheckScope(In,Error,false))W->Fail(Error);else if(const auto* L=W->Selected()){if(L->ID==ID)if(auto* Sound=L->Sound.LoadSynchronous())W->OpenSource(FAssetData(Sound));}}return FReply::Handled();})[Label(TEXT("Открыть схему"),10)]]];
        Rows->AddSlot().AutoHeight().Padding(0,4)[SNew(STextBlock).Text(LOCTEXT("SchemesNote","Sound Cue и MetaSound редактируются в своих настоящих редакторах. Слои события звучат вместе.")).AutoWrapText(true).ColorAndOpacity(Muted).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11))];
        return Rows;
    }

    TSharedRef<SWidget> SSFXComposerWorkspace::MakeLibrary()
    {
        const auto Weak=TWeakPtr<SSFXComposerWorkspace>(SharedThis(this));
        const auto EventDestination=[Weak]()->FText{
            const auto W=Weak.Pin();if(!W)return FText::GetEmpty();
            const auto* Entry=W->Catalogue.FindByPredicate([&](const auto& E){return E.ID==W->SelectedAction;});
            FString Path=Entry?(Entry->OwnerLabel.IsEmpty()?Entry->Label.ToString():Entry->OwnerLabel.ToString()):W->SelectedAction.ToString();
            if(Entry&&Entry->OwnerID!=Entry->ID)Path+=TEXT(" / ")+(Entry->ActionLabel.IsEmpty()?Entry->Label.ToString():Entry->ActionLabel.ToString());
            return FText::FromString(Path+TEXT(" / ")+W->EventLabel(W->SelectedEvent).ToString());};
        return SNew(SBorder).BorderImage(&PanelBrush()).BorderBackgroundColor(PanelFill).Padding(6)
            [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().Padding(4,0,4,5)[SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,7,0)[SNew(SSFXDropTarget)
                        .Text_Lambda([Weak]{const auto W=Weak.Pin();return FText::FromString(TEXT("＋ Новый слой · ")+(W?W->EventLabel(W->SelectedEvent).ToString():FString()));})
                        .Destination_Lambda(EventDestination)
                        .TargetKey_Lambda([Weak]{const auto W=Weak.Pin();if(!W)return FString();const auto In=W->Scope();return In.Action.ToString()+TEXT("/")+In.Event.ToString()+TEXT("/")+In.Token.ToString();})
                        .Enabled_Lambda([Weak]{const auto W=Weak.Pin();return W&&W->CanEdit();})
                        .CanDrop([Weak](const FAssetData&,const TOptional<FSFXEditScope>& Origin,FString& Error){const auto W=Weak.Pin();return W&&W->CheckScope(W->Scope(),Error)&&(!Origin.IsSet()||W->CheckScope(Origin.GetValue(),Error));})
                        .Feedback([Weak](const FString& Message){if(auto W=Weak.Pin())W->Status=Message;})
                        .Drop([Weak](const FAssetData& Asset,const TOptional<FSFXEditScope>&){if(auto W=Weak.Pin())W->Assign(Asset,W->Scope());return true;})]
                    +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,7,0)[SNew(SSFXDropTarget).Replacement(true)
                        .Text_Lambda([Weak]{const auto W=Weak.Pin();const auto* Layer=W?W->Selected():nullptr;return FText::FromString(Layer?TEXT("Заменить: ")+(Layer->Name.IsNone()?Layer->Sound.ToSoftObjectPath().GetAssetName():Layer->Name.ToString()):TEXT("Заменить: выберите слой справа"));})
                        .Destination_Lambda(EventDestination)
                        .TargetKey_Lambda([Weak]{const auto W=Weak.Pin();if(!W)return FString();const auto In=W->Scope();return In.Action.ToString()+TEXT("/")+In.Event.ToString()+TEXT("/")+In.Token.ToString()+TEXT("/")+W->SelectedLayer.ToString();})
                        .Enabled_Lambda([Weak]{const auto W=Weak.Pin();return W&&W->CanEdit()&&W->Selected()!=nullptr;})
                        .CanDrop([Weak](const FAssetData&,const TOptional<FSFXEditScope>& Origin,FString& Error){const auto W=Weak.Pin();if(!W)return false;if(!W->CheckScope(W->Scope(),Error)||(Origin.IsSet()&&!W->CheckScope(Origin.GetValue(),Error)))return false;if(!W->Selected()){Error=TEXT("Выберите конкретный звуковой слой для замены.");return false;}return true;})
                        .Feedback([Weak](const FString& Message){if(auto W=Weak.Pin())W->Status=Message;})
                        .Drop([Weak](const FAssetData& Asset,const TOptional<FSFXEditScope>&){if(auto W=Weak.Pin()){const auto* Layer=W->Selected();if(!Layer)W->Fail(TEXT("Заменяемый слой больше не существует."));else W->Assign(Asset,W->Scope(),Layer->ID);}return true;})]
                    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(8,6))
                        .OnClicked_Lambda([this]{bLibraryFocus=!bLibraryFocus;RebuildBody();return FReply::Handled();})
                        [SNew(STextBlock).Text_Lambda([this]{return bLibraryFocus?LOCTEXT("CollapseLibrary","Вернуть превью"):LOCTEXT("ExpandLibrary","Развернуть библиотеку");}).ColorAndOpacity(TextColor).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11))]]]
                +SVerticalBox::Slot().FillHeight(1)[SAssignNew(Library,SMCSFXLibrary).Context(FMCSFXLibraryContext())]];
    }
    void SSFXComposerWorkspace::RefreshLibrary()
    {
        if(!Library)return;
        const FSFXEditScope In=Scope();const auto Weak=TWeakPtr<SSFXComposerWorkspace>(SharedThis(this));
        FMCSFXLibraryContext Context;Context.EventLabel=FText::FromString(TargetLabel());Context.bCanAdd=CanEdit();
        const auto Capability=Session?Session->GetSFXCapability(SelectedEvent):FMCComposerSFXCapabilities();
        Context.bCanAudition=!bSceneMode&&CanPlay()&&View->IsPreviewAudioEnabled();
        Context.AuditionReason=FText::FromString(!View->IsPreviewAudioEnabled()?TEXT("Включите звук превью."):Capability.UnavailableReason);
        Context.Add=[Weak,In](const FAssetData& Asset){if(auto W=Weak.Pin())W->Assign(Asset,In);};
        Context.AuditionInAction=[Weak,In](const FAssetData& Asset){if(auto W=Weak.Pin())W->Audition(Asset,In);};
        Context.OpenSource=[Weak](const FAssetData& Asset){if(auto W=Weak.Pin())W->OpenSource(Asset);};
        Context.BeforeSourcePlay=[Weak]{if(auto W=Weak.Pin())W->Stop();};
        const bool bScopedSource=Session&&In.Token.IsValid()&&!bSceneMode;
        Context.BeginDrag=[In,bScopedSource](const FAssetData& Asset){
            if(bScopedSource)return FReply::Handled().BeginDragDrop(FSFXSourceDrag::New(Asset,In));
            return FReply::Handled().BeginDragDrop(FAssetDragDropOp::New(Asset));};
        Library->SetContext(MoveTemp(Context));
    }
    void SSFXComposerWorkspace::Refresh(bool bPreview,bool bPreserveActiveEdit)
    {
        if(Session)
        {
            if(SeenPlaybackRevision!=MAX_uint64&&Session->GetPlaybackRevision()!=SeenPlaybackRevision&&View->OwnsPreview())Stop();
            SeenRevision=Session->GetRevision();SeenPlaybackRevision=Session->GetPlaybackRevision();
            const auto* Slot=Session->GetSFXSlot(SelectedEvent);
            if(!Selected()&&Slot&&!Slot->Layers.IsEmpty())SelectedLayer=Slot->Layers[0].ID;
            else if(!Slot||Slot->Layers.IsEmpty())SelectedLayer.Invalidate();
        }
        if(bPreview&&PreviewHost)
        {
            if(Session&&CanPlay())PreviewHost->SetContent(View->MakePreviewWidget());
            else PreviewHost->SetContent(SNew(STextBlock).Text(FText::FromString(Session?Session->GetPreviewUnavailableReason():Status)).AutoWrapText(true).ColorAndOpacity(Muted));
        }
        if(EventHost)
        {
            auto Tabs=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(5,4));
            if(Session)for(FName Event:Session->GetActionStages())Tabs->AddSlot()[SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(10,6))
                .ButtonColorAndOpacity(Event==SelectedEvent?UIColor(TEXT("#28514D")):ButtonFill)
                .OnClicked_Lambda([this,Event]{if(bAudition)Stop();SelectedEvent=Event;SelectedLayer.Invalidate();bSceneMode=false;Refresh();BuildTree();return FReply::Handled();})
                [SNew(STextBlock).Text(EventLabel(Event)).ColorAndOpacity(Event==SelectedEvent?WorkspaceMint:TextColor).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12))]];
            EventHost->SetContent(Tabs);
        }
        RefreshInspector(bPreserveActiveEdit);RefreshLibrary();
    }
    void SSFXComposerWorkspace::Edit(FSFXEditScope In,FGuid ID,TFunction<void(FMCEventSFXLayer&)> Mutation)
    {
        FString Error;if(!CheckScope(In,Error)){Fail(Error);return;}
        const auto* Slot=Session->GetSFXSlot(In.Event);const auto* Current=Slot?Slot->Layers.FindByPredicate([ID](const auto& L){return L.ID==ID;}):nullptr;
        if(!Current){Fail(TEXT("Слой больше не существует. Обновите выделение."));return;}
        auto Layer=*Current;Mutation(Layer);
        if(!Session->SetSFXLayer(In.Token,In.Event,ID,Layer,Error)){Fail(Error);return;}
        Status=TEXT("Общий черновик обновлён. Эти же настройки доступны в VFX Composer.");Refresh();
    }
    bool SSFXComposerWorkspace::CanAppendSoundToEvent(const FAssetData& Asset,FName Action,FName Event,const TOptional<FSFXEditScope>& Origin,FString& Error) const
    {
        Error=SSFXDropTarget::SoundAssetUnavailableReason(Asset);if(!Error.IsEmpty())return false;
        const auto* Entry=Catalogue.FindByPredicate([Action](const auto& E){return E.ID==Action;});
        if(!Entry||Event.IsNone()||!Entry->Stages.Contains(Event))
        {Error=TEXT("Перетащите звук на зарегистрированное событие действия, а не на папку или владельца.");return false;}
        if(!Entry->bEnabled||!Entry->bSupportsProfileOverride)
        {Error=Entry->ProfileOverrideUnsupportedReason.IsEmpty()?TEXT("Аудиослои этого события пока недоступны для редактирования."):Entry->ProfileOverrideUnsupportedReason;return false;}
        if(!ValidateSFXDragOrigin(Origin,Error))return false;
        if(const auto Target=MCComposerDocuments::Find(Action))
        {
            if(!Target->IsContextCurrent()||!Target->CanEditProfile()||!Target->GetActionStages().Contains(Event))
            {Error=TEXT("Целевое событие больше не соответствует открытому документу. Перенос отменён.");return false;}
            const auto* Slot=Target->GetSFXSlot(Event);
            if(Slot&&Slot->Layers.Num()>=32){Error=TEXT("В этом событии уже 32 звуковых слоя.");return false;}
        }
        return true;
    }
    bool SSFXComposerWorkspace::AppendSoundToEvent(const FAssetData& Asset,FName Action,FName Event,FGuid ExpectedToken,const TOptional<FSFXEditScope>& Origin,FString& Error)
    {
        if(!CanAppendSoundToEvent(Asset,Action,Event,Origin,Error))return false;
        if(!ValidateSFXDragOrigin(Origin,Error,true))return false;
        // Opening a document is data-only. Never call View::Select or touch either window's navigation here.
        const auto Target=MCComposerDocuments::Acquire(Action,Error);if(!Target)return false;
        const FGuid Token=ExpectedToken.IsValid()?ExpectedToken:Target->GetSFXDraftToken();
        if(Target->GetActionID()!=Action||Target->GetSFXDraftToken()!=Token||!Target->CanEditProfile()||!Target->GetActionStages().Contains(Event))
        {Error=TEXT("Целевой документ или событие изменились. Начните перенос звука заново.");return false;}
        if(!Target->ValidateSourceCurrent(Error))return false;
        auto* Sound=Cast<USoundBase>(Asset.GetAsset());
        if(!Sound){Error=TEXT("Не удалось открыть переносимый Sound Wave, Sound Cue или MetaSound Source.");return false;}
        // Loading a source may run editor callbacks. Recheck both exact identities before writing.
        if(!ValidateSFXDragOrigin(Origin,Error,true)||!Target->ValidateSourceCurrent(Error))return false;
        if(Target->GetActionID()!=Action||Target->GetSFXDraftToken()!=Token||!Target->GetActionStages().Contains(Event))
        {Error=TEXT("Назначение изменилось при загрузке источника. Звуковой слой не добавлен.");return false;}
        const FGuid Added=Target->AddSFXLayer(Token,Event,Sound,FGuid(),Error);if(!Added.IsValid())return false;
        const auto* Entry=Catalogue.FindByPredicate([Action](const auto& E){return E.ID==Action;});
        FString Destination=Entry?(Entry->OwnerLabel.IsEmpty()?Entry->Label.ToString():Entry->OwnerLabel.ToString()):Action.ToString();
        if(Entry&&Entry->OwnerID!=Entry->ID)Destination+=TEXT(" / ")+(Entry->ActionLabel.IsEmpty()?Entry->Label.ToString():Entry->ActionLabel.ToString());
        Destination+=TEXT(" / ")+Target->GetEventLabel(Event).ToString();
        if(Session==Target)Refresh();
        Status=TEXT("Добавлен звук «")+Asset.AssetName.ToString()+TEXT("» → ")+Destination+TEXT(". Выбранное событие сохранено.");
        return true;
    }
    bool SSFXComposerWorkspace::Assign(const FAssetData& Asset,FSFXEditScope In,FGuid Replace)
    {
        FString Error;if(!CheckScope(In,Error)){Fail(Error);return false;}
        if(!Asset.IsValid()){Fail(TEXT("Выберите звуковой источник в библиотеке."));return false;}
        const auto Unsupported=SMCSFXGallery::GetAssetUnavailableReason(Asset);
        if(!Unsupported.IsEmpty()){Fail(Unsupported.ToString());return false;}
        auto* Sound=Cast<USoundBase>(Asset.GetAsset());if(!Sound){Fail(TEXT("Источник не является Sound Wave, Sound Cue или MetaSound Source."));return false;}
        if(Replace.IsValid())
        {
            const auto* Slot=Session->GetSFXSlot(In.Event);const auto* Existing=Slot?Slot->Layers.FindByPredicate([Replace](const auto& L){return L.ID==Replace;}):nullptr;
            if(!Existing){Fail(TEXT("Заменяемый слой больше не существует."));return false;}
            auto Layer=*Existing;Layer.Sound=Sound;
            if(!Session->SetSFXLayer(In.Token,In.Event,Replace,Layer,Error)){Fail(Error);return false;}
            SelectedLayer=Replace;
        }
        else
        {
            const auto ID=Session->AddSFXLayer(In.Token,In.Event,Sound,FGuid(),Error);
            if(!ID.IsValid()){Fail(Error);return false;}SelectedLayer=ID;
        }
        Status=Replace.IsValid()?TEXT("Источник выбранного слоя заменён в общем черновике."):TEXT("Звуковой слой добавлен в общий черновик события.");Refresh();return true;
    }
    void SSFXComposerWorkspace::AssignSelected(bool bReplace)
    {if(!Library)return;if(bReplace&&!Selected()){Fail(TEXT("Сначала выберите слой для замены источника."));return;}Assign(Library->GetSelectedAsset(),Scope(),bReplace?SelectedLayer:FGuid());}
    void SSFXComposerWorkspace::OpenSource(const FAssetData& Asset)
    {if(GEditor)if(auto* Editor=GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())if(auto* Object=Asset.GetAsset())Editor->OpenEditorForAsset(Object);}

    bool SSFXComposerWorkspace::PrepareScene(FString& Error)
    {
        if(!View)return false;
        if(!bBackground){View->ClearSceneAudio();return true;}
        if(!SceneSession||!SceneSession->GetDraft()||!SceneSession->GetScene(SelectedScene)){View->ClearSceneAudio();return true;}
        return View->SetSceneAudio(SceneSession->GetDraft(),SelectedScene,bMusic?1.f:0.f,bAmbience?1.f:0.f,Error);
    }
    bool SSFXComposerWorkspace::Play(FString& Error)
    {
        if(!CanPlay()){Error=Session?Session->GetPreviewUnavailableReason():TEXT("Выберите действие.");return false;}
        Stop();if(Library)Library->StopSourcePreview();
        if(!PrepareScene(Error)||!View->Play(Scenario,Error))return false;
        bPlaying=true;bAudition=false;Status=TEXT("Реальное действие · общий черновик аудио · игровой профиль не изменён.");return true;
    }
    bool SSFXComposerWorkspace::PlayForReview(EMCPerkPreviewScenario InScenario,bool Loop,FString& Error)
    {Scenario=InScenario;bLoop=Loop;return Play(Error);}
    void SSFXComposerWorkspace::Audition(const FAssetData& Asset,FSFXEditScope In)
    {
        FString Error;if(!CheckScope(In,Error,false)){Fail(Error);return;}
        if(!CanPlay()||!View->IsPreviewAudioEnabled()){Fail(TEXT("Включите звук превью и выберите действие с настоящим контекстным сценарием."));return;}
        const auto Unsupported=SMCSFXGallery::GetAssetUnavailableReason(Asset);if(!Unsupported.IsEmpty()){Fail(Unsupported.ToString());return;}
        auto* Sound=Cast<USoundBase>(Asset.GetAsset());if(!Sound){Fail(TEXT("Звуковой источник недоступен."));return;}
        Stop();if(Library)Library->StopSourcePreview();
        if(!PrepareScene(Error)||!View->AuditionSound(In.Event,Sound,Scenario,Error)){Fail(Error);return;}
        AuditionAsset=Asset;bAudition=true;bPlaying=true;
        if(bLibraryFocus){bLibraryFocus=false;RebuildBody();}
        Status=TEXT("Примерка в действии: временный дополнительный звук. Чтобы сохранить, добавьте слой из библиотеки.");
    }
    void SSFXComposerWorkspace::Apply()
    {
        FString Error;Stop();
        const bool Saved=bSceneMode?SceneSession&&SceneSession->Apply(Error):Session&&Session->ApplyAudio(Error);
        if(!Saved){Fail(Error.IsEmpty()?TEXT("Сохранение аудио недоступно."):Error);return;}
        Status=TEXT("Аудио сохранено в игровом источнике. Визуальные слои не изменены.");Refresh();
    }
    bool SSFXComposerWorkspace::Poll(float)
    {
        if(Session&&Session->GetRevision()!=SeenRevision)
        {
            if(Session->GetPlaybackRevision()!=SeenPlaybackRevision&&View->OwnsPreview())Stop();
            Status=Session->GetStatus();
            Refresh(false,true);
        }
        if(SceneSession&&SeenSceneRevision!=SceneSession->GetRevision())
        {SeenSceneRevision=SceneSession->GetRevision();if(View->OwnsPreview())Stop();if(bSceneMode)RefreshInspector(true);}
        if(bInspectorRefreshPending&&InspectorHost&&!InspectorHost->HasFocusedDescendants())RefreshInspector();
        if(!bPlaying)return true;
        const auto Tab=OwnerTab.Pin();const auto Window=FSlateApplication::Get().FindWidgetWindow(AsShared());
        if(!View->OwnsPreview()||!Tab||!Tab->IsForeground()||!Window||!Window->IsVisible()||Window->IsWindowMinimized()){Stop();return true;}
        if(bPaused)return true;
        const auto Runner=View->GetRunner();if(!Runner){Stop();return true;}
        if(Runner->IsRunning()){RepeatAt=0;return true;}
        const bool Complete=Runner->GetEvents().ContainsByPredicate([](const auto& Event){return Event.Stage==TEXT("ActionFinished");});
        const bool Safety=Runner->GetEvents().ContainsByPredicate([](const auto& Event){return Event.Stage==TEXT("PreviewTailSafetyLimit");});
        if(!Complete||Safety){const FString EndStatus=Runner->GetStatus();Stop();Status=EndStatus;return true;}
        if(!bLoop){bPlaying=false;View->ClearSceneAudio();return true;}
        const double Now=FPlatformTime::Seconds();if(RepeatAt==0)RepeatAt=Now+.6;
        if(Now>=RepeatAt){if(bAudition)Audition(AuditionAsset,Scope());else{FString Error;if(!Play(Error))Fail(Error);}}
        return true;
    }
    FReply SSFXComposerWorkspace::OnKeyDown(const FGeometry&,const FKeyEvent& Event)
    {
        if(Event.GetKey()==EKeys::Escape){Stop();if(Library)Library->StopSourcePreview();return FReply::Handled();}
        if(Event.GetKey()==EKeys::SpaceBar&&!Event.IsControlDown()&&!Event.IsAltDown())
        {if(bPlaying)Stop();else{FString Error;if(!Play(Error))Fail(Error);}return FReply::Handled();}
        return FReply::Unhandled();
    }
    FString SSFXComposerWorkspace::Snapshot() const
    {
        auto Root=MakeShared<FJsonObject>();Root->SetStringField(TEXT("workspace"),TEXT("SFX"));
        Root->SetStringField(TEXT("selected_action"),SelectedAction.ToString());Root->SetStringField(TEXT("selected_event"),SelectedEvent.ToString());
        Root->SetStringField(TEXT("selected_layer"),SelectedLayer.ToString());Root->SetStringField(TEXT("target"),TargetLabel());
        Root->SetBoolField(TEXT("scene_mode"),bSceneMode);Root->SetStringField(TEXT("selected_scene"),SelectedScene.ToString());
        Root->SetBoolField(TEXT("compact"),bCompact);Root->SetBoolField(TEXT("library_focus"),bLibraryFocus);
        Root->SetBoolField(TEXT("playing"),bPlaying);Root->SetBoolField(TEXT("paused"),bPaused);Root->SetBoolField(TEXT("loop"),bLoop);
        Root->SetBoolField(TEXT("owns_preview"),View&&View->OwnsPreview());Root->SetBoolField(TEXT("audio_dirty"),Session&&Session->IsAudioDirty());
        Root->SetStringField(TEXT("status"),Status);Root->SetStringField(TEXT("catalogue_query"),Query);Root->SetNumberField(TEXT("catalogue_count"),Catalogue.Num());
        if(Library){Root->SetNumberField(TEXT("library_total"),Library->GetTotalCount());Root->SetNumberField(TEXT("library_matched"),Library->GetMatchedCount());Root->SetStringField(TEXT("library_query"),Library->GetQuery());Root->SetStringField(TEXT("library_selected"),Library->GetSelectedAssetPath().ToString());Root->SetStringField(TEXT("library_folder"),Library->GetSelectedFolder());Root->SetNumberField(TEXT("source_waveform_peaks"),Library->GetSourceWaveformPeakCount());}
        TSharedPtr<FJsonObject> Document;if(View&&Session&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(View->GetSnapshotJson()),Document)&&Document)Root->SetObjectField(TEXT("document_view"),Document);
        TSharedPtr<FJsonObject> Model;if(Session&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Session->GetSnapshotJson()),Model)&&Model)Root->SetObjectField(TEXT("session"),Model);
        FString Result;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Result));return Result;
    }
}

void MCSFXComposerWorkspace::Register()
{
    if(IsRunningCommandlet()||FGlobalTabmanager::Get()->HasTabSpawner(SFXWorkspaceTab))return;
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(SFXWorkspaceTab,FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
    {
        const auto Panel=SNew(SSFXComposerWorkspace);ActiveSFXWorkspace=Panel;
        auto Tab=SNew(SDockTab).TabRole(ETabRole::NomadTab)
            .OnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda([Weak=TWeakPtr<SSFXComposerWorkspace>(Panel)](TSharedRef<SDockTab>){if(auto P=Weak.Pin())P->Closed();}))[Panel];
        Panel->SetOwnerTab(Tab);return Tab;
    })).SetDisplayName(LOCTEXT("SFXTab","SFX Composer"))
        .SetTooltipText(LOCTEXT("SFXTabHint","Отдельная аудиостудия: реальные события, общие аудиослои, музыка, атмосфера и библиотека."))
        .SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
}
void MCSFXComposerWorkspace::Unregister()
{
    if(auto Tab=FGlobalTabmanager::Get()->FindExistingLiveTab(SFXWorkspaceTab))Tab->RequestCloseTab();
    ActiveSFXWorkspace.Reset();FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(SFXWorkspaceTab);
}
void MCSFXComposerWorkspace::Open()
{
    Register();const auto Tab=FGlobalTabmanager::Get()->TryInvokeTab(SFXWorkspaceTab);
    if(Tab)if(const auto Window=Tab->GetParentWindow())
        if(Window!=FGlobalTabmanager::Get()->GetRootWindow()&&Window->GetType()==EWindowType::Normal)
        {if(!Window->IsWindowMaximized())Window->Maximize();Window->BringToFront(true);}
}
TSharedPtr<FMCVFXActionSession> MCSFXComposerWorkspace::GetActiveSession()
{const auto Panel=ActiveSFXWorkspace.Pin();return Panel?Panel->GetSession():nullptr;}
FString MCSFXComposerWorkspace::GetSnapshotJson()
{const auto Panel=ActiveSFXWorkspace.Pin();return Panel?Panel->Snapshot():TEXT("{\"error\":\"sfx_workspace_closed\"}");}
bool MCSFXComposerWorkspace::SelectForReview(FName Action,FName Event,FGuid Layer,FString& Error)
{const auto Panel=ActiveSFXWorkspace.Pin();if(!Panel){Error=TEXT("Сначала откройте SFX Composer.");return false;}return Panel->SelectForReview(Action,Event,Layer,Error);}
bool MCSFXComposerWorkspace::PlayForReview(EMCPerkPreviewScenario Scenario,bool Loop,FString& Error)
{const auto Panel=ActiveSFXWorkspace.Pin();if(!Panel){Error=TEXT("Сначала откройте SFX Composer.");return false;}return Panel->PlayForReview(Scenario,Loop,Error);}
void MCSFXComposerWorkspace::StopForReview()
{if(const auto Panel=ActiveSFXWorkspace.Pin())Panel->Stop();}
bool MCSFXComposerWorkspace::ResizeForReview(FVector2D Size)
{
    if(!FMath::IsFinite(Size.X)||!FMath::IsFinite(Size.Y)||Size.X<1000||Size.Y<650||Size.X>3840||Size.Y>2160)return false;
    const auto Tab=FGlobalTabmanager::Get()->FindExistingLiveTab(SFXWorkspaceTab);if(!Tab)return false;
    const auto Window=Tab->GetParentWindow();
    if(!Window||Window==FGlobalTabmanager::Get()->GetRootWindow()||Window->GetType()!=EWindowType::Normal)return false;
    Window->Restore();Window->Resize(Size);
    const FSlateRect Area=FSlateApplication::Get().GetPreferredWorkArea();
    Window->MoveWindowTo(FVector2D(Area.Left+FMath::Max(0.,(Area.GetSize().X-Size.X)*.5),Area.Top+FMath::Max(0.,(Area.GetSize().Y-Size.Y)*.5)));
    Window->BringToFront(true);return true;
}

#undef LOCTEXT_NAMESPACE
