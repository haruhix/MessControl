#include "MCColdColaPreviewAdapter.h"
#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewGameMode.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXSourceContracts.h"
#include "MCWorldPresentationComponent.h"
#include "MCColdCola.h"
#include "MCInventoryComponent.h"
#include "MCToothCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "NiagaraSystem.h"
#include "UObject/Package.h"

namespace MCColdColaPreviewAdapter
{
/** This scenario covers real block shatter, not the full climate timeline. */
class FAdapter final:public IMCPerkPreviewAdapter
{
public:
    bool Prepare(FMCPerkPreviewContext& C,FString& Error)override
    {
        if(!C.World||C.World->WorldType!=EWorldType::Game||C.World->GetOutermost()!=GetTransientPackage()
            ||!C.World->GetAuthGameMode<AMCPerkPreviewGameMode>()||!C.Hero||!C.Hero->HasAuthority()||!C.Hero->CanWork()||!C.Hero->Inventory)
        {Error=TEXT("Нужен настоящий authority-персонаж изолированного мира.");return false;}
        C.Hero->SetPrimaryInputHeld(false);PreviousSlot=C.Hero->Inventory->Selected;bHasSelection=true;C.Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
        if(C.Hero->Inventory->Selected!=EMCToolSlot::Pickaxe){Error=TEXT("Штатный выбор кирки отклонён.");return false;}
        const FTransform Pose(FRotator::ZeroRotator,C.Hero->GetActorLocation()+C.Hero->GetActorForwardVector()*110-FVector(0,0,60));
        auto* B=C.World->SpawnActor<AMCIceBlock>(AMCIceBlock::StaticClass(),Pose);
        if(!B){Error=TEXT("Настоящий ледяной блок не создан.");return false;}
        Block=B;C.Targets.Add(B);B->SetFlags(RF_Transient);InitialHealth=B->Health;
        // Preserve native shape, health, physics and actual melee acquisition.
        Presentation=MCWorldPresentation::PrepareOwner(B,{TEXT("World.ColdCola")});
        SwingsBefore=C.Hero->ValidatedSwingCount;HitsBefore=C.Hero->ConfirmedHitCount;
        BeforeSerial=B->GetAcceptedShatterSerial();if(C.RequestFramingUpdate)C.RequestFramingUpdate();return Presentation.IsValid();
    }
    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error)override
    {
        auto* P=Presentation.Get();const auto* Contract=MCVFXSourceContracts::Find(TEXT("World.ColdCola"));
        if(!P||!Contract||!P->PrepareSources(Error,true))return false;
        auto* S=P->GetSource(TEXT("World.ColdCola"));return S&&(!C.PresentationOverride||S->PrepareScopedOverride(TEXT("World.ColdCola"),Contract->Events,C.PresentationOverride,Error));
    }
    void GetPreparedVisualSystems(const FMCPerkPreviewContext&,TArray<UNiagaraSystem*>& Out)const override
    {
        if(auto* P=Presentation.Get())if(auto* S=P->GetSource(TEXT("World.ColdCola")))S->GetPreparedResources(Out);
        if(auto* B=Block.Get())if(auto* N=B->OriginalShatterSystem.Get())Out.AddUnique(N);
    }
    void OnCommitted(FMCPerkPreviewContext& C)override{bCommitted=true;C.Hero->SetPrimaryInputHeld(true);}
    void Tick(FMCPerkPreviewContext& C,float Dt)override
    {
        if(!bCommitted||bDone)return;Age+=FMath::Max(0.f,Dt);auto* B=Block.Get();
        if(B&&C.Hero&&C.Hero->ValidatedSwingCount>SwingsBefore&&C.Hero->ConfirmedHitCount>HitsBefore&&B->Health<InitialHealth)
            if(B->bBroken&&B->GetAcceptedShatterSerial()!=BeforeSerial&&!bAccepted) {
                bAccepted=true;AcceptedAt=Age;C.Hero->SetPrimaryInputHeld(false);
                if(C.RecordEvent){C.RecordEvent(TEXT("NativePickaxeBreakAccepted"));C.RecordEvent(TEXT("Shatter"));}
            }
        // Combat consumes press edges; holding primary repeats only food chop
        // and frozen-leg self-rescue. A normal80-health block needs more than
        // one native pickaxe contact. Press again only after real recovery.
        if(!bAccepted&&Age<12.f&&B&&!B->bBroken&&C.Hero&&C.Hero->CanWork()&&C.Hero->Inventory
            &&C.Hero->Inventory->Selected==EMCToolSlot::Pickaxe
            &&C.Hero->GetToolSwingElapsed()>=C.Hero->Inventory->SwingDuration()+.05f) {
            C.Hero->SetPrimaryInputHeld(false);C.Hero->SetPrimaryInputHeld(true);
            if(C.RecordEvent)C.RecordEvent(TEXT("NativePickaxeInput"));
        }
        if(bAccepted&&Age-AcceptedAt>.6f)bDone=true;
        if(Age>12&&!bAccepted){Release(C);bDone=true;if(C.ReportStatus)C.ReportStatus(TEXT("Штатная кирка не подтвердила принятую поломку; Shatter не подставлен."));if(C.RecordEvent)C.RecordEvent(TEXT("ScenarioRejected"));}
    }
    void Release(FMCPerkPreviewContext& C)override{if(C.Hero)C.Hero->SetPrimaryInputHeld(false);}
    void Cancel(FMCPerkPreviewContext& C)override
    {
        Release(C);if(auto* P=Presentation.Get())if(auto* S=P->GetSource(TEXT("World.ColdCola")))S->StopSource(TEXT("World.ColdCola"),true);
        if(auto* B=Block.Get())B->Destroy();Block.Reset();
        if(bHasSelection&&C.Hero&&C.Hero->Inventory)C.Hero->Inventory->ServerSelect(PreviousSlot);bHasSelection=false;bDone=true;
    }
    void Reset(FMCPerkPreviewContext& C)override{Cancel(C);Presentation.Reset();}
    bool IsComplete(const FMCPerkPreviewContext&)const override{return bDone;}
    FString GetPhaseName(const FMCPerkPreviewContext&)const override{return bDone?TEXT("Настоящий Shatter завершён"):TEXT("Обычный ввод кирки → реальный физический блок → принятая поломка");}
    float GetTailSeconds()const override{return .5f;}
private:
    EMCToolSlot PreviousSlot=EMCToolSlot::Brush;bool bHasSelection=false,bCommitted=false,bDone=false,bAccepted=false;
    int32 SwingsBefore=0,HitsBefore=0;uint32 BeforeSerial=0;float InitialHealth=0,Age=0,AcceptedAt=0;
    TWeakObjectPtr<AMCIceBlock> Block;TWeakObjectPtr<UMCWorldPresentationComponent> Presentation;
};
void Register()
{
    const auto* C=MCVFXSourceContracts::Find(TEXT("World.ColdCola"));if(!C)return;
    FMCPerkPreviewAdapterEntry E;E.PerkID=C->SourceID;E.Kind=EMCPerkPreviewAdapterKind::Context;E.Label=C->Label;E.Stages=C->Events;E.bSupportsProfileOverride=true;
    E.Scenarios={EMCPerkPreviewScenario::Target};E.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(TEXT("Кирка · настоящий Shatter блока; полный климат этим сценарием не проверяется")));
    E.DefaultSystems.Add(TEXT("Shatter"),GetDefault<AMCIceBlock>()->OriginalShatterSystem);
    E.StageLabels.Add(TEXT("Shatter"),FText::FromString(TEXT("Принятое раскалывание")));E.StageLabels.Add(TEXT("Frost"),FText::FromString(TEXT("Климат · исходный MPC и независимые дополнительные слои")));
    E.Create=[]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FAdapter>();};MCPerkPreviewAdapters::Register(MoveTemp(E));
}
}
