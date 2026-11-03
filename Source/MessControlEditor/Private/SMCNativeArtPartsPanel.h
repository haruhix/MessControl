#pragma once
// The renderer descriptor -> DTO adapter is the only code that reads the typed
// runtime NativePresentation schema. Slate never switches on a SourceID.
#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "Widgets/SCompoundWidget.h"

class UClass;
class UMCVFXNativePresentationSettings;
struct FMCVFXNativePartSettings;

enum class EMCNativeArtControlKind:uint8 {Scalar,Color,Toggle,Resource,Curve,ReadOnly};
struct FMCNativeArtScope
{
    bool bPersistent=false;
    FName Event; // None for real persistent state, not a fabricated event.
};
struct FMCNativeArtResourceView
{
    FText Role;
    FSoftObjectPath Asset;
    FString GeometrySource; // Source-verified generator/master; opened by a checked editor callback.
};
struct FMCNativeArtControlView
{
    FName ID; // Renderer-supported property key, not an arbitrary reflected field path.
    FText Label,Hint,Units;
    EMCNativeArtControlKind Kind=EMCNativeArtControlKind::ReadOnly;
    bool bWritable=false;
    double Scalar=0;
    TOptional<double> Min,Max;
    bool Toggle=false;
    FLinearColor Color=FLinearColor::White;
    FSoftObjectPath Resource;
    UClass* AllowedResourceClass=nullptr;
    FText ReadOnlyValue;
};
struct FMCNativeArtPartView
{
    FName PartID;
    FText Label,Summary,AvailabilityReason;
    bool bProtectedGeometry=false;
    TArray<FMCNativeArtResourceView> Sources;
    TArray<FMCNativeArtControlView> Controls;
};
struct FMCNativeArtEdit
{
    FName SourceID;
    FGuid DraftInstanceID; // Reject a popup commit from an earlier source/session incarnation.
    FMCNativeArtScope Scope;
    FName PartID,ControlID;
    EMCNativeArtControlKind Kind=EMCNativeArtControlKind::ReadOnly;
    double Scalar=0;
    bool Toggle=false;
    FLinearColor Color=FLinearColor::White;
    FSoftObjectPath Resource;
};
struct FMCNativeArtEditorContext
{
    FName SourceID;
    FGuid DraftInstanceID;
    FMCNativeArtScope Scope;
    TArray<FMCNativeArtPartView> Parts; // Actual renderer native defaults even when style asset is null.
    bool bCanEdit=false;
    FText Reason;
    TFunction<bool(const FMCNativeArtEdit&,FString&)> Commit;
    TFunction<bool(FName,FString&)> ResetPart;
    TFunction<void(const FMCNativeArtResourceView&)> OpenSource;
    TFunction<void(const FString&)> ShowStatus;
};

namespace MCNativeArtParts
{
    /** Builds only controls explicitly supported by the actual renderer descriptor. */
    TArray<FMCNativeArtPartView> BuildViews(const UMCVFXNativePresentationSettings* Draft,FMCNativeArtScope Scope);
    /** Copies supported values into an existing complete typed part; does not modify an asset. */
    bool ApplyEdit(const FMCNativeArtEdit& Edit,const UMCVFXNativePresentationSettings* Draft,FMCVFXNativePartSettings& Out,FString& Error);
}

/** Embedded inside existing Workspace Details; no independent window/catalog. */
class SMCNativeArtPartsPanel final:public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMCNativeArtPartsPanel){}
        SLATE_ARGUMENT(FMCNativeArtEditorContext,Context)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
private:
    FMCNativeArtEditorContext Context;
    TSharedRef<SWidget> PartCard(const FMCNativeArtPartView& Part);
    TSharedRef<SWidget> ControlRow(FName PartID,const FMCNativeArtControlView& Control);
    FMCNativeArtEdit MakeEdit(FName PartID,const FMCNativeArtControlView& Control)const;
    void Commit(FMCNativeArtEdit Edit);
};
