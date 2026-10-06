#include "MCPlaytestBotController.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothCalculusComponent.h"
#include "MCBrushContactComponent.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCGripComponent.h"
#include "MCArenaTooth.h"
#include "MCMouthSurface.h"
#include "MCFirePatch.h"
#include "MCBossCharacter.h"
#include "MCThroat.h"
#include "MCGameState.h"
#include "MCPlayerState.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "EngineUtils.h"
#include "TimerManager.h"

AMCPlaytestBotController::AMCPlaytestBotController()
{
    bWantsPlayerState=true;
    bStartAILogicOnPossess=false;
    bStopAILogicOnUnposses=true;
    PrimaryActorTick.bCanEverTick=true;
}

void AMCPlaytestBotController::Configure(EMCPlaytestBotSkill InSkill,int32 BotIndex,int32 Seed)
{
    Skill=InSkill; Tuning=FMCPlaytestBotTuning::ForSkill(Skill);
    Random.Initialize(int32(uint32(Seed)^((uint32(BotIndex)+1u)*2654435761u)));
    AimError=Random.FRandRange(-Tuning.AimErrorDegrees,Tuning.AimErrorDegrees);
    bConfigured=true;
    if(auto* PS=GetPlayerState<AMCPlayerState>()) {
        PS->SetPlayerName(FString::Printf(TEXT("Bot %d (%s)"),BotIndex+1,MCPlaytestSkillName(Skill)));
        PS->SetIsABot(true);
        static const FLinearColor Colors[]={FLinearColor(.4f,.9f,.3f),FLinearColor(.8f,.4f,1),FLinearColor(1,.35f,.25f),FLinearColor(.24f,.65f,1)};
        PS->PlayerColor=Colors[FMath::Abs(BotIndex%4)]; PS->bSessionHost=false; PS->ForceNetUpdate();
        if(Hero) Hero->ApplyPlayerColor(PS->PlayerColor);
    }
    if(Hero) GetWorldTimerManager().SetTimer(DecisionTimer,this,&AMCPlaytestBotController::Decide,Tuning.DecisionSeconds,true,.1f);
}

void AMCPlaytestBotController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);
    Hero=Cast<AMCToothCharacter>(InPawn);
    Target.Reset(); Goal=EMCPlaytestBotGoal::Idle; NextTaskAt=NextMoveAt=0; HazardSeenAt=-1; EscapeUntil=0;
    bHasWorkApproach=false; RecoveryUntil=JumpReleaseAt=0; FailedApproaches.Reset();
    if(Hero) {
        Hero->GetCharacterMovement()->GetNavMovementProperties()->bUseAccelerationForPaths=true;
        LastProgressPosition=Hero->GetActorLocation(); ProgressCheckedAt=GetWorld()->GetTimeSeconds();
        if(const auto* PS=GetPlayerState<AMCPlayerState>()) Hero->ApplyPlayerColor(PS->PlayerColor);
        // CMC turns to path acceleration; when stationary we turn toward the work surface.
        GetWorldTimerManager().SetTimer(DecisionTimer,this,&AMCPlaytestBotController::Decide,Tuning.DecisionSeconds,true,.1f);
    }
}

void AMCPlaytestBotController::ReleaseInputs(bool bDropCollection)
{
    if(!IsValid(Hero)) return;
    Hero->SetPrimaryInputHeld(false); Hero->SetHandleInputHeld(false);
    Hero->SetJumpInputHeld(false); Hero->SetSprintInputHeld(false); JumpReleaseAt=0;
    Hero->SetSelfCareInput(false);
    if(bDropCollection && Hero->FoodCollection->bCollecting) {
        // E is the same collection toggle used by a human; never dispose food from AI.
        Hero->SetHandleInputHeld(true); Hero->SetHandleInputHeld(false);
    }
}

void AMCPlaytestBotController::OnUnPossess()
{
    GetWorldTimerManager().ClearTimer(DecisionTimer); ReleaseInputs(); StopMovement();
    Hero=nullptr; Target.Reset(); Super::OnUnPossess();
}

void AMCPlaytestBotController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(DecisionTimer); ReleaseInputs();
    Super::EndPlay(EndPlayReason);
}

