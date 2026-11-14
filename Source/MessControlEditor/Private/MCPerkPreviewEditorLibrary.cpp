#include "MCPerkPreviewEditorLibrary.h"
#include "MCPerkActionPreviewPanel.h"
#include "MCPerkPreviewRunner.h"
#include "MCPerkPreviewAdapter.h"
#include "MCVFXRecipeComposer.h"
#include "MCVFXActionWorkspace.h"
#include "MCVFXActionSession.h"
#include "MCVFXProfileBinding.h"
#include "MCVFXRecipePreset.h"
#include "MCActiveSkillAction.h"
#include "MCRocketSkillAction.h"
#include "MCToothCharacter.h"
#include "MCGameState.h"
#include "MCPerkVFXProfile.h"
#include "MCPerkVFXCompositionPlayer.h"
#include "MCVFXPresentationComponent.h"
#include "MCPerkTypes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraEmitter.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraComponentRendererProperties.h"
#include "NiagaraSimulationStageBase.h"
#include "Stateless/NiagaraStatelessEmitter.h"
#include "Stateless/NiagaraStatelessModule.h"
#include "UObject/UObjectGlobals.h"
#include "Modules/ModuleManager.h"
#include "NiagaraParameterStore.h"
#include "NiagaraDataInterfaceArrayFloat.h"
#include "NiagaraDataInterfaceArrayInt.h"
#include "MCFreezeCapturedGeometry.h"
#include "MCVFXNativeInputs.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeInput.h"
#include "EdGraphSchema_Niagara.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "ShaderCompilerCore.h"
#include "Engine/StaticMesh.h"
#include "Sound/SoundBase.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

bool UMCPerkPreviewEditorLibrary::SetMeshRendererUserBinding(UNiagaraSystem* System,FName EmitterName,
    int32 RendererIndex,int32 MeshIndex,FName UserName,FString& OutError)
{
    OutError.Reset();
    if(!IsValid(System)||System->GetOutermost()==GetTransientPackage()||!UserName.ToString().StartsWith(TEXT("User.")))
    {OutError=TEXT("Choose a saved system and an exact User.* mesh parameter.");return false;}
    const FNiagaraVariable Parameter(FNiagaraTypeDefinition(UStaticMesh::StaticClass()),UserName);
    if(System->GetExposedParameters().IndexOf(Parameter)==INDEX_NONE)
    {OutError=TEXT("The system must declare this exact StaticMesh user parameter first.");return false;}
    for(const auto& Handle:System->GetEmitterHandles())if(Handle.GetName()==EmitterName) {
        const auto* Data=Handle.GetEmitterData();
        auto* Renderer=Data&&Data->GetRenderers().IsValidIndex(RendererIndex)
            ?Cast<UNiagaraMeshRendererProperties>(Data->GetRenderers()[RendererIndex]):nullptr;
        if(!Renderer||!Renderer->Meshes.IsValidIndex(MeshIndex))
        {OutError=TEXT("Choose an existing mesh renderer and mesh slot.");return false;}
        const FScopedTransaction Transaction(NSLOCTEXT("MCVFXComposer","BindCapturedMesh","Bind actual captured tool mesh"));
        System->Modify();Renderer->Modify();
        auto& Binding=Renderer->Meshes[MeshIndex].MeshParameterBinding;
        Binding.ResolvedParameter=Parameter;Binding.AliasedParameter=Parameter;
        Renderer->Meshes[MeshIndex].Mesh=nullptr; // Absent captured parts must not draw the factory mesh.
        Renderer->PostEditChange();System->MarkPackageDirty();System->RequestCompile(false);return true;
    }
    OutError=TEXT("The named emitter does not belong to this system.");return false;
}

bool UMCPerkPreviewEditorLibrary::RequestNiagaraSystemCompile(UNiagaraSystem* System,FString& OutError)
{
    OutError.Reset();
    if(!IsInGameThread()||!GIsEditor||!GEditor||IsRunningCookCommandlet())
    {OutError=TEXT("Request Niagara compilation from the live Editor game thread, outside cook.");return false;}
    if(!IsValid(System)||System->IsTemplate()||System->HasAnyFlags(RF_BeginDestroyed|RF_FinishDestroyed))
    {OutError=TEXT("Choose an actual live Niagara System instance, not a template or destroyed object.");return false;}
    if(!System->GetSystemSpawnScript()||!System->GetSystemUpdateScript())
    {OutError=TEXT("The Niagara System must have initialized system scripts before compilation.");return false;}
    if(System->GetOutermost()->bIsCookedForEditor||System->GetOutermost()->HasAnyPackageFlags(PKG_FilterEditorOnly)||!AllowShaderCompiling())
    {OutError=TEXT("Niagara compilation requires editable source data and enabled shader compilation.");return false;}
    // The SDK returns false when no new work is necessary too. Acceptance is
    // deliberately separate from completion/errors reported by the normal gates.
    System->RequestCompile(false);
    return true;
}

bool UMCPerkPreviewEditorLibrary::AddNativeArrayUserParameter(UNiagaraSystem* System,FName UserName,FName ElementType,FString& OutError)
{
    OutError.Reset();
    if(!IsInGameThread()||!GIsEditor||!GEditor||GEditor->PlayWorld||IsRunningCookCommandlet())
    {OutError=TEXT("Declare Niagara inputs from the live Editor game thread, outside PIE and cook.");return false;}
    if(!IsValid(System)||System->IsTemplate()||System->HasAnyFlags(RF_NeedLoad|RF_NeedPostLoad|RF_BeginDestroyed|RF_FinishDestroyed)
        ||System->GetOutermost()->bIsCookedForEditor||System->GetOutermost()->HasAnyPackageFlags(PKG_FilterEditorOnly))
    {OutError=TEXT("Choose an initialized editable Niagara System, not cooked source or a template.");return false;}
    if(!System->GetOutermost()->GetName().StartsWith(TEXT("/Game/")))
    {OutError=TEXT("Declare inputs on a project-owned /Game Niagara System; Engine/plugin originals were not changed.");return false;}
    if(!MCVFXNativeInputs::IsNativeName(UserName)||UserName.ToString().Len()<=FCString::Strlen(TEXT("User.Native.")))
    {OutError=TEXT("Choose a nonempty exact User.Native.* gameplay input name.");return false;}
    UClass* ArrayClass=nullptr;
    if(ElementType==TEXT("Float"))ArrayClass=UNiagaraDataInterfaceArrayFloat::StaticClass();
    else if(ElementType==TEXT("Vector"))ArrayClass=UNiagaraDataInterfaceArrayFloat3::StaticClass();
    else if(ElementType==TEXT("Position"))ArrayClass=UNiagaraDataInterfaceArrayPosition::StaticClass();
    else if(FMCPreparedFreezeGeometryLayer::IsCaptureName(UserName)&&ElementType==TEXT("Vector4"))ArrayClass=UNiagaraDataInterfaceArrayFloat4::StaticClass();
    else if(FMCPreparedFreezeGeometryLayer::IsCaptureName(UserName)&&ElementType==TEXT("Int"))ArrayClass=UNiagaraDataInterfaceArrayInt32::StaticClass();
    else {OutError=TEXT("Array element type must be Float, Vector or Position; exact freeze capture rows additionally support Vector4/Int.");return false;}
    if(FMCPreparedFreezeGeometryLayer::IsCaptureName(UserName)) {
        FNiagaraTypeDefinition Expected;
        if(!FMCPreparedFreezeGeometryLayer::FindType(UserName,Expected)||Expected!=FNiagaraTypeDefinition(ArrayClass))
        {OutError=TEXT("Freeze capture array name/type is not part of the finite readonly resource contract.");return false;}
    }
    auto& Store=System->GetExposedParameters();
    if(Store.GetOwner()!=System)
    {OutError=TEXT("The system's exposed parameter store must own its data interfaces.");return false;}
    const FNiagaraVariable Parameter(FNiagaraTypeDefinition(ArrayClass),UserName);
    for(const auto& Existing:Store.ReadParameterVariables())if(Existing.GetName()==UserName) {
        const auto* Interface=Existing.GetType()==Parameter.GetType()?Store.GetDataInterface(Parameter):nullptr;
        if(!Interface||Interface->GetClass()!=ArrayClass||!Interface->IsIn(System))
        {OutError=TEXT("This name already exists with another type, or without a system-owned array interface. It was not changed.");return false;}
        return true;
    }
    const bool bWasDirty=System->GetOutermost()->IsDirty();
    FScopedTransaction Transaction(NSLOCTEXT("MCVFXComposer","DeclareNativeArray","Declare gameplay Niagara array input"));
    System->Modify();
    // The normal AddUserVariables scalar setter cannot initialize a DI. The
    // public store API constructs the exact interface with the system as outer.
    if(!Store.AddParameter(Parameter,true))
    {Transaction.Cancel();System->GetOutermost()->SetDirtyFlag(bWasDirty);OutError=TEXT("The array parameter could not be added.");return false;}
    const auto* Interface=Store.GetDataInterface(Parameter);
    if(!Interface||Interface->GetClass()!=ArrayClass||!Interface->IsIn(System)) {
        Store.RemoveParameter(Parameter);
        Transaction.Cancel();System->GetOutermost()->SetDirtyFlag(bWasDirty);
        OutError=TEXT("Array initialization failed; the newly added parameter was removed.");return false;
    }
    FPropertyChangedEvent Changed(FindFProperty<FProperty>(UNiagaraSystem::StaticClass(),TEXT("ExposedParameters")),EPropertyChangeType::ArrayAdd);
    System->PostEditChangeProperty(Changed);
    System->MarkPackageDirty();return true;
}

