#include "MCComposerDocumentView.h"
#include "MCComposerDocuments.h"
#include "MCVFXActionSession.h"
#include "MCPerkActionPreviewPanel.h"
#include "MCPerkPreviewRunner.h"
#include "MCDataAssets.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"

FMCComposerDocumentView::FMCComposerDocumentView():ViewID(FGuid::NewGuid()){}
FMCComposerDocumentView::~FMCComposerDocumentView(){Stop();}

bool FMCComposerDocumentView::Select(FName ActionID,FString& Error)
{
    const auto Next=MCComposerDocuments::Acquire(ActionID,Error);
    if(!Next)return false;
    if(Next!=Document){Stop();Document=Next;SetCamera=nullptr;RefreshPreviewHosts();}
    return true;
}

bool FMCComposerDocumentView::OwnsPreview() const
{return Document && MCPerkActionPreviewPanel::OwnsPreview(ViewID,PreviewEpoch)
    && MCPerkActionPreviewPanel::GetContextPerkID()==Document->GetActionID();}

TSharedPtr<FMCPerkPreviewRunner> FMCComposerDocumentView::GetRunner() const
{return OwnsPreview()?MCPerkActionPreviewPanel::GetOwnedRunner(ViewID,PreviewEpoch):nullptr;}

bool FMCComposerDocumentView::Start(FName Event,USoundBase* Sound,UNiagaraSystem* System,
    EMCPerkPreviewScenario Scenario,FString& Error)
{
    Error.Reset();
    if(!Document || !Document->CanPreview())
    {Error=Document?Document->GetPreviewUnavailableReason():TEXT("Выберите действие.");return false;}
    if(Sound&&!bAudioEnabled){Error=TEXT("Включите звук предпросмотра перед примеркой.");return false;}
    const auto RequestedDocument=Document;
    const FGuid DraftToken=Document->GetDraftToken();
    const uint64 DraftRevision=Document->GetRevision();
    UMCPerkVFXProfile* Profile=nullptr;
    if(!Document->BuildPreviewProfile(Profile,Error,Event,Sound,System,bUseDocumentPreviewMix))return false;
    if(Document!=RequestedDocument||Document->GetDraftToken()!=DraftToken||Document->GetRevision()!=DraftRevision)
    {Error=TEXT("Действие или черновик изменились во время подготовки. Запустите актуальный сценарий заново.");return false;}
    uint64 Epoch=0;
    if(!MCPerkActionPreviewPanel::ClaimPreview(Document->GetActionID(),ViewID,Epoch,Error))return false;
    PreviewEpoch=Epoch;
    const auto Runner=GetRunner();
    if(!Runner){Error=TEXT("Не удалось получить принадлежащую окну песочницу.");Stop();return false;}
    if(!Runner->SetAudioEnabled(bAudioEnabled,Error)){Stop();return false;}
    if(ScenePalette.IsValid())
    {
        Runner->SetSceneAudioMix(MusicGain,AmbienceGain);
        if(!Runner->SetSceneAudio(ScenePalette.Get(),Scene,Error)){Stop();return false;}
    }
    else Runner->ClearSceneAudio();
    if(!Runner->StartAction(Document->GetActionID(),Scenario,Profile))
    {Error=Runner->GetStatus();Stop();return false;}
    RefreshPreviewHosts();
    return true;
}

bool FMCComposerDocumentView::Play(EMCPerkPreviewScenario Scenario,FString& Error)
{return Start(NAME_None,nullptr,nullptr,Scenario,Error);}
bool FMCComposerDocumentView::AuditionSound(FName Event,USoundBase* Sound,EMCPerkPreviewScenario Scenario,FString& Error)
{if(!Sound){Error=TEXT("Выберите звук из библиотеки.");return false;}return Start(Event,Sound,nullptr,Scenario,Error);}
bool FMCComposerDocumentView::AuditionSystem(FName Event,UNiagaraSystem* System,EMCPerkPreviewScenario Scenario,FString& Error)
{if(!System){Error=TEXT("Выберите Niagara System из библиотеки.");return false;}return Start(Event,nullptr,System,Scenario,Error);}

void FMCComposerDocumentView::Stop()
{MCPerkActionPreviewPanel::ReleasePreview(ViewID,PreviewEpoch);PreviewEpoch=0;}
void FMCComposerDocumentView::Pause(bool bPaused)
{if(const auto Runner=GetRunner())Runner->SetPaused(bPaused);}

