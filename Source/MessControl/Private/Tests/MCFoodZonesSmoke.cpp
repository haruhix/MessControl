#include "MCFoodZonesSmoke.h"

#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCGripComponent.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCDayDirector.h"
#include "MCInventoryComponent.h"
#include "MCPlayerState.h"
#include "MCThroat.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCTongueMotion.h"
#include "ProceduralMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

bool UMCFoodZonesSmoke::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
    return (FParse::Param(FCommandLine::Get(),TEXT("MCFoodZonesSmoke"))
        || FParse::Param(FCommandLine::Get(),TEXT("MCFoodZonesCloseup"))) && Super::ShouldCreateSubsystem(Outer);
#else
    return false;
#endif
}

void UMCFoodZonesSmoke::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    StartedAt=FPlatformTime::Seconds();
    bCloseup=FParse::Param(FCommandLine::Get(),TEXT("MCFoodZonesCloseup"));
}

void UMCFoodZonesSmoke::Fail(const TCHAR* Reason)
{
    bFinished=true;
    UE_LOG(LogTemp,Error,TEXT("MC_FOOD_ZONES_FAIL case=%d stage=%d reason=%s hero=%s held=%d"),
        Case,Stage,Reason,Hero?*Hero->GetActorLocation().ToString():TEXT("missing"),Hero?Hero->FoodCollection->Pieces.Num():-1);
    FPlatformMisc::RequestExitWithStatus(false,1);
}

bool UMCFoodZonesSmoke::PrepareCloseupView()
{
    AMCFoodDisposal* Zone=Case<2?static_cast<AMCFoodDisposal*>(Green.Get()):Red.Get();
    TArray<FVector> Outer,Inner;
    if(!Zone->GetDeliveryZoneOutline(Outer,Inner) || Outer.Num()<3 || Outer.Num()!=Inner.Num()) {
        Fail(TEXT("Closeup requires the authored tongue cap contour"));return false;
    }
    const bool Grazing=(Case%2)==1;
    const float ContourIndex=(Outer.Num()-1)*(Grazing?.25f:.42f);
    const int32 Index=FMath::Min(FMath::FloorToInt(ContourIndex),Outer.Num()-2);
    const FVector Rim=FMath::Lerp(Outer[Index],Outer[Index+1],ContourIndex-Index);
    const FVector Edge=FMath::Lerp(Inner[Index],Inner[Index+1],ContourIndex-Index);
    CloseupTarget=FMath::Lerp(Rim,Edge,.5f);
    FHitResult Floor;
    if(!Tongue->SurfacePoint(CloseupTarget,Floor)) {Fail(TEXT("Closeup cap target has no real tongue support"));return false;}
    CloseupTarget.Z=Floor.ImpactPoint.Z;
    Direction=Zone->GetDeliveryDirection();
    const FVector Across=FVector::CrossProduct(FVector::UpVector,Direction);
    const FVector Eye=CloseupTarget-Direction*(Grazing?1000:750)+Across*(Grazing?300:180)+FVector(0,0,Grazing?360:760);
    const FVector Aim=CloseupTarget+FVector(0,0,65);
    CloseupCamera->SetActorLocationAndRotation(Eye,(Aim-Eye).Rotation());
    CloseupCamera->GetCameraComponent()->SetFieldOfView(Grazing?65:75);
    GetWorld()->GetFirstPlayerController()->SetViewTarget(CloseupCamera);
    // Idle starts without a load in the cap. Subsequent phases use this real, supported pawn.
    FVector Away=Tongue->Surface->Bounds.Origin;
    if(!Tongue->SurfacePoint(Away,Floor)) {Fail(TEXT("Closeup waiting position has no real tongue support"));return false;}
    Away.Z=Floor.ImpactPoint.Z+Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3;
    Hero->SetActorLocation(Away,false,nullptr,ETeleportType::TeleportPhysics);
    Hero->GetCharacterMovement()->StopMovementImmediately();
    Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Tongue->ResetPain();Tongue->ResetYawn();Tongue->ResetPressure();
    Stage=1;StageAt=GetWorld()->GetTimeSeconds();
    UE_LOG(LogTemp,Display,TEXT("MC_FOOD_ZONES_CLOSEUP_VIEW view=%d role=%s grazing=%d eye=%s target=%s"),
        Case,Case<2?TEXT("green"):TEXT("red"),Grazing,*Eye.ToString(),*CloseupTarget.ToString());
    return true;
}

