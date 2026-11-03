#include "MCPerkPreviewEditorLibrary.h"
#include "MCNiagaraInputBatchTypes.h"
#include "ToolsetRegistry/ToolsetJsonConverter.h"
#include "ToolsetRegistry/ToolsetRegistrySubsystem.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "ScopedTransaction.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace
{
// Public exported converter entry points are protected for converter authors.
// Reuse their registered variant/object/type handling without depending on
// NiagaraToolsets' private implementation or silently flattening typed values.
class FBatchJsonPipeline : public UE::ToolsetRegistry::FToolsetJsonConverter
{
public:
    using FToolsetJsonConverter::ToolsetJsonDataToStruct;
    using FToolsetJsonConverter::ToolsetStructToJsonData;
};
FString EncodeBatchResult(const TSharedRef<FJsonObject>& Object)
{
    FString Result;FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Result));return Result;
}
bool IsAllowedBatchVariant(const FString& Path)
{
    static const TSet<FString> Allowed={
        TEXT("/Script/Niagara.NiagaraFloat"),TEXT("/Script/Niagara.NiagaraBool"),TEXT("/Script/Niagara.NiagaraInt32"),
        TEXT("/Script/Niagara.NiagaraPosition"),TEXT("/Script/Niagara.NiagaraID"),TEXT("/Script/CoreUObject.Vector3f"),
        TEXT("/Script/CoreUObject.Vector2f"),TEXT("/Script/CoreUObject.Vector4f"),TEXT("/Script/CoreUObject.Quat4f"),
        TEXT("/Script/CoreUObject.LinearColor"),TEXT("/Script/CoreUObject.Rotator"),
        TEXT("/Script/NiagaraEditor.NiagaraExt_StackInputData_Enum"),TEXT("/Script/NiagaraEditor.NiagaraExt_StackInputData_Linked"),
        TEXT("/Script/NiagaraEditor.NiagaraExt_StackInputData_HlslExpression"),TEXT("/Script/NiagaraEditor.NiagaraExt_StackInputData_DataInterface"),
        TEXT("/Script/NiagaraEditor.NiagaraExt_StackInputData_DynamicInput")};
    return Allowed.Contains(Path);
}
}

