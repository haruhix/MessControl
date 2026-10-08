#include "MCNutRainEvent.h"

#include "MCFoodActor.h"
#include "MCFoodBodyComponent.h"
#include "MCGameState.h"
#include "MCNutEnemy.h"
#include "MCNutBoss.h"
#include "MCNutLandingShadow.h"
#include "MCSingleDayDirector.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

AMCNutRainEvent::AMCNutRainEvent()
{
    bReplicates=true; bAlwaysRelevant=true; SetNetUpdateFrequency(5);
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.bStartWithTickEnabled=false;
    PrimaryActorTick.TickInterval=.05f;
}

void AMCNutRainEvent::Start(UMCDayPlan* Plan,UMCNutRainProfile* Profile)
{
    if(!HasAuthority()) return;
    Stop(); bFailed=false; NutsSpawned=EnemiesLeft=DropsAttempted=0;
    ActiveProfile=Profile?Profile:LoadObject<UMCNutRainProfile>(nullptr,TEXT("/Game/Gameplay/CoreLoop/DA_NutRain.DA_NutRain"));
    if(!ActiveProfile) ActiveProfile=NewObject<UMCNutRainProfile>(this);
    Settings=ActiveProfile->Settings; Settings.Sanitize();
    int32 Players=0;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        if(It->Status && It->Status->IsAlive()) ++Players;
    PartyPlayers=FMath::Clamp(Players,1,8);
    RainTargetCount=Settings.RainCountForPlayers(Players); EnemyTargetCount=Settings.EnemyCountForPlayers(Players);
    if(Settings.bBossEncounter) EnemyTargetCount=FMath::Min(EnemyTargetCount,Settings.Boss.MaxLiveCreeps);
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    Random.Initialize(int32(uint32(State?State->RunSeed:41)^0x4E555453u)); FoodBatch=10000+int32(GetUniqueID());
    // Irregular bursts and quiet gaps share the same bounded count and duration.
    // Normalize the intervals so the final launch still ends the authored rain.
    if(!Settings.bBossEncounter) {
    PlannedLaunchTimes.Add(0); float Total=0;
    for(int32 Index=1;Index<RainTargetCount;++Index) {
        Total+=Random.FRand()<.65f?Random.FRandRange(.12f,.45f):Random.FRandRange(.85f,2.4f);
        PlannedLaunchTimes.Add(Total);
    }
    if(Total>0) for(float& Time:PlannedLaunchTimes) Time=Time/Total*Settings.RainSeconds;
    }
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It)
        if(!It->CurrentVertices().IsEmpty()) { Tongue=*It; break; }
    NutRowName=ActiveProfile->NutRow; NutRow=FMCFoodRow();
    bool FoundRow=false;
    if(UDataTable* Menu=ActiveProfile->Menu.LoadSynchronous())
        if(const auto* Row=Menu->FindRow<FMCFoodRow>(NutRowName,TEXT("Nut cataclysm"))) { NutRow=*Row; FoundRow=true; }
    if(!FoundRow)
    {
        NutRow.Label=NSLOCTEXT("NutRain","Nut","WALNUT");
        NutRow.WholeMeshes={Settings.Boss.WholeMesh};
        if(UStaticMesh* Mesh=NutRow.WholeMeshes[0].LoadSynchronous())
            NutRow.Scale=FVector(Settings.Enemy.BodyRadius/FMath::Max(1.,Mesh->GetBounds().BoxExtent.GetMax()));
        NutRow.FragmentMeshes={Settings.Boss.ShellMesh,Settings.Boss.KernelMesh}; NutRow.FragmentScale=NutRow.Scale*.55;
        NutRow.Resistance=EMCFoodResistance::Hard; NutRow.Health=90; NutRow.Mass=7; NutRow.SpoilSeconds=600;
    }
    if(Settings.bBossEncounter && !NutRow.WholeMeshes.IsEmpty())
        if(const UStaticMesh* Mesh=NutRow.WholeMeshes[0].LoadSynchronous())
            NutRow.Scale=FVector(Settings.LargeNutHeight/FMath::Max(.001,Mesh->GetBounds().BoxExtent.Z*2));
    NutRow.Sanitize();
    if(!IsValid(Tongue))
    {
        bFailed=true; Enter(EMCNutRainStage::Complete);
        UE_LOG(LogTemp,Error,TEXT("MC_NUT_RAIN_FAILED no live tongue surface, plan=%s"),*GetNameSafe(Plan));
        return;
    }
    SetActorTickEnabled(true); Enter(EMCNutRainStage::Rainfall);
    if(Settings.bBossEncounter) BeginSeries(); else SpawnNut();
    Record(FString::Printf(TEXT("Орехи: дождь %.0f с, минимум %d очереди, по %d–%d ореха; другие события закрыты"),Settings.RainSeconds,Settings.MinimumSeries,Settings.MinimumNutsPerSeries,Settings.MaximumNutsPerSeries));
    UE_LOG(LogTemp,Display,TEXT("MC_NUT_RAIN_START players=%d target=%d enemies=%d seconds=%.1f"),Players,RainTargetCount,EnemyTargetCount,Settings.RainSeconds);
}

