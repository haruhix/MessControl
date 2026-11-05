#include "MCClothingFitEditorLibrary.h"
#include "SMCClothingFitViewport.h"
#include "MCCosmeticCatalog.h"
#include "DataTableEditorUtils.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

bool UMCClothingFitEditorLibrary::OpenTableRow(UDataTable* Table,FName RowName)
{
    if(!IsInGameThread() || !GIsEditor || !GEditor || GEditor->PlayWorld || IsRunningCommandlet() || !IsValid(Table)
        || Table->GetPathName()!=TEXT("/Game/Data/DT_CharacterCosmetics.DT_CharacterCosmetics")
        || Table->GetRowStruct()!=FMCCosmeticCatalogRow::StaticStruct() || RowName.IsNone() || !Table->GetRowMap().Contains(RowName)) return false;
    auto* Editors=GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if(!Editors || !Editors->OpenEditorForAsset(Table)) return false;
    // The public manager invokes the ordinary DataTable editor's SelectionChange/Row Editor restore.
    return FDataTableEditorUtils::SelectRow(Table,RowName);
}
bool UMCClothingFitEditorLibrary::SetPreviewPreset(const FString& TableObjectPath,FName RowName,int32 Preset)
{
    if(!IsInGameThread() || !GIsEditor || !GEditor || GEditor->PlayWorld || IsRunningCommandlet() || RowName.IsNone()
        || TableObjectPath!=TEXT("/Game/Data/DT_CharacterCosmetics.DT_CharacterCosmetics") || Preset<0 || Preset>2) return false;
    FString Error;const auto View=SMCClothingFitViewport::FindOpen(TableObjectPath,RowName,Error);
    if(!View) return false;View->SetViewPreset(Preset);return true;
}

FString UMCClothingFitEditorLibrary::InspectOpenPreview(const FString& TableObjectPath,FName RowName)
{
    auto Result=MakeShared<FJsonObject>();FString Error;
    const bool EditorRead=IsInGameThread() && GIsEditor && GEditor && !IsRunningCommandlet();
    // FindObject never loads a package; false never focuses/opens an asset editor.
    auto* Table=EditorRead?FindObject<UDataTable>(nullptr,*TableObjectPath):nullptr;
    auto* Editors=EditorRead?GEditor->GetEditorSubsystem<UAssetEditorSubsystem>():nullptr;
    const bool TableEditorOpen=Table && Editors && Editors->FindEditorForAsset(Table,false)!=nullptr;
    if(!IsInGameThread() || !GIsEditor || IsRunningCommandlet() || RowName.IsNone() || !TableObjectPath.StartsWith(TEXT("/Game/")))
        Error=TEXT("Requires an already open exact /Game DataTable row fitting viewport on the editor game thread.");
    else if(const auto View=SMCClothingFitViewport::FindOpen(TableObjectPath,RowName,Error)) Result=View->Inspect();
    if(!Error.IsEmpty()) {Result->SetBoolField(TEXT("valid"),false);Result->SetStringField(TEXT("error"),Error);}
    // Always available for guarded migration, including RowName=None or no fit viewport.
    Result->SetBoolField(TEXT("tableLoaded"),Table!=nullptr);
    Result->SetBoolField(TEXT("tableEditorOpen"),TableEditorOpen);
    Result->SetStringField(TEXT("tableQueried"),TableObjectPath);
    FString Text;const auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(Result,Writer);return Text;
}
bool UMCClothingFitEditorLibrary::CaptureOpenPreview(const FString& TableObjectPath,FName RowName,const FString& OutputFile)
{
    if(!IsInGameThread() || !GIsEditor || IsRunningCommandlet() || RowName.IsNone() || !TableObjectPath.StartsWith(TEXT("/Game/"))) return false;
    FString Error;const auto View=SMCClothingFitViewport::FindOpen(TableObjectPath,RowName,Error);
    return View && View->Capture(OutputFile);
}
