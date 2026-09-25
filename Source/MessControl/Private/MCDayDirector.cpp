#include "MCDayDirector.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCFoodActor.h"
#include "MCArenaTooth.h"
#include "MCMouthSurface.h"
#include "MCCoffeeFlood.h"
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
    GS->DayStartedAt=GS->GetServerWorldTimeSeconds(); GS->bPhysicalBrushes=true; GS->CurrentEvent=nullptr;
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
        if (const auto* Patch=Cast<AMCMouthSurface>(*It); Patch && Patch->bUlcer) continue;
        if (const auto* Tooth=Cast<AMCArenaTooth>(*It); Tooth && !Tooth->IsAvailable()) continue;
        if (const auto* Status=It->FindComponentByClass<UMCToothStatusComponent>(); Status && Status->NeedsCare(true)) ++Count;
    }
    return Count;
}
int32 AMCDayDirector::CountFood(int32 Batch) const
{
    int32 Count=0; for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if (!It->bBrushTool && !It->IsDisposed() && It->Batch==Batch) ++Count;
    return Count;
}
void AMCDayDirector::DropBrushes()
{
    auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const int32 Count=FMath::Max(1,GS->PlayerArray.Num());
    for (int32 I=0;I<Count;++I)
    {
        const FTransform T(FVector(-700+I*110,-220+I*140,500+I*40));
        auto* Brush=GetWorld()->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),T);
        if (Brush) { Brush->ConfigureBrush(); UGameplayStatics::FinishSpawningActor(Brush,T); }
    }
}
void AMCDayDirector::DirtyMouth(bool bCoffee)
{
    auto* GS=GetWorld()->GetGameState<AMCGameState>();
    for (AMCArenaTooth* Tooth:GS->ArenaTeeth) if (IsValid(Tooth) && Tooth->IsAvailable()) Tooth->Status->ApplyCoffee(bCoffee?1.f:.5f);
    if (bCoffee) for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if (It->Status->IsAlive()) It->Status->ApplyCoffee();
    TArray<AMCMouthSurface*> Existing; for (TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if (!It->bUlcer) Existing.Add(*It);
    for (auto* Patch:Existing) Patch->Destroy();
    for (int32 I=0;I<Settings->SurfacePatches;++I)
    {
        const FVector P(-700+(I%5)*330,-340+(I/5)*260,350);
        FHitResult Floor; FCollisionQueryParams Params(SCENE_QUERY_STAT(MCDirtFloor));
        const FVector Location=GetWorld()->LineTraceSingleByChannel(Floor,P,P-FVector(0,0,600),ECC_WorldStatic,Params)?Floor.ImpactPoint+FVector(0,0,5):FVector(P.X,P.Y,5);
        const FRotator Rotation=Floor.bBlockingHit?FRotationMatrix::MakeFromZ(Floor.ImpactNormal).Rotator():FRotator::ZeroRotator;
        auto* Patch=GetWorld()->SpawnActor<AMCMouthSurface>(Location,Rotation);
        if (Patch) { Patch->Status->ApplyCoffee(bCoffee?1.f:.5f); Patch->Batch=GS->StepIndex; }
    }
}
AMCFoodActor* AMCDayDirector::SpawnMenuFood(FVector Position,int32 Batch)
{
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
    const FTransform T(FRotator(0,Random.FRandRange(-180,180),0),Position);
    auto* Food=GetWorld()->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (Food) { Food->ConfigureItem(Choice,Row,Random); Food->Batch=Batch; UGameplayStatics::FinishSpawningActor(Food,T); }
    return Food;
}
void AMCDayDirector::EnterStep()
{
    auto* GS=GetWorld()->GetGameState<AMCGameState>();
    if (!Settings->Steps.IsValidIndex(GS->StepIndex))
    {
        if (Flood) Flood->Stop();
        GS->bDayOneComplete=true; GS->Phase=EMCShiftPhase::Intermission; GS->PhaseEndsAt=0; GS->TasksLeft=0; GS->ForceNetUpdate(); return;
    }
    const auto& Step=Settings->Steps[GS->StepIndex]; StepStartedAt=GS->GetServerWorldTimeSeconds();
    GS->PhaseEndsAt=!GS->bDevManualEvents && Step.Seconds>0?StepStartedAt+Step.Seconds:0; GS->TasksTotal=0; GS->TasksLeft=0;
    // A directly selected cleanup/discard step needs the objects normally left by its predecessor.
    if (GS->bDevManualEvents && Step.Step==EMCDayStep::DiscardBrushes) DropBrushes();
    if (GS->bDevManualEvents && Step.Step==EMCDayStep::BreakfastCleanup)
        for (int32 I=0;I<Settings->BreakfastCount;++I) SpawnMenuFood(FVector(Random.FRandRange(-620,650),Random.FRandRange(-430,430),650),2);
    if (Step.Step==EMCDayStep::BrushLesson) { DirtyMouth(false); DropBrushes(); }
    if (Step.Step==EMCDayStep::BreakfastRain) RainSpawned=0;
    if (Step.Step==EMCDayStep::CoffeeWaves && Flood)
    {
        Flood->Start(Settings);
        Settings->Steps[GS->StepIndex].Seconds=Flood->Seconds;
        GS->PhaseEndsAt=GS->bDevManualEvents?0:StepStartedAt+Flood->Seconds;
    }
    if (Step.Step==EMCDayStep::CoffeeCleanup) { if (Flood) Flood->Stop(); DirtyMouth(true); DropBrushes(); }
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
    if (Flood) Flood->Stop();
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
        if (Left>0)
        {
            bool BrushExists=false; for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if (It->bBrushTool && !It->IsDisposed()) BrushExists=true;
            if (!BrushExists) DropBrushes();
        }
        break;
    case EMCDayStep::DiscardBrushes:
        for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if (It->bBrushTool && !It->IsDisposed()) ++Left;
        break;
    case EMCDayStep::BreakfastRain:
        while (RainSpawned<Settings->BreakfastCount && Elapsed>=double(RainSpawned)*FMath::Max(.1f,Step.Seconds)/Settings->BreakfastCount)
        { SpawnMenuFood(FVector(Random.FRandRange(-620,650),Random.FRandRange(-430,430),650),2); ++RainSpawned; }
        Left=GS->bDevManualEvents?CountFood(2):Settings->BreakfastCount-RainSpawned; bWait=Elapsed<Step.Seconds; break;
    case EMCDayStep::BreakfastCleanup: Left=CountFood(2); break;
    case EMCDayStep::CoffeeWaves: bWait=Flood && Flood->IsActive(); Left=bWait?FMath::Max(1,Flood->Waves-Flood->Wave+1):0; break;
    case EMCDayStep::StuckFood: Left=CountFood(3); break;
    default: break;
    }
    GS->TasksLeft=Left; GS->TasksTotal=FMath::Max(GS->TasksTotal,Left);
    if (GS->bDevManualEvents) return;
    // The profile owns the complete fill/drain duration; never cut the final drain short.
    if (Step.Step==EMCDayStep::CoffeeWaves) { if (!bWait) Next(false); return; }
    if (Left==0 && !bWait) Next(false);
    else if (Step.Seconds>0 && Elapsed>=Step.Seconds) Next(Step.Step!=EMCDayStep::BreakfastRain && Step.Step!=EMCDayStep::CoffeeWaves);
}
