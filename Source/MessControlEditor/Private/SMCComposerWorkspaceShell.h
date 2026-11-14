#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class SSplitter;

/** Shared editor chrome. Slots retain their widgets and authoring state when the layout changes. */
class SMCComposerWorkspaceShell final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMCComposerWorkspaceShell) {}
        SLATE_ARGUMENT(FName, SettingsKey)
        SLATE_NAMED_SLOT(FArguments, Header)
        SLATE_NAMED_SLOT(FArguments, Catalogue)
        SLATE_NAMED_SLOT(FArguments, Preview)
        SLATE_NAMED_SLOT(FArguments, Graph)
        SLATE_NAMED_SLOT(FArguments, Inspector)
        SLATE_NAMED_SLOT(FArguments, Library)
        SLATE_NAMED_SLOT(FArguments, Transport)
        SLATE_NAMED_SLOT(FArguments, Footer)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args);
    virtual void Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime) override;
    bool IsCompact() const { return bCompact; }
    FString GetDensityName() const;
    FString GetLibraryModeName() const;
    bool IsGraphOnly() const;

private:
    enum class EDensity : uint8 { Auto, Full, Compact };
    enum class ECenter : uint8 { Preview, Graph, Both };
    enum class ELibrary : uint8 { Dock, Expanded, Hidden };
    EDensity Density = EDensity::Auto;
    ECenter Center = ECenter::Both;
    ELibrary LibraryMode = ELibrary::Dock;
    bool bCompact = false;
    float DockFraction = .30f;
    FVector FullWidths = FVector(.16f,.62f,.22f);
    FVector CompactWidths = FVector(.20f,.50f,.30f);
    FVector2D CenterWidths = FVector2D(.48f,.52f);
    FString SettingsSection;
    TSharedPtr<SWidget> CatalogueWidget, PreviewWidget, GraphWidget, InspectorWidget, LibraryWidget;
    TSharedPtr<SBox> BodyHost;
    TSharedPtr<SSplitter> VerticalSplitter, MainSplitter, CenterSplitter;

    void RebuildBody();
    void SetDensity(EDensity Value);
    void SetCenter(ECenter Value);
    void SetLibraryMode(ELibrary Value);
    void SaveSettings() const;
    TSharedRef<SWidget> DensityMenu();
    TSharedRef<SWidget> CenterButton(const FText& Text, ECenter Value);
    TSharedRef<SWidget> LibraryButton(const FText& Text, ELibrary Value);
};
