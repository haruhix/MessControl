#include "CoreMinimal.h"
#if !UE_BUILD_SHIPPING
#include "MCFoodActor.h"
#include "MCFoodBodyComponent.h"
#include "MCFoodCollectionComponent.h"
#include "MCFoodStackSettings.h"
#include "MCGameState.h"
#include "MCThroat.h"
#include "MCThroatVortex.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "ProceduralMeshComponent.h"
#include "RenderTimer.h"
#include "UnrealClient.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

CSV_DEFINE_CATEGORY(MCFoodPile,true);

namespace
{
enum class EPileStage : uint8 { Baseline, FreePile, Pickup, Carry, Queued, Swallow, Recovery };
const TCHAR* StageName(EPileStage Stage)
{
    switch(Stage)
    {
    case EPileStage::Baseline:return TEXT("baseline");
    case EPileStage::FreePile:return TEXT("free_pile");
    case EPileStage::Pickup:return TEXT("pickup");
    case EPileStage::Carry:return TEXT("carry12");
    case EPileStage::Queued:return TEXT("queued12");
    case EPileStage::Swallow:return TEXT("swallow12");
    default:return TEXT("remaining_pile");
    }
}
struct FPileSamples
{
    TArray<float> Wall, GameThread, Harness;
};
struct FPileRun
{
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<AMCThroat> Throat;
    TWeakObjectPtr<AMCTongue> Tongue;
    TWeakObjectPtr<AMCToothCharacter> Carriers[2];
    TWeakObjectPtr<ACameraActor> Camera;
    TArray<TWeakObjectPtr<AMCFoodActor>> Food;
    TSet<TWeakObjectPtr<AMCFoodActor>> DisposedFood;
    FPileSamples Samples[7];
    TSharedFuture<FString> CSVCompletion;
    FString CSV=TEXT("segment,elapsed_s,wall_frame_ms,game_thread_busy_ms,world_dt_ms,harness_ms,simulating,held,queued,disposed,vortex_actors,compound_shapes\n");
    double Start=-1,StageAt=0,LastWall=0,FirstWindow=-1,SwallowAt=-1,WallStart=0;
    EPileStage Stage=EPileStage::Baseline;
    FVector Heap=FVector::ZeroVector,Zone=FVector::ZeroVector,Forward=FVector::ForwardVector,Right=FVector::RightVector;
    int32 Count=64,Photos=0;
    bool Invalid=false,Finished=false,Jolted=false,SecondEntered=false;

