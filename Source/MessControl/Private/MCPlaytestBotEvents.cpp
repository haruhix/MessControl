#include "MCPlaytestBotController.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCNutEnemy.h"
#include "MCThroat.h"
#include "MCTongue.h"
#include "MCIceEvent.h"
#include "MCSmallMintCandy.h"
#include "MCHazardWave.h"
#include "MCFogBrawlEvent.h"
#include "MCArenaTooth.h"
#include "MCGripComponent.h"
#include "MCCoffeeFlood.h"
#include "MCToothPhysicsComponent.h"
#include "MCGameState.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"
#include "HAL/IConsoleManager.h"

bool AMCPlaytestBotController::HandleCoffeeSurvival(float DeltaSeconds)
{
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    if(State && (State->Phase!=EMCShiftPhase::Working || State->bLobbyWaiting || State->bDayOneComplete)) return false;
    const FVector Position=Hero->GetActorLocation();
    AMCCoffeeFlood* Water=nullptr;
    for(TActorIterator<AMCCoffeeFlood> It(GetWorld());It;++It) {
        if(!It->IsActive() || Hero->IsSeatedOnRaft()) continue;
        const FVector Offset=Position-It->ArenaCenter;
        if(FMath::Abs(Offset.X)>It->HalfSize.X+600 || FMath::Abs(Offset.Y)>It->HalfSize.Y+600) continue;
        Water=*It;break;
    }
    if(!Water) {
        if(bCoffeeSurvival) {
            ReleaseInputs();Hero->ServerPaddle(FVector2D::ZeroVector);
            Hero->ConsumeMovementInputVector();StopMovement();Target.Reset();ResetApproach();
            Goal=EMCPlaytestBotGoal::Idle;NextTaskAt=NextMoveAt=0;
        }
        CoffeeThreat.Reset();bCoffeeSurvival=bCoffeeHasSafePoint=bCoffeeWasWet=false;
        return false;
    }
    if(!Hero->CanWork() && !Hero->bInCoffee) return false;
    const double Now=GetWorld()->GetTimeSeconds();
    if(CoffeeThreat.Get()!=Water || !bCoffeeSurvival) {
        StopMovement();ReleaseInputs();Target.Reset();ResetApproach();
        CoffeeThreat=Water;bCoffeeSurvival=true;bCoffeeHasSafePoint=bCoffeeWasWet=false;NextCoffeePlanAt=0;
    }
    Goal=EMCPlaytestBotGoal::Escape;
    // Decide must not release the handhold or brake on every decision. Movement
    // is submitted once per Tick, including before the approaching front is wet.
    if(DeltaSeconds<=0) return true;
    const float Half=Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const float Margin=Hero->GetCapsuleComponent()->GetScaledCapsuleRadius()+140.f;
    const FVector Sole=Position-FVector(0,0,Half-10.f);
    const bool Wet=Hero->bInCoffee || Water->Contains(Sole) || Hero->GetCharacterMovement()->IsSwimming();
    if(Wet && !bCoffeeWasWet) {StopMovement();Hero->ConsumeMovementInputVector();}
    bCoffeeWasWet=Wet;
    const FVector Downstream=Water->bRiverFlood?Water->RiverDirection.GetSafeNormal2D():Water->StormStreamDirection();
    const FVector Side(-Downstream.Y,Downstream.X,0);
    const float Span=FMath::Abs(Downstream.X)*Water->HalfSize.X+FMath::Abs(Downstream.Y)*Water->HalfSize.Y;
    const float Width=FMath::Abs(Side.X)*Water->HalfSize.X+FMath::Abs(Side.Y)*Water->HalfSize.Y;
    AMCTongue* Tongue=nullptr;
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) if(!It->CurrentVertices().IsEmpty()) {Tongue=*It;break;}
    if(Now>=NextCoffeePlanAt) {
        NextCoffeePlanAt=Now+.6;
        if(Tongue && Water->bRiverFlood && bCoffeeHasSafePoint) {
            FHitResult Floor;
            bCoffeeHasSafePoint=Tongue->InteriorSurfacePoint(CoffeeSafePoint,Margin,Floor);
            if(bCoffeeHasSafePoint) CoffeeSafePoint.Z=Floor.ImpactPoint.Z+Half+4.f;
        }
        // Do not run to the closest rear tooth after a delivery: the crest can
        // carry that runner through the open throat. Spend its approach time
        // retreating into the supported front/centre, leaving downstream room.
        const float Across=FMath::Clamp(float(FVector::DotProduct(Position-Water->ArenaCenter,Side)),-Width*.4f,Width*.4f);
        if(Water->bRiverFlood || !bCoffeeHasSafePoint) {
            for(float Depth:{-.25f,-.05f}) {
                if(bCoffeeHasSafePoint) break;
                for(float Lateral:{Across,0.f,-Width*.25f,Width*.25f}) {
                    FVector Candidate=Water->ArenaCenter+Downstream*(Span*Depth)+Side*Lateral;
                    FHitResult Floor;
                    if(Tongue) {
                        if(!Tongue->InteriorSurfacePoint(Candidate,Margin,Floor)) continue;
                        Candidate=Floor.ImpactPoint+FVector(0,0,Half+4.f);
                        if(!IsApproachPositionClear(Candidate)) continue;
                    } else Candidate.Z=Position.Z;
                    CoffeeSafePoint=Candidate;bCoffeeHasSafePoint=true;break;
                }
            }
        }
        if(!Water->bRiverFlood) {
            // Deep legacy water has real teeth to cling to. Prefer a reachable
            // surface, rather than a tooth pivot potentially inside the gums.
            double Best=DBL_MAX;
            for(TActorIterator<AMCArenaTooth> It(GetWorld());It;++It) if(It->IsAvailable()) {
                const FVector Contact=It->Body->Bounds.GetBox().GetClosestPointTo(Position);
                const double Distance=FVector::DistSquared2D(Position,Contact);
                if(Distance<Best) {Best=Distance;CoffeeSafePoint=Contact;bCoffeeHasSafePoint=true;}
            }
        }
    }
    if(!bCoffeeHasSafePoint) {CoffeeSafePoint=Water->ArenaCenter-Downstream*FMath::Min(500.f,Span*.25f);CoffeeSafePoint.Z=Position.Z;}
    const FVector ToSafety=CoffeeSafePoint-Position;
    FVector Current=Water->FlowAtPosition(Water->bRiverFlood?Sole:Position,Hero);Current.Z=0;
    const FVector Desired=ToSafety.GetClampedToMaxSize(400.f)-(Water->bRiverFlood?Current/2.4f:Current);
    const FVector Direction=Desired.GetSafeNormal2D();
    if(Hero->CanWork()) {
        // The river deliberately disables old LMB cling. Do not issue blind
        // RMB grabs here: its normal contextual query can grab a teammate/food
        // instead of scenery. Early retreat leaves room to ride out the crest.
        if(!Water->bRiverFlood && Wet && !Hero->IsPrimaryHeld()) Hero->ServerSetPrimary(true);
        // Sprint's press starts a native dash using the previous movement
        // acceleration. Let ordinary steering turn upstream before that press.
        const FVector Acceleration=Hero->GetCharacterMovement()->GetCurrentAcceleration().GetSafeNormal2D();
        Hero->SetSprintInputHeld(!Hero->GetCharacterMovement()->IsSwimming()
            && FVector::DotProduct(Acceleration,Direction)>.65f);
    }
    if(!Wet && bCoffeeHasSafePoint) {
        auto* Nav=UNavigationSystemV1::GetCurrent(GetWorld());FNavLocation Projected;
        if(Nav && Nav->ProjectPointToNavigation(CoffeeSafePoint,Projected,FVector(65,65,350),&Hero->GetNavAgentPropertiesRef())) {
            MoveTowards(CoffeeSafePoint,45.f);
            if(GetMoveStatus()==EPathFollowingStatus::Moving) return true;
        }
    }
    // ServerPaddle drives the old ragdoll only. Upright CMC swimming and shallow
    // river walking consume ordinary movement input/acceleration instead.
    if(!Direction.IsNearlyZero()) Hero->AddMovementInput(Direction);
    if(Hero->bInCoffee && !Hero->ToothPhysics->CanAct()) Hero->ServerPaddle(FVector2D(Direction.X,Direction.Y));
    if(!Direction.IsNearlyZero()) SetControlRotation(Direction.Rotation());
    return true;
}

