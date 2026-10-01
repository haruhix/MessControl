#include "MCProjectIndexCommandlet.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "K2Node_CallFunction.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, MessControlProjectIndex)
DEFINE_LOG_CATEGORY_STATIC(LogMCProjectIndex, Log, All);

namespace MCProjectIndex
{
constexpr int32 MaxCollectionItems = 256;
constexpr int32 MaxTextChars = 32768;

TSharedPtr<FJsonValue> String(const FString& Value) { return MakeShared<FJsonValueString>(Value); }

bool WriteJson(const FString& Path, const TSharedRef<FJsonObject>& Json)
{
    FString Text;
    const auto Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text);
    if (!FJsonSerializer::Serialize(Json, Writer)) return false;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    const FString Temp = Path + TEXT(".tmp");
    if (!FFileHelper::SaveStringToFile(Text, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return false;
    return IFileManager::Get().Move(*Path, *Temp, true, true);
}

FString ObjectPath(const UObject* Object) { return Object ? Object->GetPathName() : FString(); }

// UObject references remain paths: recursively serializing them would follow cycles and load the whole engine.
TSharedPtr<FJsonValue> ExportSpecialProperty(FProperty* Property, const void* Value)
{
    if (const auto* SoftProperty = CastField<FSoftObjectProperty>(Property))
        return String(SoftProperty->GetPropertyValue(Value).ToSoftObjectPath().ToString());
    if (const auto* ObjectProperty = CastField<FObjectPropertyBase>(Property))
        return String(ObjectPath(ObjectProperty->GetObjectPropertyValue(Value)));
    if (const auto* ArrayProperty = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper Array(ArrayProperty, Value);
        if (Array.Num() > MaxCollectionItems)
        {
            auto Summary = MakeShared<FJsonObject>();
            Summary->SetBoolField(TEXT("truncated"), true);
            Summary->SetNumberField(TEXT("count"), Array.Num());
            Summary->SetStringField(TEXT("reason"), TEXT("collection exceeds 256 items"));
            return MakeShared<FJsonValueObject>(Summary);
        }
    }
    if (const auto* MapProperty = CastField<FMapProperty>(Property))
    {
        FScriptMapHelper Map(MapProperty, Value);
        if (Map.Num() > MaxCollectionItems)
        {
            auto Summary = MakeShared<FJsonObject>();
            Summary->SetBoolField(TEXT("truncated"), true);
            Summary->SetNumberField(TEXT("count"), Map.Num());
            return MakeShared<FJsonValueObject>(Summary);
        }
    }
    if (const auto* SetProperty = CastField<FSetProperty>(Property))
    {
        FScriptSetHelper Set(SetProperty, Value);
        if (Set.Num() > MaxCollectionItems)
        {
            auto Summary = MakeShared<FJsonObject>();
            Summary->SetBoolField(TEXT("truncated"), true);
            Summary->SetNumberField(TEXT("count"), Set.Num());
            return MakeShared<FJsonValueObject>(Summary);
        }
    }
    return nullptr; // Let the engine's converter handle scalar and reflected struct values.
}

TSharedRef<FJsonObject> Properties(UObject* Object)
{
    auto Result = MakeShared<FJsonObject>();
    auto Types = MakeShared<FJsonObject>();
    auto Values = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Skipped;
    const auto Callback = FJsonObjectConverter::CustomExportCallback::CreateStatic(&ExportSpecialProperty);
    for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
    {
        FProperty* Property = *It;
        if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_SkipSerialization)) continue;
        const FString Name = Property->GetName();
        const void* Address = Property->ContainerPtrToValuePtr<void>(Object);
        Types->SetStringField(Name, Property->GetCPPType());
        // ExportText is also retained for fields whose native representation is opaque to JSON.
        FString Text;
        Property->ExportTextItem_Direct(Text, Address, nullptr, Object, PPF_None);
        if (Text.Len() > MaxTextChars)
        {
            Skipped.Add(String(Name + TEXT(": text truncated")));
            Text = Text.Left(MaxTextChars);
        }
        auto Field = MakeShared<FJsonObject>();
        Field->SetStringField(TEXT("text"), Text);
        auto Value = FJsonObjectConverter::UPropertyToJsonValue(Property, Address, 0,
            CPF_Transient | CPF_Deprecated | CPF_SkipSerialization, &Callback);
        if (Value.IsValid()) Field->SetField(TEXT("value"), Value);
        else Skipped.Add(String(Name + TEXT(": JSON unavailable; inspect text")));
        Values->SetObjectField(Name, Field);
    }
    Result->SetObjectField(TEXT("types"), Types);
    Result->SetObjectField(TEXT("fields"), Values);
    Result->SetArrayField(TEXT("diagnostics"), Skipped);
    return Result;
}

