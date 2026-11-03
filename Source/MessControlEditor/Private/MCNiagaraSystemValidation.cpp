#include "MCNiagaraInputToolset.h"
#include "NiagaraExternalSystemEditorUtilities.h"
#include "NiagaraSystem.h"
#include "Editor.h"
#include "HAL/PlatformMemory.h"
#include "ToolsetRegistry/ToolsetJsonConverter.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
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
    if (!IsInGameThread() || !GIsEditor || !GEditor || GEditor->PlayWorld || IsRunningCookCommandlet())
        return Reject(TEXT("Owned compile inspection requires the Editor game thread outside PIE/cook."));
    if (!IsValid(System) || System->IsTemplate() || !System->GetOutermost()->GetName().StartsWith(TEXT("/Game/"))
        || System->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad | RF_BeginDestroyed | RF_FinishDestroyed))
        return Reject(TEXT("An initialized project Niagara System is required."));
    if (OwnerMetadataKey.IsNone() || ExpectedOwnerValue.IsEmpty()
        || System->GetOutermost()->GetMetaData().GetValue(System, OwnerMetadataKey) != ExpectedOwnerValue)
        return Reject(TEXT("Exact generated ownership metadata does not match."));
    const auto MemoryBefore = FPlatformMemory::GetStats();
    constexpr uint64 GiB = 1024ull * 1024ull * 1024ull;
    if (MemoryBefore.UsedVirtual >= 12 * GiB || MemoryBefore.AvailablePhysical < 4 * GiB)
        return Reject(TEXT("12 GiB private / 4 GiB free memory boundary before compile inspection."));
    // SOURCE CONTRACT: SDK GetSystemCompileState checks the object and reads script
    // statuses/events. It never accesses Context.SystemViewModel. The DEFAULT ctor
    // has no VM; Context(System) would allocate a complete data-processing VM.
    FNiagaraExternalEditContext Context;
    FNiagaraExt_SystemCompileState State;
    UNiagaraExternalEditUtilities::GetSystemCompileState(System, State, Context);
    if (Context.HasErrors()) {
        TArray<FString> Errors;
        for (const FText& Error : Context.Errors) Errors.Add(Error.ToString());
        return Reject(FString::Join(Errors, TEXT("\n")));
    }
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