namespace
{
bool NeedsBotFoodDiscard(const AMCFoodActor* Food)
{ return IsValid(Food) && (Food->bSpoiled || Food->IsWrongIngredient() || Food->FoodData.Kind==EMCFoodKind::Spicy); }

TAutoConsoleVariable<int32> FoodThrowDebug(TEXT("mc.Bots.ThrowDebug"),0,
    TEXT("Log at most one AI food trajectory result per worker every two game seconds (0 off, 1 on)."));

void LogFoodArc(AMCToothCharacter* Hero,AMCFoodActor* Food,bool HighArc,FVector Launch,FVector Aim,FVector Velocity,
    FVector HalfExtent,FQuat Rotation,float Time,const FHitResult* Block)
{
    if(!FoodThrowDebug.GetValueOnGameThread()) return;
    static TMap<TWeakObjectPtr<AMCToothCharacter>,double> NextLog;
    for(auto It=NextLog.CreateIterator();It;++It) if(!It.Key().IsValid()) It.RemoveCurrent();
    const double Now=Hero->GetWorld()->GetTimeSeconds();
    double& Due=NextLog.FindOrAdd(Hero);
    if(Now<Due) return;
    Due=Now+2.;
    UE_LOG(LogTemp,Display,TEXT("MC_BOT_THROW_%s hero=%s food=%s shape=box channel=%d high=%d start=%s aim=%s velocity=%s half_extent=%s rotation=%s time=%.3f blocker=%s component=%s hit=%s initial_overlap=%d"),
        Block?TEXT("BLOCKED"):TEXT("CLEAR"),*GetNameSafe(Hero),*GetNameSafe(Food),int32(Food->Body->GetCollisionObjectType()),HighArc?1:0,
        *Launch.ToString(),*Aim.ToString(),*Velocity.ToString(),*HalfExtent.ToString(),*Rotation.ToString(),Time,
        *GetNameSafe(Block?Block->GetActor():nullptr),*GetNameSafe(Block?Block->GetComponent():nullptr),
        Block?*Block->ImpactPoint.ToString():TEXT("none"),Block && Block->bStartPenetrating?1:0);
}

bool IsFoodBoxFlightClear(AMCToothCharacter* Hero,AMCFoodActor* Food,FVector Launch,FVector Aim,FVector Velocity,float FlightSeconds,bool HighArc,const FQuat& Initial)
{
    const FVector HalfExtent=Food->Body->GetScaledBoxExtent();
    const double Gravity=Hero->GetWorld()->GetGravityZ();
    // Match LaunchManualThrow's world-space spin and damping. A sphere around
    // the box diagonal falsely touches the tongue for wide, flat food pieces.
    const FVector AngularVelocity=FVector::CrossProduct(FVector::UpVector,Velocity.GetSafeNormal()).GetSafeNormal()*1.6f+FVector::UpVector*.55f;
    const double AngularSpeed=AngularVelocity.Size();
    const FVector AngularAxis=AngularVelocity.GetSafeNormal();
    auto AngleAt=[AngularSpeed](double Time) {return AngularSpeed*(1-FMath::Exp(-.12*Time))/.12;};
    auto RotationAt=[&](double Time) {return (FQuat(AngularAxis,AngleAt(Time))*Initial).GetNormalized();};
    auto PositionAt=[&](double Time) {return Launch+Velocity*Time+FVector(0,0,.5*Gravity*Time*Time);};
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCPlaytestFoodBoxFlight),false,Hero);
    Query.AddIgnoredActor(Food);
    for(const auto& Piece:Hero->FoodCollection->Pieces) if(IsValid(Piece)) Query.AddIgnoredActor(Piece.Get());
    if(IsValid(Hero->HeldFood)) Query.AddIgnoredActor(Hero->HeldFood.Get());
    if(Hero->Grip && IsValid(Hero->Grip->Secondary.Food)) Query.AddIgnoredActor(Hero->Grip->Secondary.Food.Get());
    FCollisionResponseParams Response(Food->Body->GetCollisionResponseToChannels());
    // OnRep_Phase restores these two blocking responses as Q releases a
    // tower slot. Forecast the free projectile, not its kinematic pickup state.
    Response.CollisionResponse.SetResponse(ECC_PhysicsBody,ECR_Block);
    Response.CollisionResponse.SetResponse(AMCFoodActor::FragmentCollisionChannel,ECR_Block);
    const int32 Steps=FMath::Clamp(FMath::CeilToInt(FlightSeconds*30.f),1,300);
    for(int32 Index=0;Index<Steps;++Index) {
        const double Before=FlightSeconds*double(Index)/Steps,After=FlightSeconds*double(Index+1)/Steps;
        const FQuat Rotation=RotationAt((Before+After)*.5);
        // A sweep has one orientation. This small envelope covers rotation
        // either side of the midpoint, plus the ballistic chord's gravity sag.
        const double AngularMargin=2*HalfExtent.Size()*FMath::Sin((AngleAt(After)-AngleAt(Before))*.25);
        const double Sag=FMath::Abs(Gravity)*FMath::Square(After-Before)/8.;
        const FVector Extent=HalfExtent+FVector(AngularMargin+Sag+.1);
        FHitResult Hit;
        if(Hero->GetWorld()->SweepSingleByChannel(Hit,PositionAt(Before),PositionAt(After),Rotation,
            Food->Body->GetCollisionObjectType(),FCollisionShape::MakeBox(Extent),Query,Response)) {
            LogFoodArc(Hero,Food,HighArc,Launch,Aim,Velocity,Extent,Rotation,float(Before),&Hit);return false;
        }
    }
    LogFoodArc(Hero,Food,HighArc,Launch,Aim,Velocity,HalfExtent,Initial,FlightSeconds,nullptr);return true;
}

