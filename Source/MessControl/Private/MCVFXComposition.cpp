#include "MCVFXComposition.h"

#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"

namespace MCVFXCompositionPrivate
{
    template<typename T> bool ValidateOverrides(const TArray<T>& Values,TSet<FName>& Used,FString& Error)
    {
        for(const auto& Item:Values)
        {
            if(!Item.bOverride) continue;
            if(!Item.Name.ToString().StartsWith(TEXT("User.")) || Used.Contains(Item.Name))
            {
                Error=FString::Printf(TEXT("Override %s must have a unique canonical User.* name."),*Item.Name.ToString());
                return false;
            }
            Used.Add(Item.Name);
        }
        return true;
    }

    template<typename T> bool CheckTypes(const TArray<T>& Values,const FNiagaraTypeDefinition& Type,
        const FNiagaraParameterStore& Store,FString& Error)
    {
        for(const auto& Item:Values)
            if(Item.bOverride && Store.IndexOf(FNiagaraVariable(Type,Item.Name))==INDEX_NONE)
            {
                Error=FString::Printf(TEXT("%s is missing or has a different type in this Niagara System."),*Item.Name.ToString());
                return false;
            }
        return true;
    }

    template<typename T,typename V> void AddDiscovered(TArray<T>& NewValues,const TArray<T>& OldValues,FName Name,const V& Value)
    {
        if(const T* Previous=OldValues.FindByPredicate([Name](const T& Item){return Item.Name==Name;})) NewValues.Add(*Previous);
        else {T Item;Item.Name=Name;Item.Value=Value;NewValues.Add(Item);}
    }
}

bool UMCVFXComposition::Validate(FString& OutError) const
{
    OutError.Reset();
    if(!FMath::IsFinite(DurationSeconds) || DurationSeconds<.05f || DurationSeconds>30.f
        || !FMath::IsFinite(TailSeconds) || TailSeconds<0 || TailSeconds>10.f)
    {
        OutError=TEXT("Duration must be 0.05–30 seconds; tail must be 0–10 seconds.");return false;
    }
    if(Layers.IsEmpty() || Layers.Num()>32) {OutError=TEXT("A composition needs 1–32 layers.");return false;}
    bool bHasEnabled=false;
    for(int32 Index=0;Index<Layers.Num();++Index)
    {
        const auto& Layer=Layers[Index];
        if(!Layer.bEnabled) continue;
        bHasEnabled=true;
        if(Layer.System.IsNull()) {OutError=FString::Printf(TEXT("Layer %d has no Niagara System."),Index+1);return false;}
        if(Layer.Binding!=EMCVFXCompositionBinding::Composition && Layer.Binding!=EMCVFXCompositionBinding::World
            && Layer.Binding!=EMCVFXCompositionBinding::Subject)
        {OutError=TEXT("Unknown layer binding.");return false;}
        if(Layer.Transform.ContainsNaN() || !Layer.Transform.GetRotation().IsNormalized()
            || Layer.Transform.GetScale3D().GetAbsMin()<.0001)
        {OutError=TEXT("Layer transform must be finite with a normalized rotation and nonzero scale.");return false;}
        if(!FMath::IsFinite(Layer.StartSeconds) || Layer.StartSeconds<0 || Layer.StartSeconds>=DurationSeconds
            || !FMath::IsFinite(Layer.EmissionSeconds) || Layer.EmissionSeconds<0 || Layer.EmissionSeconds>30)
        {OutError=TEXT("Layer start must be inside the composition; emission duration must be 0–30 seconds.");return false;}
        if(Layer.Binding!=EMCVFXCompositionBinding::Subject && !Layer.Socket.IsNone())
        {OutError=TEXT("Only Subject-bound layers can specify a socket.");return false;}
        TSet<FName> Used;
        using namespace MCVFXCompositionPrivate;
        if(!ValidateOverrides(Layer.Floats,Used,OutError) || !ValidateOverrides(Layer.Colors,Used,OutError)
            || !ValidateOverrides(Layer.Vectors,Used,OutError) || !ValidateOverrides(Layer.Bools,Used,OutError)) return false;
        for(const auto& Item:Layer.Floats) if(Item.bOverride && !FMath::IsFinite(Item.Value))
        {OutError=TEXT("Float override must be finite.");return false;}
        for(const auto& Item:Layer.Colors) if(Item.bOverride && (!FMath::IsFinite(Item.Value.R) || !FMath::IsFinite(Item.Value.G)
            || !FMath::IsFinite(Item.Value.B) || !FMath::IsFinite(Item.Value.A)))
        {OutError=TEXT("Color override must be finite.");return false;}
        for(const auto& Item:Layer.Vectors) if(Item.bOverride && Item.Value.ContainsNaN())
        {OutError=TEXT("Vector override must be finite.");return false;}
    }
    if(!bHasEnabled) {OutError=TEXT("Enable at least one layer.");return false;}
    return true;
}

