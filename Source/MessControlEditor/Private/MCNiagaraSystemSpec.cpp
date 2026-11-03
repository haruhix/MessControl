#include "MCNiagaraInputToolset.h"
#include "MCGeneratedNiagaraEditScope.h"
#include "MCNiagaraSystemSpecTypes.h"
#include "MCNiagaraInputBatchTypes.h"
#include "MCPerkPreviewEditorLibrary.h"
#include "ToolsetRegistry/ToolsetJsonConverter.h"
#include "ToolsetRegistry/ToolsetRegistrySubsystem.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "Templates/UnrealTemplate.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceCurveBase.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraComponentRendererProperties.h"
#include "MCNiagaraProceduralSurfaceComponent.h"
#include "MCNiagaraProceduralSurfaceRenderer.h"
#include "Components/PointLightComponent.h"
#include "NiagaraConstants.h"
#include "NiagaraParameterStore.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraTypes.h"
#include "NiagaraVariableMetaData.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraEditorModule.h"
#include "NiagaraNodeFunctionCall.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "ViewModels/Stack/NiagaraStackScriptItemGroup.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace MCNiagaraWholeSystemSpec
{
// One synchronous request owns this context. No resident VM/session or engine SDK fork.
constexpr int32 MaxOperations = 8192;
constexpr int32 MaxJsonCharacters = 16 * 1024 * 1024;
constexpr double MaxSeconds = 180.0;
constexpr uint64 GiB = 1024ull * 1024ull * 1024ull;
bool bExecuting = false;
bool bCreating = false;

class FJsonPipeline : public UE::ToolsetRegistry::FToolsetJsonConverter
{
public:
    using FToolsetJsonConverter::ToolsetJsonDataToStruct;
    using FToolsetJsonConverter::ToolsetStructToJsonData;
};

FString Encode(const TSharedRef<FJsonObject>& Object)
{
    FString Text;
    FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text));
    return Text;
}

template <typename T>
TSharedPtr<FJsonObject> Json(const T& Value)
{
    return FJsonPipeline::ToolsetStructToJsonData(T::StaticStruct(), &Value);
}

FString MemoryError()
{
    const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
    // Win64 UsedVirtual is process PagefileUsage/commit, matching the host private-byte guard.
    if (Stats.UsedVirtual >= 12 * GiB || Stats.AvailablePhysical < 4 * GiB)
        return FString::Printf(TEXT("Memory boundary before the next operation: commit=%llu freePhysical=%llu. No automatic retry."),
            static_cast<unsigned long long>(Stats.UsedVirtual), static_cast<unsigned long long>(Stats.AvailablePhysical));
    return FString();
}

bool AllowedInput(const FNiagaraExt_StackInputValue& Value)
{
    if (!Value.IsValid()) return false;
    // Same installed wire variants admitted by the existing bounded input API.
    static const TSet<FString> Paths = {
        TEXT("/Script/Niagara.NiagaraFloat"), TEXT("/Script/Niagara.NiagaraBool"), TEXT("/Script/Niagara.NiagaraInt32"),
        TEXT("/Script/Niagara.NiagaraPosition"), TEXT("/Script/Niagara.NiagaraID"), TEXT("/Script/CoreUObject.Vector3f"),
        TEXT("/Script/CoreUObject.Vector2f"), TEXT("/Script/CoreUObject.Vector4f"), TEXT("/Script/CoreUObject.Quat4f"),
        TEXT("/Script/CoreUObject.LinearColor"), TEXT("/Script/CoreUObject.Rotator"),
        TEXT("/Script/NiagaraEditor.NiagaraExt_StackInputData_Enum"), TEXT("/Script/NiagaraEditor.NiagaraExt_StackInputData_Linked"),
        TEXT("/Script/NiagaraEditor.NiagaraExt_StackInputData_HlslExpression"), TEXT("/Script/NiagaraEditor.NiagaraExt_StackInputData_DataInterface"),
        TEXT("/Script/NiagaraEditor.NiagaraExt_StackInputData_DynamicInput") };
    if (!Paths.Contains(Value.GetScriptStruct()->GetPathName())) return false;
    if (const auto* V = Value.GetPtr<FNiagaraExt_StackInputData_DynamicInput>())
        return IsValid(V->DynamicInputAsset) && V->DynamicInputAsset->GetUsage() == ENiagaraScriptUsage::DynamicInput;
    if (const auto* V = Value.GetPtr<FNiagaraExt_StackInputData_Enum>())
        return IsValid(V->Enum) && !V->EnumName.IsNone() && V->Enum->GetIndexByName(V->EnumName) != INDEX_NONE;
    if (const auto* V = Value.GetPtr<FNiagaraExt_StackInputData_Linked>())
        return !V->LinkedVariable.Name.IsNone() && V->LinkedVariable.Type.IsValid();
    return true;
}

bool IsReadOnly(FName Operation)
{
    return Operation == TEXT("BindModule") || Operation == TEXT("BindRenderer") || Operation == TEXT("GetEmitterTopology")
        || Operation == TEXT("GetScriptStackTopology") || Operation == TEXT("GetModuleTopology");
}

bool ContainsProcessLocalTypeIndex(const TSharedPtr<FJsonValue>& Value)
{
    if (!Value) return false;
    if (Value->Type == EJson::Object) {
        for (const auto& Pair : Value->AsObject()->Values) {
            if (Pair.Key.ToView().Equals(TEXT("registeredTypeIndex"), ESearchCase::IgnoreCase) || ContainsProcessLocalTypeIndex(Pair.Value)) return true;
        }
    } else if (Value->Type == EJson::Array) {
        for (const auto& Child : Value->AsArray()) if (ContainsProcessLocalTypeIndex(Child)) return true;
    }
    return false;
}

