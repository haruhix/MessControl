#include "MCTongue.h"
#include "KismetProceduralMeshLibrary.h"
#include "Engine/StaticMesh.h"
#include "MCGameState.h"
#include "MCGameMode.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCFoodActor.h"
#include "MCGazeComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"

AMCTongue::AMCTongue()
{
    bReplicates=true; bAlwaysRelevant=true;
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickGroup=TG_PrePhysics;
    PrimaryActorTick.TickInterval=1.f/30.f;
    Surface=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TongueSurface")); SetRootComponent(Surface);
    Surface->SetMobility(EComponentMobility::Movable);
    Surface->SetCollisionProfileName(TEXT("BlockAll")); Surface->SetCollisionObjectType(ECC_WorldStatic);
    Surface->bUseComplexAsSimpleCollision=true;
    // Initial construction is synchronous so a player can never spawn before the floor exists.
    Surface->bUseAsyncCooking=false; Surface->CanCharacterStepUpOn=ECB_Yes;
}
void AMCTongue::OnConstruction(const FTransform& Transform) { Super::OnConstruction(Transform); RebuildSurface(); }
void AMCTongue::RebuildSurface()
{
    if (!SourceMesh) return;
    UKismetProceduralMeshLibrary::GetSectionFromStaticMesh(SourceMesh,0,0,Rest,Indices,RestNormals,UV,RestTangents);
    if (Rest.IsEmpty()) { UE_LOG(LogTemp,Error,TEXT("Tongue source requires Allow CPU Access and section 0")); return; }
    RestBounds=FBox(Rest); Positions=Rest; Normals=RestNormals; Tangents=RestTangents;
    // Weld UV/normal duplicates only for detecting the open seam with the static mouth.
    TMap<FIntVector,int32> Weld; TArray<int32> Welded; TArray<FVector> Unique;
    for (FVector P:Rest)
    {
        const FIntVector Key(FMath::RoundToInt(P.X*100),FMath::RoundToInt(P.Y*100),FMath::RoundToInt(P.Z*100));
        int32* Existing=Weld.Find(Key);
        if (Existing) Welded.Add(*Existing); else { Welded.Add(Unique.Num()); Weld.Add(Key,Unique.Num()); Unique.Add(P); }
    }
    TMap<FIntPoint,int32> Edges;
    for (int32 I=0;I<Indices.Num();I+=3) for (int32 J=0;J<3;++J)
    {
        const int32 A=Welded[Indices[I+J]],B=Welded[Indices[I+(J+1)%3]];
        ++Edges.FindOrAdd(FIntPoint(FMath::Min(A,B),FMath::Max(A,B)));
    }
    TSet<int32> Boundary;
    for (const auto& Edge:Edges) if (Edge.Value==1) { Boundary.Add(Edge.Key.X); Boundary.Add(Edge.Key.Y); }
    AnchorWeights.Reset(); AnchorGradients.Reset();
    for (FVector P:Rest)
    {
        float Distance=180; FVector Away=FVector::ZeroVector;
        for (int32 Index:Boundary) if (const float D=FVector::Distance(P,Unique[Index]); D<Distance) { Distance=D; Away=(P-Unique[Index]).GetSafeNormal(); }
        const float U=Distance/180;
        AnchorWeights.Add(FMath::SmoothStep(0.f,180.f,Distance));
        AnchorGradients.Add(Away*(6*U*(1-U)/180));
    }
    Colors.Init(FColor(0,0,0,255),Rest.Num());
    IndentDepth.Init(0,Rest.Num()); IndentGradient.Init(FVector::ZeroVector,Rest.Num()); PressureUV.Init(FVector2D::ZeroVector,Rest.Num());
    Surface->ClearAllMeshSections();
    Surface->CreateMeshSection(0,Positions,Indices,Normals,UV,PressureUV,TArray<FVector2D>(),TArray<FVector2D>(),Colors,Tangents,true);
    Surface->SetMaterial(0,SurfaceMaterial?SurfaceMaterial.Get():SourceMesh->GetMaterial(0));
}
void AMCTongue::BeginPlay()
{
    Super::BeginPlay(); RebuildSurface();
    if (HasAuthority())
    {
        Settings=Profile?Profile->Settings:FMCTongueSettings(); Settings.Sanitize();
        PressureSettings=Profile?Profile->Pressure:FMCTonguePressureSettings(); PressureSettings.Sanitize();
        ScheduleJolt(); ForceNetUpdate();
    }
}
float AMCTongue::ServerTime() const
{
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    return GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
}
bool AMCTongue::TriggerPain(FVector Point)
{
    if (Profile && Profile->PainMotion) return PlayMotion(Profile->PainMotion,Point,FVector::ForwardVector);
    FMCTongueMotionSettings Event;
    Event.Shape=EMCTongueShape::RadialWave; Event.Height=Settings.WaveHeight;
    Event.Speed=Settings.WaveSpeed; Event.Width=Settings.WaveWidth; Event.Radius=Settings.WaveRadius;
    Event.Anticipation=0; Event.Redness=1; Event.Lift=Settings.LiftSpeed; Event.Push=Settings.PushSpeed;
    Event.AffectHeight=Settings.AffectHeight; Event.RestAfter=FMath::Max(.2f,Settings.Cooldown-Settings.Duration());
    return StartMotion(Event,Point,FVector::ForwardVector,1);
}
void AMCTongue::ScheduleJolt() { NextJoltAt=ServerTime()+FMath::FRandRange(Settings.JoltRestMin,Settings.JoltRestMax); }
bool AMCTongue::TriggerJolt()
{
    const FVector Origin=GetActorTransform().TransformPosition(RestBounds.GetCenter());
    const FVector Front=GetActorTransform().TransformVectorNoScale(FVector(0,1,0));
    if (Profile && Profile->JoltMotion) return PlayMotion(Profile->JoltMotion,Origin,Front);
    FMCTongueMotionSettings Event;
    Event.Shape=EMCTongueShape::FrontBend; Event.Height=Settings.JoltHeight;
    Event.Anticipation=Settings.JoltAnticipation; Event.Rise=Settings.JoltRise; Event.Return=Settings.JoltReturn;
    Event.Lift=Settings.JoltLift; Event.Push=Settings.JoltPush; Event.AffectHeight=Settings.AffectHeight;
    Event.bPushFromOrigin=false; Event.bFoodLiftIgnoresMass=true;
    return StartMotion(Event,Origin,Front,1);
}
bool AMCTongue::IsMotionActive() const
{
    return Motion.Serial>0 && ServerTime()<Motion.StartedAt+Motion.Settings.Duration()+Motion.Settings.RestAfter;
}
bool AMCTongue::PlayMotion(UMCTongueMotionProfile* Event,FVector Origin,FVector Direction,float Strength)
{
    return Event && StartMotion(Event->Settings,Origin,Direction,Strength);
}
bool AMCTongue::StartMotion(FMCTongueMotionSettings Event,FVector Origin,FVector Direction,float Strength)
{
    if (!HasAuthority() || IsMotionActive() || Rest.IsEmpty() || Origin.ContainsNaN() || Direction.ContainsNaN()
        || !FMath::IsFinite(Strength) || Strength<=0) return false;
    Event.Sanitize(); Strength=FMath::Clamp(Strength,.05f,2.f);
    Event.Height*=Strength; Event.Push*=Strength; Event.Lift*=Strength; Event.Sanitize();
    Motion.Settings=Event; Motion.Origin=GetActorTransform().InverseTransformPosition(Origin);
    Motion.Direction=GetActorTransform().InverseTransformVectorNoScale(Direction.GetSafeNormal2D(KINDA_SMALL_NUMBER,FVector::ForwardVector));
    Motion.StartedAt=ServerTime()+.2; ++Motion.Serial;
    PlayerPushes=FoodPushes=0; HitActors.Empty(); PreviousMotionAge=-1;
    ScheduleJolt(); NextJoltAt+=Event.Duration(); ForceNetUpdate();
    for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        if (It->Gaze) It->Gaze->NoticePoint(Origin+FVector(0,0,40),FMath::Max(.5f,Event.Anticipation));
    return true;
}
void AMCTongue::ResetPain()
{
    if (!HasAuthority()) return;
    Motion=FMCTongueMotionState(); HitActors.Empty(); PreviousMotionAge=-1; PlayerPushes=FoodPushes=0;
    ScheduleJolt(); ForceNetUpdate();
}
float AMCTongue::JoltWeight(FVector P) const
{
    const FVector U=(P-RestBounds.Min)/RestBounds.GetSize();
    const float Lateral=FMath::Square(FMath::Sin(PI*FMath::Clamp(float(U.X),0.f,1.f)));
    // +local Y is the front of this authored tongue. The root remains fixed.
    return Lateral*FMath::SmoothStep(.1f,.48f,float(U.Y))
        *(1-FMath::SmoothStep(.88f,1.f,float(U.Y)))*FMath::SmoothStep(.15f,.75f,float(U.Z));
}
float AMCTongue::SurfaceWeight(FVector P) const
{
    const FVector Unit=(P-RestBounds.Min)/RestBounds.GetSize();
    // Fix the rim and underside to the original mouth. No cracks at the shell seam.
    const float Rim=FMath::Square(FMath::Sin(PI*FMath::Clamp(float(Unit.X),0.f,1.f)))
        *FMath::Square(FMath::Sin(PI*FMath::Clamp(float(Unit.Y),0.f,1.f)));
    return Rim*FMath::SmoothStep(.15f,.75f,float(Unit.Z));
}
float AMCTongue::MotionDistance(FVector P) const
{
    const FVector Delta=GetActorTransform().TransformVector(P-Motion.Origin);
    return Motion.Settings.Shape==EMCTongueShape::DirectionalWave?FVector::DotProduct(Delta,GetActorTransform().TransformVectorNoScale(Motion.Direction)):Delta.Size2D();
}
float AMCTongue::MotionWeight(FVector P) const
{
    const auto& S=Motion.Settings;
    if (S.Shape==EMCTongueShape::FrontBend) return JoltWeight(P);
    const float Weight=FMath::Sqrt(SurfaceWeight(P));
    if (S.Shape==EMCTongueShape::LocalLift) return Weight*(1-FMath::SmoothStep(0.f,S.Radius,MotionDistance(P)));
    if (S.Shape==EMCTongueShape::DirectionalWave)
    {
        const FVector Delta=GetActorTransform().TransformVector(P-Motion.Origin);
        const FVector Direction=GetActorTransform().TransformVectorNoScale(Motion.Direction);
        const float Side=FMath::Abs(FVector::DotProduct(Delta,FVector::CrossProduct(Direction,FVector::UpVector)));
        return Weight*(1-FMath::SmoothStep(S.Radius*.6f,S.Radius,Side));
    }
    return Weight;
}
float AMCTongue::Offset(FVector P,float Time,float& Red) const
{
    const float UnitY=(P.Y-RestBounds.Min.Y)/RestBounds.GetSize().Y;
    const float Idle=SurfaceWeight(P)*Settings.IdleHeight*FMath::Sin(Time*2*PI/Settings.IdlePeriod+UnitY*1.2f);
    const auto& S=Motion.Settings; const float Age=Time-Motion.StartedAt;
    const float Amount=Motion.Serial>0?MotionWeight(P)*(S.IsWave()?S.Band(MotionDistance(P),Age):S.Envelope(Age)):0;
    Red=FMath::Max(0.f,Amount)*S.Redness;
    return Idle+S.Height*Amount;
}
void AMCTongue::Deform(float Time)
{
    for (int32 I=0;I<Rest.Num();++I)
    {
        const FVector P=Rest[I]; float Red=0,Dummy=0;
        const float PhysicalHeight=Offset(P,Time,Red),Height=PhysicalHeight-IndentDepth[I],Anchor=AnchorWeights[I];
        Positions[I]=P+FVector(0,0,PhysicalHeight*Anchor); Red*=Anchor;
        PressureUV[I]=FVector2D(IndentDepth[I]*Anchor,0);
        // Transform artist normals/tangents with the displacement gradient; keep UV seam smoothing.
        const float Dx=((Offset(P+FVector(1,0,0),Time,Dummy)-Offset(P-FVector(1,0,0),Time,Dummy))*.5f-IndentGradient[I].X)*Anchor+Height*AnchorGradients[I].X;
        const float Dy=((Offset(P+FVector(0,1,0),Time,Dummy)-Offset(P-FVector(0,1,0),Time,Dummy))*.5f-IndentGradient[I].Y)*Anchor+Height*AnchorGradients[I].Y;
        const float Dz=((Offset(P+FVector(0,0,1),Time,Dummy)-Offset(P-FVector(0,0,1),Time,Dummy))*.5f-IndentGradient[I].Z)*Anchor+Height*AnchorGradients[I].Z;
        const FVector N=RestNormals[I],T=RestTangents[I].TangentX;
        const float Nz=N.Z/FMath::Max(.5f,1+Dz);
        Normals[I]=FVector(N.X-Dx*Nz,N.Y-Dy*Nz,Nz).GetSafeNormal();
        Tangents[I]=FProcMeshTangent((T+FVector(0,0,Dx*T.X+Dy*T.Y+Dz*T.Z)).GetSafeNormal(),RestTangents[I].bFlipTangentY);
        Colors[I]=FColor(FMath::RoundToInt(FMath::Clamp(Red,0.f,1.f)*255),0,0,255);
    }
    Surface->UpdateMeshSection(0,Positions,Normals,UV,PressureUV,TArray<FVector2D>(),TArray<FVector2D>(),Colors,Tangents);
}
bool AMCTongue::SurfacePoint(FVector P,FHitResult& Hit) const
{
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MCTongueSurface),true);
    Params.bReturnFaceIndex=true;
    return Surface->LineTraceComponent(Hit,P+FVector(0,0,1200),P-FVector(0,0,1600),Params);
}
void AMCTongue::PushMotion(float Age)
{
    const auto& S=Motion.Settings;
    if (Motion.Serial==0 || Age<0 || PreviousMotionAge>S.Duration()) return;
    const FVector Origin=GetActorTransform().TransformPosition(Motion.Origin);
    const FVector Direction=GetActorTransform().TransformVectorNoScale(Motion.Direction).GetSafeNormal2D();
    auto Strength=[&](AActor* Actor,FVector P)
    {
        if (HitActors.Contains(Actor)) return 0.f;
        const FVector Local=GetActorTransform().InverseTransformPosition(P);
        if (S.IsWave()? !S.Crossed(MotionDistance(Local),PreviousMotionAge,Age): !(Age>=S.Anticipation && PreviousMotionAge<S.Anticipation)) return 0.f;
        FHitResult Hit;
        if (!SurfacePoint(P,Hit) || P.Z<Hit.ImpactPoint.Z-30 || P.Z>Hit.ImpactPoint.Z+S.AffectHeight) return 0.f;
        const float Weight=MotionWeight(GetActorTransform().InverseTransformPosition(Hit.ImpactPoint));
        if (Weight<.15f) return 0.f;
        HitActors.Add(Actor); return S.IsWave()?1.f:FMath::Sqrt(Weight);
    };
    for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        auto* Hero=*It; const FVector P=Hero->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll?Hero->ToothPhysics->PhysicalLocation():Hero->GetActorLocation();
        if (!Hero->Status->IsAlive()) continue;
        const float W=Strength(Hero,P); if (W<=0) continue;
        const FVector Away=S.bPushFromOrigin?(P-Origin).GetSafeNormal2D(KINDA_SMALL_NUMBER,Direction):Direction;
        Hero->ToothPhysics->ApplyHit((Away*S.Push+FVector(0,0,S.Lift))*W,P); ++PlayerPushes;
    }
    for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
    {
        auto* Food=*It; const FVector P=Food->Body->GetComponentLocation();
        if (Food->IsDisposed() || Food->Phase==EMCFoodPhase::Equipped || Food->Phase==EMCFoodPhase::Stuck || !Food->Body->IsSimulatingPhysics()) continue;
        const float W=Strength(Food,P); if (W<=0) continue;
        const FVector Away=S.bPushFromOrigin?(P-Origin).GetSafeNormal2D(KINDA_SMALL_NUMBER,Direction):Direction;
        const float MassRatio=4.f/FMath::Max(1.f,Food->Body->GetMass());
        const FVector Velocity=S.bFoodLiftIgnoresMass?(Away*S.Push*FMath::Sqrt(MassRatio)+FVector(0,0,S.Lift)):(Away*S.Push+FVector(0,0,S.Lift))*MassRatio;
        Food->Body->AddImpulse(Velocity*W,NAME_None,true); ++FoodPushes;
    }
    PreviousMotionAge=Age;
}
void AMCTongue::Tick(float Dt)
{
    Super::Tick(Dt); if (Rest.IsEmpty()) return;
    TArray<TPair<AMCToothCharacter*,float>> Riders;
    for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        auto* Hero=*It; auto* Move=Hero->GetCharacterMovement();
        Move->AddTickPrerequisiteActor(this);
        if ((HasAuthority() || Hero->IsLocallyControlled()) && Hero->ToothPhysics->CanAct() && Move->IsMovingOnGround() && Move->CurrentFloor.HitResult.GetComponent()==Surface)
        {
            FHitResult Hit; if (SurfacePoint(Hero->GetActorLocation(),Hit)) Riders.Emplace(Hero,Hit.ImpactPoint.Z);
        }
    }
    const float Time=ServerTime();
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    const bool Playing=State && State->Phase==EMCShiftPhase::Working && !State->bDayOneComplete;
    if (HasAuthority() && Playing)
    {
        const auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>();
        if (Settings.bAutomaticJolts && Mode && Mode->bUseDayOnePlan && !State->bDevManualEvents && Time>=NextJoltAt) TriggerJolt();
    }
    // Ulcers persist through intermissions. An active surface motion keeps its
    // force until the run ends, even if the event that started it just completed.
    if (HasAuthority() && State && State->Phase!=EMCShiftPhase::Won && State->Phase!=EMCShiftPhase::Lost && !State->bDayOneComplete)
        PushMotion(Time-Motion.StartedAt);
    if (HasAuthority()) GatherPressure(Dt);
    UpdatePressureField(Dt);
    Deform(Time);
    // A deforming mesh has no component translation for CharacterMovement's usual based movement.
    for (const auto& Rider:Riders)
    {
        FHitResult Hit; if (Rider.Key->ToothPhysics->CanAct() && SurfacePoint(Rider.Key->GetActorLocation(),Hit))
            Rider.Key->AddActorWorldOffset(FVector(0,0,Hit.ImpactPoint.Z-Rider.Value),true);
    }
    if (HasAuthority())
    {
        // Sleeping rigid bodies otherwise retain contacts against the previous triangle positions.
        for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if (!It->IsDisposed() && It->Body->IsSimulatingPhysics())
        {
            FHitResult Hit; const FVector P=It->Body->Bounds.Origin;
            if (SurfacePoint(P,Hit) && FMath::Abs(float(It->Body->Bounds.GetBox().Min.Z-Hit.ImpactPoint.Z))<20) It->Body->WakeAllRigidBodies();
        }
    }
}
void AMCTongue::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMCTongue,Settings); DOREPLIFETIME(AMCTongue,Motion);
    DOREPLIFETIME(AMCTongue,PressureSettings); DOREPLIFETIME(AMCTongue,PressureFrame);
}
