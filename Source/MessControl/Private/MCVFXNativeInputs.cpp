#include "MCVFXNativeInputs.h"
#include "MCFreezeCapturedGeometry.h"

#include "Misc/LargeWorldRenderPosition.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFloat.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraTypes.h"

namespace
{
constexpr TCHAR NativeInputPrefix[] = TEXT("User.Native.");
bool FloatVector(const FVector& V)
{
    const double Limit = TNumericLimits<float>::Max();
    return !V.ContainsNaN() && FMath::Abs(V.X)<=Limit && FMath::Abs(V.Y)<=Limit && FMath::Abs(V.Z)<=Limit;
}
bool FiniteColor(const FLinearColor& V)
{return FMath::IsFinite(V.R)&&FMath::IsFinite(V.G)&&FMath::IsFinite(V.B)&&FMath::IsFinite(V.A);}
bool ArrayType(EMCVFXNativeInputType Type)
{return Type==EMCVFXNativeInputType::FloatArray || Type==EMCVFXNativeInputType::VectorArray || Type==EMCVFXNativeInputType::PositionArray;}
FNiagaraTypeDefinition TypeOf(EMCVFXNativeInputType Type)
{
    switch(Type) {
    case EMCVFXNativeInputType::Float:return FNiagaraTypeDefinition::GetFloatDef();
    case EMCVFXNativeInputType::Int:return FNiagaraTypeDefinition::GetIntDef();
    case EMCVFXNativeInputType::Bool:return FNiagaraTypeDefinition::GetBoolDef();
    case EMCVFXNativeInputType::Color:return FNiagaraTypeDefinition::GetColorDef();
    case EMCVFXNativeInputType::Vector:return FNiagaraTypeDefinition::GetVec3Def();
    case EMCVFXNativeInputType::Position:return FNiagaraTypeDefinition::GetPositionDef();
    case EMCVFXNativeInputType::Quaternion:return FNiagaraTypeDefinition::GetQuatDef();
    case EMCVFXNativeInputType::FloatArray:return FNiagaraTypeDefinition(UNiagaraDataInterfaceArrayFloat::StaticClass());
    case EMCVFXNativeInputType::VectorArray:return FNiagaraTypeDefinition(UNiagaraDataInterfaceArrayFloat3::StaticClass());
    case EMCVFXNativeInputType::PositionArray:return FNiagaraTypeDefinition(UNiagaraDataInterfaceArrayPosition::StaticClass());
    default:return FNiagaraTypeDefinition();
    }
}
bool EqualArray(const FMCVFXNativeInput& A,const FMCVFXNativeInput* B)
{
    if(!B || A.Name!=B->Name || A.Type!=B->Type)return false;
    switch(A.Type) {
    case EMCVFXNativeInputType::FloatArray:return A.FloatValues==B->FloatValues;
    case EMCVFXNativeInputType::VectorArray:return A.VectorValues==B->VectorValues;
    case EMCVFXNativeInputType::PositionArray:return A.PositionValues==B->PositionValues;
    default:return false;
    }
}
bool LWCOrigin(UNiagaraComponent* C,FVector& Out,bool& HasController,FString& Error)
{
    // Identical to the public position-array library's live converter. Before
    // activation that library instead uses zero, so prebase using the exact
    // initial-reset tile rule from NiagaraSystemInstance.cpp, not actor origin.
    const auto Controller=C->GetSystemInstanceController();HasController=Controller.IsValid();
    if(HasController) {
        const auto* Instance=Controller->GetSystemInstance_Unsafe();
        if(!Instance){Error=TEXT("Native position arrays require a valid Niagara controller instance.");return false;}
        Out=Instance->GetLWCConverter().ConvertSimulationPositionToWorld(FNiagaraPosition(FVector3f::ZeroVector));
    }
    else {
        C->UpdateComponentToWorld();
        if(C->GetComponentLocation().ContainsNaN()){Error=TEXT("Native position arrays require a finite evaluated component frame.");return false;}
        Out=C->GetAsset()->SupportsLargeWorldCoordinates()
            ?FVector(FLargeWorldRenderScalar::GetTileFor(C->GetComponentLocation()))*FLargeWorldRenderScalar::GetTileSize()
            :FVector::ZeroVector;
    }
    if(Out.ContainsNaN()){Error=TEXT("Native position array LWC tile is not finite.");return false;}
    return true;
}
}

bool MCVFXNativeInputs::IsNativeName(FName Name)
{return Name.ToString().StartsWith(NativeInputPrefix,ESearchCase::IgnoreCase);}

const FMCVFXNativeInput* FMCVFXNativeInputs::Find(FName Name) const
{return Entries.FindByPredicate([Name](const auto& Entry){return Entry.Name==Name;});}

