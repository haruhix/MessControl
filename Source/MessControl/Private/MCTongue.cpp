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
#include "MCHazardWave.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

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
    IndentDepth.Init(0,Rest.Num()); IndentGradient.Init(FVector::ZeroVector,Rest.Num()); PressureHold.Init(0,Rest.Num());
    BuildDeformationSamples();
    BuildPressureGrid();
    Surface->ClearAllMeshSections();
    Surface->CreateMeshSection(0,Positions,Indices,Normals,UV,Colors,Tangents,true);
    RefreshPressureMaterial();
}
void AMCTongue::BeginPlay()
{
    Super::BeginPlay(); RebuildSurface();
    if (HasAuthority())
    {
        Settings=Profile?Profile->Settings:FMCTongueSettings(); Settings.Sanitize();
        ReloadPressureProfile();
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
    DeformationMotionSerial=INDEX_NONE;
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
float AMCTongue::Offset(FVector P,float Time,float& Red,TConstArrayView<FMCTongueMotionState> Pulses) const
{
    const float UnitY=(P.Y-RestBounds.Min.Y)/RestBounds.GetSize().Y;
    const float SurfaceMask=SurfaceWeight(P);
    const float Idle=SurfaceMask*Settings.IdleHeight*FMath::Sin(Time*2*PI/Settings.IdlePeriod+UnitY*1.2f);
    const auto& S=Motion.Settings; const float Age=Time-Motion.StartedAt;
    const float Amount=Motion.Serial>0?MotionWeight(P)*(S.IsWave()?S.Band(MotionDistance(P),Age):S.Envelope(Age)):0;
    Red=FMath::Max(0.f,Amount)*S.Redness;
    float Height=Idle+S.Height*Amount;
    if(YawnStartedAt>=0 && Time<YawnStartedAt+YawnDuration && Time>=YawnStartedAt)
        Height+=SurfaceMask*45*FMath::Sin(PI*(Time-YawnStartedAt)/FMath::Max(1.f,YawnDuration));
    // Independent ulcers can pulse together and during a larger tongue event.
    // They retain their own radius, server clock and damage; no second push is applied.
    const float PulseWeight=FMath::Sqrt(SurfaceMask);
    for(const auto& Pulse:Pulses)
    {
        const float Distance=GetActorTransform().TransformVector(P-Pulse.Origin).Size2D();
        const float PulseAmount=PulseWeight*Pulse.Settings.Band(Distance,Time-Pulse.StartedAt);
        Height+=Pulse.Settings.Height*PulseAmount;
        Red=FMath::Max(Red,FMath::Max(0.f,PulseAmount)*Pulse.Settings.Redness);
    }
    return Height;
}
void AMCTongue::BuildDeformationSamples()
{
    static const FVector Steps[]={FVector::ZeroVector,FVector(1,0,0),FVector(-1,0,0),FVector(0,1,0),FVector(0,-1,0),FVector(0,0,1),FVector(0,0,-1)};
    DeformationSamples.Reset(Rest.Num()*7);
    for (const FVector& Vertex:Rest) for (const FVector& Step:Steps)
    {
        FDeformationSample Sample; Sample.Point=Vertex+Step;
        Sample.SurfaceMask=SurfaceWeight(Sample.Point); Sample.PulseWeight=FMath::Sqrt(Sample.SurfaceMask);
        Sample.JoltMask=JoltWeight(Sample.Point);
        Sample.IdlePhase=float((Sample.Point.Y-RestBounds.Min.Y)/RestBounds.GetSize().Y)*1.2f;
        DeformationSamples.Add(Sample);
    }
    DeformationMotionSerial=INDEX_NONE;
    RefreshDeformationMotion();
}
void AMCTongue::RefreshDeformationMotion()
{
    // Reset and a new event can coalesce into one network update with the same
    // serial. Compare geometry inputs too so clients never retain the old pose.
    if (DeformationMotionSerial==Motion.Serial && DeformationTransform.Equals(GetActorTransform())
        && DeformationMotion.Origin.Equals(Motion.Origin) && DeformationMotion.Direction.Equals(Motion.Direction)
        && DeformationMotion.Settings.Shape==Motion.Settings.Shape && DeformationMotion.Settings.Radius==Motion.Settings.Radius) return;
    DeformationMotionSerial=Motion.Serial; DeformationTransform=GetActorTransform();
    DeformationMotion=Motion;
    for (FDeformationSample& Sample:DeformationSamples)
    {
        Sample.Distance=MotionDistance(Sample.Point);
        const auto& S=Motion.Settings;
        if (S.Shape==EMCTongueShape::FrontBend) Sample.MotionMask=Sample.JoltMask;
        else if (S.Shape==EMCTongueShape::LocalLift) Sample.MotionMask=Sample.PulseWeight*(1-FMath::SmoothStep(0.f,S.Radius,Sample.Distance));
        else if (S.Shape==EMCTongueShape::DirectionalWave)
        {
            const FVector Delta=DeformationTransform.TransformVector(Sample.Point-Motion.Origin);
            const FVector Direction=DeformationTransform.TransformVectorNoScale(Motion.Direction);
            const float Side=FMath::Abs(FVector::DotProduct(Delta,FVector::CrossProduct(Direction,FVector::UpVector)));
            Sample.MotionMask=Sample.PulseWeight*(1-FMath::SmoothStep(S.Radius*.6f,S.Radius,Side));
        }
        else Sample.MotionMask=Sample.PulseWeight;
    }
}
float AMCTongue::SampleOffset(const FDeformationSample& Sample,float Time,float IdleAngle,float Envelope,float YawnHeight,float& Red,TConstArrayView<FMCTongueMotionState> Pulses) const
{
    const auto& S=Motion.Settings;
    const float Amount=Motion.Serial>0?Sample.MotionMask*(S.IsWave()?S.Band(Sample.Distance,Time-Motion.StartedAt):Envelope):0;
    Red=FMath::Max(0.f,Amount)*S.Redness;
    float Height=Sample.SurfaceMask*Settings.IdleHeight*FMath::Sin(IdleAngle+Sample.IdlePhase)+S.Height*Amount;
    Height+=Sample.SurfaceMask*YawnHeight;
    for (const auto& Pulse:Pulses)
    {
        const float Distance=DeformationTransform.TransformVector(Sample.Point-Pulse.Origin).Size2D();
        const float PulseAmount=Sample.PulseWeight*Pulse.Settings.Band(Distance,Time-Pulse.StartedAt);
        Height+=Pulse.Settings.Height*PulseAmount;
        Red=FMath::Max(Red,FMath::Max(0.f,PulseAmount)*Pulse.Settings.Redness);
    }
    return Height;
}
void AMCTongue::Deform(float Time)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MCTongue_DeformAndCollision);
    TArray<FMCTongueMotionState,TInlineAllocator<6>> Pulses;
    for(TActorIterator<AMCHazardWave> It(GetWorld());It;++It)
    {
        FMCTongueMotionState Pulse;
        if(It->SurfaceMotion(this,Time,Pulse)) Pulses.Add(Pulse);
    }
    RefreshDeformationMotion();
    const float IdleAngle=Time*2*PI/Settings.IdlePeriod;
    const float Envelope=Motion.Settings.IsWave()?0:Motion.Settings.Envelope(Time-Motion.StartedAt);
    const float YawnHeight=YawnStartedAt>=0 && Time>=YawnStartedAt && Time<YawnStartedAt+YawnDuration
        ?45*FMath::Sin(PI*(Time-YawnStartedAt)/FMath::Max(1.f,YawnDuration)):0;
    const float ScaleZ=FMath::Abs(GetActorScale3D().Z);
    for (int32 I=0;I<Rest.Num();++I)
    {
        const FVector P=Rest[I]; float Red=0,Dummy=0;
        const FDeformationSample* Samples=&DeformationSamples[I*7];
        auto OffsetAt=[&](int32 J,float& R) { return SampleOffset(Samples[J],Time,IdleAngle,Envelope,YawnHeight,R,Pulses); };
        const float PhysicalHeight=OffsetAt(0,Red),Height=PhysicalHeight-IndentDepth[I],Anchor=AnchorWeights[I];
        // Events and weight share this buffer with Chaos. Never add a second
        // material-only displacement: it leaves feet/food above the visible dent.
        Positions[I]=P+FVector(0,0,Height*Anchor); Red*=Anchor;
        // Transform artist normals/tangents with the displacement gradient; keep UV seam smoothing.
        const float Dx=((OffsetAt(1,Dummy)-OffsetAt(2,Dummy))*.5f-IndentGradient[I].X)*Anchor+Height*AnchorGradients[I].X;
        const float Dy=((OffsetAt(3,Dummy)-OffsetAt(4,Dummy))*.5f-IndentGradient[I].Y)*Anchor+Height*AnchorGradients[I].Y;
        const float Dz=((OffsetAt(5,Dummy)-OffsetAt(6,Dummy))*.5f-IndentGradient[I].Z)*Anchor+Height*AnchorGradients[I].Z;
        const FVector N=RestNormals[I],T=RestTangents[I].TangentX;
        const float Nz=N.Z/FMath::Max(.5f,1+Dz);
        Normals[I]=FVector(N.X-Dx*Nz,N.Y-Dy*Nz,Nz).GetSafeNormal();
        Tangents[I]=FProcMeshTangent((T+FVector(0,0,Dx*T.X+Dy*T.Y+Dz*T.Z)).GetSafeNormal(),RestTangents[I].bFlipTangentY);
        // One pressure field drives geometry, physics and material masks. No UV1 displacement.
        const float Depth=IndentDepth[I]*Anchor*ScaleZ;
        const float Mask=FMath::Clamp(Depth/FMath::Max(1.f,PressureSettings.MaxDepth),0.f,1.f);
        const float Rim=FMath::Clamp(float((IndentGradient[I]*Anchor+IndentDepth[I]*AnchorGradients[I]).Size2D())*4,0.f,1.f);
        Colors[I]=FColor(FMath::RoundToInt(FMath::Clamp(Red,0.f,1.f)*255),FMath::RoundToInt(Mask*255),FMath::RoundToInt(Rim*255),255);
    }
    Surface->UpdateMeshSection(0,Positions,Normals,UV,Colors,Tangents);
}
bool AMCTongue::SurfacePoint(FVector P,FHitResult& Hit) const
{
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MCTongueSurface),true);
    Params.bReturnFaceIndex=true;
    return Surface->LineTraceComponent(Hit,P+FVector(0,0,1200),P-FVector(0,0,1600),Params);
}
bool AMCTongue::RestSurfacePoint(FVector P,FVector& Point) const
{
    const FTransform T=GetActorTransform();
    bool Found=false; double Height=-DBL_MAX;
    for(int32 I=0;I+2<Indices.Num();I+=3)
    {
        const FVector A=T.TransformPosition(Rest[Indices[I]]),B=T.TransformPosition(Rest[Indices[I+1]]),C=T.TransformPosition(Rest[Indices[I+2]]);
        if(P.X<FMath::Min3(A.X,B.X,C.X) || P.X>FMath::Max3(A.X,B.X,C.X)
            || P.Y<FMath::Min3(A.Y,B.Y,C.Y) || P.Y>FMath::Max3(A.Y,B.Y,C.Y)) continue;
        const double Det=(B.Y-C.Y)*(A.X-C.X)+(C.X-B.X)*(A.Y-C.Y);
        if(FMath::Abs(Det)<1.e-6) continue;
        const double U=((B.Y-C.Y)*(P.X-C.X)+(C.X-B.X)*(P.Y-C.Y))/Det;
        const double V=((C.Y-A.Y)*(P.X-C.X)+(A.X-C.X)*(P.Y-C.Y))/Det;
        if(U<-.0001 || V<-.0001 || U+V>1.0001) continue;
        Height=FMath::Max(Height,U*A.Z+V*B.Z+(1-U-V)*C.Z); Found=true;
    }
    if(Found) Point=FVector(P.X,P.Y,Height);
    return Found;
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
        if(bAutomaticYawns && !State->bDevManualEvents && Time>=NextYawnAt) StartYawn();
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
        // Accumulate displacement while a body sleeps, so slow breathing still
        // wakes it when needed without reactivating every contact on every tick.
        for(auto It=FoodSupports.CreateIterator();It;++It)
            if(!It.Key().IsValid() || It.Key()->IsDisposed() || !It.Key()->Body->IsSimulatingPhysics()) It.RemoveCurrent();
        for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if (!It->IsDisposed() && It->Body->IsSimulatingPhysics())
        {
            FHitResult Hit; const FVector P=It->Body->Bounds.Origin;
            if (!SurfacePoint(P,Hit) || FMath::Abs(float(It->Body->Bounds.GetBox().Min.Z-Hit.ImpactPoint.Z))>=20)
            { FoodSupports.Remove(*It); continue; }
            auto* Previous=FoodSupports.Find(*It);
            const bool Moved=Previous && (FVector::DistSquared(Previous->Point,Hit.ImpactPoint)>=FMath::Square(.25f)
                || FVector::DotProduct(Previous->Normal,Hit.ImpactNormal)<.99996f);
            if(Moved && !It->Body->IsAnyRigidBodyAwake()) It->Body->WakeAllRigidBodies();
            if(!Previous || Moved || It->Body->IsAnyRigidBodyAwake())
                FoodSupports.Add(*It,FFoodSupport{Hit.ImpactPoint,Hit.ImpactNormal});
        }
    }
}
void AMCTongue::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMCTongue,Settings); DOREPLIFETIME(AMCTongue,Motion);
    DOREPLIFETIME(AMCTongue,PressureSettings); DOREPLIFETIME(AMCTongue,PressureFrame);
    DOREPLIFETIME(AMCTongue,ActivePressurePreset);
    DOREPLIFETIME(AMCTongue,ActivePressureMaterial);
    DOREPLIFETIME(AMCTongue,YawnStartedAt);DOREPLIFETIME(AMCTongue,YawnDuration);
}
