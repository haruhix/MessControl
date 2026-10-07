#include "MCDayDirector.h"
#include "MCGameState.h"
#include "MCGameMode.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCFoodActor.h"
#include "MCFirePatch.h"
#include "MCArenaTooth.h"
#include "MCMouthSurface.h"
#include "MCTongue.h"
#include "MCCoffeeFlood.h"
#include "MCColdCola.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
AMCDayDirector::AMCDayDirector() { PrimaryActorTick.bCanEverTick=true; }
void AMCDayDirector::Start(UMCDayPlan* Plan,int32 InitialStep,bool bManual)
{
    if (!HasAuthority() || !Plan || !Plan->Steps.IsValidIndex(InitialStep)) return;
#if UE_BUILD_SHIPPING
    bManual=false;
#endif
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); if (!GS) return;
    Settings=DuplicateObject<UMCDayPlan>(Plan,this); Settings->Sanitize(); Random.Initialize(GS->RunSeed);
    GS->DayPlan=Plan; GS->StepIndex=InitialStep; GS->bDevManualEvents=bManual;
    GS->DayStartedAt=GS->GetServerWorldTimeSeconds(); GS->bPhysicalBrushes=false; GS->CurrentEvent=nullptr;
    GS->Phase=EMCShiftPhase::Working; GS->bDayOneComplete=false; GS->FailedEvents=0;
    AMCFoodDisposal* Bin=nullptr;
    for (TActorIterator<AMCFoodDisposal> It(GetWorld());It;++It) if (It->bBrushBin) { Bin=*It; break; }
    if (!Bin)
    {
        Bin=GetWorld()->SpawnActor<AMCFoodDisposal>(FVector(-1110,0,140),FRotator::ZeroRotator);
        if (Bin) { Bin->Tags.Add(TEXT("DayOne")); Bin->bBrushBin=true; Bin->Volume->SetBoxExtent(FVector(70,680,240)); Bin->Label->SetRelativeLocation(FVector(80,0,0)); }
    }
    Flood=GetWorld()->SpawnActor<AMCCoffeeFlood>(); EnterStep();
}
int32 AMCDayDirector::CountDirt() const
{
    int32 Count=0;
    for (TActorIterator<AActor> It(GetWorld());It;++It)
    {
        if (const auto* Patch=Cast<AMCMouthSurface>(*It); Patch && Patch->bUlcer) { Count+=!Patch->IsHealed(); continue; }
        if (const auto* Tooth=Cast<AMCArenaTooth>(*It); Tooth && !Tooth->IsAvailable()) continue;
        if (const auto* Status=It->FindComponentByClass<UMCToothStatusComponent>(); Status && Status->NeedsCare(true)) ++Count;
    }
    return Count;
}
int32 AMCDayDirector::CountFood(int32 Batch) const
{
    int32 Count=0; for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if (!It->bBrushTool && !It->IsDisposed() && It->Batch==Batch) ++Count;
    for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if(It->bUlcer && !It->IsHealed() && It->Batch==Batch) ++Count;
    for(TActorIterator<AMCFirePatch> It(GetWorld());It;++It) if(It->IsBurning() && It->Batch==Batch) ++Count;
    return Count;
}
void AMCDayDirector::DropBrushes()
{
    // Kept for legacy developer actions. Every player owns the brush in inventory slot one.
}
void AMCDayDirector::DirtyMouth(bool bCoffee)
{
    auto* GS=GetWorld()->GetGameState<AMCGameState>();
    for (AMCArenaTooth* Tooth:GS->ArenaTeeth) if (IsValid(Tooth) && Tooth->IsAvailable()) Tooth->Status->ApplyCoffee(bCoffee?1.f:.5f);
    if (bCoffee) for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if (It->Status->IsAlive()) It->Status->ApplyCoffee();
    TArray<AMCMouthSurface*> Existing; for (TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if (!It->bUlcer) Existing.Add(*It);
    for (auto* Patch:Existing) Patch->Destroy();
    TArray<AMCTongue*> Tongues; for (TActorIterator<AMCTongue> It(GetWorld());It;++It) Tongues.Add(*It);
    TArray<FVector> Placed;
    for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if(It->bUlcer) Placed.Add(It->GetActorLocation());
    for (int32 I=0;I<Settings->SurfacePatches;++I)
    {
        const float HalfSize=Random.FRandRange(55.f,175.f);
        FHitResult Floor; bool FoundFloor=false;
        for(auto* Tongue:Tongues)
            if(Tongue->RandomGameplaySpawnPoint(Random,HalfSize*1.415f+40,HalfSize*1.6f+90,Placed,Floor)) {FoundFloor=true;break;}
        if(!FoundFloor) {UE_LOG(LogTemp,Warning,TEXT("MC_DIRT_SPAWN no interior tongue footprint for patch %d"),I);continue;}
        const FTransform Pose(FRotationMatrix::MakeFromZX(Floor.ImpactNormal,FVector::ForwardVector).ToQuat(),Floor.ImpactPoint+Floor.ImpactNormal*5);
        auto* Patch=GetWorld()->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),Pose);
        if(Patch) {
            Patch->bRandomizeLiquidSize=false; Patch->LiquidHalfSize=HalfSize; Patch->Batch=GS->StepIndex;
            Patch->FinishSpawning(Pose); Patch->Status->ApplyCoffee(bCoffee?1.f:.5f); Patch->ForceNetUpdate();
            Placed.Add(Floor.ImpactPoint);
        }
    }
}
AMCFoodActor* AMCDayDirector::SpawnMenuFood(FVector Position,int32 Batch)
{
    return SpawnMenuFoodInternal(Position,Batch,false);
}
AMCFoodActor* AMCDayDirector::SpawnMenuFoodDrop(float Height,int32 Batch,bool bHeightFromSurface)
{
    return SpawnMenuFoodInternal(FVector(0,0,Height),Batch,true,bHeightFromSurface);
}
AMCFoodActor* AMCDayDirector::SpawnMenuFoodEntry(int32 Batch)
{
    return SpawnMenuFoodInternal(FVector::ZeroVector,Batch,true,false,true);
}
AMCFoodActor* AMCDayDirector::SpawnMenuFoodInternal(FVector Position,int32 Batch,bool bRandomDrop,bool bHeightFromSurface,bool bMouthEntry)
{
    if (!HasAuthority() || !Settings || Position.ContainsNaN()) return nullptr;
    FName Choice=TEXT("Broccoli"); FMCFoodRow Row; Row.Label=FText::FromString(TEXT("BROCCOLI"));
    if (auto* Table=Settings->Menu.LoadSynchronous())
    {
        TArray<FName> Names=Table->GetRowNames(); Names.Sort(FNameLexicalLess());
        float Total=0; for (const auto Name:Names) if (const auto* R=Table->FindRow<FMCFoodRow>(Name,TEXT("Breakfast"))) { FMCFoodRow V=*R; V.Sanitize(); Total+=V.SelectionWeight; }
        float Pick=Random.FRand()*Total;
        for (const auto Name:Names) if (const auto* R=Table->FindRow<FMCFoodRow>(Name,TEXT("Breakfast")))
        { FMCFoodRow V=*R; V.Sanitize(); if (V.SelectionWeight<=0) continue; Pick-=V.SelectionWeight; if (Pick<=0) { Choice=Name; Row=V; break; } }
        if (Batch==3) if (const auto* Fibre=Table->FindRow<FMCFoodRow>(TEXT("Fibre"),TEXT("Stuck food"))) { Choice=TEXT("Fibre"); Row=*Fibre; }
    }
    FTransform T(FRotator(0,Random.FRandRange(-180,180),0),Position);
    auto* Food=GetWorld()->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (Food)
    {
        Food->ConfigureItem(Choice,Row,Random); Food->Batch=Batch;
        FVector EntryVelocity=FVector::ZeroVector;
        if (bRandomDrop)
        {
            // The configured mesh, including the selected variant and scale, sets the footprint.
            const float Margin=Food->Body->GetScaledBoxExtent().Size2D()+20;
            FHitResult Floor; bool Found=false; AMCTongue* LandingTongue=nullptr;
            for (TActorIterator<AMCTongue> It(GetWorld());It;++It)
                if (It->RandomGameplaySpawnPoint(Random,Margin,Margin*2+40,TConstArrayView<FVector>(),Floor)) { Found=true; LandingTongue=*It; break; }
            if (!Found)
            {
                UE_LOG(LogTemp,Warning,TEXT("MC_FOOD_DROP no gameplay zone footprint for %s (radius %.1f)"),*Choice.ToString(),Margin);
                Food->Destroy(); return nullptr;
            }
            Position.X=Floor.ImpactPoint.X; Position.Y=Floor.ImpactPoint.Y;
            if (bHeightFromSurface) Position.Z+=Floor.ImpactPoint.Z;
            if(bMouthEntry)
            {
                const FVector LandingCenter=Floor.ImpactPoint+FVector(0,0,Food->Body->GetScaledBoxExtent().Z+5);
                if(!Settings->FoodEntry.BuildTrajectory(LandingTongue->Surface->Bounds.GetBox(),LandingCenter,GetWorld()->GetGravityZ(),Position,EntryVelocity))
                { Food->Destroy();return nullptr; }
            }
            T.SetLocation(Position);
        }
        UGameplayStatics::FinishSpawningActor(Food,T);
        if(bMouthEntry) Food->BeginMouthEntry(EntryVelocity,Settings->FoodEntry.PushSpeed);
    }
    return Food;
}
void AMCDayDirector::EnterStep()
{
    auto* GS=GetWorld()->GetGameState<AMCGameState>();
    // Preserve saved plan indices while bypassing the obsolete physical-tool objective.
    // Advancing here avoids awarding completion rewards for a step that is never played.
    while (Settings->Steps.IsValidIndex(GS->StepIndex) && Settings->Steps[GS->StepIndex].Step==EMCDayStep::DiscardBrushes)
        ++GS->StepIndex;
    if (!Settings->Steps.IsValidIndex(GS->StepIndex))
    {
        if (Flood) Flood->Stop();
        GS->bDayOneComplete=true; GS->Phase=EMCShiftPhase::Intermission; GS->PhaseEndsAt=0; GS->TasksLeft=0; GS->ForceNetUpdate(); return;
    }
    const auto& Step=Settings->Steps[GS->StepIndex]; StepStartedAt=GS->GetServerWorldTimeSeconds();
    GS->StepStartedAt=StepStartedAt;
    GS->PhaseEndsAt=!GS->bDevManualEvents && Step.Seconds>0?StepStartedAt+Step.Seconds:0; GS->TasksTotal=0; GS->TasksLeft=0;
    // A directly selected cleanup step needs the food normally left by its predecessor.
    if (GS->bDevManualEvents && Step.Step==EMCDayStep::BreakfastCleanup)
        for (int32 I=0;I<Settings->BreakfastCount;++I) SpawnMenuFoodDrop(650,2);
    if (Step.Step==EMCDayStep::BrushLesson) DirtyMouth(false);
    if (Step.Step==EMCDayStep::BreakfastRain) RainSpawned=0;
    if (Step.Step==EMCDayStep::CoffeeWaves && Flood)
    {
        Flood->Start(Settings);
        Settings->Steps[GS->StepIndex].Seconds=Flood->Seconds;
        GS->PhaseEndsAt=GS->bDevManualEvents?0:StepStartedAt+Flood->Seconds;
    }
    if (Step.Step==EMCDayStep::CoffeeCleanup) { if (Flood) Flood->Stop(); DirtyMouth(true); }
    if (Step.Step==EMCDayStep::ColdCola) { ColdCola=GetWorld()->SpawnActor<AMCColdColaEvent>(); ColdCola->Start(Settings); }
    if (Step.Step==EMCDayStep::StuckFood)
    {
        int32 Spawned=0;
        for (AMCArenaTooth* Tooth:GS->ArenaTeeth)
        {
            if (!IsValid(Tooth) || !Tooth->IsAvailable() || Spawned>=Settings->StuckCount) continue;
            FVector P=Tooth->GetActorLocation(); const float Side=FMath::Sign(P.Y);
            P.Y-=Side*(Tooth->Body->Bounds.BoxExtent.Y+22); P.Z=95;
            auto* Food=SpawnMenuFood(P,3);
            if (Food) { Food->Initialize(true,FVector(0,-Side,0)); Food->Phase=EMCFoodPhase::Stuck; Food->Body->SetSimulatePhysics(false); Food->StuckTooth=Tooth; Food->ForceNetUpdate(); ++Spawned; }
        }
    }
    GS->ForceNetUpdate(); UE_LOG(LogTemp,Display,TEXT("MC_DAY1_STEP %d %s"),GS->StepIndex,*Step.Title.ToString());
}
void AMCDayDirector::Next(bool bFailed)
{
    if (!HasAuthority() || !Settings) return;
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); if (!GS || GS->bDayOneComplete || !Settings->Steps.IsValidIndex(GS->StepIndex)) return;
    if (bFailed) { ++GS->FailedEvents; GS->MouthHealth=FMath::Max(0.f,GS->MouthHealth-Settings->Steps[GS->StepIndex].FailureDamage); }
    else if (Settings->Steps[GS->StepIndex].Step!=EMCDayStep::BreakfastRain
        && Settings->Steps[GS->StepIndex].Step!=EMCDayStep::Complete)
    {
        if (auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>())
            Mode->NotifyObjectiveCompleted(FName(*FString::Printf(TEXT("DayStep_%d_%d"),GS->Day,GS->StepIndex)));
    }
    GS->PreviousStepFailed=bFailed;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(It->Status->IsAlive()) It->NotifyTaskFeedback(!bFailed);
    if (Flood) Flood->Stop();
    if (ColdCola) { ColdCola->Stop(); ColdCola->Destroy(); ColdCola=nullptr; }
    ++GS->StepIndex; EnterStep();
}
void AMCDayDirector::Tick(float Dt)
{
    Super::Tick(Dt); if (!HasAuthority() || !Settings) return;
    auto* GS=GetWorld()->GetGameState<AMCGameState>();
    if (!GS || GS->Phase!=EMCShiftPhase::Working || GS->bDayOneComplete || !Settings->Steps.IsValidIndex(GS->StepIndex)) return;
    const auto Step=Settings->Steps[GS->StepIndex]; const double Elapsed=GS->GetServerWorldTimeSeconds()-StepStartedAt;
    int32 Left=0; bool bWait=false;
    switch (Step.Step)
    {
    case EMCDayStep::BrushLesson: case EMCDayStep::CoffeeCleanup:
        Left=CountDirt();
        break;
    case EMCDayStep::BreakfastRain:
        // The event owns the cadence. Each call introduces one independent piece,
        // without a catch-up burst after a slow frame.
        if (RainSpawned<Settings->BreakfastCount && Elapsed>=double(RainSpawned)*FMath::Max(.1f,Step.Seconds)/Settings->BreakfastCount)
        { SpawnMenuFoodEntry(2); ++RainSpawned; }
        Left=GS->bDevManualEvents?CountFood(2):Settings->BreakfastCount-RainSpawned; bWait=Elapsed<Step.Seconds;break;
    case EMCDayStep::BreakfastCleanup: Left=CountFood(2);break;
    case EMCDayStep::CoffeeWaves: bWait=Flood && Flood->IsActive(); Left=bWait?FMath::Max(1,Flood->Waves-Flood->Wave+1):0; break;
    case EMCDayStep::StuckFood: Left=CountFood(3); break;
    case EMCDayStep::ColdCola: Left=ColdCola?ColdCola->IceLeft():0; bWait=ColdCola && !ColdCola->IsComplete(); break;
    default: break;
    }
    GS->TasksLeft=Left; GS->TasksTotal=FMath::Max(GS->TasksTotal,Left);
    if (GS->bDevManualEvents) return;
    // A late frame may pass the deadline before every individual spawn ran.
    if (Step.Step==EMCDayStep::BreakfastRain && RainSpawned<Settings->BreakfastCount) return;
    // The profile owns the complete fill/drain duration; never cut the final drain short.
    if (Step.Step==EMCDayStep::CoffeeWaves) { if (!bWait) Next(false); return; }
    if (Left==0 && !bWait) Next(false);
    else if (Step.Seconds>0 && Elapsed>=Step.Seconds) Next(Step.Step!=EMCDayStep::BreakfastRain && Step.Step!=EMCDayStep::CoffeeWaves);
}