void AMCPlaytestBotController::Tick(float Dt)
{
    Super::Tick(Dt);
    if(!HasAuthority() || !IsValid(Hero)) return;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const bool Terminal=GS && (GS->Phase==EMCShiftPhase::Won || GS->Phase==EMCShiftPhase::Lost || GS->bDayOneComplete || GS->bLobbyWaiting);
    if(!Hero->Status->IsAlive() || Terminal || (!Hero->CanWork() && !Hero->bInCoffee)) {
        if(!bSuspended || Hero->IsPrimaryHeld() || Hero->bBrushing || Hero->bHandling || Hero->bPressedJump || !Hero->GetPendingMovementInputVector().IsNearlyZero())
        { StopMovement(); Hero->CancelGameplayInput(); Hero->ConsumeMovementInputVector(); Target.Reset(); ResetApproach(); RecoveryUntil=JumpReleaseAt=0; Goal=EMCPlaytestBotGoal::Idle; bSuspended=true; }
        return;
    }
    bSuspended=false;
    if(JumpReleaseAt>0 && GetWorld()->GetTimeSeconds()>=JumpReleaseAt) { Hero->SetJumpInputHeld(false); JumpReleaseAt=0; }
    if(Hero->bInCoffee) {
        AMCArenaTooth* Nearest=nullptr; double Best=DBL_MAX;
        for(TActorIterator<AMCArenaTooth> It(GetWorld());It;++It) if(It->IsAvailable()) {
            const double Distance=FVector::DistSquared2D(Hero->GetActorLocation(),It->GetActorLocation());
            if(Distance<Best) {Nearest=*It; Best=Distance;}
        }
        if(Nearest) {
            const FVector Direction=(Nearest->GetActorLocation()-Hero->GetActorLocation()).GetSafeNormal2D();
            Hero->ServerPaddle(FVector2D(Direction.X,Direction.Y));
            // Primary on a swimming/ragdoll pawn is the player's ordinary cling intent.
            if(!Hero->IsPrimaryHeld()) Hero->ServerSetPrimary(true);
        }
        return;
    }
    if(!Hero->CanWork()) return;
    if(Goal==EMCPlaytestBotGoal::Clean && Hero->IsPrimaryHeld() && Target.IsValid() && Target!=Hero) {
        FVector Contact,Normal; bool Reachable=false;
        if(auto* Tooth=Cast<AMCArenaTooth>(Target.Get())) Reachable=Tooth->FindDirtyContact(Hero,Contact,Normal);
        else if(auto* Surface=Cast<AMCMouthSurface>(Target.Get())) Reachable=Surface->FindDirtyContact(Hero,Contact,Normal);
        else if(auto* Player=Cast<AMCToothCharacter>(Target.Get())) Reachable=Player->FindPlayerBrushContact(Hero,Contact,Normal);
        if(!Reachable) Hero->SetPrimaryInputHeld(false);
    }
    if(Goal==EMCPlaytestBotGoal::PullFood && IsValid(Hero->HeldFood) && Hero->HeldFood->Phase==EMCFoodPhase::Stuck)
        Hero->AddMovementInput(FVector(Hero->HeldFood->PullDirection).GetSafeNormal2D());
    const bool Working=Hero->bBrushing || Hero->bHandling || Hero->IsPrimaryHeld()
        || !Hero->FoodCollection->Pieces.IsEmpty() || Hero->HeldFood;
    if(Working) WorkSeconds+=Dt;
    else if(Hero->GetVelocity().Size2D()<20) IdleSeconds+=Dt;
}

FString AMCPlaytestBotController::GetActivity() const
{
    static const TCHAR* Names[]={TEXT("idle"),TEXT("clean"),TEXT("repair"),TEXT("spray"),TEXT("break_food"),TEXT("collect_food"),TEXT("pull_food"),TEXT("deliver"),TEXT("calculus"),TEXT("fight"),TEXT("escape"),TEXT("recover")};
    return Names[uint8(Goal)];
}

FVector AMCPlaytestBotController::TaskPoint(AActor* Actor) const
{
    if(!IsValid(Actor) || !Hero) return FVector::ZeroVector;
    if(const auto* Boss=Cast<AMCBossCharacter>(Actor)) return Boss->GetMeleeTargetPoint(Hero->GetActorLocation());
    if(const auto* Zone=Cast<AMCFoodDisposal>(Actor)) {
        FTransform Transform; FVector Extent; bool Circular;
        Zone->GetDeliveryZoneGeometry(Transform,Extent,Circular); return Transform.GetLocation();
    }
    if(const auto* Tooth=Cast<AMCArenaTooth>(Actor)) return Tooth->Visual->Bounds.GetBox().GetClosestPointTo(Hero->GetActorLocation());
    if(const auto* Food=Cast<AMCFoodActor>(Actor)) return Food->Visual->Bounds.GetBox().GetClosestPointTo(Hero->GetActorLocation());
    return Actor->GetActorLocation();
}

bool AMCPlaytestBotController::CanObserve(AActor* Actor) const
{
    if(!IsValid(Actor) || !Hero) return false;
    if(Actor==Hero) return true;
    const FVector Point=TaskPoint(Actor)+FVector(0,0,35);
    if(FVector::DistSquared(Point,Hero->GetActorLocation())>FMath::Square(Tuning.SightRadius)) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PlaytestBotSight),false,Hero); Query.AddIgnoredActor(Actor);
    FHitResult Hit;
    return !GetWorld()->LineTraceSingleByChannel(Hit,Hero->GetActorLocation()+FVector(0,0,35),Point,ECC_Visibility,Query);
}

