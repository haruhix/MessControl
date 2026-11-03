#include "MCStainSurfacePreviewAdapter.h"
#include "MCPerkPreviewAdapter.h"
#include "MCVFXSourceContracts.h"
#include "MCVFXPresentationComponent.h"
#include "MCWorldPresentationComponent.h"
#include "MCWorldPreloadSubsystem.h"
#include "MCNiagaraProceduralSurfaceResources.h"
#include "MCMouthSurface.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "MCBrushContactComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "NiagaraSystem.h"

namespace MCStainSurfacePreview
{
const FName SourceID(TEXT("EditorOnly.CleanStain"));

/** Same ordinary stain/brush path as the FloorBrush tool station. The private
 * fixture is initialized before the barrier; only normal held input may clean. */
class FAdapter final:public IMCPerkPreviewAdapter
{
public:
    bool Prepare(FMCPerkPreviewContext& C,FString& Error)override
    {
        if(!C.World||!IsValid(C.Hero)||!C.Hero->HasAuthority()||!C.Hero->Inventory||!C.Hero->BrushContact)
        {Error=TEXT("Для уборки нужны настоящий персонаж, щётка и изолированный authority-мир.");return false;}
        C.Hero->SetPrimaryInputHeld(false);C.Hero->SetSelfCareInput(false);
        PreviousSlot=C.Hero->Inventory->Selected;C.Hero->Inventory->ServerSelect(EMCToolSlot::Brush);
        if(C.Hero->Inventory->Selected!=EMCToolSlot::Brush)
        {Error=TEXT("Штатный выбор щётки отклонён.");return false;}
        bSelected=true;
        const FVector Aim=C.Hero->GetActorLocation()+C.Hero->GetActorForwardVector()*110.f;
        FHitResult Floor;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCComposerStainFixture),false,C.Hero);
        if(!C.World->LineTraceSingleByChannel(Floor,Aim+FVector(0,0,100),Aim-FVector(0,0,300),ECC_Visibility,Query)
            ||Floor.ImpactNormal.Z<.7f)
        {Error=TEXT("Под настоящим пятном нет доступного пола песочницы.");return false;}
        Pose=FTransform(FRotationMatrix::MakeFromZX(Floor.ImpactNormal,C.Hero->GetActorForwardVector()).ToQuat(),Floor.ImpactPoint+Floor.ImpactNormal*5.f);
        auto* Stain=C.World->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),Pose,nullptr,nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Stain){Error=TEXT("Не удалось создать настоящее пятно.");return false;}
        Patch=Stain;C.Targets.Add(Stain);
        // A small ordinary fixture keeps all native cells inside real hand reach.
        // Cleaning rate/radius, mask, tool readiness and completion rules stay native.
        Stain->bRandomizeLiquidSize=false;Stain->LiquidHalfSize=40.f;Stain->bShowCareLabel=true;
        Presentation=MCWorldPresentation::PrepareOwner(Stain,{SourceID});
        if(!Presentation.IsValid()){Error=TEXT("Не найден настоящий источник пятна.");return false;}
        Brush=C.Hero->BrushContact;bPreviousStrict=Brush->RequiresEvaluatedContact();
        Brush->SetRequireEvaluatedContact(true);bStrictApplied=true;
        if(C.ReportStatus)C.ReportStatus(TEXT("Настоящее пятно: подготовка исходной поверхности → удержание щётки → принятый контакт щетины → удаление маски → штатное завершение."));
        return true;
    }
    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error)override
    {
        auto* Owner=Presentation.Get();auto* Stain=Patch.Get();const auto* Contract=MCVFXSourceContracts::Find(SourceID);
        if(!Owner||!Stain||!Contract||!Owner->PrepareSources(Error,true))return false;
        auto* Source=Owner->GetSource(SourceID);
        if(!Source){Error=TEXT("Настоящий канал пятна не подготовлен.");return false;}
        if(C.PresentationOverride&&!Source->PrepareScopedOverride(SourceID,Contract->Events,C.PresentationOverride,Error))return false;
        // Preparation/override precede native BeginPlay and any surface activation.
        Stain->FinishSpawning(Pose);Stain->SetActorTickEnabled(false);
        Stain->Status->ApplyCoffee(1.f);
        if(!Stain->PrepareLiquidPresentation(Error))return false;
        InitialRemaining=Stain->RemainingLiquid();
        if(Stain->IsClean()||InitialRemaining<=0)
        {Error=TEXT("Штатная инициализация не создала настоящее загрязнение.");return false;}
        return true;
    }
    void GetPreparedVisualSystems(const FMCPerkPreviewContext& C,TArray<UNiagaraSystem*>& Out)const override
    {
        if(auto* Owner=Presentation.Get())if(auto* Source=Owner->GetSource(SourceID))Source->GetPreparedResources(Out);
        if(IsValid(C.Hero)&&C.Hero->BrushContact)if(auto* Foam=C.Hero->BrushContact->FoamSystem.Get())Out.AddUnique(Foam);
    }
    bool PrepareResidentPresentation(FMCPerkPreviewContext& C,FGuid,bool& Waiting,FString& Error)override
    {
        Waiting=false;TArray<UNiagaraSystem*> Systems;GetPreparedVisualSystems(C,Systems);
        const bool NeedsSurface=Systems.ContainsByPredicate([](const auto* NS){return UMCNiagaraProceduralSurfaceResources::Uses(NS);});
        auto* Loading=C.World?C.World->GetSubsystem<UMCWorldPreloadSubsystem>():nullptr;
        auto* Resources=Loading?Loading->GetProceduralSurfaceResources():nullptr;
        if(NeedsSurface&&(!Resources||!Resources->IsReadyForPlayback(C.World)))
        {Error=TEXT("Настоящий ComponentRenderer пятна не подготовлен общим барьером ресурсов.");return false;}
        return IsValid(Patch.Get());
    }
    void OnCommitted(FMCPerkPreviewContext& C)override
    {
        if(auto* Stain=Patch.Get())Stain->SetActorTickEnabled(true);
        bCommitted=true;if(IsValid(C.Hero))C.Hero->SetPrimaryInputHeld(true);
    }
    void Tick(FMCPerkPreviewContext& C,float Dt)override
    {
        if(!bCommitted||bDone)return;Age+=Dt;
        auto* Stain=Patch.Get();auto* Contact=Brush.Get();
        if(!Stain||!IsValid(C.Hero)||!Contact){bDone=true;return;}
        const float Remaining=Stain->RemainingLiquid();
        const auto& Accepted=Stain->GetAcceptedStainCleaningState();
        // Native mask removal is the acceptance proof, not a scheduled effect.
        if(!bContact&&Accepted.ContactSerial!=0&&Remaining<InitialRemaining)
        {bContact=true;if(C.RecordEvent)C.RecordEvent(TEXT("Contact"));}
        if(!bComplete&&bContact&&Accepted.CompletionSerial!=0&&Stain->IsClean())
        {bComplete=true;CompletedAt=Age;Release(C);if(C.RecordEvent)C.RecordEvent(TEXT("Complete"));}
        auto* Loading=C.World->GetSubsystem<UMCWorldPreloadSubsystem>();
        if(auto* Resources=Loading?Loading->GetProceduralSurfaceResources():nullptr)
            if(!bDraw&&Resources->HasAcceptedDraw(Stain->Liquid,SourceID))
            {bDraw=true;if(C.RecordEvent)C.RecordEvent(TEXT("Surface"));}
        if(bComplete&&Age-CompletedAt>.7f)bDone=true;
        if(Age>30.f&&!bDone) {
            if(C.RecordEvent)C.RecordEvent(TEXT("NativeStainCleaningTimedOut"));
            if(C.ReportStatus)C.ReportStatus(FString::Printf(TEXT("Настоящее пятно осталось %.3f; контакт %s, завершение %s. Подмены результата нет."),Remaining,bContact?TEXT("да"):TEXT("нет"),bComplete?TEXT("да"):TEXT("нет")));
            Release(C);bDone=true;
        }
    }
    void Release(FMCPerkPreviewContext& C)override
    {if(IsValid(C.Hero))C.Hero->SetPrimaryInputHeld(false);}
    void Cancel(FMCPerkPreviewContext& C)override
    {
        Release(C);if(auto* Owner=Presentation.Get()) {
            Owner->StopStates(SourceID);
            // Explicit sandbox reset/cancel also owns the independent Complete
            // one-shot tail; ordinary actor destruction keeps its native policy.
            if(auto* Source=Owner->GetSource(SourceID))Source->StopSource(SourceID,true);
        }
        if(auto* Stain=Patch.Get())Stain->SetActorTickEnabled(false);
        if(bStrictApplied)if(auto* Contact=Brush.Get())Contact->SetRequireEvaluatedContact(bPreviousStrict);
        bStrictApplied=false;
        if(bSelected&&IsValid(C.Hero)&&C.Hero->Inventory)C.Hero->Inventory->ServerSelect(PreviousSlot);
        bSelected=false;bDone=true;
    }
    void Reset(FMCPerkPreviewContext& C)override
    {Cancel(C);if(auto* Stain=Patch.Get())Stain->Destroy();Patch.Reset();Presentation.Reset();Brush.Reset();}
    bool IsComplete(const FMCPerkPreviewContext&)const override{return bDone;}
    FString GetPhaseName(const FMCPerkPreviewContext&)const override
    {return bDone?TEXT("Уборка завершена"):bComplete?TEXT("Штатное завершение пятна"):bContact?TEXT("Настоящее удаление маски"):TEXT("Подход щётки к пятну");}
    float GetTailSeconds()const override{return 1.f;}
