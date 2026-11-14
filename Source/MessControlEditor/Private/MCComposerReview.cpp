#include "MCComposerReview.h"

#include "MCComposerDocuments.h"
#include "MCVFXActionSession.h"
#include "MCVFXNativePresentationSettings.h"
#include "MCVFXRecipePreset.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#else
#include <cstdio>
#endif

namespace
{
FString LaneName(EMCComposerReviewLane Lane){return Lane==EMCComposerReviewLane::SFX?TEXT("SFX"):TEXT("VFX");}
FString Key(FName Action,FName Event,EMCComposerReviewLane Lane)
{const FString A=Action.ToString(),E=Event.ToString();return FString::FromInt(A.Len())+TEXT(":")+A+FString::FromInt(E.Len())+TEXT(":")+E+LaneName(Lane);}
FString Digest(const FString& Value){FTCHARToUTF8 Bytes(*Value);return FMD5::HashBytes(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length());}
template<typename T> FString StructText(const T& Value)
{
    FString Text;FJsonObjectConverter::UStructToJsonObjectString(T::StaticStruct(),&Value,Text,0,0);return Text;
}
struct FReviewEntry
{
    FName Action,Event;
    EMCComposerReviewLane Lane=EMCComposerReviewLane::VFX;
    FString Fingerprint,ApprovedAt;
};
}

struct FMCComposerReviewStore::FImpl
{
    explicit FImpl(FString InPath):Path(MoveTemp(InPath)){}
    FString Path,ReadError;
    bool bLoaded=false;
    TMap<FString,FReviewEntry> Entries;
    bool Load()
    {
        if(bLoaded)return ReadError.IsEmpty();
        bLoaded=true;Entries.Reset();ReadError.Reset();
        if(!IFileManager::Get().FileExists(*Path))return true;
        FString Text;TSharedPtr<FJsonObject> Root;double Version=0;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
        if(!FFileHelper::LoadFileToString(Text,*Path)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root
            ||!Root->TryGetNumberField(TEXT("version"),Version)||Version!=1||!Root->TryGetArrayField(TEXT("reviews"),Rows))
        {ReadError=TEXT("Не удалось прочитать отметки Composer. Существующий файл не перезаписан: ")+Path;return false;}
        for(const auto& Row:*Rows)
        {
            const TSharedPtr<FJsonObject>* Object=nullptr;
            if(Row.IsValid())Row->TryGetObject(Object);
            FString Action,Event,Lane,Hash,At;
            if(!Object||!Object->IsValid()||!(*Object)->TryGetStringField(TEXT("action"),Action)||!(*Object)->TryGetStringField(TEXT("event"),Event)
                ||!(*Object)->TryGetStringField(TEXT("lane"),Lane)||!(*Object)->TryGetStringField(TEXT("fingerprint"),Hash)
                ||!(*Object)->TryGetStringField(TEXT("approved_at"),At)||Action.IsEmpty()||Event.IsEmpty()||Hash.IsEmpty()||(Lane!=TEXT("VFX")&&Lane!=TEXT("SFX")))
            {Entries.Reset();ReadError=TEXT("Повреждена запись одобрения. Файл не перезаписан: ")+Path;return false;}
            FReviewEntry Entry{FName(*Action),FName(*Event),Lane==TEXT("SFX")?EMCComposerReviewLane::SFX:EMCComposerReviewLane::VFX,Hash,At};
            const FString EntryKey=Key(Entry.Action,Entry.Event,Entry.Lane);
            if(Entries.Contains(EntryKey)){Entries.Reset();ReadError=TEXT("Повторная запись одобрения. Файл не перезаписан: ")+Path;return false;}
            Entries.Add(EntryKey,MoveTemp(Entry));
        }
        return true;
    }
    FString Json() const
    {
        auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("version"),1);
        TArray<FString> Keys;Entries.GetKeys(Keys);Keys.Sort();TArray<TSharedPtr<FJsonValue>> Rows;
        for(const auto& EntryKey:Keys)
        {
            const auto& E=Entries[EntryKey];auto Row=MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("action"),E.Action.ToString());Row->SetStringField(TEXT("event"),E.Event.ToString());Row->SetStringField(TEXT("lane"),LaneName(E.Lane));
            Row->SetStringField(TEXT("fingerprint"),E.Fingerprint);Row->SetStringField(TEXT("approved_at"),E.ApprovedAt);Rows.Add(MakeShared<FJsonValueObject>(Row));
        }
        Root->SetArrayField(TEXT("reviews"),Rows);FString Result;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Result));return Result;
    }
};

