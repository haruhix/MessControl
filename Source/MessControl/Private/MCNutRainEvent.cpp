#include "MCNutRainEvent.h"

#include "MCFoodActor.h"
#include "MCFoodBodyComponent.h"
#include "MCGameState.h"
#include "MCNutEnemy.h"
#include "MCNutBoss.h"
#include "MCNutLandingShadow.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

namespace
{
bool IsSupportedRainNut(const AMCFoodActor* Food,const AMCTongue* Tongue,float NutHeight)
{
    if(!IsValid(Food) || Food->IsMouthEntryActive()) return false;
    if(Food->Phase==EMCFoodPhase::Free || Food->Phase==EMCFoodPhase::Stuck) return true;
    if(Food->Phase!=EMCFoodPhase::Falling || !IsValid(Tongue) || !Food->Body) return false;
    // Food/obstacle contacts can stop entry without setting the food's
    // WorldStatic-only landing phase. Use the actual floor and motion for that
    // resting case for summon eligibility and subsequent landing reservations.
    FHitResult Floor;
    if(!Tongue->SurfacePoint(Food->GetActorLocation(),Floor)) return false;
    const float Bottom=float(Food->GetActorLocation().Z-Food->Body->GetScaledBoxExtent().Z);
    const float Height=Bottom-float(Floor.ImpactPoint.Z);
    return Height>=-45.f && Height<=NutHeight+45.f && Food->GetVelocity().Size()<=180.f;
}

struct FNutLandingReservation
{
    FVector Point;
    float BodyRadius=0;
};

void CollectNutLandingReservations(const TArray<TObjectPtr<AMCFoodActor>>& Nuts,const AMCTongue* Tongue,
    float NutHeight,TArray<FNutLandingReservation>& Out)
{
    Out.Reset();
    for(const AMCFoodActor* Food:Nuts) {
        if(!IsValid(Food) || Food->IsActorBeingDestroyed() || Food->IsDisposed() || Food->bFragment
            || Food->StackCarrier || !Food->Holders.IsEmpty() || !Food->Body) continue;
        const bool Airborne=Food->IsMouthEntryActive();
        if(!Airborne && !IsSupportedRainNut(Food,Tongue,NutHeight)) continue;
        // Individual wizard spells retain their explicit placement clearance.
        // The opening patterns intentionally permit nut-to-nut impacts. Held
        // food and fragments do not reserve a spell's landing footprint.
        Out.Add({Airborne?Food->WorkPosition():Food->GetActorLocation(),float(Food->Body->GetScaledBoxExtent().Size2D())});
    }
}

float NutLandingClearance(float NewRadius,float OldRadius,float NutHeight)
{
    // The horizontal box diagonal conservatively encloses a rotating body.
    // Keep the existing minimum and a small physical gap, rather than reserving
    // the much larger damage-warning radius and exhausting the arena.
    return FMath::Max(NutHeight*1.15f,NewRadius+OldRadius+24.f);
}

bool IsNutLandingClear(FVector Candidate,float NewRadius,float NutHeight,TConstArrayView<FNutLandingReservation> Reserved)
{
    for(const auto& Item:Reserved)
        if(FVector::DistSquared2D(Candidate,Item.Point)<FMath::Square(NutLandingClearance(NewRadius,Item.BodyRadius,NutHeight))) return false;
    return true;
}
}

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
    if(Settings.bBossEncounter) {
        // The fight opens with one knight, which becomes the wizard. Summons come from ground nuts,
        // rather than an unrelated quota of enemies at the transition.
        EnemyTargetCount=0;
    }
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
    RainStartedAt=StageStartedAt;
    if(Settings.bBossEncounter) BeginSeries(); else SpawnNut();
    if(Settings.bBossEncounter)
        Record(FString::Printf(TEXT("Орехи: 2 серии зон %s → %s, %d падений каждые %.2f с, между сериями %.2f с; затем Рыцарь и Колдун"),
            FirstPattern==0?TEXT("A"):TEXT("B"),FirstPattern==0?TEXT("B"):TEXT("A"),RainTargetCount,Settings.PatternDropSeconds,Settings.PatternSeriesGapSeconds));
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
    FirstPattern=Random.RandRange(0,1);
    MCNutRainPattern::BuildSchedule(Settings.PatternA,Settings.PatternB,FirstPattern,
        Settings.PatternDropSeconds,Settings.PatternSeriesGapSeconds,PatternSteps);
    for(const FMCNutRainPatternStep& Step:PatternSteps) {
        PlannedLaunchTimes.Add(Step.LaunchSeconds); PlannedPatternIndices.Add(Step.PatternIndex);
        PlannedZoneIndices.Add(Step.ZoneIndex); PlannedZoneCoordinates.Add(Step.Zone);
    }
    RainTargetCount=PlannedLaunchTimes.Num();
    SeriesIndex=1; ClusterDropsLeft=0;
    if(!SpawnNut()) bFailed=true;
    LastPatternLaunchAt=GetWorld()->GetTimeSeconds(); UpdatePatternProgress();
    NextDropAt=PlannedLaunchTimes.Num()>1?MCNutRainPattern::NextLaunchAt(LastPatternLaunchAt,PlannedLaunchTimes[0],PlannedLaunchTimes[1]):0;
    ForceNetUpdate();
}

