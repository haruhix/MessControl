#include "MCVFXComposerToolset.h"

#include "MCPerkActionPreviewPanel.h"
#include "MCPerkPreviewEditorLibrary.h"
#include "MCNiagaraMaterialContextComponent.h"
#include "MCToothCharacter.h"
#include "MCVFXActionSession.h"
#include "MCVFXActionWorkspace.h"
#include "MCWorldPreloadSubsystem.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/PackageName.h"
#include "NiagaraSystem.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"

namespace MCVFXComposerMCP
{
    struct FComposerMCPPlayback
    {
        FGuid Token;
        TWeakPtr<FMCPerkPreviewRunner> Runner;
        TWeakObjectPtr<AMCToothCharacter> Hero;
    };
    FComposerMCPPlayback OwnedPlayback;

    FString Write(const TSharedRef<FJsonObject>& Data)
    {
        FString Result;
        FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Result));
        return Result;
    }

    TSharedPtr<FJsonObject> Read(const FString& Json)
    {
        TSharedPtr<FJsonObject> Result;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Result);
        return Result;
    }

    TSharedRef<FJsonObject> Result(bool bOk,const FString& Error=FString())
    {
        auto Data=MakeShared<FJsonObject>();
        Data->SetBoolField(TEXT("ok"),bOk);
        if(!bOk)Data->SetStringField(TEXT("error"),Error);
        return Data;
    }

    FString Failure(const FString& Error)
    {
        return Write(Result(false,Error));
    }

    void AddDraftIdentity(const TSharedRef<FJsonObject>& Data,const FMCVFXActionSession& Session)
    {
        Data->SetStringField(TEXT("action"),Session.GetActionID().ToString());
        Data->SetStringField(TEXT("draft_token"),Session.GetDraftToken().ToString());
        Data->SetNumberField(TEXT("revision"),double(Session.GetRevision()));
        Data->SetBoolField(TEXT("runtime_dirty"),Session.IsRuntimeDirty());
    }

    bool CheckEditor(FString& Error,bool bMutating=false)
    {
        if(!IsInGameThread()||!GIsEditor||!GEditor||IsRunningCommandlet()||!FSlateApplication::IsInitialized())
        {Error=TEXT("VFX Composer MCP requires the interactive editor game thread.");return false;}
        if(bMutating&&GEditor->IsPlaySessionInProgress())
        {Error=TEXT("VFX Composer MCP cannot change state while PIE/SIE is active or queued.");return false;}
        return true;
    }

    bool CheckIdle(FString& Error)
    {
        if(FSlateApplication::Get().IsDragDropping())
        {Error=TEXT("A drag operation is in progress. Composer state was preserved.");return false;}
        const auto Runner=MCPerkActionPreviewPanel::GetActiveRunner();
        const auto Workspace=Read(MCVFXActionWorkspace::GetSnapshotJson());
        bool bWorkspaceBusy=false;
        if(Workspace)for(const TCHAR* Field:{TEXT("playing"),TEXT("paused"),TEXT("audition_pending"),TEXT("sfx_audition")}) {
            bool bValue=false;
            bWorkspaceBusy|=Workspace->TryGetBoolField(Field,bValue)&&bValue;
        }
        if(bWorkspaceBusy||(Runner&&(Runner->IsRunning()||Runner->IsPaused())))
        {Error=TEXT("A preview or audition is already active. Composer state was preserved.");return false;}
        return true;
    }

    TSharedPtr<FMCVFXActionSession> GetExactSession(FName Action,FString& Error)
    {
        const auto Session=MCVFXActionWorkspace::GetActiveSession();
        if(Action.IsNone()||!Session||Session->GetActionID()!=Action||!Session->IsContextCurrent())
        {Error=TEXT("The expected action is not selected in the current Composer workspace.");return nullptr;}
        return Session;
    }

    TSharedPtr<FMCVFXActionSession> GetExactDraft(FName Action,const FString& Token,int64 Revision,FString& Error)
    {
        if(!CheckEditor(Error,true)||!CheckIdle(Error))return nullptr;
        const auto Session=GetExactSession(Action,Error);
        if(!Session)return nullptr;
        FGuid Parsed;
        if(!FGuid::Parse(Token,Parsed)||!Parsed.IsValid()||Session->GetDraftToken()!=Parsed
            ||Revision<0||Session->GetRevision()!=uint64(Revision))
        {Error=TEXT("The Composer draft token or revision changed. Read GetWorkspace before retrying.");return nullptr;}
        if(!Session->CanEditProfile())
        {Error=Session->GetEditUnavailableReason();return nullptr;}
        return Session;
    }
}

