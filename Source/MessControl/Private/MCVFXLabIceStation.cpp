#include "MCVFXLabIceStation.h"
#include "MCVFXLab.h"
#include "MCIceEvent.h"
#include "MCIceEventVFX.h"
#include "MCColdCola.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCPlaytestBotController.h"
#include "MCPlayerState.h"
#include "MCInventoryComponent.h"
#include "MCLocomotionSurface.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"

AMCVFXLabIceStation::AMCVFXLabIceStation()
{
    bReplicates=true; bAlwaysRelevant=true;
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bStartWithTickEnabled=false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("StationRoot")));
    StatusLabel=CreateDefaultSubobject<UTextRenderComponent>(TEXT("StationStatus"));
    StatusLabel->SetupAttachment(GetRootComponent());
    StatusLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    StatusLabel->SetRelativeLocation(FVector(0,-760,200));
    StatusLabel->SetWorldSize(24); StatusLabel->SetHorizontalAlignment(EHTA_Center);
}

void AMCVFXLabIceStation::BeginPlay()
{
    Super::BeginPlay();
    SetActorTickEnabled(false);
    if(HasAuthority()) {
        bRunning=false;
        Show(TEXT("Paused; waiting for a nearby spectator"));
        if(bAutoRun && !AMCVFXLab::Find(GetWorld())) SetRunning(true);
    }
    OnRep_Status();
}

void AMCVFXLabIceStation::Show(const FString& Message)
{
    Status=Message;
    if(!LastFailure.IsEmpty()) Status+=TEXT("\nLAST FAILURE: ")+LastFailure;
    OnRep_Status(); ForceNetUpdate();
}

void AMCVFXLabIceStation::OnRep_Status()
{
    if(!StatusLabel) return;
    const FString Name=StaticEnum<EMCVFXLabIceKind>()->GetNameStringByValue(int64(Kind));
    StatusLabel->SetText(FText::FromString(FString::Printf(TEXT("%s  |  cycles %d  passed %d  failed %d\n%s"),
        *Name,Cycles,Passed,Failed,*Status)));
    StatusLabel->SetTextRenderColor(Failed==0?FColor(160,235,255):FColor(255,150,100));
}

void AMCVFXLabIceStation::Observe(bool Condition,const TCHAR* Result)
{
    if(Condition) ++Passed;
    else {++Failed; LastFailure=Result; bCycleFailed=true;}
    Show(FString(Condition?TEXT("OK: "):TEXT("FAILED: "))+Result);
}

void AMCVFXLabIceStation::RecordCycle()
{
    if(!bComplete || bCycleRecorded) return;
    bCycleRecorded=true;
    if(bCycleFailed) ++FailedCycles; else ++PassedCycles;
    Show(bCycleFailed?TEXT("Cycle finished with recorded failures"):TEXT("Cycle complete; holding final scene until repeat"));
}

bool AMCVFXLabIceStation::SurfaceAt(FVector Local,FHitResult& Hit) const
{
    return IsValid(Floor) && Floor->SurfacePoint(GetActorTransform().TransformPosition(Local),Hit)
        && Hit.ImpactNormal.Z>.65f && Hit.ImpactPoint.Z>-180;
}

bool AMCVFXLabIceStation::PlaceSubject(int32 Index,FVector Local,FVector Facing)
{
    FHitResult Hit;
    if(!Subjects.IsValidIndex(Index) || !IsValid(Subjects[Index]) || !SurfaceAt(Local,Hit)) return false;
    auto* Hero=Subjects[Index].Get();
    if(!Hero->ToothPhysics->CanAct()) return false;
    Hero->CancelGameplayInput(); Hero->GetCharacterMovement()->StopMovementImmediately();
    Hero->SetActorLocationAndRotation(Hit.ImpactPoint+FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),
        GetActorTransform().TransformVectorNoScale(Facing).Rotation(),false,nullptr,ETeleportType::TeleportPhysics);
    Hero->ForceNetUpdate(); return true;
}