FString Validate(const FMCNiagaraSystemSpecOperation& Op, UNiagaraSystem* System)
{
    static const TSet<FName> Allowed = {
        TEXT("AddEmitter"), TEXT("RemoveEmitter"), TEXT("AddModule"), TEXT("AddSetParametersModule"),
        TEXT("RemoveModule"), TEXT("SetModuleEnabled"), TEXT("BindModule"), TEXT("BindRenderer"), TEXT("SetStackInputData"),
        TEXT("SetSystemData"), TEXT("SetEmitterData"), TEXT("SetRendererData"), TEXT("AddRenderer"), TEXT("RemoveRenderer"),
        TEXT("AddUserVariable"), TEXT("DeclareNativeArray"), TEXT("SetUserDIProperties"), TEXT("SetMeshRendererUserBinding"),
        TEXT("GetEmitterTopology"), TEXT("GetScriptStackTopology"), TEXT("GetModuleTopology"),
        TEXT("SetStaticChoice"), TEXT("SetRendererMaterialUserBinding"), TEXT("ConfigurePointLightComponentRenderer"),
        TEXT("ConfigureProceduralSurfaceComponentRenderer"),TEXT("UpgradePreparedSurfaceRenderer") };
    if (Op.Id.IsEmpty() || Op.Id.Len() > 256 || !Allowed.Contains(Op.Operation)) return TEXT("Missing/invalid id or unsupported operation.");
    if (Op.Reference.System && Op.Reference.System != System) return TEXT("A reference targets a different system.");
    if (Op.Reference.InputNameStack.Num() > 32) return TEXT("Input reference depth exceeds 32.");
    if (Op.Operation == TEXT("SetStackInputData") && !AllowedInput(Op.InputData)) return TEXT("Invalid/unsupported installed typed input variant.");
    if (Op.Operation == TEXT("AddEmitter") && (!IsValid(Op.EmitterTemplate) || Op.EmitterName.IsNone())) return TEXT("AddEmitter needs a real template and explicit name.");
    if (Op.Operation == TEXT("AddModule") && (!IsValid(Op.ModuleAsset) || Op.ModuleAsset->GetUsage() != ENiagaraScriptUsage::Module)) return TEXT("AddModule needs a real Module script.");
    if (Op.Operation == TEXT("BindModule") && !Op.ModuleAsset && Op.ModuleNamePrefix.IsNone() && Op.Reference.ModuleName.IsNone()) return TEXT("BindModule needs an exact name, script asset or explicit prefix; ambiguous matches fail.");
    if (Op.Operation == TEXT("BindRenderer") && !Op.RendererClass && Op.Reference.RendererIndex == INDEX_NONE) return TEXT("BindRenderer needs an exact index or class; ambiguous matches fail.");
    if (Op.Operation == TEXT("AddSetParametersModule")) {
        if (Op.Parameters.IsEmpty() || Op.Parameters.Num() > 256) return TEXT("SetParameters needs 1..256 typed entries.");
        for (const auto& P : Op.Parameters)
            if (P.Variable.Name.IsNone() || !P.Variable.Type.IsValid() || !P.DefaultValue.IsValid()) return TEXT("Invalid typed SetParameters entry.");
    }
    if (Op.Operation == TEXT("AddUserVariable") && (Op.UserVariable.Name.IsNone() || !Op.UserVariable.Name.ToString().StartsWith(TEXT("User."))
        || !Op.UserVariable.Type.IsValid() || !Op.UserVariable.DefaultValue.IsValid())) return TEXT("Invalid typed User variable/default.");
    if (Op.Operation == TEXT("AddRenderer") && !Op.RendererClass) return TEXT("AddRenderer needs a renderer class.");
    if (Op.Operation == TEXT("SetStaticChoice") && Op.StaticChoice.IsEmpty()) return TEXT("SetStaticChoice needs one exact nonempty live display label.");
    if (Op.Operation == TEXT("SetRendererMaterialUserBinding") && (Op.MaterialIndex < 0 || !Op.UserName.ToString().StartsWith(TEXT("User."))))
        return TEXT("Material binding needs an existing material index and exact User parameter name.");
    if (Op.Operation == TEXT("DeclareNativeArray") && (Op.UserName.IsNone() || Op.NativeArrayElementType.IsNone())) return TEXT("DeclareNativeArray needs exact name and finite element type.");
    if (Op.Operation == TEXT("SetUserDIProperties") && (Op.UserName.IsNone() || !Op.UserName.ToString().StartsWith(TEXT("User.Native.")))) return TEXT("DI property writes are limited to existing system-owned User.Native interfaces.");
    if (Op.Operation == TEXT("SetSystemData") || Op.Operation == TEXT("SetEmitterData") || Op.Operation == TEXT("SetRendererData") || Op.Operation == TEXT("SetUserDIProperties")
        || Op.Operation == TEXT("ConfigureProceduralSurfaceComponentRenderer")) {
        TSharedPtr<FJsonObject> Object;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Op.PropertyValues), Object) || !Object.IsValid() || Object->Values.IsEmpty())
            return TEXT("PropertyValues must be a nonempty ordinary SDK JSON object.");
        if (ContainsProcessLocalTypeIndex(MakeShared<FJsonValueObject>(Object)))
            return TEXT("Do not replay process-local registeredTypeIndex values. Use semantic typed bindings or an evidence-derived artist-property delta without unchanged registry-bearing defaults.");
    }
    if (Op.Operation == TEXT("ConfigureProceduralSurfaceComponentRenderer")) {
        TSharedPtr<FJsonObject> P;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Op.PropertyValues), P);
        static const TSet<FString> Fields = { TEXT("SourceID"),TEXT("NativeOriginalMaterials"),TEXT("AuthoredMaterials"),
            TEXT("ScalarParameters"),TEXT("VectorParameters"),TEXT("TextureParameters"),TEXT("MaximumTriangles"),
            TEXT("SmallPreparedInstances"),TEXT("LargePreparedInstances") };
        for (const auto& Pair : P->Values) if (!Fields.Contains(FString(*Pair.Key))) return TEXT("Unknown procedural surface template property.");
        FString Source; if (!P->TryGetStringField(TEXT("SourceID"),Source) || Source.IsEmpty() || Source==TEXT("None")) return TEXT("Surface SourceID must be explicit.");
        const TArray<TSharedPtr<FJsonValue>>* Originals=nullptr; const TArray<TSharedPtr<FJsonValue>>* Authored=nullptr;
        if (!P->TryGetArrayField(TEXT("NativeOriginalMaterials"),Originals) || !P->TryGetArrayField(TEXT("AuthoredMaterials"),Authored)
            || Originals->IsEmpty() || Originals->Num()!=Authored->Num() || Originals->Num()>32) return TEXT("Surface original/authored materials need equal nonempty bounded arrays.");
        for (const auto* Array : {Originals,Authored}) for (const auto& Value : *Array) {
            FString Path; if (!Value || Value->Type!=EJson::Object || !Value->AsObject()->TryGetStringField(TEXT("refPath"),Path)
                || !Path.StartsWith(TEXT("/Game/"))) return TEXT("Surface materials need ordinary explicit /Game refPath references.");
        }
        for (const auto& Field : {TEXT("ScalarParameters"),TEXT("VectorParameters"),TEXT("TextureParameters")}) {
            const TArray<TSharedPtr<FJsonValue>>* Names=nullptr;
            if (!P->TryGetArrayField(Field,Names) || Names->Num()>64) return TEXT("Surface parameter whitelist arrays must be explicit and bounded.");
            TSet<FName> Unique;
            for (const auto& Value : *Names) {
                FString Name; if (!Value || !Value->TryGetString(Name) || Name.IsEmpty() || Unique.Contains(FName(*Name))) return TEXT("Invalid/duplicate surface uniform name.");
                Unique.Add(FName(*Name));
            }
        }
        for (const auto& Field : {TEXT("MaximumTriangles"),TEXT("SmallPreparedInstances"),TEXT("LargePreparedInstances")}) {
            double N=0; const int32 Min=FString(Field)==TEXT("LargePreparedInstances")?0:1;
            const int32 Max=FString(Field)==TEXT("MaximumTriangles")?131072:FString(Field)==TEXT("SmallPreparedInstances")?64:32;
            if (!P->TryGetNumberField(Field,N) || !FMath::IsFinite(N) || N<Min || N>Max || N!=FMath::FloorToDouble(N)) return TEXT("Invalid surface triangle/reserve capacity.");
        }
    }
    return FString();
}

bool Resolve(const FMCNiagaraSystemSpecOperation& Op, UNiagaraSystem* System,
    const TMap<FName, FNiagaraExt_StackItemReference>& Aliases, FNiagaraExt_StackItemReference& Out, FString& Error)
{
    if (!Op.ReferenceAlias.IsNone()) {
        const auto* Found = Aliases.Find(Op.ReferenceAlias);
        if (!Found) { Error = TEXT("Reference alias is not available from a successful preceding operation."); return false; }
        Out = *Found; // The SDK copy assignment deliberately drops cached stack UObject pointers.
        if (!Op.Reference.EmitterName.IsNone()) Out.EmitterName = Op.Reference.EmitterName;
        if (!Op.Reference.ScriptName.IsNone()) Out.ScriptName = Op.Reference.ScriptName;
        if (!Op.Reference.ModuleName.IsNone()) Out.ModuleName = Op.Reference.ModuleName;
        if (Op.Reference.RendererIndex != INDEX_NONE) Out.RendererIndex = Op.Reference.RendererIndex;
        if (!Op.Reference.InputNameStack.IsEmpty()) Out.InputNameStack = Op.Reference.InputNameStack;
    } else Out = Op.Reference;
    Out.System = System;
    return true;
}

TSharedPtr<FJsonObject> ReadUserVariable(UNiagaraSystem* System, FName Name, FNiagaraExternalEditContext& Context)
{
    FNiagaraExt_UserVariables Variables;
    UNiagaraExternalEditUtilities::GetUserVariables(System, Variables, Context);
    const auto* Value = Variables.UserVariables.FindByPredicate([Name](const auto& V) { return V.Name == Name; });
    if (!Value) { Context.Error(FText::FromString(TEXT("The exact user variable was not found after the operation."))); return nullptr; }
    return Json(*Value);
}


// The Context accessors are intentionally not exported by the engine. This public
// module API returns the existing VM registered by the ONE Context, without
// creating another VM. ExecuteOwnedSystemSpec refuses any pre-existing VM first.
TSharedPtr<FNiagaraSystemViewModel> OwnedViewModel(UNiagaraSystem* System, FNiagaraExternalEditContext& Context)
{
    auto VM = FNiagaraEditorModule::Get().GetExistingViewModelForSystem(System);
    if (!VM.IsValid() || &VM->GetSystem() != System) {
        Context.Error(FText::FromString(TEXT("The owned persistent Niagara ViewModel is unavailable; no new context or deferred retry.")));
        return nullptr;
    }
    return VM;
}

UNiagaraStackEntry* OwnedStackRoot(UNiagaraSystem* System, FName EmitterName, FNiagaraExternalEditContext& Context)
{
    const auto VM = OwnedViewModel(System, Context);
    if (!VM) return nullptr;
    if (EmitterName.IsNone()) {
        auto* Stack = VM->GetSystemStackViewModel();
        auto* Root = Stack ? Stack->GetRootEntry() : nullptr;
        if (!Root) Context.Error(FText::FromString(TEXT("The actual system stack root is unavailable.")));
        return Root;
    }
    UNiagaraStackEntry* Match = nullptr;
    for (const auto& EmitterVM : VM->GetEmitterHandleViewModels()) {
        const auto* Handle = EmitterVM->GetEmitterHandle();
        if (!Handle || Handle->GetName() != EmitterName) continue;
        auto* Stack = EmitterVM->GetEmitterStackViewModel();
        if (Match || !Stack || !Stack->GetRootEntry()) {
            Context.Error(FText::FromString(TEXT("The actual emitter stack root is missing/ambiguous."))); return nullptr;
        }
        Match = Stack->GetRootEntry();
    }
    if (!Match) Context.Error(FText::FromString(TEXT("The actual emitter stack root was not found.")));
    return Match;
}

UNiagaraStackModuleItem* FindOwningModule(UNiagaraSystem* System, const FNiagaraExt_StackItemReference& Ref, FNiagaraExternalEditContext& Context)
{
    auto* Root = OwnedStackRoot(System, Ref.EmitterName, Context);
    if (!Root || Context.HasErrors()) return nullptr;
    const int64 UsageValue = StaticEnum<ENiagaraScriptUsage>()->GetValueByName(Ref.ScriptName);
    if (UsageValue == INDEX_NONE) { Context.Error(FText::FromString(TEXT("Invalid exact script usage for synchronous refresh."))); return nullptr; }
    TArray<UNiagaraStackScriptItemGroup*> Groups;
    Root->GetUnfilteredChildrenOfType<UNiagaraStackScriptItemGroup>(Groups, true);
    UNiagaraStackModuleItem* Match = nullptr;
    for (auto* Group : Groups) {
        if (Group->GetScriptUsage() != static_cast<ENiagaraScriptUsage>(UsageValue) || Group->GetScriptUsageId().IsValid()) continue;
        TArray<UNiagaraStackModuleItem*> Modules;
        Group->GetUnfilteredChildrenOfType<UNiagaraStackModuleItem>(Modules, false);
        for (auto* Module : Modules) {
            if (FName(*Module->GetModuleNode().GetFunctionName()) != Ref.ModuleName) continue;
            if (Match) { Context.Error(FText::FromString(TEXT("The actual module refresh target is ambiguous."))); return nullptr; }
            Match = Module;
        }
    }
    if (!Match || Match->IsFinalized()) { Context.Error(FText::FromString(TEXT("The actual module refresh target was not found/finalized."))); return nullptr; }
    return Match;
}

