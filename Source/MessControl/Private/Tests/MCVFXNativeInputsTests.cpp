#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCVFXNativeInputs.h"
#include "Misc/LargeWorldRenderPosition.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFloat.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include <limits>

namespace MCVFXNativeInputsTests
{
FMCVFXNativeInput Entry(const TCHAR* Name,EMCVFXNativeInputType Type)
{FMCVFXNativeInput E;E.Name=Name;E.Type=Type;return E;}
void Declare(UNiagaraSystem* S,FName Name,const FNiagaraTypeDefinition& Type)
{S->GetExposedParameters().AddParameter(FNiagaraVariable(Type,Name));}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXNativePayloadValidation,"MessControl.VFX.NativeInputs.BoundedTypedPayload",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXNativePayloadValidation::RunTest(const FString&)
{
    using namespace MCVFXNativeInputsTests;FString Error;FMCVFXNativeInputs P;
    P.Entries.Add(Entry(TEXT("User.Native.CueAge"),EMCVFXNativeInputType::Float));
    TestTrue(TEXT("An accepted native float is valid"),P.Validate(Error));
    const FName CaseAlias(TEXT("user.native.cueage"));
    TestTrue(TEXT("Niagara's FName address ignores display case"),CaseAlias==P.Entries[0].Name);
    TestTrue(TEXT("The same case alias remains reserved read-only"),MCVFXNativeInputs::IsNativeName(CaseAlias));
    TestFalse(TEXT("A similar namespace without the exact dot boundary is not native"),MCVFXNativeInputs::IsNativeName(TEXT("User.NativeArtist.CueAge")));
    P.Entries.Add(Entry(TEXT("user.native.cueage"),EMCVFXNativeInputType::Float));
    TestFalse(TEXT("Case aliases cannot bypass duplicate-name validation"),P.Validate(Error));P.Entries.Pop();
    const FMCVFXNativeInput Duplicate=P.Entries[0];
    P.Entries.Add(Duplicate);TestFalse(TEXT("Duplicate names fail before any writes"),P.Validate(Error));P.Entries.Pop();
    P.Entries[0].Name=TEXT("User.Artist.CueAge");TestFalse(TEXT("Artist namespace cannot enter a native receipt"),P.Validate(Error));
    P.Entries[0].Name=TEXT("User.Native.");TestFalse(TEXT("The reserved prefix requires a suffix"),P.Validate(Error));
    P.Entries[0].Name=TEXT("User.Native.CueAge");P.Entries[0].FloatValue=std::numeric_limits<float>::infinity();
    TestFalse(TEXT("Nonfinite accepted data is rejected"),P.Validate(Error));P.Entries[0].FloatValue=0;
    P.Entries[0].FloatValues.Add(1);TestFalse(TEXT("A scalar cannot hide replicated array storage"),P.Validate(Error));P.Entries[0].FloatValues.Reset();
    P.Entries[0].Type=EMCVFXNativeInputType::Position;P.Entries[0].PositionValue=FVector(1.e12+.125,-1.e12,17);
    TestTrue(TEXT("LWC world positions retain double precision"),P.Validate(Error));
    P.Entries[0].Type=EMCVFXNativeInputType::Vector;P.Entries[0].VectorValue=FVector(1.e100,0,0);
    TestFalse(TEXT("A normal vector cannot overflow its Niagara float representation"),P.Validate(Error));
    P.Entries[0].Type=EMCVFXNativeInputType::Quaternion;P.Entries[0].QuaternionValue=FQuat(0,0,0,0);
    TestFalse(TEXT("Degenerate rotations are rejected rather than silently normalized"),P.Validate(Error));
    P.Entries.Reset();
    for(int32 I=0;I<64;++I)P.Entries.Add(Entry(*FString::Printf(TEXT("User.Native.Value%d"),I),EMCVFXNativeInputType::Int));
    TestTrue(TEXT("64 records are allowed"),P.Validate(Error));P.Entries.Add(Entry(TEXT("User.Native.Excess"),EMCVFXNativeInputType::Int));
    TestFalse(TEXT("The 65th record fails"),P.Validate(Error));
    P.Entries.Reset();auto& A=P.Entries.Add_GetRef(Entry(TEXT("User.Native.Ground"),EMCVFXNativeInputType::PositionArray));A.PositionValues.Init(FVector::ZeroVector,1024);
    const bool bArrayBoundAccepted=P.Validate(Error);
    TestTrue(FString::Printf(TEXT("1024 array values are allowed (validator: %s)"),*Error),bArrayBoundAccepted);
    auto& B=P.Entries.Add_GetRef(Entry(TEXT("User.Native.Weights"),EMCVFXNativeInputType::FloatArray));B.FloatValues.Add(1);
    TestFalse(TEXT("The aggregate bound covers all array entries"),P.Validate(Error));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXNativeDeclaredContract,"MessControl.VFX.NativeInputs.DeclaredNamespaceContract",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXNativeDeclaredContract::RunTest(const FString&)
{
    using namespace MCVFXNativeInputsTests;auto* S=NewObject<UNiagaraSystem>();FString Error;FMCVFXNativeInputs P;
    Declare(S,TEXT("User.Native.Ground"),FNiagaraTypeDefinition::GetPositionDef());
    Declare(S,TEXT("User.Native.Points"),FNiagaraTypeDefinition(UNiagaraDataInterfaceArrayPosition::StaticClass()));
    Declare(S,TEXT("User.Artist.Width"),FNiagaraTypeDefinition::GetFloatDef());
    TestFalse(TEXT("Each declared native input requires accepted data"),P.ValidateForSystem(S,Error));
    P.Entries.Add(Entry(TEXT("User.Native.Ground"),EMCVFXNativeInputType::Vector));
    P.Entries.Add(Entry(TEXT("User.Native.Points"),EMCVFXNativeInputType::VectorArray));
    TestFalse(TEXT("Vector cannot substitute for LWC Position"),P.ValidateForSystem(S,Error));
    P.Entries[0].Type=EMCVFXNativeInputType::Position;
    TestFalse(TEXT("Vector array cannot substitute for the exact Position DI"),P.ValidateForSystem(S,Error));
    P.Entries[1].Type=EMCVFXNativeInputType::PositionArray;
    P.Entries.Add(Entry(TEXT("User.Native.OtherCueValue"),EMCVFXNativeInputType::Bool));
    TestTrue(TEXT("Valid extra native fields are ignored by an unrelated layer"),P.ValidateForSystem(S,Error));
    Declare(S,TEXT("User.Native.NativeRotation"),FNiagaraTypeDefinition::GetQuatDef());
    P.Entries.Add(Entry(TEXT("User.Native.NativeRotation"),EMCVFXNativeInputType::Vector));
    TestFalse(TEXT("Quaternion remains its exact Niagara type"),P.ValidateForSystem(S,Error));
    P.Entries.Last().Type=EMCVFXNativeInputType::Quaternion;
    TestTrue(TEXT("The exact typed contract is complete"),P.ValidateForSystem(S,Error));
    auto* Lower=NewObject<UNiagaraSystem>();Declare(Lower,TEXT("user.native.cueage"),FNiagaraTypeDefinition::GetFloatDef());
    FMCVFXNativeInputs AliasPayload;
    TestFalse(TEXT("Lowercase native declarations cannot evade required payload checks"),AliasPayload.ValidateForSystem(Lower,Error));
    AliasPayload.Entries.Add(Entry(TEXT("User.Native.CueAge"),EMCVFXNativeInputType::Float));
    TestTrue(TEXT("Canonical payload addresses the same lowercase FName declaration"),AliasPayload.ValidateForSystem(Lower,Error));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXNativeActualSetters,"MessControl.VFX.NativeInputs.TypedSettersAndStaticArrayDiff",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXNativeActualSetters::RunTest(const FString&)
{
    using namespace MCVFXNativeInputsTests;auto* S=NewObject<UNiagaraSystem>();FMCVFXNativeInputs P;FString Error;
    P.Entries.Add(Entry(TEXT("User.Native.CueAge"),EMCVFXNativeInputType::Float));P.Entries.Last().FloatValue=.25f;
    P.Entries.Add(Entry(TEXT("User.Native.Count"),EMCVFXNativeInputType::Int));P.Entries.Last().IntValue=63;
    P.Entries.Add(Entry(TEXT("User.Native.HasGround"),EMCVFXNativeInputType::Bool));P.Entries.Last().BoolValue=true;
    P.Entries.Add(Entry(TEXT("User.Native.Tint"),EMCVFXNativeInputType::Color));P.Entries.Last().ColorValue=FLinearColor(.2f,.4f,.8f,.6f);
    P.Entries.Add(Entry(TEXT("User.Native.Normal"),EMCVFXNativeInputType::Vector));P.Entries.Last().VectorValue=FVector(0,0,1);
    P.Entries.Add(Entry(TEXT("User.Native.Ground"),EMCVFXNativeInputType::Position));P.Entries.Last().PositionValue=FVector(1.e12+.125,5,7);
    P.Entries.Add(Entry(TEXT("User.Native.Rotation"),EMCVFXNativeInputType::Quaternion));P.Entries.Last().QuaternionValue=FQuat(FVector::UpVector,.6);
    P.Entries.Add(Entry(TEXT("User.Native.Weights"),EMCVFXNativeInputType::FloatArray));P.Entries.Last().FloatValues={.1f,.3f,.8f};
    P.Entries.Add(Entry(TEXT("User.Native.Points"),EMCVFXNativeInputType::VectorArray));
    for(int32 I=0;I<63;++I)P.Entries.Last().VectorValues.Add(FVector(I*.125,I*.25,-I));
    const FNiagaraTypeDefinition Types[]={FNiagaraTypeDefinition::GetFloatDef(),FNiagaraTypeDefinition::GetIntDef(),FNiagaraTypeDefinition::GetBoolDef(),
        FNiagaraTypeDefinition::GetColorDef(),FNiagaraTypeDefinition::GetVec3Def(),FNiagaraTypeDefinition::GetPositionDef(),FNiagaraTypeDefinition::GetQuatDef(),
        FNiagaraTypeDefinition(UNiagaraDataInterfaceArrayFloat::StaticClass()),FNiagaraTypeDefinition(UNiagaraDataInterfaceArrayFloat3::StaticClass())};
    for(int32 I=0;I<P.Entries.Num();++I)Declare(S,P.Entries[I].Name,Types[I]);
    auto* C=NewObject<UNiagaraComponent>();C->SetAutoActivate(false);C->SetAsset(S);FMCVFXNativeInputsApplyState State;int32 Uploads=-1;
    if(!TestTrue(TEXT("Apply exact native data through the actual component APIs before activation"),MCVFXNativeInputs::Apply(C,P,Error,&State,&Uploads)))
    {AddError(Error);return false;}
    TestEqual(TEXT("Two declared arrays upload once"),Uploads,2);
    const auto& Store=C->GetOverrideParameters();
    TestEqual(TEXT("Actual float setter retains cue age"),Store.GetParameterValue<float>(FNiagaraVariable(Types[0],P.Entries[0].Name)),.25f);
    TestEqual(TEXT("Actual int setter retains count"),Store.GetParameterValue<int32>(FNiagaraVariable(Types[1],P.Entries[1].Name)),63);
    TestTrue(TEXT("Actual bool setter keeps Niagara's true mask"),Store.GetParameterValue<FNiagaraBool>(FNiagaraVariable(Types[2],P.Entries[2].Name)).GetValue());
    TestTrue(TEXT("Actual color setter retains all channels"),Store.GetParameterValue<FLinearColor>(FNiagaraVariable(Types[3],P.Entries[3].Name)).Equals(P.Entries[3].ColorValue));
    TestTrue(TEXT("Actual vector setter retains its vector semantics"),Store.GetParameterValue<FVector3f>(FNiagaraVariable(Types[4],P.Entries[4].Name)).Equals(FVector3f(0,0,1)));
    const auto* Position=Store.GetPositionParameterValue(P.Entries[5].Name);
    if(TestNotNull(TEXT("Position setter uses the true double position store"),Position))TestEqual(TEXT("Subunit precision at a huge world coordinate is retained"),Position->X,P.Entries[5].PositionValue.X);
    TestTrue(TEXT("Actual quaternion setter retains the captured rotation"),Store.GetParameterValue<FQuat4f>(FNiagaraVariable(Types[6],P.Entries[6].Name)).Equals(FQuat4f(P.Entries[6].QuaternionValue)));
    TestEqual(TEXT("Actual Float DI receives authored array data"),UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayFloat(C,P.Entries[7].Name).Num(),3);
    const auto Points=UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayVector(C,P.Entries[8].Name);
    if(TestEqual(TEXT("Actual Vector DI retains all 63 points"),Points.Num(),63))TestTrue(TEXT("A distant vector point was not replaced by a Position conversion"),Points.Last().Equals(P.Entries[8].VectorValues.Last()));
    P.Entries[0].FloatValue=.5f;
    if(!TestTrue(TEXT("A new cue clock reuses unchanged arrays"),MCVFXNativeInputs::Apply(C,P,Error,&State,&Uploads)))return false;
    TestEqual(TEXT("Static 63-point data is not uploaded every tick"),Uploads,0);
    P.Entries[8].VectorValues[7].X+=1;
    if(!TestTrue(TEXT("A changed array entry updates independently"),MCVFXNativeInputs::Apply(C,P,Error,&State,&Uploads)))return false;
    TestEqual(TEXT("Only the changed vector array uploads"),Uploads,1);
    const auto Previous=State.Previous;P.Entries[0].FloatValue=.75f;P.Entries[8].Type=EMCVFXNativeInputType::PositionArray;P.Entries[8].VectorValues.Reset();
    TestFalse(TEXT("Wrong array type fails before updating an otherwise valid cue clock"),MCVFXNativeInputs::Apply(C,P,Error,&State,&Uploads));
    TestEqual(TEXT("Failed preflight leaves live scalar unchanged"),Store.GetParameterValue<float>(FNiagaraVariable(Types[0],P.Entries[0].Name)),.5f);
    TestEqual(TEXT("Failed preflight preserves previous cache"),State.Previous.Entries[0].FloatValue,Previous.Entries[0].FloatValue);
    const auto* SourceArray=Cast<UNiagaraDataInterfaceArrayFloat3>(S->GetExposedParameters().GetDataInterface(
        S->GetExposedParameters().IndexOf(FNiagaraVariable(Types[8],TEXT("User.Native.Points")))));
    if(TestNotNull(TEXT("Source template array still exists"),SourceArray))TestTrue(TEXT("Component updates never mutate the NS source DI"),SourceArray->GetArrayReference().IsEmpty());
    // A same-class DI from a different live component is still not our storage.
    // Reject it before updating a valid scalar or touching that foreign proxy.
    auto* Other=NewObject<UNiagaraComponent>();Other->SetAutoActivate(false);Other->SetAsset(S);
    auto* Foreign=UNiagaraFunctionLibrary::GetDataInterface<UNiagaraDataInterfaceArrayFloat3>(Other,TEXT("User.Native.Points"));
    if(!TestNotNull(TEXT("A distinct component owns its own vector DI"),Foreign))return false;
    const FNiagaraVariable VectorKey(Types[8],TEXT("User.Native.Points"));
    C->GetOverrideParameters().SetDataInterface(Foreign,C->GetOverrideParameters().IndexOf(VectorKey));
    P=Previous;P.Entries[0].FloatValue=.75f;
    TestFalse(TEXT("Another component's DI cannot be borrowed or mutated by native inputs"),MCVFXNativeInputs::Apply(C,P,Error,&State,&Uploads));
    TestEqual(TEXT("Foreign-DI rejection occurs before scalar writes"),Store.GetParameterValue<float>(FNiagaraVariable(Types[0],P.Entries[0].Name)),.5f);
    TestTrue(TEXT("The foreign DI remains untouched"),UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayVector(Other,TEXT("User.Native.Points")).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXNativePositionArray,"MessControl.VFX.NativeInputs.PositionArrayPreactivationTile",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXNativePositionArray::RunTest(const FString&)
{
    using namespace MCVFXNativeInputsTests;auto* S=NewObject<UNiagaraSystem>();FString Error;
    const FName Name(TEXT("User.Native.GroundPoints"));Declare(S,Name,FNiagaraTypeDefinition(UNiagaraDataInterfaceArrayPosition::StaticClass()));
    if(!TestTrue(TEXT("This fixture uses the engine's real LWC system mode"),S->SupportsLargeWorldCoordinates()))return false;
    auto* C=NewObject<UNiagaraComponent>();C->SetAutoActivate(false);C->SetWorldLocation(FVector(8*FLargeWorldRenderScalar::GetTileSize()+123,-456,900));C->SetAsset(S);
    FMCVFXNativeInputs P;P.Entries.Add(Entry(TEXT("User.Native.GroundPoints"),EMCVFXNativeInputType::PositionArray));
    P.Entries[0].PositionValues={C->GetComponentLocation()+FVector(.125,.25,.5),C->GetComponentLocation()+FVector(63.125,-7.25,4.5)};
    FMCVFXNativeInputsApplyState State;int32 Uploads=0;
    if(!TestTrue(TEXT("Actual Position DI setter prepares data before activation at a nonzero tile"),MCVFXNativeInputs::Apply(C,P,Error,&State,&Uploads)))
    {AddError(Error);return false;}
    TestEqual(TEXT("The position array is uploaded once"),Uploads,1);
    const FVector ExpectedOrigin=FVector(FLargeWorldRenderScalar::GetTileFor(C->GetComponentLocation()))*FLargeWorldRenderScalar::GetTileSize();
    TestTrue(TEXT("Prepared origin equals the exact Niagara initial-reset tile rule"),State.LWCOrigin.Equals(ExpectedOrigin,0));
    // With no controller the stock getter also has a zero converter. Its real
    // readback is simulation data; reconstruct using the verified upcoming tile.
    auto Readback=UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayPosition(C,Name);
    if(!TestEqual(TEXT("Actual Position DI returns both stored points"),Readback.Num(),2))return false;
    for(int32 I=0;I<2;++I)TestTrue(TEXT("LWC rebasing happens before float conversion; fractions survive"),(Readback[I]+State.LWCOrigin).Equals(P.Entries[0].PositionValues[I],.0001));
    if(!TestTrue(TEXT("Unchanged tile and payload reuse position data"),MCVFXNativeInputs::Apply(C,P,Error,&State,&Uploads)))return false;
    TestEqual(TEXT("No redundant position upload"),Uploads,0);
    C->SetWorldLocation(C->GetComponentLocation()+FVector(FLargeWorldRenderScalar::GetTileSize(),0,0));
    if(!TestTrue(TEXT("Moving to another tile rebases the same accepted world points"),MCVFXNativeInputs::Apply(C,P,Error,&State,&Uploads)))return false;
    TestEqual(TEXT("A tile transition requires one explicit position upload"),Uploads,1);
    Readback=UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayPosition(C,Name);
    for(int32 I=0;I<2;++I)TestTrue(TEXT("The world endpoints stay fixed after rebasing"),(Readback[I]+State.LWCOrigin).Equals(P.Entries[0].PositionValues[I],.13));
    return true;
}
#endif