void AMCNutRainEvent::UpdatePatternProgress()
{
    if(PatternSteps.IsEmpty()) {SeriesIndex=CompletedSeries=SeriesDropsLeft=0;return;}
    CompletedSeries=0;
    for(int32 I=0;I<DropsAttempted && I<PatternSteps.Num();++I)
        if(I+1==PatternSteps.Num() || PatternSteps[I+1].PatternIndex!=PatternSteps[I].PatternIndex) ++CompletedSeries;
    const int32 Last=FMath::Clamp(DropsAttempted-1,0,PatternSteps.Num()-1);
    SeriesIndex=PatternSteps.IsValidIndex(Last) && PatternSteps[Last].PatternIndex==FirstPattern?1:2;
    SeriesDropsLeft=0;
    for(int32 I=DropsAttempted;I<PatternSteps.Num() && PatternSteps[I].PatternIndex==PatternSteps[Last].PatternIndex;++I) ++SeriesDropsLeft;
}

void AMCNutRainEvent::Enter(EMCNutRainStage Next)
{
    Stage=Next; StageStartedAt=GetWorld()->GetTimeSeconds(); ForceNetUpdate();
}

bool AMCNutRainEvent::SpawnNut(TOptional<FVector> Candidate,TOptional<FVector2D> PatternZone)
{
    const int32 DropIndex=DropsAttempted;
    ++DropsAttempted;
    if(!IsValid(Tongue)) return false;
    FTransform Pose(FRotator(0,Random.FRandRange(-180,180),0));
    auto* Food=GetWorld()->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Food) return false;
    Food->ConfigureItem(NutRowName,NutRow,Random); Food->Batch=FoodBatch; Food->Tags.AddUnique(TEXT("MCNutRain"));
    const auto* Collision=Cast<UMCFoodBodyComponent>(Food->Body);
    if(!Collision || !Collision->HasMeshCollision())
    {
        Food->Destroy(); bFailed=true;
        UE_LOG(LogTemp,Error,TEXT("MC_NUT_RAIN_FAILED walnut needs baked food collision; run author_nut_rain.py"));
        return false;
    }
    const float Margin=FMath::Max(float(Food->Body->GetScaledBoxExtent().Size2D()+18),Settings.ImpactRadius);
    const float BodyRadius=float(Food->Body->GetScaledBoxExtent().Size2D());
    FHitResult Floor;
    const bool PatternDrop=Settings.bBossEncounter && Stage==EMCNutRainStage::Rainfall && PatternSteps.IsValidIndex(DropIndex);
    const bool UsesPattern=PatternDrop || PatternZone.IsSet();
    bool FoundFloor=UsesPattern?ResolvePatternLanding(PatternZone.IsSet()?PatternZone.GetValue():PatternSteps[DropIndex].Zone,Margin,Floor)
        :Candidate.IsSet() && Tongue->GameplaySpawnFootprint(Candidate.GetValue(),Margin,Floor);
    if(FoundFloor && Settings.bBossEncounter && !UsesPattern) {
        TArray<FNutLandingReservation> Reserved;
        CollectNutLandingReservations(Nuts,Tongue,Settings.LargeNutHeight,Reserved);
        FoundFloor=IsNutLandingClear(Floor.ImpactPoint,BodyRadius,Settings.LargeNutHeight,Reserved);
    }
    // A wizard can target a player in a delivery strip or at the tongue edge.
    // Preserve the rain spell by moving that individual nut to a reachable
    // gameplay footprint; its real landing warning advertises the new point.
    if(!FoundFloor && !UsesPattern) FoundFloor=ChooseLanding(Margin,BodyRadius,Floor);
    if(!FoundFloor) {
        Food->Destroy();
        if(PatternDrop) {
            bFailed=true;
            UE_LOG(LogTemp,Error,TEXT("MC_NUT_RAIN_FAILED pattern=%d zone=%d has no reachable footprint; fix authored zone/margin"),
                PatternSteps[DropIndex].PatternIndex,PatternSteps[DropIndex].ZoneIndex);
        }
        return false;
    }
    const FVector Landing=Floor.ImpactPoint+FVector(0,0,Food->Body->GetScaledBoxExtent().Z+5);
    FVector Start,Velocity;
    if(!Settings.Entry.BuildTrajectory(Tongue->Surface->Bounds.GetBox(),Landing,GetWorld()->GetGravityZ(),Start,Velocity)) { Food->Destroy(); return false; }
    const FBox Bounds=Tongue->Surface->Bounds.GetBox();
    const float Angle=Random.FRandRange(-PI,PI),Outside=Settings.Entry.OutsideDistance*Random.FRandRange(.5f,1.5f);
    Start.X=Settings.bBossEncounter?Landing.X:Bounds.GetCenter().X+FMath::Cos(Angle)*(Bounds.GetExtent().X+Outside);
    Start.Y=Settings.bBossEncounter?Landing.Y:Bounds.GetCenter().Y+FMath::Sin(Angle)*(Bounds.GetExtent().Y+Outside);
    Start.Z=FMath::Max(Landing.Z,Bounds.Max.Z)+FMath::Clamp(Settings.Entry.EntryHeight*Random.FRandRange(.75f,1.2f),150.f,900.f);
    Velocity=(Landing-Start-FVector(0,0,GetWorld()->GetGravityZ())*(.5f*Settings.Entry.FlightSeconds*Settings.Entry.FlightSeconds))/Settings.Entry.FlightSeconds;
    Pose.SetLocation(Start); Food->FinishSpawning(Pose);
    if(Settings.bBossEncounter) Food->EnableNutKinetics();
    if(Settings.bBossEncounter) Food->Settings.MaxDamage=Settings.ImpactDamageLimit;
    const FTransform ShadowPose(Floor.ImpactPoint);
    if(auto* Shadow=GetWorld()->SpawnActorDeferred<AMCNutLandingShadow>(AMCNutLandingShadow::StaticClass(),ShadowPose,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn)) {
        Shadow->Configure(Tongue,Food,Floor.ImpactPoint,Settings.Entry.FlightSeconds,Settings.ImpactRadius,Settings.LandingShadowOpacity,
            Settings.ImpactDamageLimit,Settings.ImpactPushSpeed);
        Shadow->FinishSpawning(ShadowPose); LandingShadows.Add(Shadow);
    } else {
        Food->Destroy(); bFailed=true; return false;
    }
    Food->BeginMouthEntry(Velocity,Settings.Entry.PushSpeed,Landing);
    PlannedLandings.Add(Landing); LaunchLocations.Add(Start);
    if(PatternDrop) ActualLaunchTimes.Add(float(GetWorld()->GetTimeSeconds()-RainStartedAt));
    Nuts.Add(Food); ++NutsSpawned; ForceNetUpdate(); return true;
}