void AMCNutRainEvent::Record(const FString& Decision)
{
    if(auto* State=GetWorld()->GetGameState<AMCGameState>()) State->RecordDirectorDecision(Decision);
}

void AMCNutRainEvent::TrackEncounterActor(AActor* Actor)
{
    if(HasAuthority() && IsValid(Actor)) {
        EncounterActors.RemoveAll([](const auto& Item) { return !IsValid(Item.Get()) || Item->IsActorBeingDestroyed(); });
        EncounterActors.AddUnique(Actor);
    }
}

void AMCNutRainEvent::RegisterEncounterEnemy(AMCNutEnemy* Enemy)
{
    if(!HasAuthority() || !IsValid(Enemy)) return;
    Enemies.RemoveAll([](const auto& Item) { return !IsValid(Item.Get()) || Item->IsActorBeingDestroyed(); });
    Enemies.AddUnique(Enemy); TrackEncounterActor(Enemy);
    EnemiesLeft=0; for(const AMCNutEnemy* Candidate:Enemies) if(IsValid(Candidate) && Candidate->IsEncounterAlive()) ++EnemiesLeft;
    ForceNetUpdate();
}

void AMCNutRainEvent::BeginSeries()
{
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    const auto* Director=State?State->SingleDayDirector.Get():nullptr;
    SeriesDropsLeft=Director?Director->ChooseNutSeriesSize(Settings.MinimumNutsPerSeries,Settings.MaximumNutsPerSeries,CompletedSeries)
        :Random.RandRange(Settings.MinimumNutsPerSeries,Settings.MaximumNutsPerSeries);
    ++SeriesIndex; ClusterDropsLeft=0;
    Record(FString::Printf(TEXT("Орехи: очередь %d, %d падения; пауза %.0f с после очереди"),SeriesIndex,SeriesDropsLeft,Settings.SeriesRestSeconds));
    const double Time=GetWorld()->GetTimeSeconds();
    PlannedLaunchTimes.Add(float(Time-StageStartedAt)); SpawnNut(); --SeriesDropsLeft;
    NextDropAt=Time+Random.FRandRange(Settings.MinimumDropSeconds,Settings.MaximumDropSeconds);
    ForceNetUpdate();
}

void AMCNutRainEvent::Enter(EMCNutRainStage Next)
{
    Stage=Next; StageStartedAt=GetWorld()->GetTimeSeconds(); ForceNetUpdate();
}