bool AMCPlaytestBotController::IsTaskValid() const
{
    AActor* Actor=Target.Get(); if(!IsValid(Actor)) return false;
    if(Goal==EMCPlaytestBotGoal::Deliver) return Hero && (!Hero->FoodCollection->Pieces.IsEmpty() || IsValid(Hero->HeldFood));
    if(Goal==EMCPlaytestBotGoal::Clean || Goal==EMCPlaytestBotGoal::Repair) {
        const auto* Status=Actor->FindComponentByClass<UMCToothStatusComponent>();
        const auto* Tooth=Cast<AMCArenaTooth>(Actor);
        return Status && Status->NeedsCare(Goal==EMCPlaytestBotGoal::Clean) && (!Tooth || Tooth->IsAvailable());
    }
    if(Goal==EMCPlaytestBotGoal::Spray) {
        if(const auto* Fire=Cast<AMCFirePatch>(Actor)) return Fire->IsBurning();
        const auto* Surface=Cast<AMCMouthSurface>(Actor); return Surface && Surface->bUlcer && !Surface->IsHealed();
    }
    if(Goal==EMCPlaytestBotGoal::Calculus) {
        const auto* Tooth=Cast<AMCArenaTooth>(Actor); return Tooth && Tooth->IsAvailable() && Tooth->Calculus->HasCalculus();
    }
    if(Goal==EMCPlaytestBotGoal::Fight) {
        const auto* Boss=Cast<AMCBossCharacter>(Actor); return Boss && Boss->CanReceiveWeaponHit() && Boss->Runtime.State!=EMCBossState::Dormant;
    }
    const auto* Food=Cast<AMCFoodActor>(Actor);
    return Food && !Food->IsDisposed() && !Food->StackCarrier && Food->Phase!=EMCFoodPhase::Swallowing
        && Food->Phase!=EMCFoodPhase::Equipped && Food->Phase!=EMCFoodPhase::Absorbing;
}

void AMCPlaytestBotController::ChooseTask()
{
    if(!IsValid(Hero->HeldFood)) ReleaseInputs();
    Target.Reset(); Goal=EMCPlaytestBotGoal::Idle;
    ResetApproach();
    const double Now=GetWorld()->GetTimeSeconds(); double Best=DBL_MAX;
    auto Consider=[&](AActor* Actor,EMCPlaytestBotGoal Candidate,float Priority,bool bKnownLandmark=false) {
        if(!bKnownLandmark && !CanObserve(Actor)) return;
        if(const double* Retry=FailedTargets.Find(Actor); Retry && Now<*Retry) return;
        double Score=FVector::Dist2D(Hero->GetActorLocation(),TaskPoint(Actor))-Priority;
        for(TActorIterator<AMCPlaytestBotController> It(GetWorld());It;++It)
            if(*It!=this && It->GetTaskTarget()==Actor) Score+=Tuning.CooperationPenalty;
        Score+=Random.FRandRange(0,Skill==EMCPlaytestBotSkill::Novice?240.f:50.f);
        if(Score<Best) { Best=Score; Target=Actor; Goal=Candidate; }
    };
    if(!Hero->FoodCollection->Pieces.IsEmpty() || IsValid(Hero->HeldFood)) {
        const AMCFoodActor* Piece=Hero->HeldFood?Hero->HeldFood.Get():Hero->FoodCollection->Pieces[0].Get();
        for(TActorIterator<AMCFoodDisposal> It(GetWorld());It;++It)
            if(Piece && It->bBrushBin==Piece->IsWrongIngredient()) Consider(*It,EMCPlaytestBotGoal::Deliver,3000,true);
    } else {
        if(Hero->Status->NeedsCare(false)) Consider(Hero,EMCPlaytestBotGoal::Repair,1800);
        if(Hero->Status->NeedsCare(true)) Consider(Hero,EMCPlaytestBotGoal::Clean,850);
        for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(*It!=Hero && It->GetController() && It->Status->IsAlive()) {
            if(It->Status->NeedsCare(false)) Consider(*It,EMCPlaytestBotGoal::Repair,1500);
            if(It->Status->NeedsCare(true)) Consider(*It,EMCPlaytestBotGoal::Clean,700);
        }
        for(TActorIterator<AMCArenaTooth> It(GetWorld());It;++It) if(It->IsAvailable()) {
            if(It->Status->NeedsCare(false)) Consider(*It,EMCPlaytestBotGoal::Repair,1100);
            if(It->Status->NeedsCare(true)) Consider(*It,EMCPlaytestBotGoal::Clean,850);
            if(It->Calculus->HasCalculus()) Consider(*It,EMCPlaytestBotGoal::Calculus,400);
        }
        for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) {
            if(It->bUlcer && !It->IsHealed()) Consider(*It,EMCPlaytestBotGoal::Spray,1300);
            else if(!It->bUlcer && !It->IsClean()) Consider(*It,EMCPlaytestBotGoal::Clean,900);
        }
        for(TActorIterator<AMCFirePatch> It(GetWorld());It;++It) if(It->IsBurning()) Consider(*It,EMCPlaytestBotGoal::Spray,1500);
        for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) {
            if(It->bBrushTool || It->IsDisposed() || It->StackCarrier || !It->Holders.IsEmpty()
                || (It->Phase!=EMCFoodPhase::Free && It->Phase!=EMCFoodPhase::Falling && It->Phase!=EMCFoodPhase::Stuck)) continue;
            if(It->UsesLegacyGrip()) Consider(*It,EMCPlaytestBotGoal::PullFood,It->IsWrongIngredient()?850:650);
            else if(It->Phase==EMCFoodPhase::Stuck || It->Body->GetScaledBoxExtent().GetMax()>55 || It->Visual->Bounds.SphereRadius>85)
                Consider(*It,EMCPlaytestBotGoal::BreakFood,700);
            else Consider(*It,EMCPlaytestBotGoal::CollectFood,850);
        }
        for(TActorIterator<AMCBossCharacter> It(GetWorld());It;++It)
            if(It->CanReceiveWeaponHit() && It->Runtime.State!=EMCBossState::Dormant) Consider(*It,EMCPlaytestBotGoal::Fight,1700);
    }
    AimError=Random.FRandRange(-Tuning.AimErrorDegrees,Tuning.AimErrorDegrees);
    NextTaskAt=Now+Tuning.CommitSeconds;
    LastWorkProgressAt=Now; LastTargetProgress=TargetProgress();
}