FString UMCVFXComposerToolset::GetCatalogue(const FString& Query,int32 Offset,int32 Limit,bool bIncludeCanonical)
{
    using namespace MCVFXComposerMCP;
    FString Error;
    if(!CheckEditor(Error))return Failure(Error);
    if(Offset<0||Limit<1||Limit>50)return Failure(TEXT("Use a non-negative offset and a page size from 1 to 50."));
    const auto Catalogue=Read(UMCPerkPreviewEditorLibrary::GetVFXComposerCatalogueJson(bIncludeCanonical));
    const TArray<TSharedPtr<FJsonValue>>* Actions=nullptr;
    if(!Catalogue||!Catalogue->TryGetArrayField(TEXT("actions"),Actions))
        return Failure(TEXT("The Composer catalogue snapshot is unavailable."));

    const FString Search=Query.TrimStartAndEnd();
    TArray<TSharedPtr<FJsonValue>> Page;
    int32 Matched=0;
    for(const auto& Action:*Actions) {
        const auto Row=Action->AsObject();
        if(!Row)continue;
        if(!Search.IsEmpty()&&!Row->GetStringField(TEXT("id")).Contains(Search)
            &&!Row->GetStringField(TEXT("label")).Contains(Search)&&!Row->GetStringField(TEXT("category")).Contains(Search))continue;
        if(Matched++>=Offset&&Page.Num()<Limit)Page.Add(Action);
    }
    const int32 Count=Page.Num();
    Catalogue->SetArrayField(TEXT("actions"),MoveTemp(Page));
    Catalogue->SetBoolField(TEXT("ok"),true);
    Catalogue->SetNumberField(TEXT("offset"),Offset);
    Catalogue->SetNumberField(TEXT("returned"),Count);
    Catalogue->SetNumberField(TEXT("total_matches"),Matched);
    Catalogue->SetBoolField(TEXT("has_more"),int64(Offset)+Count<Matched);
    return Write(Catalogue.ToSharedRef());
}

FString UMCVFXComposerToolset::GetWorkspace()
{
    using namespace MCVFXComposerMCP;
    FString Error;
    return CheckEditor(Error)?MCVFXActionWorkspace::GetSnapshotJson():Failure(Error);
}

FString UMCVFXComposerToolset::GetPreview()
{
    using namespace MCVFXComposerMCP;
    FString Error;
    return CheckEditor(Error)?UMCPerkPreviewEditorLibrary::GetPerkPreviewSnapshotJson():Failure(Error);
}