void AMCNutRainEvent::SpawnNut()
{
    ++DropsAttempted;
    if(!IsValid(Tongue)) return;
    FTransform Pose(FRotator(0,Random.FRandRange(-180,180),0));
    auto* Food=GetWorld()->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Food) return;
    Food->ConfigureItem(NutRowName,NutRow,Random); Food->Batch=FoodBatch; Food->Tags.AddUnique(TEXT("MCNutRain"));
    const auto* Collision=Cast<UMCFoodBodyComponent>(Food->Body);
    if(!Collision || !Collision->HasMeshCollision())
    {
        Food->Destroy(); bFailed=true;
        UE_LOG(LogTemp,Error,TEXT("MC_NUT_RAIN_FAILED walnut needs baked food collision; run author_nut_rain.py"));
        return;
    }
    const float Margin=FMath::Max(float(Food->Body->GetScaledBoxExtent().Size2D()+18),Settings.ImpactRadius);
    FHitResult Floor;
    if(!ChooseLanding(Margin,Floor)) { Food->Destroy(); return; }
    const FVector Landing=Floor.ImpactPoint+FVector(0,0,Food->Body->GetScaledBoxExtent().Z+5);
    FVector Start,Velocity;
    if(!Settings.Entry.BuildTrajectory(Tongue->Surface->Bounds.GetBox(),Landing,GetWorld()->GetGravityZ(),Start,Velocity)) { Food->Destroy(); return; }
    const FBox Bounds=Tongue->Surface->Bounds.GetBox();
    const float Angle=Random.FRandRange(-PI,PI),Outside=Settings.Entry.OutsideDistance*Random.FRandRange(.5f,1.5f);
    Start.X=Bounds.GetCenter().X+FMath::Cos(Angle)*(Bounds.GetExtent().X+Outside);
    Start.Y=Bounds.GetCenter().Y+FMath::Sin(Angle)*(Bounds.GetExtent().Y+Outside);
    Start.Z=FMath::Max(Landing.Z,Bounds.Max.Z)+FMath::Clamp(Settings.Entry.EntryHeight*Random.FRandRange(.75f,1.2f),150.f,900.f);
    Velocity=(Landing-Start-FVector(0,0,GetWorld()->GetGravityZ())*(.5f*Settings.Entry.FlightSeconds*Settings.Entry.FlightSeconds))/Settings.Entry.FlightSeconds;
    Pose.SetLocation(Start); Food->FinishSpawning(Pose);
    if(Settings.bBossEncounter) Food->Settings.MaxDamage=Settings.ImpactDamageLimit;
    const FTransform ShadowPose(Floor.ImpactPoint);
    if(auto* Shadow=GetWorld()->SpawnActorDeferred<AMCNutLandingShadow>(AMCNutLandingShadow::StaticClass(),ShadowPose,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn)) {
        Shadow->Configure(Tongue,Food,Floor.ImpactPoint,Settings.Entry.FlightSeconds,Settings.ImpactRadius,Settings.LandingShadowOpacity,
            Settings.ImpactDamageLimit,Settings.ImpactPushSpeed);
        Shadow->FinishSpawning(ShadowPose); LandingShadows.Add(Shadow);
    } else {
        Food->Destroy(); bFailed=true; return;
    }
    Food->BeginMouthEntry(Velocity,Settings.Entry.PushSpeed,Landing);
    PlannedLandings.Add(Landing); LaunchLocations.Add(Start);
    Nuts.Add(Food); ++NutsSpawned; ForceNetUpdate();
}

bool AMCNutRainEvent::ChooseLanding(float Margin,FHitResult& Floor)
{
    if(ClusterDropsLeft<=0) {
        if(!Tongue->RandomGameplaySpawnPoint(Random,Margin,0,TConstArrayView<FVector>(),Floor)) return false;
        ClusterCenter=Floor.ImpactPoint; ClusterDropsLeft=Random.RandRange(2,6);
    }
    --ClusterDropsLeft;
    for(int32 Attempt=0;Attempt<24;++Attempt) {
        const float Angle=Random.FRandRange(-PI,PI);
        const float Distance=Settings.ClusterRadius*(Random.FRand()+Random.FRand())*.5f;
        const FVector Candidate=ClusterCenter+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Distance;
        if(Tongue->GameplaySpawnFootprint(Candidate,Margin,Floor)) return true;
    }
    // A clipped patch can move to a new valid patch; the delivery lanes remain excluded.
    ClusterDropsLeft=0;
    return Tongue->RandomGameplaySpawnPoint(Random,Margin,0,TConstArrayView<FVector>(),Floor);
}

