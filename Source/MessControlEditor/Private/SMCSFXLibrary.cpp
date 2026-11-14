#include "SMCSFXLibrary.h"
#include "SMCSFXSourceWaveform.h"

#include "MCComposerStyle.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Components/AudioComponent.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/CriticalSection.h"
#include "Internationalization/TextTransformer.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Templates/Atomic.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "MCSFXLibrary"

struct FMCSFXLibraryFolder
{
    FString Path, Name;
    int32 Count = 0;
    TArray<TSharedPtr<FMCSFXLibraryFolder>> Children;
};

// Registry delegates can run off-thread. Only this mailbox crosses that boundary.
struct FMCSFXLibraryRegistryState
{
    TAtomic<bool> bPending{false};
    FCriticalSection Mutex;
    FSoftObjectPath SelectedPath;
    void Select(const FSoftObjectPath& Path) { FScopeLock Lock(&Mutex); SelectedPath = Path; }
    FSoftObjectPath Selection() { FScopeLock Lock(&Mutex); return SelectedPath; }
    void Rename(const FSoftObjectPath& Before, const FSoftObjectPath& After)
    { FScopeLock Lock(&Mutex); if (SelectedPath == Before) SelectedPath = After; }
};

SMCSFXLibrary::~SMCSFXLibrary()
{
    StopSourcePreview();
    if (const auto* Module = FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry")))
        if (auto* Registry = Module->TryGet())
        {
            Registry->OnAssetAdded().Remove(AssetAdded);
            Registry->OnAssetRemoved().Remove(AssetRemoved);
            Registry->OnAssetUpdated().Remove(AssetUpdated);
            Registry->OnAssetUpdatedOnDisk().Remove(AssetUpdatedOnDisk);
            Registry->OnAssetRenamed().Remove(AssetRenamed);
            Registry->OnFilesLoaded().Remove(FilesLoaded);
        }
}

void SMCSFXLibrary::Construct(const FArguments& Args)
{
    using namespace MCComposerStyle;
    Context = Args._Context;
    SetCanTick(true);
    ExpandedFolders.Add(TEXT("/Game"));
    RegistryState = MakeShared<FMCSFXLibraryRegistryState, ESPMode::ThreadSafe>();
    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    const auto State = RegistryState;
    const auto Queue = [State](const FAssetData& Asset)
    { if (Asset.PackageName.ToString().StartsWith(TEXT("/Game/"))) State->bPending.Store(true); };
    AssetAdded = Registry.OnAssetAdded().AddLambda(Queue);
    AssetRemoved = Registry.OnAssetRemoved().AddLambda(Queue);
    AssetUpdated = Registry.OnAssetUpdated().AddLambda(Queue);
    AssetUpdatedOnDisk = Registry.OnAssetUpdatedOnDisk().AddLambda(Queue);
    AssetRenamed = Registry.OnAssetRenamed().AddLambda([State](const FAssetData& Asset, const FString& OldPath)
    {
        if (Asset.PackageName.ToString().StartsWith(TEXT("/Game/")) || OldPath.StartsWith(TEXT("/Game/")))
        { State->Rename(FSoftObjectPath(OldPath), Asset.GetSoftObjectPath()); State->bPending.Store(true); }
    });
    FilesLoaded = Registry.OnFilesLoaded().AddLambda([State] { State->bPending.Store(true); });
    RefreshAssets();

    const auto Toolbar = SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,12,0)
            [SNew(STextBlock).Text(LOCTEXT("Title", "БИБЛИОТЕКА ЗВУКОВ")).ColorAndOpacity(WorkspaceMint)]
        + SHorizontalBox::Slot().FillWidth(1).Padding(0,0,8,0)
            [SNew(SSearchBox).HintText(LOCTEXT("Search", "Имя, тип или путь…"))
                .ToolTipText(LOCTEXT("SearchScope", "Поиск в выбранной папке и всех её вложенных папках. Источники не загружаются."))
                .OnTextChanged_Lambda([this](const FText& Text) { Query=Text.ToString(); Filter(); })]
        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,4,0)
            [SNew(SComboButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(8,4))
                .OnGetMenuContent(this,&SMCSFXLibrary::SortMenu)
                .ButtonContent()[SNew(STextBlock).ColorAndOpacity(TextColor).Text_Lambda([this]
                    { return Sort==ESort::Name?LOCTEXT("Name", "Имя"):Sort==ESort::Duration?LOCTEXT("Duration", "Длительность"):LOCTEXT("Type", "Тип"); })]]
        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)
            [SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(8,4)).ForegroundColor(TextColor)
                .Text_Lambda([this] { return FText::FromString(bDescending?TEXT("↓"):TEXT("↑")); })
                .ToolTipText(LOCTEXT("Reverse", "Изменить направление сортировки. Неизвестная длительность всегда в конце."))
                .OnClicked_Lambda([this] { bDescending=!bDescending; Filter(); return FReply::Handled(); })]
        + SHorizontalBox::Slot().AutoWidth()
            [SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(9,4)).ForegroundColor(TextColor)
                .Text(LOCTEXT("Import", "Импорт…"))
                .ToolTipText(LOCTEXT("ImportHint", "Импорт файлов в выбранную /Game папку через стандартный диалог Unreal. Затем сохраните новые ассеты в Content Browser. Настройки cook не меняются."))
                .IsEnabled_Lambda([] { return GEditor && !GEditor->IsPlaySessionInProgress(); })
                .OnClicked_Lambda([this] { ImportIntoFolder(); return FReply::Handled(); })];

    const auto FileList = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(6,2)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0,0,5,0)[SNew(SBox).WidthOverride(28)[Label(TEXT("▶"),10,Muted)]]
                + SHorizontalBox::Slot().FillWidth(1)[Label(TEXT("ИМЯ / ПУТЬ"),9,Muted)]
                + SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(126)[Label(TEXT("ТИП"),9,Muted)]]
                + SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(100)[Label(TEXT("ДЛИТЕЛЬНОСТЬ"),9,Muted)]]]
        + SVerticalBox::Slot().FillHeight(1)
            [SAssignNew(List,SListView<FItem>).ListItemsSource(&Filtered).SelectionMode(ESelectionMode::Single)
                .OnGenerateRow(this,&SMCSFXLibrary::AssetRow)
                .OnSelectionChanged_Lambda([this](FItem Item,ESelectInfo::Type Origin) { if(Item && Origin!=ESelectInfo::Direct) SelectAsset(Item); })
                .OnMouseButtonDoubleClick_Lambda([this](FItem Item) { if(Item) { SelectAsset(Item); OpenSelectedSource(); } })]
        + SVerticalBox::Slot().AutoHeight()
            [SNew(STextBlock).Text(LOCTEXT("Empty", "Нет звуков. Выберите /Game или очистите поиск."))
                .Visibility_Lambda([this] { return Filtered.IsEmpty()?EVisibility::Visible:EVisibility::Collapsed; })
                .ColorAndOpacity(Muted).AutoWrapText(true)];

    const auto Browser = SNew(SSplitter)
        + SSplitter::Slot().Value(.15f).MinSize(160)
            [SNew(SBorder).BorderImage(&PanelBrush()).BorderBackgroundColor(GraphFill).Padding(3)
                [SAssignNew(Tree,STreeView<FFolder>).TreeItemsSource(&Roots).SelectionMode(ESelectionMode::Single)
                    .OnGenerateRow(this,&SMCSFXLibrary::FolderRow)
                    .OnGetChildren_Lambda([](FFolder Item,TArray<FFolder>& Children) { if(Item) Children=Item->Children; })
                    .OnExpansionChanged_Lambda([this](FFolder Item,bool Expanded)
                        { if(!bRefreshingTree && Item) { if(Expanded) ExpandedFolders.Add(Item->Path); else ExpandedFolders.Remove(Item->Path); } })
                    .OnSelectionChanged_Lambda([this](FFolder Item,ESelectInfo::Type Origin)
                        { if(Item && !bRefreshingTree && Origin!=ESelectInfo::Direct) { SelectedFolder=Item->Path; Filter(); } })]]
        + SSplitter::Slot().Value(.85f).MinSize(260)
            [SNew(SBorder).BorderImage(&PanelBrush()).BorderBackgroundColor(GraphFill).Padding(3)[FileList]];

    const auto SourceControls = SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,4,0)
            [SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(9,4)).ForegroundColor(TextColor)
                .Text(LOCTEXT("SourcePlay", "▶ Слушать звук"))
                .ToolTipText(LOCTEXT("SourcePlayHint", "Слушать выбранный звук отдельно от игрового действия. Можно также нажать ▶ слева от имени в любой строке. Черновик и игра не изменяются."))
                .IsEnabled_Lambda([this] { return GEditor && HasUsableSelection(); })
                .OnClicked_Lambda([this] { PlaySelectedSource(); return FReply::Handled(); })]
        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,10,0)
            [SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(9,4)).ForegroundColor(TextColor)
                .Text(LOCTEXT("SourceStop", "■"))
                .ToolTipText(LOCTEXT("SourceStopHint", "Остановить только предпрослушку, запущенную этой библиотекой."))
                .IsEnabled_Lambda([this] { return IsSourcePreviewPlaying(); })
                .OnClicked_Lambda([this] { StopSourcePreview(); return FReply::Handled(); })]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,12,0)
            [SNew(STextBlock).Text_Lambda([this] { return Selected.IsValid()?DurationText(Selected):FText::GetEmpty(); }).ColorAndOpacity(WorkspaceMint)]
        + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0,0,8,0)
            [SAssignNew(SourceWaveform,SMCSFXSourceWaveform).OnSeek_Lambda([this](float Seconds){
                SourceStartSeconds=Seconds;
                if(IsSourcePreviewPlaying()&&PlayingAssetPath==SelectedAssetPath){SourcePreview->Play(Seconds);SourcePlayOrder=SourcePreview->GetLastPlayOrder();}
            })]
        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,5,0)
            [SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(8,4)).ForegroundColor(TextColor)
                .Text(LOCTEXT("Open", "Открыть"))
                .ToolTipText(LOCTEXT("Waveform", "Открыть редактор источника. Для Sound Cue и MetaSound здесь доступна их настоящая схема."))
                .IsEnabled_Lambda([this] { return HasUsableSelection(); })
                .OnClicked_Lambda([this] { OpenSelectedSource(); return FReply::Handled(); })]
        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,5,0)
            [SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(8,4)).ForegroundColor(TextColor)
                .Text(LOCTEXT("Audition", "Примерить в действии"))
                .IsEnabled_Lambda([this] { return Context.bCanAudition && HasUsableSelection() && bool(Context.AuditionInAction); })
                .ToolTipText_Lambda([this] { return Context.AuditionReason; })
                .OnClicked_Lambda([this] { StopSourcePreview(); if(Context.bCanAudition && HasUsableSelection() && Context.AuditionInAction){const FAssetData Asset=Selected;const auto Audition=Context.AuditionInAction;Audition(Asset);} return FReply::Handled(); })]
        + SHorizontalBox::Slot().AutoWidth()
            [SNew(SButton).ButtonStyle(&SolidStyle()).ContentPadding(FMargin(10,4))
                .ButtonColorAndOpacity_Lambda([this]{return Context.bCanAdd&&HasUsableSelection()?WorkspaceMint:HeadingFill;}).ForegroundColor(Ink)
                .Text(LOCTEXT("Add", "Добавить выбранный звук"))
                .IsEnabled_Lambda([this] { return Context.bCanAdd && HasUsableSelection() && bool(Context.Add); })
                .ToolTipText_Lambda([this] { return FText::Format(LOCTEXT("AddHint", "Добавить выбранный источник: {0}"),Context.EventLabel); })
                .OnClicked_Lambda([this] { if(Context.bCanAdd && HasUsableSelection() && Context.Add){const FAssetData Asset=Selected;const auto Add=Context.Add;Add(Asset);} return FReply::Handled(); })];

    ChildSlot[SNew(SBorder).BorderImage(&PanelBrush()).BorderBackgroundColor(AppFill).Padding(8)
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,5)[Toolbar]
            + SVerticalBox::Slot().AutoHeight().Padding(2,0,2,3)
                [SNew(STextBlock).Text_Lambda([this] { return FText::Format(LOCTEXT("Count", "{0}  ·  {1} из {2} звуков  ·  с вложенными папками"),
                    FText::FromString(SelectedFolder),FText::AsNumber(Filtered.Num()),FText::AsNumber(All.Num())); }).ColorAndOpacity(Muted)
                    .Font(FAppStyle::GetFontStyle(TEXT("SmallFont"))) ]
            + SVerticalBox::Slot().FillHeight(1)[Browser]
            + SVerticalBox::Slot().AutoHeight().Padding(2,4,2,2)
                [SNew(STextBlock).Text_Lambda([this] { return SelectedAssetPath.IsNull()?LOCTEXT("SelectSource", "Выберите источник. Клик и поиск не изменяют черновик."):FText::FromString(SelectedAssetPath.ToString()); })
                    .ToolTipText_Lambda([this] { return FText::FromString(SelectedAssetPath.ToString()); })
                    .OverflowPolicy(ETextOverflowPolicy::Ellipsis).ColorAndOpacity(TextColor).Font(FAppStyle::GetFontStyle(TEXT("SmallFont"))) ]
            + SVerticalBox::Slot().AutoHeight()[SourceControls]
            + SVerticalBox::Slot().AutoHeight().Padding(2,2)[SNew(STextBlock)
                .Text_Lambda([this]{return SelectedStatus();}).ColorAndOpacity(Muted)
                .Font(FAppStyle::GetFontStyle(TEXT("SmallFont"))).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]]];
    RebuildFolders();
    Filter();
}

