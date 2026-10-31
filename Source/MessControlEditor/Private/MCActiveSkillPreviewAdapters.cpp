#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewGameMode.h"

#include "MCAirSkillAction.h"
#include "MCFoodActor.h"
#include "MCGrenadeSkillAction.h"
#include "MCHookSkillAction.h"
#include "MCMouthSurface.h"
#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "MCSpongeSkillAction.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"

#include "Camera/CameraActor.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include <initializer_list>

namespace MCActivePreview
{
// All fixtures belong to the runner's private transient Game world. None of these
// adapters modifies a CDO, balance row, source asset, map or the player's world.
bool CheckContext(FMCPerkPreviewContext& C,FString& Error)
{
    if(!C.World || C.World->WorldType!=EWorldType::Game || C.World->GetOutermost()!=GetTransientPackage()
        || !C.World->GetAuthGameMode<AMCPerkPreviewGameMode>() || !IsValid(C.Hero)
        || C.Hero->GetWorld()!=C.World || !C.Controller || C.Controller->GetWorld()!=C.World)
    {
        Error=TEXT("Для действия необходим собственный временный Game-мир предпросмотра с управляемым зубиком.");
        return false;
    }
    return true;
}

void AimForward(FMCPerkPreviewContext& C)
{
    C.Controller->SetControlRotation(FRotator::ZeroRotator);
    if(C.AimCamera)
    {
        C.AimCamera->SetActorLocationAndRotation(C.Hero->GetActorLocation()+FVector(0,0,35),FRotator::ZeroRotator);
        C.Controller->SetViewTarget(C.AimCamera);
    }
}

void HideDefaultTargets(FMCPerkPreviewContext& C)
{
    // The runner retains its original neutral Rocket target as stage furniture.
    // Other adapters supply their own scenario targets instead of accidentally
    // hitting that hidden-in-another-mode wall on a supposedly open miss.
    for(const auto& Weak:C.Targets) if(auto* Target=Cast<AStaticMeshActor>(Weak.Get()))
    {
        Target->SetActorHiddenInGame(true);
        Target->SetActorEnableCollision(false);
    }
}

void EnableGroundMovement(AMCToothCharacter* H)
{
    if(!H) return;
    auto* Move=H->GetCharacterMovement();
    Move->StopMovementImmediately();
    Move->SetMovementMode(MOVE_Walking);
    // Query real support. Do not manufacture a walkable floor or tick the world
    // here; the runner's regular warmup evaluates its capsule and physical arms.
    Move->FindFloor(H->GetActorLocation(),Move->CurrentFloor,false);
    Move->bForceNextFloorCheck=true;
}

float Parameter(const FMCPerkPreviewContext& C,FName Key,float Fallback)
{
    if(C.Definition) if(const float* Value=C.Definition->Parameters.Find(Key)) return *Value;
    return Fallback;
}

void Note(FMCPerkPreviewContext& C,const FString& Text)
{
    if(C.ReportStatus) C.ReportStatus(Text);
}

template<typename T> bool RequireAction(const AMCActiveSkillAction& Action,FString& Error)
{
    if(Action.IsA<T>()) return true;
    Error=FString::Printf(TEXT("Строка перка создала %s вместо %s; подмена механики не выполняется."),
        *Action.GetClass()->GetName(),*T::StaticClass()->GetName());
    return false;
}

/** A real saved food row: the native food actor owns collision, identity and bag claims. */
struct FFoodFixture
{
    TStrongObjectPtr<UDataTable> Menu;
    FName RowName;
    FMCFoodRow Row;