bool SolveClearFoodArc(AMCToothCharacter* Hero,AMCFoodActor* Food,FVector Launch,FVector Aim,FVector& Velocity,const FQuat* PredictedRotation=nullptr)
{
    if(!IsValid(Hero) || !IsValid(Food) || !Food->Body || !Hero->FoodCollection) return false;
    for(const bool HighArc:{false,true}) {
        UGameplayStatics::FSuggestProjectileVelocityParameters Arc(Hero,Launch,Aim,Hero->FoodCollection->EffectiveThrowSpeed());
        Arc.OverrideGravityZ=Hero->GetWorld()->GetGravityZ();Arc.TraceOption=ESuggestProjVelocityTraceOption::DoNotTrace;Arc.bFavorHighArc=HighArc;
        if(!UGameplayStatics::SuggestProjectileVelocity(Arc,Velocity) || Velocity.Size2D()<1) continue;
        const float FlightSeconds=FVector::Dist2D(Launch,Aim)/Velocity.Size2D();
        if(!FMath::IsFinite(FlightSeconds) || FlightSeconds<=0 || FlightSeconds>10) continue;
        // Throat tissue, gums, teeth and containment remain in the query.
        if(IsFoodBoxFlightClear(Hero,Food,Launch,Aim,Velocity,FlightSeconds,HighArc,
            PredictedRotation?*PredictedRotation:Food->Body->GetComponentQuat())) return true;
    }
    return false;
}
}

AMCFoodActor* AMCPlaytestBotController::CarriedFood() const
{
    if(!Hero) return nullptr;
    if(Hero->FoodCollection) for(int32 Index=Hero->FoodCollection->Pieces.Num()-1;Index>=0;--Index)
        if(IsValid(Hero->FoodCollection->Pieces[Index])) return Hero->FoodCollection->Pieces[Index];
    return IsValid(Hero->HeldFood)?Hero->HeldFood.Get():nullptr;
}

void AMCPlaytestBotController::RefreshFoodLoadProgress()
{
    const int32 Count=Hero->FoodCollection->Pieces.Num();
    const double Now=GetWorld()->GetTimeSeconds();
    if(Count>LastFoodLoadCount) {
        if(LastFoodLoadCount==0) {CollectionOrigin=Hero->GetActorLocation();CollectStartedAt=Now;}
        LastCollectedAt=Now;LastWorkProgressAt=Now;
    }
    if(Count==0 && !IsValid(Hero->HeldFood)) {bDeliveringTower=false;CollectStartedAt=LastCollectedAt=0;}
    LastFoodLoadCount=Count;
}

