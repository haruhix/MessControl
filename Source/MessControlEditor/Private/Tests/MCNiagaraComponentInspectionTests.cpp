#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCPerkPreviewEditorLibrary.h"
#include "MCPerkVFXCompositionPlayer.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "MCVFXTestWorld.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFloat.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include <limits>

namespace MCNiagaraComponentInspectionTests
{
TSharedPtr<FJsonObject> Parse(const FString& Text)
{TSharedPtr<FJsonObject> Result;const auto Reader=TJsonReaderFactory<>::Create(Text);return FJsonSerializer::Deserialize(Reader,Result)?Result:nullptr;}
TSharedPtr<FJsonObject> Find(const TSharedPtr<FJsonObject>& Root,const TCHAR* Array,FName Name)
{
    if(!Root)return nullptr;
    for(const auto& Value:Root->GetArrayField(Array)) {
        auto Row=Value->AsObject();if(Row.IsValid()&&FName(*Row->GetStringField(TEXT("name")))==Name)return Row;
    }
    return nullptr;
}
struct FWorld
{
    UWorld* World=MCVFXTestWorld::Create(EWorldType::EditorPreview,false);
    FWorld(){if(World&&GEngine)GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);}
    ~FWorld(){if(World){World->DestroyWorld(false);if(GEngine)GEngine->DestroyWorldContext(World);}}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNiagaraInspectionFalseIsPresent,
    "MessControl.VFX.EditorInspection.FalseMissingAndInvalidEncoding",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNiagaraInspectionFalseIsPresent::RunTest(const FString&)
{
    using namespace MCNiagaraComponentInspectionTests;
    auto* Component=NewObject<UNiagaraComponent>(GetTransientPackage());
    auto& Store=Component->GetOverrideParameters();
    const FName FalseName(TEXT("User.EchoOverhead")),MissingName(TEXT("User.Native.Missing")),BadName(TEXT("User.Native.BadBool")),NanName(TEXT("User.Native.NotFinite"));
    Store.SetParameterValue(FNiagaraBool(false),FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(),FalseName),true);
    FNiagaraBool Bad;Bad.SetRawValue(2);
    Store.SetParameterValue(Bad,FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(),BadName),true);
    Store.SetParameterValue(std::numeric_limits<float>::quiet_NaN(),FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),NanName),true);
    const int32 Before=Store.ReadParameterVariables().Num();const bool DirtyBefore=Component->GetOutermost()->IsDirty();
    FString Error;auto Root=Parse(UMCPerkPreviewEditorLibrary::GetNiagaraComponentParameterInspection(Component,{FalseName,MissingName,BadName,NanName},Error));
    if(!TestTrue(TEXT("Typed inspector returns JSON without an out-error"),Error.IsEmpty()&&Root.IsValid()))return false;
    auto False=Find(Root,TEXT("parameters"),FalseName);auto Missing=Find(Root,TEXT("parameters"),MissingName);
    auto Invalid=Find(Root,TEXT("parameters"),BadName);auto Nan=Find(Root,TEXT("parameters"),NanName);
    if(!TestTrue(TEXT("All requested rows exist including missing/invalid ones"),False.IsValid()&&Missing.IsValid()&&Invalid.IsValid()&&Nan.IsValid()))return false;
    TestTrue(TEXT("False is a present valid typed value"),False->GetBoolField(TEXT("present"))&&False->GetBoolField(TEXT("valid")));
    TestFalse(TEXT("False survives FString JSON return without Python bool-return elision"),False->GetBoolField(TEXT("value")));
    TestEqual(TEXT("Its actual exact type is NiagaraBool"),False->GetStringField(TEXT("type")),FNiagaraTypeDefinition::GetBoolDef().GetStruct()->GetPathName());
    TestFalse(TEXT("Missing is explicitly absent"),Missing->GetBoolField(TEXT("present")));
    TestFalse(TEXT("Missing is not promoted to a valid false/default"),Missing->GetBoolField(TEXT("valid")));
    TestTrue(TEXT("Missing has null value, not a fabricated bool"),Missing->TryGetField(TEXT("value"))->IsNull());
    TestTrue(TEXT("Noncanonical raw bool is present but invalid"),Invalid->GetBoolField(TEXT("present"))&&!Invalid->GetBoolField(TEXT("valid")));
    TestTrue(TEXT("Nonfinite scalar is null and invalid; JSON never receives NaN"),!Nan->GetBoolField(TEXT("valid"))&&Nan->TryGetField(TEXT("value"))->IsNull());
    TestEqual(TEXT("Inspection adds/removes no store entries"),Store.ReadParameterVariables().Num(),Before);
    TestFalse(TEXT("The actual stored bool remains false"),Store.GetParameterValue<FNiagaraBool>(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(),FalseName)).GetValue());
    TestEqual(TEXT("No package dirty-state mutation"),Component->GetOutermost()->IsDirty(),DirtyBefore);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNiagaraInspectionRetainedMeshMismatch,
    "MessControl.VFX.EditorInspection.RetainedCapturedMeshMismatch",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNiagaraInspectionRetainedMeshMismatch::RunTest(const FString&)
{
    using namespace MCNiagaraComponentInspectionTests;
    if(GEditor&&GEditor->PlayWorld){AddError(TEXT("Run outside PIE; only a disposable EditorPreview world is used."));return false;}
    FWorld Fixture;if(!TestNotNull(TEXT("Disposable world"),Fixture.World))return false;
    auto* Player=Fixture.World->SpawnActor<AMCPerkVFXCompositionPlayer>();
    if(!TestNotNull(TEXT("Actual composition player"),Player))return false;
    auto* Captured=NewObject<UStaticMesh>(GetTransientPackage(),NAME_None,RF_Transient);
    auto* Current=NewObject<UStaticMesh>(GetTransientPackage(),NAME_None,RF_Transient);
    auto* Recipe=NewObject<UMCVFXComposition>();FMCVFXCompositionLayer Layer;Layer.StartSeconds=10.f;Recipe->Layers.Add(Layer);
    FMCPreparedPerkVFXComposition Prepared;Prepared.Snapshot=Recipe;Prepared.Systems.Add(NewObject<UNiagaraSystem>());
    FMCPerkVFXGameplayValues Values;FMCEchoToolPart Part;Part.Mesh=Captured;Part.ContactPose=FTransform(FQuat::Identity,FVector(7,11,13));Values.CapturedMeshes.Add(Part);
    Values.bCapturedMeshOverhead=false;Values.CapturedContactAt=.25;
    FString Error;
    if(!TestTrue(TEXT("Real StartPrepared retains the accepted mesh without activating its delayed layer"),Player->StartPrepared(Prepared,FTransform::Identity,nullptr,nullptr,false,Values,Error))) {AddError(Error);return false;}
    auto* Component=NewObject<UNiagaraComponent>(Player,NAME_None,RF_Transient);Component->SetAutoActivate(false);Player->AddInstanceComponent(Component);
    auto* CurrentTool=NewObject<UStaticMeshComponent>(Player);Player->AddInstanceComponent(CurrentTool);CurrentTool->SetStaticMesh(Current);
    auto& Store=Component->GetOverrideParameters();const FName MeshName(TEXT("User.ToolMesh0")),NullMeshName(TEXT("User.ToolMesh1")),OverheadName(TEXT("User.EchoOverhead"));
    const FNiagaraVariable MeshParameter(FNiagaraTypeDefinition(UStaticMesh::StaticClass()),MeshName);
    const FNiagaraVariable NullMeshParameter(FNiagaraTypeDefinition(UStaticMesh::StaticClass()),NullMeshName);
    Store.AddParameter(MeshParameter,true);Store.SetUObject(Captured,MeshParameter);
    Store.AddParameter(NullMeshParameter,true);Store.SetUObject(nullptr,NullMeshParameter);
    Store.SetParameterValue(FNiagaraBool(false),FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(),OverheadName),true);
    auto Root=Parse(UMCPerkPreviewEditorLibrary::GetNiagaraComponentParameterInspection(Component,{MeshName,NullMeshName,OverheadName},Error));
    if(!TestTrue(TEXT("Read-only captured comparison returns JSON"),Root.IsValid()&&Error.IsEmpty()))return false;
    auto MeshRow=Find(Root,TEXT("parameters"),MeshName);auto NullMeshRow=Find(Root,TEXT("parameters"),NullMeshName);
    if(!TestTrue(TEXT("Both actual StaticMesh slots are present"),MeshRow.IsValid()&&NullMeshRow.IsValid()))return false;
    TestEqual(TEXT("The declared StaticMesh type is explicit, not an empty struct type"),MeshRow->GetStringField(TEXT("type")),FString(TEXT("/Script/Engine.StaticMesh")));
    TestEqual(TEXT("A valid null slot retains that exact StaticMesh type"),NullMeshRow->GetStringField(TEXT("type")),FString(TEXT("/Script/Engine.StaticMesh")));
    TestTrue(TEXT("Null object is a present valid slot, not a missing parameter"),NullMeshRow->GetBoolField(TEXT("present"))&&NullMeshRow->GetBoolField(TEXT("valid"))&&NullMeshRow->GetObjectField(TEXT("value"))->GetBoolField(TEXT("is_null")));
    auto Capture=Root->GetObjectField(TEXT("capture"));auto Match=Find(Capture,TEXT("comparisons"),MeshName);
    if(!TestTrue(TEXT("Actual retained source is available"),Match.IsValid()&&Capture->GetBoolField(TEXT("available"))))return false;
    TestTrue(TEXT("Captured mesh binding matches the accepted retained part"),Match->GetBoolField(TEXT("matches")));
    TestEqual(TEXT("Current tool replacement does not substitute the captured source"),Match->GetObjectField(TEXT("expected"))->GetObjectField(TEXT("value"))->GetStringField(TEXT("path")),Captured->GetPathName());
    TestFalse(TEXT("An auxiliary unregistered probe is never certified as the player's managed runtime layer"),Capture->GetBoolField(TEXT("managed_layer_component")));
    TestFalse(TEXT("Partial fields/unmanaged probe never claim full runtime-capture acceptance"),Capture->GetBoolField(TEXT("runtime_capture_match_verified")));
    Store.SetUObject(Current,MeshParameter);
    Root=Parse(UMCPerkPreviewEditorLibrary::GetNiagaraComponentParameterInspection(Component,{MeshName,NullMeshName,OverheadName},Error));
    if(!TestTrue(TEXT("Mismatched binding is inspectable without a failure/fallback"),Root.IsValid()&&Error.IsEmpty()))return false;
    Match=Find(Root->GetObjectField(TEXT("capture")),TEXT("comparisons"),MeshName);
    if(!TestTrue(TEXT("The mismatched comparison row remains present"),Match.IsValid()))return false;
    TestFalse(TEXT("A real different StaticMesh override is reported as a captured mismatch"),Match->GetBoolField(TEXT("matches")));
    TestEqual(TEXT("Actual override identifies the mismatched object"),Match->GetObjectField(TEXT("actual"))->GetObjectField(TEXT("value"))->GetStringField(TEXT("path")),Current->GetPathName());
    Player->StopEmission(true);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNiagaraInspectionBoundsAndLWC,
    "MessControl.VFX.EditorInspection.BoundedNamesAndWorldPosition",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNiagaraInspectionBoundsAndLWC::RunTest(const FString&)
{
    using namespace MCNiagaraComponentInspectionTests;
    auto* Component=NewObject<UNiagaraComponent>();auto& Store=Component->GetOverrideParameters();
    const FName PositionName(TEXT("User.Native.Endpoint")),ArrayName(TEXT("User.Native.Samples"));
    const FVector WorldPosition(10000000000.25,-20000000000.5,30000000000.75);
    if(!TestTrue(TEXT("Actual OriginalPositionData stores the world/LWC value"),Store.SetPositionParameterValue(WorldPosition,PositionName,true)))return false;
    auto* Array=NewObject<UNiagaraDataInterfaceArrayFloat>(Component);Array->FloatData.Init(99.f,4096);
    const FNiagaraVariable ArrayParameter(FNiagaraTypeDefinition(UNiagaraDataInterfaceArrayFloat::StaticClass()),ArrayName);
    Store.AddParameter(ArrayParameter,true);Store.SetDataInterface(Array,ArrayParameter);
    FString Error;auto Root=Parse(UMCPerkPreviewEditorLibrary::GetNiagaraComponentParameterInspection(Component,{PositionName,ArrayName},Error));
    if(!TestTrue(TEXT("World-position and reference-only DI readback"),Root.IsValid()&&Error.IsEmpty()))return false;
    auto Position=Find(Root,TEXT("parameters"),PositionName);
    if(!TestTrue(TEXT("Actual world-position row is present and valid"),Position.IsValid()&&Position->GetBoolField(TEXT("valid"))))return false;
    const auto& Coordinates=Position->GetArrayField(TEXT("value"));
    TestTrue(TEXT("Position readback uses original double world data, not packed tile-relative FVector3f"),Coordinates.Num()==3&&FMath::IsNearlyEqual(Coordinates[0]->AsNumber(),WorldPosition.X,.0001)&&FMath::IsNearlyEqual(Coordinates[1]->AsNumber(),WorldPosition.Y,.0001)&&FMath::IsNearlyEqual(Coordinates[2]->AsNumber(),WorldPosition.Z,.0001));
    auto Interface=Find(Root,TEXT("parameters"),ArrayName);
    if(!TestTrue(TEXT("The actual DI reference row is present"),Interface.IsValid()))return false;
    TestTrue(TEXT("DI presence/type is real, contents remain explicitly unobserved"),Interface->GetBoolField(TEXT("present"))&&Interface->GetBoolField(TEXT("valid"))&&Interface->GetBoolField(TEXT("reference_only")));
    TestEqual(TEXT("The actual DI class is serialized as its declared type"),Interface->GetStringField(TEXT("type")),FString(TEXT("/Script/Niagara.NiagaraDataInterfaceArrayFloat")));
    TestEqual(TEXT("The DI backing array is neither copied into JSON nor modified"),Array->FloatData.Num(),4096);
    TArray<FName> Excess;for(int32 Index=0;Index<65;++Index)Excess.Add(FName(*FString::Printf(TEXT("User.Native.Input%d"),Index)));
    TestTrue(TEXT("An oversized request is rejected before traversal"),UMCPerkPreviewEditorLibrary::GetNiagaraComponentParameterInspection(Component,Excess,Error).IsEmpty()&&!Error.IsEmpty());
    const int32 Count=Store.ReadParameterVariables().Num();
    TestTrue(TEXT("FName case aliases cannot silently duplicate a query"),UMCPerkPreviewEditorLibrary::GetNiagaraComponentParameterInspection(Component,{TEXT("User.Native.Endpoint"),TEXT("user.native.endpoint")},Error).IsEmpty()&&!Error.IsEmpty());
    TestEqual(TEXT("Rejected queries preserve actual parameter membership"),Store.ReadParameterVariables().Num(),Count);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNiagaraInspectionPersistentAcceptedPayload,
    "MessControl.VFX.EditorInspection.PersistentAcceptedFloatPayload",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNiagaraInspectionPersistentAcceptedPayload::RunTest(const FString&)
{
    using namespace MCNiagaraComponentInspectionTests;
    if(GEditor&&GEditor->PlayWorld){AddError(TEXT("Run outside PIE; only a disposable EditorPreview world is used."));return false;}
    FWorld Fixture;if(!TestNotNull(TEXT("Disposable world"),Fixture.World))return false;
    auto* Player=Fixture.World->SpawnActor<AMCPerkVFXCompositionPlayer>();auto* Owner=Fixture.World->SpawnActor<AActor>();
    if(!TestTrue(TEXT("Actual player and persistent owner"),IsValid(Player)&&IsValid(Owner)))return false;
    auto* Recipe=NewObject<UMCVFXComposition>();FMCVFXCompositionLayer Layer;Layer.StartSeconds=10.f;Recipe->Layers.Add(Layer);
    FMCPreparedPerkVFXComposition Prepared;Prepared.Snapshot=Recipe;Prepared.Systems.Add(NewObject<UNiagaraSystem>());
    const FName Radius(TEXT("User.Native.CoatingRadius")),Remaining(TEXT("User.Native.CoatingRemainingSeconds")),Missing(TEXT("User.Native.Unaccepted")),Samples(TEXT("User.Native.Samples"));
    FMCPerkVFXGameplayValues Values;FMCVFXNativeInput Input;Input.Name=Radius;Input.FloatValue=123.5f;Values.NativeInputs.Entries.Add(Input);
    Input.Name=Remaining;Input.FloatValue=0.f;Values.NativeInputs.Entries.Add(Input);
    Input.Name=Samples;Input.Type=EMCVFXNativeInputType::FloatArray;Input.FloatValues={1.f,2.f};Values.NativeInputs.Entries.Add(Input);
    FString Error;
    if(!TestTrue(TEXT("Real persistent StartPrepared retains accepted values without activating the delayed layer"),Player->StartPrepared(Prepared,FTransform::Identity,nullptr,nullptr,false,Values,Error,0.f,Owner))) {AddError(Error);return false;}
    auto* Component=NewObject<UNiagaraComponent>(Player,NAME_None,RF_Transient);Component->SetAutoActivate(false);Player->AddInstanceComponent(Component);
    auto& Store=Component->GetOverrideParameters();const FNiagaraVariable RadiusParameter(FNiagaraTypeDefinition::GetFloatDef(),Radius);
    const FNiagaraVariable RemainingParameter(FNiagaraTypeDefinition::GetFloatDef(),Remaining);
    Store.SetParameterValue(123.5f,RadiusParameter,true);Store.SetParameterValue(0.f,RemainingParameter,true);
    auto Root=Parse(UMCPerkPreviewEditorLibrary::GetNiagaraComponentParameterInspection(Component,{Radius,Remaining},Error));
    if(!TestTrue(TEXT("Persistent accepted-payload inspection returns JSON"),Root.IsValid()&&Error.IsEmpty()))return false;
    auto Native=Root->GetObjectField(TEXT("native_payload"));auto OwnerJson=Native->GetObjectField(TEXT("persistent_owner"));
    TestEqual(TEXT("Exact persistent owner identity"),OwnerJson->GetStringField(TEXT("path")),Owner->GetPathName());
    TestEqual(TEXT("Exact persistent owner class"),OwnerJson->GetStringField(TEXT("class")),Owner->GetClass()->GetPathName());
    TestTrue(TEXT("Owner is a valid actual same-world persistent actor"),OwnerJson->GetBoolField(TEXT("object_valid"))&&Native->GetBoolField(TEXT("persistent_state_enabled"))&&Native->GetBoolField(TEXT("owner_same_world")));
    TestEqual(TEXT("Comparison explicitly describes retained accepted payload"),Native->GetStringField(TEXT("comparison_scope")),FString(TEXT("accepted_payload")));
    TestFalse(TEXT("Actor expiry is never independently sampled"),Native->GetBoolField(TEXT("independent_actor_expiry_sampled")));
    TestTrue(TEXT("Both requested Float overrides match, including a real zero"),Native->GetBoolField(TEXT("requested_float_fields_match")));
    TestFalse(TEXT("Auxiliary component is never promoted to a managed persistent layer"),Native->GetBoolField(TEXT("managed_layer_component")));
    TestFalse(TEXT("Payload matching alone is not managed runtime acceptance"),Native->GetBoolField(TEXT("managed_persistent_accepted_payload_match")));
    TestEqual(TEXT("Actual inspected component identity"),Native->GetObjectField(TEXT("component"))->GetStringField(TEXT("path")),Component->GetPathName());
    Store.SetParameterValue(123.75f,RadiusParameter,true);Store.RemoveParameter(RemainingParameter);
    Store.SetParameterValue(0.f,FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),Missing),true);
    Root=Parse(UMCPerkPreviewEditorLibrary::GetNiagaraComponentParameterInspection(Component,{Radius,Remaining,Missing,Samples},Error));
    if(!TestTrue(TEXT("Mismatch, missing, and unobserved array remain inspectable"),Root.IsValid()&&Error.IsEmpty()))return false;
    Native=Root->GetObjectField(TEXT("native_payload"));auto RadiusRow=Find(Native,TEXT("comparisons"),Radius);auto RemainingRow=Find(Native,TEXT("comparisons"),Remaining);
    auto MissingRow=Find(Native,TEXT("comparisons"),Missing);auto ArrayRow=Find(Native,TEXT("comparisons"),Samples);
    if(!TestTrue(TEXT("Each requested native comparison has an explicit row"),RadiusRow.IsValid()&&RemainingRow.IsValid()&&MissingRow.IsValid()&&ArrayRow.IsValid()))return false;
    TestFalse(TEXT("Different actual Float is a mismatch"),RadiusRow->GetBoolField(TEXT("matches")));
    TestFalse(TEXT("Missing actual Float is not synthesized from accepted zero"),RemainingRow->GetObjectField(TEXT("actual"))->GetBoolField(TEXT("present")));
    TestTrue(TEXT("Accepted zero stays valid despite the missing actual override"),RemainingRow->GetObjectField(TEXT("expected"))->GetBoolField(TEXT("valid"))&&RemainingRow->GetObjectField(TEXT("expected"))->GetNumberField(TEXT("value"))==0.);
    TestFalse(TEXT("Unaccepted actual zero has no fabricated accepted payload entry"),MissingRow->GetObjectField(TEXT("expected"))->GetBoolField(TEXT("present")));
    TestTrue(TEXT("Missing accepted entry retains null value"),MissingRow->GetObjectField(TEXT("expected"))->TryGetField(TEXT("value"))->IsNull());
    TestTrue(TEXT("Array entry presence is known without inspecting its contents"),ArrayRow->GetObjectField(TEXT("expected"))->GetBoolField(TEXT("present"))&&!ArrayRow->GetBoolField(TEXT("comparison_supported")));
    TestFalse(TEXT("Non-Float/missing/mismatch cannot yield aggregate acceptance"),Native->GetBoolField(TEXT("requested_float_fields_match")));
    TestFalse(TEXT("Array contents remain explicitly unobserved"),Native->GetBoolField(TEXT("array_contents_observed")));
    Player->StopEmission(true);return true;
}
#endif
