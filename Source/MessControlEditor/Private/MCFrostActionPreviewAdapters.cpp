#include "MCFrostActionPreviewAdapters.h"

#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewGameMode.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXSourceContracts.h"
#include "MCWorldPresentationComponent.h"
#include "MCFrostNovaVFXComponent.h"
#include "MCHazardWave.h"
#include "MCSmallMintCandy.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraActor.h"
#include "MCTongue.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "NiagaraSystem.h"
#include "UObject/Package.h"

namespace
{
/** The real native wave (or real falling candy), not a hand-scheduled VFX loop. */
class FFrostBurstAdapter final:public IMCPerkPreviewAdapter
{
public:
    explicit FFrostBurstAdapter(bool bInMint):bMint(bInMint){}
    bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
    {
        ID=bMint?TEXT("World.MintCandy"):TEXT("World.FrostNova");
        if(C.PerkID!=ID || !C.World || C.World->WorldType!=EWorldType::Game || C.World->GetOutermost()!=GetTransientPackage()
            || !C.World->GetAuthGameMode<AMCPerkPreviewGameMode>() || !IsValid(C.Hero)
            || C.Hero->GetWorld()!=C.World || !C.Hero->HasAuthority() || C.Hero->HasFrozenLegs())
        {Error=TEXT("Нужны изолированный Game-мир и исходный зубик без чужого обледенения.");return false;}
        auto* SavedPlane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
        auto* PrivatePlane=SavedPlane?DuplicateObject<UStaticMesh>(SavedPlane,GetTransientPackage()):nullptr;
        if(!PrivatePlane){Error=TEXT("Не удалось создать частную поверхность реального языка.");return false;}
        PrivatePlane->ClearFlags(RF_Public|RF_Standalone);PrivatePlane->SetFlags(RF_Transient);PrivatePlane->bAllowCPUAccess=true;
        const FTransform FloorPose(FRotator::ZeroRotator,FVector(0,0,-.1f),FVector(70,70,1));
        auto* Floor=C.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FloorPose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Floor){Error=TEXT("Не удалось создать настоящий язык песочницы.");return false;}
        Tongue=Floor;C.Targets.Add(Floor);Floor->SetFlags(RF_Transient);Floor->SourceMesh=PrivatePlane;Floor->bAutomaticYawns=false;
        Floor->FinishSpawning(FloorPose);Floor->SetActorTickEnabled(false);Floor->Surface->SetVisibility(false);
        Floor->Surface->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        bShatter=bMint && C.Scenario==EMCPerkPreviewScenario::NearWall;
        if(bShatter && (!C.Controller || !C.Hero->Inventory)) {Error=TEXT("Для родного раскалывания нужны контроллер и инвентарь.");return false;}
        if(bShatter)C.Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
        FVector Point=C.Hero->GetActorLocation()+FVector(bShatter?150:500,0,0);Point.Z=0;
        SpawnPose=FTransform(FQuat::Identity,Point);
        AActor* Owner=nullptr;
        if(bMint)
        {
            auto* Candy=C.World->SpawnActorDeferred<AMCSmallMintCandy>(AMCSmallMintCandy::StaticClass(),SpawnPose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(Candy){Mint=Candy;Owner=Candy;Presenter=Candy->FrostNovaVFX;}
        }
        else
        {
            auto* Pulse=C.World->SpawnActorDeferred<AMCHazardWave>(AMCHazardWave::StaticClass(),SpawnPose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(Pulse){Wave=Pulse;Owner=Pulse;Pulse->bFreezeLegs=true;Pulse->Damage=0;Pulse->Source=C.Hero;Presenter=Pulse->GetFrostNovaVFX();}
        }
        if(!Owner || !Presenter.IsValid()){Error=TEXT("Не создан настоящий владелец морозного импульса.");return false;}
        NativeOwner=Owner;Owner->SetFlags(RF_Transient);C.Targets.Add(Owner);
        if(auto* Candy=Mint.Get();Candy&&!Candy->PrepareShatterPresentation())
        {Error=TEXT("Не подготовлен исходный Niagara раскалывания конфеты.");return false;}
        // Real components and their required material variants are retained
        // before the shared barrier. Native timers begin only OnCommitted.
        if(auto* Root=Owner->GetRootComponent())if(!Root->IsRegistered())Root->RegisterComponent();
        if(!Presenter->IsRegistered())Presenter->RegisterComponent();
        Presenter->PresentationSourceID=ID;
        if(!Presenter->PrepareForWave()){Error=TEXT("Родные материалы/меши морозной волны не подготовлены.");return false;}
        Presentation=Presenter->GetPreparedPresentation();
        if(!Presentation.IsValid()){Error=TEXT("Нет подготовленного канала реального морозного владельца.");return false;}
        BeforeWave=Presenter->GetAcceptedWaveSerial();BeforeBurst=Presenter->GetAcceptedBurstSerial();
        if(const auto* Candy=Mint.Get())BeforeShatter=Candy->GetAcceptedShatterSerial();
        C.PreviewFront=(C.Hero->GetActorLocation()-Point).GetSafeNormal2D();
        if(C.RequestFramingUpdate)C.RequestFramingUpdate();
        if(C.ReportStatus)C.ReportStatus(bMint?
            (bShatter?TEXT("Настоящая конфета: падение → обычные удары киркой → штатное разрушение и Shatter."):TEXT("Настоящая конфета: падение → штатное ожидание → волна с родным контуром, редактируемые кристаллы/вуаль и исходный всплеск.")):
            TEXT("Настоящая замораживающая волна: предупреждение → штатный импульс → родной контур, редактируемые кристаллы/вуаль и исходный всплеск."));
        return true;
    }
    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error) override
    {
        auto* Owner=Presentation.Get();const auto* Contract=MCVFXSourceContracts::Find(ID);
        if(!Owner || !Contract || !Owner->PrepareSources(Error,true))return false;
        auto* Source=Owner->GetSource(ID);
        if(!Source){Error=TEXT("Канал морозного импульса не подготовлен.");return false;}
        if(auto* Actor=NativeOwner.Get())if(auto* Root=Actor->GetRootComponent())
            if(!Source->BindSourceAudioSocket(ID,Root,Error))return false;
        return !C.PresentationOverride || Source->PrepareScopedOverride(ID,Contract->Events,C.PresentationOverride,Error);
    }
    void GetPreparedVisualSystems(const FMCPerkPreviewContext&,TArray<UNiagaraSystem*>& Out) const override
    {
        if(const auto* P=Presentation.Get())if(const auto* Source=P->GetSource(ID))
        {TArray<UNiagaraSystem*> Prepared;Source->GetPreparedResources(Prepared);for(auto* NS:Prepared)Out.AddUnique(NS);}
        if(const auto* VFX=Presenter.Get())if(auto* NS=VFX->BurstSystem.Get())Out.AddUnique(NS);
        if(const auto* Candy=Mint.Get())if(auto* NS=Candy->ShatterSystem.Get())Out.AddUnique(NS);
    }
    void OnCommitted(FMCPerkPreviewContext& C) override
    {
        bCommitted=true;
        if(auto* Candy=Mint.Get())
        {
            Candy->FinishSpawning(SpawnPose);
            bStarted=Candy->Start(Tongue.Get(),FMCSmallMintCandySettings(),371);
        }
        else if(auto* Pulse=Wave.Get()){Pulse->FinishSpawning(SpawnPose);bStarted=true;}
        if(!bStarted){bDone=true;if(C.RecordEvent)C.RecordEvent(TEXT("NativeFrostStartRejected"));}
    }
    void Tick(FMCPerkPreviewContext& C,float Dt) override
    {
        if(!bCommitted || bDone)return;
        Age+=FMath::Max(0.f,Dt);
        if(bShatter) {
            auto* Candy=Mint.Get();
            if(Candy && Candy->Stage==EMCSmallMintStage::Active && !bWorkerPlaced) {
                const FVector Target=Candy->Body->Bounds.Origin;
                const FVector Worker(Target.X-150,Target.Y,C.Hero->GetActorLocation().Z);
                C.Hero->SetActorLocation(Worker,false,nullptr,ETeleportType::TeleportPhysics);
                const FRotator Aim=(Target-Worker).Rotation();C.Hero->SetActorRotation(FRotator(0,Aim.Yaw,0));
                C.Controller->SetControlRotation(Aim);
                if(C.AimCamera)C.AimCamera->SetActorLocationAndRotation(Worker+FVector(0,0,75),(Target-Worker-FVector(0,0,75)).Rotation());
                bWorkerPlaced=true;if(C.RequestFramingUpdate)C.RequestFramingUpdate();
            }
            if(Candy && Candy->Stage==EMCSmallMintStage::Active && bWorkerPlaced && Age-LastSwing>=1.f && Swings<16) {
                C.Hero->SwingBrush();LastSwing=Age;++Swings;if(C.RecordEvent)C.RecordEvent(TEXT("NativePickaxeInput"));
            }
            if(Candy && !bShattered && Candy->GetAcceptedShatterSerial()>BeforeShatter) {
                bShattered=true;if(C.RecordEvent)C.RecordEvent(TEXT("Shatter"));
                // Both the returned unpooled native component and prepared
                // custom VFX/SFX finish naturally; no guessed lifetime.
            }
            if(bShattered) {
                if(Candy && !Candy->IsShatterPresentationRunning())bDone=true;
            }
            if(Age>24.f && !bDone) {bDone=true;if(C.RecordEvent)C.RecordEvent(TEXT("NativeMintShatterMissing"));}
            return;
        }
        if(auto* VFX=Presenter.Get())
        {
            if(!bWave && VFX->GetAcceptedWaveSerial()>BeforeWave)
            {bWave=true;if(C.RecordEvent)C.RecordEvent(TEXT("Wave"));}
            if(!bBurst && VFX->GetAcceptedBurstSerial()>BeforeBurst)
            {bBurst=true;BurstAt=Age;if(C.RecordEvent)C.RecordEvent(TEXT("Burst"));if(C.RequestFramingUpdate)C.RequestFramingUpdate();}
        }
        if(bBurst && Age-BurstAt>1.1f){bDone=true;return;}
        if(Age>(bMint?13.f:4.f))
        {
            bDone=true;if(C.RecordEvent)C.RecordEvent(TEXT("NativeFrostBurstMissing"));
            if(C.ReportStatus)C.ReportStatus(TEXT("Родной всплеск не подтверждён. Никакой эффект вручную не подставлен."));
        }
    }
    void Release(FMCPerkPreviewContext&) override{}
    void Cancel(FMCPerkPreviewContext& C) override
    {
        if(auto* Candy=Mint.Get()){Candy->Stop();Candy->StopShatterPresentation();}
        if(auto* VFX=Presenter.Get())VFX->Stop();
        // Prepare rejects a pre-existing foreign leg freeze. Only the result
        // of this disposable native wave can therefore be cleared here.
        if(bStarted && IsValid(C.Hero) && C.Hero->HasFrozenLegs())C.Hero->ClearFrozenLegs();
        // Runner::Cancel intentionally does not recreate/reset its sandbox.
        // Cancellation itself must therefore release this adapter's fixtures,
        // including any still-running child waves retired by native Mint.Stop.
        if(auto* Owner=NativeOwner.Get();Owner && !Owner->IsActorBeingDestroyed())Owner->Destroy();
        if(auto* Floor=Tongue.Get();Floor && !Floor->IsActorBeingDestroyed())Floor->Destroy();
        bDone=true;
    }
    void Reset(FMCPerkPreviewContext& C) override
    {
        Cancel(C);
        NativeOwner.Reset();Mint.Reset();Wave.Reset();Tongue.Reset();Presenter.Reset();Presentation.Reset();
    }
    bool IsComplete(const FMCPerkPreviewContext&) const override{return bDone;}
    FString GetPhaseName(const FMCPerkPreviewContext&) const override
    {return bDone?TEXT("Морозный импульс завершён"):bBurst?TEXT("Родной Niagara-всплеск"):bWave?TEXT("Настоящая волна"):TEXT("Настоящее ожидание/падение");}
    float GetTailSeconds() const override{return 1.1f;}
private:
    FName ID;bool bMint=false,bCommitted=false,bStarted=false,bWave=false,bBurst=false,bDone=false;
    bool bShatter=false,bShattered=false,bWorkerPlaced=false;int32 Swings=0;float LastSwing=-100.f;uint64 BeforeShatter=0;
    uint64 BeforeWave=0,BeforeBurst=0;float Age=0,BurstAt=0;FTransform SpawnPose;
    TWeakObjectPtr<AMCTongue> Tongue;TWeakObjectPtr<AMCSmallMintCandy> Mint;TWeakObjectPtr<AMCHazardWave> Wave;
    TWeakObjectPtr<AActor> NativeOwner;TWeakObjectPtr<UMCFrostNovaVFXComponent> Presenter;
    TWeakObjectPtr<UMCWorldPresentationComponent> Presentation;
};
}

