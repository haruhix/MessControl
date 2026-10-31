#include "MCVFXComposerBuilderLibrary.h"

#include "MCVFXComposition.h"
#include "MCVFXRecipePreset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "Misc/PackageName.h"
#include "NiagaraDataInterface.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraExternalSystemEditorUtilities.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraParameterStore.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "ScopedTransaction.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Internationalization/Regex.h"

namespace MCVFXComposerBuilderPrivate
{
struct FLayerSource
{
    int32 Index=0;
    FGuid ID;
    UNiagaraSystem* System=nullptr;
    const FMCVFXCompositionLayer* Layer=nullptr;
};
struct FSharedValue
{
    FNiagaraVariable Variable;
    TArray<uint8> Bytes;
    FString Owner;
};

/** Niagara's public editor helpers transact even when editing temporary copies. */
class FScopedGeneratedNiagaraTransaction final
{
public:
    FScopedGeneratedNiagaraTransaction()
        : Transaction(NSLOCTEXT("MCVFXComposerBuilder","TemporaryBuild","Build temporary VFX composition")) {}
    ~FScopedGeneratedNiagaraTransaction()
    {
        // This scope starts only with no active transaction, so its index is zero.
        // Cancel drops this build's records (without undoing the built objects)
        // and restores the previous Undo/Redo history. Never cancel a nested
        // transaction: UE 5.8 would cancel the user's entire active transaction.
        Transaction.Cancel();
    }
private:
    FScopedTransaction Transaction;
};

/** Compare authored system lifecycle, never generated compile IDs or emitter-instance names. */
bool SystemLifecycleSignature(const FLayerSource& Source,FString& Signature,FString& Error)
{
    Signature.Reset();
    FNiagaraExternalEditContext Context(Source.System);
    for(const FName Stage:{FName(TEXT("SystemSpawnScript")),FName(TEXT("SystemUpdateScript"))}) {
        FNiagaraExt_StackItemReference Ref(Source.System,NAME_None,Stage);
        FNiagaraExt_ScriptStackTopology Topology;TArray<FNiagaraExt_ModuleInputValues> Values;
        UNiagaraExternalEditUtilities::GetScriptStackTopology(Ref,Topology,Context);
        UNiagaraExternalEditUtilities::GetScriptStackInputValues(Ref,Values,Context);
        if(Context.HasErrors()) {Error=TEXT("Cannot inspect source system lifecycle: ")+Context.Errors[0].ToString();return false;}
        if((Stage==TEXT("SystemSpawnScript") && !Topology.Modules.IsEmpty())
            || (Stage==TEXT("SystemUpdateScript") && (Topology.Modules.Num()!=1 || !Topology.Modules[0].Enabled
                || !Topology.Modules[0].ModuleScript || Topology.Modules[0].ModuleScript->GetPathName()!=TEXT("/Niagara/Modules/System/SystemState.SystemState")))) {
            Error=TEXT("Custom SystemSpawn/Update graph is outside the supported export contract; only empty SystemSpawn and one standard enabled SystemState are accepted.");return false;
        }
        Signature+=Stage.ToString()+TEXT("{");
        for(int32 ModuleIndex=0;ModuleIndex<Topology.Modules.Num();++ModuleIndex) {
            const auto& Module=Topology.Modules[ModuleIndex];
            const auto* ModuleValues=Values.FindByPredicate([&](const auto& V){return V.ModuleName==Module.ModuleName;});
            if(!ModuleValues) {Error=TEXT("SystemState inputs could not be resolved.");return false;}
            Signature+=Module.ModuleScript->GetPathName()+TEXT("[");
            const UNiagaraScript* NativeScript=Stage==TEXT("SystemSpawnScript")?Source.System->GetSystemSpawnScript():Source.System->GetSystemUpdateScript();
            const auto* ScriptSource=NativeScript?Cast<UNiagaraScriptSource>(NativeScript->GetLatestSource()):nullptr;
            if(!ScriptSource || !ScriptSource->NodeGraph) {Error=TEXT("Source system graph is unavailable.");return false;}
            bool FoundVersion=false;
            for(const UEdGraphNode* Node:ScriptSource->NodeGraph->Nodes) if(const auto* Function=Cast<UNiagaraNodeFunctionCall>(Node))
                if(Function->GetFunctionName()==Module.ModuleName.ToString()) {
                    Signature+=TEXT("ScriptVersion:")+Function->SelectedScriptVersion.ToString(EGuidFormats::Digits)+TEXT(";");FoundVersion=true;break;
                }
            if(!FoundVersion) {Error=TEXT("Cannot identify the SystemState module version in its source graph.");return false;}
            for(const auto& Input:Module.Inputs) {
                const auto* Entry=ModuleValues->Inputs.FindByPredicate([&](const auto& V){return V.Name==Input.Name;});
                if(!Entry || !Entry->Value.IsValid()) {Error=TEXT("Unresolved SystemState input: ")+Input.Name.ToString();return false;}
                const UScriptStruct* Type=Entry->Value.GetScriptStruct();
                const FString TypeName=Type->GetName();
                if(Input.bIsVisible && (TypeName.EndsWith(TEXT("_Unsupported")) || TypeName.EndsWith(TEXT("_DynamicInput"))
                    || TypeName.EndsWith(TEXT("_DataInterface")))) {
                    Error=TEXT("SystemState input ")+Input.Name.ToString()+TEXT(" uses an unsupported value/DI/dynamic graph; export refused rather than replacing it with defaults.");return false;
                }
                if(const auto* Linked=Entry->Value.GetPtr<FNiagaraExt_StackInputData_Linked>()) {
                    const FName Name=Linked->LinkedVariable.Name;
                    if(Name!=TEXT("User.Direction") && Name!=TEXT("User.Radius") && Name!=TEXT("User.Duration")
                        && Name!=TEXT("User.Intensity") && Name!=TEXT("User.Fill") && Name!=TEXT("User.Color")) {
                        Error=TEXT("SystemState linked input ")+Name.ToString()+TEXT(" is outside the explicit shared gameplay contract.");return false;
                    }
                }
                FString Text;Type->ExportText(Text,Entry->Value.GetMemory(),nullptr,nullptr,PPF_None,nullptr);
                Signature+=Input.Name.ToString()+FString::Printf(TEXT(":%d:%d:"),Input.bIsVisible,Input.bIsStaticSwitch)+Type->GetPathName()+TEXT("=")+Text+TEXT(";");
            }
            Signature+=TEXT("]");
        }
        const UNiagaraScript* Script=Stage==TEXT("SystemSpawnScript")?Source.System->GetSystemSpawnScript():Source.System->GetSystemUpdateScript();
        if(!Script) {Error=TEXT("Source has no native system script.");return false;}
        TArray<FNiagaraVariable> Constants;Script->RapidIterationParameters.GetParameters(Constants);
        Constants.Sort([](const auto& A,const auto& B){return A.GetName().LexicalLess(B.GetName());});
        TArray<FString> Rows;
        for(const auto& Constant:Constants) {
            FString Name=Constant.GetName().ToString();bool SystemModuleConstant=false;
            // System scripts also cache dependent emitter rapid-iteration values.
            // Niagara deliberately retains removed parameters when applying a
            // compilation, so a saved source can still contain a deleted emitter's
            // Constants.<Emitter>.* rows. Current emitter handles cannot identify
            // those orphan rows. Scope comparison to the real system modules
            // established above (empty Spawn / one standard SystemState Update),
            // preserving every value owned by that lifecycle module.
            for(int32 I=0;I<Topology.Modules.Num();++I) {
                const FString Prefix=TEXT("Constants.")+Topology.Modules[I].ModuleName.ToString()+TEXT(".");
                if(Name.StartsWith(Prefix)) {
                    Name=FString::Printf(TEXT("Constants.Module%d."),I)+Name.RightChop(Prefix.Len());
                    SystemModuleConstant=true;break;
                }
            }
            if(!SystemModuleConstant)continue;
            const uint8* Data=Script->RapidIterationParameters.GetParameterData(Constant);
            if(!Data) {Error=TEXT("Source system rapid-iteration value is unavailable: ")+Name;return false;}
            FString Bytes;for(int32 I=0;I<Constant.GetSizeInBytes();++I)Bytes+=FString::Printf(TEXT("%02X"),Data[I]);
            Rows.Add(Name+TEXT(":")+Constant.GetType().GetName()+TEXT("=")+Bytes);
        }
        Rows.Sort();for(const FString& Row:Rows)Signature+=TEXT("RI:")+Row+TEXT(";");
        Signature+=TEXT("}");
    }
    Signature+=FString::Printf(TEXT("Warmup:%.9g:%d:%.9g;Tick:%d:%.9g;Det:%d:%d;Effect:%s"),
        double(Source.System->GetWarmupTime()),Source.System->GetWarmupTickCount(),double(Source.System->GetWarmupTickDelta()),
        Source.System->HasFixedTickDelta(),double(Source.System->GetFixedTickDeltaTime()),Source.System->NeedsDeterminism(),
        Source.System->GetRandomSeed(),*GetPathNameSafe(Source.System->GetEffectType()));
    return true;
}

bool IsShared(FName Name)
{
    return Name==TEXT("User.Direction") || Name==TEXT("User.Radius") || Name==TEXT("User.Duration")
        || Name==TEXT("User.Intensity") || Name==TEXT("User.Fill") || Name==TEXT("User.Color");
}
bool IsBasicType(const FNiagaraTypeDefinition& Type)
{
    return Type==FNiagaraTypeDefinition::GetFloatDef() || Type==FNiagaraTypeDefinition::GetColorDef()
        || Type==FNiagaraTypeDefinition::GetVec3Def() || Type==FNiagaraTypeDefinition::GetBoolDef();
}
bool IsSharedType(const FNiagaraVariable& Variable)
{
    if(Variable.GetName()==TEXT("User.Direction")) return Variable.GetType()==FNiagaraTypeDefinition::GetVec3Def();
    if(Variable.GetName()==TEXT("User.Color")) return Variable.GetType()==FNiagaraTypeDefinition::GetColorDef();
    return Variable.GetType()==FNiagaraTypeDefinition::GetFloatDef();
}
FString LayerName(const FLayerSource& Source)
{return FString::Printf(TEXT("Layer %d (%s)"),Source.Index+1,*Source.Layer->Name.ToString());}

/** This is an explicit first contract, not a claim that arbitrary NS are interchangeable. */
bool IsCuratedPath(const FString& Path)
{
    return Path.StartsWith(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_"))
        || Path.StartsWith(TEXT("/Game/Tests/VFXPractice/Effects/NS_"))
        || Path.StartsWith(TEXT("/Game/Development/HealStyleOptions/NS_"))
        || Path.StartsWith(TEXT("/Game/Development/VFXRecovery01/NS_"));
}
bool IsOwnExportWithMaster(UNiagaraSystem* System)
{
    if(!System || System->GetOutermost()==GetTransientPackage())return false;
    FMetaData& Meta=System->GetOutermost()->GetMetaData();
    if(Meta.GetValue(System,TEXT("MCComposerBuilderVersion"))!=TEXT("1"))return false;
    const FString Path=Meta.GetValue(System,TEXT("MCComposerSourceRecipe"));
    if(!Path.StartsWith(TEXT("/Game/")) || !FSoftObjectPath(Path).IsValid())return false;
    return Cast<UMCVFXRecipePreset>(LoadObject<UMCVFXComposition>(nullptr,*Path))!=nullptr;
}
FString Namespaced(FGuid ID,FName Name)
{return TEXT("User.L_")+ID.ToString(EGuidFormats::Digits)+TEXT(".")+Name.ToString().RightChop(5);}

bool CheckDependency(const FString& Name,const FLayerSource& Source,const TSet<FName>& UserNames,FString& Error)
{
    if(Name.StartsWith(TEXT("NPC.")) || Name.StartsWith(TEXT("DataInstance."))) {
        Error=LayerName(Source)+TEXT(": external dependency ")+Name+TEXT(" needs a curated adapter; export refused.");return false;
    }
    if(Name.StartsWith(TEXT("System."))) {
        // Engine lifecycle variables can be supplied by the new SystemState module. Artist/system data cannot.
        static const TSet<FString> Lifecycle={TEXT("System.ExecutionState"),TEXT("System.ExecutionStateSource"),TEXT("System.Age"),
            TEXT("System.LoopedAge"),TEXT("System.LoopCount"),TEXT("System.LoopDuration"),TEXT("System.LoopDelay")};
        if(!Lifecycle.Contains(Name)) {Error=LayerName(Source)+TEXT(": unsupported system-scoped dependency ")+Name;return false;}
    }
    bool KnownUser=false;
    for(const FName User:UserNames) if(Name==User.ToString() || Name.StartsWith(User.ToString()+TEXT("."))) {KnownUser=true;break;}
    if(Name.StartsWith(TEXT("User.")) && !KnownUser) {
        Error=LayerName(Source)+TEXT(": User dependency ")+Name+TEXT(" has no typed default in its source System.");return false;
    }
    return true;
}

bool InspectSource(const FLayerSource& Source,FString& Error)
{
    TArray<FNiagaraVariable> Variables;Source.System->GetExposedParameters().GetParameters(Variables);
    TSet<FName> UserNames;
    for(const auto& Variable:Variables) {
        if(!Variable.GetName().ToString().StartsWith(TEXT("User.")) || !IsBasicType(Variable.GetType())) {
            Error=LayerName(Source)+TEXT(": unsupported exposed parameter ")+Variable.GetName().ToString()+TEXT("; only float/color/vec3/bool are curated.");return false;
        }
        if(IsShared(Variable.GetName()) && !IsSharedType(Variable)) {
            Error=LayerName(Source)+TEXT(": wrong type for shared gameplay input ")+Variable.GetName().ToString();return false;
        }
        UserNames.Add(Variable.GetName());
    }
    int32 Enabled=0;
    for(const auto& Handle:Source.System->GetEmitterHandles()) {
        if(!Handle.GetIsEnabled()) continue;
        ++Enabled;
        const auto* Data=Handle.GetEmitterData();
        if(!Data || !Handle.GetInstance().Emitter || !Data->EventHandlerScriptProps.IsEmpty() || !Data->GetSimulationStages().IsEmpty()) {
            Error=LayerName(Source)+TEXT(": event handlers, simulation stages and stateless emitters need an explicit adapter.");return false;
        }
        const bool HasFixed=Source.System->bFixedBounds || Data->CalculateBoundsMode==ENiagaraEmitterCalculateBoundMode::Fixed;
        if(Data->SimTarget==ENiagaraSimTarget::GPUComputeSim && !HasFixed) {
            Error=LayerName(Source)+TEXT(": GPU emitter ")+Handle.GetName().ToString()+TEXT(" has no supported fixed bounds; export refused.");return false;
        }
        if(HasFixed) {
            const FBox Box=Source.System->bFixedBounds?Source.System->GetFixedBounds():Data->FixedBounds;
            if(!Box.IsValid || Box.Min.ContainsNaN() || Box.Max.ContainsNaN() || Box.GetExtent().GetMin()<=0) {
                Error=LayerName(Source)+TEXT(": invalid fixed bounds for ")+Handle.GetName().ToString();return false;
            }
        }
        for(const auto* Renderer:Data->GetRenderers()) if(Renderer && Renderer->GetClass()->GetFName()==TEXT("NiagaraComponentRendererProperties")) {
            Error=LayerName(Source)+TEXT(": component/actor renderers need a dedicated lifetime adapter.");return false;
        }
    }
    if(Enabled==0) {Error=LayerName(Source)+TEXT(": source has no enabled standard emitters.");return false;}

    TArray<UObject*> Objects;GetObjectsWithOuter(Source.System,Objects,EGetObjectsFlags::IncludeNestedObjects);
    const FRegexPattern ScopedName(TEXT("(?<![A-Za-z0-9_.])(?:User|System|NPC|DataInstance)\\.[A-Za-z_][A-Za-z0-9_.]*"));
    for(UObject* Object:Objects) {
        if(const auto* DI=Cast<UNiagaraDataInterface>(Object)) {
            const FName Class=DI->GetClass()->GetFName();
            if(Class!=TEXT("NiagaraDataInterfaceCurve") && Class!=TEXT("NiagaraDataInterfaceVectorCurve")
                && Class!=TEXT("NiagaraDataInterfaceColorCurve")) {
                Error=LayerName(Source)+TEXT(": data interface ")+DI->GetClass()->GetName()+TEXT(" is outside the self-contained curve-only contract.");return false;
            }
        }
        if(const auto* Function=Cast<UNiagaraNodeFunctionCall>(Object)) {
            auto* Script=Function->FunctionScript.Get();
            if(Script && !Script->IsIn(Source.System) && !Script->GetPathName().StartsWith(TEXT("/Niagara/"))) {
                Error=LayerName(Source)+TEXT(": external script ")+Script->GetPathName()+TEXT(" has not been curated.");return false;
            }
        }
        if(const auto* Node=Cast<UEdGraphNode>(Object))
            for(const UEdGraphPin* Pin:Node->Pins) if(Pin && !CheckDependency(Pin->PinName.ToString(),Source,UserNames,Error)) return false;
        // MinimalAPI does not export CustomHlsl getters. Its declared reflected property is read-only here.
        if(const auto* Property=FindFProperty<FStrProperty>(Object->GetClass(),TEXT("CustomHlsl"))) {
            const FString Hlsl=Property->GetPropertyValue_InContainer(Object);
            FRegexMatcher Matcher(ScopedName,Hlsl);
            while(Matcher.FindNext()) {
                const FString Name=Matcher.GetCaptureGroup(0);
                if(!CheckDependency(Name,Source,UserNames,Error)) return false;
                FName UserToken(*Name);
                for(const FName User:UserNames) if(Name==User.ToString() || Name.StartsWith(User.ToString()+TEXT("."))) {UserToken=User;break;}
                if(Name.StartsWith(TEXT("User.")) && !IsShared(UserToken)) {
                    Error=LayerName(Source)+TEXT(": private User input ")+Name+TEXT(" appears in custom HLSL; a tested namespace adapter is required.");return false;
                }
            }
        }
    }
    return true;
}

FNiagaraVariable EffectiveVariable(const FNiagaraVariable& Input,const FLayerSource& Source)
{
    FNiagaraVariable Result=Input;
    Result.SetData(Source.System->GetExposedParameters().GetParameterData(Input));
    const auto& Layer=*Source.Layer;
    if(Input.GetType()==FNiagaraTypeDefinition::GetFloatDef()) {
        if(const auto* P=Layer.Floats.FindByPredicate([&](const auto& V){return V.bOverride && V.Name==Input.GetName();})) Result.SetValue(P->Value);
    }
    else if(Input.GetType()==FNiagaraTypeDefinition::GetColorDef()) {
        if(const auto* P=Layer.Colors.FindByPredicate([&](const auto& V){return V.bOverride && V.Name==Input.GetName();})) Result.SetValue(P->Value);
    }
    else if(Input.GetType()==FNiagaraTypeDefinition::GetVec3Def()) {
        if(const auto* P=Layer.Vectors.FindByPredicate([&](const auto& V){return V.bOverride && V.Name==Input.GetName();})) Result.SetValue(FVector3f(P->Value));
    }
    else if(Input.GetType()==FNiagaraTypeDefinition::GetBoolDef()) {
        if(const auto* P=Layer.Bools.FindByPredicate([&](const auto& V){return V.bOverride && V.Name==Input.GetName();})) Result.SetValue(FNiagaraBool(P->Value));
    }
    return Result;
}

FString LifecycleDifference(const FString& A,const FString& B)
{
    int32 Offset=0;while(Offset<FMath::Min(A.Len(),B.Len()) && A[Offset]==B[Offset])++Offset;
    const auto FieldAt=[Offset](const FString& Value) {
        const int32 Separator=Value.Left(Offset).Find(TEXT(";"),ESearchCase::CaseSensitive,ESearchDir::FromEnd);
        const int32 Start=FMath::Max(Separator+1,Offset-100);
        const int32 End=Value.Find(TEXT(";"),ESearchCase::CaseSensitive,ESearchDir::FromStart,Offset);
        return Value.Mid(Start,FMath::Min((End==INDEX_NONE?Value.Len():End)-Start,240));
    };
    return TEXT(" First differing field: [")+FieldAt(A)+TEXT("] versus [")+FieldAt(B)+TEXT("].");
}

bool Gather(const UMCVFXComposition* Recipe,TArray<FLayerSource>& Sources,TMap<FName,FSharedValue>& Shared,FString& Error)
{
    if(!Recipe || !Recipe->Validate(Error)) {if(!Recipe)Error=TEXT("Choose a source recipe.");return false;}
    const auto* Preset=Cast<UMCVFXRecipePreset>(Recipe);
    if(Preset && !Preset->ValidateLayout(Error)) return false;
    TOptional<EMCVFXCompositionBinding> CommonBinding;
    FString CommonLifecycle;FString LifecycleOwner;
    for(int32 Index=0;Index<Recipe->Layers.Num();++Index) {
        const auto& Layer=Recipe->Layers[Index];if(!Layer.bEnabled)continue;
        FLayerSource Source;Source.Index=Index;Source.Layer=&Layer;
        Source.ID=Preset?Preset->Nodes[Index].ID:FGuid(0,0,0,uint32(Index+1));
        if(Layer.Binding==EMCVFXCompositionBinding::Subject || !Layer.Socket.IsNone()
            || (CommonBinding.IsSet() && CommonBinding.GetValue()!=Layer.Binding)) {
            Error=LayerName(Source)+TEXT(": one NS needs a common anchor; mixed bindings/socket layers are not supported.");return false;
        }
        CommonBinding=Layer.Binding;
        if(!Layer.Transform.Equals(FTransform::Identity)) {
            Error=LayerName(Source)+TEXT(": per-layer transform needs a curated emitter-space adapter; export refused.");return false;
        }
        if(Layer.EmissionSeconds>0) {
            Error=LayerName(Source)+TEXT(": per-layer emission cutoffs are not yet baked into emitter lifecycle; export refused.");return false;
        }
        Source.System=Layer.System.LoadSynchronous();
        if(!Source.System) {Error=LayerName(Source)+TEXT(": source System is unavailable.");return false;}
        if(!IsCuratedPath(Layer.System.ToSoftObjectPath().ToString()) && !IsOwnExportWithMaster(Source.System)) {
            Error=LayerName(Source)+TEXT(": source is neither a curated library candidate nor a versioned Composer export with a loadable source master.");return false;
        }
        if(!UMCVFXComposition::ValidateParameters(Layer,Source.System,Error) || !InspectSource(Source,Error))return false;
        FString Lifecycle;
        if(!SystemLifecycleSignature(Source,Lifecycle,Error)) {Error=LayerName(Source)+TEXT(": ")+Error;return false;}
        if(!Sources.IsEmpty() && CommonLifecycle!=Lifecycle) {
            Error=TEXT("System lifecycle differs between ")+LifecycleOwner+TEXT(" and ")+LayerName(Source)
                +TEXT(" (SystemSpawn/Update inputs/rapid-iteration or warmup/tick/effect settings).")+LifecycleDifference(CommonLifecycle,Lifecycle)
                +TEXT(" Match their native SystemState settings or export separately; no defaults will be substituted.");return false;
        }
        if(Sources.IsEmpty()) {CommonLifecycle=Lifecycle;LifecycleOwner=LayerName(Source);}
        TArray<FNiagaraVariable> Variables;Source.System->GetExposedParameters().GetParameters(Variables);
        for(const auto& Variable:Variables) {
            if(!IsShared(Variable.GetName()))continue;
            const FNiagaraVariable Effective=EffectiveVariable(Variable,Source);
            if(const auto* Previous=Shared.Find(Variable.GetName())) {
                if(Previous->Variable.GetType()!=Effective.GetType() || Previous->Bytes.Num()!=Effective.GetSizeInBytes()
                    || FMemory::Memcmp(Previous->Bytes.GetData(),Effective.GetData(),Effective.GetSizeInBytes())!=0) {
                    Error=FString::Printf(TEXT("Shared gameplay input %s conflicts between %s and %s. Match their defaults/overrides, or use a curated private binding; values will not be silently merged."),
                        *Variable.GetName().ToString(),*Previous->Owner,*LayerName(Source));return false;
                }
            }
            else {
                FSharedValue Value;Value.Variable=Effective;Value.Owner=LayerName(Source);
                Value.Bytes.Append(Effective.GetData(),Effective.GetSizeInBytes());Shared.Add(Variable.GetName(),MoveTemp(Value));
            }
        }
        Sources.Add(Source);
    }
    if(Sources.IsEmpty()) {Error=TEXT("Enable at least one source layer before building a System.");return false;}
    return true;
}

bool DelayBurst(UNiagaraSystem* System,FName EmitterName,float Delay,FString& Error)
{
    if(Delay<=0)return true;
    FNiagaraExternalEditContext Context(System);FNiagaraExt_StackItemReference Ref(System,EmitterName);
    FNiagaraExt_EmitterTopology Topology;UNiagaraExternalEditUtilities::GetEmitterTopology(Ref,Topology,Context);
    bool Found=false;
    for(const auto& Module:Topology.EmitterUpdateScript.Modules) {
        if(!Module.Enabled || !Module.ModuleScript)continue;
        if(Module.ModuleScript->GetPathName().Contains(TEXT("/SpawnRate."))) {
            Error=TEXT("Spawn delay for continuous SpawnRate emitters needs a curated lifecycle adapter.");return false;
        }
        if(!Module.ModuleScript->GetPathName().Contains(TEXT("/SpawnBurst_Instantaneous.")))continue;
        FNiagaraExt_StackItemReference Input(System,EmitterName,Topology.EmitterUpdateScript.ScriptName,Module.ModuleName);
        Input.InputNameStack.Add(TEXT("Spawn Time"));FNiagaraExt_StackInputValue Existing;
        UNiagaraExternalEditUtilities::GetStackInputData(Input,Existing,Context);
        const auto* Value=Existing.GetPtr<FNiagaraFloat>();
        if(!Value) {Error=TEXT("Burst Spawn Time is dynamic/linked; cannot safely add a delay.");return false;}
        FNiagaraExt_StackInputValue NewValue;NewValue.InitializeAs<FNiagaraFloat>();NewValue.GetMutable<FNiagaraFloat>().Value=Value->Value+Delay;
        UNiagaraExternalEditUtilities::SetStackInputData(Input,NewValue,Context);Found=true;
    }
    if(Context.HasErrors() || !Found) {
        Error=Context.HasErrors()?Context.Errors[0].ToString():TEXT("No supported instantaneous burst was found for delay.");return false;
    }
    return true;
}
}

bool UMCVFXComposerBuilderLibrary::ValidateRecipe(const UMCVFXComposition* Recipe,FString& OutError)
{
    OutError.Reset();TArray<MCVFXComposerBuilderPrivate::FLayerSource> Sources;TMap<FName,MCVFXComposerBuilderPrivate::FSharedValue> Shared;
    return MCVFXComposerBuilderPrivate::Gather(Recipe,Sources,Shared,OutError);
}

UNiagaraSystem* UMCVFXComposerBuilderLibrary::BuildTransient(const UMCVFXComposition* Recipe,FString& OutError)
{
    using namespace MCVFXComposerBuilderPrivate;
    OutError.Reset();
    if(GIsTransacting || (GEditor && GEditor->IsTransactionActive())) {
        OutError=TEXT("Finish the current editor edit or Undo/Redo operation before building the VFX preview; its transaction has been preserved.");return nullptr;
    }
    TArray<FLayerSource> Sources;TMap<FName,FSharedValue> Shared;
    if(!Gather(Recipe,Sources,Shared,OutError))return nullptr;
    for(const auto& Source:Sources) {
        if(!Source.System->IsReadyToRun())Source.System->WaitForCompilationComplete(true,false);
        if(!Source.System->IsReadyToRun()) {OutError=LayerName(Source)+TEXT(": source is not compiled/runnable; no source is duplicated.");return nullptr;}
    }
    if(GIsTransacting || (GEditor && GEditor->IsTransactionActive())) {
        OutError=TEXT("Finish the current editor edit or Undo/Redo operation before building the VFX preview; its transaction has been preserved.");return nullptr;
    }
    FScopedGeneratedNiagaraTransaction GeneratedTransaction;
    // The common lifecycle is copied, not recreated by InitializeSystem defaults.
    auto* Output=DuplicateObject<UNiagaraSystem>(Sources[0].System,GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(),UNiagaraSystem::StaticClass(),TEXT("ComposerGenerated")));
    Output->ClearFlags(RF_Public|RF_Standalone);Output->SetFlags(RF_Transient|RF_Transactional);
    TSet<FGuid> Remove;for(const auto& Handle:Output->GetEmitterHandles())Remove.Add(Handle.GetId());
    // The editor utility is not DLL-exported in UE 5.8. This owned transient
    // system has no live instances; the exported runtime removal is sufficient.
    // AddEmitterToSystem below rebuilds/synchronizes the editor graph before
    // the generated system can be compiled or returned.
    Output->RemoveEmitterHandlesById(Remove);
    // Source-system boxes are reapplied per copied emitter below, avoiding a global box clipping other layers.
    Output->bFixedBounds=false;
    TArray<FNiagaraVariable> OldVariables;Output->GetExposedParameters().GetParameters(OldVariables);
    for(const auto& Variable:OldVariables)Output->GetExposedParameters().RemoveParameter(Variable);
    for(const auto& Pair:Shared) Output->GetExposedParameters().SetParameterData(Pair.Value.Bytes.GetData(),Pair.Value.Variable,true);
    for(const auto& Source:Sources) {
        TStrongObjectPtr<UNiagaraSystem> Copy(DuplicateObject<UNiagaraSystem>(Source.System,GetTransientPackage(),
            MakeUniqueObjectName(GetTransientPackage(),UNiagaraSystem::StaticClass(),TEXT("ComposerSourceCopy"))));
        Copy->ClearFlags(RF_Public|RF_Standalone);Copy->SetFlags(RF_Transient);
        // UE 5.8's user-parameter helper otherwise creates a data-only view model
        // without a message GUID. Its stack entries still subscribe to messages,
        // which asserts for the first private parameter on a transient system.
        // Register our own scoped data-only view model before calling the public
        // rename helper; this preserves its graph/renderer-binding fixups. Only
        // the owned copy is registered, and its view model dies before the copy.
        TSharedPtr<FNiagaraSystemViewModel> ParameterViewModel;
        TArray<FNiagaraVariable> Variables;Source.System->GetExposedParameters().GetParameters(Variables);
        for(const auto& Variable:Variables) {
            FNiagaraVariable Effective=EffectiveVariable(Variable,Source);
            if(!IsShared(Variable.GetName())) {
                if(!ParameterViewModel.IsValid()) {
                    FNiagaraSystemViewModelOptions Options;
                    Options.bCanAutoCompile=false;
                    Options.bCanModifyEmittersFromTimeline=false;
                    Options.bCanSimulate=false;
                    Options.bCompileForEdit=false;
                    Options.bIsForDataProcessingOnly=true;
                    Options.EditMode=ENiagaraSystemViewModelEditMode::SystemAsset;
                    Options.MessageLogGuid=FGuid::NewGuid();
                    ParameterViewModel=MakeShared<FNiagaraSystemViewModel>();
                    ParameterViewModel->Initialize(*Copy,Options);
                }
                Effective.SetName(FName(*Namespaced(Source.ID,Variable.GetName())));
                FNiagaraEditorUtilities::UserParameters::RenameUserParameterForSystem(*Copy,Variable,Effective);
                Output->GetExposedParameters().SetParameterData(Effective.GetData(),Effective,true);
            }
        }
        for(const auto& Handle:Copy->GetEmitterHandles()) {
            if(!Handle.GetIsEnabled())continue;
            const auto Versioned=Handle.GetInstance();
            const FGuid ID=FNiagaraEditorUtilities::AddEmitterToSystem(*Output,*Versioned.Emitter,Versioned.Version,true);
            auto* NewHandle=Output->GetEmitterHandles().FindByPredicate([&](const auto& H){return H.GetId()==ID;});
            if(!NewHandle || !NewHandle->GetEmitterData()) {OutError=TEXT("Niagara failed to create an owned emitter.");return nullptr;}
            NewHandle->SetName(FName(*(TEXT("L_")+Source.ID.ToString(EGuidFormats::Digits)+TEXT("_")+Handle.GetName().ToString())),*Output);
            NewHandle->GetEmitterData()->RemoveParent();
            if(Source.System->bFixedBounds) {
                // Preserve the source system's box per owned emitter. Do not force a global union
                // that would accidentally clip unrelated dynamic CPU emitters in the new system.
                NewHandle->GetEmitterData()->CalculateBoundsMode=ENiagaraEmitterCalculateBoundMode::Fixed;
                NewHandle->GetEmitterData()->FixedBounds=Source.System->GetFixedBounds();
            }
            if(!DelayBurst(Output,NewHandle->GetName(),Source.Layer->StartSeconds,OutError)) {OutError=LayerName(Source)+TEXT(": ")+OutError;return nullptr;}
        }
        TArray<UObject*> Owned;GetObjectsWithOuter(Output,Owned,EGetObjectsFlags::IncludeNestedObjects);Owned.Add(Output);
        for(auto* Object:Owned) {
            TArray<UObject*> References;FReferenceFinder Finder(References,nullptr,false,true,false,false);Finder.FindReferences(Object);
            for(auto* Reference:References) if(Reference && (Reference==Copy.Get() || Reference->IsIn(Copy.Get()))) {
                OutError=LayerName(Source)+TEXT(": copied emitter still references temporary source data ")+Reference->GetPathName()+TEXT("; export refused.");return nullptr;
            }
        }
    }
    Output->RequestCompile(false);
    return Output;
}