namespace MCNiagaraComponentInspection
{
constexpr int32 MaxRequestedNames=64,MaxStoreVariables=4096,MaxCapturedParts=3;
struct FValue
{
    bool bPresent=false,bValid=false;
    FNiagaraTypeDefinition Type;
    TSharedPtr<FJsonValue> Json=MakeShared<FJsonValueNull>();
    FString Reason;
    const UObject* Object=nullptr;
    bool Boolean=false;
    int32 Integer=0;
    float Scalar=0;
    FVector Vector=FVector::ZeroVector;
    FQuat Quaternion=FQuat::Identity;
    bool bReferenceOnly=false;
};
FString TypePath(const FNiagaraTypeDefinition& Type)
{
    if(!Type.IsValid())return {};
    if(Type.GetEnum())return GetPathNameSafe(Type.GetEnum());
    if(Type.IsUObject()||Type.IsDataInterface())return GetPathNameSafe(Type.GetClass());
    return GetPathNameSafe(Type.GetStruct());
}
TSharedRef<FJsonObject> ObjectValue(const UObject* Object)
{
    auto Result=MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("is_null"),Object==nullptr);
    Result->SetBoolField(TEXT("object_valid"),IsValid(Object));
    Result->SetStringField(TEXT("path"),Object?Object->GetPathName():FString());
    Result->SetStringField(TEXT("class"),Object?Object->GetClass()->GetPathName():FString());return Result;
}
TSharedRef<FJsonValue> Numbers(std::initializer_list<double> Values)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    for(double Value:Values)Result.Add(MakeShared<FJsonValueNumber>(Value));
    return MakeShared<FJsonValueArray>(Result);
}
template<class T> bool ReadBytes(const FNiagaraParameterStore& Store,const FNiagaraVariableBase& Variable,T& Out)
{
    if(Variable.GetType().GetSize()!=static_cast<int32>(sizeof(T)))return false;
    const uint8* Bytes=Store.GetParameterData(Variable);if(!Bytes)return false;
    FMemory::Memcpy(&Out,Bytes,sizeof(T));return true;
}
FValue Read(const FNiagaraParameterStore& Store,FName Name)
{
    FValue Result;const FNiagaraVariableWithOffset* Found=nullptr;
    for(const auto& Variable:Store.ReadParameterVariables())if(Variable.GetName()==Name) {
        Result.bPresent=true;
        if(Found){Result.Reason=TEXT("ambiguous_name_multiple_types");return Result;}
        Found=&Variable;
    }
    if(!Found){Result.Reason=TEXT("missing_from_component_override_store");return Result;}
    Result.Type=Found->GetType();const FNiagaraVariable Variable(Result.Type,Name);
    const auto& Type=Result.Type;
    if(!Type.IsValid()){Result.Reason=TEXT("invalid_type_definition");return Result;}
    if(Type.IsDataInterface()) {
        const int32 Index=Store.IndexOf(Variable);
        if(!Store.GetDataInterfaces().IsValidIndex(Index)){Result.Reason=TEXT("invalid_interface_slot");return Result;}
        Result.Object=Store.GetDataInterface(Variable);Result.bValid=IsValid(Result.Object)&&Result.Object->IsA(Type.GetClass());
        Result.bReferenceOnly=true;Result.Json=MakeShared<FJsonValueObject>(ObjectValue(Result.Object));
        Result.Reason=TEXT("interface_reference_only_runtime_array_or_simulation_contents_not_inspected");return Result;
    }
    if(Type.IsUObject()) {
        const int32 Index=Store.IndexOf(Variable);
        if(!Store.GetUObjects().IsValidIndex(Index)){Result.Reason=TEXT("invalid_object_slot");return Result;}
        Result.Object=Store.GetUObject(Index).Get();
        Result.bValid=!Result.Object||(IsValid(Result.Object)&&Result.Object->IsA(Type.GetClass()));
        Result.Json=MakeShared<FJsonValueObject>(ObjectValue(Result.Object));
    } else if(Type==FNiagaraTypeDefinition::GetBoolDef()) {
        FNiagaraBool Value;
        Result.bValid=ReadBytes(Store,Variable,Value)&&Value.IsValid();
        if(Result.bValid){Result.Boolean=Value.GetValue();Result.Json=MakeShared<FJsonValueBoolean>(Result.Boolean);}
        else Result.Reason=TEXT("invalid_bool_storage_or_encoding");
    } else if(Type==FNiagaraTypeDefinition::GetIntDef()) {
        Result.bValid=ReadBytes(Store,Variable,Result.Integer);
        if(Result.bValid)Result.Json=MakeShared<FJsonValueNumber>(Result.Integer);
    } else if(Type==FNiagaraTypeDefinition::GetFloatDef()) {
        Result.bValid=ReadBytes(Store,Variable,Result.Scalar)&&FMath::IsFinite(Result.Scalar);
        if(Result.bValid)Result.Json=MakeShared<FJsonValueNumber>(Result.Scalar);
    } else if(Type==FNiagaraTypeDefinition::GetVec3Def()) {
        FVector3f Value;Result.bValid=ReadBytes(Store,Variable,Value)&&!Value.ContainsNaN();
        if(Result.bValid){Result.Vector=FVector(Value);Result.Json=Numbers({Value.X,Value.Y,Value.Z});}
    } else if(Type==FNiagaraTypeDefinition::GetPositionDef()) {
        const FVector* Value=Store.GetPositionParameterValue(Name);
        Result.bValid=Value&&!Value->ContainsNaN();
        if(Result.bValid){Result.Vector=*Value;Result.Json=Numbers({Value->X,Value->Y,Value->Z});}
        else Result.Reason=TEXT("world_position_value_unavailable_no_lwc_fallback");
    } else if(Type==FNiagaraTypeDefinition::GetQuatDef()) {
        FQuat4f Value;Result.bValid=ReadBytes(Store,Variable,Value)&&!Value.ContainsNaN();
        if(Result.bValid){Result.Quaternion=FQuat(Value);Result.Json=Numbers({Value.X,Value.Y,Value.Z,Value.W});}
    } else if(Type==FNiagaraTypeDefinition::GetColorDef()) {
        FLinearColor Value;Result.bValid=ReadBytes(Store,Variable,Value)&&FMath::IsFinite(Value.R)&&FMath::IsFinite(Value.G)&&FMath::IsFinite(Value.B)&&FMath::IsFinite(Value.A);
        if(Result.bValid)Result.Json=Numbers({Value.R,Value.G,Value.B,Value.A});
    } else Result.Reason=TEXT("unsupported_value_type_no_raw_memory_or_default_fallback");
    if(!Result.bValid&&Result.Reason.IsEmpty())Result.Reason=TEXT("invalid_storage_or_nonfinite_value");
    return Result;
}
TSharedRef<FJsonObject> ValueJson(FName Name,const FValue& Value)
{
    auto Result=MakeShared<FJsonObject>();Result->SetStringField(TEXT("name"),Name.ToString());
    Result->SetBoolField(TEXT("present"),Value.bPresent);Result->SetBoolField(TEXT("valid"),Value.bValid);
    Result->SetStringField(TEXT("type"),Value.bPresent?TypePath(Value.Type):FString());
    Result->SetField(TEXT("value"),Value.Json);Result->SetStringField(TEXT("reason"),Value.Reason);
    Result->SetBoolField(TEXT("reference_only"),Value.bReferenceOnly);return Result;
}
TArray<FName> CaptureNames()
{
    TArray<FName> Result={TEXT("User.ToolPartCount"),TEXT("User.EchoOverhead"),TEXT("User.ContactDelay")};
    for(int32 Index=0;Index<MaxCapturedParts;++Index)for(const TCHAR* Kind:{TEXT("Mesh"),TEXT("Position"),TEXT("Orientation"),TEXT("Scale")})
        Result.Add(FName(*FString::Printf(TEXT("User.Tool%s%d"),Kind,Index)));
    return Result;
}
}