TSharedRef<FJsonObject> Graph(UEdGraph* GraphObject)
{
    auto Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path"), ObjectPath(GraphObject));
    Result->SetStringField(TEXT("class"), ObjectPath(GraphObject->GetClass()));
    TArray<TSharedPtr<FJsonValue>> Nodes;
    for (UEdGraphNode* Node : GraphObject->Nodes)
    {
        if (!Node) continue;
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("id"), Node->NodeGuid.ToString());
        Row->SetStringField(TEXT("path"), ObjectPath(Node));
        Row->SetStringField(TEXT("class"), ObjectPath(Node->GetClass()));
        Row->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
        Row->SetNumberField(TEXT("x"), Node->NodePosX);
        Row->SetNumberField(TEXT("y"), Node->NodePosY);
        Row->SetStringField(TEXT("comment"), Node->NodeComment);
        if (const auto* Call = Cast<UK2Node_CallFunction>(Node))
        {
            Row->SetStringField(TEXT("function"), Call->FunctionReference.GetMemberName().ToString());
            Row->SetStringField(TEXT("function_owner"), ObjectPath(Call->FunctionReference.GetMemberParentClass()));
            if (UFunction* Function = Call->GetTargetFunction())
            {
                Row->SetStringField(TEXT("resolved_function"), ObjectPath(Function));
                UClass* OwnerClass = Function->GetOuterUClass();
                Row->SetStringField(TEXT("cpp_function"), FString(OwnerClass->GetPrefixCPP()) + OwnerClass->GetName() + TEXT("::") + Function->GetName());
            }
        }
        Row->SetObjectField(TEXT("properties"), Properties(Node));
        TArray<TSharedPtr<FJsonValue>> Pins;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin) continue;
            auto P = MakeShared<FJsonObject>();
            P->SetStringField(TEXT("id"), Pin->PinId.ToString());
            P->SetStringField(TEXT("name"), Pin->PinName.ToString());
            P->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
            P->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
            P->SetStringField(TEXT("subcategory"), Pin->PinType.PinSubCategory.ToString());
            P->SetStringField(TEXT("type_object"), ObjectPath(Pin->PinType.PinSubCategoryObject.Get()));
            P->SetNumberField(TEXT("container_type"), static_cast<int32>(Pin->PinType.ContainerType));
            P->SetStringField(TEXT("default"), Pin->DefaultValue);
            P->SetStringField(TEXT("autogenerated_default"), Pin->AutogeneratedDefaultValue);
            P->SetStringField(TEXT("default_object"), ObjectPath(Pin->DefaultObject));
            P->SetStringField(TEXT("default_text"), Pin->DefaultTextValue.ToString());
            P->SetBoolField(TEXT("hidden"), Pin->bHidden);
            P->SetBoolField(TEXT("orphaned"), Pin->bOrphanedPin);
            TArray<TSharedPtr<FJsonValue>> Links;
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                if (!Linked || !Linked->GetOwningNode()) continue;
                auto Link = MakeShared<FJsonObject>();
                Link->SetStringField(TEXT("node"), Linked->GetOwningNode()->NodeGuid.ToString());
                Link->SetStringField(TEXT("pin"), Linked->PinId.ToString());
                Link->SetStringField(TEXT("graph"), ObjectPath(Linked->GetOwningNode()->GetGraph()));
                Links.Add(MakeShared<FJsonValueObject>(Link));
            }
            P->SetArrayField(TEXT("links"), Links);
            Pins.Add(MakeShared<FJsonValueObject>(P));
        }
        Row->SetArrayField(TEXT("pins"), Pins);
        Nodes.Add(MakeShared<FJsonValueObject>(Row));
    }
    Result->SetArrayField(TEXT("nodes"), Nodes);
    return Result;
}