    bool Prepare(FString& Error)
    {
        Menu.Reset(LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu")));
        if(!Menu.IsValid() || Menu->GetRowStruct()!=FMCFoodRow::StaticStruct())
        {
            Error=TEXT("Не удалось прочитать настоящий DT_BreakfastMenu для физических продуктов.");
            return false;
        }
        auto Names=Menu->GetRowNames();
        Names.Sort([](FName A,FName B){return A.LexicalLess(B);});
        FName Fallback;
        for(FName Name:Names)
        {
            const auto* Candidate=Menu->FindRow<FMCFoodRow>(Name,TEXT("ActivePreviewFood"),false);
            if(!Candidate || Candidate->Kind!=EMCFoodKind::Food || Candidate->FragmentMeshes.IsEmpty()) continue;
            bool bGrape=false;
            for(const auto& Mesh:Candidate->WholeMeshes) bGrape|=Mesh.ToSoftObjectPath().ToString().Contains(TEXT("Grape"));
            if(Fallback.IsNone()) Fallback=Name;
            if(bGrape) {RowName=Name;break;}
        }
        if(RowName.IsNone()) RowName=Fallback;
        const auto* Found=Menu->FindRow<FMCFoodRow>(RowName,TEXT("ActivePreviewFood"),false);
        if(!Found) {Error=TEXT("В меню нет обычной еды с настоящими мешами фрагментов.");return false;}
        Row=*Found;
        // Load only this selected ingredient before the runner's resource gate.
        // Runtime ConfigureItem then uses its existing authored collision data.
        bool bLoaded=false;
        for(const auto& Mesh:Row.FragmentMeshes) bLoaded|=Mesh.LoadSynchronous()!=nullptr;
        if(!bLoaded) {Error=TEXT("Меши фрагментов выбранного продукта отсутствуют.");return false;}
        return true;
    }

    AMCFoodActor* Spawn(FMCPerkPreviewContext& C,FVector Position,int32 Index,bool bFragment=true)
    {
        FActorSpawnParameters Spawn;
        Spawn.ObjectFlags=RF_Transient;
        Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Spawn.bDeferConstruction=true;
        const FTransform Pose(Position);
        auto* Food=C.World->SpawnActor<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,Spawn);
        if(!Food) return nullptr;
        Food->Tags.Add(TEXT("MC.VFXActionPreview"));
        FRandomStream Random(3517+Index);
        Food->ConfigureItem(RowName,Row,Random,bFragment);
        Food->Batch=0;
        Food->FinishSpawning(Pose);
        C.Targets.Add(Food);
        return Food;
    }