private:
    TWeakObjectPtr<AMCMouthSurface> Patch;TWeakObjectPtr<UMCWorldPresentationComponent> Presentation;
    TWeakObjectPtr<UMCBrushContactComponent> Brush;FTransform Pose;
    EMCToolSlot PreviousSlot=EMCToolSlot::Brush;
    float Age=0,CompletedAt=0,InitialRemaining=0;
    bool bPreviousStrict=false,bStrictApplied=false,bSelected=false,bCommitted=false,bContact=false,bComplete=false,bDraw=false,bDone=false;
};
}

void MCStainSurfacePreviewAdapter::Register()
{
    const auto* Contract=MCVFXSourceContracts::Find(MCStainSurfacePreview::SourceID);if(!Contract)return;
    FMCPerkPreviewAdapterEntry Entry;Entry.PerkID=Contract->SourceID;Entry.Label=Contract->Label;
    Entry.Kind=EMCPerkPreviewAdapterKind::Context;Entry.Stages=Contract->Events;
    Entry.bSupportsProfileOverride=true;Entry.Scenarios={EMCPerkPreviewScenario::Target};
    Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(TEXT("Настоящая уборка пятна щёткой")));
    Entry.StageLabels.Add(TEXT("Surface"),FText::FromString(TEXT("Исходная поверхность пятна")));
    Entry.StageLabels.Add(TEXT("Contact"),FText::FromString(TEXT("Принятая очистка маски")));
    Entry.StageLabels.Add(TEXT("Complete"),FText::FromString(TEXT("Штатное завершение уборки")));
    Entry.Create=[]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<MCStainSurfacePreview::FAdapter>();};
    MCPerkPreviewAdapters::Register(MoveTemp(Entry));
}
