#include "MCIceActionPreviewAdapters.h"
#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewGameMode.h"
#include "MCIceEvent.h"
#include "MCIceEventVFX.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "MCTongue.h"
#include "MCVFXSourceContracts.h"
#include "MCWorldPresentationComponent.h"
#include "MCVFXPresentationComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "NiagaraSystem.h"
#include "UObject/Package.h"

namespace MCIceActionPreviewPrivate
{
/** One disposable real encounter. Timers, attack selection, phase transitions,
 * accepted contacts and completion remain native. The adapter only supplies
 * ordinary pickaxe input and bounded editable fixture settings. */
class FIceEncounter final:public IMCPerkPreviewAdapter
{
public:
    bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
    {
        if((C.PerkID!=TEXT("World.Ice")&&C.PerkID!=TEXT("World.Ice.Surface"))||!C.World||C.World->WorldType!=EWorldType::Game
            ||C.World->GetOutermost()!=GetTransientPackage()||!C.World->GetAuthGameMode<AMCPerkPreviewGameMode>()
            ||!IsValid(C.Hero)||C.Hero->GetWorld()!=C.World||!C.Hero->HasAuthority()||!C.Hero->Inventory||C.Hero->HasFrozenLegs())
        {Error=TEXT("Нужен частный Game-мир Composer и настоящий зубик без чужого обледенения.");return false;}
        C.Hero->SetPrimaryInputHeld(false);C.Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
        if(C.Hero->Inventory->Selected!=EMCToolSlot::Pickaxe)
        {Error=TEXT("Штатный выбор кирки отклонён.");return false;}
        auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
        auto* Private=Plane?DuplicateObject<UStaticMesh>(Plane,GetTransientPackage()):nullptr;
        if(!Private){Error=TEXT("Не удалось подготовить частную поверхность языка.");return false;}
        Private->ClearFlags(RF_Public|RF_Standalone);Private->SetFlags(RF_Transient);Private->bAllowCPUAccess=true;
        const FTransform FloorPose(FQuat::Identity,C.Hero->GetActorLocation()+FVector(1000,0,-C.Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),FVector(70,70,1));
        auto* Floor=C.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FloorPose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Floor){Error=TEXT("Настоящий язык песочницы не создан.");return false;}
        Tongue=Floor;C.Targets.Add(Floor);Floor->SetFlags(RF_Transient);Floor->SourceMesh=Private;Floor->bAutomaticYawns=false;
        Floor->FinishSpawning(FloorPose);Floor->SetActorTickEnabled(false);Floor->Surface->SetVisibility(false);Floor->Surface->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        auto* Owner=C.World->SpawnActor<AMCIceEvent>();
        if(!Owner){Error=TEXT("Настоящий ледяной босс не создан.");return false;}
        Event=Owner;C.Targets.Add(Owner);Owner->SetFlags(RF_Transient);
        Owner->CandyHealthPerPlayer=FMath::Clamp(C.Hero->Inventory->Damage()*8,32.f,500.f);
        Owner->ArrivalSeconds=1;Owner->ColdThroneActiveSeconds=4;Owner->ColdThroneRestSeconds=4;
        Owner->FreezeSeconds=60;Owner->IceBombDamage=0;Owner->IcePrisonDamage=0;Owner->IcicleDamage=0;
        Owner->PrepareLocalPresentation();
        Presenter=Owner->GetLocalPresentation();
        if(!Presenter.IsValid()||!Presenter->HasRequiredAssets())
        {Error=TEXT("Настоящие ресурсы ледяного эффекта не подготовлены.");return false;}
        Presentation=Presenter->GetComposerPresentation();
        if(!Presentation.IsValid()){Error=TEXT("У ледяного владельца нет общего канала Composer.");return false;}
        Presenter->GetObservedComposerEvents(Observed);
        C.PreviewFront=FVector(-1,0,0);return true;
    }
    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error) override
    {
        // Hero BeginPlay is deliberately no-load and can precede ToolLibrary.
        // Prepare the real contact tool before the same shared visual/audio barrier.
        auto* Tools=IsValid(C.Hero)?MCWorldPresentation::PrepareOwner(C.Hero,{TEXT("Tool.Pickaxe")}):nullptr;
        if(!Tools||!Tools->PrepareSources(Error,true)||!Tools->GetSource(TEXT("Tool.Pickaxe"))) {
            if(Error.IsEmpty())Error=TEXT("Настоящий источник кирки не подготовлен до ледяного сценария.");return false;
        }
        auto* Owner=Presentation.Get();const auto* Contract=MCVFXSourceContracts::Find(C.PerkID);
        if(!Owner||!Contract||!Owner->PrepareSources(Error,true))return false;
        auto* Channel=Owner->GetSource(C.PerkID);
        if(!Channel){Error=TEXT("Не подготовлен источник ледяного события.");return false;}
        if(auto* Root=Presenter->GetRootComponent())if(!Channel->BindSourceAudioSocket(C.PerkID,Root,Error))return false;
        return !C.PresentationOverride||Channel->PrepareScopedOverride(C.PerkID,Contract->Events,C.PresentationOverride,Error);
    }
    void GetPreparedVisualSystems(const FMCPerkPreviewContext& C,TArray<UNiagaraSystem*>& Out) const override
    {
        if(const auto* P=Presentation.Get())for(FName ID:{FName(TEXT("World.Ice")),FName(TEXT("World.Ice.Surface"))})
            if(const auto* Channel=P->GetSource(ID)) {
                TArray<UNiagaraSystem*> Actual;Channel->GetPreparedResources(Actual);
                for(auto* System:Actual)Out.AddUnique(System);
            }
        if(IsValid(C.Hero))if(const auto* Tools=C.Hero->FindComponentByClass<UMCWorldPresentationComponent>())
            if(const auto* Pickaxe=Tools->GetSource(TEXT("Tool.Pickaxe"))) {
                TArray<UNiagaraSystem*> Actual;Pickaxe->GetPreparedResources(Actual);
                for(auto* System:Actual)Out.AddUnique(System);
            }
    }
    void OnCommitted(FMCPerkPreviewContext& C) override
    {
        bCommitted=true;auto* Owner=Event.Get();if(!Owner)return;
        Owner->Start();
        // Start consumes the preparation token. Recreating a presenter here
        // would invalidate the shared resource barrier; fail instead.
        if(!Owner->IsActive()||Owner->GetLocalPresentation()!=Presenter.Get())
        {Fail(C,TEXT("NativeIceStartOrPreparedOwnerRejected"));return;}
        FHitResult Floor;
        const FVector Center=Owner->Body->Bounds.Origin;
        FVector Approach=Owner->CandyContactPoint(Center-FVector(2000,0,0))-FVector(75,0,0);
        if(!Tongue->SurfacePoint(Approach,Floor)){Fail(C,TEXT("NativeIceContactFloorMissing"));return;}
        Approach=Floor.ImpactPoint+Floor.ImpactNormal*(C.Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2);
        C.Hero->SetActorLocationAndRotation(Approach,FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
        if(C.Controller)C.Controller->SetControlRotation(FRotator::ZeroRotator);
        InitialHealth=Owner->CandyHealth;
        if(C.RequestFramingUpdate)C.RequestFramingUpdate();
        if(C.ReportStatus)C.ReportStatus(TEXT("Реальный ледяной босс: обычные взмахи кирки → принятая вторая фаза → возврат климата → настоящее завершение. Все события отмечаются только после наблюдения."));
    }
    void Tick(FMCPerkPreviewContext& C,float Dt) override
    {
        if(!bCommitted||bDone)return;Age+=FMath::Max(0.f,Dt);
        auto* Owner=Event.Get();if(!Owner||!IsValid(C.Hero)){Fail(C,TEXT("NativeIceOwnerLost"));return;}
        if(C.PerkID==TEXT("World.Ice.Surface")) {
            const uint8 Actual=Owner->GetAcceptedFrostSurfacePhases();
            const FName Names[]={TEXT("Frost"),TEXT("WarmZone"),TEXT("NextZone"),TEXT("Nova")};
            for(uint8 I=0;I<4;++I)if((Actual&(1<<I))&&!(ObservedSurfacePhases&(1<<I)))
                if(C.RecordEvent)C.RecordEvent(Names[I]);
            ObservedSurfacePhases|=Actual;
        }
        else if(auto* FX=Presenter.Get())
        {
            TMap<FName,uint64> Current;FX->GetObservedComposerEvents(Current);
            for(const auto& Pair:Current)if(Pair.Value>Observed.FindRef(Pair.Key))
            {if(C.RecordEvent)C.RecordEvent(Pair.Key);Observed.Add(Pair.Key,Pair.Value);}
        }
        if(Owner->bSecondPhase&&!bSecondPhase){bSecondPhase=true;C.Hero->SetPrimaryInputHeld(false);bPressed=false;}
        bSawCold|=Owner->IsGlobalColdActive();
        if(bSawCold&&!Owner->IsGlobalColdActive()&&Owner->PhaseReturnAge()>3.3f)bReturned=true;
        const bool WantSwing=Owner->Stage==EMCIceEventStage::Active&&(!bSecondPhase||bReturned);
        if(!WantSwing&&bPressed){C.Hero->SetPrimaryInputHeld(false);bPressed=false;}
        if(WantSwing)
        {
            // Native bomb/crystal impulses can move the hero outside contact
            // and onto the other side of the candy. Reacquire with ordinary
            // movement/look input; never move the boss, teleport, or apply hits.
            const FVector ToContact=Owner->CandyContactPoint(C.Hero->GetActorLocation())-C.Hero->GetActorLocation();
            const FVector Direction=ToContact.GetSafeNormal2D();
            if(!Direction.IsNearlyZero()) {
                if(C.Controller)C.Controller->SetControlRotation(FRotator(0,Direction.Rotation().Yaw,0));
                const float StandOff=FMath::Min(75.f,C.Hero->Inventory->MeleeReach()*.5f);
                const bool NeedsApproach=ToContact.SizeSquared2D()>FMath::Square(StandOff);
                const bool NeedsTurn=FVector::DotProduct(C.Hero->GetActorForwardVector().GetSafeNormal2D(),Direction)<.8f;
                if(C.Hero->CanWork()&&(NeedsApproach||NeedsTurn))
                    C.Hero->AddMovementInput(Direction,NeedsApproach?1.f:.2f);
            }
            if(bPressed&&Age-PressedAt>.08f){C.Hero->SetPrimaryInputHeld(false);bPressed=false;}
            if(!bPressed&&Age>=NextPress){C.Hero->SetPrimaryInputHeld(true);bPressed=true;PressedAt=Age;NextPress=Age+C.Hero->Inventory->SwingDuration()+.25f;}
        }
        if(Owner->IsComplete())
        {
            C.Hero->SetPrimaryInputHeld(false);bPressed=false;
            if(FinishAt<0)FinishAt=Age;
            if(Age-FinishAt>1.7f)bDone=true;
        }
        else if(Owner->bFailed||Age>45)Fail(C,Owner->CandyHealth>=InitialHealth?TEXT("NativeIceToolContactNotAccepted"):TEXT("NativeIceEncounterIncomplete"));
    }
    void Release(FMCPerkPreviewContext& C) override{if(IsValid(C.Hero))C.Hero->SetPrimaryInputHeld(false);bPressed=false;}
    void Cancel(FMCPerkPreviewContext& C) override
    {
        Release(C);if(auto* Owner=Event.Get()){Owner->Stop();Owner->Destroy();}
        // No foreign actors/world/profile are touched. Stop owns its actual
        // child attacks, slipperiness and climate contribution.
        if(auto* FX=Presenter.Get())if(!FX->IsActorBeingDestroyed())FX->Destroy();
        if(auto* Floor=Tongue.Get())Floor->Destroy();
        if(bCommitted&&IsValid(C.Hero)&&C.Hero->HasFrozenLegs())C.Hero->ClearFrozenLegs();
        bDone=true;
    }
    void Reset(FMCPerkPreviewContext& C) override
    {Cancel(C);Event.Reset();Presenter.Reset();Presentation.Reset();Tongue.Reset();Observed.Reset();}
    bool IsComplete(const FMCPerkPreviewContext&) const override{return bDone;}
    FString GetPhaseName(const FMCPerkPreviewContext&) const override
    {return bDone?TEXT("Ледяной сценарий завершён"):bReturned?TEXT("Штатные удары и настоящее завершение"):bSecondPhase?TEXT("Реальная вторая фаза и возврат климата"):TEXT("Настоящее прибытие и контакт кирки");}
    float GetTailSeconds() const override{return 1.7f;}
private:
    void Fail(FMCPerkPreviewContext& C,FName Reason)
    {Release(C);bDone=true;if(C.RecordEvent)C.RecordEvent(Reason);if(C.ReportStatus)C.ReportStatus(TEXT("Нативный ледяной сценарий не завершён: ")+Reason.ToString());}
    TWeakObjectPtr<AMCIceEvent> Event;TWeakObjectPtr<AMCIceEventVFX> Presenter;TWeakObjectPtr<AMCTongue> Tongue;
    TWeakObjectPtr<UMCWorldPresentationComponent> Presentation;TMap<FName,uint64> Observed;
    bool bCommitted=false,bDone=false,bPressed=false,bSecondPhase=false,bSawCold=false,bReturned=false;uint8 ObservedSurfacePhases=0;
    float Age=0,NextPress=0,PressedAt=0,InitialHealth=0,FinishAt=-1;
};
}

void MCIceActionPreviewAdapters::Register()
{
    for(FName ID:{FName(TEXT("World.Ice")),FName(TEXT("World.Ice.Surface"))}) {
    const auto* Contract=MCVFXSourceContracts::Find(ID);if(!Contract)continue;
    FMCPerkPreviewAdapterEntry Entry;Entry.PerkID=Contract->SourceID;Entry.SourceRowID=NAME_None;Entry.Label=Contract->Label;
    Entry.Kind=EMCPerkPreviewAdapterKind::Context;Entry.Stages=Contract->Events;Entry.bSupportsProfileOverride=true;
    Entry.Scenarios={EMCPerkPreviewScenario::Target};
    Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(TEXT("Настоящий босс: кирка → вторая фаза → завершение")));
    Entry.Create=[]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<MCIceActionPreviewPrivate::FIceEncounter>();};
    MCPerkPreviewAdapters::Register(MoveTemp(Entry));
    }
}