bool AMCPlaytestBotController::ContinueFoodCollection()
{
    auto* Collection=Hero->FoodCollection.Get();
    auto* Top=CarriedFood();
    if(!Top || Collection->Pieces.IsEmpty() || bDeliveringTower || Top->UsesLegacyGrip()
        || (Goal==EMCPlaytestBotGoal::Fight && Cast<AMCNutEnemy>(Target.Get()))) return false;
    const double Now=GetWorld()->GetTimeSeconds();
    const int32 Capacity=FMath::Clamp(FMath::Min(Collection->EffectiveCapacity(),Collection->MaxPieces),1,8);
    // Build a useful tower through the ordinary E pickup cadence. A bounded
    // search/age budget still delivers the last piece and food close to spoiling.
    if(Collection->Pieces.Num()>=Capacity || Now-CollectStartedAt>=10. || Now-LastCollectedAt>=2.5) return false;
    for(const auto& Piece:Collection->Pieces)
        if(IsValid(Piece) && Piece->SpoilAt>Now && Piece->SpoilAt<Now+12.) return false;
    AMCFoodActor* Best=nullptr;double BestScore=DBL_MAX;
    for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) {
        auto* Food=*It;
        if(!Food->bFragment || Food->bBrushTool || Food->UsesLegacyGrip() || Food->IsDisposed()
            || Food->StackCarrier || !Food->Holders.IsEmpty() || NeedsBotFoodDiscard(Food)!=NeedsBotFoodDiscard(Top)
            || (Food->Phase!=EMCFoodPhase::Free && Food->Phase!=EMCFoodPhase::Falling)) continue;
        const double Distance=FVector::DistSquared2D(Food->GetActorLocation(),Hero->GetActorLocation());
        if(Distance>FMath::Square(700.f) || FVector::DistSquared2D(Food->GetActorLocation(),CollectionOrigin)>FMath::Square(950.f)
            || (!IsKnownMealFood(Food) && !Collection->CanCollect(Food) && !CanObserve(Food))) continue;
        if(const double* Retry=FailedTargets.Find(Food);Retry && Now<*Retry) continue;
        bool Claimed=false;
        for(TActorIterator<AMCPlaytestBotController> Other(GetWorld());Other;++Other)
            if(*Other!=this && Other->Goal==EMCPlaytestBotGoal::CollectFood && Other->Target.Get()==Food
                && IsValid(Other->Hero) && FVector::DistSquared2D(Other->Hero->GetActorLocation(),Food->GetActorLocation())<FMath::Square(700.f)) {Claimed=true;break;}
        if(Claimed) continue;
        if(Distance<BestScore) {Best=Food;BestScore=Distance;}
    }
    if(!Best) {
        if(Now-LastCollectedAt>.8) return false;
        ReleaseInputs();StopMovement();return true; // Allow the last pickup animation to settle.
    }
    if(Target.Get()!=Best || Goal!=EMCPlaytestBotGoal::CollectFood) {
        ReleaseInputs();ResetApproach();Target=Best;Goal=EMCPlaytestBotGoal::CollectFood;NextMoveAt=0;
        LastWorkProgressAt=Now;LastTargetProgress=TargetProgress();
    }
    // WorkAtTarget holds the same E collection action as a player and walks to
    // the next nearby fragment; it never attaches, teleports or invents pieces.
    WorkAtTarget();return true;
}

bool AMCPlaytestBotController::IsDeliveryStandReserved(FVector Stand) const
{
    for(TActorIterator<AMCPlaytestBotController> Other(GetWorld());Other;++Other) {
        if(*Other==this || Other->Goal!=EMCPlaytestBotGoal::Deliver || !Other->bHasWorkApproach
            || !IsValid(Other->Hero) || !Other->Hero->CanWork() || !Other->CarriedFood()) continue;
        if(FVector::DistSquared2D(Stand,Other->WorkStand)<FMath::Square(210.f)) return true;
    }
    return false;
}

void AMCPlaytestBotController::PulsePrimary()
{
    const double Now=GetWorld()->GetTimeSeconds();
    if((Hero->Inventory->Selected==EMCToolSlot::Pickaxe && Hero->Inventory->HasUpgrade(EMCToolUpgrade::Buffer))
        || (Hero->Inventory->Selected==EMCToolSlot::Knife && Hero->Inventory->HasUpgrade(EMCToolUpgrade::Chainsaw))) {
        PrimaryReleaseAt=0;Hero->SetPrimaryInputHeld(true);return;
    }
    // Ordinary combat is an edge-triggered combo. Keep the press long enough
    // for input processing, then release in Tick; no direct hit or damage calls.
    if(Now<NextPrimaryAt || PrimaryReleaseAt>0) return;
    Hero->SetPrimaryInputHeld(false);Hero->SetPrimaryInputHeld(true);
    PrimaryReleaseAt=Now+.12;
    NextPrimaryAt=Now+FMath::Max(.35f,Hero->Inventory->SwingDuration()+.08f);
}

