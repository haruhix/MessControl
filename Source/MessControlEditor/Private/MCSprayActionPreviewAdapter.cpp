#include "MCSprayActionPreviewAdapter.h"
#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewGameMode.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXSourceContracts.h"
#include "MCWorldPresentationComponent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCInventoryComponent.h"
#include "MCPlayerState.h"
#include "MCPerkComponent.h"
#include "MCFirePatch.h"
#include "MCTongue.h"
#include "Camera/CameraActor.h"
#include "ProceduralMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "NiagaraSystem.h"
#include "UObject/Package.h"

namespace MCSprayActionPreviewAdapter
{
/** The only event producer is ordinary inventory tick and real Extinguish. */
class FAdapter final:public IMCPerkPreviewAdapter
{
public:
    bool Prepare(FMCPerkPreviewContext& C,FString& Error)override
    {
        if(C.PerkID!=TEXT("Tool.Spray")||!C.World||C.World->WorldType!=EWorldType::Game
            ||C.World->GetOutermost()!=GetTransientPackage()||!C.World->GetAuthGameMode<AMCPerkPreviewGameMode>()
            ||!C.Hero||!C.Hero->HasAuthority()||!C.Hero->CanWork()||!C.Hero->Inventory||!C.Controller)
        {Error=TEXT("Нужен штатный инвентарь authority-персонажа изолированного Composer мира.");return false;}
        C.Hero->SetPrimaryInputHeld(false);auto* I=C.Hero->Inventory.Get();bWater=C.Scenario==EMCPerkPreviewScenario::NearWall;
        PreviousSlot=I->Selected;PreviousPressure=I->bPressureMode;PreviousView=C.Controller->GetViewTarget();PreviousControl=C.Controller->GetControlRotation();
        if(bWater&&!I->HasUpgrade(EMCToolUpgrade::Watergun)) {
            auto* Perks=C.PlayerState?C.PlayerState->Perks.Get():nullptr;const auto* Table=Perks?Perks->GetPerkTable():nullptr;
            if(!Table||Table->GetRowStruct()!=FMCPerkDefinition::StaticStruct()){Error=TEXT("Нет сохранённой DT усилений.");return false;}
            auto Names=Table->GetRowNames();Names.Sort([](FName A,FName B){return A.LexicalLess(B);});
            for(FName Name:Names)if(Granted.IsNone())if(const auto* R=Table->FindRow<FMCPerkDefinition>(Name,TEXT("ComposerSprayCare"),false))
                if(R->bEnabled&&R->ToolUpgrade==EMCToolUpgrade::Watergun&&Perks->ServerGrantPerk(Name))Granted=Name;
        }
        if(I->HasUpgrade(EMCToolUpgrade::Watergun)!=bWater){Error=TEXT("Штатная выдача режима спрея не подтверждена.");return false;}
        I->ServerSelect(EMCToolSlot::Spray);if(bWater&&I->bPressureMode)I->ServerSelect(EMCToolSlot::Spray);
        if(I->Selected!=EMCToolSlot::Spray||(bWater&&I->bPressureMode)){Error=TEXT("Штатный выбор режима ухода отклонён.");return false;}
        auto* Saved=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
        auto* Private=Saved?DuplicateObject<UStaticMesh>(Saved,GetTransientPackage()):nullptr;
        if(!Private){Error=TEXT("Нет частной поверхности настоящего языка.");return false;}
        Private->ClearFlags(RF_Public|RF_Standalone);Private->SetFlags(RF_Transient);Private->bAllowCPUAccess=true;
        const FTransform FloorPose(FRotator::ZeroRotator,FVector(0,0,20),FVector(70,70,1));
        auto* T=C.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FloorPose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!T)return false;Tongue=T;C.Targets.Add(T);T->SourceMesh=Private;T->bAutomaticYawns=false;T->SetFlags(RF_Transient);T->FinishSpawning(FloorPose);
        T->SetActorTickEnabled(false);T->Surface->SetVisibility(false);T->Surface->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        FVector Point=C.Hero->GetActorLocation()+C.Hero->GetActorForwardVector()*145;Point.Z=25;FirePose=FTransform(Point);
        auto* F=C.World->SpawnActorDeferred<AMCFirePatch>(AMCFirePatch::StaticClass(),FirePose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!F)return false;Fire=F;C.Targets.Add(F);F->SetFlags(RF_Transient);F->BurnRadius=90;F->BurnSeconds=0;F->bCreateLesion=false;
        Presentation=MCWorldPresentation::PrepareOwner(C.Hero,{TEXT("Tool.Spray")});
        if(!Presentation.IsValid()){Error=TEXT("Нет общего подготовленного источника спрея.");return false;}
        BeforeContact=I->LastSprayAt;BeforeEnd=I->GetAcceptedSprayEndSerial();
        if(C.RequestFramingUpdate)C.RequestFramingUpdate();return true;
    }
    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error)override
    {
        auto* P=Presentation.Get();const auto* Contract=MCVFXSourceContracts::Find(TEXT("Tool.Spray"));
        if(!P||!Contract||!P->PrepareSources(Error,true))return false;
        auto* S=P->GetSource(TEXT("Tool.Spray"));if(!S)return false;
        // Load only during preparation; actual original components retain these.
        auto* I=C.Hero->Inventory.Get();if(!I->OriginalSpraySystem.LoadSynchronous()||!I->OriginalWaterCareSystem.LoadSynchronous())
        {Error=TEXT("Один из двух настоящих NS спрея отсутствует.");return false;}
        return !C.PresentationOverride||S->PrepareScopedOverride(TEXT("Tool.Spray"),Contract->Events,C.PresentationOverride,Error);
    }
    void GetPreparedVisualSystems(const FMCPerkPreviewContext& C,TArray<UNiagaraSystem*>& Out)const override
    {
        if(auto* P=Presentation.Get())if(auto* S=P->GetSource(TEXT("Tool.Spray")))S->GetPreparedResources(Out);
        if(C.Hero&&C.Hero->Inventory){Out.AddUnique(C.Hero->Inventory->OriginalSpraySystem.Get());Out.AddUnique(C.Hero->Inventory->OriginalWaterCareSystem.Get());}
    }
    void OnCommitted(FMCPerkPreviewContext& C)override
    {
        auto* F=Fire.Get();if(!F){Reject(C,TEXT("Настоящий контактный участок исчез."));return;}
        F->FinishSpawning(FirePose);LastHeat=F->Heat;bCommitted=true;
        if(C.AimCamera){C.AimCamera->SetActorLocation(C.Hero->GetActorLocation()+FVector(-180,0,95));C.AimCamera->SetActorRotation((F->GetActorLocation()-C.AimCamera->GetActorLocation()).Rotation());C.Controller->SetViewTarget(C.AimCamera);}
        C.Controller->SetControlRotation((F->GetActorLocation()-C.Hero->GetActorLocation()).Rotation());C.Hero->SetPrimaryInputHeld(true);
    }
    void Tick(FMCPerkPreviewContext& C,float Dt)override
    {
        if(!bCommitted||bDone)return;Age+=FMath::Max(0.f,Dt);auto* I=C.Hero?C.Hero->Inventory.Get():nullptr;
        if(!I){Reject(C,TEXT("Инвентарь исчез."));return;}
        const FName Event=bWater?TEXT("WaterCare"):TEXT("Start");
        if(!bStart&&I->GetAcceptedSprayPresentationEvent()==Event){bStart=true;if(C.RecordEvent)C.RecordEvent(Event);}
        if(auto* F=Fire.Get()) {
            if(F->Heat<LastHeat&&I->LastSprayAt>BeforeContact&&I->GetAcceptedSprayContactAt()>BeforeContact) {
                if(!bContact){bContact=true;ContactAt=Age;if(C.RecordEvent){C.RecordEvent(TEXT("Contact"));C.RecordEvent(TEXT("NativeExtinguishAccepted"));}}
            }
            LastHeat=F->Heat;
        }
        if(bStart&&bContact&&Age-ContactAt>.35f&&!bReleased){Release(C);bReleased=true;}
        if(bReleased&&I->GetAcceptedSprayEndSerial()!=BeforeEnd){if(!bEnd&&C.RecordEvent)C.RecordEvent(TEXT("End"));bEnd=true;bDone=true;}
        if(Age>10&&!bDone)Reject(C,TEXT("Штатный ввод не подтвердил nozzle, принятое тушение и завершение; эффекты не подставлены."));
    }
    void Release(FMCPerkPreviewContext& C)override{if(C.Hero)C.Hero->SetPrimaryInputHeld(false);}
    void Cancel(FMCPerkPreviewContext& C)override
    {
        if(bCleaned)return;bCleaned=true;
        Release(C);if(C.Hero&&C.Hero->Inventory)C.Hero->Inventory->ResetSprayPresentationForPreview();
        if(auto* P=Presentation.Get())if(auto* S=P->GetSource(TEXT("Tool.Spray")))S->StopSource(TEXT("Tool.Spray"),true);
        if(auto* F=Fire.Get())F->Destroy();if(auto* T=Tongue.Get())T->Destroy();Fire.Reset();Tongue.Reset();
        if(C.Hero&&C.Hero->Inventory){auto* I=C.Hero->Inventory.Get();
            if(I->bPressureMode!=PreviousPressure&&I->HasUpgrade(EMCToolUpgrade::Watergun)){I->ServerSelect(EMCToolSlot::Spray);if(I->bPressureMode!=PreviousPressure)I->ServerSelect(EMCToolSlot::Spray);}
            I->ServerSelect(PreviousSlot);}
        if(!Granted.IsNone()&&C.PlayerState&&C.PlayerState->Perks)C.PlayerState->Perks->RemovePerk(Granted);Granted=NAME_None;
        if(C.Controller){C.Controller->SetControlRotation(PreviousControl);if(PreviousView.IsValid())C.Controller->SetViewTarget(PreviousView.Get());}bDone=true;
    }
    void Reset(FMCPerkPreviewContext& C)override{Cancel(C);Presentation.Reset();}
    bool IsComplete(const FMCPerkPreviewContext&)const override{return bDone;}
    FString GetPhaseName(const FMCPerkPreviewContext&)const override{return bDone?TEXT("Штатный сценарий спрея завершён"):TEXT("Реальный режим ухода → штатный ввод → принятое тушение → отпускание");}
    float GetTailSeconds()const override{return .5f;}
