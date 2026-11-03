#include "MCFoodRewardPreviewAdapters.h"
#include "MCPerkPreviewAdapter.h"
#include "MCVFXActionCatalog.h"
#include "MCVFXSourceContracts.h"
#include "MCVFXPresentationComponent.h"
#include "MCWorldPresentationComponent.h"
#include "MCFoodActor.h"
#include "MCReactionVFX.h"
#include "MCToothCharacter.h"
#include "MCFirePatch.h"
#include "MCMouthSurface.h"
#include "MCDayPlan.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"

namespace MCFoodRewardPreview
{
/** Real saved food; ConfigureItem happens before BeginPlay, ordinary native clocks
 * start only after the shared resource/audio precommit barrier has succeeded. */
class FFoodAdapter final:public IMCPerkPreviewAdapter
{
public:
    explicit FFoodAdapter(FName InID):ID(InID){}
    bool Prepare(FMCPerkPreviewContext& C,FString& Error)override
    {
        if(!C.World||!IsValid(C.Hero)||!C.Hero->HasAuthority())return false;
        const auto* Table=GetDefault<UMCDayPlan>()->Menu.LoadSynchronous();
        if(!Table||Table->GetRowStruct()!=FMCFoodRow::StaticStruct()){Error=TEXT("Сохранённое игровое меню еды недоступно.");return false;}
        auto Names=Table->GetRowNames();Names.Sort([](FName A,FName B){return A.LexicalLess(B);});
        const FMCFoodRow* Row=nullptr;FName Name;
        for(FName Candidate:Names)if(const auto* R=Table->FindRow<FMCFoodRow>(Candidate,TEXT("ComposerFood"),false)) {
            if(R->Kind!=(ID==TEXT("Food.Spicy")?EMCFoodKind::Spicy:EMCFoodKind::Food)||R->WholeMeshes.IsEmpty()||!FMath::IsFinite(R->Health)||R->Health<=0)continue;
            if(ID==TEXT("Food.Spoiled")&&(!FMath::IsFinite(R->SpoilSeconds)||R->SpoilSeconds<=0))continue;
            if(!Row||(ID==TEXT("Food.Spoiled")&&R->SpoilSeconds<Row->SpoilSeconds)){Row=R;Name=Candidate;}
        }
        if(!Row){Error=TEXT("В сохранённом меню нет настоящей еды нужного типа.");return false;}
        Pose=FTransform(C.Hero->GetActorQuat(),C.Hero->GetActorLocation()+C.Hero->GetActorForwardVector()*230+FVector(0,0,30));
        auto* A=C.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!A)return false;Food=A;C.Targets.Add(A);
        FRandomStream FixtureVariant(743);A->Initialize(false,C.Hero->GetActorForwardVector());A->ConfigureItem(Name,*Row,FixtureVariant);
        const auto Mesh=A->Visual->GetStaticMesh();
        if(!Mesh||!Row->WholeMeshes.ContainsByPredicate([Mesh](const auto& Ref){return Ref.Get()==Mesh.Get();}))
        {Error=TEXT("Настоящий mesh сохранённой строки не загрузился; prototype fallback не принимается.");return false;}
        Presentation=MCWorldPresentation::PrepareOwner(A,{ID});
        if(!Presentation.IsValid()){Error=TEXT("Не подготовлен настоящий владелец Composer.");return false;}
        RowName=Name;InitialHealth=A->Health;
        if(C.ReportStatus)C.ReportStatus(FString::Printf(TEXT("Строка %s; штатные HP %.1f, срок порчи %.1fс, фитиль %.1fс. Часы и параметры еды сохранены."),*Name.ToString(),A->Health,A->FoodData.SpoilSeconds,A->FoodData.FuseSeconds));
        return true;
    }
    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error)override
    {
        auto* P=Presentation.Get();const auto* Contract=MCVFXSourceContracts::Find(ID);
        if(!P||!Contract||!P->PrepareSources(Error,true))return false;
        auto* Source=P->GetSource(ID);if(!Source)return false;
        return !C.PresentationOverride||Source->PrepareScopedOverride(ID,Contract->Events,C.PresentationOverride,Error);
    }
    void GetPreparedVisualSystems(const FMCPerkPreviewContext&,TArray<UNiagaraSystem*>& Out)const override
    {if(auto* P=Presentation.Get())if(auto* S=P->GetSource(ID))S->GetPreparedResources(Out);}
    void OnCommitted(FMCPerkPreviewContext& C)override
    {
        auto* A=Food.Get();if(!A)return;
        A->FinishSpawning(Pose);bCommitted=true;
        if(ID==TEXT("Food.Break")) {
            // Public native accepted damage path; no HP assignment, synthetic cue,
            // economy award, gameplay RNG replay, or direct Reaction spawn.
            const bool Hit=A->HitFood(InitialHealth,C.Hero->GetActorForwardVector(),nullptr);
            bBoundary=Hit&&A->Health<=0&&A->IsDisposed();
        }
    }
    void Tick(FMCPerkPreviewContext& C,float)override
    {
        if(!bCommitted||bDone||bRejected)return;
        auto* A=Food.Get();if(!A){Reject(C,TEXT("Настоящая еда исчезла до подтверждения события."));return;}
        const auto* State=C.World->GetGameState();const double Now=State?State->GetServerWorldTimeSeconds():C.World->GetTimeSeconds();LastNow=Now;
        if(ID==TEXT("Food.Spicy")) {
            if(A->FuseEndsAt>0)AcceptedDeadline=A->FuseEndsAt;
            bBoundary=AcceptedDeadline>0&&Now>=AcceptedDeadline&&A->IsDisposed();
        } else if(ID==TEXT("Food.Spoiled"))bBoundary=A->bSpoiled&&A->SpoilAt>0&&Now>=A->SpoilAt;
        if(!bBoundary)return;
        for(TActorIterator<AMCReactionVFX> It(C.World);It;++It) {
            auto* FX=*It;if(FX->GetOwner()!=A||FX->GetPresentationSourceID()!=ID)continue;
            if(!FX->GetPreparedSourceHandoffError().IsEmpty()){Reject(C,FX->GetPreparedSourceHandoffError());return;}
            if(!FX->HasPreparedSourceHandoff())continue;
            const FName Main=ID==TEXT("Food.Break")?FName(TEXT("BreakArt")):ID==TEXT("Food.Spicy")?FName(TEXT("DetonateArt")):FName(TEXT("Loop"));
            if(FX->HasAcceptedComposerEvent(Main)&&!bMainSeen) {
                bMainSeen=true;AcceptedAt=Now;if(C.RecordEvent)C.RecordEvent(Main);
            }
            if(ID==TEXT("Food.Break")&&FX->HasAcceptedComposerEvent(TEXT("BreakDust"))&&!bDustSeen) {
                bDustSeen=true;if(C.RecordEvent)C.RecordEvent(TEXT("BreakDust"));
            }
        }
        if(!bMainSeen||(ID==TEXT("Food.Break")&&!bDustSeen))return;
        // This is an observation hold AFTER a real accepted loop, never a timer
        // that manufactures its cue. End through ordinary disposal to test reset.
        if(ID==TEXT("Food.Spoiled")) {
            if(Now-AcceptedAt<3.)return;A->Dispose();
            if(C.RecordEvent)C.RecordEvent(TEXT("NativeDisposed"));
        } else if(Now-AcceptedAt<.8)return;
        StopOwnedHazards();bDone=true;
    }
    bool IsComplete(const FMCPerkPreviewContext&)const override{return bDone||bRejected;}
    FString GetPhaseName(const FMCPerkPreviewContext&)const override
    {
        if(bRejected)return TEXT("Сценарий отклонён · см. причину");
        if(bDone)return TEXT("Настоящее событие принято и завершено");
        if(!bCommitted)return TEXT("Подготовка сохранённой еды до BeginPlay");
        if(const auto* A=Food.Get()) {
            if(ID==TEXT("Food.Spoiled"))return FString::Printf(TEXT("%s · до штатной порчи %.1f с (исходный срок %.1f с)"),*RowName.ToString(),FMath::Max(0.,A->SpoilAt-LastNow),A->FoodData.SpoilSeconds);
            if(ID==TEXT("Food.Spicy"))return FString::Printf(TEXT("%s · штатный фитиль %.1f с"),*RowName.ToString(),A->FuseRemaining());
        }
        return TEXT("Принятое повреждение → разрушение и пыль");
    }
    void Cancel(FMCPerkPreviewContext&)override
    {if(auto* A=Food.Get())if(!A->IsDisposed())A->Dispose();if(auto* P=Presentation.Get())P->StopStates(ID);StopOwnedHazards();}
    void Reset(FMCPerkPreviewContext& C)override
    {Cancel(C);if(auto* A=Food.Get())A->Destroy();Food.Reset();Presentation.Reset();}
    float GetTailSeconds()const override{return .3f;}
