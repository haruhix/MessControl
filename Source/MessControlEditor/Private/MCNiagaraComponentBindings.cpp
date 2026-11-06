#include "MCNiagaraInputToolset.h"

#include "Editor.h"
#include "NiagaraComponentRendererProperties.h"
#include "NiagaraSystem.h"
#include "ScopedTransaction.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

FString UMCNiagaraInputToolset::SetNiagaraComponentFloatBinding(UNiagaraSystem* System,FName EmitterName,int32 RendererIndex,
    FName PropertyName,FName ParticleAttribute,FName OwnerMetadataKey,const FString& ExpectedOwnerValue)
{
    auto Result=MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("complete"),false);
    const auto Encode=[&Result]() {
        FString Json;
        FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Json));
        return Json;
    };
    const auto Reject=[&](const TCHAR* Error) {
        Result->SetStringField(TEXT("error"),Error);
        return Encode();
    };
    if(!IsInGameThread()||!GIsEditor||!GEditor||GEditor->PlayWorld||IsRunningCookCommandlet()||GEditor->IsTransactionActive())
        return Reject(TEXT("Edit from the Editor game thread, outside PIE, cook or an existing transaction."));
    if(!IsValid(System)||System->IsTemplate()||System->HasAnyFlags(RF_NeedLoad|RF_NeedPostLoad|RF_BeginDestroyed|RF_FinishDestroyed))
        return Reject(TEXT("An initialized Niagara System is required."));
    UPackage* Package=System->GetOutermost();
    if(!Package->GetName().StartsWith(TEXT("/Game/"))||Package->bIsCookedForEditor||Package->HasAnyPackageFlags(PKG_FilterEditorOnly)
        ||OwnerMetadataKey.IsNone()||ExpectedOwnerValue.IsEmpty()||Package->GetMetaData().GetValue(System,OwnerMetadataKey)!=ExpectedOwnerValue)
        return Reject(TEXT("The system must be a project asset with the exact ownership metadata."));
    if(System->HasOutstandingCompilationRequests()||!System->IsReadyToRun())
        return Reject(TEXT("Compile the system and wait for it to be ready before binding a particle attribute."));

    const FNiagaraEmitterHandle* Handle=System->GetEmitterHandles().FindByPredicate([EmitterName](const FNiagaraEmitterHandle& Candidate) {
        return Candidate.GetName()==EmitterName;
    });
    const FVersionedNiagaraEmitterData* EmitterData=Handle&&Handle->GetIsEnabled()?Handle->GetEmitterData():nullptr;
    if(!EmitterData||EmitterData->SimTarget!=ENiagaraSimTarget::CPUSim||!EmitterData->GetRenderers().IsValidIndex(RendererIndex))
        return Reject(TEXT("Select an exact enabled CPU emitter and valid renderer index."));
    auto* Renderer=Cast<UNiagaraComponentRendererProperties>(EmitterData->GetRenderers()[RendererIndex]);
    if(!IsValid(Renderer)||!Renderer->GetIsEnabled()||Renderer->GetTypedOuter<UNiagaraSystem>()!=System
        ||!IsValid(Renderer->TemplateComponent)||Renderer->TemplateComponent->GetOuter()!=Renderer)
        return Reject(TEXT("Select an enabled Component Renderer with a template owned by this system."));

    const UClass* TemplateClass=Renderer->TemplateComponent->GetClass();
    const FFloatProperty* Property=FindFProperty<FFloatProperty>(TemplateClass,PropertyName);
    if(!Property||Property->ArrayDim!=1||!Property->HasAnyPropertyFlags(CPF_Edit)||Property->HasAnyPropertyFlags(CPF_EditConst))
        return Reject(TEXT("Select an editable scalar float template property."));
    const FName SetterName(*Property->GetMetaData(TEXT("BlueprintSetter")));
    const UFunction* Setter=SetterName.IsNone()?nullptr:TemplateClass->FindFunctionByName(SetterName);
    if(!Setter||Setter->NumParms!=1||Setter->GetReturnProperty()||Setter->HasAnyFunctionFlags(FUNC_Static))
        return Reject(TEXT("The float property must declare an explicit single-argument BlueprintSetter."));
    TFieldIterator<FProperty> Argument(Setter);
    for(;Argument&&!Argument->HasAnyPropertyFlags(CPF_Parm);++Argument) {}
    if(!Argument||!CastField<FFloatProperty>(*Argument)||Argument->ArrayDim!=1||Argument->HasAnyPropertyFlags(CPF_OutParm|CPF_ReferenceParm))
        return Reject(TEXT("The setter must accept one scalar float by value."));

    const FNiagaraVariable Attribute(FNiagaraTypeDefinition::GetFloatDef(),ParticleAttribute);
    if(!Attribute.IsInNameSpace(FName(TEXT("Particles"))))
        return Reject(TEXT("Provide a fully qualified Particles.* float attribute."));
    FNiagaraComponentPropertyBinding Binding;
    Binding.PropertyName=PropertyName;
    Binding.PropertyType=FNiagaraTypeDefinition::GetFloatDef();
    Binding.MetadataSetterName=SetterName;
    Binding.AttributeBinding.Setup(Attribute,Attribute);
    const FNiagaraVariableBase DataSetAttribute=Binding.AttributeBinding.GetDataSetBindableVariable();
    TArray<FNiagaraVariableBase> CompiledAttributes;
    EmitterData->GatherCompiledParticleAttributes(CompiledAttributes);
    if(!CompiledAttributes.Contains(DataSetAttribute))
        return Reject(TEXT("This emitter has no matching float attribute in the compiled particle data set."));

    const auto MatchesProperty=[PropertyName](const FNiagaraComponentPropertyBinding& Existing) {
        return Existing.PropertyName==PropertyName;
    };
    const FNiagaraComponentPropertyBinding* Existing=Renderer->PropertyBindings.FindByPredicate(MatchesProperty);
    const bool bChanged=!Existing||Existing->MetadataSetterName!=SetterName||Existing->PropertyType!=Binding.PropertyType
        ||Existing->AttributeBinding.GetDataSetBindableVariable()!=DataSetAttribute||!Existing->AttributeBinding.IsParticleBinding()
        ||Renderer->PropertyBindings.FindLastByPredicate(MatchesProperty)!=Renderer->PropertyBindings.IndexOfByPredicate(MatchesProperty);
    if(bChanged) {
        FProperty* BindingsProperty=FindFProperty<FProperty>(UNiagaraComponentRendererProperties::StaticClass(),
            GET_MEMBER_NAME_CHECKED(UNiagaraComponentRendererProperties,PropertyBindings));
        if(!BindingsProperty)return Reject(TEXT("The renderer's reflected binding array is unavailable."));
        const FScopedTransaction Transaction(NSLOCTEXT("MCVFXComposer","BindNiagaraComponentFloat","Bind Niagara component float"));
        Renderer->Modify();
        Renderer->PreEditChange(BindingsProperty);
        Renderer->PropertyBindings.RemoveAll(MatchesProperty);
        Renderer->PropertyBindings.Add(MoveTemp(Binding));
        FPropertyChangedEvent Change(BindingsProperty,EPropertyChangeType::ValueSet);
        Renderer->PostEditChangeProperty(Change);
        System->MarkPackageDirty();
    }
    const bool bCompileRequested=bChanged&&System->RequestCompile(true);

    Result->SetBoolField(TEXT("complete"),true);
    Result->SetBoolField(TEXT("changed"),bChanged);
    Result->SetBoolField(TEXT("compile_requested"),bCompileRequested);
    Result->SetStringField(TEXT("system"),System->GetPathName());
    Result->SetStringField(TEXT("emitter"),EmitterName.ToString());
    Result->SetNumberField(TEXT("renderer_index"),RendererIndex);
    Result->SetStringField(TEXT("property"),PropertyName.ToString());
    Result->SetStringField(TEXT("particle_attribute"),ParticleAttribute.ToString());
    Result->SetStringField(TEXT("setter"),SetterName.ToString());
    return Encode();
}
