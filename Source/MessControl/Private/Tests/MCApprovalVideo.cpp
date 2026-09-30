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
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
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
    if(Last!=World) { Last=World; At=World->GetTimeSeconds(); Next=0; Frame=0; }
    const double T=World->GetTimeSeconds()-At; if(T<3 || T<Next) return; Next=T+1./15.;
    const FString Dir=FPaths::ProjectSavedDir()/TEXT("ApprovalFrames")/Name;
    IFileManager::Get().MakeDirectory(*Dir,true);
    const FString File=FString::Printf(TEXT("%06d.bmp"),Frame++);
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
        int32 Stage=-1,BlockIndex=0,PoseSamples=0; float LowestPickClearance=MAX_flt;
        bool Failed=false,Climbed=false,Hung=false,Sideways=false,Mantled=false,Jumped=false,Slippery=false;
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
            const FVector P(Box.GetCenter().X,Box.Min.Y-H->GetCapsuleComponent()->GetScaledCapsuleRadius()-12,Box.Max.Z-160);
            Place(P,FRotator(0,90,0)); H->StartHandle(); R.StartZ=P.Z; R.StartY=P.X;
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
        if(R.Stage==0) {
            if(S<.5) H->AddMovementInput(FVector(0,1,0));
            else if(S<.75) H->MoveForward(FInputActionValue(1.f));
            R.Climbed|=Move->IsClimbing() && H->GetActorLocation().Z>R.StartZ+25;
            if(S>.85) {UE_LOG(LogTemp,Display,TEXT("MC_CLIMB_POSE climb P=%s V=%s mode=%d"),*H->GetActorLocation().ToString(),*Move->Velocity.ToString(),int32(Move->MovementMode)); R.Stage=1; R.StageAt=Now; R.StartP=H->GetActorLocation();}
        } else if(R.Stage==1) {
            R.Hung|=Move->IsClimbing() && Move->Velocity.Size()<5;
            if(S>1.5) {UE_LOG(LogTemp,Display,TEXT("MC_CLIMB_POSE hang P=%s V=%s mode=%d"),*H->GetActorLocation().ToString(),*Move->Velocity.ToString(),int32(Move->MovementMode)); R.Stage=2; R.StageAt=Now; R.StartP=H->GetActorLocation();}
        } else if(R.Stage==2) {
            if(S<.45) H->MoveRight(FInputActionValue(1.f));
            R.Sideways|=Move->IsClimbing() && FVector::Dist2D(R.StartP,H->GetActorLocation())>25;
            if(S>1) {R.Stage=3; R.StageAt=Now;}
        } else if(R.Stage==3) {
            H->MoveForward(FInputActionValue(1.f)); R.Mantled|=Move->IsMovingOnGround() && H->GetActorLocation().Z>R.StartZ+100;
            if(S>2) { UE_LOG(LogTemp,Display,TEXT("MC_CLIMB_POSE mantle P=%s V=%s mode=%d"),*H->GetActorLocation().ToString(),*Move->Velocity.ToString(),int32(Move->MovementMode)); Check(R.Climbed,TEXT("E and W climb actual gameplay tooth")); Check(R.Hung,TEXT("hang without stamina")); Check(R.Sideways,TEXT("crawl sideways on actual tooth")); Check(R.Mantled,TEXT("mantle onto tooth crown")); R.Stage=4; R.StageAt=Now; }
        } else if(R.Stage==4 && S>1.5) {
            const auto Box=R.Tooth->Body->Bounds.GetBox(); const FVector P(Box.GetCenter().X,Box.Min.Y-H->GetCapsuleComponent()->GetScaledCapsuleRadius()-12,Box.Max.Z-110);
            Place(P,FRotator(0,90,0)); H->StartHandle(); R.Stage=5; R.StageAt=Now;
        } else if(R.Stage==5) {
            if(S>.6 && !R.Jumped) {Check(Move->IsClimbing(),TEXT("reattach before wall jump")); H->Jump(); R.Jumped=true;}
            if(S>2.5) {Check(!Move->IsClimbing(),TEXT("Space jumps away from wall")); Finish();}
        }
    } else if(Case==TEXT("Coffee")) {
        if(T>2 && T<4.5) H->AddMovementInput(FVector(0,1,0));
        if(T>2) View(H->GetActorLocation()+FVector(0,0,35),FVector(-850,-650,520));
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
            else if(S<.15) {
                const FVector P=Ice->Body->Bounds.Origin;
                const float Radius=H->GetCapsuleComponent()->GetScaledCapsuleRadius();
                const FVector Stand=Floor(P+FVector(-Ice->Body->Bounds.BoxExtent.X-Radius-20,0,0))+FVector(0,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3);
                Place(Stand); View(P+FVector(-30,0,40),FVector(-480,-410,310)); H->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
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
        if(T<5) H->SwingBrush();
        else if(R.Stage==0) {
            for(const auto& Tooth:GS->ArenaTeeth) if(Tooth && Tooth->State.ToothId==8) R.Tooth=Tooth;
            if(!R.Tooth.IsValid()) {Check(false,TEXT("actual tooth exists for surface swing")); Finish(); return;}
            const auto Box=R.Tooth->Body->Bounds.GetBox();
            Place(Floor(FVector(Box.GetCenter().X,Box.Min.Y-55,0))+FVector(0,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),FRotator(0,90,0));
            View(H->GetActorLocation()+FVector(0,30,45),FVector(-300,-430,190)); R.Stage=1;
        } else if(T<10) H->SwingBrush();
        if(T>.5 && T<10 && !(T>5 && T<5.5)) {
            TArray<UStaticMeshComponent*> Components; H->GetComponents(Components);
            for(auto* Tool:Components) if(Tool->GetFName()==TEXT("InventoryTool") && Tool->GetStaticMesh()) {
                const auto Bounds=Tool->GetStaticMesh()->GetBounds(); const auto Transform=Tool->GetComponentTransform();
                FCollisionQueryParams Q(SCENE_QUERY_STAT(MCPickApproval),false,H);
                for(int32 I=0;I<8;++I) {
                    const FVector Corner=Transform.TransformPosition(Bounds.Origin+FVector(I&1?Bounds.BoxExtent.X:-Bounds.BoxExtent.X,I&2?Bounds.BoxExtent.Y:-Bounds.BoxExtent.Y,I&4?Bounds.BoxExtent.Z:-Bounds.BoxExtent.Z));
                    FHitResult Hit;
                    if(World->LineTraceSingleByChannel(Hit,Corner+FVector(0,0,220),Corner-FVector(0,0,120),ECC_Visibility,Q) && Hit.ImpactNormal.Z>.4)
                        R.LowestPickClearance=FMath::Min(R.LowestPickClearance,float(FVector::DotProduct(Corner-Hit.ImpactPoint,Hit.ImpactNormal)));
                }
                ++R.PoseSamples;
            }
        }
        if(T>11) {
            UE_LOG(LogTemp,Display,TEXT("MC_PICKAXE_CLEARANCE samples=%d min=%.2f"),R.PoseSamples,R.LowestPickClearance);
            Check(R.PoseSamples>20 && R.LowestPickClearance>-3,TEXT("pickaxe pose stays above tongue and tooth surfaces")); Finish();
        }
    }
    if(T>70) {Check(false,TEXT("approval scenario timeout")); Finish();}
}
#endif