bool AMCNutRainEvent::ResolvePatternLanding(FVector2D Zone,float Margin,FHitResult& Floor) const
{
    if(!IsValid(Tongue) || Zone.ContainsNaN() || !FMath::IsFinite(Margin)) return false;
    const FBox Bounds=Tongue->Surface->Bounds.GetBox();
    const float NearDepth=FMath::IsFinite(Tongue->GameplaySpawnNearDepth)?FMath::Clamp(Tongue->GameplaySpawnNearDepth,0.f,.95f):.18f;
    const float FarDepth=FMath::IsFinite(Tongue->GameplaySpawnFarDepth)?FMath::Clamp(Tongue->GameplaySpawnFarDepth,NearDepth+.05f,1.f):.82f;
    const double Back=Bounds.Max.X-Bounds.GetSize().X*NearDepth-Margin-2.;
    const double Front=Bounds.Max.X-Bounds.GetSize().X*FarDepth+Margin+2.;
    if(Back<=Front || Bounds.GetSize().Y<=2*Margin) return false;
    const double X=FMath::Lerp(Back,Front,Zone.Y);
    // Mouth depth is +world X at the throat and -world X at the front. Normalize
    // width to the largest *reachable* cross-section of the real tongue, never
    // to its rectangular bounds. This preserves the staggered pattern without
    // spilling nuts under gums or relocating a blocked zone to a random patch.
    constexpr int32 Samples=48;
    int32 RunStart=INDEX_NONE,BestStart=INDEX_NONE,BestEnd=INDEX_NONE;
    const double MinY=Bounds.Min.Y+Margin,MaxY=Bounds.Max.Y-Margin;
    for(int32 I=0;I<=Samples+1;++I) {
        FHitResult Test;
        const bool Valid=I<=Samples && Tongue->GameplaySpawnFootprint(FVector(X,FMath::Lerp(MinY,MaxY,double(I)/Samples),Bounds.GetCenter().Z),Margin,Test);
        if(Valid && RunStart==INDEX_NONE) RunStart=I;
        if(!Valid && RunStart!=INDEX_NONE) {
            if(BestStart==INDEX_NONE || I-1-RunStart>BestEnd-BestStart) {BestStart=RunStart;BestEnd=I-1;}
            RunStart=INDEX_NONE;
        }
    }
    if(BestStart==INDEX_NONE || BestEnd<=BestStart) return false;
    const double Left=FMath::Lerp(MinY,MaxY,double(BestStart)/Samples);
    const double Right=FMath::Lerp(MinY,MaxY,double(BestEnd)/Samples);
    const FVector Candidate(X,FMath::Lerp(Left,Right,Zone.X),Bounds.GetCenter().Z);
    return Tongue->GameplaySpawnFootprint(Candidate,Margin,Floor);
}