bool AMCPlaytestBotController::WorkFoodDelivery(AMCFoodDisposal* Zone)
{
    AMCFoodActor* Food=CarriedFood();
    if(!IsValid(Zone) || !Food) {ReleaseInputs();Target.Reset();Goal=EMCPlaytestBotGoal::Idle;return false;}
    const bool Discard=Food->bSpoiled || Food->IsWrongIngredient() || Food->FoodData.Kind==EMCFoodKind::Spicy;
    if(Discard!=Zone->bBrushBin) {Target.Reset();NextTaskAt=0;return false;}
    const double Now=GetWorld()->GetTimeSeconds();
    auto* Throat=Cast<AMCThroat>(Zone);
    if((Throat && Throat->bManualIntake) || Zone->bBrushBin) {
        FailedApproaches.RemoveAll([Now](const auto& Entry){return Now>=Entry.Value;});
        FVector Aim,Direction,FrontLip=FVector::ZeroVector;float Radius=0;
        AMCTongue* DiscardTongue=nullptr;
        if(Throat) {
            if(!Throat->GetIntakeAimTarget(Aim,Radius)) {ReleaseInputs();StopMovement();return true;}
            Direction=Throat->GetActorForwardVector().GetSafeNormal2D();
        } else {
            TArray<FVector> Outer,Inner;
            for(TActorIterator<AMCTongue> It(GetWorld());It;++It) if(!It->CurrentVertices().IsEmpty()) {DiscardTongue=*It;break;}
            if(!DiscardTongue || !Zone->GetDeliveryZoneOutline(Outer,Inner) || Outer.Num()<2) {FailPath();return true;}
            Direction=Zone->GetDeliveryDirection();
            // The retired box can be far inside the replacement tongue. Use
            // its actual front silhouette, just as native spoiled disposal does.
            FrontLip=Outer[Outer.Num()/2];FHitResult LipFloor;
            if(!DiscardTongue->SurfacePoint(FrontLip-Direction*25.f,LipFloor)) {FailPath();return true;}
            FrontLip.Z=LipFloor.ImpactPoint.Z;
            const FBox Envelope=Food->Body->Bounds.GetBox()+Food->Visual->Bounds.GetBox();
            Radius=float(Envelope.GetExtent().Size()); // clears the full body during its native spin
            Aim=FrontLip+Direction*(Radius+180.f);
            // The lip slopes below the front teeth. Aim above the real arena
            // and current load, not relative to that submerged silhouette edge;
            // the existing box sweeps must still clear teeth along the full arc.
            const double ThrowHeight=FMath::Max(DiscardTongue->Surface->Bounds.GetBox().Max.Z,Food->Body->Bounds.Origin.Z);
            Aim.Z=FMath::Max(double(FrontLip.Z+FMath::Max(240.f,Radius+80.f)),ThrowHeight+Radius+240.f);
        }
        // Stand on tissue, before the aperture/front edge, and toss from range.
        // Walking inside the old green delivery strip is lethal at the throat.
        if(bHasWorkApproach && (!IsApproachPositionClear(WorkStand) || IsDeliveryStandReserved(WorkStand))) ResetApproach(true);
        if(!bHasWorkApproach) {
            const FVector Base=Throat?Aim:FrontLip;
            const FVector Side=FVector::CrossProduct(FVector::UpVector,Direction);
            double Best=DBL_MAX;FVector Chosen=FVector::ZeroVector;
            // Already standing on exposed tissue with a clear ordinary arc:
            // throw there instead of taking an unnecessary trip to the lip.
            FHitResult CurrentFloor;FVector CurrentVelocity;
            FCollisionQueryParams FloorQuery(SCENE_QUERY_STAT(MCBotDeliveryFloor),false,Hero);
            for(const auto& Piece:Hero->FoodCollection->Pieces) FloorQuery.AddIgnoredActor(Piece.Get());
            const FVector Position=Hero->GetActorLocation();
            const float Height=Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            const float BackDistance=float(FVector::DotProduct(Aim-Position,Direction));
            const bool Supported=DiscardTongue?DiscardTongue->InteriorSurfacePoint(Position,
                Hero->GetCapsuleComponent()->GetScaledCapsuleRadius()+20.f,CurrentFloor)
                :GetWorld()->LineTraceSingleByChannel(CurrentFloor,Position,Position-FVector(0,0,Height+40.f),ECC_Visibility,FloorQuery)
                    && CurrentFloor.ImpactNormal.Z>=Hero->GetCharacterMovement()->GetWalkableFloorZ()
                    && !Cast<APawn>(CurrentFloor.GetActor()) && !Cast<AMCFoodActor>(CurrentFloor.GetActor());
            if(Supported && (!Throat || (BackDistance>=400.f && BackDistance<=1800.f))
                && FMath::Abs(Hero->GetActorLocation().Z-Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-CurrentFloor.ImpactPoint.Z)<30.f
                && !IsDeliveryStandReserved(Position)
                && IsApproachPositionClear(Hero->GetActorLocation())
                && SolveClearFoodArc(Hero,Food,Food->GetActorLocation(),Aim,CurrentVelocity)) {Best=0;Chosen=Hero->GetActorLocation();}
            // A tall real stack needs distance to descend through the aperture;
            // a close downward throw can hit the tissue above the opening.
            for(float Back:{700.f,1000.f,500.f,1300.f,1600.f,350.f}) for(float Lateral:{PreferredDeliveryLane,0.f,-270.f,270.f,-540.f,540.f}) {
                FVector Stand;
                if(!ProjectStandingPosition(Base-Direction*Back+Side*Lateral,Stand)) continue;
                if((Throat && Throat->ContainsDeliveryPosition(Stand)) || IsDeliveryStandReserved(Stand)) continue;
                FHitResult Floor;
                if(DiscardTongue && (!DiscardTongue->InteriorSurfacePoint(Stand,Hero->GetCapsuleComponent()->GetScaledCapsuleRadius()+20.f,Floor)
                    || FMath::Abs(Stand.Z-Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-Floor.ImpactPoint.Z)>35.f)) continue;
                bool Failed=false;for(const auto& Bad:FailedApproaches) if(FVector::DistSquared2D(Bad.Key,Stand)<FMath::Square(80.f)) {Failed=true;break;}
                if(Failed) continue;
                const double Distance=FVector::DistSquared2D(Hero->GetActorLocation(),Stand);
                const double Score=Distance+FMath::Square(Lateral-PreferredDeliveryLane)*.25;
                if(Score>=Best) continue;
                // Turning at the destination rotates the hand and every tower
                // slot. Predict that pose, not the old world-space hand offset.
                const FQuat Facing=FRotator(0,(Aim-Stand).Rotation().Yaw,0).Quaternion();
                const FQuat Turn=Facing*Hero->GetActorQuat().Inverse();
                const FVector Launch=Stand+Turn.RotateVector(Food->GetActorLocation()-Hero->GetActorLocation());
                const FQuat FoodRotation=(Turn*Food->Body->GetComponentQuat()).GetNormalized();
                FVector Velocity;if(!SolveClearFoodArc(Hero,Food,Launch,Aim,Velocity,&FoodRotation)) continue;
                if(Distance>FMath::Square(45.f)) {
                    const auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Hero->GetActorLocation(),Stand,this);
                    if(!Path || !Path->IsValid() || Path->IsPartial()) continue;
                }
                Best=Score;Chosen=Stand;
            }
            if(Best==DBL_MAX) {FailPath();return true;}
            WorkStand=Chosen;WorkAim=Aim;bHasWorkApproach=true;
        }
        if(FVector::DistSquared2D(Hero->GetActorLocation(),WorkStand)>FMath::Square(45.f)) {
            ReleaseInputs();MoveTowards(WorkStand,25);return true;
        }
        StopMovement();
        if(!IsValid(Hero->HeldFood)) {Hero->SetPrimaryInputHeld(false);Hero->SetHandleInputHeld(false);}
        Hero->SetBraceInputHeld(false);Hero->SetSelfCareInput(false);
        const FRotator Facing=(Aim-Hero->GetActorLocation()).Rotation();SetControlRotation(Facing);
        if(FMath::Abs(FMath::FindDeltaAngleDegrees(Hero->GetActorRotation().Yaw,Facing.Yaw))>3.f) DeliveryPoseReadyAt=Now+.15;
        Hero->SetActorRotation(FRotator(0,Facing.Yaw,0));
        if(Now<DeliveryPoseReadyAt || Food->IsStackPickupActive()) return true;
        if(Now>=NextThrowAt) {
            // Feed the same direction RPC as Q. The player's camera reticle
            // belongs to a local PlayerController, so an AI solves its arc to
            // the visible aperture directly instead of borrowing that camera.
            FVector Velocity;
            if(!SolveClearFoodArc(Hero,Food,Food->GetActorLocation(),Aim,Velocity)) {
                ResetApproach(true);NextMoveAt=0;NextThrowAt=Now+.3;return true;
            }
            Hero->ServerSetThrowHeld(true,Velocity.GetSafeNormal());Hero->ServerSetThrowHeld(false,Velocity.GetSafeNormal());
            NextThrowAt=Now+Hero->FoodCollection->ThrowIntervalSeconds()+.08;
            const bool Released=Food->StackCarrier!=Hero && Hero->HeldFood!=Food && !Hero->FoodCollection->Contains(Food);
            if(Released) {
                LastWorkProgressAt=Now;
                if(FoodThrowDebug.GetValueOnGameThread()) UE_LOG(LogTemp,Display,
                    TEXT("MC_BOT_THROW_RELEASE hero=%s food=%s remaining=%d manual=%d time=%.3f"),
                    *Hero->GetPathName(),*Food->GetPathName(),Hero->FoodCollection->Pieces.Num(),Food->WasManuallyThrown()?1:0,Now);
            }
            else if(Now-LastWorkProgressAt>3.) {ResetApproach(true);NextMoveAt=0;}
        }
        return true;
    }
    // Retain explicitly enabled legacy fixture zones without restoring them to
    // the game map. They still receive the player's normal collection release.
    if(!Zone->bEnableLegacyDeliveryZone) {FailPath();return true;}
    if(Zone->ContainsDeliveryPosition(Hero->FoodCollection->HandPoint()) || Zone->ContainsDeliveryPosition(Hero->GetActorLocation())) {
        StopMovement();ReleaseInputs(true);Target.Reset();Goal=EMCPlaytestBotGoal::Idle;NextTaskAt=0;
    } else MoveTowards(TaskPoint(Zone),30);
    return true;
}

