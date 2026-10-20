#include "CoreMinimal.h"

#if UE_BUILD_DEVELOPMENT
#include "MCArenaTooth.h"
#include "MCBrushContactComponent.h"
#include "MCFoodActor.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

namespace MCPlaytestToothInspectionPrivate
{
struct FCounts
{
    int32 Candidates=0,Floor=0,Capsule=0,Range=0,Normal=0,Occluded=0,Hand=0,Reachable=0;
    int32 NativeReachable=0,WideReachable=0,NavMissing=0,NavFloor=0,NavPose=0,NavReachable=0;
    int32 Paths=0,PathRejected=0,PathReachable=0,PathCapped=0;
};

bool CapsuleClear(UWorld* World,AMCToothCharacter* Hero,FVector Center,FString& Blocker)
{
    const auto* Capsule=Hero->GetCapsuleComponent();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCInspectToothCapsule),false,Hero);
    const float Radius=Capsule->GetScaledCapsuleRadius()+4;
    const float Half=FMath::Max(Radius,Capsule->GetScaledCapsuleHalfHeight());
    TArray<FOverlapResult> Hits;
    World->OverlapMultiByChannel(Hits,Center,FQuat::Identity,Capsule->GetCollisionObjectType(),
        FCollisionShape::MakeCapsule(Radius,Half),Query,FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
    for(const auto& Hit:Hits) if(Hit.bBlockingHit) {
        Blocker=FString::Printf(TEXT("%s.%s"),*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()));return false;
    }
    return true;
}

bool ResolveFloor(UWorld* World,AMCToothCharacter* Hero,AMCTongue* Tongue,FVector Candidate,
    FVector& Stand,FString& Blocker,bool NavSeed=false)
{
    const float Half=Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    FVector Seed=Candidate;
    if(!NavSeed && Tongue) {
        FHitResult Tissue;
        if(!Tongue->SurfacePoint(Candidate,Tissue)) {Blocker=TEXT("no_tongue_surface");return false;}
        Seed.Z=Tissue.ImpactPoint.Z;
    }
    FHitResult Floor;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCInspectToothFloor),false,Hero);
    // Match the planner's physical floor contract. Only the seed differs: live
    // tongue instead of Nav, or the actual Nav projection in the second pass.
    if(!World->LineTraceSingleByChannel(Floor,Seed+FVector(0,0,Half+15),Seed-FVector(0,0,160),ECC_Visibility,Query))
        {Blocker=TEXT("no_floor_hit");return false;}
    if(Floor.ImpactNormal.Z<Hero->GetCharacterMovement()->GetWalkableFloorZ()
        || Cast<AMCArenaTooth>(Floor.GetActor()) || Cast<APawn>(Floor.GetActor()) || Cast<AMCFoodActor>(Floor.GetActor())) {
        Blocker=FString::Printf(TEXT("%s.%s normal_z=%.3f"),*GetNameSafe(Floor.GetActor()),*GetNameSafe(Floor.GetComponent()),Floor.ImpactNormal.Z);return false;
    }
    Stand=FVector(Seed.X,Seed.Y,Floor.ImpactPoint.Z+Half+4);return true;
}

bool LineClear(UWorld* World,AMCToothCharacter* Hero,AMCArenaTooth* Tooth,FVector Stand,FVector Point,FVector Normal)
{
    FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCInspectToothSight),false,Hero);Query.AddIgnoredActor(Tooth);
    return !World->LineTraceSingleByChannel(Hit,Stand+FVector(0,0,40),Point-Normal*2,ECC_Visibility,Query);
}

