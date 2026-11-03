#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewGameMode.h"

#include "MCNutEnemy.h"
#include "MCReactionVFX.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCVFXPresentationComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraSystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace MCWorldVFXPreview
{
/** A real stationary enemy, not a cue timeline or an active-skill stand-in.
 * The fixture intentionally has no arena AI/Tongue. Its tick is held while
 * alive, then native damage/Defeat and native death shrink own presentation. */
class FNutEnemyDeathAdapter final : public IMCPerkPreviewAdapter
{
public:
    bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
    {
        if(!C.World || C.World->WorldType!=EWorldType::Game || C.World->GetOutermost()!=GetTransientPackage()
            || !C.World->GetAuthGameMode<AMCPerkPreviewGameMode>() || !IsValid(C.Hero)
            || C.Hero->GetWorld()!=C.World || !C.Hero->HasAuthority()
            || !C.Hero->Status || !C.Hero->Status->IsAlive()
            || C.PerkID!=TEXT("EditorOnly.NutEnemyDeath") || C.Scenario!=EMCPerkPreviewScenario::Target)
        {
            Error=TEXT("Смерть ореха требует отдельный временный Game-мир предпросмотра и настоящий живой зубик.");
            return false;
        }
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_Whole.SM_Walnut_Whole"));
        if(!Mesh) {Error=TEXT("Не найден сохранённый меш обычного ореха.");return false;}

        FActorSpawnParameters Spawn;
        Spawn.ObjectFlags=RF_Transient;Spawn.bDeferConstruction=true;
        Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        const FVector Point(C.Hero->GetActorLocation().X+180.f,C.Hero->GetActorLocation().Y,44.f);
        auto* Target=C.World->SpawnActor<AMCNutEnemy>(Point,FRotator(0,180,0),Spawn);
        if(!Target) {Error=TEXT("Не удалось создать настоящий AMCNutEnemy.");return false;}
        Enemy=Target;C.Targets.Add(Target);
        Target->SetActorTickEnabled(false);
        Target->Settings.MaxHealth=30.f;Target->Settings.BodyRadius=40.f;Target->Settings.AttackDamage=0.f;
        Target->NutMesh=Mesh;Target->MeshScale=FVector(80.f/FMath::Max(1.f,Mesh->GetBounds().BoxExtent.Z*2.f));
        Target->Health=Target->Settings.MaxHealth;
        Target->FinishSpawning(FTransform(FRotator(0,180,0),Point));
        // BeginPlay does not change this fixed death-only fixture into a live
        // arena encounter. Warmup must not make null-Tongue AI defeat it early.
        Target->SetActorTickEnabled(false);
        if(!Target->Presentation) {Error=TEXT("У обычного ореха нет родного компонента VFX-представления.");return false;}
        Presentation=Target->Presentation;
        if(const auto* Defaults=GetDefault<AMCReactionVFX>())
            LegacyDust.Reset(Defaults->FoodBreakSystem.LoadSynchronous());
        // A real fatal tool hit also owns the existing hit/food-break material
        // presentation. Prepare its exact native dependencies without spawning
        // a cue before the accepted gameplay event.
        if(!C.AddBox){Error=TEXT("Нет подготовщика ресурсов временного мира.");return false;}
        const TCHAR* NativeMaterialPaths[]={
            TEXT("/Game/Gameplay/VFX/M_Reaction.M_Reaction"),
            TEXT("/Game/Gameplay/VFX/M_ReactionSoft.M_ReactionSoft"),
            TEXT("/Game/Gameplay/VFX/M_ReactionFire.M_ReactionFire"),
            TEXT("/Game/Gameplay/VFX/M_ReactionSmoke.M_ReactionSmoke")};
        for(int32 Index=0;Index<UE_ARRAY_COUNT(NativeMaterialPaths);++Index)
        {
            auto* Material=LoadObject<UMaterialInterface>(nullptr,NativeMaterialPaths[Index]);
            auto* Anchor=C.AddBox(FName(*FString::Printf(TEXT("NutDeathResource_%d"),Index)),
                FVector(0,0,-10000-Index*10),FVector(.001f));
            if(!Material || !Anchor){Error=TEXT("Не удалось подготовить родные материалы реакции ореха.");return false;}
            Anchor->SetActorHiddenInGame(true);Anchor->SetActorEnableCollision(false);
            Anchor->GetStaticMeshComponent()->SetMaterial(0,Material);C.Targets.Add(Anchor);
        }
        Age=0;bCommitted=false;bFatalAccepted=false;bDeathObserved=false;bFailed=false;
        if(C.ReportStatus) C.ReportStatus(TEXT("Настоящий орех: урон → Defeat → Death; родное уменьшение меша сохранено отдельно от VFX."));
        return true;
    }

    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error) override
    {
        auto* Component=Presentation.Get();
        if(!Component || !Enemy.IsValid()) {Error=TEXT("Сценарий ореха ещё не подготовлен.");return false;}
        if(DeathHandle.IsValid()) Component->OnEventAccepted.Remove(DeathHandle);
        DeathHandle=Component->OnEventAccepted.AddLambda([this,Record=C.RecordEvent,Report=C.ReportStatus]
            (FName Event,EMCVFXPresentationResult Result)
        {
            if(!bCommitted)return;
            if(Event==TEXT("DeathDust")) {
                if(Record)Record(Result==EMCVFXPresentationResult::Rejected?FName(TEXT("NativeDeathDustRejected")):Event);
                return;
            }
            if(Event!=TEXT("Death"))return;
            if(Result==EMCVFXPresentationResult::Rejected)
            {
                bFailed=true;
                if(Record) Record(TEXT("NativeDeathPresentationRejected"));
                if(Report) Report(TEXT("Родной Defeat произошёл, но VFX-профиль отклонён; это диагностический провал, не готовое превью."));
                return;
            }
            bDeathObserved=true;LastResult=Result;
            // Death is reported only by the actual gameplay owner's receipt.
            // The adapter never fabricates the editable event's timeline.
            if(Record) Record(Event);
        });
        return C.PresentationOverride
            ?Component->PrepareProfileOverride(C.PresentationOverride,Error)
            :Component->Prepare(Error);
    }

    void GetPreparedVisualSystems(const FMCPerkPreviewContext&,TArray<UNiagaraSystem*>& Out) const override
    {
        if(const auto* Component=Presentation.Get()) Component->GetPreparedResources(Out);
        // UseDefault preserves the real FoodBreak fallback. Its actual native
        // system is retained and included in readiness before any fatal hit.
        if(LegacyDust.IsValid()) Out.AddUnique(LegacyDust.Get());
    }

    void OnCommitted(FMCPerkPreviewContext& C) override
    {
        auto* Target=Enemy.Get();
        bCommitted=true;Age=0;
        if(!Target || Target->bDefeated || !IsValid(C.Hero))
        {bFailed=true;if(C.RecordEvent)C.RecordEvent(TEXT("NativeDeathFixtureInvalid"));return;}
        const float Expected=Target->Health;
        const float Applied=Target->ReceiveToolDamage(Expected+1.f,C.Hero);
        bFatalAccepted=Applied>0.f && Target->bDefeated && Target->Health<=0.f;
        // Native TickPresentation now supplies the same pop/shrink as gameplay;
        // bDefeated prevents the absent arena AI/Tongue branch from running.
        Target->SetActorTickEnabled(true);
        if(C.RecordEvent) C.RecordEvent(bFatalAccepted?FName(TEXT("NativeFatalDamageAccepted")):FName(TEXT("NativeFatalDamageRejected")));
        if(!bFatalAccepted) bFailed=true;
    }

    void Tick(FMCPerkPreviewContext& C,float Dt) override
    {
        if(!bCommitted)return;
        Age+=FMath::Max(0.f,Dt);
        if(Age>=2.f && !bDeathObserved && !bFailed)
        {
            bFailed=true;
            if(C.RecordEvent)C.RecordEvent(TEXT("NativeDeathReceiptMissing"));
            if(C.ReportStatus)C.ReportStatus(TEXT("Урон принят, но родное событие Death не подтверждено. Превью не прошло проверку."));
        }
    }

    bool IsComplete(const FMCPerkPreviewContext&) const override
    {return bCommitted && Age>=1.5f && (bFailed || (bFatalAccepted && bDeathObserved));}
    FString GetPhaseName(const FMCPerkPreviewContext&) const override
    {
        if(bFailed)return TEXT("Смерть ореха · проверка не пройдена");
        if(!bCommitted)return TEXT("Подготовка настоящего ореха и VFX");
        if(!bDeathObserved)return TEXT("Родной урон → Defeat");
        if(LastResult==EMCVFXPresentationResult::Suppressed)return TEXT("Death · эффект отключён, родное исчезновение меша");
        return TEXT("Death · родное исчезновение и остаток эффекта");
    }
    float GetTailSeconds() const override {return 2.f;}
    void Cancel(FMCPerkPreviewContext& C) override
    {
        if(auto* Component=Presentation.Get())Component->StopPresentation(true);
        // The native enemy is removed after 1.2s, while a world-owned cosmetic
        // tail may still run. Cancellation must also reach that surviving tail.
        if(C.World && C.World->WorldType==EWorldType::Game && C.World->GetOutermost()==GetTransientPackage()
            && C.World->GetAuthGameMode<AMCPerkPreviewGameMode>())
            for(TActorIterator<AMCPerkVFXCompositionPlayer> It(C.World);It;++It)
                if(IsValid(*It) && !It->IsActorBeingDestroyed()){It->StopEmission(true);It->Destroy(false,false);}
    }
    void Reset(FMCPerkPreviewContext&) override
    {
        if(auto* Component=Presentation.Get())
        {if(DeathHandle.IsValid())Component->OnEventAccepted.Remove(DeathHandle);Component->StopPresentation(true);}
        DeathHandle.Reset();Presentation.Reset();Enemy.Reset();LegacyDust.Reset();
        Age=0;bCommitted=false;bFatalAccepted=false;bDeathObserved=false;bFailed=false;
    }
private:
    TWeakObjectPtr<AMCNutEnemy> Enemy;
    TWeakObjectPtr<UMCVFXPresentationComponent> Presentation;
    TStrongObjectPtr<UNiagaraSystem> LegacyDust;
    FDelegateHandle DeathHandle;
    float Age=0;
    bool bCommitted=false,bFatalAccepted=false,bDeathObserved=false,bFailed=false;
    EMCVFXPresentationResult LastResult=EMCVFXPresentationResult::UseDefault;
};