bool UMCFoodZonesSmoke::AuditCloseupGuide(const TCHAR* PhaseName)
{
    AMCFoodDisposal* Zone=Case<2?static_cast<AMCFoodDisposal*>(Green.Get()):Red.Get();
    const auto& NativeIndices=Tongue->TriangleIndices();
    TArray<FVector> NativeWorld;NativeWorld.Reserve(Tongue->CurrentVertices().Num());
    const FTransform NativeTransform=Tongue->Surface->GetComponentTransform();
    for(const FVector Vertex:Tongue->CurrentVertices()) NativeWorld.Add(NativeTransform.TransformPosition(Vertex));
    auto Cross=[](FVector2D A,FVector2D B){return A.X*B.Y-A.Y*B.X;};
    struct FFace {
        FVector A,B,C;
        FVector2D AB,AC;
        FVector Alpha=FVector::ZeroVector;
        double Det=0;
        int32 Triangle=0;
    };
    auto Cell=[](FVector Point){return FIntPoint(FMath::FloorToInt(Point.X/128.),FMath::FloorToInt(Point.Y/128.));};
    auto AddCells=[&](const FFace& Face,int32 Index,TMap<FIntPoint,TArray<int32>>& Grid) {
        const FIntPoint First=Cell(FVector(FMath::Min3(Face.A.X,Face.B.X,Face.C.X),FMath::Min3(Face.A.Y,Face.B.Y,Face.C.Y),0));
        const FIntPoint Last=Cell(FVector(FMath::Max3(Face.A.X,Face.B.X,Face.C.X),FMath::Max3(Face.A.Y,Face.B.Y,Face.C.Y),0));
        for(int32 X=First.X;X<=Last.X;++X) for(int32 Y=First.Y;Y<=Last.Y;++Y) Grid.FindOrAdd(FIntPoint(X,Y)).Add(Index);
    };
    auto Weights=[&](const FFace& Face,FVector Point) {
        const FVector2D AP(Point.X-Face.A.X,Point.Y-Face.A.Y);
        const double WB=Cross(AP,Face.AC)/Face.Det,WC=Cross(Face.AB,AP)/Face.Det;
        return FVector(1-WB-WC,WB,WC);
    };
    auto Contains=[&](const FFace& Face,FVector Point,FVector& Bary) {
        Bary=Weights(Face,Point);return FMath::Min3(Bary.X,Bary.Y,Bary.Z)>=-1.e-4;
    };
    auto Height=[](const FFace& Face,FVector Bary){return Face.A.Z*Bary.X+Face.B.Z*Bary.Y+Face.C.Z*Bary.Z;};
    TArray<FFace> NativeFaces;TMap<FIntPoint,TArray<int32>> NativeGrid;
    for(int32 NativeIndex=0;NativeIndex+2<NativeIndices.Num();NativeIndex+=3) {
        const int32 IA=NativeIndices[NativeIndex],IB=NativeIndices[NativeIndex+1],IC=NativeIndices[NativeIndex+2];
        if(!NativeWorld.IsValidIndex(IA) || !NativeWorld.IsValidIndex(IB) || !NativeWorld.IsValidIndex(IC)) continue;
        FFace Face;Face.A=NativeWorld[IA];Face.B=NativeWorld[IB];Face.C=NativeWorld[IC];Face.Triangle=NativeIndex/3;
        Face.AB=FVector2D(Face.B.X-Face.A.X,Face.B.Y-Face.A.Y);Face.AC=FVector2D(Face.C.X-Face.A.X,Face.C.Y-Face.A.Y);
        Face.Det=Cross(Face.AB,Face.AC);
        // Native clockwise faces point upwards; a folded upper surface can overlap XY.
        if(Face.Det>=-1.e-5) continue;
        AddCells(Face,NativeFaces.Add(Face),NativeGrid);
    }
    TArray<UProceduralMeshComponent*> Meshes;Zone->GetComponents(Meshes);
    int32 AuditedMeshes=0;
    for(UProceduralMeshComponent* Mesh:Meshes) {
        if(Mesh->GetFName()!=TEXT("DeliveryFloorGuide") && Mesh->GetFName()!=TEXT("DeliveryArrowGuide")) continue;
        auto* Section=Mesh->GetProcMeshSection(0);
        if(!Section || Section->ProcIndexBuffer.IsEmpty() || !Mesh->IsVisible() || Mesh->GetCollisionEnabled()!=ECollisionEnabled::NoCollision) {
            Fail(TEXT("Closeup delivery guide is missing, hidden or has gameplay collision"));return false;
        }
        ++AuditedMeshes;
        const int32 TriangleCount=Section->ProcIndexBuffer.Num()/3;
        const int32 Stride=FMath::Max(1,TriangleCount/900);
        const bool Arrow=Mesh->GetFName()==TEXT("DeliveryArrowGuide");
        TArray<FFace> GuideFaces;TMap<FIntPoint,TArray<int32>> GuideGrid;
        for(int32 Triangle=0;Triangle<TriangleCount;++Triangle) {
            FVector V[3],Alpha;bool Visible=false;
            for(int32 Corner=0;Corner<3;++Corner) {
                const auto& Vertex=Section->ProcVertexBuffer[Section->ProcIndexBuffer[Triangle*3+Corner]];
                V[Corner]=Mesh->GetComponentTransform().TransformPosition(Vertex.Position);
                Alpha[Corner]=Vertex.Color.A/255.;Visible|=Vertex.Color.A>2;
            }
            if(!Visible) continue;
            FFace Face;Face.A=V[0];Face.B=V[1];Face.C=V[2];Face.Alpha=Alpha;Face.Triangle=Triangle;
            Face.AB=FVector2D(Face.B.X-Face.A.X,Face.B.Y-Face.A.Y);Face.AC=FVector2D(Face.C.X-Face.A.X,Face.C.Y-Face.A.Y);
            Face.Det=Cross(Face.AB,Face.AC);if(FMath::Abs(Face.Det)<1.e-9) continue;
            AddCells(Face,GuideFaces.Add(Face),GuideGrid);
        }
        auto Attached=[&](const FFace& Guide,int32& SourceFace) {
            const FVector Centroid=(Guide.A+Guide.B+Guide.C)/3.;
            const auto* Candidates=NativeGrid.Find(Cell(Centroid));if(!Candidates) return false;
            const FVector Points[]={Guide.A,Guide.B,Guide.C};
            for(int32 Candidate:*Candidates) {
                const FFace& Native=NativeFaces[Candidate];double Gap[3];bool Fits=true;
                for(int32 Corner=0;Corner<3;++Corner) {
                    FVector Bary;
                    if(!Contains(Native,Points[Corner],Bary)) {Fits=false;break;}
                    Gap[Corner]=Points[Corner].Z-Height(Native,Bary);
                }
                if(!Fits || FMath::Max3(Gap[0],Gap[1],Gap[2])-FMath::Min3(Gap[0],Gap[1],Gap[2])>.15) continue;
                const auto IsLift=[&](double Lift){return FMath::Abs(Gap[0]-Lift)<=.15 && FMath::Abs(Gap[1]-Lift)<=.15 && FMath::Abs(Gap[2]-Lift)<=.15;};
                if(Arrow?IsLift(6):(IsLift(2) || IsLift(3))) {SourceFace=Native.Triangle;return true;}
            }
            return false;
        };
        int32 AttachedCount=0,AttachmentFailures=0,Supported=0,Missing=0,MissingCentroids=0,Below=0,TopCoverageFailures=0,Diagnosed=0;
        double MinGap=DBL_MAX;
        for(const FFace& Guide:GuideFaces) {
            if((Guide.Triangle%Stride)!=0) continue;
            int32 SourceFace=INDEX_NONE;
            if(Attached(Guide,SourceFace)) ++AttachedCount;
            else {
                ++AttachmentFailures;
                if(AttachmentFailures<=8) UE_LOG(LogTemp,Display,TEXT("MC_FOOD_ZONES_CLOSEUP_ATTACHMENT_FAIL view=%d phase=%s mesh=%s triangle=%d a=%s b=%s c=%s expected_lift=%s"),
                    Case,PhaseName,*Mesh->GetName(),Guide.Triangle,*Guide.A.ToString(),*Guide.B.ToString(),*Guide.C.ToString(),Arrow?TEXT("6"):TEXT("2_or_3"));
            }
            const FVector Samples[]={Guide.A,Guide.B,Guide.C,(Guide.A+Guide.B)*.5,(Guide.B+Guide.C)*.5,(Guide.C+Guide.A)*.5,(Guide.A+Guide.B+Guide.C)/3.};
            const double SampleAlpha[]={Guide.Alpha.X,Guide.Alpha.Y,Guide.Alpha.Z,(Guide.Alpha.X+Guide.Alpha.Y)*.5,
                (Guide.Alpha.Y+Guide.Alpha.Z)*.5,(Guide.Alpha.Z+Guide.Alpha.X)*.5,(Guide.Alpha.X+Guide.Alpha.Y+Guide.Alpha.Z)/3.};
            for(int32 SampleIndex=0;SampleIndex<UE_ARRAY_COUNT(Samples);++SampleIndex) {
                const FVector Point=Samples[SampleIndex];
                FHitResult Hit;
                if(!Tongue->SurfacePoint(Point,Hit)) {
                    ++Missing;if(SampleIndex==6) {
                        ++MissingCentroids;
                        if(MissingCentroids<=8) UE_LOG(LogTemp,Display,TEXT("MC_FOOD_ZONES_CLOSEUP_CENTROID_MISSING view=%d phase=%s mesh=%s triangle=%d point=%s source_face=%d"),
                            Case,PhaseName,*Mesh->GetName(),Guide.Triangle,*Point.ToString(),SourceFace);
                    }
                    continue;
                }
                ++Supported;const double Gap=Point.Z-Hit.ImpactPoint.Z;MinGap=FMath::Min(MinGap,Gap);
                if(Gap< -1.) {
                    ++Below;
                    const bool CoverageRequired=SampleAlpha[SampleIndex]>2./255.;
                    bool Covered=false;int32 CoverTriangle=INDEX_NONE;double CoverGap=-DBL_MAX;
                    if(CoverageRequired) if(const auto* Candidates=GuideGrid.Find(Cell(Point))) for(int32 Candidate:*Candidates) {
                        const FFace& Cover=GuideFaces[Candidate];FVector Bary;
                        if(!Contains(Cover,Point,Bary) || FVector::DotProduct(Cover.Alpha,Bary)<=2./255.) continue;
                        const double CandidateGap=Height(Cover,Bary)-Hit.ImpactPoint.Z;
                        if(CandidateGap<(Arrow?5.:1.)) continue;
                        int32 CoverSource=INDEX_NONE;if(!Attached(Cover,CoverSource)) continue;
                        Covered=true;CoverTriangle=Cover.Triangle;CoverGap=CandidateGap;break;
                    }
                    if(CoverageRequired && !Covered) ++TopCoverageFailures;
                    if(Diagnosed<8) {
                        ++Diagnosed;
                        UE_LOG(LogTemp,Display,TEXT("MC_FOOD_ZONES_CLOSEUP_OCCLUDED_LOWER view=%d phase=%s mesh=%s overlay_triangle=%d sample=%d source_face=%d point=%s hit=%s hit_face=%d top_gap=%.6f sample_alpha=%.6f coverage_required=%d top_covered=%d cover_triangle=%d cover_gap=%.6f"),
                            Case,PhaseName,*Mesh->GetName(),Guide.Triangle,SampleIndex,SourceFace,*Point.ToString(),*Hit.ImpactPoint.ToString(),Hit.FaceIndex,Gap,SampleAlpha[SampleIndex],CoverageRequired,Covered,CoverTriangle,CoverGap);
                    }
                }
            }
        }
        UE_LOG(LogTemp,Display,TEXT("MC_FOOD_ZONES_CLOSEUP_GEOMETRY view=%d phase=%s mesh=%s triangles=%d attached=%d attachment_failures=%d supported=%d missing_border=%d missing_centroids=%d occluded_lower=%d top_coverage_failures=%d min_top_gap=%.3f"),
            Case,PhaseName,*Mesh->GetName(),TriangleCount,AttachedCount,AttachmentFailures,Supported,Missing-MissingCentroids,MissingCentroids,Below,TopCoverageFailures,MinGap);
        if(AttachedCount==0 || AttachmentFailures>0) {Fail(TEXT("A closeup guide triangle is not attached to one current native tongue face at its authored lift"));return false;}
        if(Supported==0 || MissingCentroids>0) {Fail(TEXT("A closeup guide centroid has no real tongue support"));return false;}
        if(TopCoverageFailures>0) {Fail(TEXT("A folded upper tongue face lacks visible delivery guide coverage"));return false;}
    }
    if(AuditedMeshes!=2) {Fail(TEXT("Both closeup floor and arrow guides are required"));return false;}
    return true;
}

