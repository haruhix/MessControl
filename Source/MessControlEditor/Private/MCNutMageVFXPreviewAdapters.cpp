#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewGameMode.h"
#include "MCVFXActionCatalog.h"
#include "MCVFXProfileBinding.h"
#include "MCVFXPresentationLibrary.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXLabBossStation.h"
#include "MCNutBoss.h"
#include "MCNutCombatEffect.h"
#include "MCNutRainProfile.h"
#include "MCNutSpellProjectile.h"
#include "MCToothCharacter.h"
#include "MCPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "MCToothStatusComponent.h"
#include "MCTongue.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraSystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// Observes native owner events; this adapter never directly plays a cosmetic event.
namespace MCNutMageVFXPreview
{
const FName SourceID(TEXT("Boss.NutMage.Fireball"));
const TArray<FName> Events={TEXT("Charge"),TEXT("Release"),TEXT("Flight"),TEXT("Impact")};

/** A real laboratory fireball cast, rather than a complete encounter/AI run.
 * The station's public friend API calls the normal boss BeginAttack(Fireball).
 * Boss/subject landing, authored animation and projectile sweep/damage remain
 * native. The old station visual-cue validator is
 * intentionally not used: custom presentation need not create legacy actors. */
class FFireballAdapter final:public IMCPerkPreviewAdapter
{
public:
    bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
    {
        if(!C.World || C.World->WorldType!=EWorldType::Game || C.World->GetOutermost()!=GetTransientPackage()
            || !C.World->GetAuthGameMode<AMCPerkPreviewGameMode>() || !IsValid(C.Hero)
            || C.Hero->GetWorld()!=C.World || !C.Hero->HasAuthority() || C.PerkID!=SourceID
            || (C.Scenario!=EMCPerkPreviewScenario::Target && C.Scenario!=EMCPerkPreviewScenario::NearWall && C.Scenario!=EMCPerkPreviewScenario::Timeout))
        {Error=TEXT("Огненный шар требует отдельного временного Game-мира и настоящего сценария мага.");return false;}
        auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
        auto* Profile=LoadObject<UMCNutRainProfile>(nullptr,TEXT("/Game/Gameplay/CoreLoop/DA_NutRain.DA_NutRain"));
        if(!Plane || !Profile || !C.AddBox){Error=TEXT("Не найдены сохранённая DA_NutRain и поверхность полигона.");return false;}
        const FTransform FloorPose(FRotator::ZeroRotator,FVector(0,0,-.1f),FVector(35,35,1));
        auto* Surface=C.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FloorPose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Surface){Error=TEXT("Не удалось создать поверхность настоящих запросов.");return false;}
        Surface->SetFlags(RF_Transient);Surface->SourceMesh=Plane;Surface->bAutomaticYawns=false;
        Surface->FinishSpawning(FloorPose);Surface->SetActorTickEnabled(false);
        Surface->Surface->SetVisibility(false);Surface->Surface->SetCollisionEnabled(ECollisionEnabled::QueryOnly);C.Targets.Add(Surface);
        const FTransform Pose(FRotator(0,180,0),C.Hero->GetActorLocation()+FVector(650,0,-C.Hero->GetActorLocation().Z));
        auto* Lab=C.World->SpawnActorDeferred<AMCVFXLabBossStation>(AMCVFXLabBossStation::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Lab){Error=TEXT("Не удалось создать native lab station.");return false;}
        Station=Lab;Lab->SetFlags(RF_Transient);Lab->Floor=Surface;Lab->bAutoRun=false;Lab->bComparisonMode=true;Lab->Kind=EMCVFXLabBossKind::MageFireball;
        Lab->FinishSpawning(Pose);Lab->Label->SetHiddenInGame(true);
        if(!Lab->PrepareComparisonUsingSubject(EMCVFXLabBossKind::MageFireball,C.Hero)) {Error=Lab->Status;return false;}
        Lab->SetActorTickEnabled(false); // native boss/projectile ticks remain enabled
        auto* Target=Cast<AMCNutBoss>(Lab->GetComparisonEnemy());
        auto* Tooth=Lab->GetComparisonSubject();
        if(!Target || Tooth!=C.Hero || !Tooth->Status || Tooth->GetController()!=C.Controller
            || Tooth->GetPlayerState()!=C.PlayerState || !Target->MageModel || !Target->MageModel->GetSkeletalMeshAsset()
            || !Target->FireballPresentation || Target->BossRole!=EMCNutBossRole::Mage)
        {Error=TEXT("Настоящий маг, сохранённая модель или зубик-мишень не готовы; подмена примитивом запрещена.");return false;}
        Boss=Target;Subject=Tooth;Presentation=Target->FireballPresentation;
        C.Targets.Add(Target);C.Targets.Add(Tooth);
        // The lab BORROWS this same native Hero, Controller and PlayerState.
        // It never creates, hides, re-possesses or destroys a second pawn.
        if(C.Scenario==EMCPerkPreviewScenario::NearWall) {
            const FVector Wall=(Target->GetActorLocation()+Tooth->GetActorLocation())*.5f;
            // AddBox takes unit-cube scale, not centimeter half-extents.
            WallTarget=C.AddBox(TEXT("MageFireballRealWall"),FVector(Wall.X,Wall.Y,145),FVector(.36f,4.2f,2.9f));
            if(!WallTarget.IsValid()){Error=TEXT("Не удалось создать реальную стену.");return false;}C.Targets.Add(WallTarget);
        }
        if(!PrepareLegacyResources(C,Error))return false;
        bCommitted=bLaunched=bFailed=bImpactObserved=bDamageVerified=false;Age=0;ShotCount=0;
        Observed.Reset();if(C.ReportStatus)C.ReportStatus(TEXT("Настоящий огненный шар: landing → native cast → столкновение/промах в пол → recovery. Это изолированный lab-сценарий, не полный бой."));
        return true;
    }
    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error) override
    {
        auto* B=Boss.Get();auto* P=Presentation.Get();
        if(!B || !P){Error=TEXT("Компонент настоящего мага отсутствует.");return false;}
        Record=C.RecordEvent;Scenario=C.Scenario;
        BossReceipt=P->OnEventAccepted.AddLambda([this](FName Event,EMCVFXPresentationResult Result){Accept(Event,Result);});
        // This callback must run after immutable resource handoff and BEFORE
        // FinishSpawning/BeginPlay, so Flight cannot be missed by the observer.
        SpawnReceipt=B->OnFireballSpawned.AddLambda([this](AMCNutSpellProjectile* Shot){BindShot(Shot);});
        return C.PresentationOverride?P->PrepareProfileOverride(C.PresentationOverride,Error):P->Prepare(Error);
    }
    void GetPreparedVisualSystems(const FMCPerkPreviewContext&,TArray<UNiagaraSystem*>& Out)const override
    {
        if(const auto* P=Presentation.Get())P->GetPreparedResources(Out);
        for(const auto& NS:LegacySystems)if(NS.IsValid())Out.AddUnique(NS.Get());
    }
    void OnCommitted(FMCPerkPreviewContext& C)override
    {bCommitted=true;Age=0;if(C.RecordEvent)C.RecordEvent(TEXT("NativeMageScenarioCommitted"));}
    void Tick(FMCPerkPreviewContext& C,float Dt)override
    {
        if(!bCommitted)return;Age+=FMath::Max(0.f,Dt);
        auto* Lab=Station.Get();auto* B=Boss.Get();
        if(!Lab || !B || !Subject.IsValid()){Fail(TEXT("NativeMageFixtureLost"));return;}
        if(!bLaunched && !bFailed && Lab->IsComparisonReady()) {
            if(C.RequestFramingUpdate)C.RequestFramingUpdate();
            if(!Lab->RunComparisonAction()){Fail(TEXT("NativeMageCastRejected"));return;}
            bLaunched=true;Lab->SetActorTickEnabled(false);
            if(Record)Record(TEXT("NativeMageCastAccepted"));
        }
        if(auto* Shot=Projectile.Get();Shot && Shot->Impact.bImpacted) {
            bImpactObserved=true;
            if(!bDamageVerified) {
                bDamageVerified=true;const bool HitHero=Shot->DamageApplications==1;
                if(Scenario==EMCPerkPreviewScenario::Target) {
                    if(!HitHero)Fail(TEXT("NativeMageTargetDamageMissing"));
                    else if(Record)Record(TEXT("NativeMageTargetDamageAccepted"));
                } else if(Shot->DamageApplications!=0)Fail(TEXT("NativeMageUnexpectedDamage"));
                else if(Scenario==EMCPerkPreviewScenario::NearWall) {
                    const auto* Wall=WallTarget.Get();const FBox WallBounds=Wall?Wall->GetComponentsBoundingBox(true):FBox(ForceInit);
                    const bool NearActualWall=WallBounds.IsValid && WallBounds.ExpandBy(Shot->Flight.Radius+4.f).IsInsideOrOn(Shot->Impact.Point)
                        && FVector::Dist(Shot->Impact.Point,Shot->Flight.Target)>Shot->Flight.Radius*2.f;
                    if(!NearActualWall)Fail(TEXT("NativeMageWallContactNotConfirmed"));else if(Record)Record(TEXT("NativeMageWallStoppedShot"));
                } else {
                    // The locked target is a tooth's center, below one sphere
                    // radius above the floor. A real dodge therefore lets the
                    // unchanged native sweep hit the ground before that point;
                    // it must not be reclassified as an airborne expiry.
                    FHitResult Ground;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCMagePreviewGroundMiss),false,Shot);
                    Query.AddIgnoredActor(Subject.Get());Query.AddIgnoredActor(Boss.Get());
                    const bool GroundFound=C.World->LineTraceSingleByObjectType(Ground,Shot->Impact.Point+FVector(0,0,20),
                        Shot->Impact.Point-FVector(0,0,20),FCollisionObjectQueryParams(ECC_WorldStatic),Query);
                    const auto* Tooth=Subject.Get();
                    const float ToothRadius=Tooth && Tooth->GetCapsuleComponent()?Tooth->GetCapsuleComponent()->GetScaledCapsuleRadius():0.f;
                    const bool SubjectClear=Tooth && FVector::Dist2D(Tooth->GetActorLocation(),Shot->Impact.Point)>Shot->Flight.Radius+ToothRadius+15.f;
                    const bool ActualGroundMiss=GroundFound && Ground.bBlockingHit && Ground.ImpactNormal.Z>.9f
                        && Shot->Impact.Normal.Z>.9f && Ground.ImpactPoint.Equals(Shot->Impact.Point,2.f) && SubjectClear;
                    if(!ActualGroundMiss)Fail(TEXT("NativeMageGroundMissNotConfirmed"));
                    else if(Record)Record(TEXT("NativeMageGroundMissNoDamage"));
                }
            }
        }
        if(Age>14.f && !IsComplete(C))Fail(TEXT("NativeMageScenarioTimeout"));
    }
    bool IsComplete(const FMCPerkPreviewContext&)const override
    {
        if(!bCommitted)return false;
        if(bFailed)return true;
        const auto* B=Boss.Get();
        return bLaunched && bImpactObserved && bDamageVerified && ShotCount==1 && B
            && B->State==EMCNutBossState::Idle && Observed.Contains(TEXT("Charge")) && Observed.Contains(TEXT("Release"))
            && Observed.Contains(TEXT("Flight")) && Observed.Contains(TEXT("Impact"));
    }
    FString GetPhaseName(const FMCPerkPreviewContext&)const override
    {
        if(bFailed)return TEXT("Огненный шар · сценарий не подтверждён");
        if(!bCommitted)return TEXT("Подготовка native мага и ресурсов");
        if(!bLaunched)return TEXT("Настоящее приземление мага перед изолированным cast");
        if(bImpactObserved)return TEXT("Native Impact · authored recovery / хвост VFX");
        return Projectile.IsValid()?TEXT("Native Flight · реальная проверка столкновения"):TEXT("Native Charge/Release · authored cast");
    }
    float GetTailSeconds()const override{return 2.f;}
    void Cancel(FMCPerkPreviewContext&)override
    {
        if(auto* P=Presentation.Get())P->StopPresentation(true);
        for(auto& Bound:BoundShots)if(auto* Shot=Bound.Shot.Get()){if(Shot->FireballPresentation)Shot->FireballPresentation->StopPresentation(true);Shot->Cancel();}
    }
    void Reset(FMCPerkPreviewContext&)override
    {
        if(auto* B=Boss.Get();B && SpawnReceipt.IsValid())B->OnFireballSpawned.Remove(SpawnReceipt);
        if(auto* P=Presentation.Get();P && BossReceipt.IsValid())P->OnEventAccepted.Remove(BossReceipt);
        for(auto& Bound:BoundShots)if(auto* Shot=Bound.Shot.Get();Shot && Shot->FireballPresentation && Bound.Receipt.IsValid())Shot->FireballPresentation->OnEventAccepted.Remove(Bound.Receipt);
        // The runner, not the station, owns Hero/Controller/PlayerState and
        // destroys its own complete sandbox after delegate removal.
        BossReceipt.Reset();SpawnReceipt.Reset();BoundShots.Reset();Projectile.Reset();Presentation.Reset();Boss.Reset();Subject.Reset();Station.Reset();
        WallTarget.Reset();LegacySystems.Reset();Observed.Reset();Record=nullptr;bCommitted=bLaunched=bFailed=bImpactObserved=bDamageVerified=false;Age=0;ShotCount=0;
    }
