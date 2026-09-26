#include "MCValidationSubsystem.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCFoodActor.h"
#include "MCGameState.h"
#include "MCGameMode.h"
#include "MCDayDirector.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

void UMCValidationSubsystem::TickTonguePressure(float Dt)
{
#if !UE_BUILD_SHIPPING
    Age+=Dt;
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); auto* PC=GetWorld()->GetFirstPlayerController();
    AMCTongue* Tongue=nullptr; for (TActorIterator<AMCTongue> It(GetWorld());It;++It) { Tongue=*It; break; }
    if (!GS || !PC || !Tongue) { if (Age>60) FPlatformMisc::RequestExitWithStatus(false,1); return; }
    const bool Host=GetWorld()->GetNetMode()!=NM_Client;
    const bool Capture=Host && FParse::Param(FCommandLine::Get(),TEXT("MCTonguePressureCapture"));
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("TonguePressureFrames");
    auto Finish=[&]()
    {
        const bool Pass=DevSeen==511 && !bTongueInvalid && TongueError<.5;
        if (Capture) FFileHelper::SaveStringToFile(CoffeeTiming,*(Folder/TEXT("times.csv")));
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s PRESSURE net=%d seen=%d collisionError=%.4f invalid=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),int32(GetWorld()->GetNetMode()),DevSeen,TongueError,bTongueInvalid);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    };
    TArray<AMCToothCharacter*> Heroes;
    for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if (It->GetPlayerState()) Heroes.Add(*It);
    Heroes.Sort([](const AMCToothCharacter& A,const AMCToothCharacter& B){return A.GetPlayerState()->GetPlayerId()<B.GetPlayerState()->GetPlayerId();});
    if (Heroes.Num()<4)
    {
        if (Host && DevStartedAt>=0 && GS->GetServerWorldTimeSeconds()-DevStartedAt>21) Finish();
        else if (Age>65) FPlatformMisc::RequestExitWithStatus(false,1);
        return;
    }
    auto Place=[&](AMCToothCharacter* Hero,FVector Point,float Yaw)
    {
        FHitResult Hit; if (!Tongue->SurfacePoint(Point,Hit)) { bTongueInvalid=true; return; }
        Hero->GetCharacterMovement()->StopMovementImmediately();
        Hero->SetActorLocation(Hit.ImpactPoint+FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),false,nullptr,ETeleportType::TeleportPhysics);
        Hero->SetActorRotation(FRotator(0,Yaw,0)); Hero->ForceNetUpdate();
    };
    bool Ready=true;
#if WITH_EDITOR
    Ready=!Capture || !GShaderCompilingManager || !GShaderCompilingManager->IsCompiling();
