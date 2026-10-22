#include "MCMainMenuPreview.h"

#include "MCGameplayUIFactory.h"
#include "MCGameplayUIPreview.h"
#include "MCMainMenuWidget.h"
#include "Camera/CameraActor.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Framework/Docking/SDockingNode.h"
#include "Framework/Docking/SDockingTabStack.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/PlatformTime.h"
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#include "LevelSequenceActor.h"
#include "LevelSequence.h"
#include "Modules/ModuleManager.h"
#include "SLevelViewport.h"
#include "ShowFlags.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UnrealEdMisc.h"
#include "UnrealClient.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditorUtils.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "MCMainMenuPreview"

namespace MCMainMenuPreviewPrivate
{
    const FName TabName(TEXT("MCMainMenuPreview"));
    const FName CameraTag(TEXT("MC_MenuV2_Camera"));
    const FString MenuMap(TEXT("/Game/Maps/L_MainMenu"));
    const FText RealtimeOwner=LOCTEXT("RealtimeOwner","Main Menu Composition Preview");
    const TArray<FString> PageNames={TEXT("Main"),TEXT("Host"),TEXT("Join"),TEXT("SettingsGraphics"),
        TEXT("SettingsAudio"),TEXT("SettingsLanguage"),TEXT("SettingsControls"),TEXT("SettingsVoice"),
        TEXT("Credits"),TEXT("Exit"),TEXT("Lobby"),TEXT("Leave"),TEXT("VideoConfirm"),TEXT("Pause"),TEXT("Connection")};
    const TArray<FString> Cultures={TEXT("ru"),TEXT("en"),TEXT("de"),TEXT("fr"),TEXT("es"),TEXT("pt-BR"),
        TEXT("zh-Hans"),TEXT("zh-Hant"),TEXT("ja"),TEXT("ko")};

    UWorld* EditorWorld()
    {
        return GEditor?GEditor->GetEditorWorldContext().World():nullptr;
    }

    bool CanPreview()
    {
        const auto* World=EditorWorld();
        return GEditor && !GEditor->IsPlaySessionInProgress() && World && World->WorldType==EWorldType::Editor
            && World->GetOutermost()->GetName()==MenuMap;
    }

    UWidgetBlueprint* MainMenuBlueprint()
    {
        UClass* Class=UMCGameplayUIFactory::ResolveClass(TEXT("MainMenu"),UMCMainMenuWidget::StaticClass());
        return Class?Cast<UWidgetBlueprint>(Class->ClassGeneratedBy):nullptr;
    }

    void OpenAsset(UObject* Asset)
    {
        if(GEditor && Asset)
            if(auto* Editors=GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()) Editors->OpenEditorForAsset(Asset);
    }

    struct FViewportSnapshot
    {
        FVector Location=FVector::ZeroVector;
        FRotator Rotation=FRotator::ZeroRotator;
        float ViewFOV=90.f;
        float StoredFOV=90.f;
        bool bGameView=false;
        bool bAllowCinematicControl=true;
        bool bLockedCameraView=true;
        bool bDisableInput=false;
        bool bShowWidget=true;
        FEngineShowFlags ShowFlags{ESFIM_Editor};
        FEngineShowFlags LastShowFlags{ESFIM_Editor};
        FLevelViewportActorLock ActorLock;
        FLevelViewportActorLock CinematicLock;
    };

    class FPreviewSession
    {
    public:
        FName State=TEXT("Main");
        FString Culture=TEXT("ru");
        FText Status=LOCTEXT("Idle","Open L_MainMenu, then start the overlay. Edit the actual MainMenu widget in Designer.");
        TWeakPtr<SDockTab> Tab;

        void RequestReadableControlWidth()
        {
            // Wait for the newly docked tab to receive its first Slate geometry.
            ControlWidthAt=FPlatformTime::Seconds()+0.2;
            ControlWidthAttempts=3;
        }