    bool FragmentHalfExtent(int32 Index,FVector& OutExtent) const
    {
        // Match the variant selected by ConfigureItem without changing its
        // authored scale, pivot or baked collision. UMCFoodBodyComponent uses
        // these same mesh bounds for its query box half extent.
        FMCFoodRow NativeRow=Row;NativeRow.Sanitize();
        TArray<int32> Available;
        for(int32 I=0;I<NativeRow.FragmentMeshes.Num();++I)
            if(!NativeRow.FragmentMeshes[I].IsNull()) Available.Add(I);
        FRandomStream Random(3517+Index);
        while(!Available.IsEmpty())
        {
            const int32 Pick=Random.RandRange(0,Available.Num()-1);
            auto* Mesh=NativeRow.FragmentMeshes[Available[Pick]].LoadSynchronous();
            Available.RemoveAt(Pick);
            if(Mesh)
            {
                OutExtent=Mesh->GetBounds().BoxExtent*NativeRow.FragmentScale.GetAbs();
                return !OutExtent.ContainsNaN() && OutExtent.GetMin()>0;
            }
        }
        return false;
    }
};

class FHookAdapter final:public IMCPerkPreviewAdapter
{
public:
    virtual bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
    {
        if(!CheckContext(C,Error)) return false;
        bReported=false;HideDefaultTargets(C);EnableGroundMovement(C.Hero);AimForward(C);
        if(C.Scenario==EMCPerkPreviewScenario::NearWall)
        {
            if(!C.AddBox) {Error=TEXT("Нет фабрики временной стены.");return false;}
            auto* Wall=C.AddBox(TEXT("Hook_WorldBlocker"),FVector(420,0,170),FVector(.35f,6.f,3.4f));
            if(!Wall) {Error=TEXT("Не удалось создать стену для настоящей проверки попадания крюка.");return false;}
            C.Targets.Add(Wall);
        }
        else if(C.Scenario==EMCPerkPreviewScenario::Target)
        {
            FActorSpawnParameters Spawn;
            Spawn.ObjectFlags=RF_Transient;
            Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* AllyPC=C.World->SpawnActor<APlayerController>(FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
            auto* AllyPS=AllyPC?AllyPC->GetPlayerState<AMCPlayerState>():nullptr;
            if(!AllyPS) AllyPS=C.World->SpawnActor<AMCPlayerState>(FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
            auto* Ally=C.World->SpawnActor<AMCToothCharacter>(FVector(790,0,C.Hero->GetActorLocation().Z),FRotator(0,180,0),Spawn);
            if(!AllyPC || !AllyPS || !Ally) {Error=TEXT("Не удалось создать зубика-союзника для крюка.");return false;}
            C.Targets.Add(AllyPC);C.Targets.Add(AllyPS);C.Targets.Add(Ally);
            // The private world deliberately has no LocalPlayer or net driver.
            // Mark only this sandbox controller local so CMC executes the real
            // pull/root-motion source instead of waiting for remote move packets.
            AllyPC->SetAsLocalPlayerController();
            AllyPC->SetPlayerState(AllyPS);AllyPS->SetOwner(AllyPC);AllyPS->SetPlayerName(TEXT("Союзник"));
            AllyPC->Possess(Ally);AllyPC->bShowMouseCursor=false;
            Ally->Status->Initialize(100.f);EnableGroundMovement(Ally);
            Ally->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            Ally->GetMesh()->bEnableUpdateRateOptimizations=false;
        }
        return true;
    }
    virtual bool ValidateAction(const AMCActiveSkillAction& A,FString& Error) const override {return RequireAction<AMCHookSkillAction>(A,Error);}
    virtual void Tick(FMCPerkPreviewContext& C,float) override
    {
        const auto* Hook=Cast<AMCHookSkillAction>(C.Action);
        if(Hook && Hook->IsFinished() && !bReported)
        {
            bReported=true;
            Note(C,Hook->DidContactTarget()?TEXT("Крюк поймал и подтянул настоящего зубика; возврат и реакция завершены.")
                :TEXT("Крюк вернулся после промаха или препятствия; цель не перемещалась."));
        }
    }
    virtual FString GetPhaseName(const FMCPerkPreviewContext& C) const override
    {
        const auto* Hook=Cast<AMCHookSkillAction>(C.Action);
        if(!Hook) return TEXT("Возврат к обычной стойке");
        switch(Hook->GetCastPhase())
        {
        case EMCHookCastPhase::Windup:return TEXT("Крюк · замах");
        case EMCHookCastPhase::Outbound:return TEXT("Крюк · полёт");
        case EMCHookCastPhase::Pulling:return TEXT("Крюк · подтягивание");
        case EMCHookCastPhase::Returning:return TEXT("Крюк · возвращение");
        case EMCHookCastPhase::Recovery:return TEXT("Крюк · восстановление хвата");
        default:return TEXT("Крюк · подготовка");
        }
    }
private:
    bool bReported=false;
};

class FSpongeAdapter final:public IMCPerkPreviewAdapter
{
public:
    virtual bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
    {
        if(!CheckContext(C,Error)) return false;
        LastBounce=0;bRepeated=false;Stains.Reset();
        HideDefaultTargets(C);EnableGroundMovement(C.Hero);AimForward(C);
        for(int32 Index=0;Index<3;++Index)
        {
            const FTransform Pose(FVector(180+Index*180,0,1.5f));
            FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;Spawn.bDeferConstruction=true;
            Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Stain=C.World->SpawnActor<AMCMouthSurface>(AMCMouthSurface::StaticClass(),Pose,Spawn);
            if(!Stain) {Error=TEXT("Не удалось создать настоящую маску загрязнения.");return false;}
            Stain->bRandomizeLiquidSize=false;Stain->LiquidHalfSize=85.f;Stain->LiquidSeed=17+Index;
            Stain->FinishSpawning(Pose);Stain->Status->ApplyCoffee(1.f);Stain->ResetLiquid();
            C.Targets.Add(Stain);Stains.Add(Stain);
        }
        if(C.Scenario==EMCPerkPreviewScenario::NearWall)
        {
            if(!C.AddBox) {Error=TEXT("Нет фабрики временной стены для отражения Губки.");return false;}
            auto* Wall=C.AddBox(TEXT("Sponge_ReflectionWall"),FVector(740,0,150),FVector(.35f,8.f,3.f));
            if(!Wall) {Error=TEXT("Не удалось создать стену для отражения Губки.");return false;}
            C.Targets.Add(Wall);
        }
        return true;
    }
    virtual bool ValidateAction(const AMCActiveSkillAction& A,FString& Error) const override {return RequireAction<AMCSpongeSkillAction>(A,Error);}
    virtual void Tick(FMCPerkPreviewContext& C,float) override
    {
        auto* Sponge=Cast<AMCSpongeSkillAction>(C.Action);
        if(!Sponge || !Sponge->IsCommitted() || Sponge->IsFinished()) return;
        if(Sponge->BounceCount>LastBounce)
        {
            LastBounce=Sponge->BounceCount;
            if(C.RecordEvent) C.RecordEvent(TEXT("SpongeWallReflection"));
        }
        // This is the actual second press documented for Sponge, not an actor
        // timeout or a direct pose reset. Timeout demonstrates its native timer.
        if(C.Scenario!=EMCPerkPreviewScenario::Timeout && C.Elapsed>=2.7f && !bRepeated)
        {
            bRepeated=true;Sponge->RepeatInput();
            if(C.RecordEvent) C.RecordEvent(TEXT("SpongeRepeatInput"));
            int32 Changed=0;
            for(const auto& Stain:Stains) if(Stain.IsValid() && Stain->RemainingLiquid()<.999f) ++Changed;
            Note(C,FString::Printf(TEXT("Поездка завершена повторным нажатием. Изменённых реальных масок: %d; отражений: %d."),Changed,LastBounce));
        }
    }
    virtual FString GetPhaseName(const FMCPerkPreviewContext& C) const override
    {
        const auto* Sponge=Cast<AMCSpongeSkillAction>(C.Action);
        return Sponge && !Sponge->IsFinished()?FString::Printf(TEXT("Губка · скольжение / отражений %d"),Sponge->BounceCount)
            :TEXT("Губка · сход / затухание мыльного следа");
    }
private:
    TArray<TWeakObjectPtr<AMCMouthSurface>> Stains;
    int32 LastBounce=0;
    bool bRepeated=false;
};

class FGrenadeAdapter final:public IMCPerkPreviewAdapter
{
public:
    virtual bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
    {
        if(!CheckContext(C,Error) || !Food.Prepare(Error)) return false;
        bReleased=false;HideDefaultTargets(C);AimForward(C);
        for(int32 Index=0;Index<3;++Index)
            if(!Food.Spawn(C,FVector(590+Index*90,(Index-1)*65,65),Index))
            {Error=TEXT("Не удалось создать физическую пищевую цель гранаты.");return false;}
        if(C.Scenario==EMCPerkPreviewScenario::NearWall)
        {
            if(!C.AddBox) {Error=TEXT("Нет фабрики временной стены.");return false;}
            auto* Wall=C.AddBox(TEXT("Grenade_BounceWall"),FVector(390,0,180),FVector(.35f,8.f,3.6f));
            if(!Wall) {Error=TEXT("Не удалось создать стену для отскока гранаты.");return false;}
            C.Targets.Add(Wall);
        }
        ReleaseAt=FMath::Clamp(Parameter(C,TEXT("FuseSeconds"),3.f)*.22f,.1f,.7f);
        return true;
    }
    virtual bool ValidateAction(const AMCActiveSkillAction& A,FString& Error) const override {return RequireAction<AMCGrenadeSkillAction>(A,Error);}
    virtual void Tick(FMCPerkPreviewContext& C,float) override
    {
        auto* Grenade=Cast<AMCGrenadeSkillAction>(C.Action);
        if(!Grenade || !Grenade->IsCommitted() || Grenade->IsFinished()) return;
        if(C.Scenario!=EMCPerkPreviewScenario::Timeout && !bReleased && C.Elapsed>=ReleaseAt)
        {
            bReleased=true;Grenade->ReleaseInput();
            if(C.RecordEvent) C.RecordEvent(TEXT("GrenadeReleaseInput"));
        }
    }
    virtual FString GetPhaseName(const FMCPerkPreviewContext& C) const override
    {
        const auto* Grenade=Cast<AMCGrenadeSkillAction>(C.Action);
        if(!Grenade) return TEXT("Граната · восстановление / остаток взрыва");
        switch(Grenade->Phase)
        {
        case EMCGrenadeSkillPhase::Cooking:return TEXT("Граната · удержание / фитиль");
        case EMCGrenadeSkillPhase::Thrown:return TEXT("Граната · бросок / физический отскок");
        case EMCGrenadeSkillPhase::Dropped:return TEXT("Граната · выпадение после передержки");
        case EMCGrenadeSkillPhase::Detonated:return TEXT("Граната · взрыв");
        default:return TEXT("Граната · подготовка");
        }
    }
private:
    FFoodFixture Food;
    float ReleaseAt=.65f;
    bool bReleased=false;
};

class FAirAdapter final:public IMCPerkPreviewAdapter
{
public:
    virtual bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
    {
        if(!CheckContext(C,Error) || !Food.Prepare(Error)) return false;
        bReleased=false;MostStored=0;FedCount=0;NextFeedAt=0;HideDefaultTargets(C);AimForward(C);
        Capacity=FMath::Clamp(FMath::RoundToInt(Parameter(C,TEXT("BagCapacity"),6)),1,16);
        const int32 Count=C.Scenario==EMCPerkPreviewScenario::Timeout?Capacity:FMath::Min(3,FMath::Max(1,Capacity-1));
        ExpectedCount=Count;
        for(int32 Index=0;Index<(C.Scenario==EMCPerkPreviewScenario::Timeout?0:Count);++Index)
        {
            // All pieces start in the native intake cone and remain ordinary
            // Chaos bodies. The action must actually pull and claim each actor.
            const FVector Position(220+(Index%4)*75,(Index/4-1.5f)*32,55);
            auto* Piece=Food.Spawn(C,Position,Index);
            if(!Piece) {Error=TEXT("Не удалось создать реальные фрагменты для Воздухана.");return false;}
        }
        if(C.Scenario==EMCPerkPreviewScenario::NearWall)
        {
            // Obstacle behind the intake supply lets the real blow collide;
            // it never obstructs the short hero-to-nozzle safety trace.
            if(!C.AddBox) {Error=TEXT("Нет фабрики временной стены.");return false;}
            auto* Wall=C.AddBox(TEXT("Air_BlowWall"),FVector(650,0,170),FVector(.35f,8.f,3.4f));
            if(!Wall) {Error=TEXT("Не удалось создать стену для выдувания фрагментов.");return false;}
            C.Targets.Add(Wall);
        }
        return true;
    }
    virtual bool ValidateAction(const AMCActiveSkillAction& A,FString& Error) const override {return RequireAction<AMCAirSkillAction>(A,Error);}
    virtual void OnCommitted(FMCPerkPreviewContext& C) override
    {
        if(C.Scenario==EMCPerkPreviewScenario::Timeout)
        {
            if(auto* Air=Cast<AMCAirSkillAction>(C.Action))
            {
                ExpectedCount=Air->BagCapacity;
                FeedNext(C,*Air);
            }
        }
    }
    virtual void Tick(FMCPerkPreviewContext& C,float) override
    {
        auto* Air=Cast<AMCAirSkillAction>(C.Action);
        if(!Air || !Air->IsCommitted() || Air->IsFinished()) return;
        MostStored=FMath::Max(MostStored,Air->StoredCount);
        if(C.Scenario==EMCPerkPreviewScenario::Timeout && Air->bSucking && !Air->bOverflowWarning
            && FedCount<ExpectedCount && Air->StoredCount>=FedCount && C.Elapsed>=NextFeedAt)
            FeedNext(C,*Air);
        // Timeout holds the same accepted press through the native full-bag
        // grace and rupture. No adapter sets stored count or manufactures cues.
        if(C.Scenario!=EMCPerkPreviewScenario::Timeout && Air->bSucking && !bReleased
            && ((Air->StoredCount>=ExpectedCount && C.Elapsed>=.65f) || C.Elapsed>=4.5f))
        {
            bReleased=true;Air->ReleaseInput();
            if(C.RecordEvent) C.RecordEvent(TEXT("AirReleaseInput"));
            Note(C,FString::Printf(TEXT("Отпущена та же кнопка слота: реально захвачено %d из %d; выдувание выполняет штатное действие."),MostStored,ExpectedCount));
        }
    }
    virtual FString GetPhaseName(const FMCPerkPreviewContext& C) const override
    {
        const auto* Air=Cast<AMCAirSkillAction>(C.Action);
        if(!Air) return TEXT("Воздухан · восстановление хвата");
        if(Air->bRuptured) return TEXT("Воздухан · разрыв переполненного мешка");
        if(Air->bOverflowWarning) return TEXT("Воздухан · мешок полон / предупреждение");
        if(Air->bSucking) return FString::Printf(TEXT("Воздухан · всасывание %d/%d"),Air->StoredCount,Air->BagCapacity);
        return TEXT("Воздухан · выдувание / отдача");
    }
private:
    void FeedNext(FMCPerkPreviewContext& C,AMCAirSkillAction& Air)
    {
        // This scenario isolates the real full-bag warning and rupture, rather
        // than depending on scattered floor pieces reaching the intake cone.
        // Feed one ordinary physical fragment only after the native action has
        // captured the previous actor. No bag state, food claim or cue is set.
        FVector HalfExtent;
        const FVector Forward=Air.GetIntakeForward().GetSafeNormal();
        if(!Food.FragmentHalfExtent(FedCount,HalfExtent) || Forward.IsNearlyZero())
        {
            NextFeedAt=C.Elapsed+.5f;
            Note(C,TEXT("Не удалось определить физический размер фрагмента для безопасной подачи."));
            return;
        }
        // A +35 cm fixture offset works for the unit-test's tiny cube, but a
        // real authored grape is roughly 134 cm wide. Placing its centre that
        // close puts the same food across the hero-to-nozzle clearance ray and
        // correctly prevents native suction. Keep the entire query box beyond
        // that ray, while still inside the native distance-based capture gate.
        const float Along=FVector::DotProduct(Forward.GetAbs(),HalfExtent);
        const float SafeNear=Along+12.f;
        const float Capture=FMath::Clamp(Air.Param(TEXT("CaptureRadius"),70.f),25.f,120.f)+HalfExtent.GetMin();
        const float SafeFar=Capture-8.f;
        if(SafeNear>=SafeFar)
        {
            NextFeedAt=C.Elapsed+.5f;
            Note(C,TEXT("Этот фрагмент не помещается между безопасным просветом носика и штатной зоной захвата. Переполнение не подтверждено."));
            return;
        }
        const FVector Position=Air.GetIntakePoint()+Forward*((SafeNear+SafeFar)*.5f);
        if(Food.Spawn(C,Position,FedCount))
        {
            ++FedCount;
            NextFeedAt=C.Elapsed+.15f;
        }
        else
        {
            NextFeedAt=C.Elapsed+.5f;
            Note(C,TEXT("Не удалось подать следующий физический фрагмент; нативное переполнение ещё не подтверждено."));
        }
    }
    FFoodFixture Food;
    int32 Capacity=6,ExpectedCount=3,MostStored=0;
    int32 FedCount=0;
    float NextFeedAt=0;
    bool bReleased=false;
};

template<typename T> void Register(FName ID,const TCHAR* Label,std::initializer_list<TPair<FName,const TCHAR*>> Stages,
    const TCHAR* TargetLabel,const TCHAR* WallLabel,const TCHAR* TimeoutLabel)
{
    FMCPerkPreviewAdapterEntry Entry;
    Entry.PerkID=ID;Entry.Label=FText::FromString(Label);
    for(const auto& Stage:Stages)
    {
        Entry.Stages.Add(Stage.Key);
        Entry.StageLabels.Add(Stage.Key,FText::FromString(Stage.Value));
    }
    Entry.Scenarios={EMCPerkPreviewScenario::Target,EMCPerkPreviewScenario::NearWall,EMCPerkPreviewScenario::Timeout};
    Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(TargetLabel));
    Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::NearWall,FText::FromString(WallLabel));
    Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Timeout,FText::FromString(TimeoutLabel));
    Entry.Create=[]()->TUniquePtr<IMCPerkPreviewAdapter> {return MakeUnique<T>();};
    MCPerkPreviewAdapters::Register(MoveTemp(Entry));
}