void SMCSFXLibrary::Tick(const FGeometry& Geometry,double CurrentTime,float DeltaTime)
{
    SCompoundWidget::Tick(Geometry,CurrentTime,DeltaTime);
    if(RegistryState && RegistryState->bPending.Exchange(false)) RefreshAssets();
    if(SourcePreview.IsValid() && !IsSourcePreviewPlaying())
    { SourcePreview.Reset(); SourcePlayOrder=0; PlayingAssetPath.Reset(); }
}

void SMCSFXLibrary::RefreshAssets()
{
    check(IsInGameThread());
    const auto* Module=FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry"));
    auto* Registry=Module?Module->TryGet():nullptr;
    if(!Registry) return;
    if(RegistryState) SelectedAssetPath=RegistryState->Selection();
    FARFilter Scope;
    Scope.ClassPaths.Add(USoundBase::StaticClass()->GetClassPathName()); Scope.bRecursiveClasses=true;
    Scope.PackagePaths.Add(TEXT("/Game")); Scope.bRecursivePaths=true;
    TArray<FAssetData> Assets; Registry->GetAssets(Scope,Assets);
    All.Reset(Assets.Num()); Selected=FAssetData(); ExplicitDurations.Reset();
    for(auto& Asset:Assets)
    {
        if(Asset.GetSoftObjectPath()==SelectedAssetPath) Selected=Asset;
        All.Add(MakeShared<FAssetData>(MoveTemp(Asset)));
    }
    RebuildFolders(); Filter();
}