void AMCNutRainEvent::AwakenLastNuts()
{
    // Removing/collecting nuts during the rain reduces the aftermath. Never turn
    // food already held by a player, swallowed, broken or disposed into an enemy.
    for(int32 Index=Nuts.Num()-1;Index>=0 && Enemies.Num()<EnemyTargetCount;--Index)
    {
        AMCFoodActor* Food=Nuts[Index];
        if(!IsValid(Food) || Food->IsActorBeingDestroyed() || Food->IsDisposed() || Food->bFragment || Food->StackCarrier
            || !Food->Holders.IsEmpty() || Food->IsMouthEntryActive()
            || (Food->Phase!=EMCFoodPhase::Free && Food->Phase!=EMCFoodPhase::Stuck)) continue;
        FHitResult Floor;
        if(!IsValid(Tongue) || !Tongue->InteriorSurfacePoint(Food->GetActorLocation(),Settings.Enemy.BodyRadius+5,Floor)) continue;
        const FTransform Pose(FRotator::ZeroRotator,Floor.ImpactPoint+FVector(0,0,Settings.Enemy.BodyRadius+4));
        auto* Enemy=GetWorld()->SpawnActorDeferred<AMCNutEnemy>(AMCNutEnemy::StaticClass(),Pose,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Enemy) continue;
        Enemy->ConfigureFromFood(Food,Tongue,Settings.Enemy); Enemy->FinishSpawning(Pose);
        Enemies.Add(Enemy); Food->Dispose();
    }
    EnemiesLeft=Enemies.Num(); Enter(EnemiesLeft>0?EMCNutRainStage::Enemies:EMCNutRainStage::Complete);
    if(Stage==EMCNutRainStage::Complete) SetActorTickEnabled(false);
    UE_LOG(LogTemp,Display,TEXT("MC_NUT_RAIN_AWAKEN spawned=%d enemies=%d target=%d"),NutsSpawned,EnemiesLeft,EnemyTargetCount);
}

void AMCNutRainEvent::SpawnBossEncounter()
{
    if(!IsValid(Tongue)) { bFailed=true; Enter(EMCNutRainStage::Complete); return; }
    Enter(EMCNutRainStage::Enemies);
    TArray<FVector> Occupied;
    for(int32 Index=0;Index<2;++Index) {
        const EMCNutBossRole EncounterRole=Index==0?EMCNutBossRole::Tank:EMCNutBossRole::Mage;
        const float Radius=EncounterRole==EMCNutBossRole::Tank?100.f:90.f;
        FHitResult Floor;
        if(!Tongue->RandomGameplaySpawnPoint(Random,Radius+20,340,Occupied,Floor)) { bFailed=true; break; }
        const FTransform Pose(FRotator(0,Random.FRandRange(-180,180),0),Floor.ImpactPoint+FVector(0,0,Radius+4));
        auto* Boss=GetWorld()->SpawnActorDeferred<AMCNutBoss>(AMCNutBoss::StaticClass(),Pose,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Boss) { bFailed=true; break; }
        Boss->ConfigureEncounter(Tongue,this,EncounterRole,Settings.Boss,PartyPlayers,Random.RandHelper(MAX_int32));
        FTransform AirbornePose=Pose; AirbornePose.SetLocation(Boss->LockedStart);
        Boss->FinishSpawning(AirbornePose); Bosses.Add(Boss); RegisterEncounterEnemy(Boss); Occupied.Add(Floor.ImpactPoint);
    }
    if(bFailed || Bosses.Num()!=2) {
        bFailed=true; Record(TEXT("Орехи: не удалось разместить двух боссов; этап остановлен"));
        Enter(EMCNutRainStage::Complete); SetActorTickEnabled(false); return;
    }
    UStaticMesh* Kernel=Settings.Boss.KernelMesh.LoadSynchronous();
    if(!Kernel) { bFailed=true; Enter(EMCNutRainStage::Complete); SetActorTickEnabled(false); return; }
    const FVector Scale(Settings.Boss.CreepHeight/FMath::Max(.001,Kernel->GetBounds().BoxExtent.Z*2));
    FMCNutEnemySettings Creep=Settings.Enemy; Creep.MaxHealth=Settings.Boss.CreepHealth; Creep.Sanitize();
    for(int32 Index=0;Index<EnemyTargetCount;++Index) {
        FHitResult Floor; if(!Tongue->RandomGameplaySpawnPoint(Random,Creep.BodyRadius+10,80,Occupied,Floor)) continue;
        const FTransform Pose(Floor.ImpactPoint+FVector(0,0,Creep.BodyRadius+4));
        auto* Enemy=GetWorld()->SpawnActorDeferred<AMCNutEnemy>(AMCNutEnemy::StaticClass(),Pose,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Enemy) continue;
        Enemy->ConfigureEnemy(Tongue,Kernel,Scale,Creep); Enemy->FinishSpawning(Pose); RegisterEncounterEnemy(Enemy); Occupied.Add(Floor.ImpactPoint);
    }
    BossesLeft=2;
    Record(FString::Printf(TEXT("Орехи: %d очереди; высажены танк, маг и %d крипов. Переход ждёт победы"),CompletedSeries,EnemiesLeft-2));
    UE_LOG(LogTemp,Display,TEXT("MC_NUT_ENCOUNTER_START series=%d nuts=%d bosses=2 creeps=%d"),CompletedSeries,NutsSpawned,EnemiesLeft-2);
}