FString UMCPerkPreviewEditorLibrary::GetNiagaraComponentParameterInspection(const UNiagaraComponent* Component,
    const TArray<FName>& UserNames,FString& OutError)
{
    using namespace MCNiagaraComponentInspection;
    OutError.Reset();
    if(!IsInGameThread()||!GIsEditor||IsRunningCookCommandlet()||!IsValid(Component)||Component->IsTemplate()
        ||Component->HasAnyFlags(RF_NeedLoad|RF_NeedPostLoad|RF_BeginDestroyed|RF_FinishDestroyed))
    {OutError=TEXT("Inspect an initialized actual Niagara component on the Editor game thread, outside cook.");return {};}
    if(UserNames.Num()>MaxRequestedNames)
    {OutError=TEXT("Request at most 64 distinct User.* names; no unbounded store dump.");return {};}
    TSet<FName> Names;
    for(FName Name:UserNames) {
        const FString Text=Name.ToString();
        if(Name.IsNone()||Text.Len()<=5||Text.Len()>160||!Text.StartsWith(TEXT("User."),ESearchCase::IgnoreCase)||Names.Contains(Name))
        {OutError=TEXT("Use unique User.* names of at most 160 characters; case aliases are the same name.");return {};}
        Names.Add(Name);
    }
    const auto& Store=Component->GetOverrideParameters();
    if(Store.ReadParameterVariables().Num()>MaxStoreVariables)
    {OutError=TEXT("The actual override store exceeds the bounded 4096-variable inspection limit.");return {};}
    auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("schema_version"),1);
    Root->SetStringField(TEXT("component"),Component->GetPathName());Root->SetStringField(TEXT("system"),GetPathNameSafe(Component->GetAsset()));
    Root->SetStringField(TEXT("world"),GetPathNameSafe(Component->GetWorld()));Root->SetBoolField(TEXT("registered"),Component->IsRegistered());
    Root->SetBoolField(TEXT("active"),Component->IsActive());Root->SetNumberField(TEXT("override_store_variables"),Store.ReadParameterVariables().Num());
    Root->SetStringField(TEXT("scope"),TEXT("Component override store only; no asset-default fallback, GPU/VM readback, load, compile, save or mutation."));
    TMap<FName,FValue> Values;TArray<TSharedPtr<FJsonValue>> Rows;
    for(FName Name:UserNames){FValue Value=Read(Store,Name);Rows.Add(MakeShared<FJsonValueObject>(ValueJson(Name,Value)));Values.Add(Name,MoveTemp(Value));}
    Root->SetArrayField(TEXT("parameters"),Rows);
    auto Capture=MakeShared<FJsonObject>();Root->SetObjectField(TEXT("capture"),Capture);
    const auto* Player=Cast<AMCPerkVFXCompositionPlayer>(Component->GetOwner());
    Capture->SetBoolField(TEXT("available"),IsValid(Player));
    auto Native=MakeShared<FJsonObject>();Root->SetObjectField(TEXT("native_payload"),Native);
    Native->SetBoolField(TEXT("available"),IsValid(Player));Native->SetStringField(TEXT("comparison_scope"),TEXT("accepted_payload"));
    Native->SetBoolField(TEXT("independent_actor_expiry_sampled"),false);Native->SetBoolField(TEXT("array_contents_observed"),false);
    Native->SetBoolField(TEXT("managed_persistent_accepted_payload_match"),false);
    if(IsValid(Player)) {
        const TArray<FMCEchoToolPart>* Parts=nullptr;bool bOverhead=false;double ContactAt=0;float LayerStart=0;
        const bool Managed=Player->GetCapturedToolContextForEditorInspection(Component,Parts,bOverhead,ContactAt,LayerStart);
        const int32 RetainedCount=Parts?Parts->Num():0,Count=FMath::Min(RetainedCount,MaxCapturedParts);
        Capture->SetStringField(TEXT("player"),Player->GetPathName());Capture->SetBoolField(TEXT("managed_layer_component"),Managed);
        bool Persistent=false;const AActor* PersistentOwner=Player->GetPersistentStateOwnerForEditorInspection(Persistent);
        const bool SameWorld=IsValid(PersistentOwner)&&Component->GetWorld()&&PersistentOwner->GetWorld()==Component->GetWorld();
        Native->SetObjectField(TEXT("persistent_owner"),ObjectValue(PersistentOwner));Native->SetObjectField(TEXT("component"),ObjectValue(Component));
        Native->SetBoolField(TEXT("persistent_state_enabled"),Persistent);Native->SetBoolField(TEXT("owner_same_world"),SameWorld);
        Native->SetBoolField(TEXT("managed_layer_component"),Managed);
        bool AllNativeMatch=true;int32 NativeCompared=0;TArray<TSharedPtr<FJsonValue>> NativeComparisons;
        for(FName Name:UserNames)if(MCVFXNativeInputs::IsNativeName(Name)) {
            const auto* Entry=Player->FindAcceptedNativeInputForEditorInspection(Name);FValue Expected;Expected.bPresent=Entry!=nullptr;
            const bool Supported=Entry&&Entry->Type==EMCVFXNativeInputType::Float;
            if(Supported) {
                Expected.Type=FNiagaraTypeDefinition::GetFloatDef();Expected.Scalar=Entry->FloatValue;
                Expected.bValid=FMath::IsFinite(Expected.Scalar);
                if(Expected.bValid)Expected.Json=MakeShared<FJsonValueNumber>(Expected.Scalar);
                else Expected.Reason=TEXT("accepted_payload_float_is_not_finite");
            } else Expected.Reason=Entry?TEXT("accepted_payload_non_float_comparison_unobserved"):TEXT("accepted_payload_entry_missing_or_exceeds_64_entry_bound");
            const auto& Actual=Values.FindChecked(Name);
            const bool Match=Supported&&Expected.bValid&&Actual.bPresent&&Actual.bValid&&Actual.Type==Expected.Type
                &&FMath::IsNearlyEqual(Actual.Scalar,Expected.Scalar,.001f);
            auto Check=MakeShared<FJsonObject>();Check->SetStringField(TEXT("name"),Name.ToString());
            Check->SetObjectField(TEXT("expected"),ValueJson(Name,Expected));Check->SetObjectField(TEXT("actual"),ValueJson(Name,Actual));
            Check->SetBoolField(TEXT("comparison_supported"),Supported);Check->SetBoolField(TEXT("matches"),Match);
            if(Entry)Check->SetNumberField(TEXT("accepted_payload_type"),static_cast<uint8>(Entry->Type));
            else Check->SetField(TEXT("accepted_payload_type"),MakeShared<FJsonValueNull>());
            NativeComparisons.Add(MakeShared<FJsonValueObject>(Check));AllNativeMatch&=Match;++NativeCompared;
        }
        Native->SetArrayField(TEXT("comparisons"),NativeComparisons);Native->SetNumberField(TEXT("requested_native_fields"),NativeCompared);
        Native->SetBoolField(TEXT("requested_float_fields_match"),NativeCompared>0&&AllNativeMatch);
        Native->SetBoolField(TEXT("managed_persistent_accepted_payload_match"),Managed&&Persistent&&SameWorld&&NativeCompared>0&&AllNativeMatch);
        Capture->SetNumberField(TEXT("retained_part_count"),RetainedCount);Capture->SetNumberField(TEXT("effective_part_count"),Count);
        Capture->SetBoolField(TEXT("retained_parts_truncated"),RetainedCount>MaxCapturedParts);
        const auto* World=Player->GetWorld();const auto* State=World?World->GetGameState():nullptr;
        const double Now=State?State->GetServerWorldTimeSeconds():World?World->GetTimeSeconds():0.;
        const double Delay=ContactAt-Now+Player->GetPlaybackTime()-LayerStart;
        Capture->SetBoolField(TEXT("contact_clock_valid"),FMath::IsFinite(ContactAt)&&FMath::IsFinite(Now)&&FMath::IsFinite(Delay));
        if(FMath::IsFinite(Now))Capture->SetNumberField(TEXT("sample_world_time"),Now);
        bool AllMatch=true;int32 Compared=0;TArray<TSharedPtr<FJsonValue>> Comparisons;
        const TArray<FName> Required=CaptureNames();
        for(FName Name:Required)if(const auto* Actual=Values.Find(Name)) {
            FValue Expected;Expected.bPresent=true;Expected.bValid=true;const FString Text=Name.ToString();
            if(Name==FName(TEXT("User.ToolPartCount"))) {
                Expected.Type=FNiagaraTypeDefinition::GetIntDef();Expected.Integer=Count;Expected.Json=MakeShared<FJsonValueNumber>(Count);
            } else if(Name==FName(TEXT("User.EchoOverhead"))) {
                Expected.Type=FNiagaraTypeDefinition::GetBoolDef();Expected.Boolean=bOverhead;Expected.Json=MakeShared<FJsonValueBoolean>(bOverhead);
            } else if(Name==FName(TEXT("User.ContactDelay"))) {
                Expected.Type=FNiagaraTypeDefinition::GetFloatDef();Expected.Scalar=float(Delay);
                Expected.bValid=Managed&&FMath::IsFinite(Expected.Scalar);
                if(Expected.bValid)Expected.Json=MakeShared<FJsonValueNumber>(Expected.Scalar);
                else Expected.Reason=TEXT("managed_layer_and_finite_native_clock_required");
            } else {
                const int32 Index=Text[Text.Len()-1]-TEXT('0');
                const auto* Part=Parts&&Parts->IsValidIndex(Index)?&(*Parts)[Index]:nullptr;
                const FTransform Pose=Part?Part->ContactPose:FTransform::Identity;
                Expected.bValid=!Pose.ContainsNaN()&&Pose.GetRotation().IsNormalized();
                if(Text.StartsWith(TEXT("User.ToolMesh"))) {
                    Expected.Type=FNiagaraTypeDefinition(UStaticMesh::StaticClass());Expected.Object=Part?Part->Mesh.Get():nullptr;
                    Expected.bValid=!Part||IsValid(Expected.Object);Expected.Json=MakeShared<FJsonValueObject>(ObjectValue(Expected.Object));
                } else if(Text.StartsWith(TEXT("User.ToolOrientation"))) {
                    Expected.Type=FNiagaraTypeDefinition::GetQuatDef();Expected.Quaternion=Pose.GetRotation();
                    if(Expected.bValid)Expected.Json=Numbers({Expected.Quaternion.X,Expected.Quaternion.Y,Expected.Quaternion.Z,Expected.Quaternion.W});
                } else {
                    Expected.Type=FNiagaraTypeDefinition::GetVec3Def();Expected.Vector=Text.StartsWith(TEXT("User.ToolPosition"))?Pose.GetLocation():Pose.GetScale3D();
                    if(Expected.bValid)Expected.Json=Numbers({Expected.Vector.X,Expected.Vector.Y,Expected.Vector.Z});
                }
            }
            bool Match=Actual->bPresent&&Actual->bValid&&Expected.bValid&&Actual->Type==Expected.Type;
            if(Match) {
                if(Expected.Type.IsUObject())Match=Actual->Object==Expected.Object;
                else if(Expected.Type==FNiagaraTypeDefinition::GetBoolDef())Match=Actual->Boolean==Expected.Boolean;
                else if(Expected.Type==FNiagaraTypeDefinition::GetIntDef())Match=Actual->Integer==Expected.Integer;
                else if(Expected.Type==FNiagaraTypeDefinition::GetFloatDef())Match=FMath::IsNearlyEqual(Actual->Scalar,Expected.Scalar,.001f);
                else if(Expected.Type==FNiagaraTypeDefinition::GetVec3Def())Match=Actual->Vector.Equals(Expected.Vector,.001);
                else if(Expected.Type==FNiagaraTypeDefinition::GetQuatDef())Match=Actual->Quaternion.Equals(Expected.Quaternion,.0001);
            }
            auto Check=MakeShared<FJsonObject>();Check->SetStringField(TEXT("name"),Text);
            Check->SetObjectField(TEXT("expected"),ValueJson(Name,Expected));Check->SetObjectField(TEXT("actual"),ValueJson(Name,*Actual));
            Check->SetBoolField(TEXT("matches"),Match);Comparisons.Add(MakeShared<FJsonValueObject>(Check));AllMatch&=Match;++Compared;
        }
        bool Complete=true;for(FName Name:Required)Complete&=Names.Contains(Name);
        Capture->SetArrayField(TEXT("comparisons"),Comparisons);Capture->SetNumberField(TEXT("compared_fields"),Compared);
        Capture->SetBoolField(TEXT("requested_fields_match"),Compared>0&&AllMatch);
        Capture->SetBoolField(TEXT("complete_contract_requested"),Complete);
        Capture->SetBoolField(TEXT("runtime_capture_match_verified"),Managed&&Complete&&Count>0&&RetainedCount<=MaxCapturedParts&&AllMatch);
    } else Capture->SetStringField(TEXT("reason"),TEXT("component_has_no_actual_composition_player_owner"));
    FString Result;const auto Writer=TJsonWriterFactory<>::Create(&Result);
    if(!FJsonSerializer::Serialize(Root,Writer)){OutError=TEXT("Could not serialize finite typed component inspection.");return {};}
    return Result;
}