bool AMCPlaytestBotController::RescueFrozenLegs()
{
    if(!Hero->HasFrozenLegs()) {
        AMCToothCharacter* Ally=nullptr;double Best=FMath::Square(650.f);
        for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) {
            if(*It==Hero || !It->GetController() || !It->Status->IsAlive() || !It->HasFrozenLegs() || !CanObserve(*It)) continue;
            const double* Retry=FailedTargets.Find(*It);if(Retry && GetWorld()->GetTimeSeconds()<*Retry) continue;
            const double Distance=FVector::DistSquared2D(It->GetActorLocation(),Hero->GetActorLocation());
            if(Distance<Best) {Best=Distance;Ally=*It;}
        }
        if(!Ally) return false;
        if(Target.Get()!=Ally || Goal!=EMCPlaytestBotGoal::RescueIce) {
            ReleaseInputs();ResetApproach();Target=Ally;Goal=EMCPlaytestBotGoal::RescueIce;
            LastTargetProgress=-Ally->IceLegHealth;LastWorkProgressAt=GetWorld()->GetTimeSeconds();
        }
        const double Progress=-Ally->IceLegHealth;
        if(Progress!=LastTargetProgress) {LastTargetProgress=Progress;LastWorkProgressAt=GetWorld()->GetTimeSeconds();}
        else if(Hero->IsPrimaryHeld() && GetWorld()->GetTimeSeconds()-LastWorkProgressAt>3) {FailPath();return true;}
        WorkAtTarget();return true;
    }
    if(Target.Get()!=Hero || Goal!=EMCPlaytestBotGoal::RescueIce) {
        ReleaseInputs();ResetApproach();Target=Hero;Goal=EMCPlaytestBotGoal::RescueIce;
        LastTargetProgress=-Hero->IceLegHealth;LastWorkProgressAt=GetWorld()->GetTimeSeconds();
    }
    StopMovement();Hero->SetSelfCareInput(false);
    if(Hero->Inventory->Selected!=EMCToolSlot::Pickaxe) {
        ReleaseInputs();Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
    }
    // This is the same held pickaxe rescue advertised by the player's HUD.
    Hero->SetPrimaryInputHeld(true);return true;
}