FString UMCPerkPreviewEditorLibrary::SetNiagaraStackInputBatch(UNiagaraSystem* System,const FString& OperationsJson,
    FName OwnerMetadataKey,const FString& ExpectedOwnerValue)
{
    auto Result=MakeShared<FJsonObject>();Result->SetBoolField(TEXT("complete"),false);
    Result->SetNumberField(TEXT("applied_prefix"),0);Result->SetBoolField(TEXT("failed_operation_may_have_mutated"),false);
    Result->SetBoolField(TEXT("atomic_rollback"),false);
    Result->SetBoolField(TEXT("transaction_started"),false);Result->SetBoolField(TEXT("preflight_rejected"),true);
    Result->SetNumberField(TEXT("undo_queue_before"),IsInGameThread()&&GEditor&&GEditor->Trans?GEditor->Trans->GetQueueLength():-1);
    Result->SetNumberField(TEXT("undo_count_before"),IsInGameThread()&&GEditor&&GEditor->Trans?GEditor->Trans->GetUndoCount():-1);
    TArray<TSharedPtr<FJsonValue>> Rows;
    const auto Fail=[&](const FString& Error) {
        Result->SetStringField(TEXT("error"),Error);Result->SetArrayField(TEXT("rows"),Rows);
        Result->SetNumberField(TEXT("undo_queue_after"),IsInGameThread()&&GEditor&&GEditor->Trans?GEditor->Trans->GetQueueLength():-1);
        Result->SetNumberField(TEXT("undo_count_after"),IsInGameThread()&&GEditor&&GEditor->Trans?GEditor->Trans->GetUndoCount():-1);
        return EncodeBatchResult(Result);
    };
    if(!IsInGameThread()||!GIsEditor||!GEditor||GEditor->PlayWorld||IsRunningCookCommandlet())
        return Fail(TEXT("Run a bounded authoring batch on the Editor game thread, outside PIE/cook."));
    if(GEditor->IsTransactionActive())return Fail(TEXT("Finish the current editor transaction before starting a batch; no foreign Undo scope was joined."));
    if(!IsValid(System)||System->IsTemplate()||System->HasAnyFlags(RF_NeedLoad|RF_NeedPostLoad|RF_BeginDestroyed|RF_FinishDestroyed))
        return Fail(TEXT("Choose an initialized Niagara System draft."));
    UPackage* Package=System->GetOutermost();
    if(!Package->GetName().StartsWith(TEXT("/Game/"))||Package->bIsCookedForEditor||Package->HasAnyPackageFlags(PKG_FilterEditorOnly))
        return Fail(TEXT("Engine/plugin originals, transient systems and cooked packages are not batch targets."));
    if(OwnerMetadataKey.IsNone()||ExpectedOwnerValue.IsEmpty())
        return Fail(TEXT("Provide a nonempty explicit draft ownership key and value."));
    auto& Metadata=Package->GetMetaData();
    if(Metadata.GetValue(System,OwnerMetadataKey)!=ExpectedOwnerValue)
        return Fail(TEXT("The exact explicit draft ownership metadata must match; nothing was edited."));
    const auto Registry=UToolsetRegistrySubsystem::Get();
    if(!Registry.HasValue())return Fail(TEXT("The installed toolset registry is unavailable; no fallback variant conversion was attempted."));
    const auto ValueConverter=Registry.GetValue()->ToolsetRegistry.GetConverterForProperty(
        FindFProperty<FProperty>(FMCNiagaraInputBatchEntry::StaticStruct(),TEXT("InputData")));
    const auto TypeConverter=Registry.GetValue()->ToolsetRegistry.GetConverterForProperty(
        FindFProperty<FProperty>(FNiagaraExt_Variable::StaticStruct(),TEXT("Type")));
    if(!ValueConverter||ValueConverter->GetName()!=TEXT("NiagaraInstancedValueConverter")
        ||!TypeConverter||TypeConverter->GetName()!=TEXT("ToolsetNiagaraTypeDefinition"))
        return Fail(TEXT("The installed Niagara variant/type converters must be registered before batch authoring."));
    if(OperationsJson.Len()>131072)return Fail(TEXT("Batch JSON exceeds the 128 Ki-character bound."));
    TArray<TSharedPtr<FJsonValue>> JsonOperations;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(OperationsJson),JsonOperations)
        ||JsonOperations.IsEmpty()||JsonOperations.Num()>16)
        return Fail(TEXT("Provide an array of 1 to 16 ordinary inputItemRef/inputData operations."));
    TArray<FMCNiagaraInputBatchEntry> Entries;Entries.Reserve(JsonOperations.Num());
    for(int32 Index=0;Index<JsonOperations.Num();++Index) {
        if(!JsonOperations[Index].IsValid()||JsonOperations[Index]->Type!=EJson::Object)
            return Fail(FString::Printf(TEXT("Operation %d is not an object; no edit was attempted."),Index));
        const auto Object=JsonOperations[Index]->AsObject();
        if(Object->HasField(TEXT("stackInputRef"))) {
            if(Object->HasField(TEXT("inputItemRef")))return Fail(TEXT("Use exactly one stackInputRef/inputItemRef key, not ambiguous aliases."));
            Object->SetField(TEXT("inputItemRef"),Object->Values.FindChecked(TEXT("stackInputRef")));Object->RemoveField(TEXT("stackInputRef"));
        }
        const TSharedPtr<FJsonObject>* InputJson=nullptr;const TSharedPtr<FJsonObject>* StructJson=nullptr;FString VariantPath;
        if(!Object->TryGetObjectField(TEXT("inputData"),InputJson)||!InputJson||!InputJson->IsValid()
            ||!(*InputJson)->TryGetObjectField(TEXT("struct"),StructJson)||!StructJson||!StructJson->IsValid()
            ||!(*StructJson)->TryGetStringField(TEXT("refPath"),VariantPath)||!IsAllowedBatchVariant(VariantPath))
            return Fail(FString::Printf(TEXT("Operation %d has no supported exact Niagara input variant; no edit was attempted."),Index));
        auto& Entry=Entries.AddDefaulted_GetRef();
        if(!FBatchJsonPipeline::ToolsetJsonDataToStruct(Object.ToSharedRef(),FMCNiagaraInputBatchEntry::StaticStruct(),&Entry)
            ||!Entry.InputData.IsValid()||!IsAllowedBatchVariant(Entry.InputData.GetScriptStruct()->GetPathName()))
            return Fail(FString::Printf(TEXT("Operation %d did not resolve a typed value through the installed toolset converter; no edit was attempted."),Index));
        if(const auto* Dynamic=Entry.InputData.GetPtr<FNiagaraExt_StackInputData_DynamicInput>();Dynamic
            &&(!IsValid(Dynamic->DynamicInputAsset)||Dynamic->DynamicInputAsset->GetUsage()!=ENiagaraScriptUsage::DynamicInput))
            return Fail(TEXT("A dynamic input must reference an initialized DynamicInput script."));
        if(const auto* EnumValue=Entry.InputData.GetPtr<FNiagaraExt_StackInputData_Enum>();EnumValue
            &&(!IsValid(EnumValue->Enum)||EnumValue->EnumName.IsNone()||EnumValue->Enum->GetIndexByName(EnumValue->EnumName)==INDEX_NONE))
            return Fail(TEXT("An enum input must identify an existing exact enum value."));
        if(const auto* Link=Entry.InputData.GetPtr<FNiagaraExt_StackInputData_Linked>();Link
            &&(Link->LinkedVariable.Name.IsNone()||!Link->LinkedVariable.Type.IsValid()))
            return Fail(TEXT("A linked input must identify a named, exactly typed variable."));
        const auto& Ref=Entry.InputItemRef;
        if(Ref.System!=System||Ref.ModuleName.IsNone()||Ref.ScriptName.IsNone()||Ref.InputNameStack.IsEmpty()
            ||Ref.InputNameStack.Num()>16||Ref.RendererIndex!=INDEX_NONE)
            return Fail(FString::Printf(TEXT("Operation %d is not a bounded input reference in the exact owned system."),Index));
    }
    // All wire values/owners are checked before the first edit. Individual SDK
    // visibility/type/edit-condition checks still run in order. A later error is
    // an honest partial prefix, not a promise that Undo/Cancel rolled it back.
    FNiagaraExternalEditContext Context(System);
    if(Context.HasErrors()) {
        TArray<FString> Errors;for(const auto& E:Context.Errors)Errors.Add(E.ToString());return Fail(FString::Join(Errors,TEXT("\n")));
    }
    FString EditError;Result->SetBoolField(TEXT("preflight_rejected"),false);
    {
    const FScopedTransaction Transaction(NSLOCTEXT("MCVFXComposer","BatchNiagaraInputs","Edit owned Niagara input batch"));
    Result->SetBoolField(TEXT("transaction_started"),true);
    for(int32 Index=0;Index<Entries.Num();++Index) {
        auto& Entry=Entries[Index];
        UNiagaraExternalEditUtilities::SetStackInputData(Entry.InputItemRef,Entry.InputData,Context);
        if(!Context.HasErrors())UNiagaraExternalEditUtilities::GetStackInputData(Entry.InputItemRef,Entry.InputData,Context);
        if(Context.HasErrors()) {
            Result->SetNumberField(TEXT("failed_index"),Index);Result->SetBoolField(TEXT("failed_operation_may_have_mutated"),true);
            TArray<FString> Errors;for(const auto& E:Context.Errors)Errors.Add(E.ToString());EditError=FString::Join(Errors,TEXT("\n"));break;
        }
        const auto Readback=FBatchJsonPipeline::ToolsetStructToJsonData(FMCNiagaraInputBatchEntry::StaticStruct(),&Entry);
        if(!Readback) {
            Result->SetNumberField(TEXT("failed_index"),Index);Result->SetBoolField(TEXT("failed_operation_may_have_mutated"),true);
            EditError=TEXT("The SDK accepted an operation but its actual typed readback could not be serialized; inspect the draft before resuming.");break;
        }
        Rows.Add(MakeShared<FJsonValueObject>(Readback));Result->SetNumberField(TEXT("applied_prefix"),Index+1);
    }
    }
    if(!EditError.IsEmpty())return Fail(EditError);
    Result->SetBoolField(TEXT("complete"),true);Result->SetArrayField(TEXT("rows"),Rows);
    Result->SetNumberField(TEXT("undo_queue_after"),GEditor->Trans?GEditor->Trans->GetQueueLength():-1);
    Result->SetNumberField(TEXT("undo_count_after"),GEditor->Trans?GEditor->Trans->GetUndoCount():-1);
    Result->SetStringField(TEXT("system"),System->GetPathName());return EncodeBatchResult(Result);
}