FString UMCVFXComposerToolset::GetVisualReadiness(const TArray<FString>& ObjectPaths,bool bUsePIE)
{
    using namespace MCVFXComposerMCP;
    FString Error;
    if(!CheckEditor(Error))return Failure(Error);
    if(ObjectPaths.Num()>32)return Failure(TEXT("Inspect at most 32 exact object paths at a time."));
    const auto Runner=MCPerkActionPreviewPanel::GetActiveRunner();
    UWorld* World=bUsePIE?GEditor->PlayWorld.Get():Runner?Runner->GetWorld():nullptr;
    if(!IsValid(World))return Failure(TEXT("The requested live preview world is unavailable."));
    const auto* Loading=World->GetSubsystem<UMCWorldPreloadSubsystem>();
    if(!Loading)return Failure(TEXT("The requested world has no visual preparation subsystem."));
    const auto* Context=Loading->GetMaterialContextResources();
    auto Data=Result(true);
    Data->SetStringField(TEXT("world"),World->GetPathName());
    Data->SetStringField(TEXT("phase"),UEnum::GetValueAsString(Loading->GetPhase()));
    Data->SetBoolField(TEXT("ready"),Loading->IsReady());
    Data->SetBoolField(TEXT("material_context_ready"),Context&&Context->IsReadyForPlayback(World));
    TArray<TSharedPtr<FJsonValue>> Rows;
    for(const FString& Path:ObjectPaths) {
        const FSoftObjectPath Asset(Path);
        if(!Asset.IsValid()||Path.IsEmpty())return Failure(TEXT("Use exact Unreal object paths such as /Game/Folder/Asset.Asset."));
        const UObject* Object=Asset.ResolveObject();
        auto Row=MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("path"),Path);
        Row->SetBoolField(TEXT("loaded"),IsValid(Object));
        Row->SetBoolField(TEXT("retained"),IsValid(Object)&&Loading->IsVisualResourceRetained(Object));
        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    Data->SetArrayField(TEXT("resources"),MoveTemp(Rows));
    return Write(Data);
}

FString UMCVFXComposerToolset::SelectAction(FName ActionID)
{
    using namespace MCVFXComposerMCP;
    FString Error;
    if(!CheckEditor(Error,true))return Failure(Error);
    if(ActionID.IsNone())return Failure(TEXT("Select an exact action ID from GetCatalogue."));
    if(const auto Session=GetExactSession(ActionID,Error)) {
        auto Data=Result(true);
        AddDraftIdentity(Data,*Session);
        return Write(Data);
    }
    if(!CheckIdle(Error))return Failure(Error);
    if(const auto Current=MCVFXActionWorkspace::GetActiveSession();Current&&Current->HasUnsavedPackChanges())
        return Failure(TEXT("The open Composer pack has unapplied changes. Its draft was preserved."));
    if(!MCVFXActionWorkspace::GetActiveSession())UMCPerkPreviewEditorLibrary::OpenVFXComposer();
    Error.Reset();
    if(!UMCPerkPreviewEditorLibrary::SelectVFXComposerAction(ActionID,Error)) {
        auto Data=Result(false,Error);
        FString Selected;
        if(const auto Workspace=Read(MCVFXActionWorkspace::GetSnapshotJson());Workspace&&Workspace->TryGetStringField(TEXT("selected_action"),Selected))
            Data->SetStringField(TEXT("selected_action"),Selected);
        return Write(Data);
    }
    const auto Session=GetExactSession(ActionID,Error);
    if(!Session)return Failure(Error);
    auto Data=Result(true);
    AddDraftIdentity(Data,*Session);
    return Write(Data);
}

FString UMCVFXComposerToolset::PlayScenario(FName ExpectedAction,const FString& Scenario)
{
    using namespace MCVFXComposerMCP;
    FString Error;
    if(!CheckEditor(Error,true)||!CheckIdle(Error))return Failure(Error);
    if(!GetExactSession(ExpectedAction,Error))return Failure(Error);
    UMCPerkPreviewEditorLibrary::OpenVFXComposer();
    if(!UMCPerkPreviewEditorLibrary::PlayVFXComposerAction(Scenario,false,Error))return Failure(Error);

    const auto Runner=MCPerkActionPreviewPanel::GetActiveRunner();
    if(!Runner||!IsValid(Runner->GetHero())||Runner->GetPerkID()!=ExpectedAction) {
        UMCPerkPreviewEditorLibrary::StopVFXComposerAction();
        return Failure(TEXT("The requested isolated run did not provide a matching preview identity."));
    }
    // Starting a real run resets the sandbox and creates a new hero. A later
    // Slate button or another preview client therefore cannot inherit this lease.
    OwnedPlayback={FGuid::NewGuid(),Runner,Runner->GetHero()};
    auto Data=Result(true);
    Data->SetStringField(TEXT("action"),ExpectedAction.ToString());
    Data->SetStringField(TEXT("scenario"),Scenario);
    Data->SetStringField(TEXT("playback_token"),OwnedPlayback.Token.ToString());
    return Write(Data);
}