void UMCFoodZonesSmoke::CaptureCloseup(const TCHAR* PhaseName)
{
    if(!AuditCloseupGuide(PhaseName)) return;
    auto* Controller=GetWorld()->GetFirstPlayerController();
    const FVector2D Pixels[]={FVector2D(800,316),FVector2D(650,300),FVector2D(930,330)};
    for(const FVector2D Pixel:Pixels) {
        FVector Origin,DirectionRay;
        if(!Controller->DeprojectScreenPositionToWorld(Pixel.X,Pixel.Y,Origin,DirectionRay)) {
            UE_LOG(LogTemp,Display,TEXT("MC_FOOD_ZONES_CLOSEUP_RAY view=%d phase=%s pixel=%.0f,%.0f deprojection_failed=1"),Case,PhaseName,Pixel.X,Pixel.Y);
            continue;
        }
        const FVector End=Origin+DirectionRay*10000;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(MCFoodZonesCloseupRay),true);Params.bReturnFaceIndex=true;
        FHitResult NativeHit,SceneHit;
        const bool Native=Tongue->Surface->LineTraceComponent(NativeHit,Origin,End,Params);
        const bool Scene=GetWorld()->LineTraceSingleByChannel(SceneHit,Origin,End,ECC_Visibility,Params);
        UE_LOG(LogTemp,Display,TEXT("MC_FOOD_ZONES_CLOSEUP_RAY view=%d phase=%s pixel=%.0f,%.0f origin=%s direction=%s native=%d native_point=%s native_face=%d native_distance=%.3f scene=%d scene_actor=%s scene_component=%s scene_point=%s scene_face=%d scene_distance=%.3f hide_throat_art=%d"),
            Case,PhaseName,Pixel.X,Pixel.Y,*Origin.ToString(),*DirectionRay.ToString(),Native,*NativeHit.ImpactPoint.ToString(),NativeHit.FaceIndex,NativeHit.Distance,
            Scene,*GetNameSafe(SceneHit.GetActor()),*GetNameSafe(SceneHit.GetComponent()),*SceneHit.ImpactPoint.ToString(),SceneHit.FaceIndex,SceneHit.Distance,
            FParse::Param(FCommandLine::Get(),TEXT("MCFoodZonesHideThroatArt")));
    }
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("FoodZonesCloseup");
    IFileManager::Get().MakeDirectory(*Folder,true);
    const FString Name=FString::Printf(TEXT("%02d_%s_%s_%s.png"),Case,Case<2?TEXT("Green"):TEXT("Red"),
        (Case%2)==1?TEXT("RimGrazing"):TEXT("CloseOblique"),PhaseName);
    FScreenshotRequest::RequestScreenshot(Folder/Name,true,false);
    UE_LOG(LogTemp,Display,TEXT("MC_FOOD_ZONES_CLOSEUP_CAPTURE %s"),*Name);
}

