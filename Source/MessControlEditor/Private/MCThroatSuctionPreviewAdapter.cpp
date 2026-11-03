#include "MCThroatSuctionPreviewAdapter.h"
#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewGameMode.h"
#include "MCVFXSourceContracts.h"
#include "MCVFXPresentationComponent.h"
#include "MCWorldPresentationComponent.h"
#include "MCThroatVortex.h"
#include "MCThroat.h"
#include "MCTongue.h"
#include "MCFoodActor.h"
#include "MCToothCharacter.h"
#include "MCDayPlan.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "UObject/Package.h"

namespace MCThroatSuctionPreview
{
class FAdapter final:public IMCPerkPreviewAdapter
{
public:
    ~FAdapter()override{Unlisten();}
    bool Prepare(FMCPerkPreviewContext& C,FString& Error)override
    {
        if(!C.World||C.World->WorldType!=EWorldType::Game||C.World->GetOutermost()!=GetTransientPackage()
            ||!C.World->GetAuthGameMode<AMCPerkPreviewGameMode>()||!IsValid(C.Hero)||!C.Hero->HasAuthority())
        {Error=TEXT("Нужен изолированный authority Composer-мир.");return false;}
        auto* Saved=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
        auto* Mesh=Saved?DuplicateObject<UStaticMesh>(Saved,GetTransientPackage()):nullptr;
        if(!Mesh)return false;Mesh->ClearFlags(RF_Public|RF_Standalone);Mesh->SetFlags(RF_Transient);Mesh->bAllowCPUAccess=true;
        const FTransform FloorPose(FRotator::ZeroRotator,FVector(0,0,20),FVector(70,70,1));
        auto* Ground=C.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FloorPose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Ground)return false;Tongue=Ground;C.Targets.Add(Ground);Ground->SourceMesh=Mesh;Ground->bAutomaticYawns=false;Ground->FinishSpawning(FloorPose);
        Ground->SetActorTickEnabled(false);Ground->Surface->SetVisibility(false);Ground->Surface->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        auto* T=C.World->SpawnActor<AMCThroat>(AMCThroat::StaticClass(),FTransform(FVector(700,0,200)));
        if(!T)return false;Throat=T;C.Targets.Add(T);T->SetActorTickEnabled(false);
        if(!T->bManualIntake){Error=TEXT("Нужен штатный ручной приём без legacy-подмены.");return false;}
        if(T->AuthoredMouth&&T->AuthoredMouth->GetSkeletalMeshAsset())
        {Error=TEXT("Ось произвольного authored-mouth не доказана; этот сценарий не подменяет его геометрию.");return false;}
        FVector Aim;float Radius;if(!T->GetIntakeAimTarget(Aim,Radius)){Error=TEXT("Нет настоящего отверстия глотки.");return false;}
        const auto* Table=GetDefault<UMCDayPlan>()->Menu.LoadSynchronous();
        if(!Table||Table->GetRowStruct()!=FMCFoodRow::StaticStruct())return false;
        auto Names=Table->GetRowNames();Names.Sort([](FName A,FName B){return A.LexicalLess(B);});
        const FMCFoodRow* Row=nullptr;FName Name;
        for(FName N:Names)if(const auto* R=Table->FindRow<FMCFoodRow>(N,TEXT("ComposerThroatIntake"),false))
            if(R->Kind==EMCFoodKind::Food&&!R->WholeMeshes.IsEmpty()){Row=R;Name=N;break;}
        if(!Row){Error=TEXT("Нет сохранённой строки нормальной еды для настоящего приёма.");return false;}
        Direction=T->GetActorForwardVector();LaunchPoint=Aim+Direction*30;FoodPose=FTransform(LaunchPoint);
        auto* F=C.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),FoodPose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!F)return false;Food=F;C.Targets.Add(F);FRandomStream Choice(743);F->Initialize(false,Direction);F->ConfigureItem(Name,*Row,Choice);
        const auto Actual=F->Visual->GetStaticMesh();
        if(!Actual||!Row->WholeMeshes.ContainsByPredicate([Actual](const auto& X){return X.Get()==Actual.Get();}))
        {Error=TEXT("Prototype fallback еды не принимается.");return false;}
        BeforeSwallowed=T->FoodSwallowed;Presentation=MCWorldPresentation::PrepareOwner(T,{TEXT("World.Throat.Suction")});
        if(!Presentation.IsValid())return false;
        if(C.ReportStatus)C.ReportStatus(TEXT("Настоящая сохранённая еда → штатный физический бросок → ручной приём → Vortex. Четыре события отправляет настоящий владелец: начало, приём, всасывание и нормальное завершение."));
        if(C.RequestFramingUpdate)C.RequestFramingUpdate();return true;
    }
    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error)override
    {
        auto* P=Presentation.Get();const auto* Contract=MCVFXSourceContracts::Find(TEXT("World.Throat.Suction"));
        if(!P||!Contract||!P->PrepareSources(Error,true))return false;auto* S=P->GetSource(Contract->SourceID);
        return S&&(!C.PresentationOverride||S->PrepareScopedOverride(Contract->SourceID,Contract->Events,C.PresentationOverride,Error));
    }
    void GetPreparedVisualSystems(const FMCPerkPreviewContext&,TArray<UNiagaraSystem*>& Out)const override
    {if(auto* P=Presentation.Get())if(auto* S=P->GetSource(TEXT("World.Throat.Suction")))S->GetPreparedResources(Out);}
    void OnCommitted(FMCPerkPreviewContext& C)override
    {
        auto* T=Throat.Get();auto* F=Food.Get();if(!T||!F){Reject(C,TEXT("Владелец или еда исчезли."));return;}
        T->SetActorTickEnabled(true);F->FinishSpawning(FoodPose);F->LaunchManualThrow(C.Hero,Direction,400);
        bCommitted=F->WasManuallyThrown();if(!bCommitted)Reject(C,TEXT("Штатный физический бросок отклонён."));
    }
    void Tick(FMCPerkPreviewContext& C,float Dt)override
    {
        Context=&C;
        if(!bCommitted||bDone||bRejected)return;Age+=FMath::Max(0.f,Dt);auto* T=Throat.Get();if(!T)return;
        if(!bIntake)if(auto* F=Food.Get();F&&F->Phase==EMCFoodPhase::Swallowing&&T->QueuedFoodCount>0) {
            bIntake=true;if(C.RecordEvent)C.RecordEvent(TEXT("NativeIntakeAccepted"));
        }
        const auto* State=C.World->GetGameState();const double Now=State?State->GetServerWorldTimeSeconds():C.World->GetTimeSeconds();
        for(TActorIterator<AMCThroatVortex> It(C.World);It;++It)if(It->GetOwner()==T) {
            auto* V=*It;
            if(!V->GetPreparedSourceHandoffError().IsEmpty()){Reject(C,V->GetPreparedSourceHandoffError());return;}
            if(!V->HasPreparedSourceHandoff())continue;
            NativeEnd=FMath::Max(NativeEnd,V->StartedAt+V->Duration);
            if(auto* P=V->GetComposerPresentation())if(auto* Channel=P->GetSource(TEXT("World.Throat.Suction"));Channel&&ObservedChannel.Get()!=Channel) {
                Unlisten();ObservedChannel=Channel;
                EventHandle=Channel->OnSourceEventAccepted.AddRaw(this,&FAdapter::OnAccepted);
            }
            if(V->HasAcceptedComposerIntake()&&!bIntakeArt){bIntakeArt=true;if(C.RecordEvent)C.RecordEvent(TEXT("Intake"));}
            if(V->HasAcceptedComposerEnd()&&!bEnd){bEnd=true;if(C.RecordEvent)C.RecordEvent(TEXT("End"));}
            if(V->GetActorLocation().ContainsNaN()||V->InwardDirection.IsNearlyZero()||V->FoodSources.Num()>24)
            {Reject(C,TEXT("Фактический контекст Vortex невалиден."));return;}
            if(V->HasAcceptedComposerStart()&&!bStart){bStart=true;if(C.RecordEvent)C.RecordEvent(TEXT("Start"));}
            if(V->HasAcceptedComposerLoop()&&!bLoop){bLoop=true;if(C.RecordEvent)C.RecordEvent(TEXT("Loop"));}
        }
        if(bIntake&&bStart&&bLoop&&bIntakeArt&&bEnd&&T->FoodSwallowed>BeforeSwallowed&&NativeEnd>0&&Now>=NativeEnd+.2) {
            bDone=true;if(C.RecordEvent)C.RecordEvent(TEXT("NativeIntakeFinished"));
        }
        if(Age>12&&!bDone)Reject(C,TEXT("Штатный приём и четыре подготовленных события не подтверждены."));
    }
    void Cancel(FMCPerkPreviewContext& C)override
    {
        Unlisten();
        if(auto* T=Throat.Get()) {
            T->ResetSwallow();TArray<AMCThroatVortex*> Owned;
            for(TActorIterator<AMCThroatVortex> It(C.World);It;++It)if(It->GetOwner()==T)Owned.Add(*It);
            for(auto* V:Owned){if(auto* P=V->GetComposerPresentation())P->StopStates(TEXT("World.Throat.Suction"));V->Destroy();}
        }
        if(auto* P=Presentation.Get())P->StopStates(TEXT("World.Throat.Suction"));
    }
    void Reset(FMCPerkPreviewContext& C)override
    {Cancel(C);if(auto* F=Food.Get())F->Destroy();if(auto* T=Throat.Get())T->Destroy();if(auto* G=Tongue.Get())G->Destroy();Food.Reset();Throat.Reset();Tongue.Reset();Presentation.Reset();}
    bool IsComplete(const FMCPerkPreviewContext&)const override{return bDone||bRejected;}
    FString GetPhaseName(const FMCPerkPreviewContext&)const override
    {return bRejected?TEXT("Настоящий приём не подтверждён"):bDone?TEXT("Реальный приём и четыре события завершены"):bLoop?TEXT("Настоящий Vortex · реальные следы еды"):TEXT("Штатный бросок → отверстие глотки → ручной приём");}
    float GetTailSeconds()const override{return .3f;}