#endif
    if (Host && DevStage==0)
    {
        GS->PhaseEndsAt=GS->GetServerWorldTimeSeconds()+300;
        for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
            Ready&=It->Get()->GetPawn() && (It->Get()->IsLocalController() || It->Get()->AcknowledgedPawn==It->Get()->GetPawn());
        if (Ready && Age>8)
        {
            GS->bDevManualEvents=true; GS->Phase=EMCShiftPhase::Working; GS->PhaseEndsAt=0;
            GS->DayStartedAt=GS->GetServerWorldTimeSeconds(); GS->ForceNetUpdate();
            auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>();
            if (!Mode->DayDirector) Mode->DayDirector=GetWorld()->SpawnActor<AMCDayDirector>();
            Tongue->ResetPain(); Tongue->ResetPressure(); Tongue->Settings.IdleHeight=0; Tongue->ForceNetUpdate();
            for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) It->Destroy();
            const FVector Points[]={FVector(-100,-380,0),FVector(350,210,0),FVector(-450,220,0),FVector(-530,-210,0)};
            for (int32 I=0;I<4;++I) Place(Heroes[I],Points[I],90);
            for (TActorIterator<AActor> It(GetWorld());It;++It)
                if (auto* Status=It->FindComponentByClass<UMCToothStatusComponent>()) Status->Initialize(Status->State.MaxHealth);
            for (int32 I=0;I<2;++I)
            {
                FHitResult Hit; Tongue->SurfacePoint(FVector(I==0?-200:200,-60,0),Hit);
                auto* Food=GetWorld()->SpawnActor<AMCFoodActor>(Hit.ImpactPoint+FVector(0,0,260),FRotator::ZeroRotator);
                Food->ItemName=I==0?TEXT("PressureLight"):TEXT("PressureHeavy");
                Food->FoodData.Label=FText::FromString(I==0?TEXT("4 KG"):TEXT("28 KG"));
                Food->FoodData.HalfExtent=Food->Body->GetUnscaledBoxExtent();
                Food->FoodData.Mass=Food->Settings.Mass=I==0?4:28; Food->SpoilAt=1e9;
                Food->Body->SetMassOverrideInKg(NAME_None,Food->Settings.Mass,true); Food->ForceNetUpdate();
            }
            if (Capture)
            {
                CoffeeCamera=GetWorld()->SpawnActor<ACameraActor>(FVector(-860,-950,600),FRotator::ZeroRotator);
                CoffeeCamera->SetActorRotation((FVector(50,-50,20)-CoffeeCamera->GetActorLocation()).Rotation());
                CoffeeCamera->GetCameraComponent()->SetFieldOfView(57); PC->SetViewTarget(CoffeeCamera);
                IFileManager::Get().MakeDirectory(*Folder,true);
                for (TActorIterator<AActor> It(GetWorld());It;++It)
                {
                    TArray<UTextRenderComponent*> Labels; It->GetComponents(Labels);
                    for (auto* Label:Labels) Label->SetVisibility(false);
                }
            }
            ++DevStage;
        }
    }
    if (GS->bDevManualEvents && DevStartedAt<0) DevStartedAt=GS->DayStartedAt;
    const float T=DevStartedAt<0?-1:GS->GetServerWorldTimeSeconds()-DevStartedAt;
    AMCFoodActor* Light=nullptr; AMCFoodActor* Heavy=nullptr;
    for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
    { if (It->ItemName==TEXT("PressureLight")) Light=*It; if (It->ItemName==TEXT("PressureHeavy")) Heavy=*It; }
    if (!Light || !Heavy) { if (Age>70) Finish(); return; }
    if (PressureOldPoint.IsNearlyZero() && T>2.5) PressureOldPoint=Heavy->GetActorLocation();
    if (Host)
    {
        if (DevStage==1 && T>4) { Heroes[1]->ToothPhysics->ApplyHit(FVector(0,0,-350),Heroes[1]->GetActorLocation()); ++DevStage; }
        if (DevStage==2 && T>6)
        {
            Light->Body->SetEnableGravity(false); Light->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
            Light->SetActorLocation(Light->GetActorLocation()+FVector(0,0,350),false,nullptr,ETeleportType::TeleportPhysics); Light->ForceNetUpdate(); ++DevStage;
        }
        if (DevStage==3 && T>7)
        {
            for (const int32 I:{0,3})
            {
                Place(Heroes[I],Heavy->GetActorLocation()+FVector(0,I==0?-105:105,0),I==0?90:-90);
                Heroes[I]->bHandling=true; bTongueInvalid|=!Heavy->TryGrab(Heroes[I]); Heroes[I]->ForceNetUpdate();
            }
            ++DevStage;
        }
        if (DevStage==4 && T>12)
        {
            for (const int32 I:{0,3}) { Heavy->Release(Heroes[I]); Heroes[I]->bHandling=false; Heroes[I]->ForceNetUpdate(); }
            ++DevStage;
        }
        if (DevStage==5 && T>14)
        {
            auto* Event=NewObject<UMCTongueMotionProfile>(); Event->Settings.Height=35; Event->Settings.Radius=800;
            Event->Settings.Lift=0; Event->Settings.Push=0; Event->Settings.Redness=.4f;
            bTongueInvalid|=!Tongue->PlayMotion(Event,Heavy->GetActorLocation(),FVector::ForwardVector); ++DevStage;
        }
        if (DevStage==6 && T>19)
        { Tongue->PressureSettings.bEnabled=false; Tongue->ResetPain(); Tongue->ResetPressure(); Tongue->ForceNetUpdate(); ++DevStage; }
    }
    // Input must run on the owning peer; sorted PlayerIds need not put the listen host first.
    for (const int32 I:{0,3})
        if (T>8 && T<12 && Heroes[I]->IsLocallyControlled()) Heroes[I]->AddMovementInput(FVector::ForwardVector,.65f);
    if (T>=0)
    {
        const auto& Sources=Tongue->PressureLoads();
        bool Player=false,Ragdoll=false,LightLoad=false;
        int32 HeavyCount=0;
        for (const auto& S:Sources)
        {
            Player|=S.Kind==EMCTongueLoadKind::Player; Ragdoll|=S.Kind==EMCTongueLoadKind::Ragdoll;
            LightLoad|=S.Actor==Light; HeavyCount+=S.Actor==Heavy?1:0;
            bTongueInvalid|=S.LocalPoint.ContainsNaN() || !FMath::IsFinite(S.Depth);
        }
        if (Player && Tongue->IndentationAt(Heroes[2]->GetActorLocation())>.5) DevSeen|=1;
        const float LD=Tongue->IndentationAt(Light->GetActorLocation()),HD=Tongue->IndentationAt(Heavy->GetActorLocation());
        if (T>2 && T<6 && LD>.5 && HD>LD*1.3) DevSeen|=2;
        if (Ragdoll) DevSeen|=4;
        if (!Heavy->Holders.IsEmpty() && HD>.5 && FVector::Dist2D(PressureOldPoint,Heavy->GetActorLocation())>80) DevSeen|=8;
        if (T>13 && T<19 && (DevSeen&8) && Tongue->IndentationAt(PressureOldPoint)<1) DevSeen|=16;
        if (Tongue->Motion.Serial>0 && GS->GetServerWorldTimeSeconds()>Tongue->Motion.StartedAt+.7 && Player && HD>.5) DevSeen|=64;
        if (T>20 && !Tongue->PressureSettings.bEnabled && Sources.IsEmpty() && Tongue->IndentationAt(Heroes[2]->GetActorLocation())<.01) DevSeen|=128;
        if (T>6.5 && !LightLoad && Light->GetActorLocation().Z>200) DevSeen|=256;
        bTongueInvalid|=HeavyCount>1 || Sources.Num()>Tongue->PressureSettings.MaxSources || HD>Tongue->PressureSettings.MaxDepth+.1;
        const auto& V=Tongue->CurrentVertices(); const auto& Idx=Tongue->TriangleIndices(); int32 Probes=0;
        for (int32 I=0;I+2<Idx.Num();I+=93)
        {
            const FVector P=Tongue->GetActorTransform().TransformPosition((V[Idx[I]]+V[Idx[I+1]]+V[Idx[I+2]])/3);
            if (P.X<-800 || P.X>1000 || FMath::Abs(P.Y)>450) continue;
            FHitResult Hit; if (Tongue->SurfacePoint(P,Hit)) { TongueError=FMath::Max(TongueError,float(FMath::Abs(P.Z-Hit.ImpactPoint.Z))); ++Probes; }
            else bTongueInvalid=true;
        }
        if (Probes>8) DevSeen|=32;
        for (auto* Hero:Heroes) bTongueInvalid|=Hero->GetActorLocation().ContainsNaN() || Hero->GetActorLocation().Z<-250;
        if (Capture && T>=CoffeeNextFrame && T<22)
        {
            const FString Name=FString::Printf(TEXT("Tongue_%04d.png"),CoffeeFrame++);
            FScreenshotRequest::RequestScreenshot(Folder/Name,false,false);
            CoffeeTiming+=FString::Printf(TEXT("%s,%.6f\n"),*Name,T); CoffeeNextFrame=T+.1f;
        }
        if (Age>=NextLog)
        {
            NextLog+=3;
            UE_LOG(LogTemp,Display,TEXT("MC_PRESSURE net=%d t=%.2f seen=%d loads=%d light=%.2f heavy=%.2f drag=%.1f old=%.2f error=%.3f invalid=%d grip=%d hero=%s local=%d speed=%.1f"),int32(GetWorld()->GetNetMode()),T,DevSeen,Sources.Num(),LD,HD,FVector::Dist2D(PressureOldPoint,Heavy->GetActorLocation()),Tongue->IndentationAt(PressureOldPoint),TongueError,bTongueInvalid,Heavy->Holders.Num(),*Heroes[0]->GetActorLocation().ToCompactString(),Heroes[0]->IsLocallyControlled(),Heroes[0]->GetVelocity().Size2D());
        }
    }
    if (T>(Host?26:23) || Age>110) Finish();
#endif
}