void UMCFoodZonesSmoke::TickCloseup()
{
    if(Stage==0) {PrepareCloseupView();return;}
    const double Now=GetWorld()->GetTimeSeconds(),Age=Now-StageAt;
    if(Stage==1 && Age>1.2) {
        CaptureCloseup(TEXT("Idle"));if(bFinished) return;
        Stage=10;StageAt=Now;
    } else if(Stage==10 && Age>.05 && !FScreenshotRequest::IsScreenshotRequested()) {
        // The requested frame must render before moving a load into the idle cap.
        FHitResult Floor;
        if(!Tongue->SurfacePoint(CloseupTarget,Floor)) {Fail(TEXT("Native pressure test has no real support"));return;}
        const FVector Position=Floor.ImpactPoint+FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3);
        Hero->SetActorLocationAndRotation(Position,Direction.Rotation(),false,nullptr,ETeleportType::TeleportPhysics);
        Hero->GetCharacterMovement()->StopMovementImmediately();Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        Stage=2;StageAt=Now;
    } else if(Stage==2 && Age>1.2) {
        const float Depth=Tongue->IndentationAt(Hero->GetActorLocation());
        if(Depth<=.05f || Tongue->PressureLoads().IsEmpty()) {Fail(TEXT("Closeup pressure phase never loaded the tongue through the real supported pawn"));return;}
        UE_LOG(LogTemp,Display,TEXT("MC_FOOD_ZONES_CLOSEUP_PRESSURE view=%d depth=%.3f sources=%d"),Case,Depth,Tongue->PressureLoads().Num());
        CaptureCloseup(TEXT("PlayerPressure"));if(bFinished) return;
        Stage=11;StageAt=Now;
    } else if(Stage==11 && Age>.05 && !FScreenshotRequest::IsScreenshotRequested()) {
        // Keep the pressure-only frame stable until the screenshot is consumed.
        auto* Wave=NewObject<UMCTongueMotionProfile>(this);
        Wave->Settings.Shape=EMCTongueShape::DirectionalWave;Wave->Settings.Height=180;Wave->Settings.Radius=2000;
        Wave->Settings.Width=320;Wave->Settings.Speed=600;Wave->Settings.Anticipation=.25f;
        Wave->Settings.Lift=0;Wave->Settings.Push=0;Wave->Settings.RestAfter=0;Wave->Settings.Redness=0;
        if(!Tongue->PlayMotion(Wave,CloseupTarget-Direction*500,Direction)) {Fail(TEXT("Closeup native wave did not start"));return;}
        Stage=3;StageAt=Now;
    } else if(Stage==3 && Now-Tongue->Motion.StartedAt>1.08) {
        CaptureCloseup(TEXT("WaveCrestNear"));if(bFinished) return;
        Stage=4;StageAt=Now;
    } else if(Stage==4 && Now-Tongue->Motion.StartedAt>1.45) {
        CaptureCloseup(TEXT("WaveCrestRim"));if(bFinished) return;
        Stage=5;StageAt=Now;
    } else if(Stage==5 && Age>.5 && !FScreenshotRequest::IsScreenshotRequested()) {
        ++Case;Stage=0;
        if(Case==4) {
            bFinished=true;
            UE_LOG(LogTemp,Display,TEXT("MC_FOOD_ZONES_CLOSEUP_PASS four in-mouth green/red close/grazing views x idle, real pawn pressure and two native directional wave phases; render review required"));
            FPlatformMisc::RequestExitWithStatus(false,0);
        }
    }
}