AMCToothCharacter* AMCVFXLabIceStation::SpawnSubject(FVector Local,int32 Index)
{
    FHitResult Hit; if(!SurfaceAt(Local,Hit)) return nullptr;
    auto* Class=LoadClass<AMCToothCharacter>(nullptr,TEXT("/Game/Blueprints/BP_PlayerCharacter.BP_PlayerCharacter_C"));
    if(!Class) return nullptr;
    const auto* Defaults=Class->GetDefaultObject<AMCToothCharacter>();
    const FTransform Pose(GetActorQuat(),Hit.ImpactPoint+FVector(0,0,Defaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3));
    auto* Hero=GetWorld()->SpawnActorDeferred<AMCToothCharacter>(Class,Pose,this,nullptr,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Hero) return nullptr;
    Hero->AutoPossessAI=EAutoPossessAI::Disabled;
    Hero->Tags.Add(TEXT("MC_VFXLabSubject")); Hero->FinishSpawning(Pose); OwnedActors.Add(Hero);
    Hero->Status->Initialize(300);
    FActorSpawnParameters Params; Params.Owner=this; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Controller=GetWorld()->SpawnActor<AMCPlaytestBotController>(FVector::ZeroVector,FRotator::ZeroRotator,Params);
    if(!Controller) {Hero->Destroy();return nullptr;}
    OwnedActors.Add(Controller); Controller->Possess(Hero);
    Controller->Configure(EMCPlaytestBotSkill::Regular,Index,41+int32(Kind)*97);
    auto* Identity=Controller->GetPlayerState<AMCPlayerState>();
    if(!Identity) return nullptr;
    Identity->SetIsSpectator(false); Identity->SetIsOnlyASpectator(false);
    // Keep genuine gameplay participant/player-state identity while the station owns inputs.
    Controller->StopMovement(); Controller->SetActorTickEnabled(false);
    GetWorldTimerManager().ClearAllTimersForObject(Controller);
    Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
    Subjects.Add(Hero); return Hero;
}

bool AMCVFXLabIceStation::StartWinter()
{
    const FTransform Pose(GetActorRotation(),GetActorLocation());
    Winter=GetWorld()->SpawnActorDeferred<AMCIceEvent>(AMCIceEvent::StaticClass(),Pose,this,nullptr,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Winter) return false;
    OwnedActors.Add(Winter); Winter->bLabEvent=true; Winter->Tongue=Floor;
    for(AMCToothCharacter* Hero:Subjects) Winter->LabParticipants.Add(Hero);
    Winter->ArrivalSeconds=1; Winter->FreezeSeconds=Kind==EMCVFXLabIceKind::FreezeThawAndRescue?4:60;
    Winter->CircleRadius=Winter->MinCircleRadius=180; Winter->CircleMinGap=0;
    Winter->CircleSeconds=Winter->MinCircleSeconds=120; Winter->BossFrostRadius=100;
    Winter->FinishSpawning(Pose);
    Winter->Start(); if(!Winter->IsActive()) return false;
    // Observe the event after its authoritative stage/landing update, including
    // the first long frame while a second PIE client loads the arena copies.
    AddTickPrerequisiteActor(Winter);
    FHitResult Core,Safe;
    if(!SurfaceAt(FVector::ZeroVector,Core) || !SurfaceAt(FVector(-550,0,0),Safe)) return false;
    Winter->CandyAnchor=Floor->GetActorTransform().InverseTransformPosition(Core.ImpactPoint);
    Winter->SafeAnchor=Floor->GetActorTransform().InverseTransformPosition(Safe.ImpactPoint);
    Winter->NextIcicleAt=Winter->NextZoneCrystalAt=Winter->NextNovaAt=Winter->Now()+1.e8;
    Winter->NextCircleRetryAt=Winter->Now()+1.e8;
    Winter->RefreshPresentation(.1f); Winter->ForceNetUpdate();
    if(Winter->SlipperyFloor) {
        Winter->SlipperyFloor->SetActorLocation(Core.ImpactPoint);
        Winter->SlipperyFloor->HalfExtent=FVector(900,650,700);
        Winter->SlipperyFloor->RefreshBounds(); Winter->SlipperyFloor->ForceNetUpdate();
    }
    if(Kind==EMCVFXLabIceKind::CentralCrystal) {
        // Keep ordinary tool damage/cadence; allow enough time to actually break 1440 HP.
        CycleSeconds=FMath::Max(CycleSeconds,FMath::CeilToFloat(Winter->MaxCandyHealth/Subjects[0]->Inventory->Damage())
            *(Subjects[0]->Inventory->SwingDuration()+.03f)+8.f);
        FHitResult Warm; if(SurfaceAt(FVector(280,0,0),Warm))
            Winter->SafeAnchor=Floor->GetActorTransform().InverseTransformPosition(Warm.ImpactPoint);
    }
    return true;
}

