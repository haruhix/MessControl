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
#include "MCCoffeeWipe.h"
#include "MCSurfaceWipe.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "MCArenaTooth.h"
#include "Components/SceneComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
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
    MaxPaintTiles=FMath::Clamp(MaxPaintTiles,4,24);
    DirtBatch=Batch>0?Batch:100000+int32(GetUniqueID()%1000000);
    CareBaselines.Reset();
    PaintTiles.Reset();PendingPaintTiles.Reset();PaintGridSize=FIntPoint::ZeroValue;
    DeathPaintToothSamples.Reset();PaintTilesCreated=0;PaintTileCount=0;PaintStrokesAccepted=0;PaintCoverageCells=0;PaintWetCells=0;PaintDistanceCm=0;
    DeathPaintBursts=0;DeathPaintCoverageAddedCells=0;DeathPaintTeethTouched=0;
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
    if(bDeathBurst) {DepositDeathPaint(Center,FMath::Clamp(Radius,160.f,400.f));return;}
    float Half=FMath::Clamp(Radius,65.f,300.f);
    FHitResult Floor;bool Found=false;
    for(int32 Attempt=0;Attempt<4 && !Found;++Attempt) {
        Found=Tongue->GameplaySpawnFootprint(Center,Half*1.415f+12.f,Floor);
        if(!Found) Half*=.72f;
    }
    if(!Found || Tongue->GameplaySpawnZone(Floor.ImpactPoint)<0) return;
    TArray<AMCMouthSurface*> Patches;
    AMCMouthSurface* Nearest=nullptr;float Best=MAX_flt;
    for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if(It->Batch==DirtBatch && !It->bUlcer && !It->bPaintTile) {
        Patches.Add(*It);const float Distance=FVector::DistSquared2D(It->GetActorLocation(),Floor.ImpactPoint);
        if(Distance<Best) {Best=Distance;Nearest=*It;}
    }
    if(Nearest && Best<FMath::Square(FMath::Max(90.f,Half))) {
        // Seed dirt is authored once. A later source never resets unrelated cleaned coverage.
        return;
    }
    if(Patches.Num()>=MaxDirtPatches) return;
    const FTransform Pose(FRotationMatrix::MakeFromZX(Floor.ImpactNormal,FVector::ForwardVector).ToQuat(),Floor.ImpactPoint+Floor.ImpactNormal*5);
    auto* Patch=GetWorld()->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),Pose,this);
    if(!Patch) return;
    Patch->bRandomizeLiquidSize=false;Patch->LiquidHalfSize=Half;Patch->Batch=DirtBatch;
    Patch->LiquidSeed=Random.RandRange(1,MAX_int32);Patch->Tags.AddUnique(TEXT("MCCataclysmDirt"));
    Patch->FinishSpawning(Pose);Patch->Status->ApplyCoffee(1);Patch->ForceNetUpdate();
}
bool AMCCataclysmEvent::InitializePaintGrid()
{
    if(PaintGridSize.X>0 && PaintGridSize.Y>0) return true;
    const FBox Bounds=Tongue->Surface->Bounds.GetBox();
    if(!Bounds.IsValid) return false;
    const float Near=FMath::Clamp(FMath::IsFinite(Tongue->GameplaySpawnNearDepth)?Tongue->GameplaySpawnNearDepth:.18f,0.f,.95f);
    const float Far=FMath::Clamp(FMath::IsFinite(Tongue->GameplaySpawnFarDepth)?Tongue->GameplaySpawnFarDepth:.82f,Near+.05f,1.f);
    const FVector2D Extent(Bounds.GetSize().X*(Far-Near),Bounds.GetSize().Y);
    PaintGridOrigin=FVector2D(Bounds.Max.X-Bounds.GetSize().X*Far,Bounds.Min.Y);
    // Paint owns its separate bounded canvas budget. Wave residue and seed blobs
    // retain their existing cleanup contract without consuming trail resolution.
    PaintTileWidth=1200.f;
    do {
        PaintGridSize=FIntPoint(FMath::Max(1,FMath::CeilToInt(Extent.X/PaintTileWidth)),FMath::Max(1,FMath::CeilToInt(Extent.Y/PaintTileWidth)));
        if(PaintGridSize.X*PaintGridSize.Y<=MaxPaintTiles) return true;
        PaintTileWidth+=100.f;
    } while(PaintTileWidth<=3200.f);
    PaintGridSize=FIntPoint::ZeroValue;return false;
}
bool AMCCataclysmEvent::DepositSupportedPaintSegment(FVector From,FVector To,float Radius,const TFunction<bool(FVector)>& CoverageAllowed)
{
    if(!InitializePaintGrid()) return false;
    bool Changed=false;
    const FVector2D A(From),B(To);
    const FVector2D Low(FMath::Min(A.X,B.X)-Radius,FMath::Min(A.Y,B.Y)-Radius);
    const FVector2D High(FMath::Max(A.X,B.X)+Radius,FMath::Max(A.Y,B.Y)+Radius);
    const auto Cell=[&](FVector2D P) {
        const FVector2D Local=(P-PaintGridOrigin)/PaintTileWidth;
        return FIntPoint(FMath::Clamp(FMath::FloorToInt(Local.X),0,PaintGridSize.X-1),FMath::Clamp(FMath::FloorToInt(Local.Y),0,PaintGridSize.Y-1));
    };
    const FIntPoint Min=Cell(Low),Max=Cell(High);
    for(int32 Y=Min.Y;Y<=Max.Y;++Y) for(int32 X=Min.X;X<=Max.X;++X) {
        const FIntPoint Key(X,Y);
        auto* Patch=PaintTiles.FindRef(Key).Get();
        if(!IsValid(Patch)) {
            int32 Used=0;
            for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if(It->Batch==DirtBatch && It->bPaintTile) ++Used;
            if(Used>=MaxPaintTiles) continue;
            const FVector2D XY=PaintGridOrigin+FVector2D(X+.5,Y+.5)*PaintTileWidth;
            FVector Center(XY.X,XY.Y,From.Z+5);
            FHitResult Floor;
            if(Tongue->SurfacePoint(Center,Floor)) Center.Z=Floor.ImpactPoint.Z+5;
            const FTransform Pose(FRotator::ZeroRotator,Center);
            Patch=GetWorld()->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),Pose,this);
            if(!Patch) continue;
            Patch->bRandomizeLiquidSize=false;Patch->LiquidHalfSize=PaintTileWidth*.5f;Patch->Batch=DirtBatch;
            Patch->LiquidMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Cataclysm/Jelly/M_CataclysmPaintTrail.M_CataclysmPaintTrail")));
            Patch->Tags.AddUnique(TEXT("MCCataclysmDirt"));Patch->Tags.AddUnique(TEXT("MCCataclysmPaintTile"));
            Patch->InitializePaintTile(Tongue);Patch->FinishSpawning(Pose);PaintTiles.Add(Key,Patch);
            ++PaintTilesCreated;
        }
        if(Patch->DepositPaintStroke(From,To,Radius,CoverageAllowed)) {PendingPaintTiles.Add(Patch);Changed=true;}
    }
    return Changed;
}
void AMCCataclysmEvent::RegisterPaintStroke(FVector From,FVector To,float Radius)
{
    if(!HasAuthority() || !IsRunning() || !Tongue || From.ContainsNaN() || To.ContainsNaN() || !FMath::IsFinite(Radius) || Radius<=0) return;
    Radius=FMath::Clamp(Radius,20.f,400.f);
    // A teleport has no traversed path. Deposit only its supported destination.
    if(FVector::DistSquared2D(From,To)>FMath::Square(360.f) || FMath::Abs(From.Z-To.Z)>100.f) From=To;
    if(!InitializePaintGrid()) return;
    const float Length=FVector::Dist2D(From,To);
    const int32 Steps=FMath::Max(1,FMath::CeilToInt(Length/FMath::Min(Radius*.5f,24.f)));
    const float Margin=Radius+PaintTileWidth/FMCCoffeeWipe::Size*.7072f+Length/Steps*.5f;
    FVector Previous=FVector::ZeroVector;bool Connected=false,Accepted=false;
    for(int32 I=0;I<=Steps;++I) {
        const FVector Candidate=FMath::Lerp(From,To,float(I)/Steps);
        FHitResult Floor;
        if(!Tongue->GameplaySpawnFootprint(Candidate,Margin,Floor) || Floor.ImpactNormal.Z<.65f) {Connected=false;continue;}
        const FVector Point=Floor.ImpactPoint;
        if(DepositSupportedPaintSegment(Connected?Previous:Point,Point,Radius)) {
            if(Connected) PaintDistanceCm+=FVector::Dist2D(Previous,Point);
            Accepted=true;
        }
        Previous=Point;Connected=true;
    }
    if(Accepted) ++PaintStrokesAccepted;
}
void AMCCataclysmEvent::FlushPaintTiles()
{
    for(const auto& Entry:PendingPaintTiles) if(auto* Patch=Entry.Get()) Patch->FlushPaintDeposit();
    PendingPaintTiles.Reset();
}
void AMCCataclysmEvent::DepositDeathPaint(FVector Center,float Radius)
{
    if(!InitializePaintGrid()) return;
    const auto WetCells=[&]() {int32 Count=0;for(const auto& Entry:PaintTiles) if(auto* Tile=Entry.Value.Get())
        for(uint8 Cell:Tile->WipeMask) Count+=Cell>63;return Count;};
    const int32 BeforeWet=WetCells();
    ++DeathPaintBursts;
    const FVector Splash=Center+FVector(0,0,90);
    FCollisionQueryParams SplashQuery(SCENE_QUERY_STAT(MCJellyFloorPaintOcclusion),false,this);
    SplashQuery.AddIgnoredActor(Tongue);
    for(const auto& Jelly:Jellies) if(IsValid(Jelly)) SplashQuery.AddIgnoredActor(Jelly);
    const TFunction<bool(FVector)> PaintAllowed=[&](FVector Point) {
        FHitResult Floor,Block;
        if(!Tongue->SurfacePoint(Point,Floor) || Floor.ImpactNormal.Z<.65f) return false;
        return !GetWorld()->LineTraceSingleByChannel(Block,Splash,Floor.ImpactPoint+Floor.ImpactNormal*3.f,ECC_Visibility,SplashQuery);
    };
    // The center forms a broad balloon spill; spoke capsules carry the thrown paint.
    // Footprint checks shorten it near delivery caps, gums and unsupported edges.
    for(float R=Radius*.82f;R>=45.f;R*=.74f) {
        FHitResult Floor;
        if(Tongue->GameplaySpawnFootprint(Center,R+PaintTileWidth/FMCCoffeeWipe::Size*.7072f,Floor)) {
            DepositSupportedPaintSegment(Floor.ImpactPoint,Floor.ImpactPoint,R,PaintAllowed);break;
        }
    }
    const float Heading=Random.FRandRange(0,2*PI);
    for(int32 I=0;I<12;++I) {
        const float Angle=Heading+I*2*PI/12+Random.FRandRange(-.09f,.09f);
        const FVector Direction(FMath::Cos(Angle),FMath::Sin(Angle),0);
        const FVector End=Center+Direction*Radius*Random.FRandRange(1.25f,1.7f);
        const float Width=Random.FRandRange(22.f,40.f);
        const float Length=FVector::Dist2D(Center,End);
        const int32 Steps=FMath::Max(1,FMath::CeilToInt(Length/FMath::Min(Width*.5f,24.f)));
        const float Margin=Width+PaintTileWidth/FMCCoffeeWipe::Size*.7072f+Length/Steps*.5f;
        FVector Previous=Center;bool Connected=false,Accepted=false;
        for(int32 Step=0;Step<=Steps;++Step) {
            FHitResult Floor;
            const FVector Candidate=FMath::Lerp(Center,End,float(Step)/Steps);
            // Thrown paint stops at its first gap or blocker. It cannot restart
            // on reachable tissue on the far side of a wall or delivery lane.
            if(!Tongue->GameplaySpawnFootprint(Candidate,Margin,Floor) || !PaintAllowed(Floor.ImpactPoint)
                || (Connected && FMath::Abs(Floor.ImpactPoint.Z-Previous.Z)>45.f)) break;
            if(DepositSupportedPaintSegment(Connected?Previous:Floor.ImpactPoint,Floor.ImpactPoint,Width,PaintAllowed)) {
                if(Connected) PaintDistanceCm+=FVector::Dist2D(Previous,Floor.ImpactPoint);
                Accepted=true;
            }
            Previous=Floor.ImpactPoint;Connected=true;
        }
        if(Accepted) ++PaintStrokesAccepted;
    }
    // Existing 3D grime coverage preserves previous stains. Only visible nearby
    // tooth samples with an unobstructed splash path receive the new coating.
    const float Reach=Radius*1.7f;
    for(TActorIterator<AMCArenaTooth> It(GetWorld());It;++It) {
        if(!It->IsAvailable() || !It->Visual->GetStaticMesh()) continue;
        auto* Tooth=*It;
        if(FVector::DistSquared(Tooth->Visual->Bounds.GetBox().GetClosestPointTo(Splash),Splash)>FMath::Square(Reach)) continue;
        const TArray<uint8> Before=Tooth->Status->NeedsCare(true)?Tooth->GrimeMask:TArray<uint8>();
        const bool Deposited=Tooth->DepositLiquidGrime([&](FVector Point) {
            if(FVector::DistSquared(Point,Splash)>FMath::Square(Reach)) return false;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(MCJellyPaintOcclusion),false,this);
            Query.AddIgnoredActor(Tongue);
            for(const auto& Jelly:Jellies) if(IsValid(Jelly)) Query.AddIgnoredActor(Jelly);
            FHitResult Block;
            if(!GetWorld()->LineTraceSingleByChannel(Block,Splash,Point,ECC_Visibility,Query)) return true;
            // A hit on the target crown is allowed only at the contacted surface,
            // never through its opposite side or an intervening tooth/wall.
            return Block.GetActor()==Tooth && FVector::DistSquared(Block.ImpactPoint,Point)<FMath::Square(24.f);
        });
        // A rejected deposit leaves a clean tooth's unused volume cells intact.
        // Only newly increased samples that the real brush can acquire belong
        // to this event; raw bounding-volume voxels are not work targets.
        if(!Deposited || !Tooth->Status->NeedsCare(true)) continue;
        TArray<FVector> Points,Normals,MaskUVs;
        Tooth->GetDirtyContactSamples(Points,Normals,&MaskUVs);
        TArray<FDeathPaintSample>* Owned=nullptr;
        for(int32 Index=0;Index<MaskUVs.Num();++Index) {
            // Damage meshes can have different bounds. Use the native brush's
            // original-crown sample coordinates rather than reprojecting a chip.
            const FVector UV=MaskUVs[Index];
            const float Baseline=Before.Num()==FMCSurfaceWipe::Count?FMCSurfaceWipe::Sample(Before,UV):0.f;
            const float Current=FMCSurfaceWipe::Sample(Tooth->GrimeMask,UV);
            if(Current<=FMath::Max(.25f,Baseline)+.005f) continue;
            if(!Owned) Owned=&DeathPaintToothSamples.FindOrAdd(Tooth);
            auto* Existing=Owned->FindByPredicate([&](const FDeathPaintSample& Sample){return Sample.UV.Equals(UV,.0001);});
            if(Existing) Existing->Baseline=FMath::Min(Existing->Baseline,Baseline);
            else {FDeathPaintSample Sample;Sample.UV=UV;Sample.Baseline=Baseline;Owned->Add(Sample);}
        }
        if(Owned) ++DeathPaintTeethTouched;
    }
    FlushPaintTiles();
    DeathPaintCoverageAddedCells+=FMath::Max(0,WetCells()-BeforeWet);
    UE_LOG(LogTemp,Display,TEXT("MC_CATACLYSM JELLY_SPLAT radius=%.1f tiles=%d"),Radius,PaintTiles.Num());
}
void AMCCataclysmEvent::ObserveConsequences()
{
    JelliesLeft=0;for(AMCCataclysmJelly* Jelly:Jellies) if(IsValid(Jelly) && Jelly->IsEncounterAlive()) ++JelliesLeft;
    DirtLeft=0;
    PaintCoverageCells=0;PaintWetCells=0;
    PaintTileCount=0;
    for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if(It->Batch==DirtBatch && !It->bUlcer) {
        if(!It->IsClean()) ++DirtLeft;
        if(It->bPaintTile) {
            ++PaintTileCount;
            for(uint8 Cell:It->LiquidDepositMask) PaintCoverageCells+=Cell>63;
            for(uint8 Cell:It->WipeMask) PaintWetCells+=Cell>63;
        }
    }
    TSet<TWeakObjectPtr<UMCToothStatusComponent>> CountedCare;
    for(const auto& Base:CareBaselines) if(auto* Status=Base.Status.Get();Status && Status->IsAlive()) {
        if(const auto* Tooth=Cast<AMCArenaTooth>(Status->GetOwner());Tooth && !Tooth->IsAvailable()) continue;
        if(Status->CoffeeAmount()>Base.Amount+.01f) {++DirtLeft;CountedCare.Add(Status);}
    }
    for(const auto& Entry:DeathPaintToothSamples) if(auto* Tooth=Entry.Key.Get();Tooth && Tooth->IsAvailable()
        && Tooth->Status->NeedsCare(true) && !CountedCare.Contains(Tooth->Status)) {
        for(const FDeathPaintSample& Sample:Entry.Value)
            if(FMCSurfaceWipe::Sample(Tooth->GrimeMask,Sample.UV)>FMath::Max(.25f,Sample.Baseline)+.005f) {
                ++DirtLeft;break;
            }
    }
    DirtTotal=FMath::Max(DirtTotal,DirtLeft);ForceNetUpdate();
}
bool AMCCataclysmEvent::IsPaintWetAt(FVector Point) const
{
    // Clients reconstruct this query from the replicated tile actors as well.
    if(GetWorld()) for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It)
        if(It->Batch==DirtBatch && It->bPaintTile && It->IsPaintWetAt(Point)) return true;
    return false;
}
void AMCCataclysmEvent::Tick(float Dt)
{
    Super::Tick(Dt);if(!HasAuthority()) return;
    FlushPaintTiles();
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
    if(Phase==EMCCataclysmPhase::JellyFight) return TEXT("Желе рисует грязные дорожки. Убейте желе и очистите следы. Перед смертью оно раздувается и разбрызгивает краску вокруг.");
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
    DOREPLIFETIME(AMCCataclysmEvent,PaintTilesCreated);DOREPLIFETIME(AMCCataclysmEvent,PaintStrokesAccepted);
    DOREPLIFETIME(AMCCataclysmEvent,PaintTileCount);
    DOREPLIFETIME(AMCCataclysmEvent,PaintCoverageCells);DOREPLIFETIME(AMCCataclysmEvent,PaintWetCells);DOREPLIFETIME(AMCCataclysmEvent,PaintDistanceCm);
    DOREPLIFETIME(AMCCataclysmEvent,DeathPaintBursts);DOREPLIFETIME(AMCCataclysmEvent,DeathPaintCoverageAddedCells);DOREPLIFETIME(AMCCataclysmEvent,DeathPaintTeethTouched);
}
