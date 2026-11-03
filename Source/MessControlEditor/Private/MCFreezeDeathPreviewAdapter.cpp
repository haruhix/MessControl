#include "MCFreezeDeathPreviewAdapter.h"
#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewGameMode.h"
#include "MCToothCharacter.h"
#include "MCFreezeDeathVFX.h"
#include "MCToothStatusComponent.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXSourceContracts.h"
#include "MCWorldPreloadSubsystem.h"
#include "MCIceEvent.h"
#include "MCTongue.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraRendererProperties.h"
#include "UObject/Package.h"

namespace MCFreezePreviewPrivate
{
/** Actual native cold threshold -> BeginFreezeDeath/OnRep -> native1.6s death
 * and observed Shatter. No private-state writes or hand-scheduled substitute. */
class FFreezeDeath final:public IMCPerkPreviewAdapter
{
public:
    bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
    {
        if(C.PerkID!=TEXT("Character.FreezeDeath")||!C.World||C.World->WorldType!=EWorldType::Game
            ||C.World->GetOutermost()!=GetTransientPackage()||!C.World->GetAuthGameMode<AMCPerkPreviewGameMode>()
            ||!IsValid(C.Hero)||C.Hero->GetWorld()!=C.World||!C.Hero->HasAuthority()||!C.Hero->Status
            ||!C.Hero->Status->IsAlive()||C.Hero->IsFreezingToDeath()||!C.Hero->GetController())
        {Error=TEXT("Нужен живой настоящий зубик с контроллером в частном Game-мире Composer.");return false;}
        auto* Saved=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
        auto* Private=Saved?DuplicateObject<UStaticMesh>(Saved,GetTransientPackage()):nullptr;
        if(!Private){Error=TEXT("Не создана частная поверхность настоящего языка.");return false;}
        Private->ClearFlags(RF_Public|RF_Standalone);Private->SetFlags(RF_Transient);Private->bAllowCPUAccess=true;
        const FTransform FloorPose(FQuat::Identity,C.Hero->GetActorLocation()+FVector(1000,0,-C.Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),FVector(70,70,1));
        auto* Floor=C.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FloorPose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Floor){Error=TEXT("Настоящий язык для морозного условия не создан.");return false;}
        Tongue=Floor;C.Targets.Add(Floor);Floor->SetFlags(RF_Transient);Floor->SourceMesh=Private;Floor->bAutomaticYawns=false;
        Floor->FinishSpawning(FloorPose);Floor->SetActorTickEnabled(false);Floor->Surface->SetVisibility(false);Floor->Surface->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        auto* Owner=C.World->SpawnActor<AMCIceEvent>();
        if(!Owner){Error=TEXT("Настоящий источник смертельного холода не создан.");return false;}
        Event=Owner;C.Targets.Add(Owner);Owner->SetFlags(RF_Transient);
        // Native Start clamps these ordinary settings. No meter/RNG/damage or
        // accepted timestamps are forged. Full actual aura reaches the threshold.
        Owner->ArrivalSeconds=1;Owner->FreezeSeconds=4;Owner->ColdThroneActiveSeconds=12;
        Owner->ColdThroneRestSeconds=12;Owner->IceBombDamage=0;Owner->IcePrisonDamage=0;Owner->IcicleDamage=0;
        Owner->PrepareLocalPresentation();
        if(!Owner->GetLocalPresentation()) {Error=TEXT("Реальный ледяной источник не подготовлен.");return false;}
        C.PreviewFront=FVector(-1,0,0);return true;
    }
    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error) override
    {
        if(!C.Hero->PrepareFreezeDeathSource(Error,true))return false;
        const auto* Contract=MCVFXSourceContracts::Find(TEXT("Character.FreezeDeath"));
        if(!Contract)return false;
        return !C.PresentationOverride||C.Hero->PassivePresentation->PrepareScopedOverride(Contract->SourceID,Contract->Events,C.PresentationOverride,Error);
    }
    bool PrepareResidentPresentation(FMCPerkPreviewContext& C,FGuid Epoch,bool& Waiting,FString& Error) override
    {
        Waiting=false;
        if(!bOwnerPrepared) {
            if(!C.Hero->PrepareFreezeDeathFromPreparedSource(Epoch,nullptr,nullptr,nullptr,Error))return false;
            bOwnerPrepared=true;PreparedEpoch=Epoch;
            for(TActorIterator<AActor> It(C.World);It;++It) {
                TInlineComponentArray<UPrimitiveComponent*> Parts(*It);
                for(auto* Part:Parts)if(IsValid(Part)&&Part->IsRegistered())Part->PrecachePSOs();
            }
            Waiting=true;return true; // Actual newly registered factories need a subsequent frame/barrier.
        }
        auto* FX=C.Hero->GetPreparedFreezeDeathVisual();
        if(PreparedEpoch!=Epoch||!FX||!FX->ValidatePreparation(C.Hero,Epoch,Error))return false;
        TArray<UObject*> Resources;FX->GetPreparedResources(Resources);
        TArray<UNiagaraSystem*> Systems;GetPreparedVisualSystems(C,Systems);
        for(auto* NS:Systems)if(IsValid(NS))for(const auto& Handle:NS->GetEmitterHandles())
            if(const auto* Data=Handle.GetEmitterData())for(auto* Renderer:Data->GetRenderers())if(IsValid(Renderer)) {
                TArray<UObject*> Meshes;Renderer->GetUsedMeshes(nullptr,Meshes);
                for(auto* Mesh:Meshes)if(IsValid(Mesh))Resources.AddUnique(Mesh);
            }
        auto* Loading=C.World->GetSubsystem<UMCWorldPreloadSubsystem>();
        if(!Loading){Error=TEXT("Нет общего сервиса готовности частного мира.");return false;}
        if(Resources.Num()>64||Loading->GetPhase()!=EMCWorldPreloadPhase::Idle
            ||Resources.ContainsByPredicate([](auto* R){return !IsValid(R)||(!R->IsA<UStaticMesh>()&&!R->IsA<UMaterialInterface>());}))
        {Error=TEXT("Actual Freeze resource/factory roots exceed the existing resident precommit contract.");return false;}
        if(!Loading->PrepareResidentVisualResources(Resources,Error)) {Waiting=true;return true;}
        for(TActorIterator<AActor> It(C.World);It;++It) {
            TInlineComponentArray<UPrimitiveComponent*> Parts(*It);
            for(auto* Part:Parts)if(IsValid(Part)&&Part->IsPSOPrecaching()){Waiting=true;return true;}
        }
        return true;
    }
    void GetPreparedVisualSystems(const FMCPerkPreviewContext& C,TArray<UNiagaraSystem*>& Out) const override
    {if(IsValid(C.Hero)&&C.Hero->PassivePresentation)C.Hero->PassivePresentation->GetPreparedResources(Out);}
    void OnCommitted(FMCPerkPreviewContext& C) override
    {
        auto* Owner=Event.Get();if(!Owner){Fail(C,TEXT("NativeFreezeSourceLost"));return;}
        Owner->Start();if(!Owner->IsActive()){Fail(C,TEXT("NativeFreezeIceStartRejected"));return;}
        // Place actual feet inside the real throne aura, using the real tongue
        // contact and policy query. The native UpdateFreeze owns all accumulation.
        FHitResult Floor;const FVector Point=Owner->Body->Bounds.Origin+FVector(Owner->BossFrostRadius*.72f,0,0);
        if(!Tongue->SurfacePoint(Point,Floor)||!Owner->IsInsideColdThrone(Floor.ImpactPoint))
        {Fail(C,TEXT("NativeFreezeThroneContactMissing"));return;}
        C.Hero->SetActorLocation(Floor.ImpactPoint+Floor.ImpactNormal*(C.Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2),false,nullptr,ETeleportType::TeleportPhysics);
        bCommitted=true;if(C.RequestFramingUpdate)C.RequestFramingUpdate();
        if(C.ReportStatus)C.ReportStatus(TEXT("Настоящий холод → штатный порог → OnRep FreezeDeath → смерть через1.6с → наблюдаемый Shatter. Готовность captured artwork проверяется отдельно."));
    }
    void Tick(FMCPerkPreviewContext& C,float Dt) override
    {
        if(!bCommitted||bDone)return;Age+=FMath::Max(0.f,Dt);
        if(!IsValid(C.Hero)){Fail(C,TEXT("NativeFreezeHeroLost"));return;}
        auto* Owner=Event.Get();
        if(!bHold&&C.Hero->IsFreezingToDeath()) {
            // Accepted timestamp is real replicated native state, never adapter time.
            bHold=true;if(C.RecordEvent)C.RecordEvent(TEXT("Hold"));
            if(Owner)Owner->Stop(); // Remove only our actual cold source once its fatal condition was accepted.
        }
        const auto* FX=C.Hero->GetPreparedFreezeDeathVisual();
        if(bHold&&!bShatter&&IsValid(FX)&&FX->HasAcceptedShatter()) {
            bShatter=true;if(C.RecordEvent)C.RecordEvent(TEXT("Shatter"));if(C.RequestFramingUpdate)C.RequestFramingUpdate();
        }
        if(bHold&&!bNativeDeath&&!C.Hero->Status->IsAlive()) {
            bNativeDeath=true;if(C.RecordEvent)C.RecordEvent(TEXT("NativeFatalDamageAccepted"));
        }
        if(bShatter&&bNativeDeath&&C.Hero->FreezeDeathAge()>=AMCToothCharacter::FreezeDeathHoldSeconds+2.2f)bDone=true;
        if(Age>20&&!bDone)Fail(C,TEXT("NativeFreezeConditionOrShatterUnconfirmed"));
    }
    void Release(FMCPerkPreviewContext&) override{}
    void Cancel(FMCPerkPreviewContext& C) override
    {
        if(auto* Owner=Event.Get()){Owner->Stop();Owner->Destroy();}
        if(auto* Floor=Tongue.Get())Floor->Destroy();
        if(IsValid(C.Hero)) {
            if(C.Hero->PassivePresentation)C.Hero->PassivePresentation->StopSource(TEXT("Character.FreezeDeath"),true);
            if(auto* FX=C.Hero->GetPreparedFreezeDeathVisual())FX->Destroy();
        }
        // No illicit resurrection of a committed pawn. Runner's ordinary reset
        // destroys/recreates this disposable actual hero before the next action.
        bDone=true;
    }
    void Reset(FMCPerkPreviewContext& C) override{Cancel(C);Event.Reset();Tongue.Reset();}
    bool IsComplete(const FMCPerkPreviewContext&) const override{return bDone;}
    FString GetPhaseName(const FMCPerkPreviewContext&) const override
    {return bDone?TEXT("Ледяная смерть завершена"):bShatter?TEXT("Настоящее раскалывание"):bHold?TEXT("Принятая смертельная заморозка"):TEXT("Настоящее накопление холода");}
    float GetTailSeconds() const override{return 2.f;}