double AMCPlaytestBotController::TargetProgress() const
{
    AActor* Actor=Target.Get(); if(!IsValid(Actor)) return 0;
    if(const auto* Food=Cast<AMCFoodActor>(Actor)) return -Food->Health+Food->PullProgress*100;
    if(const auto* Surface=Cast<AMCMouthSurface>(Actor)) return Surface->bUlcer?Surface->Healing*100:(1-Surface->RemainingLiquid())*100;
    if(const auto* Fire=Cast<AMCFirePatch>(Actor)) return -Fire->Heat*100;
    if(const auto* Boss=Cast<AMCBossCharacter>(Actor)) return -Boss->Runtime.Health;
    if(const auto* Tooth=Cast<AMCArenaTooth>(Actor); Tooth && Goal==EMCPlaytestBotGoal::Calculus) return -Tooth->Calculus->RemainingFraction()*100;
    if(const auto* Status=Actor->FindComponentByClass<UMCToothStatusComponent>())
    {
        const auto* Tooth=Cast<AMCArenaTooth>(Actor);
        const double Grime=Tooth && Goal==EMCPlaytestBotGoal::Clean?(1-Tooth->RemainingGrime())*100:0;
        return Status->State.Health-Status->State.CoffeeLeft*25-Status->State.RepairLeft*25+Grime;
    }
    return 0;
}

void AMCPlaytestBotController::FailPath()
{
    ++PathFailures;
    if(Target.IsValid()) FailedTargets.Add(Target,GetWorld()->GetTimeSeconds()+5);
    ResetApproach(true);
    StopMovement(); ReleaseInputs(); Target.Reset(); Goal=EMCPlaytestBotGoal::Idle; NextTaskAt=0;
}

bool AMCPlaytestBotController::IsApproachPositionClear(FVector PawnCenter) const
{
    if(!IsValid(Hero) || PawnCenter.ContainsNaN()) return false;
    const auto* Capsule=Hero->GetCapsuleComponent();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCPlaytestWorkCapsule),false,Hero);
    // Include the real target and teammates. A projected nav point can still
    // leave the worker's body inside a padded tooth box or another player.
    const float Radius=Capsule->GetScaledCapsuleRadius()+4;
    const float Height=FMath::Max(Radius,Capsule->GetScaledCapsuleHalfHeight());
    return !GetWorld()->OverlapBlockingTestByProfile(PawnCenter,FQuat::Identity,
        Capsule->GetCollisionProfileName(),FCollisionShape::MakeCapsule(Radius,Height),Query);
}