bool RefreshOwningModule(UNiagaraSystem* System, const FNiagaraExt_StackItemReference& Ref, FNiagaraExternalEditContext& Context)
{
    auto* Module = FindOwningModule(System, Ref, Context);
    if (!Module) return false;
    // Recursively recomputes static/hidden/edit conditions without compiling/resetting the VM.
    Module->RefreshChildren();
    return !Context.HasErrors();
}

void RefreshPropertyDependents(const FMCNiagaraSystemSpecOperation& Op, UNiagaraSystem* System,
    const FNiagaraExt_StackItemReference& Ref, FNiagaraExternalEditContext& Context)
{
    const FName Kind = Op.Operation;
    if (Kind == TEXT("SetModuleEnabled")) { RefreshOwningModule(System, Ref, Context); return; }
    const bool bAllRoots = Kind == TEXT("SetSystemData") || Kind == TEXT("AddUserVariable")
        || Kind == TEXT("DeclareNativeArray") || Kind == TEXT("SetUserDIProperties");
    const bool bEmitterRoot = Kind == TEXT("SetEmitterData") || Kind == TEXT("SetRendererData")
        || Kind == TEXT("SetMeshRendererUserBinding") || Kind == TEXT("SetRendererMaterialUserBinding")
        || Kind == TEXT("AddRenderer") || Kind == TEXT("RemoveRenderer") || Kind == TEXT("ConfigurePointLightComponentRenderer")
        || Kind == TEXT("ConfigureProceduralSurfaceComponentRenderer") || Kind == TEXT("UpgradePreparedSurfaceRenderer");
    if (!bAllRoots && !bEmitterRoot) return;
    if (!bAllRoots) {
        if (auto* Root = OwnedStackRoot(System, Ref.EmitterName, Context)) Root->RefreshChildren();
        return;
    }
    const auto VM = OwnedViewModel(System, Context);
    if (!VM) return;
    if (auto* Stack = VM->GetSystemStackViewModel()) if (auto* Root = Stack->GetRootEntry()) Root->RefreshChildren();
    for (const auto& EmitterVM : VM->GetEmitterHandleViewModels())
        if (auto* Stack = EmitterVM->GetEmitterStackViewModel()) if (auto* Root = Stack->GetRootEntry()) Root->RefreshChildren();
}

// Match the stock SDK path resolver: flatten category/static input children,
// but cross a Dynamic-input boundary only for the next explicit inputNameStack item.
UNiagaraStackFunctionInput* FindInputInScope(UNiagaraStackEntry* Root, FName Name,
    FNiagaraExternalEditContext& Context)
{
    UNiagaraStackFunctionInput* Match = nullptr;
    TFunction<void(UNiagaraStackEntry*)> Visit = [&](UNiagaraStackEntry* Entry) {
        TArray<UNiagaraStackEntry*> Children;
        Entry->GetUnfilteredChildren(Children);
        for (auto* Child : Children) {
            if (auto* Input = Cast<UNiagaraStackFunctionInput>(Child)) {
                if (Input->GetInputParameterHandle().GetName() == Name) {
                    if (Match && Match != Input) { Context.Error(FText::FromString(TEXT("The actual curve input name is ambiguous in this scope."))); return; }
                    Match = Input;
                }
                if (Input->GetValueMode() != UNiagaraStackFunctionInput::EValueMode::Dynamic) Visit(Input);
            } else Visit(Child);
            if (Context.HasErrors()) return;
        }
    };
    Visit(Root);
    return Context.HasErrors() ? nullptr : Match;
}

bool SetCurveInputWithFreshLUT(UNiagaraSystem* System, const FNiagaraExt_StackItemReference& Ref,
    const FNiagaraExt_StackInputData_DataInterface& Value, FNiagaraExternalEditContext& Context, bool& bHandled)
{
    bHandled = false;
    FNiagaraExt_StackInputTopology Topology;
    UNiagaraExternalEditUtilities::GetStackInputTopology(Ref, Topology, Context);
    if (Context.HasErrors()) return false;
    UClass* Class = Topology.Type.GetClass();
    if (!Class || !Class->IsChildOf<UNiagaraDataInterfaceCurveBase>()) return true;
    bHandled = true;
    // Same visibility/edit-condition/type admission as the ordinary SDK setter.
    if (!Topology.bIsEditable) { Context.Error(FText::FromString(TEXT("The actual curve input is hidden or its visibility/edit condition does not pass."))); return false; }
    auto* Module = FindOwningModule(System, Ref, Context);
    if (!Module) return false;
    UNiagaraStackEntry* Scope = Module;
    UNiagaraStackFunctionInput* Input = nullptr;
    for (int32 I = 0; I < Ref.InputNameStack.Num(); ++I) {
        if (I > 0 && (!Input || Input->GetValueMode() != UNiagaraStackFunctionInput::EValueMode::Dynamic)) {
            Context.Error(FText::FromString(TEXT("The curve path crosses a non-dynamic parent."))); return false;
        }
        Input = FindInputInScope(Scope, Ref.InputNameStack[I], Context);
        if (!Input) { Context.Error(FText::FromString(TEXT("The exact actual curve input path was not found."))); return false; }
        Scope = Input;
    }
    if (!Input || Input->GetInputType() != Topology.Type) {
        Context.Error(FText::FromString(TEXT("The resolved curve input type differs from the SDK topology."))); return false;
    }
    const auto& Provider = UNiagaraExternalEditUtilities::GetPropertyProvider();
    if (!Provider.SetObjectProperties) {
        Context.Error(FText::FromString(TEXT("The installed curve property provider is unavailable."))); return false;
    }
    bool bCallbackExecuted = false;
    // This IS the public editor setter used by the installed SDK (SDK cpp3018),
    // including its placeholder manager, Modify, OnChanged and override sync.
    Input->SetDataInterfaceValueExternal(Class, [&](UNiagaraDataInterface* DI) {
        bCallbackExecuted = true;
        auto* Curve = Cast<UNiagaraDataInterfaceCurveBase>(DI);
        if (!Curve) { Context.Error(FText::FromString(TEXT("The stock curve placeholder was not created."))); return; }
        const bool bPropertiesAccepted = Provider.SetObjectProperties(Curve, Value.PropertyValues);
        // All keys AND LUT/optimization options are now set. Rebuild BEFORE the
        // stock setter broadcasts OnChanged/CopyToInternal, whose source LUT
        // would otherwise be stale. Also keep a rejected partial internally
        // consistent; a failed provider still fails the operation honestly.
        Curve->UpdateLUT();
        if (!bPropertiesAccepted) Context.Error(FText::FromString(TEXT("The normal property provider rejected curve properties; retain this partial without replay.")));
    });
    if (!bCallbackExecuted) Context.Error(FText::FromString(TEXT("The stock curve edit callback did not execute.")));
    return !Context.HasErrors();
}

