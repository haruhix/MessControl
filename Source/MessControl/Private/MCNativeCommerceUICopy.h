#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Internationalization/Text.h"
#include "MCUIDataTypes.h"

/** Read-only native message identity bridge shared by the original commerce UI owners. */
namespace MCNativeCommerceUICopy
{
    inline FName FindMessageKey(const UDataTable* Table,const FText& Original)
    {
        if(!Table || Table->GetRowStruct()!=FMCUILocalizationRow::StaticStruct()) return NAME_None;
        const TOptional<FString> NativeNamespace=FTextInspector::GetNamespace(Original);
        const TOptional<FString> NativeKey=FTextInspector::GetKey(Original);
        if(!NativeNamespace.IsSet() || NativeNamespace.GetValue()!=TEXT("MCHubEconomy")
            || !NativeKey.IsSet() || NativeKey.GetValue().IsEmpty()) return NAME_None;
        const FName MessageKey(*(FString(TEXT("UI.Economy.Message."))+NativeKey.GetValue()));
        const auto* Row=Table->FindRow<FMCUILocalizationRow>(MessageKey,TEXT("Native commerce UI message"),false);
        // A missing/incomplete row must preserve the incoming FText, including its identity/history.
        return Row && !Row->GetNativeText().IsEmpty()?MessageKey:NAME_None;
    }
}
