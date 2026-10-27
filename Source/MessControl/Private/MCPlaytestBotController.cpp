#include "MCPlaytestBotController.h"
#include "MCPlaytestPhysicalApproach.h"
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
#include "MCCoffeeWipe.h"
#include "MCFirePatch.h"
#include "MCBossCharacter.h"
#include "MCNutEnemy.h"
#include "MCNutKamikaze.h"
#include "MCNutBoss.h"
#include "MCNutRainEvent.h"
#include "MCSingleDayDirector.h"
#include "MCNutLandingShadow.h"
#include "MCNutSpellProjectile.h"
#include "MCSmallMintCandy.h"
#include "MCIceEvent.h"
#include "MCCataclysmJelly.h"
#include "MCThroat.h"
#include "MCTongue.h"
#include "MCGameState.h"
#include "MCPlayerState.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NavigationData.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"

namespace {
AMCNutRainEvent* ActiveNutEncounter(UWorld* World)
{
    const auto* State=World?World->GetGameState<AMCGameState>():nullptr;
    const auto* Director=State?State->SingleDayDirector.Get():nullptr;
    return State && State->bSingleDayLoop && IsValid(Director) && Director->Stage==EMCSingleDayStage::Nuts
        && IsValid(Director->NutEvent) && Director->NutEvent->Settings.bBossEncounter
        && Director->NutEvent->Stage==EMCNutRainStage::Enemies?Director->NutEvent.Get():nullptr;
}
bool IsKnownEncounterBoss(UWorld* World,const AMCNutBoss* Boss)
{
    const auto* Encounter=ActiveNutEncounter(World);
    return Encounter && IsValid(Boss) && Boss->EncounterOwner==Encounter;
}
bool IsDestructionFood(const AMCFoodActor* Food)
{
    return IsValid(Food) && !Food->bBrushTool && !Food->bFragment && Food->FoodData.Kind==EMCFoodKind::Food;
}
bool NeedsDiscard(const AMCFoodActor* Food)
{
    return IsValid(Food) && (Food->bSpoiled || Food->FoodData.Kind==EMCFoodKind::Spicy || Food->IsWrongIngredient());
}
bool IsMealWorkStage(EMCSingleDayStage Stage)
{
    return Stage==EMCSingleDayStage::FirstMeal || Stage==EMCSingleDayStage::PostNutMeal
        || Stage==EMCSingleDayStage::PostNutCoffee || Stage==EMCSingleDayStage::PostNutCoffeePause
        || Stage==EMCSingleDayStage::MealRest || Stage==EMCSingleDayStage::MealCoffeeWave
        || Stage==EMCSingleDayStage::BetweenMealsPause;
}
bool HasAvailableMealFood(UWorld* World,const AMCSingleDayDirector* Director,const TSet<int32>& Batches)
{
    if(!IsValid(Director) || !IsMealWorkStage(Director->Stage) || Batches.IsEmpty()) return false;
    for(TActorIterator<AMCFoodActor> Food(World);Food;++Food)
        if(!Food->bBrushTool && !Food->ActorHasTag(TEXT("MCNutRain")) && !Food->IsDisposed()
            && !Food->StackCarrier && Food->Holders.IsEmpty() && Batches.Contains(Food->Batch)
            && (Food->Phase==EMCFoodPhase::Free || Food->Phase==EMCFoodPhase::Falling || Food->Phase==EMCFoodPhase::Stuck)) return true;
    return false;
}
}

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
    NutFlankSide=BotIndex%2==0?1:-1;
    static constexpr float DeliveryLanes[]={0.f,-270.f,270.f,-540.f,540.f};
    PreferredDeliveryLane=DeliveryLanes[FMath::Abs(BotIndex%int32(UE_ARRAY_COUNT(DeliveryLanes)))];
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
    bHasWorkApproach=false;bManualWorkApproach=bManualWorkAtAnchor=false;NextManualPoseCheckAt=0; RecoveryUntil=JumpReleaseAt=0; FailedApproaches.Reset();
    NutFlankTarget.Reset(); NutFlankPoints.Reset(); NutFlankPointIndex=0; NutFlankUntil=NextNutFlankAt=0;
    bNutBossCare=false;
    NextPrimaryAt=PrimaryReleaseAt=NextThrowAt=CollectStartedAt=0;bSeekingWarmth=false;
    LastCollectedAt=DeliveryPoseReadyAt=0;LastFoodLoadCount=0;bDeliveringTower=false;
    CoffeeThreat.Reset();bCoffeeSurvival=bCoffeeHasSafePoint=bCoffeeWasWet=false;NextCoffeePlanAt=0;
    if(Hero) {
        Hero->GetCharacterMovement()->GetNavMovementProperties()->bUseAccelerationForPaths=true;
        LastProgressPosition=Hero->GetActorLocation(); ProgressCheckedAt=GetWorld()->GetTimeSeconds();
        if(const auto* PS=GetPlayerState<AMCPlayerState>()) Hero->ApplyPlayerColor(PS->PlayerColor);
        // CMC turns to path acceleration; when stationary we turn toward the work surface.
        GetWorldTimerManager().SetTimer(DecisionTimer,this,&AMCPlaytestBotController::Decide,Tuning.DecisionSeconds,true,.1f);
    }
}

void AMCPlaytestBotController::ReleaseInputs(bool bDropCollection,bool bKeepSprint)
{
    if(!IsValid(Hero)) return;
    // Legacy carried hazards need their grip intent until the Q throw. A
    // routine path/decision refresh must not drop them at the worker's feet.
    if(bDropCollection || !IsValid(Hero->HeldFood)) {Hero->SetPrimaryInputHeld(false);Hero->SetHandleInputHeld(false);}
    Hero->SetThrowInputHeld(false); Hero->SetBraceInputHeld(false); PrimaryReleaseAt=0;
    Hero->SetJumpInputHeld(false); if(!bKeepSprint) Hero->SetSprintInputHeld(false); JumpReleaseAt=0;
    Hero->SetSelfCareInput(false);
    if(bDropCollection && Hero->FoodCollection->bCollecting) {
        // The player's E collection action, without accidentally opening a nearby
        // chest or grabbing a toothpick through the contextual handle input.
        Hero->SetFoodCollectionInput(false);
    }
}

void AMCPlaytestBotController::OnUnPossess()
{
    GetWorldTimerManager().ClearTimer(DecisionTimer); ReleaseInputs(true); StopMovement();ResetApproach();
    Hero=nullptr; Target.Reset(); bNutBossCare=false; Super::OnUnPossess();
}

void AMCPlaytestBotController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(DecisionTimer); ReleaseInputs(true);ResetApproach();
    Super::EndPlay(EndPlayReason);
}

void AMCPlaytestBotController::Tick(float Dt)
{
    Super::Tick(Dt);
    if(bLabControlled) return;
    if(!HasAuthority() || !IsValid(Hero)) return;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const bool Terminal=GS && (GS->Phase!=EMCShiftPhase::Working || GS->bDayOneComplete || GS->bLobbyWaiting);
    if(!Hero->Status->IsAlive() || Terminal || (!Hero->CanWork() && !Hero->bInCoffee)) {
        if(!bSuspended || Hero->IsPrimaryHeld() || Hero->bBrushing || Hero->bHandling || Hero->bPressedJump || !Hero->GetPendingMovementInputVector().IsNearlyZero())
        { StopMovement(); Hero->CancelGameplayInput(); Hero->ConsumeMovementInputVector(); Target.Reset(); ResetApproach(); RecoveryUntil=JumpReleaseAt=0; Goal=EMCPlaytestBotGoal::Idle; bSuspended=true; }
        bCoffeeSurvival=false;
        return;
    }
    bSuspended=false;
    if(HandleCoffeeSurvival(Dt)) return;
    if(PrimaryReleaseAt>0 && GetWorld()->GetTimeSeconds()>=PrimaryReleaseAt) {Hero->SetPrimaryInputHeld(false);PrimaryReleaseAt=0;}
    if(JumpReleaseAt>0 && GetWorld()->GetTimeSeconds()>=JumpReleaseAt) { Hero->SetJumpInputHeld(false); JumpReleaseAt=0; }
    if(!Hero->CanWork()) return;
    if(GetMoveStatus()==EPathFollowingStatus::Moving) {
        const auto* Following=GetPathFollowingComponent();
        const auto Path=Following?Following->GetPath():FNavPathSharedPtr();
        const auto* Nav=UNavigationSystemV1::GetCurrent(GetWorld());
        // Abstract/direct routes here are short supported return/contact legs.
        // Recheck them each frame just like the crown approach.
        if(Nav && Path.IsValid() && Path->GetNavigationDataUsed()==Nav->GetAbstractNavData()
            && !MCPlaytestPhysicalApproach::CanWalkSegment(Hero,Hero->GetActorLocation(),Following->GetPathDestination())) {
            FailPath();return;
        }
    }
    const bool PhysicalApproaching=bManualWorkApproach && UpdatePhysicalCleanApproach(true);
    ReactToGroundWaves();
    if(PhysicalApproaching) return;
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
    if(bCoffeeSurvival) return TEXT("coffee_survival");
    if(bManualWorkApproach && bManualWorkAtAnchor) return TEXT("physical_tooth_approach");
    static const TCHAR* Names[]={TEXT("idle"),TEXT("clean"),TEXT("repair"),TEXT("spray"),TEXT("break_food"),TEXT("collect_food"),TEXT("pull_food"),TEXT("deliver"),TEXT("calculus"),TEXT("fight"),TEXT("escape"),TEXT("recover"),TEXT("rescue_ice"),TEXT("warm"),TEXT("fog_guard")};
    return Names[uint8(Goal)];
}