bool AMCPlaytestBotController::ProjectStandingPosition(FVector Candidate,FVector& PawnCenter) const
{
    auto* Nav=UNavigationSystemV1::GetCurrent(GetWorld()); FNavLocation Projected;
    if(!Nav || !Nav->ProjectPointToNavigation(Candidate,Projected,FVector(65,65,600),&Hero->GetNavAgentPropertiesRef())) return false;
    FHitResult Floor; FCollisionQueryParams Query(SCENE_QUERY_STAT(MCPlaytestWorkFloor),false,Hero);
    const float Height=Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    if(!GetWorld()->LineTraceSingleByChannel(Floor,Projected.Location+FVector(0,0,Height+15),
        Projected.Location-FVector(0,0,160),ECC_Visibility,Query)
        || Floor.ImpactNormal.Z<Hero->GetCharacterMovement()->GetWalkableFloorZ()
        || Cast<AMCArenaTooth>(Floor.GetActor()) || Cast<APawn>(Floor.GetActor()) || Cast<AMCFoodActor>(Floor.GetActor())) return false;
    PawnCenter=FVector(Projected.Location.X,Projected.Location.Y,Floor.ImpactPoint.Z+Height+4);
    return IsApproachPositionClear(PawnCenter);
}

void AMCPlaytestBotController::ResetApproach(bool bRememberFailure)
{
    if(bRememberFailure && bHasWorkApproach) {
        FailedApproaches.Emplace(WorkStand,GetWorld()->GetTimeSeconds()+12);
        if(FailedApproaches.Num()>16) FailedApproaches.RemoveAt(0);
    }
    bHasWorkApproach=false;
}

bool AMCPlaytestBotController::PlanCleanApproach(AMCArenaTooth* Tooth)
{
    TArray<FVector> Points,Normals; Tooth->GetDirtyContactSamples(Points,Normals);
    const double Now=GetWorld()->GetTimeSeconds();
    FailedApproaches.RemoveAll([Now](const auto& Entry){ return Now>=Entry.Value; });
    struct FOption { FVector Stand,Aim; double Score; };
    TArray<FOption> Options;
    const FVector Origin=Hero->GetActorLocation();
    auto Consider=[&](FVector Stand,FVector Aim,FVector Normal) {
        for(const auto& Bad:FailedApproaches) if(FVector::DistSquared2D(Bad.Key,Stand)<FMath::Square(55.f)) return;
        for(TActorIterator<AMCPlaytestBotController> It(GetWorld());It;++It) {
            if(*It==this || !It->Hero || !It->bHasWorkApproach || It->Target!=Tooth) continue;
            const float Gap=Hero->GetCapsuleComponent()->GetScaledCapsuleRadius()+It->Hero->GetCapsuleComponent()->GetScaledCapsuleRadius()+25;
            if(FVector::DistSquared2D(Stand,It->WorkStand)<FMath::Square(Gap)) return;
        }
        const FRotator Facing(0,(Aim-Stand).Rotation().Yaw+AimError,0);
        if(!Hero->BrushContact->CanReachFromPose(Stand,Facing,Aim,Normal,Tooth)) return;
        const double Score=FVector::DistSquared2D(Origin,Stand)+FMath::Square(float(Aim.Z-Stand.Z-65))*.15;
        Options.Add({Stand,Aim,Score});
    };
    // Search actual remaining stains, including the opposite side of the crown.
    // The fixed bounding-box aim never changed after its reachable side was clean.
    const bool CurrentPositionClear=IsApproachPositionClear(Origin);
    const int32 Stride=FMath::Max(1,Points.Num()/96);
    const int32 Start=Points.IsEmpty()?0:Random.RandRange(0,Stride-1);
    for(int32 I=Start;I<Points.Num();I+=Stride) {
        const FVector P=Points[I],N=Normals[I];
        if(CurrentPositionClear) Consider(Origin,P,N);
        FVector Out=N.GetSafeNormal2D();
        if(Out.IsNearlyZero()) Out=(Origin-P).GetSafeNormal2D();
        const FVector Side=FVector::CrossProduct(FVector::UpVector,Out);
        for(float Distance:{65.f,105.f,150.f}) for(float Tangent:{0.f,-65.f,65.f}) {
            FVector Stand;
            if(ProjectStandingPosition(P+Out*Distance+Side*Tangent,Stand)) Consider(Stand,P,N);
        }
    }
    Options.Sort([](const auto& A,const auto& B){return A.Score<B.Score;});
    for(const auto& Option:Options) {
        if(FVector::DistSquared2D(Origin,Option.Stand)>FMath::Square(25.f)) {
            const auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Origin,Option.Stand,this);
            if(!Path || !Path->IsValid() || Path->IsPartial()) continue;
        }
        WorkStand=Option.Stand; WorkAim=Option.Aim; bHasWorkApproach=true;
        LastWorkProgressAt=Now; return true;
    }
    return false;
}