void UMCFoodZonesSmoke::Capture(const TCHAR* Name)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("MCFoodZonesCapture"))) return;
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("FoodZonesSmoke");
    IFileManager::Get().MakeDirectory(*Folder,true);
    FScreenshotRequest::RequestScreenshot(Folder/FString(Name)+TEXT(".png"),true,false);
}

bool UMCFoodZonesSmoke::SpawnPiece(int32 Index,bool Wrong,bool Spoiled)
{
    const auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
    const auto* SavedRow=Table?Table->FindRow<FMCFoodRow>(TEXT("Egg"),TEXT("Delivery zone smoke")):nullptr;
    if(!SavedRow) { Fail(TEXT("Saved breakfast egg row is missing")); return false; }
    FMCFoodRow Row=*SavedRow;
    Row.Kind=Wrong?EMCFoodKind::ForeignObject:EMCFoodKind::Food;
    Row.SpoilSeconds=300;
    if(Case==4) {
        // A small foreign object uses the existing two-hand/rigid-body grip, rather than a food stack.
        Row.WholeMeshes={TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")))};
        Row.FragmentMeshes=Row.WholeMeshes;Row.Scale=Row.FragmentScale=FVector(.32,.32,.12);Row.CollisionData.Reset();
    }
    const int32 Count=Case==0?3:Case==3?2:1;
    FVector Position=Hero->GetActorLocation()+Direction*(Case>=4?68:110)+FVector::CrossProduct(FVector::UpVector,Direction)*((Index-(Count-1)*.5f)*45);
    const FTransform Pose(Position);
    auto* Piece=GetWorld()->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Piece) { Fail(TEXT("Cannot spawn a saved food fragment")); return false; }
    FRandomStream Random(41+Index);
    Piece->ConfigureItem(TEXT("Egg"),Row,Random,true);
    Piece->Batch=93080+Case;Piece->bSpoiled=Spoiled;
    Piece->FinishSpawning(Pose);
    const FVector Extent=Piece->Body->GetScaledBoxExtent();
    float Highest=-FLT_MAX;
    const FVector Samples[]={FVector::ZeroVector,FVector(Extent.X,0,0),FVector(-Extent.X,0,0),FVector(0,Extent.Y,0),FVector(0,-Extent.Y,0)};
    for(const FVector Sample:Samples) {
        FHitResult Floor;
        if(!Tongue->SurfacePoint(Position+Sample,Floor)) {
            Piece->Destroy();Fail(TEXT("Source food footprint has no real tongue support"));return false;
        }
        Highest=FMath::Max(Highest,float(Floor.ImpactPoint.Z));
    }
    Position.Z=Highest+Extent.Z+12;
    Piece->SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);
    Piece->SpoilAt=GetWorld()->GetTimeSeconds()+300;
    Piece->ForceNetUpdate();Food.Add(Piece);
    return true;
}

