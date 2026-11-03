#include "MCYawnWaterPreviewAdapters.h"
#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewGameMode.h"
#include "MCVFXSourceContracts.h"
#include "MCVFXPresentationComponent.h"
#include "MCWorldPresentationComponent.h"
#include "MCReactionVFX.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCPlayerState.h"
#include "MCPerkComponent.h"
#include "MCInventoryComponent.h"
#include "MCTongue.h"
#include "MCThroat.h"
#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "UObject/Package.h"

namespace MCYawnWaterPreview
{
/** Calls ordinary native owners/input, never a scheduled cosmetic cue. */
class FAdapter final:public IMCPerkPreviewAdapter
{
public:
    explicit FAdapter(bool Water):bWater(Water){}
    bool Prepare(FMCPerkPreviewContext& C,FString& Error)override
    {
        ID=bWater?TEXT("Tool.Watergun"):TEXT("World.Yawn");
        if(!C.World||C.World->WorldType!=EWorldType::Game||C.World->GetOutermost()!=GetTransientPackage()
            ||!C.World->GetAuthGameMode<AMCPerkPreviewGameMode>()||!IsValid(C.Hero)||!C.Hero->HasAuthority()||!C.Hero->Status||!C.Hero->Status->IsAlive())
        {Error=TEXT("Нужен настоящий authority-персонаж изолированного Composer мира.");return false;}
        C.Hero->SetPrimaryInputHeld(false);
        if(bWater) {
            auto* Inventory=C.Hero->Inventory.Get();auto* Perks=C.PlayerState?C.PlayerState->Perks.Get():nullptr;
            const auto* Table=Perks?Perks->GetPerkTable():nullptr;
            if(!Inventory||!Table||Table->GetRowStruct()!=FMCPerkDefinition::StaticStruct())
            {Error=TEXT("Не загружены обычный инвентарь и сохранённая DT усилений.");return false;}
            auto Names=Table->GetRowNames();Names.Sort([](FName A,FName B){return A.LexicalLess(B);});
            bool bSavedEnabledWatergun=false;
            for(FName Name:Names)if(const auto* Row=Table->FindRow<FMCPerkDefinition>(Name,TEXT("ComposerWatergunAvailability"),false))
                bSavedEnabledWatergun|=Row->bEnabled&&Row->ToolUpgrade==EMCToolUpgrade::Watergun;
            bool Granted=Inventory->HasUpgrade(EMCToolUpgrade::Watergun);
            for(FName Name:Names)if(!Granted)if(const auto* Row=Table->FindRow<FMCPerkDefinition>(Name,TEXT("ComposerWatergun"),false))
                if(Row->bEnabled&&Row->ToolUpgrade==EMCToolUpgrade::Watergun&&Perks->ServerGrantPerk(Name))Granted=Inventory->HasUpgrade(EMCToolUpgrade::Watergun);
            if(!Granted){Error=bSavedEnabledWatergun?TEXT("Штатная выдача сохранённого enabled Watergun upgrade отклонена."):
                TEXT("Контекстный сценарий недоступен: в сохранённой DT_Perks улучшение Watergun отключено. Слои NS/SFX доступны для редактирования; строки усилений не включаются ради preview.");return false;}
            Inventory->ServerSelect(EMCToolSlot::Spray);
            if(!Inventory->bPressureMode)Inventory->ServerSelect(EMCToolSlot::Spray);
            if(Inventory->Selected!=EMCToolSlot::Spray||!Inventory->bPressureMode)
            {Error=TEXT("Штатный выбор режима стрельбы отклонён.");return false;}
            if(!C.AddBox){Error=TEXT("Нет штатной поверхности мишени песочницы.");return false;}
            Target=C.AddBox(TEXT("WatergunRealContact"),C.Hero->GetActorLocation()+C.Hero->GetActorForwardVector()*600+FVector(0,0,40),FVector(1,4,4));
            if(!Target.IsValid()){Error=TEXT("Не создана настоящая collision-мишень.");return false;}
            Presentation=MCWorldPresentation::PrepareOwner(C.Hero,{ID});
            BeforeShot=Inventory->LastWaterShotAt;BeforeAmmo=Inventory->WaterAmmo;
        } else {
            auto* Source=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/Arena/V10/SM_TongueSurface.SM_TongueSurface"));
            if(!Source){Error=TEXT("Нет сохранённой поверхности настоящего языка.");return false;}
            const FTransform FloorPose(FVector(0,0,20));
            auto* T=C.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FloorPose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(!T)return false;Tongue=T;C.Targets.Add(T);T->SourceMesh=Source;T->bAutomaticYawns=false;T->FinishSpawning(FloorPose);
            if(T->CurrentVertices().IsEmpty()){Error=TEXT("Настоящий язык не прочитал свою сохранённую топологию.");return false;}
            auto* M=C.World->SpawnActor<AMCThroat>(AMCThroat::StaticClass(),FTransform(FVector(700,0,200)));
            if(!M)return false;Mouth=M;C.Targets.Add(M);
            if(M->VacuumInlet().ContainsNaN()){Error=TEXT("Настоящее отверстие глотки не определено.");return false;}
            Presentation=MCWorldPresentation::PrepareOwner(T,{ID});
        }
        if(!Presentation.IsValid()){Error=TEXT("Нет канала настоящего владельца.");return false;}
        if(C.RequestFramingUpdate)C.RequestFramingUpdate();return true;
    }
    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error)override
    {
        auto* P=Presentation.Get();const auto* Contract=MCVFXSourceContracts::Find(ID);
        if(!P||!Contract||!P->PrepareSources(Error,true))return false;
        auto* S=P->GetSource(ID);return S&&(!C.PresentationOverride||S->PrepareScopedOverride(ID,Contract->Events,C.PresentationOverride,Error));
    }
    void GetPreparedVisualSystems(const FMCPerkPreviewContext&,TArray<UNiagaraSystem*>& Out)const override
    {if(auto* P=Presentation.Get())if(auto* S=P->GetSource(ID))S->GetPreparedResources(Out);}
    void OnCommitted(FMCPerkPreviewContext& C)override
    {
        bCommitted=true;
        if(bWater) {
            auto* Wall=Target.Get();if(!Wall||!C.Controller){Reject(C,TEXT("Мишень или настоящий controller исчезли."));return;}
            const FVector Point=Wall->GetActorLocation();
            if(C.AimCamera){C.AimCamera->SetActorLocation(C.Hero->GetActorLocation()+FVector(-180,0,95));C.AimCamera->SetActorRotation((Point-C.AimCamera->GetActorLocation()).Rotation());C.Controller->SetViewTarget(C.AimCamera);}
            C.Controller->SetControlRotation((Point-C.Hero->GetActorLocation()).Rotation());C.Hero->SetPrimaryInputHeld(true);
        } else if(auto* T=Tongue.Get()) {
            bNativeStarted=T->StartYawn(T->YawnDuration)&&T->IsYawnActive()&&C.Hero->IsYawning();
            if(!bNativeStarted)Reject(C,TEXT("Настоящий StartYawn отклонён; событие не подменяется."));
        }
    }
    void Tick(FMCPerkPreviewContext& C,float Dt)override
    {
        if(!bCommitted||bDone||bRejected)return;Age+=FMath::Max(0.f,Dt);
        if(bWater) {
            const auto* I=C.Hero->Inventory.Get();
            if(I&&I->LastWaterShotAt>BeforeShot&&I->WaterAmmo<BeforeAmmo){bNativeStarted=true;C.Hero->SetPrimaryInputHeld(false);}
        }
        for(TActorIterator<AMCReactionVFX> It(C.World);It;++It) {
            auto* R=*It;if(R->GetPresentationSourceID()!=ID||R->GetOwner()!=(bWater?static_cast<AActor*>(C.Hero):Mouth.Get()))continue;
            if(!R->GetPreparedSourceHandoffError().IsEmpty()){Reject(C,R->GetPreparedSourceHandoffError());return;}
            if(!R->HasPreparedSourceHandoff()||!R->HasAcceptedComposerEvent(R->GetPresentationEvent()))continue;
            if(bWater) {
                if(R->GetPresentationEvent()==TEXT("Shot")&&!Shot.IsValid())Shot=R;
                if(R->GetPresentationEvent()==TEXT("Impact")&&!Impact.IsValid())Impact=R;
            } else if(!bFirstSeen&&bNativeStarted&&R->GetPresentationEvent()==TEXT("Wind")) {
                bFirstSeen=true;if(C.RecordEvent)C.RecordEvent(TEXT("Wind"));
            }
        }
        if(bWater&&bNativeStarted&&!bFirstSeen&&Shot.IsValid()&&Impact.IsValid()) {
            auto* S=Shot.Get();auto* I=Impact.Get();auto* Wall=Target.Get();
            FVector Origin,Extent;Wall->GetActorBounds(false,Origin,Extent);
            const FVector D=I->GetActorLocation()-S->GetActorLocation();
            const float RadiusError=FMath::Abs(float(D.Size())-S->FlowLength);
            if(RadiusError>.25f||FVector::CrossProduct(D,S->Direction).Size()>6.f
                ||!FBox(Origin-Extent,Origin+Extent).ExpandBy(6.f).IsInside(I->GetActorLocation()))
            {Reject(C,TEXT("Настоящий trace не подтвердил общий конец струи и контакт с мишенью."));return;}
            bFirstSeen=true;AcceptedAt=Age;if(C.RecordEvent){C.RecordEvent(TEXT("Shot"));C.RecordEvent(TEXT("Impact"));}
        }
        if(bWater) {
            if(bFirstSeen&&Age-AcceptedAt>.6f)bDone=true;
            if(Age>8&&!bFirstSeen)Reject(C,TEXT("Штатный ввод не подтвердил выстрел и настоящее попадание."));
        } else if(bFirstSeen)if(auto* T=Tongue.Get();T&&!T->IsYawnActive()&&!C.Hero->IsYawning()) {
            bDone=true;if(C.RecordEvent)C.RecordEvent(TEXT("NativeYawnFinished"));
        }
    }
    void Release(FMCPerkPreviewContext& C)override{if(C.Hero)C.Hero->SetPrimaryInputHeld(false);}
    void Cancel(FMCPerkPreviewContext& C)override
    {Release(C);if(auto* T=Tongue.Get())T->ResetYawn();if(auto* P=Presentation.Get())P->StopStates(ID);}
    void Reset(FMCPerkPreviewContext& C)override
    {Cancel(C);if(auto* T=Tongue.Get())T->Destroy();if(auto* M=Mouth.Get())M->Destroy();if(auto* W=Target.Get())W->Destroy();Tongue.Reset();Mouth.Reset();Target.Reset();Presentation.Reset();}
    bool IsComplete(const FMCPerkPreviewContext&)const override{return bDone||bRejected;}
    FString GetPhaseName(const FMCPerkPreviewContext&)const override
    {return bRejected?TEXT("Настоящее событие не подтверждено · см. причину"):bDone?TEXT("Настоящее событие завершено"):bWater?TEXT("Штатный Watergun ввод → trace → общий конец Shot/Impact"):TEXT("Настоящий StartYawn → ветер во вход глотки → штатное завершение");}
    float GetTailSeconds()const override{return .3f;}
private:
    void Reject(FMCPerkPreviewContext& C,const FString& Reason)
    {bRejected=true;if(C.Hero)C.Hero->SetPrimaryInputHeld(false);if(C.ReportStatus)C.ReportStatus(Reason);if(C.RecordEvent)C.RecordEvent(TEXT("ScenarioRejected"));}
    bool bWater=false,bCommitted=false,bNativeStarted=false,bFirstSeen=false,bDone=false,bRejected=false;
    FName ID;float Age=0,AcceptedAt=0;double BeforeShot=-100;int32 BeforeAmmo=0;
    TWeakObjectPtr<AMCTongue> Tongue;TWeakObjectPtr<AMCThroat> Mouth;TWeakObjectPtr<AStaticMeshActor> Target;
    TWeakObjectPtr<UMCWorldPresentationComponent> Presentation;TWeakObjectPtr<AMCReactionVFX> Shot,Impact;
};
void Register()
{
    for(bool Water:{false,true}) {
        const FName ID=Water?TEXT("Tool.Watergun"):TEXT("World.Yawn");const auto* C=MCVFXSourceContracts::Find(ID);if(!C)continue;
        FMCPerkPreviewAdapterEntry E;E.PerkID=ID;E.Kind=EMCPerkPreviewAdapterKind::Context;E.Label=C->Label;E.Stages=C->Events;
        E.Scenarios={EMCPerkPreviewScenario::Target};E.bSupportsProfileOverride=true;
        E.StageLabels.Add(TEXT("Wind"),FText::FromString(TEXT("Ветер")));E.StageLabels.Add(TEXT("Shot"),FText::FromString(TEXT("Выстрел")));E.StageLabels.Add(TEXT("Impact"),FText::FromString(TEXT("Попадание")));
        E.Create=[Water]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FAdapter>(Water);};MCPerkPreviewAdapters::Register(MoveTemp(E));
    }
}
}
