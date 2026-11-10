#include "SMCSFXGallery.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "Templates/Atomic.h"
#include "Internationalization/TextTransformer.h"
#include "Modules/ModuleManager.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundSourceBus.h"
#include "Sound/SoundWaveProcedural.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "MCSFXGallery"

// Registry callbacks can run off the game thread. They own only this thread-safe
// mailbox, never a widget, UObject, registry query or Slate call.
struct FMCSFXRegistryRefreshState
{
    TAtomic<bool> bPending{false};
    FCriticalSection SelectionMutex;
    FSoftObjectPath SelectedPath;
    void Select(const FSoftObjectPath& Path){FScopeLock Lock(&SelectionMutex);SelectedPath=Path;}
    FSoftObjectPath GetSelectedPath(){FScopeLock Lock(&SelectionMutex);return SelectedPath;}
    void Rename(const FSoftObjectPath& Before,const FSoftObjectPath& After)
    {FScopeLock Lock(&SelectionMutex);if(SelectedPath==Before)SelectedPath=After;}
};

SMCSFXGallery::~SMCSFXGallery()
{
    if(const auto* Module=FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry")))
        if(auto* Registry=Module->TryGet())
        {
            Registry->OnAssetAdded().Remove(AssetAdded);Registry->OnAssetRemoved().Remove(AssetRemoved);
            Registry->OnAssetRenamed().Remove(AssetRenamed);Registry->OnAssetUpdated().Remove(AssetUpdated);
            Registry->OnAssetUpdatedOnDisk().Remove(AssetUpdatedOnDisk);Registry->OnFilesLoaded().Remove(FilesLoaded);
        }
    RegistryRefreshState.Reset();
}

void SMCSFXGallery::Tick(const FGeometry& Geometry,const double CurrentTime,const float DeltaTime)
{
    SCompoundWidget::Tick(Geometry,CurrentTime,DeltaTime);
    if(RegistryRefreshState&&RegistryRefreshState->bPending.Exchange(false))RefreshAssets();
}

void SMCSFXGallery::RefreshAssets()
{
    check(IsInGameThread());
    const auto* Module=FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry"));
    auto* Registry=Module?Module->TryGet():nullptr;if(!Registry)return;
    if(RegistryRefreshState)SelectedAssetPath=RegistryRefreshState->GetSelectedPath();
    FARFilter AssetFilter;AssetFilter.ClassPaths.Add(USoundBase::StaticClass()->GetClassPathName());
    AssetFilter.bRecursiveClasses=true;AssetFilter.PackagePaths.Add(TEXT("/Game"));AssetFilter.bRecursivePaths=true;
    TArray<FAssetData> Assets;Registry->GetAssets(AssetFilter,Assets);
    Assets.Sort([](const auto& A,const auto& B){return A.AssetName.LexicalLess(B.AssetName);});
    All.Reset(Assets.Num());Selected=FAssetData();SelectedAssetUnavailableReason=FText::GetEmpty();
    for(auto& Asset:Assets)
    {
        if(Asset.GetSoftObjectPath()==SelectedAssetPath){Selected=Asset;SelectedAssetUnavailableReason=GetAssetUnavailableReason(Selected);}
        All.Add(MakeShared<FAssetData>(MoveTemp(Asset)));
    }
    // Keep the intended path across removal/reimport, but disable using a missing source.
    if(!SelectedAssetPath.IsNull()&&!Selected.IsValid())
        SelectedAssetUnavailableReason=FText::Format(LOCTEXT("SelectedSourceMissing","Выбранный звук больше не найден: {0}. Выберите другой звук или верните исходник."),FText::FromString(SelectedAssetPath.ToString()));
    Filter();
}

FText SMCSFXGallery::GetAssetUnavailableReason(const FAssetData& Asset)
{
    if(!Asset.IsValid())return FText::GetEmpty();
    // Mirrors BuildEventAudioPlan's explicit procedural/source-bus rejection.
    // Class paths/ancestry are registry data; never GetAsset, resolve a class or prepare audio on hover.
    TArray<FTopLevelAssetPath> Ancestors;
    if(const auto* Registry=FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry")))
        Registry->Get().GetAncestorClassNames(Asset.AssetClassPath,Ancestors);
    const auto IsType=[&](const FTopLevelAssetPath& Type){return Asset.AssetClassPath==Type||Ancestors.Contains(Type);};
    const bool bMetaSound=IsType(FTopLevelAssetPath(TEXT("/Script/MetasoundEngine"),TEXT("MetaSoundSource")));
    if(bMetaSound || (!IsType(USoundWaveProcedural::StaticClass()->GetClassPathName())
        && !IsType(USoundSourceBus::StaticClass()->GetClassPathName())))return FText::GetEmpty();
    return FText::Format(LOCTEXT("UnsupportedTypeReason","{0} пока не поддерживается в слоях Composer. Доступны Sound Wave (WAV), Sound Cue и MetaSound Source."),
        FText::FromName(Asset.AssetClassPath.GetAssetName()));
}

void SMCSFXGallery::Construct(const FArguments& Args)
{
    Context=Args._Context;SetCanTick(true);
    RegistryRefreshState=MakeShared<FMCSFXRegistryRefreshState,ESPMode::ThreadSafe>();
    auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    const auto State=RegistryRefreshState;
    const auto QueueRefresh=[State](const FAssetData& Asset)
    {if(Asset.PackageName.ToString().StartsWith(TEXT("/Game/")))State->bPending.Store(true);};
    AssetAdded=Registry.OnAssetAdded().AddLambda(QueueRefresh);
    AssetRemoved=Registry.OnAssetRemoved().AddLambda(QueueRefresh);
    AssetUpdated=Registry.OnAssetUpdated().AddLambda(QueueRefresh);
    AssetUpdatedOnDisk=Registry.OnAssetUpdatedOnDisk().AddLambda(QueueRefresh);
    AssetRenamed=Registry.OnAssetRenamed().AddLambda([State](const FAssetData& Asset,const FString& OldPath)
    {
        if(Asset.PackageName.ToString().StartsWith(TEXT("/Game/"))||OldPath.StartsWith(TEXT("/Game/")))
        {State->Rename(FSoftObjectPath(OldPath),Asset.GetSoftObjectPath());State->bPending.Store(true);}
    });
    FilesLoaded=Registry.OnFilesLoaded().AddLambda([State]{State->bPending.Store(true);});
    // Subscribe before the initial snapshot so background registry changes cannot be missed.
    RefreshAssets();
    ChildSlot[SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(5)[SNew(STextBlock).Text_Lambda([this]{return FText::Format(LOCTEXT("ForEvent","Звуки для события: {0}"),Context.EventLabel);}).AutoWrapText(true)]
        +SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(SSearchBox).HintText(LOCTEXT("Search","Найти звук по имени, типу или пути…"))
            .OnTextChanged_Lambda([this](const FText& T){Query=T.ToString();Filter();})]
        +SVerticalBox::Slot().AutoHeight().Padding(5)[SNew(STextBlock)
            .Text(LOCTEXT("SoundTypes","Sound Wave (WAV), Sound Cue, MetaSound Source")).AutoWrapText(true)]
        +SVerticalBox::Slot().AutoHeight().Padding(5)[SNew(STextBlock).Text_Lambda([this]{return FText::Format(LOCTEXT("Count","Найдено {0} из {1}"),FText::AsNumber(Filtered.Num()),FText::AsNumber(All.Num()));})]
        +SVerticalBox::Slot().FillHeight(1)[SAssignNew(List,SListView<FItem>).ListItemsSource(&Filtered).SelectionMode(ESelectionMode::Single)
            .OnGenerateRow(this,&SMCSFXGallery::Row)
            .OnMouseButtonDoubleClick_Lambda([this](FItem Item){if(Item&&Context.OpenSource)Context.OpenSource(*Item);})
            .OnSelectionChanged_Lambda([this](FItem Item,ESelectInfo::Type Origin){if(Item&&Origin!=ESelectInfo::Direct){Selected=*Item;SelectedAssetPath=Selected.GetSoftObjectPath();RegistryRefreshState->Select(SelectedAssetPath);SelectedAssetUnavailableReason=GetAssetUnavailableReason(Selected);if(Context.Select)Context.Select(Selected);}})]
        +SVerticalBox::Slot().AutoHeight().Padding(5)[SNew(STextBlock).Visibility_Lambda([this]{return Filtered.IsEmpty()?EVisibility::Visible:EVisibility::Collapsed;})
            .Text(LOCTEXT("Empty","Нет подходящих звуков. Очистите строку поиска; выбранный звук сохранён.")).AutoWrapText(true)]
        +SVerticalBox::Slot().AutoHeight().Padding(5)[SNew(STextBlock).Text_Lambda([this]{return Selected.IsValid()?FText::FromName(Selected.AssetName):!SelectedAssetPath.IsNull()?LOCTEXT("SelectedUnavailable","Выбранный звук недоступен"):LOCTEXT("SelectHint","Клик — выбрать звук. Примерка и добавление выполняются отдельно.");}).AutoWrapText(true)]
        +SVerticalBox::Slot().AutoHeight().Padding(5)[SNew(STextBlock)
            .Visibility_Lambda([this]{return SelectedAssetUnavailableReason.IsEmpty()?EVisibility::Collapsed:EVisibility::Visible;})
            .Text_Lambda([this]{return SelectedAssetUnavailableReason;}).AutoWrapText(true).ColorAndOpacity(FLinearColor(1.f,.67f,.3f))]
        +SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)[SNew(SButton).Text(LOCTEXT("Audition","Примерить в действии"))
                .IsEnabled_Lambda([this]{return Context.bCanAudition&&Selected.IsValid()&&SelectedAssetUnavailableReason.IsEmpty();})
                .ToolTipText_Lambda([this]{return SelectedAssetUnavailableReason.IsEmpty()?Context.AuditionReason:SelectedAssetUnavailableReason;})
                .OnClicked_Lambda([this]{if(SelectedAssetUnavailableReason.IsEmpty()&&Context.AuditionInAction)Context.AuditionInAction(Selected);return FReply::Handled();})]
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).Text(LOCTEXT("Add","Добавить звук"))
                .IsEnabled_Lambda([this]{return Context.bCanAdd&&Selected.IsValid()&&SelectedAssetUnavailableReason.IsEmpty();})
                .ToolTipText_Lambda([this]{return SelectedAssetUnavailableReason;})
                .OnClicked_Lambda([this]{if(SelectedAssetUnavailableReason.IsEmpty()&&Context.Add)Context.Add(Selected);return FReply::Handled();})]]
        +SVerticalBox::Slot().AutoHeight().Padding(5)[SNew(STextBlock).Visibility_Lambda([this]{return Context.bCanAudition?EVisibility::Collapsed:EVisibility::Visible;})
            .Text_Lambda([this]{return Context.AuditionReason;}).AutoWrapText(true)]];
}
void SMCSFXGallery::Filter()
{
    Filtered.Reset();const FString Needle=FTextTransformer::ToLower(Query.TrimStartAndEnd());
    auto Contains=[&](const FString& Value){return FTextTransformer::ToLower(Value).Contains(Needle,ESearchCase::CaseSensitive);};
    for(const auto& Item:All)if(Needle.IsEmpty()||Contains(Item->AssetName.ToString())||Contains(Item->PackagePath.ToString())
        || Contains(Item->AssetClassPath.GetAssetName().ToString()))Filtered.Add(Item);
    if(List){List->RequestListRefresh();List->ClearSelection();for(const auto& Item:Filtered)if(Item->GetSoftObjectPath()==Selected.GetSoftObjectPath()){List->SetSelection(Item,ESelectInfo::Direct);break;}}
}
TSharedRef<ITableRow> SMCSFXGallery::Row(FItem Item,const TSharedRef<STableViewBase>& Owner)
{
    const FText UnavailableReason=GetAssetUnavailableReason(*Item);
    const FString Hint=UnavailableReason.IsEmpty()?TEXT("Перетащите звук на событие или группу."):UnavailableReason.ToString();
    return SNew(STableRow<FItem>,Owner).Padding(6)
        .ToolTipText(FText::FromString(Hint+TEXT("\nДвойной клик — открыть источник.\n")+Item->GetSoftObjectPath().ToString()))
        .OnDragDetected_Lambda([this,Item,UnavailableReason](const FGeometry&,const FPointerEvent&){Selected=*Item;SelectedAssetPath=Selected.GetSoftObjectPath();RegistryRefreshState->Select(SelectedAssetPath);SelectedAssetUnavailableReason=UnavailableReason;
            if(!Context.bCanAdd||!UnavailableReason.IsEmpty())return FReply::Handled();
            return Context.BeginDrag?Context.BeginDrag(*Item):FReply::Unhandled();})
        [SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,7,0)[SNew(STextBlock).Text(FText::FromString(TEXT("♪")))]
        +SHorizontalBox::Slot().FillWidth(1)[SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromName(Item->AssetName)).AutoWrapText(true)]
            +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromName(Item->AssetClassPath.GetAssetName())).AutoWrapText(true)]
            +SVerticalBox::Slot().AutoHeight().Padding(0,3,0,0)[SNew(STextBlock).Text(LOCTEXT("UnavailableBadge","Пока не поддерживается"))
                .Visibility(UnavailableReason.IsEmpty()?EVisibility::Collapsed:EVisibility::Visible)
                .ToolTipText(UnavailableReason).Font(FAppStyle::GetFontStyle(TEXT("SmallFont"))).ColorAndOpacity(FLinearColor(1.f,.67f,.3f))]]
        +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("Open","Открыть"))
            .OnClicked_Lambda([Open=Context.OpenSource,Item]{if(Open)Open(*Item);return FReply::Handled();})]];
}
#undef LOCTEXT_NAMESPACE