void AMCPlaytestBotController::RecoverFromStall()
{
    const double Now=GetWorld()->GetTimeSeconds();
    const bool WasMoving=GetMoveStatus()==EPathFollowingStatus::Moving;
    ++PathFailures; ResetApproach(true); StopMovement(); ReleaseInputs();
    FVector Away=Target.IsValid()?(Hero->GetActorLocation()-TaskPoint(Target.Get())).GetSafeNormal2D():-Hero->GetActorForwardVector();
    if(Away.IsNearlyZero()) Away=-Hero->GetActorForwardVector();
    const FVector Side=FVector::CrossProduct(FVector::UpVector,Away);
    FVector Stand=FVector::ZeroVector; bool Found=false;
    for(const FVector Direction:{Away,Side,-Side,(Away+Side).GetSafeNormal(),(Away-Side).GetSafeNormal()}) {
        if(!ProjectStandingPosition(Hero->GetActorLocation()+Direction*180,Stand)) continue;
        const auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Hero->GetActorLocation(),Stand,this);
        if(Path && Path->IsValid() && !Path->IsPartial()) {Found=true; break;}
    }
    if(Target.IsValid()) FailedTargets.Add(Target,Now+2);
    Target.Reset(); Goal=EMCPlaytestBotGoal::Recover; RecoveryUntil=Now+1.3; NextTaskAt=0; NextMoveAt=0;
    LastProgressPosition=Hero->GetActorLocation(); ProgressCheckedAt=Now;
    if(Found) MoveTowards(Stand,25);
    // A press and release in one decision never reaches CharacterMovement.
    // Hold across movement ticks, then release in Tick using the ordinary input.
    if(WasMoving && Now>=NextJumpAt && Hero->CanWork()) {
        Hero->SetJumpInputHeld(true); JumpReleaseAt=Now+.16; NextJumpAt=Now+3;
    }
}

void AMCPlaytestBotController::MoveTowards(FVector Destination,float Acceptance)
{
    const double Now=GetWorld()->GetTimeSeconds();
    if(Now<NextMoveAt && FVector::DistSquared2D(Destination,LastRequestedGoal)<FMath::Square(90.f)) return;
    NextMoveAt=Now+1.; LastRequestedGoal=Destination;
    auto* Nav=UNavigationSystemV1::GetCurrent(GetWorld());
    FNavLocation Projected;
    if(!Nav || !Nav->ProjectPointToNavigation(Destination,Projected,FVector(180,180,350),&Hero->GetNavAgentPropertiesRef())) { FailPath(); return; }
    FAIMoveRequest Request(Projected.Location);
    Request.SetAcceptanceRadius(Acceptance); Request.SetUsePathfinding(true); Request.SetAllowPartialPath(false);
    Request.SetReachTestIncludesAgentRadius(false); Request.SetProjectGoalLocation(false);
    if(GetMoveStatus()!=EPathFollowingStatus::Moving) {LastProgressPosition=Hero->GetActorLocation(); ProgressCheckedAt=Now;}
    if(MoveTo(Request).Code==EPathFollowingRequestResult::Failed) FailPath();
}

void AMCPlaytestBotController::OnMoveCompleted(FAIRequestID RequestID,const FPathFollowingResult& Result)
{
    Super::OnMoveCompleted(RequestID,Result);
    if(Result.Code!=EPathFollowingResult::Success && Result.Code!=EPathFollowingResult::Aborted && Result.Code!=EPathFollowingResult::Invalid) FailPath();
}

bool AMCPlaytestBotController::ReactToHazard()
{
    const double Now=GetWorld()->GetTimeSeconds();
    if(Now<EscapeUntil) { ReleaseInputs(); Hero->SetSprintInputHeld(true); MoveTowards(EscapeDestination,45); Goal=EMCPlaytestBotGoal::Escape; return true; }
    FVector Away=FVector::ZeroVector;
    for(TActorIterator<AMCBossCharacter> It(GetWorld());It;++It) {
        if(!CanObserve(*It) || (It->Runtime.State!=EMCBossState::Telegraph && It->Runtime.State!=EMCBossState::Attacking)) continue;
        for(const auto& Attack:It->GetAttackDefinitions())
            if(Attack.AttackId==It->Runtime.AttackId && It->IsPlayerInAttack(Hero,Attack,It->Runtime.AttackForward))
                Away+=(Hero->GetActorLocation()-It->GetActorLocation()).GetSafeNormal2D();
    }
    if(Away.IsNearlyZero()) { HazardSeenAt=-1; return false; }
    if(HazardSeenAt<0) HazardSeenAt=Now;
    if(Now-HazardSeenAt<Tuning.HazardReactionSeconds) return false;
    EscapeDestination=Hero->GetActorLocation()+Away.GetSafeNormal2D()*450;
    EscapeUntil=Now+1.5; Target.Reset(); ReleaseInputs(); Goal=EMCPlaytestBotGoal::Escape;
    Hero->SetSprintInputHeld(true); MoveTowards(EscapeDestination,45); return true;
}