        void RegisterHooks()
        {
            Ticker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this,&FPreviewSession::Tick));
            PropertyHandle=FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this,&FPreviewSession::PropertyChanged);
            UndoHandle=FEditorDelegates::PostUndoRedo.AddRaw(this,&FPreviewSession::UndoRedo);
            PIEHandle=FEditorDelegates::PreBeginPIE.AddRaw(this,&FPreviewSession::BeforePIE);
            auto& LevelEditor=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
            MapHandle=LevelEditor.OnMapChanged().AddRaw(this,&FPreviewSession::MapChanged);
        }

        void UnregisterHooks()
        {
            Stop();
            UnbindBlueprint();
            FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
            FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyHandle);
            FEditorDelegates::PostUndoRedo.Remove(UndoHandle);
            FEditorDelegates::PreBeginPIE.Remove(PIEHandle);
            if(auto* LevelEditor=FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
                LevelEditor->OnMapChanged().Remove(MapHandle);
        }

        bool IsActive() const {return Viewport.IsValid() && Preview.IsValid();}
        UMCMainMenuWidget* GetWidget() const {return Preview.Get();}

        void Start()
        {
            if(!CanPreview()) {
                Status=LOCTEXT("WrongWorld","Preview requires the open L_MainMenu editor map and no PIE. No map is loaded automatically.");
                return;
            }
            // Keep the original snapshot even after a temporarily failed widget rebuild.
            if(Viewport.IsValid()) {Rebuild();if(!Preview.IsValid()) Stop(false);return;}
            auto& LevelEditor=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
            auto Target=LevelEditor.GetFirstActiveLevelViewport();
            if(!Target) {Status=LOCTEXT("NoViewport","No active level viewport. Open a perspective viewport first.");return;}
            ACameraActor* Camera=nullptr;
            for(TActorIterator<ACameraActor> It(EditorWorld());It;++It)
                if(It->ActorHasTag(CameraTag)) {Camera=*It;break;}
            if(!Camera) {Status=LOCTEXT("NoCamera","The saved main-menu camera was not found. The scene was not changed.");return;}
            auto& Client=Target->GetLevelViewportClient();
            if(!Client.IsPerspective()) {Status=LOCTEXT("PerspectiveRequired","Use a perspective level viewport for the composition preview.");return;}
            Snapshot.Location=Client.GetViewLocation();Snapshot.Rotation=Client.GetViewRotation();
            Snapshot.ViewFOV=Client.ViewFOV;Snapshot.StoredFOV=Client.FOVAngle;
            Snapshot.bGameView=Client.IsInGameView();Snapshot.ShowFlags=Client.EngineShowFlags;
            Snapshot.LastShowFlags=Client.LastEngineShowFlags;
            Snapshot.bAllowCinematicControl=Client.AllowsCinematicControl();
            Snapshot.bLockedCameraView=Client.bLockedCameraView;
            Snapshot.bDisableInput=Client.bDisableInput;
            Snapshot.bShowWidget=Client.GetShowWidget();
            Snapshot.ActorLock=Client.GetActorLock();Snapshot.CinematicLock=Client.GetCinematicActorLock();
            SnapshotWorld=EditorWorld();
            PreviewCamera=Camera;
            Viewport=Target;
            // Cinematic lock reads the saved lens/postprocess but never pilots/moves the camera actor.
            Client.SetAllowCinematicControl(true);Client.bLockedCameraView=true;
            // The composed preview is display-only; block editor navigation/selection/gizmos too.
            Client.bDisableInput=true;
            Client.SetCinematicActorLock(Camera);
            Client.SetGameView(true);
            Client.AddRealtimeOverride(true,RealtimeOwner);
            // Merely assigning a cinematic lock does not move a background/non-ticking viewport.
            // Finish any previous editor camera transition, then apply the actual lens/PP immediately.
            Client.GetViewTransform().TransitionToLocation(Camera->GetActorLocation(),TWeakPtr<SWidget>(),true);
            Client.SetViewRotation(Camera->GetActorRotation());
            SynchronizeViewport();
            Overlay=SNew(SBox).Visibility(EVisibility::HitTestInvisible)
                [SNew(SDPIScaler).DPIScale_Lambda([this]{return DPIScale();})[SAssignNew(WidgetHost,SBox)]];
            Target->AddOverlayWidget(Overlay.ToSharedRef(),100);
            Rebuild();
            if(!Preview.IsValid()) Stop(false);
            Client.Invalidate();
        }

        void Stop(bool bSetStatus=true)
        {
            RebuildAt=0.;
            ReleaseWidget();
            if(auto Target=Viewport.Pin()) {
                if(Overlay) Target->RemoveOverlayWidget(Overlay.ToSharedRef());
                auto& Client=Target->GetLevelViewportClient();
                Client.RemoveRealtimeOverride(RealtimeOwner,false);
                // A reused viewport may already belong to the newly loaded map.
                // Never resurrect a camera lock or view transform from its previous world.
                if(SnapshotWorld.IsValid() && Client.GetWorld()==SnapshotWorld.Get() && EditorWorld()==SnapshotWorld.Get()) {
                    Client.SetCinematicActorLock(Snapshot.CinematicLock);Client.SetActorLock(Snapshot.ActorLock);
                    Client.SetAllowCinematicControl(Snapshot.bAllowCinematicControl);
                    Client.bLockedCameraView=Snapshot.bLockedCameraView;
                    Client.bDisableInput=Snapshot.bDisableInput;
                    Client.SetGameView(Snapshot.bGameView);Client.EngineShowFlags=Snapshot.ShowFlags;
                    Client.LastEngineShowFlags=Snapshot.LastShowFlags;
                    Client.ShowWidget(Snapshot.bShowWidget);
                    Client.ViewFOV=Snapshot.ViewFOV;Client.FOVAngle=Snapshot.StoredFOV;
                    Client.SetViewLocation(Snapshot.Location);Client.SetViewRotation(Snapshot.Rotation);
                }
                Client.Invalidate();
            }
            Viewport.Reset();SnapshotWorld.Reset();PreviewCamera.Reset();Overlay.Reset();WidgetHost.Reset();UnbindBlueprint();
            if(bSetStatus) Status=LOCTEXT("Stopped","Overlay stopped. The previous viewport state is restored; no asset was saved.");
        }

        void Rebuild()
        {
            if(bRebuilding || !Viewport.IsValid()) return;
            if(!CanPreview()) {Stop();return;}
            TGuardValue<bool> Guard(bRebuilding,true);
            RebuildAt=0.;
            UWidgetBlueprint* Blueprint=MainMenuBlueprint();
            if(!Blueprint || !Blueprint->GeneratedClass) {
                Status=LOCTEXT("NoBlueprint","The current MainMenu role has no usable Widget Blueprint. Compile its errors in Designer first.");
                return;
            }
            if(BoundBlueprint.Get()!=Blueprint) {
                UnbindBlueprint();BoundBlueprint=Blueprint;
                ChangedHandle=Blueprint->OnChanged().AddRaw(this,&FPreviewSession::BlueprintChanged);
                CompiledHandle=Blueprint->OnCompiled().AddRaw(this,&FPreviewSession::BlueprintChanged);
            }
            ReleaseWidget();
            // This is the Designer's own latest-tree path: unsaved layout edits need no runtime world or CDO write.
            const FWidgetBlueprintEditorUtils::FCreateWidgetFromBlueprintParams Params{
                EWidgetDesignFlags::Designing|EWidgetDesignFlags::ExecutePreConstruct,nullptr};
            auto* Created=Cast<UMCMainMenuWidget>(FWidgetBlueprintEditorUtils::CreateUserWidgetFromBlueprint(
                GetTransientPackage(),Blueprint,Params));
            if(!Created) {Status=LOCTEXT("CreateFailed","Could not construct the actual menu's Designer preview.");return;}
            Preview.Reset(Created);
            Preview->PreviewState=State;Preview->PreviewCulture=Culture;
            WidgetHost->SetContent(Preview->TakeWidget());
            ApplyState();
            Status=FText::Format(LOCTEXT("Active","LIVE EDITOR PREVIEW · {0}\nLayout/theme changes refresh automatically. Buttons are display-only; use the page selector here. No game is running."),
                FText::FromString(Blueprint->GetPathName()));
        }

        void ApplyState()
        {
            if(!Preview.IsValid()) return;
            Preview->PreviewState=State;Preview->PreviewCulture=Culture;
            if(State==TEXT("SettingsGraphics")) Preview->PreviewSettingsTab=0;
            Preview->ResetEditorPreview();Preview->ApplyEditorPreview(0.f);
            MCGameplayUIPreview::SynchronizeFixtureTree(Preview.Get());
            Preview->InvalidateLayoutAndVolatility();
            if(auto Target=Viewport.Pin()) Target->GetLevelViewportClient().Invalidate();
        }

        void OpenScene()
        {
            if(!GEditor || GEditor->IsPlaySessionInProgress()) {
                Status=LOCTEXT("PIEPreserve","Stop your Play session yourself before opening the scene. Preview never ends PIE.");return;
            }
            if(CanPreview()) {Start();return;}
            Stop();
            // Explicit button only: Unreal's Save/Don't Save/Cancel dialog owns any dirty-map decision.
            if(!FEditorFileUtils::SaveDirtyPackages(true,true,false,false,false,true)) return;
            if(FEditorFileUtils::LoadMap(MenuMap,false,true)) Start();
        }

        void OpenAnimation()
        {
            if(CanPreview()) for(TActorIterator<ALevelSequenceActor> It(EditorWorld());It;++It)
                if(ULevelSequence* Sequence=It->GetSequence()) {OpenAsset(Sequence);return;}
            OpenAsset(LoadObject<UObject>(nullptr,TEXT("/Game/UI/MenuShowcase/AnatomyV2/LS_MainMenuCleaningV2.LS_MainMenuCleaningV2")));
        }

    private:
        void ReleaseWidget()
        {
            if(WidgetHost) WidgetHost->SetContent(SNullWidget::NullWidget);
            if(Preview.IsValid()) {
                FWidgetBlueprintEditorUtils::DestroyUserWidget(Preview.Get());Preview.Reset();
            }
        }
        void UnbindBlueprint()
        {
            if(auto* Blueprint=BoundBlueprint.Get()) {
                Blueprint->OnChanged().Remove(ChangedHandle);Blueprint->OnCompiled().Remove(CompiledHandle);
            }
            BoundBlueprint.Reset();
        }
        void QueueRefresh() {if(Viewport.IsValid() && !bRebuilding) RebuildAt=FPlatformTime::Seconds()+.15;}
        void BlueprintChanged(UBlueprint*) {QueueRefresh();}
        void UndoRedo() {QueueRefresh();}
        void PropertyChanged(UObject* Object,FPropertyChangedEvent&)
        {
            if(!Object || bRebuilding || !Viewport.IsValid()) return;
            auto* Blueprint=BoundBlueprint.Get();
            if(Object==UMCGameplayUIFactory::GetMasterTheme() ||
                (Blueprint && Object->GetOutermost()==Blueprint->GetOutermost())) QueueRefresh();
        }
        void BeforePIE(bool) {Stop();}
        void MapChanged(UWorld*,EMapChangeType Type)
        {
            // LevelEditor itself disables input during teardown. Do not undo that engine lock.
            if(Type==EMapChangeType::TearDownWorld) Snapshot.bDisableInput=true;
            if(Type!=EMapChangeType::SaveMap) Stop();
        }
        bool Tick(float)
        {
            if(ControlWidthAt>0. && FPlatformTime::Seconds()>=ControlWidthAt) {
                const bool bGeometryReady=EnsureReadableControlWidth();
                ControlWidthAt=(!bGeometryReady && --ControlWidthAttempts>0)?FPlatformTime::Seconds()+0.1:0.;
            }
            if(Viewport.IsValid() && !CanPreview()) Stop();
            if(Viewport.IsValid()) {
                SynchronizeViewport();
                if(Preview.IsValid()) Preview->RefreshPresentationGeometry();
            }
            if(RebuildAt>0. && FPlatformTime::Seconds()>=RebuildAt) Rebuild();
            return true;
        }
        bool EnsureReadableControlWidth()
        {
            const auto ControlTab=Tab.Pin();
            if(!ControlTab) return true;
            const float Width=ControlTab->GetCachedGeometry().GetLocalSize().X;
            if(Width<=0.f) return false;
            if(Width>=320.f) return true;
            TSharedPtr<SDockingNode> Column=ControlTab->GetParentDockTabStack();
            if(!Column) return true;
            auto Parent=Column->GetParentWidget();
            for(int32 Depth=0;Parent && Depth<64;++Depth,Parent=Parent->GetParentWidget()) {
                const FString Type=Parent->GetTypeAsString();
                if(Type!=TEXT("SDockingSplitter") && Type!=TEXT("SDockingArea")) continue;
                // Both types derive from the public SDockingNode; no private splitter API is used.
                const auto Node=StaticCastSharedPtr<SDockingNode>(Parent);
                const auto Layout=Node->GatherPersistentLayout();
                const auto Splitter=Layout?Layout->AsSplitter():nullptr;
                if(Splitter && Splitter->GetOrientation()==Orient_Horizontal) {
                    // Expand only the existing control column, once per explicit Open.
                    // Do not continuously override the user's subsequent manual resizing.
                    Column->SetSizeCoefficient(FMath::Max(Column->GetSizeCoefficient(),0.35f));
                    return true;
                }
                Column=Node;
            }
            return true;
        }
        void SynchronizeViewport()
        {
            auto Target=Viewport.Pin();
            if(!Target || !PreviewCamera.IsValid() || PreviewCamera->GetWorld()!=SnapshotWorld.Get()) return;
            auto& Client=Target->GetLevelViewportClient();
            Client.SetAllowCinematicControl(true);Client.bLockedCameraView=true;
            if(Client.GetCinematicActorLock().GetLockedActor()!=PreviewCamera.Get())
                Client.SetCinematicActorLock(PreviewCamera.Get());
            Client.UpdateViewForLockedActor(0.f);
            // SetGameView deliberately carries CompositeEditorPrimitives across modes.
            // The preview must explicitly suppress those editor-only icons/overlays.
            Client.EngineShowFlags.SetGame(true);
            Client.EngineShowFlags.SetCompositeEditorPrimitives(false);
            Client.EngineShowFlags.SetBillboardSprites(false);
            Client.EngineShowFlags.SetModeWidgets(false);
            Client.EngineShowFlags.SetSelection(false);
            Client.EngineShowFlags.SetSelectionOutline(false);
            Client.EngineShowFlags.SetGrid(false);
            Client.EngineShowFlags.SetBounds(false);
            Client.EngineShowFlags.SetPostProcessing(true);Client.EngineShowFlags.SetTemporalAA(true);
            Client.ShowWidget(false);
            Client.Invalidate();
        }
        float DPIScale() const
        {
            auto Target=Viewport.Pin();
            const auto* Surface=Target?Target->GetLevelViewportClient().Viewport:nullptr;
            return Surface?GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Surface->GetSizeXY()):1.f;
        }

        TWeakPtr<SLevelViewport> Viewport;
        TWeakObjectPtr<UWorld> SnapshotWorld;
        TWeakObjectPtr<ACameraActor> PreviewCamera;
        TSharedPtr<SWidget> Overlay;
        TSharedPtr<SBox> WidgetHost;
        TStrongObjectPtr<UMCMainMenuWidget> Preview;
        TWeakObjectPtr<UWidgetBlueprint> BoundBlueprint;
        FViewportSnapshot Snapshot;
        FTSTicker::FDelegateHandle Ticker;
        FDelegateHandle PropertyHandle,UndoHandle,PIEHandle,MapHandle,ChangedHandle,CompiledHandle;
        double RebuildAt=0.;
        double ControlWidthAt=0.;
        int32 ControlWidthAttempts=0;
        bool bRebuilding=false;
    };

    TUniquePtr<FPreviewSession> Session;

    class SMainMenuPreviewControls final : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SMainMenuPreviewControls) {} SLATE_END_ARGS()
        void Construct(const FArguments&)
        {
            for(const FString& Name:PageNames) States.Add(MakeShared<FString>(Name));
            for(const FString& Name:Cultures) Languages.Add(MakeShared<FString>(Name));
            auto Button=[](const FText& Label,TFunction<void()> Action) {
                return SNew(SButton).Text(Label).OnClicked_Lambda([Action]{Action();return FReply::Handled();});
            };
            ChildSlot
            [SNew(SScrollBox)+SScrollBox::Slot()
                [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().Padding(12)
                    [SNew(STextBlock).Text(LOCTEXT("Help","The actual saved menu scene and current MainMenu Widget Blueprint, together in the level viewport. Edit layout in Designer; this preview never runs gameplay or saves assets.")).AutoWrapText(true)]
                +SVerticalBox::Slot().AutoHeight().Padding(12,4)
                    [Button(LOCTEXT("OpenWidget","Open actual menu in Designer"),[]{OpenAsset(MainMenuBlueprint());})]
                +SVerticalBox::Slot().AutoHeight().Padding(12,4)
                    [Button(LOCTEXT("OpenScene","Open main-menu scene…"),[]{if(Session) Session->OpenScene();})]
                +SVerticalBox::Slot().AutoHeight().Padding(12,4)
                    [Button(LOCTEXT("OpenTheme","Open shared UI theme"),[]{MCGameplayUIPreview::OpenThemeEditor(UMCGameplayUIFactory::GetMasterTheme());})]
                +SVerticalBox::Slot().AutoHeight().Padding(12,4)
                    [Button(LOCTEXT("OpenAnimation","Open scene animation (Sequencer)"),[]{if(Session) Session->OpenAnimation();})]
                +SVerticalBox::Slot().AutoHeight().Padding(12,8)
                    [SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)
                        [Button(LOCTEXT("Start","Start overlay"),[]{if(Session) Session->Start();})]
                    +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)
                        [Button(LOCTEXT("Stop","Stop overlay"),[]{MCMainMenuPreview::Close();})]
                    +SHorizontalBox::Slot().FillWidth(1)
                        [Button(LOCTEXT("Refresh","Refresh"),[]{MCMainMenuPreview::Refresh();})]]
                +SVerticalBox::Slot().AutoHeight().Padding(12,6)
                    [SNew(STextBlock).Text(LOCTEXT("Page","Preview page / settings tab"))]
                +SVerticalBox::Slot().AutoHeight().Padding(12,0,12,6)
                    [SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&States)
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> Value){return SNew(STextBlock).Text(FText::FromString(*Value));})
                    .OnSelectionChanged_Lambda([](TSharedPtr<FString> Value,ESelectInfo::Type){if(Value && Session) MCMainMenuPreview::SetState(FName(**Value),Session->Culture);})
                        [SNew(STextBlock).Text_Lambda([]{return Session?FText::FromName(Session->State):FText::GetEmpty();})]]
                +SVerticalBox::Slot().AutoHeight().Padding(12,6)
                    [SNew(STextBlock).Text(LOCTEXT("Language","Preview language (does not change game/editor settings)"))]
                +SVerticalBox::Slot().AutoHeight().Padding(12,0,12,6)
                    [SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&Languages)
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> Value){return SNew(STextBlock).Text(FText::FromString(*Value));})
                    .OnSelectionChanged_Lambda([](TSharedPtr<FString> Value,ESelectInfo::Type){if(Value && Session) MCMainMenuPreview::SetState(Session->State,*Value);})
                        [SNew(STextBlock).Text_Lambda([]{return Session?FText::FromString(Session->Culture):FText::GetEmpty();})]]
                +SVerticalBox::Slot().AutoHeight().Padding(12)
                    [SNew(STextBlock).Text_Lambda([]{return Session?Session->Status:FText::GetEmpty();}).AutoWrapText(true)]
                ]
            ];
        }
    private:
        TArray<TSharedPtr<FString>> States,Languages;
    };

    TSharedRef<SDockTab> MakeControlTab()
    {
        auto Tab=SNew(SDockTab).TabRole(ETabRole::NomadTab)
            .Label(LOCTEXT("TabTitle","Main Menu Preview"))[SNew(SMainMenuPreviewControls)];
        Tab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda([](TSharedRef<SDockTab>){MCMainMenuPreview::Close();}));
        if(Session) Session->Tab=Tab;
        return Tab;
    }
}

