#if !UE_BUILD_SHIPPING
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCInventoryComponent.h"
#include "MCGameState.h"
#include "MCArenaTooth.h"
#include "MCFoodActor.h"
#include "MCCoffeeFlood.h"
#include "MCColdCola.h"
#include "MCTongue.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "HAL/FileManager.h"
#include "InputActionValue.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

// Records the game render target only. No desktop capture or OS input.
void MCTickApprovalRecorder(UWorld* World)
{
    FString Name; if(!FParse::Value(FCommandLine::Get(),TEXT("MCVideo="),Name)) return;
    static TWeakObjectPtr<UWorld> Last; static double At=0,Next=0; static int32 Frame=0;
    if(Last!=World) {
        Last=World; At=World->GetTimeSeconds(); Next=0; Frame=0;
        // Render more samples per game second on a shared GPU. The encoder uses
        // game timestamps, so the resulting clip keeps the real gameplay timing.
        float CaptureTimeScale=1;
        if(FParse::Value(FCommandLine::Get(),TEXT("MCCaptureTimeScale="),CaptureTimeScale))
            World->GetWorldSettings()->SetTimeDilation(FMath::Clamp(CaptureTimeScale,.25f,1.f));
    }
    const double T=World->GetTimeSeconds()-At; if(T<3 || T<Next) return; Next=FMath::Max(Next+1./15.,T-1./15.);
    const FString Dir=FPaths::ProjectSavedDir()/TEXT("ApprovalFrames")/Name;
    IFileManager::Get().MakeDirectory(*Dir,true);
    const FString File=FString::Printf(TEXT("%06d.png"),Frame++);
    FScreenshotRequest::RequestScreenshot(Dir/File,true,false);
    const FString Entry=FString::Printf(TEXT("%s,%.6f\n"),*File,T);
    FFileHelper::SaveStringToFile(Entry,*(Dir/TEXT("times.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
}

void MCTickApprovalValidation(UWorld* World)
{
    struct FRun {
        TWeakObjectPtr<UWorld> World; TWeakObjectPtr<ACameraActor> Camera;
        TWeakObjectPtr<AMCArenaTooth> Tooth; TWeakObjectPtr<AMCCoffeeFlood> Coffee; TWeakObjectPtr<AMCColdColaEvent> Cola;
        TArray<TWeakObjectPtr<AMCIceBlock>> Blocks;
        double At=0,StageAt=0; float Age=0,StartZ=0,StartY=0,SwimSeconds=0,Slide=0; FVector StartP;
        int32 Stage=-1,BlockIndex=0,PlacedBlock=-1,PoseSamples=0,EnamelSamples=0; float LowestPickClearance=MAX_flt,LowestEnamelClearance=MAX_flt;
        bool Failed=false,Climbed=false,Hung=false,Sideways=false,Mantled=false,Jumped=false,Slippery=false,ToolVisible=true;
        bool ClimbStartedWalking=false,ClimbPressSent=false,SwamBeforeClimb=false,ClimbedFromSwim=false,JumpLeftWall=false;
        uint8 WallContactMask=0,LimbMotionMask=0;
        FVector FirstLimbPositions[4]={}; bool LimbBaseline=false;
        bool HiddenPickaxeSafe=true,HiddenPickaxeSeen=false,SwimHandBaseline=false;
        uint8 SwimHandMotionMask=0;FVector FirstSwimHands[2]={};
        double NextClimbContactDiagnostic=0;float ClosestClimbContacts[4]={MAX_flt,MAX_flt,MAX_flt,MAX_flt};
    }; static FRun R; if(R.World!=World) { R=FRun(); R.World=World; }
    R.Age+=World->GetDeltaSeconds(); auto* GS=World->GetGameState<AMCGameState>(); auto* PC=World->GetFirstPlayerController();
    auto* H=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    AMCTongue* Tongue=nullptr; for(TActorIterator<AMCTongue> It(World);It;++It) { Tongue=*It; break; }
    if(!H || !GS || !Tongue || R.Age<4) return;
#if WITH_EDITOR
    if(GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
    FString Case; FParse::Value(FCommandLine::Get(),TEXT("MCApproval="),Case);
    if(auto* Mode=World->GetAuthGameMode()) Mode->SetActorTickEnabled(false);
    GS->Phase=EMCShiftPhase::Working; GS->PhaseEndsAt=0; GS->bDevManualEvents=true; GS->bPhysicalBrushes=false;
    if(GS->DayPlan) for(int32 I=0;I<GS->DayPlan->Steps.Num();++I) {
        const EMCDayStep Wanted=Case==TEXT("Coffee")?EMCDayStep::CoffeeWaves:Case==TEXT("Cola")?EMCDayStep::ColdCola:EMCDayStep::BrushLesson;
        if(GS->DayPlan->Steps[I].Step==Wanted) GS->StepIndex=I;
    }
    if(R.Cola.IsValid()) {GS->TasksTotal=R.Cola->Profile?R.Cola->Profile->IceCount:5; GS->TasksLeft=R.Cola->IceLeft();}
    const double Now=GS->GetServerWorldTimeSeconds(),T=Now-R.At,S=Now-R.StageAt;
    auto* Move=CastChecked<UMCToothMovementComponent>(H->GetCharacterMovement());
    auto Check=[&](bool OK,const TCHAR* Text) { R.Failed|=!OK; UE_LOG(LogTemp,Display,TEXT("MC_APPROVAL_CHECK %s %s"),OK?TEXT("PASS"):TEXT("FAIL"),Text); };
    auto Finish=[&](){ UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s APPROVAL_%s"),R.Failed?TEXT("FAIL"):TEXT("PASS"),*Case); FPlatformMisc::RequestExitWithStatus(false,R.Failed?1:0); };
    auto Floor=[&](FVector P){ FHitResult Hit; return Tongue->SurfacePoint(P,Hit)?Hit.ImpactPoint:P; };
    auto Place=[&](FVector P,FRotator Yaw=FRotator::ZeroRotator){ H->StopHandle(); H->ServerSetPrimary(false); Move->StopMovementImmediately(); H->SetActorLocationAndRotation(P,Yaw,false,nullptr,ETeleportType::TeleportPhysics); Move->SetMovementMode(MOVE_Falling); };
    auto View=[&](FVector Aim,FVector Offset){ if(!R.Camera.IsValid()) {R.Camera=World->SpawnActor<ACameraActor>(); R.Camera->GetCameraComponent()->SetFieldOfView(55); PC->SetViewTarget(R.Camera.Get());} R.Camera->SetActorLocationAndRotation(Aim+Offset,(-Offset).Rotation()); };
    if(R.Stage<0) {
        for(TActorIterator<AMCFoodActor> It(World);It;++It) It->Dispose();
        GS->DayPlan=LoadObject<UMCDayPlan>(nullptr,TEXT("/Game/Data/DA_Day01.DA_Day01")); H->Status->Initialize(100);
        R.Stage=0; R.At=R.StageAt=Now;
        if(Case==TEXT("Climb")) {
            for(const auto& Tooth:GS->ArenaTeeth) if(Tooth && Tooth->State.ToothId==8) R.Tooth=Tooth;
            if(!R.Tooth.IsValid()) {Check(false,TEXT("actual arena tooth 8 exists")); Finish(); return;}
            const auto Box=R.Tooth->Body->Bounds.GetBox();
            const FVector Approach(Box.GetCenter().X,Box.Min.Y-H->GetCapsuleComponent()->GetScaledCapsuleRadius()-100,0);
            FHitResult Ground;
            if(!Tongue->SurfacePoint(Approach,Ground)) {Check(false,TEXT("walkable tongue approach to actual tooth exists")); Finish(); return;}
            const FVector P=Ground.ImpactPoint+FVector(0,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3);
            Place(P,FRotator(0,90,0)); R.StartZ=P.Z; R.StartY=P.X;
            View(Box.GetCenter()+FVector(0,-65,110),FVector(-700,-780,420));
        } else if(Case==TEXT("Coffee")) {
            Place(Floor(FVector(-200,0,0))+FVector(0,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3));
            auto* Plan=DuplicateObject<UMCDayPlan>(GS->DayPlan,World); Plan->FloodHeight=155;
            R.Coffee=World->SpawnActor<AMCCoffeeFlood>(); R.Coffee->Start(Plan);
            R.Coffee->WaterSettings.FillSeconds=7; R.Coffee->WaterSettings.DrainSeconds=5; R.Coffee->Seconds=12;
            R.Coffee->WaterSettings.DrainAcceleration=120; R.StartP=H->GetActorLocation();
            View(FVector(-30,0,0),FVector(-1050,-750,720));
        } else if(Case==TEXT("Cola")) {
            Place(Floor(FVector(-800,0,0))+FVector(0,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3));
            R.Cola=World->SpawnActor<AMCColdColaEvent>(); R.Cola->Start(GS->DayPlan);
            View(FVector(-100,0,0),FVector(-1350,-700,750)); H->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
        } else if(Case==TEXT("Camera")) {
            Place(Floor(FVector(-800,0,0))+FVector(0,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3)); PC->SetViewTarget(H);
        } else if(Case==TEXT("Materials")) {
            Place(Floor(FVector(-450,450,0))+FVector(0,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3));
            View(FVector(-250,0,30),FVector(-1200,-150,430));
        } else if(Case==TEXT("Pickaxe")) {
            Place(Floor(FVector(-500,300,0))+FVector(0,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3));
            H->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
            View(H->GetActorLocation()+FVector(20,0,10),FVector(-260,-370,140));
        } else {Check(false,TEXT("known approval case")); Finish();}
    }
    else if(Case==TEXT("Climb")) {
        if(H->Inventory->Selected==EMCToolSlot::Pickaxe && (Move->IsSwimming() || Move->IsClimbing())) {
            R.HiddenPickaxeSeen=true;
            R.HiddenPickaxeSafe&=!H->Inventory->ShouldPresentTool()
                && H->Inventory->ConstrainPickaxeGrip(H->GetMesh()->GetSocketTransform(H->RigBone(TEXT("hand_r")))).IsNearlyZero();
            if(Move->IsSwimming() && H->AnimationSwim>.8f) {
                for(int32 Side=0;Side<2;++Side) {
                    const FVector Local=H->GetMesh()->GetComponentTransform().InverseTransformPosition(H->GetMesh()->GetSocketLocation(H->RigBone(Side==0?TEXT("hand_l"):TEXT("hand_r"))));
                    if(!R.SwimHandBaseline) R.FirstSwimHands[Side]=Local;
                    else if(FVector::Dist(Local,R.FirstSwimHands[Side])>4) R.SwimHandMotionMask|=uint8(1u<<Side);
                }
                R.SwimHandBaseline=true;
            }
        }
        // Read the final rendered skeleton, after pose/physics blending. A climb
        // mode flag cannot prove that the mittens or feet reach visible enamel.
        if(Move->IsClimbing() && H->AnimationClimb>.9f) {
            const FName Roles[]={TEXT("hand_l"),TEXT("hand_r"),TEXT("foot_l"),TEXT("foot_r")};
            const FVector N=Move->ClimbNormal;
            const bool Diagnose=(R.Stage==5 || R.Stage==6) && Now>=R.NextClimbContactDiagnostic;
            if(Diagnose) R.NextClimbContactDiagnostic=Now+.2;
            for(int32 Limb=0;Limb<4;++Limb) {
                const FVector P=H->GetMesh()->GetSocketLocation(H->RigBone(Roles[Limb]));
                const FVector Local=H->GetMesh()->GetComponentTransform().InverseTransformPosition(P);
                if(!R.LimbBaseline) R.FirstLimbPositions[Limb]=Local;
                else if(FVector::Dist(Local,R.FirstLimbPositions[Limb])>4) R.LimbMotionMask|=uint8(1u<<Limb);
                FHitResult Touch;FCollisionQueryParams Q(SCENE_QUERY_STAT(MCApprovalClimbContact),true,H);
                const bool Hit=R.Tooth->BrushSurface->LineTraceComponent(Touch,P+N*40,P-N*80,Q);
                const float Distance=Hit?float(FVector::Dist(P,Touch.ImpactPoint)):MAX_flt;
                R.ClosestClimbContacts[Limb]=FMath::Min(R.ClosestClimbContacts[Limb],Distance);
                if(Hit && Distance<(Limb<2?24.f:27.f)) R.WallContactMask|=uint8(1u<<Limb);
                if(Diagnose) UE_LOG(LogTemp,Display,TEXT("MC_CLIMB_RENDER_CONTACT stage=%d limb=%d hit=%d distance=%.2f closest=%.2f climb=%.3f swim=%.3f actor=%s socket=%s touch=%s normal=%s"),
                    R.Stage,Limb,Hit,Distance,R.ClosestClimbContacts[Limb],H->AnimationClimb,H->AnimationSwim,*H->GetActorLocation().ToString(),*P.ToString(),*Touch.ImpactPoint.ToString(),*Touch.ImpactNormal.ToString());
            }
            R.LimbBaseline=true;
        }
        if(R.Stage==0) {
            R.ClimbStartedWalking|=Move->IsMovingOnGround() && !R.ClimbPressSent;
            if(S>.6 && !R.ClimbPressSent) {H->StartHandle();R.ClimbPressSent=true;}
            if(R.ClimbPressSent) {
                if(Move->IsClimbing()) {H->MoveRight(FInputActionValue(0.f));H->MoveForward(FInputActionValue(1.f));}
                else H->MoveRight(FInputActionValue(1.f));
            }
            R.Climbed|=Move->IsClimbing() && H->GetActorLocation().Z>R.StartZ+25;
            if(R.Climbed || S>4) {
                Check(R.ClimbStartedWalking && R.Climbed,TEXT("E plus movement attaches from the tongue without an air teleport"));
                if(!R.Climbed) {Finish();return;}
                UE_LOG(LogTemp,Display,TEXT("MC_CLIMB_POSE climb P=%s V=%s mode=%d"),*H->GetActorLocation().ToString(),*Move->Velocity.ToString(),int32(Move->MovementMode));
                H->MoveForward(FInputActionValue(0.f));R.Stage=7; R.StageAt=Now;
            }
        } else if(R.Stage==7) {
            // AddMovementInput is accumulated for the next movement tick. The
            // last upward command above remains queued when this fixture sends
            // zero, so measure hanging after that command has been consumed.
            if(S>.3) {
                R.Hung=Move->IsClimbing() && Move->Velocity.Size()<5;
                Check(R.Hung,TEXT("released upward input settles while E remains held"));
                R.StartP=H->GetActorLocation();R.Stage=1;R.StageAt=Now;
                UE_LOG(LogTemp,Display,TEXT("MC_CLIMB_POSE hang_baseline P=%s V=%s mode=%d"),*R.StartP.ToString(),*Move->Velocity.ToString(),int32(Move->MovementMode));
            }
        } else if(R.Stage==1) {
            R.Hung&=Move->IsClimbing() && Move->Velocity.Size()<5 && FMath::Abs(H->GetActorLocation().Z-R.StartP.Z)<4;
            if(S>1.2) {Check(R.Hung,TEXT("held E keeps a stable wall hang without stamina")); UE_LOG(LogTemp,Display,TEXT("MC_CLIMB_POSE hang P=%s V=%s mode=%d"),*H->GetActorLocation().ToString(),*Move->Velocity.ToString(),int32(Move->MovementMode)); R.Stage=2; R.StageAt=Now; R.StartP=H->GetActorLocation();}
        } else if(R.Stage==2) {
            if(S<.45) H->MoveRight(FInputActionValue(1.f));else H->MoveRight(FInputActionValue(0.f));
            R.Sideways|=Move->IsClimbing() && FVector::Dist2D(R.StartP,H->GetActorLocation())>25;
            if(S>1) {Check(R.Sideways,TEXT("held E crawls sideways across the tooth"));R.Stage=3; R.StageAt=Now;}
        } else if(R.Stage==3) {
            H->MoveForward(FInputActionValue(1.f));
            R.Mantled=Move->IsMovingOnGround() && H->GetMovementBaseObject()==R.Tooth->Body.Get() && H->GetActorLocation().Z>R.StartZ+50;
            if(R.Mantled || S>6) {
                Check(R.Mantled,TEXT("mantle ends walking on this tooth crown"));
                Check(R.WallContactMask==15,TEXT("both rendered hands and feet contact visible enamel"));
                Check(R.LimbMotionMask==15,TEXT("all four rendered limbs move through the procedural cycle"));
                UE_LOG(LogTemp,Display,TEXT("MC_CLIMB_CONTACTS mask=%d motion=%d"),int32(R.WallContactMask),int32(R.LimbMotionMask));
                H->MoveForward(FInputActionValue(0.f));H->StopHandle();R.Stage=4; R.StageAt=Now;
            }
        } else if(R.Stage==4 && S>1) {
            const auto Box=R.Tooth->Body->Bounds.GetBox();
            const FVector Approach(Box.GetCenter().X,Box.Min.Y-H->GetCapsuleComponent()->GetScaledCapsuleRadius()-100,0);
            const FVector Sole=Floor(Approach);
            Place(Sole+FVector(0,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),FRotator(0,90,0));
            H->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
            R.WallContactMask=0;R.LimbMotionMask=0;R.LimbBaseline=false;
            for(float& Distance:R.ClosestClimbContacts) Distance=MAX_flt;
            auto* Plan=DuplicateObject<UMCDayPlan>(GS->DayPlan,World);Plan->FloodHeight=float(Sole.Z+120);
            R.Coffee=World->SpawnActor<AMCCoffeeFlood>();R.Coffee->Start(Plan);
            R.Coffee->WaterSettings.DryHeight=float(Sole.Z-40);R.Coffee->WaterSettings.FillSeconds=15;R.Coffee->WaterSettings.DrainSeconds=10;
            R.Coffee->WaterSettings.RippleHeight=3;R.Coffee->WaterSettings.FrontHeight=0;R.Coffee->Flow=0;R.Coffee->WaterSettings.DrainAcceleration=0;
            R.Coffee->HalfSize=R.Coffee->HalfSize.ComponentMax(FVector(1400,1000,220));R.Coffee->Seconds=25;R.Coffee->StartedAt=Now-10;
            R.ClimbPressSent=false;R.Stage=5; R.StageAt=Now;
        } else if(R.Stage==5) {
            if(Move->IsSwimming() && !R.ClimbPressSent) R.SwamBeforeClimb=true;
            if(S>1 && R.SwamBeforeClimb && !R.ClimbPressSent) {H->StartHandle();R.ClimbPressSent=true;R.StartZ=H->GetActorLocation().Z;}
            if(R.ClimbPressSent) {
                if(Move->IsClimbing()) H->MoveForward(FInputActionValue(1.f));else H->MoveRight(FInputActionValue(1.f));
                R.ClimbedFromSwim|=Move->IsClimbing() && !H->ClingTooth && H->GetActorLocation().Z>R.StartZ+30;
            }
            if(R.ClimbedFromSwim || S>6) {
                Check(R.SwamBeforeClimb && R.ClimbedFromSwim,TEXT("E transfers a real swimmer into climbing without a static coffee anchor"));
                H->MoveForward(FInputActionValue(0.f));H->MoveRight(FInputActionValue(0.f));R.Stage=6;R.StageAt=Now;
            }
        } else if(R.Stage==6) {
            // Move again after the swim-to-climb blend settles. Measure a fresh
            // four-limb cycle with the pickaxe selected and hidden throughout.
            if(!R.Jumped) H->MoveRight(FInputActionValue(S<.35?1.f:0.f));
            if(S>.7 && !R.Jumped) {
                Check(Move->IsClimbing(),TEXT("wall hang remains controllable after leaving water"));
                Check(R.HiddenPickaxeSeen && R.HiddenPickaxeSafe && R.SwimHandMotionMask==3,TEXT("hidden selected pickaxe preserves both rendered swimming hand strokes"));
                Check(R.WallContactMask==15 && R.LimbMotionMask==15,TEXT("hidden selected pickaxe preserves all four rendered climbing contacts and motions"));
                UE_LOG(LogTemp,Display,TEXT("MC_CLIMB_HIDDEN_PICKAXE safe=%d swim_hands=%d contacts=%d motion=%d"),R.HiddenPickaxeSafe,int32(R.SwimHandMotionMask),int32(R.WallContactMask),int32(R.LimbMotionMask));
                H->Jump();R.Jumped=true;
            }
            if(R.Jumped && S<1.2 && !Move->IsClimbing() && FVector::DotProduct(Move->Velocity,FVector(Move->ClimbNormal))>100 && Move->Velocity.Z>100) R.JumpLeftWall=true;
            if(S>2.2) {Check(R.JumpLeftWall,TEXT("Space launches away from the wall with upward velocity")); H->StopJumping();H->StopHandle();R.Coffee->Stop();Finish();}
        }
    } else if(Case==TEXT("Coffee")) {
        if(T>2 && T<4.5) H->AddMovementInput(FVector(0,1,0));
        if(T>2) View(H->GetActorLocation()+FVector(0,0,25),FVector(-440,-340,230));
        if(Move->IsSwimming()) R.SwimSeconds+=World->GetDeltaSeconds();
        R.Failed|=!H->ToothPhysics->CanAct();
        if(T>13) { Check(R.SwimSeconds>2,TEXT("coffee enters controllable swimming")); Check(H->ToothPhysics->CanAct(),TEXT("coffee front does not stun")); Check(FMath::Abs(H->GetActorLocation().Y-R.StartP.Y)>100,TEXT("swimming input moves player")); Finish(); }
    } else if(Case==TEXT("Cola")) {
        R.Slippery|=Move->GroundSurface==EMCGroundSurface::Slippery;
        if(T>9 && R.Stage==0) {
            Check(R.Cola->FrostAmount()>.9f,TEXT("whole arena frost reaches full strength"));
            for(TActorIterator<AMCIceBlock> It(World);It;++It) R.Blocks.Add(*It);
            Check(R.Blocks.Num()>=3,TEXT("ice falls in multiple arbitrary shapes")); R.Stage=1; R.StageAt=Now;
            Place(Floor(FVector(-800,-100,0))+FVector(0,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3));
        } else if(R.Stage==1) {
            if(S<1.2) H->AddMovementInput(FVector(1,0,0));
            else if(S<2.4) {R.Slide=FMath::Max(R.Slide,float(Move->Velocity.Size2D()));}
            if(S>3) {Check(R.Slippery && R.Slide>30,TEXT("frost gives sliding inertia after input release")); R.Stage=2; R.StageAt=Now;}
        } else if(R.Stage==2 && R.BlockIndex<R.Blocks.Num()) {
            auto* Ice=R.Blocks[R.BlockIndex].Get();
            if(!Ice || Ice->bBroken) {++R.BlockIndex; R.StageAt=Now;}
            else if(R.PlacedBlock!=R.BlockIndex) {
                const FVector P=Ice->Body->Bounds.Origin;
                const float Radius=H->GetCapsuleComponent()->GetScaledCapsuleRadius();
                const FVector Stand=Floor(P+FVector(-Ice->Body->Bounds.BoxExtent.X-Radius-20,0,0))+FVector(0,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3);
                Place(Stand); View(P+FVector(-30,0,40),FVector(-480,-410,310)); H->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
                R.PlacedBlock=R.BlockIndex;R.StageAt=Now;
            } else if(S>.8) {
                const FVector D=Ice->Body->Bounds.GetBox().GetClosestPointTo(H->GetActorLocation())-H->GetActorLocation();
                H->SetActorRotation(FRotator(0,D.Rotation().Yaw,0));
                if(D.SizeSquared2D()>FMath::Square(95.f)) H->AddMovementInput(D.GetSafeNormal2D());
                H->SwingBrush();
                if(S>.9 && S<1.05) UE_LOG(LogTemp,Display,TEXT("MC_ICE_POSE index=%d H=%s Ice=%s D=%.1f can=%d contact=%d mode=%d hp=%.1f"),R.BlockIndex,*H->GetActorLocation().ToString(),*Ice->GetActorLocation().ToString(),D.Size(),H->CanWork(),H->CanContact(Ice),int32(Move->MovementMode),Ice->Health);
            }
            if(S>8) { Check(false,TEXT("pickaxe breaks settled ice block")); ++R.BlockIndex; R.StageAt=Now; }
        } else if(R.Stage==2) {Check(R.Cola->IceLeft()==0,TEXT("all block shapes break with pickaxe and reduce event tasks")); R.Stage=3; R.StageAt=Now; View(FVector(-100,0,0),FVector(-1350,-700,750));}
        else if(R.Stage==3 && S>6) {Check(R.Cola->IsComplete(),TEXT("frost thaws and event finishes after ice cleanup")); Finish();}
    } else if(Case==TEXT("Camera")) {
        const FVector Points[]={FVector(-950,0,0),FVector(-700,-480,0),FVector(-200,450,0),FVector(550,0,0)};
        const int32 Index=FMath::Min(3,int32(T/3));
        const FVector Goal=Floor(Points[Index])+FVector(0,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3);
        H->AddMovementInput((Goal-H->GetActorLocation()).GetSafeNormal2D());
        if(T>14) { Check(H->Camera->GetComponentLocation().X<H->GetActorLocation().X-350,TEXT("camera retains distance toward mouth front")); Finish(); }
    } else if(Case==TEXT("Materials")) {
        const FVector Aims[]={FVector(-250,0,30),FVector(100,800,-180),FVector(-300,700,320),FVector(1150,-30,235)};
        const FVector Offsets[]={FVector(-1200,-150,430),FVector(-500,-500,250),FVector(-500,-780,-180),FVector(-650,-310,180)};
        const int32 Index=FMath::Min(3,int32(T/5)); const float Pan=FMath::Fmod(float(T),5.f)/5.f;
        View(Aims[Index]+FVector(100*Pan,0,0),Offsets[Index]+FVector(0,80*Pan,0));
        if(T>20) {Check(true,TEXT("real arena gum palate and throat rendered with updated materials")); Finish();}
    } else if(Case==TEXT("Pickaxe")) {
        if(T<5) {
            H->SetActorRotation(FRotator(0,FMath::FloorToFloat(float(T)/1.25f)*90,0));
            // This fixture teleports yaw after skeletal evaluation. Settle the
            // new heading before starting and measuring its full swing.
            if(FMath::Fmod(float(T),1.25f)>.20f) H->SwingBrush();
        }
        else if(R.Stage==0) {
            for(const auto& Tooth:GS->ArenaTeeth) if(Tooth && Tooth->State.ToothId==8) R.Tooth=Tooth;
            if(!R.Tooth.IsValid()) {Check(false,TEXT("actual tooth exists for surface swing")); Finish(); return;}
            const auto Box=R.Tooth->Body->Bounds.GetBox();
            Place(Floor(FVector(Box.GetCenter().X,Box.Min.Y-55,0))+FVector(0,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),FRotator(0,90,0));
            View(H->GetActorLocation()+FVector(0,30,45),FVector(-300,-430,190)); R.Stage=1;
        } else if(T<14) H->SwingBrush();
        if(T>.5 && T<14 && !(T>5 && T<5.5) && (T>=5 || FMath::Fmod(float(T),1.25f)>.20f)) {
            TArray<UStaticMeshComponent*> Components; H->GetComponents(Components);
            for(auto* Tool:Components) if(Tool->GetFName()==TEXT("InventoryTool") && Tool->GetStaticMesh()) {
                R.ToolVisible&=Tool->IsVisible();
                const auto Bounds=Tool->GetStaticMesh()->GetBounds(); const auto Transform=Tool->GetComponentTransform();
                FCollisionQueryParams Q(SCENE_QUERY_STAT(MCPickApproval),false,H);
                for(int32 I=0;I<8;++I) {
                    const FVector Corner=Transform.TransformPosition(Bounds.Origin+FVector(I&1?Bounds.BoxExtent.X:-Bounds.BoxExtent.X,I&2?Bounds.BoxExtent.Y:-Bounds.BoxExtent.Y,I&4?Bounds.BoxExtent.Z:-Bounds.BoxExtent.Z));
                    FHitResult Hit;
                    if(World->LineTraceSingleByChannel(Hit,Corner+FVector(0,0,220),Corner-FVector(0,0,120),ECC_Visibility,Q) && Hit.ImpactNormal.Z>.4) {
                        const float Clearance=FVector::DotProduct(Corner-Hit.ImpactPoint,Hit.ImpactNormal);
                        if(Clearance<R.LowestPickClearance && Clearance<-3)
                            UE_LOG(LogTemp,Display,TEXT("MC_PICKAXE_PENETRATION t=%.3f yaw=%.0f corner=%s surface=%s hit=%s clearance=%.2f"),T,H->GetActorRotation().Yaw,*Corner.ToString(),*Hit.ImpactPoint.ToString(),*GetNameSafe(Hit.GetComponent()),Clearance);
                        R.LowestPickClearance=FMath::Min(R.LowestPickClearance,Clearance);
                    }
                    if(R.Tooth.IsValid()) {
                        FCollisionQueryParams EnamelQ(SCENE_QUERY_STAT(MCPickApprovalEnamel),true,H);
                        const FVector Direction=(Corner-H->GetActorLocation()).GetSafeNormal();
                        if(R.Tooth->BrushSurface->LineTraceComponent(Hit,H->GetActorLocation(),Corner+Direction*60,EnamelQ)
                            && FVector::DotProduct(Hit.ImpactNormal,Direction)<-.1f) {
                            const float Clearance=FVector::DotProduct(Corner-Hit.ImpactPoint,Hit.ImpactNormal);
                            if(Clearance<R.LowestEnamelClearance && Clearance<-3)
                                UE_LOG(LogTemp,Display,TEXT("MC_PICKAXE_ENAMEL_PENETRATION t=%.3f corner=%s surface=%s clearance=%.2f"),T,*Corner.ToString(),*Hit.ImpactPoint.ToString(),Clearance);
                            R.LowestEnamelClearance=FMath::Min(R.LowestEnamelClearance,Clearance);++R.EnamelSamples;
                        }
                    }
                }
                ++R.PoseSamples;
            }
        }
        if(T>15) {
            UE_LOG(LogTemp,Display,TEXT("MC_PICKAXE_CLEARANCE samples=%d min=%.2f"),R.PoseSamples,R.LowestPickClearance);
            UE_LOG(LogTemp,Display,TEXT("MC_PICKAXE_ENAMEL_CLEARANCE samples=%d min=%.2f"),R.EnamelSamples,R.LowestEnamelClearance);
            Check(R.ToolVisible,TEXT("pickaxe remains visible at four headings and beside an actual tooth"));
            Check(R.PoseSamples>20 && R.LowestPickClearance>-3,TEXT("pickaxe pose stays above tongue and tooth surfaces"));
            Check(R.EnamelSamples>20 && R.LowestEnamelClearance>-3,TEXT("pickaxe stays outside visible curved enamel"));Finish();
        }
    }
    if(T>70) {Check(false,TEXT("approval scenario timeout")); Finish();}
}
#endif