void AMCPlaytestBotController::ReactToGroundWaves()
{
    const double Now=GetWorld()->GetTimeSeconds();
    if(Hero->HasFrozenLegs() || Now<NextJumpAt || !Hero->GetCharacterMovement()->IsMovingOnGround()) return;
    const float HalfHeight=Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const float Capsule=Hero->GetCapsuleComponent()->GetScaledCapsuleRadius();
    bool ShouldJump=false;
    for(TActorIterator<AMCHazardWave> It(GetWorld());It;++It) {
        if(It->IsActorBeingDestroyed() || It->MaxRadius<=0) continue;
        const float Distance=FVector::Dist2D(Hero->GetActorLocation(),It->GetActorLocation());
        if(Distance>It->MaxRadius+Capsule) continue;
        const float Speed=It->MaxRadius/FMath::Max(.1f,It->TravelSeconds);
        const double CrossesAt=It->StartedAt+It->WarningSeconds+(Distance-Capsule-12)/Speed;
        // A modest lookahead accounts for the jump's launch and the real sole
        // clearance test. Each wave keeps its authoritative one-hit ledger.
        if(CrossesAt>=Now-.03 && CrossesAt<=Now+.30 && FMath::Abs(Hero->GetActorLocation().Z-HalfHeight-It->GetActorLocation().Z)<120) ShouldJump=true;
    }
    for(TActorIterator<AMCIceEvent> It(GetWorld());It;++It) {
        if(!It->IsActive() || It->NovaImpactAt<=0) continue;
        const double CrossesAt=It->NovaImpactAt+FVector::Dist2D(Hero->GetActorLocation(),It->GetActorLocation())/FMath::Max(1.f,It->NovaRange/FMath::Max(.5f,It->WaveTravelSeconds));
        if(CrossesAt>=Now-.03 && CrossesAt<=Now+.30) ShouldJump=true;
    }
    if(ShouldJump) {Hero->SetJumpInputHeld(true);JumpReleaseAt=Now+.12;NextJumpAt=Now+.8;}
}

