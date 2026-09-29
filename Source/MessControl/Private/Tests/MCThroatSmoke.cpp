#include "MCValidationSubsystem.h"
#include "MCThroat.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCTongue.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/DataTable.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
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
    const bool Capture=Host && FParse::Param(FCommandLine::Get(),TEXT("MCThroatCapture"));
    TArray<AMCToothCharacter*> Heroes;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(It->GetPlayerState()) Heroes.Add(*It);
    Heroes.Sort([](const AMCToothCharacter& A,const AMCToothCharacter& B){return A.GetPlayerState()->GetPlayerId()<B.GetPlayerState()->GetPlayerId();});
    int32 Expected=4; FParse::Value(FCommandLine::Get(),TEXT("MCExpectedPlayers="),Expected);
    const double Now=GS->GetServerWorldTimeSeconds();
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
            Meal=Spawn(901,Center+FVector(-55,-65,0),false); Brush=Spawn(902,Center+FVector(0,100,0),true);
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
            Outside->SetActorLocation(Floor.ImpactPoint+FVector(80,70,32)); Outside->ForceNetUpdate();
            DevStage=3; DevStartedAt=Now;
        }
        if(DevStage==3 && Now-DevStartedAt>2) {
            Heroes[0]->ServerOrderJump(Throat); if(Heroes[0]->OrderJumpTarget) DevStage=4;
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
    if(Capture && Ready) {
        for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) It->Label->SetVisibility(false);
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("ThroatFrames");
        if(!CoffeeCamera) {
            CoffeeCamera=GetWorld()->SpawnActor<ACameraActor>(); CoffeeCamera->GetCameraComponent()->SetFieldOfView(68);
            CoffeeCamera->GetCameraComponent()->SetAspectRatio(1.5f);
            IFileManager::Get().MakeDirectory(*Folder,true); PC->SetViewTarget(CoffeeCamera);
            const FVector Eye(-850,0,620),Aim(800,0,-250);
            CoffeeCamera->SetActorLocationAndRotation(Eye,(Aim-Eye).Rotation());
        }
        if(Now>=CoffeeNextFrame) {
            CoffeeNextFrame=Now+(GetWorld()->GetNetMode()==NM_Standalone?1./30:.1);
            if(CoffeeLastFrame>=0) CoffeeTiming+=FString::Printf(TEXT("duration %.6f\n"),Now-CoffeeLastFrame);
            CoffeeTiming+=FString::Printf(TEXT("file 'Frame%05d.png'\n"),CoffeeFrame);
            FScreenshotRequest::RequestScreenshot(Folder/FString::Printf(TEXT("Frame%05d.png"),CoffeeFrame++),false,false); CoffeeLastFrame=Now;
        }
    }
    if(Age>=NextLog) {
        NextLog=Age+3;
        UE_LOG(LogTemp,Display,TEXT("MC_THROAT net=%d seen=%d phase=%d staged=%d weight=%.1f swallowed=%d pos=%s"),int32(GetWorld()->GetNetMode()),DevSeen,int32(Throat->ThroatPhase),Throat->FoodInZone,Throat->Weight,Throat->FoodSwallowed,Heroes.Num()?*Heroes[0]->GetActorLocation().ToString():TEXT("none"));
    }
    if(DevSeen==2047 && CoffeeReadyAt<0) CoffeeReadyAt=Now;
    if((CoffeeReadyAt>=0 && Now-CoffeeReadyAt>(Host?6:1)) || Age>75) {
        const bool Pass=DevSeen==2047 && !bTongueInvalid;
        if(Capture) FFileHelper::SaveStringToFile(CoffeeTiming,*(FPaths::ProjectSavedDir()/TEXT("ThroatFrames/times.csv")));
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s THROAT net=%d seen=%d cycles=%d swallowed=%d spasms=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),int32(GetWorld()->GetNetMode()),DevSeen,Throat->SwallowCount,Throat->FoodSwallowed,Throat->SpasmCount);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    }
#endif
}