FVector AMCPlaytestBotController::TaskPoint(AActor* Actor) const
{
    if(!IsValid(Actor) || !Hero) return FVector::ZeroVector;
    if(const auto* Boss=Cast<AMCBossCharacter>(Actor)) return Boss->GetMeleeTargetPoint(Hero->GetActorLocation());
    if(const auto* Nut=Cast<AMCNutEnemy>(Actor)) return Nut->GetToolTargetPoint(Hero->GetActorLocation());
    if(const auto* Mint=Cast<AMCSmallMintCandy>(Actor)) return Mint->ContactPoint(Hero->GetActorLocation());
    if(const auto* Ice=Cast<AMCIceEvent>(Actor)) return Ice->CandyContactPoint(Hero->GetActorLocation());
    if(const auto* Surface=Cast<AMCMouthSurface>(Actor);Surface && !Surface->bUlcer) {
        FVector Point,Normal;if(Surface->GetNearestPaintPoint(Hero->GetActorLocation(),Point,Normal)) return Point;
    }
    if(const auto* Zone=Cast<AMCFoodDisposal>(Actor)) {
        FTransform Transform; FVector Extent; bool Circular;
        Zone->GetDeliveryZoneGeometry(Transform,Extent,Circular); return Transform.GetLocation();
    }
    if(const auto* Tooth=Cast<AMCArenaTooth>(Actor)) return (Goal==EMCPlaytestBotGoal::Repair?Tooth->Body->Bounds:Tooth->Visual->Bounds).GetBox().GetClosestPointTo(Hero->GetActorLocation());
    if(const auto* Food=Cast<AMCFoodActor>(Actor)) {
        if(Goal==EMCPlaytestBotGoal::CollectFood) return Food->GetActorLocation();
        // Large rounded ingredients have empty AABB corners. Approach their
        // actual visible skin rather than stopping outside chopping contact.
        FHitResult Contact;
        if(IsDestructionFood(Food) && Food->FindGripSurface(Hero->GetActorLocation(),Contact)) return Contact.ImpactPoint;
        return Food->Visual->Bounds.GetBox().GetClosestPointTo(Hero->GetActorLocation());
    }
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

void AMCPlaytestBotController::RefreshMealObjectiveKnowledge()
{
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    auto* Director=State && State->bSingleDayLoop?State->SingleDayDirector.Get():nullptr;
    if(MealObjectiveDirector.Get()!=Director) {MealObjectiveDirector=Director;KnownMealBatches.Reset();}
    if(!IsValid(Director) || !IsMealWorkStage(Director->Stage)) return;
    // Whole entries explicitly belong to the current single-day director.
    // Ordinary fragments inherit Batch, not Owner. Retain that observed batch
    // across chopping, delivery trips and respawns, including the original's
    // short disposed lifetime; never invent a batch from an ID/number range.
    for(TActorIterator<AMCFoodActor> Food(GetWorld());Food;++Food)
        if(!Food->IsActorBeingDestroyed() && !Food->bBrushTool && Food->GetOwner()==Director && Food->Batch>0)
            KnownMealBatches.Add(Food->Batch);
    for(TActorIterator<AMCPlaytestBotController> Bot(GetWorld());Bot;++Bot)
        if(*Bot!=this && Bot->MealObjectiveDirector.Get()==Director)
            for(const int32 Batch:Bot->KnownMealBatches) KnownMealBatches.Add(Batch);
}

bool AMCPlaytestBotController::IsKnownMealFood(const AMCFoodActor* Food) const
{
    const auto* Director=MealObjectiveDirector.Get();
    return IsValid(Food) && IsValid(Director) && IsMealWorkStage(Director->Stage)
        && !Food->bBrushTool && !Food->ActorHasTag(TEXT("MCNutRain"))
        && KnownMealBatches.Contains(Food->Batch);
}

bool AMCPlaytestBotController::IsTaskValid() const
{
    AActor* Actor=Target.Get(); if(!IsValid(Actor)) return false;
    if(Goal==EMCPlaytestBotGoal::Deliver) {
        const auto* Food=CarriedFood();const auto* Zone=Cast<AMCFoodDisposal>(Actor);
        return Food && Zone && Zone->bBrushBin==NeedsDiscard(Food);
    }
    if(Goal==EMCPlaytestBotGoal::RescueIce) {const auto* Player=Cast<AMCToothCharacter>(Actor);return Player && Player->Status->IsAlive() && Player->HasFrozenLegs();}
    if(Goal==EMCPlaytestBotGoal::Clean || Goal==EMCPlaytestBotGoal::Repair) {
        if(const auto* Surface=Cast<AMCMouthSurface>(Actor);Surface && Goal==EMCPlaytestBotGoal::Clean) return !Surface->bUlcer && !Surface->IsClean();
        const auto* Status=Actor->FindComponentByClass<UMCToothStatusComponent>();
        const auto* Tooth=Cast<AMCArenaTooth>(Actor);
        return Status && Status->NeedsCare(Goal==EMCPlaytestBotGoal::Clean) && (!Tooth || Tooth->IsAvailable());
    }
    if(Goal==EMCPlaytestBotGoal::Spray) {
        if(const auto* Fire=Cast<AMCFirePatch>(Actor)) return Fire->IsBurning();
        const auto* Surface=Cast<AMCMouthSurface>(Actor); return Surface && Surface->bUlcer && !Surface->IsHealed();
    }
    if(Goal==EMCPlaytestBotGoal::Calculus) {
        if(HasAvailableMealFood(GetWorld(),MealObjectiveDirector.Get(),KnownMealBatches)) return false;
        const auto* Tooth=Cast<AMCArenaTooth>(Actor); return Tooth && Tooth->IsAvailable() && Tooth->Calculus->HasCalculus();
    }
    if(Goal==EMCPlaytestBotGoal::Fight) {
        if(const auto* Mint=Cast<AMCSmallMintCandy>(Actor)) return Mint->Stage==EMCSmallMintStage::Active && Mint->Health>0;
        if(const auto* Ice=Cast<AMCIceEvent>(Actor)) return Ice->Stage==EMCIceEventStage::Active && Ice->CandyHealth>0;
        if(const auto* Nut=Cast<AMCNutEnemy>(Actor)) return Nut->CanReceiveToolHit();
        const auto* Boss=Cast<AMCBossCharacter>(Actor); return Boss && Boss->CanReceiveWeaponHit() && Boss->Runtime.State!=EMCBossState::Dormant;
    }
    const auto* Food=Cast<AMCFoodActor>(Actor);
    if(Goal==EMCPlaytestBotGoal::BreakFood && (!IsDestructionFood(Food) || !Food->Holders.IsEmpty())) return false;
    return Food && !Food->IsDisposed() && !Food->StackCarrier && Food->Phase!=EMCFoodPhase::Swallowing
        && Food->Phase!=EMCFoodPhase::Equipped && Food->Phase!=EMCFoodPhase::Absorbing;
}

void AMCPlaytestBotController::ChooseTask()
{
    // Keep the physical stack until its pieces have been thrown. Ordinary
    // selection never drops another worker's load or bypasses delivery.
    ReleaseInputs();
    if(Hero->FoodCollection->bCollecting && Hero->FoodCollection->Pieces.IsEmpty()) Hero->SetFoodCollectionInput(false);
    Target.Reset(); Goal=EMCPlaytestBotGoal::Idle;
    ResetApproach();
    const double Now=GetWorld()->GetTimeSeconds(); double Best=DBL_MAX;
    auto Consider=[&](AActor* Actor,EMCPlaytestBotGoal Candidate,float Priority,bool bKnownLandmark=false) {
        if(!bKnownLandmark && !CanObserve(Actor)) return;
        if(const double* Retry=FailedTargets.Find(Actor); Retry && Now<*Retry) return;
        double Score=FVector::Dist2D(Hero->GetActorLocation(),TaskPoint(Actor))-Priority;
        const float SharingPenalty=Candidate==EMCPlaytestBotGoal::Fight && Cast<AMCNutBoss>(Actor) && IsNutEncounterActive()
            ?Tuning.CooperationPenalty*.2f:Tuning.CooperationPenalty;
        for(TActorIterator<AMCPlaytestBotController> It(GetWorld());It;++It)
            if(*It!=this && It->GetTaskTarget()==Actor) Score+=SharingPenalty;
        Score+=Random.FRandRange(0,Skill==EMCPlaytestBotSkill::Novice?240.f:50.f);
        if(Score<Best) { Best=Score; Target=Actor; Goal=Candidate; }
    };
    if(const AMCFoodActor* Piece=CarriedFood()) {
        for(TActorIterator<AMCFoodDisposal> It(GetWorld());It;++It)
            if(It->bBrushBin==NeedsDiscard(Piece) && (It->bBrushBin || Cast<AMCThroat>(*It) || It->bEnableLegacyDeliveryZone)) Consider(*It,EMCPlaytestBotGoal::Deliver,3000,true);
        if(Goal==EMCPlaytestBotGoal::Deliver) bDeliveringTower=true;
    } else {
        if(Hero->Status->NeedsCare(false)) Consider(Hero,EMCPlaytestBotGoal::Repair,1800);
        if(Hero->Status->NeedsCare(true)) Consider(Hero,EMCPlaytestBotGoal::Clean,850);
        for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(*It!=Hero && It->GetController() && It->Status->IsAlive()) {
            if(It->HasFrozenLegs()) Consider(*It,EMCPlaytestBotGoal::RescueIce,2300);
            if(It->Status->NeedsCare(false)) Consider(*It,EMCPlaytestBotGoal::Repair,1500);
            if(It->Status->NeedsCare(true)) Consider(*It,EMCPlaytestBotGoal::Clean,700);
        }
        const bool MealFoodPending=HasAvailableMealFood(GetWorld(),MealObjectiveDirector.Get(),KnownMealBatches);
        for(TActorIterator<AMCArenaTooth> It(GetWorld());It;++It) if(It->IsAvailable()) {
            if(It->Status->NeedsCare(false)) Consider(*It,EMCPlaytestBotGoal::Repair,1100);
            if(It->Status->NeedsCare(true)) Consider(*It,EMCPlaytestBotGoal::Clean,850);
            if(!MealFoodPending && It->Calculus->HasCalculus()) Consider(*It,EMCPlaytestBotGoal::Calculus,400);
        }
        for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) {
            if(It->bUlcer && !It->IsHealed()) Consider(*It,EMCPlaytestBotGoal::Spray,1300);
            else if(!It->bUlcer && !It->IsClean()) Consider(*It,EMCPlaytestBotGoal::Clean,900);
        }
        for(TActorIterator<AMCFirePatch> It(GetWorld());It;++It) if(It->IsBurning()) Consider(*It,EMCPlaytestBotGoal::Spray,1500);
        for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) {
            if(It->bBrushTool || It->IsDisposed() || It->StackCarrier || !It->Holders.IsEmpty()
                || (It->Phase!=EMCFoodPhase::Free && It->Phase!=EMCFoodPhase::Falling && It->Phase!=EMCFoodPhase::Stuck)) continue;
            const bool KnownObjective=IsKnownMealFood(*It);
            if(It->UsesLegacyGrip()) Consider(*It,EMCPlaytestBotGoal::PullFood,
                It->FoodData.Kind==EMCFoodKind::Spicy?1600:NeedsDiscard(*It)?850:650,KnownObjective);
            else if(It->bFragment) Consider(*It,EMCPlaytestBotGoal::CollectFood,NeedsDiscard(*It)?1250:1050,KnownObjective);
            else Consider(*It,EMCPlaytestBotGoal::BreakFood,850,KnownObjective);
        }
        for(TActorIterator<AMCSmallMintCandy> It(GetWorld());It;++It)
            if(It->Stage==EMCSmallMintStage::Active && It->Health>0) Consider(*It,EMCPlaytestBotGoal::Fight,2200,true);
        for(TActorIterator<AMCBossCharacter> It(GetWorld());It;++It)
            if(It->CanReceiveWeaponHit() && It->Runtime.State!=EMCBossState::Dormant) Consider(*It,EMCPlaytestBotGoal::Fight,1700);
        for(TActorIterator<AMCNutEnemy> It(GetWorld());It;++It) if(It->CanReceiveToolHit()) {
            const auto* Boss=Cast<AMCNutBoss>(*It);
            const float Priority=Boss?(Boss->BossRole==EMCNutBossRole::Mage?2000:1650):Cast<AMCNutKamikaze>(*It)?2400:1950;
            Consider(*It,EMCPlaytestBotGoal::Fight,Priority,IsKnownEncounterBoss(GetWorld(),Boss));
        }
    }
    AimError=Random.FRandRange(-Tuning.AimErrorDegrees,Tuning.AimErrorDegrees);
    NextTaskAt=Now+Tuning.CommitSeconds;
    LastWorkProgressAt=Now; LastTargetProgress=TargetProgress();
}

