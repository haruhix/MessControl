#include "MCVFXActionWorkspace.h"

#include "MCVFXActionSession.h"
#include "MCComposerDocumentView.h"
#include "MCComposerDocuments.h"
#include "MCComposerReview.h"
#include "MCComposerSourceDrag.h"
#include "MCComposerStyle.h"
#include "SMCComposerWorkspaceShell.h"
#include "MCVFXSourceContracts.h"
#include "MCVFXCatalogueFilter.h"
#include "MCVFXNativePresentationSettings.h"
#include "SMCNativeArtPartsPanel.h"
#include "SMCEventSFXPanel.h"
#include "SMCSFXGallery.h"
#include "SMCSFXLibrary.h"
#include "MCComposerSceneAudioSession.h"
#include "SMCComposerSceneAudioPanel.h"
#include "MCDataAssets.h"
#include "Sound/SoundBase.h"
#include "MCPerkActionPreviewPanel.h"
#include "MCVFXRecipePreset.h"
#include "MCVFXActionPack.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetThumbnail.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Containers/Ticker.h"
#include "ContentBrowserModule.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/MessageDialog.h"
#include "Misc/Crc.h"
#include "Misc/ObjectThumbnail.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "SourceCodeNavigation.h"
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
#include "Widgets/Input/SSearchBox.h"
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
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STileView.h"
#include "Widgets/Views/STreeView.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "MCVFXActionWorkspace"

