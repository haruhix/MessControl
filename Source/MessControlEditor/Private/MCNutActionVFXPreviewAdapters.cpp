#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewGameMode.h"
#include "MCVFXActionCatalog.h"
#include "MCVFXProfileBinding.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXPresentationLibrary.h"
#include "MCVFXLabBossStation.h"
#include "MCNutVFXCatalog.h"
#include "MCFoodActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "MCNutBoss.h"
#include "MCNutKamikaze.h"
#include "MCNutCombatEffect.h"
#include "MCNutRainProfile.h"
#include "MCNutRainEvent.h"
#include "MCNutLandingShadow.h"
#include "Materials/MaterialInterface.h"
#include "MCToothCharacter.h"
#include "MCTongue.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NiagaraSystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace MCNutActionPreview
{
bool LabKind(const FMCNutVFXAction& Action,EMCVFXLabBossKind& Out)
{
    const auto* Enum=StaticEnum<EMCVFXLabBossKind>();
    const int64 Value=Enum?Enum->GetValueByName(Action.LabScenario):INDEX_NONE;
    if(Value==INDEX_NONE)return false;
    Out=EMCVFXLabBossKind(Value);return true;
}

/** One real native laboratory action, sharing the runtime source contract. No
 * synthetic animation or manually scheduled VFX event is used here. */
class FAdapter final:public IMCPerkPreviewAdapter
{
public:
    explicit FAdapter(FName InSource):Source(InSource){}
    bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
    {
        const auto* Contract=MCNutVFX::Find(Source);EMCVFXLabBossKind Kind;
        if(!Contract || !LabKind(*Contract,Kind) || C.PerkID!=Source || !C.World
            || C.World->WorldType!=EWorldType::Game || C.World->GetOutermost()!=GetTransientPackage()
            || !C.World->GetAuthGameMode<AMCPerkPreviewGameMode>() || !IsValid(C.Hero)
            || C.Hero->GetWorld()!=C.World || !C.Hero->HasAuthority())
        {Error=TEXT("Предпросмотр требует временный игровой мир и настоящий сценарий орехового босса.");return false;}
        auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
        if(!Plane){Error=TEXT("Не найдена поверхность изолированного полигона.");return false;}
        // Every native action samples the real tongue, including contact rings,
        // physical rain landings and teleport footprint/clearance queries.
        // Keep its private CPU-readable geometry and physical support consistent.
        const bool bPhysicalEntry=true;
        if(bPhysicalEntry) {
            // Only this disposable fixture owns the CPU-readable copy. Never
            // change the shared Engine Plane or its global Allow CPU Access.
            Plane=DuplicateObject<UStaticMesh>(Plane,GetTransientPackage());
            if(!Plane){Error=TEXT("Не удалось подготовить частный меш поверхности падения.");return false;}
            Plane->ClearFlags(RF_Public|RF_Standalone);Plane->SetFlags(RF_Transient);Plane->bAllowCPUAccess=true;
        }
        // The runner's stage box is also physical. Native teleport clearance
        // uses BodyRadius+10 around a point BodyRadius+4 above support, so lift
        // the real tongue clear of that unrelated box instead of weakening the
        // native overlap test or changing its target/danger geometry.
        constexpr float SurfaceHeight=32.f;
        const FTransform FloorPose(FRotator::ZeroRotator,FVector(0,0,SurfaceHeight),FVector(35,35,1));
        C.Hero->SetActorLocation(C.Hero->GetActorLocation()+FVector(0,0,SurfaceHeight),false,nullptr,ETeleportType::TeleportPhysics);
        auto* Surface=C.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FloorPose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Surface){Error=TEXT("Не удалось создать настоящую поверхность запросов.");return false;}
        C.Targets.Add(Surface);Surface->SetFlags(RF_Transient);Surface->SourceMesh=Plane;Surface->bAutomaticYawns=false;
        Surface->FinishSpawning(FloorPose);Surface->SetActorTickEnabled(false);
        Surface->Surface->SetVisibility(false);
        Surface->Surface->SetCollisionEnabled(bPhysicalEntry?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::QueryOnly);
        const bool Ranged=Kind==EMCVFXLabBossKind::TankCharge || Kind==EMCVFXLabBossKind::TankJump
            || Kind==EMCVFXLabBossKind::TankRoll || Kind==EMCVFXLabBossKind::MageSummon
            || Kind==EMCVFXLabBossKind::MageNutRain || Kind==EMCVFXLabBossKind::MageTeleport;
        FVector Point=C.Hero->GetActorLocation()+FVector(Ranged?650.f:270.f,0,0);Point.Z=0;
        const FTransform Pose(FRotator(0,180,0),Point);
        auto* Lab=C.World->SpawnActorDeferred<AMCVFXLabBossStation>(AMCVFXLabBossStation::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Lab){Error=TEXT("Не удалось создать владельца настоящего сценария.");return false;}
        C.Targets.Add(Lab);Station=Lab;Lab->SetFlags(RF_Transient);Lab->Floor=Surface;
        Lab->bAutoRun=false;Lab->bComparisonMode=true;Lab->Kind=Kind;Lab->FinishSpawning(Pose);Lab->Label->SetHiddenInGame(true);
        if(!Lab->PrepareComparisonUsingSubject(Kind,C.Hero)){Error=Lab->Status;return false;}
        Lab->SetActorTickEnabled(false);
        auto* NativeActor=Lab->GetComparisonEnemy();auto* NativeBoss=Cast<AMCNutBoss>(NativeActor);
        if(!IsValid(NativeActor) || Lab->GetComparisonSubject()!=C.Hero || !Lab->GetComparisonPresentation())
        {Error=TEXT("Настоящий владелец действия или исходный зубик не готовы.");return false;}
        Boss=NativeBoss;NativeOwner=NativeActor;C.Targets.Add(NativeActor);
        bKamikaze=Kind==EMCVFXLabBossKind::KamikazeFuse || Kind==EMCVFXLabBossKind::KamikazeBlast;
        bEntry=Kind==EMCVFXLabBossKind::RainEntry;
        bCounter=Kind==EMCVFXLabBossKind::TankCounterShove;
        bMeleeSequence=Kind==EMCVFXLabBossKind::TankMelee;MeleeStarts=0;
        bHeadSplit=Kind==EMCVFXLabBossKind::TankHeadSplit;
        bSummon=Source==TEXT("Boss.NutMage.Summon");bRain=Source==TEXT("Boss.NutMage.NutRain");
        if(bSummon || bRain) {
            auto* Profile=LoadObject<UMCNutRainProfile>(nullptr,TEXT("/Game/Gameplay/CoreLoop/DA_NutRain.DA_NutRain"));
            if(!NativeBoss->EncounterOwner || !NativeBoss->EncounterOwner->PrepareIsolatedSpellContext(Surface,Profile))
            {Error=TEXT("Не удалось подготовить настоящий владелец орехов для призыва/дождя.");return false;}
            if(bSummon && !NativeBoss->EncounterOwner->SpawnSpellNut(NativeBoss->GetActorLocation()+FVector(-180,220,0),137))
            {Error=TEXT("Настоящий орех для призыва не создан.");return false;}
        }C.PreviewFront=(C.Hero->GetActorLocation()-Point).GetSafeNormal2D();
        bEntrance=Kind==EMCVFXLabBossKind::TankEntrance || Kind==EMCVFXLabBossKind::MageEntrance;
        bDeath=Kind==EMCVFXLabBossKind::TankDeath || Kind==EMCVFXLabBossKind::MageDeath;
        // Retain exactly the original resources before the preview barrier.
        // These roots are local to this disposable scenario, never Hub roots.
        const auto* Defaults=GetDefault<AMCNutCombatEffect>();
        const TSoftObjectPtr<UNiagaraSystem> Systems[]={Defaults->DustImpactSystem,Defaults->MotionDustSystem,
            Defaults->ArcaneBurstSystem,Defaults->StormMotesSystem,Defaults->ShieldHitSystem,
            Defaults->FireImpactSystem,Defaults->CastChargeSystem};
        for(const auto& Ref:Systems)if(auto* NS=Ref.LoadSynchronous())LegacySystems.Emplace(NS);
        const TSoftObjectPtr<UMaterialInterface> Materials[]={Defaults->TelegraphMaterial,Defaults->ShadowMaterial,
            Defaults->FlameMaterial,Defaults->MagicMaterial,Defaults->ShieldFractureMaterial,Defaults->StormMaterial};
        for(int32 I=0;I<UE_ARRAY_COUNT(Materials);++I) {
            auto* Material=Materials[I].LoadSynchronous();
            auto* Anchor=C.AddBox?C.AddBox(FName(*FString::Printf(TEXT("NutOriginalResource_%d"),I)),FVector(0,0,-11000-I*10),FVector(.001f)):nullptr;
            if(!Material || !Anchor){Error=TEXT("Не удалось подготовить родные материалы сценария.");return false;}
            Anchor->SetActorHiddenInGame(true);Anchor->SetActorEnableCollision(false);
            Anchor->GetStaticMeshComponent()->SetMaterial(0,Material);C.Targets.Add(Anchor);
        }
        Age=0;bCommitted=bLaunched=bFailed=bTerminalObserved=false;Seen.Reset();return true;
    }
    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error) override
    {
        auto* Lab=Station.Get();const auto* Contract=MCNutVFX::Find(Source);
        auto* P=Lab?Lab->GetComparisonPresentation():nullptr;
        if(!P || !Contract){Error=TEXT("Компонент настоящего сценария отсутствует.");return false;}
        P->SourceID=Source;P->Events=Contract->Events;
        auto* Subject=Lab->GetComparisonVisualSubject();
        if(!P->BindSourceAudioSocket(Source,Subject,Error))return false;
        const bool Ready=C.PresentationOverride?P->PrepareProfileOverride(C.PresentationOverride,Error):MCNutVFX::Prepare(P,Source,Error,true);
        if(!Ready)return false;
        if(auto* K=Cast<AMCNutKamikaze>(NativeOwner.Get())) {
            // This is one real fuse-to-blast action. Its other source must be
            // ready too, even when the user edits only one half of it.
            const bool EditingFuse=Source==TEXT("Enemy.NutKamikaze.Fuse");
            const FName Other=EditingFuse?TEXT("Enemy.NutKamikaze.Blast"):TEXT("Enemy.NutKamikaze.Fuse");
            auto* Companion=EditingFuse?K->VFXContext.Get():K->FusePresentation.Get();
            if(!Companion || !Companion->BindSourceAudioSocket(Other,K->SummonModel,Error)
                || !MCNutVFX::Prepare(Companion,Other,Error,true))return false;
        }
        // Startup cues exist, but readiness is not certified yet. Do not copy
        // a new SFX/NS channel until OnCommitted crosses the shared barrier.
        StartupCues.Reset();StartupShadows.Reset();
        for(TActorIterator<AMCNutCombatEffect> It(C.World);It;++It)
            if(It->Cue.PresentationSource==Source && It->SourceActor==NativeOwner.Get() && It->Presentation)StartupCues.Add(*It);
        if(bEntry)for(TActorIterator<AMCNutLandingShadow> It(C.World);It;++It)
            if(It->PresentationSource==Source && It->TrackedFood==NativeOwner.Get() && It->Presentation)StartupShadows.Add(*It);
        return true;
    }
    void GetPreparedVisualSystems(const FMCPerkPreviewContext&,TArray<UNiagaraSystem*>& Out)const override
    {
        if(const auto* Lab=Station.Get())if(auto* P=Lab->GetComparisonPresentation())P->GetPreparedResources(Out);
        if(const auto* K=Cast<AMCNutKamikaze>(NativeOwner.Get())) {
            if(K->FusePresentation)K->FusePresentation->GetPreparedResources(Out);
            if(K->VFXContext)K->VFXContext->GetPreparedResources(Out);
        }
        for(const auto& NS:LegacySystems)if(NS.IsValid())Out.AddUnique(NS.Get());
    }
    void OnCommitted(FMCPerkPreviewContext& C)override
    {
        bCommitted=true;Age=0;auto* Lab=Station.Get();auto* P=Lab?Lab->GetComparisonPresentation():nullptr;
        for(const auto& Weak:StartupCues)if(auto* Cue=Weak.Get()) {
            FString Error;
            if(!P || !P->CopyPreparedSourceTo(Source,Cue->Presentation,Error)) {
                bFailed=true;if(C.RecordEvent)C.RecordEvent(TEXT("NutStartupReadinessHandoffRejected"));
                if(C.ReportStatus)C.ReportStatus(TEXT("Запуск остановлен до первого кадра: ")+Error);
                if(Lab)Lab->Destroy();return;
            }
        }
        for(const auto& Weak:StartupShadows)if(auto* Shadow=Weak.Get()) {
            FString Error;
            if(!P || !P->CopyPreparedSourceTo(Source,Shadow->Presentation,Error)) {
                bFailed=true;if(C.RecordEvent)C.RecordEvent(TEXT("NutStartupReadinessHandoffRejected"));
                if(C.ReportStatus)C.ReportStatus(TEXT("Падение остановлено до первого кадра: ")+Error);
                if(Lab)Lab->Destroy();return;
            }
        }
        StartupCues.Reset();StartupShadows.Reset();
    }
    void Tick(FMCPerkPreviewContext& C,float Dt)override
    {
        if(!bCommitted)return;Age+=FMath::Max(0.f,Dt);
        auto* Lab=Station.Get();auto* B=Boss.Get();
        if(!Lab || (!IsValid(NativeOwner.Get()) && !bDeath && !bTerminalObserved)){bFailed=true;return;}
        bool FoodReady=true;
        if(bSummon && B && B->EncounterOwner) {TArray<AMCFoodActor*> Food;B->EncounterOwner->GetAvailableRainNuts(Food);FoodReady=!Food.IsEmpty();}
        const bool MeleeReady=!bMeleeSequence || (B && B->State==EMCNutBossState::Idle
            && B->GetAttackCooldownRemaining(EMCNutBossAttack::Melee)<=0.f);
        const bool NextMelee=bMeleeSequence && bLaunched && MeleeStarts<3 && MeleeReady;
        if(!bFailed && FoodReady && MeleeReady && ((!bLaunched && (bEntrance || Lab->IsComparisonReady())) || NextMelee)) {
            const bool Accepted=bEntrance || Lab->RunComparisonAction();
            if(!Accepted){bFailed=true;if(C.RecordEvent)C.RecordEvent(TEXT("NutNativeActionRejected"));return;}
            bLaunched=true;
            if(bMeleeSequence) {
                // Observe BeginAttack's own sequence result. Presentation never
                // sets the variant, contact count, seed, cooldown or damage.
                if(!B || B->TankMeleeVariant!=MeleeStarts) {
                    bFailed=true;if(C.RecordEvent)C.RecordEvent(TEXT("NutNativeMeleeSequenceRejected"));return;
                }
                ++MeleeStarts;
            }
            Lab->SetActorTickEnabled(bCounter);if(C.RequestFramingUpdate)C.RequestFramingUpdate();
        }
        auto Mark=[&](FName E){if(!Seen.Contains(E)){Seen.Add(E);if(C.RecordEvent)C.RecordEvent(E);}};
        if(bKamikaze)if(auto* K=Cast<AMCNutKamikaze>(NativeOwner.Get())) {
            // Observe the real native state/footprint; do not reroll gameplay
            // or manufacture Fuse/Danger from a parallel timing formula.
            if(Source==TEXT("Enemy.NutKamikaze.Fuse")) {
                if(K->bArmed)Mark(TEXT("Fuse"));
                if(K->BlastWarning && K->BlastWarning->IsVisible())Mark(TEXT("Danger"));
            }
            bTerminalObserved|=K->bExploded;
        }
        for(TActorIterator<AMCNutCombatEffect> It(C.World);It;++It)if(It->Cue.PresentationSource==Source)
        {
            const float CueAge=float(C.World->GetTimeSeconds()-It->Cue.StartedAt);
            const FName Event=It->Cue.AfterWindupEvent.IsNone() || CueAge<It->Cue.WindupSeconds?It->Cue.PresentationEvent:It->Cue.AfterWindupEvent;
            if(!Event.IsNone() && !Seen.Contains(Event)){Seen.Add(Event);if(C.RecordEvent)C.RecordEvent(Event);}
            TArray<FName> Bursts;It->GetAcceptedBurstEvents(Bursts);
            for(FName Burst:Bursts)if(!Seen.Contains(Burst)){Seen.Add(Burst);if(C.RecordEvent)C.RecordEvent(Burst);}
        }
        if(bRain || bEntry)for(TActorIterator<AMCNutLandingShadow> It(C.World);It;++It)if(It->PresentationSource==Source) {
            if(It->IsWarningActive()){Mark(TEXT("Approach"));if(bRain && IsValid(It->TrackedFood.Get()))Mark(TEXT("DropTrail"));}
            if(It->bImpacted){Mark(TEXT("Impact"));bTerminalObserved|=bEntry;}
        }
        if(Age>(bMeleeSequence?45.f:20.f)){bFailed=true;if(C.RecordEvent)C.RecordEvent(TEXT("NutNativeActionTimeout"));}
    }
    bool IsComplete(const FMCPerkPreviewContext&)const override
    {
        if(!bCommitted || !bLaunched)return bFailed;
        if(bFailed)return true;
        const auto* B=Boss.Get();
        if(bMeleeSequence)return MeleeStarts==3 && B && B->State==EMCNutBossState::Idle
            && Seen.Contains(TEXT("Tell")) && Seen.Contains(TEXT("Slash")) && Seen.Contains(TEXT("Slam"));
        if(bKamikaze)return bTerminalObserved && !Seen.IsEmpty()
            && (Source!=TEXT("Enemy.NutKamikaze.Fuse") || Seen.Contains(TEXT("Danger")))
            && (Source!=TEXT("Enemy.NutKamikaze.Blast") || Seen.Contains(TEXT("Impact")));
        if(bEntry)return bTerminalObserved && Seen.Contains(TEXT("Approach")) && Seen.Contains(TEXT("Impact"));
        if(bHeadSplit)return B && B->BossRole==EMCNutBossRole::Mage && B->State==EMCNutBossState::Idle
            && Seen.Contains(TEXT("HeadSplit")) && Seen.Contains(TEXT("HeadDust"));
        if(bCounter)return B && B->State==EMCNutBossState::Idle && Seen.Contains(TEXT("Tell")) && Seen.Contains(TEXT("Impact"));
        return Age>1.f && !Seen.IsEmpty() && (!bRain || Seen.Contains(TEXT("Impact")))
            && (!bSummon || Seen.Contains(TEXT("Release"))) && (!B || B->State==EMCNutBossState::Defeated
            || (B->State==EMCNutBossState::Idle && Age>2.f));
    }
    FString GetPhaseName(const FMCPerkPreviewContext&)const override
    {return bFailed?TEXT("Сценарий не подтверждён · проверьте диагностику"):!bCommitted?TEXT("Подготовка настоящего владельца действия"):!bLaunched?TEXT("Подготовка реального контакта/приземления"):bCounter?TEXT("Настоящие контакты → накопление давления → контрудар"):bKamikaze?TEXT("Настоящий фитиль, преследование и взрыв"):bEntry?TEXT("Настоящий падающий орех и столкновение"):TEXT("Реальное действие босса · исходные события и анимация");}
    float GetTailSeconds()const override{return 2.f;}
    void Cancel(FMCPerkPreviewContext&)override {if(auto* Lab=Station.Get())Lab->Destroy();}
    void Reset(FMCPerkPreviewContext& C)override {Cancel(C);Station.Reset();Boss.Reset();NativeOwner.Reset();LegacySystems.Reset();StartupCues.Reset();StartupShadows.Reset();Seen.Reset();Age=0;bCommitted=bLaunched=false;}