double AMCPlaytestBotController::TargetProgress() const
{
    AActor* Actor=Target.Get(); if(!IsValid(Actor)) return 0;
    if(const auto* Food=Cast<AMCFoodActor>(Actor)) return -Food->Health+Food->PullProgress*100;
    if(const auto* Mint=Cast<AMCSmallMintCandy>(Actor)) return -Mint->Health;
    if(const auto* Ice=Cast<AMCIceEvent>(Actor)) return -Ice->CandyHealth;
    if(const auto* Surface=Cast<AMCMouthSurface>(Actor)) return Surface->bUlcer?Surface->Healing*100:(1-Surface->RemainingLiquid())*100;
    if(const auto* Fire=Cast<AMCFirePatch>(Actor)) return -Fire->Heat*100;
    if(const auto* Boss=Cast<AMCBossCharacter>(Actor)) return -Boss->Runtime.Health;
    if(const auto* Boss=Cast<AMCNutBoss>(Actor)) return -Boss->Health-Boss->ShieldHealth;
    if(const auto* Nut=Cast<AMCNutEnemy>(Actor)) return -Nut->Health;
    if(const auto* Player=Cast<AMCToothCharacter>(Actor);Player && Goal==EMCPlaytestBotGoal::RescueIce) return -Player->IceLegHealth;
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
    return !GetWorld()->OverlapBlockingTestByChannel(PawnCenter,FQuat::Identity,
        Capsule->GetCollisionObjectType(),FCollisionShape::MakeCapsule(Radius,Height),Query,
        FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
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
    if(bManualWorkApproach && Hero) Hero->ConsumeMovementInputVector();
    bHasWorkApproach=false;bManualWorkApproach=bManualWorkAtAnchor=false;NextManualPoseCheckAt=0;
    NutFlankTarget.Reset(); NutFlankPoints.Reset(); NutFlankPointIndex=0;
}

bool AMCPlaytestBotController::PlanCleanApproach(AMCArenaTooth* Tooth)
{
    TArray<FVector> Points,Normals; Tooth->GetDirtyContactSamples(Points,Normals);
    const double Now=GetWorld()->GetTimeSeconds();
    static TAutoConsoleVariable<int32> CleanDebug(TEXT("mc.Bots.CleanDebug"),0,
        TEXT("Log native tooth-approach rejection gates at most once per worker every two seconds."));
    static TMap<TWeakObjectPtr<AMCPlaytestBotController>,double> NextDebugAt;
    for(auto It=NextDebugAt.CreateIterator();It;++It) if(!It.Key().IsValid()) It.RemoveCurrent();
    const bool Diagnostic=CleanDebug.GetValueOnGameThread()!=0 && Now>=NextDebugAt.FindOrAdd(this);
    int32 Tested=0,ProjectAttempts=0,Projected=0,NavReject=0,FloorReject=0,CapsuleReject=0,PoseAttempts=0,PoseAccepted=0,PathAttempts=0,PathReject=0;
    int32 RangeReject=0,NormalReject=0,ContactOccluded=0,HandPoseReject=0;
    FString FirstFloorBlock,FirstPoseBlock;
    FVector FirstPoseAim=FVector::ZeroVector,FirstPoseStand=FVector::ZeroVector,FirstPoseNormal=FVector::ZeroVector;
    int32 SearchTier=0;
    double MinZ=MAX_dbl,MaxZ=-MAX_dbl;
    if(Diagnostic) for(const FVector Point:Points) {MinZ=FMath::Min(MinZ,Point.Z);MaxZ=FMath::Max(MaxZ,Point.Z);}
    auto Project=[&](FVector Candidate,FVector& Stand) {
        if(Diagnostic) ++ProjectAttempts;
        if(ProjectStandingPosition(Candidate,Stand)) {if(Diagnostic) ++Projected;return true;}
        if(!Diagnostic) return false;
        // Classification repeats the same read-only gates after a rejected
        // candidate; it never changes the normal projection/standing decision.
        auto* Nav=UNavigationSystemV1::GetCurrent(GetWorld());FNavLocation Location;
        if(!Nav || !Nav->ProjectPointToNavigation(Candidate,Location,FVector(65,65,600),&Hero->GetNavAgentPropertiesRef())) {++NavReject;return false;}
        FHitResult Floor;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCPlaytestCleanDebugFloor),false,Hero);
        const float Height=Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        if(!GetWorld()->LineTraceSingleByChannel(Floor,Location.Location+FVector(0,0,Height+15),Location.Location-FVector(0,0,160),ECC_Visibility,Query)
            || Floor.ImpactNormal.Z<Hero->GetCharacterMovement()->GetWalkableFloorZ()
            || Cast<AMCArenaTooth>(Floor.GetActor()) || Cast<APawn>(Floor.GetActor()) || Cast<AMCFoodActor>(Floor.GetActor())) {
            ++FloorReject;if(FirstFloorBlock.IsEmpty()) FirstFloorBlock=GetNameSafe(Floor.GetActor());
        } else ++CapsuleReject;
        return false;
    };
    FailedApproaches.RemoveAll([Now](const auto& Entry){ return Now>=Entry.Value; });
    struct FOption { FVector Stand,Aim,Normal; double Score; };
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
        if(Diagnostic) ++PoseAttempts;
        if(!Hero->BrushContact->CanReachFromPose(Stand,Facing,Aim,Normal,Tooth)) {
            if(Diagnostic) {
                const FVector Offset=Aim-Stand;
                if(Offset.Size2D()>Hero->BrushContact->SurfaceReach) ++RangeReject;
                else if(FVector::DotProduct(Normal.GetSafeNormal(),(-Offset).GetSafeNormal())<.05f) ++NormalReject;
                else {
                    FHitResult Block;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCPlaytestCleanDebugPose),false,Hero);Query.AddIgnoredActor(Tooth);
                    if(GetWorld()->LineTraceSingleByChannel(Block,Stand+FVector(0,0,40),Aim-Normal.GetSafeNormal()*2,ECC_Visibility,Query)) {
                        ++ContactOccluded;if(FirstPoseBlock.IsEmpty()) FirstPoseBlock=GetNameSafe(Block.GetActor());
                    } else {
                        ++HandPoseReject;
                        if(HandPoseReject==1) {FirstPoseAim=Aim;FirstPoseStand=Stand;FirstPoseNormal=Normal;}
                    }
                }
            }
            return;
        }
        if(Diagnostic) ++PoseAccepted;
        const double Score=FVector::DistSquared2D(Origin,Stand)+FMath::Square(float(Aim.Z-Stand.Z-65))*.15;
        Options.Add({Stand,Aim,Normal,Score});
    };
    // Search actual remaining stains, including the opposite side of the crown.
    // The fixed bounding-box aim never changed after its reachable side was clean.
    const bool CurrentPositionClear=IsApproachPositionClear(Origin);
    FVector MouthCenter=FVector::ZeroVector;
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) if(!It->CurrentVertices().IsEmpty()) {MouthCenter=It->Surface->Bounds.Origin;break;}
    const int32 Stride=FMath::Max(1,Points.Num()/96);
    const int32 Start=Points.IsEmpty()?0:Random.RandRange(0,Stride-1);
    auto LogPlan=[&](bool Chosen) {
        if(!Diagnostic) return;
        NextDebugAt.FindOrAdd(this)=Now+2.;
        UE_LOG(LogTemp,Display,TEXT("MC_BOT_CLEAN_PLAN hero=%s tooth=%s chosen=%d dirty_samples=%d tested=%d project_attempts=%d projected=%d nav_reject=%d floor_reject=%d capsule_reject=%d first_floor=%s pose_attempts=%d pose_accepted=%d options=%d path_attempts=%d path_reject=%d dirty_z_min=%.1f dirty_z_max=%.1f pawn=%s body_center=%s body_extent=%s"),
            *GetNameSafe(Hero),*GetNameSafe(Tooth),Chosen?1:0,Points.Num(),Tested,ProjectAttempts,Projected,NavReject,FloorReject,CapsuleReject,*FirstFloorBlock,
            PoseAttempts,PoseAccepted,Options.Num(),PathAttempts,PathReject,Points.IsEmpty()?0.:MinZ,Points.IsEmpty()?0.:MaxZ,
            *Hero->GetActorLocation().ToString(),*Tooth->Body->Bounds.Origin.ToString(),*Tooth->Body->Bounds.BoxExtent.ToString());
        UE_LOG(LogTemp,Display,TEXT("MC_BOT_CLEAN_POSE hero=%s tooth=%s range_reject=%d normal_reject=%d contact_occluded=%d hand_pose_reject=%d first_blocker=%s first_hand_aim=%s first_hand_stand=%s first_hand_normal=%s stand_floor_z=%.1f capsule_half=%.1f search_tier=%d surface_reach=%.1f hand_travel=%.1f hand_vertical=%.1f"),
            *GetNameSafe(Hero),*GetNameSafe(Tooth),RangeReject,NormalReject,ContactOccluded,HandPoseReject,*FirstPoseBlock,
            *FirstPoseAim.ToString(),*FirstPoseStand.ToString(),*FirstPoseNormal.ToString(),
            FirstPoseStand.Z-Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-4.,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),SearchTier,
            Hero->BrushContact->SurfaceReach,Hero->BrushContact->MaxHandTravel,Hero->BrushContact->MaxHandVerticalTravel);
    };
    // Most crowns use the original cheap search. Wider offsets and the mouth-
    // facing heading are fallbacks only after no baseline pose has a full path.
    for(SearchTier=0;SearchTier<3;++SearchTier) {
        Options.Reset();
        for(int32 I=Start;I<Points.Num();I+=Stride) {
            if(Diagnostic) ++Tested;
            const FVector P=Points[I],N=Normals[I];
            if(SearchTier==0 && CurrentPositionClear) Consider(Origin,P,N);
            FVector Out=N.GetSafeNormal2D();
            if(Out.IsNearlyZero()) Out=(Origin-P).GetSafeNormal2D();
            FVector Heading=Out;
            if(SearchTier==2) {
                const FVector Inward=(MouthCenter-P).GetSafeNormal2D();
                const float Alignment=FVector::DotProduct(Inward,Out);
                if(Inward.IsNearlyZero() || Alignment<=.1f || Alignment>=.98f) continue;
                Heading=Inward;
            }
            const FVector Side=FVector::CrossProduct(FVector::UpVector,Heading);
            for(float Distance:{65.f,105.f,150.f,185.f,205.f}) for(float Tangent:{0.f,-65.f,65.f,-100.f,100.f}) {
                const bool Baseline=Distance<=150.f && FMath::Abs(Tangent)<=65.f;
                if((SearchTier==0 && !Baseline) || (SearchTier==1 && Baseline)) continue;
                FVector Stand;
                if(Project(P+Heading*Distance+Side*Tangent,Stand)) Consider(Stand,P,N);
            }
        }
        Options.Sort([](const auto& A,const auto& B){return A.Score<B.Score;});
        for(const auto& Option:Options) {
            if(FVector::DistSquared2D(Origin,Option.Stand)>FMath::Square(25.f)) {
                if(Diagnostic) ++PathAttempts;
                const auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Origin,Option.Stand,this);
                if(!Path || !Path->IsValid() || Path->IsPartial()) {if(Diagnostic) ++PathReject;continue;}
            }
            WorkStand=Option.Stand; WorkAim=Option.Aim; bHasWorkApproach=true;
            WorkNormal=Option.Normal;bManualWorkApproach=bManualWorkAtAnchor=false;
            LastWorkProgressAt=Now;LogPlan(true);return true;
        }
    }
    // Nav's baked edge can exclude a physically supported gum/inner-crown
    // stance. Only after every all-Nav tier fails, consider a short CMC walk
    // from a full-path Nav anchor; no movement or cleaning rules are bypassed.
    Options.Reset();
    const int32 PhysicalSamples=FMath::Min(32,Points.Num());
    for(int32 Sample=0;Sample<PhysicalSamples;++Sample) {
        const int32 I=(Start+Sample*Points.Num()/PhysicalSamples)%Points.Num();
        const FVector P=Points[I],N=Normals[I];
        FVector Out=N.GetSafeNormal2D();if(Out.IsNearlyZero()) Out=(MouthCenter-P).GetSafeNormal2D();
        TArray<FVector,TInlineAllocator<2>> Headings{Out};
        const FVector Inward=(MouthCenter-P).GetSafeNormal2D();
        const float Alignment=FVector::DotProduct(Inward,Out);
        if(!Inward.IsNearlyZero() && Alignment>.1f && Alignment<.98f) Headings.Add(Inward);
        for(const FVector Heading:Headings) {
            const FVector Side=FVector::CrossProduct(FVector::UpVector,Heading);
            for(float Distance:{65.f,105.f,150.f,185.f,205.f}) for(float Tangent:{0.f,-65.f,65.f}) {
                FVector Stand;
                if(MCPlaytestPhysicalApproach::FindStandingPose(Hero,P+Heading*Distance+Side*Tangent,Stand)) Consider(Stand,P,N);
            }
        }
    }
    Options.Sort([](const auto& A,const auto& B){return A.Score<B.Score;});
    auto* Nav=UNavigationSystemV1::GetCurrent(GetWorld());
    const float Half=Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    int32 AnchorAttempts=0,AnchorNavReject=0,AnchorFloorReject=0,AnchorDistanceReject=0,AnchorHeightReject=0,AnchorSegmentReject=0,AnchorPathReject=0;
    int32 DirectPhysicalCandidates=0;
    FString FirstAnchorFailure;
    FVector FirstAnchorProjection=FVector::ZeroVector,FirstAnchor=FVector::ZeroVector,FirstPhysicalStand=FVector::ZeroVector;
    auto RecordAnchorFailure=[&](const TCHAR* Reason,FVector Projection,FVector Anchor,FVector Stand) {
        if(Diagnostic && FirstAnchorFailure.IsEmpty()) {FirstAnchorFailure=Reason;FirstAnchorProjection=Projection;FirstAnchor=Anchor;FirstPhysicalStand=Stand;}
    };
    auto LogPhysical=[&](bool Chosen) {
        if(!Diagnostic) return;
        UE_LOG(LogTemp,Display,TEXT("MC_BOT_CLEAN_PHYSICAL hero=%s tooth=%s chosen=%d options=%d anchor_attempts=%d nav_reject=%d floor_reject=%d distance_reject=%d height_reject=%d segment_reject=%d path_reject=%d direct_physical_candidates=%d first_failure=%s first_projection=%s first_anchor=%s first_stand=%s origin=%s anchor=%s stand=%s aim=%s"),
            *GetNameSafe(Hero),*GetNameSafe(Tooth),Chosen?1:0,Options.Num(),AnchorAttempts,AnchorNavReject,AnchorFloorReject,AnchorDistanceReject,AnchorHeightReject,AnchorSegmentReject,AnchorPathReject,DirectPhysicalCandidates,
            *FirstAnchorFailure,*FirstAnchorProjection.ToString(),*FirstAnchor.ToString(),*FirstPhysicalStand.ToString(),*Origin.ToString(),*WorkNavAnchor.ToString(),*WorkStand.ToString(),*WorkAim.ToString());
    };
    // Already standing just outside the baked Nav edge: the Nav prefix is
    // empty. A fully validated short physical leg must not require a new path
    // from that off-Nav origin back to an unnecessary anchor first.
    for(int32 I=0;I<FMath::Min(32,Options.Num());++I) {
        const auto& Option=Options[I];
        if(!MCPlaytestPhysicalApproach::CanWalkSegment(Hero,Origin,Option.Stand)) continue;
        ++DirectPhysicalCandidates;
        StopMovement();Hero->ConsumeMovementInputVector();
        WorkStand=Option.Stand;WorkAim=Option.Aim;WorkNormal=Option.Normal;WorkNavAnchor=Origin;
        bHasWorkApproach=bManualWorkApproach=bManualWorkAtAnchor=true;NextManualPoseCheckAt=0;LastWorkProgressAt=Now;
        LogPhysical(true);LogPlan(true);return true;
    }
    for(int32 I=0;Nav && I<FMath::Min(32,Options.Num());++I) {
        const auto& Option=Options[I];
        const FVector Out=(Option.Stand-Option.Aim).GetSafeNormal2D();
        for(float Angle:{0.f,-45.f,45.f,-90.f,90.f,180.f,360.f}) {
            FNavLocation AnchorProjection;
            const FVector Candidate=Angle==360.f?Option.Stand:Option.Stand+Out.RotateAngleAxis(Angle,FVector::UpVector)*160.f;
            if(!Nav->ProjectPointToNavigation(Candidate,AnchorProjection,FVector(180,180,200),&Hero->GetNavAgentPropertiesRef())) {++AnchorNavReject;continue;}
            ++AnchorAttempts;FVector Anchor=FVector::ZeroVector;
            if(!MCPlaytestPhysicalApproach::FindStandingPose(Hero,AnchorProjection.Location,Anchor)) {
                ++AnchorFloorReject;RecordAnchorFailure(TEXT("standing_pose"),AnchorProjection.Location,Anchor,Option.Stand);continue;
            }
            if(FVector::DistSquared(Anchor,Option.Stand)>FMath::Square(180.f)) {
                ++AnchorDistanceReject;RecordAnchorFailure(TEXT("anchor_distance"),AnchorProjection.Location,Anchor,Option.Stand);continue;
            }
            if(FMath::Abs(Anchor.Z-Half-4.f-AnchorProjection.Location.Z)>Hero->GetCharacterMovement()->MaxStepHeight+20.f) {
                ++AnchorHeightReject;RecordAnchorFailure(TEXT("nav_height"),AnchorProjection.Location,Anchor,Option.Stand);continue;
            }
            if(!MCPlaytestPhysicalApproach::CanWalkSegment(Hero,Anchor,Option.Stand)) {
                ++AnchorSegmentReject;RecordAnchorFailure(TEXT("physical_segment"),AnchorProjection.Location,Anchor,Option.Stand);continue;
            }
            const auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Origin,Anchor,this);
            if(!Path || !Path->IsValid() || Path->IsPartial()) {
                ++AnchorPathReject;RecordAnchorFailure(!Path?TEXT("path_null"):!Path->IsValid()?TEXT("path_invalid"):TEXT("path_partial"),AnchorProjection.Location,Anchor,Option.Stand);continue;
            }
            WorkStand=Option.Stand;WorkAim=Option.Aim;WorkNormal=Option.Normal;WorkNavAnchor=Anchor;
            bHasWorkApproach=bManualWorkApproach=true;bManualWorkAtAnchor=false;NextManualPoseCheckAt=0;LastWorkProgressAt=Now;
            LogPhysical(true);
            LogPlan(true);return true;
        }
    }
    LogPhysical(false);
    LogPlan(false);
    return false;
}