void SMCSFXLibrary::RebuildFolders()
{
    TGuardValue<bool> Guard(bRefreshingTree,true);
    Roots.Reset(); Folders.Reset();
    auto Root=MakeShared<FMCSFXLibraryFolder>(); Root->Path=TEXT("/Game"); Root->Name=TEXT("/Game");
    Roots.Add(Root); Folders.Add(Root->Path,Root);
    for(const auto& Asset:All)
    {
        ++Root->Count;
        TArray<FString> Parts; Asset->PackagePath.ToString().ParseIntoArray(Parts,TEXT("/"),true);
        FFolder Parent=Root; FString Path=TEXT("/Game");
        for(int32 I=1;I<Parts.Num();++I)
        {
            Path+=TEXT("/")+Parts[I];
            auto* Found=Folders.Find(Path);
            if(!Found)
            {
                auto Folder=MakeShared<FMCSFXLibraryFolder>(); Folder->Name=Parts[I]; Folder->Path=Path;
                Parent->Children.Add(Folder); Folders.Add(Path,Folder);
            }
            Parent=Folders.FindChecked(Path); ++Parent->Count;
        }
    }
    for(auto& Pair:Folders) Pair.Value->Children.Sort([](const FFolder& A,const FFolder& B) { return A->Name.Compare(B->Name,ESearchCase::IgnoreCase)<0; });
    while(!Folders.Contains(SelectedFolder) && SelectedFolder!=TEXT("/Game"))
    { int32 Slash=INDEX_NONE; SelectedFolder.FindLastChar(TEXT('/'),Slash); SelectedFolder=Slash>0?SelectedFolder.Left(Slash):TEXT("/Game"); }
    if(Tree)
    {
        Tree->RequestTreeRefresh();
        for(const auto& Pair:Folders) if(ExpandedFolders.Contains(Pair.Key)) Tree->SetItemExpansion(Pair.Value,true);
        Tree->SetSelection(Folders.FindChecked(SelectedFolder),ESelectInfo::Direct);
    }
}

