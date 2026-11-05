#include "MCUIGridScrollBox.h"

#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Brushes/SlateNoResource.h"
#include "Types/SlateConstants.h"
#include "Widgets/SWidget.h"

UMCUIGridScrollBox::UMCUIGridScrollBox(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
    ApplyRowPolicy();
}
void UMCUIGridScrollBox::ApplyRowPolicy()
{
    auto Style=GetWidgetStyle();
    Style.SetTopShadowBrush(FSlateNoResource()).SetBottomShadowBrush(FSlateNoResource())
        .SetLeftShadowBrush(FSlateNoResource()).SetRightShadowBrush(FSlateNoResource());
    SetWidgetStyle(Style);
    SetOrientation(Orient_Vertical);SetAllowOverscroll(false);SetAnimateWheelScrolling(false);
    SetAllowRightClickDragScrolling(false);SetIsTouchScrollingEnabled(false);
    SetScrollWhenFocusChanges(EScrollWhenFocusChanges::NoScroll);
    SetNavigationDestination(EDescendantScrollDestination::IntoView);
    if(RowStride>0.f) SetWheelScrollMultiplier(RowStride/FMath::Max(.001f,GetGlobalScrollAmount()));
}
TSharedRef<SWidget> UMCUIGridScrollBox::RebuildWidget()
{
    OnUserScrolled.AddUniqueDynamic(this,&ThisClass::HandleRowScrolled);
    ApplyRowPolicy();return Super::RebuildWidget();
}
void UMCUIGridScrollBox::SynchronizeProperties()
{
    Super::SynchronizeProperties();OnUserScrolled.AddUniqueDynamic(this,&ThisClass::HandleRowScrolled);ApplyRowPolicy();RefreshGridMetrics();
}
void UMCUIGridScrollBox::ReleaseSlateResources(bool bReleaseChildren)
{
    LastFocusedCell.Reset();Super::ReleaseSlateResources(bReleaseChildren);
}
void UMCUIGridScrollBox::ConfigureGrid(UUniformGridPanel* InGrid,int32 InColumns,int32 InVisibleRows,float InRowStride)
{
    if(CellGrid.Get()!=InGrid) LastFocusedCell.Reset();
    CellGrid=InGrid;GridColumns=FMath::Max(1,InColumns);VisibleRows=FMath::Max(1,InVisibleRows);
    AuthoredRowStride=FMath::IsFinite(InRowStride)?FMath::Max(0.f,InRowStride):0.f;
    RefreshGridMetrics();
}
void UMCUIGridScrollBox::RefreshGridMetrics()
{
    auto* Grid=CellGrid.Get();if(!Grid) return;
    const auto& GridGeometry=Grid->GetPaintSpaceGeometry();
    TotalRows=0;float FirstOrigin=0.f,MeasuredStride=0.f,SingleCellExtent=0.f;int32 FirstRow=INDEX_NONE;
    UWidget* FocusedCell=nullptr;int32 FocusedIndex=INDEX_NONE;
    const FMargin CellInset=Grid->GetSlotPadding();
    for(int32 Index=0;Index<Grid->GetChildrenCount();++Index) if(auto* Cell=Grid->GetChildAt(Index);Cell && Cell->GetVisibility()!=ESlateVisibility::Collapsed) {
        const auto* GridCell=Cast<UUniformGridSlot>(Cell->Slot);const int32 Row=GridCell?GridCell->GetRow():Index/GridColumns;TotalRows=FMath::Max(TotalRows,Row+1);
        const auto& CellGeometry=Cell->GetPaintSpaceGeometry();
        if(CellGeometry.GetLocalSize().Y>0.f && GridGeometry.GetLocalSize().Y>0.f) {
            const float Origin=GridGeometry.AbsoluteToLocal(CellGeometry.LocalToAbsolute(FVector2D::ZeroVector)).Y;
            if(FirstRow==INDEX_NONE) {FirstRow=Row;FirstOrigin=Origin;}
            else if(Row!=FirstRow) MeasuredStride=FMath::Max(MeasuredStride,FMath::Abs((Origin-FirstOrigin)/float(Row-FirstRow)));
            SingleCellExtent=FMath::Max(SingleCellExtent,float(CellGeometry.GetLocalSize().Y)+CellInset.Top+CellInset.Bottom);
        }
        const auto SlateCell=Cell->GetCachedWidget();
        if(Cell->HasAnyUserFocus() || (SlateCell.IsValid() && SlateCell->HasFocusedDescendants())) {FocusedCell=Cell;FocusedIndex=Index;}
    }
    // An explicit stride is the owner's actual allocated cell+padding contract.
    // Otherwise use measured origins from the same paint coordinate space.
    const float NewStride=AuthoredRowStride>0.f?AuthoredRowStride:MeasuredStride>0.f?MeasuredStride:SingleCellExtent;
    if(NewStride>0.f && FMath::IsFinite(NewStride)) RowStride=NewStride;
    ApplyRowPolicy();
    if(FocusedCell && LastFocusedCell.Get()!=FocusedCell) {LastFocusedCell=FocusedCell;ScrollToCell(FocusedIndex);}
    else {if(!FocusedCell) LastFocusedCell.Reset();SnapOffset(MyScrollBox.IsValid()?GetScrollOffset():DesiredScrollOffset);}
}
int32 UMCUIGridScrollBox::GetFirstVisibleRow() const
{
    return RowStride>0.f?FMath::Clamp(FMath::RoundToInt((MyScrollBox.IsValid()?GetScrollOffset():DesiredScrollOffset)/RowStride),0,FMath::Max(0,TotalRows-VisibleRows)):0;
}
void UMCUIGridScrollBox::SnapOffset(float RequestedOffset)
{
    if(bSnappingRows || RowStride<=0.f || !FMath::IsFinite(RequestedOffset)) return;
    const int32 Row=FMath::Clamp(FMath::RoundToInt(RequestedOffset/RowStride),0,FMath::Max(0,TotalRows-VisibleRows));
    const float Target=float(Row)*RowStride;
    TGuardValue<bool> SnapGuard(bSnappingRows,true);EndInertialScrolling();
    if(!FMath::IsNearlyEqual(GetScrollOffset(),Target,.01f) || !FMath::IsNearlyEqual(DesiredScrollOffset,Target,.01f)) SetScrollOffset(Target);
}
void UMCUIGridScrollBox::HandleRowScrolled(float CurrentOffset)
{
    // SScrollBox assigns DesiredScrollOffset before emitting OnUserScrolled;
    // SetScrollOffset here changes the target used by that same native tick.
    SnapOffset(CurrentOffset);
}
void UMCUIGridScrollBox::ScrollToCell(int32 CellIndex)
{
    auto* Grid=CellGrid.Get();if(!Grid || RowStride<=0.f || !Grid->GetChildAt(CellIndex)) return;
    const auto* GridCell=Cast<UUniformGridSlot>(Grid->GetChildAt(CellIndex)->Slot);
    const int32 Row=GridCell?GridCell->GetRow():CellIndex/GridColumns;
    int32 First=GetFirstVisibleRow();if(Row<First) First=Row;else if(Row>=First+VisibleRows) First=Row-VisibleRows+1;
    SnapOffset(float(First)*RowStride);
}
