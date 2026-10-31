#include "MCVFXActionCatalog.h"

#include "MCActiveSkillAction.h"
#include "MCActiveSkillPerkEffect.h"
#include "MCPerkPreviewAdapter.h"
#include "MCPerkTypes.h"
#include "Engine/DataTable.h"

namespace
{
TMap<FName,FMCVFXActionCatalogueEntry>& SourceEntries()
{static TMap<FName,FMCVFXActionCatalogueEntry> Entries;return Entries;}

const AMCActiveSkillAction* ActionDefaults(const FMCPerkDefinition& Row)
{
    const auto* Effect=Row.EffectClass?Cast<UMCActiveSkillPerkEffect>(Row.EffectClass->GetDefaultObject()):nullptr;
    return Effect && Effect->ActionClass?Cast<AMCActiveSkillAction>(Effect->ActionClass->GetDefaultObject()):nullptr;
}

void ResolveProvider(FMCVFXActionCatalogueEntry& Entry,const FMCPerkDefinition* Row,const UDataTable* Table)
{
    const auto* Provider=MCPerkPreviewAdapters::Find(Entry.ID);
    if(Provider) Entry.Stages=Provider->Stages;
    if(!Row && Provider && !Provider->SourceRowID.IsNone() && Table)
        Row=Table->FindRow<FMCPerkDefinition>(Provider->SourceRowID,TEXT("Context preview source"),false);
    const bool bContext=Provider && Provider->Kind==EMCPerkPreviewAdapterKind::Context;
    const bool bPassive=!bContext && Row && Row->Kind==EMCPerkKind::Passive;
    const bool bHasDefinition=Row || (bContext && Provider->SourceRowID.IsNone());
    const bool bCorrectKind=Provider && (bContext || (bPassive && Provider->Kind==EMCPerkPreviewAdapterKind::Passive)
        || (!bPassive && Provider->Kind==EMCPerkPreviewAdapterKind::ActiveSkill));
    // Definition-free contexts are real registered providers, never Rocket
    // stand-ins. Contexts with a SourceRowID retain the canonical enabled gate.
    Entry.bEnabled=Entry.bEnabled && (!Row || Row->bEnabled);
    Entry.bSupportsPreview=bHasDefinition && Entry.bEnabled && Provider && Provider->Create && bCorrectKind;
    Entry.bSupportsProfileOverride=Entry.bSupportsPreview && Provider->bSupportsProfileOverride;
    if(!Entry.bEnabled) Entry.UnsupportedReason=TEXT("Отключено в текущем игровом каталоге.");
    else if(!Provider) Entry.UnsupportedReason=TEXT("Механика существует в игре; для неё ещё не зарегистрирован контекстный preview adapter.");
    else if(!bHasDefinition) Entry.UnsupportedReason=TEXT("SourceRowID контекстного сценария отсутствует в текущей DT.");
    else if(!bCorrectKind) Entry.UnsupportedReason=TEXT("Тип preview adapter не соответствует текущей строке DT; подмена механики запрещена.");
    else Entry.UnsupportedReason.Reset();
    if(!Entry.bSupportsProfileOverride) Entry.ProfileOverrideUnsupportedReason=bPassive
        ?TEXT("Текущая пассивка использует родное/процедурное представление и не читает DT.VFXProfile. Просмотр механики не означает возможность применить новый Niagara.")
        :TEXT("Редактируемая подмена VFX для этого события пока не подключена.");
    else Entry.ProfileOverrideUnsupportedReason.Reset();
}

struct FRegisterSourceHooks
{
    FRegisterSourceHooks()
    {
        auto Add=[](const TCHAR* ID,const TCHAR* Label,const TCHAR* Category,const TCHAR* Owner,const TCHAR* Description)
        {
            FMCVFXActionCatalogueEntry E;E.ID=FName(ID);E.Label=FText::FromString(Label);E.Category=FName(Category);
            E.SourceOwner=Owner;E.Description=FText::FromString(Description);MCVFXActionCatalog::RegisterSource(MoveTemp(E));
        };
        Add(TEXT("EditorOnly.CleanStain"),TEXT("Уборка · очищенное пятно"),TEXT("Cleaning"),
            TEXT("AMCMouthSurface / UMCResultPassives"),
            TEXT("MCMouthLiquid.cpp: ApplyLiquidCleaning → MCResultPassives.cpp: CompleteCleaning. Родная маска FMCCoffeeWipe; NS_BrushFoam показывает контакт щётки, а не подтверждённое завершение пятна."));
        Add(TEXT("EditorOnly.CleanArenaTooth"),TEXT("Уход · очистка большого зуба"),TEXT("ToothCare"),
            TEXT("AMCArenaTooth / UMCToothCareFeedbackComponent"),
            TEXT("MCArenaToothGrime.cpp: BrushGrime → ReportAccepted(Cleaning). Процедурное представление с /Game/Gameplay/Care/M_ToothCarePearl; самостоятельная Niagara-фаза ещё не подключена."));
        Add(TEXT("EditorOnly.RepairArenaTooth"),TEXT("Уход · восстановление большого зуба"),TEXT("ToothCare"),
            TEXT("UMCToothStatusComponent / UMCToothCareFeedbackComponent"),
            TEXT("MCToothStatusComponent.cpp: CareContact → ReportAccepted(Repair) после реального восстановления HP. Текущее представление процедурное, материал M_ToothCarePearl."));
        Add(TEXT("EditorOnly.NutEnemyDeath"),TEXT("Смерть · обычный орех"),TEXT("Enemies"),
            TEXT("AMCNutEnemy / AMCReactionVFX"),
            TEXT("MCNutEnemy.cpp: Defeat → Spawn(FoodBreak), /Game/Gameplay/VFX/NutCombat/NS_NutDustImpact вместе с родным уменьшением меша/реакцией. Это не смерть босса; полный каталог боссов ещё не аудирован."));
        Add(TEXT("EditorOnly.CharacterDeath"),TEXT("Смерть · зубик-персонаж"),TEXT("Character"),
            TEXT("AMCToothCharacter / UMCToothPhysicsComponent / AMCGameMode"),
            TEXT("StatusChanged → EnterDeath / PlayerDied. Обычная смерть использует физическую позу и сменщика; заморозка отдельно OnRep_FreezeDeath → AMCFreezeDeathVFX с материалами /Game/Gameplay/Cold/Death. Ни один вариант не подменяется сценарием ракетницы."));
        Add(TEXT("EditorOnly.FishingCatch"),TEXT("Награда · улов рыбы"),TEXT("Hub"),
            TEXT("UMCFishingRodComponent / UMCFishingRewardWidget"),
            TEXT("Улов → ClientFishingCatch → PresentCatch. Значок у персонажа и перелёт в счётчик хаба рисуются UMG/Slate в MCFishingRewardWidget.cpp; это не Niagara System."));
    }
} RegisterSourceHooks;
}