// Metadata only. UObject creation and package resolution are deferred until a
// user selects an action and the runner supplies its isolated preview world.
const bool bRegistered=[]
{
    Register<FHookAdapter>(TEXT("Active_Hook"),TEXT("Крюк"),
        {{TEXT("Start"),TEXT("Бросок")},{TEXT("Loop"),TEXT("Полёт крюка")},{TEXT("Impact"),TEXT("Зацеп")},{TEXT("End"),TEXT("Возврат")}},
        TEXT("Подтянуть союзника"),TEXT("Препятствие на пути"),TEXT("Промах и возврат"));
    Register<FSpongeAdapter>(TEXT("Active_Sponge"),TEXT("Губка"),
        {{TEXT("Start"),TEXT("Старт")},{TEXT("Loop"),TEXT("Движение")},{TEXT("End"),TEXT("Завершение")}},
        TEXT("Уборка и сход"),TEXT("Отражение от стены"),TEXT("До конца поездки"));
    Register<FGrenadeAdapter>(TEXT("Consumable_Grenade"),TEXT("Граната"),
        {{TEXT("Start"),TEXT("Подготовка броска")},{TEXT("Loop"),TEXT("Фитиль и полёт")},{TEXT("Warning"),TEXT("Сигнал опасности")},{TEXT("Impact"),TEXT("Взрыв")}},
        TEXT("Удержать и бросить"),TEXT("Отскок от стены"),TEXT("Передержать в руке"));
    Register<FAirAdapter>(TEXT("Consumable_Air"),TEXT("Воздухан"),
        {{TEXT("Loop"),TEXT("Всасывание")},{TEXT("Impact"),TEXT("Выдувание")},{TEXT("Warning"),TEXT("Перегрузка")},{TEXT("End"),TEXT("Разрыв мешка")}},
        TEXT("Всосать и выдуть"),TEXT("Выдуть в стену"),TEXT("Переполнить мешок"));
    return true;
}();
}