TOptional<float> SMCSFXLibrary::Duration(const FAssetData& Asset) const
{
    if(const auto* Value=ExplicitDurations.Find(Asset.GetSoftObjectPath())) return *Value;
    float Seconds=0;
    if(Asset.GetTagValue(FName(TEXT("Duration")),Seconds) && FMath::IsFinite(Seconds) && Seconds>0) return Seconds;
    return TOptional<float>();
}

FText SMCSFXLibrary::DurationText(const FAssetData& Asset) const
{
    const auto Seconds=Duration(Asset);
    if(!Seconds.IsSet()) return LOCTEXT("UnknownDuration", "—");
    if(Seconds.GetValue()>=INDEFINITELY_LOOPING_DURATION) return LOCTEXT("DynamicDuration", "Цикл / граф");
    return FText::FromString(FString::Printf(TEXT("%.2f с"),Seconds.GetValue()));
}

void SMCSFXLibrary::Filter()
{
    Filtered.Reset(); const FString Needle=FTextTransformer::ToLower(Query.TrimStartAndEnd());
    for(const auto& Asset:All)
    {
        const FString Path=Asset->PackagePath.ToString();
        if(Path!=SelectedFolder && !Path.StartsWith(SelectedFolder+TEXT("/"))) continue;
        const FString Haystack=Asset->AssetName.ToString()+TEXT(" ")+Path+TEXT(" ")+Asset->AssetClassPath.GetAssetName().ToString();
        if(Needle.IsEmpty() || FTextTransformer::ToLower(Haystack).Contains(Needle,ESearchCase::CaseSensitive)) Filtered.Add(Asset);
    }
    Filtered.Sort([this](const FItem& A,const FItem& B)
    {
        if(Sort==ESort::Duration)
        {
            const auto DA=Duration(*A), DB=Duration(*B);
            if(DA.IsSet()!=DB.IsSet()) return DA.IsSet();
            if(DA.IsSet() && DA.GetValue()!=DB.GetValue()) return bDescending?DA.GetValue()>DB.GetValue():DA.GetValue()<DB.GetValue();
        }
        if(Sort==ESort::Type && A->AssetClassPath!=B->AssetClassPath)
        {
            const int32 Order=A->AssetClassPath.ToString().Compare(B->AssetClassPath.ToString(),ESearchCase::IgnoreCase);
            return bDescending?Order>0:Order<0;
        }
        const int32 NameOrder=A->AssetName.ToString().Compare(B->AssetName.ToString(),ESearchCase::IgnoreCase);
        const int32 Order=NameOrder?NameOrder:A->GetSoftObjectPath().ToString().Compare(B->GetSoftObjectPath().ToString());
        return bDescending?Order>0:Order<0;
    });
    if(List)
    {
        List->RequestListRefresh(); List->ClearSelection();
        for(const auto& Asset:Filtered) if(Asset->GetSoftObjectPath()==SelectedAssetPath) { List->SetSelection(Asset,ESelectInfo::Direct); break; }
    }
}