FString UMCVFXComposerToolset::StopPreview(FName ExpectedAction,const FString& PlaybackToken)
{
    using namespace MCVFXComposerMCP;
    FString Error;
    if(!CheckEditor(Error,true)||!GetExactSession(ExpectedAction,Error))return Failure(Error);
    FGuid Token;
    const auto Runner=MCPerkActionPreviewPanel::GetActiveRunner();
    if(!FGuid::Parse(PlaybackToken,Token)||!Token.IsValid()||Token!=OwnedPlayback.Token
        ||!Runner||Runner!=OwnedPlayback.Runner.Pin()||Runner->GetPerkID()!=ExpectedAction
        ||!OwnedPlayback.Hero.IsValid()||Runner->GetHero()!=OwnedPlayback.Hero.Get())
        return Failure(TEXT("The requested playback is stale or owned by another preview client."));
    UMCPerkPreviewEditorLibrary::StopVFXComposerAction();
    OwnedPlayback={};
    return Write(Result(true));
}

FString UMCVFXComposerToolset::AddSystemToGroup(FName ExpectedAction,const FString& DraftToken,int64 ExpectedRevision,
    FName Event,const FString& GroupID,UNiagaraSystem* System)
{
    using namespace MCVFXComposerMCP;
    FString Error;
    const auto Session=GetExactDraft(ExpectedAction,DraftToken,ExpectedRevision,Error);
    if(!Session)return Failure(Error);
    if(!IsValid(System)||System->IsTemplate()||System->HasAnyFlags(RF_Transient)
        ||!System->GetOutermost()->GetName().StartsWith(TEXT("/Game/"))
        ||!FPackageName::DoesPackageExist(System->GetOutermost()->GetName()))
        return Failure(TEXT("Add a saved Niagara System from /Game."));
    FGuid Group;
    if(!GroupID.IsEmpty()&&!FGuid::Parse(GroupID,Group))return Failure(TEXT("GroupID must be a GUID or empty."));
    const FGuid Layer=Session->AddSystemToGroup(Session->GetDraftToken(),Event,System,Group,Error);
    if(!Layer.IsValid())return Failure(Error);
    auto Data=Result(true);
    AddDraftIdentity(Data,*Session);
    Data->SetStringField(TEXT("event"),Event.ToString());
    Data->SetStringField(TEXT("layer_id"),Layer.ToString());
    if(!Error.IsEmpty())Data->SetStringField(TEXT("warning"),Error);
    return Write(Data);
}

FString UMCVFXComposerToolset::ApplyDraftToGame(FName ExpectedAction,const FString& DraftToken,int64 ExpectedRevision,bool bConfirmApply)
{
    using namespace MCVFXComposerMCP;
    if(!bConfirmApply)return Failure(TEXT("ConfirmApply must be true to write the canonical game profile."));
    FString Error;
    const auto Session=GetExactDraft(ExpectedAction,DraftToken,ExpectedRevision,Error);
    if(!Session)return Failure(Error);
    if(!Session->IsRuntimeDirty())return Failure(TEXT("This draft has no runtime changes to apply."));
    if(!Session->GetLoadedPackPath().IsNull()&&!Session->HasUnsavedPackChanges())
        return Failure(TEXT("This is a saved pack. Apply it using the Composer pack workflow."));
    const bool bApplied=Session->ApplyAll(Error);
    auto Data=Result(bApplied,Error);
    AddDraftIdentity(Data,*Session);
    Data->SetStringField(TEXT("status"),Session->GetStatus());
    Data->SetStringField(TEXT("canonical_owner"),Session->GetCanonicalOwner().ToString());
    return Write(Data);
}
