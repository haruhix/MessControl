#include "MCVFXMaterialToolset.h"

#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace
{
FString EncodeMaterialInputs(const TSharedRef<FJsonObject>& Result)
{
    FString Json;
    FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Json));
    return Json;
}

FString RejectMaterialInputs(const FString& Error)
{
    const auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("complete"), false);
    Result->SetStringField(TEXT("error"), Error);
    return EncodeMaterialInputs(Result);
}

const TCHAR* ValidateMaterialInputs(UMaterial* Material, UMaterialExpressionCustom* Expression,
    FName OwnerMetadataKey, const FString& ExpectedOwnerValue)
{
    if (!IsInGameThread() || !GIsEditor || !GEditor || IsRunningCookCommandlet())
        return TEXT("Material input operations require the Editor game thread outside cook.");

    constexpr EObjectFlags Incomplete = RF_NeedLoad | RF_NeedPostLoad | RF_BeginDestroyed | RF_FinishDestroyed;
    if (!IsValid(Material) || !Material->IsAsset() || Material->IsTemplate() || Material->HasAnyFlags(Incomplete)
        || !IsValid(Expression) || Expression->IsTemplate() || Expression->HasAnyFlags(Incomplete))
        return TEXT("Select an initialized project material and one of its Custom expressions.");

    UPackage* Package = Material->GetOutermost();
    if (!Package->GetName().StartsWith(TEXT("/Game/")) || Package->bIsCookedForEditor
        || Package->HasAnyPackageFlags(PKG_FilterEditorOnly) || OwnerMetadataKey.IsNone() || ExpectedOwnerValue.IsEmpty()
        || !Package->GetMetaData().GetValue(Material, OwnerMetadataKey).Equals(ExpectedOwnerValue, ESearchCase::CaseSensitive))
        return TEXT("The material must be a project asset with the exact ownership metadata.");

    if (Expression->GetOuter() != Material || Expression->Material != Material || Expression->Function
        || !Material->GetExpressions().ContainsByPredicate([Expression](const auto& Entry) { return Entry.Get() == Expression; }))
        return TEXT("The Custom expression must belong to the supplied material graph.");
    return nullptr;
}

FString ReadMaterialInputs(const UMaterial& Material, const UMaterialExpressionCustom& Expression, bool bChanged)
{
    const auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("complete"), true);
    Result->SetBoolField(TEXT("changed"), bChanged);
    Result->SetBoolField(TEXT("saved"), false);
    Result->SetBoolField(TEXT("shader_status_verified"), false);
    Result->SetStringField(TEXT("material"), Material.GetPathName());
    Result->SetStringField(TEXT("expression"), Expression.GetPathName());
    TArray<TSharedPtr<FJsonValue>> Inputs;
    for (const FCustomInput& Input : Expression.Inputs)
    {
        const auto Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("name"), Input.InputName.ToString());
        Entry->SetBoolField(TEXT("connected"), Input.Input.IsConnected());
        if (Input.Input.IsConnected())
        {
            Entry->SetStringField(TEXT("source"), GetPathNameSafe(Input.Input.Expression));
            Entry->SetNumberField(TEXT("output_index"), Input.Input.OutputIndex);
        }
        Inputs.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Result->SetArrayField(TEXT("inputs"), MoveTemp(Inputs));
    return EncodeMaterialInputs(Result);
}

bool IsCustomInputName(FName Name)
{
    if (Name.IsNone()) return false;
    const FString Text = Name.ToString();
    const auto IsLetter = [](TCHAR Ch) {
        return (Ch >= TEXT('a') && Ch <= TEXT('z')) || (Ch >= TEXT('A') && Ch <= TEXT('Z')) || Ch == TEXT('_');
    };
    if (Text.IsEmpty() || Text.Len() > 64 || !IsLetter(Text[0])) return false;
    for (TCHAR Ch : Text)
        if (!IsLetter(Ch) && (Ch < TEXT('0') || Ch > TEXT('9'))) return false;
    return true;
}
}

FString UMCVFXMaterialToolset::GetOwnedCustomMaterialInputs(UMaterial* Material, UMaterialExpressionCustom* Expression,
    FName OwnerMetadataKey, const FString& ExpectedOwnerValue)
{
    if (const TCHAR* Error = ValidateMaterialInputs(Material, Expression, OwnerMetadataKey, ExpectedOwnerValue))
        return RejectMaterialInputs(Error);
    return ReadMaterialInputs(*Material, *Expression, false);
}

