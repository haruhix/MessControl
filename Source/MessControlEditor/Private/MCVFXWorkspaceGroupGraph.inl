// Included inside the workspace's private namespace. No editor/runtime model ownership lives here.
DECLARE_DELEGATE_ThreeParams(FDropWorkspacePresentation,FName,FGuid,FAssetData);
DECLARE_DELEGATE_TwoParams(FSelectWorkspaceLayer,FName,FGuid);
DECLARE_DELEGATE_OneParam(FWorkspaceGraphChanged,bool);
DECLARE_DELEGATE_OneParam(FWorkspaceGraphError,FString);

class FWorkspaceEffectDrag final : public FDecoratedDragDropOp
{
public:
    DRAG_DROP_OPERATOR_TYPE(FWorkspaceEffectDrag,FDecoratedDragDropOp)
    FAssetData Asset;FName SourceID;FGuid Token;
    static TSharedRef<FWorkspaceEffectDrag> New(const FAssetData& Asset,FName Source,FGuid Token)
    {auto Op=MakeShared<FWorkspaceEffectDrag>();Op->Asset=Asset;Op->SourceID=Source;Op->Token=Token;Op->SetupDefaults();Op->Construct();Op->Reject();return Op;}
    void Reject(){SetToolTip(FText::FromString(Asset.AssetName.ToString()+TEXT(" · На событие или группу · Esc — отмена")),nullptr);CurrentIconColorAndOpacity=Muted;}
    void Target(const FString& Target,bool bAllowed)
    {SetToolTip(FText::FromString(Asset.AssetName.ToString()+(bAllowed?TEXT(" → Добавить в "):TEXT(" → Контекст недоступен: "))+Target),nullptr);CurrentIconColorAndOpacity=bAllowed?WorkspaceMint:Muted;}
};
class FWorkspaceLayerDrag final : public FDecoratedDragDropOp
{
public:
    DRAG_DROP_OPERATOR_TYPE(FWorkspaceLayerDrag,FDecoratedDragDropOp)
    FName SourceID,Event;FGuid Token,Layer;EMCComposerLayerLane Lane=EMCComposerLayerLane::VFX;
    static TSharedRef<FWorkspaceLayerDrag> New(FName Source,FGuid Token,FName Event,FGuid Layer,EMCComposerLayerLane Lane)
    {auto Op=MakeShared<FWorkspaceLayerDrag>();Op->SourceID=Source;Op->Token=Token;Op->Event=Event;Op->Layer=Layer;Op->Lane=Lane;Op->DefaultHoverText=FText::FromString(TEXT("Переместить слой в группу этого события · Esc — отмена"));Op->ResetToDefaultToolTip();Op->Construct();return Op;}
};
class SWorkspaceLayerButton final : public SButton
{
public:
    SLATE_BEGIN_ARGS(SWorkspaceLayerButton){}SLATE_ARGUMENT(FSoftObjectPath,Source)SLATE_ARGUMENT(FText,Label)
        SLATE_ATTRIBUTE(bool,Selected)SLATE_EVENT(FSimpleDelegate,OnSelected)SLATE_ARGUMENT(TFunction<FReply()>,BeginDrag)SLATE_END_ARGS()
    void Construct(const FArguments& A)
    {
        Source=A._Source;Selected=A._Selected;OnSelected=A._OnSelected;BeginDrag=A._BeginDrag;
        SButton::Construct(SButton::FArguments().ButtonStyle(&SolidStyle()).ContentPadding(FMargin(8,7))
            .ButtonColorAndOpacity_Lambda([this]{return Selected.Get()?WorkspaceMint:ButtonFill;})
            .ForegroundColor_Lambda([this]{return FSlateColor(Selected.Get()?Ink:TextColor);})
            .OnClicked_Lambda([this]{OnSelected.ExecuteIfBound();return FReply::Handled();})
            [SNew(STextBlock).Text(A._Label).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11)).WrapTextAt(140).AutoWrapText(true)]);
        SetToolTipText(FText::FromString(TEXT("Клик — настройки · Двойной клик — исходный ресурс\nПеретаскивание — переместить в группу этого события\n")+Source.ToString()));
    }
    virtual FReply OnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)override
    {FReply R=SButton::OnMouseButtonDown(G,E);if(E.GetEffectingButton()==EKeys::LeftMouseButton&&BeginDrag)R.DetectDrag(SharedThis(this),EKeys::LeftMouseButton);return R;}
    virtual FReply OnDragDetected(const FGeometry&,const FPointerEvent&)override{return BeginDrag?BeginDrag():FReply::Unhandled();}
    virtual FReply OnMouseButtonDoubleClick(const FGeometry& G,const FPointerEvent& E)override
    {
        if(E.GetEffectingButton()!=EKeys::LeftMouseButton)return SButton::OnMouseButtonDoubleClick(G,E);
        OnSelected.ExecuteIfBound();if(auto* Asset=Source.TryLoad();Asset&&GEditor&&!Asset->HasAnyFlags(RF_Transient))
            if(auto* Editors=GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())Editors->OpenEditorForAsset(Asset);
        return FReply::Handled();
    }
