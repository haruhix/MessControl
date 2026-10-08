#include "MCNutRainEvent.h"

#include "MCFoodActor.h"
#include "MCFoodBodyComponent.h"
#include "MCGameState.h"
#include "MCNutEnemy.h"
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
    RainTargetCount=Settings.RainCountForPlayers(Players); EnemyTargetCount=Settings.EnemyCountForPlayers(Players);
    Random.Initialize(FMath::Rand()); FoodBatch=10000+int32(GetUniqueID());
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It)
        if(!It->CurrentVertices().IsEmpty()) { Tongue=*It; break; }
    NutRowName=ActiveProfile->NutRow; NutRow=FMCFoodRow();
    bool FoundRow=false;
    if(UDataTable* Menu=ActiveProfile->Menu.LoadSynchronous())
        if(const auto* Row=Menu->FindRow<FMCFoodRow>(NutRowName,TEXT("Nut cataclysm"))) { NutRow=*Row; FoundRow=true; }
    if(!FoundRow)
    {
        NutRow.Label=NSLOCTEXT("NutRain","Nut","WALNUT");
        NutRow.WholeMeshes={TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Fab/Stylized_Walnut_-_Game_Ready/stylized_walnut_game_ready/StaticMeshes/stylized_walnut_game_ready.stylized_walnut_game_ready")))};
        if(UStaticMesh* Mesh=NutRow.WholeMeshes[0].LoadSynchronous())
            NutRow.Scale=FVector(Settings.Enemy.BodyRadius/FMath::Max(1.,Mesh->GetBounds().BoxExtent.GetMax()));
        NutRow.FragmentMeshes=NutRow.WholeMeshes; NutRow.FragmentScale=NutRow.Scale*.55;
        NutRow.Resistance=EMCFoodResistance::Hard; NutRow.Health=90; NutRow.Mass=7; NutRow.SpoilSeconds=600;
    }
    NutRow.Sanitize();
    if(!IsValid(Tongue))
    {
        bFailed=true; Enter(EMCNutRainStage::Complete);
        UE_LOG(LogTemp,Error,TEXT("MC_NUT_RAIN_FAILED no live tongue surface, plan=%s"),*GetNameSafe(Plan));
        return;
    }
    SetActorTickEnabled(true); Enter(EMCNutRainStage::Rainfall); SpawnNut();
    UE_LOG(LogTemp,Display,TEXT("MC_NUT_RAIN_START players=%d target=%d enemies=%d seconds=%.1f"),Players,RainTargetCount,EnemyTargetCount,Settings.RainSeconds);
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
    const float Margin=Food->Body->GetScaledBoxExtent().Size2D()+18;
    FHitResult Floor;
    if(!Tongue->RandomGameplaySpawnPoint(Random,Margin,Margin*2+25,TConstArrayView<FVector>(),Floor)) { Food->Destroy(); return; }
    const FVector Landing=Floor.ImpactPoint+FVector(0,0,Food->Body->GetScaledBoxExtent().Z+5);
    FVector Start,Velocity;
    if(!Settings.Entry.BuildTrajectory(Tongue->Surface->Bounds.GetBox(),Landing,GetWorld()->GetGravityZ(),Start,Velocity)) { Food->Destroy(); return; }
    Pose.SetLocation(Start); Food->FinishSpawning(Pose);
    Food->BeginMouthEntry(Velocity,Settings.Entry.PushSpeed,Landing);
    Nuts.Add(Food); ++NutsSpawned; ForceNetUpdate();
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

void AMCNutRainEvent::Tick(float Dt)
{
    Super::Tick(Dt);
    if(!HasAuthority() || Stage==EMCNutRainStage::Idle || Stage==EMCNutRainStage::Complete) return;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    if(GS && (GS->Phase==EMCShiftPhase::Won || GS->Phase==EMCShiftPhase::Lost)) { Stop(); return; }
    const double Age=GetWorld()->GetTimeSeconds()-StageStartedAt;
    if(Stage==EMCNutRainStage::Rainfall)
    {
        const int32 Due=FMath::Clamp(1+FMath::FloorToInt(Age/Settings.RainSeconds*RainTargetCount),1,RainTargetCount);
        // Bounded emission keeps a slow frame from creating the entire storm at once.
        for(int32 Burst=0;DropsAttempted<Due && Burst<4;++Burst) SpawnNut();
        if(bFailed) { Enter(EMCNutRainStage::Complete); SetActorTickEnabled(false); return; }
        if(Age>=Settings.RainSeconds && DropsAttempted>=RainTargetCount)
        {
            if(NutsSpawned==0) bFailed=true;
            Enter(EMCNutRainStage::Settling);
        }
    }
    else if(Stage==EMCNutRainStage::Settling && Age>=Settings.SettleSeconds) AwakenLastNuts();
    else if(Stage==EMCNutRainStage::Enemies)
    {
        int32 Alive=0;
        for(AMCNutEnemy* Enemy:Enemies) if(IsValid(Enemy) && Enemy->CanReceiveToolHit()) ++Alive;
        if(EnemiesLeft!=Alive) { EnemiesLeft=Alive; ForceNetUpdate(); }
        if(EnemiesLeft==0) { Enter(EMCNutRainStage::Complete); SetActorTickEnabled(false); }
    }
}

void AMCNutRainEvent::Stop()
{
    if(!HasAuthority()) return;
    SetActorTickEnabled(false);
    for(AMCNutEnemy* Enemy:Enemies) if(IsValid(Enemy)) Enemy->Destroy();
    // Broken whole nuts spawn ordinary AMCFoodActor fragments with the same Batch.
    // Track that batch as well as the original actors so retries cannot leak food.
    if(FoodBatch!=0) for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
        if(It->Batch==FoodBatch) It->Destroy();
    Nuts.Reset(); Enemies.Reset(); Tongue=nullptr; ActiveProfile=nullptr;
    EnemiesLeft=0; Stage=EMCNutRainStage::Idle; FoodBatch=0; ForceNetUpdate();
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
}
