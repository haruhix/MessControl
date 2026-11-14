#include "MCSFXComposerEditorLibrary.h"

#include "MCSFXComposerWorkspace.h"
#include "MCComposerDocuments.h"
#include "MCComposerReview.h"
#include "MCComposerAudioProbe.h"
#include "MCPerkPreviewAdapter.h"

void UMCSFXComposerEditorLibrary::OpenSFXComposer(){MCSFXComposerWorkspace::Open();}
FString UMCSFXComposerEditorLibrary::GetSFXComposerSnapshotJson(){return MCSFXComposerWorkspace::GetSnapshotJson();}
FString UMCSFXComposerEditorLibrary::GetSharedComposerDocumentsSnapshotJson(){return MCComposerDocuments::GetSnapshotJson();}
FString UMCSFXComposerEditorLibrary::GetComposerReviewSnapshotJson(){return MCComposerReview::GetSnapshotJson();}
bool UMCSFXComposerEditorLibrary::StartPIEComposerAudioProbe(FString& Error){return MCComposerAudioProbe::Start(Error);}
void UMCSFXComposerEditorLibrary::StopPIEComposerAudioProbe(){MCComposerAudioProbe::Stop();}
FString UMCSFXComposerEditorLibrary::GetPIEComposerAudioProbeSnapshotJson(){return MCComposerAudioProbe::SnapshotJson();}
bool UMCSFXComposerEditorLibrary::SelectSFXComposerTarget(FName Action,FName Event,FGuid Layer,FString& Error)
{return MCSFXComposerWorkspace::SelectForReview(Action,Event,Layer,Error);}
bool UMCSFXComposerEditorLibrary::PlaySFXComposerAction(const FString& Scenario,bool bLoop,FString& Error)
{
    EMCPerkPreviewScenario Mode;
    if(Scenario==TEXT("Target"))Mode=EMCPerkPreviewScenario::Target;
    else if(Scenario==TEXT("NearWall"))Mode=EMCPerkPreviewScenario::NearWall;
    else if(Scenario==TEXT("Timeout"))Mode=EMCPerkPreviewScenario::Timeout;
    else if(Scenario==TEXT("Acquire"))Mode=EMCPerkPreviewScenario::Acquire;
    else {Error=TEXT("Неизвестный сценарий SFX Composer.");return false;}
    return MCSFXComposerWorkspace::PlayForReview(Mode,bLoop,Error);
}
void UMCSFXComposerEditorLibrary::StopSFXComposerAction(){MCSFXComposerWorkspace::StopForReview();}
bool UMCSFXComposerEditorLibrary::ResizeSFXComposerForReview(FVector2D ClientSize){return MCSFXComposerWorkspace::ResizeForReview(ClientSize);}