bool AMCVFXLabIceStation::WinterVisualsReady() const
{
    return Winter && (GetNetMode()==NM_DedicatedServer ||
        (IsValid(Winter->EventVFX) && Winter->EventVFX->HasRequiredAssets() && Winter->EventVFX->HasWindVisuals()));
}

void AMCVFXLabIceStation::HitCue(AMCToothCharacter* Worker)
{
    if(IsValid(Worker)) Worker->SetPrimaryInputHeld(true);
}

void AMCVFXLabIceStation::Cleanup()
{
    if(IsValid(Winter)) {
        RemoveTickPrerequisiteActor(Winter);
        auto* Effects=Winter->EventVFX.Get();
        Winter->Stop();
        // Success bursts normally outlive an event. Lab reset owns them too.
        if(IsValid(Effects)) Effects->Destroy();
    }
    Winter=nullptr;
    for(AMCToothCharacter* Hero:Subjects) if(IsValid(Hero)) Hero->CancelGameplayInput();
    for(int32 I=OwnedActors.Num()-1;I>=0;--I) if(IsValid(OwnedActors[I])) {
        auto* Controller=Cast<AMCPlaytestBotController>(OwnedActors[I]);
        auto* Identity=Controller?Controller->GetPlayerState<AMCPlayerState>():nullptr;
        OwnedActors[I]->Destroy();
        if(IsValid(Identity)) Identity->Destroy();
    }
    OwnedActors.Reset(); Subjects.Reset(); Blocks.Reset();
}