namespace MCMainMenuPreview
{
using namespace MCMainMenuPreviewPrivate;

void Register()
{
    if(Session || IsRunningCommandlet()) return;
    Session=MakeUnique<FPreviewSession>();Session->RegisterHooks();
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName,FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
    {
        return MakeControlTab();
    }))
    .SetDisplayName(LOCTEXT("TabTitle","Main Menu Preview"))
    .SetTooltipText(LOCTEXT("TabTooltip","Preview the actual editable menu over its 3D scene without starting the game."))
    .SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
}

void Unregister()
{
    if(!Session) return;
    if(auto Tab=Session->Tab.Pin()) Tab->RequestCloseTab();
    Session->UnregisterHooks();Session.Reset();
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
}

void Open()
{
    Register();if(!Session) return;
    if(auto Tab=Session->Tab.Pin()) Tab->DrawAttention();
    else {
        auto& LevelEditor=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
        if(auto Manager=LevelEditor.GetLevelEditorTabManager()) {
            // Keep the composition unobstructed: default to the right-hand Details stack.
            Manager->TryInvokeTab(LevelEditorTabIds::LevelEditorSelectionDetails);
            Manager->InsertNewDocumentTab(LevelEditorTabIds::LevelEditorSelectionDetails,
                TabName,FTabManager::FLiveTabSearch(),MakeControlTab());
        }
        else FGlobalTabmanager::Get()->TryInvokeTab(TabName);
    }
    Session->RequestReadableControlWidth();
    Session->Start();
}

void Close() {if(Session) Session->Stop();}
void Refresh() {if(Session) Session->Rebuild();}
void SetState(FName State,FString Culture)
{
    Register();if(!Session) return;
    Session->State=PageNames.Contains(State.ToString())?State:FName(TEXT("Main"));
    Session->Culture=Cultures.Contains(Culture)?MoveTemp(Culture):FString(TEXT("ru"));
    Session->ApplyState();
}
UMCMainMenuWidget* GetPreviewWidget() {return Session?Session->GetWidget():nullptr;}
bool IsActive() {return Session && Session->IsActive();}
}

#undef LOCTEXT_NAMESPACE