private:
    void Reject(FMCPerkPreviewContext& C,const FString& Error){Release(C);bDone=true;if(C.ReportStatus)C.ReportStatus(Error);if(C.RecordEvent)C.RecordEvent(TEXT("ScenarioRejected"));}
    bool bWater=false,bCommitted=false,bStart=false,bContact=false,bReleased=false,bEnd=false,bDone=false,PreviousPressure=false,bCleaned=false;
    EMCToolSlot PreviousSlot=EMCToolSlot::Brush;FName Granted;uint32 BeforeEnd=0;double BeforeContact=-100;
    float Age=0,ContactAt=0,LastHeat=1;FTransform FirePose;FRotator PreviousControl;TWeakObjectPtr<AActor> PreviousView;
    TWeakObjectPtr<AMCTongue> Tongue;TWeakObjectPtr<AMCFirePatch> Fire;TWeakObjectPtr<UMCWorldPresentationComponent> Presentation;
};
void Register()
{
    const auto* C=MCVFXSourceContracts::Find(TEXT("Tool.Spray"));if(!C)return;
    const auto* I=GetDefault<UMCInventoryComponent>();FMCPerkPreviewAdapterEntry E;E.PerkID=C->SourceID;E.Label=C->Label;E.Stages=C->Events;
    E.Kind=EMCPerkPreviewAdapterKind::Context;E.bSupportsProfileOverride=true;E.Scenarios={EMCPerkPreviewScenario::Target,EMCPerkPreviewScenario::NearWall};
    E.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(TEXT("Обычный спрей · настоящее тушение")));
    E.ScenarioLabels.Add(EMCPerkPreviewScenario::NearWall,FText::FromString(TEXT("Watergun · штатный режим ухода")));
    E.StageLabels.Add(TEXT("Start"),FText::FromString(TEXT("Обычный спрей")));E.StageLabels.Add(TEXT("WaterCare"),FText::FromString(TEXT("Watergun · уход")));
    E.DefaultSystems.Add(TEXT("Start"),I->OriginalSpraySystem);E.DefaultSystems.Add(TEXT("WaterCare"),I->OriginalWaterCareSystem);
    E.Create=[]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FAdapter>();};MCPerkPreviewAdapters::Register(MoveTemp(E));
}
}