void SMCSFXLibrary::SelectAsset(FItem Item)
{
    if(!Item) return;
    const bool Changed=SelectedAssetPath!=Item->GetSoftObjectPath();
    Selected=*Item; SelectedAssetPath=Selected.GetSoftObjectPath();
    if(Changed){SourceStartSeconds=0;if(SourceWaveform)SourceWaveform->Reset();}
    RegistryState->Select(SelectedAssetPath); Status.Reset();
    // A row's Play button handles its mouse event, so select the exact row here too.
    // Direct selection does not re-enter the list's user-selection callback.
    if(List)List->SetSelection(Item,ESelectInfo::Direct);
    const FAssetData Asset=Selected;const auto OnSelect=Context.Select;
    if(OnSelect)OnSelect(Asset);
}

void SMCSFXLibrary::ToggleRowSource(FItem Item)
{
    if(!Item || !Item->IsValid())return;
    const FSoftObjectPath Path=Item->GetSoftObjectPath();
    const bool WasPlaying=IsSourcePreviewPlaying()&&PlayingAssetPath==Path;
    SelectAsset(Item);
    // Selection callbacks may refresh the host/context; never play a substituted row.
    if(!HasUsableSelection()||SelectedAssetPath!=Path)return;
    if(WasPlaying)StopSourcePreview();
    else PlaySelectedSource();
}