private:
    void Unlisten(){if(auto* P=ObservedChannel.Get())P->OnSourceEventAccepted.Remove(EventHandle);ObservedChannel.Reset();EventHandle.Reset();}
    void OnAccepted(FName ID,FName Event,EMCVFXPresentationResult Result)
    {
        if(!Context||ID!=TEXT("World.Throat.Suction")||Result==EMCVFXPresentationResult::Rejected)return;
        if(Event==TEXT("Intake")&&!bIntakeArt){bIntakeArt=true;if(Context->RecordEvent)Context->RecordEvent(Event);}
        if(Event==TEXT("End")&&!bEnd){bEnd=true;if(Context->RecordEvent)Context->RecordEvent(Event);}
    }
    void Reject(FMCPerkPreviewContext& C,const FString& Why){bRejected=true;if(C.ReportStatus)C.ReportStatus(Why);if(C.RecordEvent)C.RecordEvent(TEXT("ScenarioRejected"));}
    TWeakObjectPtr<AMCThroat> Throat;TWeakObjectPtr<AMCTongue> Tongue;TWeakObjectPtr<AMCFoodActor> Food;
    TWeakObjectPtr<UMCWorldPresentationComponent> Presentation;FTransform FoodPose;FVector LaunchPoint,Direction;
    TWeakObjectPtr<UMCVFXPresentationComponent> ObservedChannel;FDelegateHandle EventHandle;FMCPerkPreviewContext* Context=nullptr;
    int32 BeforeSwallowed=0;float Age=0;double NativeEnd=0;bool bCommitted=false,bIntake=false,bStart=false,bLoop=false,bIntakeArt=false,bEnd=false,bDone=false,bRejected=false;
};
void Register()
{
    const auto* C=MCVFXSourceContracts::Find(TEXT("World.Throat.Suction"));if(!C)return;
    FMCPerkPreviewAdapterEntry E;E.PerkID=C->SourceID;E.Kind=EMCPerkPreviewAdapterKind::Context;E.Label=C->Label;E.Stages=C->Events;
    E.bSupportsProfileOverride=true;E.Scenarios={EMCPerkPreviewScenario::Target};
    E.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(TEXT("Ручной приём · полный цикл")));
    E.StageLabels.Add(TEXT("Start"),FText::FromString(TEXT("Начало")));E.StageLabels.Add(TEXT("Loop"),FText::FromString(TEXT("Всасывание")));
    E.Create=[]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FAdapter>();};MCPerkPreviewAdapters::Register(MoveTemp(E));
}
}