private:
    struct FBoundShot{TWeakObjectPtr<AMCNutSpellProjectile> Shot;FDelegateHandle Receipt;};
    TWeakObjectPtr<AMCVFXLabBossStation> Station;
    TWeakObjectPtr<AMCNutBoss> Boss;
    TWeakObjectPtr<AMCToothCharacter> Subject;
    TWeakObjectPtr<UMCVFXPresentationComponent> Presentation;
    TWeakObjectPtr<AMCNutSpellProjectile> Projectile;
    TWeakObjectPtr<AStaticMeshActor> WallTarget;
    TArray<FBoundShot> BoundShots;
    TArray<TStrongObjectPtr<UNiagaraSystem>> LegacySystems;
    FDelegateHandle BossReceipt,SpawnReceipt;
    TSet<FName> Observed;
    TFunction<void(FName)> Record;
    EMCPerkPreviewScenario Scenario=EMCPerkPreviewScenario::Target;
    float Age=0;int32 ShotCount=0;
    bool bCommitted=false,bLaunched=false,bFailed=false,bImpactObserved=false,bDamageVerified=false;
    void Fail(FName Event){if(!bFailed && Record)Record(Event);bFailed=true;}
    void Accept(FName Event,EMCVFXPresentationResult Result)
    {
        if(!bCommitted)return;
        if(!Events.Contains(Event) || Result==EMCVFXPresentationResult::Rejected){Fail(TEXT("NativeMagePresentationRejected"));return;}
        Observed.Add(Event);if(Record)Record(Event);
    }
    void BindShot(AMCNutSpellProjectile* Shot)
    {
        if(!bCommitted || !IsValid(Shot) || !Shot->FireballPresentation || Shot->SourceActor!=Boss.Get()) {Fail(TEXT("NativeMageShotInvalid"));return;}
        ++ShotCount;Projectile=Shot;
        FBoundShot Bound;Bound.Shot=Shot;
        Bound.Receipt=Shot->FireballPresentation->OnEventAccepted.AddLambda([this](FName Event,EMCVFXPresentationResult Result){Accept(Event,Result);});
        BoundShots.Add(Bound);if(Record)Record(TEXT("NativeMageProjectileSpawned"));
        if(Scenario==EMCPerkPreviewScenario::NearWall)if(auto* Wall=WallTarget.Get()) {
            // The real authored palm/muzzle may extend far from the boss root.
            // Position the already-prepared physical wall between the ACTUAL
            // locked flight endpoints, before the native first sweep.
            const FVector Mid=(Shot->Flight.Start+Shot->Flight.Target)*.5f;
            Wall->SetActorLocation(FVector(Mid.X,Mid.Y,145.f),false);
        }
        if(Scenario==EMCPerkPreviewScenario::Timeout)if(auto* Tooth=Subject.Get()) {
            // Aim has actually been locked by the native cast. The unchanged
            // projectile now misses this tooth and may hit the physical floor;
            // neither its target nor the arena collision is altered.
            Tooth->SetActorLocation(Tooth->GetActorLocation()+FVector(0,550,0),false);
            if(Record)Record(TEXT("NativeMageTargetSidesteppedAfterRelease"));
        }
    }
    bool PrepareLegacyResources(FMCPerkPreviewContext& C,FString& Error)
    {
        const auto* Cue=GetDefault<AMCNutCombatEffect>();const auto* Shot=GetDefault<AMCNutSpellProjectile>();
        TArray<TSoftObjectPtr<UNiagaraSystem>> NS={Cue->CastChargeSystem,Cue->ArcaneBurstSystem,Cue->FireImpactSystem,Shot->TrailSystem};
        for(const auto& Ref:NS){auto* System=Ref.LoadSynchronous();if(!System){Error=TEXT("Native Fireball Niagara-ресурс не подготовлен.");return false;}LegacySystems.Emplace(System);}
        TArray<TSoftObjectPtr<UMaterialInterface>> Materials={Cue->FlameMaterial,Cue->MagicMaterial,Shot->CoreMaterial,Shot->WakeMaterial,Shot->FlameMaterial};
        for(const auto& Ref:Materials) {
            auto* Material=Ref.LoadSynchronous();if(!Material){Error=TEXT("Native Fireball материал отсутствует.");return false;}
            auto* Anchor=C.AddBox(TEXT("MageFireballResourceAnchor"),FVector(0,0,-600),FVector(1));
            if(!Anchor){Error=TEXT("Не удалось удержать native материал до readiness barrier.");return false;}
            Anchor->SetActorHiddenInGame(true);Anchor->SetActorEnableCollision(false);Anchor->GetStaticMeshComponent()->SetMaterial(0,Material);C.Targets.Add(Anchor);
        }
        return true;
    }
};

