#pragma once

#include "Components/ScrollBox.h"
#include "MCUIGridScrollBox.generated.h"

class UUniformGridPanel;

/** A uniform cell grid scrolls only to complete row boundaries. No global input hook. */
UCLASS(meta=(DisplayName="UI Cell Grid Scroll Box"))
class MESSCONTROL_API UMCUIGridScrollBox : public UScrollBox
{
    GENERATED_BODY()
public:
    UMCUIGridScrollBox(const FObjectInitializer& ObjectInitializer=FObjectInitializer::Get());
    /** Call after the owner sizes the cells and viewport. Stride is Slate units;
     * zero measures actual row origins/geometry, including grid slot padding. */
    UFUNCTION(BlueprintCallable,Category="Grid Scroll")
    void ConfigureGrid(UUniformGridPanel* InGrid,int32 InColumns,int32 InVisibleRows=3,float InRowStride=0.f);
    UFUNCTION(BlueprintCallable,Category="Grid Scroll") void RefreshGridMetrics();
    UFUNCTION(BlueprintCallable,Category="Grid Scroll") void ScrollToCell(int32 CellIndex);
    UFUNCTION(BlueprintPure,Category="Grid Scroll") float GetRowStride() const {return RowStride;}
    UFUNCTION(BlueprintPure,Category="Grid Scroll") int32 GetFirstVisibleRow() const;
    virtual void SynchronizeProperties() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    void ApplyRowPolicy();
    void SnapOffset(float RequestedOffset);
    UFUNCTION() void HandleRowScrolled(float CurrentOffset);
    UPROPERTY(Transient) TWeakObjectPtr<UUniformGridPanel> CellGrid;
    UPROPERTY(Transient) TWeakObjectPtr<UWidget> LastFocusedCell;
    int32 GridColumns=1,VisibleRows=3,TotalRows=0;
    float RowStride=0.f,AuthoredRowStride=0.f;
    bool bSnappingRows=false;
};
