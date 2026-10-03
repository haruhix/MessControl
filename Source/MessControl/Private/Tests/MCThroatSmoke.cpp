#include "MCValidationSubsystem.h"
#include "MCThroat.h"
#include "MCThroatVortex.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCFoodCollectionComponent.h"
#include "MCTongue.h"
#include "MCMouthSurface.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/TextRenderComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/DataTable.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameModeBase.h"
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
    Age+=Dt;auto* GS=GetWorld()->GetGameState<AMCGameState>();auto* PC=GetWorld()->GetFirstPlayerController();
    AMCThroat* Throat=nullptr;AMCTongue* Tongue=nullptr;
    for(TActorIterator<AMCThroat> It(GetWorld());It;++It) {Throat=*It;break;}
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) {Tongue=*It;break;}
    if(!GS || !PC || !Throat || !Tongue) {if(Age>60) FPlatformMisc::RequestExitWithStatus(false,1);return;}
    const bool Host=GetWorld()->GetNetMode()!=NM_Client;
    const bool Review=Host && FParse::Param(FCommandLine::Get(),TEXT("MCVomitReview"));
    const bool Capture=Host && (Review || FParse::Param(FCommandLine::Get(),TEXT("MCThroatCapture")));
    const double Now=GS->GetServerWorldTimeSeconds();
    struct FRun {
        TWeakObjectPtr<UWorld> World;TWeakObjectPtr<AMCToothCharacter> First,Second;
        int32 Stage=0,Seen=0;bool Failed=false,SharedCheck=false,LateEntered=false;
        int32 LastFirstHeld=2,LastSecondHeld=2;
        double At=0,Window=0,SwallowAt=0,LatePreparedAt=0,FinalAt=0;
    };
    static FRun Run;if(Run.World.Get()!=GetWorld()) {Run=FRun();Run.World=GetWorld();}
    auto Check=[&](bool OK,const TCHAR* Text) {
        Run.Failed|=!OK;UE_LOG(LogTemp,Display,TEXT("MC_THROAT_CHECK net=%d %s %s"),int32(GetWorld()->GetNetMode()),OK?TEXT("PASS"):TEXT("FAIL"),Text);
    };
    TArray<AMCToothCharacter*> Heroes;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(It->GetPlayerState()) Heroes.Add(*It);
    Heroes.Sort([](const AMCToothCharacter& A,const AMCToothCharacter& B){return A.GetPlayerState()->GetPlayerId()<B.GetPlayerState()->GetPlayerId();});
    int32 Expected=4;FParse::Value(FCommandLine::Get(),TEXT("MCExpectedPlayers="),Expected);
    bool Ready=Heroes.Num()>=Expected && Age>3;
#if WITH_EDITOR
    Ready &= !Capture || !GShaderCompilingManager || !GShaderCompilingManager->IsCompiling();
