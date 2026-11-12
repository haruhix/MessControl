#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewGameMode.h"
#include "MCVFXActionCatalog.h"
#include "MCVFXProfileBinding.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXPresentationLibrary.h"
#include "MCVFXLabBossStation.h"
#include "MCNutVFXCatalog.h"
#include "MCNutWizardVFXDefaults.h"
#include "MCFoodActor.h"
#include "MCInventoryComponent.h"
#include "Algo/AllOf.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "MCNutBoss.h"
#include "MCNutKamikaze.h"
#include "MCNutCombatEffect.h"
#include "MCNutRainProfile.h"
#include "MCNutRainEvent.h"
#include "MCNutLandingShadow.h"
#include "Materials/MaterialInterface.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCTongue.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "NiagaraSystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace MCNutActionPreview
{
const FName KnightMelee(TEXT("Boss.NutKnight.Melee"));

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
            || Kind==EMCVFXLabBossKind::MageNutRain || Kind==EMCVFXLabBossKind::MageGravityWaves
            || Kind==EMCVFXLabBossKind::MageNutSphere
            || Kind==EMCVFXLabBossKind::MageTeleport || Kind==EMCVFXLabBossKind::TankChain;
        const float Spacing=Kind==EMCVFXLabBossKind::TankChain
            ?(C.Scenario==EMCPerkPreviewScenario::Timeout?1300.f:950.f):Ranged?650.f:Kind==EMCVFXLabBossKind::MageFlash?235.f:270.f;
        FVector Point=C.Hero->GetActorLocation()+FVector(Spacing,0,0);Point.Z=0;
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
        bGravity=Kind==EMCVFXLabBossKind::MageGravityWaves;
        bSphere=Kind==EMCVFXLabBossKind::MageNutSphere;
        bFlash=Kind==EMCVFXLabBossKind::MageFlash;bFlashToolSelected=false;
        GravityLiftedNuts.Reset();GravityImpactedNuts.Reset();
        bChain=Kind==EMCVFXLabBossKind::TankChain;
        bChainBait=bChain && C.Scenario==EMCPerkPreviewScenario::Acquire;
        bChainDodge=bChain && C.Scenario==EMCPerkPreviewScenario::Timeout;
        bChainDodgeLaunched=false;
        if(bSummon || bRain || bGravity || bSphere || bChainBait) {
            auto* Profile=LoadObject<UMCNutRainProfile>(nullptr,TEXT("/Game/Gameplay/CoreLoop/DA_NutRain.DA_NutRain"));
            if(!NativeBoss->EncounterOwner || !NativeBoss->EncounterOwner->PrepareIsolatedSpellContext(Surface,Profile))
            {Error=TEXT("Не удалось подготовить настоящего владельца орехов.");return false;}
            if(bSummon && !NativeBoss->EncounterOwner->SpawnSpellNut(NativeBoss->GetActorLocation()+FVector(-180,220,0),137))
            {Error=TEXT("Настоящий орех для призыва не создан.");return false;}
            if(bChainBait) {
                const FVector Candidate=FMath::Lerp(NativeBoss->GetChainOrigin(),C.Hero->GetActorLocation(),.72);
                if(!NativeBoss->EncounterOwner->SpawnIsolatedEntryNut(Candidate,137))
                {Error=TEXT("Не удалось подготовить целый орех на пути цепи.");return false;}
                auto* Nut=NativeBoss->EncounterOwner->Nuts[0].Get();
                C.Targets.Add(Nut);
                int32 FragmentIndex=0;
                for(const auto& Ref:Nut->FoodData.FragmentMeshes) if(!Ref.IsNull()) {
                    auto* Mesh=Ref.LoadSynchronous();
                    auto* Anchor=C.AddBox?C.AddBox(FName(*FString::Printf(TEXT("KnightChainNutFragment_%d"),FragmentIndex)),
                        FVector(0,0,-11200-FragmentIndex*10),FVector(.001f)):nullptr;
                    if(!Mesh || !Anchor){Error=TEXT("Не удалось подготовить меши осколков целого ореха.");return false;}
                    Anchor->SetActorHiddenInGame(true);Anchor->SetActorEnableCollision(false);
                    Anchor->GetStaticMeshComponent()->SetStaticMesh(Mesh);C.Targets.Add(Anchor);
                    ++FragmentIndex;
                }
                if(FragmentIndex==0){Error=TEXT("В сохранённом орехе отсутствуют меши осколков.");return false;}
            }
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
        bool Ready=false;
        if(!C.PresentationOverride) Ready=MCNutVFX::Prepare(P,Source,Error,true);
        else if(bChain) Ready=MCNutVFX::PrepareChainOverride(P,C.PresentationOverride,Error);
        else if(bGravity) Ready=MCNutVFX::PrepareGravityWavesOverride(P,C.PresentationOverride,Error);
        else if(bSphere || bFlash) Ready=P->PrepareScopedOverride(Source,Contract->Events,
            MCNutVFX::WithWizardSpellDefaults(C.PresentationOverride,*Contract),Error);
        else Ready=P->PrepareProfileOverride(C.PresentationOverride,Error);
        if(!Ready)return false;
        // The successful chain continues into the regular melee presentation.
        if(bChain && !bChainBait && !bChainDodge && !MCNutVFX::Prepare(P,KnightMelee,Error,true))return false;
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
        if(bGravity || bSphere) {
            auto* B=Boss.Get();
            auto* Encounter=B?B->EncounterOwner.Get():nullptr;
            if(Encounter) {
                const FVector Center=FMath::Lerp(B->GetActorLocation(),C.Hero->GetActorLocation(),.55f);
                const FVector Side=FVector::CrossProduct(FVector::UpVector,(C.Hero->GetActorLocation()-B->GetActorLocation()).GetSafeNormal2D());
                // Spawn real whole nuts through the encounter's falling-food path.
                // Both hands get a visible target after the native landings settle.
                const bool bLeft=Encounter->SpawnSpellNut(Center-Side*560.f,137);
                const bool bRight=Encounter->SpawnSpellNut(Center+Side*560.f,271);
                const bool bCenter=!bSphere || Encounter->SpawnSpellNut(Center,419);
                if(bLeft && bRight && bCenter) {
                    for(auto& Food:Encounter->Nuts)if(IsValid(Food.Get()))C.Targets.Add(Food.Get());
                } else bFailed=true;
            } else bFailed=true;
            if(bFailed) {
                if(C.RecordEvent)C.RecordEvent(bSphere?TEXT("SphereSeedRejected"):TEXT("GravitySeedRejected"));
                if(C.ReportStatus)C.ReportStatus(bSphere?TEXT("Не удалось подготовить настоящие орехи для сферы."):
                    TEXT("Не удалось подготовить два настоящих ореха для волн."));
                return;
            }
        }
        if(bChain && P) {
            // The generic sandbox holds the pawn still for visual readiness.
            // The player pull and dodge need CMC. In the bait scenario the
            // borrowed pawn waits in place while the real nut lands and settles.
            if(!bChainBait)C.Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            ChainSubjectHealth=C.Hero->Status->State.Health;
            ChainEvents=P->OnSourceEventAccepted.AddLambda([this,Record=C.RecordEvent](FName ID,FName Event,EMCVFXPresentationResult Result) {
                if(ID!=Source)return;
                if(Result==EMCVFXPresentationResult::Rejected) {bFailed=true;return;}
                if(!Seen.Contains(Event)) {Seen.Add(Event);if(Record)Record(Event);}
            });
            if(bChainBait) {
                const auto* B=Boss.Get();
                if(!B || !B->EncounterOwner || !B->EncounterOwner->LaunchIsolatedEntryNut()) {
                    bFailed=true;
                    if(C.ReportStatus)C.ReportStatus(TEXT("Не удалось запустить настоящее падение ореха для перехвата цепи."));
                }
            }
        }
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
        AMCFoodActor* BaitNut=nullptr;
        if((bSummon || bGravity || bSphere || bChainBait) && !bLaunched && B && B->EncounterOwner) {
            TArray<AMCFoodActor*> Food;
            B->EncounterOwner->GetAvailableRainNuts(Food);
            FoodReady=!Food.IsEmpty();
            if(bGravity || bSphere) FoodReady=Food.Num()>=(bSphere?3:2) && Algo::AllOf(Food,[](const AMCFoodActor* Nut) {
                return Nut->Phase==EMCFoodPhase::Free && Nut->GetVelocity().SizeSquared()<=FMath::Square(80.);
            });
            if(bChainBait && FoodReady) {
                BaitNut=Food[0];
                FoodReady=BaitNut->Phase==EMCFoodPhase::Free && BaitNut->GetVelocity().SizeSquared()<=FMath::Square(40.);
            }
        }
        const bool ChainReady=!bChain || (B && B->State==EMCNutBossState::Idle);
        const bool MeleeReady=!bMeleeSequence || (B && B->State==EMCNutBossState::Idle
            && B->GetAttackCooldownRemaining(EMCNutBossAttack::Melee)<=0.f);
        const bool NextMelee=bMeleeSequence && bLaunched && MeleeStarts<3 && MeleeReady;
        if(!bFailed && FoodReady && ChainReady && MeleeReady && ((!bLaunched && (bEntrance || Lab->IsComparisonReady())) || NextMelee)) {
            if(BaitNut) {
                // Align the private target with the nut's actual resting spot.
                // The live encounter still chooses its own aim and sweep result.
                FVector NutCenter,NutExtent;
                BaitNut->GetActorBounds(true,NutCenter,NutExtent);
                const FVector BossCenter=B->GetActorLocation();
                const FVector LaunchOffset=B->GetActorQuat().UnrotateVector(B->GetChainOrigin()-BossCenter);
                FVector BehindNut=C.Hero->GetActorLocation();
                const double HeroHeight=BehindNut.Z;
                // The launch tip rotates with the new target direction. Fit the
                // private pawn behind the real nut using that future origin.
                for(int32 Iteration=0;Iteration<6;++Iteration) {
                    const FVector Facing=(BehindNut-BossCenter).GetSafeNormal2D(SMALL_NUMBER,B->GetActorForwardVector());
                    const FVector Origin=BossCenter+Facing.Rotation().RotateVector(LaunchOffset);
                    const double Distance=FVector::Dist2D(Origin,NutCenter);
                    const double Alpha=Distance/FMath::Max(1.,Distance+175.);
                    const double LineHeight=FMath::Lerp(Origin.Z,HeroHeight,Alpha);
                    FVector Intercept=NutCenter;
                    Intercept.Z=FMath::Clamp(LineHeight,NutCenter.Z-NutExtent.Z*.7,NutCenter.Z+NutExtent.Z*.7);
                    const double Extend=(Origin.Z-HeroHeight)/FMath::Max(1.,Origin.Z-Intercept.Z);
                    BehindNut=Origin+(Intercept-Origin)*FMath::Clamp(Extend,1.1,2.);
                    BehindNut.Z=HeroHeight;
                }
                C.Hero->SetActorLocation(BehindNut,false,nullptr,ETeleportType::TeleportPhysics);
            }
            const bool Accepted=bFlash?StartFlashInput(C,B):bEntrance || Lab->RunComparisonAction();
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
        for(TActorIterator<AMCNutCombatEffect> It(C.World);It;++It) {
            if(bChain && It->SourceActor==B && It->Cue.PresentationSource==KnightMelee) {
                if(It->Cue.PresentationEvent==TEXT("Tell"))Mark(TEXT("StrikeTell"));
                if(It->Cue.PresentationEvent==TEXT("Slash"))Mark(TEXT("Strike"));
                continue;
            }
            if(It->Cue.PresentationSource!=Source || ((bFlash || bSphere) && It->SourceActor!=B))continue;
            const float CueAge=float(C.World->GetTimeSeconds()-It->Cue.StartedAt);
            const FName Event=It->Cue.AfterWindupEvent.IsNone() || CueAge<It->Cue.WindupSeconds?It->Cue.PresentationEvent:It->Cue.AfterWindupEvent;
            if(!Event.IsNone() && !Seen.Contains(Event)){Seen.Add(Event);if(C.RecordEvent)C.RecordEvent(Event);}
            TArray<FName> Bursts;It->GetAcceptedBurstEvents(Bursts);
            for(FName Burst:Bursts)if(!Seen.Contains(Burst)){Seen.Add(Burst);if(C.RecordEvent)C.RecordEvent(Burst);}
        }
        if(bFlash && bLaunched && !bFailed && B && bFlashToolSelected) {
            if(Seen.Contains(TEXT("Departure")) && Seen.Contains(TEXT("Arrival"))) Release(C);
            else if(C.Hero->CanWork() && C.Hero->Inventory
                && C.Hero->GetToolSwingElapsed()>=C.Hero->Inventory->SwingDuration()+.05f) {
                C.Hero->SetPrimaryInputHeld(false);
                C.Hero->SetPrimaryInputHeld(true);
            }
        }
        if(bChain && Seen.Contains(TEXT("Strike")) && C.Hero->Status->State.Health<ChainSubjectHealth)Mark(TEXT("StrikeHit"));
        if(bChainBait && B && Seen.Contains(TEXT("Fracture")) && !Seen.Contains(TEXT("PhysicalFragments")))
            for(TActorIterator<AMCFoodActor> It(C.World);It;++It)
                if(It->GetOwner()==B->EncounterOwner && It->bFragment && !It->IsDisposed()
                    && It->Body->IsSimulatingPhysics() && !It->GetVelocity().IsNearlyZero()) {
                    Mark(TEXT("PhysicalFragments"));
                    break;
                }
        if(bRain || bEntry)for(TActorIterator<AMCNutLandingShadow> It(C.World);It;++It)if(It->PresentationSource==Source) {
            if(It->IsWarningActive()){Mark(TEXT("Approach"));if(bRain && IsValid(It->TrackedFood.Get()))Mark(TEXT("DropTrail"));}
            if(It->bImpacted){Mark(TEXT("Impact"));bTerminalObserved|=bEntry;}
        }
        if(bGravity)for(TActorIterator<AMCNutLandingShadow> It(C.World);It;++It) {
            if(It->PresentationSource!=Source || !IsValid(It->TrackedFood.Get()))continue;
            auto* Food=It->TrackedFood.Get();
            if(It->IsWarningActive())Mark(TEXT("Approach"));
            if(Food->IsWizardGravityLiftActive()) {GravityLiftedNuts.Add(Food);Mark(TEXT("NutLift"));}
            if(It->bImpacted) {GravityImpactedNuts.Add(Food);Mark(TEXT("Impact"));}
        }
        if(bChainDodge && bLaunched && !bChainDodgeLaunched && B) {
            const auto& Settings=B->BossSettings;
            const float LockAlpha=1.f-Settings.TankChainAimLockLeadSeconds/Settings.TankChainWindup;
            const bool AimLocked=B->State==EMCNutBossState::Telegraph && B->GetChainPhaseAlpha()>=LockAlpha+.02f;
            if(AimLocked || B->GetChainPhase()==EMCNutBossChainPhase::Flight) {
                const FVector Side=FVector::CrossProduct(FVector::UpVector,B->AttackForward).GetSafeNormal();
                C.Hero->LaunchCharacter(Side*850.f+FVector(0,0,180),true,true);
                bChainDodgeLaunched=true;
            }
        }
        if(Age>(bMeleeSequence?45.f:20.f)){Release(C);bFailed=true;if(C.RecordEvent)C.RecordEvent(TEXT("NutNativeActionTimeout"));}
    }
    bool IsComplete(const FMCPerkPreviewContext&)const override
    {
        if(!bCommitted || !bLaunched)return bFailed;
        if(bFailed)return true;
        const auto* B=Boss.Get();
        if(bFlash)return B && Seen.Contains(TEXT("Departure")) && Seen.Contains(TEXT("Arrival"))
            && FVector::DistSquared2D(B->GetActorLocation(),FlashStart)>FMath::Square(200.f);
        // The native Shot cue is emitted only after the encounter accepts an actual nut launch.
        if(bSphere)return B && B->State==EMCNutBossState::Idle
            && Seen.Contains(TEXT("ChargeLeft")) && Seen.Contains(TEXT("ChargeRight"))
            && Seen.Contains(TEXT("Merge")) && Seen.Contains(TEXT("Shot"));
        if(bChain) {
            if(!B || B->State!=EMCNutBossState::Idle || !Seen.Contains(TEXT("End")))return false;
            if(bChainBait)return Seen.Contains(TEXT("CatchNut")) && Seen.Contains(TEXT("Impact")) && Seen.Contains(TEXT("Fracture"))
                && Seen.Contains(TEXT("PhysicalFragments")) && !Seen.Contains(TEXT("Strike"));
            if(bChainDodge)return Seen.Contains(TEXT("Reel")) && !Seen.Contains(TEXT("CatchNut")) && !Seen.Contains(TEXT("CatchPlayer")) && !Seen.Contains(TEXT("Strike"));
            return Seen.Contains(TEXT("CatchPlayer")) && Seen.Contains(TEXT("Reel")) && Seen.Contains(TEXT("StrikeTell"))
                && Seen.Contains(TEXT("Strike")) && Seen.Contains(TEXT("StrikeHit"));
        }
        if(bMeleeSequence)return MeleeStarts==3 && B && B->State==EMCNutBossState::Idle
            && Seen.Contains(TEXT("Tell")) && Seen.Contains(TEXT("Slash")) && Seen.Contains(TEXT("Slam"));
        if(bKamikaze)return bTerminalObserved && !Seen.IsEmpty()
            && (Source!=TEXT("Enemy.NutKamikaze.Fuse") || Seen.Contains(TEXT("Danger")))
            && (Source!=TEXT("Enemy.NutKamikaze.Blast") || Seen.Contains(TEXT("Impact")));
        if(bEntry)return bTerminalObserved && Seen.Contains(TEXT("Approach")) && Seen.Contains(TEXT("Impact"));
        if(bHeadSplit)return B && B->BossRole==EMCNutBossRole::Mage && B->State==EMCNutBossState::Idle
            && Seen.Contains(TEXT("HeadSplit")) && Seen.Contains(TEXT("HeadDust"));
        if(bCounter)return B && B->State==EMCNutBossState::Idle && Seen.Contains(TEXT("Tell")) && Seen.Contains(TEXT("Impact"));
        if(bGravity)return B && B->State==EMCNutBossState::Idle && GravityLiftedNuts.Num()>=2 && GravityImpactedNuts.Num()>=2
            && Seen.Contains(TEXT("LiftLeft")) && Seen.Contains(TEXT("DropLeft")) && Seen.Contains(TEXT("LiftRight"))
            && Seen.Contains(TEXT("DropRight")) && Seen.Contains(TEXT("Approach"));
        return Age>1.f && !Seen.IsEmpty() && (!bRain || Seen.Contains(TEXT("Impact")))
            && (!bSummon || Seen.Contains(TEXT("Release"))) && (!B || B->State==EMCNutBossState::Defeated
            || (B->State==EMCNutBossState::Idle && Age>2.f));
    }
    FString GetPhaseName(const FMCPerkPreviewContext&)const override
    {return bFailed?TEXT("Сценарий не подтверждён · проверьте диагностику"):!bCommitted?TEXT("Подготовка настоящего владельца действия"):!bLaunched?TEXT("Подготовка реального контакта/приземления"):bCounter?TEXT("Настоящие контакты → накопление давления → контрудар"):bKamikaze?TEXT("Настоящий фитиль, преследование и взрыв"):bEntry?TEXT("Настоящий падающий орех и столкновение"):TEXT("Реальное действие босса · исходные события и анимация");}
    float GetTailSeconds()const override{return 2.f;}
    void Release(FMCPerkPreviewContext& C)override {
        if(bFlashToolSelected && IsValid(C.Hero))C.Hero->SetPrimaryInputHeld(false);
    }
    void Cancel(FMCPerkPreviewContext& C)override {
        Release(C);
        if(bFlashToolSelected && IsValid(C.Hero) && C.Hero->Inventory)
            C.Hero->Inventory->ServerSelect(PreviousFlashSlot);
        bFlashToolSelected=false;
        if(auto* Lab=Station.Get()) {
            if(auto* P=Lab->GetComparisonPresentation())P->OnSourceEventAccepted.Remove(ChainEvents);
            Lab->Destroy();
        }
        ChainEvents.Reset();
    }
    void Reset(FMCPerkPreviewContext& C)override {Cancel(C);Station.Reset();Boss.Reset();NativeOwner.Reset();LegacySystems.Reset();StartupCues.Reset();StartupShadows.Reset();Seen.Reset();Age=0;bCommitted=bLaunched=false;}
private:
    bool StartFlashInput(FMCPerkPreviewContext& C,const AMCNutBoss* B) {
        if(!IsValid(B) || !IsValid(C.Hero) || !C.Hero->Inventory || !IsValid(C.Controller))return false;
        C.Hero->SetPrimaryInputHeld(false);
        PreviousFlashSlot=C.Hero->Inventory->Selected;bFlashToolSelected=true;
        C.Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
        if(C.Hero->Inventory->Selected!=EMCToolSlot::Pickaxe)return false;
        FlashStart=B->GetActorLocation();
        const FRotator Aim=(B->GetToolTargetPoint(C.Hero->GetActorLocation())-C.Hero->GetActorLocation()).Rotation();
        C.Controller->SetControlRotation(Aim);
        C.Hero->SetActorRotation(FRotator(0,Aim.Yaw,0));
        C.Hero->SetPrimaryInputHeld(true);
        return true;
    }
    FName Source;TWeakObjectPtr<AMCVFXLabBossStation> Station;TWeakObjectPtr<AMCNutBoss> Boss;
    TWeakObjectPtr<AActor> NativeOwner;
    TArray<TStrongObjectPtr<UNiagaraSystem>> LegacySystems;TArray<TWeakObjectPtr<AMCNutCombatEffect>> StartupCues;TSet<FName> Seen;
    TSet<TWeakObjectPtr<AMCFoodActor>> GravityLiftedNuts,GravityImpactedNuts;
    TArray<TWeakObjectPtr<AMCNutLandingShadow>> StartupShadows;
    int32 MeleeStarts=0;bool bMeleeSequence=false;
    FDelegateHandle ChainEvents;
    bool bChain=false,bChainBait=false,bChainDodge=false,bChainDodgeLaunched=false;
    float ChainSubjectHealth=0;
    float Age=0;bool bCommitted=false,bLaunched=false,bFailed=false,bEntrance=false,bDeath=false,bSummon=false,bRain=false;
    bool bKamikaze=false,bEntry=false,bCounter=false,bHeadSplit=false,bTerminalObserved=false,bGravity=false;
    bool bSphere=false,bFlash=false,bFlashToolSelected=false;
    EMCToolSlot PreviousFlashSlot=EMCToolSlot::Brush;
    FVector FlashStart=FVector::ZeroVector;
};

const bool Registered=[]
{
    for(const auto& Contract:MCNutVFX::Actions())
    {
        if(Contract.ID==TEXT("Boss.NutKnight.Shield") || Contract.ID==TEXT("Boss.NutMage.Fireball"))continue;
        const bool bChainAction=Contract.ID==MCNutVFX::ChainSource();
        const bool bGravityAction=Contract.ID==MCNutVFX::GravityWavesSource();
        const bool bFlashAction=Contract.ID==TEXT("Boss.NutMage.Flash");
        const bool bSphereAction=Contract.ID==TEXT("Boss.NutMage.NutSphere");
        EMCVFXLabBossKind Kind;const bool HasLab=LabKind(Contract,Kind);
        // Chain source, original audio and canonical profile are registered by
        // the shared source catalogue. This is its contextual provider only.
        if(!bChainAction) {
            MCVFXProfileBindings::RegisterLibrarySource(Contract.ID,UMCVFXPresentationLibrary::MouthLibraryPath(),Contract.ID,Contract.Events);
            FMCVFXActionCatalogueEntry Row;Row.ID=Contract.ID;Row.Label=Contract.Label;Row.Stages=Contract.Events;
            Row.Category=Contract.ID.ToString().StartsWith(TEXT("Boss."))?TEXT("Boss"):TEXT("World");
            Row.SourceOwner=TEXT("MCNutVFXCatalog / actual AMCNutBoss, AMCNutKamikaze or AMCNutLandingShadow owner");
            Row.Description=FText::FromString(TEXT("Настоящие игровые события. Контакт, хитбоксы и опасная область принадлежат механике; VFX и SFX редактируются отдельно. Миграция исходного арта проверяется отдельно от наличия привязки."));
            if(Contract.ID==TEXT("Boss.NutKnight.Jump") || Contract.ID==TEXT("Boss.NutKnight.Melee")) {
                Row.SourceOwner+=TEXT(" / AMCTongue::TriggerBossImpactPainWave");
                Row.Description=FText::FromString(Row.Description.ToString()+TEXT("\nПриземление прыжка или Slam последнего удара комбо создаёт волну на языке (AMCTongue, MCTongue.cpp); радиус — 135% радиуса удара. Геометрия языка не редактируется профилем Niagara; плоская поверхность сценария не показывает её деформацию."));
            }
            if(bGravityAction) {
                Row.SourceOwner=TEXT("AMCNutBoss / AMCNutRainEvent / AMCNutLandingShadow");
                Row.Description=FText::FromString(TEXT("Две группы настоящих орехов. Захват следует за корпусом ореха, вспышки следуют за соответствующей кистью, контур падения привязан к языку."));
            }
            if(!HasLab)Row.UnsupportedReason=TEXT("В каталоге не указан зарегистрированный настоящий сценарий полигона.");
            if(bFlashAction) {
                Row.SourceOwner=TEXT("AMCNutBoss / AMCNutCombatEffect");
                Row.Description=FText::FromString(TEXT("Две короткие вспышки в принятых механикой точках начала и конца уклонения от удара киркой."));
            } else if(bSphereAction) {
                Row.SourceOwner=TEXT("AMCNutBoss / AMCNutRainEvent / AMCNutCombatEffect");
                Row.Description=FText::FromString(TEXT("Красный и синий заряды следуют за ладонями, фиолетовый центр остаётся на точке сбора, вспышка выстрела направлена по траектории запуска целого ореха."));
            }
            MCVFXActionCatalog::RegisterSource(MoveTemp(Row));
        }
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
        if(bFlashAction || bSphereAction) {
            const auto SpellDefault=[&](FName Event,const TCHAR* Label) {
                const TCHAR* Path=MCNutVFX::WizardSpellDefaultPath(Contract.ID,Event);
                if(Path)Default(Event,TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(Path)),Label);
            };
            if(bFlashAction) {
                SpellDefault(TEXT("Departure"),TEXT("Уклонение · исходная точка"));
                SpellDefault(TEXT("Arrival"),TEXT("Уклонение · конечная точка"));
            } else {
                SpellDefault(TEXT("ChargeLeft"),TEXT("Красный заряд · левая ладонь"));
                SpellDefault(TEXT("ChargeRight"),TEXT("Синий заряд · правая ладонь"));
                SpellDefault(TEXT("Merge"),TEXT("Слияние · центр сферы"));
                SpellDefault(TEXT("Shot"),TEXT("Выстрел · центр и направление полёта"));
            }
        }
        if(bGravityAction) {
            Default(TEXT("LiftLeft"),MCNutVFX::GravityWavesDefaultSystem(TEXT("LiftLeft")),TEXT("Подъём · левая кисть"));
            Default(TEXT("DropLeft"),MCNutVFX::GravityWavesDefaultSystem(TEXT("DropLeft")),TEXT("Сброс · левая кисть"));
            Default(TEXT("LiftRight"),MCNutVFX::GravityWavesDefaultSystem(TEXT("LiftRight")),TEXT("Подъём · правая кисть"));
            Default(TEXT("DropRight"),MCNutVFX::GravityWavesDefaultSystem(TEXT("DropRight")),TEXT("Сброс · правая кисть"));
            Default(TEXT("NutLift"),MCNutVFX::GravityWavesDefaultSystem(TEXT("NutLift")),TEXT("Гравитационный захват · корпус ореха"));
            Entry.StageLabels.Add(TEXT("Approach"),FText::FromString(TEXT("Предупреждение · подтверждённое место падения")));
            Default(TEXT("Impact"),MCNutVFX::GravityWavesDefaultSystem(TEXT("Impact")),TEXT("Удар · контакт с языком"));
        }
        if(bChainAction) {
            Default(TEXT("Tell"),MCNutVFX::ChainDefaultSystem(TEXT("Tell")),TEXT("Замах · оголовок дубины"));
            Default(TEXT("Spin"),MCNutVFX::ChainDefaultSystem(TEXT("Spin")),TEXT("Оборот крюка · от оголовка дубины"));
            Default(TEXT("Release"),MCNutVFX::ChainDefaultSystem(TEXT("Release")),TEXT("Выпуск · начало полёта крюка"));
            Default(TEXT("Flight"),MCNutVFX::ChainDefaultSystem(TEXT("Flight")),TEXT("Цепь · от дубины до крюка или груза"));
            Default(TEXT("CatchPlayer"),MCNutVFX::ChainDefaultSystem(TEXT("CatchPlayer")),TEXT("Захват зубика · точка контакта"));
            Default(TEXT("CatchNut"),MCNutVFX::ChainDefaultSystem(TEXT("CatchNut")),TEXT("Захват ореха · точка контакта"));
            Default(TEXT("Reel"),MCNutVFX::ChainDefaultSystem(TEXT("Reel")),TEXT("Сматывание · от крюка к дубине"));
            Default(TEXT("Impact"),MCNutVFX::ChainDefaultSystem(TEXT("Impact")),TEXT("Орех попал в рыцаря"));
            Default(TEXT("End"),MCNutVFX::ChainDefaultSystem(TEXT("End")),TEXT("Окончание атаки · оголовок дубины"));
            Default(TEXT("Fracture"),MCNutVFX::ChainDefaultSystem(TEXT("Fracture")),TEXT("Осколки целого ореха · точка удара"));
        }
        if(HasLab) {
            Entry.Scenarios={EMCPerkPreviewScenario::Target};
            const TCHAR* ScenarioLabel=Kind==EMCVFXLabBossKind::TankMelee?TEXT("Три родных атаки: два взмаха → комбо с ударом в землю"):
                bChainAction?TEXT("Крюк подтягивает зубика → удар дубиной"):
                Kind==EMCVFXLabBossKind::TankCounterShove?TEXT("Контакты игрока → настоящий контрудар"):
                Kind==EMCVFXLabBossKind::TankHeadSplit?TEXT("Поражение рыцаря → раскол → настоящий маг"):
                bGravityAction?TEXT("Два целых ореха → левый и правый подъём → два приземления"):
                bFlashAction?TEXT("Настоящий взмах киркой → рывок мага в сторону"):
                bSphereAction?TEXT("Сбор целых орехов → слияние зарядов → физические выстрелы"):
                Kind==EMCVFXLabBossKind::KamikazeFuse || Kind==EMCVFXLabBossKind::KamikazeBlast?TEXT("Настоящий фитиль → преследование → взрыв"):
                Kind==EMCVFXLabBossKind::RainEntry?TEXT("Настоящее падение ореха → столкновение"):TEXT("Настоящее действие босса");
            Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(ScenarioLabel));
            if(bChainAction) {
                Entry.Scenarios.Append({EMCPerkPreviewScenario::Acquire,EMCPerkPreviewScenario::Timeout});
                Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Acquire,FText::FromString(TEXT("Целый орех перехватывает цепь и оглушает рыцаря")));
                Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Timeout,FText::FromString(TEXT("Зубик уходит в сторону после замаха")));
            }
            const FName ID=Contract.ID;Entry.Create=[ID]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FAdapter>(ID);};
        }
        // No factory means honestly metadata-only; the shared registry must not
        // turn this entry into a successful contextual-preview capability.
        MCPerkPreviewAdapters::Register(MoveTemp(Entry));
    }
    return true;
}();
}
