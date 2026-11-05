#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCClothingFitEditorLibrary.generated.h"

class UDataTable;

/** Native clothing Row Editor actions plus separate read-only preview diagnostics. */
UCLASS()
class MESSCONTROLEDITOR_API UMCClothingFitEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Idle editor only: open the existing canonical table editor and select its actual row. Never changes row data or saves. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Clothing|Editor")
    static bool OpenTableRow(UDataTable* Table,FName RowName);

    /** Idle editor only: change the matching existing preview camera, 0 Front / 1 Side / 2 Back. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Clothing|Editor")
    static bool SetPreviewPreset(const FString& TableObjectPath,FName RowName,int32 Preset);

    /** Exact table object path and selected row. Never opens an editor or changes its selection. */
    UFUNCTION(BlueprintPure, Category="Mess Control|Clothing|Editor")
    static FString InspectOpenPreview(const FString& TableObjectPath,FName RowName);

    /** Captures the existing offscreen fitting viewport. Fresh absolute E: PNG only. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Clothing|Editor")
    static bool CaptureOpenPreview(const FString& TableObjectPath,FName RowName,const FString& OutputFile);
};