void AMCPlaytestBotController::WorkAtTarget()
{
    AActor* Actor=Target.Get(); if(!Actor) return;
    FVector Point=TaskPoint(Actor);
    if(Goal==EMCPlaytestBotGoal::Deliver) {
        auto* Zone=CastChecked<AMCFoodDisposal>(Actor);
        if(Zone->ContainsDeliveryPosition(Hero->FoodCollection->HandPoint()) || Zone->ContainsDeliveryPosition(Hero->GetActorLocation())) {
            StopMovement(); ReleaseInputs(true); Target.Reset(); Goal=EMCPlaytestBotGoal::Idle; NextTaskAt=0; return;
        }
        // The authored tongue-end footprint can differ from the fixture rectangle.
        auto* Nav=UNavigationSystemV1::GetCurrent(GetWorld());
        TArray<FVector> Outer,Inner,Candidates; Candidates.Add(Point);
        if(Zone->GetDeliveryZoneOutline(Outer,Inner)) {
            for(int32 I=0;I<FMath::Min(Outer.Num(),Inner.Num());I+=FMath::Max(1,Outer.Num()/12)) Candidates.Add((Outer[I]+Inner[I])*.5);
        }
        double Best=DBL_MAX; FVector Destination=Point; bool Found=false;
        for(FVector Candidate:Candidates) {
            FNavLocation Projected;
            if(!Nav || !Nav->ProjectPointToNavigation(Candidate,Projected,FVector(180,180,350),&Hero->GetNavAgentPropertiesRef())) continue;
            if(!Zone->ContainsDeliveryPosition(Projected.Location+FVector(0,0,Hero->GetSimpleCollisionHalfHeight()))) continue;
            const double Distance=FVector::DistSquared2D(Hero->GetActorLocation(),Projected.Location);
            if(Distance<Best) {Best=Distance; Destination=Projected.Location; Found=true;}
        }
        if(Found) MoveTowards(Destination,30); else FailPath();
        return;
    }
    if(auto* Tooth=Cast<AMCArenaTooth>(Actor); Tooth && Goal==EMCPlaytestBotGoal::Clean) {
        if(Hero->Inventory->Selected!=EMCToolSlot::Brush) {
            ReleaseInputs(); Hero->Inventory->ServerSelect(EMCToolSlot::Brush);
            if(Hero->Inventory->Selected!=EMCToolSlot::Brush) return;
        }
        if(bHasWorkApproach && !IsApproachPositionClear(WorkStand)) ResetApproach(true);
        if(!bHasWorkApproach && !PlanCleanApproach(Tooth)) {FailPath(); return;}
        if(FVector::DistSquared2D(Hero->GetActorLocation(),WorkStand)>FMath::Square(24.f)) {
            ReleaseInputs(); MoveTowards(WorkStand,15); return;
        }
        StopMovement();
        const FRotator Facing(0,(WorkAim-Hero->GetActorLocation()).Rotation().Yaw+AimError,0);
        SetControlRotation(Facing);
        Hero->SetActorRotation(FMath::RInterpTo(Hero->GetActorRotation(),Facing,Tuning.DecisionSeconds,5.f));
        Hero->SetSelfCareInput(false);
        FVector Contact,Normal;
        if(!Tooth->FindDirtyContact(Hero,Contact,Normal)) {
            Hero->SetPrimaryInputHeld(false);
            if(FMath::Abs(FMath::FindDeltaAngleDegrees(Hero->GetActorRotation().Yaw,Facing.Yaw))<15 || GetWorld()->GetTimeSeconds()-LastWorkProgressAt>1.2)
                ResetApproach(true);
            return;
        }
        WorkAim=Contact;
        Hero->SetPrimaryInputHeld(true);
        return;
    }
    const float Reach=Goal==EMCPlaytestBotGoal::Spray?180:Goal==EMCPlaytestBotGoal::CollectFood?125:90;
    const FVector Delta=Point-Hero->GetActorLocation();
    if(Actor!=Hero && Delta.Size2D()>Reach) {
        ReleaseInputs();
        const FVector Towards=Delta.GetSafeNormal2D();
        MoveTowards(Point-Towards*(Reach*.65f),25); return;
    }
    StopMovement();
    FVector Facing=Delta.GetSafeNormal2D();
    if(!Facing.IsNearlyZero()) {
        FRotator Rotation=Facing.Rotation(); Rotation.Yaw+=AimError;
        SetControlRotation(Rotation);
        Hero->SetActorRotation(FMath::RInterpTo(Hero->GetActorRotation(),Rotation,Tuning.DecisionSeconds,5.f));
    }
    EMCToolSlot Slot=EMCToolSlot::Brush;
    if(Goal==EMCPlaytestBotGoal::Spray) Slot=EMCToolSlot::Spray;
    else if(Goal==EMCPlaytestBotGoal::Calculus || Goal==EMCPlaytestBotGoal::Fight) Slot=EMCToolSlot::Pickaxe;
    else if(Goal==EMCPlaytestBotGoal::BreakFood) Slot=CastChecked<AMCFoodActor>(Actor)->IsHardFood()?EMCToolSlot::Pickaxe:EMCToolSlot::Knife;
    if(Hero->Inventory->Selected!=Slot) {
        ReleaseInputs(); Hero->Inventory->ServerSelect(Slot);
        if(Hero->Inventory->Selected!=Slot) return;
    }
    Hero->SetSelfCareInput(Actor==Hero);
    if(Goal==EMCPlaytestBotGoal::Clean && Actor!=Hero) {
        FVector Contact,Normal; bool Reachable=false;
        if(auto* Surface=Cast<AMCMouthSurface>(Actor)) Reachable=Surface->FindDirtyContact(Hero,Contact,Normal);
        else if(auto* Player=Cast<AMCToothCharacter>(Actor)) Reachable=Player->FindPlayerBrushContact(Hero,Contact,Normal);
        if(!Reachable) {ReleaseInputs(); RecoverFromStall(); return;}
    }
    if(Goal==EMCPlaytestBotGoal::Repair && !Hero->CanContact(Actor)) {ReleaseInputs(); FailPath(); return;}
    // Primary selects care by the same nearby target rules as a human. E also
    // requests climbing and opens chests, so it is unsuitable as a repair intent.
    Hero->SetPrimaryInputHeld(true);
}