private:
    FSoftObjectPath Source;TAttribute<bool> Selected;FSimpleDelegate OnSelected;TFunction<FReply()> BeginDrag;
};
class SWorkspaceCardHandle final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWorkspaceCardHandle){}SLATE_DEFAULT_SLOT(FArguments,Content)SLATE_ARGUMENT(TFunction<FReply()>,Pressed)SLATE_END_ARGS()
    void Construct(const FArguments& A){Pressed=A._Pressed;SetCursor(EMouseCursor::CardinalCross);ChildSlot[A._Content.Widget];}
    virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent& E)override
    {return E.GetEffectingButton()==EKeys::LeftMouseButton&&Pressed?Pressed():FReply::Unhandled();}
private:TFunction<FReply()> Pressed;
};
class SWorkspaceEventDropZone final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWorkspaceEventDropZone){}SLATE_DEFAULT_SLOT(FArguments,Content)SLATE_ARGUMENT(TSharedPtr<FMCVFXActionSession>,Session)
        SLATE_ARGUMENT(FName,Stage)SLATE_ARGUMENT(FGuid,Group)SLATE_ARGUMENT(FString,TargetLabel)SLATE_ATTRIBUTE(bool,Selected)
        SLATE_EVENT(FDropWorkspacePresentation,OnEffectDropped)SLATE_EVENT(FWorkspaceGraphChanged,OnChanged)SLATE_EVENT(FWorkspaceGraphError,OnError)SLATE_END_ARGS()
    void Construct(const FArguments& A)
    {
        Session=A._Session;Stage=A._Stage;Group=A._Group;TargetLabel=A._TargetLabel;Selected=A._Selected;OnEffectDropped=A._OnEffectDropped;OnChanged=A._OnChanged;OnError=A._OnError;
        ChildSlot[SNew(SBorder).Padding(2).BorderImage(&PanelBrush()).BorderBackgroundColor_Lambda([this]{return bHovered?WorkspaceMint:Selected.Get()?WorkspaceMint:BorderFill;})[A._Content.Widget]];
    }
    bool Accepts(FName Source,FGuid Token)const{return Session&&Session->CanEditProfile()&&Session->IsContextCurrent()&&Session->GetActionID()==Source&&Session->GetDraftToken()==Token;}
    virtual FReply OnDragOver(const FGeometry&,const FDragDropEvent& E)override
    {
        if(const auto Op=E.GetOperationAs<FWorkspaceEffectDrag>()){bHovered=Accepts(Op->SourceID,Op->Token);Op->Target(TargetLabel,bHovered);Invalidate(EInvalidateWidgetReason::Paint);return FReply::Handled();}
        if(const auto Op=E.GetOperationAs<FWorkspaceLayerDrag>()){bHovered=Accepts(Op->SourceID,Op->Token)&&Op->Event==Stage;
            Op->SetToolTip(FText::FromString(bHovered?TEXT("Переместить в ")+TargetLabel:TEXT("Перенос слоёв возможен только между группами одного события")),nullptr);Invalidate(EInvalidateWidgetReason::Paint);return FReply::Handled();}
        return FReply::Unhandled();
    }
    virtual void OnDragLeave(const FDragDropEvent& E)override{bHovered=false;if(const auto Op=E.GetOperationAs<FWorkspaceEffectDrag>())Op->Reject();Invalidate(EInvalidateWidgetReason::Paint);}
    virtual FReply OnDrop(const FGeometry&,const FDragDropEvent& E)override
    {
        bHovered=false;
        if(const auto Op=E.GetOperationAs<FWorkspaceEffectDrag>())
        {if(Accepts(Op->SourceID,Op->Token))OnEffectDropped.ExecuteIfBound(Stage,Group,Op->Asset);return FReply::Handled();}
        if(const auto Op=E.GetOperationAs<FWorkspaceLayerDrag>())
        {if(Accepts(Op->SourceID,Op->Token)&&Op->Event==Stage){FString Error;if(Session->AssignLayerToGroup(Op->Token,Stage,Op->Lane,Op->Layer,Group,Error))OnChanged.ExecuteIfBound(true);else OnError.ExecuteIfBound(Error);}return FReply::Handled();}
        return FReply::Unhandled();
    }