FString UMCVFXMaterialToolset::SetOwnedCustomMaterialInputs(UMaterial* Material, UMaterialExpressionCustom* Expression,
    const TArray<FName>& InputNames, FName OwnerMetadataKey, const FString& ExpectedOwnerValue)
{
    if (const TCHAR* Error = ValidateMaterialInputs(Material, Expression, OwnerMetadataKey, ExpectedOwnerValue))
        return RejectMaterialInputs(Error);
    if (GEditor->PlayWorld || GEditor->IsTransactionActive())
        return RejectMaterialInputs(TEXT("Change Custom inputs outside PIE and existing transactions."));
    auto* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (AssetEditors && AssetEditors->FindEditorForAsset(Material, false))
        return RejectMaterialInputs(TEXT("Close the material editor before changing this asset's Custom inputs."));
    if (InputNames.Num() > 32)
        return RejectMaterialInputs(TEXT("Supply at most 32 Custom input names."));

    TSet<FName> UniqueNames;
    for (FName Name : InputNames)
    {
        if (!IsCustomInputName(Name) || UniqueNames.Contains(Name))
            return RejectMaterialInputs(TEXT("Custom input names must be unique ASCII identifiers of at most 64 characters."));
        UniqueNames.Add(Name);
    }

    TMap<FName, int32> OldIndices;
    bool bChanged = InputNames.Num() != Expression->Inputs.Num();
    for (int32 Index = 0; Index < Expression->Inputs.Num(); ++Index)
    {
        const FCustomInput& Old = Expression->Inputs[Index];
        if (!Old.InputName.IsNone() && OldIndices.Contains(Old.InputName))
            return RejectMaterialInputs(TEXT("The expression already has ambiguous duplicate input names."));
        if (!Old.InputName.IsNone()) OldIndices.Add(Old.InputName, Index);
        if (!UniqueNames.Contains(Old.InputName) && Old.Input.IsConnected())
            return RejectMaterialInputs(FString::Printf(TEXT("Disconnect input '%s' before removing or renaming it."), *Old.InputName.ToString()));
        bChanged |= !InputNames.IsValidIndex(Index) || !Old.InputName.IsEqual(InputNames[Index], ENameCase::CaseSensitive);
    }
    if (!bChanged) return ReadMaterialInputs(*Material, *Expression, false);

    TArray<FCustomInput> Replacement;
    Replacement.Reserve(InputNames.Num());
    for (FName Name : InputNames)
    {
        const int32* OldIndex = OldIndices.Find(Name);
        FCustomInput& Input = Replacement.Add_GetRef(OldIndex ? Expression->Inputs[*OldIndex] : FCustomInput{});
        Input.InputName = Name;
    }

    auto* GraphNode = Cast<UMaterialGraphNode>(Expression->GraphNode);
    if (Expression->GraphNode && (!GraphNode || !GraphNode->GetGraph() || GraphNode->GetOutermost() != Material->GetOutermost()))
        return RejectMaterialInputs(TEXT("The expression has an unexpected graph representation."));
    TMap<FName, UEdGraphPin*> SourcePins;
    TArray<UEdGraphPin*> ObsoletePins;
    if (GraphNode)
    {
        for (int32 Index = 0; Index < Expression->Inputs.Num(); ++Index)
        {
            UEdGraphPin* Pin = GraphNode->GetInputPin(Index);
            const FCustomInput& Input = Expression->Inputs[Index];
            if (!Pin || Pin->LinkedTo.Num() != (Input.Input.IsConnected() ? 1 : 0))
                return RejectMaterialInputs(TEXT("The graph's wires disagree with the material inputs."));
            if (!UniqueNames.Contains(Input.InputName)) ObsoletePins.Add(Pin);
            if (!Input.Input.IsConnected()) continue;
            UEdGraphPin* Source = Pin->LinkedTo[0];
            const UEdGraphNode* SourceNode = Source ? Source->GetOwningNode() : nullptr;
            if (!SourceNode || SourceNode == GraphNode || SourceNode->GetOutermost() != Material->GetOutermost())
                return RejectMaterialInputs(TEXT("An input has an unexpected graph connection."));
            SourcePins.Add(Input.InputName, Source);
        }
    }
    FProperty* Property = FindFProperty<FProperty>(UMaterialExpressionCustom::StaticClass(),
        GET_MEMBER_NAME_CHECKED(UMaterialExpressionCustom, Inputs));
    if (!Property) return RejectMaterialInputs(TEXT("The engine did not expose the Custom Inputs property."));

    const FScopedTransaction Transaction(NSLOCTEXT("MCVFXMaterialToolset", "CustomInputs", "Change Custom material inputs"));
    Material->Modify();
    Expression->Modify();
    Expression->PreEditChange(Property);
    if (GraphNode)
    {
        GraphNode->Modify();
        // The engine reconstructs by abbreviated display name and then by index.
        // Restore our actual named wires explicitly after it allocates the new pins.
        for (const auto& Pair : SourcePins)
            GraphNode->GetInputPin(OldIndices.FindChecked(Pair.Key))->BreakLinkTo(Pair.Value);
        for (UEdGraphPin* Pin : ObsoletePins) GraphNode->RemovePin(Pin);
    }
    Expression->Inputs = MoveTemp(Replacement);
    FPropertyChangedEvent Changed(Property, EPropertyChangeType::ValueSet);
    Expression->PostEditChangeProperty(Changed);
    if (GraphNode)
    {
        for (int32 Index = 0; Index < InputNames.Num(); ++Index)
            if (UEdGraphPin* Source = SourcePins.FindRef(InputNames[Index]))
                GraphNode->GetInputPin(Index)->MakeLinkTo(Source);
        GraphNode->GetGraph()->NotifyGraphChanged();
    }
    Material->MarkPackageDirty();
    return ReadMaterialInputs(*Material, *Expression, true);
}
