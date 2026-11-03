#include "MCPerkPreviewEditorLibrary.h"
#include "MCVFXActionCatalog.h"
#include "MCPerkPreviewAdapter.h"
#include "MCVFXProfileBinding.h"
#include "MCVFXSourceContracts.h"
#include "MCPerkVFXProfile.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FString UMCPerkPreviewEditorLibrary::GetVFXComposerCatalogueJson(bool bIncludeCanonical)
{
    const auto Entries=MCVFXActionCatalog::GetCatalogue();
    auto Root=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Rows;
    Root->SetBoolField(TEXT("read_only"),true);
    Root->SetBoolField(TEXT("canonical_inspection_requested"),bIncludeCanonical);
    Root->SetBoolField(TEXT("runtime_or_art_acceptance"),false);
    Root->SetStringField(TEXT("evidence"),TEXT("Current catalogue/provider metadata; saved profile identity is optional. No scenario, audibility, exhaustive asset coverage or visual acceptance is inferred."));
    for(const auto& Entry:Entries) {
        auto Row=MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("id"),Entry.ID.ToString());
        Row->SetStringField(TEXT("label"),Entry.Label.ToString());
        Row->SetStringField(TEXT("category"),Entry.Category.ToString());
        Row->SetStringField(TEXT("source_owner"),Entry.SourceOwner);
        Row->SetStringField(TEXT("description"),Entry.Description.ToString());
        Row->SetBoolField(TEXT("enabled"),Entry.bEnabled);
        Row->SetBoolField(TEXT("supports_preview"),Entry.bSupportsPreview);
        Row->SetBoolField(TEXT("supports_profile_override"),Entry.bSupportsProfileOverride);
        Row->SetStringField(TEXT("preview_unavailable_reason"),Entry.UnsupportedReason);
        Row->SetStringField(TEXT("profile_unavailable_reason"),Entry.ProfileOverrideUnsupportedReason);
        const auto* Adapter=MCPerkPreviewAdapters::Find(Entry.ID);
        Row->SetBoolField(TEXT("registered_provider"),Adapter!=nullptr);
        TArray<TSharedPtr<FJsonValue>> Scenarios;
        if(Adapter)for(auto Scenario:Adapter->Scenarios) {
            const TCHAR* Name=Scenario==EMCPerkPreviewScenario::Target?TEXT("Target"):
                Scenario==EMCPerkPreviewScenario::NearWall?TEXT("NearWall"):
                Scenario==EMCPerkPreviewScenario::Timeout?TEXT("Timeout"):TEXT("Acquire");
            Scenarios.Add(MakeShared<FJsonValueString>(Name));
        }
        Row->SetArrayField(TEXT("scenarios"),Scenarios);
        TArray<TSharedPtr<FJsonValue>> Events;
        for(FName Stage:Entry.Stages) {
            auto Event=MakeShared<FJsonObject>();Event->SetStringField(TEXT("event"),Stage.ToString());
            if(Adapter)if(const auto* System=Adapter->DefaultSystems.Find(Stage))Event->SetStringField(TEXT("registered_default_system"),System->ToSoftObjectPath().ToString());
            const auto Audio=MCVFXSourceContracts::GetOriginalAudioStatus(Entry.ID,Stage);
            Event->SetStringField(TEXT("original_audio_knowledge"),Audio.Knowledge==EMCVFXOriginalAudioKnowledge::DeclaredSource?TEXT("declared_source"):
                Audio.Knowledge==EMCVFXOriginalAudioKnowledge::ConfirmedAbsent?TEXT("confirmed_absent"):TEXT("unknown"));
            Event->SetBoolField(TEXT("original_audio_independent_gate"),Audio.bIndependentNativeGate);
            Events.Add(MakeShared<FJsonValueObject>(Event));
        }
        Row->SetArrayField(TEXT("events"),Events);
        if(bIncludeCanonical) {
            FMCVFXCanonicalProfileState State;FString Error;
            const bool Read=MCVFXProfileBindings::ReadCanonical(Entry.ID,State,Error);
            Row->SetBoolField(TEXT("canonical_read_succeeded"),Read);
            if(Read) {
                Row->SetStringField(TEXT("canonical_owner"),State.OwnerAsset.ToString());
                Row->SetStringField(TEXT("canonical_key"),State.OwnerKey.ToString());
                Row->SetStringField(TEXT("canonical_profile"),State.Profile.ToSoftObjectPath().ToString());
                Row->SetStringField(TEXT("canonical_owner_digest"),State.OwnerDigest);
                Row->SetStringField(TEXT("canonical_profile_digest"),State.ProfileDigest);
            } else Row->SetStringField(TEXT("canonical_read_error"),Error);
        }
        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    Root->SetArrayField(TEXT("actions"),Rows);Root->SetNumberField(TEXT("catalogue_count"),Rows.Num());
    FString Json;FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));return Json;
}