    void Fail(const FString& Reason)
    {
        if(!Invalid) UE_LOG(LogTemp,Error,TEXT("MC_FOOD_PILE_INVALID %s"),*Reason);
        Invalid=true;
    }
    void SetStage(EPileStage Next,double Now)
    {
        Stage=Next;StageAt=Now;
        CSV_EVENT(MCFoodPile,TEXT("segment=%s"),StageName(Stage));
        UE_LOG(LogTemp,Display,TEXT("MC_FOOD_PILE_SEGMENT %s at=%.3f"),StageName(Stage),Now-Start);
    }
    FVector OnFloor(FVector Point,float Height) const
    {
        FHitResult Hit;
        if(Tongue.IsValid() && Tongue->SurfacePoint(Point,Hit)) Point=Hit.ImpactPoint;
        return Point+FVector(0,0,Height);
    }
    void PlaceCarrier(int32 Index,FVector Point)
    {
        if(auto* Hero=Carriers[Index].Get())
        {
            Hero->GetCharacterMovement()->StopMovementImmediately();Hero->GetCharacterMovement()->DisableMovement();
            Point=OnFloor(Point,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+6);
            Hero->SetActorLocationAndRotation(Point,Forward.Rotation(),false,nullptr,ETeleportType::TeleportPhysics);
            Hero->ForceNetUpdate();
        }
    }
    void Photo(const TCHAR* Name,FVector Aim,FVector Offset)
    {
        if(!FParse::Param(FCommandLine::Get(),TEXT("MCFoodPileCapture")) || !Camera.IsValid()) return;
        const FVector Eye=Aim+Offset;
        Camera->SetActorLocationAndRotation(Eye,(Aim-Eye).Rotation());
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("FoodPileFrames");
        IFileManager::Get().MakeDirectory(*Folder,true);
        FScreenshotRequest::RequestScreenshot(Folder/Name,false,false);
    }
    bool SpawnPile(UWorld* InWorld)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(MCFoodPileSpawn);
        auto* Menu=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
        if(!Menu) {Fail(TEXT("Saved DT_BreakfastMenu is missing"));return false;}
        const FName Names[]={TEXT("Carrot"),TEXT("Egg")};
        const FMCFoodRow* SavedRows[]={Menu->FindRow<FMCFoodRow>(Names[0],TEXT("Food pile performance")),Menu->FindRow<FMCFoodRow>(Names[1],TEXT("Food pile performance"))};
        if(!SavedRows[0] || !SavedRows[1]) {Fail(TEXT("Current carrot and egg rows are required"));return false;}
        FMCFoodRow Rows[]={*SavedRows[0],*SavedRows[1]};
        // Some saved variants are deliberately too large for normal hand loads.
        // Sample only eligible existing variants; preserve menu scales and policy.
        for(int32 I=0;I<UE_ARRAY_COUNT(Rows);++I)
        {
            Rows[I].FragmentMeshes.Reset();
            for(const auto& Choice:SavedRows[I]->FragmentMeshes) if(auto* Mesh=Choice.LoadSynchronous())
            {
                const FVector Scale=SavedRows[I]->FragmentScale.GetAbs();
                const FVector Extent=Mesh->GetBounds().BoxExtent*Scale;
                const float SphereRadius=float(Mesh->GetBounds().SphereRadius*Scale.GetMax());
                if(Extent.GetMax()<=55 && SphereRadius<=85) Rows[I].FragmentMeshes.Add(Choice);
            }
            UE_LOG(LogTemp,Display,TEXT("MC_FOOD_PILE_ELIGIBLE row=%s variants=%d/%d"),*Names[I].ToString(),Rows[I].FragmentMeshes.Num(),SavedRows[I]->FragmentMeshes.Num());
            if(Rows[I].FragmentMeshes.IsEmpty()) {Fail(FString::Printf(TEXT("Saved row %s has no variants eligible for normal pickup"),*Names[I].ToString()));return false;}
        }
        FVector MaxExtent=FVector(1);
        for(const auto& Row:Rows) for(const auto& Choice:Row.FragmentMeshes) if(auto* Mesh=Choice.LoadSynchronous())
        {
            const FVector Extent=Mesh->GetBounds().BoxExtent*Row.FragmentScale.GetAbs();
            const FVector Flat=FMCFoodStackSettings::RotatedExtent(Extent,Row.Stack.RestRotation(Mesh,Extent,0));
            MaxExtent.X=FMath::Max(MaxExtent.X,Flat.X);MaxExtent.Y=FMath::Max(MaxExtent.Y,Flat.Y);MaxExtent.Z=FMath::Max(MaxExtent.Z,Flat.Z);
        }
        const int32 Across=FMath::CeilToInt(FMath::Sqrt(float(FMath::Min(Count,16))));
        const int32 PerLayer=Across*Across;
        const float StepX=MaxExtent.X*2+7,StepY=MaxExtent.Y*2+7,StepZ=MaxExtent.Z*2+6;
        FRandomStream Random(103);
        for(int32 I=0;I<Count;++I)
        {
            FMCFoodRow Row=Rows[I%2];Row.SpoilSeconds=300;
            FVector Position=Heap+Forward*((I%Across-(Across-1)*.5f)*StepX)
                +Right*((I/Across%Across-(Across-1)*.5f)*StepY);
            Position=OnFloor(Position,MaxExtent.Z+4+(I/PerLayer)*StepZ);
            const FTransform Initial(Position);
            auto* F=InWorld->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Initial,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(!F) {Fail(TEXT("A fragment failed to spawn"));return false;}
            F->ConfigureItem(Names[I%2],Row,Random,true);F->Phase=EMCFoodPhase::Free;F->Batch=61003;
            F->PrepareHorizontalStackPose(&F->FoodData.Stack,0);
            const FQuat Rotation=F->StackRestRotation(Forward.Rotation().Quaternion());
            F->FinishSpawning(FTransform(Rotation,Position));
            F->Label->SetVisibility(false);Food.Add(F);
            if(F->Visual->Bounds.SphereRadius>85 || F->Body->GetScaledBoxExtent().GetMax()>55)
                Fail(FString::Printf(TEXT("Saved fragment is too large for normal pickup: %s radius=%.1f extent=%.1f"),*F->ItemMesh->GetPathName(),F->Visual->Bounds.SphereRadius,F->Body->GetScaledBoxExtent().GetMax()));
        }
        // Four low layers are separated before the first physics step. The mesh
        // scales/collision/mass are the actual menu values, without overlap spikes.
        PlaceCarrier(0,Heap-Forward*(StepX*(Across-1)*.5f+80)-Right*75);
        PlaceCarrier(1,Heap-Forward*(StepX*(Across-1)*.5f+80)+Right*75);
        UE_LOG(LogTemp,Display,TEXT("MC_FOOD_PILE_SPAWN count=%d cell=%s layers=%d"),Count,*FVector(StepX,StepY,StepZ).ToString(),FMath::DivideAndRoundUp(Count,PerLayer));
        return !Invalid;
    }
    void Finish()
    {
        // A process exit can discard the CSV writer's buffered physics frames.
        if(CSVCompletion.IsValid()) {if(!CSVCompletion.IsReady()) return;}
        else if(FCsvProfiler::IsCapturing()) {CSVCompletion=FCsvProfiler::Get()->EndCapture();return;}
        Finished=true;
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("Profiling");IFileManager::Get().MakeDirectory(*Folder,true);
        FFileHelper::SaveStringToFile(CSV,*(Folder/TEXT("FoodPilePerformance.csv")));
        FString Summary=TEXT("segment,metric,samples,mean_ms,p95_ms\n");
        auto Describe=[&](const TCHAR* Segment,const TCHAR* Metric,TArray<float>& Values)
        {
            if(Values.IsEmpty()) return;
            Values.Sort();double Total=0;for(float Value:Values) Total+=Value;
            const double Mean=Total/Values.Num();const float P95=Values[FMath::Min(Values.Num()-1,FMath::FloorToInt(Values.Num()*.95f))];
            Summary+=FString::Printf(TEXT("%s,%s,%d,%.4f,%.4f\n"),Segment,Metric,Values.Num(),Mean,P95);
            UE_LOG(LogTemp,Display,TEXT("MC_FOOD_PILE_PERF segment=%s metric=%s frames=%d mean_ms=%.3f p95_ms=%.3f"),Segment,Metric,Values.Num(),Mean,P95);
        };
        for(int32 I=0;I<7;++I)
        {
            Describe(StageName(EPileStage(I)),TEXT("wall_pacing_includes_idle"),Samples[I].Wall);
            Describe(StageName(EPileStage(I)),TEXT("engine_game_thread_busy"),Samples[I].GameThread);
            Describe(StageName(EPileStage(I)),TEXT("harness_work"),Samples[I].Harness);
        }
        FFileHelper::SaveStringToFile(Summary,*(Folder/TEXT("FoodPilePerformanceSummary.csv")));
        UE_LOG(LogTemp,Display,TEXT("MC_FOOD_PILE_%s spawned=%d swallowed=%d leftovers=%d timings_are_diagnostics_no_fps_threshold"),Invalid?TEXT("FAIL"):TEXT("PASS"),Food.Num(),Throat.IsValid()?Throat->FoodSwallowed:0,Count-12);
        FPlatformMisc::RequestExitWithStatus(false,Invalid?1:0);
    }
};
}