bool AMCPlaytestBotController::UpdatePhysicalCleanApproach(bool bSubmitInput)
{
    if(!bManualWorkApproach) return false;
    auto* Tooth=Cast<AMCArenaTooth>(Target.Get());
    if(!bHasWorkApproach || Goal!=EMCPlaytestBotGoal::Clean || !IsValid(Tooth) || !Tooth->IsAvailable()
        || !Tooth->Status->NeedsCare(true) || !Hero->CanWork() || Hero->HasFrozenLegs() || Hero->bInCoffee) {
        ResetApproach();return true;
    }
    const double Now=GetWorld()->GetTimeSeconds();
    if(Now>=NextManualPoseCheckAt) {
        NextManualPoseCheckAt=Now+.25;
        FVector Supported;
        const FRotator Facing(0,(WorkAim-WorkStand).Rotation().Yaw+AimError,0);
        if(!MCPlaytestPhysicalApproach::FindStandingPose(Hero,WorkStand,Supported)
            || FMath::Abs(Supported.Z-WorkStand.Z)>15.f
            || !Hero->BrushContact->CanReachFromPose(Supported,Facing,WorkAim,WorkNormal,Tooth)) {FailPath();return true;}
    }
    if(!bManualWorkAtAnchor) {
        if(FVector::DistSquared2D(Hero->GetActorLocation(),WorkNavAnchor)>FMath::Square(15.f)) {
            ReleaseInputs();MoveTowards(WorkNavAnchor,8.f);return true;
        }
        StopMovement();Hero->ConsumeMovementInputVector();bManualWorkAtAnchor=true;
    }
    if(FVector::DistSquared2D(Hero->GetActorLocation(),WorkStand)<=FMath::Square(20.f)) {
        Hero->ConsumeMovementInputVector();return false;
    }
    Hero->SetPrimaryInputHeld(false);Hero->SetSprintInputHeld(false);
    if(!bSubmitInput) return true;
    // Re-check the entire remaining short segment each movement frame. Native
    // CMC performs the actual walk; an appearing wall/gap cancels, never snaps.
    if(!MCPlaytestPhysicalApproach::SubmitWalkInput(Hero,WorkStand)) {FailPath();return true;}
    SetControlRotation((WorkStand-Hero->GetActorLocation()).GetSafeNormal2D().Rotation());
    return true;
}

