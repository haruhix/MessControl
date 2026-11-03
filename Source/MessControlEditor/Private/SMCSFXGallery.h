#pragma once
// Metadata search and selection never play/load a sound.
#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

struct FMCSFXRegistryRefreshState;

struct FMCSFXGalleryContext
{
    FText EventLabel;
    bool bCanAdd=false,bCanAudition=false;
    FText AuditionReason;
    TFunction<void(const FAssetData&)> Add,AuditionInAction,OpenSource,Select;
    /** Host supplies a scoped operation; a drag never auditions a sound. */
    TFunction<FReply(const FAssetData&)> BeginDrag;
};
class SMCSFXGallery final:public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMCSFXGallery){}SLATE_ARGUMENT(FMCSFXGalleryContext,Context)SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual ~SMCSFXGallery()override;
    virtual void Tick(const FGeometry& Geometry,const double CurrentTime,const float DeltaTime)override;
    /** Context/channel refresh preserves local query and selected source asset. */
    void SetContext(FMCSFXGalleryContext NewContext){Context=MoveTemp(NewContext);}
    int32 GetTotalCount()const{return All.Num();}
    int32 GetMatchedCount()const{return Filtered.Num();}
    FString GetQuery()const{return Query;}
    FAssetData GetSelectedAsset()const{return Selected;}
    FSoftObjectPath GetSelectedAssetPath()const{return SelectedAssetPath;}
    /** Explicit unsupported types only. Registry metadata never loads a source; empty means validate lazily. */
    static FText GetAssetUnavailableReason(const FAssetData& Asset);
private:
    using FItem=TSharedPtr<FAssetData>;
    FMCSFXGalleryContext Context;
    TArray<FItem> All,Filtered;
    TSharedPtr<SListView<FItem>> List;
    FAssetData Selected;
    FSoftObjectPath SelectedAssetPath;
    TSharedPtr<FMCSFXRegistryRefreshState,ESPMode::ThreadSafe> RegistryRefreshState;
    FDelegateHandle AssetAdded,AssetRemoved,AssetRenamed,AssetUpdated,AssetUpdatedOnDisk,FilesLoaded;
    FText SelectedAssetUnavailableReason;
    FString Query;
    void RefreshAssets();
    void Filter();
    TSharedRef<ITableRow> Row(FItem Item,const TSharedRef<STableViewBase>& Owner);
};