const bool bRegistered=[]
{
    FMCPerkPreviewAdapterEntry Entry;Entry.PerkID=SourceID;Entry.SourceRowID=NAME_None;Entry.Kind=EMCPerkPreviewAdapterKind::Context;
    Entry.Label=FText::FromString(TEXT("Ореховый маг · огненный шар"));Entry.bSupportsProfileOverride=true;Entry.Stages=Events;
    Entry.DescribeNativeSources=MCVFXNativeSources::DescribeMageFireball;
    Entry.StageLabels.Add(TEXT("Charge"),FText::FromString(TEXT("Накопление в ладонях")));
    Entry.StageLabels.Add(TEXT("Release"),FText::FromString(TEXT("Настоящий выпуск шара")));
    Entry.StageLabels.Add(TEXT("Flight"),FText::FromString(TEXT("Полет и шлейф")));
    Entry.StageLabels.Add(TEXT("Impact"),FText::FromString(TEXT("Настоящее столкновение")));
    Entry.Scenarios={EMCPerkPreviewScenario::Target,EMCPerkPreviewScenario::NearWall,EMCPerkPreviewScenario::Timeout};
    Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(TEXT("Настоящее попадание в зубика")));
    Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::NearWall,FText::FromString(TEXT("Настоящая стена перехватывает шар")));
    Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Timeout,FText::FromString(TEXT("Уклонение после выпуска · попадание в пол")));
    Entry.Create=[]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FFireballAdapter>();};MCPerkPreviewAdapters::Register(MoveTemp(Entry));
    MCVFXProfileBindings::RegisterLibrarySource(SourceID,UMCVFXPresentationLibrary::MouthLibraryPath(),SourceID,Events);
    FMCVFXActionCatalogueEntry Source;Source.ID=SourceID;Source.Label=FText::FromString(TEXT("Ореховый маг · огненный шар"));Source.Category=TEXT("Boss");
    Source.SourceOwner=TEXT("AMCVFXLabBossStation::PrepareComparisonUsingSubject/RunComparisonAction → AMCNutBoss::BeginAttack/ExecuteAttack → AMCNutSpellProjectile");
    Source.Description=FText::FromString(TEXT("Настоящий лабораторный cast и projectile sweep с сохранённым магом. Charge/Release принадлежат боссу, Flight/Impact — его реальному снаряду. Это не полный бой и не подменённая VFX-анимация."));
    Source.Stages=Events;MCVFXActionCatalog::RegisterSource(MoveTemp(Source));return true;
}();
}