void Inspect(UWorld* World,const TArray<FString>& Args)
{
    if(!World || !World->IsGameWorld() || World->GetNetMode()==NM_Client) {
        UE_LOG(LogTemp,Warning,TEXT("MC_TOOTH_INSPECT requires an existing host game/PIE world; read-only, no world is started."));return;
    }
    AMCToothCharacter* Hero=nullptr;
    const FString PawnFilter=Args.Num()>1?Args[1]:FString();
    for(auto It=World->GetPlayerControllerIterator();It;++It) {
        const auto* Player=It->Get();
        auto* Candidate=Player?Cast<AMCToothCharacter>(Player->GetPawn()):nullptr;
        if(Candidate && Candidate->Status->IsAlive() && (PawnFilter.IsEmpty() || Candidate->GetName().Contains(PawnFilter))) {Hero=Candidate;break;}
    }
    if(!Hero) for(TActorIterator<AMCToothCharacter> It(World);It;++It)
        if(It->GetController() && It->Status->IsAlive() && (PawnFilter.IsEmpty() || It->GetName().Contains(PawnFilter))) {Hero=*It;break;}
    if(!Hero || !Hero->BrushContact) {UE_LOG(LogTemp,Warning,TEXT("MC_TOOTH_INSPECT no matching existing live tooth pawn"));return;}
    AMCTongue* Tongue=nullptr;
    for(TActorIterator<AMCTongue> It(World);It;++It) if(!It->CurrentVertices().IsEmpty()) {Tongue=*It;break;}
    auto* Nav=UNavigationSystemV1::GetCurrent(World);
    const FString Filter=Args.IsEmpty()?FString():Args[0];
    UE_LOG(LogTemp,Display,TEXT("MC_TOOTH_INSPECT_BEGIN hero=%s class=%s pose=%s capsule=%.2f/%.2f surface_reach=%.2f hand_xy=%.2f hand_z=%.2f nav=%d scope=bounded_existing_dirty_samples_no_world_edits"),
        *Hero->GetName(),*Hero->GetClass()->GetPathName(),*Hero->GetActorLocation().ToString(),Hero->GetCapsuleComponent()->GetScaledCapsuleRadius(),
        Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),Hero->BrushContact->SurfaceReach,Hero->BrushContact->MaxHandTravel,Hero->BrushContact->MaxHandVerticalTravel,Nav?1:0);
    for(TActorIterator<AMCArenaTooth> It(World);It;++It) {
        auto* Tooth=*It;const FString Mesh=GetNameSafe(Tooth->Visual->GetStaticMesh());
        const bool DefaultMatch=Mesh.Contains(TEXT("Teeth_L_01")) || Mesh.Contains(TEXT("Teeth_R_01")) || Mesh.Contains(TEXT("Teeth_L_05")) || Mesh.Contains(TEXT("Teeth_L_07"));
        if(Filter.IsEmpty()? !DefaultMatch : (!Filter.Equals(TEXT("all"),ESearchCase::IgnoreCase) && !Mesh.Contains(Filter) && !Tooth->GetName().Contains(Filter))) continue;
        TArray<FVector> Points,Normals;Tooth->GetDirtyContactSamples(Points,Normals);
        if(Points.IsEmpty()) {
            UE_LOG(LogTemp,Display,TEXT("MC_TOOTH_INSPECT tooth=%s mesh=%s samples=0 available=%d coffee=%d reason=clean_or_no_coating_samples_no_dirt_was_created"),
                *Tooth->GetName(),*Mesh,Tooth->IsAvailable(),Tooth->Status->State.CoffeeLeft);continue;
        }
        TArray<int32> Samples;
        if(Points.Num()<=96) for(int32 I=0;I<Points.Num();++I) Samples.Add(I);
        else {
            for(int32 Axis=0;Axis<3;++Axis) {
                int32 Low=0,High=0;
                for(int32 I=1;I<Points.Num();++I) {if(Points[I][Axis]<Points[Low][Axis]) Low=I;if(Points[I][Axis]>Points[High][Axis]) High=I;}
                Samples.AddUnique(Low);Samples.AddUnique(High);
            }
            for(int32 I=0;I<90;++I) Samples.AddUnique(I*(Points.Num()-1)/89);
        }
        FCounts Count;FString FirstFloor,FirstCapsule;int32 PrintedGood=0,PrintedHand=0;
        float MinZ=MAX_flt,MaxZ=-MAX_flt;
        for(const auto Point:Points) {MinZ=FMath::Min(MinZ,float(Point.Z));MaxZ=FMath::Max(MaxZ,float(Point.Z));}
        for(const int32 Sample:Samples) {
            const FVector Point=Points[Sample],Normal=Normals[Sample].GetSafeNormal();
            const FVector Out=Normal.GetSafeNormal2D(UE_SMALL_NUMBER,(Hero->GetActorLocation()-Point).GetSafeNormal2D());
            const FVector Side=FVector::CrossProduct(FVector::UpVector,Out);
            auto TestPose=[&](FVector Candidate,bool Wide) {
                ++Count.Candidates;FVector Stand;FString Blocker;
                if(!ResolveFloor(World,Hero,Tongue,Candidate,Stand,Blocker)) {++Count.Floor;if(FirstFloor.IsEmpty()) FirstFloor=Blocker;return;}
                if(!CapsuleClear(World,Hero,Stand,Blocker)) {++Count.Capsule;if(FirstCapsule.IsEmpty()) FirstCapsule=Blocker;return;}
                const FVector Delta=Point-Stand;
                if(Delta.Size2D()>Hero->BrushContact->SurfaceReach) {++Count.Range;return;}
                if(Delta.IsNearlyZero() || FVector::DotProduct(Normal,(-Delta).GetSafeNormal())<.05f) {++Count.Normal;return;}
                if(!LineClear(World,Hero,Tooth,Stand,Point,Normal)) {++Count.Occluded;return;}
                const FRotator Facing(0,Delta.Rotation().Yaw,0);
                if(!Hero->BrushContact->CanReachFromPose(Stand,Facing,Point,Normal,Tooth)) {
                    ++Count.Hand;
                    if(PrintedHand++<3) UE_LOG(LogTemp,Display,TEXT("MC_TOOTH_INSPECT_HAND tooth=%s sample=%d wide=%d stand=%s point=%s normal=%s delta_z=%.2f range_xy=%.2f reason=hand_or_handle_clearance_after_clear_capsule_and_sight"),
                        *Tooth->GetName(),Sample,Wide,*Stand.ToString(),*Point.ToString(),*Normal.ToString(),Delta.Z,Delta.Size2D());
                    return;
                }
                ++Count.Reachable;if(Wide) ++Count.WideReachable;else ++Count.NativeReachable;
                if(PrintedGood++<3) UE_LOG(LogTemp,Display,TEXT("MC_TOOTH_INSPECT_POSE tooth=%s sample=%d wide=%d stand=%s aim=%s facing=%s"),
                    *Tooth->GetName(),Sample,Wide,*Stand.ToString(),*Point.ToString(),*Facing.ToString());
                FNavLocation Projected;
                if(!Nav || !Nav->ProjectPointToNavigation(Stand,Projected,FVector(65,65,600),&Hero->GetNavAgentPropertiesRef())) {++Count.NavMissing;return;}
                FVector NavStand;
                if(!ResolveFloor(World,Hero,Tongue,Projected.Location,NavStand,Blocker,true) || !CapsuleClear(World,Hero,NavStand,Blocker)) {++Count.NavFloor;return;}
                const FRotator NavFacing(0,(Point-NavStand).Rotation().Yaw,0);
                if(!Hero->BrushContact->CanReachFromPose(NavStand,NavFacing,Point,Normal,Tooth)) {++Count.NavPose;return;}
                ++Count.NavReachable;
                if(Count.Paths>=32) {++Count.PathCapped;return;}
                ++Count.Paths;
                const auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Hero->GetActorLocation(),NavStand,Hero->GetController());
                if(Path && Path->IsValid() && !Path->IsPartial()) ++Count.PathReachable;else ++Count.PathRejected;
            };
            for(float Distance:{65.f,105.f,150.f}) for(float Tangent:{0.f,-65.f,65.f}) TestPose(Point+Out*Distance+Side*Tangent,false);
            for(float Angle:{-70.f,-35.f,35.f,70.f}) for(float Distance:{90.f,140.f,190.f})
                TestPose(Point+Out.RotateAngleAxis(Angle,FVector::UpVector)*Distance,true);
            TestPose(Point+Out*190.f,true);
        }
        UE_LOG(LogTemp,Display,TEXT("MC_TOOTH_INSPECT_RESULT tooth=%s mesh=%s coffee=%d remaining=%.5f dirty_samples=%d sampled=%d z=%.2f..%.2f candidates=%d floor_reject=%d capsule_reject=%d range_reject=%d normal_reject=%d los_reject=%d hand_reject=%d physical_reachable=%d native_pattern=%d wider_pattern=%d nav_missing=%d nav_floor_capsule_reject=%d nav_pose_reject=%d nav_reachable=%d paths_tested=%d path_reject=%d path_reachable=%d path_capped=%d first_floor=%s first_capsule=%s"),
            *Tooth->GetName(),*Mesh,Tooth->Status->State.CoffeeLeft,Tooth->RemainingGrime(),Points.Num(),Samples.Num(),MinZ,MaxZ,Count.Candidates,
            Count.Floor,Count.Capsule,Count.Range,Count.Normal,Count.Occluded,Count.Hand,Count.Reachable,Count.NativeReachable,Count.WideReachable,
            Count.NavMissing,Count.NavFloor,Count.NavPose,Count.NavReachable,Count.Paths,Count.PathRejected,Count.PathReachable,Count.PathCapped,*FirstFloor,*FirstCapsule);
    }
    UE_LOG(LogTemp,Display,TEXT("MC_TOOTH_INSPECT_END no inputs, transforms, health, masks, objectives or packages changed"));
}

FAutoConsoleCommandWithWorldAndArgs Command(TEXT("MC.Bots.InspectTooth"),
    TEXT("Development-only read-only stain reach inspection: MC.Bots.InspectTooth [mesh/name substring|all] [existing pawn substring]. Defaults to L01/R01/L05/L07; clean teeth are not dirtied."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World) {Inspect(World,Args);}));
}
#endif