bool AMCPlaytestBotController::PlanLabFloorApproach(AMCMouthSurface* Surface)
{
    const double Now=GetWorld()->GetTimeSeconds();
    FailedApproaches.RemoveAll([Now](const auto& Entry){return Now>=Entry.Value;});
    struct FOption {FVector Stand,Aim;double Score;};
    TArray<FOption> Options;
    const FVector Origin=Hero->GetActorLocation();
    const FTransform Transform=Surface->GetActorTransform();
    struct FWetPoint {FVector Aim,Normal;double Distance;};TArray<FWetPoint> WetPoints;
    bool CurrentFailed=false;
    for(const auto& Bad:FailedApproaches) CurrentFailed|=FVector::DistSquared2D(Bad.Key,Origin)<FMath::Square(55.f);
    const bool CurrentClear=!CurrentFailed && IsApproachPositionClear(Origin);
    // Scan every surviving cell so a one-cell residue cannot block the meal.
    // Expensive floor/nav/contact queries are bounded to the nearest 32 points.
    for(int32 Y=0;Y<FMCCoffeeWipe::Size;++Y) for(int32 X=0;X<FMCCoffeeWipe::Size;++X) {
        const FVector2D UV((X+.5)/FMCCoffeeWipe::Size,(Y+.5)/FMCCoffeeWipe::Size);
        if(!FMCCoffeeWipe::WetAt(Surface->LiquidDepositMask,UV,Surface->LiquidSeed,Surface->bPaintTile,Surface->bPaintTile?63:100)
            || (Surface->WipeMask.Num()==FMCCoffeeWipe::Count && Surface->WipeMask[Y*FMCCoffeeWipe::Size+X]<=63)) continue;
        const FVector Aim=Transform.TransformPosition(FVector((UV.X-.5)*2*Surface->LiquidHalfSize,(UV.Y-.5)*2*Surface->LiquidHalfSize,0));
        WetPoints.Add({Aim,Surface->GetActorUpVector(),FVector::DistSquared2D(Origin,Aim)});
    }
    WetPoints.Sort([](const auto& A,const auto& B){return A.Distance<B.Distance;});
    const int32 Limit=FMath::Min(32,WetPoints.Num());
    for(int32 Sample=0;Sample<Limit;++Sample) {
        const int32 Index=WetPoints.Num()<=32 || Sample<16?Sample:(Sample-16)*WetPoints.Num()/16;
        FVector Aim=WetPoints[Index].Aim,Normal=WetPoints[Index].Normal;FHitResult Floor;
        if(const auto* Tongue=Surface->GetTongue();Tongue && Tongue->SurfacePoint(Aim,Floor)) {Aim=Floor.ImpactPoint+Floor.ImpactNormal*2;Normal=Floor.ImpactNormal;}
        const FRotator CurrentFacing(0,(Aim-Origin).Rotation().Yaw,0);
        if(CurrentClear && Hero->BrushContact->CanReachFromPose(Origin,CurrentFacing,Aim,Normal,Surface)) {
            WorkStand=Origin;WorkAim=Aim;bHasWorkApproach=true;LastWorkProgressAt=Now;return true;
        }
        const FVector Out=(Origin-Aim).GetSafeNormal2D(KINDA_SMALL_NUMBER,-Hero->GetActorForwardVector());
        for(float Angle:{0.f,-90.f,90.f,180.f}) for(float Distance:{65.f,105.f}) {
            FVector Stand;
            if(!ProjectStandingPosition(Aim+Out.RotateAngleAxis(Angle,FVector::UpVector)*Distance,Stand)) continue;
            bool Failed=false;
            for(const auto& Bad:FailedApproaches) Failed|=FVector::DistSquared2D(Bad.Key,Stand)<FMath::Square(55.f);
            if(Failed) continue;
            const FRotator Facing(0,(Aim-Stand).Rotation().Yaw,0);
            if(!Hero->BrushContact->CanReachFromPose(Stand,Facing,Aim,Normal,Surface)) continue;
            Options.Add({Stand,Aim,FVector::DistSquared2D(Origin,Stand)});
        }
    }
    Options.Sort([](const auto& A,const auto& B){return A.Score<B.Score;});
    for(const auto& Option:Options) {
        if(Option.Score>FMath::Square(25.f)) {
            const auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Origin,Option.Stand,this);
            if(!Path || !Path->IsValid() || Path->IsPartial()) continue;
        }
        WorkStand=Option.Stand;WorkAim=Option.Aim;bHasWorkApproach=true;LastWorkProgressAt=Now;return true;
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
    // A large enemy's exposed skin may lie just beyond baked Nav. Once the
    // worker is close, finish only the same fully supported short physical
    // leg used for crowns; repeatedly stopping at the clamped Nav edge cannot
    // produce an ordinary weapon contact.
    if(Goal==EMCPlaytestBotGoal::Fight && Cast<AMCNutEnemy>(Target.Get()) && Hero->CanWork()
        && !Hero->HasFrozenLegs() && !Hero->bInCoffee && Hero->GetCharacterMovement()->IsMovingOnGround()
        && FVector::DistSquared2D(Destination,Projected.Location)>FMath::Square(Acceptance)) {
        FVector Stand;
        if(MCPlaytestPhysicalApproach::FindStandingPose(Hero,Destination,Stand)
            && MCPlaytestPhysicalApproach::CanWalkSegment(Hero,Hero->GetActorLocation(),Stand)) {
            FAIMoveRequest ContactApproach(Stand);ContactApproach.SetAcceptanceRadius(Acceptance);
            ContactApproach.SetUsePathfinding(false);ContactApproach.SetAllowPartialPath(false);
            ContactApproach.SetReachTestIncludesAgentRadius(false);ContactApproach.SetProjectGoalLocation(false);
            if(MoveTo(ContactApproach).Code!=EPathFollowingRequestResult::Failed) return;
        }
    }
    FAIMoveRequest Request(Projected.Location);
    Request.SetAcceptanceRadius(Acceptance); Request.SetUsePathfinding(true); Request.SetAllowPartialPath(false);
    Request.SetReachTestIncludesAgentRadius(false); Request.SetProjectGoalLocation(false);
    if(GetMoveStatus()!=EPathFollowingStatus::Moving) {LastProgressPosition=Hero->GetActorLocation(); ProgressCheckedAt=Now;}
    if(MoveTo(Request).Code!=EPathFollowingRequestResult::Failed) return;
    // A completed crown approach can leave the capsule just outside baked
    // Nav. Return by an ordinary, short direct move before resuming the full
    // Nav route. Neither a wall nor unsupported ground is skipped.
    FNavLocation OriginProjection;FVector Anchor;
    const FVector Origin=Hero->GetActorLocation();
    if(Hero->CanWork() && !Hero->HasFrozenLegs() && !Hero->bInCoffee && Hero->GetCharacterMovement()->IsMovingOnGround()
        && Nav->ProjectPointToNavigation(Origin,OriginProjection,FVector(180,180,350),&Hero->GetNavAgentPropertiesRef())
        && MCPlaytestPhysicalApproach::FindStandingPose(Hero,OriginProjection.Location,Anchor)
        && FVector::DistSquared2D(Origin,Anchor)>FMath::Square(8.f)
        && MCPlaytestPhysicalApproach::CanWalkSegment(Hero,Origin,Anchor)) {
        const auto* Remaining=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Anchor,Projected.Location,this);
        if(Remaining && Remaining->IsValid() && !Remaining->IsPartial()) {
            FAIMoveRequest Return(Anchor);Return.SetAcceptanceRadius(8.f);Return.SetUsePathfinding(false);
            Return.SetAllowPartialPath(false);Return.SetReachTestIncludesAgentRadius(false);Return.SetProjectGoalLocation(false);
            if(MoveTo(Return).Code!=EPathFollowingRequestResult::Failed) return;
        }
    }
    FailPath();
}

void AMCPlaytestBotController::OnMoveCompleted(FAIRequestID RequestID,const FPathFollowingResult& Result)
{
    Super::OnMoveCompleted(RequestID,Result);
    if(Result.Code!=EPathFollowingResult::Success && Result.Code!=EPathFollowingResult::Aborted && Result.Code!=EPathFollowingResult::Invalid) FailPath();
}