TSharedRef<FJsonObject> CatalogRow(const FAssetData& Asset, IAssetRegistry& Registry)
{
    auto Row = MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("path"), Asset.GetSoftObjectPath().ToString());
    Row->SetStringField(TEXT("package"), Asset.PackageName.ToString());
    Row->SetStringField(TEXT("name"), Asset.AssetName.ToString());
    Row->SetStringField(TEXT("class"), Asset.AssetClassPath.ToString());
    FString Filename;
    FPackageName::DoesPackageExist(Asset.PackageName.ToString(), &Filename);
    Row->SetStringField(TEXT("file"), FPaths::ConvertRelativePathToFull(Filename));
    Row->SetStringField(TEXT("snapshot_file"), TEXT("snapshots") + Asset.PackageName.ToString() + TEXT(".") + Asset.AssetName.ToString() + TEXT(".json"));
    auto Tags = MakeShared<FJsonObject>();
    Asset.TagsAndValues.ForEach([&Tags](const TPair<FName, FAssetTagValueRef>& Tag) {
        Tags->SetStringField(Tag.Key.ToString(), Tag.Value.AsString());
    });
    Row->SetObjectField(TEXT("tags"), Tags);
    TArray<FName> Dependencies;
    Registry.GetDependencies(Asset.PackageName, Dependencies);
    TArray<TSharedPtr<FJsonValue>> Refs;
    for (FName Dependency : Dependencies) Refs.Add(String(Dependency.ToString()));
    Row->SetArrayField(TEXT("dependencies"), Refs);
    return Row;
}
}

UMCProjectIndexCommandlet::UMCProjectIndexCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
}