void MCFrostActionPreviewAdapters::Register()
{
    for(const FName ID:{FName(TEXT("World.FrostNova")),FName(TEXT("World.MintCandy"))})
    {
        const auto* Contract=MCVFXSourceContracts::Find(ID);if(!Contract)continue;
        FMCPerkPreviewAdapterEntry Entry;Entry.PerkID=ID;Entry.SourceRowID=NAME_None;Entry.Label=Contract->Label;
        Entry.Kind=EMCPerkPreviewAdapterKind::Context;Entry.Stages=Contract->Events;Entry.bSupportsProfileOverride=true;
        Entry.StageLabels.Add(TEXT("Wave"),FText::FromString(TEXT("Декор волны · контур опасности сохранён")));
        Entry.DefaultSystems.Add(TEXT("Wave"),TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Gameplay/Cold/VFX/ComposerV1/Wave/NS_FrostNovaWave.NS_FrostNovaWave"))));
        Entry.StageLabels.Add(TEXT("Burst"),FText::FromString(TEXT("Исходный Niagara-всплеск")));
        Entry.DefaultSystems.Add(TEXT("Burst"),GetDefault<UMCFrostNovaVFXComponent>()->BurstSystem);
        Entry.Scenarios={EMCPerkPreviewScenario::Target};
        const bool bMint=ID==TEXT("World.MintCandy");
        Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(bMint?TEXT("Настоящая конфета → первая волна"):TEXT("Настоящая волна заморозки")));
        if(bMint) {
            Entry.StageLabels.Add(TEXT("Shatter"),FText::FromString(TEXT("Раскалывание киркой")));
            Entry.DefaultSystems.Add(TEXT("Shatter"),GetDefault<AMCSmallMintCandy>()->ShatterSystem);
            Entry.Scenarios.Add(EMCPerkPreviewScenario::NearWall);
            Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::NearWall,FText::FromString(TEXT("Настоящая кирка → расколоть конфету")));
        }
        Entry.Create=[bMint]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FFrostBurstAdapter>(bMint);};
        MCPerkPreviewAdapters::Register(MoveTemp(Entry));
    }
}