bool AMCPlaytestBotController::HandleEventWork()
{
    const double Now=GetWorld()->GetTimeSeconds();
    for(TActorIterator<AMCFogBrawlEvent> It(GetWorld());It;++It) {
        if(!It->IsActive()) continue;
        const bool Guarding=It->Stage==EMCFogBrawlStage::Warning;
        const bool Preposition=It->Stage==EMCFogBrawlStage::Recovery && It->Targets.ContainsByPredicate([&](const FMCFogBrawlTarget& Candidate) {
            return !Candidate.bResolved && Candidate.Side==It->CurrentSide && IsValid(Candidate.Tooth) && Candidate.Tooth->IsAvailable();
        });
        if(!Guarding && !Preposition) {
            if(Goal==EMCPlaytestBotGoal::FogGuard) {ReleaseInputs();StopMovement();Target.Reset();Goal=EMCPlaytestBotGoal::Idle;}
            return false;
        }
        AMCArenaTooth* Tooth=nullptr;double BestTarget=DBL_MAX;
        for(const auto& Candidate:It->Targets) {
            if(Candidate.bResolved || Candidate.Side!=It->CurrentSide || !IsValid(Candidate.Tooth) || !Candidate.Tooth->IsAvailable()) continue;
            int32 Occupied=0;
            for(TActorIterator<AMCPlaytestBotController> Bot(GetWorld());Bot;++Bot)
                if(*Bot!=this && Bot->Hero && Bot->Hero->Status->IsAlive() && Bot->Goal==EMCPlaytestBotGoal::FogGuard
                    && Bot->Target.Get()==Candidate.Tooth.Get()) ++Occupied;
            for(TActorIterator<AMCToothCharacter> Player(GetWorld());Player;++Player)
                if(*Player!=Hero && Player->GetController() && !Cast<AMCPlaytestBotController>(Player->GetController())
                    && It->IsGuardingTooth(*Player,Candidate.Tooth)) ++Occupied;
            const double Stable=Goal==EMCPlaytestBotGoal::FogGuard && Target.Get()==Candidate.Tooth.Get()?200.:0.;
            const double Score=FVector::Dist2D(Hero->GetActorLocation(),Candidate.Tooth->GetActorLocation())+Occupied*6000.-Stable;
            if(Score<BestTarget) {BestTarget=Score;Tooth=Candidate.Tooth;}
        }
        if(!IsValid(Tooth)) {ReleaseInputs();StopMovement();Target.Reset();Goal=EMCPlaytestBotGoal::Idle;return true;}
        if(Goal!=EMCPlaytestBotGoal::FogGuard || Target.Get()!=Tooth) {ReleaseInputs();ResetApproach();Target=Tooth;Goal=EMCPlaytestBotGoal::FogGuard;}
        if(Guarding && It->CanMarkTarget(Hero)) {Hero->SetHandleInputHeld(true);Hero->SetHandleInputHeld(false);}
        if(Guarding && It->IsGuardingTooth(Hero,Tooth)) {StopMovement();Hero->SetSprintInputHeld(false);return true;}
        // Find the inward, reachable side of the tooth rather than its padded
        // collision centre. Existing guard rules check brace, floor and LOS.
        const FVector Contact=Tooth->Visual->Bounds.GetBox().GetClosestPointTo(Hero->GetActorLocation());
        FVector Out=(Hero->GetActorLocation()-Contact).GetSafeNormal2D(KINDA_SMALL_NUMBER,-Tooth->GetActorForwardVector());
        const float Distance=FVector::Dist2D(Hero->GetActorLocation(),Contact);
        if(Distance<=It->GuardRadius*.70f) {
            StopMovement();Hero->SetSprintInputHeld(false);Hero->SetBraceInputHeld(Guarding);return true;
        }
        Hero->SetBraceInputHeld(false);Hero->SetSprintInputHeld(Distance>350);MoveTowards(Contact+Out*130,35);return true;
    }
    if(Goal==EMCPlaytestBotGoal::FogGuard) {ReleaseInputs();Target.Reset();Goal=EMCPlaytestBotGoal::Idle;}
    for(TActorIterator<AMCIceEvent> It(GetWorld());It;++It) {
        if(It->Stage!=EMCIceEventStage::Active || !It->Tongue || It->CandyHealth<=0) continue;
        const float Amount=It->FreezeAmount(Hero);
        bSeekingWarmth=Amount>=.35f || (bSeekingWarmth && Amount>.08f);
        if(bSeekingWarmth) {
            if(Target.Get()!=*It || Goal!=EMCPlaytestBotGoal::Warm) {ReleaseInputs();ResetApproach();Target=*It;Goal=EMCPlaytestBotGoal::Warm;}
            const FVector Feet=Hero->GetActorLocation()-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            if(It->IsSafePoint(Feet)) {StopMovement();Hero->SetPrimaryInputHeld(false);Hero->SetSprintInputHeld(false);return true;}
            // Re-evaluate moving/blocked circles every decision. Brushing is a
            // fallback outside the strong aura, which closes warmth immediately.
            TArray<FVector> Anchors{It->SafeAnchor};if(It->bNextCircle) Anchors.Add(It->NextSafeAnchor);
            for(const auto& Patch:It->BrushedWarmPatches) if(It->BrushedWarmRadius(Patch)>=90) Anchors.Add(Patch.Anchor);
            double Best=DBL_MAX;FVector Stand=FVector::ZeroVector;
            for(const auto Anchor:Anchors) {
                FVector Candidate=It->Tongue->GetActorTransform().TransformPosition(Anchor),Projected;
                if(!It->IsSafePoint(Candidate) || !ProjectStandingPosition(Candidate,Projected)) continue;
                if(!It->IsSafePoint(Projected-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()))) continue;
                const double Distance=FVector::DistSquared2D(Hero->GetActorLocation(),Projected);
                if(Distance<Best) {Best=Distance;Stand=Projected;}
            }
            if(Best<DBL_MAX) {Hero->SetPrimaryInputHeld(false);Hero->SetSprintInputHeld(true);MoveTowards(Stand,35);return true;}
            if(!It->IsInsideColdThrone(Feet)) {
                StopMovement();Hero->SetSprintInputHeld(false);Hero->SetSelfCareInput(false);
                if(Hero->Inventory->Selected!=EMCToolSlot::Brush) {ReleaseInputs();Hero->Inventory->ServerSelect(EMCToolSlot::Brush);}
                Hero->SetPrimaryInputHeld(true);return true;
            }
            FVector Away=(Feet-It->CandyContactPoint(Feet)).GetSafeNormal2D(KINDA_SMALL_NUMBER,Hero->GetActorRightVector());
            ReleaseInputs();Hero->SetSprintInputHeld(true);MoveTowards(Hero->GetActorLocation()+Away*(It->BossFrostRadius+250),45);return true;
        }
        if(Target.Get()!=*It || Goal!=EMCPlaytestBotGoal::Fight) {
            ReleaseInputs();ResetApproach();Target=*It;Goal=EMCPlaytestBotGoal::Fight;
            LastWorkProgressAt=Now;LastTargetProgress=-It->CandyHealth;
        }
        WorkAtTarget();return true;
    }
    bSeekingWarmth=false;
    if(Goal==EMCPlaytestBotGoal::Warm) {ReleaseInputs();Target.Reset();Goal=EMCPlaytestBotGoal::Idle;}
    return false;
}