#endif
    const FVector Center=Throat->GetActorTransform().TransformPosition(Throat->ZoneCenter);
    auto Place=[&](AMCToothCharacter* H,FVector Offset) {
        FHitResult Hit;FVector P=Center+Offset;
        if(Tongue->SurfacePoint(P,Hit)) P=Hit.ImpactPoint+FVector(0,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3);
        H->SetActorLocationAndRotation(P,Throat->GetActorRotation(),false,nullptr,ETeleportType::TeleportPhysics);
        H->GetCharacterMovement()->StopMovementImmediately();H->GetCharacterMovement()->DisableMovement();H->ForceNetUpdate();
    };
    auto Spawn=[&](int32 Batch,FVector P,bool Tool=false,bool Spoiled=false) {
        const auto* Menu=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
        FMCFoodRow Row;Row.Mass=3;Row.Scale=Row.FragmentScale=FVector(.32,.32,.12);Row.SpoilSeconds=300;
        Row.WholeMeshes={TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")))};Row.FragmentMeshes=Row.WholeMeshes;
        if(!Tool && Menu) if(const auto* Egg=Menu->FindRow<FMCFoodRow>(TEXT("Egg"),TEXT("Throat delivery validation"))) Row=*Egg;
        const FTransform Transform(P);auto* F=GetWorld()->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FRandomStream Random(Batch);F->ConfigureItem(TEXT("Egg"),Row,Random,true);F->Batch=Batch;F->bSpoiled=Spoiled;
        if(Tool) F->ConfigureBrush();F->FinishSpawning(Transform);F->SpoilAt=Now+300;
        F->Body->SetEnableGravity(false);F->Body->SetSimulatePhysics(false);F->ForceNetUpdate();return F;
    };
    auto Stack=[&](AMCToothCharacter* H,int32 Batch,int32 Count) {
        if(!H->FoodCollection->bCollecting) H->FoodCollection->Toggle();
        for(int32 I=0;I<Count;++I) {
            auto* F=Spawn(Batch,H->GetActorLocation()+H->GetActorForwardVector()*105+H->GetActorRightVector()*((I-(Count-1)*.5f)*45)-FVector(0,0,35));
            // A fixed heroZ-35 embedded taller egg variants in the sloped
            // tongue. Weak blocked pickups release silently without a spill.
            const FVector Source=F->GetActorLocation(),Extent=F->Body->GetScaledBoxExtent();
            const FVector Samples[]={FVector::ZeroVector,FVector(Extent.X,0,0),FVector(-Extent.X,0,0),FVector(0,Extent.Y,0),FVector(0,-Extent.Y,0)};
            float FloorZ=-FLT_MAX;int32 Supports=0;
            for(FVector Sample:Samples) {
                FHitResult Hit;if(Tongue->SurfacePoint(Source+Sample,Hit)) {FloorZ=FMath::Max(FloorZ,float(Hit.ImpactPoint.Z));++Supports;}
            }
            Check(Supports==UE_ARRAY_COUNT(Samples),TEXT("the tongue supports the full authored source-food footprint"));
            if(Supports) F->SetActorLocation(FVector(Source.X,Source.Y,FloorZ+Extent.Z+12),false,nullptr,ETeleportType::TeleportPhysics);
            UE_LOG(LogTemp,Display,TEXT("MC_THROAT_SOURCE batch=%d mesh=%s extent=%s center=%s floor=%.2f gap=%.2f"),Batch,*GetNameSafe(F->ItemMesh),*Extent.ToString(),*F->GetActorLocation().ToString(),FloorZ,float(F->GetActorLocation().Z-Extent.Z-FloorZ));
            Check(H->FoodCollection->Collect(F),TEXT("fixture food can be collected outside the delivery zone"));
        }
    };
    AMCFoodActor* Late=nullptr;AMCFoodActor* Brush=nullptr;AMCFoodActor* Outside=nullptr;
    for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) {
        if(It->Batch==93003) Late=*It;if(It->Batch==93004) Brush=*It;if(It->Batch==93005) Outside=*It;
    }
    if(Host && Ready) {
        GS->bDevManualEvents=true;GS->Phase=EMCShiftPhase::Working;GS->PhaseEndsAt=0;GS->bPhysicalBrushes=false;GS->ForceNetUpdate();
        if(auto* Mode=GetWorld()->GetAuthGameMode()) Mode->SetActorTickEnabled(false);
        Tongue->bAutomaticYawns=false;Tongue->ResetPain();
        if(Run.Stage==0) {
            Throat->ResetSwallow();Throat->FoodSwallowed=Throat->SwallowCount=Throat->SpasmCount=Throat->VomitCount=0;
            Throat->AnticipationSeconds=3;Throat->SwallowSeconds=2;
            for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) It->Destroy();
            for(int32 I=0;I<Heroes.Num();++I) {Heroes[I]->CancelGameplayInput();Place(Heroes[I],FVector(-Throat->ZoneRadius-250,-110+I*220,0));}
            Run.First=Heroes[0];Run.Second=Heroes.Num()>1?Heroes[1]:GetWorld()->SpawnActor<AMCToothCharacter>();
            if(Heroes.Num()==1) Place(Run.Second.Get(),FVector(-Throat->ZoneRadius-250,110,0));
            Stack(Run.First.Get(),93001,2);Stack(Run.Second.Get(),93002,2);
            Brush=Spawn(93004,Center+FVector(-30,190,55),true);Outside=Spawn(93005,Center+FVector(-Throat->ZoneRadius-180,Throat->ZoneRadius+600,55));
            Run.At=Now;Run.Stage=1;Throat->ForceNetUpdate();
        }
        if(Run.Stage>0 && Run.Stage<5) {
            auto LogChanges=[&](AMCToothCharacter* H,int32 Batch,int32& Previous) {
                const int32 Held=H->FoodCollection->Pieces.Num();if(Held==Previous) return;Previous=Held;
                UE_LOG(LogTemp,Display,TEXT("MC_THROAT_CARRIER batch=%d held=%d stage=%d collecting=%d fallen=%d location=%s"),Batch,Held,Run.Stage,H->FoodCollection->bCollecting,H->FoodCollection->FallenPieces,*H->GetActorLocation().ToString());
                for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if(It->Batch==Batch) {
                    FHitResult Floor;const bool Supported=Tongue->SurfacePoint(It->GetActorLocation(),Floor);
                    const FVector Extent=FMCFoodStackSettings::RotatedExtent(It->Body->GetScaledBoxExtent(),It->GetActorQuat());
                    UE_LOG(LogTemp,Display,TEXT("MC_THROAT_PIECE batch=%d mesh=%s carrier=%s phase=%d pickup=%d location=%s floorGap=%.2f"),Batch,*GetNameSafe(It->ItemMesh),*GetNameSafe(It->StackCarrier),int32(It->Phase),It->IsStackPickupActive(),*It->GetActorLocation().ToString(),Supported?float(It->GetActorLocation().Z-Extent.Z-Floor.ImpactPoint.Z):MAX_flt);
                }
            };
            LogChanges(Run.First.Get(),93001,Run.LastFirstHeld);LogChanges(Run.Second.Get(),93002,Run.LastSecondHeld);
        }
        if(Run.Stage==1 && Now-Run.At>.7) {
            Check(Run.First->FoodCollection->Pieces.Num()==2 && Run.Second->FoodCollection->Pieces.Num()==2,TEXT("both authored two-piece stacks remain held before entering the delivery zone"));
            Place(Run.First.Get(),FVector(-60,-110,0));Run.Stage=2;
        }
        if(Run.Stage==2 && Throat->ThroatPhase==EMCThroatPhase::Anticipation) {
            Check(Run.First->FoodCollection->Pieces.IsEmpty() && !Run.First->FoodCollection->bCollecting,TEXT("entering the zone delivers the entire stack without a button"));
            Check(Throat->QueuedFoodCount==2 && Throat->SwallowCount==0 && Throat->FoodSwallowed==0,TEXT("first delivery starts a reserved three-second gathering window"));
            Run.Window=Throat->PhaseStartedAt;Run.Stage=3;
        }
        if(Run.Stage==3 && Now-Run.Window>1.25) {Place(Run.Second.Get(),FVector(-60,110,0));Run.Stage=4;}
        if(Run.Stage==4 && Throat->QueuedFoodCount==4 && !Run.SharedCheck) {
            Check(Run.Second->FoodCollection->Pieces.IsEmpty() && !Run.Second->FoodCollection->bCollecting,TEXT("second carrier joins automatically"));
            Check(Throat->PhaseStartedAt==Run.Window,TEXT("teammate delivery keeps the first carrier's original deadline"));Run.SharedCheck=true;
        }
        if(Run.Stage==4 && Throat->ThroatPhase==EMCThroatPhase::Swallowing) {
            Check(Run.SharedCheck && Now-Run.Window>=3 && Now-Run.Window<3.3,TEXT("one four-piece batch starts after the fixed gathering window"));
            Check(Throat->SwallowCount==1 && Throat->FoodSwallowed==0,TEXT("ingredients remain alive while the visible suction plays"));
            Run.SwallowAt=Throat->PhaseStartedAt;Run.Stage=5;
        }
        if(Run.Stage==5 && Now-Run.SwallowAt>.45) {
            Place(Run.First.Get(),FVector(-Throat->ZoneRadius-250,-110,0));Stack(Run.First.Get(),93003,1);
            Run.LatePreparedAt=Now;Run.Stage=6;
        }
        if(Run.Stage==6 && !Run.LateEntered && Now-Run.LatePreparedAt>.55) {
            Check(Run.First->FoodCollection->Pieces.Num()==1 && Late && Late->StackCarrier==Run.First.Get() && !Late->IsStackPickupActive(),TEXT("late delivery enters with a settled carried ingredient during suction"));
            Place(Run.First.Get(),FVector(-60,-110,0));Run.LateEntered=true;
        }
        if(Run.Stage==6 && Throat->FoodSwallowed==4) {
            Check(Now-Run.SwallowAt>=2,TEXT("the full suction event finishes before committing food"));
            Check(Late && !Late->IsDisposed() && Throat->QueuedFoodCount==1,TEXT("a delivery during suction remains queued for the next batch"));Run.Stage=7;
        }
        if(Run.Stage==7 && Throat->FoodSwallowed==5 && Throat->ThroatPhase==EMCThroatPhase::Collecting) {
            Check(Throat->SwallowCount==2 && Brush && !Brush->IsDisposed() && Outside && !Outside->IsDisposed(),TEXT("the next batch consumes the late piece once and excludes tools and outside food"));
            Spawn(93006,Center+FVector(0,0,50),false,true);Run.Stage=8;
        }
        if(Run.Stage==8 && Throat->VomitCount==1 && Throat->ThroatPhase==EMCThroatPhase::Collecting) {
            Check(Throat->SwallowCount==3 && Throat->SpasmCount==1 && Throat->FoodSwallowed==5,TEXT("spoiled food preserves spasm and vomit without consuming it"));
            Throat->SetActorTickEnabled(false);Run.Stage=9;Run.FinalAt=Now;
        }
    }
    if(Throat->ThroatPhase==EMCThroatPhase::Anticipation && Throat->QueuedFoodCount>=2) Run.Seen|=1;
    if(Throat->ThroatPhase==EMCThroatPhase::Anticipation && Throat->QueuedFoodCount==4) Run.Seen|=2;
    if(Throat->ThroatPhase==EMCThroatPhase::Swallowing && Throat->OpenAmount()>.7f) Run.Seen|=4;
    if(Throat->ThroatPhase==EMCThroatPhase::Swallowing && Throat->SwallowCount==1 && Throat->QueuedFoodCount==1) Run.Seen|=8;
    if(Throat->FoodSwallowed==4) Run.Seen|=16;
    if(Throat->ThroatPhase==EMCThroatPhase::Anticipation && Throat->SwallowCount==1 && Throat->FoodSwallowed==4) Run.Seen|=32;
    if(Throat->FoodSwallowed==5 && Throat->SwallowCount==2) Run.Seen|=64;
    if(Throat->ThroatPhase==EMCThroatPhase::Spasm) Run.Seen|=128;
    if(Throat->ThroatPhase==EMCThroatPhase::Vomiting) Run.Seen|=256;
    if(!(Run.Seen&512)) for(TActorIterator<AMCThroatVortex> It(GetWorld());It;++It) {
        if(It->FoodSources.Num()<4 || !FMath::IsNearlyEqual(It->Duration,2.f) || !Tongue->Surface || !Tongue->Surface->IsRegistered()) continue;
        const FBox Bounds=Tongue->Surface->Bounds.GetBox();if(!Bounds.IsValid) continue;
        const FVector Axis=Throat->GetActorForwardVector().GetSafeNormal2D(),Side=FVector::CrossProduct(FVector::UpVector,Axis);
        // These deliveries are beside the mouth: the tongue footprint, rather
        // than the little meal, must determine the replicated field dimensions.
        float ExpectedLength=Throat->ZoneRadius+100,ExpectedHalfWidth=Throat->ZoneRadius;
        for(int32 Corner=0;Corner<8;++Corner) {
            const FVector Point(Corner&1?Bounds.Max.X:Bounds.Min.X,Corner&2?Bounds.Max.Y:Bounds.Min.Y,Corner&4?Bounds.Max.Z:Bounds.Min.Z);
            const FVector Offset=Point-It->GetActorLocation();
            ExpectedLength=FMath::Max(ExpectedLength,float(-FVector::DotProduct(Offset,Axis))+40.f);
            ExpectedHalfWidth=FMath::Max(ExpectedHalfWidth,float(FMath::Abs(FVector::DotProduct(Offset,Side))));
        }
        if(!It->InwardDirection.Equals(Axis,.001) || !FMath::IsNearlyEqual(It->FlowLength,ExpectedLength,1.f)
            || !FMath::IsNearlyEqual(It->FlowHalfWidth,ExpectedHalfWidth,1.f) || !FMath::IsNearlyEqual(It->FlowHeight,260.f,.01f)) continue;
        Run.Seen|=512;
        UE_LOG(LogTemp,Display,TEXT("MC_THROAT_VFX_DIMENSIONS net=%d span=%.2f expected_span=%.2f half_width=%.2f expected_half_width=%.2f height=%.2f sources=%d direction=%s"),
            int32(GetWorld()->GetNetMode()),It->FlowLength,ExpectedLength,It->FlowHalfWidth,ExpectedHalfWidth,It->FlowHeight,It->FoodSources.Num(),*It->InwardDirection.ToString());
        break;
    }
    int32 Puddles=0;for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if(It->Batch==10000+Throat->MealSequence && !It->IsClean()) ++Puddles;
    if(Throat->VomitCount>0 && Puddles==3) Run.Seen|=1024;
    bool Safe=!Heroes.IsEmpty();for(auto* H:Heroes) Safe &= H->Status->IsAlive() && !H->SwallowedBy && H->CanWork();
    if(Safe && Throat->FoodSwallowed==5 && Throat->VomitCount==1) Run.Seen|=2048;
    if(Ready) Run.Failed |= !Safe || (Brush && Brush->IsDisposed()) || (Outside && Outside->IsDisposed()) || Throat->FoodSwallowed>5 || Throat->SwallowCount>3;
    if(Capture && Ready) {
        for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) It->Label->SetVisibility(false);
        const FString Folder=Review?FPaths::ProjectDir()/TEXT("Artifacts/Vomit"):FPaths::ProjectSavedDir()/TEXT("ThroatFrames");
        if(!CoffeeCamera) {
            CoffeeCamera=GetWorld()->SpawnActor<ACameraActor>();CoffeeCamera->GetCameraComponent()->SetFieldOfView(65);CoffeeCamera->GetCameraComponent()->SetAspectRatio(1.5f);
            IFileManager::Get().MakeDirectory(*Folder,true);PC->SetViewTarget(CoffeeCamera);
            const FVector Eye=Center+FVector(-1050,-650,560),Aim=Center+FVector(-120,0,80);CoffeeCamera->SetActorLocationAndRotation(Eye,(Aim-Eye).Rotation());
        }
        if(Review) {
            const float PhaseAge=Now-Throat->PhaseStartedAt;FString Image;
            if(CoffeeFrame==0 && Throat->ThroatPhase==EMCThroatPhase::Spasm && PhaseAge>.65) Image=TEXT("00_Gag.png");
            if(CoffeeFrame==1 && Throat->ThroatPhase==EMCThroatPhase::Vomiting && PhaseAge>.4) Image=TEXT("01_Airborne.png");
            if(CoffeeFrame==2 && Throat->ThroatPhase==EMCThroatPhase::Vomiting && PhaseAge>.85) Image=TEXT("02_Stream.png");
            if(CoffeeFrame==3 && Throat->ThroatPhase==EMCThroatPhase::Vomiting && PhaseAge>1.12) Image=TEXT("03_Impact.png");
            if(CoffeeFrame==4 && Throat->ThroatPhase==EMCThroatPhase::Vomiting && PhaseAge>1.9) Image=TEXT("04_Splashes.png");
            if(CoffeeFrame==5 && Throat->VomitCount>0 && Throat->ThroatPhase==EMCThroatPhase::Collecting && PhaseAge>2) Image=TEXT("05_Residue.png");
            if(!Image.IsEmpty()) {FScreenshotRequest::RequestScreenshot(Folder/Image,false,false);++CoffeeFrame;}
        } else if(Now>=CoffeeNextFrame) {
            CoffeeNextFrame=Now+(GetWorld()->GetNetMode()==NM_Standalone?1./30:.1);
            if(CoffeeLastFrame>=0) CoffeeTiming+=FString::Printf(TEXT("duration %.6f\n"),Now-CoffeeLastFrame);
            CoffeeTiming+=FString::Printf(TEXT("file 'Frame%05d.png'\n"),CoffeeFrame);
            FString VideoName;if(!FParse::Value(FCommandLine::Get(),TEXT("MCVideo="),VideoName)) FScreenshotRequest::RequestScreenshot(Folder/FString::Printf(TEXT("Frame%05d.png"),CoffeeFrame),false,false);
            ++CoffeeFrame;CoffeeLastFrame=Now;
        }
    }
    if(Age>=NextLog) {NextLog=Age+3;UE_LOG(LogTemp,Display,TEXT("MC_THROAT net=%d seen=%d phase=%d queued=%d swallowed=%d cycles=%d"),int32(GetWorld()->GetNetMode()),Run.Seen,int32(Throat->ThroatPhase),Throat->QueuedFoodCount,Throat->FoodSwallowed,Throat->SwallowCount);}
    const bool Finished=Throat->VomitCount==1 && Throat->SwallowCount==3 && Throat->FoodSwallowed==5 && Throat->ThroatPhase==EMCThroatPhase::Collecting;
    if(!Host && Finished && Run.FinalAt==0) Run.FinalAt=Now;
    if((Finished && Run.FinalAt>0 && Now-Run.FinalAt>(Host?4:1) && (!Review || CoffeeFrame>=6)) || Age>75) {
        Check(Run.Seen==4095,TEXT("this peer observed shared delivery, late queue, rich suction and preserved vomit"));const bool Pass=Finished && !Run.Failed;
        if(Capture) FFileHelper::SaveStringToFile(CoffeeTiming,*(FPaths::ProjectSavedDir()/TEXT("ThroatFrames/times.csv")));
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s THROAT net=%d seen=%d cycles=%d swallowed=%d spasms=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),int32(GetWorld()->GetNetMode()),Run.Seen,Throat->SwallowCount,Throat->FoodSwallowed,Throat->SpasmCount);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    }
#endif
}