void AMCNutRainEvent::Tick(float Dt)
{
    Super::Tick(Dt);
    if(!HasAuthority() || Stage==EMCNutRainStage::Idle || Stage==EMCNutRainStage::Complete) return;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    if(GS && (GS->Phase==EMCShiftPhase::Won || GS->Phase==EMCShiftPhase::Lost)) { Stop(); return; }
    const double Age=GetWorld()->GetTimeSeconds()-StageStartedAt;
    if(Stage==EMCNutRainStage::Rainfall)
    {
        if(Settings.bBossEncounter) {
            const double Time=GetWorld()->GetTimeSeconds();
            if(SeriesDropsLeft>0 && Time>=NextDropAt) {
                PlannedLaunchTimes.Add(float(Time-StageStartedAt)); SpawnNut(); --SeriesDropsLeft;
                if(SeriesDropsLeft>0) NextDropAt=Time+Random.FRandRange(Settings.MinimumDropSeconds,Settings.MaximumDropSeconds);
                else { ++CompletedSeries; NextDropAt=Time+Settings.SeriesRestSeconds; ForceNetUpdate(); }
            }
            if(bFailed) { Enter(EMCNutRainStage::Complete); SetActorTickEnabled(false); return; }
            if(SeriesDropsLeft==0 && Time>=NextDropAt) {
                const auto* Director=GS?GS->SingleDayDirector.Get():nullptr;
                const bool Finish=Director?Director->ShouldFinishNutRain(CompletedSeries,Settings.MinimumSeries,float(Age),Settings.RainSeconds)
                    :CompletedSeries>=Settings.MinimumSeries && Age>=Settings.RainSeconds;
                if(Finish || CompletedSeries>=Settings.MaximumSeries && Age>=Settings.RainSeconds) {
                    if(NutsSpawned==0) { bFailed=true; Enter(EMCNutRainStage::Complete); SetActorTickEnabled(false); return; }
                    RainTargetCount=NutsSpawned; Enter(EMCNutRainStage::Settling);
                } else if(CompletedSeries<Settings.MaximumSeries) BeginSeries();
            }
            return;
        }
        // Bounded emission keeps a slow frame from creating the entire storm at once.
        for(int32 Burst=0;DropsAttempted<PlannedLaunchTimes.Num() && Age>=PlannedLaunchTimes[DropsAttempted] && Burst<4;++Burst) SpawnNut();
        if(bFailed) { Enter(EMCNutRainStage::Complete); SetActorTickEnabled(false); return; }
        if(Age>=Settings.RainSeconds && DropsAttempted>=RainTargetCount)
        {
            if(NutsSpawned==0) bFailed=true;
            Enter(EMCNutRainStage::Settling);
        }
    }
    else if(Stage==EMCNutRainStage::Settling && Age>=Settings.SettleSeconds) {
        if(Settings.bBossEncounter) SpawnBossEncounter(); else AwakenLastNuts();
    }
    else if(Stage==EMCNutRainStage::Enemies)
    {
        int32 Alive=0;
        int32 AliveBosses=0;
        for(AMCNutEnemy* Enemy:Enemies) if(IsValid(Enemy) && Enemy->IsEncounterAlive()) ++Alive;
        for(const AMCNutBoss* Boss:Bosses) if(IsValid(Boss) && Boss->IsEncounterAlive()) ++AliveBosses;
        if(BossesLeft!=AliveBosses) { BossesLeft=AliveBosses; Record(FString::Printf(TEXT("Орехи: боссов осталось %d"),BossesLeft)); ForceNetUpdate(); }
        if(EnemiesLeft!=Alive) { EnemiesLeft=Alive; ForceNetUpdate(); }
        if(EnemiesLeft==0) { Record(TEXT("Орехи: оба босса и все крипы побеждены; директор снова управляет промежутком")); Enter(EMCNutRainStage::Complete); SetActorTickEnabled(false); }
    }
}

