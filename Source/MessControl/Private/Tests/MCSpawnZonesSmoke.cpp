#if !UE_BUILD_SHIPPING
#include "MCTongue.h"
#include "MCDayDirector.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCFoodActor.h"
#include "MCMouthSurface.h"
#include "MCToothCharacter.h"
#include "MCThroat.h"
#include "Components/BoxComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace
{
bool SpawnReviewPointAllowed(UWorld* World,const AMCTongue* Tongue,FVector Point)
{
    const FBox B=Tongue->Surface->Bounds.GetBox();
    const double Depth=(B.Max.X-Point.X)/B.GetSize().X;
    if(Depth<Tongue->GameplaySpawnNearDepth-1.e-5 || Depth>Tongue->GameplaySpawnFarDepth+1.e-5) return false;
    FHitResult Floor;
    if(!Tongue->SurfacePoint(Point,Floor)) return false;
    for(TActorIterator<AMCFoodDisposal> It(World);It;++It)
        if(It->ContainsDeliveryPosition(Floor.ImpactPoint+FVector(0,0,35))) return false;
    return true;
}

bool SpawnReviewFootprintAllowed(UWorld* World,const AMCTongue* Tongue,FVector Center,float Radius)
{
    if(!SpawnReviewPointAllowed(World,Tongue,Center)) return false;
    for(int32 I=0;I<64;++I) {
        const double Angle=I*2*PI/64;
        if(!SpawnReviewPointAllowed(World,Tongue,Center+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Radius)) return false;
    }
    return true;
}
}

// Opt-in actual-map check. Production food/coating APIs run unchanged; only
// their finished actors are frozen afterward to make the footprint review clear.
void MCTickSpawnZonesValidation(UWorld* World)
{
    struct FRun
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AMCTongue> Tongue;
        TWeakObjectPtr<ACameraActor> Camera;
        TArray<TWeakObjectPtr<AMCMouthSurface>> Patches;
        double StartedAt=-1;
        int32 Food[2]={0,0},Coffee[2]={0,0},Probes[2]={0,0},Forbidden=0,Skipped=0,LiquidVertices=0;
        bool Failed=false,GeometryChecked=false,Overview=false,Oblique=false,ObliqueShot=false,Finished=false;
        FString CSV=TEXT("kind,zone,x,y,radius\n");
    };
    static FRun R;
    if(R.World.Get()!=World) {R=FRun();R.World=World;}
    if(R.Finished || !World || World->GetNetMode()!=NM_Standalone) return;
    auto* State=World->GetGameState<AMCGameState>();
    auto* Mode=World->GetAuthGameMode<AMCGameMode>();
    auto* PC=World->GetFirstPlayerController();
    auto* Hero=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    auto Check=[&](bool OK,const TCHAR* Text) {
        R.Failed|=!OK;UE_LOG(LogTemp,Display,TEXT("MC_SPAWN_ZONES_CHECK %s %s"),OK?TEXT("PASS"):TEXT("FAIL"),Text);
    };
    auto Finish=[&]() {
        R.Finished=true;
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s SPAWN_ZONES food=%d/%d coffee=%d/%d probes=%d/%d forbidden=%d skipped=%d liquid_vertices=%d"),
            R.Failed?TEXT("FAIL"):TEXT("PASS"),R.Food[0],R.Food[1],R.Coffee[0],R.Coffee[1],R.Probes[0],R.Probes[1],R.Forbidden,R.Skipped,R.LiquidVertices);
        FPlatformMisc::RequestExitWithStatus(false,R.Failed?1:0);
    };
    if(World->GetTimeSeconds()>180) {Check(false,TEXT("Actual-map spawn review timed out"));Finish();return;}
    if(!State || !Mode || !PC || !Hero || World->GetTimeSeconds()<3) return;
