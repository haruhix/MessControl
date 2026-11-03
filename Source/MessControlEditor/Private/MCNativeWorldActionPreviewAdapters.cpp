#include "MCNativeWorldActionPreviewAdapters.h"
#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewGameMode.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXSourceContracts.h"
#include "MCWorldPresentationComponent.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "MCToothStatusComponent.h"
#include "MCFirePatch.h"
#include "MCReactionVFX.h"
#include "MCVomitBurst.h"
#include "MCThroat.h"
#include "MCTongue.h"
#include "MCDayPlan.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "UObject/Package.h"

namespace
{
/** One real native owner/input path. All fixtures live only in Runner's Game world. */
class FNativeWorldAdapter final : public IMCPerkPreviewAdapter
{
public:
    explicit FNativeWorldAdapter(bool InVomit):bVomit(InVomit){}
    ~FNativeWorldAdapter() override { Unlisten(); }
    bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
    {
        ID=bVomit?TEXT("World.Vomit"):TEXT("World.FirePatch");
        if(C.PerkID!=ID||!C.World||C.World->WorldType!=EWorldType::Game||C.World->GetOutermost()!=GetTransientPackage()
            ||!C.World->GetAuthGameMode<AMCPerkPreviewGameMode>()||!IsValid(C.Hero)||C.Hero->GetWorld()!=C.World
            ||!C.Hero->HasAuthority()||!C.Hero->Inventory||!C.Hero->Status||!C.Hero->CanWork())
        {Error=TEXT("Нужны изолированный authority Game-мир, настоящий зубик и его инвентарь.");return false;}
        World=C.World;Context=&C;C.Hero->SetPrimaryInputHeld(false);
        // The original native tongue collision/random-interior algorithm is
        // retained. Only the disposable surface is a private CPU-readable plane.
        auto* Saved=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
        auto* Private=Saved?DuplicateObject<UStaticMesh>(Saved,GetTransientPackage()):nullptr;
        if(!Private){Error=TEXT("Не подготовлена частная поверхность настоящего языка.");return false;}
        Private->ClearFlags(RF_Public|RF_Standalone);Private->SetFlags(RF_Transient);Private->bAllowCPUAccess=true;
        const FTransform FloorPose(FRotator::ZeroRotator,FVector(0,0,20),FVector(70,70,1));
        auto* Floor=C.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FloorPose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Floor){Error=TEXT("Не создан настоящий язык сценария.");return false;}
        Tongue=Floor;C.Targets.Add(Floor);Floor->SourceMesh=Private;Floor->bAutomaticYawns=false;Floor->SetFlags(RF_Transient);
        Floor->FinishSpawning(FloorPose);Floor->SetActorTickEnabled(false);Floor->Surface->SetVisibility(false);Floor->Surface->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        FString WrongIngredientFixture;
        if(bVomit) {
            const FTransform Pose(FRotator::ZeroRotator,FVector(700,0,200));
            auto* T=C.World->SpawnActor<AMCThroat>(AMCThroat::StaticClass(),Pose);
            if(!T){Error=TEXT("Не создан настоящий ручной приём глотки.");return false;}
            Throat=T;C.Targets.Add(T);T->SetFlags(RF_Transient);
            if(!T->bManualIntake){Error=TEXT("Сценарий требует штатный ручной приём, без legacy-подмены.");return false;}
            const auto* Table=GetDefault<UMCDayPlan>()->Menu.LoadSynchronous();
            if(!Table||Table->GetRowStruct()!=FMCFoodRow::StaticStruct()){Error=TEXT("Нет сохранённого меню настоящей еды.");return false;}
            TArray<FName> Names=Table->GetRowNames();Names.Sort([](FName A,FName B){return A.LexicalLess(B);});
            const FMCFoodRow* Chosen=nullptr;FName RowID;bool bSpoiledFixture=false;
            for(FName Name:Names)if(const auto* Row=Table->FindRow<FMCFoodRow>(Name,TEXT("ComposerNativeVomit"),false))
                if(Row->Kind==EMCFoodKind::ForeignObject&&!Row->WholeMeshes.IsEmpty()){Chosen=Row;RowID=Name;break;}
            // A spoiled saved food is also a genuine native wrong ingredient.
            // The same initial condition is used by MCThroatSmoke/Dev VomitMeal;
            // no gameplay deadline, food definition or native acceptance is changed.
            if(!Chosen)for(FName Name:Names)if(const auto* Row=Table->FindRow<FMCFoodRow>(Name,TEXT("ComposerNativeVomitSpoiled"),false))
                if(Row->Kind==EMCFoodKind::Food&&!Row->WholeMeshes.IsEmpty()&&FMath::IsFinite(Row->Health)&&Row->Health>0)
                {Chosen=Row;RowID=Name;bSpoiledFixture=true;break;}
            if(!Chosen){Error=TEXT("Нет сохранённого постороннего предмета или настоящей еды для исходного испорченного предмета.");return false;}
            FVector Aim;float Radius;
            if(!T->GetIntakeAimTarget(Aim,Radius)){Error=TEXT("Настоящее отверстие глотки не готово.");return false;}
            // Default native throat has no authored-mouth override: use its
            // public actual aperture result and inward actor axis, not a guessed mesh hole.
            if(T->AuthoredMouth&&T->AuthoredMouth->GetSkeletalMeshAsset())
            {Error=TEXT("Сценарий пока не выводит ось из произвольного authored-mouth; исходная геометрия не заменяется.");return false;}
            LaunchDirection=T->GetActorForwardVector();LaunchPoint=Aim+LaunchDirection*30;
            const FTransform FoodPose(LaunchPoint);
            auto* F=C.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),FoodPose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(!F){Error=TEXT("Не создан настоящий пищевой предмет.");return false;}
            Food=F;C.Targets.Add(F);F->SetFlags(RF_Transient);FRandomStream Choice(743);F->Initialize(false,LaunchDirection);F->ConfigureItem(RowID,*Chosen,Choice);
            if(bSpoiledFixture)F->bSpoiled=true; // Disposable initial fixture, before BeginPlay.
            F->FinishSpawning(FoodPose);
            UStaticMesh* SavedMesh=F->Visual->GetStaticMesh();
            if(!SavedMesh||!Chosen->WholeMeshes.ContainsByPredicate([SavedMesh](const auto& Ref){return Ref.Get()==SavedMesh;}))
            {Error=TEXT("Реальная геометрия сохранённой строки не загрузилась; prototype fallback не принимается.");return false;}
            WrongIngredientFixture=FString::Printf(TEXT("Строка %s · исходное состояние: %s; определения, часы и RNG не изменены."),*RowID.ToString(),bSpoiledFixture?TEXT("испорченная еда, как в native fixture"):TEXT("посторонний предмет"));
            if(!F->IsWrongIngredient()||!F->Body->IsSimulatingPhysics())
            {Error=TEXT("Сохранённый предмет не подтвердил неправильный ингредиент/физику.");return false;}
            F->SetActorTickEnabled(false);F->Body->SetSimulatePhysics(false);
            T->SetActorTickEnabled(false);BeforeVomit=T->VomitCount;
            Presentation=MCWorldPresentation::PrepareOwner(T,{ID});
        } else {
            C.Hero->Inventory->ServerSelect(EMCToolSlot::Spray);
            if(C.Hero->Inventory->Selected!=EMCToolSlot::Spray){Error=TEXT("Штатный выбор спрея отклонён.");return false;}
            FVector Point=C.Hero->GetActorLocation()+C.Hero->GetActorForwardVector()*145;Point.Z=25;
            FirePose=FTransform(Point);auto* F=C.World->SpawnActorDeferred<AMCFirePatch>(AMCFirePatch::StaticClass(),FirePose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(!F){Error=TEXT("Не создан настоящий огненный участок.");return false;}
            Fire=F;C.Targets.Add(F);F->SetFlags(RF_Transient);F->BurnRadius=90;F->BurnSeconds=0;F->bCreateLesion=false;
            Presentation=MCWorldPresentation::PrepareOwner(F,{ID});
        }
        if(!Presentation.IsValid()){Error=TEXT("Нет общего канала настоящего владельца.");return false;}
        if(C.RequestFramingUpdate)C.RequestFramingUpdate();
        if(C.ReportStatus) {
            if(bVomit)C.ReportStatus(WrongIngredientFixture+TEXT(" Настоящий физический бросок → ручной приём → спазм → штатный выброс и реальные столкновения. Принудительного BeginVomit нет."));
            else C.ReportStatus(TEXT("Настоящий AMCFirePatch → штатный спрей и удержание ввода → принятый Extinguish → родной Steam. Contact этим сценарием не заявляется."));
        }
        return true;
    }
    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error) override
    {
        auto* P=Presentation.Get();const auto* Contract=MCVFXSourceContracts::Find(ID);
        if(!P||!Contract||!P->PrepareSources(Error,true))return false;
        auto* Source=P->GetSource(ID);if(!Source){Error=TEXT("Не подготовлен общий профиль источника.");return false;}
        return !C.PresentationOverride||Source->PrepareScopedOverride(ID,Contract->Events,C.PresentationOverride,Error);
    }
    void GetPreparedVisualSystems(const FMCPerkPreviewContext&,TArray<UNiagaraSystem*>& Out) const override
    {if(const auto* P=Presentation.Get())if(const auto* Source=P->GetSource(ID))Source->GetPreparedResources(Out);}
    void OnCommitted(FMCPerkPreviewContext& C) override
    {
        bCommitted=true;
        SpawnHandle=C.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateRaw(this,&FNativeWorldAdapter::OnSpawned));
        if(bVomit) {
            auto* T=Throat.Get();auto* F=Food.Get();if(!T||!F){bDone=true;return;}
            T->SetActorTickEnabled(true);F->SetActorTickEnabled(true);F->SetActorLocation(LaunchPoint);F->Body->SetSimulatePhysics(true);
            F->LaunchManualThrow(C.Hero,LaunchDirection,400);
            if(!F->WasManuallyThrown()){bDone=true;if(C.RecordEvent)C.RecordEvent(TEXT("NativeManualThrowRejected"));}
        } else if(auto* F=Fire.Get())F->FinishSpawning(FirePose);
    }
    void Tick(FMCPerkPreviewContext& C,float Dt) override
    {
        if(!bCommitted||bDone)return;Age+=FMath::Max(0.f,Dt);
        if(!HandoffError.IsEmpty()){bDone=true;if(C.ReportStatus)C.ReportStatus(HandoffError);if(C.RecordEvent)C.RecordEvent(TEXT("PreparedNativeHandoffRejected"));return;}
        if(bVomit) {
            auto* T=Throat.Get();if(!T){bDone=true;return;}
            if(!bSpasm&&T->SpasmCount>0){bSpasm=true;if(C.RecordEvent)C.RecordEvent(TEXT("NativeWrongIngredientSpasm"));}
            if(!bBurst&&T->VomitCount>BeforeVomit) {
                auto* B=Burst.Get();auto* P=B?B->FindComponentByClass<UMCWorldPresentationComponent>():nullptr;
                if(B&&B->Portions.Num()>0&&B->HasAcceptedComposerBurst()&&P&&P->GetLastError().IsEmpty())
                {bBurst=true;BurstAt=Age;if(C.RecordEvent)C.RecordEvent(TEXT("Burst"));}
            }
            if(bBurst&&Age-BurstAt>5.2f)bDone=true;
            if(Age>15&&!bBurst){bDone=true;if(C.RecordEvent)C.RecordEvent(TEXT("NativeVomitNotAccepted"));}
        } else {
            auto* F=Fire.Get();if(!F){bDone=true;return;}
            if(!bLoop&&LoopCue.IsValid()&&C.World->GetTimeSeconds()>LoopCue->StartedAt) {
                auto* P=LoopCue->GetComposerPresentation();
                if(P&&P->GetLastError().IsEmpty()&&LoopCue->HasAcceptedComposerEvent(TEXT("Loop"))){bLoop=true;if(C.RecordEvent)C.RecordEvent(TEXT("Loop"));}
            }
            if(bLoop&&!bInput&&Age>1.2f){C.Hero->SetPrimaryInputHeld(true);bInput=true;}
            if(F->Heat<LastHeat){if(!bHeatChanged&&C.RecordEvent)C.RecordEvent(TEXT("NativeSprayExtinguishAccepted"));bHeatChanged=true;}LastHeat=F->Heat;
            if(!bSteam&&SteamCue.IsValid()&&C.World->GetTimeSeconds()>SteamCue->StartedAt) {
                auto* P=SteamCue->GetComposerPresentation();if(P&&P->GetLastError().IsEmpty()&&SteamCue->HasAcceptedComposerEvent(TEXT("Steam")))
                {bSteam=true;BurstAt=Age;C.Hero->SetPrimaryInputHeld(false);if(C.RecordEvent)C.RecordEvent(TEXT("Steam"));}
            }
            if(bSteam&&Age-BurstAt>1.6f)bDone=true;
            if(Age>10&&!bSteam){bDone=true;C.Hero->SetPrimaryInputHeld(false);if(C.RecordEvent)C.RecordEvent(TEXT("NativeFireSprayContactNotAccepted"));}
        }
    }
    void Release(FMCPerkPreviewContext& C) override {if(IsValid(C.Hero))C.Hero->SetPrimaryInputHeld(false);}
    void Cancel(FMCPerkPreviewContext& C) override
    {
        Release(C);Unlisten();if(auto* P=Presentation.Get()){P->StopStates(ID);TArray<UMCVFXPresentationComponent*> Channels;P->GetPresentationComponents(Channels);for(auto* Source:Channels)if(Source)Source->StopPresentation(true);}
        for(auto Weak:{TWeakObjectPtr<AActor>(LoopCue.Get()),TWeakObjectPtr<AActor>(SteamCue.Get()),TWeakObjectPtr<AActor>(Burst.Get()),TWeakObjectPtr<AActor>(Fire.Get()),TWeakObjectPtr<AActor>(Food.Get()),TWeakObjectPtr<AActor>(Throat.Get()),TWeakObjectPtr<AActor>(Tongue.Get())})
            if(auto* Actor=Weak.Get();Actor&&!Actor->IsActorBeingDestroyed()) {
                if(auto* P=Actor->FindComponentByClass<UMCWorldPresentationComponent>()){P->StopStates();TArray<UMCVFXPresentationComponent*> Channels;P->GetPresentationComponents(Channels);for(auto* Source:Channels)if(Source)Source->StopPresentation(true);}Actor->Destroy();
            }
        bDone=true;
    }
    void Reset(FMCPerkPreviewContext& C) override {Cancel(C);Context=nullptr;}
    bool IsComplete(const FMCPerkPreviewContext&) const override {return bDone;}
    FString GetPhaseName(const FMCPerkPreviewContext&) const override
    {return bDone?TEXT("Настоящий сценарий завершён"):bVomit?(bBurst?TEXT("Принятый выброс и реальные столкновения"):bSpasm?TEXT("Настоящий спазм"):TEXT("Штатный бросок и ручной приём")):(bSteam?TEXT("Родной пар"):bHeatChanged?TEXT("Настоящее тушение спреем"):TEXT("Настоящий огненный участок"));}
    float GetTailSeconds() const override{return .3f;}
