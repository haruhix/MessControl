#include "MCFishingRodComponent.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothMovementComponent.h"
#include "MCFoodCollectionComponent.h"
#include "MCExpressionComponent.h"
#include "MCGripComponent.h"
#include "MCHubBasketball.h"
#include "MCRewardChest.h"
#include "MCPlayerState.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PhysicsVolume.h"
#include "KismetProceduralMeshLibrary.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace { constexpr int32 LineSegments=24; constexpr float Step=1.f/120.f; }

UMCFishingRodComponent::UMCFishingRodComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PrePhysics;
    SetIsReplicatedByDefault(true);
    CarryPose=FTransform(FRotator(34,-65,-7),FVector(38,20,-15),FVector(.62));
    FishingPoseTransform=FTransform(FRotator(18,-5,0),FVector(54,12,-15),FVector(.62));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Rod(TEXT("/Game/Hub/Fishing/Meshes/SM_FishingRod"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Reel(TEXT("/Game/Hub/Fishing/Meshes/SM_FishingReel"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Lure(TEXT("/Game/Hub/Fishing/Meshes/SM_FishingLure"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Line(TEXT("/Game/Hub/Fishing/Materials/M_FishingLine"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Fish(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> CatchMaterial(TEXT("/Game/Hub/Fishing/Materials/M_FishingCatch"));
    RodMesh=Rod.Object;ReelMesh=Reel.Object;LureMesh=Lure.Object;LineMaterial=Line.Object;
    FishMesh=Fish.Object;FishMaterial=CatchMaterial.Object;
}

void UMCFishingRodComponent::BeginPlay()
{
    Super::BeginPlay();Hero=Cast<AMCToothCharacter>(GetOwner());
    if(!Hero || !IsHub()) {SetComponentTickEnabled(false);return;}
    AddTickPrerequisiteComponent(Hero->GetCharacterMovement());
    Hero->GetMesh()->AddTickPrerequisiteComponent(this);
    CreateVisuals();
}

bool UMCFishingRodComponent::IsHub() const
{
    return GetWorld() && GetWorld()->GetMapName().EndsWith(TEXT("Hub"));
}

bool UMCFishingRodComponent::IsPresented() const
{
    if(!Hero || !bEquipped || !IsHub() || !Hero->ToothPhysics->CanAct() || Hero->IsSeatedOnRaft()
        || Hero->HeldFood || Hero->FoodCollection->bCollecting || AMCHubBasketball::HeldBy(Hero)
        || Hero->OrderJumpTarget || Hero->SwallowedBy || Hero->MimicCaptor || Hero->IsYawning()) return false;
    const auto* Move=Cast<UMCToothMovementComponent>(Hero->GetCharacterMovement());
    if(Move && (Move->IsSwimming() || Move->IsClimbing())) return false;
    if(Hero->RewardInteraction && (Hero->RewardInteraction->Stage==EMCRewardChestStage::Lockpicking
        || Hero->RewardInteraction->Stage==EMCRewardChestStage::Opening)) return false;
    return Hero->Expression->BodyAlpha()<.001f && !Hero->Grip->Frame.Food && !Hero->Grip->Secondary.Food
        && !Hero->Grip->GrabbedPlayer && Hero->Grip->Blend()<.05f;
}

void UMCFishingRodComponent::ToggleEquipped() {ServerSetEquipped(!bEquipped);}
bool UMCFishingRodComponent::PondSurfaceAt(const APhysicsVolume* Pond,FVector Point,FVector& Surface) const
{
    if(!IsValid(Pond) || !Pond->bWaterVolume || !Pond->ActorHasTag(TEXT("FishingPond"))) return false;
    const FBox Bounds=Pond->GetComponentsBoundingBox(true);
    if(!Bounds.IsValid || !Pond->EncompassesPoint(FVector(Point.X,Point.Y,Bounds.Max.Z-1))) return false;
    Surface=FVector(Point.X,Point.Y,Bounds.Max.Z+4);
    // The swimming brush also covers the banks. Reject dry land and the pier
    // using the actual scene collision above the pond surface.
    FHitResult Floor;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCFishingWater),false,Hero);
    return !GetWorld()->LineTraceSingleByChannel(Floor,Surface+FVector(0,0,1500),Surface-FVector(0,0,220),ECC_Visibility,Query)
        || Floor.ImpactPoint.Z<Bounds.Max.Z-12;
}

bool UMCFishingRodComponent::FindPondCast(FVector& Origin,FVector& Target,FVector& Velocity,float& Duration,APhysicsVolume*& Pond) const
{
    if(!Hero || !IsHub()) return false;
    const FTransform Frame=RodFrame?RodFrame->GetComponentTransform():CarryPose*Hero->GetActorTransform();
    Origin=Frame.TransformPosition(BentPoint(FVector(294.5,0,-9.5)));
    const FVector Forward=Hero->GetActorForwardVector().GetSafeNormal2D();
    const FVector Gravity(0,0,-980);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCFishingCast),false,Hero);
    for(TActorIterator<APhysicsVolume> It(GetWorld());It;++It) {
        if(!It->bWaterVolume || !It->ActorHasTag(TEXT("FishingPond"))) continue;
        for(float Distance=1000;Distance>=300;Distance-=100) {
            if(!PondSurfaceAt(*It,Hero->GetActorLocation()+Forward*Distance,Target)
                || FVector::DistSquared(Origin,Target)>FMath::Square(MaxLineLength-60)) continue;
            Duration=FMath::Max(.9f,float(FVector::Dist2D(Origin,Target))/1050.f)+.45f;
            Velocity=(Target-Origin-Gravity*(.5f*Duration*Duration))/Duration;
            bool Clear=true;FVector Previous=Origin;
            for(int32 I=1;I<=16;++I) {
                const float Time=Duration*I/16;
                const FVector Next=Origin+Velocity*Time+Gravity*(.5f*Time*Time);
                FHitResult Hit;
                if(GetWorld()->SweepSingleByChannel(Hit,Previous,Next,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(4),Query)) {Clear=false;break;}
                Previous=Next;
            }
            if(Clear) {Pond=*It;return true;}
        }
    }
    return false;
}

bool UMCFishingRodComponent::CanCastLine() const
{
    if(!IsPresented() || bLineOut || Hero->GetVelocity().SizeSquared2D()>FMath::Square(20.f)) return false;
    FVector Origin,Target,Velocity;float Duration;APhysicsVolume* Pond=nullptr;
    return FindPondCast(Origin,Target,Velocity,Duration,Pond);
}

void UMCFishingRodComponent::CancelFishing()
{
    bFishingPose=bReeling=bLineOut=false;CastPond=nullptr;
    bFishHooked=false;BiteAt=-1;
    if(Hero) Hero->ForceNetUpdate();
}

void UMCFishingRodComponent::UpdateFishingCatch()
{
    if(!Hero || !Hero->HasAuthority()) return;
    if(!bLineOut || !CastPond || bReeling || bFishHooked || !bLureInWater || !Hero->GetPlayerState<AMCPlayerState>()) {BiteAt=-1;return;}
    const double Now=GetWorld()->GetTimeSeconds();
    if(BiteAt<0) {
        const float Minimum=FMath::IsFinite(MinBiteDelay)?FMath::Clamp(MinBiteDelay,.1f,60.f):4.f;
        const float Maximum=FMath::IsFinite(MaxBiteDelay)?FMath::Clamp(MaxBiteDelay,Minimum,60.f):FMath::Max(Minimum,9.f);
        LastBiteDelay=FMath::FRandRange(Minimum,Maximum);BiteAt=Now+LastBiteDelay;
    }
    if(Now<BiteAt) return;
    FVector Surface;
    if(!PondSurfaceAt(CastPond,LurePoint,Surface)) {CancelFishing();return;}
    bFishHooked=true;bReeling=true;BiteAt=-1;Hero->ForceNetUpdate();
}

void UMCFishingRodComponent::ServerSetEquipped_Implementation(bool Equipped)
{
    if(!Hero) Hero=Cast<AMCToothCharacter>(GetOwner());
    if(!IsHub() || !Hero || (Equipped && !Hero->CanSwitchTool())) return;
    if(Equipped) Hero->CancelGameplayInput();
    bEquipped=Equipped;CancelFishing();
}
void UMCFishingRodComponent::ServerSetFishingPose_Implementation(bool Raised)
{
    if(!Hero) Hero=Cast<AMCToothCharacter>(GetOwner());
    if(!Hero) return;
    if(!Raised || (IsPresented() && Hero->GetVelocity().SizeSquared2D()<=FMath::Square(20.f) && (bLineOut || CanCastLine()))) {bFishingPose=Raised;Hero->ForceNetUpdate();}
}
void UMCFishingRodComponent::ServerSetReeling_Implementation(bool Reeling)
{
    if(!Hero) Hero=Cast<AMCToothCharacter>(GetOwner());
    if(!Hero) return;
    if(!Reeling && bFishHooked) return; // A hooked catch is retrieved automatically.
    if(!Reeling || (IsPresented() && bLineOut)) {bReeling=Reeling;Hero->ForceNetUpdate();}
}
void UMCFishingRodComponent::CastLine_Implementation()
{
    if(!IsPresented() || !RodFrame || bLineOut || Hero->GetVelocity().SizeSquared2D()>FMath::Square(20.f)) return;
    APhysicsVolume* Pond=nullptr;
    if(!FindPondCast(CastOrigin,CastTarget,CastVelocity,CastFlightDuration,Pond)) return;
    CastPond=Pond;
    bLineOut=true;bFishingPose=true;
    bFishHooked=false;BiteAt=-1;
    ++CastSerial;Hero->ForceNetUpdate();
}

void UMCFishingRodComponent::CreateVisuals()
{
    if(!RodMesh || !ReelMesh || !LureMesh || !RodMesh->bAllowCPUAccess) return;
    RodFrame=NewObject<USceneComponent>(Hero,TEXT("FishingRodFrame"));
    RodFrame->SetupAttachment(Hero->GetRootComponent());RodFrame->RegisterComponent();
    auto Configure=[&](UPrimitiveComponent* Part) {
        Part->SetupAttachment(RodFrame);Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetGenerateOverlapEvents(false);Part->SetCanEverAffectNavigation(false);Part->RegisterComponent();
    };
    RodVisual=NewObject<UProceduralMeshComponent>(Hero,TEXT("FishingRodVisual"));Configure(RodVisual);
    UKismetProceduralMeshLibrary::GetSectionFromStaticMesh(RodMesh,0,0,RestVertices,Triangles,RestNormals,UVs,RestTangents);
    Vertices=RestVertices;Normals=RestNormals;Tangents=RestTangents;
    RodVisual->CreateMeshSection_LinearColor(0,Vertices,Triangles,Normals,UVs,TArray<FLinearColor>(),Tangents,false);
    RodVisual->SetMaterial(0,RodMesh->GetMaterial(0));
    ReelVisual=NewObject<UStaticMeshComponent>(Hero,TEXT("FishingReel"));ReelVisual->SetStaticMesh(ReelMesh);Configure(ReelVisual);
    LureVisual=NewObject<UStaticMeshComponent>(Hero,TEXT("FishingLure"));LureVisual->SetStaticMesh(LureMesh);Configure(LureVisual);
    LineVisual=NewObject<UProceduralMeshComponent>(Hero,TEXT("FishingLine"));Configure(LineVisual);
    LineVisual->SetCastShadow(false);LineVisual->SetMaterial(0,LineMaterial);
    FishVisual=NewObject<UStaticMeshComponent>(Hero,TEXT("FishingCatchCube"));
    FishVisual->SetStaticMesh(FishMesh);Configure(FishVisual);FishVisual->SetMaterial(0,FishMaterial);FishVisual->SetVisibility(false);
}

FVector UMCFishingRodComponent::BentPoint(FVector Point) const
{
    const float Weight=FMath::Clamp(float((Point.X-90)/205),0.f,1.f);
    return Point+TipBend*Weight*Weight;
}

void UMCFishingRodComponent::ResetLine(FVector Tip)
{
    FreeLineLength=FMath::Clamp(HangingLength,20.f,MaxLineLength);
    Points.SetNum(LineSegments+1);PreviousPoints.SetNum(LineSegments+1);
    for(int32 I=0;I<=LineSegments;++I) Points[I]=PreviousPoints[I]=Tip-FVector(0,0,FreeLineLength*I/LineSegments);
    Accumulator=0;LurePoint=Points.Last();LastAnchor=Tip;LastFrameVelocity=BendVelocity=TipBend=FVector::ZeroVector;
}

void UMCFishingRodComponent::SimulateLine(float Dt,FVector Tip)
{
    if(Points.Num()!=LineSegments+1 || FVector::DistSquared(LastAnchor,Tip)>FMath::Square(500.f)) ResetLine(Tip);
    if(CastSerial!=LastCastSerial) {
        LastCastSerial=CastSerial;
        if(bLineOut) {
            ResetLine(CastOrigin);CastAge=0;bCastFlight=true;bLureInWater=false;
            for(int32 I=1;I<=LineSegments;++I) Points[I]=PreviousPoints[I]=CastOrigin;
        }
    }
    if(!bLineOut) FreeLineLength=HangingLength;
    if(bReeling) FreeLineLength=FMath::Max(HangingLength,FreeLineLength-Dt*320);
    Accumulator+=FMath::Min(Dt,.05f);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCFishingLure),false,Hero);
    while(Accumulator>=Step) {
        Accumulator-=Step;
        const FVector OldEnd=Points.Last();
        if(bReeling || !bLineOut) bCastFlight=bLureInWater=false;
        if(bCastFlight) {
            CastAge+=Step;
            if(CastAge>=CastFlightDuration) {bCastFlight=false;bLureInWater=true;WaterAnchor=CastTarget;}
        }
        const bool PinLure=bLineOut && !bReeling && (bCastFlight || bLureInWater);
        const FVector Pinned=bCastFlight?CastOrigin+CastVelocity*CastAge+FVector(0,0,-490)*CastAge*CastAge:WaterAnchor;
        for(int32 I=1;I<=LineSegments;++I) {
            const FVector Current=Points[I];
            Points[I]+=(Points[I]-PreviousPoints[I])*(I==LineSegments?.998f:.987f)+FVector(0,0,-980)*Step*Step;
            PreviousPoints[I]=Current;
        }
        if(PinLure) {Points.Last()=Pinned;PreviousPoints.Last()=OldEnd;}
        if(bLineOut && !bReeling) FreeLineLength=FMath::Clamp(float(FVector::Distance(Tip,Points.Last()))+3,FreeLineLength,MaxLineLength);
        const float Segment=FreeLineLength/LineSegments;
        for(int32 Iteration=0;Iteration<12;++Iteration) {
            Points[0]=Tip;
            for(int32 I=0;I<LineSegments;++I) {
                const FVector Delta=Points[I+1]-Points[I];const float Length=Delta.Size();
                if(Length<UE_SMALL_NUMBER) continue;
                const float W0=I==0?0.f:1.f,W1=I+1==LineSegments?(PinLure?0.f:.08f):1.f;
                const FVector Correction=Delta*((Length-Segment)/Length)/(W0+W1);
                Points[I]+=Correction*W0;Points[I+1]-=Correction*W1;
            }
            for(int32 I=LineSegments-1;I>=0;--I) {
                const FVector Delta=Points[I+1]-Points[I];const float Length=Delta.Size();
                if(Length<UE_SMALL_NUMBER) continue;
                const float W0=I==0?0.f:1.f,W1=I+1==LineSegments?(PinLure?0.f:.08f):1.f;
                const FVector Correction=Delta*((Length-Segment)/Length)/(W0+W1);
                Points[I]+=Correction*W0;Points[I+1]-=Correction*W1;
            }
        }
        Points[0]=Tip;
        FHitResult Hit;
        if(!PinLure && GetWorld()->SweepSingleByChannel(Hit,OldEnd,Points.Last(),FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(4),Query)) {
            Points.Last()=Hit.Location+Hit.Normal*.5f;
            const FVector Velocity=(Points.Last()-PreviousPoints.Last())/Step;
            PreviousPoints.Last()=Points.Last()-FVector::VectorPlaneProject(Velocity,Hit.Normal)*Step*.75f;
        }
        // A fast turn can move the tip farther than an iterative chain settles
        // in one step. The lure still cannot travel beyond the paid-out length.
        const FVector Radius=Points.Last()-Tip;
        if(Radius.SizeSquared()>FMath::Square(FreeLineLength)) {
            const FVector Direction=Radius.GetSafeNormal();
            FVector Velocity=(Points.Last()-PreviousPoints.Last())/Step;
            Velocity-=Direction*FMath::Max(0.,FVector::DotProduct(Velocity,Direction));
            Points.Last()=Tip+Direction*FreeLineLength;
            PreviousPoints.Last()=Points.Last()-Velocity*Step;
        }
        if(bLineOut && !bReeling && !bCastFlight && !bLureInWater && CastPond && Points.Last().Z<=CastTarget.Z) {
            FVector Surface;
            if(PondSurfaceAt(CastPond,Points.Last(),Surface)) {WaterAnchor=Points.Last()=PreviousPoints.Last()=Surface;bLureInWater=true;}
        }
    }
    LastAnchor=Tip;LurePoint=Points.Last();
    if(Hero->HasAuthority() && bReeling && FreeLineLength<=HangingLength+.1f) {
        if(bFishHooked) if(auto* Identity=Hero->GetPlayerState<AMCPlayerState>()) Identity->AddCaughtFish();
        CancelFishing();
    }
}

void UMCFishingRodComponent::DrawLine()
{
    const FTransform World=RodFrame->GetComponentTransform();
    TArray<FVector> Path;
    const FVector Guides[]={FVector(50,0,-12),FVector(95.5,0,-10),FVector(199.2,0,-11),FVector(294.5,0,-9.5)};
    for(int32 Span=0;Span<3;++Span) for(int32 J=0;J<7;++J) {
        const float T=J/7.f;
        Path.Add(World.TransformPosition(FMath::Lerp(BentPoint(Guides[Span]),BentPoint(Guides[Span+1]),T))-FVector(0,0,4*T*(1-T)*1.3f));
    }
    Path.Append(Points);
    LineVertices.Reset();LineNormals.Reset();LineUVs.Reset();LineTangents.Reset();
    for(int32 I=0;I<Path.Num();++I) {
        const FVector Axis=(Path[FMath::Min(I+1,Path.Num()-1)]-Path[FMath::Max(0,I-1)]).GetSafeNormal();
        FVector U,V;Axis.FindBestAxisVectors(U,V);
        for(int32 Side=0;Side<4;++Side) {
            const FVector Normal=U*FMath::Cos(Side*UE_HALF_PI)+V*FMath::Sin(Side*UE_HALF_PI);
            LineVertices.Add(World.InverseTransformPosition(Path[I]+Normal*.14f));
            LineNormals.Add(World.InverseTransformVectorNoScale(Normal));
            LineUVs.Add(FVector2D(Side/4.f,I/8.f));
            LineTangents.Add(FProcMeshTangent(World.InverseTransformVectorNoScale(Axis),false));
        }
    }
    if(LineTriangles.IsEmpty()) {
        for(int32 I=0;I<Path.Num()-1;++I) for(int32 Side=0;Side<4;++Side) {
            const int32 A=I*4+Side,B=I*4+(Side+1)%4;
            LineTriangles.Append({A,B,A+4,B,B+4,A+4});
        }
        LineVisual->CreateMeshSection_LinearColor(0,LineVertices,LineTriangles,LineNormals,LineUVs,TArray<FLinearColor>(),LineTangents,false);
    } else LineVisual->UpdateMeshSection_LinearColor(0,LineVertices,LineNormals,LineUVs,TArray<FLinearColor>(),LineTangents);
}

void UMCFishingRodComponent::TickComponent(float Dt,ELevelTick Tick,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Dt,Tick,Function);
    if(!RodFrame || !RodVisual || !LineVisual || Dt<=0) return;
    const bool Visible=IsPresented();
    const bool Walking=Hero->GetVelocity().SizeSquared2D()>FMath::Square(20.f);
    if(Hero->HasAuthority() && Walking && (bFishingPose || bReeling || bLineOut)) CancelFishing();
    RodFrame->SetVisibility(Visible,true);
    if(!Visible) {
        bWasVisible=false;HandsAlpha=0;
        if(Hero->HasAuthority() && (bFishingPose || bReeling || bLineOut)) CancelFishing();
        return;
    }
    PoseAlpha=FMath::FInterpTo(PoseAlpha,!Walking && (bFishingPose || bLineOut)?1.f:0.f,Dt,7.f);
    FTransform Local;Local.Blend(CarryPose,FishingPoseTransform,PoseAlpha);
    const float Speed=FMath::Clamp(float(Hero->GetVelocity().Size2D())/400.f,0.f,1.f);
    Local.AddToTranslation(FVector(0,0,FMath::Sin(Hero->AnimationGait*2)*1.3f*Speed));
    RodFrame->SetRelativeTransform(Local);
    FVector Tip=RodFrame->GetComponentTransform().TransformPosition(BentPoint(FVector(294.5,0,-9.5)));
    if(!bWasVisible || (bWasLineOut && !bLineOut)) {ResetLine(Tip);bWasVisible=true;}
    bWasLineOut=bLineOut;
    const FVector Velocity=Hero->GetVelocity();
    const FVector Acceleration=(Velocity-LastFrameVelocity)/FMath::Max(Dt,.008f);LastFrameVelocity=Velocity;
    FVector Target=RodFrame->GetComponentTransform().InverseTransformVectorNoScale(-Acceleration*.006f+FVector(0,0,-2));
    if(Points.Num()>1) Target+=RodFrame->GetComponentTransform().InverseTransformVectorNoScale((Points[1]-Tip).GetSafeNormal()*5);
    Target.X=0;Target=Target.GetClampedToMaxSize(24);
    const float BendDt=FMath::Min(Dt,.033f);
    BendVelocity+=(Target-TipBend)*75*BendDt;BendVelocity*=FMath::Exp(-10*BendDt);TipBend=(TipBend+BendVelocity*BendDt).GetClampedToMaxSize(28);
    for(int32 I=0;I<Vertices.Num();++I) {
        Vertices[I]=BentPoint(RestVertices[I]);
        const float Derivative=2*FMath::Clamp(float((RestVertices[I].X-90)/205),0.f,1.f)/205;
        const FQuat Q=FQuat::FindBetweenNormals(FVector::ForwardVector,(FVector::ForwardVector+TipBend*Derivative).GetSafeNormal());
        Normals[I]=Q.RotateVector(RestNormals[I]);Tangents[I]=FProcMeshTangent(Q.RotateVector(RestTangents[I].TangentX),RestTangents[I].bFlipTangentY);
    }
    RodVisual->UpdateMeshSection_LinearColor(0,Vertices,Normals,UVs,TArray<FLinearColor>(),Tangents);
    Tip=RodFrame->GetComponentTransform().TransformPosition(BentPoint(FVector(294.5,0,-9.5)));
    SimulateLine(Dt,Tip);UpdateFishingCatch();DrawLine();
    if(FishVisual) {
        FishVisual->SetVisibility(bFishHooked);
        if(bFishHooked) {
            const float Pickup=1-FMath::SmoothStep(HangingLength,HangingLength+180,FreeLineLength);
            const FVector Bag=Hero->GetActorLocation()+Hero->GetActorForwardVector()*25+FVector(0,0,5);
            FishVisual->SetWorldLocation(FMath::Lerp(LurePoint+FVector(0,0,16),Bag,Pickup));
            FishVisual->SetWorldScale3D(FVector(.3));
            const float Time=GetWorld()->GetTimeSeconds();
            FishVisual->SetWorldRotation(FRotator(FMath::Sin(Time*11)*15,Time*120,FMath::Cos(Time*9)*12));
        }
    }
    if(bReeling) ReelAngle-=Dt*400;
    const FQuat ReelRotation(FVector::RightVector,FMath::DegreesToRadians(ReelAngle));
    const FVector ReelCenter(50.492,11.4035,0);
    ReelVisual->SetRelativeTransform(FTransform(ReelRotation,ReelCenter-ReelRotation.RotateVector(ReelCenter)));
    const FVector Cord=(Points[LineSegments-1]-LurePoint).GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector);
    const FQuat LureRotation=FQuat::FindBetweenNormals(FVector::UpVector,Cord);
    const FBox Bounds=LureMesh->GetBoundingBox();const FVector Top(Bounds.GetCenter().X,Bounds.GetCenter().Y,Bounds.Max.Z);
    LureVisual->SetWorldLocationAndRotation(LurePoint-LureRotation.RotateVector(Top*RodFrame->GetComponentScale()),LureRotation);
    LureVisual->SetWorldScale3D(RodFrame->GetComponentScale());
}

