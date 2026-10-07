#if !UE_BUILD_SHIPPING
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCDayDirector.h"
#include "MCColdCola.h"
#include "MCCoffeeFlood.h"
#include "MCLocomotionSurface.h"
#include "MCFoodActor.h"
#include "MCFirePatch.h"
#include "MCMouthSurface.h"
#include "MCHazardWave.h"
#include "MCThroat.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCInventoryComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "NiagaraComponent.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

// Runs in the saved L_Mouth arena with the actual possessed pawn. It tests two
// full RestartShift calls, not the separate question of an automatic Day 2.
// -MCShiftResetTest -MCVideo=ShiftReset records only the game render target.
void MCTickShiftResetValidation(UWorld* World)
{
    struct FRun
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<ACameraActor> Camera;
        TWeakObjectPtr<AMCColdColaEvent> Cola;
        TWeakObjectPtr<AMCFoodActor> Food,Pepper;
        TWeakObjectPtr<AMCMouthSurface> Ulcer;
        TWeakObjectPtr<AMCHazardWave> Wave;
        TWeakObjectPtr<AMCThroat> Throat;
        TWeakObjectPtr<AMCToothCharacter> OldPawn;
        TWeakObjectPtr<AMCDayDirector> FirstDirector;
        TArray<TWeakObjectPtr<AActor>> OldActors;
        double StageAt=0,ResetAt=0;
        float Age=0,SwimSeconds=0,PeakFrost=0,ExpectedPlayerHealth=0,ExpectedMouthHealth=0;
        int32 Stage=-1,ResetNumber=0,Checks=0,VomitBaseline=0,RainIndex=INDEX_NONE;
        bool Failed=false,Absorbing=false,ToolWasHidden=false,ToolChecked[2]={false,false};
        FString Report;
    };
    static FRun R;if(R.World!=World) {R=FRun();R.World=World;}
    R.Age+=World->GetDeltaSeconds();
    auto* GS=World->GetGameState<AMCGameState>();
    auto* Mode=World->GetAuthGameMode<AMCGameMode>();
    auto* PC=World->GetFirstPlayerController();
    auto* Hero=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    auto Check=[&](bool OK,const TCHAR* Message) {
        ++R.Checks;R.Failed|=!OK;
        const FString Line=FString::Printf(TEXT("MC_SHIFT_RESET_CHECK %s %s\n"),OK?TEXT("PASS"):TEXT("FAIL"),Message);
        R.Report+=Line;UE_LOG(LogTemp,Display,TEXT("%s"),*Line.TrimEnd());
    };
    auto Finish=[&]() {
        const FString Result=FString::Printf(TEXT("MC_VALIDATION_%s SHIFT_RESET resets=%d checks=%d frost=%.3f swimming=%.2f"),R.Failed?TEXT("FAIL"):TEXT("PASS"),R.ResetNumber,R.Checks,R.PeakFrost,R.SwimSeconds);
        UE_LOG(LogTemp,Display,TEXT("%s"),*Result);
        FFileHelper::SaveStringToFile(R.Report+Result+TEXT("\n"),*(FPaths::ProjectSavedDir()/TEXT("ShiftReset_Validation.txt")));
        FPlatformMisc::RequestExitWithStatus(false,R.Failed?1:0);
    };
    if(R.Age>85) {Check(false,TEXT("runtime reset scenario timed out"));Finish();return;}
    if(!GS || !Mode || !PC || !Hero || R.Age<4) return;