private:
    TSharedPtr<FMCVFXActionSession> Session;FName Stage;FGuid Group;FString TargetLabel;TAttribute<bool> Selected;bool bHovered=false;
    FDropWorkspacePresentation OnEffectDropped;FWorkspaceGraphChanged OnChanged;FWorkspaceGraphError OnError;
};
class SActionStageGraph;
class SActionConnections final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SActionConnections):_PinsOnly(false){}SLATE_ARGUMENT(TWeakPtr<SActionStageGraph>,Graph)SLATE_ARGUMENT(bool,PinsOnly)SLATE_END_ARGS()
    void Construct(const FArguments& A){Graph=A._Graph;bPinsOnly=A._PinsOnly;SetVisibility(EVisibility::HitTestInvisible);}
    virtual FVector2D ComputeDesiredSize(float)const override{return GraphExtent;}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry&,const FSlateRect&,FSlateWindowElementList&,int32,const FWidgetStyle&,bool)const override;
    TWeakPtr<SActionStageGraph> Graph;bool bPinsOnly=false;
};
class SActionStageGraph final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SActionStageGraph){}SLATE_ARGUMENT(TSharedPtr<FMCVFXActionSession>,Session)SLATE_ATTRIBUTE(FName,SelectedStage)
        SLATE_ATTRIBUTE(FGuid,SelectedLayer)SLATE_ATTRIBUTE(FGuid,SelectedGroup)SLATE_ATTRIBUTE(FGuid,SelectedSound)SLATE_ATTRIBUTE(bool,ShowTechnicalSources)
        SLATE_EVENT(FSelectWorkspaceLayer,OnSelect)SLATE_EVENT(FSelectWorkspaceLayer,OnSelectSound)SLATE_EVENT(FSelectWorkspaceLayer,OnSelectGroup)
        SLATE_EVENT(FSimpleDelegate,OnHub)SLATE_EVENT(FDropWorkspacePresentation,OnEffectDropped)SLATE_EVENT(FWorkspaceGraphChanged,OnChanged)SLATE_EVENT(FWorkspaceGraphError,OnError)SLATE_END_ARGS()
    struct FCard{FName Event;FGuid Group;FVector2D DefaultPosition,Size;bool bHub=false;};
    void Construct(const FArguments& A)
    {Session=A._Session;SelectedStage=A._SelectedStage;SelectedLayer=A._SelectedLayer;SelectedGroup=A._SelectedGroup;SelectedSound=A._SelectedSound;ShowTechnicalSources=A._ShowTechnicalSources;
        OnSelect=A._OnSelect;OnSelectSound=A._OnSelectSound;OnSelectGroup=A._OnSelectGroup;OnHub=A._OnHub;OnEffectDropped=A._OnEffectDropped;OnChanged=A._OnChanged;OnError=A._OnError;Rebuild();}
    static FString Key(FName E,FGuid G,bool Hub=false){return Hub?TEXT("@hub"):E.ToString()+TEXT("/")+G.ToString();}
    FVector2D Position(const FCard& C)const
    {
        const FString K=Key(C.Event,C.Group,C.bHub);if(bMoving&&K==MovingKey)return MovingPosition;
        FVector2D P;if(C.bHub&&Session->GetActionHubPosition(P))return P;
        if(C.Group.IsValid()){FMCComposerGroupLayout L;if(Session->GetEventGroupLayout(C.Event,C.Group,L))return L.Position;}
        else if(!C.bHub&&Session->GetEventCardPosition(C.Event,P))return P;return C.DefaultPosition;
    }
    TSharedRef<SWidget> Lane(FName Event,FGuid Group,EMCComposerLayerLane Kind)
    {
        auto Rows=SNew(SVerticalBox);Rows->AddSlot().AutoHeight().Padding(6,6)[Label(Kind==EMCComposerLayerLane::VFX?TEXT("VFX · NIAGARA"):TEXT("SFX · AUDIO"),9,Kind==EMCComposerLayerLane::VFX?WorkspaceMint:FLinearColor(.92f,.75f,.41f))];int32 Count=0;
        const FName Source=Session->GetActionID();const FGuid Token=Session->GetDraftToken();
        if(Kind==EMCComposerLayerLane::SFX&&!Group.IsValid()) {
            const auto Capability=Session->GetSFXCapability(Event);
            const auto* Audio=Session->GetSFXSlot(Event);
            const bool bOriginal=!Audio||Audio->Mode==EMCEventSFXMode::UseDefault||Audio->Mode==EMCEventSFXMode::Add;
            for(const auto& Original:Capability.Originals)if(!Original.Sound.IsNull()) {
                ++Count;
                Rows->AddSlot().AutoHeight().Padding(3,2)[SNew(SWorkspaceLayerButton).Source(Original.Sound)
                    .Label(FText::FromString((bOriginal?TEXT("♪ "):TEXT("○ "))+Original.Label.ToString()))
                    .ToolTipText(FText::FromString(TEXT("Двойной клик открывает этот реальный источник. ")+Original.SourceProperty))
                    .OnSelected_Lambda([this,Event]{OnSelectSound.ExecuteIfBound(Event,FGuid());})];
            }
        }
        if(Kind==EMCComposerLayerLane::VFX)
        {
            const auto* Recipe=Session->GetRecipe(Event);if(Recipe)for(int32 I=0;I<Recipe->Layers.Num()&&Recipe->Nodes.IsValidIndex(I);++I)
            {
                const auto& L=Recipe->Layers[I];if(L.GroupID!=Group)continue;const FGuid ID=Recipe->Nodes[I].ID;++Count;
                Rows->AddSlot().AutoHeight().Padding(3,2)[SNew(SWorkspaceLayerButton).Source(L.System.ToSoftObjectPath())
                    .Label(FText::FromString((L.bEnabled?TEXT("● "):TEXT("○ "))+LayerLabel(L)))
                    .Selected_Lambda([this,Event,ID]{return SelectedStage.Get()==Event&&SelectedLayer.Get()==ID;})
                    .OnSelected_Lambda([this,Event,ID]{OnSelect.ExecuteIfBound(Event,ID);})
                    .BeginDrag([Source,Token,Event,ID]{return FReply::Handled().BeginDragDrop(FWorkspaceLayerDrag::New(Source,Token,Event,ID,EMCComposerLayerLane::VFX));})];
            }
        }
        else if(const auto* Slot=Session->GetSFXSlot(Event))for(const auto& L:Slot->Layers)
        {
            if(L.GroupID!=Group)continue;const FGuid ID=L.ID;++Count;const FString Name=L.Name.IsNone()?L.Sound.ToSoftObjectPath().GetAssetName():L.Name.ToString();
            Rows->AddSlot().AutoHeight().Padding(3,2)[SNew(SWorkspaceLayerButton).Source(L.Sound.ToSoftObjectPath())
                .Label(FText::FromString((L.bEnabled?TEXT("♪ "):TEXT("○ "))+Name))
                .Selected_Lambda([this,Event,ID]{return SelectedStage.Get()==Event&&SelectedSound.Get()==ID;})
                .OnSelected_Lambda([this,Event,ID]{OnSelectSound.ExecuteIfBound(Event,ID);})
                .BeginDrag([Source,Token,Event,ID]{return FReply::Handled().BeginDragDrop(FWorkspaceLayerDrag::New(Source,Token,Event,ID,EMCComposerLayerLane::SFX));})];
        }
        if(Count==0)Rows->AddSlot().AutoHeight().Padding(8,8)[Label(Kind==EMCComposerLayerLane::VFX?TEXT("Перетащите Niagara"):TEXT("Перетащите звук"),10,Muted)];
        return SNew(SBorder).BorderImage(&PanelBrush()).BorderBackgroundColor(GraphFill).Padding(3)[Rows];
    }
    void RuntimeChanged(){OnChanged.ExecuteIfBound(true);}
    FReply BeginMove(const FCard& C)
    {const FVector2D Initial=Position(C);if(!C.bHub)OnSelectGroup.ExecuteIfBound(C.Event,C.Group);bMoving=true;bMoved=false;MovingCard=C;MovingKey=Key(C.Event,C.Group,C.bHub);MovingPosition=Initial;MovingToken=Session->GetDraftToken();
        StartPosition=MovingPosition;return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this),EFocusCause::Mouse);}
    void Rebuild()
    {
        Cards.Reset();auto Canvas=SNew(SConstraintCanvas).RenderTransform_Lambda([this]{return FSlateRenderTransform(FScale2D(Zoom),Pan);}).RenderTransformPivot(FVector2D::ZeroVector);
        const auto Stages=Session->GetActionStages();const FGuid Token=Session->GetDraftToken();const bool bTechnical=ShowTechnicalSources.Get(false);int32 GroupNumber=0;
        auto AddCard=[this,&Canvas](const FCard& Card,TSharedRef<SWidget> Contents)
        {Cards.Add(Card);Canvas->AddSlot().Offset_Lambda([this,Card]{const auto P=Position(Card);return FMargin(P.X,P.Y,Card.Size.X,Card.Size.Y);}).Alignment(FVector2D::ZeroVector)[Contents];};
        for(int32 I=0;I<Stages.Num();++I)
        {
            const FName Event=Stages[I];const auto Native=Session->GetNativeSources(Event);FCard Card{Event,FGuid(),FVector2D(I%2?570:0,(I/2)*410),FVector2D(352,150),false};
            auto Body=SNew(SVerticalBox);
            if(bTechnical&&!Native.IsEmpty())
            {
                Body->AddSlot().AutoHeight().Padding(7,4)[Label(TEXT("РОДНАЯ ОСНОВА · закреплена"),9,WorkspaceMint)];
                for(const auto& Part:Native)Body->AddSlot().AutoHeight().Padding(3,1)[SNew(SWorkspaceNativeSourceButton).Source(Part).OnSelected_Lambda([this,Event,Token]{OnSelect.ExecuteIfBound(Event,FGuid());})];
            }
            else if(HasActiveNativeDecorativeBasis(*Session,Event,Native))Body->AddSlot().AutoHeight().Padding(7,3)
                [SNew(STextBlock).Text(FText::FromString(TEXT("Родные акценты · ⓘ"))).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9)).ColorAndOpacity(Muted)
                    .ToolTipText(FText::FromString(TEXT("Для события включено родное декоративное оформление вне редактируемых Niagara-слоёв. Его ресурсы и режим доступны через «Техническая основа».")))];
            Body->AddSlot().AutoHeight().Padding(3,5)[Label(TEXT("Без группы"),10,Muted)];
            Body->AddSlot().AutoHeight()[SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1).Padding(0,0,3,0)[Lane(Event,FGuid(),EMCComposerLayerLane::VFX)]
                +SHorizontalBox::Slot().FillWidth(1)[Lane(Event,FGuid(),EMCComposerLayerLane::SFX)]];
            auto Header=SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1)[SNew(SWorkspaceCardHandle).Pressed([this,Event]{
                        const auto* Current=Cards.FindByPredicate([Event](const FCard& C){return !C.bHub&&C.Event==Event&&!C.Group.IsValid();});
                        if(!Current)return FReply::Unhandled();
                        const FCard DragCard=*Current;return BeginMove(DragCard);})
                    [SNew(SBorder).BorderImage(&PanelBrush()).BorderBackgroundColor(HeadingFill).Padding(10,10)
                        [SNew(STextBlock).Text(Session->GetEventLabel(Event)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14))
                            .ColorAndOpacity(WorkspaceStageColor(Event)).WrapTextAt(220).AutoWrapText(true)]]]
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SmallCommand(TEXT("＋ Группа"),[this,Event,Token]{FString Error;const FGuid ID=Session->CreateEventGroup(Token,Event,TEXT("Новая группа"),Error);if(ID.IsValid()){OnSelectGroup.ExecuteIfBound(Event,ID);RuntimeChanged();FrameSelected();}else OnError.ExecuteIfBound(Error);})];
            // Measure the real wrapped contents once per rebuild. Large lists
            // keep the existing bounded scroll area; empty/single-layer events
            // no longer reserve a full 350-unit card. No saved layout is changed.
            const float LayoutScale=GetPrepassLayoutScaleMultiplier();
            Header->SlatePrepass(LayoutScale);Body->SlatePrepass(LayoutScale);
            Card.Size.Y=FMath::Clamp(Header->GetDesiredSize().Y+Body->GetDesiredSize().Y+8.f,150.f,350.f);
            AddCard(Card,SNew(SWorkspaceEventDropZone).Session(Session).Stage(Event).Group(FGuid()).TargetLabel(Session->GetEventLabel(Event).ToString()+TEXT(" / Без группы"))
                .Selected_Lambda([this,Event,Token]{return SelectedStage.Get()==Event&&!SelectedGroup.Get().IsValid();}).OnEffectDropped(OnEffectDropped).OnChanged(OnChanged).OnError(OnError)
                [SNew(SBorder).Padding(2).BorderImage(&PanelBrush()).BorderBackgroundColor(Surface)[SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Header]
                    +SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[Body]]]]);
            if(const auto* Set=Session->GetEventGroups(Event))for(const auto& G:Set->Groups)
            {
                const FGuid ID=G.ID;FCard GroupCard{Event,ID,FVector2D(1000+(GroupNumber%2)*390,(GroupNumber/2)*310),FVector2D(366,270),false};++GroupNumber;
                FMCComposerGroupLayout Layout;if(Session->GetEventGroupLayout(Event,ID,Layout))GroupCard.Size=Layout.Size.ComponentMax(FVector2D(320,220));
                auto Controls=SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth()[SNew(SCheckBox).ToolTipText(FText::FromString(TEXT("Группа включена: VFX и SFX"))).IsChecked(G.bEnabled?ECheckBoxState::Checked:ECheckBoxState::Unchecked)
                        .OnCheckStateChanged_Lambda([this,Event,ID,Token](ECheckBoxState V){FString E;if(Session->SetEventGroupEnabled(Token,Event,ID,V==ECheckBoxState::Checked,E))RuntimeChanged();else OnError.ExecuteIfBound(E);})]
                    +SHorizontalBox::Slot().FillWidth(1).Padding(5,0)[SNew(SEditableTextBox).Text(FText::FromName(G.Name)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),11))
                        .OnTextCommitted_Lambda([this,Event,ID,Token](const FText& T,ETextCommit::Type C){
                            if(C==ETextCommit::OnCleared)return;const FName Name(*T.ToString().TrimStartAndEnd());
                            const auto* Groups=Session->GetEventGroups(Event);const auto* Current=Groups?Groups->Find(ID):nullptr;
                            if(!Current||Current->Name==Name)return;
                            FString E;if(Session->RenameEventGroup(Token,Event,ID,Name,E))RuntimeChanged();else OnError.ExecuteIfBound(E);})]
                    +SHorizontalBox::Slot().AutoWidth()[SmallCommand(G.bMuteSFX?TEXT("♪ ВЫКЛ"):TEXT("♪"),[this,Event,ID,Token,MutedAudio=G.bMuteSFX]{FString E;if(Session->SetEventGroupAudioMuted(Token,Event,ID,!MutedAudio,E))RuntimeChanged();else OnError.ExecuteIfBound(E);})];
                auto Footer=SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth()[SmallCommand(Session->GetPreviewSoloGroups(Event).Contains(ID)?TEXT("Solo ●"):TEXT("Solo"),[this,Event,ID,Token]{FString E;TArray<FGuid> Solo=Session->GetPreviewSoloGroups(Event);if(Solo.Contains(ID))Solo.Remove(ID);else Solo.Add(ID);if(Session->SetPreviewSoloGroups(Token,Event,Solo,E))OnChanged.ExecuteIfBound(true);else OnError.ExecuteIfBound(E);})]
                    +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[SmallCommand(TEXT("Дублировать"),[this,Event,ID,Token]{FString E;const auto New=Session->DuplicateEventGroup(Token,Event,ID,E);if(New.IsValid()){OnSelectGroup.ExecuteIfBound(Event,New);RuntimeChanged();FrameSelected();}else OnError.ExecuteIfBound(E);})]
                    +SHorizontalBox::Slot().AutoWidth()[SmallCommand(TEXT("Разгруппировать"),[this,Event,ID,Token]{FString E;if(Session->RemoveEventGroup(Token,Event,ID,EMCComposerGroupRemoval::UngroupChildren,E)){OnSelectGroup.ExecuteIfBound(Event,FGuid());RuntimeChanged();}else OnError.ExecuteIfBound(E);})];
                AddCard(GroupCard,SNew(SWorkspaceEventDropZone).Session(Session).Stage(Event).Group(ID).TargetLabel(G.Name.ToString()).Selected_Lambda([this,Event,ID,Token]{return SelectedStage.Get()==Event&&SelectedGroup.Get()==ID;})
                    .OnEffectDropped(OnEffectDropped).OnChanged(OnChanged).OnError(OnError)[SNew(SBorder).Padding(6).BorderImage(&PanelBrush()).BorderBackgroundColor(G.bEnabled?PanelFill:AppFill)
                        [SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight()[SNew(SWorkspaceCardHandle).Pressed([this,GroupCard]{return BeginMove(GroupCard);})[SNew(SBorder).Padding(5,5).BorderImage(&PanelBrush()).BorderBackgroundColor(HeadingFill)[Label(TEXT("⠿  ")+Session->GetEventLabel(Event).ToString()+TEXT(" / ГРУППА"),9,WorkspaceStageColor(Event))]]]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,5)[Controls]
                        +SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1).Padding(0,0,3,0)[Lane(Event,ID,EMCComposerLayerLane::VFX)]
                            +SHorizontalBox::Slot().FillWidth(1)[Lane(Event,ID,EMCComposerLayerLane::SFX)]]]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,5,0,0)[Footer]]]);
            }
        }
        FCard Hub{NAME_None,FGuid(),FVector2D(398,145),HubSize,true};
        static const FButtonStyle HubStyle=FButtonStyle().SetNormal(FSlateRoundedBoxBrush(UIColor(TEXT("#173337")),68.f,WorkspaceMint,2.f)).SetHovered(FSlateRoundedBoxBrush(UIColor(TEXT("#20464A")),68.f,WorkspaceMint,3.f));
        AddCard(Hub,SNew(SWorkspaceCardHandle).Pressed([this,Hub]{OnHub.ExecuteIfBound();return BeginMove(Hub);})
            [SNew(SBorder).BorderImage(&HubStyle.Normal).Padding(17).HAlign(HAlign_Center).VAlign(VAlign_Center)[SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Session->GetActionLabel()).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),12)).WrapTextAt(100).Justification(ETextJustify::Center)]
                +SVerticalBox::Slot().AutoHeight().Padding(0,6)[SNew(STextBlock).Text(FText::FromString(TEXT("ДЕЙСТВИЕ"))).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9)).ColorAndOpacity(WorkspaceMint).Justification(ETextJustify::Center)]]]);
        ChildSlot[SNew(SBorder).Padding(0).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(GraphFill).Clipping(EWidgetClipping::ClipToBounds)
            [SNew(SOverlay)+SOverlay::Slot()[SNew(SActionConnections).Graph(SharedThis(this))]+SOverlay::Slot()[Canvas]+SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(8)
                [Label(TEXT("Перетащите заголовок · ПКМ — холст · Колесо — масштаб · Esc — отмена"),9,Muted)]]];
    }
    /** Fit once for a newly selected action; ordinary refreshes keep manual pan/zoom. */
    void FrameActionOverview()
    {if(OverviewAction!=Session->GetActionID()){OverviewAction=Session->GetActionID();Frame();}}
    void Frame(){bFocusSelection=false;bTrackSelectedFrame=false;bFit=true;LastSize=FVector2D::ZeroVector;}
    void FrameSelected(){bFit=false;bFocusSelection=true;bTrackSelectedFrame=true;Invalidate(EInvalidateWidgetReason::Layout);}
    virtual bool SupportsKeyboardFocus()const override{return true;}
    virtual void Tick(const FGeometry& G,double T,float Dt)override
    {
        SCompoundWidget::Tick(G,T,Dt);const FVector2D S=G.GetLocalSize();if(S.X<2||S.Y<2||bMoving||bPanning)return;
        if(bTrackSelectedFrame&&!S.Equals(LastSize,1.f))bFocusSelection=true;
        if(bFocusSelection)
        {
            const auto* Card=Cards.FindByPredicate([this](const FCard& C){return !C.bHub&&C.Event==SelectedStage.Get()&&C.Group==SelectedGroup.Get();});
            if(!Card)Card=Cards.FindByPredicate([this](const FCard& C){return !C.bHub&&C.Event==SelectedStage.Get()&&!C.Group.IsValid();});
            if(Card){Zoom=float(FMath::Clamp(FMath::Min((S.X-50)/Card->Size.X,(S.Y-70)/Card->Size.Y),.4,1.4));
                Pan=(S-Card->Size*Zoom)*.5-Position(*Card)*Zoom;bFocusSelection=false;LastSize=S;Invalidate(EInvalidateWidgetReason::Layout);return;}
        }
        if(!bFit||S.Equals(LastSize,1.f))return;FBox2D Bounds(ForceInit);
        for(const auto& C:Cards){Bounds+=Position(C);Bounds+=Position(C)+C.Size;}const auto Extent=Bounds.GetSize()+FVector2D(40,40);
        Zoom=float(FMath::Clamp(FMath::Min((S.X-20)/Extent.X,(S.Y-20)/Extent.Y),.12,1.25));Pan=(S-Extent*Zoom)*.5-(Bounds.Min-FVector2D(20,20))*Zoom;LastSize=S;
    }
    virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent& E)override
    {if(E.GetEffectingButton()==EKeys::RightMouseButton||E.GetEffectingButton()==EKeys::MiddleMouseButton){bPanning=true;bFit=false;bFocusSelection=false;bTrackSelectedFrame=false;return FReply::Handled().CaptureMouse(SharedThis(this));}return FReply::Unhandled();}
    virtual FReply OnMouseMove(const FGeometry&,const FPointerEvent& E)override
    {if(bMoving){MovingPosition+=E.GetCursorDelta()/Zoom;bMoved|=!MovingPosition.Equals(StartPosition,2);bFit=false;bFocusSelection=false;bTrackSelectedFrame=false;Invalidate(EInvalidateWidgetReason::Layout);return FReply::Handled();}
        if(bPanning){Pan+=E.GetCursorDelta();Invalidate(EInvalidateWidgetReason::Layout);return FReply::Handled();}return FReply::Unhandled();}
    virtual FReply OnMouseButtonUp(const FGeometry&,const FPointerEvent&)override
    {
        if(bMoving){const auto C=MovingCard;const auto P=MovingPosition;const bool Commit=bMoved;bMoving=false;
            if(Commit){FString E;bool OK=false;if(C.bHub)OK=Session->SetActionHubPosition(MovingToken,P,E);
                else if(C.Group.IsValid()){FMCComposerGroupLayout Layout;Session->GetEventGroupLayout(C.Event,C.Group,Layout);Layout.GroupID=C.Group;Layout.Position=P;Layout.Size=C.Size;OK=Session->SetEventGroupLayout(MovingToken,C.Event,C.Group,Layout,E);}
                else OK=Session->SetEventCardPosition(MovingToken,C.Event,P,E);if(OK)OnChanged.ExecuteIfBound(false);else OnError.ExecuteIfBound(E);}
            return FReply::Handled().ReleaseMouseCapture();}
        if(bPanning){bPanning=false;return FReply::Handled().ReleaseMouseCapture();}return FReply::Unhandled();
    }
    virtual FReply OnKeyDown(const FGeometry&,const FKeyEvent& E)override
    {if(E.GetKey()==EKeys::Escape&&(bMoving||bPanning)){bMoving=false;bPanning=false;Invalidate(EInvalidateWidgetReason::Layout);return FReply::Handled().ReleaseMouseCapture();}return FReply::Unhandled();}
    virtual void OnMouseCaptureLost(const FCaptureLostEvent&)override{bMoving=false;bPanning=false;Invalidate(EInvalidateWidgetReason::Layout);}
    virtual FReply OnMouseWheel(const FGeometry& G,const FPointerEvent& E)override
    {const auto P=G.AbsoluteToLocal(E.GetScreenSpacePosition());const float Z=FMath::Clamp(Zoom*FMath::Pow(1.12f,E.GetWheelDelta()),.12f,1.8f);Pan=P-(P-Pan)*(Z/Zoom);Zoom=Z;bFit=false;bFocusSelection=false;bTrackSelectedFrame=false;Invalidate(EInvalidateWidgetReason::Layout);return FReply::Handled();}
    TSharedPtr<FMCVFXActionSession> Session;TArray<FCard> Cards;FVector2D Pan=FVector2D::ZeroVector;float Zoom=1;
