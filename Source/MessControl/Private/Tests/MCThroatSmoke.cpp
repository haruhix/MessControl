#include "MCValidationSubsystem.h"
#include "MCThroat.h"
#include "MCVomitBurst.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCTongue.h"
#include "MCMouthSurface.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DataTable.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

void UMCValidationSubsystem::TickThroat(float Dt)
{
#if !UE_BUILD_SHIPPING
    Age+=Dt;
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); auto* PC=GetWorld()->GetFirstPlayerController();
    AMCThroat* Throat=nullptr; AMCTongue* Tongue=nullptr;
    for(TActorIterator<AMCThroat> It(GetWorld());It;++It) { Throat=*It; break; }
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) { Tongue=*It; break; }
    if(!GS || !PC || !Throat || !Tongue) { if(Age>60) FPlatformMisc::RequestExitWithStatus(false,1); return; }
    const bool Host=GetWorld()->GetNetMode()!=NM_Client;
    const bool Review=Host && FParse::Param(FCommandLine::Get(),TEXT("MCVomitReview"));
    const bool Capture=Host && (Review || FParse::Param(FCommandLine::Get(),TEXT("MCThroatCapture")));
    TArray<AMCToothCharacter*> Heroes;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(It->GetPlayerState()) Heroes.Add(*It);
    Heroes.Sort([](const AMCToothCharacter& A,const AMCToothCharacter& B){return A.GetPlayerState()->GetPlayerId()<B.GetPlayerState()->GetPlayerId();});
    int32 Expected=4; FParse::Value(FCommandLine::Get(),TEXT("MCExpectedPlayers="),Expected);
    const double Now=GS->GetServerWorldTimeSeconds();
    struct FOrderCheck { TWeakObjectPtr<UWorld> World; bool Prepared=false,Flight=false,Pressed=false,Hopped=false,Invalid=false; float Drop=0,Clearance=MAX_flt; FVector PrepareStart=FVector::ZeroVector; bool Preparing=false; };
    static FOrderCheck Order;
    if(Order.World.Get()!=GetWorld()) { Order=FOrderCheck(); Order.World=GetWorld(); }
    struct FCameraCheck {
        TWeakObjectPtr<UWorld> World; bool Holding=false,Held=false,Returned=false,Invalid=false;
        FVector Eye=FVector::ZeroVector,Start=FVector::ZeroVector; FRotator Rotation=FRotator::ZeroRotator;
        float Drift=0,Travel=0,HoldTime=0;
    };
    static FCameraCheck View;
    if(View.World.Get()!=GetWorld()) { View=FCameraCheck(); View.World=GetWorld(); }
    for(auto* H:Heroes) if(H->IsLocallyControlled()) {
        if(H->SwallowedBy==Throat && H->bMouthCameraHeld) {
            if(!View.Holding) { View.Eye=H->MouthCameraEye; View.Rotation=H->CameraBoom->GetComponentRotation(); View.Start=H->ThroatCaptureStart; }
            View.Holding=true; View.Held=true; View.HoldTime+=Dt;
            View.Travel=FMath::Max(View.Travel,float(FVector::Dist(View.Start,H->GetActorLocation())));
            if(View.HoldTime>.15f) {
                View.Drift=FMath::Max(View.Drift,float(FVector::Dist(H->Camera->GetComponentLocation(),View.Eye)));
                View.Invalid|=!H->CameraBoom->GetComponentRotation().Equals(View.Rotation,.1f) || View.Drift>1;
            }
            View.Invalid|=H->GetCharacterMovement()->MovementMode!=MOVE_None || !H->Status->IsAlive() || H->bInCoffee;
        } else if(View.Holding && !H->SwallowedBy && !H->bMouthCameraHeld && Throat->VomitCount>0) {
            View.Holding=false; View.Returned=true;
            View.Invalid|=!H->Status->IsAlive() || H->GetCapsuleComponent()->GetCollisionEnabled()!=ECollisionEnabled::QueryAndPhysics;
        }
    }
    if(Host && Heroes.Num()) {
        const auto* H=Heroes[0];
        if(H->OrderJumpTarget && !H->bOrderJumpLaunched) {
            if(!Order.Preparing) Order.PrepareStart=H->GetActorLocation();
            Order.Preparing=true; Order.Prepared|=H->AnimationOrderPrepare>.55f;
            const float Drift=FVector::Dist(Order.PrepareStart,H->GetActorLocation());
            if(Drift>3 && !Order.Invalid) UE_LOG(LogTemp,Error,TEXT("MC_UVULA_PREP_DRIFT %.2f delta=%s velocity=%s base=%s stage=%d"),Drift,*(H->GetActorLocation()-Order.PrepareStart).ToString(),*H->GetVelocity().ToString(),*GetNameSafe(H->GetMovementBaseObject()),DevStage);
            Order.Invalid|=Drift>3;
        } else Order.Preparing=false;
        Order.Flight|=H->bOrderJumpLaunched && H->AnimationOrderFlight>.5f && H->GetVelocity().Z>100;
        if(H->bOrderJumpLaunched || H->GetMovementBaseObject()==Throat->UvulaLanding) {
            const float Gap=Throat->UvulaBodyClearance(H->GetActorLocation(),H->GetCapsuleComponent()->GetScaledCapsuleRadius()+6,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+4);
            Order.Clearance=FMath::Min(Order.Clearance,Gap);
            if(Gap<0 && !Order.Invalid) UE_LOG(LogTemp,Error,TEXT("MC_UVULA_BODY_CLEARANCE %.2f"),Gap);
            Order.Invalid|=Gap<0;
        }
        if(H->GetMovementBaseObject()==Throat->UvulaLanding) {
            Order.Pressed|=H->AnimationOrderPress>.6f;
            Order.Drop=FMath::Max(Order.Drop,float(Throat->Uvula->GetRelativeScale3D().Z*100-Throat->UvulaLength));
            const float Feet=H->GetActorLocation().Z-H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            const float Gap=Feet-Throat->UvulaLanding->Bounds.GetBox().Max.Z;
            if(Gap<-3 && !Order.Invalid) UE_LOG(LogTemp,Error,TEXT("MC_UVULA_FEET_GAP %.2f"),Gap);
            Order.Invalid|=Gap<-3;
        }
        Order.Hopped|=Throat->ThroatPhase==EMCThroatPhase::Anticipation && H->GetCharacterMovement()->IsFalling() && H->GetVelocity().Z>100;
    }
    AMCFoodActor* Meal=nullptr; AMCFoodActor* Brush=nullptr; AMCFoodActor* Outside=nullptr;
    for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) {
        if(It->Batch==901) Meal=*It; if(It->Batch==902) Brush=*It; if(It->Batch==903) Outside=*It;
    }
    bool Ready=Heroes.Num()>=Expected && Age>3;