const bool bRegistered=[]
{
    FMCPerkPreviewAdapterEntry Entry;
    Entry.PerkID=TEXT("EditorOnly.NutEnemyDeath");Entry.SourceRowID=NAME_None;
    Entry.Label=FText::FromString(TEXT("Обычный орех · смерть"));
    Entry.Kind=EMCPerkPreviewAdapterKind::Context;Entry.bSupportsProfileOverride=true;
    Entry.Stages={TEXT("Death"),TEXT("DeathDust")};
    Entry.StageLabels.Add(TEXT("Death"),FText::FromString(TEXT("Смерть ореха · звёзды и пыль")));
    Entry.StageLabels.Add(TEXT("DeathDust"),FText::FromString(TEXT("Отдельный родной Niagara-всплеск")));
    // The native FoodBreak fallback is NS_NutDustImpact, not a newly invented
    // full death-animation replacement. No package is loaded at static init.
    Entry.DefaultSystems.Add(TEXT("DeathDust"),TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(
        TEXT("/Game/Gameplay/VFX/NutCombat/NS_NutDustImpact.NS_NutDustImpact"))));
    Entry.Scenarios={EMCPerkPreviewScenario::Target};
    Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(TEXT("Настоящий орех · смертельный удар")));
    Entry.Create=[]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FNutEnemyDeathAdapter>();};
    MCPerkPreviewAdapters::Register(MoveTemp(Entry));return true;
}();
}