private:
    TAttribute<FName> SelectedStage;TAttribute<FGuid> SelectedLayer,SelectedGroup,SelectedSound;TAttribute<bool> ShowTechnicalSources;
    FSelectWorkspaceLayer OnSelect,OnSelectSound,OnSelectGroup;FSimpleDelegate OnHub;FDropWorkspacePresentation OnEffectDropped;FWorkspaceGraphChanged OnChanged;FWorkspaceGraphError OnError;
    FName OverviewAction;
    bool bPanning=false,bFit=true,bFocusSelection=false,bTrackSelectedFrame=false,bMoving=false,bMoved=false;FVector2D LastSize=FVector2D::ZeroVector,MovingPosition,StartPosition;FString MovingKey;FCard MovingCard;FGuid MovingToken;
};
int32 SActionConnections::OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& O,int32 L,const FWidgetStyle&,bool)const
{
    const auto P=Graph.Pin();if(!P)return L;const auto Size=G.GetLocalSize();const float Step=FMath::Max(8.f,32.f*P->Zoom);const auto Grid=UIColor(TEXT("#23303C")).CopyWithNewOpacity(.55f);
    for(float X=FMath::Fmod(P->Pan.X,Step);X<Size.X;X+=Step)FSlateDrawElement::MakeLines(O,L,G.ToPaintGeometry(),{FVector2D(X,0),FVector2D(X,Size.Y)},ESlateDrawEffect::None,Grid,false,1.f);
    for(float Y=FMath::Fmod(P->Pan.Y,Step);Y<Size.Y;Y+=Step)FSlateDrawElement::MakeLines(O,L,G.ToPaintGeometry(),{FVector2D(0,Y),FVector2D(Size.X,Y)},ESlateDrawEffect::None,Grid,false,1.f);
    const auto* Hub=P->Cards.FindByPredicate([](const auto& C){return C.bHub;});if(!Hub)return L;const auto HubCenter=P->Position(*Hub)+Hub->Size*.5;
    for(const auto& C:P->Cards)
    {
        if(C.bHub)continue;FVector2D Origin=HubCenter;if(C.Group.IsValid())if(const auto* E=P->Cards.FindByPredicate([&](const auto& E){return E.Event==C.Event&&!E.Group.IsValid()&&!E.bHub;}))Origin=P->Position(*E)+E->Size*.5;
        const auto End=P->Position(C)+C.Size*.5;const auto Dir=(End-Origin).GetSafeNormal();const auto A=P->Pan+(Origin+Dir*(C.Group.IsValid()?130:68))*P->Zoom;
        const double Edge=FMath::Min((C.Size.X*.5)/FMath::Max(.0001,FMath::Abs(Dir.X)),(C.Size.Y*.5)/FMath::Max(.0001,FMath::Abs(Dir.Y)));
        const auto B=P->Pan+(End-Dir*Edge)*P->Zoom;FSlateDrawElement::MakeDrawSpaceSpline(O,L+1,G.LocalToAbsolute(A),Dir*70*P->Zoom,G.LocalToAbsolute(B),Dir*70*P->Zoom,2.f,ESlateDrawEffect::None,WorkspaceStageColor(C.Event));
    }
    return L+1;
}