#if WITH_EDITOR
    if(GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
    const bool Capture=FParse::Param(FCommandLine::Get(),TEXT("MCSpawnZonesCapture"));
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("SpawnZoneReview");
    if(R.StartedAt<0)
    {
        AMCTongue* Tongue=nullptr;
        for(TActorIterator<AMCTongue> It(World);It;++It) {Tongue=*It;break;}
        if(!Tongue) {Check(false,TEXT("L_Mouth contains a tongue"));Finish();return;}
        R.Tongue=Tongue;R.StartedAt=World->GetTimeSeconds();
        Mode->SetActorTickEnabled(false);
        for(TActorIterator<AMCDayDirector> It(World);It;++It) It->SetActorTickEnabled(false);
        Hero->CancelGameplayInput();Hero->DropFood();
        Tongue->bAutomaticYawns=false;Tongue->ResetPain();Tongue->ResetYawn();Tongue->SetActorTickEnabled(false);
        for(TActorIterator<AMCThroat> It(World);It;++It) {It->ResetSwallow();It->SetActorTickEnabled(false);}
        auto* Plan=LoadObject<UMCDayPlan>(nullptr,TEXT("/Game/Data/DA_Day01.DA_Day01"));
        if(!Plan) {Check(false,TEXT("Saved day plan is available"));Finish();return;}
        auto* Director=IsValid(Mode->DayDirector)?Mode->DayDirector.Get():World->SpawnActor<AMCDayDirector>();
        Mode->DayDirector=Director;Director->Start(Plan,0,true);Director->SetActorTickEnabled(false);
        State->Phase=EMCShiftPhase::Working;State->PhaseEndsAt=0;State->bDevManualEvents=true;
        for(TActorIterator<AMCFoodActor> It(World);It;++It) It->Destroy();
        for(TActorIterator<AMCMouthSurface> It(World);It;++It) It->Destroy();
        int32 Caps=0;
        for(TActorIterator<AMCFoodDisposal> It(World);It;++It) {
            TArray<FVector> Outer,Inner;if(It->GetDeliveryZoneOutline(Outer,Inner)) ++Caps;
        }
        Check(Caps>=2,TEXT("Both real delivery caps participate in the map check"));
        FRandomStream Random(4107);
        for(int32 I=0;I<2048;++I) {
            FHitResult Hit;int32 Zone=-1;
            if(!Tongue->RandomGameplaySpawnPoint(Random,40,0,{},Hit,&Zone)) {++R.Skipped;continue;}
            if(Zone>=0 && Zone<2) ++R.Probes[Zone];else ++R.Forbidden;
            if(!SpawnReviewFootprintAllowed(World,Tongue,Hit.ImpactPoint,40)) ++R.Forbidden;
        }
        const int32 ProbeCount=R.Probes[0]+R.Probes[1];
        Check(ProbeCount>2000,TEXT("Small footprints remain available in both weighted regions"));
        Check(ProbeCount>0 && FMath::Abs(float(R.Probes[0])/ProbeCount-Tongue->GameplaySpawnLeftChance)<.04f,TEXT("Actual-map sampler preserves the configured 30/70 weights"));
        for(int32 I=0;I<24;++I) {
            auto* Food=Director->SpawnMenuFoodDrop(650,2);
            if(!Food) {++R.Skipped;continue;}
            const FVector Spawn=Food->GetActorLocation();const int32 Zone=Tongue->GameplaySpawnZone(Spawn);
            const float Radius=Food->Body->GetScaledBoxExtent().Size2D()+20;
            if(Zone>=0 && Zone<2) ++R.Food[Zone];else ++R.Forbidden;
            if(!SpawnReviewFootprintAllowed(World,Tongue,Spawn,Radius)) ++R.Forbidden;
            R.CSV+=FString::Printf(TEXT("food,%d,%.3f,%.3f,%.3f\n"),Zone,Spawn.X,Spawn.Y,Radius);
            Food->Body->SetSimulatePhysics(false);Food->SetActorTickEnabled(false);
            FHitResult Floor;
            if(Tongue->SurfacePoint(Spawn,Floor)) Food->SetActorLocation(Floor.ImpactPoint+FVector(0,0,Food->Body->GetScaledBoxExtent().Z+5),false,nullptr,ETeleportType::TeleportPhysics);
        }
        Mode->ExecuteDevAction(PC,EMCDevAction::SpicyPepper);
        int32 PepperCount=0;
        for(TActorIterator<AMCFoodActor> It(World);It;++It) if(It->Batch==12000 && It->FoodData.Kind==EMCFoodKind::Spicy) {
            ++PepperCount;const FVector Spawn=It->GetActorLocation();
            const float Radius=It->Body->GetScaledBoxExtent().Size2D()+20;
            if(!SpawnReviewFootprintAllowed(World,Tongue,Spawn,Radius)) ++R.Forbidden;
            R.CSV+=FString::Printf(TEXT("pepper,%d,%.3f,%.3f,%.3f\n"),Tongue->GameplaySpawnZone(Spawn),Spawn.X,Spawn.Y,Radius);
            It->Body->SetSimulatePhysics(false);It->SetActorTickEnabled(false);
        }
        Check(PepperCount>0,TEXT("The F3 pepper action also respects the gameplay spawn zones"));
        Director->DirtyMouth(true);
        for(TActorIterator<AMCMouthSurface> It(World);It;++It) {
            if(It->bUlcer) continue;
            const FVector Spawn=It->GetActorLocation();const int32 Zone=Tongue->GameplaySpawnZone(Spawn);
            const float Radius=It->LiquidHalfSize*1.415f+40;
            if(Zone>=0 && Zone<2) ++R.Coffee[Zone];else ++R.Forbidden;
            if(!SpawnReviewFootprintAllowed(World,Tongue,Spawn,Radius)) ++R.Forbidden;
            R.Patches.Add(*It);R.CSV+=FString::Printf(TEXT("coffee,%d,%.3f,%.3f,%.3f\n"),Zone,Spawn.X,Spawn.Y,Radius);
        }
        Check(R.Food[0]+R.Food[1]>=8,TEXT("Production menu drops create a reviewable group of food"));
        Check(R.Coffee[0]+R.Coffee[1]>=3,TEXT("Production coffee creates a reviewable group of stains"));
        Check(R.Forbidden==0,TEXT("Every actual spawn and reserved footprint stays outside both forbidden strips and delivery caps"));
        IFileManager::Get().MakeDirectory(*Folder,true);FFileHelper::SaveStringToFile(R.CSV,*(Folder/TEXT("Placements.csv")));
        if(Capture) {
            // These two files belong to this opt-in capture. Remove earlier
            // output so a previous run cannot satisfy this run's render check.
            IFileManager::Get().Delete(*(Folder/TEXT("01_Overview.png")));
            IFileManager::Get().Delete(*(Folder/TEXT("02_Oblique.png")));
            const FBox B=Tongue->Surface->Bounds.GetBox();const FVector Focus=B.GetCenter();
            const double Height=FMath::Max(B.GetSize().X,B.GetSize().Y*16/9)*.5/FMath::Tan(FMath::DegreesToRadians(30.))*1.12;
            R.Camera=World->SpawnActor<ACameraActor>();R.Camera->SetActorLocationAndRotation(Focus+FVector(0,0,Height),FRotator(-90,90,0));
            R.Camera->GetCameraComponent()->SetFieldOfView(60);PC->SetViewTarget(R.Camera.Get());
            for(float Depth:{Tongue->GameplaySpawnNearDepth,Tongue->GameplaySpawnSplitDepth,Tongue->GameplaySpawnFarDepth}) {
                FHitResult Before;bool HasBefore=false;
                for(int32 I=0;I<=64;++I) {
                    FHitResult Now;const FVector P(B.Max.X-B.GetSize().X*Depth,FMath::Lerp(B.Min.Y,B.Max.Y,I/64.),B.Max.Z);
                    if(!Tongue->SurfacePoint(P,Now)) {HasBefore=false;continue;}
                    if(HasBefore) DrawDebugLine(World,Before.ImpactPoint+FVector(0,0,12),Now.ImpactPoint+FVector(0,0,12),Depth==Tongue->GameplaySpawnSplitDepth?FColor::Cyan:FColor::Magenta,true,-1,0,4);
                    Before=Now;HasBefore=true;
                }
            }
        }
        return;
    }
    const double Age=World->GetTimeSeconds()-R.StartedAt;
    if(Age>1 && !R.GeometryChecked) {
        R.GeometryChecked=true;
        for(const auto& Weak:R.Patches) {
            auto* Patch=Weak.Get();const auto* Section=Patch?Patch->Liquid->GetProcMeshSection(0):nullptr;
            if(!Section || Section->ProcIndexBuffer.IsEmpty()) {Check(false,TEXT("Coffee has actual rendered liquid geometry"));continue;}
            for(const FProcMeshVertex& Vertex:Section->ProcVertexBuffer) {
                ++R.LiquidVertices;
                if(!SpawnReviewPointAllowed(World,R.Tongue.Get(),Patch->Liquid->GetComponentTransform().TransformPosition(Vertex.Position))) ++R.Forbidden;
            }
        }
        Check(R.LiquidVertices>0 && R.Forbidden==0,TEXT("Rendered coffee mesh vertices also remain within allowed tissue"));
    }
    if(Capture && Age>2 && !R.Overview) {FScreenshotRequest::RequestScreenshot(Folder/TEXT("01_Overview.png"),false,false);R.Overview=true;return;}
    if(Capture && Age>3 && !R.Oblique) {
        const FVector Focus=R.Tongue->Surface->Bounds.Origin,Offset(0,-900,3100);
        R.Camera->SetActorLocationAndRotation(Focus+Offset,(-Offset).Rotation());R.Camera->GetCameraComponent()->SetFieldOfView(80);
        R.Oblique=true;return;
    }
    if(Capture && Age>4 && R.Oblique && !R.ObliqueShot) {
        FScreenshotRequest::RequestScreenshot(Folder/TEXT("02_Oblique.png"),false,false);R.ObliqueShot=true;return;
    }
    if(Age>6) {
        if(Capture) Check(IFileManager::Get().FileExists(*(Folder/TEXT("01_Overview.png"))) && IFileManager::Get().FileExists(*(Folder/TEXT("02_Oblique.png"))),TEXT("Actual rendered map review frames were saved"));
        Finish();
    }
}
#endif