private:
    FName Source;TWeakObjectPtr<AMCVFXLabBossStation> Station;TWeakObjectPtr<AMCNutBoss> Boss;
    TWeakObjectPtr<AActor> NativeOwner;
    TArray<TStrongObjectPtr<UNiagaraSystem>> LegacySystems;TArray<TWeakObjectPtr<AMCNutCombatEffect>> StartupCues;TSet<FName> Seen;
    TArray<TWeakObjectPtr<AMCNutLandingShadow>> StartupShadows;
    int32 MeleeStarts=0;bool bMeleeSequence=false;
    float Age=0;bool bCommitted=false,bLaunched=false,bFailed=false,bEntrance=false,bDeath=false,bSummon=false,bRain=false;
    bool bKamikaze=false,bEntry=false,bCounter=false,bHeadSplit=false,bTerminalObserved=false;
};

const bool Registered=[]
{
    for(const auto& Contract:MCNutVFX::Actions())
    {
        if(Contract.ID==TEXT("Boss.NutKnight.Shield") || Contract.ID==TEXT("Boss.NutMage.Fireball"))continue;
        MCVFXProfileBindings::RegisterLibrarySource(Contract.ID,UMCVFXPresentationLibrary::MouthLibraryPath(),Contract.ID,Contract.Events);
        EMCVFXLabBossKind Kind;const bool HasLab=LabKind(Contract,Kind);
        FMCVFXActionCatalogueEntry Row;Row.ID=Contract.ID;Row.Label=Contract.Label;Row.Stages=Contract.Events;
        Row.Category=Contract.ID.ToString().StartsWith(TEXT("Boss."))?TEXT("Boss"):TEXT("World");
        Row.SourceOwner=TEXT("MCNutVFXCatalog / actual AMCNutBoss, AMCNutKamikaze or AMCNutLandingShadow owner");
        Row.Description=FText::FromString(TEXT("Настоящие игровые события. Контакт, хитбоксы и опасная область принадлежат механике; VFX и SFX редактируются отдельно. Миграция исходного арта проверяется отдельно от наличия привязки."));
        if(!HasLab)Row.UnsupportedReason=TEXT("В каталоге не указан зарегистрированный настоящий сценарий полигона.");
        MCVFXActionCatalog::RegisterSource(MoveTemp(Row));
        FMCPerkPreviewAdapterEntry Entry;Entry.PerkID=Contract.ID;Entry.SourceRowID=NAME_None;Entry.Label=Contract.Label;
        Entry.Kind=EMCPerkPreviewAdapterKind::Context;Entry.Stages=Contract.Events;Entry.bSupportsProfileOverride=true;
        // Read only actual native CDO soft references. Static registration does
        // not load/create effects or alter a canonical/user runtime profile.
        const auto* Defaults=GetDefault<AMCNutCombatEffect>();
        auto Default=[&](FName Event,const TSoftObjectPtr<UNiagaraSystem>& System,const TCHAR* Label) {
            Entry.DefaultSystems.Add(Event,System);Entry.StageLabels.Add(Event,FText::FromString(Label));
        };
        if(Contract.ID==TEXT("Boss.NutKnight.Death"))Default(TEXT("BodyBurst"),Defaults->ShieldHitSystem,TEXT("Всплеск у тела"));
        else if(Contract.ID==TEXT("Boss.NutMage.Death"))Default(TEXT("BodyBurst"),Defaults->ArcaneBurstSystem,TEXT("Всплеск у тела"));
        else if(Contract.ID==TEXT("Boss.NutKnight.Roll"))Default(TEXT("TransformBurst"),Defaults->ShieldHitSystem,TEXT("Вспышка превращения"));
        else if(Contract.ID==TEXT("Boss.NutMage.Summon")||Contract.ID==TEXT("Boss.NutMage.NutRain"))
            Default(TEXT("ReleaseBurst"),Defaults->ArcaneBurstSystem,TEXT("Выпуск частиц ритуала"));
        else if(Contract.ID==TEXT("Boss.NutMage.Teleport"))Default(TEXT("ArrivalBurst"),Defaults->ArcaneBurstSystem,TEXT("Всплеск в точке прибытия"));
        else if(Contract.ID==TEXT("Boss.NutKnight.Transform"))Default(TEXT("HeadDust"),Defaults->DustImpactSystem,TEXT("Пыль раскола оболочки"));
        if(HasLab) {
            Entry.Scenarios={EMCPerkPreviewScenario::Target};
            const TCHAR* ScenarioLabel=Kind==EMCVFXLabBossKind::TankMelee?TEXT("Три родных атаки: два взмаха → комбо с ударом в землю"):
                Kind==EMCVFXLabBossKind::TankCounterShove?TEXT("Контакты игрока → настоящий контрудар"):
                Kind==EMCVFXLabBossKind::TankHeadSplit?TEXT("Поражение рыцаря → раскол → настоящий маг"):
                Kind==EMCVFXLabBossKind::KamikazeFuse || Kind==EMCVFXLabBossKind::KamikazeBlast?TEXT("Настоящий фитиль → преследование → взрыв"):
                Kind==EMCVFXLabBossKind::RainEntry?TEXT("Настоящее падение ореха → столкновение"):TEXT("Настоящее действие босса");
            Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(ScenarioLabel));
            const FName ID=Contract.ID;Entry.Create=[ID]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FAdapter>(ID);};
        }
        // No factory means honestly metadata-only; the shared registry must not
        // turn this entry into a successful contextual-preview capability.
        MCPerkPreviewAdapters::Register(MoveTemp(Entry));
    }
    return true;
}();
}