#if WITH_EDITOR
    Ready &= !Capture || !GShaderCompilingManager || !GShaderCompilingManager->IsCompiling();
#endif
    if(Host) {
        GS->Phase=EMCShiftPhase::Intermission; GS->PhaseEndsAt=Now+300;
        if(DevStage==0 && Ready) {
            Throat->ResetSwallow(); Throat->FoodSwallowed=Throat->SwallowCount=Throat->SpasmCount=0;
            for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) It->Dispose();
            auto Spawn=[&](int32 Batch,FVector P,bool Tool) {
                FHitResult Hit; if(Tongue->SurfacePoint(P,Hit)) P=Hit.ImpactPoint+FVector(0,0,40);
                const FTransform Transform(P); auto* F=GetWorld()->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Transform);
                FMCFoodRow Row; Row.Mass=4; Row.Scale=FVector(.75); Row.SpoilSeconds=300; Row.WholeMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))));
                if(!Tool) if(auto* Menu=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu")))
                    if(auto* Broccoli=Menu->FindRow<FMCFoodRow>(TEXT("Broccoli"),TEXT("Throat capture"))) Row=*Broccoli;
                FRandomStream Random(41); F->ConfigureItem(TEXT("ThroatTest"),Row,Random); F->Phase=EMCFoodPhase::Free; F->Batch=Batch;
                if(Tool) F->ConfigureBrush(); F->FinishSpawning(Transform);
                F->Body->SetEnableGravity(false); F->Body->SetSimulatePhysics(false); return F;
            };
            const FVector Center=Throat->GetActorTransform().TransformPosition(Throat->ZoneCenter);
            Meal=Spawn(901,Center+FVector(-55,-65,0),false); Brush=Spawn(902,Center+FVector(0,Throat->ZoneRadius+100,0),true);
            Outside=Spawn(903,Center+FVector(-Throat->ZoneRadius-100,0,0),false);
            for(int32 I=0;I<Heroes.Num();++I) {
                auto* H=Heroes[I]; FHitResult Hit; FVector P=Center+FVector(I==0?-100:-550,-180+I*110,0);
                if(Tongue->SurfacePoint(P,Hit)) P=Hit.ImpactPoint+FVector(0,0,62);
                H->SetActorLocation(P,false,nullptr,ETeleportType::TeleportPhysics); H->GetCharacterMovement()->StopMovementImmediately();
            }
            DevStage=1; DevStartedAt=Now; Throat->ForceNetUpdate();
        }
        if(DevStage==1 && Ready && Now-DevStartedAt>2.5) {
            auto* Hero=Heroes[0]; Hero->ServerOrderJump(Throat);
            if(Hero->OrderJumpTarget) { DevStage=2; UE_LOG(LogTemp,Display,TEXT("MC_THROAT_ORDER_LAUNCH z=%.1f"),Hero->GetActorLocation().Z); }
        }
        if(DevStage==2 && Throat->ThroatPhase==EMCThroatPhase::Anticipation)
            Heroes[0]->AddMovementInput(-Throat->GetActorForwardVector(),1.f);
        if(DevStage==2 && Throat->SwallowCount==1 && Throat->ThroatPhase==EMCThroatPhase::Collecting && (DevSeen&127)==127) {
            bTongueInvalid |= Throat->SpasmCount!=0 || Throat->ContainsPlayer(Heroes[0]);
            const FVector Center=Throat->GetActorTransform().TransformPosition(Throat->ZoneCenter);
            FHitResult Floor; Tongue->SurfacePoint(Center+FVector(-100,0,0),Floor);
            auto* Hero=Heroes[0]; Hero->GetCharacterMovement()->StopMovementImmediately();
            Hero->SetActorLocation(Floor.ImpactPoint+FVector(0,0,62),false,nullptr,ETeleportType::TeleportPhysics);
            // Keep the second meal outside the reset pawn's capsule. The old
            // fixture embedded a broccoli collider and measured depenetration
            // as preparation drift when the character turned toward the uvula.
            Outside->SetActorLocation(Floor.ImpactPoint+FVector(180,160,32)); Outside->ForceNetUpdate();
            DevStage=3; DevStartedAt=Now;
        }
        if(DevStage==3 && Now-DevStartedAt>2) {
            Heroes[0]->ServerOrderJump(Throat); if(Heroes[0]->OrderJumpTarget) DevStage=4;
        }
        if(DevStage==4 && Throat->ThroatPhase==EMCThroatPhase::Anticipation) {
            const FVector Center=Throat->GetActorTransform().TransformPosition(Throat->ZoneCenter);
            for(int32 I=1;I<Heroes.Num();++I) {
                FHitResult Hit; FVector P=Center+FVector(-80,(I-2)*110,0);
                if(Tongue->SurfacePoint(P,Hit)) P=Hit.ImpactPoint+FVector(0,0,62);
                Heroes[I]->SetActorLocation(P,false,nullptr,ETeleportType::TeleportPhysics);
                Heroes[I]->GetCharacterMovement()->StopMovementImmediately(); Heroes[I]->ForceNetUpdate();
            }
            DevStage=5;
        }

    }
    if(Meal && !Meal->IsDisposed() && Throat->ThroatPhase==EMCThroatPhase::Collecting && Throat->FoodInZone>0 && Throat->SwallowCount==0) DevSeen|=1;
    if(Throat->ThroatPhase==EMCThroatPhase::Anticipation) DevSeen|=2;
    if(Throat->ThroatPhase==EMCThroatPhase::Swallowing && Throat->OpenAmount()>.7f) DevSeen|=4;
    if(Meal && Meal->Phase==EMCFoodPhase::Swallowing) DevSeen|=8;
    if(Throat->FoodSwallowed==1) DevSeen|=16;
    if(Throat->SwallowCount==1 && Throat->ThroatPhase==EMCThroatPhase::Collecting && Throat->OpenAmount()==0) DevSeen|=32;
    if(Brush && !Brush->IsDisposed() && Outside && !Outside->IsDisposed() && Throat->FoodSwallowed==1) DevSeen|=64;
    if(Throat->ThroatPhase==EMCThroatPhase::Spasm) DevSeen|=128;
    for(auto* Hero:Heroes) if(Hero->SwallowedBy==Throat) DevSeen|=256;
    if(Throat->SpasmCount==1 && Throat->ThroatPhase==EMCThroatPhase::Recovering && Heroes.Num() && !Heroes[0]->SwallowedBy) DevSeen|=512;
    if(Throat->SpasmCount==1 && Throat->SwallowCount==2 && Throat->ThroatPhase==EMCThroatPhase::Collecting && Throat->FoodSwallowed==1) DevSeen|=1024;
    bTongueInvalid |= Throat->SwallowCount>2 || (Brush && Brush->IsDisposed()) || (Outside && Outside->IsDisposed());
    bTongueInvalid |= !Throat->AuthoredMouth->GetSkeletalMeshAsset();
    if(Throat->ThroatPhase==EMCThroatPhase::Spasm && Now-Throat->PhaseStartedAt>.3)
        bTongueInvalid |= Throat->AuthoredMouth->GetMorphTarget(TEXT("vomit"))<.5f;
    if(Throat->ThroatPhase==EMCThroatPhase::Vomiting) DevSeen|=2048;
    for(TActorIterator<AMCVomitBurst> It(GetWorld());It;++It) if(It->Batch==10000+Throat->MealSequence) {
        if(It->AirborneInstances>0) DevSeen|=4096;
        if(It->SplashInstances>0) DevSeen|=8192;
    }
    if(Throat->VomitCount>0 && Throat->ThroatPhase==EMCThroatPhase::Collecting && (!Review || CoffeeFrame<6)) {
        int32 Puddles=0;
        for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if(It->Batch==10000+Throat->MealSequence && !It->IsClean()) ++Puddles;
        bTongueInvalid |= Puddles!=3;
        if(Puddles==3) DevSeen|=16384;
    }
    if(Capture && Ready) {
        for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) It->Label->SetVisibility(false);
        const FString Folder=Review?FPaths::ProjectDir()/TEXT("Artifacts/Vomit"):FPaths::ProjectSavedDir()/TEXT("ThroatFrames");
        if(!CoffeeCamera) {
            CoffeeCamera=GetWorld()->SpawnActor<ACameraActor>(); CoffeeCamera->GetCameraComponent()->SetFieldOfView(65);
            CoffeeCamera->GetCameraComponent()->SetAspectRatio(1.5f);
            IFileManager::Get().MakeDirectory(*Folder,true); PC->SetViewTarget(CoffeeCamera);
            const FVector Center=Throat->GetActorTransform().TransformPosition(Throat->ZoneCenter);
            const FVector Eye=Center+FVector(-1050,-650,560),Aim=Center+FVector(-180,0,70);
            CoffeeCamera->SetActorLocationAndRotation(Eye,(Aim-Eye).Rotation());
        }
        if(Review) {
            const float PhaseAge=Now-Throat->PhaseStartedAt;
            FString Image;
            if(CoffeeFrame==0 && Throat->ThroatPhase==EMCThroatPhase::Spasm && PhaseAge>.65) Image=TEXT("00_Gag.png");
            if(CoffeeFrame==1 && Throat->ThroatPhase==EMCThroatPhase::Vomiting && PhaseAge>.4) Image=TEXT("01_Airborne.png");
            if(CoffeeFrame==2 && Throat->ThroatPhase==EMCThroatPhase::Vomiting && PhaseAge>.85) Image=TEXT("02_Stream.png");
            if(CoffeeFrame==3 && Throat->ThroatPhase==EMCThroatPhase::Vomiting && PhaseAge>1.12) Image=TEXT("03_Impact.png");
            if(CoffeeFrame==4 && Throat->ThroatPhase==EMCThroatPhase::Vomiting && PhaseAge>1.9) Image=TEXT("04_Splashes.png");
            if(CoffeeFrame==5 && Throat->VomitCount>0 && Throat->ThroatPhase==EMCThroatPhase::Collecting && PhaseAge>3) Image=TEXT("05_Residue.png");
            if(CoffeeFrame>=6 && Heroes.Num()) {
                AMCMouthSurface* Patch=nullptr;
                for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if(It->Batch==10000+Throat->MealSequence && (!Patch || It->GetActorLocation().X>Patch->GetActorLocation().X)) Patch=*It;
                if(Patch) {
                    auto* Hero=Heroes[0]; GS->bPhysicalBrushes=false;
                    Hero->SetActorLocation(Patch->GetActorLocation()+FVector(-65,0,75),false,nullptr,ETeleportType::TeleportPhysics);
                    Hero->SetActorRotation(FRotator::ZeroRotator); Hero->GetCharacterMovement()->StopMovementImmediately();
                    Hero->bBrushing=true; Hero->AdvanceCare(Dt);
                    const FVector Eye=Patch->GetActorLocation()+FVector(-330,-250,240),Aim=Patch->GetActorLocation()+FVector(0,0,20);
                    CoffeeCamera->SetActorLocationAndRotation(Eye,(Aim-Eye).Rotation());
                    if(CoffeeFrame==6 && Patch->RemainingLiquid()<.75f) Image=TEXT("06_BrushTrack.png");
                    if(CoffeeFrame==7 && Patch->IsClean()) { Image=TEXT("07_Clean.png"); CoffeeNextFrame=Now+.6; }
                }
            }
            if(!Image.IsEmpty()) { FScreenshotRequest::RequestScreenshot(Folder/Image,false,false); ++CoffeeFrame; }
        } else if(Now>=CoffeeNextFrame) {
            CoffeeNextFrame=Now+(GetWorld()->GetNetMode()==NM_Standalone?1./30:.1);
            if(CoffeeLastFrame>=0) CoffeeTiming+=FString::Printf(TEXT("duration %.6f\n"),Now-CoffeeLastFrame);
            CoffeeTiming+=FString::Printf(TEXT("file 'Frame%05d.png'\n"),CoffeeFrame);
            FString VideoName; if(!FParse::Value(FCommandLine::Get(),TEXT("MCVideo="),VideoName)) FScreenshotRequest::RequestScreenshot(Folder/FString::Printf(TEXT("Frame%05d.png"),CoffeeFrame),false,false);
            ++CoffeeFrame; CoffeeLastFrame=Now;
        }
    }
    if(Age>=NextLog) {
        NextLog=Age+3;
        UE_LOG(LogTemp,Display,TEXT("MC_THROAT net=%d seen=%d phase=%d staged=%d weight=%.1f swallowed=%d pos=%s"),int32(GetWorld()->GetNetMode()),DevSeen,int32(Throat->ThroatPhase),Throat->FoodInZone,Throat->Weight,Throat->FoodSwallowed,Heroes.Num()?*Heroes[0]->GetActorLocation().ToString():TEXT("none"));
    }
    if(DevSeen==32767 && CoffeeReadyAt<0) CoffeeReadyAt=Now;
    if((CoffeeReadyAt>=0 && (Review?(CoffeeFrame>=8 && Now>CoffeeNextFrame):Now-CoffeeReadyAt>(Host?6:1))) || Age>75) {
        const bool OrderPass=!Host || (Order.Prepared && Order.Flight && Order.Pressed && Order.Hopped && Order.Drop>18 && !Order.Invalid);
        const bool CameraPass=View.Held && View.Returned && View.Travel>300 && !View.Invalid;
        const bool Pass=DevSeen==32767 && !bTongueInvalid && OrderPass && CameraPass;
        UE_LOG(LogTemp,Display,TEXT("MC_THROAT_CAMERA pass=%d held=%d returned=%d travel=%.2f drift=%.3f invalid=%d"),CameraPass,View.Held,View.Returned,View.Travel,View.Drift,View.Invalid);
        UE_LOG(LogTemp,Display,TEXT("MC_UVULA_ANIMATION pass=%d prepare=%d flight=%d press=%d hop=%d drop=%.2f clearance=%.2f invalid=%d"),OrderPass,Order.Prepared,Order.Flight,Order.Pressed,Order.Hopped,Order.Drop,Order.Clearance,Order.Invalid);
        if(Capture) FFileHelper::SaveStringToFile(CoffeeTiming,*(FPaths::ProjectSavedDir()/TEXT("ThroatFrames/times.csv")));
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s THROAT net=%d seen=%d cycles=%d swallowed=%d spasms=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),int32(GetWorld()->GetNetMode()),DevSeen,Throat->SwallowCount,Throat->FoodSwallowed,Throat->SpasmCount);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    }
#endif
}
