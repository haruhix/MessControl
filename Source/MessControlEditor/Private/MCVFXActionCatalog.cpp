#include "MCVFXActionCatalog.h"

#include "MCActiveSkillAction.h"
#include "MCActiveSkillPerkEffect.h"
#include "MCPerkPreviewAdapter.h"
#include "MCPerkTypes.h"
#include "MCUIDataLibrary.h"
#include "MCVFXCatalogueFilter.h"
#include "MCVFXProfileBinding.h"
#include "Engine/DataTable.h"

namespace
{
TMap<FName,FMCVFXActionCatalogueEntry>& SourceEntries()
{static TMap<FName,FMCVFXActionCatalogueEntry> Entries;return Entries;}

void ResolveNavigation(FMCVFXActionCatalogueEntry& Entry)
{
    // These are declared source namespaces, not a second action inventory or
    // an inference from a translated display name / technical SourceOwner.
    if(Entry.OwnerID.IsNone())
    {
        const FString ID=Entry.ID.ToString();
        struct FOwnerNamespace { const TCHAR* Prefix; const TCHAR* ID; const TCHAR* Label; };
        static const FOwnerNamespace Owners[]={
            {TEXT("Boss.NutKnight."),TEXT("Boss.NutKnight"),TEXT("Рыцарь")},
            {TEXT("Boss.NutMage."),TEXT("Boss.NutMage"),TEXT("Маг")},
            {TEXT("Enemy.Nut."),TEXT("Enemy.Nut"),TEXT("Обычный орех")},
            {TEXT("Enemy.NutKamikaze."),TEXT("Enemy.NutKamikaze"),TEXT("Орех-камикадзе")},
            {TEXT("Character."),TEXT("Character"),TEXT("Персонаж")},
            {TEXT("Player."),TEXT("Player"),TEXT("Персонаж")},
            {TEXT("Hub."),TEXT("Hub"),TEXT("Хаб")}
        };
        for(const auto& Owner:Owners)if(ID.StartsWith(Owner.Prefix))
        {Entry.OwnerID=Owner.ID;Entry.OwnerLabel=FText::FromString(Owner.Label);break;}
        if(Entry.OwnerID.IsNone()) {Entry.OwnerID=Entry.ID;Entry.OwnerLabel=Entry.Label;}
    }
    if(Entry.OwnerLabel.IsEmpty())Entry.OwnerLabel=FText::FromName(Entry.OwnerID);
    // A source reaction with an unmapped category still belongs to its declared
    // owner family. Keep it with that owner's actions instead of duplicating
    // e.g. Knight under both Bosses and the fallback Other section.
    if(Entry.Section==EMCVFXActionSection::Other)
    {
        if(Entry.OwnerID==TEXT("Boss.NutKnight")||Entry.OwnerID==TEXT("Boss.NutMage"))Entry.Section=EMCVFXActionSection::Bosses;
        else if(Entry.OwnerID==TEXT("Enemy.Nut")||Entry.OwnerID==TEXT("Enemy.NutKamikaze")||Entry.OwnerID==TEXT("Character")||Entry.OwnerID==TEXT("Player"))Entry.Section=EMCVFXActionSection::General;
        else if(Entry.OwnerID==TEXT("Hub"))Entry.Section=EMCVFXActionSection::InterfaceHub;
    }
    if(Entry.ActionLabel.IsEmpty())
    {
        FString Name=Entry.Label.ToString();
        if(Entry.OwnerID!=Entry.ID)
        {
            int32 Separator=INDEX_NONE;
            if(Name.FindChar(TEXT('·'),Separator))Name=Name.Mid(Separator+1).TrimStartAndEnd();
        }
        Entry.ActionLabel=FText::FromString(Name);
    }
}

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
    Entry.bSupportsPreview=bHasDefinition && Entry.bEnabled && Provider && Provider->Create
        && !Provider->Scenarios.IsEmpty() && bCorrectKind;
    Entry.bSupportsProfileOverride=bHasDefinition && Entry.bEnabled && Provider && bCorrectKind
        && !Provider->Stages.IsEmpty() && Provider->bSupportsProfileOverride;
    if(!Entry.bEnabled) Entry.UnsupportedReason=TEXT("Отключено в текущем игровом каталоге.");
    else if(!Provider) Entry.UnsupportedReason=TEXT("Механика существует в игре; для неё ещё не зарегистрирован контекстный preview adapter.");
    else if(!bHasDefinition) Entry.UnsupportedReason=TEXT("SourceRowID контекстного сценария отсутствует в текущей DT.");
    else if(!bCorrectKind) Entry.UnsupportedReason=TEXT("Тип preview adapter не соответствует текущей строке DT; подмена механики запрещена.");
    else if(!Provider->Create || Provider->Scenarios.IsEmpty()) {
        if(Entry.UnsupportedReason.IsEmpty())Entry.UnsupportedReason=TEXT("Контекстный сценарий для этого действия ещё не подключён. Сохранённые VFX и исходники редактируются отдельно от предпросмотра.");
    }
    else Entry.UnsupportedReason.Reset();
    if(Entry.bSupportsProfileOverride)
    {
        FMCVFXCanonicalProfileState Binding;FString Error;
        if(!MCVFXProfileBindings::ReadCanonical(Entry.ID,Binding,Error))
        {
            Entry.bSupportsProfileOverride=false;
            Entry.ProfileOverrideUnsupportedReason=Error;
            return;
        }
    }
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
            TEXT("MCArenaToothGrime.cpp: BrushGrime → ReportAccepted(Cleaning). Contact подтверждает удаление загрязнения, Complete — завершение очистки коронки."));
        Add(TEXT("EditorOnly.RepairArenaTooth"),TEXT("Уход · восстановление большого зуба"),TEXT("ToothCare"),
            TEXT("UMCToothStatusComponent / UMCToothCareFeedbackComponent"),
            TEXT("MCToothStatusComponent.cpp: RepairWithTool → ReportAccepted(Repair). Contact подтверждает прирост HP, Complete отмечает восстановление коронки из сломанного состояния или достижение полного HP."));
        Add(TEXT("EditorOnly.NutEnemyDeath"),TEXT("Смерть · обычный орех"),TEXT("Enemies"),
            TEXT("AMCNutEnemy / UMCVFXPresentationComponent"),
            TEXT("Настоящий fatal damage → Defeat → Death и отдельный DeathDust на исходном первом такте FoodBreak. Death принимает исходный процедурный рисунок через общий Reaction-контекст; DeathDust сохраняет родной Niagara-всплеск и его масштаб. Профиль — DA_MouthPresentation; физическое уменьшение и удаление ореха остаются родными. Назначение сохранённых NS и визуальная приёмка проверяются отдельно; это не смерть босса."));
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
    if(Entry.Section==EMCVFXActionSection::Other)Entry.Section=MCVFXCatalogueNavigation::FromSourceCategory(Entry.Category);
    // Capabilities are resolved from real runtime definitions/providers on read.
    Entry.bSupportsPreview=false;Entry.bSupportsProfileOverride=false;
    SourceEntries().Add(Entry.ID,MoveTemp(Entry));
}