bool UMCFoodZonesSmoke::PrepareCase()
{
    Hero->FoodCollection->Stop(false);
    for(AMCFoodActor* Piece:Food) if(IsValid(Piece)) Piece->Destroy();
    Food.Empty();
    AMCFoodDisposal* Zone=Case==3?static_cast<AMCFoodDisposal*>(Green.Get()):Red.Get();
    FTransform ZonePose;FVector Half;bool Circular=false;
    Zone->GetDeliveryZoneGeometry(ZonePose,Half,Circular);
    Direction=Zone->GetDeliveryDirection();
    const float Depth=Half.X*FMath::Abs(float(ZonePose.GetScale3D().X));
    Start=ZonePose.GetLocation()-Direction*(Depth+300);
    // Enter the playable inner portion of the zone. The Q case releases before the player enters.
    Goal=ZonePose.GetLocation()-Direction*((Case==1 || Case==4)?Depth+155:Depth-65);
    FHitResult StartFloor,GoalFloor;
    if(!Tongue->SurfacePoint(Start,StartFloor) || !Tongue->SurfacePoint(Goal,GoalFloor)) {
        Fail(TEXT("A saved delivery zone is unreachable from the real tongue"));return false;
    }
    const float Height=Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3;
    Start.Z=StartFloor.ImpactPoint.Z+Height;Goal.Z=GoalFloor.ImpactPoint.Z+Height;
    Hero->CancelGameplayInput();Hero->bInCoffee=false;
    Hero->SetActorLocationAndRotation(Start,Direction.Rotation(),false,nullptr,ETeleportType::TeleportPhysics);
    Hero->GetCharacterMovement()->StopMovementImmediately();
    Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    if(Zone->ContainsDeliveryPosition(Hero->GetActorLocation())) {
        Fail(TEXT("Collection fixture starts inside a delivery zone"));return false;
    }
    const int32 Count=Case==5?0:Case==0?3:Case==3?2:1;
    for(int32 Index=0;Index<Count;++Index)
        if(!SpawnPiece(Index,Case==4,(Case==0 && Index<2) || Case==1)) return false;
    InitialPoints=Hero->GetPlayerState<AMCPlayerState>()->Points;
    InitialSwallowed=Green->FoodSwallowed;InitialSwallowCount=Green->SwallowCount;
    if(Case==5) {
        Hero->Inventory->ServerSelect(EMCToolSlot::Brush);
        if(!Hero->HasBrush() || Hero->EquippedBrush) {Fail(TEXT("Slot one must provide its brush without picking up an actor"));return false;}
    } else if(Case==4) {
        Hero->bHandling=true;
        if(!Food[0]->TryGrab(Hero)) {Fail(TEXT("A nearby real foreign object cannot begin the native rigid-body grip"));return false;}
    } else {
        if(!Hero->FoodCollection->HasCandidate()) {Fail(TEXT("A real nearby fragment cannot be collected"));return false;}
        // This is the same reliable RPC reached by E/LMB, without directly populating Pieces.
        UFunction* Toggle=Hero->FindFunction(TEXT("ServerToggleFoodCollection"));
        if(!Toggle) {Fail(TEXT("Production collection RPC is missing"));return false;}
        Hero->ProcessEvent(Toggle,nullptr);
    }
    Stage=1;StageAt=GetWorld()->GetTimeSeconds();WindowAt=SwallowAt=PassedAt=0;bSawSuction=false;
    UE_LOG(LogTemp,Display,TEXT("MC_FOOD_ZONES_BEGIN case=%d start=%s goal=%s pieces=%d circular=%d"),
        Case,*Start.ToString(),*Goal.ToString(),Count,Circular);
    return true;
}

void UMCFoodZonesSmoke::AdvanceCase()
{
    UE_LOG(LogTemp,Display,TEXT("MC_FOOD_ZONES_CHECK PASS case=%d walked=%.1f held=%d score=%d swallowed=%d"),
        Case,FVector::Dist2D(Start,Hero->GetActorLocation()),Hero->FoodCollection->Pieces.Num(),Hero->GetPlayerState<AMCPlayerState>()->Points,Green->FoodSwallowed);
    ++Case;Stage=0;
    if(Case==6) {
        bFinished=true;
        UE_LOG(LogTemp,Display,TEXT("MC_FOOD_ZONES_PASS real collection RPC, walking, mixed spoiled discard, Q stack physics, fresh exclusion, timed intake/score, foreign-object grip/Q and permanent slot-one brush preservation after Q; controlled events"));
        FPlatformMisc::RequestExitWithStatus(false,0);
    }
}