TSharedPtr<FJsonObject> Execute(const FMCNiagaraSystemSpecOperation& Op, UNiagaraSystem* System,
    FNiagaraExt_StackItemReference& Ref, FNiagaraExternalEditContext& Context)
{
    using SDK = UNiagaraExternalEditUtilities;
    const FName Kind = Op.Operation;
    if (Kind == TEXT("AddEmitter")) {
        for (const auto& H : System->GetEmitterHandles()) if (H.GetName() == Op.EmitterName) {
            Context.Error(FText::FromString(TEXT("An emitter with the requested name already exists; automatic replay/rename is refused."))); return nullptr;
        }
        FNiagaraExt_EmitterTopology Actual;
        SDK::AddEmitter(Op.EmitterTemplate, Op.EmitterName, Actual, Context);
        Ref = FNiagaraExt_StackItemReference(System, Actual.EmitterName);
        return Context.HasErrors() ? nullptr : Json(Actual);
    }
    if (Kind == TEXT("GetEmitterTopology")) {
        FNiagaraExt_EmitterTopology Actual; SDK::GetEmitterTopology(Ref, Actual, Context); return Context.HasErrors() ? nullptr : Json(Actual);
    }
    if (Kind == TEXT("BindRenderer")) {
        FNiagaraExt_EmitterTopology Actual; SDK::GetEmitterTopology(Ref, Actual, Context);
        if (Context.HasErrors()) return nullptr;
        const FNiagaraExt_RendererRef* Match = nullptr;
        for (const auto& R : Actual.Renderers) {
            if (Op.Reference.RendererIndex != INDEX_NONE && R.RendererIndex != Op.Reference.RendererIndex) continue;
            if (Op.RendererClass && R.RendererClass != Op.RendererClass) continue;
            if (Match) { Context.Error(FText::FromString(TEXT("BindRenderer matched multiple renderers; supply exact index/class."))); return nullptr; }
            Match = &R;
        }
        if (!Match) { Context.Error(FText::FromString(TEXT("BindRenderer did not match an actual renderer."))); return nullptr; }
        Ref.RendererIndex = Match->RendererIndex; Ref.ModuleName = NAME_None; Ref.ScriptName = NAME_None; Ref.InputNameStack.Reset();
        return Json(*Match);
    }
    if (Kind == TEXT("GetScriptStackTopology") || Kind == TEXT("BindModule")) {
        FNiagaraExt_ScriptStackTopology Actual; SDK::GetScriptStackTopology(Ref, Actual, Context);
        if (Context.HasErrors()) return nullptr;
        if (Kind == TEXT("GetScriptStackTopology")) return Json(Actual);
        const FNiagaraExt_ModuleTopology* Match = nullptr;
        for (const auto& M : Actual.Modules) {
            if (!Op.Reference.ModuleName.IsNone() && M.ModuleName != Op.Reference.ModuleName) continue;
            if (Op.ModuleAsset && M.ModuleScript != Op.ModuleAsset) continue;
            if (!Op.ModuleNamePrefix.IsNone() && !M.ModuleName.ToString().StartsWith(Op.ModuleNamePrefix.ToString())) continue;
            if (Match) { Context.Error(FText::FromString(TEXT("BindModule matched more than one actual module; supply its exact name/asset."))); return nullptr; }
            Match = &M;
        }
        if (!Match) { Context.Error(FText::FromString(TEXT("BindModule did not match an actual module."))); return nullptr; }
        Ref.ModuleName = Match->ModuleName; Ref.InputNameStack.Reset(); return Json(*Match);
    }
    if (Kind == TEXT("AddModule") || Kind == TEXT("AddSetParametersModule")) {
        FNiagaraExt_ModuleTopology Actual;
        if (Kind == TEXT("AddModule")) SDK::AddModule(Ref, Op.ModuleAsset, Actual, Context);
        else SDK::AddSetParametersModule(Ref, Op.Parameters, Actual, Context);
        Ref.ModuleName = Actual.ModuleName; Ref.InputNameStack.Reset(); return Context.HasErrors() ? nullptr : Json(Actual);
    }
    if (Kind == TEXT("SetModuleEnabled") || Kind == TEXT("GetModuleTopology")) {
        if (Kind == TEXT("SetModuleEnabled")) SDK::SetModuleEnabled(Ref, Op.Enabled, Context);
        FNiagaraExt_ModuleTopology Actual;
        if (!Context.HasErrors()) SDK::GetModuleTopology(Ref, Actual, Context);
        if (!Context.HasErrors() && Kind == TEXT("SetModuleEnabled") && Actual.Enabled != Op.Enabled)
            Context.Error(FText::FromString(TEXT("Actual module enabled readback differs from the request.")));
        return Context.HasErrors() ? nullptr : Json(Actual);
    }
    // The choice is resolved from this live input's schema in the same VM.
    // Never replay an enum registry index or an offline dropdown ordinal.
    if (Kind == TEXT("SetStaticChoice")) {
        if (Ref.ModuleName.IsNone() || Ref.ScriptName.IsNone() || Ref.InputNameStack.IsEmpty() || Ref.RendererIndex != INDEX_NONE) {
            Context.Error(FText::FromString(TEXT("SetStaticChoice requires an exact module/script/input chain."))); return nullptr;
        }
        FNiagaraExt_StackInputSchema Schema;
        SDK::GetStackInputSchema(Ref, Schema, Context);
        if (Context.HasErrors()) return nullptr;
        FNiagaraExt_StackInputValue Desired;
        const auto BaseType = Schema.Type.RemoveStaticDef();
        if (BaseType == FNiagaraTypeDefinition::GetIntDef()) {
            const auto& Choices = Schema.MetaData.WidgetCustomization.EnumStyleDropdownValues;
            int32 Match = INDEX_NONE;
            for (int32 Index = 0; Index < Choices.Num(); ++Index) {
                if (Choices[Index].DisplayName.ToString() != Op.StaticChoice) continue;
                if (Match != INDEX_NONE) { Context.Error(FText::FromString(TEXT("Static integer choice matched multiple actual labels."))); return nullptr; }
                Match = Index;
            }
            if (Match == INDEX_NONE) { Context.Error(FText::FromString(TEXT("Static integer choice is absent from the actual EnumStyleDropdownValues."))); return nullptr; }
            Desired.InitializeAs<FNiagaraInt32>().Value = Match;
        } else if (Schema.Type.IsEnum()) {
            UEnum* Enum = Schema.Type.GetEnum();
            int32 Match = INDEX_NONE;
            // Prefer a real exact enum name or complete display label. Only if
            // neither exists, accept one unambiguous human label prefix at a
            // token boundary (e.g. Kill -> Kill (Emitter and Particles...)).
            for (int32 Pass = 0; Pass < 2 && Match == INDEX_NONE; ++Pass) {
                for (int32 Index = 0; Index < Enum->NumEnums(); ++Index) {
                    if (Enum->HasMetaData(TEXT("Hidden"), Index)) continue;
                    const FString Display = Enum->GetDisplayNameTextByIndex(Index).ToString();
                    const FString Name = Enum->GetNameByIndex(Index).ToString();
                    const bool bExact = Display == Op.StaticChoice || Name == Op.StaticChoice;
                    const int32 PrefixLength = Op.StaticChoice.Len();
                    const bool bTokenPrefix = Display.StartsWith(Op.StaticChoice, ESearchCase::CaseSensitive)
                        && Display.Len() > PrefixLength
                        && (FChar::IsWhitespace(Display[PrefixLength]) || Display[PrefixLength] == TEXT('('));
                    if (Pass == 0 ? !bExact : !bTokenPrefix) continue;
                    if (Match != INDEX_NONE) { Context.Error(FText::FromString(TEXT("Static enum choice is ambiguous in the actual enum."))); return nullptr; }
                    Match = Index;
                }
            }
            if (Match == INDEX_NONE) { Context.Error(FText::FromString(TEXT("Static enum choice is absent from the actual enum."))); return nullptr; }
            auto& Value = Desired.InitializeAs<FNiagaraExt_StackInputData_Enum>();
            Value.Enum = Enum;
            Value.EnumName = Enum->GetNameByIndex(Match);
            Value.DisplayName = Enum->GetDisplayNameTextByIndex(Match);
        } else if (BaseType == FNiagaraTypeDefinition::GetBoolDef()) {
            if (Op.StaticChoice != TEXT("True") && Op.StaticChoice != TEXT("False")) {
                Context.Error(FText::FromString(TEXT("A Boolean choice must be exactly True or False."))); return nullptr;
            }
            Desired.InitializeAs<FNiagaraBool>().SetValue(Op.StaticChoice == TEXT("True"));
        } else {
            Context.Error(FText::FromString(TEXT("Static choice input is not an actual Int32 dropdown, enum or Boolean."))); return nullptr;
        }
        SDK::SetStackInputData(Ref, Desired, Context);
        // DependencyRefresh57 supplies this synchronous same-VM refresh.
        if (!Context.HasErrors()) RefreshOwningModule(System, Ref, Context);
        FMCNiagaraInputBatchEntry Actual;
        FNiagaraExt_StackItemReference FreshRef(Ref); // drops cached SDK weak pointers
        Actual.InputItemRef = FreshRef;
        if (!Context.HasErrors()) SDK::GetStackInputData(FreshRef, Actual.InputData, Context);
        if (!Context.HasErrors()) {
            bool bEqual = false;
            if (const auto* WantInt = Desired.GetPtr<FNiagaraInt32>()) {
                const auto* Got = Actual.InputData.GetPtr<FNiagaraInt32>(); bEqual = Got && Got->Value == WantInt->Value;
            } else if (const auto* WantBool = Desired.GetPtr<FNiagaraBool>()) {
                const auto* Got = Actual.InputData.GetPtr<FNiagaraBool>(); bEqual = Got && Got->GetValue() == WantBool->GetValue();
            } else if (const auto* WantEnum = Desired.GetPtr<FNiagaraExt_StackInputData_Enum>()) {
                const auto* Got = Actual.InputData.GetPtr<FNiagaraExt_StackInputData_Enum>();
                bEqual = Got && Got->Enum == WantEnum->Enum && Got->EnumName == WantEnum->EnumName;
            }
            if (!bEqual) Context.Error(FText::FromString(TEXT("Static choice typed readback differs from the actual-schema-resolved request.")));
        }
        return Context.HasErrors() ? nullptr : Json(Actual);
    }

    if (Kind == TEXT("SetStackInputData")) {
        if (Ref.ModuleName.IsNone() || Ref.ScriptName.IsNone() || Ref.InputNameStack.IsEmpty() || Ref.RendererIndex != INDEX_NONE) {
            Context.Error(FText::FromString(TEXT("SetStackInputData requires an exact module/script/input chain."))); return nullptr;
        }
        bool bCurveHandled = false;
        if (const auto* DI = Op.InputData.GetPtr<FNiagaraExt_StackInputData_DataInterface>())
            SetCurveInputWithFreshLUT(System, Ref, *DI, Context, bCurveHandled);
        if (!Context.HasErrors() && !bCurveHandled) SDK::SetStackInputData(Ref, Op.InputData, Context);
        if (!Context.HasErrors()) RefreshOwningModule(System, Ref, Context);
        // Refresh may finalize/recreate dynamic/conditional children. SDK copies
        // clear its weak input/module caches before reading the actual new tree.
        const FNiagaraExt_StackItemReference FreshRef = Ref;
        FMCNiagaraInputBatchEntry Actual; Actual.InputItemRef = FreshRef;
        if (!Context.HasErrors()) SDK::GetStackInputData(FreshRef, Actual.InputData, Context);
        if (Context.HasErrors()) return nullptr;
        auto Readback = Json(Actual);
        if (Readback && bCurveHandled) Readback->SetBoolField(TEXT("curve_lut_rebuilt_before_publish"), true);
        return Readback;
    }
    if (Kind == TEXT("SetSystemData")) {
        FNiagaraExt_SystemData In, Actual; In.PropertyValues = Op.PropertyValues; SDK::SetSystemData(System, In, Context);
        if (!Context.HasErrors()) SDK::GetSystemData(System, Actual, Context); return Context.HasErrors() ? nullptr : Json(Actual);
    }
    if (Kind == TEXT("SetEmitterData")) {
        FNiagaraExt_EmitterData In, Actual; In.PropertyValues = Op.PropertyValues; SDK::SetEmitterData(Ref, In, Context);
        if (!Context.HasErrors()) SDK::GetEmitterData(Ref, Actual, Context); return Context.HasErrors() ? nullptr : Json(Actual);
    }
    if (Kind == TEXT("SetRendererMaterialUserBinding")) {
        const FNiagaraVariable Parameter(FNiagaraTypeDefinition(UMaterialInterface::StaticClass()), Op.UserName);
        if (!Op.UserName.ToString().StartsWith(TEXT("User.")) || System->GetExposedParameters().IndexOf(Parameter) == INDEX_NONE) {
            Context.Error(FText::FromString(TEXT("Declare the exact UMaterialInterface User parameter before binding a renderer material."))); return nullptr;
        }
        UNiagaraMeshRendererProperties* Renderer = nullptr;
        for (const auto& Handle : System->GetEmitterHandles()) {
            if (Handle.GetName() != Ref.EmitterName) continue;
            const auto* Data = Handle.GetEmitterData();
            if (Data && Data->GetRenderers().IsValidIndex(Ref.RendererIndex))
                Renderer = Cast<UNiagaraMeshRendererProperties>(Data->GetRenderers()[Ref.RendererIndex]);
            break;
        }
        if (!Renderer || !Renderer->OverrideMaterials.IsValidIndex(Op.MaterialIndex)) {
            Context.Error(FText::FromString(TEXT("Choose an existing exact mesh renderer and material override index; no implicit resize."))); return nullptr;
        }
        System->Modify(); Renderer->Modify();
        Renderer->bOverrideMaterials = true;
        Renderer->OverrideMaterials[Op.MaterialIndex].UserParamBinding.Parameter = Parameter;
        Renderer->PostEditChange(); System->MarkPackageDirty();
        if (!Renderer->OverrideMaterials.IsValidIndex(Op.MaterialIndex)
            || Renderer->OverrideMaterials[Op.MaterialIndex].UserParamBinding.Parameter != Parameter || !Renderer->bOverrideMaterials) {
            Context.Error(FText::FromString(TEXT("Actual typed material binding differs after PostEditChange."))); return nullptr;
        }
        FNiagaraExt_RendererData Actual;
        FNiagaraExt_StackItemReference FreshRef(Ref);
        if (!Context.HasErrors()) SDK::GetRendererData(FreshRef, Actual, Context);
        return Context.HasErrors() ? nullptr : Json(Actual);
    }

    if (Kind == TEXT("SetRendererData") || Kind == TEXT("SetMeshRendererUserBinding")) {
        if (Kind == TEXT("SetRendererData")) {
            FNiagaraExt_RendererData In; In.PropertyValues = Op.PropertyValues; SDK::SetRendererData(Ref, In, Context);
        } else {
            FString Error;
            if (!UMCPerkPreviewEditorLibrary::SetMeshRendererUserBinding(System, Ref.EmitterName, Ref.RendererIndex, Op.MeshIndex, Op.UserName, Error))
                Context.Error(FText::FromString(Error));
        }
        FNiagaraExt_RendererData Actual;
        if (!Context.HasErrors()) SDK::GetRendererData(Ref, Actual, Context); return Context.HasErrors() ? nullptr : Json(Actual);
    }
    if(Kind==TEXT("UpgradePreparedSurfaceRenderer")) {
        UNiagaraComponentRendererProperties* Old=nullptr;
        for(const auto& Handle:System->GetEmitterHandles())if(Handle.GetName()==Ref.EmitterName) {
            const auto* Data=Handle.GetEmitterData();
            if(Data&&Data->SimTarget==ENiagaraSimTarget::CPUSim&&!Data->bLocalSpace&&Data->GetRenderers().IsValidIndex(Ref.RendererIndex))
                Old=Cast<UNiagaraComponentRendererProperties>(Data->GetRenderers()[Ref.RendererIndex]);
            break;
        }
        auto* Template=Old?Cast<UMCNiagaraProceduralSurfaceComponent>(Old->TemplateComponent):nullptr;
        const auto& Provider=SDK::GetPropertyProvider();
        if(!Old||Old->GetClass()!=UNiagaraComponentRendererProperties::StaticClass()||!Template
            ||Old->ComponentCountLimit!=1||!Old->PropertyBindings.IsEmpty()||Template->SourceID.IsNone()||!Provider.GetObjectProperties) {
            Context.Error(FText::FromString(TEXT("Upgrade requires one exact owned stock Surface template, one draw and no arbitrary component property bindings.")));return nullptr;
        }
        const TArray<FName> Fields={TEXT("SourceID"),TEXT("NativeOriginalMaterials"),TEXT("AuthoredMaterials"),TEXT("ScalarParameters"),TEXT("VectorParameters"),TEXT("TextureParameters"),TEXT("MaximumTriangles"),TEXT("SmallPreparedInstances"),TEXT("LargePreparedInstances")};
        const FString BeforeTemplate=Provider.GetObjectProperties(Template,Fields);
        const int32 OldIndex=Ref.RendererIndex;
        FNiagaraExt_StackItemReference AppendRef=Ref;AppendRef.RendererIndex=INDEX_NONE;
        FNiagaraExt_RendererRef Added;SDK::AddRenderer(AppendRef,UMCNiagaraProceduralSurfaceRendererProperties::StaticClass(),Added,Context);
        if(Context.HasErrors())return nullptr;
        UMCNiagaraProceduralSurfaceRendererProperties* New=nullptr;
        for(const auto& Handle:System->GetEmitterHandles())if(Handle.GetName()==Ref.EmitterName)
            if(const auto* Data=Handle.GetEmitterData();Data&&Data->GetRenderers().IsValidIndex(Added.RendererIndex))
                New=Cast<UMCNiagaraProceduralSurfaceRendererProperties>(Data->GetRenderers()[Added.RendererIndex]);
        if(!New){Context.Error(FText::FromString(TEXT("New Surface renderer was not returned by the public SDK.")));return nullptr;}
        // Copy actual same-process reflected common fields, never registry indices
        // replayed from another process. Template is duplicated intact, not rebuilt.
        for(TFieldIterator<FProperty> P(Old->GetClass());P;++P)if(P->GetFName()!=TEXT("TemplateComponent"))
            if(FProperty* Target=FindFProperty<FProperty>(New->GetClass(),P->GetFName());Target&&Target->SameType(*P))
                Target->CopyCompleteValue(Target->ContainerPtrToValuePtr<void>(New),P->ContainerPtrToValuePtr<void>(Old));
        New->TemplateComponent=DuplicateObject<UMCNiagaraProceduralSurfaceComponent>(Template,New);
        New->TemplateComponent->SetFlags(RF_ArchetypeObject);New->PostEditChange();
        if(BeforeTemplate!=Provider.GetObjectProperties(New->TemplateComponent,Fields)) {
            Context.Error(FText::FromString(TEXT("Surface template property roundtrip differs; retain the owned partial, do not retry.")));return nullptr;
        }
        SDK::RemoveRenderer(Ref,Context);if(Context.HasErrors())return nullptr;
        Ref.RendererIndex=Added.RendererIndex-(Added.RendererIndex>OldIndex?1:0);
        bool ActualOwnedRenderer=false;
        for(const auto& Handle:System->GetEmitterHandles())if(Handle.GetName()==Ref.EmitterName)
            if(const auto* Data=Handle.GetEmitterData();Data&&Data->GetRenderers().IsValidIndex(Ref.RendererIndex))
                ActualOwnedRenderer=Data->GetRenderers()[Ref.RendererIndex]==New;
        if(!ActualOwnedRenderer) {
            Context.Error(FText::FromString(TEXT("Surface renderer replacement did not retain its actual new renderer; do not save or retry.")));return nullptr;
        }
        System->MarkPackageDirty();FNiagaraExt_RendererData Actual;SDK::GetRendererData(Ref,Actual,Context);
        if(Context.HasErrors())return nullptr;
        auto Result=Json(Actual);Result->SetStringField(TEXT("surface_template_properties"),Provider.GetObjectProperties(New->TemplateComponent,Fields));
        Result->SetBoolField(TEXT("surface_template_roundtrip_identical"),true);return Result;
    }
    if (Kind == TEXT("ConfigureProceduralSurfaceComponentRenderer")) {
        UMCNiagaraProceduralSurfaceRendererProperties* Renderer=nullptr;
        for (const auto& Handle:System->GetEmitterHandles()) {
            if (Handle.GetName()!=Ref.EmitterName) continue;
            const auto* Data=Handle.GetEmitterData();
            if (Data && Data->SimTarget==ENiagaraSimTarget::CPUSim && !Data->bLocalSpace && Data->GetRenderers().IsValidIndex(Ref.RendererIndex))
                Renderer=Cast<UMCNiagaraProceduralSurfaceRendererProperties>(Data->GetRenderers()[Ref.RendererIndex]);
            break;
        }
        const auto& Provider=SDK::GetPropertyProvider();
        if (!Renderer || Renderer->TemplateComponent || !Provider.SetObjectProperties || !Provider.GetObjectProperties) {
            Context.Error(FText::FromString(TEXT("Surface configuration requires a NEW empty CPU world-space prepared Surface renderer and installed public property provider; no artist template is overwritten.")));return nullptr;
        }
        const auto& Vars=System->GetExposedParameters().ReadParameterVariables();
        auto Has=[&](FName Name,const FNiagaraTypeDefinition& Type) {return Vars.ContainsByPredicate([&](const auto& V){return V.GetName()==Name && V.GetType()==Type;});};
        if (!Has(TEXT("User.VisualSubject"),FNiagaraTypeDefinition(USceneComponent::StaticClass()))
            || !Has(TEXT("User.SurfacePreparing"),FNiagaraTypeDefinition::GetBoolDef())
            || !Has(TEXT("User.SurfaceTriangleCapacity"),FNiagaraTypeDefinition::GetIntDef())) {
            Context.Error(FText::FromString(TEXT("Declare exact typed VisualSubject(SceneComponent), SurfacePreparing(Bool), SurfaceTriangleCapacity(Int) before configuring the renderer.")));return nullptr;
        }
        System->Modify();Renderer->Modify();
        auto* Surface=NewObject<UMCNiagaraProceduralSurfaceComponent>(Renderer,NAME_None,RF_ArchetypeObject);
        Surface->SetVisibility(false);Surface->SetAutoActivate(false);Surface->SetAbsolute(true,true,true);
        Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);Surface->SetCastShadow(false);Surface->SetCanEverAffectNavigation(false);
        // Public ordinary property conversion preserves actual reflected material classes and authored arrays.
        if (!Provider.SetObjectProperties(Surface,Op.PropertyValues)
            || Surface->SourceID.IsNone() || Surface->NativeOriginalMaterials.IsEmpty()
            || Surface->NativeOriginalMaterials.Num()!=Surface->AuthoredMaterials.Num()
            || Surface->NativeOriginalMaterials.Contains(nullptr) || Surface->AuthoredMaterials.Contains(nullptr)) {
            Context.Error(FText::FromString(TEXT("Surface template properties were rejected; retain this owned partial, never replay automatically.")));return nullptr;
        }
        Renderer->TemplateComponent=Surface;
        Renderer->PostEditChange();System->MarkPackageDirty();
        FNiagaraExt_RendererData Actual;SDK::GetRendererData(Ref,Actual,Context);
        if (Context.HasErrors()) return nullptr;
        auto Result=Json(Actual);
        if (!Result || Renderer->TemplateComponent!=Surface) {
            Context.Error(FText::FromString(TEXT("Actual surface renderer template identity changed after configuration.")));return nullptr;
        }
        TArray<FName> Names={TEXT("SourceID"),TEXT("NativeOriginalMaterials"),TEXT("AuthoredMaterials"),TEXT("ScalarParameters"),TEXT("VectorParameters"),
            TEXT("TextureParameters"),TEXT("MaximumTriangles"),TEXT("SmallPreparedInstances"),TEXT("LargePreparedInstances")};
        Result->SetStringField(TEXT("surface_template"),Surface->GetPathName());
        Result->SetStringField(TEXT("surface_template_class"),Surface->GetClass()->GetPathName());
        Result->SetStringField(TEXT("surface_template_properties"),Provider.GetObjectProperties(Surface,Names));
        return Result;
    }
    if (Kind == TEXT("ConfigurePointLightComponentRenderer")) {
        UNiagaraComponentRendererProperties* Renderer = nullptr;
        for (const auto& Handle : System->GetEmitterHandles()) {
            if (Handle.GetName() != Ref.EmitterName) continue;
            const auto* Data = Handle.GetEmitterData();
            if (Data && !Data->bLocalSpace && Data->GetRenderers().IsValidIndex(Ref.RendererIndex))
                Renderer = Cast<UNiagaraComponentRendererProperties>(Data->GetRenderers()[Ref.RendererIndex]);
            break;
        }
        if (!Renderer || Renderer->TemplateComponent || Renderer->ComponentType) {
            Context.Error(FText::FromString(TEXT("Choose one new empty world-space ComponentRenderer; existing templates/artist data are never overwritten."))); return nullptr;
        }
        System->Modify(); Renderer->Modify();
        Renderer->ComponentType = UPointLightComponent::StaticClass(); Renderer->ComponentCountLimit = 1;
        Renderer->bAssignComponentsOnParticleID = true; Renderer->bCreateComponentFirstParticleFrame = false;
        auto* Light = NewObject<UPointLightComponent>(Renderer, NAME_None, RF_ArchetypeObject);
        Light->SetVisibility(false); Light->SetAutoActivate(false); Light->SetComponentTickEnabled(false);
        Light->SetAbsolute(true,true,true); Light->SetCastShadows(false);
        Light->IntensityUnits = ELightUnits::Lumens; Light->bUseInverseSquaredFalloff = true;
        Light->SetIntensity(0.f); Light->SetAttenuationRadius(1.f); Light->SetSourceRadius(18.f); Light->SetSoftSourceRadius(28.f);
        Light->SetLightColor(FLinearColor(1.f,.30f,.12f));
        Renderer->TemplateComponent = Light; Renderer->PropertyBindings.Reset();
        auto Bind = [&](FName Attribute,FName Property,FName Setter,const FNiagaraTypeDefinition& AttributeType,const FNiagaraTypeDefinition& PropertyType) {
            FNiagaraComponentPropertyBinding B;const FNiagaraVariable Variable(AttributeType,Attribute);
            B.AttributeBinding.Setup(Variable,Variable);B.PropertyName=Property;B.MetadataSetterName=Setter;B.PropertyType=PropertyType;
            Renderer->PropertyBindings.Add(MoveTemp(B));
        };
        Bind(TEXT("Particles.Position"),TEXT("RelativeLocation"),NAME_None,FNiagaraTypeDefinition::GetPositionDef(),FNiagaraTypeDefinition(TBaseStructure<FVector>::Get()));
        Bind(TEXT("Particles.FireLightIntensity"),TEXT("Intensity"),TEXT("SetIntensity"),FNiagaraTypeDefinition::GetFloatDef(),FNiagaraTypeDefinition::GetFloatDef());
        Bind(TEXT("Particles.LightRadius"),TEXT("AttenuationRadius"),TEXT("SetAttenuationRadius"),FNiagaraTypeDefinition::GetFloatDef(),FNiagaraTypeDefinition::GetFloatDef());
        const FNiagaraVariable Enabled(FNiagaraTypeDefinition::GetBoolDef(),TEXT("Particles.FireLightEnabled"));
        Renderer->EnabledBinding.Setup(Enabled,Enabled);
        Renderer->PostEditChange(); System->MarkPackageDirty();
        FNiagaraExt_RendererData Actual;SDK::GetRendererData(Ref,Actual,Context);return Context.HasErrors()?nullptr:Json(Actual);
    }
    if (Kind == TEXT("AddRenderer")) {
        FNiagaraExt_RendererRef Actual; SDK::AddRenderer(Ref, Op.RendererClass, Actual, Context);
        Ref.RendererIndex = Actual.RendererIndex; return Context.HasErrors() ? nullptr : Json(Actual);
    }
    if (Kind == TEXT("RemoveEmitter") || Kind == TEXT("RemoveModule") || Kind == TEXT("RemoveRenderer")) {
        if (Kind == TEXT("RemoveEmitter")) SDK::RemoveEmitter(Ref, Context);
        else if (Kind == TEXT("RemoveModule")) SDK::RemoveModule(Ref, Context);
        else SDK::RemoveRenderer(Ref, Context);
        if (Context.HasErrors()) return nullptr;
        // Actual post-removal structure, not an echo of an assumed successful delete.
        if (Kind == TEXT("RemoveEmitter")) {
            FNiagaraExt_SystemSummary Actual; SDK::GetSystemSummary(System, Actual, Context); return Context.HasErrors() ? nullptr : Json(Actual);
        }
        if (Kind == TEXT("RemoveModule")) {
            Ref.ModuleName = NAME_None; Ref.InputNameStack.Reset();
            FNiagaraExt_ScriptStackTopology Actual; SDK::GetScriptStackTopology(Ref, Actual, Context); return Context.HasErrors() ? nullptr : Json(Actual);
        }
        FNiagaraExt_EmitterTopology Actual; SDK::GetEmitterTopology(Ref, Actual, Context); return Context.HasErrors() ? nullptr : Json(Actual);
    }
    if (Kind == TEXT("AddUserVariable")) {
        // Avoid SDK default replacement under a conflicting type. Identical names/types retain ordinary setter semantics.
        for (const auto& V : System->GetExposedParameters().ReadParameterVariables())
            if (V.GetName() == Op.UserVariable.Name && V.GetType() != Op.UserVariable.Type) {
                Context.Error(FText::FromString(TEXT("Existing user variable has a different type; it was not replaced."))); return nullptr;
            }
        SDK::AddUserVariable(System, Op.UserVariable, Context);
        return Context.HasErrors() ? nullptr : ReadUserVariable(System, Op.UserVariable.Name, Context);
    }
    if (Kind == TEXT("DeclareNativeArray")) {
        FString Error;
        if (!UMCPerkPreviewEditorLibrary::AddNativeArrayUserParameter(System, Op.UserName, Op.NativeArrayElementType, Error))
            Context.Error(FText::FromString(Error));
        return Context.HasErrors() ? nullptr : ReadUserVariable(System, Op.UserName, Context);
    }
    if (Kind == TEXT("SetUserDIProperties")) {
        auto& Store = System->GetExposedParameters();
        UNiagaraDataInterface* Interface = nullptr;
        for (const auto& V : Store.ReadParameterVariables()) if (V.GetName() == Op.UserName && V.GetType().IsDataInterface()) {
            Interface = Store.GetDataInterface(V); break;
        }
        const auto& Provider = SDK::GetPropertyProvider();
        if (!Interface || !Interface->IsIn(System) || Store.GetOwner() != System || !Provider.SetObjectProperties || !Provider.GetObjectProperties) {
            Context.Error(FText::FromString(TEXT("An existing system-owned interface and installed property provider are required."))); return nullptr;
        }
        TSharedPtr<FJsonObject> Requested;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Op.PropertyValues), Requested);
        TArray<FName> Names;
        for (const auto& Pair : Requested->Values) Names.Add(FName(*Pair.Key));
        System->Modify(); Interface->Modify();
        if (!Provider.SetObjectProperties(Interface, Op.PropertyValues)) {
            Context.Error(FText::FromString(TEXT("The public property provider rejected DI data; this operation may have partially changed it."))); return nullptr;
        }
        auto Actual = MakeShared<FJsonObject>();
        Actual->SetStringField(TEXT("interface"), Interface->GetPathName());
        Actual->SetStringField(TEXT("class"), Interface->GetClass()->GetPathName());
        Actual->SetStringField(TEXT("propertyValues"), Provider.GetObjectProperties(Interface, Names));
        if (Actual->GetStringField(TEXT("propertyValues")).IsEmpty()) {
            Context.Error(FText::FromString(TEXT("The actual DI property readback is empty."))); return nullptr;
        }
        return Actual;
    }
    Context.Error(FText::FromString(TEXT("Unsupported operation reached execution."))); return nullptr;
}