namespace
{
    const FName WorkspaceTab(TEXT("MCVFXActionWorkspace"));
    using namespace MCComposerStyle;
    using FGalleryItem=TSharedPtr<FAssetData>;
    using FCatalogItem=TSharedPtr<FMCVFXActionCatalogueEntry>;
    const FVector2D HubPosition(234,210),HubSize(136,136),GraphExtent(588,548);
    int32 AddedSoundCount(const FMCVFXActionSession& Session,FName Stage)
    {const auto* Slot=Session.GetSFXSlot(Stage);return Slot?Slot->Layers.Num():0;}
    FVector2D StageDimensions(const UMCVFXRecipePreset* Recipe,int32 NativeCount=0,int32 SoundCount=0)
    {
        const int32 Count=Recipe?Recipe->Layers.Num():0;
        const int32 SoundHeight=SoundCount>0?28:0;
        if(NativeCount>0)return FVector2D(216,FMath::Min(380,104+NativeCount*34+FMath::Min(Count,3)*32+SoundHeight));
        return FVector2D(216,(Count==0?132:FMath::Min(192,68+Count*32))+SoundHeight);
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
    FLinearColor WorkspaceStageColor(FName Stage)
    {
        if(Stage==TEXT("Start"))return FLinearColor(.83f,.56f,.25f);
        if(Stage==TEXT("Loop"))return FLinearColor(.28f,.71f,.73f);
        if(Stage==TEXT("Impact"))return FLinearColor(.90f,.43f,.39f);
        return FLinearColor(.49f,.57f,.86f);
    }
    FString NativeModeLabel(EMCPerkVFXSlotMode Mode,bool bKeepNative=false)
    {
        if(Mode==EMCPerkVFXSlotMode::UseDefault)return TEXT("Родная основа включена");
        if(Mode==EMCPerkVFXSlotMode::Disabled)return TEXT("Родные акценты отключены");
        return bKeepNative?TEXT("Родная основа + Niagara"):TEXT("Замена родной основы");
    }
    bool NativeKeepsOriginal(const FMCVFXActionSession& Session,FName Stage)
    {
        const auto* Draft=Session.GetNativeDraft();if(!Draft)return false;
        const auto* Event=Draft->Events.Find(Stage);return Event&&Event->bKeepNativeAccentsWithComposition;
    }
    bool HasActiveNativeDecorativeBasis(const FMCVFXActionSession& Session,FName Event,const TArray<FMCVFXNativeSourcePart>& Sources)
    {
        if(!Sources.ContainsByPredicate([](const FMCVFXNativeSourcePart& Part){return Part.Kind!=EMCVFXNativeSourceKind::Niagara;}))return false;
        // Source evidence remains inspectable after replacement. Match the
        // native accent policy; a protected gameplay body is not legacy art.
        const auto Mode=Session.GetPreviewSlotMode(Event);
        if(Mode==EMCPerkVFXSlotMode::Disabled || (Mode!=EMCPerkVFXSlotMode::UseDefault&&!NativeKeepsOriginal(Session,Event)))return false;
        const auto* Draft=Session.GetNativeDraft();
        if(!Draft || !Draft->Events.Contains(Event))return true;
        for(const auto& Descriptor:UMCVFXNativePresentationSettings::DescribeParts(Session.GetActionID(),Event))
            if(!Descriptor.bProtectedGeometry)if(const auto* Part=Draft->FindPart(Event,Descriptor.PartID);Part&&Part->bEnabled)return true;
        return false;
    }
    void OpenNativeSource(const FMCVFXNativeSourcePart& Part)
    {
        if(!Part.Asset.IsNull()) {
            if(auto* Asset=Part.Asset.TryLoad();Asset && !Asset->HasAnyFlags(RF_Transient))
                if(GEditor)if(auto* Editors=GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())Editors->OpenEditorForAsset(Asset);
            return;
        }
        if(!Part.SourceFile.IsEmpty()) {
            const FString File=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(),Part.SourceFile);
            if(FPaths::IsUnderDirectory(File,FPaths::ConvertRelativePathToFull(FPaths::ProjectDir())) && IFileManager::Get().FileExists(*File))
                FSourceCodeNavigation::OpenSourceFile(File);
        }
    }
    class SWorkspaceNativeSourceButton final:public SButton
    {
    public:
        SLATE_BEGIN_ARGS(SWorkspaceNativeSourceButton){}SLATE_ARGUMENT(FMCVFXNativeSourcePart,Source)SLATE_EVENT(FSimpleDelegate,OnSelected)SLATE_END_ARGS()
        void Construct(const FArguments& A)
        {
            Source=A._Source;OnSelected=A._OnSelected;
            const FString Resource=Source.Asset.IsNull()?TEXT("Процедурная геометрия"):Source.Asset.GetAssetName();
            SButton::Construct(SButton::FArguments().ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(7,4))
                .OnClicked_Lambda([this]{OnSelected.ExecuteIfBound();return FReply::Handled();})
                [SNew(SVerticalBox)
                    +SVerticalBox::Slot().AutoHeight()[Label(MCVFXNativeSources::KindLabel(Source.Kind).ToString()+TEXT(" · ")+Source.Label.ToString(),10,WorkspaceMint)]
                    +SVerticalBox::Slot().AutoHeight()[Label(Resource,9,Muted)]]);
            SetToolTipText(FText::FromString(TEXT("Реальный источник родного эффекта. Двойной клик — открыть.\n")+(Source.Asset.IsNull()?Source.SourceFile:Source.Asset.ToString())
                +TEXT("\nЭтот источник общий: правка ассета меняет его во всех местах использования.")));
        }
        virtual FReply OnMouseButtonDoubleClick(const FGeometry& G,const FPointerEvent& E)override
        {
            if(E.GetEffectingButton()!=EKeys::LeftMouseButton)return SButton::OnMouseButtonDoubleClick(G,E);
            OnSelected.ExecuteIfBound();OpenNativeSource(Source);return FReply::Handled();
        }
    private:FMCVFXNativeSourcePart Source;FSimpleDelegate OnSelected;
    };
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
    FString WorkspaceEffectLabel(const FString& Name)
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
        if(Layer.Name.IsNone()||Name==TEXT("Layer")||Name==System||Name==System.Replace(TEXT("NS_Template_"),TEXT("")))return WorkspaceEffectLabel(System);
        return WorkspaceEffectLabel(Name);
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
        if(Name.StartsWith(TEXT("NS_Template_")))return {WorkspaceEffectLabel(Name).Left(3).ToUpper(),WorkspaceMint};
        if(Name.StartsWith(TEXT("NS_Practice_")))return {TEXT("УЧ"),FLinearColor(.48f,.57f,.63f)};
        return {TEXT("VFX"),Muted};
    }
    struct FGalleryThumbnailEvidence{bool bHasSavedImage=false;uint32 Hash=0;};
    #include "MCVFXWorkspaceGroupGraph.inl"

    enum class EVFXCatalogueNode : uint8 { Section, Owner, Action, Event };
    struct FVFXCatalogueNode
    {
        FString Key;
        FText Label;
        EVFXCatalogueNode Kind=EVFXCatalogueNode::Section;
        FCatalogItem Entry;
        FName Event;
        TWeakPtr<FVFXCatalogueNode> Parent;
        TArray<TSharedPtr<FVFXCatalogueNode>> Children;
    };
    using FCatalogueTreeItem=TSharedPtr<FVFXCatalogueNode>;
    struct FVFXCatalogueDropState
    {
        TWeakPtr<FDragDropOperation> Operation;
        bool bTargetWasOpen=false;
        FGuid TargetToken;
    };

    FText CatalogueEventLabel(FName Action,FName Event)
    {
        if(const auto* Adapter=MCPerkPreviewAdapters::Find(Action))
            if(const auto* Label=Adapter->StageLabels.Find(Event))return *Label;
        return FText::FromName(Event);
    }

    class SActionWorkspace final : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SActionWorkspace){}SLATE_END_ARGS()
        void Construct(const FArguments&);
        virtual ~SActionWorkspace()override;
        void SetOwnerTab(const TSharedRef<SDockTab>& Tab){OwnerTab=Tab;}
        bool CanClose();
        bool ConfirmDraftTransition(const FString& Reason);
        void Closed();
        FString Snapshot()const;
        TSharedPtr<FMCVFXActionSession> GetSession()const{return Session;}
        bool ImportSavedEvent(FName ActionID,FName Stage,UMCVFXComposition* Source,FString& Error);
        bool SelectForReview(FName ActionID,FString& Error);
        bool PlayForReview(EMCPerkPreviewScenario Scenario,bool Loop,FString& Error)
        {
            if(!CanPlay() || !Session->GetScenarios().Contains(Scenario)) {Error=TEXT("Выбранный сценарий недоступен для этого действия.");return false;}
            Stop();SelectedScenario=Scenario;bLoop=Loop;Play(false);
            if(!bPlaying){Error=Status;return false;}return true;
        }
        void StopForReview(){Stop();}
        void SetCameraForReview(int32 Preset){DocumentView->SetPreviewView(Preset);}
        void SelectAudioForReview(FName Event,FGuid Layer){SelectSound(Event,Layer);if(Graph)Graph->FrameSelected();}
    private:
        TSharedPtr<SMCComposerWorkspaceShell> WorkspaceShell;
        TSharedPtr<FMCComposerDocumentView> DocumentView;
        TSharedPtr<FMCVFXActionSession> Session;
        TSharedPtr<SWidget> NativePreview;
        TSharedPtr<SBox> PreviewHost,GraphHost;
        TSharedPtr<SBox> SFXGalleryHost;
        TSharedPtr<SMCSFXLibrary> SFXGallery;
        bool bShowSFX=false;
        // Both audio views retain the existing action draft; scene audio has one
        // shared canonical-palette draft, never a second event/profile copy.
        TSharedPtr<FMCComposerSceneAudioSession> SceneAudioSession;
        bool bShowSceneAudio=false,bUseSceneAudio=true,bFollowActionScene=true;
        bool bMusicAudible=true,bAmbienceAudible=true;
        FName SelectedAudioScene=TEXT("Gameplay");
        uint64 SeenSceneAudioRevision=MAX_uint64;
        bool bShowTechnicalSources=false;
        TMap<FName,FGuid> SelectedSFXLayers;
        bool bSFXAudition=false;
        FAssetData SFXAuditionAsset;
        TSharedPtr<SActionStageGraph> Graph;
        TSharedPtr<SVerticalBox> Details;
        TSharedPtr<FAssetThumbnailPool> GalleryPool;
        TSharedPtr<STileView<FGalleryItem>> GalleryView;
        TSharedPtr<SEditableTextBox> GallerySearchBox;
        TArray<FAssetData> GalleryCatalog;
        TArray<FGalleryItem> GalleryItems;
        TArray<FCatalogItem> Catalogue;
        TArray<FCatalogItem> FilteredCatalogue;
        TArray<FCatalogueTreeItem> CatalogueRoots;
        TSharedPtr<STreeView<FCatalogueTreeItem>> CatalogueView;
        TSet<FString> ExpandedCatalogueNodes,KnownCatalogueNodes;
        FString CatalogueDropTarget;
        TSharedPtr<SSearchBox> CatalogueSearchBox;
        FMCVFXCatalogueFilter CatalogueFilter;
        bool bRefreshingCatalogueFilter=false;
        TWeakPtr<SDockTab> OwnerTab;
        FTSTicker::FDelegateHandle TickHandle;
        FAssetData GallerySelection;
        FName SelectedAction=TEXT("Consumable_Rocket"),SelectedStage=TEXT("Start");
        EMCPerkPreviewScenario SelectedScenario=EMCPerkPreviewScenario::Target;
        FGuid SelectedLayer,SelectedGroup;
        FString GalleryQuery,Status;
        TMap<FName,FGalleryThumbnailEvidence> GalleryThumbnailEvidence;
        TMap<uint32,TSet<FName>> GalleryThumbnailSources;
        uint64 SeenRevision=MAX_uint64;
        uint64 SeenPlaybackRevision=MAX_uint64;
        bool bUnsupported=false,bShowActionDetails=false,bLoop=true,bLive=true,bPlaying=false,bObserved=false,bPaused=false,bAudition=false,bPendingGalleryAudition=false;
        double RepeatAt=0;
        bool TickWorkspace(float);
        void SelectAction(FCatalogItem Item);
        void SelectLayer(FName Stage,FGuid ID);
        void SelectEventScenario();
        void SelectGallery(FGalleryItem Item);
        void Refresh();
        void RefreshDetails();
        void RefreshGallery(const TCHAR* Path=nullptr);
        void RefreshCatalogueFilter();
        void BuildCatalogueTree();
        void SelectCatalogueNode(FCatalogueTreeItem Item,ESelectInfo::Type Origin);
        void SyncCatalogueSelection(bool bReveal=false);
        TSharedRef<ITableRow> CatalogueRow(FCatalogueTreeItem Item,const TSharedRef<STableViewBase>& Owner);
        TSharedRef<SWidget> VFXReviewCheckbox(FName Action,FName Event,bool bCompact);
        bool ReadCatalogueDrop(const FDragDropEvent& Drag,FAssetData& Asset,FString& Reason)const;
        bool DropOnCatalogueEvent(FCatalogueTreeItem Item,const FDragDropEvent& Drag,const FVFXCatalogueDropState& Expected);
        TSharedRef<SWidget> MakeTransport();
        void ClearCatalogueFilter();
        TSharedRef<SWidget> CatalogueNavigation();
        TSharedRef<SWidget> CatalogueCategoryMenu();
        TSharedRef<SWidget> CatalogueBoosterMenu();
        TSharedRef<SWidget> NativePartsPanel(bool bPersistent,FName Event);
        TSharedRef<SWidget> SFXEventPanel(FName Event);
        TSharedRef<SWidget> SceneAudioPanel(EMCComposerSceneAudioPanelSection Section);
        bool PrepareSceneAudio(FString& OutError);
        FName DefaultAudioScene() const;
        void SetAudioView(bool bScenes,bool bSounds);
        void PlayWithSceneAudio();
        void RefreshSFXGallery();
        void AuditionSound(const FAssetData& Asset);
        bool IsSelectedActionVisible()const;
        TSharedRef<SWidget> GalleryImage(const FAssetData& Asset);
        void AddSelected();
        void DropEffect(FName Stage,const FAssetData& Asset);
        void DropPresentation(FName Stage,FGuid Group,const FAssetData& Asset);
        void SelectSound(FName Stage,FGuid ID);
        void SelectGroup(FName Stage,FGuid ID);
        void SavePack();
        void LoadPackAsset(const FAssetData& Asset,bool bCompare=false);
        TSharedRef<SWidget> PackMenu();
        TSharedRef<SWidget> CompareMenu();
        FSoftObjectPath CompareA,CompareB;
        FString PackLabel;
        TMap<FSoftObjectPath,FString> PackDisplayNames;
        FString PackDisplayName(const FSoftObjectPath& Path)const;
        void CachePackDisplayName(const UMCVFXActionPack* Pack);
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
        bool CanPlay()const{return !bUnsupported&&Session&&Session->CanPreview();}
        bool CanEdit()const{return !bUnsupported&&Session&&Session->CanEditProfile();}
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
        DocumentView=MakeShared<FMCComposerDocumentView>();
        DocumentView->SetUseDocumentPreviewMix(true);
        SceneAudioSession=FMCComposerSceneAudioSession::Shared();
        if(!SceneAudioSession->GetSource()) {
            FString SceneError;
            if(auto* Palette=LoadObject<UMCSoundPalette>(nullptr,TEXT("/Game/Data/DA_MouthSounds.DA_MouthSounds")))
                SceneAudioSession->Open(Palette,SceneError);
        }
        FString Error;const bool Ready=DocumentView->Select(SelectedAction,Error);
        Session=Ready?DocumentView->GetDocument():MakeShared<FMCVFXActionSession>();
        bUnsupported=!Ready;
        Status=Ready?TEXT("Общий черновик VFX и SFX. Навигация сохраняет изменения; применение в игре выполняется отдельно."):TEXT("Ошибка: ")+Error;
        NativePreview=Ready?DocumentView->MakePreviewWidget():Label(Error,14);
        for(const auto& Entry:FMCVFXActionSession::GetCatalogue())Catalogue.Add(MakeShared<FMCVFXActionCatalogueEntry>(Entry));
        RefreshCatalogueFilter();
        GalleryPool=MakeShared<FAssetThumbnailPool>(24);RefreshGallery(TEXT("/Game/Gameplay"));
        const auto Header=SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(10,8)[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(TEXT("VFX Composer  /  ")+ActionTitle());}).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),18))]
                +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SComboButton).ButtonStyle(&SolidStyle()).ContentPadding(FMargin(14,10))
                    .ButtonColorAndOpacity_Lambda([this]{return CanEdit()?WorkspaceMint:HeadingFill;}).ForegroundColor_Lambda([this]{return FSlateColor(CanEdit()?Ink:Muted);})
                    .IsEnabled_Lambda([this]{return !bShowSceneAudio&&CanEdit();}).ToolTipText_Lambda([this]{return CanEdit()?LOCTEXT("NewCompoundHint","Создать композицию выбранного события: пустую или из готового шаблона. Ctrl+Z возвращает предыдущие слои."):FText::FromString(AuthoringUnavailableReason());})
                    .OnGetMenuContent_Lambda([this]{return CompoundMenu();}).ButtonContent()[SNew(STextBlock).Text(LOCTEXT("NewCompoundButton","＋ Новая композиция")).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),13))]]
                +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(&SolidStyle()).ContentPadding(FMargin(14,10))
                    .ButtonColorAndOpacity_Lambda([this]{return CanEdit()?ButtonFill:HeadingFill;})
                    .IsEnabled_Lambda([this]{return bShowSceneAudio?SceneAudioSession&&SceneAudioSession->HasChanges():CanEdit()&&Session->IsRuntimeDirty();})
                    .ToolTipText_Lambda([this]{return bShowSceneAudio?LOCTEXT("ApplySceneAudioHint","Сохранить музыку и атмосферу в выбранной игровой палитре. VFX, SFX событий и пак эффектов не изменяются."):
                        CanEdit()?LOCTEXT("ApplyAllHint","Сохранить композиции с независимыми Niagara-слоями и назначить новый VFX-профиль действию в игре. Исходные эффекты не перезаписываются."):FText::FromString(AuthoringUnavailableReason());})
                    .OnClicked_Lambda([this]{Apply();return FReply::Handled();})[Label(TEXT("Применить в игре"),13)]]]
            +SVerticalBox::Slot().AutoHeight().Padding(10,0,10,8)[SNew(SExpandableArea).InitiallyCollapsed(true)
                .HeaderContent()[Label(TEXT("Версии и паки эффектов"),11,Muted)]
                .BodyContent()[SNew(SBorder).Visibility_Lambda([this]{return bShowSceneAudio?EVisibility::Collapsed:EVisibility::Visible;}).BorderImage(&PanelBrush()).BorderBackgroundColor(PanelFill).Padding(8,6)
                [SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,10,0)[Label(TEXT("ПАК ЭФФЕКТОВ"),10,WorkspaceMint)]
                    +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)[SNew(SEditableTextBox).HintText(FText::FromString(TEXT("Название новой версии…")))
                        .Text_Lambda([this]{return FText::FromString(PackLabel);}).OnTextChanged_Lambda([this](const FText& T){PackLabel=T.ToString();})]
                    +SHorizontalBox::Slot().AutoWidth().Padding(0,0,5,0)[SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(12,7))
                        .IsEnabled_Lambda([this]{return CanEdit();}).ToolTipText(FText::FromString(TEXT("Сохранить новую независимую версию VFX + SFX, групп и раскладки. Не назначает её в игре.")))
                        .OnClicked_Lambda([this]{SavePack();return FReply::Handled();})[Label(TEXT("Сохранить новую версию"),11)]]
                    +SHorizontalBox::Slot().AutoWidth().Padding(0,0,5,0)[SNew(SComboButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(10,7))
                        .OnGetMenuContent_Lambda([this]{return PackMenu();}).ButtonContent()[Label(TEXT("Открыть пак"),11)]]
                    +SHorizontalBox::Slot().AutoWidth()[SNew(SComboButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(10,7))
                        .OnGetMenuContent_Lambda([this]{return CompareMenu();}).ButtonContent()[Label(TEXT("A / B"),11)]]]]]
            +SVerticalBox::Slot().AutoHeight().Padding(12,0,12,8)[SNew(STextBlock).Text_Lambda([this]{
                if(bShowSceneAudio)return FText::FromString(TEXT("Музыка и атмосфера · ")+SelectedAudioScene.ToString()+TEXT(" · ")+
                    (SceneAudioSession?SceneAudioSession->GetStatus():TEXT("Выберите палитру")));
                if(!CanPlay()&&!CanEdit())return FText::FromString(bUnsupported?TEXT("Каталог событий"):TEXT("Контекст изменился · выберите действие заново"));
                return FText::FromString(FString(!CanEdit()?TEXT("Просмотр механики  ·  "):Session->HasUnsavedPackChanges()?TEXT("● Несохранённый черновик  ·  "):TEXT("Сохранённая рабочая копия  ·  "))+TEXT("Событие: ")+Session->GetEventLabel(SelectedStage).ToString()+TEXT("  ·  Пак: ")+PackDisplayName(Session->GetLoadedPackPath()));}).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12)).ColorAndOpacity(Muted)];
        const TSharedRef<SWidget> CataloguePanel=SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[Heading(TEXT("ДЕЙСТВИЯ И СОБЫТИЯ"))]
            +SVerticalBox::Slot().AutoHeight().Padding(8,4,8,0)[Label(TEXT("Отметки VFX: ✓ одобрено · ! проверить"),10,Muted)]
            +SVerticalBox::Slot().AutoHeight()[CatalogueNavigation()]
            +SVerticalBox::Slot().FillHeight(1)[SAssignNew(CatalogueView,STreeView<FCatalogueTreeItem>)
                .TreeItemsSource(&CatalogueRoots).SelectionMode(ESelectionMode::Single)
                .OnGenerateRow(this,&SActionWorkspace::CatalogueRow)
                .OnGetChildren_Lambda([](FCatalogueTreeItem Item,TArray<FCatalogueTreeItem>& Children){if(Item)Children=Item->Children;})
                .OnSelectionChanged(this,&SActionWorkspace::SelectCatalogueNode)
                .OnMouseButtonClick_Lambda([this](FCatalogueTreeItem Item){
                    if(Item&&(Item->Kind==EVFXCatalogueNode::Section||Item->Kind==EVFXCatalogueNode::Owner))
                        CatalogueView->SetItemExpansion(Item,!CatalogueView->IsItemExpanded(Item));
                })
                .OnExpansionChanged_Lambda([this](FCatalogueTreeItem Item,bool Expanded){
                    if(!Item||bRefreshingCatalogueFilter)return;
                    KnownCatalogueNodes.Add(Item->Key);
                    if(Expanded)ExpandedCatalogueNodes.Add(Item->Key);else ExpandedCatalogueNodes.Remove(Item->Key);
                })];
        const TSharedRef<SWidget> Preview=SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[SNew(SBorder).Padding(9,5).BorderImage(&PanelBrush()).BorderBackgroundColor(HeadingFill)
                .ToolTipText_Lambda([this]{return CanEdit()?LOCTEXT("PreviewModeHint","Примерка временно добавляет источник к событию. «Добавить» оставляет его в черновике."):FText::FromString(AuthoringUnavailableReason());})
                [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(!CanPlay()?TEXT("Предпросмотр недоступен"):bAudition||bSFXAudition?TEXT("Примерка · не добавлено"):TEXT("Превью действия"));})
                    .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),12)).ColorAndOpacity(WorkspaceMint)]]
            +SVerticalBox::Slot().FillHeight(1)[SAssignNew(PreviewHost,SBox)[NativePreview.ToSharedRef()]]
            +SVerticalBox::Slot().AutoHeight().Padding(6,5)[MakeTransport()];
        const TSharedRef<SWidget> Transport=SNew(SBox)
            .Visibility_Lambda([this]{return WorkspaceShell&&WorkspaceShell->IsGraphOnly()?EVisibility::Visible:EVisibility::Collapsed;})
            [MakeTransport()];
        const TSharedRef<SWidget> GraphPanel=SNew(SVerticalBox)
                    +SVerticalBox::Slot().AutoHeight()[SNew(SWrapBox).UseAllottedSize(true)
                        +SWrapBox::Slot()[Heading(TEXT("ДЕЙСТВИЕ → СОБЫТИЯ → СЛОИ"))]
                        +SWrapBox::Slot().Padding(5,0,9,0).VAlign(VAlign_Center)
                            [SNew(SCheckBox).IsChecked_Lambda([this]{return bShowTechnicalSources?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                                .ToolTipText(FText::FromString(TEXT("Показать исходные материалы, геометрию и управление родной основой. Только вид интерфейса: данные и воспроизведение не меняются.")))
                                .OnCheckStateChanged_Lambda([this](ECheckBoxState State){bShowTechnicalSources=State==ECheckBoxState::Checked;if(Graph)Graph->Rebuild();RefreshDetails();})
                                [Label(TEXT("Техническая основа"),10,Muted)]]
                        +SWrapBox::Slot().Padding(0,0,4,0)[SNew(SBox).ToolTipText(FText::FromString(TEXT("Вписать выбранное событие или группу в видимую область. Раскладка не меняется.")))
                            [Command(TEXT("Вписать узел"),[this]{if(Graph)Graph->FrameSelected();})]]
                        +SWrapBox::Slot()[SNew(SBox).ToolTipText(FText::FromString(TEXT("Показать все события, группы и центральный узел. Раскладка не меняется.")))
                            [Command(TEXT("Вписать граф"),[this]{if(Graph)Graph->Frame();})]]]
                    +SVerticalBox::Slot().FillHeight(1)[SAssignNew(GraphHost,SBox)
                        [SAssignNew(Graph,SActionStageGraph).Session(Session).SelectedStage_Lambda([this]{return SelectedStage;}).SelectedLayer_Lambda([this]{return SelectedLayer;})
                            .ShowTechnicalSources_Lambda([this]{return bShowTechnicalSources;})
                            .SelectedGroup_Lambda([this]{return SelectedGroup;})
                            .SelectedSound_Lambda([this]{return bShowSFX?SelectedSFXLayers.FindRef(SelectedStage):FGuid();})
                            .OnSelectSound_Lambda([this](FName Event,FGuid ID){SelectSound(Event,ID);})
                            .OnSelectGroup_Lambda([this](FName Event,FGuid ID){SelectGroup(Event,ID);})
                            .OnError_Lambda([this](FString Error){Fail(Error);})
                            .OnChanged_Lambda([this](bool Runtime){if(Runtime)Stop();Refresh();Status=Runtime?TEXT("Группа изменена в черновике. Играть — проверить; Применить — сохранить в игру."):TEXT("Раскладка сохранена в черновике; воспроизведение продолжается. Ctrl+Z — отмена.");})
                            .OnSelect_Lambda([this](FName Stage,FGuid ID){SelectLayer(Stage,ID);}).OnHub_Lambda([this]{bShowActionDetails=true;SelectedLayer.Invalidate();RefreshDetails();SyncCatalogueSelection();})
                            .OnEffectDropped_Lambda([this](FName Stage,FGuid Group,FAssetData Asset){DropPresentation(Stage,Group,Asset);})]]
                    +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text_Lambda([this]{return CanEdit()?LOCTEXT("GraphNavigation","1 Событие → 2 Примерить → 3 Добавить → 4 Применить"):
                        FText::FromString(CanPlay()?TEXT("Реальный сценарий · Niagara-слои пока не редактируются"):TEXT("Выберите действие, чтобы восстановить контекст"));})
                        .ToolTipText_Lambda([this]{return CanEdit()?LOCTEXT("GraphMeaning","ПКМ — панорама; колесо — масштаб. Выберите событие, затем слой. Провода показывают события одного настоящего действия; они не создают новую игровую механику."):FText::FromString(AuthoringUnavailableReason());})
                        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10)).ColorAndOpacity(Muted)];
        const TSharedRef<SWidget> Library=SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,3,0)[SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(7,5))
                                .ButtonColorAndOpacity_Lambda([this]{return !bShowSceneAudio&&!bShowSFX?WorkspaceMint:FLinearColor::White;})
                                .OnClicked_Lambda([this]{SetAudioView(false,false);return FReply::Handled();})[Label(TEXT("VFX"),11)]]
                            +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,3,0)[SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(7,5))
                                .ButtonColorAndOpacity_Lambda([this]{return !bShowSceneAudio&&bShowSFX?WorkspaceMint:FLinearColor::White;})
                                .OnClicked_Lambda([this]{SetAudioView(false,true);return FReply::Handled();})[Label(TEXT("SFX"),11)]]
                            +SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(7,5))
                                .ButtonColorAndOpacity_Lambda([this]{return bShowSceneAudio?WorkspaceMint:FLinearColor::White;})
                                .OnClicked_Lambda([this]{SetAudioView(true,false);return FReply::Handled();})[Label(TEXT("Фон сцены"),11)]]]
                        +SVerticalBox::Slot().FillHeight(1)[SNew(SWidgetSwitcher).WidgetIndex_Lambda([this]{return bShowSceneAudio?2:bShowSFX?1:0;})
                            +SWidgetSwitcher::Slot()[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth().Padding(6)[SNew(SBox).WidthOverride(180)
                    [SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight()[Heading(TEXT("NIAGARA · ИСТОЧНИКИ"))]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,5)[SmallCommand(TEXT("Игра · /Game/Gameplay"),[this]{RefreshGallery(TEXT("/Game/Gameplay"));})]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,5)[SmallCommand(TEXT("Учебные эффекты"),[this]{RefreshGallery(TEXT("/Game/Tests/VFXPractice/Effects"));})]
                        +SVerticalBox::Slot().AutoHeight()[SmallCommand(TEXT("Весь /Game"),[this]{RefreshGallery(TEXT("/Game"));})]
                        +SVerticalBox::Slot().FillHeight(1)
                        +SVerticalBox::Slot().AutoHeight().Padding(4)[Label(TEXT("Перетащите эффект на событие или группу схемы."),10,Muted)]]]
                +SHorizontalBox::Slot().FillWidth(1).Padding(0,4)
                    [SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight()[SAssignNew(GallerySearchBox,SEditableTextBox).HintText(LOCTEXT("Search","Найти эффект…")).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12)).Padding(FMargin(7,5))
                            .OnTextChanged_Lambda([this](const FText& T){GalleryQuery=T.ToString();RefreshGallery();})]
                        +SVerticalBox::Slot().FillHeight(1)[SAssignNew(GalleryView,STileView<FGalleryItem>).ListItemsSource(&GalleryItems).SelectionMode(ESelectionMode::Single).ItemWidth(126.f).ItemHeight(126.f)
                            .OnGenerateTile_Lambda([this](FGalleryItem Item,const TSharedRef<STableViewBase>& Owner)->TSharedRef<ITableRow>
                            {return SNew(STableRow<FGalleryItem>,Owner).Padding(4).SignalSelectionMode(ETableRowSignalSelectionMode::Deferred)
                                .OnDragDetected_Lambda([this,Item](const FGeometry&,const FPointerEvent&){return BeginGalleryDrag(*Item);})
                                [SNew(SBorder).Padding(5).BorderImage_Lambda([this,Item]{return GallerySelection.PackageName==Item->PackageName?&SelectedPanelBrush():&PanelBrush();})
                                    .BorderBackgroundColor_Lambda([this,Item]{return GallerySelection.PackageName==Item->PackageName?UIColor(TEXT("#1B3D3F")):Surface;})
                                    .ToolTipText(FText::FromString(Item->GetSoftObjectPath().ToString()))
                                    [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(64)[GalleryImage(*Item)]]
                                        +SVerticalBox::Slot().AutoHeight().Padding(0,4)[SNew(SBox).HeightOverride(28)[Label(WorkspaceEffectLabel(Item->AssetName.ToString()),10)]]
                                        +SVerticalBox::Slot().AutoHeight()[Label(ProvenanceLabel(*Item),9,Muted)]]];})
                            .OnSelectionChanged_Lambda([this](FGalleryItem Item,ESelectInfo::Type){if(Item)SelectGallery(Item);})]
                        +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1)[SNew(SCheckBox).IsEnabled_Lambda([this]{return CanPlay()&&CanEdit();}).IsChecked_Lambda([this]{return bLive?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                                .OnCheckStateChanged_Lambda([this](ECheckBoxState S){if(!CanPlay()||!CanEdit()){Status=Session->GetPreviewUnavailableReason();return;}bLive=S==ECheckBoxState::Checked;if(bLive&&GallerySelection.IsValid())Play(true);else if(!bLive&&bAudition)Stop();})[Label(TEXT("Клик — просмотр"),10)]]
                            +SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(FString::Printf(TEXT("%d NS"),GalleryItems.Num()));}).ColorAndOpacity(Muted)]]]
                +SHorizontalBox::Slot().AutoWidth().Padding(6)[SNew(SBox).WidthOverride(280)
                    [SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight()[Heading(TEXT("ВЫБРАННЫЙ ИСТОЧНИК"))]
                        +SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()
                            [SNew(SVerticalBox)
                                +SVerticalBox::Slot().AutoHeight().Padding(5)[SNew(STextBlock)
                                    .Text_Lambda([this]{return FText::FromString(GallerySelection.IsValid()?WorkspaceEffectLabel(GallerySelection.AssetName.ToString()):TEXT("Выберите эффект в библиотеке"));})
                                    .ColorAndOpacity(TextColor).AutoWrapText(true)]
                                +SVerticalBox::Slot().AutoHeight().Padding(5)[SNew(STextBlock)
                                    .Text_Lambda([this]{return FText::FromString(GallerySelection.GetSoftObjectPath().ToString());})
                                    .ColorAndOpacity(Muted).AutoWrapText(true)]
                                +SVerticalBox::Slot().AutoHeight().Padding(5)[SNew(STextBlock)
                                    .Text_Lambda([this]{return FText::FromString(TEXT("В событие: ")+Session->GetEventLabel(SelectedStage).ToString());})
                                    .ColorAndOpacity(WorkspaceMint).AutoWrapText(true)]]]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,4)[SNew(SButton).ButtonStyle(&SolidStyle()).ContentPadding(FMargin(10,6))
                            .ButtonColorAndOpacity_Lambda([this]{return CanEdit()&&GallerySelection.IsValid()?WorkspaceMint:HeadingFill;}).ForegroundColor_Lambda([this]{return FSlateColor(CanEdit()&&GallerySelection.IsValid()?Ink:Muted);})
                            .ToolTipText_Lambda([this]{return CanEdit()?FText::FromString(TEXT("Добавить выбранную Niagara новым слоем события.")):FText::FromString(AuthoringUnavailableReason());})
                            .IsEnabled_Lambda([this]{return CanEdit()&&GallerySelection.IsValid();}).OnClicked_Lambda([this]{AddSelected();return FReply::Handled();})
                            [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(!CanEdit()?(CanPlay()?TEXT("Niagara-привязка пока недоступна"):TEXT("Откройте контекст действия")):
                                GallerySelection.IsValid()?TEXT("＋ Добавить в «")+Session->GetEventLabel(SelectedStage).ToString()+TEXT("»"):TEXT("Выберите эффект в галерее"));}).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),12)).AutoWrapText(true)]]]]]
                            +SWidgetSwitcher::Slot()[SAssignNew(SFXGalleryHost,SBox)]
                            +SWidgetSwitcher::Slot()[SNew(SScrollBox)+SScrollBox::Slot()[SNew(SVerticalBox)
                                +SVerticalBox::Slot().AutoHeight().Padding(8)[SceneAudioPanel(EMCComposerSceneAudioPanelSection::Header)]
                                +SVerticalBox::Slot().AutoHeight().Padding(8,3)[SNew(SCheckBox)
                                    .IsChecked_Lambda([this]{return bUseSceneAudio?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                                    .OnCheckStateChanged_Lambda([this](ECheckBoxState State){Stop();bUseSceneAudio=State==ECheckBoxState::Checked;})
                                    [Label(TEXT("Слышать фон вместе с действием"),11)]]
                                +SVerticalBox::Slot().AutoHeight().Padding(8,3)[SNew(SCheckBox)
                                    .IsChecked_Lambda([this]{return bFollowActionScene?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                                    .OnCheckStateChanged_Lambda([this](ECheckBoxState State){Stop();bFollowActionScene=State==ECheckBoxState::Checked;if(bFollowActionScene)SelectedAudioScene=DefaultAudioScene();RefreshDetails();})
                                    [Label(TEXT("Выбирать фон по действию"),11)]]
                                +SVerticalBox::Slot().AutoHeight().Padding(8,3)[SNew(SHorizontalBox)
                                    +SHorizontalBox::Slot().FillWidth(1)[SNew(SCheckBox)
                                        .IsChecked_Lambda([this]{return bMusicAudible?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                                        .OnCheckStateChanged_Lambda([this](ECheckBoxState State){bMusicAudible=State==ECheckBoxState::Checked;if(const auto Runner=DocumentView->GetRunner())Runner->SetSceneAudioMix(bMusicAudible?1.f:0.f,bAmbienceAudible?1.f:0.f);})
                                        [Label(TEXT("Музыка"),11)]]
                                    +SHorizontalBox::Slot().FillWidth(1)[SNew(SCheckBox)
                                        .IsChecked_Lambda([this]{return bAmbienceAudible?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                                        .OnCheckStateChanged_Lambda([this](ECheckBoxState State){bAmbienceAudible=State==ECheckBoxState::Checked;if(const auto Runner=DocumentView->GetRunner())Runner->SetSceneAudioMix(bMusicAudible?1.f:0.f,bAmbienceAudible?1.f:0.f);})
                                        [Label(TEXT("Атмосфера"),11)]]]
                                +SVerticalBox::Slot().AutoHeight().Padding(8)[Label(TEXT("Фон звучит в том же превью, что выбранное действие слева. Рыцарь и Маг пока используют общий NutEncounter. Фазовые музыкальные переходы и полный хаб-превью подключаются следующими этапами."),11,Muted)]
                            ]]];
        const TSharedRef<SWidget> Inspector=SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight()[SNew(SBorder).Padding(9,6).BorderImage(&PanelBrush()).BorderBackgroundColor(HeadingFill)
                            [SNew(STextBlock).Text_Lambda([this]{return bShowSceneAudio?LOCTEXT("SceneAudioDetailsTitle","МУЗЫКА И АТМОСФЕРА"):bShowActionDetails?LOCTEXT("ActionDetailsTitle","ДЕЙСТВИЕ"):bShowSFX?LOCTEXT("SFXDetailsTitle","ЗВУК СОБЫТИЯ"):
                                SelectedLayer.IsValid()?LOCTEXT("LayerDetailsTitle","ВЫБРАННЫЙ СЛОЙ"):LOCTEXT("EventDetailsTitle","СОБЫТИЕ");})
                                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),12)).ColorAndOpacity(TextColor)]]
                        +SVerticalBox::Slot().FillHeight(1)[SNew(SBorder).Padding(0).BorderImage(&PanelBrush()).BorderBackgroundColor(PanelFill)
                            [SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(Details,SVerticalBox)]]];
        const TSharedRef<SWidget> Footer=SNew(SBorder).Padding(10,7).BorderImage(&PanelBrush()).BorderBackgroundColor(AppFill)[SNew(SBox).MaxDesiredHeight(58).Clipping(EWidgetClipping::ClipToBounds)
                [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Status);}).ToolTipText_Lambda([this]{return FText::FromString(Status);})
                    .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11)).AutoWrapText(true)
                    .WrapTextAt_Lambda([this]{return FMath::Max(200.f,float(GetCachedGeometry().GetLocalSize().X)-24.f);})
                    .ColorAndOpacity_Lambda([this]{return Status.StartsWith(TEXT("Ошибка:"))||Status.StartsWith(TEXT("Не применено:"))?FLinearColor(1.f,.56f,.47f):Muted;})]];
        ChildSlot[SAssignNew(WorkspaceShell,SMCComposerWorkspaceShell).SettingsKey(TEXT("VFX"))
            .Header()[Header]
            .Catalogue()[CataloguePanel]
            .Preview()[Preview]
            .Graph()[GraphPanel]
            .Inspector()[Inspector]
            .Library()[Library]
            .Transport()[Transport]
            .Footer()[Footer]];
        Refresh();TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(this,&SActionWorkspace::TickWorkspace),.1f);
        RefreshCatalogueFilter();
    }

    SActionWorkspace::~SActionWorkspace(){FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);Stop();}
    bool SActionWorkspace::ConfirmDraftTransition(const FString& Reason)
    {
        if(!Session||!Session->HasUnsavedPackChanges())return true;
        enum class EChoice{Cancel,Save,Discard};EChoice Choice=EChoice::Cancel;
        const auto Dialog=SNew(SWindow).Title(FText::FromString(TEXT("Несохранённый пак эффектов"))).ClientSize(FVector2D(560,190)).SupportsMinimize(false).SupportsMaximize(false);
        const TWeakPtr<SWindow> WeakDialog=Dialog;
        auto Choose=[&Choice,WeakDialog](EChoice Value){Choice=Value;if(const auto Window=WeakDialog.Pin())Window->RequestDestroyWindow();};
        Dialog->SetContent(SNew(SBorder).BorderImage(&PanelBrush()).BorderBackgroundColor(PanelFill).Padding(20)
            [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[Label(TEXT("Сохранить изменения перед переходом?"),17)]
                +SVerticalBox::Slot().FillHeight(1).Padding(0,10)[Label(Reason+TEXT(". В текущем черновике есть изменения VFX, SFX или раскладки, ещё не сохранённые в пак. Сохранение не назначает эффекты в игре."),12,Muted)]
                +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)[Command(TEXT("Сохранить пак"),[Choose]{Choose(EChoice::Save);},true)]
                    +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)[Command(TEXT("Отбросить"),[Choose]{Choose(EChoice::Discard);})]
                    +SHorizontalBox::Slot().FillWidth(1)[Command(TEXT("Отмена"),[Choose]{Choose(EChoice::Cancel);})]]]);
        FSlateApplication::Get().AddModalWindow(Dialog,FSlateApplication::Get().FindWidgetWindow(AsShared()),false);
        if(Choice==EChoice::Save){SavePack();return !Session->HasUnsavedPackChanges();}return Choice==EChoice::Discard;
    }
    bool SActionWorkspace::CanClose()
    {
        // Closing one view cannot discard the audio document still shown in
        // another. Editor-lifetime documents retain drafts until explicit Apply.
        return true;
    }
    void SActionWorkspace::Closed(){Stop();}
    FString SActionWorkspace::ActionTitle()const
    {for(const auto& Entry:Catalogue)if(Entry->ID==SelectedAction)return Entry->Label.ToString();return SelectedAction.ToString();}
    TSharedRef<SWidget> SActionWorkspace::MakeTransport()
    {
        return SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4,4))
            +SWrapBox::Slot()[SNew(SButton).ButtonStyle(&SolidStyle()).ContentPadding(FMargin(10,6))
                .ButtonColorAndOpacity_Lambda([this]{return CanPlay()?WorkspaceMint:HeadingFill;})
                .ForegroundColor_Lambda([this]{return FSlateColor(CanPlay()?Ink:Muted);}).IsEnabled_Lambda([this]{return CanPlay();})
                .ToolTipText_Lambda([this]{return CanPlay()?LOCTEXT("PlayHint","Выполнить действие со всеми событиями черновика."):
                    FText::FromString(Session?Session->GetPreviewUnavailableReason():TEXT("Выберите действие."));})
                .OnClicked_Lambda([this]{Play();return FReply::Handled();})[Label(TEXT("▶ Играть"),11,Ink)]]
            +SWrapBox::Slot()[SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(9,6))
                .Text_Lambda([this]{return FText::FromString(bPaused?TEXT("▶"):TEXT("Ⅱ"));})
                .ToolTipText(LOCTEXT("PausePreview","Пауза / продолжить это превью"))
                .IsEnabled_Lambda([this]{return DocumentView&&DocumentView->OwnsPreview()&&(bPlaying||bPaused);})
                .OnClicked_Lambda([this]{if(DocumentView->OwnsPreview()){bPaused=!bPaused;DocumentView->Pause(bPaused);}return FReply::Handled();})]
            +SWrapBox::Slot()[SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(9,6)).Text(FText::FromString(TEXT("■")))
                .ToolTipText(LOCTEXT("StopPreview","Остановить это превью и прослушивание источника"))
                .OnClicked_Lambda([this]{Stop();return FReply::Handled();})]
            +SWrapBox::Slot().VAlign(VAlign_Center)[SNew(SCheckBox)
                .IsChecked_Lambda([this]{return bLoop?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State){bLoop=State==ECheckBoxState::Checked;RepeatAt=0;})[Label(TEXT("Повтор"),10)]]
            +SWrapBox::Slot().VAlign(VAlign_Center)[SNew(SCheckBox)
                .IsChecked_Lambda([this]{return DocumentView->IsPreviewAudioEnabled()?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State){FString Error;if(!DocumentView->SetPreviewAudioEnabled(State==ECheckBoxState::Checked,Error))Fail(Error);RefreshSFXGallery();RefreshDetails();})
                .ToolTipText(LOCTEXT("PreviewAudio","Звук в изолированном предпросмотре"))[Label(TEXT("Звук"),10)]]
            +SWrapBox::Slot().FillEmptySpace(true)[SNew(SComboButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(7,6))
                .IsEnabled_Lambda([this]{return CanPlay();})
                .ToolTipText(LOCTEXT("ScenarioHint","Сценарий действия"))
                .OnGetMenuContent_Lambda([this]()->TSharedRef<SWidget>{
                    auto Menu=SNew(SVerticalBox);
                    for(auto Scenario:Session->GetScenarios())Menu->AddSlot().AutoHeight()[Command(Session->GetScenarioLabel(Scenario).ToString(),[this,Scenario]{Stop();SelectedScenario=Scenario;FSlateApplication::Get().DismissAllMenus();})];
                    return Menu;
                }).ButtonContent()[SNew(STextBlock).Text_Lambda([this]{return Session->GetScenarioLabel(SelectedScenario);})
                    .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10)).ColorAndOpacity(TextColor).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]];
    }
    bool SActionWorkspace::IsSelectedActionVisible()const
    {return FilteredCatalogue.ContainsByPredicate([this](const FCatalogItem& Item){return Item&&Item->ID==SelectedAction;});}
    void SActionWorkspace::RefreshCatalogueFilter()
    {
        // This is strictly a view refresh. Direct selection must never re-enter
        // SelectAction and stop/reinitialize the artist's current native context.
        TGuardValue<bool> Guard(bRefreshingCatalogueFilter,true);
        FilteredCatalogue.Reset();
        for(const auto& Item:Catalogue)if(Item)
        {
            auto Searchable=*Item;
            for(FName Event:Item->Stages)Searchable.SearchNames.Add(Event.ToString()+TEXT(" ")+CatalogueEventLabel(Item->ID,Event).ToString());
            if(CatalogueFilter.Matches(Searchable))FilteredCatalogue.Add(Item);
        }
        BuildCatalogueTree();
    }
    void SActionWorkspace::BuildCatalogueTree()
    {
        CatalogueRoots.Reset();TMap<FString,FCatalogueTreeItem> Sections,Owners;
        auto Add=[](TArray<FCatalogueTreeItem>& Into,const FString& Key,const FText& Label,EVFXCatalogueNode Kind,FCatalogueTreeItem Parent=FCatalogueTreeItem()){
            auto Node=MakeShared<FVFXCatalogueNode>();Node->Key=Key;Node->Label=Label;Node->Kind=Kind;Node->Parent=Parent;Into.Add(Node);return Node;
        };
        for(const auto& Entry:FilteredCatalogue)
        {
            const FString SectionKey=TEXT("section/")+FString::FromInt(static_cast<int32>(Entry->Section));
            auto Section=Sections.FindRef(SectionKey);
            if(!Section){Section=Add(CatalogueRoots,SectionKey,MCVFXCatalogueNavigation::Label(Entry->Section),EVFXCatalogueNode::Section);Sections.Add(SectionKey,Section);}
            const FName OwnerID=Entry->OwnerID.IsNone()?Entry->ID:Entry->OwnerID;
            const FText OwnerLabel=Entry->OwnerLabel.IsEmpty()?Entry->Label:Entry->OwnerLabel;
            auto Parent=Section;
            if(OwnerID!=Entry->ID)
            {
                const FString OwnerKey=SectionKey+TEXT("/owner/")+OwnerID.ToString();Parent=Owners.FindRef(OwnerKey);
                if(!Parent){Parent=Add(Section->Children,OwnerKey,OwnerLabel,EVFXCatalogueNode::Owner,Section);Owners.Add(OwnerKey,Parent);}
            }
            auto Action=Add(Parent->Children,TEXT("action/")+Entry->ID.ToString(),OwnerID==Entry->ID?OwnerLabel:Entry->ActionLabel.IsEmpty()?Entry->Label:Entry->ActionLabel,EVFXCatalogueNode::Action,Parent);
            Action->Entry=Entry;
            for(FName Event:Entry->Stages)
            {
                auto Node=Add(Action->Children,Action->Key+TEXT("/event/")+Event.ToString(),CatalogueEventLabel(Entry->ID,Event),EVFXCatalogueNode::Event,Action);
                Node->Entry=Entry;Node->Event=Event;
            }
        }
        if(!CatalogueView)return;
        TGuardValue<bool> Guard(bRefreshingCatalogueFilter,true);
        CatalogueView->RequestTreeRefresh();
        TFunction<void(FCatalogueTreeItem)> Restore=[&](FCatalogueTreeItem Node){
            const bool DefaultOpen=Node->Kind==EVFXCatalogueNode::Section||Node->Kind==EVFXCatalogueNode::Owner||(Node->Kind==EVFXCatalogueNode::Action&&Node->Entry->ID==SelectedAction);
            const bool Expanded=!CatalogueFilter.Query.IsEmpty()||ExpandedCatalogueNodes.Contains(Node->Key)||(!KnownCatalogueNodes.Contains(Node->Key)&&DefaultOpen);
            CatalogueView->SetItemExpansion(Node,Expanded);
            if(!KnownCatalogueNodes.Contains(Node->Key)){KnownCatalogueNodes.Add(Node->Key);if(DefaultOpen)ExpandedCatalogueNodes.Add(Node->Key);}
            for(const auto& Child:Node->Children)Restore(Child);
        };
        for(const auto& Root:CatalogueRoots)Restore(Root);
        SyncCatalogueSelection();
    }
    void SActionWorkspace::SyncCatalogueSelection(bool bReveal)
    {
        if(!CatalogueView)return;TGuardValue<bool> Guard(bRefreshingCatalogueFilter,true);
        FCatalogueTreeItem Found,Action;
        TFunction<void(FCatalogueTreeItem)> Find=[&](FCatalogueTreeItem Node){
            if(Node->Entry&&Node->Entry->ID==SelectedAction){
                if(Node->Kind==EVFXCatalogueNode::Action)Action=Node;
                if(Node->Kind==EVFXCatalogueNode::Event&&Node->Event==SelectedStage&&!bShowActionDetails)Found=Node;
            }
            for(const auto& Child:Node->Children)Find(Child);
        };
        for(const auto& Root:CatalogueRoots)Find(Root);
        if(!Found)Found=Action;
        CatalogueView->ClearSelection();
        if(!Found)return;
        CatalogueView->SetSelection(Found,ESelectInfo::Direct);
        if(bReveal){for(auto Parent=Found->Parent.Pin();Parent;Parent=Parent->Parent.Pin()){
            CatalogueView->SetItemExpansion(Parent,true);KnownCatalogueNodes.Add(Parent->Key);ExpandedCatalogueNodes.Add(Parent->Key);}
            CatalogueView->RequestScrollIntoView(Found);}
    }
    void SActionWorkspace::SelectCatalogueNode(FCatalogueTreeItem Item,ESelectInfo::Type Origin)
    {
        if(!Item||bRefreshingCatalogueFilter||Origin==ESelectInfo::Direct)return;
        if(Item->Kind==EVFXCatalogueNode::Section||Item->Kind==EVFXCatalogueNode::Owner)return;
        if(!Item->Entry)return;
        if(Item->Entry->ID!=SelectedAction)SelectAction(Item->Entry);
        bShowSceneAudio=false;SelectedLayer.Invalidate();SelectedGroup.Invalidate();SelectedSFXLayers.Reset();
        if(Item->Kind==EVFXCatalogueNode::Event)
        {
            if(!Item->Entry->Stages.Contains(Item->Event))return;
            if(bAudition||bSFXAudition)Stop();
            SelectedStage=Item->Event;bShowActionDetails=false;SelectEventScenario();
            if(Graph&&!bUnsupported)Graph->FrameSelected();
        }
        else bShowActionDetails=true;
        RefreshSFXGallery();RefreshDetails();SyncCatalogueSelection();
    }
    TSharedRef<ITableRow> SActionWorkspace::CatalogueRow(FCatalogueTreeItem Item,const TSharedRef<STableViewBase>& Owner)
    {
        const bool Group=Item->Kind==EVFXCatalogueNode::Section||Item->Kind==EVFXCatalogueNode::Owner;
        const bool Metadata=Item->Entry&&!Item->Entry->bSupportsPreview&&!Item->Entry->bSupportsProfileOverride;
        const auto DropState=MakeShared<FVFXCatalogueDropState>();
        return SNew(STableRow<FCatalogueTreeItem>,Owner).Padding(FMargin(2,3))
            .ToolTipText(FText::FromString(Item->Entry?(Metadata?Item->Entry->UnsupportedReason:Item->Entry->Description.ToString()):Item->Label.ToString()))
            .OnCanAcceptDrop_Lambda([this,DropState](const FDragDropEvent& Drag,EItemDropZone,FCatalogueTreeItem Node)->TOptional<EItemDropZone>{
                if(!Node||Node->Kind!=EVFXCatalogueNode::Event||!Node->Entry)return TOptional<EItemDropZone>();
                const auto Target=MCComposerDocuments::Find(Node->Entry->ID);
                if(DropState->Operation.Pin()!=Drag.GetOperation()){
                    DropState->Operation=Drag.GetOperation();DropState->bTargetWasOpen=Target.IsValid();
                    DropState->TargetToken=Target?Target->GetDraftToken():FGuid();
                }
                FAssetData Asset;FString Reason;
                const bool SameTarget=DropState->bTargetWasOpen?(Target&&Target->GetDraftToken()==DropState->TargetToken):!Target;
                if(!SameTarget)Reason=TEXT("Черновик назначения изменился. Начните перетаскивание заново.");
                const bool Valid=SameTarget&&Node->Entry->bSupportsProfileOverride&&ReadCatalogueDrop(Drag,Asset,Reason);
                CatalogueDropTarget=Valid?Node->Key:FString();
                const FString TargetLabel=Node->Entry->Label.ToString()+TEXT(" / ")+Node->Label.ToString();
                if(const auto Op=Drag.GetOperationAs<FWorkspaceEffectDrag>())Op->Target(TargetLabel,Valid);
                Status=Valid?Asset.AssetName.ToString()+TEXT(" → ")+TargetLabel+TEXT(" · новый слой"):
                    Reason.IsEmpty()?TEXT("Для этого события редактирование пока недоступно."):Reason;
                return Valid?TOptional<EItemDropZone>(EItemDropZone::OntoItem):TOptional<EItemDropZone>();
            })
            .OnDragLeave_Lambda([this](const FDragDropEvent& Drag){CatalogueDropTarget.Reset();if(const auto Op=Drag.GetOperationAs<FWorkspaceEffectDrag>())Op->Reject();})
            .OnAcceptDrop_Lambda([this,DropState](const FDragDropEvent& Drag,EItemDropZone,FCatalogueTreeItem Node){DropOnCatalogueEvent(Node,Drag,*DropState);return FReply::Handled();})
            [SNew(SBorder).Padding(3,2).BorderImage(&PanelBrush())
                .BorderBackgroundColor_Lambda([this,Item]{return CatalogueDropTarget==Item->Key?UIColor(TEXT("#204C44")):FLinearColor::Transparent;})
                [SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)
                        [Item->Kind==EVFXCatalogueNode::Event&&Item->Entry?VFXReviewCheckbox(Item->Entry->ID,Item->Event,true):SNullWidget::NullWidget]
                    +SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(Item->Label).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                        .Font(FCoreStyle::GetDefaultFontStyle(Group?TEXT("Bold"):TEXT("Regular"),11))
                        .ColorAndOpacity(Group||Metadata?Muted:Item->Kind==EVFXCatalogueNode::Event?WorkspaceMint:TextColor)]
                    +SHorizontalBox::Slot().AutoWidth().Padding(4,0,0,0)[SNew(STextBlock)
                        .Text_Lambda([Item]{if(!Item->Entry)return FText::GetEmpty();if(!Item->Entry->bSupportsPreview&&!Item->Entry->bSupportsProfileOverride)return FText::FromString(TEXT("каталог"));
                            if(Item->Kind==EVFXCatalogueNode::Event)if(const auto Document=MCComposerDocuments::Find(Item->Entry->ID)){
                                const int32 VFX=Document->GetLayerCount(Item->Event),SFX=AddedSoundCount(*Document,Item->Event);
                                if(VFX||SFX)return FText::FromString(FString::Printf(TEXT("%d ◇  %d ♪"),VFX,SFX));}
                            return FText::GetEmpty();}).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9)).ColorAndOpacity(Muted)]]];
    }
    TSharedRef<SWidget> SActionWorkspace::VFXReviewCheckbox(FName Action,FName Event,bool bCompact)
    {
        // Reading review metadata must not acquire a document, load a package,
        // change navigation, or claim the other Composer's preview.
        auto Read=[Action,Event]{const auto Document=MCComposerDocuments::Find(Action);
            return MCComposerReview::Query(Action,Event,EMCComposerReviewLane::VFX,Document.Get());};
        return SNew(SCheckBox)
            .IsChecked_Lambda([Read]{const auto Review=Read();
                if(Review.bStale||(Review.bApproved&&!Review.bCurrentKnown))return ECheckBoxState::Undetermined;
                return Review.bApproved?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .ToolTipText_Lambda([Read]{const auto Review=Read();
                const FString State=Review.bStale?TEXT("Нужна повторная проверка VFX. Нажмите после проверки, чтобы одобрить текущую версию."):
                    Review.bApproved&&!Review.bCurrentKnown?TEXT("Одобрение VFX сохранено. Текущая версия ещё не сверена; нажмите после проверки."):
                    Review.bApproved?TEXT("VFX одобрено. Нажмите, чтобы снять отметку."):
                    TEXT("Отметить VFX события как одобренное после проверки. Это отметка проверки, а не включение эффекта.");
                return FText::FromString(Review.Detail.IsEmpty()?State:State+TEXT("\n")+Review.Detail);})
            .OnCheckStateChanged_Lambda([this,Read,Action,Event](ECheckBoxState State){
                const auto Previous=Read();
                // An indeterminate mark is a request to review the new version;
                // the explicit click approves that version rather than hiding staleness.
                const bool Approve=State==ECheckBoxState::Checked||Previous.bStale||(Previous.bApproved&&!Previous.bCurrentKnown);
                FString Error;
                if(!MCComposerReview::SetApproved(Action,Event,EMCComposerReviewLane::VFX,Approve,Error)){Fail(Error);return;}
                Status=(Approve?TEXT("VFX одобрено: "):TEXT("Отметка одобрения VFX снята: "))+CatalogueEventLabel(Action,Event).ToString();
            })
            [SNew(STextBlock).Text_Lambda([Read,bCompact]{const auto Review=Read();
                const bool NeedsReview=Review.bStale||(Review.bApproved&&!Review.bCurrentKnown);
                if(bCompact)return NeedsReview?FText::FromString(TEXT("!")):FText::GetEmpty();
                return FText::FromString(NeedsReview?TEXT("VFX: нужна повторная проверка"):Review.bApproved?TEXT("VFX одобрено"):TEXT("VFX ещё не одобрено"));})
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),bCompact?10:11)).AutoWrapText(!bCompact)
                .ColorAndOpacity_Lambda([Read]{const auto Review=Read();return Review.bStale||(Review.bApproved&&!Review.bCurrentKnown)?FLinearColor(1.f,.72f,.36f):Review.bApproved?WorkspaceMint:Muted;})];
    }
    bool SActionWorkspace::ReadCatalogueDrop(const FDragDropEvent& Drag,FAssetData& Asset,FString& Reason)const
    {
        if(const auto Op=Drag.GetOperationAs<FWorkspaceEffectDrag>())
        {
            const auto Source=MCComposerDocuments::Find(Op->SourceID);
            if(!Source||!Source->IsContextCurrent()||Source->GetDraftToken()!=Op->Token){Reason=TEXT("Черновик источника изменился. Начните перетаскивание заново.");return false;}
            Asset=Op->Asset;
        }
        else if(const auto AssetOp=Drag.GetOperationAs<FAssetDragDropOp>())
        {
            if(const auto Scoped=Drag.GetOperationAs<FMCComposerSourceDrag>();Scoped&&!Scoped->IsSourceCurrent()){
                Reason=TEXT("Черновик источника изменился. Начните перетаскивание заново.");return false;}
            if(AssetOp->GetAssets().Num()!=1){Reason=TEXT("Перетащите один звуковой источник или Niagara System.");return false;}
            Asset=AssetOp->GetAssets()[0];
        }
        else return false;
        if(!Asset.IsValid()||(!Asset.IsInstanceOf(USoundBase::StaticClass())&&!Asset.IsInstanceOf(UNiagaraSystem::StaticClass()))){Reason=TEXT("Поддерживаются звуковые источники и Niagara System.");return false;}
        const FText Unavailable=SMCSFXGallery::GetAssetUnavailableReason(Asset);
        if(!Unavailable.IsEmpty()){Reason=Unavailable.ToString();return false;}
        return true;
    }
    bool SActionWorkspace::DropOnCatalogueEvent(FCatalogueTreeItem Item,const FDragDropEvent& Drag,const FVFXCatalogueDropState& Expected)
    {
        CatalogueDropTarget.Reset();FAssetData Asset;FString Error;
        if(!Item||Item->Kind!=EVFXCatalogueNode::Event||!Item->Entry||!Item->Entry->bSupportsProfileOverride||!Item->Entry->Stages.Contains(Item->Event))return false;
        if(!ReadCatalogueDrop(Drag,Asset,Error)){if(!Error.IsEmpty())Fail(Error);return false;}
        const FName Action=Item->Entry->ID,Event=Item->Event;
        const auto Existing=MCComposerDocuments::Find(Action);
        if(Expected.Operation.Pin()!=Drag.GetOperation()||(Expected.bTargetWasOpen?(!Existing||Existing->GetDraftToken()!=Expected.TargetToken):Existing.IsValid())){
            Fail(TEXT("Черновик назначения изменился. Начните перетаскивание заново."));return false;}
        const auto Target=MCComposerDocuments::Acquire(Action,Error);
        const FGuid Token=Expected.bTargetWasOpen?Expected.TargetToken:Target?Target->GetDraftToken():FGuid();
        if(!Target||Target->GetDraftToken()!=Token||!Target->CanEditProfile()||!Target->GetActionStages().Contains(Event)||!Target->ValidateSourceCurrent(Error)){
            Fail(Error.IsEmpty()?TEXT("Событие больше не доступно для редактирования."):Error);return false;}
        auto* Source=Asset.GetAsset();
        FAssetData Checked;
        if(!Target->ValidateSourceCurrent(Error)||Target->GetDraftToken()!=Token||!ReadCatalogueDrop(Drag,Checked,Error)){
            Fail(Error.IsEmpty()?TEXT("Черновик изменился во время загрузки источника."):Error);return false;}
        FGuid ID;
        if(auto* Sound=Cast<USoundBase>(Source))ID=Target->AddSFXLayer(Token,Event,Sound,FGuid(),Error);
        else if(auto* System=Cast<UNiagaraSystem>(Source))ID=Target->AddSystemToGroup(Token,Event,System,FGuid(),Error);
        else Error=TEXT("Источник не загружается как звук или Niagara System.");
        if(!ID.IsValid()){Fail(Error);return false;}
        if(Target==Session)Refresh();
        Status=TEXT("Добавлен новый слой: ")+Item->Entry->Label.ToString()+TEXT(" / ")+Item->Label.ToString()+TEXT(". Ctrl+Z — отмена.");
        return true;
    }
    void SActionWorkspace::ClearCatalogueFilter()
    {
        CatalogueFilter=FMCVFXCatalogueFilter();
        if(CatalogueSearchBox)CatalogueSearchBox->SetText(FText::GetEmpty());
        RefreshCatalogueFilter();
    }
    TSharedRef<SWidget> SActionWorkspace::CatalogueCategoryMenu()
    {
        auto Menu=SNew(SVerticalBox);
        for(const auto Section:{EMCVFXActionSection::All,EMCVFXActionSection::Boosters,EMCVFXActionSection::Bosses,
            EMCVFXActionSection::WorldEvents,EMCVFXActionSection::General,EMCVFXActionSection::InterfaceHub,EMCVFXActionSection::Other}) {
            if(Section==EMCVFXActionSection::Other&&!Catalogue.ContainsByPredicate([](const FCatalogItem& Item){return Item->Section==EMCVFXActionSection::Other;}))continue;
            int32 Count=0;for(const auto& Item:Catalogue)if(Section==EMCVFXActionSection::All||Item->Section==Section)++Count;
            Menu->AddSlot().AutoHeight()[Command(MCVFXCatalogueNavigation::Label(Section).ToString()+FString::Printf(TEXT("  · %d"),Count),[this,Section]{
                CatalogueFilter.Section=Section;CatalogueFilter.BoosterKind.Reset();RefreshCatalogueFilter();FSlateApplication::Get().DismissAllMenus();})];
        }
        return Menu;
    }
    TSharedRef<SWidget> SActionWorkspace::CatalogueBoosterMenu()
    {
        auto Menu=SNew(SVerticalBox);
        auto Add=[this,&Menu](TOptional<EMCPerkKind> Kind){Menu->AddSlot().AutoHeight()[Command(MCVFXCatalogueNavigation::BoosterLabel(Kind).ToString(),[this,Kind]{
            CatalogueFilter.BoosterKind=Kind;RefreshCatalogueFilter();FSlateApplication::Get().DismissAllMenus();})];};
        Add(TOptional<EMCPerkKind>());Add(EMCPerkKind::Active);Add(EMCPerkKind::Passive);Add(EMCPerkKind::Consumable);
        return Menu;
    }
    TSharedRef<SWidget> SActionWorkspace::CatalogueNavigation()
    {
        return SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(4,6,4,3)[SAssignNew(CatalogueSearchBox,SSearchBox)
                .HintText(LOCTEXT("FindAction","Найти действие или ID…"))
                .ToolTipText(LOCTEXT("FindActionHint","Название RU/EN, ключ локализации или постоянный ID. Поиск не закрывает текущий черновик."))
                .OnTextChanged_Lambda([this](const FText& Text){CatalogueFilter.Query=Text.ToString();RefreshCatalogueFilter();})]
            +SVerticalBox::Slot().AutoHeight().Padding(4,3)[SNew(SComboButton).ContentPadding(FMargin(8,5))
                .OnGetMenuContent(this,&SActionWorkspace::CatalogueCategoryMenu)
                .ButtonContent()[SNew(STextBlock).Text_Lambda([this]{return MCVFXCatalogueNavigation::Label(CatalogueFilter.Section);})
                    .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),11)).ColorAndOpacity(TextColor).AutoWrapText(true)]]
            +SVerticalBox::Slot().AutoHeight().Padding(4,3)[SNew(SComboButton).ContentPadding(FMargin(8,5))
                .Visibility_Lambda([this]{return CatalogueFilter.Section==EMCVFXActionSection::Boosters?EVisibility::Visible:EVisibility::Collapsed;})
                .OnGetMenuContent(this,&SActionWorkspace::CatalogueBoosterMenu)
                .ButtonContent()[SNew(STextBlock).Text_Lambda([this]{return MCVFXCatalogueNavigation::BoosterLabel(CatalogueFilter.BoosterKind);})
                    .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11)).ColorAndOpacity(TextColor).AutoWrapText(true)]]
            +SVerticalBox::Slot().AutoHeight().Padding(8,3)[SNew(STextBlock)
                .Text_Lambda([this]{return FText::Format(LOCTEXT("CatalogueCount","Найдено {0} из {1}"),FText::AsNumber(FilteredCatalogue.Num()),FText::AsNumber(Catalogue.Num()));})
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10)).ColorAndOpacity(Muted)]
            +SVerticalBox::Slot().AutoHeight().Padding(4,3)[SNew(SBorder).Padding(7).BorderImage(&PanelBrush()).BorderBackgroundColor(HeadingFill)
                .Visibility_Lambda([this]{return !IsSelectedActionVisible()?EVisibility::Visible:EVisibility::Collapsed;})
                [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
                    .Text_Lambda([this]{return FText::Format(LOCTEXT("OpenOutsideFilter","Открыто: {0}\nВне текущего фильтра"),FText::FromString(ActionTitle()));})
                    .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10)).ColorAndOpacity(WorkspaceMint).AutoWrapText(true)]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,5,0,0)[Command(TEXT("Показать открытое"),[this]{ClearCatalogueFilter();})]]]
            +SVerticalBox::Slot().AutoHeight().Padding(4,6)[SNew(SVerticalBox)
                .Visibility_Lambda([this]{return FilteredCatalogue.IsEmpty()?EVisibility::Visible:EVisibility::Collapsed;})
                +SVerticalBox::Slot().AutoHeight().Padding(5)[Label(TEXT("Ничего не найдено. Текущее действие остаётся открытым."),11,Muted)]
                +SVerticalBox::Slot().AutoHeight()[Command(TEXT("Сбросить фильтры"),[this]{ClearCatalogueFilter();})]];
    }
    bool SActionWorkspace::SelectForReview(FName ActionID,FString& Error)
    {
        TArray<FCatalogItem> Fresh;
        FCatalogItem Found;
        for(auto Entry:FMCVFXActionSession::GetCatalogue()) {
            auto Item=MakeShared<FMCVFXActionCatalogueEntry>(MoveTemp(Entry));
            if(Item->ID==ActionID)Found=Item;Fresh.Add(Item);
        }
        if(!Found){Error=TEXT("Действие не зарегистрировано в общем каталоге Composer.");return false;}
        Stop();Catalogue=MoveTemp(Fresh);SelectAction(Found);
        RefreshCatalogueFilter();
        SyncCatalogueSelection(true);
        if(bUnsupported || !Session->IsContextCurrent()) {Error=Status;return false;}
        return true;
    }
    void SActionWorkspace::SelectAction(FCatalogItem Item)
    {
        if(!Item)return;Stop();SelectedAction=Item->ID;bUnsupported=!Item->bSupportsPreview&&!Item->bSupportsProfileOverride;bShowActionDetails=true;SelectedLayer.Invalidate();SelectedGroup.Invalidate();SelectedSFXLayers.Reset();GallerySelection=FAssetData();
        if(bUnsupported)
        {
            Session=MakeShared<FMCVFXActionSession>();SelectedStage=NAME_None;
            PreviewHost->SetContent(Label(TEXT("Предпросмотр для «")+Item->Label.ToString()+TEXT("» ещё не подключён."),14,Muted));
            GraphHost->SetContent(SNew(SBorder).Padding(24)[SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,15)[Label(Item->Label.ToString(),20)]
                +SVerticalBox::Slot().AutoHeight()[Label(Item->Description.ToString(),14)]
                +SVerticalBox::Slot().AutoHeight().Padding(0,20)[Label(TEXT("Запись каталога. Предпросмотр и редактирование пока недоступны."),12,Muted)]]);
            Status=TEXT("Каталог: ")+Item->Label.ToString()+TEXT(". Адаптер полного действия пока не подключён.");
        }
        else
        {
            if(Session->GetActionID()!=Item->ID||!Session->IsContextCurrent())
            {
                FString Error;if(!DocumentView->Select(Item->ID,Error))
                {
                    Fail(Error);bUnsupported=true;
                    PreviewHost->SetContent(Label(Error,14,Muted));GraphHost->SetContent(Label(TEXT("Сценарий не открыт. ")+Error,14,Muted));
                    RefreshDetails();return;
                }
                Session=DocumentView->GetDocument();
                if(Graph)Graph->BindDocument(Session);
            }
            const auto Stages=Session->GetActionStages();
            if(!Stages.Contains(SelectedStage))
            {
                SelectedStage=Stages.IsEmpty()?NAME_None:Stages[0];
                // Open the actual visible proc, rather than an empty Equipped
                // registration card that makes an authored passive look missing.
                for(FName Event:Stages)if(!Session->GetNativeSources(Event).IsEmpty()){SelectedStage=Event;break;}
                if(SelectedStage==TEXT("Equipped") || SelectedStage==TEXT("State"))
                    for(FName Event:Stages)if(Event!=TEXT("Equipped")&&Event!=TEXT("State")){SelectedStage=Event;break;}
            }
            if(!Session->GetScenarios().Contains(SelectedScenario))SelectedScenario=EMCPerkPreviewScenario::Target;
            SelectEventScenario();
            if(Session->CanPreview()){NativePreview=DocumentView->MakePreviewWidget();PreviewHost->SetContent(NativePreview.ToSharedRef());}
            else PreviewHost->SetContent(Label(Session->GetPreviewUnavailableReason(),14,Muted));
            GraphHost->SetContent(Graph.ToSharedRef());
            Graph->FrameActionOverview();
            Status=Session->CanEditProfile()?(Session->CanPreview()?TEXT("Перетащите эффект из галереи на карточку события. Он добавится новым слоем. «Применить» сохраняет результат в игру."):
                TEXT("Редактирование сохранённых VFX и исходников доступно. Контекстный предпросмотр ещё не подключён.")):Session->GetEditUnavailableReason();
        }
        if(bFollowActionScene)SelectedAudioScene=DefaultAudioScene();
        Refresh();
    }
    void SActionWorkspace::SelectEventScenario()
    {
        if(Session->GetScenarios().Contains(EMCPerkPreviewScenario::Acquire))
            SelectedScenario=(SelectedStage==TEXT("Equipped") || SelectedStage==TEXT("State"))
                ?EMCPerkPreviewScenario::Acquire:EMCPerkPreviewScenario::Target;
    }
    void SActionWorkspace::SelectLayer(FName Stage,FGuid ID)
    {SelectedStage=Stage;SelectedLayer=ID;SelectedGroup.Invalidate();bShowSFX=false;if(ID.IsValid()){
        if(const auto* R=Session->GetRecipe(Stage)){const int32 I=R->FindNode(ID);if(R->Layers.IsValidIndex(I))SelectedGroup=R->Layers[I].GroupID;}}
        SelectEventScenario();bShowActionDetails=false;RefreshSFXGallery();RefreshDetails();SyncCatalogueSelection();}
    void SActionWorkspace::SelectGallery(FGalleryItem Item)
    {
        if(!Item)return;Stop();GallerySelection=*Item;
        if(!CanEdit()){Status=TEXT("Выбран ")+WorkspaceEffectLabel(Item->AssetName.ToString())+TEXT(". ")+AuthoringUnavailableReason();return;}
        // Slate may signal deferred row selection immediately before OnDragDetected.
        // Audition only on the next tick; BeginGalleryDrag cancels this pending click.
        bPendingGalleryAudition=bLive&&CanEdit();
        Status=TEXT("Выбран ")+WorkspaceEffectLabel(Item->AssetName.ToString())+TEXT(". Клик — примерка, перетащите на событие или нажмите «Добавить».");
    }
    FReply SActionWorkspace::BeginGalleryDrag(const FAssetData& Asset)
    {
        const FText Unavailable=SMCSFXGallery::GetAssetUnavailableReason(Asset);
        if(!Unavailable.IsEmpty()){Status=Unavailable.ToString();return FReply::Handled();}
        Stop();if(Asset.IsInstanceOf(UNiagaraSystem::StaticClass()))GallerySelection=Asset;
        if(!CanEdit())
        {
            if(Asset.IsInstanceOf(USoundBase::StaticClass()))return FReply::Handled().BeginDragDrop(FAssetDragDropOp::New(Asset));
            Status=AuthoringUnavailableReason();return FReply::Handled();
        }
        Status=TEXT("Перетащите «")+WorkspaceEffectLabel(Asset.AssetName.ToString())+TEXT("» на карточку события. Будет добавлен новый слой; Esc — отмена.");
        return FReply::Handled().BeginDragDrop(FWorkspaceEffectDrag::New(Asset,Session->GetActionID(),Session->GetDraftToken()));
    }
    void SActionWorkspace::DropEffect(FName Stage,const FAssetData& Asset)
    {DropPresentation(Stage,Stage==SelectedStage?SelectedGroup:FGuid(),Asset);}
    void SActionWorkspace::DropPresentation(FName Stage,FGuid Group,const FAssetData& Asset)
    {
        if(!CanEdit())return;
        const FText Unavailable=SMCSFXGallery::GetAssetUnavailableReason(Asset);
        if(!Unavailable.IsEmpty()){Fail(Unavailable.ToString());return;}
        FString Error;FGuid ID;const bool Audio=Asset.IsInstanceOf(USoundBase::StaticClass());
        if(Audio)ID=Session->AddSFXLayer(Session->GetSFXDraftToken(),Stage,Cast<USoundBase>(Asset.GetAsset()),Group,Error);
        else ID=Session->AddSystemToGroup(Session->GetDraftToken(),Stage,Cast<UNiagaraSystem>(Asset.GetAsset()),Group,Error);
        if(!ID.IsValid()){Fail(Error);return;}Stop();SelectedStage=Stage;SelectedGroup=Group;bShowActionDetails=false;
        if(Audio){bShowSFX=true;SelectedLayer.Invalidate();SelectedSFXLayers.Add(Stage,ID);}
        else{bShowSFX=false;SelectedLayer=ID;GallerySelection=Asset;}
        SelectEventScenario();Refresh();Status=TEXT("Добавлен отдельный ")+FString(Audio?TEXT("SFX"):TEXT("VFX"))+TEXT("-слой. Ctrl+Z — отмена; Применить — сохранить в игру.");
    }
    void SActionWorkspace::SelectSound(FName Stage,FGuid ID)
    {
        SelectedStage=Stage;SelectedLayer.Invalidate();SelectedSFXLayers.Add(Stage,ID);bShowSceneAudio=false;bShowSFX=true;bShowActionDetails=false;SelectedGroup.Invalidate();
        if(const auto* Slot=Session->GetSFXSlot(Stage))if(const auto* L=Slot->Layers.FindByPredicate([ID](const auto& L){return L.ID==ID;}))SelectedGroup=L->GroupID;
        SelectEventScenario();RefreshSFXGallery();RefreshDetails();SyncCatalogueSelection();
    }
    void SActionWorkspace::SelectGroup(FName Stage,FGuid ID)
    {
        if(Stage.IsNone())return;SelectedStage=Stage;SelectedGroup=ID;SelectedLayer.Invalidate();SelectedSFXLayers.Remove(Stage);bShowActionDetails=false;
        SelectEventScenario();RefreshSFXGallery();RefreshDetails();SyncCatalogueSelection();
    }
    void SActionWorkspace::RefreshGallery(const TCHAR* Path)
    {
        Stop();GallerySelection=FAssetData();if(GalleryView)GalleryView->ClearSelection();
        if(Path)
        {FARFilter Filter;Filter.ClassPaths.Add(UNiagaraSystem::StaticClass()->GetClassPathName());Filter.bRecursiveClasses=true;Filter.PackagePaths.Add(FName(Path));Filter.bRecursivePaths=true;
            GalleryCatalog.Reset();FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssets(Filter,GalleryCatalog);
            GalleryCatalog.Sort([](const FAssetData& A,const FAssetData& B){return A.AssetName.LexicalLess(B.AssetName);});}
        GalleryItems.Reset();for(const auto& Asset:GalleryCatalog)if(GalleryQuery.IsEmpty()||WorkspaceEffectLabel(Asset.AssetName.ToString()).Contains(GalleryQuery)||Asset.AssetName.ToString().Contains(GalleryQuery)||Asset.PackageName.ToString().Contains(GalleryQuery))GalleryItems.Add(MakeShared<FAssetData>(Asset));
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
    void SActionWorkspace::Refresh()
    {
        SeenRevision=Session->GetRevision();
        SeenPlaybackRevision=Session->GetPlaybackRevision();
        const auto* Current=Session->GetRecipe(SelectedStage);
        if(SelectedLayer.IsValid()&&(!Current||Current->FindNode(SelectedLayer)==INDEX_NONE))SelectedLayer.Invalidate();
        if(SelectedGroup.IsValid()){const auto* G=Session->GetEventGroups(SelectedStage);if(!G||!G->Contains(SelectedGroup))SelectedGroup.Invalidate();}
        if(const FGuid Sound=SelectedSFXLayers.FindRef(SelectedStage);Sound.IsValid()){const auto* Slot=Session->GetSFXSlot(SelectedStage);
            if(!Slot||!Slot->Layers.ContainsByPredicate([Sound](const auto& L){return L.ID==Sound;}))SelectedSFXLayers.Remove(SelectedStage);}
        // Audio selection is independent; a VFX Undo must not clear a sound GUID.
        if(Graph)Graph->Rebuild();RefreshSFXGallery();RefreshDetails();SyncCatalogueSelection();
    }
    const FMCVFXCompositionLayer* SActionWorkspace::CurrentLayer()const
    {const auto* R=Session?Session->GetRecipe(SelectedStage):nullptr;const int32 I=R?R->FindNode(SelectedLayer):INDEX_NONE;return I!=INDEX_NONE?&R->Layers[I]:nullptr;}
    void SActionWorkspace::AddSelected()
    {
        if(!CanEdit()||!GallerySelection.IsValid())return;Stop();FString Error;
        auto* NS=Cast<UNiagaraSystem>(GallerySelection.GetAsset());const FGuid ID=Session->AddSystemToGroup(Session->GetDraftToken(),SelectedStage,NS,SelectedGroup,Error);
        if(!ID.IsValid()){Fail(Error);return;}SelectedLayer=ID;bShowActionDetails=false;Refresh();Status=TEXT("Слой добавлен в «")+Session->GetEventLabel(SelectedStage).ToString()+TEXT("». Это черновик; игра изменится только после «Применить в игре».");
    }
    void SActionWorkspace::NewCompound()
    {if(!CanEdit())return;Stop();FString Error;if(!Session->NewCompound(SelectedStage,Error)){Fail(Error);return;}SelectedLayer.Invalidate();bShowActionDetails=false;Refresh();Status=TEXT("Новая пустая композиция события. Добавляйте слои из галереи; Ctrl+Z вернёт предыдущий черновик.");}
    void SActionWorkspace::Apply()
    {
        if(bShowSceneAudio) {
            if(!SceneAudioSession||!SceneAudioSession->HasChanges())return;
            Stop();FString SceneError;
            const bool Saved=SceneAudioSession->Apply(SceneError);
            Status=Saved?SceneAudioSession->GetStatus():TEXT("Не применено: ")+SceneError;
            RefreshDetails();return;
        }
        if(!CanEdit()||!Session->IsRuntimeDirty())return;Stop();FString Error;bool OK=false;
        const auto Loaded=Session->GetLoadedPackPath();
        if(!Session->HasUnsavedPackChanges()&&!Loaded.IsNull())
        {if(const auto* Pack=Cast<UMCVFXActionPack>(Loaded.TryLoad()))OK=Session->ApplySavedPack(Pack,Error);else Error=TEXT("Сохранённый пак не найден. Откройте его заново.");}
        else OK=Session->ApplyAll(Error);
        Refresh();Status=OK?Session->GetStatus():TEXT("Не применено: ")+Error;
    }
    void SActionWorkspace::Play(bool Gallery)
    {
        if(!CanPlay())return;if(Gallery&&!CanEdit()){Fail(Session->GetEditUnavailableReason());return;}const bool WasPlaying=bPlaying;Stop();FString Error;bool OK=false;
        if(!PrepareSceneAudio(Error)){Fail(Error);return;}
        if(!WasPlaying)DocumentView->SetPreviewView(Gallery&&(SelectedStage==TEXT("Start")||SelectedStage==TEXT("End"))?1:0);
        if(Gallery&&GallerySelection.IsValid())OK=DocumentView->AuditionSystem(SelectedStage,Cast<UNiagaraSystem>(GallerySelection.GetAsset()),SelectedScenario,Error);
        else OK=DocumentView->Play(SelectedScenario,Error);
        if(!OK){if(const auto Runner=DocumentView->GetRunner())Runner->ClearSceneAudio();Fail(Error);return;}bPlaying=true;bObserved=false;bAudition=Gallery;RepeatAt=0;
        Status=Gallery?TEXT("Примерка: ")+WorkspaceEffectLabel(GallerySelection.AssetName.ToString())+TEXT(". Нажмите «Добавить», чтобы оставить этот слой."):
            CanEdit()?TEXT("Проигрывается черновик события. Игровой профиль не изменён."):TEXT("Выполняется реальный сценарий с родным представлением механики. Редактирование Niagara-слоёв ещё не подключено.");
    }
    void SActionWorkspace::Stop()
    {
        bPlaying=false;bObserved=false;bPaused=false;bAudition=false;bSFXAudition=false;bPendingGalleryAudition=false;RepeatAt=0;
        if(SFXGallery)SFXGallery->StopSourcePreview();
        if(const auto Runner=DocumentView->GetRunner())Runner->ClearSceneAudio();
        if(DocumentView)DocumentView->Stop();
    }
    bool SActionWorkspace::TickWorkspace(float)
    {
        if(SceneAudioSession&&SeenSceneAudioRevision!=SceneAudioSession->GetRevision()) {
            SeenSceneAudioRevision=SceneAudioSession->GetRevision();
            if(bPlaying||bPaused)Stop();
            if(bShowSceneAudio)RefreshDetails();
        }
        if(Session&&SeenRevision!=Session->GetRevision()){if(SeenPlaybackRevision!=Session->GetPlaybackRevision())Stop();Status=Session->GetStatus();Refresh();}
        if(bPendingGalleryAudition)
        {
            bPendingGalleryAudition=false;const auto Tab=OwnerTab.Pin();const auto Window=FSlateApplication::Get().FindWidgetWindow(AsShared());
            if(!FSlateApplication::Get().IsDragDropping()&&Tab&&Tab->IsForeground()&&Window&&Window->IsVisible()&&!Window->IsWindowMinimized())Play(true);
        }
        if((bPlaying||bPaused)&&!DocumentView->OwnsPreview()){Stop();return true;}
        if(!bPlaying&&!bPaused)return true;
        const auto Tab=OwnerTab.Pin();const auto Window=FSlateApplication::Get().FindWidgetWindow(AsShared());
        if(!Tab||!Tab->IsForeground()||!Window||!Window->IsVisible()||Window->IsWindowMinimized()||!CanPlay()){Stop();return true;}
        if(bPaused)return true;const auto Runner=DocumentView->GetRunner();if(!Runner){Stop();return true;}
        if(Runner->IsRunning()){bObserved=true;RepeatAt=0;return true;}
        const auto& Events=Runner->GetEvents();
        const bool Complete=Events.ContainsByPredicate([](const FMCPerkPreviewEvent& E){return E.Stage==TEXT("ActionFinished");});
        const bool SafetyStop=Events.ContainsByPredicate([](const FMCPerkPreviewEvent& E){return E.Stage==TEXT("PreviewTailSafetyLimit");});
        if(!Complete || SafetyStop){const FString EndStatus=Runner->GetStatus();Stop();Status=TEXT("Предпросмотр остановлен: ")+EndStatus;return true;}
        if(Session->GetScenarios().Contains(EMCPerkPreviewScenario::Acquire)
            && !Events.ContainsByPredicate([this](const FMCPerkPreviewEvent& E){return E.Stage==SelectedStage;}))
            Status=TEXT("Механика завершилась, но событие «")+Session->GetEventLabel(SelectedStage).ToString()
                +TEXT("» в этом запуске не произошло. Повторите сценарий; условия и шанс не подменяются.");
        if(!bObserved)return true;if(!bLoop){Runner->ClearSceneAudio();bPlaying=false;RepeatAt=0;return true;}
        const double Now=FPlatformTime::Seconds();if(RepeatAt==0){RepeatAt=Now+.6;return true;}if(Now>=RepeatAt){if(bSFXAudition)AuditionSound(SFXAuditionAsset);else Play(bAudition);}return true;
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
    void SActionWorkspace::CachePackDisplayName(const UMCVFXActionPack* Pack)
    {
        if(!Pack)return;FString Name=Pack->Label.IsEmpty()?TEXT("Без названия"):Pack->Label.ToString();
        if(!Pack->VersionLabel.IsEmpty())Name+=TEXT(" · ")+Pack->VersionLabel;
        Name+=TEXT(" · #")+Pack->PackID.ToString(EGuidFormats::Digits).Left(8);
        PackDisplayNames.Add(FSoftObjectPath(Pack),MoveTemp(Name));
    }
    FString SActionWorkspace::PackDisplayName(const FSoftObjectPath& Path)const
    {
        if(Path.IsNull())return TEXT("не открыт");if(const auto* Name=PackDisplayNames.Find(Path))return *Name;
        // ResolveObject never loads from disk. Normal save/open caches labels once.
        if(const auto* Pack=Cast<UMCVFXActionPack>(Path.ResolveObject()))
        {
            FString Name=Pack->Label.IsEmpty()?TEXT("Без названия"):Pack->Label.ToString();
            if(!Pack->VersionLabel.IsEmpty())Name+=TEXT(" · ")+Pack->VersionLabel;
            return Name+TEXT(" · #")+Pack->PackID.ToString(EGuidFormats::Digits).Left(8);
        }
        // Full package path remains distinguishable even after an unloaded version is GC'd.
        return Path.ToString();
    }
    void SActionWorkspace::SavePack()
    {
        if(!CanEdit())return;FString Error;const FString Name=PackLabel.TrimStartAndEnd().IsEmpty()?ActionTitle():PackLabel.TrimStartAndEnd();
        auto* Pack=Session->SavePackNew(FText::FromString(Name),Error);if(!Pack){Fail(Error);return;}
        CachePackDisplayName(Pack);Refresh();Status=TEXT("Сохранена версия «")+PackDisplayName(FSoftObjectPath(Pack))+TEXT("». Игровое назначение не менялось; Применить — отдельное действие.");
    }
    void SActionWorkspace::LoadPackAsset(const FAssetData& Asset,bool bCompare)
    {
        auto* Pack=Cast<UMCVFXActionPack>(Asset.GetAsset());if(!Pack){Fail(TEXT("Выберите сохранённый пак Composer."));return;}
        if(Pack->SourceID!=Session->GetActionID()){Fail(TEXT("Пак относится к другому действию. Сначала выберите его в списке слева."));return;}
        if(!ConfirmDraftTransition(TEXT("Открытие другой версии пака")))return;
        FString Error;Stop();if(!Session->LoadPack(Pack,Error,true)){Fail(Error);return;}
        CachePackDisplayName(Pack);SelectedLayer.Invalidate();SelectedGroup.Invalidate();SelectedSFXLayers.Reset();PackLabel=Pack->Label.ToString();bShowActionDetails=true;FSlateApplication::Get().DismissAllMenus();Refresh();if(Graph)Graph->FrameSelected();
        Status=FString(bCompare?TEXT("A/B · "):TEXT("Открыта версия · "))+PackDisplayName(FSoftObjectPath(Pack))+TEXT(". Игровой профиль не изменён. Разброс игровых RNG не фиксируется.");
        if(bCompare&&CanPlay())Play(false);
    }
    TSharedRef<SWidget> SActionWorkspace::PackMenu()
    {
        FARFilter Filter;Filter.ClassPaths.Add(UMCVFXActionPack::StaticClass()->GetClassPathName());Filter.bRecursiveClasses=true;
        Filter.TagsAndValues.Add(TEXT("SourceID"),Session->GetActionID().ToString());
        TArray<FAssetData> Assets;FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssets(Filter,Assets);
        Assets.Sort([](const FAssetData& A,const FAssetData& B){FString AV,BV;A.GetTagValue(TEXT("VersionLabel"),AV);B.GetTagValue(TEXT("VersionLabel"),BV);return AV==BV?A.PackageName.LexicalLess(B.PackageName):AV>BV;});
        const auto Rows=SNew(SScrollBox);const TWeakPtr<SActionWorkspace> Weak=SharedThis(this);
        // Read registry metadata only. Opening the chooser must not load every
        // saved profile, Niagara dependency or editable master in a large pack.
        auto Rebuild=[Rows,Assets,Weak](const FText& Query)
        {
            Rows->ClearChildren();int32 Matches=0;
            for(const FAssetData& Asset:Assets)
            {
                FText Label;FString Version,ID;Asset.GetTagValue(TEXT("Label"),Label);Asset.GetTagValue(TEXT("VersionLabel"),Version);Asset.GetTagValue(TEXT("PackID"),ID);
                const FString Name=Label.IsEmpty()?Asset.AssetName.ToString():Label.ToString();
                const FString Search=Name+TEXT(" ")+Version+TEXT(" ")+ID+TEXT(" ")+Asset.PackageName.ToString();
                if(!Query.IsEmpty()&&!Search.Contains(Query.ToString(),ESearchCase::IgnoreCase))continue;
                ++Matches;FGuid Guid;const FString ShortID=FGuid::Parse(ID,Guid)?Guid.ToString(EGuidFormats::Digits).Left(8):ID.Left(8);
                const FString Detail=Version+(ShortID.IsEmpty()?TEXT(""):TEXT(" · #")+ShortID);
                Rows->AddSlot().Padding(3)[SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(12,9))
                    .ToolTipText(FText::FromString(Asset.GetSoftObjectPath().ToString()))
                    .OnClicked_Lambda([Weak,Asset]{if(const auto W=Weak.Pin())W->LoadPackAsset(Asset);return FReply::Handled();})
                    [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Label.IsEmpty()?FText::FromString(Name):Label).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),12))]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)[SNew(STextBlock).Text(FText::FromString(Detail)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10)).ColorAndOpacity(Muted)]]];
            }
            if(!Matches)Rows->AddSlot().Padding(12)[SNew(STextBlock).Text(FText::FromString(Assets.IsEmpty()?TEXT("Сохранённых версий пока нет. Задайте название и сохраните первую."):TEXT("Версий с таким названием не найдено."))).AutoWrapText(true).ColorAndOpacity(Muted)];
        };
        Rebuild(FText::GetEmpty());
        return SNew(SBox).WidthOverride(620).HeightOverride(430)[SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(9)[Label(TEXT("Сохранённые паки этого действия · открытие меняет только черновик"),12,WorkspaceMint)]
            +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(SSearchBox).HintText(FText::FromString(TEXT("Найти версию по названию…"))).OnTextChanged_Lambda(Rebuild)]
            +SVerticalBox::Slot().FillHeight(1)[Rows]];
    }
    TSharedRef<SWidget> SActionWorkspace::CompareMenu()
    {
        auto Rows=SNew(SVerticalBox);Rows->AddSlot().AutoHeight().Padding(6)[Label(TEXT("Сравнение сохранённых версий"),13,WorkspaceMint)];
        Rows->AddSlot().AutoHeight().Padding(6)[Label(TEXT("Откройте пак, запомните его как A или B. Переключение не назначает эффекты в игре."),11,Muted)];
        auto Remember=[this](bool A){if(Session->GetLoadedPackPath().IsNull()||Session->HasUnsavedPackChanges()){Status=TEXT("Сначала сохраните и откройте версию пака. Несохранённый черновик не участвует в A/B.");return;}
            (A?CompareA:CompareB)=Session->GetLoadedPackPath();Status=TEXT("Версия ")+FString(A?TEXT("A"):TEXT("B"))+TEXT(" запомнена: ")+PackDisplayName(Session->GetLoadedPackPath());FSlateApplication::Get().DismissAllMenus();};
        Rows->AddSlot().AutoHeight().Padding(3)[Command(TEXT("Текущий сохранённый пак → A"),[Remember]{Remember(true);})];
        Rows->AddSlot().AutoHeight().Padding(3)[Command(TEXT("Текущий сохранённый пак → B"),[Remember]{Remember(false);})];
        auto Choice=[this,&Rows](const FSoftObjectPath& Path,const FString& Slot){Rows->AddSlot().AutoHeight().Padding(3)[SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(10,8))
            .IsEnabled(!Path.IsNull()).OnClicked_Lambda([this,Path]{if(auto* P=Path.TryLoad())LoadPackAsset(FAssetData(P),true);return FReply::Handled();})
            [Label(TEXT("▶ ")+Slot+TEXT(" · ")+(Path.IsNull()?TEXT("не выбрано"):PackDisplayName(Path)),11)]];};
        Choice(CompareA,TEXT("A"));Choice(CompareB,TEXT("B"));return SNew(SBox).WidthOverride(430)[Rows];
    }
    void SActionWorkspace::OpenCompound()
    {
        FOpenAssetDialogConfig Config;Config.DialogTitleOverride=LOCTEXT("ImportRecipe","Открыть композицию для выбранного события");Config.DefaultPath=TEXT("/Game/Tests/VFXPractice");
        Config.AssetClassNames.Add(UMCVFXRecipePreset::StaticClass()->GetClassPathName());Config.bAllowMultipleSelection=false;
        const auto Assets=FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().CreateModalOpenAssetDialog(Config);
        if(!Assets.IsEmpty())ImportCompound(Cast<UMCVFXRecipePreset>(Assets[0].GetAsset()));
    }

    FName SActionWorkspace::DefaultAudioScene() const
    {
        // Preview context only. The real controller still owns scene selection
        // in the game; Knight/Mage deliberately retain its NutEncounter key.
        const FString ID=SelectedAction.ToString();
        if(ID.StartsWith(TEXT("Hub."))||ID==TEXT("EditorOnly.FishingCatch"))return TEXT("Hub");
        if(ID.StartsWith(TEXT("Boss.Nut"))||ID.StartsWith(TEXT("Enemy.Nut"))||ID==TEXT("Event.NutRain.Entry")||ID==TEXT("Food.NutThrow"))
            return TEXT("NutEncounter");
        return TEXT("Gameplay");
    }
    void SActionWorkspace::SetAudioView(bool bScenes,bool bSounds)
    {
        Stop();bShowSceneAudio=bScenes;bShowSFX=bSounds;bPendingGalleryAudition=false;
        if(!bScenes)bShowActionDetails=false;
        if(bFollowActionScene)SelectedAudioScene=DefaultAudioScene();
        RefreshSFXGallery();RefreshDetails();
    }
    bool SActionWorkspace::PrepareSceneAudio(FString& Error)
    {
        Error.Reset();
        if(!bUseSceneAudio||!SceneAudioSession||!SceneAudioSession->GetDraft()) {DocumentView->ClearSceneAudio();return true;}
        if(bFollowActionScene)SelectedAudioScene=DefaultAudioScene();
        return DocumentView->SetSceneAudio(SceneAudioSession->GetDraft(),SelectedAudioScene,bMusicAudible?1.f:0.f,bAmbienceAudible?1.f:0.f,Error);
    }
    void SActionWorkspace::PlayWithSceneAudio()
    {
        if(!CanPlay()){Fail(Session?Session->GetPreviewUnavailableReason():TEXT("Выберите настоящее действие слева."));return;}
        FString Error;
        if(!DocumentView->SetPreviewAudioEnabled(true,Error)){Fail(Error);return;}
        bUseSceneAudio=true;Play(false);
    }
    TSharedRef<SWidget> SActionWorkspace::SceneAudioPanel(EMCComposerSceneAudioPanelSection Section)
    {
        const TWeakPtr<SActionWorkspace> Weak=SharedThis(this);
        return SNew(SMCComposerSceneAudioPanel).Session(SceneAudioSession).Section(Section)
            .Scene_Lambda([Weak]{const auto W=Weak.Pin();return W?W->SelectedAudioScene:NAME_None;})
            .CanPreview_Lambda([Weak]{const auto W=Weak.Pin();return W&&W->CanPlay();})
            .IsActive_Lambda([Weak]{const auto W=Weak.Pin();return W&&W->bShowSceneAudio;})
            .OnSceneSelected_Lambda([Weak](FName Scene){if(const auto W=Weak.Pin()){
                W->Stop();W->SelectedAudioScene=Scene;W->bFollowActionScene=false;W->RefreshDetails();}})
            .OnStop_Lambda([Weak]{if(const auto W=Weak.Pin())W->Stop();})
            .OnPlay_Lambda([Weak]{if(const auto W=Weak.Pin())W->PlayWithSceneAudio();})
            .OnChanged_Lambda([Weak]{if(const auto W=Weak.Pin()){
                W->Stop();W->SeenSceneAudioRevision=W->SceneAudioSession->GetRevision();
                W->Status=W->SceneAudioSession->GetStatus();W->RefreshDetails();}});
    }
    void SActionWorkspace::AuditionSound(const FAssetData& Asset)
    {
        if(!CanEdit()||!Asset.IsValid())return;
        const FText Unavailable=SMCSFXGallery::GetAssetUnavailableReason(Asset);
        if(!Unavailable.IsEmpty()){Fail(Unavailable.ToString());return;}
        const auto Capability=Session->GetSFXCapability(SelectedStage);
        if(!Capability.bCanAuditionInContext){Status=Capability.UnavailableReason;return;}
        auto* Sound=Cast<USoundBase>(Asset.GetAsset());if(!Sound){Fail(TEXT("Выберите звуковой ресурс."));return;}
        Stop();FString Error;
        if(!PrepareSceneAudio(Error)){Fail(Error);return;}
        if(!DocumentView->AuditionSound(SelectedStage,Sound,SelectedScenario,Error)){
            if(const auto Runner=DocumentView->GetRunner())Runner->ClearSceneAudio();Fail(Error);return;
        }
        SFXAuditionAsset=Asset;bPlaying=true;bObserved=false;bSFXAudition=true;RepeatAt=0;
        Status=TEXT("Примерка звука в настоящем действии: ")+Asset.AssetName.ToString()+TEXT(". Черновик и игровой профиль не изменены.");
    }
    void SActionWorkspace::RefreshSFXGallery()
    {
        if(!SFXGalleryHost||(!bShowSFX&&!SFXGallery))return;
        FMCSFXLibraryContext Context;Context.EventLabel=Session->GetEventLabel(SelectedStage);Context.bCanAdd=!bShowSceneAudio&&CanEdit()&&!SelectedStage.IsNone();
        const auto Capability=Session->GetSFXCapability(SelectedStage);Context.bCanAudition=Context.bCanAdd&&Capability.bCanAuditionInContext&&DocumentView->IsPreviewAudioEnabled();
        Context.AuditionReason=FText::FromString(DocumentView->IsPreviewAudioEnabled()?Capability.UnavailableReason:TEXT("Включите звук предпросмотра перед примеркой."));
        const FName Source=Session->GetActionID(),Event=SelectedStage;const FGuid Token=Session->GetSFXDraftToken(),Group=SelectedGroup;
        const TWeakPtr<SActionWorkspace> Weak=SharedThis(this);
        Context.Add=[Weak,Source,Event,Token,Group](const FAssetData& Asset)
        {
            const auto W=Weak.Pin();if(!W||W->bShowSceneAudio||!W->CanEdit()||W->Session->GetActionID()!=Source||W->Session->GetSFXDraftToken()!=Token)return;
            const FText Unavailable=SMCSFXGallery::GetAssetUnavailableReason(Asset);
            if(!Unavailable.IsEmpty()){W->Fail(Unavailable.ToString());return;}
            auto* Sound=Cast<USoundBase>(Asset.GetAsset());if(!Sound){W->Fail(TEXT("Выбранный ресурс не является SoundBase."));return;}
            FString Error;const FGuid ID=W->Session->AddSFXLayer(Token,Event,Sound,Group,Error);
            if(!ID.IsValid()){W->Fail(Error);return;}
            W->Stop();W->SelectedStage=Event;W->SelectedGroup=Group;W->bShowSFX=true;W->SelectedLayer.Invalidate();W->SelectedSFXLayers.Add(Event,ID);W->bShowActionDetails=false;W->Refresh();W->Status=TEXT("Звуковой слой добавлен в черновик события. Применение в игре выполняется отдельно.");
        };
        Context.BeginDrag=[Weak](const FAssetData& Asset){const auto W=Weak.Pin();return W?W->BeginGalleryDrag(Asset):FReply::Unhandled();};
        Context.AuditionInAction=[Weak,Source,Event,Token](const FAssetData& Asset){if(const auto W=Weak.Pin();W&&!W->bShowSceneAudio&&W->Session->GetActionID()==Source&&W->Session->GetSFXDraftToken()==Token&&W->SelectedStage==Event)W->AuditionSound(Asset);};
        Context.OpenSource=[](const FAssetData& Asset){if(auto* Sound=Cast<USoundBase>(Asset.GetAsset());Sound&&GEditor)if(auto* Editors=GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())Editors->OpenEditorForAsset(Sound);};
        Context.BeforeSourcePlay=[Weak]{if(const auto W=Weak.Pin())W->Stop();};
        if(!SFXGallery){SFXGallery=SNew(SMCSFXLibrary).Context(MoveTemp(Context));SFXGalleryHost->SetContent(SFXGallery.ToSharedRef());}
        else SFXGallery->SetContext(MoveTemp(Context));
    }
    TSharedRef<SWidget> SActionWorkspace::SFXEventPanel(FName Event)
    {
        FMCSFXPanelContext Context;Context.Scope.SourceID=Session->GetActionID();Context.Scope.Event=Event;Context.Scope.DraftToken=Session->GetSFXDraftToken();
        Context.EventLabel=Session->GetEventLabel(Event);Context.bCanEdit=CanEdit();
        if(const auto* Slot=Session->GetSFXSlot(Event))Context.Slot=*Slot;
        if(const auto* Selected=SelectedSFXLayers.Find(Event))Context.SelectedLayer=*Selected;
        const auto Capability=Session->GetSFXCapability(Event);Context.bCanSuppressOriginal=Capability.bCanReplaceNativeAudio;Context.bCanAudition=Capability.bCanAuditionInContext&&DocumentView->IsPreviewAudioEnabled();
        if(const auto* Groups=Session->GetEventGroups(Event)){Context.bGroupEnabled=Groups->IsEnabled(SelectedGroup);Context.bGroupMuted=Groups->IsAudioMuted(SelectedGroup);}
        if(Capability.bOriginalAudioConfirmedSilent)Context.OriginalReason=FText::FromString(TEXT("У этого события нет исходного звука: проверены реальный источник и сохранённые анимации. Можно использовать собственные звуковые слои или выключить звук события."));
        else if(!Capability.bOriginalAudioHookMapped)Context.OriginalReason=FText::FromString(Capability.UnavailableReason);
        Context.AuditionReason=FText::FromString(Capability.UnavailableReason);
        for(const auto& Original:Capability.Originals){FMCSFXOriginalSourceView View;View.ID=Original.ID;View.NativeEvent=Original.NativeEvent;View.GroupID=Original.GroupID;
            View.Label=Original.Label;View.Asset=Original.Sound;View.SourceOwner=Original.SourceOwner;View.SourceProperty=Original.SourceProperty;View.bRuntimeHookMapped=Original.bRuntimeHookMapped;
            View.Detail=FText::FromString(Original.SourceOwner+TEXT(" · ")+Original.SourceProperty);Context.Originals.Add(MoveTemp(View));}
        const TWeakPtr<SActionWorkspace> Weak=SharedThis(this);
        auto Current=[Weak](FMCSFXEditorScope Scope){const auto W=Weak.Pin();return W&&!W->bShowSceneAudio&&W->CanEdit()&&W->Session->GetActionID()==Scope.SourceID&&W->Session->GetSFXDraftToken()==Scope.DraftToken?W:TSharedPtr<SActionWorkspace>();};
        Context.SetMode=[Current](FMCSFXEditorScope Scope,EMCEventSFXMode Mode,FString& Error){const auto W=Current(Scope);if(!W){Error=TEXT("Контекст звукового черновика изменился.");return false;}return W->Session->SetSFXMode(Scope.DraftToken,Scope.Event,Mode,Error);};
        Context.ReadLayer=[Current](FMCSFXEditorScope Scope,FGuid ID,FMCEventSFXLayer& Out,FString& Error){const auto W=Current(Scope);const auto* Slot=W?W->Session->GetSFXSlot(Scope.Event):nullptr;
            const auto* Layer=Slot?Slot->Layers.FindByPredicate([ID](const auto& L){return L.ID==ID;}):nullptr;
            if(!Layer){Error=TEXT("Звуковой слой изменён или удалён. Выберите его заново.");return false;}Out=*Layer;return true;};
        Context.SetLayer=[Current](FMCSFXEditorScope Scope,FGuid ID,const FMCEventSFXLayer& Layer,FString& Error){const auto W=Current(Scope);if(!W){Error=TEXT("Контекст звукового черновика изменился.");return false;}return W->Session->SetSFXLayer(Scope.DraftToken,Scope.Event,ID,Layer,Error);};
        Context.RemoveLayer=[Current](FMCSFXEditorScope Scope,FGuid ID,FString& Error){const auto W=Current(Scope);if(!W){Error=TEXT("Контекст звукового черновика изменился.");return false;}if(!W->Session->RemoveSFXLayer(Scope.DraftToken,Scope.Event,ID,Error))return false;W->SelectedSFXLayers.Remove(Scope.Event);return true;};
        Context.DuplicateLayer=[Current](FMCSFXEditorScope Scope,FGuid ID,FString& Error){const auto W=Current(Scope);if(!W){Error=TEXT("Контекст звукового черновика изменился.");return false;}const FGuid New=W->Session->DuplicateSFXLayer(Scope.DraftToken,Scope.Event,ID,Error);if(!New.IsValid())return false;W->SelectedSFXLayers.Add(Scope.Event,New);return true;};
        Context.SelectLayer=[Weak,Event](FGuid ID){if(const auto W=Weak.Pin()){W->SelectSound(Event,ID);}};
        Context.Refresh=[Weak]{if(const auto W=Weak.Pin()){W->Stop();W->Refresh();}};
        Context.ShowError=[Weak](const FString& Error){if(const auto W=Weak.Pin())W->Fail(Error);};
        Context.OpenSource=[Weak](const FMCSFXOriginalSourceView& Source)
        {
            if(!Source.Asset.IsNull()){FMCVFXNativeSourcePart Part;Part.Asset=Source.Asset;OpenNativeSource(Part);return;}
            if(Source.SourceOwner.StartsWith(TEXT("/Game/"))||Source.SourceOwner.StartsWith(TEXT("/Script/")))if(auto* Owner=FSoftObjectPath(Source.SourceOwner).TryLoad()){
                if(const auto* Class=Cast<UClass>(Owner)){FSourceCodeNavigation::NavigateToClass(Class);return;}
                if(GEditor)if(auto* Editors=GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()){Editors->OpenEditorForAsset(Owner);return;}}
            if(const auto W=Weak.Pin())W->Status=TEXT("У источника нет открываемой ссылки: ")+Source.SourceOwner+TEXT(" · ")+Source.SourceProperty;
        };
        auto Rows=SNew(SVerticalBox);
        Rows->AddSlot().AutoHeight().Padding(0,0,0,8)[SNew(SCheckBox).IsChecked_Lambda([this]{return DocumentView->IsPreviewAudioEnabled()?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State){FString Error;if(!DocumentView->SetPreviewAudioEnabled(State==ECheckBoxState::Checked,Error))Fail(Error);RefreshSFXGallery();RefreshDetails();})
            [Label(TEXT("Звук в предпросмотре · включить явно"),12)]];
        Rows->AddSlot().AutoHeight().Padding(0,0,0,8)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(DocumentView->GetPreviewAudioStatus());}).AutoWrapText(true).ColorAndOpacity(Muted)];
        Rows->AddSlot().AutoHeight()[SNew(SMCEventSFXPanel).Context(MoveTemp(Context))];return Rows;
    }

    TSharedRef<SWidget> SActionWorkspace::NativePartsPanel(bool bPersistent,FName Event)
    {
        FMCNativeArtEditorContext Context;Context.SourceID=Session->GetActionID();Context.DraftInstanceID=Session->GetNativeDraftToken();
        Context.Scope.bPersistent=bPersistent;Context.Scope.Event=bPersistent?NAME_None:Event;
        Context.Parts=MCNativeArtParts::BuildViews(Session->GetNativeDraft(),Context.Scope);Context.bCanEdit=CanEdit();
        const TWeakPtr<SActionWorkspace> Weak=SharedThis(this);
        Context.Commit=[Weak](const FMCNativeArtEdit& Edit,FString& Error)
        {
            const auto W=Weak.Pin();
            if(!W||!W->CanEdit()||W->Session->GetActionID()!=Edit.SourceID||W->Session->GetNativeDraftToken()!=Edit.DraftInstanceID)
            {Error=TEXT("Открыто другое действие или новая версия черновика. Выберите настройку заново.");return false;}
            FMCVFXNativePartSettings Part;
            if(!MCNativeArtParts::ApplyEdit(Edit,W->Session->GetNativeDraft(),Part,Error)
                || !W->Session->SetNativePart(Edit.DraftInstanceID,Edit.Scope.bPersistent,Edit.Scope.Event,Edit.PartID,Part,Error))return false;
            W->Stop();W->Refresh();W->Status=TEXT("Родная основа изменена в черновике. «Играть» — проверить, «Применить» — сохранить в игру.");return true;
        };
        const FName SourceID=Context.SourceID;const FGuid Token=Context.DraftInstanceID;const auto Scope=Context.Scope;
        Context.ResetPart=[Weak,SourceID,Token,Scope](FName Part,FString& Error)
        {
            const auto W=Weak.Pin();
            if(!W||!W->CanEdit()||W->Session->GetActionID()!=SourceID||W->Session->GetNativeDraftToken()!=Token)
            {Error=TEXT("Контекст родной основы изменился. Выберите часть заново.");return false;}
            if(!W->Session->ResetNativePart(Token,Scope.bPersistent,Scope.Event,Part,Error))return false;
            W->Stop();W->Refresh();W->Status=TEXT("Исходный вид этой части восстановлен в черновике.");return true;
        };
        Context.OpenSource=[](const FMCNativeArtResourceView& Source){FMCVFXNativeSourcePart Part;Part.Asset=Source.Asset;Part.SourceFile=Source.GeometrySource;OpenNativeSource(Part);};
        Context.ShowStatus=[Weak](const FString& Error){if(const auto W=Weak.Pin())W->Fail(Error);};
        return SNew(SMCNativeArtPartsPanel).Context(MoveTemp(Context));
    }

    void SActionWorkspace::RefreshDetails()
    {
        if(!Details)return;Details->ClearChildren();auto Add=[this](const TSharedRef<SWidget>& W){Details->AddSlot().AutoHeight().Padding(9,5)[W];};
        if(bShowSceneAudio) {Add(SceneAudioPanel(EMCComposerSceneAudioPanelSection::Layers));return;}
        if(bUnsupported)
        {
            for(const auto& Entry:Catalogue)if(Entry->ID==SelectedAction)
            {Add(Label(Entry->Label.ToString(),17));Add(Label(Entry->Description.ToString(),13));Add(Label(Entry->UnsupportedReason.IsEmpty()?TEXT("Адаптер предпросмотра пока не подключён. Запуск и применение недоступны."):Entry->UnsupportedReason,12,Muted));break;}
            return;
        }
        if(!Session||!Session->IsContextCurrent())
        {Add(Label(TEXT("Контекст предпросмотра изменился"),16));Add(Label(AuthoringUnavailableReason(),13,Muted));return;}
        if(!bShowSFX&&!bShowActionDetails&&!SelectedStage.IsNone())Add(VFXReviewCheckbox(SelectedAction,SelectedStage,false));
        if(!Session->CanEditProfile())
        {
            Add(Label(bShowActionDetails?ActionTitle():Session->GetEventLabel(SelectedStage).ToString(),17));
            Add(Label(TEXT("Нажмите ▶ Играть, чтобы выполнить настоящий игровой сценарий с текущим родным представлением."),13));
            Add(Label(Session->GetEditUnavailableReason(),12,Muted));
            Add(Label(TEXT("Слои Niagara для этого события ещё не подключены к Composer. Случайные пассивки показывают эффект при фактическом срабатывании по текущему шансу."),12,Muted));
            return;
        }
        if(SelectedGroup.IsValid()&&!SelectedLayer.IsValid()&&!SelectedSFXLayers.FindRef(SelectedStage).IsValid()&&!bShowActionDetails)
        {
            const auto* Groups=Session->GetEventGroups(SelectedStage);const auto* Group=Groups?Groups->Find(SelectedGroup):nullptr;
            if(Group){Add(Label(Group->Name.ToString(),18));Add(Label(TEXT("Независимая группа VFX + SFX. Несколько включённых групп работают одновременно."),12,Muted));
                Add(Label(TEXT("Заголовок — перемещение · галочка — вся группа · ♪ — только звук · Solo — только предпросмотр."),12));
                Add(Command(TEXT("Удалить группу и её слои…"),[this]{
                    if(FMessageDialog::Open(EAppMsgType::YesNo,FText::FromString(TEXT("Удалить эту группу вместе с её VFX и SFX? Ctrl+Z отменяет удаление.")))!=EAppReturnType::Yes)return;
                    FString Error;if(!Session->RemoveEventGroup(Session->GetDraftToken(),SelectedStage,SelectedGroup,EMCComposerGroupRemoval::DeleteChildren,Error)){Fail(Error);return;}
                    Stop();SelectedGroup.Invalidate();Refresh();}));return;}
        }
        if(!bShowActionDetails&&(SelectedLayer.IsValid()||SelectedSFXLayers.FindRef(SelectedStage).IsValid()))
        {
            const FName Event=SelectedStage;const FGuid Layer=bShowSFX?SelectedSFXLayers.FindRef(Event):SelectedLayer,Token=Session->GetDraftToken();
            const EMCComposerLayerLane Lane=bShowSFX?EMCComposerLayerLane::SFX:EMCComposerLayerLane::VFX;
            Add(SNew(SComboButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(10,7)).OnGetMenuContent_Lambda([this,Event,Layer,Token,Lane]()->TSharedRef<SWidget>{
                auto Items=SNew(SVerticalBox);auto AddGroup=[this,&Items,Event,Layer,Token,Lane](FGuid Group,const FString& Name){Items->AddSlot().AutoHeight().Padding(2)[Command(Name,[this,Event,Layer,Token,Lane,Group]{
                    FString Error;if(!Session->AssignLayerToGroup(Token,Event,Lane,Layer,Group,Error)){Fail(Error);return;}Stop();SelectedGroup=Group;FSlateApplication::Get().DismissAllMenus();Refresh();})];};
                AddGroup(FGuid(),TEXT("Без группы"));if(const auto* Groups=Session->GetEventGroups(Event))for(const auto& Group:Groups->Groups)AddGroup(Group.ID,Group.Name.ToString());return Items;
            }).ButtonContent()[Label(TEXT("Переместить слой в группу…"),11)]);
        }
        if(bShowSFX&&!bShowActionDetails)
        {
            if(SelectedStage.IsNone())Add(Label(TEXT("Выберите настоящее событие действия, чтобы настроить его звук."),13,Muted));
            else Add(SFXEventPanel(SelectedStage));
            return;
        }
        if(bShowActionDetails)
        {
            Add(Label(ActionTitle(),18));Add(Label(TEXT("Сценарий запускает настоящее игровое действие. Карточки показывают его события; редактируемые события поддерживают несколько слоёв эффекта."),13));
            const FSoftObjectPath Owner=Session->GetCanonicalOwner();
            if(bShowTechnicalSources&&!Owner.IsNull())
            {
                Add(Label(TEXT("Данные профиля: ")+Owner.GetAssetName(),12,Muted));
                Add(Command(TEXT("Открыть данные профиля"),[this,Owner]
                {
                    if(auto* Asset=Owner.TryLoad())
                    {
                        if(GEditor)if(auto* Editors=GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())Editors->OpenEditorForAsset(Asset);
                    }
                    else Status=TEXT("Сохранённый источник профиля не найден: ")+Owner.ToString();
                }));
            }
            if(!Session->CanEditProfile())Add(Label(Session->GetEditUnavailableReason(),12,Muted));
            if(bShowTechnicalSources)if(const auto* Native=Session->GetNativeDraft();Native&&!Native->PersistentParts.IsEmpty())Add(NativePartsPanel(true,NAME_None));
            for(FName Stage:Session->GetActionStages()) {
                const int32 Native=Session->GetNativeSources(Stage).Num();
                const int32 Added=Session->GetLayerCount(Stage);
                const FString Counts=bShowTechnicalSources&&Native>0?FString::Printf(TEXT(" · источников основы %d · добавлено Niagara %d"),Native,Added)
                    :Added>0?FString::Printf(TEXT(" · добавленные Niagara %d/%d"),EnabledLayers(Session->GetRecipe(Stage)),Added):TEXT(" · без добавленных слоёв");
                const auto* Sounds=Session->GetSFXSlot(Stage);const int32 SoundCount=Sounds?Sounds->Layers.Num():0;
                Add(Command(Session->GetEventLabel(Stage).ToString()+Counts+FString::Printf(TEXT(" · добавлено звуков %d"),SoundCount),[this,Stage]{SelectLayer(Stage,FGuid());}));
            }
            Add(Label(TEXT("«Применить в игре» сохраняет композиции, готовые эффекты и профиль. До этой кнопки правки остаются черновиками."),12,Muted));return;
        }
        const auto* Layer=CurrentLayer();
        Add(Label(Session->GetEventLabel(SelectedStage).ToString(),16,WorkspaceStageColor(SelectedStage)));
        if(!Layer)
        {
            if(const auto* Groups=Session->GetEventGroups(SelectedStage);Groups&&!Groups->Groups.IsEmpty())
            {
                Add(Heading(TEXT("Группы события")));
                for(const auto& Group:Groups->Groups)
                {
                    const FName Event=SelectedStage;const FGuid ID=Group.ID;
                    Add(Command(TEXT("Показать группу · ")+Group.Name.ToString(),[this,Event,ID]{SelectGroup(Event,ID);if(Graph)Graph->FrameSelected();}));
                }
            }
            const auto Native=Session->GetNativeSources(SelectedStage);
            if(!bShowTechnicalSources&&HasActiveNativeDecorativeBasis(*Session,SelectedStage,Native))Add(Label(TEXT("Для события включены родные акценты вне редактируемых NS-слоёв. Ресурсы и режим — в «Техническая основа»."),11,Muted));
            if(bShowTechnicalSources&&!Native.IsEmpty()) {
                Add(Heading(TEXT("Родная основа · ")+FString::FromInt(Native.Num())+TEXT(" источников")));
                Add(Label(NativeModeLabel(Session->GetPreviewSlotMode(SelectedStage),NativeKeepsOriginal(*Session,SelectedStage)),13,WorkspaceMint));
                if(const auto* Draft=Session->GetNativeDraft();Draft&&Draft->Events.Contains(SelectedStage))Add(NativePartsPanel(false,SelectedStage));
                for(const auto& Part:Native)Add(SNew(SWorkspaceNativeSourceButton).Source(Part));
                Add(Label(TEXT("Двойной клик — открыть настоящий Niagara, материал или исходную геометрию. Это общие ресурсы игры; здесь они показаны отдельно от новых слоёв."),11,Muted));
                if(const auto* Draft=Session->GetNativeDraft();Draft&&Draft->Events.Contains(SelectedStage))
                {
                    const FName Stage=SelectedStage;const FGuid Token=Session->GetNativeDraftToken();
                    Add(SNew(SCheckBox).IsChecked(NativeKeepsOriginal(*Session,Stage)?ECheckBoxState::Checked:ECheckBoxState::Unchecked)
                        .OnCheckStateChanged_Lambda([this,Stage,Token](ECheckBoxState State){FString Error;if(!Session->SetNativeKeepWithComposition(Token,Stage,State==ECheckBoxState::Checked,Error)){Fail(Error);return;}Stop();Refresh();})
                        [Label(TEXT("Дополнять родную основу слоями Niagara"),12)]);
                    Add(Label(TEXT("Включено — родные акценты и новые слои вместе. Выключено — новые слои заменяют акценты; игровая форма и опасные зоны сохраняются."),11,Muted));
                }
                Add(Heading(TEXT("Добавленные Niagara")));
                if(!Session->GetNativeDraft())Add(Label(TEXT("Добавленные слои заменяют родные акценты этого события. Редактирование родной основы для этого renderer ещё не подключено."),11,Muted));
            }
            Add(Label(FString::Printf(TEXT("Niagara-слои: %d включено из %d"),EnabledLayers(Session->GetRecipe(SelectedStage)),Session->GetLayerCount(SelectedStage)),13));
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
        Root->SetStringField(TEXT("selected_layer"),SelectedLayer.ToString(EGuidFormats::DigitsWithHyphens));
        Root->SetStringField(TEXT("selected_group"),SelectedGroup.ToString(EGuidFormats::DigitsWithHyphens));
        Root->SetStringField(TEXT("compare_a"),CompareA.ToString());Root->SetStringField(TEXT("compare_b"),CompareB.ToString());Root->SetStringField(TEXT("gallery_selection"),GallerySelection.GetSoftObjectPath().ToString());
        Root->SetBoolField(TEXT("adapter_supported"),!bUnsupported);Root->SetBoolField(TEXT("loop"),bLoop);Root->SetBoolField(TEXT("live_gallery"),bLive);Root->SetBoolField(TEXT("playing"),bPlaying);Root->SetBoolField(TEXT("paused"),bPaused);
        const auto Drag=FSlateApplication::Get().GetDragDroppingContent();Root->SetBoolField(TEXT("gallery_drag_active"),Drag&&Drag->IsOfType<FWorkspaceEffectDrag>());Root->SetBoolField(TEXT("audition_pending"),bPendingGalleryAudition);
        Root->SetNumberField(TEXT("gallery_count"),GalleryItems.Num());Root->SetStringField(TEXT("status"),Status);
        Root->SetStringField(TEXT("gallery_channel"),bShowSFX?TEXT("SFX"):TEXT("VFX"));Root->SetBoolField(TEXT("sfx_audition"),bSFXAudition);
        Root->SetStringField(TEXT("audio_view"),bShowSceneAudio?TEXT("Scene"):bShowSFX?TEXT("SFX"):TEXT("VFX"));
        Root->SetStringField(TEXT("scene_audio_key"),SelectedAudioScene.ToString());
        Root->SetBoolField(TEXT("scene_audio_preview_enabled"),bUseSceneAudio);
        Root->SetBoolField(TEXT("scene_audio_follow_action"),bFollowActionScene);
        Root->SetBoolField(TEXT("scene_music_audible"),bMusicAudible);Root->SetBoolField(TEXT("scene_ambience_audible"),bAmbienceAudible);
        if(SceneAudioSession) {
            Root->SetBoolField(TEXT("scene_audio_dirty"),SceneAudioSession->HasChanges());
            Root->SetBoolField(TEXT("scene_audio_external_changes"),SceneAudioSession->HasExternalChanges());
            Root->SetNumberField(TEXT("scene_audio_revision"),double(SceneAudioSession->GetRevision()));
            Root->SetStringField(TEXT("scene_audio_owner"),GetPathNameSafe(SceneAudioSession->GetSource()));
            if(const auto* Scene=SceneAudioSession->GetScene(SelectedAudioScene)) {
                Root->SetStringField(TEXT("scene_music"),Scene->Music.Sound.ToSoftObjectPath().ToString());
                Root->SetStringField(TEXT("scene_ambience"),Scene->Ambience.Sound.ToSoftObjectPath().ToString());
            }
        }
        TSharedPtr<FJsonObject> ViewModel;
        if(DocumentView&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(DocumentView->GetSnapshotJson()),ViewModel)&&ViewModel)Root->SetObjectField(TEXT("view"),ViewModel);
        Root->SetStringField(TEXT("ui_layout"),TEXT("shared_shell_v1"));
        Root->SetStringField(TEXT("catalogue_navigation"),TEXT("category-owner-action-event"));
        Root->SetStringField(TEXT("transport_location"),WorkspaceShell&&WorkspaceShell->IsGraphOnly()?TEXT("graph-footer"):TEXT("below-preview"));
        if(WorkspaceShell){
            Root->SetStringField(TEXT("ui_density"),WorkspaceShell->GetDensityName());
            Root->SetBoolField(TEXT("ui_compact"),WorkspaceShell->IsCompact());
            Root->SetStringField(TEXT("ui_library_mode"),WorkspaceShell->GetLibraryModeName());
        }
        Root->SetBoolField(TEXT("technical_sources_shown"),bShowTechnicalSources);
        Root->SetBoolField(TEXT("preview_audio_enabled"),DocumentView->IsPreviewAudioEnabled());Root->SetStringField(TEXT("preview_audio_status"),DocumentView->GetPreviewAudioStatus());
        if(SFXGallery){Root->SetNumberField(TEXT("sfx_gallery_total"),SFXGallery->GetTotalCount());Root->SetNumberField(TEXT("sfx_gallery_matched"),SFXGallery->GetMatchedCount());
            Root->SetStringField(TEXT("sfx_gallery_query"),SFXGallery->GetQuery());Root->SetStringField(TEXT("sfx_gallery_selected"),SFXGallery->GetSelectedAssetPath().ToString());
            Root->SetStringField(TEXT("sfx_library_folder"),SFXGallery->GetSelectedFolder());Root->SetBoolField(TEXT("sfx_source_playing"),SFXGallery->IsSourcePreviewPlaying());
            Root->SetNumberField(TEXT("sfx_source_waveform_peaks"),SFXGallery->GetSourceWaveformPeakCount());}
        Root->SetNumberField(TEXT("catalogue_total"),Catalogue.Num());Root->SetNumberField(TEXT("catalogue_matched"),FilteredCatalogue.Num());
        Root->SetStringField(TEXT("catalogue_query"),CatalogueFilter.Query);
        Root->SetStringField(TEXT("catalogue_section"),MCVFXCatalogueNavigation::Label(CatalogueFilter.Section).ToString());
        Root->SetBoolField(TEXT("selected_action_visible"),IsSelectedActionVisible());
        TArray<TSharedPtr<FJsonValue>> VisibleIDs;for(const auto& Item:FilteredCatalogue)VisibleIDs.Add(MakeShared<FJsonValueString>(Item->ID.ToString()));
        Root->SetArrayField(TEXT("catalogue_visible_ids"),VisibleIDs);
        TArray<TSharedPtr<FJsonValue>> TreeNodes;
        TFunction<void(FCatalogueTreeItem)> AddNode=[&](FCatalogueTreeItem Node){
            auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("key"),Node->Key);Data->SetStringField(TEXT("label"),Node->Label.ToString());
            Data->SetStringField(TEXT("kind"),Node->Kind==EVFXCatalogueNode::Section?TEXT("category"):Node->Kind==EVFXCatalogueNode::Owner?TEXT("owner"):Node->Kind==EVFXCatalogueNode::Action?TEXT("action"):TEXT("event"));
            Data->SetStringField(TEXT("action"),Node->Entry?Node->Entry->ID.ToString():FString());Data->SetStringField(TEXT("event"),Node->Event.IsNone()?FString():Node->Event.ToString());
            Data->SetBoolField(TEXT("expanded"),CatalogueView&&CatalogueView->IsItemExpanded(Node));TreeNodes.Add(MakeShared<FJsonValueObject>(Data));
            for(const auto& Child:Node->Children)AddNode(Child);
        };
        for(const auto& RootNode:CatalogueRoots)AddNode(RootNode);
        Root->SetArrayField(TEXT("catalogue_tree"),TreeNodes);
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
bool MCVFXActionWorkspace::SelectActionForReview(FName ActionID,FString& Error)
{
    const auto Panel=ActiveWorkspace.Pin();if(!Panel){Error=TEXT("Сначала откройте VFX Composer.");return false;}
    return Panel->SelectForReview(ActionID,Error);
}
bool MCVFXActionWorkspace::SelectAudioTargetForReview(FName ActionID,FName Event,FGuid Layer,FString& Error)
{
    const auto Panel=ActiveWorkspace.Pin();if(!Panel){Error=TEXT("Сначала откройте VFX Composer.");return false;}
    const auto Document=MCComposerDocuments::Acquire(ActionID,Error);if(!Document)return false;
    if(!Document->GetActionStages().Contains(Event)){Error=TEXT("Событие не принадлежит выбранному действию.");return false;}
    if(Layer.IsValid())
    {
        const auto* Slot=Document->GetSFXSlot(Event);
        if(!Slot||!Slot->Layers.ContainsByPredicate([Layer](const auto& Item){return Item.ID==Layer;}))
        {Error=TEXT("Звуковой слой больше не существует. Выберите актуальный слой.");return false;}
    }
    if(!Panel->SelectForReview(ActionID,Error))return false;
    Panel->SelectAudioForReview(Event,Layer);return true;
}
bool MCVFXActionWorkspace::PlayForReview(EMCPerkPreviewScenario Scenario,bool Loop,FString& Error)
{
    const auto Panel=ActiveWorkspace.Pin();if(!Panel){Error=TEXT("Сначала откройте VFX Composer.");return false;}
    return Panel->PlayForReview(Scenario,Loop,Error);
}
void MCVFXActionWorkspace::StopForReview(){if(const auto Panel=ActiveWorkspace.Pin())Panel->StopForReview();}
bool MCVFXActionWorkspace::ResizeForReview(FVector2D Size)
{
    if(!FMath::IsFinite(Size.X)||!FMath::IsFinite(Size.Y)||Size.X<1000||Size.Y<650||Size.X>3840||Size.Y>2160)return false;
    const auto Tab=FGlobalTabmanager::Get()->FindExistingLiveTab(WorkspaceTab);if(!Tab)return false;
    const auto Window=Tab->GetParentWindow();
    if(!Window||Window==FGlobalTabmanager::Get()->GetRootWindow()||Window->GetType()!=EWindowType::Normal)return false;
    Window->Restore();Window->Resize(Size);Window->BringToFront(true);return true;
}
bool MCVFXActionWorkspace::SetPreviewViewForReview(int32 Preset)
{
    if(Preset<0||Preset>2)return false;
    const auto Panel=ActiveWorkspace.Pin();if(!Panel)return false;
    Panel->SetCameraForReview(Preset);return true;
}
bool MCVFXActionWorkspace::ImportEventRecipe(FName ActionID,FName Stage,UMCVFXComposition* Source,FString& Error)
{
    const auto Panel=ActiveWorkspace.Pin();if(!Panel){Error=TEXT("Сначала откройте VFX Composer.");return false;}
    return Panel->ImportSavedEvent(ActionID,Stage,Source,Error);
}

#undef LOCTEXT_NAMESPACE
