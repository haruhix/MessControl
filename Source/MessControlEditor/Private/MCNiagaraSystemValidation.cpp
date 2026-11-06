#include "MCNiagaraInputToolset.h"
#include "MCPerkPreviewEditorLibrary.h"
#include "NiagaraExternalSystemEditorUtilities.h"
#include "NiagaraSystem.h"
#include "Editor.h"
#include "HAL/PlatformMemory.h"
#include "ToolsetRegistry/ToolsetJsonConverter.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

namespace MCNiagaraCompileRead
{
class FJsonPipeline : public UE::ToolsetRegistry::FToolsetJsonConverter
{
public:
    using FToolsetJsonConverter::ToolsetStructToJsonData;
};
FString Encode(const TSharedRef<FJsonObject>& Object)
{
    FString Text;
    FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text));
    return Text;
}

FString CheckOwnedSystem(UNiagaraSystem* System, FName Key, const FString& Expected)
{
    if (!IsInGameThread() || !GIsEditor || !GEditor || GEditor->PlayWorld || IsRunningCommandlet())
        return TEXT("Owned Niagara operations require the Editor game thread outside PIE and commandlets.");
    if (!IsValid(System) || System->IsTemplate() || !System->IsAsset()
        || !System->GetOutermost()->GetName().StartsWith(TEXT("/Game/"))
        || System->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad | RF_BeginDestroyed | RF_FinishDestroyed))
        return TEXT("An initialized project Niagara System is required.");
    if (Key.IsNone() || Expected.IsEmpty() || System->GetOutermost()->GetMetaData().GetValue(System, Key) != Expected)
        return TEXT("Exact generated ownership metadata does not match.");
    const auto Memory = FPlatformMemory::GetStats();
    constexpr uint64 GiB = 1024ull * 1024ull * 1024ull;
    if (Memory.UsedVirtual >= 12 * GiB || Memory.AvailablePhysical < 4 * GiB)
        return TEXT("12 GiB private / 4 GiB free memory boundary for owned Niagara operations.");
    return {};
}

FString ReadCompileState(UNiagaraSystem* System, FNiagaraExt_SystemCompileState& State)
{
    // A default SDK context reads script statuses without a System ViewModel.
    FNiagaraExternalEditContext Context;
    UNiagaraExternalEditUtilities::GetSystemCompileState(System, State, Context);
    TArray<FString> Errors;
    for (const FText& Error : Context.Errors) Errors.Add(Error.ToString());
    return FString::Join(Errors, TEXT("\n"));
}

FString OperationResult(UNiagaraSystem* System, const FString& Error)
{
    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("complete"), Error.IsEmpty());
    Result->SetStringField(TEXT("system"), GetPathNameSafe(System));
    if (!Error.IsEmpty()) Result->SetStringField(TEXT("error"), Error);
    return Encode(Result);
}
}

FString UMCNiagaraInputToolset::GetOwnedNiagaraCompileState(UNiagaraSystem* System,
    FName OwnerMetadataKey, const FString& ExpectedOwnerValue)
{
    using namespace MCNiagaraCompileRead;
    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("complete"), false);
    Result->SetNumberField(TEXT("view_models_created"), 0);
    Result->SetBoolField(TEXT("stack_diagnostics_collected"), false);
    Result->SetBoolField(TEXT("saved"), false);
    const auto Reject = [&](const FString& Error) { Result->SetStringField(TEXT("error"), Error); return Encode(Result); };
    if (const FString Error = CheckOwnedSystem(System, OwnerMetadataKey, ExpectedOwnerValue); !Error.IsEmpty())
        return Reject(Error);
    const auto MemoryBefore = FPlatformMemory::GetStats();
    FNiagaraExt_SystemCompileState State;
    if (const FString Error = ReadCompileState(System, State); !Error.IsEmpty()) return Reject(Error);
    const auto Compile = FJsonPipeline::ToolsetStructToJsonData(FNiagaraExt_SystemCompileState::StaticStruct(), &State);
    if (!Compile) return Reject(TEXT("Actual SDK compile state serialization failed."));
    Result->SetStringField(TEXT("system"), System->GetPathName());
    Result->SetObjectField(TEXT("compile"), Compile.ToSharedRef());
    Result->SetNumberField(TEXT("private_before_bytes"), static_cast<double>(MemoryBefore.UsedVirtual));
    const auto MemoryAfter = FPlatformMemory::GetStats();
    Result->SetNumberField(TEXT("private_after_bytes"), static_cast<double>(MemoryAfter.UsedVirtual));
    Result->SetNumberField(TEXT("free_after_bytes"), static_cast<double>(MemoryAfter.AvailablePhysical));
    Result->SetBoolField(TEXT("complete"), true);
    return Encode(Result);
}

FString UMCNiagaraInputToolset::RequestOwnedNiagaraCompile(UNiagaraSystem* System,
    FName OwnerMetadataKey, const FString& ExpectedOwnerValue)
{
    using namespace MCNiagaraCompileRead;
    FString Error = CheckOwnedSystem(System, OwnerMetadataKey, ExpectedOwnerValue);
    if (Error.IsEmpty()) UMCPerkPreviewEditorLibrary::RequestNiagaraSystemCompile(System, Error);
    return OperationResult(System, Error);
}

FString UMCNiagaraInputToolset::SaveOwnedNiagaraSystem(UNiagaraSystem* System,
    FName OwnerMetadataKey, const FString& ExpectedOwnerValue)
{
    using namespace MCNiagaraCompileRead;
    FString Error = CheckOwnedSystem(System, OwnerMetadataKey, ExpectedOwnerValue);
    if (!Error.IsEmpty()) return OperationResult(System, Error);
    FNiagaraExt_SystemCompileState State;
    Error = ReadCompileState(System, State);
    if (!Error.IsEmpty()) return OperationResult(System, Error);
    if (State.bIsCompiling || State.bIsStale || State.bHasErrors || State.bHasWarnings
        || State.AggregateStatus != ENiagaraExt_ScriptCompileStatus::UpToDate)
        return OperationResult(System, TEXT("Wait for an up-to-date compile without errors or warnings before saving."));
    auto* Assets = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
    if (!Assets || !Assets->SaveLoadedAsset(System, true))
        return OperationResult(System, TEXT("The exact Niagara system package could not be saved."));
    return OperationResult(System, {});
}