void AMCNutRainEvent::Stop()
{
    if(!HasAuthority()) return;
    SetActorTickEnabled(false);
    for(AMCNutEnemy* Enemy:Enemies) if(IsValid(Enemy)) Enemy->Destroy();
    for(AActor* Actor:EncounterActors) if(IsValid(Actor) && !Actor->IsActorBeingDestroyed()) Actor->Destroy();
    for(AMCNutLandingShadow* Shadow:LandingShadows) if(IsValid(Shadow)) Shadow->Destroy();
    // Broken whole nuts spawn ordinary AMCFoodActor fragments with the same Batch.
    // Track that batch as well as the original actors so retries cannot leak food.
    if(FoodBatch!=0) for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
        if(It->Batch==FoodBatch) It->Destroy();
    Nuts.Reset(); Enemies.Reset(); Bosses.Reset(); EncounterActors.Reset(); LandingShadows.Reset(); PlannedLaunchTimes.Reset(); PlannedLandings.Reset(); LaunchLocations.Reset();
    ClusterDropsLeft=0; Tongue=nullptr; ActiveProfile=nullptr;
    EnemiesLeft=0; Stage=EMCNutRainStage::Idle; FoodBatch=0; ForceNetUpdate();
    SeriesIndex=CompletedSeries=SeriesDropsLeft=BossesLeft=0; NextDropAt=0;
}

void AMCNutRainEvent::EndPlay(const EEndPlayReason::Type Reason)
{
    Stop(); Super::EndPlay(Reason);
}

void AMCNutRainEvent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCNutRainEvent,Stage); DOREPLIFETIME(AMCNutRainEvent,StageStartedAt);
    DOREPLIFETIME(AMCNutRainEvent,NutsSpawned); DOREPLIFETIME(AMCNutRainEvent,EnemiesLeft);
    DOREPLIFETIME(AMCNutRainEvent,RainTargetCount); DOREPLIFETIME(AMCNutRainEvent,EnemyTargetCount);
    DOREPLIFETIME(AMCNutRainEvent,bFailed); DOREPLIFETIME(AMCNutRainEvent,Settings);
    DOREPLIFETIME(AMCNutRainEvent,SeriesIndex); DOREPLIFETIME(AMCNutRainEvent,CompletedSeries);
    DOREPLIFETIME(AMCNutRainEvent,SeriesDropsLeft); DOREPLIFETIME(AMCNutRainEvent,BossesLeft); DOREPLIFETIME(AMCNutRainEvent,NextDropAt);
}