bool ParseSpec(const FString& OperationsJson, UNiagaraSystem* System,
    TArray<FMCNiagaraSystemSpecOperation>& Ops, FString& OutError)
{
    const auto Reject = [&](const FString& Error) { OutError = Error; return false; };
    if (OperationsJson.Len() > MaxJsonCharacters) return Reject(TEXT("Spec JSON exceeds 16 Mi-characters."));
    if (const auto Error = MemoryError(); !Error.IsEmpty()) return Reject(Error);
    const auto Registry = UToolsetRegistrySubsystem::Get();
    if (!Registry.HasValue()) return Reject(TEXT("The installed toolset registry is unavailable."));
    const auto VariantConverter = Registry.GetValue()->ToolsetRegistry.GetConverterForProperty(
        FindFProperty<FProperty>(FMCNiagaraSystemSpecOperation::StaticStruct(), TEXT("InputData")));
    const auto TypeConverter = Registry.GetValue()->ToolsetRegistry.GetConverterForProperty(
        FindFProperty<FProperty>(FNiagaraExt_Variable::StaticStruct(), TEXT("Type")));
    if (!VariantConverter || VariantConverter->GetName() != TEXT("NiagaraInstancedValueConverter")
        || !TypeConverter || TypeConverter->GetName() != TEXT("ToolsetNiagaraTypeDefinition"))
        return Reject(TEXT("Ordinary installed Niagara variant and type converters must be registered; no flattened fallback."));
    TArray<TSharedPtr<FJsonValue>> JsonOperations;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(OperationsJson), JsonOperations)
        || JsonOperations.IsEmpty() || JsonOperations.Num() > MaxOperations)
        return Reject(TEXT("Provide an ordered array of 1..8192 whole-system operations."));
    Ops.Reserve(JsonOperations.Num());
    TSet<FString> Ids; TSet<FName> AliasNames;
    for (int32 I = 0; I < JsonOperations.Num(); ++I) {
        if (const auto Error = MemoryError(); !Error.IsEmpty()) return Reject(Error);
        if (!JsonOperations[I].IsValid() || JsonOperations[I]->Type != EJson::Object)
            return Reject(FString::Printf(TEXT("Operation %d is not an object; no mutation."), I));
        auto& Op = Ops.AddDefaulted_GetRef();
        if (!FJsonPipeline::ToolsetJsonDataToStruct(JsonOperations[I]->AsObject().ToSharedRef(), FMCNiagaraSystemSpecOperation::StaticStruct(), &Op))
            return Reject(FString::Printf(TEXT("Operation %d failed the installed typed JSON conversion; no mutation."), I));
        const FString Error = Validate(Op, System);
        if (!Error.IsEmpty()) return Reject(FString::Printf(TEXT("Operation %d: %s No mutation."), I, *Error));
        if (Ids.Contains(Op.Id)) return Reject(TEXT("Operation ids must be unique; no mutation."));
        Ids.Add(Op.Id);
        if (!Op.ReferenceAlias.IsNone() && !AliasNames.Contains(Op.ReferenceAlias)) return Reject(TEXT("Only an earlier successful result alias may be referenced; no mutation."));
        if (!Op.ResultAlias.IsNone()) {
            if (AliasNames.Contains(Op.ResultAlias)) return Reject(TEXT("Result aliases must be unique; no mutation."));
            if (Op.Operation != TEXT("AddEmitter") && Op.Operation != TEXT("AddModule") && Op.Operation != TEXT("AddSetParametersModule")
                && Op.Operation != TEXT("BindModule") && Op.Operation != TEXT("BindRenderer") && Op.Operation != TEXT("AddRenderer")) return Reject(TEXT("Only actual emitter/module/renderer results may define aliases; no mutation."));
            AliasNames.Add(Op.ResultAlias);
        }
    }
    return true;
}
} // namespace MCNiagaraWholeSystemSpec