int32 UMCProjectIndexCommandlet::Main(const FString& Params)
{
    using namespace MCProjectIndex;
    FString Output = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ProjectIndex"));
    FParse::Value(*Params, TEXT("MCIndexOutput="), Output);
    Output = FPaths::ConvertRelativePathToFull(Output);
    FString RequestPath;
    FParse::Value(*Params, TEXT("MCIndexRequest="), RequestPath);
    TSet<FString> Requested;
    bool CatalogOnly = FParse::Param(*Params, TEXT("MCIndexCatalogOnly"));
    if (!RequestPath.IsEmpty())
    {
        FString Text;
        TSharedPtr<FJsonObject> Request;
        if (!FFileHelper::LoadFileToString(Text, *RequestPath) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Request)) return 2;
        const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
        if (Request->TryGetArrayField(TEXT("assets"), Paths))
            for (const auto& Path : *Paths) Requested.Add(Path->AsString());
    }
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.SearchAllAssets(true);
    TArray<FAssetData> Assets;
    Registry.GetAssetsByPath(FName(TEXT("/Game")), Assets, true);
    Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });
    auto Catalog = MakeShared<FJsonObject>();
    Catalog->SetNumberField(TEXT("schema_version"), 1);
    Catalog->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
    Catalog->SetStringField(TEXT("captured_utc"), FDateTime::UtcNow().ToIso8601());
    TArray<TSharedPtr<FJsonValue>> Rows;
    TArray<TSharedPtr<FJsonValue>> Errors;
    int32 Exported = 0;
    for (const FAssetData& Asset : Assets)
    {
        auto Row = CatalogRow(Asset, Registry);
        Rows.Add(MakeShared<FJsonValueObject>(Row));
        const FString Path = Asset.GetSoftObjectPath().ToString();
        if (CatalogOnly || (!RequestPath.IsEmpty() && !Requested.Contains(Path) && !Requested.Contains(Asset.PackageName.ToString()))) continue;
        UE_LOG(LogMCProjectIndex, Display, TEXT("Export %s"), *Path);
        UObject* Object = Asset.GetAsset();
        if (!Object)
        {
            Errors.Add(String(Path + TEXT(": could not load asset")));
            continue;
        }
        auto Snapshot = MakeShared<FJsonObject>();
        Snapshot->SetObjectField(TEXT("asset"), Row);
        Snapshot->SetNumberField(TEXT("schema_version"), 1);
        Snapshot->SetStringField(TEXT("cpp_class"), FString(Object->GetClass()->GetPrefixCPP()) + Object->GetClass()->GetName());
        Snapshot->SetStringField(TEXT("captured_utc"), FDateTime::UtcNow().ToIso8601());
        Snapshot->SetObjectField(TEXT("properties"), Properties(Object));
        if (auto* Blueprint = Cast<UBlueprint>(Object))
        {
            Snapshot->SetStringField(TEXT("parent_class"), ObjectPath(Blueprint->ParentClass));
            if (Blueprint->ParentClass)
                Snapshot->SetStringField(TEXT("cpp_parent_class"), FString(Blueprint->ParentClass->GetPrefixCPP()) + Blueprint->ParentClass->GetName());
            Snapshot->SetStringField(TEXT("generated_class"), ObjectPath(Blueprint->GeneratedClass));
            if (Blueprint->GeneratedClass)
                Snapshot->SetObjectField(TEXT("class_defaults"), Properties(Blueprint->GeneratedClass->GetDefaultObject()));
        }
        if (auto* Table = Cast<UDataTable>(Object))
        {
            TArray<TSharedPtr<FJsonValue>> TableRows;
            const FString TableJson = Table->GetTableAsJSON(EDataTableExportFlags::UseJsonObjectsForStructs);
            if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TableJson), TableRows))
                Snapshot->SetArrayField(TEXT("data_table"), TableRows);
            Snapshot->SetStringField(TEXT("row_struct"), ObjectPath(Table->GetRowStruct()));
        }
        TArray<UObject*> Objects;
        GetObjectsWithPackage(Object->GetPackage(), Objects, EGetObjectsFlags::IncludeNestedObjects);
        Objects.Sort([](const UObject& A, const UObject& B) { return A.GetPathName() < B.GetPathName(); });
        TArray<TSharedPtr<FJsonValue>> Graphs;
        TArray<TSharedPtr<FJsonValue>> Subobjects;
        for (UObject* Child : Objects)
        {
            if (auto* GraphObject = Cast<UEdGraph>(Child)) Graphs.Add(MakeShared<FJsonValueObject>(Graph(GraphObject)));
            else if (Child != Object && !Child->IsA<UClass>() && !Child->IsA<UFunction>() && !Child->IsA<UEdGraphNode>())
            {
                auto Entry = MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("path"), ObjectPath(Child));
                Entry->SetStringField(TEXT("class"), ObjectPath(Child->GetClass()));
                Entry->SetObjectField(TEXT("properties"), Properties(Child));
                Subobjects.Add(MakeShared<FJsonValueObject>(Entry));
            }
        }
        Snapshot->SetArrayField(TEXT("graphs"), Graphs);
        Snapshot->SetArrayField(TEXT("subobjects"), Subobjects);
        auto Coverage = MakeShared<FJsonObject>();
        Coverage->SetBoolField(TEXT("saved_package_only"), true);
        Coverage->SetBoolField(TEXT("native_binary_payloads_decoded"), false);
        Coverage->SetNumberField(TEXT("collection_limit"), MaxCollectionItems);
        Coverage->SetStringField(TEXT("limitations"), TEXT("Reflected properties and loaded package graphs only; transient fields, native non-reflected payloads, external module graphs and runtime/render results require separate inspection."));
        Snapshot->SetObjectField(TEXT("coverage"), Coverage);
        if (!WriteJson(FPaths::Combine(Output, Row->GetStringField(TEXT("snapshot_file"))), Snapshot)) Errors.Add(String(Path + TEXT(": write failed")));
        else ++Exported;
        if (Exported > 0 && Exported % 25 == 0) CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    }
    Catalog->SetArrayField(TEXT("assets"), Rows);
    Catalog->SetArrayField(TEXT("errors"), Errors);
    Catalog->SetNumberField(TEXT("exported"), Exported);
    if (!WriteJson(FPaths::Combine(Output, TEXT("catalog.json")), Catalog)) return 3;
    UE_LOG(LogMCProjectIndex, Display, TEXT("Indexed %d assets; exported %d; errors %d"), Rows.Num(), Exported, Errors.Num());
    return Errors.IsEmpty() ? 0 : 4;
}