FMCComposerReviewStore::FMCComposerReviewStore(FString Path):Impl(MakeUnique<FImpl>(MoveTemp(Path))){}
FMCComposerReviewStore::~FMCComposerReviewStore()=default;
void FMCComposerReviewStore::Reload(){Impl->bLoaded=false;Impl->Load();}
FString FMCComposerReviewStore::GetSnapshotJson(){Impl->Load();return Impl->Json();}
FMCComposerReviewStatus FMCComposerReviewStore::Query(FName Action,FName Event,EMCComposerReviewLane Lane,const FString& Current)
{
    FMCComposerReviewStatus State;State.bCurrentKnown=!Current.IsEmpty();
    if(!Impl->Load()){State.Detail=Impl->ReadError;return State;}
    const auto* Entry=Impl->Entries.Find(Key(Action,Event,Lane));
    if(!Entry){State.Detail=LaneName(Lane)+TEXT(" ещё не одобрено. Отметку ставит автор после проверки.");return State;}
    State.bApproved=true;State.bStale=State.bCurrentKnown&&Entry->Fingerprint!=Current;
    State.Detail=State.bStale?TEXT("Слои изменились после одобрения. Нужна повторная проверка."):
        State.bCurrentKnown?LaneName(Lane)+TEXT(" одобрено для текущих слоёв."):
        TEXT("Одобрение сохранено. Откройте действие, чтобы сверить текущие слои с одобренной версией.");
    return State;
}
bool FMCComposerReviewStore::SetApproved(FName Action,FName Event,EMCComposerReviewLane Lane,bool bApproved,const FString& Fingerprint,FString& Error)
{
    Error.Reset();
    if(Action.IsNone()||Event.IsNone()||(bApproved&&Fingerprint.IsEmpty())){Error=TEXT("Нужно конкретное событие и текущая версия слоёв.");return false;}
    // Merge against the latest small metadata file before each explicit write.
    Reload();if(!Impl->ReadError.IsEmpty()){Error=Impl->ReadError;return false;}
    auto Updated=Impl->Entries;const auto EntryKey=Key(Action,Event,Lane);
    if(bApproved)Updated.Add(EntryKey,FReviewEntry{Action,Event,Lane,Fingerprint,FDateTime::UtcNow().ToIso8601()});else Updated.Remove(EntryKey);
    const auto Before=MoveTemp(Impl->Entries);Impl->Entries=MoveTemp(Updated);const FString Payload=Impl->Json();Impl->Entries=Before;
    const FString Directory=FPaths::GetPath(Impl->Path);
    if(!IFileManager::Get().MakeDirectory(*Directory,true)){Error=TEXT("Не удалось создать папку отметок: ")+Directory;return false;}
    const FString Temporary=Impl->Path+TEXT(".")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".tmp");
    if(!FFileHelper::SaveStringToFile(Payload,*Temporary,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {Error=TEXT("Не удалось записать отметки. Прежний файл сохранён; временный файл: ")+Temporary;return false;}
    // Generic IFileManager::Move deletes an existing destination before rename.
    // Use same-directory platform replacement so failed replacement preserves it.
#if PLATFORM_WINDOWS
    const bool Replaced=::MoveFileExW(*Temporary,*Impl->Path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    const bool Replaced=std::rename(TCHAR_TO_UTF8(*Temporary),TCHAR_TO_UTF8(*Impl->Path))==0;
#endif
    if(!Replaced){Error=TEXT("Не удалось заменить файл отметок. Прежние отметки сохранены; новая копия: ")+Temporary;return false;}
    Reload();return Impl->ReadError.IsEmpty();
}

FString MCComposerReview::Fingerprint(const FMCVFXActionSession& Document,FName Event,EMCComposerReviewLane Lane)
{
    if(!Document.IsContextCurrent()||!Document.GetActionStages().Contains(Event))return FString();
    FString Values=TEXT("event-review-v1|")+Document.GetActionID().ToString()+TEXT("|")+Event.ToString()+TEXT("|")+LaneName(Lane);
    TSet<FGuid> UsedGroups;
    if(Lane==EMCComposerReviewLane::SFX)
    {
        const auto* Slot=Document.GetSFXSlot(Event);const FMCEventSFXSlot Default;
        Values+=StructText(Slot?*Slot:Default);
        if(Slot)for(const auto& L:Slot->Layers)if(L.GroupID.IsValid())UsedGroups.Add(L.GroupID);
    }
    else
    {
        Values+=FString::FromInt(static_cast<int32>(Document.GetReviewSlotMode(Event)));
        if(const auto* Recipe=Document.GetRecipe(Event))
        {
            Values+=FString::Printf(TEXT("%.9g|%.9g"),Recipe->DurationSeconds,Recipe->TailSeconds);
            for(auto Layer:Recipe->Layers){Layer.bSolo=false;Values+=StructText(Layer);if(Layer.GroupID.IsValid())UsedGroups.Add(Layer.GroupID);}
        }
        if(const auto* Native=Document.GetNativeDraft())
        {
            if(const auto* Settings=Native->Events.Find(Event))Values+=StructText(*Settings);
            for(const auto& Part:Native->PersistentParts)Values+=StructText(Part);
        }
    }
    if(const auto* Groups=Document.GetEventGroups(Event))
    {
        TArray<FGuid> IDs=UsedGroups.Array();IDs.Sort([](const FGuid& A,const FGuid& B){return A.ToString()<B.ToString();});
        for(const auto ID:IDs)if(const auto* Group=Groups->Find(ID))
            Values+=ID.ToString()+(Group->bEnabled?TEXT("|enabled"):TEXT("|disabled"))+
                (Lane==EMCComposerReviewLane::SFX?(Group->bMuteSFX?TEXT("|muted"):TEXT("|audible")):TEXT(""));
    }
    return Digest(Values);
}

namespace
{
FMCComposerReviewStore& SharedStore(){static FMCComposerReviewStore Store(MCComposerReview::GetStoragePath());return Store;}
struct FCachedReviewFingerprint{FGuid Token;uint64 Revision=MAX_uint64;FString Fingerprint;};
TMap<FString,FCachedReviewFingerprint> Fingerprints;
}
FString MCComposerReview::GetStoragePath(){return FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir()/TEXT("ComposerReview.json"));}
FMCComposerReviewStatus MCComposerReview::Query(FName Action,FName Event,EMCComposerReviewLane Lane,const FMCVFXActionSession* Document)
{
    const auto Held=Document?TSharedPtr<FMCVFXActionSession>():MCComposerDocuments::Find(Action);
    const auto* Current=Document?Document:Held.Get();FString CurrentFingerprint;
    if(Current&&Current->GetActionID()==Action&&Current->IsContextCurrent())
    {
        auto& Cache=Fingerprints.FindOrAdd(Key(Action,Event,Lane));
        if(Cache.Token!=Current->GetDraftToken()||Cache.Revision!=Current->GetRevision())
        {Cache.Token=Current->GetDraftToken();Cache.Revision=Current->GetRevision();Cache.Fingerprint=MCComposerReview::Fingerprint(*Current,Event,Lane);}
        CurrentFingerprint=Cache.Fingerprint;
    }
    return SharedStore().Query(Action,Event,Lane,CurrentFingerprint);
}
bool MCComposerReview::SetApproved(FName Action,FName Event,EMCComposerReviewLane Lane,bool bApproved,FString& Error)
{
    if(!bApproved)return SharedStore().SetApproved(Action,Event,Lane,false,FString(),Error);
    const auto Document=MCComposerDocuments::Acquire(Action,Error);if(!Document)return false;
    if(!Document->ValidateSourceCurrent(Error))return false;
    return SharedStore().SetApproved(Action,Event,Lane,true,MCComposerReview::Fingerprint(*Document,Event,Lane),Error);
}
FString MCComposerReview::GetSnapshotJson()
{
    TSharedPtr<FJsonObject> Root;const FString Raw=SharedStore().GetSnapshotJson();
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Raw),Root)||!Root)return Raw;
    Root->SetStringField(TEXT("storage"),GetStoragePath());
    for(const auto& Value:Root->GetArrayField(TEXT("reviews")))
    {
        const auto Row=Value->AsObject();
        const auto State=Query(FName(*Row->GetStringField(TEXT("action"))),FName(*Row->GetStringField(TEXT("event"))),
            Row->GetStringField(TEXT("lane"))==TEXT("SFX")?EMCComposerReviewLane::SFX:EMCComposerReviewLane::VFX);
        Row->SetBoolField(TEXT("approved"),State.bApproved);Row->SetBoolField(TEXT("stale"),State.bStale);Row->SetBoolField(TEXT("current_known"),State.bCurrentKnown);Row->SetStringField(TEXT("detail"),State.Detail);
    }
    FString Result;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Result));return Result;
}
void MCComposerReview::Reload(){SharedStore().Reload();Fingerprints.Reset();}