FString UMCPerkPreviewEditorLibrary::GetNiagaraAudioInspection(UNiagaraSystem* System,FString& OutError)
{
    OutError.Reset();
    if(!IsInGameThread()||!GIsEditor||IsRunningCookCommandlet())
    {OutError=TEXT("Inspect Niagara from the Editor game thread, outside cook.");return {};}
    if(!IsValid(System)||System->IsTemplate()||System->HasAnyFlags(RF_NeedLoad|RF_NeedPostLoad|RF_BeginDestroyed|RF_FinishDestroyed))
    {OutError=TEXT("Pass an already-loaded, initialized Niagara System; inspection never loads or repairs it.");return {};}
    const auto MCNiagaraAudioObject=[](const UObject* Object) {
        auto Json=MakeShared<FJsonObject>();Json->SetStringField(TEXT("path"),GetPathNameSafe(Object));
        Json->SetStringField(TEXT("class"),Object?GetPathNameSafe(Object->GetClass()):FString());
        Json->SetBoolField(TEXT("valid"),IsValid(Object));
        Json->SetBoolField(TEXT("needs_load_or_postload"),Object&&Object->HasAnyFlags(RF_NeedLoad|RF_NeedPostLoad));
        return Json;
    };
    const auto MCNiagaraAudioReady=[](const UObject* Object) {
        return IsValid(Object)&&!Object->HasAnyFlags(RF_NeedLoad|RF_NeedPostLoad|RF_BeginDestroyed|RF_FinishDestroyed);
    };
    const auto MCNiagaraAudioResidentReferences=[&](UObject* Object) {
        auto Result=MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("basis"),TEXT("direct_already_resident_nontransient_hard_object_references"));
        Result->SetBoolField(TEXT("recursive"),false);Result->SetBoolField(TEXT("includes_unresolved_soft_references"),false);
        Result->SetBoolField(TEXT("unresolved_hard_handles_skipped_by_collector"),true);
        Result->SetBoolField(TEXT("certifies_behavior_or_audio_absence"),false);
        if(!MCNiagaraAudioReady(Object)) {
            Result->SetStringField(TEXT("status"),TEXT("UNKNOWN_object_invalid_or_not_initialized"));return Result;
        }
        TArray<UObject*> References;
        // Public property/reference visitor, no raw layout/private memory read,
        // no recursive asset loading, archetype expansion or custom Serialize.
        FReferenceFinder Finder(References,nullptr,true,true,false,true);
        Finder.FindReferences(Object);
        References.Sort([](const UObject& A,const UObject& B){return A.GetPathName()<B.GetPathName();});
        Result->SetNumberField(TEXT("native_reference_count"),References.Num());
        TArray<TSharedPtr<FJsonValue>> Values;
        const int32 Count=FMath::Min(References.Num(),128);
        for(int32 Index=0;Index<Count;++Index) {
            auto Entry=MCNiagaraAudioObject(References[Index]);Entry->SetNumberField(TEXT("index"),Index);
            Values.Add(MakeShared<FJsonValueObject>(Entry));
        }
        Result->SetBoolField(TEXT("truncated"),References.Num()>Count);
        Result->SetStringField(TEXT("status"),References.Num()>Count?TEXT("PARTIAL_bound_exceeded"):TEXT("OBSERVED_direct_hard_references_only"));
        Result->SetArrayField(TEXT("references"),Values);return Result;
    };
    const auto MCNiagaraAudioStatelessRenderer=[&](UNiagaraRendererProperties* Renderer,int32 Index) {
        auto Entry=MCNiagaraAudioObject(Renderer);Entry->SetNumberField(TEXT("index"),Index);
        const bool Ready=MCNiagaraAudioReady(Renderer);Entry->SetBoolField(TEXT("inspection_available"),Ready);
        if(!Ready) {Entry->SetStringField(TEXT("status"),TEXT("UNKNOWN_renderer_invalid_or_not_initialized"));return Entry;}
        Entry->SetBoolField(TEXT("enabled"),Renderer->GetIsEnabled());
        Entry->SetObjectField(TEXT("resident_references"),MCNiagaraAudioResidentReferences(Renderer));
        if(auto* ComponentRenderer=Cast<UNiagaraComponentRendererProperties>(Renderer)) {
            const auto& Type=ComponentRenderer->ComponentType.GetGCPtr();
            Entry->SetStringField(TEXT("component_type"),Type.GetPathName());
            Entry->SetBoolField(TEXT("component_type_reference_resolved"),Type.IsResolved());
            const auto& TemplateReference=ComponentRenderer->TemplateComponent;
            Entry->SetBoolField(TEXT("component_template_reference_resolved"),TemplateReference.IsResolved());
            Entry->SetStringField(TEXT("component_template_reference_path"),TemplateReference.GetPathName());
            auto* Template=TemplateReference.IsResolved()?TemplateReference.Get():nullptr;
            Entry->SetObjectField(TEXT("component_template"),MCNiagaraAudioObject(Template));
            const bool TemplateReady=MCNiagaraAudioReady(Template);
            Entry->SetBoolField(TEXT("component_template_inspection_available"),TemplateReady);
            if(TemplateReady) {
                Entry->SetObjectField(TEXT("component_template_resident_references"),MCNiagaraAudioResidentReferences(Template));
                const auto* AudioTemplate=Cast<UAudioComponent>(Template);
                Entry->SetBoolField(TEXT("template_is_audio_component"),AudioTemplate!=nullptr);
                if(AudioTemplate) {
                    Entry->SetBoolField(TEXT("template_sound_reference_resolved"),AudioTemplate->Sound.IsResolved());
                    Entry->SetStringField(TEXT("template_sound_reference_path"),AudioTemplate->Sound.GetPathName());
                    if(AudioTemplate->Sound.IsResolved())Entry->SetObjectField(TEXT("template_sound"),MCNiagaraAudioObject(AudioTemplate->Sound.Get()));
                    else Entry->SetStringField(TEXT("template_sound_status"),TEXT("UNKNOWN_unresolved_hard_reference_not_loaded"));
                }
            } else Entry->SetStringField(TEXT("component_template_status"),TEXT("UNKNOWN_template_absent_or_not_initialized"));
        }
        return Entry;
    };

    auto Inspection=MakeShared<FJsonObject>();Inspection->SetNumberField(TEXT("schema_version"),1);
    Inspection->SetStringField(TEXT("system"),System->GetPathName());
    Inspection->SetBoolField(TEXT("package_dirty"),System->GetOutermost()->IsDirty());
    Inspection->SetBoolField(TEXT("certifies_audio_absence"),false);
    Inspection->SetStringField(TEXT("remaining_audit"),TEXT("Inspect nested scripts/data interfaces and component templates; native counts alone do not establish silence."));
    TArray<TSharedPtr<FJsonValue>> SystemScripts;
    auto SystemSpawn=MCNiagaraAudioObject(System->GetSystemSpawnScript());SystemSpawn->SetStringField(TEXT("role"),TEXT("SystemSpawn"));
    auto SystemUpdate=MCNiagaraAudioObject(System->GetSystemUpdateScript());SystemUpdate->SetStringField(TEXT("role"),TEXT("SystemUpdate"));
    SystemScripts.Add(MakeShared<FJsonValueObject>(SystemSpawn));SystemScripts.Add(MakeShared<FJsonValueObject>(SystemUpdate));
    Inspection->SetArrayField(TEXT("system_scripts"),SystemScripts);
    auto SavedDependencies=MakeShared<FJsonObject>();
    SavedDependencies->SetStringField(TEXT("basis"),TEXT("direct_on_disk_package_dependencies_AssetRegistry_Package_category_hard_and_soft"));
    SavedDependencies->SetStringField(TEXT("package"),System->GetOutermost()->GetName());
    SavedDependencies->SetBoolField(TEXT("transitive_closure"),false);
    SavedDependencies->SetBoolField(TEXT("certifies_behavior_or_audio_absence"),false);
    const auto* RegistryModule=FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry"));
    auto* Registry=RegistryModule?RegistryModule->TryGet():nullptr;
    if(Registry) {
        TArray<FName> Dependencies;
        const bool Found=Registry->GetDependencies(System->GetOutermost()->GetFName(),Dependencies,UE::AssetRegistry::EDependencyCategory::Package);
        Dependencies.Sort(FNameLexicalLess());
        SavedDependencies->SetBoolField(TEXT("query_succeeded"),Found);
        SavedDependencies->SetBoolField(TEXT("registry_loading_assets"),Registry->IsLoadingAssets());
        SavedDependencies->SetNumberField(TEXT("native_dependency_count"),Dependencies.Num());
        TArray<TSharedPtr<FJsonValue>> Values;const int32 Count=FMath::Min(Dependencies.Num(),512);
        for(int32 Index=0;Index<Count;++Index)Values.Add(MakeShared<FJsonValueString>(Dependencies[Index].ToString()));
        SavedDependencies->SetArrayField(TEXT("packages"),Values);
        SavedDependencies->SetBoolField(TEXT("truncated"),Dependencies.Num()>Count);
        SavedDependencies->SetStringField(TEXT("status"),!Found?TEXT("UNKNOWN_package_not_indexed"):Dependencies.Num()>Count?TEXT("PARTIAL_bound_exceeded"):TEXT("OBSERVED_direct_saved_package_dependencies_only"));
    } else SavedDependencies->SetStringField(TEXT("status"),TEXT("UNKNOWN_AssetRegistry_not_already_available"));
    Inspection->SetObjectField(TEXT("saved_package_dependencies"),SavedDependencies);

    Inspection->SetNumberField(TEXT("native_handle_count"),System->GetEmitterHandles().Num());
    TArray<TSharedPtr<FJsonValue>> Emitters;
    for(const auto& Handle:System->GetEmitterHandles()) {
        auto Emitter=MakeShared<FJsonObject>();Emitter->SetStringField(TEXT("name"),Handle.GetName().ToString());
        Emitter->SetStringField(TEXT("handle_guid"),Handle.GetId().ToString());Emitter->SetBoolField(TEXT("enabled"),Handle.GetIsEnabled());
        Emitter->SetNumberField(TEXT("native_mode"),static_cast<int32>(Handle.GetEmitterMode()));
        const auto* InstanceBase=Handle.GetEmitterBase();
        Emitter->SetObjectField(TEXT("instance"),MCNiagaraAudioObject(InstanceBase));
        Emitter->SetStringField(TEXT("requested_instance_version_guid"),Handle.GetInstance().Version.ToString());
        const bool bInstanceReady=IsValid(InstanceBase)&&!InstanceBase->HasAnyFlags(RF_NeedLoad|RF_NeedPostLoad|RF_BeginDestroyed|RF_FinishDestroyed);
        const auto* Data=bInstanceReady?Handle.GetEmitterData():nullptr;
        Emitter->SetBoolField(TEXT("selected_version_data_available"),Data!=nullptr);
        Emitter->SetStringField(TEXT("state"),Data?TEXT("stateful"):Handle.GetStatelessEmitter()?TEXT("stateless"):TEXT("unknown"));
        if(Data) {
            Emitter->SetStringField(TEXT("selected_version_guid"),Data->Version.VersionGuid.ToString());
            Emitter->SetNumberField(TEXT("selected_version_major"),Data->Version.MajorVersion);
            Emitter->SetNumberField(TEXT("selected_version_minor"),Data->Version.MinorVersion);
            TArray<TSharedPtr<FJsonValue>> Scripts;
            const auto AddScript=[&](const TCHAR* Role,const UNiagaraScript* SelectedScript) {
                auto Entry=MCNiagaraAudioObject(SelectedScript);Entry->SetStringField(TEXT("role"),Role);
                Scripts.Add(MakeShared<FJsonValueObject>(Entry));
            };
            AddScript(TEXT("EmitterSpawn"),Data->EmitterSpawnScriptProps.Script.Get());
            AddScript(TEXT("EmitterUpdate"),Data->EmitterUpdateScriptProps.Script.Get());
            AddScript(TEXT("ParticleSpawn"),Data->SpawnScriptProps.Script.Get());
            AddScript(TEXT("ParticleUpdate"),Data->UpdateScriptProps.Script.Get());
            AddScript(TEXT("GPUCompute"),Data->GetGPUComputeScript());
            Emitter->SetArrayField(TEXT("normal_scripts"),Scripts);
            Emitter->SetNumberField(TEXT("native_event_handler_count"),Data->EventHandlerScriptProps.Num());
            TArray<TSharedPtr<FJsonValue>> Handlers;
            for(int32 Index=0;Index<Data->EventHandlerScriptProps.Num();++Index) {
                const auto& Handler=Data->EventHandlerScriptProps[Index];auto Entry=MCNiagaraAudioObject(Handler.Script.Get());
                Entry->SetNumberField(TEXT("index"),Index);Entry->SetStringField(TEXT("source_event"),Handler.SourceEventName.ToString());
                Entry->SetStringField(TEXT("source_emitter_guid"),Handler.SourceEmitterID.ToString());
                Handlers.Add(MakeShared<FJsonValueObject>(Entry));
            }
            Emitter->SetArrayField(TEXT("event_handlers"),Handlers);
            Emitter->SetNumberField(TEXT("native_simulation_stage_count"),Data->GetSimulationStages().Num());
            TArray<TSharedPtr<FJsonValue>> Stages;
            for(int32 Index=0;Index<Data->GetSimulationStages().Num();++Index) {
                const auto* Stage=Data->GetSimulationStages()[Index];auto Entry=MCNiagaraAudioObject(Stage);
                Entry->SetNumberField(TEXT("index"),Index);
                if(IsValid(Stage)) {
                    Entry->SetStringField(TEXT("name"),Stage->SimulationStageName.ToString());Entry->SetBoolField(TEXT("enabled"),Stage->bEnabled!=0);
                    Entry->SetObjectField(TEXT("script"),MCNiagaraAudioObject(Stage->Script.Get()));
                }
                Stages.Add(MakeShared<FJsonValueObject>(Entry));
            }
            Emitter->SetArrayField(TEXT("simulation_stages"),Stages);
            Emitter->SetNumberField(TEXT("native_renderer_count"),Data->GetRenderers().Num());
            TArray<TSharedPtr<FJsonValue>> Renderers;
            for(int32 Index=0;Index<Data->GetRenderers().Num();++Index) {
                const auto* Renderer=Data->GetRenderers()[Index];auto Entry=MCNiagaraAudioObject(Renderer);
                Entry->SetNumberField(TEXT("index"),Index);
                if(IsValid(Renderer))Entry->SetBoolField(TEXT("enabled"),Renderer->GetIsEnabled());
                if(const auto* ComponentRenderer=Cast<UNiagaraComponentRendererProperties>(Renderer)) {
                    Entry->SetStringField(TEXT("component_type"),GetPathNameSafe(ComponentRenderer->ComponentType.Get()));
                    Entry->SetObjectField(TEXT("component_template"),MCNiagaraAudioObject(ComponentRenderer->TemplateComponent.Get()));
                    const auto* AudioTemplate=Cast<UAudioComponent>(ComponentRenderer->TemplateComponent.Get());
                    Entry->SetBoolField(TEXT("template_is_audio_component"),AudioTemplate!=nullptr);
                    if(AudioTemplate)Entry->SetObjectField(TEXT("template_sound"),MCNiagaraAudioObject(AudioTemplate->Sound.Get()));
                }
                Renderers.Add(MakeShared<FJsonValueObject>(Entry));
            }
            Emitter->SetArrayField(TEXT("renderers"),Renderers);
        }
        else if(auto* Stateless=Handle.GetStatelessEmitter()) {
            // No fake stateful version/scripts/handlers for lightweight emitters.
            // Read the actual selected native emitter through public getters.
            Emitter->SetNumberField(TEXT("stateless_metadata_schema_version"),1);
            const bool Ready=bInstanceReady&&MCNiagaraAudioReady(Stateless);
            Emitter->SetBoolField(TEXT("stateless_metadata_available"),Ready);
            if(Ready) {
                const auto* Template=Stateless->GetEmitterTemplate();
                Emitter->SetObjectField(TEXT("stateless_template"),MCNiagaraAudioObject(Template));
                const bool TemplateReady=MCNiagaraAudioReady(Template);
                Emitter->SetBoolField(TEXT("stateless_template_metadata_available"),TemplateReady);
                if(TemplateReady) {
                    auto* MutableTemplate=const_cast<UNiagaraStatelessEmitterTemplate*>(Template);
                    Emitter->SetObjectField(TEXT("stateless_template_resident_references"),MCNiagaraAudioResidentReferences(MutableTemplate));
                    const auto Classes=Template->GetModules();
                    Emitter->SetNumberField(TEXT("native_stateless_template_module_class_count"),Classes.Num());
                    TArray<TSharedPtr<FJsonValue>> Values;const int32 Count=FMath::Min(Classes.Num(),256);
                    for(int32 Index=0;Index<Count;++Index) {
                        auto Entry=MCNiagaraAudioObject(Classes[Index].IsResolved()?Classes[Index].Get():nullptr);Entry->SetNumberField(TEXT("index"),Index);
                        Entry->SetBoolField(TEXT("reference_resolved"),Classes[Index].IsResolved());
                        Entry->SetStringField(TEXT("reference_path"),Classes[Index].GetPathName());
                        Values.Add(MakeShared<FJsonValueObject>(Entry));
                    }
                    Emitter->SetArrayField(TEXT("stateless_template_module_classes"),Values);
                    Emitter->SetBoolField(TEXT("stateless_template_modules_truncated"),Classes.Num()>Count);
                } else Emitter->SetStringField(TEXT("stateless_template_status"),TEXT("UNKNOWN_template_absent_or_not_initialized"));
                const auto& Modules=Stateless->GetModules();
                Emitter->SetNumberField(TEXT("native_stateless_module_count"),Modules.Num());
                TArray<TSharedPtr<FJsonValue>> ModuleValues;const int32 ModuleCount=FMath::Min(Modules.Num(),256);
                bool ModulesReady=true;
                for(int32 Index=0;Index<ModuleCount;++Index) {
                    auto* Module=Modules[Index].IsResolved()?Modules[Index].Get():nullptr;auto Entry=MCNiagaraAudioObject(Module);Entry->SetNumberField(TEXT("index"),Index);
                    Entry->SetBoolField(TEXT("reference_resolved"),Modules[Index].IsResolved());
                    Entry->SetStringField(TEXT("reference_path"),Modules[Index].GetPathName());
                    const bool ModuleReady=MCNiagaraAudioReady(Module);Entry->SetBoolField(TEXT("inspection_available"),ModuleReady);ModulesReady&=ModuleReady;
                    if(ModuleReady) {
                        Entry->SetBoolField(TEXT("enabled"),Module->IsModuleEnabled());
                        Entry->SetObjectField(TEXT("resident_references"),MCNiagaraAudioResidentReferences(Module));
                        const auto& GeneratedBy=Module->GetClass()->ClassGeneratedBy;
                        Entry->SetBoolField(TEXT("class_generated_by_reference_resolved"),GeneratedBy.IsResolved());
                        Entry->SetStringField(TEXT("class_generated_by_reference_path"),GeneratedBy.GetPathName());
                        if(GeneratedBy.IsResolved())Entry->SetObjectField(TEXT("class_generated_by"),MCNiagaraAudioObject(GeneratedBy.Get()));
                    } else Entry->SetStringField(TEXT("status"),TEXT("UNKNOWN_module_invalid_or_not_initialized"));
                    ModuleValues.Add(MakeShared<FJsonValueObject>(Entry));
                }
                Emitter->SetArrayField(TEXT("stateless_modules"),ModuleValues);
                Emitter->SetBoolField(TEXT("stateless_modules_truncated"),Modules.Num()>ModuleCount);
                const auto& Renderers=Stateless->GetRenderers();
                Emitter->SetNumberField(TEXT("native_stateless_renderer_count"),Renderers.Num());
                TArray<TSharedPtr<FJsonValue>> RendererValues;const int32 RendererCount=FMath::Min(Renderers.Num(),64);
                bool RenderersReady=true;
                for(int32 Index=0;Index<RendererCount;++Index) {
                    auto* Renderer=Renderers[Index];RenderersReady&=MCNiagaraAudioReady(Renderer);
                    RendererValues.Add(MakeShared<FJsonValueObject>(MCNiagaraAudioStatelessRenderer(Renderer,Index)));
                }
                Emitter->SetArrayField(TEXT("stateless_renderers"),RendererValues);
                Emitter->SetBoolField(TEXT("stateless_renderers_truncated"),Renderers.Num()>RendererCount);
                Emitter->SetBoolField(TEXT("stateless_module_renderer_enumeration_complete"),ModulesReady&&RenderersReady&&Modules.Num()==ModuleCount&&Renderers.Num()==RendererCount);
                Emitter->SetStringField(TEXT("stateless_remaining_audit"),TEXT("Actual renderer/module metadata and direct references only. Template shader/native/Blueprint behavior, unresolved soft refs, transitive resource closure and audibility remain unaudited."));
            } else Emitter->SetStringField(TEXT("stateless_metadata_status"),TEXT("UNKNOWN_emitter_invalid_or_not_initialized"));
        }
        // Unsupported/missing native data remains explicitly unknown, never zero.

        Emitters.Add(MakeShared<FJsonValueObject>(Emitter));
    }
    Inspection->SetArrayField(TEXT("emitters"),Emitters);
    FString Result;const auto Writer=TJsonWriterFactory<>::Create(&Result);
    if(!FJsonSerializer::Serialize(Inspection,Writer))
    {OutError=TEXT("Could not serialize the Niagara audio inspection.");return {};}
    return Result;
}