bool AMCPlaytestBotController::ReactToHazard()
{
    const double Now=GetWorld()->GetTimeSeconds();
    if(Now<EscapeUntil) { ResetApproach(); ReleaseInputs(); Hero->SetSprintInputHeld(true); MoveTowards(EscapeDestination,45); Goal=EMCPlaytestBotGoal::Escape; return true; }
    FVector Away=FVector::ZeroVector;
    for(TActorIterator<AMCNutKamikaze> It(GetWorld());It;++It) {
        if(!It->bArmed || It->bExploded || It->bDefeated || It->GetFuseSecondsRemaining()>1.2f || !CanObserve(*It)) continue;
        const FVector Offset=Hero->GetActorLocation()-It->GetActorLocation();
        if(Offset.SizeSquared2D()<FMath::Square(It->BlastRadius+65)) Away+=Offset.GetSafeNormal2D(KINDA_SMALL_NUMBER,Hero->GetActorRightVector());
    }
    for(TActorIterator<AMCCataclysmJelly> It(GetWorld());It;++It) {
        if(!It->bPendingRupture || !CanObserve(*It)) continue;
        const FVector Offset=Hero->GetActorLocation()-It->GetActorLocation();
        if(Offset.SizeSquared2D()<FMath::Square(It->RuptureRadius+65)) Away+=Offset.GetSafeNormal2D(KINDA_SMALL_NUMBER,Hero->GetActorRightVector());
    }
    for(TActorIterator<AMCNutBoss> It(GetWorld());It;++It) {
        FVector Escape;
        if(CanObserve(*It) && It->IsPlayerInThreat(Hero,Escape)) Away+=Escape;
    }
    for(TActorIterator<AMCNutSpellProjectile> It(GetWorld());It;++It) {
        if(It->Impact.bImpacted || !CanObserve(*It)) continue;
        const FVector Closest=FMath::ClosestPointOnSegment(Hero->GetActorLocation(),It->GetActorLocation(),It->Flight.Target);
        const float Radius=It->Flight.Radius+Hero->GetCapsuleComponent()->GetScaledCapsuleRadius()+30;
        if(FVector::DistSquared2D(Hero->GetActorLocation(),Closest)>FMath::Square(Radius)
            || FMath::Abs(Closest.Z-Hero->GetActorLocation().Z)>180) continue;
        FVector Direction=(Hero->GetActorLocation()-Closest).GetSafeNormal2D();
        if(Direction.IsNearlyZero()) Direction=FVector::CrossProduct((It->Flight.Target-It->Flight.Start).GetSafeNormal2D(),FVector::UpVector);
        Away+=Direction;
    }
    for(TActorIterator<AMCBossCharacter> It(GetWorld());It;++It) {
        if(!CanObserve(*It) || (It->Runtime.State!=EMCBossState::Telegraph && It->Runtime.State!=EMCBossState::Attacking)) continue;
        for(const auto& Attack:It->GetAttackDefinitions())
            if(Attack.AttackId==It->Runtime.AttackId && It->IsPlayerInAttack(Hero,Attack,It->Runtime.AttackForward))
                Away+=(Hero->GetActorLocation()-It->GetActorLocation()).GetSafeNormal2D();
    }
    for(TActorIterator<AMCNutLandingShadow> It(GetWorld());It;++It) {
        if(!It->IsWarningActive() || !CanObserve(*It)) continue;
        const FVector Offset=Hero->GetActorLocation()-It->GetActorLocation();
        const float DangerRadius=It->Radius+Hero->GetCapsuleComponent()->GetScaledCapsuleRadius()+25;
        if(FMath::Abs(Offset.Z)>180 || Offset.SizeSquared2D()>FMath::Square(DangerRadius)) continue;
        FVector Direction=Offset.GetSafeNormal2D();
        if(Direction.IsNearlyZero()) Direction=Hero->GetActorRightVector();
        Away+=Direction;
    }
    for(TActorIterator<AMCIceEvent> It(GetWorld());It;++It) if(It->IsActive() && It->Tongue) {
        const auto Transform=It->Tongue->GetActorTransform();
        for(const auto& Strike:It->Strikes) {
            if(Strike.bImpacted || Strike.bCancelled || Strike.bPrisonPunishment || Strike.ImpactAt-Now>1.8) continue;
            const FVector Offset=Hero->GetActorLocation()-Transform.TransformPosition(Strike.Anchor);
            if(Offset.SizeSquared2D()<FMath::Square(It->IcicleRadius+65)) Away+=Offset.GetSafeNormal2D(KINDA_SMALL_NUMBER,Hero->GetActorRightVector());
        }
        const auto& Attack=It->BossAttack;
        if(!Attack.bResolved && Attack.Kind==EMCCandyAttack::MiniCandy && Attack.ImpactAt-Now<1.8 && Attack.ImpactAt>=Now) {
            const FVector Offset=Hero->GetActorLocation()-Transform.TransformPosition(Attack.TargetAnchor);
            if(Offset.SizeSquared2D()<FMath::Square(Attack.TargetRadius+65)) Away+=Offset.GetSafeNormal2D(KINDA_SMALL_NUMBER,Hero->GetActorRightVector());
        }
    }
    if(Away.IsNearlyZero()) { HazardSeenAt=-1; return false; }
    if(HazardSeenAt<0) HazardSeenAt=Now;
    if(Now-HazardSeenAt<Tuning.HazardReactionSeconds) return false;
    EscapeDestination=Hero->GetActorLocation()+Away.GetSafeNormal2D()*450;
    EscapeUntil=Now+1.5; Target.Reset(); ResetApproach(); ReleaseInputs(); Goal=EMCPlaytestBotGoal::Escape;
    Hero->SetSprintInputHeld(true); MoveTowards(EscapeDestination,45); return true;
}

bool AMCPlaytestBotController::InterruptForNutThreat()
{
    // Keep an acquired enemy until it is defeated, and preserve the existing
    // boss encounter. Nearby nuts must be able to interrupt held care/collection.
    if(Goal==EMCPlaytestBotGoal::Fight && IsTaskValid() && !Cast<AMCNutBoss>(Target.Get())) return false;
    const double Now=GetWorld()->GetTimeSeconds();
    AMCNutEnemy* Threat=nullptr; double Best=DBL_MAX;
    for(TActorIterator<AMCNutEnemy> It(GetWorld());It;++It) {
        if(!It->CanReceiveToolHit() || !CanObserve(*It)) continue;
        if(const double* Retry=FailedTargets.Find(*It); Retry && Now<*Retry) continue;
        const double Distance=FVector::Dist2D(Hero->GetActorLocation(),It->GetActorLocation());
        if(Distance>FMath::Max(400.f,It->Settings.AttackRange+180.f)) continue;
        // Defend against the nut attacking this worker before helping another.
        const auto* Boss=Cast<AMCNutBoss>(*It);
        const double Score=Distance-(It->Target==Hero?400.f:0.f)-(Boss?(Boss->BossRole==EMCNutBossRole::Mage?220.f:0.f):180.f);
        if(Score<Best) {Best=Score; Threat=*It;}
    }
    if(!Threat) return false;
    if(Threat==Target.Get()) return false;
    StopMovement(); ReleaseInputs(); ResetApproach();
    Target=Threat; Goal=EMCPlaytestBotGoal::Fight;
    AimError=Random.FRandRange(-Tuning.AimErrorDegrees,Tuning.AimErrorDegrees);
    NextTaskAt=Now+Tuning.CommitSeconds; NextMoveAt=0;
    LastWorkProgressAt=Now; LastTargetProgress=TargetProgress();
    return true;
}

bool AMCPlaytestBotController::IsNutEncounterActive() const
{
    return ActiveNutEncounter(GetWorld())!=nullptr;
}

bool AMCPlaytestBotController::TryNutFlank(AMCNutBoss* Boss)
{
    const double Now=GetWorld()->GetTimeSeconds();
    auto EndAttempt=[&]() {
        NutFlankTarget.Reset(); NutFlankPoints.Reset(); NutFlankPointIndex=0;
        // Give ordinary reduced front strikes a chance instead of orbiting forever.
        NextNutFlankAt=Now+2.5;
    };
    if(NutFlankTarget.IsValid()) {
        if(NutFlankTarget.Get()!=Boss || !Boss->IsShieldProtectingFrom(Hero->GetActorLocation())
            || Now>=NutFlankUntil || FVector::DistSquared2D(Boss->GetActorLocation(),NutFlankCenter)>FMath::Square(140.f)) {
            EndAttempt(); return false;
        }
    } else {
        if(Now<NextNutFlankAt || !Boss->IsShieldProtectingFrom(Hero->GetActorLocation())
            || FVector::DistSquared2D(Hero->GetActorLocation(),Boss->GetActorLocation())>FMath::Square(500.f)) return false;
        const FVector Center=Boss->GetActorLocation();
        const FVector Radial=(Hero->GetActorLocation()-Center).GetSafeNormal2D();
        const float Capsule=Hero->GetCapsuleComponent()->GetScaledCapsuleRadius();
        const float Clearance=Boss->Settings.BodyRadius+Capsule+15;
        const float OrbitRadius=FMath::Max(Boss->Settings.BodyRadius+95,Clearance+55);
        TArray<FVector> Route;
        FVector Previous=Hero->GetActorLocation();
        for(int32 Index=1;Index<=3;++Index) {
            const FVector Direction=Radial.RotateAngleAxis(NutFlankSide*60.f*FMath::Min(Index,2),FVector::UpVector);
            const float Radius=Index==3?Boss->Settings.BodyRadius+70:OrbitRadius;
            FVector Stand;
            if(!ProjectStandingPosition(Center+Direction*Radius,Stand)
                || FVector::DistSquared2D(FMath::ClosestPointOnSegment(Center,Previous,Stand),Center)<FMath::Square(Clearance)) {
                EndAttempt(); return false;
            }
            Route.Add(Stand); Previous=Stand;
        }
        // These world-space arc points stay fixed while the tank turns to its
        // nearest worker. Recomputing a rear point every decision starves swings.
        NutFlankTarget=Boss; NutFlankCenter=Center; NutFlankPoints=MoveTemp(Route);
        NutFlankPointIndex=0; NutFlankUntil=Now+2.5;
    }
    while(NutFlankPoints.IsValidIndex(NutFlankPointIndex)
        && FVector::DistSquared2D(Hero->GetActorLocation(),NutFlankPoints[NutFlankPointIndex])<=FMath::Square(40.f)) ++NutFlankPointIndex;
    if(!NutFlankPoints.IsValidIndex(NutFlankPointIndex)) {EndAttempt();return false;}
    const FVector Stand=NutFlankPoints[NutFlankPointIndex];
    ReleaseInputs(false,true); MoveTowards(Stand,25);
    if(Goal==EMCPlaytestBotGoal::Fight && Target.Get()==Boss)
        Hero->SetSprintInputHeld(FVector::DistSquared2D(Hero->GetActorLocation(),Stand)>FMath::Square(180.f));
    return true;
}