bool FMCVFXNativeInputs::Validate(FString& Error) const
{
    Error.Reset();
    if(Entries.Num()>MaxEntries){Error=TEXT("Native VFX payload exceeds 64 entries.");return false;}
    TSet<FName> Names;int64 ArrayValues=0;
    for(const auto& Entry:Entries) {
        const FString Name=Entry.Name.ToString();
        if(FMCPreparedFreezeGeometryLayer::IsCaptureName(Entry.Name))
        {Error=TEXT("Freeze capture resources are never serialized as general NativeInputs rows.");return false;}
        if(!MCVFXNativeInputs::IsNativeName(Entry.Name) || Name.Len()<=UE_ARRAY_COUNT(NativeInputPrefix)-1 || Names.Contains(Entry.Name))
        {Error=FString::Printf(TEXT("Native VFX names must be unique, nonempty User.Native.* names: %s."),*Name);return false;}
        Names.Add(Entry.Name);
        ArrayValues+=int64(Entry.FloatValues.Num())+Entry.VectorValues.Num()+Entry.PositionValues.Num();
        if(ArrayValues>MaxArrayValues){Error=TEXT("Native VFX payload exceeds 1024 total array values.");return false;}
        if((Entry.Type!=EMCVFXNativeInputType::FloatArray && !Entry.FloatValues.IsEmpty())
            || (Entry.Type!=EMCVFXNativeInputType::VectorArray && !Entry.VectorValues.IsEmpty())
            || (Entry.Type!=EMCVFXNativeInputType::PositionArray && !Entry.PositionValues.IsEmpty()))
        {Error=FString::Printf(TEXT("Native VFX entry %s contains an array outside its declared type."),*Name);return false;}
        bool Finite=true;
        switch(Entry.Type) {
        case EMCVFXNativeInputType::Float:Finite=FMath::IsFinite(Entry.FloatValue);break;
        case EMCVFXNativeInputType::Int:case EMCVFXNativeInputType::Bool:break;
        case EMCVFXNativeInputType::Color:Finite=FiniteColor(Entry.ColorValue);break;
        case EMCVFXNativeInputType::Vector:Finite=FloatVector(Entry.VectorValue);break;
        case EMCVFXNativeInputType::Position:Finite=!Entry.PositionValue.ContainsNaN();break;
        case EMCVFXNativeInputType::Quaternion:Finite=!Entry.QuaternionValue.ContainsNaN() && Entry.QuaternionValue.IsNormalized();break;
        case EMCVFXNativeInputType::FloatArray:for(float V:Entry.FloatValues)Finite&=FMath::IsFinite(V);break;
        case EMCVFXNativeInputType::VectorArray:for(const auto& V:Entry.VectorValues)Finite&=FloatVector(V);break;
        case EMCVFXNativeInputType::PositionArray:for(const auto& V:Entry.PositionValues)Finite&=!V.ContainsNaN();break;
        default:Finite=false;break;
        }
        if(!Finite){Error=FString::Printf(TEXT("Native VFX entry %s has an invalid type, nonfinite value, unrepresentable vector or nonunit quaternion."),*Name);return false;}
    }
    return true;
}

bool FMCVFXNativeInputs::ValidateForSystem(const UNiagaraSystem* System,FString& Error,const FMCFreezeCapturedGeometry* FreezeCapture) const
{
    if(!Validate(Error))return false;
    if(!IsValid(System)){Error=TEXT("Native VFX payload needs an already loaded Niagara System.");return false;}
    const bool UsesFreeze=FMCPreparedFreezeGeometryLayer::UsesCapture(System);
    if(UsesFreeze && (!FreezeCapture||!FreezeCapture->GetWorld()||!FreezeCapture->GetEpoch().IsValid()
        ||FreezeCapture->GetTriangleCount()<=0||!FMCPreparedFreezeGeometryLayer::ValidateSystem(System,Error)))
    {if(Error.IsEmpty())Error=TEXT("Reserved freeze arrays require the separately prepared local captured geometry.");return false;}
    TArray<FNiagaraVariable> Variables;System->GetExposedParameters().GetParameters(Variables);
    for(const auto& Variable:Variables)if(MCVFXNativeInputs::IsNativeName(Variable.GetName())) {
        // These exact typed arrays are bound only by the finite freeze resource,
        // never serialized as general NativeInputs rows or artist overrides.
        if(UsesFreeze&&FMCPreparedFreezeGeometryLayer::IsCaptureName(Variable.GetName()))continue;
        const auto* Entry=Find(Variable.GetName());
        if(!Entry || Variable.GetType()!=TypeOf(Entry->Type)) {
            Error=FString::Printf(TEXT("Niagara native input %s needs a payload entry with exact declared type %s."),
                *Variable.GetName().ToString(),*Variable.GetType().GetName());return false;
        }
    }
    return true;
}

void FMCVFXNativeInputsApplyState::Reset()
{Component.Reset();System.Reset();Previous.Entries.Reset();ArrayInterfaces.Reset();LWCOrigin=FVector::ZeroVector;bValid=false;}