FString UMCPerkPreviewEditorLibrary::DumpNiagaraScriptGraph(UNiagaraScript* Script,FString& OutError)
{
    OutError.Reset();
    if(!IsInGameThread()||!GIsEditor||IsRunningCookCommandlet())
    {OutError=TEXT("Inspect Niagara source from the Editor game thread, outside cook.");return {};}
    if(!IsValid(Script)||Script->IsTemplate()||Script->HasAnyFlags(RF_NeedLoad|RF_NeedPostLoad|RF_BeginDestroyed|RF_FinishDestroyed))
    {OutError=TEXT("Pass an already-loaded, initialized Niagara script asset instance.");return {};}
    const auto Versions=Script->GetAllAvailableVersions();
    if(Versions.IsEmpty())
    {OutError=TEXT("This script has no initialized versioned source data; inspection will not initialize or repair it.");return {};}
    const FNiagaraAssetVersion Exposed=Script->GetExposedVersion();
    const bool bRequestedVersion=Script->VersionToOpenInEditor.IsValid();
    const FGuid Selected=bRequestedVersion?Script->VersionToOpenInEditor:Exposed.VersionGuid;
    const FVersionedNiagaraScriptData* Data=Script->GetScriptData(Selected);
    if(!Data||Data->Version.VersionGuid!=Selected)
    {OutError=TEXT("The requested/exposed version is unavailable; refusing to silently inspect another version.");return {};}
    const auto* Source=Cast<UNiagaraScriptSource>(Script->GetSource(Selected));
    const UNiagaraGraph* Graph=Source?Source->NodeGraph.Get():nullptr;
    if(!IsValid(Graph)||Graph->HasAnyFlags(RF_NeedLoad|RF_NeedPostLoad|RF_BeginDestroyed|RF_FinishDestroyed))
    {OutError=TEXT("This version has no initialized Niagara editor node graph.");return {};}

    const auto TypeJson=[](const FNiagaraTypeDefinition& Type) {
        auto O=MakeShared<FJsonObject>();O->SetBoolField(TEXT("valid"),Type.IsValid());
        O->SetStringField(TEXT("name"),Type.GetName());
        O->SetStringField(TEXT("class"),GetPathNameSafe(Type.GetClass()));
        O->SetStringField(TEXT("struct"),GetPathNameSafe(Type.GetStruct()));
        O->SetStringField(TEXT("enum"),GetPathNameSafe(Type.GetEnum()));return O;
    };
    const auto VariableJson=[&TypeJson](const FNiagaraVariableBase& Variable) {
        auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("name"),Variable.GetName().ToString());
        O->SetObjectField(TEXT("type"),TypeJson(Variable.GetType()));return O;
    };
    const auto VersionJson=[](const FNiagaraAssetVersion& Version) {
        auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("guid"),Version.VersionGuid.ToString());
        O->SetNumberField(TEXT("major"),Version.MajorVersion);O->SetNumberField(TEXT("minor"),Version.MinorVersion);return O;
    };
    auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("schema_version"),1);
    Root->SetStringField(TEXT("script"),Script->GetPathName());Root->SetNumberField(TEXT("usage"),static_cast<int32>(Script->Usage));
    Root->SetBoolField(TEXT("versioning_enabled"),Script->IsVersioningEnabled());
    Root->SetStringField(TEXT("selection"),bRequestedVersion?TEXT("VersionToOpenInEditor"):TEXT("ExposedVersion"));
    Root->SetObjectField(TEXT("selected_version"),VersionJson(Data->Version));
    Root->SetObjectField(TEXT("exposed_version"),VersionJson(Exposed));
    TArray<TSharedPtr<FJsonValue>> Available;for(const auto& Version:Versions)Available.Add(MakeShared<FJsonValueObject>(VersionJson(Version)));
    Root->SetArrayField(TEXT("available_versions"),Available);
    Root->SetStringField(TEXT("source"),Source->GetPathName());Root->SetStringField(TEXT("graph"),Graph->GetPathName());
    Root->SetStringField(TEXT("graph_schema"),GetPathNameSafe(Graph->GetSchema()));
    TArray<TSharedPtr<FJsonValue>> Nodes;
    for(const auto& NodePtr:Graph->Nodes) {
        const UEdGraphNode* Node=NodePtr.Get();if(!IsValid(Node))continue;
        auto N=MakeShared<FJsonObject>();N->SetStringField(TEXT("object"),Node->GetPathName());
        N->SetStringField(TEXT("class"),Node->GetClass()->GetPathName());N->SetStringField(TEXT("guid"),Node->NodeGuid.ToString());
        N->SetNumberField(TEXT("x"),Node->NodePosX);N->SetNumberField(TEXT("y"),Node->NodePosY);
        // Dispatch through the exported Engine virtual: no dependency on unexported Niagara overrides.
        N->SetStringField(TEXT("title"),Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
        if(const auto* Input=Cast<UNiagaraNodeInput>(Node)) {
            N->SetObjectField(TEXT("input_variable"),VariableJson(Input->Input));
            N->SetNumberField(TEXT("input_usage"),static_cast<int32>(Input->Usage));
            N->SetBoolField(TEXT("exposed"),Input->ExposureOptions.bExposed);N->SetBoolField(TEXT("required"),Input->ExposureOptions.bRequired);
            N->SetBoolField(TEXT("hidden"),Input->ExposureOptions.bHidden);N->SetBoolField(TEXT("auto_bind"),Input->ExposureOptions.bCanAutoBind);
        }
        if(const auto* Call=Cast<UNiagaraNodeFunctionCall>(Node)) {
            auto F=MakeShared<FJsonObject>();F->SetStringField(TEXT("function_name"),Call->GetFunctionName());
            F->SetStringField(TEXT("script"),GetPathNameSafe(Call->FunctionScript.Get()));
            F->SetStringField(TEXT("selected_script_version"),Call->SelectedScriptVersion.ToString());
            const auto& S=Call->Signature;F->SetStringField(TEXT("signature_name"),S.Name.ToString());
            F->SetStringField(TEXT("owner_name"),S.OwnerName.ToString());F->SetNumberField(TEXT("function_version"),S.FunctionVersion);
            F->SetBoolField(TEXT("member_function"),S.bMemberFunction);F->SetBoolField(TEXT("supports_cpu"),S.bSupportsCPU);F->SetBoolField(TEXT("supports_gpu"),S.bSupportsGPU);
            TArray<TSharedPtr<FJsonValue>> Inputs,Outputs;
            for(const auto& V:S.Inputs)Inputs.Add(MakeShared<FJsonValueObject>(VariableJson(V)));
            for(const auto& V:S.Outputs)Outputs.Add(MakeShared<FJsonValueObject>(VariableJson(V)));
            F->SetArrayField(TEXT("inputs"),Inputs);F->SetArrayField(TEXT("outputs"),Outputs);
            auto Specifiers=MakeShared<FJsonObject>();for(const auto& Pair:Call->FunctionSpecifiers)Specifiers->SetStringField(Pair.Key.ToString(),Pair.Value.ToString());
            F->SetObjectField(TEXT("specifiers"),Specifiers);N->SetObjectField(TEXT("function"),F);
        }
        TArray<TSharedPtr<FJsonValue>> Pins;
        for(const UEdGraphPin* Pin:Node->Pins)if(Pin) {
            auto P=MakeShared<FJsonObject>();P->SetStringField(TEXT("id"),Pin->PinId.ToString());
            P->SetStringField(TEXT("persistent_guid"),Pin->PersistentGuid.ToString());P->SetStringField(TEXT("name"),Pin->PinName.ToString());
            P->SetStringField(TEXT("direction"),Pin->Direction==EGPD_Input?TEXT("input"):TEXT("output"));
            P->SetStringField(TEXT("category"),Pin->PinType.PinCategory.ToString());P->SetStringField(TEXT("subcategory"),Pin->PinType.PinSubCategory.ToString());
            P->SetStringField(TEXT("type_object"),GetPathNameSafe(Pin->PinType.PinSubCategoryObject.Get()));
            P->SetNumberField(TEXT("container"),static_cast<int32>(Pin->PinType.ContainerType));
            P->SetBoolField(TEXT("reference"),Pin->PinType.bIsReference);P->SetBoolField(TEXT("const"),Pin->PinType.bIsConst);
            P->SetObjectField(TEXT("niagara_type"),TypeJson(UEdGraphSchema_Niagara::PinToTypeDefinition(Pin)));
            P->SetStringField(TEXT("default_value"),Pin->DefaultValue);P->SetStringField(TEXT("autogenerated_default"),Pin->AutogeneratedDefaultValue);
            P->SetStringField(TEXT("default_object"),GetPathNameSafe(Pin->DefaultObject.Get()));
            P->SetBoolField(TEXT("default_ignored"),Pin->bDefaultValueIsIgnored);P->SetBoolField(TEXT("default_read_only"),Pin->bDefaultValueIsReadOnly);
            TArray<TSharedPtr<FJsonValue>> Links;
            for(const UEdGraphPin* Other:Pin->LinkedTo)if(Other) {
                auto L=MakeShared<FJsonObject>();const UEdGraphNode* Owner=Other->GetOwningNodeUnchecked();
                L->SetStringField(TEXT("node_guid"),Owner?Owner->NodeGuid.ToString():FString());L->SetStringField(TEXT("pin_id"),Other->PinId.ToString());
                L->SetStringField(TEXT("pin_name"),Other->PinName.ToString());Links.Add(MakeShared<FJsonValueObject>(L));
            }
            P->SetArrayField(TEXT("links"),Links);Pins.Add(MakeShared<FJsonValueObject>(P));
        }
        N->SetArrayField(TEXT("pins"),Pins);Nodes.Add(MakeShared<FJsonValueObject>(N));
    }
    Root->SetArrayField(TEXT("nodes"),Nodes);
    FString Result;const auto Writer=TJsonWriterFactory<>::Create(&Result);
    if(!FJsonSerializer::Serialize(Root,Writer))
    {OutError=TEXT("Could not serialize the Niagara graph inspection.");return {};}
    return Result;
}

