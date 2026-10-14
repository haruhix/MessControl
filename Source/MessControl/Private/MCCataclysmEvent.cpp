#include "MCCataclysmEvent.h"
#include "MCCataclysmJelly.h"
#include "MCCataclysmRaft.h"
#include "MCCataclysmDebris.h"
#include "MCCoffeeFlood.h"
#include "MCCoffeeProfile.h"
#include "MCDayPlan.h"
#include "MCGameState.h"
#include "MCMouthSurface.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "MCArenaTooth.h"
#include "Components/SceneComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

AMCCataclysmEvent::AMCCataclysmEvent()
{
    bReplicates=true; bAlwaysRelevant=true;
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=.1f;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("EncounterRoot")));
}
double AMCCataclysmEvent::Now() const
{
    const auto* GS=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    return GS?GS->GetServerWorldTimeSeconds():GetWorld()?GetWorld()->GetTimeSeconds():0;
}
bool AMCCataclysmEvent::IsRunning() const
{
    return Phase!=EMCCataclysmPhase::Dormant && Phase!=EMCCataclysmPhase::Complete && Phase!=EMCCataclysmPhase::Stopped;
}
AMCCataclysmEvent* AMCCataclysmEvent::FindActive(UWorld* World)
{
    if(World) for(TActorIterator<AMCCataclysmEvent> It(World);It;++It) if(It->IsRunning()) return *It;
    return nullptr;
}
bool AMCCataclysmEvent::Start(const UMCDayPlan* Plan,EMCCataclysmVariant Choice,int32 Batch)
{
    if(!HasAuthority() || !Plan || IsRunning()) return false;
    if(const auto* Active=FindActive(GetWorld());Active && Active!=this) return false;
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It)
        if(!It->CurrentVertices().IsEmpty()) {Tongue=*It;break;}
    if(!Tongue) {bFailed=true;SetPhase(EMCCataclysmPhase::Stopped);return false;}
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    Random.Initialize(HashCombine(GS?GS->RunSeed:1,GetTypeHash(Batch?Batch:GetUniqueID())));
    Variant=Choice==EMCCataclysmVariant::Random?(Random.RandRange(0,1)?EMCCataclysmVariant::Rafts:EMCCataclysmVariant::Jellies):Choice;
    bCola=Random.RandRange(0,1)!=0;
    DrinkPlan=DuplicateObject<UMCDayPlan>(Plan,this);
    if(bCola) if(auto* Cola=LoadObject<UMCCoffeeProfile>(nullptr,TEXT("/Game/Data/DA_ColdColaLiquid.DA_ColdColaLiquid")))
        DrinkPlan->CoffeeProfile=Cola;
    DrinkPlan->FloodHeight=FMath::Max(155.f,DrinkPlan->FloodHeight);
    WarningSeconds=FMath::Clamp(FMath::IsFinite(WarningSeconds)?WarningSeconds:2.f,.5f,8.f);
    MaxDirtPatches=FMath::Clamp(MaxDirtPatches,4,32);
    DirtBatch=Batch>0?Batch:100000+int32(GetUniqueID()%1000000);
    CareBaselines.Reset();
    for(TActorIterator<AActor> It(GetWorld());It;++It)
        if(Cast<AMCArenaTooth>(*It) || (Cast<AMCToothCharacter>(*It) && Cast<AMCToothCharacter>(*It)->GetPlayerState()
            && !Cast<AMCToothCharacter>(*It)->GetPlayerState()->IsOnlyASpectator()))
            if(auto* Status=It->FindComponentByClass<UMCToothStatusComponent>()) CareBaselines.Add({Status,Status->CoffeeAmount()});
    StartedAt=Now();bFailed=false;DirtLeft=0;DirtTotal=0;JelliesLeft=0;DebrisSpawned=0;
    SetPhase(EMCCataclysmPhase::Warning);
    UE_LOG(LogTemp,Display,TEXT("MC_CATACLYSM START variant=%s drink=%s batch=%d"),
        Variant==EMCCataclysmVariant::Rafts?TEXT("rafts"):TEXT("jellies"),bCola?TEXT("cola"):TEXT("coffee"),DirtBatch);
    return true;
}
void AMCCataclysmEvent::SetPhase(EMCCataclysmPhase Next)
{
    Phase=Next;PhaseStartedAt=Now();ForceNetUpdate();
    UE_LOG(LogTemp,Display,TEXT("MC_CATACLYSM PHASE=%d jellies=%d dirt=%d"),int32(Next),JelliesLeft,DirtLeft);
}
bool AMCCataclysmEvent::StartWave(bool bDeep)
{
    if(IsValid(Liquid)) {Liquid->Stop();Liquid->Destroy();}
    FActorSpawnParameters Params;Params.Owner=this;
    Liquid=GetWorld()->SpawnActor<AMCCoffeeFlood>(Params);
    if(!Liquid) return false;
    Liquid->ResidueBatch=DirtBatch;
    Liquid->Start(DrinkPlan,0,bDeep);
    if(!Liquid->IsActive()) {Liquid->Destroy();Liquid=nullptr;return false;}
    if(bDeep) {
        Liquid->SetCataclysmDuration(FloodFillSeconds,FloodHoldSeconds,FloodDrainSeconds);
        Liquid->EnableCataclysmStorm(Random.RandRange(1,MAX_int32));
        NextDebrisAt=Now()+FMath::Max(2.f,FloodFillSeconds+1.f);
    }
    else {
        // A fast continuous incoming sheet is the common challenge, without enabling deep swimming yet.
        Liquid->RiverDepth=90.f; Liquid->WaterSettings.ImpactImpulse=680.f;
        Liquid->ForceNetUpdate();
    }
    for(int32 Index=0;Index<Rafts.Num();++Index) if(IsValid(Rafts[Index]) && RaftGroundAnchors.IsValidIndex(Index))
        Rafts[Index]->SetupForFlood(Liquid,RaftGroundAnchors[Index]);
    return true;
}
bool AMCCataclysmEvent::SpawnRafts()
{
    TArray<FVector> Placed;
    for(int32 Index=0;Index<2;++Index) {
        FHitResult Floor;bool Supported=false;
        for(int32 Attempt=0;Attempt<20 && !Supported;++Attempt) {
            // Players can still be in the green delivery strip after the wave.
            // Retain the legal footprint checks, but search the whole playable
            // bands when the limited worker-centered neighbourhood has no room.
            if(!Tongue->RandomGameplayInterestPoint(Random,195.f,420.f,Placed,Floor)
                && !Tongue->RandomGameplaySpawnPoint(Random,195.f,420.f,Placed,Floor)) continue;
            float Low=Floor.ImpactPoint.Z,High=Low;Supported=true;
            for(float X:{-90.f,90.f}) for(float Y:{-125.f,125.f}) {
                FHitResult Corner;
                if(!Tongue->SurfacePoint(Floor.ImpactPoint+FVector(X,Y,300),Corner)) {Supported=false;continue;}
                Low=FMath::Min(Low,float(Corner.ImpactPoint.Z));High=FMath::Max(High,float(Corner.ImpactPoint.Z));
            }
            Supported&=High-Low<40.f;
            if(Supported) {
                Floor.ImpactPoint.Z=High;
                // Check the whole hull and a small approach margin, not just
                // four floor contacts. The actual visual hull extends past Deck.
                FCollisionQueryParams Query(SCENE_QUERY_STAT(MCCataclysmRaftPlacement),false,Tongue);
                for(const auto& Existing:Rafts) if(IsValid(Existing)) Query.AddIgnoredActor(Existing);
                Supported=!GetWorld()->OverlapBlockingTestByChannel(Floor.ImpactPoint+FVector(0,0,57),FRotator(0,90,0).Quaternion(),
                    ECC_Pawn,FCollisionShape::MakeBox(FVector(174,133,34)),Query);
            }
        }
        if(!Supported) {UE_LOG(LogTemp,Warning,TEXT("MC_CATACLYSM RAFT_PLACEMENT_FAILED index=%d"),Index);return false;}
        FActorSpawnParameters Params;Params.Owner=this;
        auto* Raft=GetWorld()->SpawnActor<AMCCataclysmRaft>(Floor.ImpactPoint+FVector(0,0,21),FRotator(0,90,0),Params);
        if(!Raft) return false;
        Raft->SetupForFlood(Liquid,Floor.ImpactPoint);Rafts.Add(Raft);RaftGroundAnchors.Add(Floor.ImpactPoint);Placed.Add(Floor.ImpactPoint);
    }
    return Rafts.Num()==2;
}
bool AMCCataclysmEvent::SpawnJellies()
{
    int32 Players=0;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) {
        const auto* PlayerState=It->GetPlayerState();
        Players+=PlayerState && !PlayerState->IsOnlyASpectator() && !PlayerState->IsSpectator() && It->Status && It->Status->IsAlive();
    }
    const int32 Count=FMath::Clamp(Players+1,2,5);
    TArray<FVector> Placed;
    for(int32 Index=0;Index<Count;++Index) {
        FHitResult Floor;
        if(!Tongue->RandomGameplayInterestPoint(Random,100.f,240.f,Placed,Floor)
            && !Tongue->RandomGameplaySpawnPoint(Random,100.f,240.f,Placed,Floor)) continue;
        const FTransform Pose(Floor.ImpactPoint+FVector(0,0,90));
        auto* Jelly=GetWorld()->SpawnActorDeferred<AMCCataclysmJelly>(AMCCataclysmJelly::StaticClass(),Pose,this);
        if(!Jelly) continue;
        Jelly->ConfigureJelly(Tongue,FMath::Clamp(JellyHealth,80.f,1000.f));
        Jelly->OnDirtyPatchRequested.AddUObject(this,&AMCCataclysmEvent::RegisterDirtyPatch);
        Jelly->FinishSpawning(Pose);Jellies.Add(Jelly);Placed.Add(Floor.ImpactPoint);
    }
    JelliesLeft=Jellies.Num();return JelliesLeft>0;
}
void AMCCataclysmEvent::SeedDirt(int32 Count)
{
    for(int32 Index=0;Index<Count;++Index) {
        FHitResult Floor;
        if(Tongue->RandomGameplayInterestPoint(Random,220.f,0,TConstArrayView<FVector>(),Floor)
            || Tongue->RandomGameplaySpawnPoint(Random,220.f,0,TConstArrayView<FVector>(),Floor))
            RegisterDirtyPatch(Floor.ImpactPoint,150.f,false);
    }
}
void AMCCataclysmEvent::ClearFloatingDebris()
{
    for(AMCCataclysmDebris* Debris:FloatingDebris) if(IsValid(Debris)) Debris->StopDebris();
    FloatingDebris.Reset();
}
void AMCCataclysmEvent::UpdateFloatingDebris()
{
    FloatingDebris.RemoveAll([](const auto& Actor){return !IsValid(Actor) || Actor->IsActorBeingDestroyed();});
    if(!IsValid(Liquid) || !Liquid->IsActive() || !Liquid->IsCataclysmStorm()) {ClearFloatingDebris();return;}
    if(Liquid->GetPhase()==EMCCoffeePhase::Draining || HazardSecondsRemaining()<3.f || Now()<NextDebrisAt) return;
    NextDebrisAt=Now()+FMath::Lerp(3.8f,2.3f,Liquid->StormStrength())+Random.FRandRange(-.25f,.25f);
    if(FloatingDebris.Num()>=8) return;
    const FBox Bounds=Tongue->Surface->Bounds.GetBox();
    const FVector Direction=Liquid->StormStreamDirection();
    const FVector Side=FVector::CrossProduct(FVector::UpVector,Direction).GetSafeNormal();
    const float Span=FMath::Abs(Direction.X)*Bounds.GetExtent().X+FMath::Abs(Direction.Y)*Bounds.GetExtent().Y;
    const float Across=FMath::Abs(Side.X)*Bounds.GetExtent().X+FMath::Abs(Side.Y)*Bounds.GetExtent().Y;
    for(int32 Attempt=0;Attempt<16;++Attempt) {
        FVector Point=Bounds.GetCenter()-Direction*(Span*.66f)+Side*Random.FRandRange(-Across*.56f,Across*.56f);
        // Send some logs through a raft's lane. Trajectory stays downstream:
        // no invisible homing or abrupt course correction after emergence.
        if(Attempt<6 && !Rafts.IsEmpty()) if(auto* Raft=Rafts[Random.RandRange(0,Rafts.Num()-1)].Get();IsValid(Raft))
            Point+=Side*(FVector::DotProduct(Raft->GetActorLocation()-Point,Side)+Random.FRandRange(-110.f,110.f));
        Point.Z=Liquid->SurfaceHeightAt(Point)+28.f;
        bool Clear=true;
        for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
            if(It->Status && It->Status->IsAlive() && FVector::DistSquared2D(Point,It->GetActorLocation())<FMath::Square(650.f)) {Clear=false;break;}
        for(const auto& Raft:Rafts) if(IsValid(Raft) && FVector::DistSquared2D(Point,Raft->GetActorLocation())<FMath::Square(750.f)) Clear=false;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MCCataclysmDebrisSpawn),false,this);Query.AddIgnoredActor(Tongue);Query.AddIgnoredActor(Liquid);
        if(!Clear || !Liquid->Contains(Point-FVector(0,0,45)) || GetWorld()->OverlapBlockingTestByChannel(Point,Direction.ToOrientationQuat(),
            ECC_Pawn,FCollisionShape::MakeBox(FVector(100,40,40)),Query)) continue;
        const FTransform Pose(Direction.Rotation(),Point);
        auto* Debris=GetWorld()->SpawnActorDeferred<AMCCataclysmDebris>(AMCCataclysmDebris::StaticClass(),Pose,this);
        if(!Debris) continue;
        Debris->ConfigureDebris(Liquid,Tongue,Random.RandRange(1,MAX_int32));Debris->FinishSpawning(Pose);
        FloatingDebris.Add(Debris);++DebrisSpawned;return;
    }
}
void AMCCataclysmEvent::RegisterDirtyPatch(FVector Center,float Radius,bool bDeathBurst)
{
    if(!HasAuthority() || !IsRunning() || !Tongue || Center.ContainsNaN() || !FMath::IsFinite(Radius)) return;
    float Half=FMath::Clamp(Radius,65.f,300.f);
    FHitResult Floor;bool Found=false;
    for(int32 Attempt=0;Attempt<4 && !Found;++Attempt) {
        Found=Tongue->GameplaySpawnFootprint(Center,Half*1.415f+12.f,Floor);
        if(!Found) Half*=.72f;
    }
    if(!Found || Tongue->GameplaySpawnZone(Floor.ImpactPoint)<0) return;
    TArray<AMCMouthSurface*> Patches;
    AMCMouthSurface* Nearest=nullptr;float Best=MAX_flt;
    for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if(It->Batch==DirtBatch && !It->bUlcer) {
        Patches.Add(*It);const float Distance=FVector::DistSquared2D(It->GetActorLocation(),Floor.ImpactPoint);
        if(Distance<Best) {Best=Distance;Nearest=*It;}
    }
    if(Nearest && Best<FMath::Square(FMath::Max(90.f,Half))) {
        // One source can re-dirty the same footprint; it cannot create an unbounded actor trail.
        Nearest->ResetLiquid();Nearest->Status->ApplyCoffee(1);Nearest->ForceNetUpdate();return;
    }
    if(Patches.Num()>=MaxDirtPatches) return;
    const FTransform Pose(FRotationMatrix::MakeFromZX(Floor.ImpactNormal,FVector::ForwardVector).ToQuat(),Floor.ImpactPoint+Floor.ImpactNormal*5);
    auto* Patch=GetWorld()->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),Pose,this);
    if(!Patch) return;
    Patch->bRandomizeLiquidSize=false;Patch->LiquidHalfSize=Half;Patch->Batch=DirtBatch;
    Patch->LiquidSeed=Random.RandRange(1,MAX_int32);Patch->Tags.AddUnique(TEXT("MCCataclysmDirt"));
    Patch->FinishSpawning(Pose);Patch->Status->ApplyCoffee(1);Patch->ForceNetUpdate();
    if(bDeathBurst) UE_LOG(LogTemp,Display,TEXT("MC_CATACLYSM JELLY_SPLAT radius=%.1f patch=%s"),Half,*Patch->GetName());
}
void AMCCataclysmEvent::ObserveConsequences()
{
    JelliesLeft=0;for(AMCCataclysmJelly* Jelly:Jellies) if(IsValid(Jelly) && Jelly->IsEncounterAlive()) ++JelliesLeft;
    DirtLeft=0;
    for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if(It->Batch==DirtBatch && !It->bUlcer && !It->IsClean()) ++DirtLeft;
    for(const auto& Base:CareBaselines) if(auto* Status=Base.Status.Get();Status && Status->IsAlive()) {
        if(const auto* Tooth=Cast<AMCArenaTooth>(Status->GetOwner());Tooth && !Tooth->IsAvailable()) continue;
        if(Status->CoffeeAmount()>Base.Amount+.01f) ++DirtLeft;
    }
    DirtTotal=FMath::Max(DirtTotal,DirtLeft);ForceNetUpdate();
}
void AMCCataclysmEvent::Tick(float Dt)
{
    Super::Tick(Dt);if(!HasAuthority()) return;
    if(IsComplete()) {
        // A completed refuge is retired only when its last rider has stepped off.
        for(int32 Index=Rafts.Num()-1;Index>=0;--Index)
            if(!IsValid(Rafts[Index]) || Rafts[Index]->OccupantCount==0) {
                if(IsValid(Rafts[Index])) Rafts[Index]->Destroy();
                Rafts.RemoveAt(Index);if(RaftGroundAnchors.IsValidIndex(Index)) RaftGroundAnchors.RemoveAt(Index);
            }
        return;
    }
    if(!IsRunning()) return;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    if(GS && (GS->Phase==EMCShiftPhase::Lost || GS->Phase==EMCShiftPhase::Won)) {Stop();return;}
    const double Age=Now()-PhaseStartedAt;
    if(Phase==EMCCataclysmPhase::Warning && Age>=WarningSeconds) {
        if(!StartWave(false)) {bFailed=true;Stop();return;}
        SetPhase(EMCCataclysmPhase::FirstWave);
    } else if(Phase==EMCCataclysmPhase::FirstWave && (!IsValid(Liquid) || !Liquid->IsActive())) {
        if(Variant==EMCCataclysmVariant::Rafts) {
            if(!SpawnRafts()) {bFailed=true;Stop();return;}
            SetPhase(EMCCataclysmPhase::BranchWarning);
        } else {
            SeedDirt(4);
            if(!SpawnJellies()) {bFailed=true;Stop();return;}
            SetPhase(EMCCataclysmPhase::JellyFight);
        }
    } else if(Phase==EMCCataclysmPhase::BranchWarning && Age>=WarningSeconds) {
        if(!StartWave(true)) {bFailed=true;Stop();return;}
        SetPhase(EMCCataclysmPhase::Flooding);
    } else if(Phase==EMCCataclysmPhase::Flooding && (!IsValid(Liquid) || !Liquid->IsActive())) {
        ClearFloatingDebris();
        // No floating platform disappears under a player. The raft actor beaches against the tongue.
        SetPhase(EMCCataclysmPhase::Cleanup);
    }
    if(Phase==EMCCataclysmPhase::Flooding) UpdateFloatingDebris();
    if(Now()>=NextObserveAt) {
        NextObserveAt=Now()+.25;ObserveConsequences();
        if(Phase==EMCCataclysmPhase::JellyFight && JelliesLeft==0) SetPhase(EMCCataclysmPhase::Cleanup);
        if(Phase==EMCCataclysmPhase::Cleanup && DirtLeft==0) {
            SetPhase(EMCCataclysmPhase::Complete);
            // Beached rafts stay until their riders step off; they are safe scenery after completion.
            if(IsValid(Liquid)) {Liquid->Stop();Liquid->Destroy();Liquid=nullptr;}
        }
        PublishManualStatus();
    }
}
float AMCCataclysmEvent::HazardSecondsRemaining() const
{
    return Phase==EMCCataclysmPhase::Flooding && IsValid(Liquid)?FMath::Max(0.f,float(Liquid->StartedAt+Liquid->Seconds-Now())):0;
}
FString AMCCataclysmEvent::Title() const
{
    if(Phase==EMCCataclysmPhase::Complete) return TEXT("КАТАКЛИЗМ ЗАВЕРШЁН");
    if(Phase==EMCCataclysmPhase::Cleanup) return TEXT("КАТАКЛИЗМ · УБОРКА");
    if(Phase==EMCCataclysmPhase::JellyFight) return TEXT("ГРЯЗНОЕ ЖЕЛЕ");
    if(Phase==EMCCataclysmPhase::Flooding || Phase==EMCCataclysmPhase::BranchWarning) return TEXT("ЗАТОПЛЕНИЕ · ДВА ПЛОТА");
    return bCola?TEXT("КАТАКЛИЗМ · ВОЛНА КОЛЫ"):TEXT("КАТАКЛИЗМ · КОФЕЙНАЯ ВОЛНА");
}
FString AMCCataclysmEvent::Instruction() const
{
    if(Phase==EMCCataclysmPhase::Cleanup) return TEXT("Очистите пятна, затронутые зубы и себя щёткой. C + ЛКМ — самоочистка.");
    if(Phase==EMCCataclysmPhase::JellyFight) return TEXT("Убейте живучих желе, затем закончите уборку. Перед смертью желе раздувается и пачкает область вокруг.");
    if(Phase==EMCCataclysmPhase::Flooding || Phase==EMCCataclysmPhase::BranchWarning)
        return TEXT("По двое на плот. Щётка + ЛКМ — грести по направлению камеры; WASD — ходить. Гребите против течения и компенсируйте вращение. ПКМ — поручень / залезть. Берегитесь мусора!");
    if(Phase==EMCCataclysmPhase::Complete) return TEXT("Источники грязи устранены, последствия убраны.");
    return TEXT("Берегитесь первой волны: она сносит и пачкает игроков. Директор выбрал один вариант катаклизма.");
}
void AMCCataclysmEvent::PublishManualStatus()
{
    auto* GS=GetWorld()->GetGameState<AMCGameState>();if(!GS || !GS->bDevManualEvents) return;
    GS->DirectorState.CurrentTitle=Title();GS->DirectorState.Instruction=Instruction();
    GS->TasksLeft=DirtLeft+JelliesLeft+(Phase==EMCCataclysmPhase::Flooding?1:0);
    GS->TasksTotal=FMath::Max(GS->TasksTotal,GS->TasksLeft);GS->ForceNetUpdate();
}
void AMCCataclysmEvent::RemoveHazardActors()
{
    ClearFloatingDebris();
    if(IsValid(Liquid)) {Liquid->Stop();Liquid->Destroy();} Liquid=nullptr;
    for(AMCCataclysmJelly* Jelly:Jellies) if(IsValid(Jelly)) {Jelly->OnDirtyPatchRequested.RemoveAll(this);Jelly->Destroy();} Jellies.Reset();
    for(AMCCataclysmRaft* Raft:Rafts) if(IsValid(Raft)) Raft->Destroy();Rafts.Reset();RaftGroundAnchors.Reset();
}
void AMCCataclysmEvent::Stop(bool bClearEventDirt)
{
    if(!HasAuthority()) return;
    RemoveHazardActors();
    if(bClearEventDirt) {
        TArray<AMCMouthSurface*> Patches;
        for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if(It->Batch==DirtBatch && !It->bUlcer) Patches.Add(*It);
        for(auto* Patch:Patches) Patch->Destroy();
    }
    if(!IsComplete()) SetPhase(EMCCataclysmPhase::Stopped);
}
void AMCCataclysmEvent::EndPlay(const EEndPlayReason::Type Reason)
{
    if(HasAuthority()) RemoveHazardActors();Super::EndPlay(Reason);
}
void AMCCataclysmEvent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCCataclysmEvent,Variant);DOREPLIFETIME(AMCCataclysmEvent,Phase);DOREPLIFETIME(AMCCataclysmEvent,bCola);
    DOREPLIFETIME(AMCCataclysmEvent,bFailed);DOREPLIFETIME(AMCCataclysmEvent,StartedAt);DOREPLIFETIME(AMCCataclysmEvent,PhaseStartedAt);
    DOREPLIFETIME(AMCCataclysmEvent,DirtBatch);DOREPLIFETIME(AMCCataclysmEvent,JelliesLeft);DOREPLIFETIME(AMCCataclysmEvent,DirtLeft);
    DOREPLIFETIME(AMCCataclysmEvent,DirtTotal);DOREPLIFETIME(AMCCataclysmEvent,Liquid);DOREPLIFETIME(AMCCataclysmEvent,Rafts);DOREPLIFETIME(AMCCataclysmEvent,Jellies);
}
