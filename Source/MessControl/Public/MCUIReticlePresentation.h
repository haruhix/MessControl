#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

class UUserWidget;

/** Native-only, read-only presentation data. Points are in the source widget's local paint space.
 * HUD and future Hub providers retain their own aim, projection, traces, interpolation and accepted state. */
struct MESSCONTROL_API FMCUIReticleStroke
{
    TArray<FVector2D> Points;
    FLinearColor Color=FLinearColor::White;
    float Width=2;
    int32 Layer=0;
};
struct MESSCONTROL_API FMCUIReticleBox
{
    FVector2D Position=FVector2D::ZeroVector,Size=FVector2D(4,4);
    FLinearColor Color=FLinearColor::White;
    float CornerRadius=2;
    int32 Layer=0;
};
struct MESSCONTROL_API FMCUIReticleLabel
{
    FText Text;
    FSlateFontInfo Font;
    FLinearColor Color=FLinearColor::White;
    FVector2D Position=FVector2D::ZeroVector,Bounds=FVector2D(180,24);
    int32 Layer=0;
};
struct MESSCONTROL_API FMCUIReticlePresentation
{
    TWeakObjectPtr<UUserWidget> Source;
    uint64 SourceFrame=MAX_uint64;
    FVector2D LocalSize=FVector2D::ZeroVector;
    bool bSourceVisible=false,bPaintGeometryCompatible=false;
    TArray<FMCUIReticleStroke> Strokes;
    TArray<FMCUIReticleBox> Boxes;
    TArray<FMCUIReticleLabel> Labels;
    int32 MaximumLayer=0;
};
DECLARE_MULTICAST_DELEGATE_OneParam(FMCUIReticleSnapshotUpdated,const FMCUIReticlePresentation&);