TArray<FMCVFXActionCatalogueEntry> MCVFXActionCatalog::GetCatalogue()
{
    TMap<FName,FMCVFXActionCatalogueEntry> Entries=SourceEntries();
    auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Gameplay/Roguelike/DT_Perks.DT_Perks"));
    UDataTable* Localization=UMCUIDataLibrary::GetDefaultLocalizationTable();
    if(Table && Table->GetRowStruct()==FMCPerkDefinition::StaticStruct()) {
        for(const auto& Pair:Table->GetRowMap()) {
            const auto* Row=reinterpret_cast<const FMCPerkDefinition*>(Pair.Value);
            if(!Row) continue;
            const auto* Action=ActionDefaults(*Row);
            const bool bPassive=Row->bEnabled && Row->Kind==EMCPerkKind::Passive && Row->EffectClass;
            if(!Action && !bPassive) continue;
            FMCVFXActionCatalogueEntry E;E.ID=Pair.Key;E.Label=Row->DisplayName.IsEmpty()?FText::FromName(Pair.Key):Row->DisplayName;
            E.Section=EMCVFXActionSection::Boosters;E.PerkKind=Row->Kind;
            E.SearchNames.Add(Row->DisplayName.ToString());
            if(!Row->DisplayNameKey.IsNone()) {
                E.SearchNames.Add(Row->DisplayNameKey.ToString());
                E.SearchNames.Add(UMCUIDataLibrary::ResolveText(Localization,Row->DisplayNameKey,Row->DisplayName,TEXT("ru")).ToString());
                E.SearchNames.Add(UMCUIDataLibrary::ResolveText(Localization,Row->DisplayNameKey,Row->DisplayName,TEXT("en")).ToString());
                E.Label=UMCUIDataLibrary::ResolveText(Localization,Row->DisplayNameKey,E.Label);
            }
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
    for(auto& Entry:Result)ResolveNavigation(Entry);
    Result.Sort([](const auto& A,const auto& B){
        if(A.bSupportsPreview!=B.bSupportsPreview) return A.bSupportsPreview;
        if(A.Category!=B.Category) return A.Category.LexicalLess(B.Category);
        return A.Label.ToString()<B.Label.ToString();
    });
    return Result;
}