bool MCVFXNativeInputs::Apply(UNiagaraComponent* C,const FMCVFXNativeInputs& Inputs,
    FString& Error,FMCVFXNativeInputsApplyState* State,int32* OutArrayUploads,const FMCFreezeCapturedGeometry* FreezeCapture)
{
    if(OutArrayUploads)*OutArrayUploads=0;
    if(!IsInGameThread() || !IsValid(C)){Error=TEXT("Native VFX inputs apply only to a valid component on the game thread.");return false;}
    auto* System=C->GetAsset();if(!Inputs.ValidateForSystem(System,Error,FreezeCapture))return false;
    const bool Cached=State && State->bValid && State->Component==C && State->System==System;
    TMap<FName,UNiagaraDataInterface*> Interfaces;
    FVector Origin=FVector::ZeroVector;bool HasController=false,HasPositionArrays=false;
    // Preflight all referenced interfaces and LWC conversions before any writes.
    for(const auto& Entry:Inputs.Entries) {
        const auto Type=TypeOf(Entry.Type);
        if(System->GetExposedParameters().IndexOf(FNiagaraVariable(Type,Entry.Name))==INDEX_NONE)continue;
        if(ArrayType(Entry.Type)) {
            auto* DI=UNiagaraFunctionLibrary::GetDataInterface(Type.GetClass(),C,Entry.Name);
            if(!IsValid(DI) || DI->GetClass()!=Type.GetClass() || !DI->IsIn(C))
            {Error=FString::Printf(TEXT("Native VFX array %s requires its exact component-owned interface before activation."),*Entry.Name.ToString());return false;}
            Interfaces.Add(Entry.Name,DI);
            HasPositionArrays|=Entry.Type==EMCVFXNativeInputType::PositionArray;
        }
    }
    if(HasPositionArrays) {
        if(!LWCOrigin(C,Origin,HasController,Error))return false;
        for(const auto& Entry:Inputs.Entries)if(Entry.Type==EMCVFXNativeInputType::PositionArray && Interfaces.Contains(Entry.Name))
            for(const FVector& V:Entry.PositionValues)if(!FloatVector(V-Origin))
            {Error=FString::Printf(TEXT("Native position array %s cannot be represented relative to this Niagara tile."),*Entry.Name.ToString());return false;}
    }
    int32 Uploads=0;
    for(const auto& Entry:Inputs.Entries) {
        if(System->GetExposedParameters().IndexOf(FNiagaraVariable(TypeOf(Entry.Type),Entry.Name))==INDEX_NONE)continue;
        if(ArrayType(Entry.Type)) {
            const auto* OldInterface=Cached?State->ArrayInterfaces.Find(Entry.Name):nullptr;
            const bool Same=Cached && OldInterface && OldInterface->Get()==Interfaces.FindChecked(Entry.Name)
                && EqualArray(Entry,State->Previous.Find(Entry.Name))
                && (Entry.Type!=EMCVFXNativeInputType::PositionArray || State->LWCOrigin==Origin);
            if(Same)continue;
            if(Entry.Type==EMCVFXNativeInputType::FloatArray)
                UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(C,Entry.Name,Entry.FloatValues);
            else if(Entry.Type==EMCVFXNativeInputType::VectorArray)
                UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(C,Entry.Name,Entry.VectorValues);
            else if(HasController)
                UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(C,Entry.Name,Entry.PositionValues);
            else {
                // The stock setter uses a zero converter before a controller
                // exists. Store precisely what its upcoming initial reset tile
                // expects, without first rounding the original world position.
                TArray<FVector> Relative;Relative.Reserve(Entry.PositionValues.Num());
                for(const FVector& V:Entry.PositionValues)Relative.Add(V-Origin);
                UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(C,Entry.Name,Relative);
            }
            ++Uploads;continue;
        }
        switch(Entry.Type) {
        case EMCVFXNativeInputType::Float:C->SetVariableFloat(Entry.Name,Entry.FloatValue);break;
        case EMCVFXNativeInputType::Int:C->SetVariableInt(Entry.Name,Entry.IntValue);break;
        case EMCVFXNativeInputType::Bool:C->SetVariableBool(Entry.Name,Entry.BoolValue);break;
        case EMCVFXNativeInputType::Color:C->SetVariableLinearColor(Entry.Name,Entry.ColorValue);break;
        case EMCVFXNativeInputType::Vector:C->SetVariableVec3(Entry.Name,Entry.VectorValue);break;
        case EMCVFXNativeInputType::Position:C->SetVariablePosition(Entry.Name,Entry.PositionValue);break;
        case EMCVFXNativeInputType::Quaternion:C->SetVariableQuat(Entry.Name,Entry.QuaternionValue);break;
        default:break;
        }
    }
    if(State) {
        State->Component=C;State->System=System;State->Previous=Inputs;State->LWCOrigin=Origin;State->bValid=true;
        State->ArrayInterfaces.Reset();
        for(const auto& Pair:Interfaces)State->ArrayInterfaces.Add(Pair.Key,
            UNiagaraFunctionLibrary::GetDataInterface(Pair.Value->GetClass(),C,Pair.Key));
    }
    if(OutArrayUploads)*OutArrayUploads=Uploads;
    return true;
}