namespace
{
    TSharedRef<FJsonObject> VectorJson(const FVector& V)
    {
        auto O=MakeShared<FJsonObject>();
        O->SetNumberField(TEXT("x"),V.X);O->SetNumberField(TEXT("y"),V.Y);O->SetNumberField(TEXT("z"),V.Z);
        return O;
    }

}

void UMCPerkPreviewEditorLibrary::OpenPerkPreview() { MCPerkActionPreviewPanel::Open(); }
void UMCPerkPreviewEditorLibrary::OpenVFXComposer() {MCVFXActionWorkspace::Open();}
void UMCPerkPreviewEditorLibrary::OpenVFXElements() {MCVFXRecipeComposer::Open();}
FString UMCPerkPreviewEditorLibrary::GetActionWorkspaceSnapshotJson() {return MCVFXActionWorkspace::GetSnapshotJson();}
bool UMCPerkPreviewEditorLibrary::SelectVFXComposerAction(FName ActionID,FString& Error)
{return MCVFXActionWorkspace::SelectActionForReview(ActionID,Error);}
bool UMCPerkPreviewEditorLibrary::PlayVFXComposerAction(const FString& Scenario,bool Loop,FString& Error)
{
    EMCPerkPreviewScenario Mode;
    if(Scenario==TEXT("Target"))Mode=EMCPerkPreviewScenario::Target;
    else if(Scenario==TEXT("NearWall"))Mode=EMCPerkPreviewScenario::NearWall;
    else if(Scenario==TEXT("Timeout"))Mode=EMCPerkPreviewScenario::Timeout;
    else if(Scenario==TEXT("Acquire"))Mode=EMCPerkPreviewScenario::Acquire;
    else {Error=TEXT("Неизвестный сценарий Composer.");return false;}
    return MCVFXActionWorkspace::PlayForReview(Mode,Loop,Error);
}
void UMCPerkPreviewEditorLibrary::StopVFXComposerAction(){MCVFXActionWorkspace::StopForReview();}
bool UMCPerkPreviewEditorLibrary::ResizeVFXComposerForReview(FVector2D Size){return MCVFXActionWorkspace::ResizeForReview(Size);}
bool UMCPerkPreviewEditorLibrary::SetVFXComposerPreviewView(int32 Preset)
{
    return MCVFXActionWorkspace::SetPreviewViewForReview(Preset);
}
bool UMCPerkPreviewEditorLibrary::ImportActionEventRecipe(FName ActionID,FName Stage,UMCVFXComposition* Source)
{
    if(!Source || Source->GetClass()!=UMCVFXRecipePreset::StaticClass() || Source->HasAnyFlags(RF_Transient)
        || !Source->GetOutermost()->GetName().StartsWith(TEXT("/Game/"))
        || !FPackageName::DoesPackageExist(Source->GetOutermost()->GetName()) || Source->GetOutermost()->IsDirty()) {
        UE_LOG(LogTemp,Warning,TEXT("Composer event import requires a clean saved UMCVFXRecipePreset master; no draft was changed."));return false;
    }
    FString Error;
    if(!MCVFXActionWorkspace::ImportEventRecipe(ActionID,Stage,Source,Error)) {
        UE_LOG(LogTemp,Warning,TEXT("Composer event import refused: %s"),*Error);return false;
    }
    return true;
}
bool UMCPerkPreviewEditorLibrary::OpenComposerForRocketStage(FName Stage)
{
    if(Stage!=TEXT("Start")&&Stage!=TEXT("Loop")&&Stage!=TEXT("Impact")&&Stage!=TEXT("End"))return false;
    FString Error;if(!MCPerkActionPreviewPanel::EnsureContext(TEXT("Consumable_Rocket"),Error))return false;
    auto* Profile=MCPerkActionPreviewPanel::GetWorkingProfile();if(!Profile)return false;
    MCVFXRecipeComposer::OpenForSlot(TEXT("Consumable_Rocket"),Stage,Profile);return true;
}
UMCVFXComposition* UMCPerkPreviewEditorLibrary::GetWorkingVFXRecipe() {return MCVFXRecipeComposer::GetWorkingRecipe();}
bool UMCPerkPreviewEditorLibrary::ComposerAddSystem(UNiagaraSystem* System) {return MCVFXRecipeComposer::AddSystem(System);}
bool UMCPerkPreviewEditorLibrary::ComposerLoadRecipe(UMCVFXComposition* Source) {return MCVFXRecipeComposer::LoadComposition(Source);}
UMCVFXComposition* UMCPerkPreviewEditorLibrary::SaveWorkingVFXRecipeAs(const FString& ObjectPath,FString& OutError)
{return MCVFXRecipeComposer::SaveWorkingRecipeAs(ObjectPath,OutError);}
UNiagaraSystem* UMCPerkPreviewEditorLibrary::ExportWorkingVFXRecipe(const FString& PackageName,FString& OutError)
{return MCVFXRecipeComposer::ExportWorkingRecipe(PackageName,OutError);}
bool UMCPerkPreviewEditorLibrary::ComposerApplyToContext(FString& OutError)
{return MCVFXRecipeComposer::ApplyToContext(OutError);}
FString UMCPerkPreviewEditorLibrary::GetVFXComposerSnapshotJson()
{
    auto Data=MakeShared<FJsonObject>();auto* Recipe=MCVFXRecipeComposer::GetWorkingRecipe();
    Data->SetBoolField(TEXT("tool_open"),Recipe!=nullptr);
    if(Recipe)
    {
        FString Error;Data->SetBoolField(TEXT("layout_valid"),Recipe->ValidateLayout(Error));
        Data->SetStringField(TEXT("layout_error"),Error);Data->SetStringField(TEXT("name"),Recipe->PresetName);
        Data->SetNumberField(TEXT("duration"),Recipe->DurationSeconds);Data->SetNumberField(TEXT("tail"),Recipe->TailSeconds);
        TArray<TSharedPtr<FJsonValue>> Nodes;
        for(int32 I=0;I<Recipe->Layers.Num();++I)
        {
            auto N=MakeShared<FJsonObject>();const auto& L=Recipe->Layers[I];
            N->SetStringField(TEXT("name"),L.Name.ToString());N->SetStringField(TEXT("system"),L.System.ToString());
            N->SetBoolField(TEXT("enabled"),L.bEnabled);N->SetBoolField(TEXT("solo"),L.bSolo);
            N->SetNumberField(TEXT("delay"),L.StartSeconds);N->SetNumberField(TEXT("emission"),L.EmissionSeconds);
            if(Recipe->Nodes.IsValidIndex(I))
            {
                const auto& Layout=Recipe->Nodes[I];N->SetStringField(TEXT("id"),Layout.ID.ToString());
                N->SetStringField(TEXT("group"),Layout.Group.ToString());N->SetNumberField(TEXT("x"),Layout.Position.X);N->SetNumberField(TEXT("y"),Layout.Position.Y);
            }
            Nodes.Add(MakeShared<FJsonValueObject>(N));
        }
        Data->SetArrayField(TEXT("nodes"),Nodes);Data->SetNumberField(TEXT("group_count"),Recipe->Groups.Num());
    }
    FString Result;auto Writer=TJsonWriterFactory<>::Create(&Result);FJsonSerializer::Serialize(Data,Writer);return Result;
}
bool UMCPerkPreviewEditorLibrary::RunRocketScenario(FName Scenario)
{return RunActionScenario(TEXT("Consumable_Rocket"),Scenario);}
bool UMCPerkPreviewEditorLibrary::RunActionScenario(FName ActionID,FName Scenario)
{
    EMCPerkPreviewScenario Mode;
    if(Scenario==TEXT("Target")) Mode=EMCPerkPreviewScenario::Target;
    else if(Scenario==TEXT("NearWall")) Mode=EMCPerkPreviewScenario::NearWall;
    else if(Scenario==TEXT("Timeout")) Mode=EMCPerkPreviewScenario::Timeout;
    else if(Scenario==TEXT("Acquire")) Mode=EMCPerkPreviewScenario::Acquire;
    else return false;
    FString Error;
    return MCPerkActionPreviewPanel::EnsureContext(ActionID,Error) && MCPerkActionPreviewPanel::RunScenario(Mode);
}
void UMCPerkPreviewEditorLibrary::ResetPerkPreview() { MCPerkActionPreviewPanel::ResetPreview(); }
void UMCPerkPreviewEditorLibrary::PausePerkPreview(bool bPaused)
{
    if(auto Runner=MCPerkActionPreviewPanel::GetActiveRunner()) Runner->SetPaused(bPaused);
}
UMCPerkVFXProfile* UMCPerkPreviewEditorLibrary::GetWorkingVFXProfile()
{ return MCPerkActionPreviewPanel::GetWorkingProfile(); }