UNiagaraSystem* UMCVFXComposerBuilderLibrary::ExportNew(const UMCVFXComposition* Recipe,const FString& LongPackageName,FString& OutError)
{
    OutError.Reset();
    if(!LongPackageName.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(LongPackageName)
        || FPackageName::DoesPackageExist(LongPackageName) || FindPackage(nullptr,*LongPackageName)) {
        OutError=TEXT("Choose a new valid /Game package name; existing packages are never overwritten.");return nullptr;
    }
    TStrongObjectPtr<UNiagaraSystem> Built(BuildTransient(Recipe,OutError));if(!Built.IsValid())return nullptr;
    Built->WaitForCompilationComplete(true,false);
    if(!Built->IsReadyToRun()) {OutError=TEXT("Generated Niagara System did not compile to a runnable state; no asset saved.");return nullptr;}
    UPackage* Package=CreatePackage(*LongPackageName);
    auto* Saved=DuplicateObject<UNiagaraSystem>(Built.Get(),Package,*FPackageName::GetLongPackageAssetName(LongPackageName));
    Saved->ClearFlags(RF_Transient);Saved->SetFlags(RF_Public|RF_Standalone|RF_Transactional);
    TArray<UObject*> Owned;GetObjectsWithOuter(Saved,Owned,EGetObjectsFlags::IncludeNestedObjects);for(auto* Object:Owned)Object->ClearFlags(RF_Transient);
    Package->GetMetaData().SetValue(Saved,TEXT("MCVFXComposerTiming"),*GetExportNotes());
    Package->GetMetaData().SetValue(Saved,TEXT("MCVFXComposerAuthoringObjectAtExport"),*Recipe->GetPathName());
    Package->GetMetaData().SetValue(Saved,TEXT("MCComposerBuilderVersion"),TEXT("1"));
    if(Recipe->GetOutermost()->GetName().StartsWith(TEXT("/Game/")))
        Package->GetMetaData().SetValue(Saved,TEXT("MCComposerSourceRecipe"),*Recipe->GetPathName());
    if(const auto* Preset=Cast<UMCVFXRecipePreset>(Recipe)) {
        FString IDs;for(const auto& Node:Preset->Nodes){if(!IDs.IsEmpty())IDs+=TEXT(",");IDs+=Node.ID.ToString(EGuidFormats::Digits);}
        Package->GetMetaData().SetValue(Saved,TEXT("MCVFXComposerSourceNodeIDs"),*IDs);
        Package->GetMetaData().SetValue(Saved,TEXT("MCVFXComposerPresetName"),*Preset->PresetName);
    }
    FAssetRegistryModule::AssetCreated(Saved);Package->MarkPackageDirty();
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    const FString Filename=FPackageName::LongPackageNameToFilename(LongPackageName,FPackageName::GetAssetPackageExtension());
    if(!UPackage::SavePackage(Package,Saved,*Filename,Args)) {OutError=TEXT("Could not save the new generated System package.");return nullptr;}
    return Saved;
}

bool UMCVFXComposerBuilderLibrary::DiscoverControls(UMCVFXComposition* Recipe,int32 LayerIndex,FString& OutError)
{
    if(!Recipe){OutError=TEXT("Choose a recipe.");return false;}
    return Recipe->DiscoverParameters(LayerIndex,OutError);
}

FString UMCVFXComposerBuilderLibrary::GetExportNotes()
{
    return TEXT("Duration/Tail — длина предпросмотра. Экспорт копирует SystemSpawn/Update первой системы; остальные должны иметь тот же проверенный SystemState/inputs/RI. Это не запекание отсечки по Duration. Solo не меняет экспорт. Shared gameplay inputs должны совпадать у всех слоёв.");
}