void AMCVFXLabIceStation::BeginCycle()
{
    Cleanup(); ++Cycles; Step=0; bComplete=bCycleFailed=bCycleRecorded=false; Baseline=PeakFreeze=0;
    CycleStartedAt=GetWorld()->GetTimeSeconds(); NextActionAt=CycleStartedAt+1.3;
    CycleSeconds=FMath::IsFinite(CycleSeconds)?FMath::Max(24.f,CycleSeconds):28.f;
    FHitResult Hit;
    if(!SurfaceAt(FVector::ZeroVector,Hit)) {
        Observe(false,TEXT("Explicit floor must have walkable tongue collision above Z=-180")); bComplete=true; return;
    }
    const int32 Count=Kind==EMCVFXLabIceKind::PhysicsReaction?3:Kind==EMCVFXLabIceKind::CentralCrystal || Kind==EMCVFXLabIceKind::IceBlocks?1:2;
    for(int32 I=0;I<Count;++I) {
        const FVector Local=Kind==EMCVFXLabIceKind::PhysicsReaction?FVector((I-1)*280,0,0):
            Kind==EMCVFXLabIceKind::CentralCrystal?FVector(280,0,0):
            Kind==EMCVFXLabIceKind::IceBlocks?FVector(600,-300,0):I==0?FVector(550,0,0):FVector(-550,-110,0);
        if(!SpawnSubject(Local,I)) { Observe(false,TEXT("Controlled living bot spawn/footprint failed"));bComplete=true;return; }
    }
    Observe(Subjects.Num()==Count && Subjects[0]->GetController() && Subjects[0]->Status->IsAlive(),TEXT("Living controlled subjects spawned"));
    if(Kind!=EMCVFXLabIceKind::IceBlocks && Kind!=EMCVFXLabIceKind::PhysicsReaction) {
        if(!StartWinter()) {Observe(false,TEXT("Winter event failed on explicit floor"));bComplete=true;return;}
    }
    if(Kind==EMCVFXLabIceKind::IceBlocks) {
        for(int32 I=0;I<3;++I) {
            FHitResult FloorHit;
            if(!SurfaceAt(FVector((I-1)*280,0,0),FloorHit)) {Observe(false,TEXT("Ice drop footprint missing"));bComplete=true;return;}
            const FTransform Pose(FRotator(8,I*31,12),FloorHit.ImpactPoint+FVector(0,0,650));
            auto* Block=GetWorld()->SpawnActorDeferred<AMCIceBlock>(AMCIceBlock::StaticClass(),Pose,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(!Block) {Observe(false,TEXT("Ice block spawn failed"));bComplete=true;return;}
            Block->Shape=I; Block->Health=Block->MaxHealth=80; Block->Size=FVector(95);
            Block->FinishSpawning(Pose); OwnedActors.Add(Block); Blocks.Add(Block);
        }
        NextActionAt=CycleStartedAt+3;
    }
    if(Kind==EMCVFXLabIceKind::PhysicsReaction) {
        for(int32 I=0;I<Subjects.Num();++I)
            Observe(Subjects[I]->ToothPhysics->SetActiveRagdollMode(EMCActiveRagdollMode(I)),TEXT("Active limb mode applied (Off / Soft / Firm)"));
    }
    Show(TEXT("Running real gameplay; awaiting arrival / floor contact"));
}

void AMCVFXLabIceStation::Reset()
{
    if(!HasAuthority()) return;
    Cleanup(); Passed=Failed=Cycles=PassedCycles=FailedCycles=0; LastFailure.Empty(); bComplete=false;
    if(bRunning) BeginCycle(); else Show(TEXT("Stopped; all station-owned actors removed"));
}

void AMCVFXLabIceStation::SetRunning(bool Running)
{
    if(!HasAuthority() || !HasActorBegunPlay() || !GetWorld() || !GetWorld()->IsGameWorld() || bRunning==Running) return;
    bRunning=Running;
    SetActorTickEnabled(bRunning);
    if(Running) BeginCycle(); else {Cleanup();Show(TEXT("Stopped; all station-owned actors removed"));}
    ForceNetUpdate();
}

void AMCVFXLabIceStation::Advance(double Time)
{
    if(Time<NextActionAt || bComplete) return;
    if(Subjects.IsEmpty() || !IsValid(Subjects[0]) || !Subjects[0]->Status->IsAlive()) {
        Observe(false,TEXT("Subject disappeared or died before completion")); bComplete=true; return;
    }
    auto* Hero=Subjects[0].Get();
    if(Kind!=EMCVFXLabIceKind::IceBlocks && Kind!=EMCVFXLabIceKind::PhysicsReaction && !IsValid(Winter)) {
        Observe(false,TEXT("Winter owner disappeared")); bComplete=true; return;
    }
    switch(Kind)
    {
    case EMCVFXLabIceKind::FreezeThawAndRescue:
        if(Step==0) {
            Observe(Winter->Stage==EMCIceEventStage::Active && WinterVisualsReady(),TEXT("Winter arrival, loaded wind/crystal VFX and active state"));
            Step=1; NextActionAt=CycleStartedAt+3.5;
        } else if(Step==1) {
            PeakFreeze=Winter->FreezeAmount(Hero);
            Observe(PeakFreeze>.25f && PeakFreeze<1 && (GetNetMode()==NM_DedicatedServer || Winter->IceCoatings.Num()>0),TEXT("Real body freeze grew; render hosts have animated ice coating"));
            Observe(PlaceSubject(0,FVector(-550,0,0)) && Winter->IsSafePoint(Hero->GetActorLocation()-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())),TEXT("Frozen bot moved into a real warm zone"));
            Step=2; NextActionAt=Time+PeakFreeze*Winter->ThawSeconds*2+1;
        } else if(Step==2) {
            Observe(Winter->FreezeAmount(Hero)<.02f,TEXT("Warm zone thaw completed at half reference rate"));
            Hero->FreezeLegs(160); Baseline=Hero->IceLegHealth;
            Observe(Hero->HasFrozenLegs() && Hero->CanWork(),TEXT("160 HP ice cuffs root movement and retain tools"));
            HitCue(Hero); Step=3; NextActionAt=Time+.1; ActionDeadline=Time+3;
        } else if(Step==3) {
            if(Hero->IceLegHealth>=Baseline && Time<ActionDeadline) {NextActionAt=Time+.1;break;}
            Hero->SetPrimaryInputHeld(false);
            Observe(Hero->IceLegHealth<Baseline && Hero->IceLegHealth>0,TEXT("Ordinary self-rescue swing damaged foot ice"));
            Observe(PlaceSubject(1,FVector(-550,-110,0),FVector(0,1,0)),TEXT("Teammate placed within validated rescue reach"));
            HitCue(Subjects[1]); Step=4; NextActionAt=Time+.1; ActionDeadline=Time+8;
        } else {
            if(!Hero->HasFrozenLegs() || Time>=ActionDeadline) {
                Subjects[1]->SetPrimaryInputHeld(false);
                Observe(!Hero->HasFrozenLegs() && Hero->ToothPhysics->CanAct(),TEXT("Teammate ordinary swings broke cuffs and released locomotion"));
                Observe(WinterVisualsReady(),TEXT("Winter presentation remains owned after foot shatter")); bComplete=true;
            } else NextActionAt=Time+.1;
        }
        break;
    case EMCVFXLabIceKind::NovaProtection:
        if(Step==0) {
            Observe(WinterVisualsReady(),TEXT("Nova station VFX assets loaded"));
            Winter->BeginNova();
            Observe(Winter->bNovaWarning && Winter->NovaImpactAt>Winter->Now(),TEXT("Real nova warning queued"));
            Step=1; NextActionAt=Time+Winter->NovaWarningSeconds+.4;
        } else if(Step==1) {
            Observe(Hero->HasFrozenLegs() && !Subjects[1]->HasFrozenLegs(),TEXT("Nova 360 froze outside bot and spared warm-zone bot"));
            Observe(!Winter->bNovaWarning,TEXT("Nova warning resolved into impact"));
            HitCue(Hero); Step=2; NextActionAt=Time+.1; ActionDeadline=Time+8;
        } else {
            if(!Hero->HasFrozenLegs() || Time>=ActionDeadline) {
                Hero->SetPrimaryInputHeld(false);
                Observe(!Hero->HasFrozenLegs(),TEXT("Unsafe bot broke its nova cuffs with ordinary swings")); bComplete=true;
            } else NextActionAt=Time+.1;
        }
        break;
    case EMCVFXLabIceKind::CrystalAndIcicles:
        if(Step==0) {
            Observe(WinterVisualsReady(),TEXT("Falling crystal / icicle presentation loaded"));
            Observe(PlaceSubject(0,FVector(-350,0,0)),TEXT("Victim placed in the crystal landing vortex"));
            BaselineKnockdowns=Hero->ToothPhysics->KnockdownCount;
            Winter->QueueZoneCrystal();
            Observe(Winter->ZoneCrystals.Num()==1 && !Winter->IsCircleBlocked(Winter->CircleIndex),TEXT("Falling crystal exists and does not block warmth before landing"));
            Step=1; NextActionAt=Time+Winter->ZoneCrystalFallSeconds+.3;
        } else if(Step==1) {
            Observe(Winter->ZoneCrystals.Num()==1 && Winter->ZoneCrystals[0].bLanded && Winter->IsCircleBlocked(Winter->CircleIndex),TEXT("Landed crystal blocks its own warm circle"));
            Observe(Hero->ToothPhysics->KnockdownCount>BaselineKnockdowns,TEXT("Crystal radial/tangential landing impulse caused real ragdoll"));
            Step=2; NextActionAt=Time+5;
        } else if(Step==2) {
            Observe(Hero->ToothPhysics->CanAct(),TEXT("Landing victim completed real get-up"));
            Observe(PlaceSubject(1,FVector(-405,0,0),FVector(-1,0,0)),TEXT("Worker faces crystal in ordinary pickaxe reach"));
            HitCue(Subjects[1]); Step=3; NextActionAt=Time+7;
        } else if(Step==3) {
            Subjects[1]->SetPrimaryInputHeld(false);
            Observe(Winter->ZoneCrystals.IsEmpty() && !Winter->IsCircleBlocked(Winter->CircleIndex),TEXT("Actual swings broke the 160 HP obstruction and restored warmth"));
            Observe(PlaceSubject(0,FVector(550,220,0)),TEXT("Icicle victim placed on supported floor"));
            Baseline=Hero->Status->State.Health; Winter->QueueIcicle(Hero);
            Step=4; NextActionAt=Time+Winter->IcicleWarningSeconds+.35;
        } else {
            Observe(Hero->Status->State.Health<Baseline && Hero->Status->IsAlive(),TEXT("Real icicle impact damaged living bot and produced recoil"));
            if(Step<6) {Baseline=Hero->Status->State.Health;Winter->QueueIcicle(Hero);++Step;NextActionAt=Time+Winter->IcicleWarningSeconds+.35;}
            else {Observe(WinterVisualsReady(),TEXT("Three impacts retained live winter VFX"));bComplete=true;}
        }
        break;
    case EMCVFXLabIceKind::CentralCrystal:
        if(Step==0) {
            Observe(WinterVisualsReady() && Winter->CandyHealth==1440,TEXT("Central crystal has real 1440 HP and current crystal VFX"));
            Observe(PlaceSubject(0,FVector(280,0,0),FVector(-1,0,0)),TEXT("Worker faces central crystal in ordinary reach"));
            Baseline=Winter->CandyHealth; HitCue(Hero); Step=1; NextActionAt=Time+1;
        } else if(Step==1) {
            Observe(Winter->CandyHealth<Baseline && Winter->CandyHealth>0,TEXT("Ordinary animated pickaxe strikes chipped core health"));
            Step=2; NextActionAt=Time+.2;
        } else if(Winter->IsComplete()) {
            Hero->SetPrimaryInputHeld(false);
            Observe(Winter->CandyHealth<=0 && Winter->Players.IsEmpty(),TEXT("Real core destruction completed event and removed freeze gameplay"));
            Observe(GetNetMode()==NM_DedicatedServer || (IsValid(Winter->EventVFX) && Winter->EventVFX->IsFinishingPresentation()),TEXT("Success owns final shard/mist burst after core destruction"));
            bComplete=true;
        } else {NextActionAt=Time+.1;Show(FString::Printf(TEXT("Breaking core through normal swings: %.0f / %.0f HP"),Winter->CandyHealth,Winter->MaxCandyHealth));}
        break;
    case EMCVFXLabIceKind::IceBlocks:
        if(Step==0) {
            bool Landed=Blocks.Num()==3;
            for(AMCIceBlock* Block:Blocks) if(IsValid(Block)) {
                FHitResult Support; Landed&=Floor->SurfacePoint(Block->GetActorLocation(),Support)
                    && Block->GetActorLocation().Z-Support.ImpactPoint.Z<130 && Block->Body->IsSimulatingPhysics();
            } else Landed=false;
            Observe(Landed,TEXT("Cube/sphere/cylinder ice blocks physically fell and retained floor support"));
            Step=1;
        }
        if(Step>=1 && Step<=3) {
            auto* Block=Blocks[Step-1].Get();
            if(!IsValid(Block)) {Observe(false,TEXT("Ice block vanished before break observation"));bComplete=true;break;}
            if(Block->bBroken) {
                Hero->SetPrimaryInputHeld(false);
                Observe(Block->Health<=0 && Block->Body->GetCollisionEnabled()==ECollisionEnabled::NoCollision,TEXT("Ordinary swings broke physical ice and removed blocking collision"));
                ++Step; NextActionAt=Time+.4;
            } else if(!Hero->IsPrimaryHeld()) {
                const FVector Local=GetActorTransform().InverseTransformPosition(Block->GetActorLocation());
                Observe(PlaceSubject(0,FVector(Local.X,Local.Y-140,0),FVector(0,1,0)),TEXT("Worker approaches landed ice using a supported pose"));
                HitCue(Hero); NextActionAt=Time+.25;
            } else NextActionAt=Time+.2;
        } else if(Step>3) {
            Observe(LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Gameplay/VFX/NS_IceShatter.NS_IceShatter"))!=nullptr,TEXT("Current cold-cola shatter system is available"));bComplete=true;
        }
        break;
    case EMCVFXLabIceKind::PhysicsReaction:
        if(Step==0) {
            BaselineKnockdowns=Hero->ToothPhysics->KnockdownCount;
            BaselinePosition=Hero->GetActorLocation();
            const FVector Impulse=GetActorForwardVector()*FMath::Min(120.f,Hero->ToothPhysics->Settings.FallThreshold*.5f)+FVector(0,0,30);
            Hero->Status->Damage(12,-GetActorForwardVector()); Hero->ToothPhysics->ApplyHit(Impulse,Hero->GetActorLocation());
            Step=1; NextActionAt=Time+1;
        } else if(Step==1) {
            Observe(Hero->ToothPhysics->KnockdownCount==BaselineKnockdowns && FVector::DistSquared(Hero->GetActorLocation(),BaselinePosition)>25,TEXT("Below-threshold hit produced real recoil without knockdown"));
            for(int32 I=0;I<Subjects.Num();++I) {
                PlaceSubject(I,FVector((I-1)*280,0,0));
                auto* Bot=Subjects[I].Get();
                const float Push=FMath::Min(1000.f,Bot->ToothPhysics->Settings.FallThreshold+120);
                Bot->ToothPhysics->ApplyHit(GetActorForwardVector()*Push+FVector(0,0,180),Bot->GetActorLocation());
            }
            bool Fallen=true;for(AMCToothCharacter* Bot:Subjects) Fallen&=Bot->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll;
            Observe(Fallen,TEXT("Off / Soft / Firm subjects entered server-simulated ragdoll"));
            Step=2; NextActionAt=Time+7;
        } else {
            bool Recovered=true;for(AMCToothCharacter* Bot:Subjects) Recovered&=IsValid(Bot) && Bot->Status->IsAlive()
                && Bot->ToothPhysics->GetBodyState()==EMCBodyState::Standing && Bot->ToothPhysics->RecoveryCount>0
                && Bot->GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::QueryAndPhysics;
            Observe(Recovered,TEXT("All physical bodies found support, blended get-up and restored capsules"));bComplete=true;
        }
        break;
    }
    RecordCycle();
}

void AMCVFXLabIceStation::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(!HasAuthority() || !bRunning) return;
    const double Time=GetWorld()->GetTimeSeconds();
    if(Time-CycleStartedAt>=CycleSeconds) {
        if(!bComplete) {Observe(false,TEXT("Cycle timeout: requested gameplay result was not observed"));bComplete=true;}
        RecordCycle();
        BeginCycle();return;
    }
    Advance(Time); RecordCycle();
}

void AMCVFXLabIceStation::EndPlay(const EEndPlayReason::Type Reason)
{
    bRunning=false;SetActorTickEnabled(false);
    if(HasAuthority()) Cleanup();
    Super::EndPlay(Reason);
}

void AMCVFXLabIceStation::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCVFXLabIceStation,Kind); DOREPLIFETIME(AMCVFXLabIceStation,Floor);
    DOREPLIFETIME(AMCVFXLabIceStation,bRunning); DOREPLIFETIME(AMCVFXLabIceStation,Passed);
    DOREPLIFETIME(AMCVFXLabIceStation,Failed); DOREPLIFETIME(AMCVFXLabIceStation,Cycles); DOREPLIFETIME(AMCVFXLabIceStation,Status);
    DOREPLIFETIME(AMCVFXLabIceStation,PassedCycles); DOREPLIFETIME(AMCVFXLabIceStation,FailedCycles);
}