void MCVFXActionCatalog::RegisterSource(FMCVFXActionCatalogueEntry Entry)
{
    if(Entry.ID.IsNone()) return;
    // Capabilities are resolved from real runtime definitions/providers on read.
    Entry.bSupportsPreview=false;Entry.bSupportsProfileOverride=false;
    SourceEntries().Add(Entry.ID,MoveTemp(Entry));
}

TArray<FMCVFXActionCatalogueEntry> MCVFXActionCatalog::GetCatalogue()
{
    TMap<FName,FMCVFXActionCatalogueEntry> Entries=SourceEntries();
    auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Gameplay/Roguelike/DT_Perks.DT_Perks"));
    if(Table && Table->GetRowStruct()==FMCPerkDefinition::StaticStruct()) {
        for(const auto& Pair:Table->GetRowMap()) {
            const auto* Row=reinterpret_cast<const FMCPerkDefinition*>(Pair.Value);
            if(!Row) continue;
            const auto* Action=ActionDefaults(*Row);
            const bool bPassive=Row->bEnabled && Row->Kind==EMCPerkKind::Passive && Row->EffectClass;
            if(!Action && !bPassive) continue;
            FMCVFXActionCatalogueEntry E;E.ID=Pair.Key;E.Label=Row->DisplayName.IsEmpty()?FText::FromName(Pair.Key):Row->DisplayName;
            if(bPassive) E.Label=FText::FromString(TEXT("Пассивка · ")+E.Label.ToString());
            E.Description=Row->Description;E.Icon=Row->Icon;E.bEnabled=Row->bEnabled;
            E.Category=bPassive?FName(TEXT("Passives")):FName(TEXT("ActiveSkills"));
            E.SourceOwner=Row->EffectClass?Row->EffectClass->GetPathName():TEXT("DT_Perks");
            if(Action) E.SourceOwner+=TEXT(" → ")+Action->GetClass()->GetPathName();
            E.Description=FText::FromString(E.Description.ToString()+TEXT("\nDT_Perks / ")+Pair.Key.ToString()+TEXT("; ")+E.SourceOwner);
            ResolveProvider(E,Row,Table);Entries.Add(E.ID,MoveTemp(E));
        }
    }
    // Orphan providers remain visible for diagnostics, never as a working perk.
    // This catches a removed/renamed/disabled DT definition without inventing it.
    for(const auto& Provider:MCPerkPreviewAdapters::GetEntries()) {
        if(Entries.Contains(Provider.PerkID)) continue;
        FMCVFXActionCatalogueEntry E;E.ID=Provider.PerkID;E.Label=Provider.Label;
        E.Category=TEXT("UnresolvedProvider");E.SourceOwner=TEXT("MCPerkPreviewAdapters");E.bEnabled=false;
        E.Description=FText::FromString(TEXT("Зарегистрированный preview provider без доступной текущей DT/source-записи."));
        E.Stages=Provider.Stages;Entries.Add(E.ID,MoveTemp(E));
    }
    TArray<FMCVFXActionCatalogueEntry> Result;
    Entries.GenerateValueArray(Result);
    for(auto& E:Result) if(!E.bSupportsPreview) {
        const FMCPerkDefinition* Row=Table && Table->GetRowStruct()==FMCPerkDefinition::StaticStruct()
            ?Table->FindRow<FMCPerkDefinition>(E.ID,TEXT("VFX shared catalogue"),false):nullptr;
        ResolveProvider(E,Row,Table);
    }
    Result.Sort([](const auto& A,const auto& B){
        if(A.bSupportsPreview!=B.bSupportsPreview) return A.bSupportsPreview;
        if(A.Category!=B.Category) return A.Category.LexicalLess(B.Category);
        return A.Label.ToString()<B.Label.ToString();
    });
    return Result;
}