bool SMCSFXLibrary::HasUsableSelection() const
{ return Selected.IsValid() && Selected.GetSoftObjectPath()==SelectedAssetPath; }

bool SMCSFXLibrary::IsSourcePreviewPlaying() const
{
    const auto* Component=SourcePreview.Get();
    return GEditor && Component && GEditor->GetPreviewAudioComponent()==Component
        && Component->GetLastPlayOrder()==SourcePlayOrder && Component->IsPlaying();
}

void SMCSFXLibrary::StopSourcePreview()
{
    // The editor can replace/replay its preview component from any asset editor.
    // Never StopPreviewSound globally: it may now belong to another client.
    if(IsSourcePreviewPlaying()) SourcePreview->Stop();
    SourcePreview.Reset(); SourcePlayOrder=0; PlayingAssetPath.Reset();
}

void SMCSFXLibrary::PlaySelectedSource()
{
    if(!GEditor || !HasUsableSelection()) return;
    const FAssetData Asset=Selected;const FSoftObjectPath Path=Asset.GetSoftObjectPath();
    const auto BeforePlay=Context.BeforeSourcePlay;
    if(BeforePlay)BeforePlay();
    if(!HasUsableSelection()||SelectedAssetPath!=Path)return;
    auto* Sound=Cast<USoundBase>(Asset.GetAsset()); // Explicit Play is the first source load.
    if(!Sound) { Status=TEXT("Не удалось загрузить выбранный SoundBase."); return; }
    if(!HasUsableSelection()||SelectedAssetPath!=Path)return;
    StopSourcePreview();
    if(SourceWaveform){SourceWaveform->ReadSource(Sound);SourceStartSeconds=SourceWaveform->GetStartSeconds();}
    // Sound Wave.Duration is the real file length even when GetDuration reports a loop.
    const float Seconds=Cast<USoundWave>(Sound)?CastChecked<USoundWave>(Sound)->Duration:Sound->GetDuration();
    if(FMath::IsFinite(Seconds) && Seconds>0) ExplicitDurations.Add(SelectedAssetPath,Seconds);
    if(auto* Component=GEditor->ResetPreviewAudioComponent(Sound))
    {
        Component->Play(SourceStartSeconds);
        SourcePreview=Component; SourcePlayOrder=Component->GetLastPlayOrder(); PlayingAssetPath=SelectedAssetPath;
        Status=TEXT("Предпрослушка источника · вне игрового действия");
    }
    else Status=TEXT("Редактор не предоставил аудиовывод для предпрослушки.");
    Filter();
}

int32 SMCSFXLibrary::GetSourceWaveformPeakCount() const
{return SourceWaveform?SourceWaveform->GetPeakCount():0;}

void SMCSFXLibrary::OpenSelectedSource()
{
    if(!HasUsableSelection()) return;
    const FAssetData Asset=Selected;const auto Open=Context.OpenSource;
    if(Open)Open(Asset);
    else if(GEditor) if(auto* Source=Asset.GetAsset())
        if(auto* Editors=GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()) Editors->OpenEditorForAsset(Source);
}

void SMCSFXLibrary::ImportIntoFolder()
{
    if(!GEditor || GEditor->IsPlaySessionInProgress()) return;
    const auto KeepAlive=SharedThis(this);
    auto& Tools=FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    const auto Imported=Tools.ImportAssetsWithDialog(SelectedFolder);
    if(Imported.IsEmpty()) return;
    Tools.SyncBrowserToAssets(Imported);
    Status=FString::Printf(TEXT("Импортировано: %d. Сохраните новые ассеты в Content Browser. Cook не изменён."),Imported.Num());
    RefreshAssets();
}

FText SMCSFXLibrary::SelectedStatus() const
{
    if(!SelectedAssetPath.IsNull() && !Selected.IsValid()) return LOCTEXT("Missing", "Выбранный источник удалён или ещё не импортирован.");
    if(IsSourcePreviewPlaying()) return FText::FromString(PlayingAssetPath==SelectedAssetPath?TEXT("Слушаем источник · вне действия"):TEXT("Слушаем предыдущий источник · ■ остановить"));
    if(!Status.IsEmpty()) return FText::FromString(Status);
    if(SourceWaveform&&SourceWaveform->GetPeakCount()>0)return LOCTEXT("WaveformReady","Волна WAV · клик выбирает точку запуска предпрослушки.");
    return LOCTEXT("WaveformFallback", "▶ Слушать звук — кнопка слева в каждой строке или под списком. Для WAV строится волна; Cue / MetaSound открываются как схемы.");
}