static FString ExecuteOwnedSystemSpec(UNiagaraSystem* System, const FString& OperationsJson,
    FName OwnerMetadataKey, const FString& ExpectedOwnerValue, bool bCreationScopeAlreadyActive,
    const TArray<FMCNiagaraSystemSpecOperation>* ParsedOps = nullptr)
{
    using namespace MCNiagaraWholeSystemSpec;
    const double Started = FPlatformTime::Seconds();
    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("complete"), false);
    Result->SetBoolField(TEXT("atomic_rollback"), false);
    Result->SetBoolField(TEXT("saved"), false); Result->SetBoolField(TEXT("compiled"), false);
    Result->SetBoolField(TEXT("preflight_rejected"), true);
    Result->SetBoolField(TEXT("failed_operation_may_have_mutated"), false);
    Result->SetNumberField(TEXT("applied_prefix"), 0);
    Result->SetNumberField(TEXT("edit_contexts_created"), 0);
    Result->SetNumberField(TEXT("outer_transactions_started"), 0);
    TArray<TSharedPtr<FJsonValue>> Rows;
    const auto Finish = [&](const FString& Error) {
        if (!Error.IsEmpty()) Result->SetStringField(TEXT("error"), Error);
        Result->SetArrayField(TEXT("rows"), Rows);
        Result->SetNumberField(TEXT("elapsed_seconds"), FPlatformTime::Seconds() - Started);
        const auto Stats = FPlatformMemory::GetStats();
        Result->SetNumberField(TEXT("process_commit_bytes_after"), static_cast<double>(Stats.UsedVirtual));
        Result->SetNumberField(TEXT("free_physical_bytes_after"), static_cast<double>(Stats.AvailablePhysical));
        return Encode(Result);
    };
    if (!IsInGameThread() || !GIsEditor || !GEditor || GEditor->PlayWorld || IsRunningCookCommandlet())
        return Finish(TEXT("Execute an owned authoring spec on the Editor game thread outside PIE/cook."));
    if (bExecuting || (bCreating && !bCreationScopeAlreadyActive) || GIsTransacting || (GEditor->Trans && GEditor->IsTransactionActive()))
        return Finish(TEXT("No reentrant spec, Undo/Redo replay, or foreign active transaction may be joined."));
    TGuardValue<bool> ExecutionGuard(bExecuting, true);
    if (!IsValid(System) || System->IsTemplate() || System->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad | RF_BeginDestroyed | RF_FinishDestroyed))
        return Finish(TEXT("Choose an initialized Niagara System draft."));
    UPackage* Package = System->GetOutermost();
    if (!Package->GetName().StartsWith(TEXT("/Game/")) || Package->bIsCookedForEditor || Package->HasAnyPackageFlags(PKG_FilterEditorOnly))
        return Finish(TEXT("Only an initialized project-owned /Game draft may be changed."));
    if (OwnerMetadataKey.IsNone() || ExpectedOwnerValue.IsEmpty() || Package->GetMetaData().GetValue(System, OwnerMetadataKey) != ExpectedOwnerValue)
        return Finish(TEXT("The exact explicit ownership metadata must match before any mutation."));
    if (OperationsJson.Len() > MaxJsonCharacters) return Finish(TEXT("Spec JSON exceeds 16 Mi-characters."));
    if (const auto Error = MemoryError(); !Error.IsEmpty()) return Finish(Error);
    TArray<FMCNiagaraSystemSpecOperation> OwnedOps;
    FString ParseError;
    if (!ParsedOps && !ParseSpec(OperationsJson, System, OwnedOps, ParseError)) return Finish(ParseError);
    const TArray<FMCNiagaraSystemSpecOperation>& Ops = ParsedOps ? *ParsedOps : OwnedOps;
    if (const auto Error = MemoryError(); !Error.IsEmpty()) return Finish(Error);
    Result->SetStringField(TEXT("system"), System->GetPathName());
    Result->SetNumberField(TEXT("process_commit_bytes_before_context"), static_cast<double>(FPlatformMemory::GetStats().UsedVirtual));
    TMap<FName, FNiagaraExt_StackItemReference> Aliases;
    FString Error;
    if (FNiagaraEditorModule::Get().GetExistingViewModelForSystem(System).IsValid())
        return Finish(TEXT("The generated target already has a ViewModel; preserve foreign contexts and use a fresh draft."));
    const auto HistoryBefore = bCreationScopeAlreadyActive ? FMCNiagaraUndoHistorySnapshot() : FMCNiagaraUndoHistorySnapshot::Capture();
    TUniquePtr<FMCScopedGeneratedNiagaraNoUndo> GeneratedScope;
    if (!bCreationScopeAlreadyActive) {
        if (!FMCScopedGeneratedNiagaraNoUndo::CanEnter(System, Error)) return Finish(Error);
        GeneratedScope = MakeUnique<FMCScopedGeneratedNiagaraNoUndo>();
    }
    Result->SetBoolField(TEXT("undo_capture_disabled_for_owned_generation"), true);
    {
        // Exactly ONE data-only VM: no Set/Get Toolset wrapper inside this scope.
        // The SDK sets CanSimulate/CanAutoCompile/CompileForEdit=false for this context.
        FNiagaraExternalEditContext Context(System);
        Result->SetNumberField(TEXT("edit_contexts_created"), 1);
        if (Context.HasErrors()) {
            TArray<FString> Errors; for (const auto& E : Context.Errors) Errors.Add(E.ToString()); Error = FString::Join(Errors, TEXT("\n"));
        } else {
            Result->SetBoolField(TEXT("preflight_rejected"), false);
            for (int32 I = 0; I < Ops.Num(); ++I) {
                const auto& Op = Ops[I];
                Error = MemoryError();
                if (Error.IsEmpty() && FPlatformTime::Seconds() - Started >= MaxSeconds) Error = TEXT("Whole-spec deadline reached before the next operation; no automatic retry.");
                if (!Error.IsEmpty()) { Result->SetNumberField(TEXT("next_index"), I); break; }
                FNiagaraExt_StackItemReference Ref;
                if (!Resolve(Op, System, Aliases, Ref, Error)) { Result->SetNumberField(TEXT("failed_index"), I); break; }
                auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("id"), Op.Id); Row->SetStringField(TEXT("operation"), Op.Operation.ToString());
                Result->SetNumberField(TEXT("failed_index"), I);
                Result->SetBoolField(TEXT("failed_operation_may_have_mutated"), !IsReadOnly(Op.Operation));
                const auto Readback = Execute(Op, System, Ref, Context);
                if (!Context.HasErrors()) RefreshPropertyDependents(Op, System, Ref, Context);
                if (Context.HasErrors()) {
                    TArray<FString> Errors; for (const auto& E : Context.Errors) Errors.Add(E.ToString()); Error = FString::Join(Errors, TEXT("\n"));
                    if (Error.IsEmpty()) Error = TEXT("The SDK reported an error without message text; this operation is not accepted.");
                    break;
                }
                const auto ActualRef = Json(Ref);
                if (!Readback || !ActualRef) { Error = TEXT("Actual typed result/reference could not be serialized; inspect this operation before resuming."); break; }
                Row->SetObjectField(TEXT("reference"), ActualRef.ToSharedRef()); Row->SetObjectField(TEXT("readback"), Readback.ToSharedRef());
                if (!Op.ResultAlias.IsNone()) { Aliases.Add(Op.ResultAlias, Ref); Row->SetStringField(TEXT("result_alias"), Op.ResultAlias.ToString()); }
                Rows.Add(MakeShared<FJsonValueObject>(Row)); Result->SetNumberField(TEXT("applied_prefix"), I + 1);
                Result->RemoveField(TEXT("failed_index")); Result->SetBoolField(TEXT("failed_operation_may_have_mutated"), false);
            }
        }
    } // The ONE context is destroyed while the generated-edit guard still owns this synchronous scope.
    GeneratedScope.Reset(); // Restore GEditor->Trans/GUndo BEFORE reporting, compile/wait/save or returning.
    if (!bCreationScopeAlreadyActive) {
        const auto HistoryAfter = FMCNiagaraUndoHistorySnapshot::Capture();
        const bool bUnchanged = HistoryBefore.IsIdenticalTo(HistoryAfter);
        Result->SetBoolField(TEXT("previous_undo_history_identical"), bUnchanged);
        if (!bUnchanged) {
            Result->SetBoolField(TEXT("complete"), false);
            return Finish(TEXT("Unexpected transaction-history change; stop and retain the partial result without replay."));
        }
    }
    if (!Error.IsEmpty()) return Finish(Error);
    if (Rows.Num() != Ops.Num()) return Finish(TEXT("The spec did not return an actual result for every operation; incomplete, never replay automatically."));
    Result->SetBoolField(TEXT("complete"), true);
    return Finish(FString());
}

