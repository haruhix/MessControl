#pragma once

#include "CoreMinimal.h"

namespace MCGamepadHints
{
    inline FString Adapt(FString Text)
    {
        Text.ReplaceInline(TEXT("ЛКМ"),TEXT("RT"));
        Text.ReplaceInline(TEXT("ПКМ"),TEXT("LB"));
        Text.ReplaceInline(TEXT("WASD"),TEXT("LS"));
        Text.ReplaceInline(TEXT("SHIFT"),TEXT("L3"));
        Text.ReplaceInline(TEXT("Shift"),TEXT("L3"));
        Text.ReplaceInline(TEXT("SPACE"),TEXT("A"));
        Text.ReplaceInline(TEXT("Space"),TEXT("A"));
        Text.ReplaceInline(TEXT("Enter"),TEXT("A"));
        const TCHAR* Keys[]={TEXT("1"),TEXT("2"),TEXT("3"),TEXT("4"),TEXT("E"),TEXT("Q"),TEXT("C"),TEXT("F"),TEXT("G")};
        const TCHAR* Pad[]={TEXT("←"),TEXT("↑"),TEXT("→"),TEXT("↓"),TEXT("X"),TEXT("Y"),TEXT("R3"),TEXT("LT"),TEXT("B")};
        for(int32 Index=0;Index<UE_ARRAY_COUNT(Keys);++Index) {
            Text.ReplaceInline(*(FString(Keys[Index])+TEXT(" —")),*(FString(Pad[Index])+TEXT(" —")),ESearchCase::CaseSensitive);
            Text.ReplaceInline(*(FString(Keys[Index])+TEXT(" ·")),*(FString(Pad[Index])+TEXT(" ·")),ESearchCase::CaseSensitive);
            Text.ReplaceInline(*(FString(Keys[Index])+TEXT(" +")),*(FString(Pad[Index])+TEXT(" +")),ESearchCase::CaseSensitive);
            Text.ReplaceInline(*(FString(Keys[Index])+TEXT(" БРОСИТЬ")),*(FString(Pad[Index])+TEXT(" БРОСИТЬ")),ESearchCase::CaseSensitive);
        }
        return Text;
    }
}
