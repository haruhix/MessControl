#include "MCTongue.h"
#include "KismetProceduralMeshLibrary.h"
#include "Engine/StaticMesh.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCFoodActor.h"
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
    Surface->ClearAllMeshSections();
    Surface->CreateMeshSection(0,Positions,Indices,Normals,UV,Colors,Tangents,true);
    Surface->SetMaterial(0,SurfaceMaterial?SurfaceMaterial.Get():SourceMesh->GetMaterial(0));
}
void AMCTongue::BeginPlay()
{
    Super::BeginPlay(); RebuildSurface();
    if (HasAuthority()) { Settings=Profile?Profile->Settings:FMCTongueSettings(); Settings.Sanitize(); ForceNetUpdate(); }
}
float AMCTongue::ServerTime() const
{
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    return GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
}
bool AMCTongue::TriggerPain(FVector Point)
{
    if (!HasAuthority() || Point.ContainsNaN() || (Pulse.Serial>0 && ServerTime()-Pulse.StartedAt<Settings.Cooldown)) return false;
    Pulse.Origin=GetActorTransform().InverseTransformPosition(Point);
    // A short anticipation gives clients time to receive the authoritative pulse.
    Pulse.StartedAt=ServerTime()+.2; ++Pulse.Serial;
    HitActors.Empty(); PreviousWaveAge=-1; ForceNetUpdate(); return true;
}
void AMCTongue::ResetPain()
{
    if (!HasAuthority()) return;
    Pulse=FMCTonguePulse(); HitActors.Empty(); PreviousWaveAge=-1; PlayerPushes=FoodPushes=0; ForceNetUpdate();
}
float AMCTongue::Offset(FVector P,float Time,float& Red) const
{
    const FVector Unit=(P-RestBounds.Min)/RestBounds.GetSize();
    // Fix the rim and underside to the original mouth. No cracks at the shell seam.
    const float Rim=FMath::Square(FMath::Sin(PI*FMath::Clamp(float(Unit.X),0.f,1.f)))
        *FMath::Square(FMath::Sin(PI*FMath::Clamp(float(Unit.Y),0.f,1.f)));
    const float Weight=Rim*FMath::SmoothStep(.15f,.75f,float(Unit.Z));
    const float Age=Time-Pulse.StartedAt;
    const float Distance=FVector::Dist2D(GetActorTransform().TransformPosition(P),GetActorTransform().TransformPosition(Pulse.Origin));
    Red=Pulse.Serial>0?Settings.Band(Distance,Age)*FMath::Sqrt(Weight):0;
    const float Idle=Settings.IdleHeight*FMath::Sin(Time*2*PI/Settings.IdlePeriod+Unit.Y*1.2f);
    return Weight*Idle+Settings.WaveHeight*Red;
}
void AMCTongue::Deform(float Time)
{
    for (int32 I=0;I<Rest.Num();++I)
    {
        const FVector P=Rest[I]; float Red=0,Dummy=0;
        const float Height=Offset(P,Time,Red),Anchor=AnchorWeights[I];
        Positions[I]=P+FVector(0,0,Height*Anchor); Red*=Anchor;
        // Transform artist normals/tangents with the displacement gradient; keep UV seam smoothing.
        const float Dx=(Offset(P+FVector(1,0,0),Time,Dummy)-Offset(P-FVector(1,0,0),Time,Dummy))*.5f*Anchor+Height*AnchorGradients[I].X;
        const float Dy=(Offset(P+FVector(0,1,0),Time,Dummy)-Offset(P-FVector(0,1,0),Time,Dummy))*.5f*Anchor+Height*AnchorGradients[I].Y;
        const float Dz=(Offset(P+FVector(0,0,1),Time,Dummy)-Offset(P-FVector(0,0,1),Time,Dummy))*.5f*Anchor+Height*AnchorGradients[I].Z;
        const FVector N=RestNormals[I],T=RestTangents[I].TangentX;
        const float Nz=N.Z/FMath::Max(.5f,1+Dz);
        Normals[I]=FVector(N.X-Dx*Nz,N.Y-Dy*Nz,Nz).GetSafeNormal();
        Tangents[I]=FProcMeshTangent((T+FVector(0,0,Dx*T.X+Dy*T.Y+Dz*T.Z)).GetSafeNormal(),RestTangents[I].bFlipTangentY);
        Colors[I]=FColor(FMath::RoundToInt(FMath::Clamp(Red,0.f,1.f)*255),0,0,255);
    }
    Surface->UpdateMeshSection(0,Positions,Normals,UV,Colors,Tangents);
}
bool AMCTongue::SurfacePoint(FVector P,FHitResult& Hit) const
{
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MCTongueSurface),true);
    return Surface->LineTraceComponent(Hit,P+FVector(0,0,1200),P-FVector(0,0,1600),Params);
}
void AMCTongue::PushWave(float Age)
{
    if (PreviousSerial!=Pulse.Serial) { PreviousSerial=Pulse.Serial; HitActors.Empty(); PreviousWaveAge=-1; }
    if (Pulse.Serial==0 || Age<0 || PreviousWaveAge>Settings.Duration()) return;
    const FVector Origin=GetActorTransform().TransformPosition(Pulse.Origin);
    auto Reached=[&](AActor* Actor,FVector P)
    {
        if (HitActors.Contains(Actor) || !Settings.Crossed(FVector::Dist2D(P,Origin),PreviousWaveAge,Age)) return false;
        FHitResult Hit;
        // The wave only affects objects near the tongue, not objects above it or beyond the throat.
        if (!SurfacePoint(P,Hit) || P.Z<Hit.ImpactPoint.Z-30 || P.Z>Hit.ImpactPoint.Z+Settings.AffectHeight) return false;
        HitActors.Add(Actor); return true;
    };
    for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        auto* Hero=*It; const FVector P=Hero->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll?Hero->ToothPhysics->PhysicalLocation():Hero->GetActorLocation();
        if (!Hero->Status->IsAlive() || !Reached(Hero,P)) continue;
        const FVector Away=(P-Origin).GetSafeNormal2D(KINDA_SMALL_NUMBER,FVector::ForwardVector);
        Hero->ToothPhysics->ApplyHit(Away*Settings.PushSpeed+FVector(0,0,Settings.LiftSpeed),P); ++PlayerPushes;
    }
    for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
    {
        auto* Food=*It; const FVector P=Food->Body->GetComponentLocation();
        if (Food->IsDisposed() || Food->Phase==EMCFoodPhase::Equipped || Food->Phase==EMCFoodPhase::Stuck || !Food->Body->IsSimulatingPhysics() || !Reached(Food,P)) continue;
        // Physical impulse, not velocity change: heavy food resists the same shove.
        const FVector Away=(P-Origin).GetSafeNormal2D(KINDA_SMALL_NUMBER,FVector::ForwardVector);
        Food->Body->AddImpulse((Away*Settings.PushSpeed+FVector(0,0,Settings.LiftSpeed))*4.f); ++FoodPushes;
    }
    PreviousWaveAge=Age;
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
    const float Time=ServerTime(); Deform(Time);
    // A deforming mesh has no component translation for CharacterMovement's usual based movement.
    for (const auto& Rider:Riders)
    {
        FHitResult Hit; if (SurfacePoint(Rider.Key->GetActorLocation(),Hit))
            Rider.Key->AddActorWorldOffset(FVector(0,0,Hit.ImpactPoint.Z-Rider.Value),true);
    }
    if (HasAuthority())
    {
        const auto* GS=GetWorld()->GetGameState<AMCGameState>();
        if (GS && GS->Phase!=EMCShiftPhase::Lost && GS->Phase!=EMCShiftPhase::Won && !GS->bDayOneComplete) PushWave(Time-Pulse.StartedAt);
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
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMCTongue,Settings); DOREPLIFETIME(AMCTongue,Pulse);
}