FString UMCNiagaraInputToolset::ExecuteNiagaraSystemSpec(UNiagaraSystem* System, const FString& OperationsJson,
    FName OwnerMetadataKey, const FString& ExpectedOwnerValue)
{
    return ExecuteOwnedSystemSpec(System, OperationsJson, OwnerMetadataKey, ExpectedOwnerValue, false);
}

FString UMCNiagaraInputToolset::CreateNiagaraSystemFromSpec(const FString& AssetName, const FString& AssetPath,
    UNiagaraSystem* TemplateSystem, const FString& OperationsJson, FName OwnerMetadataKey, const FString& ExpectedOwnerValue)
{
    using namespace MCNiagaraWholeSystemSpec;
    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("complete"), false); Result->SetBoolField(TEXT("system_created"), false);
    Result->SetBoolField(TEXT("atomic_rollback"), false); Result->SetBoolField(TEXT("preflight_rejected"), true);
    Result->SetBoolField(TEXT("compiled"), false); Result->SetBoolField(TEXT("saved"), false);
    Result->SetNumberField(TEXT("applied_prefix"), 0); Result->SetNumberField(TEXT("edit_contexts_created"), 0);
    FString Error;
    const auto Reject = [&](const FString& Message) { Result->SetStringField(TEXT("error"), Message); return Encode(Result); };
    if (bExecuting || bCreating || !FMCScopedGeneratedNiagaraNoUndo::CanEnterEnvironment(Error))
        return Reject(Error.IsEmpty() ? TEXT("No nested/reentrant generated spec.") : Error);
    const FString PackageName = AssetPath / AssetName;
    const FString ObjectPath = PackageName + TEXT(".") + AssetName;
    if (!PackageName.StartsWith(TEXT("/Game/")) || AssetName.IsEmpty() || !FPackageName::IsValidLongPackageName(PackageName)
        || !FPackageName::IsValidObjectPath(ObjectPath))
        return Reject(TEXT("An explicit valid /Game asset path/name is required."));
    if (FindPackage(nullptr, *PackageName) || FPackageName::DoesPackageExist(PackageName))
        return Reject(TEXT("The exact new package already exists loaded or on disk; no rename, overwrite or replay."));
    if (OwnerMetadataKey.IsNone() || ExpectedOwnerValue.IsEmpty()) return Reject(TEXT("Provide exact nonempty generated ownership key/value."));
    if (!IsValid(TemplateSystem) || TemplateSystem->IsTemplate()
        || TemplateSystem->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad | RF_BeginDestroyed | RF_FinishDestroyed))
        return Reject(TEXT("Provide an initialized real Niagara System template."));
    // Stock builder waits if its template is not ready. Refuse before disabling Undo;
    // all waits/message pumping remain outside this single generated graph edit.
    if (!TemplateSystem->IsReadyToRun()) return Reject(TEXT("Prepare/compile the template outside this call; the builder must not wait inside generated authoring."));
    TArray<FMCNiagaraSystemSpecOperation> Ops;
    // Fresh-creation references deliberately omit System; aliases bind the actual new object.
    if (!ParseSpec(OperationsJson, nullptr, Ops, Error)) return Reject(Error);
    if (const auto Memory = MemoryError(); !Memory.IsEmpty()) return Reject(Memory);
    const auto HistoryBefore = FMCNiagaraUndoHistorySnapshot::Capture();
    TGuardValue<bool> CreationGuard(bCreating, true);
    bool bCreated = false;
    FString ActualPath;
    FString SpecResult;
    {
        FMCScopedGeneratedNiagaraNoUndo GeneratedScope;
        // Default context stores creation errors only; it constructs NO ViewModel.
        FNiagaraExternalEditContext CreationErrors;
        Result->SetBoolField(TEXT("creation_attempted"), true);
        Result->SetBoolField(TEXT("preflight_rejected"), false);
        Result->SetBoolField(TEXT("failed_operation_may_have_mutated"), true);
        UNiagaraSystem* System = UNiagaraExternalEditUtilities::CreateNiagaraSystem(AssetName, AssetPath, TemplateSystem, CreationErrors);
        bCreated = IsValid(System);
        if (System) ActualPath = System->GetPathName();
        if (CreationErrors.HasErrors()) {
            TArray<FString> Errors; for (const auto& E : CreationErrors.Errors) Errors.Add(E.ToString()); Error = FString::Join(Errors, TEXT("\n"));
            if (Error.IsEmpty()) Error = TEXT("The ordinary SDK builder reported an error without text.");
        } else if (!System || ActualPath != ObjectPath) Error = TEXT("The SDK did not create the exact requested system; retain any partial asset and do not retry.");
        else {
            System->GetOutermost()->GetMetaData().SetValue(System, OwnerMetadataKey, *ExpectedOwnerValue);
            System->MarkPackageDirty();
            // The only real data-only Context lives inside this private executor;
            // it is destroyed before GeneratedScope restores the saved transactor.
            if (FMCScopedGeneratedNiagaraNoUndo::HasNoLiveUsers(System, Error))
                SpecResult = ExecuteOwnedSystemSpec(System, OperationsJson, OwnerMetadataKey, ExpectedOwnerValue, true, &Ops);
        }
    } // No graph Context or disabled transaction capture survives this point.
    const auto HistoryAfter = FMCNiagaraUndoHistorySnapshot::Capture();
    const bool bHistoryIdentical = HistoryBefore.IsIdenticalTo(HistoryAfter);
    if (!SpecResult.IsEmpty()) {
        TSharedPtr<FJsonObject> Parsed;
        if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SpecResult), Parsed) && Parsed.IsValid()) Result = Parsed.ToSharedRef();
        else Error = TEXT("The native executor result could not be decoded; keep the owned partial asset without replay.");
    }
    Result->SetBoolField(TEXT("system_created"), bCreated);
    Result->SetBoolField(TEXT("standard_initial_compile_requested"), bCreated);
    Result->SetBoolField(TEXT("previous_undo_history_identical"), bHistoryIdentical);
    Result->SetBoolField(TEXT("undo_restored_before_return"), GEditor && GEditor->Trans.Get() == HistoryBefore.Transactor && GUndo == nullptr);
    if (bCreated) { Result->SetStringField(TEXT("system"), ActualPath); Result->SetBoolField(TEXT("preflight_rejected"), false); }
    if (!bHistoryIdentical) Error = TEXT("Unexpected transaction-history change; stop and retain this result without replay.");
    if (!Error.IsEmpty()) { Result->SetBoolField(TEXT("complete"), false); return Reject(Error); }
    return Encode(Result);
}