void UMCFoodZonesSmoke::Tick(float)
{
#if !UE_BUILD_SHIPPING
    UWorld* World=GetWorld();
    if(FPlatformTime::Seconds()-StartedAt>95.) {Fail(TEXT("95 second integration timeout"));return;}
    if(World->GetNetMode()!=NM_Standalone) {Fail(TEXT("Run this controlled geometry smoke in standalone"));return;}
    auto* State=World->GetGameState<AMCGameState>();
    auto* Controller=World->GetFirstPlayerController();
    if(!State || !Controller || !Controller->GetPawn() || World->GetTimeSeconds()<4) return;
#if WITH_EDITOR
    if(!bPrepared && GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
    if(!bPrepared) {
        Hero=Cast<AMCToothCharacter>(Controller->GetPawn());
        for(TActorIterator<AMCTongue> It(World);It;++It) {Tongue=*It;break;}
        for(TActorIterator<AMCThroat> It(World);It;++It) {Green=*It;break;}
        for(TActorIterator<AMCFoodDisposal> It(World);It;++It) if(It->bBrushBin) {Red=*It;break;}
        if(!Hero || !Tongue || !Green || !Red || !Hero->GetPlayerState<AMCPlayerState>()) {
            Fail(TEXT("Saved mouth lacks hero, tongue or two authored zones"));return;
        }
        if(State->bTutorialActive || State->bLobbyWaiting) {Fail(TEXT("Use ordinary L_Mouth without lobby/tutorial URL options"));return;}
        // Isolate interactions from unrelated timed weather. This harness does not claim to test the day director.
        if(auto* Mode=World->GetAuthGameMode()) Mode->SetActorTickEnabled(false);
        for(TActorIterator<AMCDayDirector> It(World);It;++It) It->SetActorTickEnabled(false);
        State->bDevManualEvents=true;State->bPhysicalBrushes=false;State->Phase=EMCShiftPhase::Working;State->PhaseEndsAt=0;
        Tongue->bAutomaticYawns=false;Tongue->ResetPain();Tongue->ResetYawn();
        for(TActorIterator<AMCFoodActor> It(World);It;++It) It->Destroy();
        Green->ResetSwallow();Hero->Inventory->ServerSelect(EMCToolSlot::Brush);
        InitialHealth=Hero->Status->State.Health;bPrepared=true;
        if(bCloseup) {
            if(FParse::Param(FCommandLine::Get(),TEXT("MCFoodZonesHideThroatArt"))) {
                // Render-only isolation in this opt-in test world; authored packages stay untouched.
                Green->AuthoredMouth->SetVisibility(false,true);Green->AuthoredMouth->SetHiddenInGame(true,true);
                TArray<UProceduralMeshComponent*> ArtMeshes;Green->GetComponents(ArtMeshes);
                int32 DepthCount=0;
                for(UProceduralMeshComponent* Art:ArtMeshes) if(Art->GetFName()==TEXT("ThroatDepth")) {
                    Art->SetVisibility(false,true);Art->SetHiddenInGame(true,true);++DepthCount;
                }
                UE_LOG(LogTemp,Display,TEXT("MC_FOOD_ZONES_CLOSEUP_HIDE_THROAT_ART authored=%s propagated_children=1 depth_components=%d"),
                    *GetNameSafe(Green->AuthoredMouth.Get()),DepthCount);
            }
            CloseupCamera=World->SpawnActor<ACameraActor>();
            if(!CloseupCamera) {Fail(TEXT("Cannot spawn closeup camera"));return;}
            Controller->SetViewTarget(CloseupCamera);
        } else if(FParse::Param(FCommandLine::Get(),TEXT("MCFoodZonesCapture"))) {
            auto* Camera=World->SpawnActor<ACameraActor>();
            const FVector Focus=Tongue->Surface->Bounds.Origin;
            const FVector Eye=Focus+FVector(0,-700,3700);
            Camera->SetActorLocationAndRotation(Eye,(Focus-Eye).Rotation());
            Camera->GetCameraComponent()->SetFieldOfView(80);Controller->SetViewTarget(Camera);
        }
    }
    if(bCloseup) {TickCloseup();return;}
    if(!Hero->Status->IsAlive() || !Hero->CanWork() || Hero->SwallowedBy || Hero->Status->State.Health<InitialHealth-.01f) {
        Fail(TEXT("Player became unsafe during a carry/delivery interaction"));return;
    }
    if(Stage==0) {PrepareCase();return;}
    const double Now=World->GetTimeSeconds(),Age=Now-StageAt;
    auto AllDisposed=[this]() {for(const AMCFoodActor* Piece:Food) if(IsValid(Piece) && !Piece->IsDisposed()) return false;return true;};
    if(Stage==1) {
        if(!bCapturedFirst && Age>.4) {Capture(TEXT("Zones_00"));bCapturedFirst=true;}
        if(!bCapturedSecond && Age>.75) {Capture(TEXT("Zones_01"));bCapturedSecond=true;}
        bool Settled=Hero->FoodCollection->Pieces.Num()==Food.Num();
        for(const AMCFoodActor* Piece:Food) Settled&=IsValid(Piece) && Piece->StackCarrier==Hero && !Piece->IsStackPickupActive();
        if(Case==4) Settled=Hero->HeldFood==Food[0] && Food[0]->Holders.Contains(Hero) && Hero->Grip->IsReady(Food[0])
            && Food[0]->Phase==EMCFoodPhase::Carried && Hero->Grip->LiftAlpha(Food[0])>.95f;
        if(Case==5) Settled=Hero->HasBrush() && !Hero->EquippedBrush && Hero->Inventory->Selected==EMCToolSlot::Brush;
        if(Settled && Age>.9) {
            if(Case==5) Hero->ServerSetPrimary(false);
            else for(const AMCFoodActor* Piece:Food) if(Piece->GetLastHandledBy()!=Hero->GetPlayerState<AMCPlayerState>()) {Fail(TEXT("Pickup lost authoritative player attribution"));return;}
            Stage=2;StageAt=Now;
        } else if(Age>4) Fail(TEXT("Automatic pickup did not settle into the real carried stack"));
        return;
    }
    if(Stage==2) {
        const FVector Travel=(Goal-Hero->GetActorLocation()).GetSafeNormal2D();
        if(Case==0 && Food[0]->IsDisposed() && Food[1]->IsDisposed()) {
            if(FVector::Dist2D(Start,Hero->GetActorLocation())<140 || Food[2]->IsDisposed()
                || Hero->FoodCollection->Pieces.Num()!=1 || !Hero->FoodCollection->Contains(Food[2]) || !Hero->FoodCollection->bCollecting) {
                Fail(TEXT("Mixed walking discard accepted outside its zone or lost the fresh remaining layer"));return;
            }
            Hero->GetCharacterMovement()->StopMovementImmediately();Stage=3;StageAt=Now;return;
        }
        if(Case==3 && Green->QueuedFoodCount==Food.Num()) {
            if(FVector::Dist2D(Start,Hero->GetActorLocation())<140 || !Hero->FoodCollection->Pieces.IsEmpty() || Green->ThroatPhase!=EMCThroatPhase::Anticipation) {
                Fail(TEXT("Walking fresh delivery skipped the real shared anticipation window"));return;
            }
            WindowAt=Green->PhaseStartedAt;Hero->GetCharacterMovement()->StopMovementImmediately();Stage=3;StageAt=Now;return;
        }
        if(FVector::Dist2D(Goal,Hero->GetActorLocation())<20) {
            Hero->GetCharacterMovement()->StopMovementImmediately();
            if(Case==1 || Case==4) {
                if(Red->ContainsDeliveryPosition(Hero->GetActorLocation()) || (Case==1?Hero->FoodCollection->Pieces.Num()!=1:Hero->HeldFood!=Food[0])) {Fail(TEXT("Q must begin with held food outside the red zone"));return;}
                Hero->ServerThrowItem();
                if(!Hero->FoodCollection->Pieces.IsEmpty() || Hero->FoodCollection->bCollecting || Food[0]->StackCarrier || !Food[0]->Holders.IsEmpty() || Hero->HeldFood) {Fail(TEXT("Q did not release the actual held object"));return;}
                Stage=3;StageAt=Now;return;
            }
            if(Case==2) {
                if(!Red->ContainsDeliveryPosition(Hero->GetActorLocation()) || AllDisposed()) {Fail(TEXT("Fresh ingredient was destroyed in the red zone"));return;}
                Stage=3;StageAt=Now;return;
            }
            if(Case==5) {
                if(!Red->ContainsDeliveryPosition(Hero->GetActorLocation()) || Hero->EquippedBrush || !Hero->HasBrush()) {
                    Fail(TEXT("Entering the red zone removed the permanent slot-one brush"));return;
                }
                Capture(TEXT("BrushPermanentRed"));Stage=3;StageAt=Now;return;
            }
        }
        if(Age>9) {Fail(TEXT("Walking the supported approach did not reach or activate the delivery zone"));return;}
        Hero->AddMovementInput(Travel,1.f);
        return;
    }
    if(Case==5) {
        bool PhysicalBrush=false;
        for(TActorIterator<AMCFoodActor> It(World);It;++It) if(It->bBrushTool && !It->IsDisposed()) PhysicalBrush=true;
        if(!Hero->HasBrush() || Hero->EquippedBrush || Hero->Inventory->Selected!=EMCToolSlot::Brush || PhysicalBrush
            || Hero->GetPlayerState<AMCPlayerState>()->Points!=InitialPoints) {
            Fail(TEXT("Permanent brush left slot one, spawned on the arena or awarded disposal score"));return;
        }
        if(Stage==3) {
            if(!Red->ContainsDeliveryPosition(Hero->GetActorLocation())) {
                Fail(TEXT("Permanent-brush probe left the red zone before Q"));return;
            }
            if(Age>1.) {
                UE_LOG(LogTemp,Display,TEXT("MC_FOOD_ZONES_BRUSH_PRESERVED permanent slot-one brush remains usable inside red cap"));
                Hero->ServerThrowItem();
                Stage=4;StageAt=Now;
            }
        } else if(Age>1.) AdvanceCase();
        return;
    }
    if(Case!=3) {
        const bool Thrown=Case==1 || Case==4;
        if(Thrown && Age>3 && !AllDisposed()) {Fail(TEXT("Physically thrown wrong ingredient never entered the red zone"));return;}
        if(Case==0 && (Food[2]->IsDisposed() || Hero->FoodCollection->Pieces.Num()!=1 || !Hero->FoodCollection->Contains(Food[2]))) {Fail(TEXT("Mixed discard lost the fresh layer after delivery"));return;}
        if(Case==2 && (AllDisposed() || Hero->FoodCollection->Pieces.Num()!=Food.Num())) {Fail(TEXT("Fresh red-zone exclusion detached or destroyed fresh food"));return;}
        if((!Thrown || AllDisposed()) && Age>1) {
            if(Thrown && (!Hero->FoodCollection->Pieces.IsEmpty() || Hero->FoodCollection->bCollecting || Hero->HeldFood)) {Fail(TEXT("Discarded food snapped back into the collection or grip"));return;}
            if(Hero->GetPlayerState<AMCPlayerState>()->Points!=InitialPoints) {Fail(TEXT("Red discard unexpectedly awarded intake score"));return;}
            AdvanceCase();
        }
        return;
    }
    if(!bSawSuction && Now<WindowAt+Green->AnticipationSeconds-.04 && Green->SwallowCount!=InitialSwallowCount) {
        Fail(TEXT("Intake started before its normal anticipation deadline"));return;
    }
    if(!bSawSuction && Green->ThroatPhase==EMCThroatPhase::Swallowing) {
        bSawSuction=true;SwallowAt=Green->PhaseStartedAt;
        if(SwallowAt-WindowAt<Green->AnticipationSeconds-.04 || Green->FoodSwallowed!=InitialSwallowed) {Fail(TEXT("Visible suction committed food early"));return;}
        Capture(TEXT("GreenIntake"));
    }
    if(Green->FoodSwallowed==InitialSwallowed+Food.Num() && PassedAt==0) {
        if(!bSawSuction || Now-SwallowAt<Green->SwallowSeconds-.04 || !AllDisposed()) {Fail(TEXT("Fresh food did not complete the full visible suction"));return;}
        PassedAt=Now;
    }
    if(PassedAt>0 && Now-PassedAt>Green->RecoverySeconds+.6) {
        const auto* Mode=World->GetAuthGameMode<AMCGameMode>();
        const int32 ExpectedPoints=InitialPoints+(Mode?Mode->ScoreRewards.Food:5)*Food.Num();
        if(Green->FoodSwallowed!=InitialSwallowed+Food.Num() || Green->SwallowCount!=InitialSwallowCount+1 || Green->ThroatPhase!=EMCThroatPhase::Collecting
            || Hero->GetPlayerState<AMCPlayerState>()->Points!=ExpectedPoints || !Hero->FoodCollection->Pieces.IsEmpty()) {
            Fail(TEXT("Intake did not consume/attribute exactly one ordinary two-piece batch"));return;
        }
        AdvanceCase();
    } else if(Age>15) Fail(TEXT("Normal timed intake did not finish and recover"));
#endif
}