private:
    void Fail(FMCPerkPreviewContext& C,FName Why){bDone=true;if(C.RecordEvent)C.RecordEvent(Why);if(C.ReportStatus)C.ReportStatus(Why.ToString());}
    TWeakObjectPtr<AMCIceEvent> Event;TWeakObjectPtr<AMCTongue> Tongue;
    FGuid PreparedEpoch;float Age=0;
    bool bOwnerPrepared=false,bCommitted=false,bHold=false,bShatter=false,bNativeDeath=false,bDone=false;
};
}

void MCFreezeDeathPreviewAdapter::Register()
{
    const auto* Contract=MCVFXSourceContracts::Find(TEXT("Character.FreezeDeath"));if(!Contract)return;
    FMCPerkPreviewAdapterEntry Entry;Entry.PerkID=Contract->SourceID;Entry.Label=Contract->Label;
    Entry.Kind=EMCPerkPreviewAdapterKind::Context;Entry.Stages=Contract->Events;Entry.bSupportsProfileOverride=true;
    Entry.StageLabels.Add(TEXT("Hold"),FText::FromString(TEXT("Принятая смертельная заморозка")));
    Entry.StageLabels.Add(TEXT("Shatter"),FText::FromString(TEXT("Настоящее раскалывание")));
    Entry.Scenarios={EMCPerkPreviewScenario::Target};
    Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(TEXT("Настоящий холод → смертельный порог")));
    // Defaults come from the shared saved source profile. No guessed future NS
    // path or generic donor is presented as migrated original artwork.
    Entry.Create=[]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<MCFreezePreviewPrivate::FFreezeDeath>();};
    MCPerkPreviewAdapters::Register(MoveTemp(Entry));
}