UMCPerkVFXProfile* UMCPerkPreviewEditorLibrary::SaveWorkingProfileAs(const FString& ObjectPath,FString& OutError)
{
    OutError.Reset();auto* Working=GetWorkingVFXProfile();FMCVFXCanonicalProfileState Canonical;
    if(!Working || !MCVFXProfileBindings::ReadCanonical(MCPerkActionPreviewPanel::GetContextPerkID(),Canonical,OutError)
        || !MCVFXProfileBindings::ValidateSavedProfileResources(Working,Canonical.AllowedEvents,OutError))
    {if(OutError.IsEmpty())OutError=TEXT("Контекст представления не открыт.");return nullptr;}
    const FString PackageName=FPackageName::ObjectPathToPackageName(ObjectPath);
    if(!FPackageName::IsValidObjectPath(ObjectPath) || !PackageName.StartsWith(TEXT("/Game/"))
        || FPackageName::DoesPackageExist(PackageName) || FindObject<UPackage>(nullptr,*PackageName))
    {OutError=TEXT("Выберите новое имя ассета в /Game/: существующие пакеты не перезаписываются.");return nullptr;}
    UPackage* Package=CreatePackage(*PackageName);
    auto* Saved=DuplicateObject<UMCPerkVFXProfile>(Working,Package,*FPackageName::ObjectPathToObjectName(ObjectPath));
    Saved->ClearFlags(RF_Transient);Saved->SetFlags(RF_Public|RF_Standalone|RF_Transactional);
    FAssetRegistryModule::AssetCreated(Saved);Package->MarkPackageDirty();
    if(!UEditorLoadingAndSavingUtils::SavePackages({Package},true))
    {OutError=TEXT("Не удалось сохранить новую DA; рабочая копия сохранена в окне.");return nullptr;}
    return Saved;
}

FString UMCPerkPreviewEditorLibrary::AssignSavedProfileToRocket(UMCPerkVFXProfile* Profile)
{return AssignSavedProfileToPerk(TEXT("Consumable_Rocket"),Profile);}
FString UMCPerkPreviewEditorLibrary::AssignSavedProfileToPerk(FName PerkID,UMCPerkVFXProfile* Profile)
{return AssignSavedProfileToSource(PerkID,Profile);}
FString UMCPerkPreviewEditorLibrary::AssignSavedProfileToSource(FName SourceID,UMCPerkVFXProfile* Profile)
{
    const auto* Entry=MCPerkPreviewAdapters::Find(SourceID);
    if(!Entry || !Entry->bSupportsProfileOverride)return TEXT("Для этого действия ещё нет реальной привязки VFX-профиля; игра не изменена.");
    FString Error;FMCVFXCanonicalProfileState Canonical;FMCVFXProfileCommitResult Result;
    if(!MCVFXProfileBindings::ReadCanonical(SourceID,Canonical,Error)
        || !MCVFXProfileBindings::AssignSavedProfile(Profile,Canonical,Result,Error))return Error;
    return Result.Message;
}