void UMCFishingRodComponent::BuildHandPose(TArray<FTransform>& Pose,const FReferenceSkeleton& Ref,float Dt)
{
    if(!RodFrame || !IsPresented()) {HandsAlpha=0;return;}
    HandsAlpha=FMath::FInterpTo(HandsAlpha,1.f,Dt,14);
    const FTransform MeshWorld=Hero->GetMesh()->GetComponentTransform();
    TArray<FTransform> CS;CS.SetNum(Pose.Num());
    auto Rebuild=[&] {for(int32 I=0;I<Pose.Num();++I) {const int32 P=Ref.GetParentIndex(I);CS[I]=P<0?Pose[I]:Pose[I]*CS[P];}};
    Rebuild();
    for(int32 Side=0;Side<2;++Side) {
        const int32 Hand=Ref.FindBoneIndex(Hero->RigBone(Side==0?TEXT("hand_l"):TEXT("hand_r")));
        const int32 Lower=Ref.FindBoneIndex(Hero->RigBone(Side==0?TEXT("forearm_l"):TEXT("forearm_r")));
        if(Hand<0 || Lower<0 || Ref.GetParentIndex(Hand)!=Lower) continue;
        FTransform Rest=Ref.GetRefBonePose()[Hand];
        for(int32 P=Ref.GetParentIndex(Hand);P>=0;P=Ref.GetParentIndex(P)) Rest=Rest*Ref.GetRefBonePose()[P];
        const FQuat RestWorld=(Hero->StandingMeshTransform().GetRotation()*Rest.GetRotation()).GetNormalized();
        const FVector Grip=Side==0?LeftGrip:RightGrip;
        const FQuat Rotation=(RodFrame->GetComponentQuat()*RestWorld).GetNormalized();
        FTransform Goal((MeshWorld.GetRotation().Inverse()*Rotation).GetNormalized(),MeshWorld.InverseTransformPosition(RodFrame->GetComponentTransform().TransformPosition(Grip)),CS[Hand].GetScale3D());
        FTransform Blended;Blended.Blend(CS[Hand],Goal,HandsAlpha);
        const FTransform Branch=Ref.GetRefBonePose()[Hand].Inverse()*Blended;
        const int32 Parent=Ref.GetParentIndex(Lower);
        Pose[Lower]=Parent<0?Branch:Branch.GetRelativeTransform(CS[Parent]);Pose[Hand]=Ref.GetRefBonePose()[Hand];Rebuild();
    }
}

void UMCFishingRodComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UMCFishingRodComponent,bEquipped);DOREPLIFETIME(UMCFishingRodComponent,bFishingPose);
    DOREPLIFETIME(UMCFishingRodComponent,bReeling);DOREPLIFETIME(UMCFishingRodComponent,bLineOut);
    DOREPLIFETIME(UMCFishingRodComponent,CastSerial);DOREPLIFETIME(UMCFishingRodComponent,CastOrigin);DOREPLIFETIME(UMCFishingRodComponent,CastVelocity);
    DOREPLIFETIME(UMCFishingRodComponent,CastTarget);DOREPLIFETIME(UMCFishingRodComponent,CastFlightDuration);DOREPLIFETIME(UMCFishingRodComponent,CastPond);
    DOREPLIFETIME(UMCFishingRodComponent,bFishHooked);
}