private:
    void Unlisten(){if(SpawnHandle.IsValid())if(auto* W=World.Get())W->RemoveOnActorSpawnedHandler(SpawnHandle);SpawnHandle.Reset();}
    void OnSpawned(AActor* Actor)
    {
        if(!bCommitted||bDone||!Context||!IsValid(Actor)||Actor->GetWorld()!=World.Get())return;
        bool Match=false;
        if(bVomit) {
            auto* B=Cast<AMCVomitBurst>(Actor);if(!B||Burst.IsValid())return;Burst=B;Match=true;
        } else {
            auto* Cue=Cast<AMCReactionVFX>(Actor);if(!Cue||Cue->GetPresentationSourceID()!=ID)return;
            if(Cue->GetPresentationEvent()==TEXT("Loop")&&!LoopCue.IsValid()){LoopCue=Cue;Match=true;}
            else if(Cue->GetPresentationEvent()==TEXT("Steam")&&!SteamCue.IsValid()){SteamCue=Cue;Match=true;}
        }
        if(!Match)return;Context->Targets.Add(Actor);
        // Native SpawnActor/FinishSpawning returns only after this callback;
        // the accepted producer has not ticked/Configure has not been called.
        auto* Owner=MCWorldPresentation::PrepareOwner(Actor,{ID});auto* Target=Owner?Owner->GetSource(ID):nullptr;
        auto* Prepared=Presentation.Get();auto* Source=Prepared?Prepared->GetSource(ID):nullptr;
        if(!Source||!Target||!Source->CopyPreparedSourceTo(ID,Target,HandoffError))
            HandoffError=TEXT("Нет готовой передачи общего профиля реальному владельцу: ")+HandoffError;
    }
    FName ID;bool bVomit=false,bCommitted=false,bDone=false,bSpasm=false,bBurst=false,bLoop=false,bInput=false,bHeatChanged=false,bSteam=false;
    float Age=0,BurstAt=0,LastHeat=1;int32 BeforeVomit=0;FString HandoffError;FVector LaunchPoint,LaunchDirection;FTransform FirePose;
    FMCPerkPreviewContext* Context=nullptr;FDelegateHandle SpawnHandle;TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<UMCWorldPresentationComponent> Presentation;TWeakObjectPtr<AMCTongue> Tongue;TWeakObjectPtr<AMCThroat> Throat;
    TWeakObjectPtr<AMCFoodActor> Food;TWeakObjectPtr<AMCVomitBurst> Burst;TWeakObjectPtr<AMCFirePatch> Fire;
    TWeakObjectPtr<AMCReactionVFX> LoopCue,SteamCue;
};
}
void MCNativeWorldActionPreviewAdapters::Register()
{
    for(const bool Vomit:{false,true}) {
        const FName ID=Vomit?TEXT("World.Vomit"):TEXT("World.FirePatch");const auto* Contract=MCVFXSourceContracts::Find(ID);if(!Contract)continue;
        FMCPerkPreviewAdapterEntry E;E.PerkID=ID;E.Label=Contract->Label;E.Stages=Contract->Events;E.Kind=EMCPerkPreviewAdapterKind::Context;E.bSupportsProfileOverride=true;
        E.Scenarios={EMCPerkPreviewScenario::Target};E.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(Vomit?TEXT("Неправильный ингредиент → настоящий выброс"):TEXT("Настоящий огонь → тушение спреем")));
        E.Create=[Vomit]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FNativeWorldAdapter>(Vomit);};MCPerkPreviewAdapters::Register(MoveTemp(E));
    }
}