TSharedRef<SWidget> FMCComposerDocumentView::MakePreviewWidget()
{
    const TWeakPtr<FMCComposerDocumentView> Weak=SharedThis(this);
    TSharedPtr<SBox> Host;
    auto Widget=SNew(SWidgetSwitcher).WidgetIndex_Lambda([Weak]{const auto View=Weak.Pin();return View&&View->OwnsPreview()?0:1;})
        +SWidgetSwitcher::Slot()[SAssignNew(Host,SBox)]
        +SWidgetSwitcher::Slot()[SNew(STextBlock).AutoWrapText(true).Text_Lambda([Weak]{const auto View=Weak.Pin();
            if(!View||!View->Document)return FText::FromString(TEXT("Выберите действие."));
            return FText::FromString(View->Document->CanPreview()
                ?TEXT("Нажмите «Играть действие», чтобы активировать предпросмотр этого окна. Выбор события не переключает чужой сценарий.")
                :View->Document->GetPreviewUnavailableReason());})];
    PreviewHosts.Add(Host);RefreshPreviewHosts();return Widget;
}

void FMCComposerDocumentView::RefreshPreviewHosts()
{
    PreviewHosts.RemoveAll([](const auto& Host){return !Host.IsValid();});
    if(!OwnsPreview())return;
    for(const auto& Weak:PreviewHosts)if(const auto Host=Weak.Pin())
    {
        Host->SetContent(MCPerkActionPreviewPanel::MakeSharedActionViewport(&SetCamera));
        if(SetCamera)SetCamera(CameraPreset);
    }
}
void FMCComposerDocumentView::SetPreviewView(int32 Preset)
{CameraPreset=Preset;if(OwnsPreview()&&SetCamera)SetCamera(Preset);}

bool FMCComposerDocumentView::SetPreviewAudioEnabled(bool bEnabled,FString& Error)
{
    Error.Reset();
    if(const auto Runner=GetRunner())if(!Runner->SetAudioEnabled(bEnabled,Error))return false;
    bAudioEnabled=bEnabled;return true;
}
bool FMCComposerDocumentView::IsPreviewAudioEnabled() const
{if(const auto Runner=GetRunner())return Runner->IsAudioEnabled();return bAudioEnabled;}
FString FMCComposerDocumentView::GetPreviewAudioStatus() const
{if(const auto Runner=GetRunner())return Runner->GetAudioStatus();return bAudioEnabled
    ?TEXT("Звук включится в изолированной песочнице при запуске этого окна."):TEXT("Звук предпросмотра этого окна выключен.");}

bool FMCComposerDocumentView::SetSceneAudio(UMCSoundPalette* Palette,FName InScene,float Music,float Ambience,FString& Error)
{
    Error.Reset();
    if(!IsValid(Palette)||!FMath::IsFinite(Music)||!FMath::IsFinite(Ambience))
    {Error=TEXT("Не найдена палитра сцены или некорректен уровень прослушивания.");return false;}
    ScenePalette.Reset(Palette);Scene=InScene;MusicGain=FMath::Clamp(Music,0.f,2.f);AmbienceGain=FMath::Clamp(Ambience,0.f,2.f);
    if(const auto Runner=GetRunner())
    {Runner->SetSceneAudioMix(MusicGain,AmbienceGain);return Runner->SetSceneAudio(Palette,Scene,Error);}
    return true;
}
void FMCComposerDocumentView::ClearSceneAudio()
{ScenePalette.Reset();Scene=NAME_None;if(const auto Runner=GetRunner())Runner->ClearSceneAudio();}

FString FMCComposerDocumentView::GetSnapshotJson() const
{
    const auto Data=MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("view_id"),ViewID.ToString());Data->SetStringField(TEXT("action"),Document?Document->GetActionID().ToString():FString());
    Data->SetBoolField(TEXT("owns_preview"),OwnsPreview());Data->SetNumberField(TEXT("preview_epoch"),double(PreviewEpoch));
    Data->SetBoolField(TEXT("audio_enabled"),IsPreviewAudioEnabled());Data->SetStringField(TEXT("scene"),Scene.ToString());
    FString Result;const auto Writer=TJsonWriterFactory<>::Create(&Result);FJsonSerializer::Serialize(Data,Writer);return Result;
}