FString UMCPerkPreviewEditorLibrary::GetPerkPreviewSnapshotJson()
{
    auto Data=MakeShared<FJsonObject>();
    auto Runner=MCPerkActionPreviewPanel::GetActiveRunner();
    Data->SetBoolField(TEXT("tool_open"),MCVFXActionWorkspace::GetActiveSession().IsValid());
    Data->SetBoolField(TEXT("context_ready"),Runner.IsValid());
    Data->SetBoolField(TEXT("pie"),GEditor && GEditor->PlayWorld);
    UWorld* EditorWorld=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
    Data->SetStringField(TEXT("editor_map"),GetPathNameSafe(EditorWorld));
    if(Runner.IsValid())
    {
        UWorld* World=Runner->GetWorld();auto* Hero=Runner->GetHero();auto* Action=Runner->GetAction();
        Data->SetStringField(TEXT("preview_world"),GetPathNameSafe(World));
        Data->SetStringField(TEXT("preview_action_id"),Runner->GetPerkID().ToString());
        Data->SetStringField(TEXT("context_action_id"),MCPerkActionPreviewPanel::GetContextPerkID().ToString());
        Data->SetBoolField(TEXT("has_perk_definition"),Runner->HasPerkDefinition());
        Data->SetStringField(TEXT("presentation_override"),GetPathNameSafe(Runner->GetPresentationOverride()));
        Data->SetBoolField(TEXT("transient_preview_world"),World && World->GetOutermost()==GetTransientPackage());
        Data->SetStringField(TEXT("world_type"),World && World->WorldType==EWorldType::Game?TEXT("Game"):TEXT("Other"));
        Data->SetStringField(TEXT("game_instance_class"),World && World->GetGameInstance()?World->GetGameInstance()->GetClass()->GetPathName():TEXT("None"));
        Data->SetStringField(TEXT("game_mode_class"),World && World->GetAuthGameMode()?World->GetAuthGameMode()->GetClass()->GetPathName():TEXT("None"));
        const auto* PC=Hero?Cast<APlayerController>(Hero->GetController()):nullptr;
        Data->SetStringField(TEXT("controller_class"),PC?PC->GetClass()->GetPathName():TEXT("None"));
        Data->SetBoolField(TEXT("has_local_player"),PC && PC->GetLocalPlayer()!=nullptr);
        Data->SetBoolField(TEXT("controller_is_local"),PC && PC->IsLocalController());
        Data->SetBoolField(TEXT("base_game_instance"),World && World->GetGameInstance() && World->GetGameInstance()->GetClass()==UGameInstance::StaticClass());
        Data->SetBoolField(TEXT("audio_disabled"),World && !World->AllowAudioPlayback());
        if(auto* GS=World?World->GetGameState<AMCGameState>():nullptr) {
            Data->SetStringField(TEXT("game_state_class"),GS->GetClass()->GetPathName());Data->SetNumberField(TEXT("player_count"),GS->PlayerArray.Num());
            Data->SetBoolField(TEXT("working_phase"),GS->Phase==EMCShiftPhase::Working && !GS->bLobbyWaiting);
        }
        Data->SetBoolField(TEXT("world_is_separate"),World && World!=EditorWorld && (!GEditor || World!=GEditor->PlayWorld));
        Data->SetStringField(TEXT("status"),Runner->GetStatus());
        Data->SetStringField(TEXT("phase"),Runner->GetPhaseName());
        Data->SetNumberField(TEXT("seconds"),Runner->GetElapsed());
        Data->SetBoolField(TEXT("paused"),Runner->IsPaused());
        Data->SetBoolField(TEXT("running"),Runner->IsRunning());
        const auto Clock=Runner->GetClockStats();auto ClockData=MakeShared<FJsonObject>();
        ClockData->SetStringField(TEXT("owner"),TEXT("editor_shared_service"));
        ClockData->SetNumberField(TEXT("tick_calls"),double(Clock.TickCalls));ClockData->SetNumberField(TEXT("skipped_same_frame_calls"),double(Clock.SkippedSameFrameCalls));
        ClockData->SetNumberField(TEXT("world_steps"),double(Clock.WorldSteps));ClockData->SetNumberField(TEXT("input_seconds"),Clock.InputSeconds);
        ClockData->SetNumberField(TEXT("advanced_seconds"),Clock.AdvancedSeconds);ClockData->SetNumberField(TEXT("dropped_catchup_seconds"),Clock.DroppedCatchupSeconds);
        ClockData->SetNumberField(TEXT("last_input_delta"),Clock.LastInputDelta);ClockData->SetNumberField(TEXT("last_advanced_delta"),Clock.LastAdvancedDelta);
        ClockData->SetNumberField(TEXT("engine_frame"),double(GFrameCounter));ClockData->SetNumberField(TEXT("world_seconds"),World?World->GetTimeSeconds():0.);
        Data->SetObjectField(TEXT("clock"),ClockData);
        Data->SetStringField(TEXT("hero"),GetPathNameSafe(Hero));
        Data->SetStringField(TEXT("action"),GetPathNameSafe(Action));
        if(Hero)
        {
            Data->SetObjectField(TEXT("hero_location"),VectorJson(Hero->GetActorLocation()));
            auto* Mesh=Hero->GetMesh();
            Data->SetStringField(TEXT("anim_instance"),Mesh?GetPathNameSafe(Mesh->GetAnimInstance()):TEXT("None"));
            if(Mesh && Action)
                for(int32 I=0;I<2;++I)
                {
                    const bool bLeft=I==0;FTransform Target;
                    auto Hand=MakeShared<FJsonObject>();
                    const bool bTarget=Action->GetPresentationHandTarget(bLeft,Target);
                    const FName Bone=Hero->RigBone(bLeft?TEXT("hand_l"):TEXT("hand_r"));
                    const FTransform Actual=Mesh->GetSocketTransform(Bone);
                    Hand->SetStringField(TEXT("bone"),Bone.ToString());
                    Hand->SetBoolField(TEXT("has_target"),bTarget);
                    Hand->SetObjectField(TEXT("evaluated_position"),VectorJson(Actual.GetLocation()));
                    if(bTarget)
                    {
                        Hand->SetObjectField(TEXT("target_position"),VectorJson(Target.GetLocation()));
                        Hand->SetNumberField(TEXT("distance_cm"),FVector::Distance(Target.GetLocation(),Actual.GetLocation()));
                        Hand->SetNumberField(TEXT("rotation_error_deg"),FMath::RadiansToDegrees(Target.GetRotation().AngularDistance(Actual.GetRotation())));
                    }
                    Data->SetObjectField(bLeft?TEXT("left_hand"):TEXT("right_hand"),Hand);
                }
        }
        if(Action)
        {
            Data->SetBoolField(TEXT("prop_visible"),Action->PropMesh && Action->PropMesh->IsVisible());
            Data->SetBoolField(TEXT("committed"),Action->IsCommitted());
            Data->SetBoolField(TEXT("finished"),Action->IsFinished());
            Data->SetStringField(TEXT("loop_system"),Action->LoopComponent?GetPathNameSafe(Action->LoopComponent->GetAsset()):TEXT("None"));
            Data->SetStringField(TEXT("prepared_impact_system"),GetPathNameSafe(Action->GetPreparedVFXSystem(TEXT("Impact"))));
            TArray<TSharedPtr<FJsonValue>> PreparedStages;
            for(FName Stage:UMCPerkVFXProfile::GetSupportedStages()) {
                auto P=MakeShared<FJsonObject>();P->SetStringField(TEXT("stage"),Stage.ToString());
                P->SetStringField(TEXT("composition"),GetPathNameSafe(Action->GetPreparedVFXComposition(Stage)));
                TArray<UNiagaraSystem*> Systems;Action->GetPreparedVFXSystems(Stage,Systems);
                TArray<TSharedPtr<FJsonValue>> Names;for(auto* System:Systems)Names.Add(MakeShared<FJsonValueString>(GetPathNameSafe(System)));
                P->SetArrayField(TEXT("systems"),Names);PreparedStages.Add(MakeShared<FJsonValueObject>(P));
            }
            Data->SetArrayField(TEXT("prepared_vfx_stages"),PreparedStages);
            if(auto* Rocket=Cast<AMCRocketSkillAction>(Action))
            {
                Data->SetNumberField(TEXT("detonations"),Rocket->DetonationCount);
                Data->SetBoolField(TEXT("launcher_held"),Rocket->IsLauncherHeld());
                Data->SetObjectField(TEXT("muzzle"),VectorJson(Rocket->GetLauncherMuzzlePoint()));
                Data->SetObjectField(TEXT("muzzle_forward"),VectorJson(Rocket->GetLauncherForward()));
            }
        }
        TArray<TSharedPtr<FJsonValue>> Events;
        for(const auto& Event:Runner->GetEvents())
        {
            auto E=MakeShared<FJsonObject>();E->SetStringField(TEXT("stage"),Event.Stage.ToString());E->SetNumberField(TEXT("seconds"),Event.Seconds);
            Events.Add(MakeShared<FJsonValueObject>(E));
        }
        Data->SetArrayField(TEXT("events"),Events);
        const FBox Framing=Runner->GetFramingBounds();
        Data->SetObjectField(TEXT("framing_min"),VectorJson(Framing.Min));
        Data->SetObjectField(TEXT("framing_max"),VectorJson(Framing.Max));
        TArray<TSharedPtr<FJsonValue>> PresentationOwners;
        if(World)for(TActorIterator<AActor> It(World);It;++It) {
            TInlineComponentArray<UMCVFXPresentationComponent*> Owners(*It);
            for(auto* Owner:Owners)if(IsValid(Owner)) {
                auto P=MakeShared<FJsonObject>();P->SetStringField(TEXT("component"),Owner->GetPathName());P->SetStringField(TEXT("source"),Owner->SourceID.ToString());
                P->SetStringField(TEXT("library"),Owner->Library.ToString());P->SetBoolField(TEXT("prepared"),Owner->IsPrepared());
                TArray<TSharedPtr<FJsonValue>> PreparedSources;
                for(FName Source:Owner->GetPreparedSourceIDs())PreparedSources.Add(MakeShared<FJsonValueString>(Source.ToString()));
                P->SetArrayField(TEXT("prepared_sources"),PreparedSources);
                P->SetNumberField(TEXT("pending_receipts"),Owner->GetPendingReceiptCount());
                P->SetBoolField(TEXT("running"),Owner->IsPresentationRunning());P->SetStringField(TEXT("error"),Owner->GetLastError());
                TArray<UNiagaraSystem*> Systems;Owner->GetPreparedResources(Systems);TArray<TSharedPtr<FJsonValue>> Names;
                for(auto* System:Systems)Names.Add(MakeShared<FJsonValueString>(GetPathNameSafe(System)));
                P->SetArrayField(TEXT("systems"),Names);PresentationOwners.Add(MakeShared<FJsonValueObject>(P));
            }
        }
        Data->SetArrayField(TEXT("presentation_owners"),PresentationOwners);
        TArray<TSharedPtr<FJsonValue>> Compositions;int32 RunningCompositions=0,LiveLayers=0,PendingLayers=0;
        if(World)for(TActorIterator<AMCPerkVFXCompositionPlayer> It(World);It;++It)if(IsValid(*It)) {
            auto C=MakeShared<FJsonObject>();C->SetStringField(TEXT("player"),It->GetPathName());
            C->SetBoolField(TEXT("running"),It->IsRunning());C->SetBoolField(TEXT("stopping"),It->IsStopping());
            C->SetNumberField(TEXT("playback_seconds"),It->GetPlaybackTime());
            C->SetNumberField(TEXT("live_layers"),It->GetLiveLayerCount());C->SetNumberField(TEXT("pending_layers"),It->GetPendingLayerCount());
            Compositions.Add(MakeShared<FJsonValueObject>(C));RunningCompositions+=It->IsRunning()?1:0;
            LiveLayers+=It->GetLiveLayerCount();PendingLayers+=It->GetPendingLayerCount();
        }
        Data->SetArrayField(TEXT("composition_players"),Compositions);Data->SetNumberField(TEXT("running_composition_players"),RunningCompositions);
        Data->SetNumberField(TEXT("composition_live_layers"),LiveLayers);Data->SetNumberField(TEXT("composition_pending_layers"),PendingLayers);
        TArray<TSharedPtr<FJsonValue>> Niagara;
        for(TObjectIterator<UNiagaraComponent> It;It;++It)
        {
            auto* C=*It;if(!IsValid(C)||C->IsTemplate()||C->GetWorld()!=World||!C->GetAsset())continue;
            auto N=MakeShared<FJsonObject>();N->SetStringField(TEXT("component"),C->GetPathName());
            N->SetStringField(TEXT("system"),GetPathNameSafe(C->GetAsset()));
            N->SetBoolField(TEXT("active"),C->IsActive());N->SetBoolField(TEXT("visible"),C->IsVisible());
            N->SetBoolField(TEXT("registered"),C->IsRegistered());
            N->SetObjectField(TEXT("location"),VectorJson(C->GetComponentLocation()));
            N->SetObjectField(TEXT("scale"),VectorJson(C->GetComponentScale()));
            // Explicit editor diagnostics only. Synchronize before reading CPU
            // emitter instances; never inspect their concurrent buffers unsafely.
            if(auto Controller=C->GetSystemInstanceController())
            {
                Controller->WaitForConcurrentTickAndFinalize();
                if(auto* Instance=Controller->GetSystemInstance_Unsafe())
                {
                    TArray<TSharedPtr<FJsonValue>> Counts;int32 Total=0;
                    for(const auto& Emitter:Instance->GetEmitters())
                    {const int32 Count=Emitter->GetNumParticles();Total+=Count;Counts.Add(MakeShared<FJsonValueNumber>(Count));}
                    N->SetArrayField(TEXT("cpu_particle_counts"),Counts);N->SetNumberField(TEXT("cpu_particles"),Total);
                }
            }
            for(const FName Name:{FName(TEXT("User.Radius")),FName(TEXT("User.Duration")),FName(TEXT("User.Intensity"))})
            {bool Valid=false;const float Value=C->GetVariableFloat(Name,Valid);if(Valid)N->SetNumberField(Name.ToString(),Value);}
            Niagara.Add(MakeShared<FJsonValueObject>(N));
        }
        Data->SetArrayField(TEXT("niagara_components"),Niagara);
        int32 ActorCount=0,ActionCount=0;
        if(World) for(TActorIterator<AActor> It(World);It;++It) { ++ActorCount;if(It->IsA<AMCActiveSkillAction>()) ++ActionCount; }
        Data->SetNumberField(TEXT("actor_count"),ActorCount);Data->SetNumberField(TEXT("action_count"),ActionCount);
    }
    FString Result;auto Writer=TJsonWriterFactory<>::Create(&Result);FJsonSerializer::Serialize(Data,Writer);return Result;
}
