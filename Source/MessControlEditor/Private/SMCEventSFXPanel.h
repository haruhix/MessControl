#pragma once
// Same-event authoring only; native playback and transactions belong to Session.
#include "CoreMinimal.h"
#include "MCEventSFX.h"
#include "Widgets/SCompoundWidget.h"

struct FMCSFXOriginalSourceView
{
    FName ID,NativeEvent;
    FGuid GroupID;
    FText Label;
    FText Detail;
    FSoftObjectPath Asset;
    FString SourceOwner,SourceProperty;
    bool bRuntimeHookMapped=false;
};
struct FMCSFXEditorScope
{
    FName SourceID,Event;
    FGuid DraftToken;
};
struct FMCSFXPanelContext
{
    FMCSFXEditorScope Scope;
    FText EventLabel;
    FMCEventSFXSlot Slot;
    FGuid SelectedLayer;
    TArray<FMCSFXOriginalSourceView> Originals;
    bool bCanEdit=false,bCanSuppressOriginal=false,bCanAudition=false;
    bool bGroupEnabled=true,bGroupMuted=false;
    FText OriginalReason,AuditionReason;
    TFunction<bool(FMCSFXEditorScope,EMCEventSFXMode,FString&)> SetMode;
    TFunction<bool(FMCSFXEditorScope,FGuid,FMCEventSFXLayer&,FString&)> ReadLayer;
    TFunction<bool(FMCSFXEditorScope,FGuid,const FMCEventSFXLayer&,FString&)> SetLayer;
    TFunction<bool(FMCSFXEditorScope,FGuid,FString&)> RemoveLayer,DuplicateLayer;
    TFunction<void(FGuid)> SelectLayer;
    TFunction<void(const FMCSFXOriginalSourceView&)> OpenSource;
    TFunction<void()> Refresh;
    TFunction<void(const FString&)> ShowError;
};

class SMCEventSFXPanel final:public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMCEventSFXPanel){}SLATE_ARGUMENT(FMCSFXPanelContext,Context)SLATE_END_ARGS()
    void Construct(const FArguments& Args);
private:
    FMCSFXPanelContext Context;
    TSharedRef<SWidget> ModeMenu();
    TSharedRef<SWidget> LayerDetails(const FMCEventSFXLayer& Layer);
    void Edit(FMCEventSFXLayer Layer,TFunction<void(FMCEventSFXLayer&)> Change);
    FText ModeLabel(EMCEventSFXMode Mode)const;
};