/** Opt-in real-map load fixture. Called only by the development validation hook. */
void MCTickFoodPilePerformance(UWorld* World)
{
    if(!World || World->GetNetMode()==NM_Client) return;
    TRACE_CPUPROFILER_EVENT_SCOPE(MCFoodPileFixture);
    const double WorkStart=FPlatformTime::Seconds();
    static FPileRun Run;
    if(Run.World.Get()!=World) {Run=FPileRun();Run.World=World;Run.WallStart=WorkStart;}
    if(Run.Finished) return;
    auto* State=World->GetGameState<AMCGameState>();auto* Controller=World->GetFirstPlayerController();
    if(State) {State->bDevManualEvents=true;State->Phase=EMCShiftPhase::Intermission;State->PhaseEndsAt=State->GetServerWorldTimeSeconds()+300;}
    const double Now=State?State->GetServerWorldTimeSeconds():World->GetTimeSeconds();
    if(Run.Start<0)
    {
        if(WorkStart-Run.WallStart>60) {Run.Fail(TEXT("Real mouth map did not become ready"));Run.Finish();return;}
        if(!State || !Controller || !Controller->GetPawn() || World->GetTimeSeconds()<2) return;
#if WITH_EDITOR
        if(GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
        for(TActorIterator<AMCThroat> It(World);It;++It) {Run.Throat=*It;break;}
        for(TActorIterator<AMCTongue> It(World);It;++It) {Run.Tongue=*It;break;}
        if(!Run.Throat.IsValid() || !Run.Tongue.IsValid()) return;
        auto* First=Cast<AMCToothCharacter>(Controller->GetPawn());if(!First) return;
        Run.Carriers[0]=First;
        for(TActorIterator<AMCToothCharacter> It(World);It;++It) if(*It!=First && It->CanWork()) {Run.Carriers[1]=*It;break;}
        if(!Run.Carriers[1].IsValid()) Run.Carriers[1]=World->SpawnActor<AMCToothCharacter>(First->GetActorLocation()+FVector(0,200,30),First->GetActorRotation());
        if(!Run.Carriers[1].IsValid()) {Run.Fail(TEXT("Second production carrier is missing"));Run.Finish();return;}
        FParse::Value(FCommandLine::Get(),TEXT("MCFoodPileCount="),Run.Count);Run.Count=FMath::Clamp(Run.Count,12,256);
        Run.Throat->ResetSwallow();Run.Throat->FoodSwallowed=Run.Throat->SwallowCount=Run.Throat->SpasmCount=Run.Throat->VomitCount=0;
        Run.Tongue->bAutomaticYawns=false;Run.Tongue->ResetYawn();Run.Tongue->ResetPain();
        for(TActorIterator<AMCFoodActor> It(World);It;++It) It->Dispose();
        Run.Forward=Run.Throat->GetActorForwardVector();Run.Right=Run.Throat->GetActorRightVector();
        Run.Zone=Run.Throat->GetActorTransform().TransformPosition(Run.Throat->ZoneCenter);
        Run.Heap=Run.Zone-Run.Forward*(Run.Throat->ZoneRadius+450);
        for(int32 I=0;I<2;++I) {Run.Carriers[I]->FoodCollection->Stop();Run.PlaceCarrier(I,Run.Heap-Run.Forward*320+Run.Right*(I?140:-140));}
        if(FParse::Param(FCommandLine::Get(),TEXT("MCFoodPileCapture")))
        {
            Run.Camera=World->SpawnActor<ACameraActor>();Run.Camera->GetCameraComponent()->SetFieldOfView(65);
            Run.Camera->GetCameraComponent()->SetAspectRatio(1.6f);Controller->SetViewTarget(Run.Camera.Get());
            const FVector Aim=Run.Heap+FVector(0,0,70),Eye=Aim-Run.Forward*700-Run.Right*450+FVector(0,0,430);
            Run.Camera->SetActorLocationAndRotation(Eye,(Aim-Eye).Rotation());
        }
        Run.Start=Now;Run.LastWall=WorkStart;Run.SetStage(EPileStage::Baseline,Now);
        UE_LOG(LogTemp,Display,TEXT("MC_FOOD_PILE_BEGIN count=%d capture=%d GT_time_excludes_idle wall_time_includes_cap_and_vsync"),Run.Count,Run.Camera.IsValid());
    }
    if(Now-Run.Start>28) {Run.Fail(TEXT("Fixture did not complete its intake cycle in 28 simulation seconds"));Run.Finish();return;}
    const double StageAge=Now-Run.StageAt;
    auto* Throat=Run.Throat.Get();
    if(Run.Stage==EPileStage::Baseline && StageAge>=2)
    {
        if(!Run.SpawnPile(World)) {Run.Finish();return;}
        Run.SetStage(EPileStage::FreePile,Now);
    }
    else if(Run.Stage==EPileStage::FreePile)
    {
        if(StageAge>1.2 && !Run.Jolted)
        {
            for(int32 I=0;I<Run.Food.Num();++I) if(auto* F=Run.Food[I].Get())
                F->Body->AddImpulse(Run.Right*((I%2?1:-1)*15)+FVector(0,0,24),NAME_None,true);
            Run.Jolted=true;
        }
        if(StageAge>2.2 && !(Run.Photos&1)) {Run.Photo(TEXT("00_FreePile.png"),Run.Heap+FVector(0,0,60),-Run.Forward*620-Run.Right*380+FVector(0,0,360));Run.Photos|=1;}
        if(StageAge>=4)
        {
            for(int32 I=0;I<2;++I) {if(Run.Carriers[I]->FoodCollection->MaxPieces!=6) Run.Fail(TEXT("Fixture requires the normal six-piece carry limit"));Run.Carriers[I]->FoodCollection->Toggle();}
            Run.SetStage(EPileStage::Pickup,Now);
        }
    }
    else if(Run.Stage==EPileStage::Pickup)
    {
        bool Ready=true;
        for(int32 I=0;I<2;++I)
        {
            const auto* Collection=Run.Carriers[I]->FoodCollection.Get();Ready&=Collection->Pieces.Num()==6;
            for(const auto& F:Collection->Pieces) Ready&=IsValid(F) && !F->IsStackPickupActive();
        }
        if(Ready) Run.SetStage(EPileStage::Carry,Now);
        else if(StageAge>6) {Run.Fail(FString::Printf(TEXT("Actual pile pickup stalled: carrier loads %d and %d"),Run.Carriers[0]->FoodCollection->Pieces.Num(),Run.Carriers[1]->FoodCollection->Pieces.Num()));Run.Finish();return;}
    }
    else if(Run.Stage==EPileStage::Carry)
    {
        if(StageAge>.25 && !(Run.Photos&2))
        {
            const FVector Aim=(Run.Carriers[0]->FoodCollection->HandPoint()+Run.Carriers[1]->FoodCollection->HandPoint())*.5f+FVector(0,0,45);
            Run.Photo(TEXT("01_TwoHorizontalStacks.png"),Aim,-Run.Forward*400-Run.Right*240+FVector(0,0,200));Run.Photos|=2;
        }
        if(StageAge>=1.2) {Run.PlaceCarrier(0,Run.Zone-Run.Forward*65-Run.Right*115);Run.SetStage(EPileStage::Queued,Now);}
    }
    else if(Run.Stage==EPileStage::Queued)
    {
        if(Throat->ThroatPhase==EMCThroatPhase::Anticipation && Run.FirstWindow<0) Run.FirstWindow=Throat->PhaseStartedAt;
        if(Run.FirstWindow>=0 && Throat->ThroatPhase==EMCThroatPhase::Anticipation && Throat->PhaseStartedAt!=Run.FirstWindow) Run.Fail(TEXT("Joining teammate extended the gathering window"));
        if(StageAge>1.1 && !Run.SecondEntered) {Run.PlaceCarrier(1,Run.Zone-Run.Forward*65+Run.Right*115);Run.SecondEntered=true;}
        if(StageAge>1.65 && Throat->ThroatPhase==EMCThroatPhase::Anticipation && (Throat->QueuedFoodCount!=12 || !Run.Carriers[0]->FoodCollection->Pieces.IsEmpty() || !Run.Carriers[1]->FoodCollection->Pieces.IsEmpty()))
            Run.Fail(TEXT("Two entering carriers did not hand over exactly 12 ingredients"));
        if(StageAge>1.9 && !(Run.Photos&4)) {Run.Photo(TEXT("02_DeliveredHorizontalStacks.png"),Run.Zone+FVector(0,0,60),-Run.Forward*560-Run.Right*400+FVector(0,0,330));Run.Photos|=4;}
        if(Throat->ThroatPhase==EMCThroatPhase::Swallowing)
        {
            if(Run.FirstWindow<0 || Throat->PhaseStartedAt-Run.FirstWindow<2.98 || Throat->SwallowCount!=1 || Throat->FoodSwallowed!=0) Run.Fail(TEXT("The fixed three-second intake deadline was violated"));
            Run.SwallowAt=Throat->PhaseStartedAt;
            Run.SetStage(EPileStage::Swallow,Now);
            if(Run.Camera.IsValid())
            {
                const FVector Aim=Run.Tongue->Surface->Bounds.Origin+FVector(0,0,350);
                const FVector Eye=Aim-Run.Forward*2100-Run.Right*130+FVector(0,0,360);
                Run.Camera->SetActorLocationAndRotation(Eye,(Aim-Eye).Rotation());
            }
        }
    }
    else if(Run.Stage==EPileStage::Swallow)
    {
        // Recovery changes the actor's phase clock in the same world tick as
        // disposal. Measure against the captured swallow clock throughout.
        const float SwallowAge=float(Now-Run.SwallowAt);
        // Show the entire mouth-wide flow, from the front of the tongue to the inlet.
        const FVector Aim=Run.Tongue.IsValid()?Run.Tongue->Surface->Bounds.Origin+FVector(0,0,350):Run.Zone;
        const FVector Eye=-Run.Forward*2100-Run.Right*130+FVector(0,0,360);
        if(SwallowAge>.45f && !(Run.Photos&8)) {Run.Photo(TEXT("03_VortexStart.png"),Aim,Eye);Run.Photos|=8;}
        if(SwallowAge>.95f && !(Run.Photos&16)) {Run.Photo(TEXT("04_VortexPeak.png"),Aim,Eye);Run.Photos|=16;}
        if(SwallowAge>1.55f && !(Run.Photos&32)) {Run.Photo(TEXT("05_VortexFinish.png"),Aim,Eye);Run.Photos|=32;}
        if(Throat->FoodSwallowed==12)
        {
            if(SwallowAge<1.98f) Run.Fail(TEXT("Food disappeared before the full two-second swallow"));
            Run.SetStage(EPileStage::Recovery,Now);
        }
    }
    int32 Simulating=0,Held=0,Disposed=0,Shapes=0,Vortices=0;
    for(const auto& Ref:Run.Food)
    {
        auto* F=Ref.Get();if(!F) {
            if(Run.DisposedFood.Contains(Ref)) ++Disposed;
            else Run.Fail(TEXT("A fixture fragment disappeared without the disposal phase"));
            continue;
        }
        if(F->GetActorLocation().ContainsNaN() || F->GetActorQuat().ContainsNaN() || F->Body->GetPhysicsLinearVelocity().ContainsNaN()) Run.Fail(TEXT("A food pose or velocity became non-finite"));
        if(F->IsDisposed()) {Run.DisposedFood.Add(Ref);++Disposed;continue;}
        if(F->Body->IsSimulatingPhysics()) ++Simulating;
        if(F->StackCarrier) ++Held;
        if(F->Visual->GetCollisionEnabled()!=ECollisionEnabled::NoCollision || (F->StackCarrier && F->Body->IsSimulatingPhysics())) Run.Fail(TEXT("A carried item simulates or its visual mesh collides"));
        if(F->Phase==EMCFoodPhase::Swallowing && (F->Body->IsSimulatingPhysics() || F->Body->GetCollisionEnabled()!=ECollisionEnabled::NoCollision)) Run.Fail(TEXT("Reserved/swallowed food re-entered the physics heap"));
        if((F->StackCarrier && !F->IsStackPickupActive()) || (F->Phase==EMCFoodPhase::Swallowing && Throat->ThroatPhase==EMCThroatPhase::Anticipation))
        {
            const FVector Vertical=F->FoodData.Stack.VerticalAxis(F->ItemMesh,F->Body->GetScaledBoxExtent());
            if(F->GetActorQuat().RotateVector(Vertical).Z<.95) Run.Fail(TEXT("A settled carry/delivery stack is not horizontal"));
        }
        if(auto* Setup=F->Body->GetBodySetup()) Shapes+=Setup->AggGeom.GetElementCount();
    }
    for(TActorIterator<AMCThroatVortex> It(World);It;++It)
    {
        ++Vortices;
        if(It->FoodSources.Num()>24) Run.Fail(TEXT("The shared vortex exceeded its bounded wake budget"));
        if(Run.Tongue.IsValid())
        {
            const FBox Bounds=Run.Tongue->Surface->Bounds.GetBox();
            for(int32 Corner=0;Corner<8;++Corner)
            {
                const FVector Point(Corner&1?Bounds.Max.X:Bounds.Min.X,
                    Corner&2?Bounds.Max.Y:Bounds.Min.Y,Corner&4?Bounds.Max.Z:Bounds.Min.Z);
                const FVector Offset=Point-It->GetActorLocation();
                if(-FVector::DotProduct(Offset,It->InwardDirection)>It->FlowLength+1.f)
                    Run.Fail(TEXT("Suction field did not reach the front of the tongue"));
                const FVector Side=FVector::CrossProduct(FVector::UpVector,It->InwardDirection);
                if(FMath::Abs(FVector::DotProduct(Offset,Side))>It->FlowHalfWidth+1.f)
                    Run.Fail(TEXT("Suction field did not cover the tongue width"));
            }
        }
    }
    if(Vortices>1 || Held>12 || Throat->QueuedFoodCount>12 || Throat->FoodSwallowed>12 || Throat->SwallowCount>1 || Throat->SpasmCount || Throat->VomitCount) Run.Fail(TEXT("The fixture exceeded its actor/load/batch counts"));
    const float WallMs=float((WorkStart-Run.LastWall)*1000),GTMs=float(FPlatformTime::ToMilliseconds(GGameThreadTime));Run.LastWall=WorkStart;
    const float HarnessMs=float((FPlatformTime::Seconds()-WorkStart)*1000);
    CSV_CUSTOM_STAT(MCFoodPile,Simulating,Simulating,ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(MCFoodPile,Held,Held,ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(MCFoodPile,Queued,Throat->QueuedFoodCount,ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(MCFoodPile,Vortices,Vortices,ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(MCFoodPile,CompoundShapes,Shapes,ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(MCFoodPile,HarnessMs,HarnessMs,ECsvCustomStatOp::Set);
    if(Now-Run.StageAt>.15)
    {
        auto& Samples=Run.Samples[int32(Run.Stage)];Samples.Wall.Add(WallMs);Samples.Harness.Add(HarnessMs);if(GTMs>0) Samples.GameThread.Add(GTMs);
        Run.CSV+=FString::Printf(TEXT("%s,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%d,%d,%d,%d,%d\n"),StageName(Run.Stage),Now-Run.Start,WallMs,GTMs,World->GetDeltaSeconds()*1000,HarnessMs,Simulating,Held,Throat->QueuedFoodCount,Disposed,Vortices,Shapes);
    }
    if(Run.Stage==EPileStage::Recovery && Now-Run.StageAt>2)
    {
        if(Disposed!=12 || Simulating!=Run.Count-12 || Throat->FoodSwallowed!=12 || Throat->ThroatPhase!=EMCThroatPhase::Collecting || Vortices!=0) Run.Fail(TEXT("The final 12-piece batch or remaining physical heap did not settle correctly"));
        Run.Finish();
    }
}
#endif