#if WITH_EDITOR
    if(GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
    AMCTongue* Tongue=nullptr;for(TActorIterator<AMCTongue> It(World);It;++It) {Tongue=*It;break;}
    if(!Tongue) {Check(false,TEXT("saved arena has a tongue floor"));Finish();return;}
    const double Now=GS->GetServerWorldTimeSeconds(),Elapsed=Now-R.StageAt;
    auto* Move=CastChecked<UMCToothMovementComponent>(Hero->GetCharacterMovement());
    auto* Climate=LoadObject<UMaterialParameterCollection>(nullptr,TEXT("/Game/Gameplay/Cold/MPC_MouthClimate.MPC_MouthClimate"));
    float Cold=-1;if(Climate) World->GetParameterCollectionInstance(Climate)->GetScalarParameterValue(TEXT("ColdAmount"),Cold);
    auto InventoryTool=[&]() {
        TArray<UStaticMeshComponent*> Components;Hero->GetComponents(Components);
        for(auto* Component:Components) if(Component->GetFName()==TEXT("InventoryTool")) return Component;
        return static_cast<UStaticMeshComponent*>(nullptr);
    };
    auto View=[&](FVector Aim,FVector Offset) {
        if(!R.Camera.IsValid()) {R.Camera=World->SpawnActor<ACameraActor>();R.Camera->GetCameraComponent()->SetFieldOfView(55);}
        R.Camera->SetActorLocationAndRotation(Aim+Offset,(-Offset).Rotation());PC->SetViewTarget(R.Camera.Get());
    };
    auto PlaceOnFloor=[&](FVector Probe,FRotator Yaw) {
        FHitResult Floor;if(!Tongue->SurfacePoint(Probe,Floor)) return false;
        Hero->CancelGameplayInput();Move->StopMovementImmediately();
        Hero->SetActorLocationAndRotation(Floor.ImpactPoint+FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),Yaw,false,nullptr,ETeleportType::TeleportPhysics);
        Move->SetMovementMode(MOVE_Falling);Hero->ForceNetUpdate();return true;
    };
    auto BeginReset=[&]() {
        R.OldActors.Reset();R.OldPawn=Hero;R.OldActors.Add(Hero);
        for(TActorIterator<AActor> It(World);It;++It)
            if(Cast<AMCDayDirector>(*It) || Cast<AMCCoffeeFlood>(*It) || Cast<AMCColdColaEvent>(*It)
                || Cast<AMCIceBlock>(*It) || Cast<AMCMouthSurface>(*It) || Cast<AMCHazardWave>(*It)
                || Cast<AMCFoodActor>(*It) || It->ActorHasTag(TEXT("DayOne"))) R.OldActors.Add(*It);
        R.VomitBaseline=R.Throat.IsValid()?R.Throat->VomitCount:0;
        Mode->bUseAdaptiveDirector=false;Mode->bUseDayOnePlan=true;Mode->RestartShift();Mode->SetActorTickEnabled(true);
        R.ResetAt=R.StageAt=Now;++R.ResetNumber;
        if(auto* Fresh=Cast<AMCToothCharacter>(PC->GetPawn())) R.ExpectedPlayerHealth=Fresh->Status->State.Health;
        R.ExpectedMouthHealth=GS->MouthHealth;
        Check(GS->Day==0 && GS->Phase==EMCShiftPhase::Intermission && GS->PhaseEndsAt>=Now+7.9 && GS->PhaseEndsAt<=Now+8.1,TEXT("restart installs a fresh eight-second intermission"));
        Check(!GS->bDevManualEvents && !GS->bDayOneComplete && GS->TasksLeft==0 && GS->TasksTotal==0,TEXT("restart clears old timeline and objectives"));
    };
    auto CheckSettledReset=[&]() {
        bool OldGone=true;for(const auto& Entry:R.OldActors) OldGone&=!Entry.IsValid();
        Check(OldGone && Hero!=R.OldPawn.Get(),TEXT("old actors and possessed pawn are gone after real world ticks"));
        Check(Move->IsMovingOnGround() && Move->GroundSurface==EMCGroundSurface::Normal && Hero->ToothPhysics->CanAct(),TEXT("fresh player walks on normal ground with control"));
        Check(!Hero->bInCoffee && !Hero->ClingTooth && Hero->AnimationSwim<.02f && Hero->AnimationClimb<.02f,TEXT("swimming, anchoring and traversal pose do not survive reset"));
        Check(FMath::IsNearlyZero(Cold,.001f),TEXT("previously nonzero climate frost resets to zero"));
        auto* Tool=InventoryTool();
        Check(Hero->Inventory->Selected==EMCToolSlot::Brush && (Hero->Brush->IsVisible() || (Tool && Tool->IsVisible())),TEXT("fresh default brush is visible after traversal"));
        Check(!Hero->IsPrimaryHeld() && !Hero->Inventory->HealingTarget && Hero->Inventory->SprayReadyAt<=Now,TEXT("old held treatment and spray target do not survive reset"));
        Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
    };
    auto CheckFreshDirector=[&]() {
        auto* Director=Mode->DayDirector.Get();
        if(!Director || GS->Day!=1 || GS->StepIndex!=0 || GS->TasksTotal==0) return false;
        Check(GS->DayPlan && Director->Settings && GS->Phase==EMCShiftPhase::Working && !GS->bDevManualEvents && !GS->bDayOneComplete,TEXT("automatic scheduler starts the real authored first-day director"));
        Check(GS->DayStartedAt>=R.ResetAt+7.9 && GS->StepStartedAt>=R.ResetAt+7.9,TEXT("new director uses its own start timestamps"));
        Check(Director->CountDirt()>0 && GS->TasksLeft==Director->CountDirt(),TEXT("new first-step tasks match actual dirt in the arena"));
        const auto& Step=Director->Settings->Steps[0];
        Check(Step.Seconds<=0?GS->PhaseEndsAt==0:GS->PhaseEndsAt>Now,TEXT("first step deadline follows its authored timer setting"));
        return true;
    };
    if(R.Stage>=1 && R.Stage<=5 && R.ResetNumber>0 && (R.Stage==1 || R.Stage==2 || R.Stage==5)) {
        if(Hero->Status->State.Health<R.ExpectedPlayerHealth-.01f || GS->MouthHealth<R.ExpectedMouthHealth-.01f) {
            Check(false,TEXT("old hazards caused delayed damage after reset"));Finish();return;
        }
        for(const auto& Entry:R.OldActors) if(Entry.IsValid()) {Check(false,TEXT("an old hazard still exists after reset"));Finish();return;}
        if(!FMath::IsNearlyZero(Cold,.001f)) {Check(false,TEXT("frost returned after reset"));Finish();return;}
    }
    if(R.Stage<0) {
        Mode->SetActorTickEnabled(false);for(TActorIterator<AMCDayDirector> It(World);It;++It) It->SetActorTickEnabled(false);
        for(TActorIterator<AMCFoodActor> It(World);It;++It) It->Dispose();
        Tongue->ResetPain();Tongue->ResetPressure();Tongue->Settings.bAutomaticJolts=false;
        GS->Phase=EMCShiftPhase::Working;GS->PhaseEndsAt=0;GS->bPhysicalBrushes=false;GS->bDevManualEvents=true;
        Hero->Status->Initialize(100);
        if(!PlaceOnFloor(FVector(-650,350,0),FRotator::ZeroRotator)) {Check(false,TEXT("cold drink probe has real tongue geometry"));Finish();return;}
        auto* Authored=LoadObject<UMCDayPlan>(nullptr,TEXT("/Game/Data/DA_Day01.DA_Day01"));
        if(!Authored || !Climate) {Check(false,TEXT("authored day plan and climate material collection exist"));Finish();return;}
        GS->DayPlan=Authored;auto* Plan=DuplicateObject<UMCDayPlan>(Authored,World);
        Plan->FloodHeight=FMath::Clamp(float(Hero->GetActorLocation().Z+100),60.f,240.f);
        R.Cola=World->SpawnActor<AMCColdColaEvent>();R.Cola->Start(Plan);
        // Bound only the drink timeline so its first fill stays live for the reset.
        R.Cola->Drink->WaterSettings.FillSeconds=10;R.Cola->Drink->WaterSettings.DrainSeconds=5;R.Cola->Drink->Seconds=15;
        R.Cola->Drink->Flow=100;R.Cola->Drink->ForceNetUpdate();
        Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
        View(FVector(-250,150,80),FVector(-1100,-720,570));R.Stage=0;R.StageAt=Now;
        UE_LOG(LogTemp,Display,TEXT("MC_SHIFT_RESET_STAGE 0 live cola"));
    }
    else if(R.Stage==0) {
        R.PeakFrost=FMath::Max(R.PeakFrost,Cold);
        if(Move->IsSwimming() && Hero->AnimationSwim>.8f && Hero->ToothPhysics->CanAct()) {
            R.SwimSeconds+=World->GetDeltaSeconds();
            if(auto* Tool=InventoryTool()) R.ToolWasHidden|=!Tool->IsVisible();
            Hero->AddMovementInput(FVector(-1,0,0),.15f);
        }
        if(Elapsed>4.5 && R.SwimSeconds>.5f && Move->IsSwimming() && R.Cola->IceLeft()>0 && R.PeakFrost>.8f) {
            Check(R.Cola->bActive && R.Cola->Drink->IsActive() && R.ToolWasHidden,TEXT("reset starts from a live frozen drink, swimming pose, hidden pickaxe and spawned ice"));
            BeginReset();R.Stage=1;
        }
        else if(Elapsed>12) {Check(false,TEXT("cold drink reached live frost, swimming and ice before reset"));Finish();}
    }
    else if(R.Stage==1 && Elapsed>1.8) {
        CheckSettledReset();R.Stage=2;R.StageAt=Now;View(Hero->GetActorLocation(),FVector(-400,440,220));
    }
    else if(R.Stage==2) {
        if(Elapsed>.4 && !R.ToolChecked[0]) {Check(InventoryTool() && InventoryTool()->IsVisible(),TEXT("pickaxe can be selected and shown after leaving swimming"));R.ToolChecked[0]=true;}
        if(CheckFreshDirector()) {
            Check(Hero->Status->State.Health==R.ExpectedPlayerHealth && GS->MouthHealth==R.ExpectedMouthHealth,TEXT("no old hazard damages the fresh player or mouth through the new-day start"));
            R.FirstDirector=Mode->DayDirector;Mode->SetActorTickEnabled(false);Mode->DayDirector->SetActorTickEnabled(false);
            GS->bPhysicalBrushes=false;GS->bDevManualEvents=true;GS->PhaseEndsAt=0;
            FHitResult Floor;if(!Tongue->SurfacePoint(FVector(-250,0,0),Floor)) {Check(false,TEXT("treatment probe has real tongue geometry"));Finish();return;}
            auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
            const auto* Egg=Table?Table->FindRow<FMCFoodRow>(TEXT("Egg"),TEXT("Shift reset")):nullptr;
            if(!Egg) {Check(false,TEXT("authored egg row exists"));Finish();return;}
            FMCFoodRow Row=*Egg;Row.SpoilSeconds=3;Row.AbsorbSeconds=.5f;
            const FTransform Transform(Floor.ImpactPoint+FVector(0,0,80));FRandomStream Random(41);
            auto* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Transform);
            Food->ConfigureItem(TEXT("Egg"),Row,Random);Food->Batch=17001;Food->FinishSpawning(Transform);Food->Phase=EMCFoodPhase::Free;
            Food->SetActorLocation(Floor.ImpactPoint+FVector(0,0,Food->Body->Bounds.BoxExtent.Z+3),false,nullptr,ETeleportType::TeleportPhysics);Food->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);R.Food=Food;
            PlaceOnFloor(Floor.ImpactPoint+FVector(-155,0,0),FRotator::ZeroRotator);
            View(Floor.ImpactPoint+FVector(-50,0,65),FVector(-170,390,280));R.Stage=3;R.StageAt=Now;
            UE_LOG(LogTemp,Display,TEXT("MC_SHIFT_RESET_STAGE 3 unattended food"));
        }
        else if(Now-R.ResetAt>11) {Check(false,TEXT("automatic real director started after first reset"));Finish();}
    }
    else if(R.Stage==3) {
        if(R.Food.IsValid() && R.Food->bSpoiled && !R.Ulcer.IsValid()) {
            auto* Fire=AMCFirePatch::Ignite(Hero,R.Food->GetActorLocation()-FVector(0,0,R.Food->Body->Bounds.BoxExtent.Z),60,2,17001);
            R.Ulcer=Fire?Fire->Lesion.Get():nullptr;
        }
        if(R.Ulcer.IsValid()) {
            Check(R.Food.IsValid() && !R.Food->bAbsorbed && R.Ulcer->bUlcer && R.Ulcer->Healing==0,TEXT("food never absorbs; fire creates the untreated ulcer"));
            for(TActorIterator<AMCThroat> It(World);It;++It) {R.Throat=*It;break;}
            auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
            const auto* PepperRow=Table?Table->FindRow<FMCFoodRow>(TEXT("SpicyPepper"),TEXT("Shift reset")):nullptr;
            if(!R.Throat.IsValid() || !PepperRow) {Check(false,TEXT("real throat and authored spicy pepper exist"));Finish();return;}
            const FTransform T(R.Throat->GetActorTransform().TransformPosition(R.Throat->ZoneCenter+FVector(0,0,60)));FRandomStream Random(42);
            auto* Pepper=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),T);
            Pepper->ConfigureItem(TEXT("SpicyPepper"),*PepperRow,Random);Pepper->FinishSpawning(T);Pepper->Phase=EMCFoodPhase::Free;Pepper->Body->SetSimulatePhysics(false);R.Pepper=Pepper;
            R.Throat->ThroatPhase=EMCThroatPhase::Collecting;R.Throat->PhaseStartedAt=Now;R.Throat->Tick(.01f);R.Throat->SetActorTickEnabled(false);R.Throat->ForceNetUpdate();
            const FTransform W(Hero->GetActorLocation()+FVector(100,0,-Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+5));
            auto* Wave=World->SpawnActorDeferred<AMCHazardWave>(AMCHazardWave::StaticClass(),W);Wave->WarningSeconds=2.5f;Wave->TravelSeconds=.8f;Wave->MaxRadius=350;Wave->FinishSpawning(W);R.Wave=Wave;
            Hero->Inventory->ServerSelect(EMCToolSlot::Spray);Hero->ServerSetPrimary(true);R.Stage=4;R.StageAt=Now;
            UE_LOG(LogTemp,Display,TEXT("MC_SHIFT_RESET_STAGE 4 held spray, paused pepper and pending wave"));
        }
        else if(Elapsed>10) {Check(false,TEXT("damage created a treatment ulcer"));Finish();}
    }
    else if(R.Stage==4 && Elapsed>1.2) {
        Check(Hero->IsPrimaryHeld() && Hero->Inventory->HealingTarget==R.Ulcer.Get() && R.Ulcer.IsValid() && R.Ulcer->Healing>.1f && R.Ulcer->Healing<.5f,TEXT("reset interrupts real LMB treatment with saved partial progress"));
        Check(R.Pepper.IsValid() && R.Pepper->bFusePaused && R.Pepper->FuseRemaining()>0 && R.Throat->ThroatPhase==EMCThroatPhase::Swallowing,TEXT("automatic throat intake has paused a live pepper fuse"));
        Check(R.Wave.IsValid() && Now>R.Wave->StartedAt+.5 && R.Wave->Radius()==0,TEXT("warning wave has really ticked and its hit is still pending"));
        BeginReset();R.Stage=5;
    }
    else if(R.Stage==5 && Elapsed>1.8) {
        CheckSettledReset();
        Check(R.Throat.IsValid() && R.Throat->ThroatPhase==EMCThroatPhase::Collecting,TEXT("reset cancels the old swallow anticipation"));
        Check(!R.FirstDirector.IsValid(),TEXT("the first real director is replaced on the second reset"));
        R.Stage=6;R.StageAt=Now;View(Hero->GetActorLocation(),FVector(-400,440,220));
    }
    else if(R.Stage==6) {
        if(Elapsed>.4 && !R.ToolChecked[1]) {Check(InventoryTool() && InventoryTool()->IsVisible(),TEXT("pickaxe also returns after a reset during treatment"));R.ToolChecked[1]=true;}
        // Wait beyond the removed wave's complete lifetime and the normal pepper
        // fuse, without writing a new day number or shortening intermission.
        if(Hero->Status->State.Health<R.ExpectedPlayerHealth-.01f || GS->MouthHealth<R.ExpectedMouthHealth-.01f || !FMath::IsNearlyZero(Cold,.001f)) {Check(false,TEXT("late wave, ulcer or pepper effects survived the second reset"));Finish();return;}
        if(CheckFreshDirector()) {
            bool OldGone=true;for(const auto& Entry:R.OldActors) OldGone&=!Entry.IsValid();
            Check(OldGone && R.Throat->VomitCount==R.VomitBaseline,TEXT("removed hazards stay gone and canceled anticipation produces no later vomit"));
            auto* Director=Mode->DayDirector.Get();
            for(int32 I=0;I<Director->Settings->Steps.Num();++I) if(Director->Settings->Steps[I].Step==EMCDayStep::BreakfastRain) {R.RainIndex=I;break;}
            if(R.RainIndex<0) {Check(false,TEXT("new authored director has a timed breakfast step"));Finish();return;}
            // Exercise the production director transition API; this is not a
            // claim that the earlier cleanup steps were completed by a player.
            while(GS->StepIndex<R.RainIndex && !GS->bDayOneComplete) Director->Next(false);
            Check(GS->StepIndex==R.RainIndex && GS->PhaseEndsAt>Now && GS->StepStartedAt>=Now-.1,TEXT("new director transition installs its own timed event"));
            R.Stage=7;R.StageAt=Now;
            UE_LOG(LogTemp,Display,TEXT("MC_SHIFT_RESET_STAGE 7 new timed breakfast"));
        }
        else if(Now-R.ResetAt>11) {Check(false,TEXT("automatic real director started after second reset"));Finish();}
    }
    else if(R.Stage==7 && Elapsed>.6) {
        auto* Director=Mode->DayDirector.Get();
        Check(Director && Director->RainSpawned>0 && Director->CountFood(2)>0
            && GS->TasksLeft==Director->Settings->BreakfastCount-Director->RainSpawned && GS->TasksTotal>=GS->TasksLeft && GS->PhaseEndsAt>Now,
            TEXT("fresh timed event really spawns food and owns its live task counters"));
        Check(R.ResetNumber==2 && R.PeakFrost>.8f && R.SwimSeconds>.5f && R.Absorbing && R.ToolChecked[0] && R.ToolChecked[1],TEXT("both full runtime reset paths ran before approval"));
        Finish();
    }
}
#endif