void AMCPlaytestBotController::Decide()
{
    if(!HasAuthority() || !IsValid(Hero) || !bConfigured) return;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    if(!Hero->CanWork() || Hero->bInCoffee || (GS && (GS->Phase!=EMCShiftPhase::Working || GS->bDayOneComplete))) {
        ReleaseInputs(); StopMovement(); Goal=EMCPlaytestBotGoal::Idle; Target.Reset(); return;
    }
    if(ReactToHazard()) return;
    const double Now=GetWorld()->GetTimeSeconds();
    if(Goal==EMCPlaytestBotGoal::Recover) {
        if(Now<RecoveryUntil) return;
        StopMovement(); ReleaseInputs(); Goal=EMCPlaytestBotGoal::Idle;
    }
    if(Target.IsValid() && Goal!=EMCPlaytestBotGoal::Deliver) {
        const double Progress=TargetProgress();
        if(FMath::Abs(Progress-LastTargetProgress)>.01) {LastTargetProgress=Progress; LastWorkProgressAt=Now;}
        // A held button is not proof of useful work. Back off from unreachable
        // contacts so one bad target cannot lock the whole playtest indefinitely.
        if((Hero->IsPrimaryHeld() || Hero->bHandling) && Now-LastWorkProgressAt>FMath::Max(3.f,Hero->Status->Settings.ContactSeconds+1.f)) {RecoverFromStall(); return;}
    }
    if(Now>=ProgressCheckedAt+2) {
        const bool Stuck=GetMoveStatus()==EPathFollowingStatus::Moving
            && FVector::DistSquared2D(LastProgressPosition,Hero->GetActorLocation())<FMath::Square(25.f);
        LastProgressPosition=Hero->GetActorLocation(); ProgressCheckedAt=Now;
        if(Stuck) {RecoverFromStall(); return;}
    }
    const bool ReadyToDeliver=(!Hero->FoodCollection->Pieces.IsEmpty() || (IsValid(Hero->HeldFood) && Hero->HeldFood->Phase!=EMCFoodPhase::Stuck)) && Goal!=EMCPlaytestBotGoal::Deliver;
    if(ReadyToDeliver || !IsTaskValid() || (Now>=NextTaskAt && !Hero->IsPrimaryHeld() && !Hero->bHandling)) {
        if(Now<NextHesitationAt) return;
        if(Random.FRand()<Tuning.HesitationChance) { ReleaseInputs(); StopMovement(); NextHesitationAt=Now+Tuning.DecisionSeconds*2; return; }
        ChooseTask();
    }
    if(!Target.IsValid()) {
        if(Now>=NextExploreAt) {
            const float Angle=Random.FRandRange(0,2*PI),Distance=Random.FRandRange(250,750);
            ExploreDestination=Hero->GetActorLocation()+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Distance;
            NextExploreAt=Now+6.;
        }
        MoveTowards(ExploreDestination,60); return;
    }
    // Do not track a moving food/player through walls after acquiring it.
    if(Goal!=EMCPlaytestBotGoal::Deliver && !CanObserve(Target.Get())) {ReleaseInputs(); ResetApproach(); Target.Reset(); Goal=EMCPlaytestBotGoal::Idle; return;}
    WorkAtTarget();
}
