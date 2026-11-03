#include "MCCoffeeRiverPreviewAdapter.h"
#include "MCPerkPreviewAdapter.h"
#include "MCCoffeeFlood.h"
#include "MCCoffeeRiverPresentation.h"
#include "MCWorldPresentationComponent.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXSourceContracts.h"
#include "MCGameMode.h"
#include "MCDayPlan.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "Engine/World.h"
#include "NiagaraSystem.h"
#include "ProceduralMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/StrongObjectPtr.h"

namespace MCCoffeeRiverPreview
{
class FAdapter final:public IMCPerkPreviewAdapter
{
public:
    bool Prepare(FMCPerkPreviewContext& C,FString& Error)override
    {
        if(!C.World||!C.Hero||!C.Hero->HasAuthority())return false;
        Plan.Reset(GetDefault<AMCGameMode>()->FirstDayPlan.LoadSynchronous());
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/Arena/V10/SM_TongueSurface.SM_TongueSurface"));
        if(!Plan||!Mesh){Error=TEXT("The real Day01 plan/tongue source mesh is unavailable.");return false;}
        auto* Ground=C.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FTransform::Identity);
        if(!Ground)return false;Ground->SourceMesh=Mesh;Ground->bAutomaticYawns=false;Ground->FinishSpawning(FTransform::Identity);
        C.Targets.Add(Ground);Tongue=Ground;
        FHitResult Hit;const FVector P=Ground->Surface->Bounds.Origin+FVector(0,0,1000);
        if(!Ground->SurfacePoint(P,Hit)){Error=TEXT("Actual river fixture source has no physical tongue support.");return false;}
        C.Hero->SetActorLocation(Hit.ImpactPoint+FVector(0,0,110));
        FActorSpawnParameters Spawn;Spawn.Owner=Ground;Spawn.ObjectFlags=RF_Transient;
        auto* Actor=C.World->SpawnActor<AMCCoffeeFlood>(AMCCoffeeFlood::StaticClass(),FTransform::Identity,Spawn);
        if(!Actor)return false;Flood=Actor;C.Targets.Add(Actor);Committed=Seen=Done=false;return true;
    }
    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error)override
    {
        auto* A=Flood.Get();auto* P=A?A->GetRiverComposerPresentation():nullptr;
        if(!P||!P->PrepareSources(Error,true))return false;
        auto* Source=P->GetSource(TEXT("World.CoffeeRiver"));
        return Source&&(C.PresentationOverride?Source->PrepareProfileOverride(C.PresentationOverride,Error):Source->IsPrepared());
    }
    bool PrepareResidentPresentation(FMCPerkPreviewContext& C,FGuid Epoch,bool& Waiting,FString& Error)override
    {
        Waiting=false;auto* A=Flood.Get();auto* P=A?A->GetRiverComposerPresentation():nullptr;
        if(!P)return false;auto* Source=P->GetSource(TEXT("World.CoffeeRiver"));if(!Source)return false;TArray<UNiagaraSystem*> Systems;Source->GetPreparedResources(Systems);
        TArray<UObject*> Objects;for(auto* NS:Systems)Objects.Add(NS);
        auto* R=C.World->GetSubsystem<UMCCoffeeRiverPresentationResources>();
        if(!R||!R->PrepareLocation(Objects,Epoch,true,Error))return false;return R->PollRendering(Waiting,Error);
    }
    void GetPreparedVisualSystems(const FMCPerkPreviewContext&,TArray<UNiagaraSystem*>& Out)const override
    {if(auto* A=Flood.Get())if(auto* P=A->GetRiverComposerPresentation())if(auto* S=P->GetSource(TEXT("World.CoffeeRiver")))S->GetPreparedResources(Out);}
    void OnCommitted(FMCPerkPreviewContext&)override
    {if(auto* A=Flood.Get()){A->Start(Plan.Get());Committed=true;}}
    void Tick(FMCPerkPreviewContext& C,float)override
    {
        auto* A=Flood.Get();if(!A||!Committed)return;
        if(A->IsActive()&&A->HasAcceptedRiverComposerState()&&!Seen){Seen=true;if(C.RecordEvent)C.RecordEvent(TEXT("Flow"));}
        if(A->HasAcceptedRiverAccentsState()&&!AccentsSeen){AccentsSeen=true;if(C.RecordEvent)C.RecordEvent(TEXT("Accents"));}
        if((Seen||AccentsSeen)&&!A->IsActive()){Done=true;if(C.RecordEvent)C.RecordEvent(TEXT("NativeRiverFinished"));}
    }
    bool IsComplete(const FMCPerkPreviewContext&)const override{return Done;}
    FString GetPhaseName(const FMCPerkPreviewContext&)const override
    {return Done?TEXT("Настоящая река завершена"):Committed?TEXT("Река · настоящая поверхность, течение и мокрая область"):TEXT("Подготовка настоящего языка и реки");}
    void Cancel(FMCPerkPreviewContext&)override{if(auto* A=Flood.Get())A->Stop();}
    void Reset(FMCPerkPreviewContext& C)override{Cancel(C);if(auto* A=Flood.Get())A->Destroy();if(auto* T=Tongue.Get())T->Destroy();Flood.Reset();Tongue.Reset();Plan.Reset();}
private:
    TWeakObjectPtr<AMCCoffeeFlood> Flood;TWeakObjectPtr<AMCTongue> Tongue;TStrongObjectPtr<UMCDayPlan> Plan;
    bool Committed=false,Seen=false,AccentsSeen=false,Done=false;
};
void Register()
{
    const auto* Contract=MCVFXSourceContracts::Find(TEXT("World.CoffeeRiver"));if(!Contract)return;
    FMCPerkPreviewAdapterEntry Entry;Entry.PerkID=Contract->SourceID;Entry.Kind=EMCPerkPreviewAdapterKind::Context;
    Entry.Label=Contract->Label;Entry.Stages=Contract->Events;Entry.Scenarios={EMCPerkPreviewScenario::Target};Entry.bSupportsProfileOverride=true;
    Entry.StageLabels.Add(TEXT("Accents"),FText::FromString(TEXT("Прибой и слив")));
    Entry.Create=[](){return MakeUnique<FAdapter>();};MCPerkPreviewAdapters::Register(MoveTemp(Entry));
}
}