void AMCPlaytestBotController::WorkAtTarget()
{
    AActor* Actor=Target.Get(); if(!Actor) return;
    FVector Point=TaskPoint(Actor);
    const bool RepairArenaTooth=Goal==EMCPlaytestBotGoal::Repair && Cast<AMCArenaTooth>(Actor);
    auto* Nut=Goal==EMCPlaytestBotGoal::Fight?Cast<AMCNutEnemy>(Actor):nullptr;
    if(Nut && Hero->Inventory->Selected!=EMCToolSlot::Pickaxe) {
        // Switching tools drops a carried pile through the same action as a
        // human. Do it before approaching so food cannot keep forcing delivery.
        ReleaseInputs(); Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
        if(Hero->Inventory->Selected!=EMCToolSlot::Pickaxe) return;
    }
    const bool NutEncounter=Nut && IsNutEncounterActive();
    // The knight's shield protects every side. Keep ordinary contact/avoidance
    // movement and attack its durability instead of orbiting for a rear bypass.
    if(Nut && FVector::DistSquared2D(Hero->GetActorLocation(),Nut->GetActorLocation())<FMath::Square(35.f)) {
        // An enemy can pass through the worker capsule. Step out using ordinary
        // movement instead of swinging straight down at an invalid aim vector.
        ReleaseInputs(); MoveTowards(Hero->GetActorLocation()-Hero->GetActorForwardVector()*100,25); return;
    }
    if(Goal==EMCPlaytestBotGoal::Deliver) {
        WorkFoodDelivery(CastChecked<AMCFoodDisposal>(Actor));return;
    }
    if(auto* Surface=Cast<AMCMouthSurface>(Actor);Surface && Goal==EMCPlaytestBotGoal::Clean) {
        // The same remaining-mask approach used by the visual lab is necessary
        // in normal play: the centre of a paint tile can already be clean.
        if(Hero->Inventory->Selected!=EMCToolSlot::Brush) {ReleaseInputs();Hero->Inventory->ServerSelect(EMCToolSlot::Brush);}
        if(bHasWorkApproach && (!IsApproachPositionClear(WorkStand) || GetWorld()->GetTimeSeconds()-LastWorkProgressAt>3)) ResetApproach(true);
        if(!bHasWorkApproach && !PlanLabFloorApproach(Surface)) {FailPath();return;}
        if(FVector::DistSquared2D(Hero->GetActorLocation(),WorkStand)>FMath::Square(24.f)) {ReleaseInputs();MoveTowards(WorkStand,15);return;}
        StopMovement();Hero->SetSelfCareInput(false);
        const FRotator Facing(0,(WorkAim-Hero->GetActorLocation()).Rotation().Yaw,0);
        SetControlRotation(Facing);Hero->SetActorRotation(FMath::RInterpTo(Hero->GetActorRotation(),Facing,Tuning.DecisionSeconds,5));
        FVector Contact,Normal;
        if(!Surface->FindDirtyContact(Hero,Contact,Normal)) {Hero->SetPrimaryInputHeld(false);ResetApproach(true);return;}
        WorkAim=Contact;Hero->SetPrimaryInputHeld(true);return;
    }
    if(auto* Tooth=Cast<AMCArenaTooth>(Actor); Tooth && Goal==EMCPlaytestBotGoal::Clean) {
        if(Hero->Inventory->Selected!=EMCToolSlot::Brush) {
            ReleaseInputs(); Hero->Inventory->ServerSelect(EMCToolSlot::Brush);
            if(Hero->Inventory->Selected!=EMCToolSlot::Brush) return;
        }
        if(bHasWorkApproach && !bManualWorkApproach && !IsApproachPositionClear(WorkStand)) ResetApproach(true);
        if(!bHasWorkApproach && !PlanCleanApproach(Tooth)) {FailPath(); return;}
        if(bManualWorkApproach && UpdatePhysicalCleanApproach(bLabControlled)) return;
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
        WorkAim=Contact;WorkNormal=Normal;
        Hero->SetPrimaryInputHeld(true);
        return;
    }
    // Approach inside the real contact reach before holding a swing. A nut
    // chasing another teammate may otherwise stay just outside weapon contact.
    const float Reach=Nut?FMath::Min(90.f,Hero->Status->Settings.Reach*.75f)
        :RepairArenaTooth?FMath::Clamp(Hero->Inventory->SprayReach()*.70f,90.f,350.f)
        :Goal==EMCPlaytestBotGoal::Spray?180:Goal==EMCPlaytestBotGoal::CollectFood?125:90;
    const FVector Delta=Point-Hero->GetActorLocation();
    if(Actor!=Hero && Delta.Size2D()>Reach) {
        ReleaseInputs(false,NutEncounter);
        const FVector Towards=Delta.GetSafeNormal2D();
        MoveTowards(Point-Towards*(Reach*.65f),25);
        if(NutEncounter && Goal==EMCPlaytestBotGoal::Fight && Target.Get()==Nut)
            Hero->SetSprintInputHeld(Delta.Size2D()>Reach+70.f);
        return;
    }
    if(NutEncounter) Hero->SetSprintInputHeld(false);
    StopMovement();
    const auto* ChoppingFood=Goal==EMCPlaytestBotGoal::BreakFood?Cast<AMCFoodActor>(Actor):nullptr;
    FVector Facing=(Nut?Nut->GetActorLocation()-Hero->GetActorLocation()
        :ChoppingFood?ChoppingFood->Visual->Bounds.Origin-Hero->GetActorLocation():Delta).GetSafeNormal2D();
    if(!Facing.IsNearlyZero()) {
        FRotator Rotation=Facing.Rotation(); Rotation.Yaw+=AimError;
        SetControlRotation(Rotation);
        Hero->SetActorRotation(FMath::RInterpTo(Hero->GetActorRotation(),Rotation,Tuning.DecisionSeconds,5.f));
    }
    EMCToolSlot Slot=EMCToolSlot::Brush;
    if(Goal==EMCPlaytestBotGoal::Spray || RepairArenaTooth) Slot=EMCToolSlot::Spray;
    else if(Goal==EMCPlaytestBotGoal::Calculus || Goal==EMCPlaytestBotGoal::Fight || Goal==EMCPlaytestBotGoal::RescueIce) Slot=EMCToolSlot::Pickaxe;
    else if(Goal==EMCPlaytestBotGoal::BreakFood) Slot=CastChecked<AMCFoodActor>(Actor)->IsHardFood()
        && !Hero->Inventory->HasUpgrade(EMCToolUpgrade::Chainsaw)?EMCToolSlot::Pickaxe:EMCToolSlot::Knife;
    if(Hero->Inventory->Selected!=Slot) {
        ReleaseInputs(); Hero->Inventory->ServerSelect(Slot);
        if(Hero->Inventory->Selected!=Slot) return;
    }
    if(Goal==EMCPlaytestBotGoal::Repair && Actor==Hero) {
        // Brush + primary means cleaning. A clean injured worker needs the
        // player's self-care + handle action to enter ordinary timed repair.
        Hero->SetPrimaryInputHeld(false);Hero->SetSelfCareInput(true);
        Hero->SetHandleInputHeld(true);return;
    }
    Hero->SetSelfCareInput(Actor==Hero);
    if(Goal==EMCPlaytestBotGoal::CollectFood) {
        Hero->SetPrimaryInputHeld(false);
        if(!Hero->FoodCollection->bCollecting) {Hero->SetFoodCollectionInput(true);CollectStartedAt=GetWorld()->GetTimeSeconds();}
        return;
    }
    if(Goal==EMCPlaytestBotGoal::Clean && Actor!=Hero) {
        FVector Contact,Normal; bool Reachable=false;
        if(auto* Surface=Cast<AMCMouthSurface>(Actor)) Reachable=Surface->FindDirtyContact(Hero,Contact,Normal);
        else if(auto* Player=Cast<AMCToothCharacter>(Actor)) Reachable=Player->FindPlayerBrushContact(Hero,Contact,Normal);
        if(!Reachable) {ReleaseInputs(); RecoverFromStall(); return;}
    }
    // Arena teeth repair through the spray's ordinary first-contact ray;
    // self/teammate care retains the brush and its near-contact rules.
    if(Goal==EMCPlaytestBotGoal::Repair && !RepairArenaTooth && !Hero->CanContact(Actor)) {ReleaseInputs(); FailPath(); return;}
    // Primary selects care by the same nearby target rules as a human. E also
    // requests climbing and opens chests, so it is unsuitable as a repair intent.
    if(Goal==EMCPlaytestBotGoal::Fight || Goal==EMCPlaytestBotGoal::RescueIce) PulsePrimary();
    else Hero->SetPrimaryInputHeld(true);
}

