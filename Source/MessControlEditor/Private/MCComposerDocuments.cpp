#include "MCComposerDocuments.h"
#include "MCVFXActionSession.h"
#include "MCPerkActionPreviewPanel.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
    TMap<FName,TSharedPtr<FMCVFXActionSession>> ComposerDocuments;
}

TSharedPtr<FMCVFXActionSession> MCComposerDocuments::Acquire(FName ActionID,FString& Error)
{
    Error.Reset();
    if(!IsInGameThread() || ActionID.IsNone())
    {Error=TEXT("Документ Composer открывается на потоке редактора по стабильному ID действия.");return nullptr;}
    if(const auto* Existing=ComposerDocuments.Find(ActionID))return *Existing;
    auto Document=MakeShared<FMCVFXActionSession>();
    if(!Document->Initialize(ActionID,Error))return nullptr;
    ComposerDocuments.Add(ActionID,Document);
    return Document;
}

TSharedPtr<FMCVFXActionSession> MCComposerDocuments::Find(FName ActionID)
{
    if(!IsInGameThread())return nullptr;
    const auto* Document=ComposerDocuments.Find(ActionID);
    return Document?*Document:nullptr;
}

FString MCComposerDocuments::GetSnapshotJson()
{
    check(IsInGameThread());
    TArray<FName> IDs;ComposerDocuments.GetKeys(IDs);
    IDs.Sort([](FName A,FName B){return A.LexicalLess(B);});
    TArray<TSharedPtr<FJsonValue>> Documents;
    for(FName ID:IDs)
    {
        const auto Document=Find(ID);TSharedPtr<FJsonObject> State;
        if(Document&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Document->GetSnapshotJson()),State)&&State)
            Documents.Add(MakeShared<FJsonValueObject>(State));
    }
    const auto Root=MakeShared<FJsonObject>();Root->SetArrayField(TEXT("documents"),Documents);
    FString Result;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Result));return Result;
}

void MCComposerDocuments::NotifyOwnedCommit(const FMCVFXCanonicalProfileState& ExpectedBefore)
{
    check(IsInGameThread());
    // Canonical reads can load packages and invoke editor callbacks. Hold a stable
    // snapshot rather than iterating the registry while those callbacks run.
    TArray<TSharedPtr<FMCVFXActionSession>> Documents;
    ComposerDocuments.GenerateValueArray(Documents);
    for(const auto& Document:Documents)
        if(Document.IsValid())Document->RebaseOwnerDigestAfterOwnedCommit(ExpectedBefore);
}

void MCComposerDocuments::Shutdown()
{
    check(IsInGameThread());
    MCPerkActionPreviewPanel::ResetPreview();
    ComposerDocuments.Reset();
}