private:
    void Reject(FMCPerkPreviewContext& C,const FString& Reason)
    {bRejected=true;if(C.ReportStatus)C.ReportStatus(Reason);if(C.RecordEvent)C.RecordEvent(TEXT("ScenarioRejected"));}
    void StopOwnedHazards()
    {
        if(auto* A=Food.Get()) {
            for(AMCFirePatch* Fire:A->FireTrail)if(IsValid(Fire))Fire->Destroy();
            if(IsValid(A->BurnLesion))A->BurnLesion->Destroy();
        }
    }
    FName ID,RowName;FTransform Pose;TWeakObjectPtr<AMCFoodActor> Food;TWeakObjectPtr<UMCWorldPresentationComponent> Presentation;
    float InitialHealth=0;double AcceptedDeadline=0,AcceptedAt=0,LastNow=0;
    bool bCommitted=false,bBoundary=false,bMainSeen=false,bDustSeen=false,bDone=false,bRejected=false;
};
void Register()
{
    for(const FName ID:{FName(TEXT("Food.Break")),FName(TEXT("Food.Spicy")),FName(TEXT("Food.Spoiled"))}) {
        const auto* C=MCVFXSourceContracts::Find(ID);if(!C)continue;
        FMCPerkPreviewAdapterEntry E;E.PerkID=ID;E.Kind=EMCPerkPreviewAdapterKind::Context;E.Label=C->Label;E.Stages=C->Events;
        E.bSupportsProfileOverride=true;E.Scenarios={EMCPerkPreviewScenario::Target};
        E.StageLabels.Add(TEXT("BreakArt"),FText::FromString(TEXT("Разрушение")));E.StageLabels.Add(TEXT("BreakDust"),FText::FromString(TEXT("Пыль")));
        E.StageLabels.Add(TEXT("DetonateArt"),FText::FromString(TEXT("Взрыв")));E.StageLabels.Add(TEXT("Loop"),FText::FromString(TEXT("Запах")));
        E.Create=[ID]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FFoodAdapter>(ID);};MCPerkPreviewAdapters::Register(MoveTemp(E));
    }
    // The isolated Composer runner owns APlayerController without LocalPlayer/
    // viewport. Do not invent eligibility or replace the native security gate.
    if(const auto* C=MCVFXSourceContracts::Find(TEXT("Reward.TaskCoins"))) {
        FMCPerkPreviewAdapterEntry Metadata;Metadata.PerkID=C->SourceID;Metadata.Kind=EMCPerkPreviewAdapterKind::Context;
        Metadata.Label=C->Label;Metadata.Stages=C->Events;Metadata.bSupportsProfileOverride=true;
        // Empty Create/scenarios is explicit metadata-only capability, never a
        // provider which pretends to satisfy the native viewport eligibility.
        MCPerkPreviewAdapters::Register(MoveTemp(Metadata));
        FMCVFXActionCatalogueEntry E;E.ID=C->SourceID;E.Label=C->Label;E.Category=C->Category;E.SourceOwner=C->SourceOwner;E.Stages=C->Events;
        E.bSupportsPreview=false;E.bSupportsProfileOverride=true;
        E.UnsupportedReason=TEXT("Предпросмотр монет требует настоящий локальный viewport и готового игрока в PIE. Изолированная песочница Composer не имеет LocalPlayer; профиль остаётся редактируемым.");
        E.Description=FText::FromString(E.UnsupportedReason);MCVFXActionCatalog::RegisterSource(MoveTemp(E));
    }
}
}