bool AMCNutRainEvent::SpawnSpellNut(FVector Candidate,int32 Seed,int32 PatternIndex)
{
    if(!HasAuthority() || Stage!=EMCNutRainStage::Enemies || !Settings.bBossEncounter || Candidate.ContainsNaN()) return false;
    TOptional<FVector2D> Zone;
    if(PatternIndex!=INDEX_NONE) {
        // Replay both complete opening patterns on the same reachable footprint.
        // Existing debris must not move the spell's zones into a smaller cluster.
        const auto& First=FirstPattern==0?Settings.PatternA:Settings.PatternB;
        const auto& Second=FirstPattern==0?Settings.PatternB:Settings.PatternA;
        if(PatternIndex<0 || PatternIndex>=First.Num()+Second.Num()) return false;
        Zone=PatternIndex<First.Num()?First[PatternIndex]:Second[PatternIndex-First.Num()];
    }
    const FRandomStream SavedRandom=Random;
    const int32 SavedAttempts=DropsAttempted;
    Random.Initialize(Seed);
    const bool Spawned=SpawnNut(Candidate,Zone);
    Random=SavedRandom; DropsAttempted=SavedAttempts;
    return Spawned;
}

void AMCNutRainEvent::GetAvailableRainNuts(TArray<AMCFoodActor*>& Out) const
{
    Out.Reset();
    for(AMCFoodActor* Food:Nuts) {
        if(!IsValid(Food) || Food->IsActorBeingDestroyed() || Food->IsDisposed() || Food->bFragment || Food->StackCarrier
            || !Food->Holders.IsEmpty() || !Food->ActorHasTag(TEXT("MCNutRain"))
            || !IsSupportedRainNut(Food,Tongue,Settings.LargeNutHeight)) continue;
        Out.Add(Food);
    }
}

bool AMCNutRainEvent::AreRainDropsResolved() const
{
    for(const AMCFoodActor* Food:Nuts) {
        if(!IsValid(Food) || Food->IsActorBeingDestroyed() || Food->IsDisposed() || Food->StackCarrier || !Food->Holders.IsEmpty()) continue;
        // Only the opening flight gates the knight. A nut can roll, bounce or
        // leave the tongue after contact without cancelling the boss fight.
        // Ground support is still required separately for the wizard's summons.
        if(Food->IsMouthEntryActive()) return false;
    }
    return true;
}

