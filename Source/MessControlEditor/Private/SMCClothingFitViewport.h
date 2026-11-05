#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "UObject/StrongObjectPtr.h"

class FAdvancedPreviewScene;
class FMCClothingFitViewportClient;
class FStructOnScope;
class FJsonObject;
class UDataTable;
class UMCPlayerAppearance;
class UImportSubsystem;
class USkeletalMeshComponent;
class UStaticMeshComponent;
struct FMCCosmeticCatalogRow;

/** Selected native DataTable row only; no pawn/controller/game-facing wardrobe actor. */
class SMCClothingFitViewport final : public SEditorViewport
{
public:
    SLATE_BEGIN_ARGS(SMCClothingFitViewport) {}
        SLATE_ARGUMENT(TSharedPtr<FStructOnScope>, RowScope)
    SLATE_END_ARGS()

    SMCClothingFitViewport();
    void Construct(const FArguments& InArgs);
    virtual ~SMCClothingFitViewport() override;
    void RequestRowRefresh();
    void SetAppearance(UMCPlayerAppearance* InAppearance);
    FString GetAppearancePath() const;
    FText GetDiagnosticsText() const;
    void Focus();
    void SetViewPreset(int32 Preset);
    bool Matches(const FString& TablePath,FName RowName) const;
    TSharedRef<FJsonObject> Inspect() const;
    bool Capture(const FString& OutputFile);

    /** Registry holds weak widgets only; no editor/table/window is opened by these calls. */
    static TSharedPtr<SMCClothingFitViewport> FindOpen(const FString& TablePath,FName RowName,FString& OutError);

protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
    friend class FMCClothingFitViewportClient;
    class FTableListener;
    void AdvancePreview(float DeltaSeconds);
    bool ResolveRow(UDataTable*& OutTable,FName& OutRowName,const FMCCosmeticCatalogRow*& OutRow) const;
    void RefreshRow();
    void RebuildBody();
    void ObjectChanged(UObject* Object,struct FPropertyChangedEvent& Event);
    void UndoRedo();
    void AssetReimported(UObject* Object);
    bool IsDisplayedDependency(const UObject* Object) const;
    FBox GetDisplayBounds() const;

    TSharedPtr<FAdvancedPreviewScene> PreviewScene;
    TSharedPtr<FMCClothingFitViewportClient> FitViewportClient;
    TWeakPtr<FStructOnScope> RowScope;
    TUniquePtr<FTableListener> TableListener;
    TStrongObjectPtr<UMCPlayerAppearance> PreviewAppearance;
    TWeakObjectPtr<USkeletalMeshComponent> DisplayBody;
    TWeakObjectPtr<UStaticMeshComponent> DisplayClothing;
    TWeakObjectPtr<UDataTable> SourceTable;
    FName SourceRow;
    FString RowExport;
    FString Diagnostics;
    FTransform ResolvedFit=FTransform::Identity;
    FDelegateHandle PropertyChangedHandle;
    FDelegateHandle UndoHandle;
    TWeakObjectPtr<UImportSubsystem> ImportSubsystem;
    FDelegateHandle ReimportHandle;
    float RefreshAge=0;
    int32 ViewPreset=0;
    bool bRefreshRequested=true;
    bool bBodyRefreshRequested=false;
    bool bAttachValid=false;
    bool bRowValid=false;
    bool bFrameRequested=true;
};
