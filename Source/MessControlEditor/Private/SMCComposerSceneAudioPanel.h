#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FMCComposerSceneAudioSession;
class UMCSoundPalette;
struct FMCSoundScene;
struct FAssetData;

enum class EMCComposerSceneAudioPanelSection : uint8 { Header, Layers, All };
DECLARE_DELEGATE_OneParam(FMCComposerSceneSelected,FName);

/** Scene controls embedded in the shared Composer; both sections retain one draft session. */
class SMCComposerSceneAudioPanel final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMCComposerSceneAudioPanel)
        :_Scene(NAME_None),_Section(EMCComposerSceneAudioPanelSection::All),_CanPreview(false),_IsActive(true){}
        SLATE_ARGUMENT(TSharedPtr<FMCComposerSceneAudioSession>,Session)
        SLATE_ATTRIBUTE(FName,Scene)
        SLATE_ARGUMENT(EMCComposerSceneAudioPanelSection,Section)
        SLATE_ATTRIBUTE(bool,CanPreview)
        SLATE_ATTRIBUTE(bool,IsActive)
        SLATE_EVENT(FMCComposerSceneSelected,OnSceneSelected)
        SLATE_EVENT(FSimpleDelegate,OnChanged)
        SLATE_EVENT(FSimpleDelegate,OnPlay)
        SLATE_EVENT(FSimpleDelegate,OnStop)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual void Tick(const FGeometry& Geometry,double CurrentTime,float DeltaTime) override;

private:
    struct FEditScope
    {
        TWeakObjectPtr<UMCSoundPalette> Source;
        FName Scene;
        uint64 Revision=0;
    };
    TSharedPtr<FMCComposerSceneAudioSession> Session;
    TAttribute<FName> Scene;
    TAttribute<bool> CanPreview,IsActive;
    EMCComposerSceneAudioPanelSection Section=EMCComposerSceneAudioPanelSection::All;
    FMCComposerSceneSelected OnSceneSelected;
    FSimpleDelegate OnChanged,OnPlay,OnStop;
    uint64 SeenRevision=MAX_uint64;
    FName SeenScene;
    TWeakObjectPtr<UMCSoundPalette> SeenSource;
    FString LocalStatus;

    FEditScope CaptureScope() const;
    bool IsScopeCurrent(const FEditScope& Scope) const;
    void Rebuild();
    TSharedRef<SWidget> BuildHeader();
    TSharedRef<SWidget> BuildLayers();
    TSharedRef<SWidget> BuildLayer(const FText& Label,bool bMusic);
    void PickPalette(const FAssetData& Asset,const FEditScope& Scope);
    void Edit(const FEditScope& Scope,TFunction<void(FMCSoundScene&)> Mutation);
    void Apply(const FEditScope& Scope);
    void Revert(const FEditScope& Scope);
    void Changed();
};