void AMCPlaytestBotController::Decide()
{
    if(bLabControlled) return;
    if(!HasAuthority() || !IsValid(Hero) || !bConfigured) return;
    RefreshMealObjectiveKnowledge();
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    if(GS && (GS->Phase!=EMCShiftPhase::Working || GS->bDayOneComplete)) {
        ReleaseInputs(); StopMovement(); ResetApproach(); Goal=EMCPlaytestBotGoal::Idle; Target.Reset(); return;
    }
    if(HandleCoffeeSurvival()) return;
    if(!Hero->CanWork()) {ReleaseInputs();StopMovement();ResetApproach();Goal=EMCPlaytestBotGoal::Idle;Target.Reset();return;}
    if(RescueFrozenLegs()) return;
    if(ReactToHazard()) return;
    const double Now=GetWorld()->GetTimeSeconds();
    RefreshFoodLoadProgress();
    bool LiveNutBoss=false;
    for(TActorIterator<AMCNutBoss> It(GetWorld());It;++It) if(It->IsEncounterAlive()) {LiveNutBoss=true;break;}
    if(LiveNutBoss && Hero->Status->Settings.bAllowSelfCare && Hero->Status->NeedsCare(false)
        && (Hero->Status->State.Health<Hero->Status->State.MaxHealth*.35f
            || (bNutBossCare && Hero->Status->State.Health<Hero->Status->State.MaxHealth*.75f))) {
        if(!bNutBossCare || Target.Get()!=Hero) {
            StopMovement(); ReleaseInputs(); ResetApproach(); Target=Hero; Goal=EMCPlaytestBotGoal::Repair;
            LastWorkProgressAt=Now; LastTargetProgress=TargetProgress();
        }
        bNutBossCare=true; WorkAtTarget(); return;
    }
    if(bNutBossCare) {bNutBossCare=false;ReleaseInputs();Target.Reset();Goal=EMCPlaytestBotGoal::Idle;NextTaskAt=0;}
    InterruptForNutThreat();
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
    if(HandleEventWork()) return;
    const bool FightingNut=Goal==EMCPlaytestBotGoal::Fight && Cast<AMCNutEnemy>(Target.Get()) && IsTaskValid();
    if(ContinueFoodCollection()) return;
    const auto* Load=CarriedFood();
    const bool ReadyToDeliver=Load && Load->Phase!=EMCFoodPhase::Stuck && Goal!=EMCPlaytestBotGoal::Deliver && !FightingNut;
    const bool KeepNutFight=FightingNut && IsNutEncounterActive();
    const bool KeepDelivery=Goal==EMCPlaytestBotGoal::Deliver && IsTaskValid();
    if(ReadyToDeliver || !IsTaskValid() || (!KeepNutFight && !KeepDelivery && Now>=NextTaskAt && !Hero->IsPrimaryHeld() && !Hero->bHandling)) {
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
    const auto* KnownMint=Cast<AMCSmallMintCandy>(Target.Get());
    const bool ActiveMintObjective=KnownMint && KnownMint->Stage==EMCSmallMintStage::Active;
    const bool KnownFoodObjective=IsKnownMealFood(Cast<AMCFoodActor>(Target.Get()));
    const bool KnownBossObjective=IsKnownEncounterBoss(GetWorld(),Cast<AMCNutBoss>(Target.Get()));
    const bool PlannedPhysicalTooth=bManualWorkApproach && Goal==EMCPlaytestBotGoal::Clean && Cast<AMCArenaTooth>(Target.Get());
    if(Goal!=EMCPlaytestBotGoal::Deliver && !ActiveMintObjective && !KnownFoodObjective && !KnownBossObjective && !PlannedPhysicalTooth && !CanObserve(Target.Get())) {ReleaseInputs(); ResetApproach(); Target.Reset(); Goal=EMCPlaytestBotGoal::Idle; return;}
    WorkAtTarget();
}

void AMCPlaytestBotController::SetLabControlled(bool bEnabled)
{
    if(!HasAuthority()) return;
    bLabControlled=bEnabled;
    GetWorldTimerManager().ClearTimer(DecisionTimer);
    ReleaseInputs(); StopMovement(); Target.Reset(); ResetApproach();
    if(!bEnabled && Hero && bConfigured)
        GetWorldTimerManager().SetTimer(DecisionTimer,this,&AMCPlaytestBotController::Decide,Tuning.DecisionSeconds,true,.1f);
}

void AMCPlaytestBotController::DriveLabTask(AActor* Actor,EMCPlaytestBotGoal Task)
{
    if(!HasAuthority() || !bLabControlled || !IsValid(Hero) || !Hero->CanWork() || !IsValid(Actor)) return;
    const double Now=GetWorld()->GetTimeSeconds();
    if(Target!=Actor || Goal!=Task) {
        ReleaseInputs(); ResetApproach(); Target=Actor; Goal=Task;
        NextMoveAt=0; LastWorkProgressAt=Now; LastTargetProgress=TargetProgress();
    }
    const double Progress=TargetProgress();
    if(FMath::Abs(Progress-LastTargetProgress)>.001) {LastTargetProgress=Progress;LastWorkProgressAt=Now;}
    if(Task==EMCPlaytestBotGoal::Clean) if(auto* Surface=Cast<AMCMouthSurface>(Actor)) {
        if(Hero->Inventory->Selected!=EMCToolSlot::Brush) {ReleaseInputs();Hero->Inventory->ServerSelect(EMCToolSlot::Brush);}
        if(bHasWorkApproach && (!IsApproachPositionClear(WorkStand) || Now-LastWorkProgressAt>3)) ResetApproach(true);
        if(!bHasWorkApproach && !PlanLabFloorApproach(Surface)) {ReleaseInputs();StopMovement();return;}
        if(FVector::DistSquared2D(Hero->GetActorLocation(),WorkStand)>FMath::Square(24.f)) {
            ReleaseInputs();MoveTowards(WorkStand,15);return;
        }
        // Hold a stable pose long enough for the ordinary hand approach to
        // finish; choose another remaining mask region only after real work stalls.
        StopMovement();Hero->SetSelfCareInput(false);
        const FRotator Facing(0,(WorkAim-Hero->GetActorLocation()).Rotation().Yaw,0);
        SetControlRotation(Facing);Hero->SetActorRotation(FMath::RInterpTo(Hero->GetActorRotation(),Facing,Tuning.DecisionSeconds,5.f));
        FVector Contact,Normal;
        if(!Surface->FindDirtyContact(Hero,Contact,Normal)) {Hero->SetPrimaryInputHeld(false);ResetApproach(true);return;}
        WorkAim=Contact;Hero->SetPrimaryInputHeld(true);return;
    }
    if(Task==EMCPlaytestBotGoal::Calculus) if(auto* Tooth=Cast<AMCArenaTooth>(Actor); Tooth && Tooth->Calculus) {
        if(Hero->Inventory->Selected!=EMCToolSlot::Pickaxe) {ReleaseInputs();Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);}
        Hero->SetSelfCareInput(false);
        const bool Buffer=Hero->Inventory->HasUpgrade(EMCToolUpgrade::Buffer);
        // Finish the contact of an already committed chisel stroke before
        // walking toward a different surviving piece.
        FVector SwingPoint,SwingNormal;
        if(!Buffer && Hero->GetCalculusSwingContact(SwingPoint,SwingNormal)
            && Hero->GetToolSwingElapsed()<Hero->Inventory->SwingContactTime()+.15f) {
            StopMovement();SetControlRotation(FRotator(0,(SwingPoint-Hero->GetActorLocation()).Rotation().Yaw,0));
            Hero->SetActorRotation(GetControlRotation());Hero->SetPrimaryInputHeld(true);return;
        }
        const auto& State=Tooth->Calculus->State;
        const int32 PerDeposit=State.Anchors.IsEmpty()?0:State.Pieces.Num()/State.Anchors.Num();
        for(int32 Deposit=0;Deposit<State.Anchors.Num() && PerDeposit>0;++Deposit) {
            bool Alive=false;
            for(int32 I=Deposit*PerDeposit;I<(Deposit+1)*PerDeposit;++I) Alive|=State.Pieces[I]>0;
            if(!Alive) continue;
            const auto& Anchor=State.Anchors[Deposit];
            const FTransform Surface=Tooth->Visual->GetComponentTransform();
            FVector Aim=Surface.TransformPosition(Anchor.Center);
            const FVector SurfaceScale=Surface.GetScale3D().GetAbs().ComponentMax(FVector(.001f));
            FVector Normal=Surface.TransformVectorNoScale(FVector(Anchor.Normal)/SurfaceScale).GetSafeNormal();
            auto Face=[&](FVector Point) {
                SetControlRotation(FRotator(0,(Point-Hero->GetActorLocation()).Rotation().Yaw,0));
                Hero->SetActorRotation(GetControlRotation());
            };
            Face(Aim);
            // Once an exposed deposit is in reach, aim at a surviving piece,
            // rather than continuing to swing at its already empty center.
            FVector Piece,PieceNormal;
            const bool Reachable=Tooth->Calculus->FindContact(Hero,Piece,PieceNormal);
            if(Reachable) {Aim=Piece;Normal=PieceNormal;Face(Aim);}
            const FVector Out=Normal.GetSafeNormal2D();
            if(Reachable && !Buffer) {
                StopMovement();Hero->SetPrimaryInputHeld(true);return;
            }
            FVector Stand;
            bool HaveStand=false;
            if(Buffer && Reachable) {
                Hero->SetPrimaryInputHeld(true);
                FVector Tip;float Radius;
                if(Hero->Inventory->BufferContactSphere(Tip,Radius)) {
                    // Test the actual constrained tip after facing the piece.
                    // A distance-only stop can leave it behind the enamel.
                    if(FVector::DotProduct(Normal,(Tip-Aim).GetSafeNormal())>.05f
                        && Hero->Inventory->CanBufferContact(Tooth,Aim)) {
                        StopMovement();return;
                    }
                    const FVector TipGoal=Aim+Normal*FMath::Min(30.f,Radius*.45f);
                    const FVector Correction=FVector(TipGoal.X-Tip.X,TipGoal.Y-Tip.Y,0).GetClampedToMaxSize(80.f);
                    const FVector Candidate=Hero->GetActorLocation()+Correction;
                    // Keep the body on the exposed side as well. Scenery can
                    // compress the tool pose; never follow that correction
                    // through the crown or abandon this target for generic aim.
                    const float BodyClearance=Hero->GetCapsuleComponent()->GetScaledCapsuleRadius()+10.f;
                    if(FVector::DotProduct(Candidate-Aim,Out)>BodyClearance
                        && ProjectStandingPosition(Candidate,Stand)
                        && FVector::DotProduct(Stand-Aim,Out)>BodyClearance
                        && FVector::DistSquared2D(Hero->GetActorLocation(),Stand)>FMath::Square(8.f)) HaveStand=true;
                }
            }
            if(!HaveStand) {
                // Visit fixed exposed flank poses rather than chasing a sine
                // wave forever. Curved edge pieces can face away from their
                // original deposit anchor, so search both sides after a stall.
                static constexpr float Angles[]={0.f,-35.f,35.f,-70.f,70.f,-85.f,85.f};
                const int32 Phase=FMath::Max(0,FMath::FloorToInt((Now-LastWorkProgressAt-2.)/2.));
                for(int32 Attempt=0;Attempt<UE_ARRAY_COUNT(Angles) && !HaveStand;++Attempt) {
                    const FVector Direction=Out.RotateAngleAxis(Angles[(Phase+Attempt)%UE_ARRAY_COUNT(Angles)],FVector::UpVector);
                    for(float Distance:{105.f,145.f}) if(ProjectStandingPosition(Aim+Direction*Distance,Stand)
                        && FVector::DotProduct(Stand-Aim,Out)>1.f) {HaveStand=true;break;}
                }
            }
            if(!HaveStand) {ReleaseInputs();StopMovement();return;}
            if(FVector::DistSquared2D(Hero->GetActorLocation(),Stand)>FMath::Square(8.f)) {
                if(!Buffer) ReleaseInputs();
                else Hero->SetPrimaryInputHeld(true);
                MoveTowards(Stand,5);return;
            }
            StopMovement();Face(Aim);Hero->SetPrimaryInputHeld(true);return;
        }
        ReleaseInputs();StopMovement();return;
    }
    if(Now-LastWorkProgressAt>3 && Task==EMCPlaytestBotGoal::Clean) {ResetApproach(true);LastWorkProgressAt=Now;}
    WorkAtTarget();
}
