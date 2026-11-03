#include "MCHurtActionPreviewAdapters.h"
#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewGameMode.h"
#include "MCVFXSourceContracts.h"
#include "MCVFXPresentationComponent.h"
#include "MCWorldPresentationComponent.h"
#include "MCCombatFeedbackComponent.h"
#include "MCReactionVFX.h"
#include "MCCombatFeedbackStation.h"
#include "MCVFXLabBossStation.h"
#include "MCNutCombatEffect.h"
#include "MCNutBoss.h"
#include "MCBossCharacter.h"
#include "MCNutRainProfile.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCTongue.h"
#include "MCDayPlan.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/Package.h"

namespace
{
class FHurtAdapter final:public IMCPerkPreviewAdapter
{
public:
    explicit FHurtAdapter(FName Source):ID(Source){}
    bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
    {
        if(!C.World||C.World->WorldType!=EWorldType::Game||C.World->GetOutermost()!=GetTransientPackage()
            ||!C.World->GetAuthGameMode<AMCPerkPreviewGameMode>()||C.PerkID!=ID||!IsValid(C.Hero)||C.Hero->GetWorld()!=C.World
            ||!C.Hero->HasAuthority()||!C.Hero->Status||!C.Hero->Status->IsAlive())
        {Error=TEXT("Нужны настоящий живой зубик и authority-мир изолированной песочницы.");return false;}
        const bool Player=ID==TEXT("Character.Hurt"),Nut=ID==TEXT("Enemy.Nut.Hurt");
        auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
        Plane=Plane?DuplicateObject<UStaticMesh>(Plane,GetTransientPackage()):nullptr;
        if(!Plane){Error=TEXT("Нет частной поверхности настоящего языка.");return false;}
        Plane->ClearFlags(RF_Public|RF_Standalone);Plane->SetFlags(RF_Transient);Plane->bAllowCPUAccess=true;
        const FTransform FloorPose(FRotator::ZeroRotator,FVector(0,0,32),FVector(35,35,1));
        auto* Floor=C.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FloorPose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Floor){Error=TEXT("Не создан настоящий язык.");return false;}
        Tongue=Floor;C.Targets.Add(Floor);Floor->SourceMesh=Plane;Floor->bAutomaticYawns=false;Floor->SetFlags(RF_Transient);
        Floor->FinishSpawning(FloorPose);Floor->SetActorTickEnabled(false);Floor->Surface->SetVisibility(false);Floor->Surface->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        C.Hero->SetActorLocation(C.Hero->GetActorLocation()+FVector(0,0,32),false,nullptr,ETeleportType::TeleportPhysics);
        FVector Point=C.Hero->GetActorLocation()+FVector(250,0,0);Point.Z=32;
        if(Player||Nut) {
            auto* Data=LoadObject<UMCNutRainProfile>(nullptr,TEXT("/Game/Gameplay/CoreLoop/DA_NutRain.DA_NutRain"));
            if(!Data){Error=TEXT("Нет сохранённого DA_NutRain.");return false;}
            UStaticMesh* Mesh=nullptr;
            if(auto* Menu=Data->Menu.LoadSynchronous())if(const auto* Row=Menu->FindRow<FMCFoodRow>(Data->NutRow,TEXT("ComposerHurt"),false))
                if(!Row->WholeMeshes.IsEmpty())Mesh=Row->WholeMeshes[0].LoadSynchronous();
            if(!Mesh)Mesh=Data->Settings.Boss.WholeMesh.LoadSynchronous();
            FMCNutEnemySettings Settings=Data->Settings.Enemy;Settings.Sanitize();
            if(!Mesh||Mesh->GetBounds().BoxExtent.Z<=UE_SMALL_NUMBER){Error=TEXT("Нет исходного сохранённого меша ореха.");return false;}
            const FTransform Pose(FRotator(0,180,0),Point+FVector(0,0,Settings.BodyRadius+4));
            auto* Enemy=C.World->SpawnActorDeferred<AMCCombatFeedbackNut>(AMCCombatFeedbackNut::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(!Enemy){Error=TEXT("Не создан настоящий damage-владелец ореха.");return false;}
            C.Targets.Add(Enemy);NativeNut=Enemy;Enemy->SetFlags(RF_Transient);
            Enemy->ConfigureEnemy(Floor,Mesh,FVector(Settings.BodyRadius/Mesh->GetBounds().BoxExtent.Z),Settings);Enemy->FinishSpawning(Pose);
            // Existing native development subclass disables autonomous AI only;
            // ReceiveToolDamage, health, feedback, model and presentation are inherited unchanged.
            Victim=Player?static_cast<AActor*>(C.Hero):static_cast<AActor*>(Enemy);
            Source=Player?static_cast<AActor*>(Enemy):static_cast<AActor*>(C.Hero);
        } else {
            const bool Generic=ID==TEXT("Boss.Generic.Hurt"),Mage=ID==TEXT("Boss.NutMage.Hurt");
            const EMCVFXLabBossKind Kind=Generic?EMCVFXLabBossKind::ZombieHurt:Mage?EMCVFXLabBossKind::MageHurt:EMCVFXLabBossKind::TankShieldHit;
            const FTransform Pose(FRotator(0,180,0),Point);
            auto* Lab=C.World->SpawnActorDeferred<AMCVFXLabBossStation>(AMCVFXLabBossStation::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(!Lab){Error=TEXT("Не создан владелец исходного boss fixture.");return false;}
            Station=Lab;C.Targets.Add(Lab);Lab->SetFlags(RF_Transient);Lab->Floor=Floor;Lab->bAutoRun=false;Lab->bComparisonMode=true;Lab->Kind=Kind;
            Lab->FinishSpawning(Pose);Lab->Label->SetHiddenInGame(true);
            if(!Lab->PrepareComparisonUsingSubject(Kind,C.Hero)){Error=Lab->Status;return false;}
            Lab->SetActorTickEnabled(false);auto* Target=Lab->GetComparisonEnemy();
            if(!Target){Error=TEXT("Нет настоящего сохранённого boss victim.");return false;}
            Victim=Target;Source=C.Hero;C.Targets.Add(Target);
            if(auto* B=Cast<AMCNutBoss>(Target)) {
                // Same already-landed fixture as the existing shield adapter;
                // no entrance/damage event is claimed by this setup.
                FHitResult Ground;if(!Floor->SurfacePoint(Point,Ground)){Error=TEXT("Нет реальной опоры босса.");return false;}
                B->State=EMCNutBossState::Idle;B->Attack=EMCNutBossAttack::None;B->SetActorLocation(Ground.ImpactPoint+FVector(0,0,B->Settings.BodyRadius+4));
                for(TActorIterator<AMCNutCombatEffect> It(C.World);It;++It)
                    if(It->GetOwner()==B&&It->Cue.Type==EMCNutCombatCue::JumpTell)It->Destroy();
            }
            Target->SetActorTickEnabled(false); // real accepted damage below, no battle AI during isolated contact review
        }
        auto* V=Victim.Get();Feedback=V?V->FindComponentByClass<UMCCombatFeedbackComponent>():nullptr;
        if(!Feedback.IsValid()||!Feedback->PresentationSources.Contains(ID)||V->ActorHasTag(TEXT("MC.LegacyCombatFeedback")))
        {Error=TEXT("Настоящий владелец не объявляет этот source ID; legacy custom lane не подменяется.");return false;}
        Presentation=V->FindComponentByClass<UMCWorldPresentationComponent>();
        if(!Presentation.IsValid()){Error=TEXT("У настоящего feedback owner нет готового общего канала.");return false;}
        BeforeSerial=Feedback->LastHit.Serial;BeforeHealth=Health(V);BeforeShield=Shield(V);
        if(C.RequestFramingUpdate)C.RequestFramingUpdate();
        if(C.ReportStatus)C.ReportStatus(TEXT("Один настоящий resolved damage вызов на исходном владельце → реальная потеря HP/щита → его точный OriginSourceID/Source. Боевой AI и обычный взмах этим сценарием не заявляются. Числовой UI остаётся штатным."));
        return true;
    }
    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error) override
    {
        auto* P=Presentation.Get();const auto* Contract=MCVFXSourceContracts::Find(ID);
        if(!P||!Contract||!P->PrepareSources(Error,true))return false;
        auto* S=P->GetSource(ID);if(!S){Error=TEXT("Нет подготовленного источника жертвы.");return false;}
        return !C.PresentationOverride||S->PrepareScopedOverride(ID,Contract->Events,C.PresentationOverride,Error);
    }
    void GetPreparedVisualSystems(const FMCPerkPreviewContext&,TArray<UNiagaraSystem*>& Out)const override
    {if(const auto* P=Presentation.Get())if(const auto* S=P->GetSource(ID))S->GetPreparedResources(Out);}
    void OnCommitted(FMCPerkPreviewContext& C) override
    {
        bCommitted=true;auto* V=Victim.Get();auto* S=Source.Get();if(!V||!S){bDone=true;return;}
        if(auto* Hero=Cast<AMCToothCharacter>(V))bNativeAccepted=Hero->Status->Damage(12,(Hero->GetActorLocation()-S->GetActorLocation()).GetSafeNormal2D(),S);
        else if(auto* N=Cast<AMCNutEnemy>(V)) {const float Before=N->Health,OldShield=Shield(N);N->ReceiveToolDamage(12,C.Hero);bNativeAccepted=N->Health<Before||Shield(N)<OldShield;}
        else if(auto* B=Cast<AMCBossCharacter>(V))bNativeAccepted=B->ReceiveBossDamage(12,C.Hero)>0;
        for(TActorIterator<AMCReactionVFX> It(C.World);It;++It)if(It->GetOwner()==V
            &&(It->Effect==EMCReactionEffect::PlayerHit||It->Effect==EMCReactionEffect::CombatHit))
            {NativeLabels.Add(*It);C.Targets.Add(*It);}
        if(!bNativeAccepted){bDone=true;if(C.RecordEvent)C.RecordEvent(TEXT("NativeHurtDamageRejected"));}
    }
    void Tick(FMCPerkPreviewContext& C,float Dt) override
    {
        if(!bCommitted||bDone)return;Age+=FMath::Max(0.f,Dt);
        auto* V=Victim.Get();auto* F=Feedback.Get();auto* P=Presentation.Get();
        if(!V||!F||!P){bDone=true;return;}
        const auto& H=F->LastHit;
        if(!bObserved&&H.Serial>BeforeSerial) {
            if(H.OriginSourceID!=ID||H.PresentationEvent!=TEXT("Hit")||H.Source!=Source.Get()
                ||(Health(V)>=BeforeHealth&&Shield(V)>=BeforeShield)||!P->GetLastError().IsEmpty()) {
                bDone=true;if(C.ReportStatus)C.ReportStatus(TEXT("Принятый damage snapshot не совпал с реальной жертвой/source/потерей HP; эффект вручную не подставлен. ")+P->GetLastError());
                if(C.RecordEvent)C.RecordEvent(TEXT("NativeHurtIdentityMismatch"));return;
            }
            bObserved=true;if(C.RecordEvent)C.RecordEvent(TEXT("Hit"));
        }
        if((bObserved&&Age>1.3f)||Age>3.f) {
            bDone=true;if(!bObserved&&C.RecordEvent)C.RecordEvent(TEXT("NativeHurtEventMissing"));
        }
    }
    void Cancel(FMCPerkPreviewContext&) override
    {
        if(auto* P=Presentation.Get()){P->StopStates(ID);TArray<UMCVFXPresentationComponent*> Channels;P->GetPresentationComponents(Channels);for(auto* S:Channels)if(S)S->StopPresentation(true);}
        for(auto Weak:NativeLabels)if(auto* Cue=Weak.Get())Cue->Destroy();NativeLabels.Reset();
        if(auto* Lab=Station.Get())Lab->Destroy();else if(auto* N=NativeNut.Get())N->Destroy();
        if(auto* T=Tongue.Get())T->Destroy();bDone=true;
    }
    void Reset(FMCPerkPreviewContext& C) override {Cancel(C);}
    bool IsComplete(const FMCPerkPreviewContext&)const override{return bDone;}
    FString GetPhaseName(const FMCPerkPreviewContext&)const override{return bDone?TEXT("Контакт завершён"):bObserved?TEXT("Настоящий подтверждённый удар"):TEXT("Штатный resolver урона");}
    float GetTailSeconds()const override{return .3f;}
private:
    static float Health(const AActor* A){if(const auto* H=Cast<AMCToothCharacter>(A))return H->Status?H->Status->State.Health:0;if(const auto* N=Cast<AMCNutEnemy>(A))return N->Health;if(const auto* B=Cast<AMCBossCharacter>(A))return B->Runtime.Health;return 0;}
    static float Shield(const AActor* A){const auto* B=Cast<AMCNutBoss>(A);return B?B->ShieldHealth:0;}
    FName ID;TWeakObjectPtr<AActor> Victim,Source;TWeakObjectPtr<UMCCombatFeedbackComponent> Feedback;TWeakObjectPtr<UMCWorldPresentationComponent> Presentation;
    TWeakObjectPtr<AMCTongue> Tongue;TWeakObjectPtr<AMCCombatFeedbackNut> NativeNut;TWeakObjectPtr<AMCVFXLabBossStation> Station;
    TArray<TWeakObjectPtr<AMCReactionVFX>> NativeLabels;
    int32 BeforeSerial=0;float BeforeHealth=0,BeforeShield=0,Age=0;bool bCommitted=false,bDone=false,bNativeAccepted=false,bObserved=false;
};
}
void MCHurtActionPreviewAdapters::Register()
{
    for(const FName ID:{FName(TEXT("Character.Hurt")),FName(TEXT("Boss.Generic.Hurt")),FName(TEXT("Enemy.Nut.Hurt")),FName(TEXT("Boss.NutKnight.Hurt")),FName(TEXT("Boss.NutMage.Hurt"))}) {
        const auto* Contract=MCVFXSourceContracts::Find(ID);if(!Contract)continue;
        FMCPerkPreviewAdapterEntry E;E.PerkID=ID;E.Label=Contract->Label;E.Stages=Contract->Events;E.Kind=EMCPerkPreviewAdapterKind::Context;E.bSupportsProfileOverride=true;
        E.Scenarios={EMCPerkPreviewScenario::Target};E.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(TEXT("Настоящий resolver → один принятый удар")));
        E.Create=[ID]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FHurtAdapter>(ID);};MCPerkPreviewAdapters::Register(MoveTemp(E));
    }
    // No current C++ producer/actual victim class is bound to Enemy.Generic.Hurt.
    // Keep it discoverable but never relabel a nut or synthesize ReportAcceptedHit.
}