bool AMCNutRainEvent::ChooseLanding(float Margin,float NutBodyRadius,FHitResult& Floor)
{
    TArray<FVector> OccupiedLandings;
    TArray<FNutLandingReservation> Reserved;
    float BodySeparation=0;
    if(Settings.bBossEncounter) {
        CollectNutLandingReservations(Nuts,Tongue,Settings.LargeNutHeight,Reserved);
        BodySeparation=Settings.LargeNutHeight*1.15f;
        for(const auto& Item:Reserved) {
            OccupiedLandings.Add(Item.Point);
            BodySeparation=FMath::Max(BodySeparation,NutLandingClearance(NutBodyRadius,Item.BodyRadius,Settings.LargeNutHeight));
        }
    } else for(const AMCFoodActor* Food:Nuts)
        if(IsValid(Food) && !Food->IsDisposed() && Food->IsMouthEntryActive()) OccupiedLandings.Add(Food->WorkPosition());
    // Keep bodies apart in flight while allowing warning circles to overlap.
    // Airborne and ground whole nuts both reserve body space. Warning circles
    // may still overlap; no fallback can override physical clearance.
    const auto IsClear=[&](FVector Point) {
        return !Settings.bBossEncounter || IsNutLandingClear(Point,NutBodyRadius,Settings.LargeNutHeight,Reserved);
    };
    if(ClusterDropsLeft<=0) {
        bool Found=false;
        if(Settings.bBossEncounter && Random.FRand()<.65f)
            Found=Tongue->RandomGameplayInterestPoint(Random,Margin,BodySeparation,OccupiedLandings,Floor);
        if(!Found) Found=Tongue->RandomGameplaySpawnPoint(Random,Margin,BodySeparation,OccupiedLandings,Floor);
        if(!Found) return false;
        ClusterCenter=Floor.ImpactPoint; ClusterDropsLeft=Random.RandRange(2,3);
    }
    --ClusterDropsLeft;
    for(int32 Attempt=0;Attempt<24;++Attempt) {
        const float Angle=Random.FRandRange(-PI,PI);
        const float Distance=Settings.ClusterRadius*(Random.FRand()+Random.FRand())*.5f;
        const FVector Candidate=ClusterCenter+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Distance;
        if(IsClear(Candidate) && Tongue->GameplaySpawnFootprint(Candidate,Margin,Floor)) return true;
    }
    // A clipped patch can move to a new valid patch; the delivery lanes remain excluded.
    ClusterDropsLeft=0;
    return Tongue->RandomGameplaySpawnPoint(Random,Margin,BodySeparation,OccupiedLandings,Floor);
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
    const float Radius=Settings.Boss.BodyRadiusForRole(EMCNutBossRole::Tank);
    FHitResult Floor;
    bool FoundFloor=Tongue->RandomGameplaySpawnPoint(Random,Radius+20,0,TArray<FVector>(),Floor);
    // Ordinary food deliberately samples just one 30/70 spawn band. A blocked
    // band must not cancel a mandatory boss: reuse a rain footprint, freshly
    // validated for the knight on the current (possibly deformed) tongue.
    const bool UsedFallback=!FoundFloor;
    if(!FoundFloor) for(const FVector& Landing:PlannedLandings) {
        if(Tongue->GameplaySpawnFootprint(Landing,Radius+20,Floor)) { FoundFloor=true; break; }
    }
    // The surface can move since the rain. Search both bands without weakening
    // edge, delivery-lane or player-access checks if old footprints no longer fit.
    if(!FoundFloor) {
        const FBox Bounds=Tongue->Surface->Bounds.GetBox();
        constexpr int32 Samples=16;
        for(int32 X=0;X<Samples && !FoundFloor;++X) for(int32 Y=0;Y<Samples && !FoundFloor;++Y) {
            const FVector Candidate=Bounds.Min+Bounds.GetSize()*FVector((X+.5f)/Samples,(Y+.5f)/Samples,.5f);
            FoundFloor=Tongue->GameplaySpawnFootprint(Candidate,Radius+20,Floor);
        }
    }
    if(FoundFloor) {
        const FTransform Pose(FRotator(0,Random.FRandRange(-180,180),0),Floor.ImpactPoint+FVector(0,0,Radius+4));
        if(auto* Boss=GetWorld()->SpawnActorDeferred<AMCNutBoss>(AMCNutBoss::StaticClass(),Pose,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn)) {
            Boss->ConfigureEncounter(Tongue,this,EMCNutBossRole::Tank,Settings.Boss,PartyPlayers,Random.RandHelper(MAX_int32));
            FTransform AirbornePose=Pose;AirbornePose.SetLocation(Boss->LockedStart);
            Boss->FinishSpawning(AirbornePose);Bosses.Add(Boss);RegisterEncounterEnemy(Boss);
        }
    }
    if(bFailed || Bosses.Num()!=1) {
        bFailed=true; Record(TEXT("Орехи: не удалось разместить Рыцаря; этап остановлен"));
        UE_LOG(LogTemp,Error,TEXT("MC_NUT_BOSS_SPAWN_FAILED floor=%d rain_landings=%d tongue=%s"),FoundFloor,PlannedLandings.Num(),*GetNameSafe(Tongue));
        Enter(EMCNutRainStage::Complete); SetActorTickEnabled(false); return;
    }
    BossesLeft=1; Enter(EMCNutRainStage::Enemies);
    Record(FString::Printf(TEXT("Орехи: после %d падений появился Рыцарь. Победите его, затем его вторую форму — Колдуна"),NutsSpawned));
    UE_LOG(LogTemp,Display,TEXT("MC_NUT_ENCOUNTER_START series=%d nuts=%d bosses=1 phases=2 creeps=0 placement_fallback=%d"),CompletedSeries,NutsSpawned,UsedFallback);
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
            // One launch per frame at most. A hitch shifts the remaining times
            // rather than bursting several overdue zones or dropping a zone.
            const double Now=GetWorld()->GetTimeSeconds();
            if(DropsAttempted<PlannedLaunchTimes.Num() && Now>=NextDropAt) {
                const int32 Previous=DropsAttempted;
                if(!SpawnNut()) bFailed=true;
                LastPatternLaunchAt=Now; UpdatePatternProgress();
                NextDropAt=DropsAttempted<PlannedLaunchTimes.Num()
                    ?MCNutRainPattern::NextLaunchAt(Now,PlannedLaunchTimes[Previous],PlannedLaunchTimes[DropsAttempted]):0;
                ForceNetUpdate();
            }
            if(bFailed) { Enter(EMCNutRainStage::Complete); SetActorTickEnabled(false); return; }
            if(DropsAttempted>=PlannedLaunchTimes.Num()) {
                if(NutsSpawned==0) { bFailed=true; Enter(EMCNutRainStage::Complete); SetActorTickEnabled(false); return; }
                RainTargetCount=NutsSpawned; Enter(EMCNutRainStage::Settling);
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
    else if(Stage==EMCNutRainStage::Settling) {
        if(Settings.bBossEncounter) {
            // Two authored series define the new duration. The old director's
            // 20–25 second clamp must not append a dead interval to this storm.
            const float FinalFlightAge=float(GetWorld()->GetTimeSeconds()-LastPatternLaunchAt);
            const bool Ready=CompletedSeries==2 && FinalFlightAge>=Settings.Entry.FlightSeconds;
            const bool Resolved=AreRainDropsResolved();
            if(Ready && Resolved) SpawnBossEncounter();
            else if(Ready && !Resolved && FinalFlightAge>Settings.Entry.FlightSeconds+10.f) {
                // Food normally ends mouth entry within eight seconds. Recover
                // only a stale flight after the last actual launch, even if a
                // slow frame delayed the schedule. Retain ground
                // nuts for summons instead of silently skipping the encounter.
                int32 Recovered=0;
                for(AMCFoodActor* Food:Nuts)
                    if(IsValid(Food) && !Food->IsActorBeingDestroyed() && !Food->IsDisposed()
                        && !Food->StackCarrier && Food->Holders.IsEmpty() && Food->IsMouthEntryActive()) {
                        Food->Dispose(); ++Recovered;
                    }
                UE_LOG(LogTemp,Warning,TEXT("MC_NUT_RAIN_RECOVER stale_flights=%d; continuing to knight"),Recovered);
                if(Ready && AreRainDropsResolved()) SpawnBossEncounter();
            }
        } else if(Age>=Settings.SettleSeconds) AwakenLastNuts();
    }
    else if(Stage==EMCNutRainStage::Enemies)
    {
        int32 Alive=0;
        int32 AliveBosses=0;
        for(AMCNutEnemy* Enemy:Enemies) if(IsValid(Enemy) && Enemy->IsEncounterAlive()) ++Alive;
        for(const AMCNutBoss* Boss:Bosses) if(IsValid(Boss) && Boss->IsEncounterAlive()) ++AliveBosses;
        if(BossesLeft!=AliveBosses) { BossesLeft=AliveBosses; Record(FString::Printf(TEXT("Орехи: боссов осталось %d"),BossesLeft)); ForceNetUpdate(); }
        if(EnemiesLeft!=Alive) { EnemiesLeft=Alive; ForceNetUpdate(); }
        if(Settings.bBossEncounter?BossesLeft==0:EnemiesLeft==0) {
            if(Settings.bBossEncounter) for(AMCNutEnemy* Enemy:Enemies)
                if(IsValid(Enemy) && !Cast<AMCNutBoss>(Enemy)) Enemy->Destroy();
            EnemiesLeft=0;
            // The final defeat unlocks the reward now. Transforming knights
            // remain IsEncounterAlive() until the wizard phase is defeated;
            // completed corpses finish their own death lifespan during Stop.
            Record(TEXT("Орехи: Рыцарь и его вторая форма Колдун побеждены; директор снова управляет промежутком"));
            Enter(EMCNutRainStage::Complete); SetActorTickEnabled(false);
        }
    }
}

void AMCNutRainEvent::Stop()
{
    if(!HasAuthority()) return;
    SetActorTickEnabled(false);
    const bool CompletedBossEncounter=Stage==EMCNutRainStage::Complete && Settings.bBossEncounter && !bFailed;
    // Bosses are also present in Enemies/EncounterActors. Visit each actor once
    // so releasing a completed corpse cannot be undone by the next array.
    TSet<AActor*> Cleanup;
    for(AMCNutEnemy* Enemy:Enemies) if(IsValid(Enemy)) Cleanup.Add(Enemy);
    for(AMCNutBoss* Boss:Bosses) if(IsValid(Boss)) Cleanup.Add(Boss);
    for(AActor* Actor:EncounterActors) if(IsValid(Actor)) Cleanup.Add(Actor);
    for(AActor* Actor:Cleanup) {
        if(Actor->IsActorBeingDestroyed()) continue;
        auto* Boss=Cast<AMCNutBoss>(Actor);
        const float Remaining=Boss?Boss->GetLifeSpan():0.f;
        if(CompletedBossEncounter && Boss && Boss->bDefeated && Boss->State==EMCNutBossState::Defeated
            && FMath::IsFinite(Remaining) && Remaining>0.f) {
            // This actor is cosmetic now: its tick returns before any encounter
            // AI and its existing lifespan still owns final destruction.
            Boss->EncounterOwner=nullptr;Boss->SetOwner(nullptr);Boss->Target=nullptr;
            Boss->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);Boss->ForceNetUpdate();
        } else Actor->Destroy();
    }
    for(AMCNutLandingShadow* Shadow:LandingShadows) if(IsValid(Shadow)) Shadow->Destroy();
    // Broken whole nuts spawn ordinary AMCFoodActor fragments with the same Batch.
    // Track that batch as well as the original actors so retries cannot leak food.
    if(FoodBatch!=0) for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
        if(It->Batch==FoodBatch) It->Destroy();
    Nuts.Reset(); Enemies.Reset(); Bosses.Reset(); EncounterActors.Reset(); LandingShadows.Reset(); PlannedLaunchTimes.Reset(); PlannedLandings.Reset(); LaunchLocations.Reset();
    ActualLaunchTimes.Reset(); PatternSteps.Reset(); PlannedPatternIndices.Reset(); PlannedZoneIndices.Reset(); PlannedZoneCoordinates.Reset();
    ClusterDropsLeft=0; Tongue=nullptr; ActiveProfile=nullptr;
    EnemiesLeft=0; Stage=EMCNutRainStage::Idle; FoodBatch=0; ForceNetUpdate();
    SeriesIndex=CompletedSeries=SeriesDropsLeft=BossesLeft=0; FirstPattern=0; NextDropAt=0; RainStartedAt=LastPatternLaunchAt=0;
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
    DOREPLIFETIME(AMCNutRainEvent,FirstPattern);
}
