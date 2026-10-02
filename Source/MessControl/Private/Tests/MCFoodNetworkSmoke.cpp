#if !UE_BUILD_SHIPPING
#include "MCFoodActor.h"
#include "MCFoodBodyComponent.h"
#include "MCFoodCollectionComponent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCInventoryComponent.h"
#include "MCGameState.h"
#include "MCTongue.h"
#include "MCMouthSurface.h"
#include "MCReactionVFX.h"
#include "MCThroat.h"
#include "MCFirePatch.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"

void MCTickFoodNetworkValidation(UWorld* W)
{
    struct FRun {
        TWeakObjectPtr<UWorld> W;double At=0;int32 Stage=0,MaxStack=0;bool Setup=false,Failed=false,YawnSeen=false,StarsSeen=false,FireSeen=false,FireSuppressed=false;
        bool CollisionScaleSent=false,CollisionVariantSent=false,StableStackSeen=false,PickupHopSeen=false,PickupStretchSeen=false;
        TMap<TWeakObjectPtr<AMCToothCharacter>,int32> ServerMax;
    };static FRun R;if(R.W!=W) {R=FRun();R.W=W;}
    auto* GS=W->GetGameState<AMCGameState>();auto* PC=W->GetFirstPlayerController();auto* H=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    if(!GS || !H) return;
    AMCTongue* Tongue=nullptr;for(TActorIterator<AMCTongue> It(W);It;++It) {Tongue=*It;break;}if(!Tongue) return;
    auto Check=[&](bool OK,const TCHAR* Text){R.Failed|=!OK;UE_LOG(LogTemp,Display,TEXT("MC_FOOD_NETWORK_CHECK net=%d %s %s"),int32(W->GetNetMode()),OK?TEXT("PASS"):TEXT("FAIL"),Text);};
    const double Now=GS->GetServerWorldTimeSeconds();
    if(W->GetNetMode()!=NM_Client && !R.Setup) {
        if(GS->PlayerArray.Num()!=4 || W->GetTimeSeconds()<5) return;
        TArray<AMCToothCharacter*> Heroes;for(const auto& PS:GS->PlayerArray) if(auto* Hero=Cast<AMCToothCharacter>(PS->GetPawn())) Heroes.Add(Hero);
        if(Heroes.Num()!=4) return;
        if(auto* Mode=W->GetAuthGameMode()) Mode->SetActorTickEnabled(false);
        GS->bDevManualEvents=true;GS->Phase=EMCShiftPhase::Working;GS->PhaseEndsAt=0;GS->bPhysicalBrushes=false;GS->ForceNetUpdate();
        Tongue->bAutomaticYawns=false;Tongue->ResetPain();
        for(TActorIterator<AMCFoodActor> It(W);It;++It) It->Destroy();
        for(TActorIterator<AMCThroat> It(W);It;++It) It->SetActorTickEnabled(false);
        const auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
        const auto* Row=Table?Table->FindRow<FMCFoodRow>(TEXT("Egg"),TEXT("Food network")):nullptr;if(!Row) return;
        for(int32 I=0;I<Heroes.Num();++I) {
            auto* Hero=Heroes[I];FHitResult Floor;
            const FVector Probe=Tongue->Surface->Bounds.Origin+FVector(0,(I-1.5f)*280,0);
            if(!Tongue->SurfacePoint(Probe,Floor)) {Check(false,TEXT("fixture tongue supports all players"));continue;}
            Hero->CancelGameplayInput();Hero->Inventory->ServerSelect(EMCToolSlot::Brush);
            Hero->SetActorLocation(Floor.ImpactPoint+FVector(-80,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),false,nullptr,ETeleportType::TeleportPhysics);
            Hero->SetActorRotation(FRotator::ZeroRotator);Hero->GetCharacterMovement()->StopMovementImmediately();Hero->ForceNetUpdate();
            for(int32 J=0;J<3;++J) {
                const FTransform T(Floor.ImpactPoint+FVector(5+(J/2)*38,(J%2?1:-1)*32,40));
                auto* F=W->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),T,Hero,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
                FRandomStream Random(41+J);F->ConfigureItem(TEXT("Egg"),*Row,Random,true);F->Batch=91001;F->FinishSpawning(T);F->SpoilAt=Now+180;F->ForceNetUpdate();
            }
        }
        // Isolated from collection and hazard fixtures: exercise a scale change
        // after initial replication, followed by a whole-to-fragment mesh rebuild.
        const FTransform CollisionPose(FRotator(13,29,-8),FVector(18000,0,4000));
        auto* CollisionFood=W->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),CollisionPose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FRandomStream CollisionRandom(7);CollisionFood->ConfigureItem(TEXT("Egg"),*Row,CollisionRandom,false);
        CollisionFood->Batch=91003;CollisionFood->FinishSpawning(CollisionPose);CollisionFood->Body->SetEnableGravity(false);
        CollisionFood->SetActorTickEnabled(false);CollisionFood->ForceNetUpdate();
        R.Setup=true;R.At=Now;UE_LOG(LogTemp,Display,TEXT("MC_FOOD_NETWORK_START peers=4"));
    }
    if(W->GetNetMode()==NM_Client && !R.Setup) {
        for(TActorIterator<AMCFoodActor> It(W);It;++It) if(It->Batch==91001 && It->GetOwner()==H) {R.At=It->SpoilAt-180;R.Setup=true;break;}
    }
    if(!R.Setup) return;const double T=Now-R.At;
    if(W->GetNetMode()!=NM_Client) for(TActorIterator<AMCFoodActor> It(W);It;++It) if(It->Batch==91003) {
        if(T>2 && !R.CollisionScaleSent) {It->SetActorScale3D(FVector(20));It->ForceNetUpdate();R.CollisionScaleSent=true;}
        if(T>4 && !R.CollisionVariantSent) {
            FRandomStream CollisionRandom(7);const FMCFoodRow ReplicatedRow=It->FoodData;
            It->ConfigureItem(TEXT("Egg"),ReplicatedRow,CollisionRandom,true);It->Body->SetEnableGravity(false);
            It->ForceNetUpdate();R.CollisionVariantSent=true;
        }
    }
    if(H->IsLocallyControlled() && T>.8 && R.Stage==0) {H->ServerSetPrimary(true);H->ServerSetPrimary(false);++R.Stage;}
    R.MaxStack=FMath::Max(R.MaxStack,H->FoodCollection->Pieces.Num());
    for(const auto& Piece:H->FoodCollection->Pieces) if(IsValid(Piece) && Piece->StackCarrier==H && Piece->IsStackPickupActive()) {
        R.PickupHopSeen|=FVector::Dist(Piece->GetActorLocation(),H->FoodCollection->StackPose(Piece->StackPickup.SlotHeight).GetLocation())>8;
        R.PickupStretchSeen|=Piece->Visual->GetRelativeScale3D().Z>Piece->FoodData.FragmentScale.Z*1.07;
    }
    if(T>4 && T<5.8 && H->FoodCollection->Pieces.Num()>=3) {
        bool Held=true;for(const auto& Piece:H->FoodCollection->Pieces)
            Held&=IsValid(Piece) && Piece->StackCarrier==H && !Piece->Body->IsSimulatingPhysics();
        R.StableStackSeen|=Held;
    }
    R.YawnSeen|=H->IsYawning() && H->YawnTongue==Tongue && !H->FoodCollection->bCollecting && !H->bBrushing && !H->bHandling;
    for(TActorIterator<AMCReactionVFX> It(W);It;++It) R.StarsSeen|=It->Effect==EMCReactionEffect::Stars;
    if(W->GetNetMode()!=NM_Client) {
        for(TActorIterator<AMCToothCharacter> It(W);It;++It) R.ServerMax.FindOrAdd(*It)=FMath::Max(R.ServerMax.FindRef(*It),It->FoodCollection->Pieces.Num());
        if(T>6 && !Tongue->IsYawnActive() && Tongue->YawnStartedAt<0) {
            for(TActorIterator<AMCFoodActor> It(W);It;++It) if(It->Batch==91001) UE_LOG(LogTemp,Display,TEXT("MC_FOOD_NETWORK_FOOD owner=%s carrier=%s impacts=%d position=%s"),*GetNameSafe(It->GetOwner()),*GetNameSafe(It->StackCarrier),It->ConfirmedImpacts,*It->GetActorLocation().ToString());
            Check(Tongue->StartYawn(3),TEXT("server starts the yawn event"));
            for(TActorIterator<AMCToothCharacter> It(W);It;++It) {
                FHitResult Floor;const bool HasFloor=Tongue->SurfacePoint(It->GetActorLocation(),Floor);
                UE_LOG(LogTemp,Display,TEXT("MC_FOOD_NETWORK_YAWN hero=%s active=%d anchor=%s floor=%d gap=%.2f position=%s"),*It->GetName(),It->IsYawning(),*GetNameSafe(It->YawnTongue),HasFloor,HasFloor?It->GetActorLocation().Z-It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-Floor.ImpactPoint.Z:9999,*It->GetActorLocation().ToString());
                It->NotifyTaskFeedback(true);
            }
        }
    }
    if(W->GetNetMode()!=NM_Client && T>10 && R.Stage==1) {
        for(TActorIterator<AMCToothCharacter> It(W);It;++It) {
            It->SetActorRotation(FRotator::ZeroRotator);FHitResult Floor;
            if(Tongue->SurfacePoint(It->GetActorLocation()+FVector(130,0,0),Floor)) {
                auto* Fire=AMCFirePatch::Ignite(*It,Floor.ImpactPoint,48,0,91002,false);if(Fire) Fire->SetOwner(*It);
            }
        }
        ++R.Stage;
    }
    AMCFirePatch* OwnedFire=nullptr;
    for(TActorIterator<AMCFirePatch> It(W);It;++It) if(It->Batch==91002 && It->GetOwner()==H) {
        OwnedFire=*It;R.FireSeen|=It->Heat>.1f;R.FireSuppressed|=It->Heat<=0;
    }
    if(H->IsLocallyControlled() && T>11 && R.Stage<3 && OwnedFire && OwnedFire->IsBurning()) {
        H->SetActorRotation((OwnedFire->GetActorLocation()-H->GetActorLocation()).GetSafeNormal2D().Rotation());
        H->Inventory->ServerSelect(EMCToolSlot::Spray);H->ServerSetPrimary(true);R.Stage=3;
    }
    if(T>(W->GetNetMode()==NM_Client?16:18)) {
        H->ServerSetPrimary(false);
        Check(R.FireSeen && R.FireSuppressed,TEXT("owning client spray extinguishes replicated persistent fire"));
        Check(R.MaxStack>=3,TEXT("owning input produced a replicated three-piece physical stack"));
        Check(R.StableStackSeen,TEXT("all collected pieces stay held without falling on this peer"));
        Check(R.PickupHopSeen && R.PickupStretchSeen,TEXT("this peer observed the timed pickup hop and visual stretch"));
        Check(R.YawnSeen && H->CanWork() && !H->IsYawning(),TEXT("replicated yawn interrupted collection, gripped tongue and released movement"));
        Check(R.StarsSeen,TEXT("completion VFX replicated to this peer"));
        AMCFoodActor* CollisionFood=nullptr;
        for(TActorIterator<AMCFoodActor> It(W);It;++It) if(It->Batch==91003) {CollisionFood=*It;break;}
        const auto* CollisionBody=CollisionFood?Cast<UMCFoodBodyComponent>(CollisionFood->Body):nullptr;
        const auto* CollisionSetup=CollisionBody?const_cast<UMCFoodBodyComponent*>(CollisionBody)->GetBodySetup():nullptr;
        const auto* SourceSetup=CollisionFood && CollisionFood->ItemMesh?CollisionFood->ItemMesh->GetBodySetup():nullptr;
        Check(CollisionFood && CollisionFood->bFragment && CollisionFood->GetActorScale3D().Equals(FVector(20),.01),TEXT("late actor scale x20 and subsequent fragment replacement replicate to this peer"));
        Check(CollisionBody && CollisionBody->HasMeshCollision() && CollisionSetup && SourceSetup
            && CollisionSetup->AggGeom.BoxElems.IsEmpty()
            && CollisionSetup->AggGeom.ConvexElems.Num()==SourceSetup->AggGeom.ConvexElems.Num()+SourceSetup->AggGeom.BoxElems.Num(),
            TEXT("peer reconstructs the authored fragment convex hulls without box physics"));
        bool CentreHit=false;
        if(CollisionFood) {
            const FVector E=CollisionFood->Body->GetUnscaledBoxExtent();const FTransform Pose=CollisionFood->GetActorTransform();
            FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(FoodNetworkCollision),false);
            CentreHit=CollisionFood->Body->LineTraceComponent(Hit,Pose.TransformPosition(FVector(-E.X*1.5,0,0)),Pose.TransformPosition(FVector(E.X*1.5,0,0)),Query);
        }
        Check(CentreHit,TEXT("replicated x20 convex body answers a real simple collision query"));
        if(W->GetNetMode()!=NM_Client) {int32 Count=0;for(const auto& Entry:R.ServerMax) Count+=Entry.Value>=3;Check(Count==4,TEXT("server observed physical stacks from all four players"));}
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s FOOD_NETWORK net=%d stack=%d yawn=%d stars=%d"),R.Failed?TEXT("FAIL"):TEXT("PASS"),int32(W->GetNetMode()),R.MaxStack,R.YawnSeen,R.StarsSeen);
        FPlatformMisc::RequestExitWithStatus(false,R.Failed?1:0);
    }
}
#endif
