#include "CoreMinimal.h"
#if !UE_BUILD_SHIPPING
#include "MCToothCharacter.h"
#include "MCBrushContactComponent.h"
#include "MCToothStatusComponent.h"
#include "MCArenaTooth.h"
#include "MCArenaToothSocket.h"
#include "MCMouthSurface.h"
#include "MCGameState.h"
#include "MCTongue.h"
#include "MCFoodActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

// Stand still and actually hold LMB until every gameplay tooth, then two tongue stains,
// is clean. No moving the avatar to chase the remaining high/low samples.
void MCTickBrushCoverage(UWorld* World)
{
    struct FRun {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AMCToothCharacter> Hero;
        TWeakObjectPtr<AMCMouthSurface> Patch;
        TWeakObjectPtr<ACameraActor> Camera;
        int32 Index=-1,Passed=0,Frames=0,Contacts=0,Scan=0;
        float Age=0,Started=0,MinZ=MAX_flt,MaxZ=-MAX_flt,MaxError=0,MinClearance=MAX_flt,MaxStretch=1,NextFrame=0,NextLog=0;
        FVector Start=FVector::ZeroVector; bool Invalid=false;
    };
    static FRun R;
    if(R.World.Get()!=World) { R=FRun(); R.World=World; }
    R.Age+=World->GetDeltaSeconds();
    auto* GS=World->GetGameState<AMCGameState>(); auto* PC=World->GetFirstPlayerController();
    if(!GS || !PC || World->GetNetMode()!=NM_Standalone) return;
    auto* H=Cast<AMCToothCharacter>(PC->GetPawn()); if(!H) return;
    AMCTongue* Tongue=nullptr; for(TActorIterator<AMCTongue> It(World);It;++It) { Tongue=*It; break; }
    if(!Tongue) return;
    GS->Phase=EMCShiftPhase::Intermission; GS->PhaseEndsAt=World->GetTimeSeconds()+300; GS->bPhysicalBrushes=false;
    const bool Capture=FParse::Param(FCommandLine::Get(),TEXT("MCBrushCapture"));
    FString ReviewFolder;
    const bool Review=FParse::Value(FCommandLine::Get(),TEXT("MCBrushReviewFolder="),ReviewFolder);
    TArray<AMCArenaTooth*> Teeth;
    for(AMCArenaTooth* Tooth:GS->ArenaTeeth) if(IsValid(Tooth) && Tooth->IsAvailable()) Teeth.Add(Tooth);
    Teeth.Sort([](const AMCArenaTooth& A,const AMCArenaTooth& B){return A.State.ToothId<B.State.ToothId;});
    if(R.Age<4 || Teeth.IsEmpty()) return;
    if(R.Index<0) {
        // Exercise every gameplay socket, including inactive reserves if authored.
        for(TActorIterator<AMCArenaToothSocket> It(World);It;++It) {
            if(Teeth.ContainsByPredicate([&](const AMCArenaTooth* T){return T->State.ToothId==It->ToothId;})) continue;
            UStaticMesh* Mesh=It->Preview->GetStaticMesh(); if(!Mesh) continue;
            const FTransform P=It->Preview->GetComponentTransform();
            const FTransform T(P.GetRotation(),P.TransformPosition(Mesh->GetBounds().Origin));
            auto* Tooth=World->SpawnActorDeferred<AMCArenaTooth>(AMCArenaTooth::StaticClass(),T);
            Tooth->SetAppearance(Mesh,P.GetScale3D()); Tooth->Initialize(It->ToothId,FMCArenaToothSettings()); Tooth->FinishSpawning(T);
            GS->ArenaTeeth.Add(Tooth); Teeth.Add(Tooth);
        }
        Teeth.Sort([](const AMCArenaTooth& A,const AMCArenaTooth& B){return A.State.ToothId<B.State.ToothId;});
        for(TActorIterator<AMCFoodActor> It(World);It;++It) It->Dispose();
        for(TActorIterator<AMCMouthSurface> It(World);It;++It) It->Destroy();
        for(auto* Tooth:Teeth) Tooth->SetCoffee(0);
        R.Index=0; R.Hero=H;
        if(Capture || Review) {
            R.Camera=World->SpawnActor<ACameraActor>(); R.Camera->GetCameraComponent()->SetFieldOfView(52);
            R.Camera->GetCameraComponent()->SetAspectRatio(1.5); PC->SetViewTarget(R.Camera.Get());
            IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("BrushCoverageFrames")),true);
        }
    }
    if(R.Started==0) {
        H->ServerSetPrimary(false); H->GetCharacterMovement()->StopMovementImmediately(); H->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        FVector P,Inward=FVector(-1,0,0),Aim;
        if(R.Index<Teeth.Num()) {
            auto* Tooth=Teeth[R.Index]; Tooth->SetCoffee(1);
            Inward=(-Tooth->GetActorLocation()).GetSafeNormal2D();
            const FVector E=Tooth->Visual->Bounds.BoxExtent;
            const float Radius=FMath::Abs(Inward.X)*E.X+FMath::Abs(Inward.Y)*E.Y;
            P=Tooth->GetActorLocation()+Inward*(Radius+65); Aim=Tooth->GetActorLocation();
            FHitResult Face; FCollisionQueryParams Query(SCENE_QUERY_STAT(MCBrushFixture),true);
            if(Tooth->BrushSurface->LineTraceComponent(Face,Aim+Inward*600,Aim-Inward*200,Query)) P=Face.ImpactPoint+Inward*65;
            // Older fixtures could teleport inside the padded tooth/gum collider,
            // allowing a brush position that a walking player can never occupy.
            FCollisionQueryParams Room(SCENE_QUERY_STAT(MCBrushFixtureRoom),false,H);
            bool Free=false;
            for(int32 Step=0;Step<20;++Step) {
                FHitResult Floor;
                if(!Tongue->SurfacePoint(P,Floor)) break;
                P.Z=Floor.ImpactPoint.Z+H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+4;
                if(!World->OverlapBlockingTestByProfile(P,FQuat::Identity,H->GetCapsuleComponent()->GetCollisionProfileName(),H->GetCapsuleComponent()->GetCollisionShape(),Room)) { Free=true; break; }
                P+=Inward*10;
            }
            R.Invalid|=!Free;
            UE_LOG(LogTemp,Display,TEXT("MC_BRUSH_APPROACH case=%d capsuleFree=%d position=%s"),R.Index,Free,*P.ToString());
        } else {
            const FVector Center(-450,(R.Index-Teeth.Num())*300-150,0);
            FHitResult Floor; if(!Tongue->SurfacePoint(Center,Floor)) return;
            const FTransform Spawn(Floor.ImpactPoint+Floor.ImpactNormal*5);
            auto* Patch=World->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),Spawn);
            Patch->bRandomizeLiquidSize=false; Patch->LiquidHalfSize=R.Index==Teeth.Num()?92:220;
            Patch->FinishSpawning(Spawn); Patch->Status->ApplyCoffee(); R.Patch=Patch;
            P=Floor.ImpactPoint+FVector(-25,0,0); Aim=Floor.ImpactPoint;
        }
        FHitResult Floor; if(!Tongue->SurfacePoint(P,Floor)) return;
        P.Z=Floor.ImpactPoint.Z+H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+4;
        H->SetActorLocationAndRotation(P,(-Inward).Rotation(),false,nullptr,ETeleportType::TeleportPhysics);
        R.Start=P; R.Started=R.Age; R.MinZ=MAX_flt; R.MaxZ=-MAX_flt; R.MaxError=0; R.MinClearance=MAX_flt; R.MaxStretch=1; R.Contacts=0; R.Scan=0;
        if(R.Camera.IsValid()) {
            Aim=(Aim+P)*.5; const FVector Eye=Aim+Inward*340+FVector::CrossProduct(Inward,FVector::UpVector)*220+FVector(0,0,170);
            R.Camera->SetActorLocationAndRotation(Eye,(Aim-Eye).Rotation());
        }
        // This subsystem runs after skeletal evaluation. Let the regular pawn
        // and brush ticks evaluate the teleported pose before measuring it.
        return;
    }
    const float T=R.Age-R.Started;
    if(R.Index>=Teeth.Num() && int32(T/5)>R.Scan) {
        // A floor stain surrounds the standing pawn. Exercise each facing,
        // rather than requiring the brush to clean behind the player's back.
        R.Scan=int32(T/5);
        H->SetActorRotation(FRotator(0,H->GetActorRotation().Yaw+90,0));
        // The fixture's instant turn moves attached tools after BuildPose ran.
        // Measure the next evaluated pose, with the same collision limits.
        return;
    }
    H->ServerSetPrimary(T>.7f);
    auto* Brush=H->BrushContact.Get();
    if(R.Age>R.NextLog) {
        R.NextLog=R.Age+3;
        FVector Point=FVector::ZeroVector,Normal=FVector::UpVector;
        const bool Found=R.Index<Teeth.Num()?Teeth[R.Index]->FindDirtyContact(H,Point,Normal):R.Patch->FindDirtyContact(H,Point,Normal);
        UE_LOG(LogTemp,Display,TEXT("MC_BRUSH_COVERAGE_PROGRESS case=%d found=%d working=%d alpha=%.2f hero=%s point=%s"),R.Index,Found,H->bBrushing,Brush->Alpha(),*H->GetActorLocation().ToString(),*Point.ToString());
    }
    if(Brush->Alpha()>.99f && H->bBrushing) {
        if(Brush->IsTouchingSurface()) ++R.Contacts;
        const FVector Point=Brush->ContactPoint(),N=Brush->ContactNormal();
        R.MinZ=FMath::Min(R.MinZ,float(Point.Z)); R.MaxZ=FMath::Max(R.MaxZ,float(Point.Z));
        if(Brush->IsTouchingSurface()) R.MaxError=FMath::Max(R.MaxError,float(FVector::Dist(Point,Brush->BristlePoint())));
        const auto* Mesh=H->GetMesh(); const auto& Ref=Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
        auto Rest=[&](FName Role){ FTransform P=FTransform::Identity; for(int32 I=Ref.FindBoneIndex(H->RigBone(Role));I>=0;I=Ref.GetParentIndex(I)) P=P*Ref.GetRefBonePose()[I]; return Mesh->GetComponentTransform().TransformPosition(P.GetLocation()); };
        const FVector Hand=Mesh->GetSocketLocation(H->RigBone(TEXT("hand_r"))),Lower=Mesh->GetSocketLocation(H->RigBone(TEXT("forearm_r")));
        R.MaxStretch=FMath::Max(R.MaxStretch,float(FVector::Dist(Hand,Lower)/FVector::Dist(Rest(TEXT("hand_r")),Rest(TEXT("forearm_r")))));
        const FBox Box=H->Brush->GetStaticMesh()->GetBoundingBox();
        for(int32 I=0;I<8;++I) {
            const FVector Corner(I&1?Box.Max.X:Box.Min.X,I&2?Box.Max.Y:Box.Min.Y,I&4?Box.Max.Z:Box.Min.Z);
            const FVector P=H->Brush->GetComponentTransform().TransformPosition(Corner);
            R.MinClearance=FMath::Min(R.MinClearance,float(FVector::DotProduct(P-Point,N)));
        }
    }
    const bool Clean=R.Index<Teeth.Num()?!Teeth[R.Index]->Status->NeedsCare(true):R.Patch.IsValid() && R.Patch->IsClean();
    if(Capture && R.Age>=R.NextFrame && (R.Index==0 || R.Index>=Teeth.Num())) {
        R.NextFrame=R.Age+1.f/30;
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BrushCoverageFrames")/FString::Printf(TEXT("Frame%05d.png"),R.Frames++),false,false);
    }
    if(R.Camera.IsValid() && Review) {
        if(FMath::Abs(T-1.f)<World->GetDeltaSeconds()*.5f || (Clean && T>1)) {
            IFileManager::Get().MakeDirectory(*ReviewFolder,true);
            FScreenshotRequest::RequestScreenshot(ReviewFolder/FString::Printf(TEXT("Tooth%02d_%s.png"),R.Index,Clean?TEXT("Clean"):TEXT("Dirty")),false,false);
        }
    }
    if(Clean || T>24) {
        const float Left=R.Index<Teeth.Num()?Teeth[R.Index]->RemainingGrime():R.Patch->RemainingLiquid();
        const bool Pass=Clean && R.Contacts>0 && R.MinClearance>=-2 && R.MaxStretch<1.05f && FVector::Dist2D(R.Start,H->GetActorLocation())<3;
        UE_LOG(LogTemp,Display,TEXT("MC_BRUSH_COVERAGE case=%d pass=%d left=%.4f time=%.2f height=%.1f error=%.2f clearance=%.2f stretch=%.3f contacts=%d"),R.Index,Pass,Left,T,R.MaxZ-R.MinZ,R.MaxError,R.MinClearance,R.MaxStretch,R.Contacts);
        R.Invalid|=!Pass; R.Passed+=Pass; ++R.Index; R.Started=0;
        H->ServerSetPrimary(false); if(R.Patch.IsValid()) { R.Patch->Destroy(); R.Patch.Reset(); }
        if(R.Index>=Teeth.Num()+2) {
            UE_LOG(LogTemp,Display,TEXT("MC_BRUSH_COVERAGE_%s cases=%d/%d"),R.Invalid?TEXT("FAIL"):TEXT("PASS"),R.Passed,R.Index);
            FPlatformMisc::RequestExitWithStatus(false,R.Invalid?1:0);
        }
    }
}
#endif
