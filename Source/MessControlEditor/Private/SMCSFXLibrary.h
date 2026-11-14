#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STreeView.h"

class UAudioComponent;
class SMCSFXSourceWaveform;
struct FMCSFXLibraryFolder;
struct FMCSFXLibraryRegistryState;

/** Host owns exact action/event/draft guards; selecting or listening never edits a draft. */
struct FMCSFXLibraryContext
{
    FText EventLabel;
    bool bCanAdd = false, bCanAudition = false;
    FText AuditionReason;
    TFunction<void(const FAssetData&)> Add, AuditionInAction, OpenSource, Select;
    TFunction<FReply(const FAssetData&)> BeginDrag;
    /** Host can stop its own contextual preview before separate source listening. */
    TFunction<void()> BeforeSourcePlay;
};

/** Asset-registry folder browser. Sources are loaded only by explicit Play/Open/host commands. */
class SMCSFXLibrary final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMCSFXLibrary) {} SLATE_ARGUMENT(FMCSFXLibraryContext, Context) SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual ~SMCSFXLibrary() override;
    virtual void Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime) override;
    /** Keeps local query, folder expansion, sort and selected asset. */
    void SetContext(FMCSFXLibraryContext NewContext) { Context = MoveTemp(NewContext); }
    FAssetData GetSelectedAsset() const { return Selected; }
    FSoftObjectPath GetSelectedAssetPath() const { return SelectedAssetPath; }
    int32 GetTotalCount() const { return All.Num(); }
    int32 GetMatchedCount() const { return Filtered.Num(); }
    FString GetQuery() const { return Query; }
    FString GetSelectedFolder() const { return SelectedFolder; }
    /** Stops only the exact source-preview component/play owned by this browser. */
    void StopSourcePreview();
    bool IsSourcePreviewPlaying() const;
    int32 GetSourceWaveformPeakCount() const;

private:
    using FItem = TSharedPtr<FAssetData>;
    using FFolder = TSharedPtr<FMCSFXLibraryFolder>;
    enum class ESort : uint8 { Name, Duration, Type };
    FMCSFXLibraryContext Context;
    TArray<FItem> All, Filtered;
    TArray<FFolder> Roots;
    TMap<FString, FFolder> Folders;
    TSet<FString> ExpandedFolders;
    TSharedPtr<STreeView<FFolder>> Tree;
    TSharedPtr<SListView<FItem>> List;
    TSharedPtr<FMCSFXLibraryRegistryState, ESPMode::ThreadSafe> RegistryState;
    FDelegateHandle AssetAdded, AssetRemoved, AssetRenamed, AssetUpdated, AssetUpdatedOnDisk, FilesLoaded;
    FAssetData Selected;
    FSoftObjectPath SelectedAssetPath;
    FString Query, SelectedFolder = TEXT("/Game"), Status;
    ESort Sort = ESort::Name;
    bool bDescending = false, bRefreshingTree = false;
    TMap<FSoftObjectPath, float> ExplicitDurations;
    TWeakObjectPtr<UAudioComponent> SourcePreview;
    uint32 SourcePlayOrder = 0;
    FSoftObjectPath PlayingAssetPath;
    TSharedPtr<SMCSFXSourceWaveform> SourceWaveform;
    float SourceStartSeconds=0;

    void RefreshAssets();
    void RebuildFolders();
    void Filter();
    void SelectAsset(FItem Item);
    void ToggleRowSource(FItem Item);
    void PlaySelectedSource();
    void ImportIntoFolder();
    void OpenSelectedSource();
    bool HasUsableSelection() const;
    TOptional<float> Duration(const FAssetData& Asset) const;
    FText DurationText(const FAssetData& Asset) const;
    FText SelectedStatus() const;
    TSharedRef<SWidget> SortMenu();
    TSharedRef<ITableRow> AssetRow(FItem Item, const TSharedRef<STableViewBase>& Owner);
    TSharedRef<ITableRow> FolderRow(FFolder Item, const TSharedRef<STableViewBase>& Owner);
};