bool UMCVFXComposition::ValidateParameters(const FMCVFXCompositionLayer& Layer,const UNiagaraSystem* System,FString& OutError)
{
    OutError.Reset();
    if(!System) {OutError=TEXT("Niagara System is unavailable.");return false;}
    const auto& Store=System->GetExposedParameters();
    using namespace MCVFXCompositionPrivate;
    return CheckTypes(Layer.Floats,FNiagaraTypeDefinition::GetFloatDef(),Store,OutError)
        && CheckTypes(Layer.Colors,FNiagaraTypeDefinition::GetColorDef(),Store,OutError)
        && CheckTypes(Layer.Vectors,FNiagaraTypeDefinition::GetVec3Def(),Store,OutError)
        && CheckTypes(Layer.Bools,FNiagaraTypeDefinition::GetBoolDef(),Store,OutError);
}

bool UMCVFXComposition::DiscoverParameters(int32 LayerIndex,FString& OutError)
{
    OutError.Reset();
    if(!Layers.IsValidIndex(LayerIndex)) {OutError=TEXT("Choose a layer.");return false;}
    auto& Layer=Layers[LayerIndex];
    const UNiagaraSystem* System=Layer.System.LoadSynchronous();
    if(!System) {OutError=TEXT("Could not load the selected Niagara System.");return false;}
    const auto& Store=System->GetExposedParameters();
    TArray<FNiagaraVariable> Variables;Store.GetParameters(Variables);
    Variables.Sort([](const FNiagaraVariable& A,const FNiagaraVariable& B){return A.GetName().LexicalLess(B.GetName());});
    TArray<FMCVFXFloatOverride> Floats;TArray<FMCVFXColorOverride> Colors;
    TArray<FMCVFXVectorOverride> Vectors;TArray<FMCVFXBoolOverride> Bools;
    using namespace MCVFXCompositionPrivate;
    int32 Unsupported=0;
    for(const auto& Variable:Variables)
    {
        if(!Variable.GetName().ToString().StartsWith(TEXT("User."))) continue;
        const auto& Type=Variable.GetType();
        if(Type==FNiagaraTypeDefinition::GetFloatDef())
            AddDiscovered(Floats,Layer.Floats,Variable.GetName(),Store.GetParameterValue<float>(Variable));
        else if(Type==FNiagaraTypeDefinition::GetColorDef())
            AddDiscovered(Colors,Layer.Colors,Variable.GetName(),Store.GetParameterValue<FLinearColor>(Variable));
        else if(Type==FNiagaraTypeDefinition::GetVec3Def())
            AddDiscovered(Vectors,Layer.Vectors,Variable.GetName(),FVector(Store.GetParameterValue<FVector3f>(Variable)));
        else if(Type==FNiagaraTypeDefinition::GetBoolDef())
            AddDiscovered(Bools,Layer.Bools,Variable.GetName(),Store.GetParameterValue<FNiagaraBool>(Variable).GetValue());
        else ++Unsupported;
    }
    Layer.Floats=MoveTemp(Floats);Layer.Colors=MoveTemp(Colors);Layer.Vectors=MoveTemp(Vectors);Layer.Bools=MoveTemp(Bools);
    if(Unsupported) OutError=FString::Printf(TEXT("Read supported defaults; %d complex/int/position inputs are unchanged in the source. Mesh sampling and live tether endpoints need a dedicated adapter."),Unsupported);
    return true;
}