TSharedRef<SWidget> SMCSFXLibrary::SortMenu()
{
    using namespace MCComposerStyle;
    auto Menu=SNew(SVerticalBox); const TWeakPtr<SMCSFXLibrary> Weak=SharedThis(this);
    for(const ESort Value:{ESort::Name,ESort::Duration,ESort::Type})
        Menu->AddSlot().AutoHeight()[Command(Value==ESort::Name?TEXT("Имя"):Value==ESort::Duration?TEXT("Длительность"):TEXT("Тип"),[Weak,Value]
            { if(auto W=Weak.Pin()) { W->Sort=Value; W->Filter(); } FSlateApplication::Get().DismissAllMenus(); })];
    return Menu;
}

TSharedRef<ITableRow> SMCSFXLibrary::FolderRow(FFolder Item,const TSharedRef<STableViewBase>& Owner)
{
    using namespace MCComposerStyle;
    return SNew(STableRow<FFolder>,Owner).Padding(FMargin(3,2))
        .ToolTipText(FText::FromString(Item->Path))
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(FText::FromString(Item->Name)).ColorAndOpacity(TextColor).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]
            + SHorizontalBox::Slot().AutoWidth().Padding(5,0)[SNew(STextBlock).Text(FText::AsNumber(Item->Count)).ColorAndOpacity(Muted)]];
}

TSharedRef<ITableRow> SMCSFXLibrary::AssetRow(FItem Item,const TSharedRef<STableViewBase>& Owner)
{
    using namespace MCComposerStyle;
    return SNew(STableRow<FItem>,Owner).Padding(FMargin(5,3))
        .ToolTipText(FText::FromString(Item->GetSoftObjectPath().ToString()))
        .OnDragDetected_Lambda([this,Item](const FGeometry&,const FPointerEvent&)
        {
            const FAssetData Asset=*Item;const auto BeginDrag=Context.BeginDrag;
            SelectAsset(Item);
            // The destination validates whether it can accept a sound. Starting a
            // drag is also useful when this view currently shows a read-only event.
            return Asset.IsValid()&&BeginDrag?BeginDrag(Asset):FReply::Unhandled();
        })
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,5,0)
                [SNew(SBox).WidthOverride(28).HeightOverride(24)
                    [SNew(SButton).ButtonStyle(&NeutralStyle()).ContentPadding(FMargin(3,2)).IsFocusable(false)
                        .IsEnabled_Lambda([Item]{return GEditor&&Item->IsValid();})
                        .Text_Lambda([this,Item]{return IsSourcePreviewPlaying()&&PlayingAssetPath==Item->GetSoftObjectPath()?LOCTEXT("RowStop","■"):LOCTEXT("RowPlay","▶");})
                        .ForegroundColor_Lambda([this,Item]{return FSlateColor(IsSourcePreviewPlaying()&&PlayingAssetPath==Item->GetSoftObjectPath()?WorkspaceMint:TextColor);})
                        .ToolTipText_Lambda([this,Item]{return FText::Format(IsSourcePreviewPlaying()&&PlayingAssetPath==Item->GetSoftObjectPath()
                            ?LOCTEXT("RowStopHint","Остановить прослушивание: {0}"):LOCTEXT("RowPlayHint","Слушать звук: {0}. Без добавления в событие."),FText::FromName(Item->AssetName));})
                        .OnClicked_Lambda([this,Item]{ToggleRowSource(Item);return FReply::Handled();})]]
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0,0,8,0)
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromName(Item->AssetName)).ColorAndOpacity(TextColor).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]
                    + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromName(Item->PackagePath)).ColorAndOpacity(Muted)
                        .Font(FAppStyle::GetFontStyle(TEXT("SmallFont"))).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(126)
                [SNew(STextBlock).Text(FText::FromName(Item->AssetClassPath.GetAssetName())).ColorAndOpacity(Muted).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(100)
                [SNew(STextBlock).Text(DurationText(*Item)).ColorAndOpacity(Muted)]]];
}

#undef LOCTEXT_NAMESPACE
