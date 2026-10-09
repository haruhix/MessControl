#include "MCIceEvent.h"
#include "MCIceEventVFX.h"
#include "MCColdCola.h"
#include "MCFreezeBarWidget.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCInventoryComponent.h"
#include "MCLocomotionSurface.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr float MintRadius=180.f;

struct FIceGuideMesh
{
    TArray<FVector> Vertices,Normals;
    TArray<int32> Indices;
    TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;

    void Ring(AMCTongue* Tongue,const FTransform& Transform,FVector Center,float Radius,float Width,FLinearColor Color,bool bDashed=false)
    {
        constexpr int32 Segments=48;
        const int32 First=Vertices.Num();
        for(int32 I=0;I<=Segments;++I)
        {
            const float Angle=I*2*PI/Segments;
            const FVector Direction(FMath::Cos(Angle),FMath::Sin(Angle),0);
            for(int32 Side=0;Side<2;++Side)
            {
                const FVector Point=Center+Direction*(Radius+(Side?Width:-Width));
                FHitResult Floor;
                const bool bFloor=Tongue && Tongue->SurfacePoint(Point,Floor);
                const FVector Up=bFloor?Floor.ImpactNormal:FVector::UpVector;
                Vertices.Add(Transform.InverseTransformPosition((bFloor?Floor.ImpactPoint:Point)+Up*5));
                Normals.Add(Transform.InverseTransformVectorNoScale(Up).GetSafeNormal());
                UV.Add(FVector2D(I/float(Segments),Side)); Colors.Add(Color);
                Tangents.Add(FProcMeshTangent(Transform.InverseTransformVectorNoScale(Direction).GetSafeNormal(),false));
            }
            if(I>0 && (!bDashed || (I-1)%4<3))
            {
                const int32 A=First+(I-1)*2;
                Indices.Append({A,A+3,A+1,A,A+2,A+3});
            }
        }
    }

    void Strip(AMCTongue* Tongue,const FTransform& Transform,FVector A,FVector B,float Width,FLinearColor Color,float Height=6)
    {
        const FVector Across=FVector::CrossProduct((B-A).GetSafeNormal2D(),FVector::UpVector)*Width;
        const int32 First=Vertices.Num();
        for(FVector Point:{A-Across,A+Across,B-Across,B+Across})
        {
            FHitResult Floor;
            if(Tongue && !Tongue->SurfacePoint(Point,Floor)) return;
            const FVector Up=Tongue?Floor.ImpactNormal:FVector::UpVector;
            Vertices.Add(Transform.InverseTransformPosition((Tongue?Floor.ImpactPoint:Point)+Up*Height));
            Normals.Add(Transform.InverseTransformVectorNoScale(Up).GetSafeNormal());
            UV.Add(FVector2D::ZeroVector); Colors.Add(Color);
            Tangents.Add(FProcMeshTangent(Transform.InverseTransformVectorNoScale((B-A).GetSafeNormal()),false));
        }
        Indices.Append({First,First+3,First+1,First,First+2,First+3});
    }
};
}

AMCIceEvent::AMCIceEvent()
{
    bReplicates=true; bAlwaysRelevant=true;
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.bStartWithTickEnabled=false;
    SetNetUpdateFrequency(10); SetMinNetUpdateFrequency(5);
    Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MintCandy")); SetRootComponent(Body);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Colors(TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Ice(TEXT("/Game/Art/Materials/ice/MI_Ice.MI_Ice"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Coating(TEXT("/Game/Gameplay/Cold/M_PlayerIceCoating.M_PlayerIceCoating"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> TongueFrost(TEXT("/Game/Gameplay/Cold/Frost/M_TongueFrost.M_TongueFrost"));
    static ConstructorHelpers::FObjectFinder<UMaterialParameterCollection> MouthClimate(TEXT("/Game/Gameplay/Cold/MPC_MouthClimate.MPC_MouthClimate"));
    Body->SetStaticMesh(Cone.Object); Body->SetMaterial(0,Ice.Object);
    ConeMesh=Cone.Object; GuideMaterial=Colors.Object; IcicleMaterial=Ice.Object; Climate=MouthClimate.Object;
    PlayerIceMaterial=Coating.Object;
    Body->SetCollisionProfileName(TEXT("BlockAllDynamic")); Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore); Body->SetVisibility(false);
    CandyFace=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MintStripes")); CandyFace->SetupAttachment(Body);
    CandyFace->SetCollisionEnabled(ECollisionEnabled::NoCollision); CandyFace->SetMaterial(0,GuideMaterial);
    FloorGuides=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WarmthAndIcicleGuides")); FloorGuides->SetupAttachment(Body);
    FloorGuides->SetCollisionEnabled(ECollisionEnabled::NoCollision); FloorGuides->SetCastShadow(false);
    FloorGuides->SetAbsolute(true,true,true);
    FloorGuides->SetMaterial(0,GuideMaterial);
    FrostSurface=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FrozenTongue")); FrostSurface->SetupAttachment(Body);
    FrostSurface->SetAbsolute(true,true,true); FrostSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FrostSurface->SetCastShadow(false); FrostSurface->SetMaterial(0,TongueFrost.Object);
    NovaWind=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FrostNovaWind")); NovaWind->SetupAttachment(Body);
    NovaWind->SetAbsolute(true,true,true); NovaWind->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    NovaWind->SetCastShadow(false); NovaWind->SetMaterial(0,GuideMaterial);
}

void AMCIceEvent::BeginPlay()
{
    Super::BeginPlay();
    if(GetNetMode()!=NM_DedicatedServer) BuildCandyFace();
    OnRep_State();
}

double AMCIceEvent::Now() const
{
    const auto* State=GetWorld()?GetWorld()->GetGameState():nullptr;
    return State?State->GetServerWorldTimeSeconds():GetWorld()?GetWorld()->GetTimeSeconds():0;
}

bool AMCIceEvent::AnchorFloor(FVector Anchor,FHitResult& Hit) const
{
    return IsValid(Tongue) && !Tongue->IsActorBeingDestroyed()
        && Tongue->SurfacePoint(Tongue->GetActorTransform().TransformPosition(Anchor),Hit);
}

void AMCIceEvent::Start()
{
    if(!HasAuthority()) return;
    Stop(); bFailed=false;
    // Keep a single winter owner even when F3 starts it during another slot.
    for(TActorIterator<AMCIceEvent> It(GetWorld());It;++It)
        if(*It!=this && It->IsActive()) { It->Stop(); It->Destroy(); }
    Tongue=nullptr;
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It)
        if(!It->CurrentVertices().IsEmpty()) { Tongue=*It; break; }
    if(!IsValid(Tongue)) { Finish(true); return; }
    int32 TeamSize=0;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        if(It->GetController() && It->Status && It->Status->IsAlive()) ++TeamSize;
    MaxCandyHealth=(FMath::IsFinite(CandyHealthPerPlayer)?FMath::Clamp(CandyHealthPerPlayer,1.f,10000.f):960.f)*FMath::Clamp(TeamSize,1,8);
    ArrivalSeconds=FMath::IsFinite(ArrivalSeconds)?FMath::Clamp(ArrivalSeconds,1.f,10.f):3.f;
    FreezeSeconds=FMath::IsFinite(FreezeSeconds)?FMath::Clamp(FreezeSeconds,4.f,60.f):12.f;
    ThawSeconds=FMath::IsFinite(ThawSeconds)?FMath::Clamp(ThawSeconds,1.f,20.f):4.f;
    CircleSeconds=FMath::IsFinite(CircleSeconds)?FMath::Clamp(CircleSeconds,20.f,120.f):45.f;
    MinCircleSeconds=FMath::IsFinite(MinCircleSeconds)?FMath::Clamp(MinCircleSeconds,20.f,CircleSeconds):20.f;
    CircleStepSeconds=FMath::IsFinite(CircleStepSeconds)?FMath::Clamp(CircleStepSeconds,0.f,30.f):5.f;
    CircleOverlapSeconds=FMath::IsFinite(CircleOverlapSeconds)?FMath::Clamp(CircleOverlapSeconds,1.f,MinCircleSeconds*.5f):10.f;
    CircleRadius=FMath::IsFinite(CircleRadius)?FMath::Clamp(CircleRadius,100.f,450.f):330.f;
    MinCircleRadius=FMath::IsFinite(MinCircleRadius)?FMath::Clamp(MinCircleRadius,80.f,CircleRadius):200.f;
    IcicleWarningSeconds=FMath::IsFinite(IcicleWarningSeconds)?FMath::Clamp(IcicleWarningSeconds,1.f,5.f):2.2f;
    IcicleSeriesSpacingSeconds=FMath::IsFinite(IcicleSeriesSpacingSeconds)?FMath::Clamp(IcicleSeriesSpacingSeconds,.5f,4.f):1.2f;
    IcicleIntervalSeconds=FMath::IsFinite(IcicleIntervalSeconds)?FMath::Clamp(IcicleIntervalSeconds,IcicleSeriesSpacingSeconds*2+1,300.f):12.f;
    IcicleRadius=FMath::IsFinite(IcicleRadius)?FMath::Clamp(IcicleRadius,50.f,220.f):155.f;
    IcicleDamage=FMath::IsFinite(IcicleDamage)?FMath::Clamp(IcicleDamage,0.f,100.f):35.f;
    ZoneCrystalIntervalSeconds=FMath::IsFinite(ZoneCrystalIntervalSeconds)?FMath::Clamp(ZoneCrystalIntervalSeconds,4.f,300.f):16.f;
    ZoneCrystalHealth=FMath::IsFinite(ZoneCrystalHealth)?FMath::Clamp(ZoneCrystalHealth,1.f,500.f):80.f;
    NovaIntervalSeconds=FMath::IsFinite(NovaIntervalSeconds)?FMath::Clamp(NovaIntervalSeconds,4.f,300.f):18.f;
    NovaWarningSeconds=FMath::IsFinite(NovaWarningSeconds)?FMath::Clamp(NovaWarningSeconds,1.f,6.f):2.5f;
    NovaRange=FMath::IsFinite(NovaRange)?FMath::Clamp(NovaRange,100.f,5000.f):2600.f;
    NovaHalfAngleDegrees=FMath::IsFinite(NovaHalfAngleDegrees)?FMath::Clamp(NovaHalfAngleDegrees,10.f,70.f):35.f;
    NovaFootHealth=FMath::IsFinite(NovaFootHealth)?FMath::Clamp(NovaFootHealth,1.f,500.f):80.f;
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    Random.Initialize((State?State->RunSeed:41)^0x1ce2026);
    FHitResult Floor;
    const FVector Center=Tongue->Surface->Bounds.Origin;
    if(!Tongue->InteriorSurfacePoint(Center,MintRadius+35,Floor)
        && !Tongue->RandomInteriorPoint(Random,MintRadius+35,0,TArray<FVector>(),Floor)) { Finish(true); return; }
    CandyAnchor=Tongue->GetActorTransform().InverseTransformPosition(Floor.ImpactPoint);
    CandyHealth=MaxCandyHealth; CircleIndex=0; StrikeSerial=0;
    if(!ChooseCircle(SafeAnchor,true)) { Finish(true); return; }
    StartedAt=Now(); CircleStartedAt=StartedAt+ArrivalSeconds; NextIcicleAt=CircleStartedAt+5;
    NextCircleStartedAt=0; NextZoneCrystalAt=CircleStartedAt+ZoneCrystalIntervalSeconds;
    NextNovaAt=CircleStartedAt+NovaIntervalSeconds;
    Stage=EMCIceEventStage::Arrival;
    const FBox Bounds=Tongue->Surface->Bounds.GetBox();
    SlipperyFloor=GetWorld()->SpawnActor<AMCLocomotionSurface>(Bounds.GetCenter(),FRotator::ZeroRotator);
    if(SlipperyFloor)
    {
        SlipperyFloor->HalfExtent=Bounds.GetExtent()+FVector(0,0,700);
        SlipperyFloor->Priority=150; SlipperyFloor->Surface=EMCGroundSurface::Slippery;
        SlipperyFloor->RefreshBounds(); SlipperyFloor->ForceNetUpdate();
    }
    OnRep_State(); RefreshPresentation(.1f); PublishManualHUD(); ForceNetUpdate();
}

float AMCIceEvent::FrostAmount() const
{
    return IsActive()?FMath::Clamp(float((Now()-StartedAt)/FMath::Max(1.f,ArrivalSeconds)),0.f,1.f):0;
}

float AMCIceEvent::CircleDuration(int32 Index) const
{
    return FMath::Max(MinCircleSeconds,CircleSeconds-CircleStepSeconds*FMath::Max(0,Index));
}

float AMCIceEvent::SafeRadius() const
{
    const float Age=FMath::Clamp(float((Now()-CircleStartedAt)/FMath::Max(1.f,CircleDuration(CircleIndex))),0.f,1.f);
    return FMath::Lerp(CircleRadius,MinCircleRadius,Age);
}

float AMCIceEvent::NextSafeRadius() const
{
    const float Age=bNextCircle?FMath::Clamp(float((Now()-NextCircleStartedAt)/FMath::Max(1.f,CircleDuration(CircleIndex+1))),0.f,1.f):0;
    return FMath::Lerp(CircleRadius,MinCircleRadius,Age);
}

bool AMCIceEvent::IsCircleBlocked(int32 Index) const
{
    return ZoneCrystals.ContainsByPredicate([Index](const FMCIceZoneCrystal& Crystal)
        {return Crystal.ZoneIndex==Index && Crystal.Health>0;});
}

bool AMCIceEvent::IsSafePoint(FVector WorldPoint) const
{
    if(!IsActive() || WorldPoint.ContainsNaN()) return false;
    auto Inside=[&](FVector Anchor,float Radius)
    {
        FHitResult Floor;
        return AnchorFloor(Anchor,Floor)
            && FVector::DistSquaredXY(WorldPoint,Floor.ImpactPoint)<=FMath::Square(Radius)
            && FMath::Abs(WorldPoint.Z-Floor.ImpactPoint.Z)<150;
    };
    const double Time=Now();
    return (Time<CircleStartedAt+CircleDuration(CircleIndex) && !IsCircleBlocked(CircleIndex) && Inside(SafeAnchor,SafeRadius()))
        || (bNextCircle && Time<NextCircleStartedAt+CircleDuration(CircleIndex+1)
            && !IsCircleBlocked(CircleIndex+1) && Inside(NextSafeAnchor,NextSafeRadius()));
}

bool AMCIceEvent::ChooseCircle(FVector& Anchor,bool bFirst)
{
    if(!IsValid(Tongue)) return false;
    FHitResult Candy,Previous,Floor;
    if(!AnchorFloor(CandyAnchor,Candy)) return false;
    const bool bPrevious=!bFirst && AnchorFloor(SafeAnchor,Previous);
    const FBox Bounds=Tongue->Surface->Bounds.GetBox();
    const float Reach=float(Bounds.GetExtent().Size2D());
    TArray<FVector> Candidates;
    const float Offset=Random.FRandRange(-PI,PI);
    // Sample the actual tissue, excluding throat/brush delivery lanes. An inset
    // of the bounds alone would let a zone hang over a concave tongue edge.
    for(int32 Ray=0;Ray<48;++Ray)
    {
        const float Angle=Offset+Ray*2*PI/48;
        const FVector Direction(FMath::Cos(Angle),FMath::Sin(Angle),0);
        for(int32 Step=20;Step>0;--Step)
        {
            const FVector Point=Candy.ImpactPoint+Direction*(Reach*Step/20);
            if(!Tongue->GameplaySpawnFootprint(Point,CircleRadius,Floor)) continue;
            Candidates.Add(Floor.ImpactPoint);
            break;
        }
    }
    // The tongue can be asymmetric. Its farthest edge alone may put every
    // candidate on the same side, so require a real crossing wherever the
    // supported surface permits two distinct full-radius zones.
    const double SeparationSquared=FMath::Square(CircleRadius*2.f);
    const bool bSeparated=bPrevious && Candidates.ContainsByPredicate([&](const FVector& Point)
        {return FVector::DistSquaredXY(Point,Previous.ImpactPoint)>=SeparationSquared;});
    double Farthest=0;
    for(const FVector& Point:Candidates)
    {
        if(bSeparated && FVector::DistSquaredXY(Point,Previous.ImpactPoint)<SeparationSquared) continue;
        Farthest=FMath::Max(Farthest,FVector::DistSquaredXY(Point,Candy.ImpactPoint));
    }
    double Best=-1;
    for(const FVector& Point:Candidates)
    {
        if(bSeparated && FVector::DistSquaredXY(Point,Previous.ImpactPoint)<SeparationSquared) continue;
        if(FVector::DistSquaredXY(Point,Candy.ImpactPoint)<Farthest*.8) continue;
        const double Score=bPrevious?FVector::DistSquaredXY(Point,Previous.ImpactPoint):Random.FRand();
        if(Score>Best) {Best=Score; Anchor=Tongue->GetActorTransform().InverseTransformPosition(Point);}
    }
    if(Best>=0) return true;
    // A small test/replacement arena may not have authored gameplay bands.
    if(Tongue->InteriorSurfacePoint(Candy.ImpactPoint,CircleRadius,Floor)) {Anchor=CandyAnchor; return true;}
    return false;
}

void AMCIceEvent::UpdateCircles(double Time)
{
    bool bChanged=false;
    for(int32 Catchup=0;Catchup<32;++Catchup)
    {
        const double Ends=CircleStartedAt+CircleDuration(CircleIndex);
        if(!bNextCircle && Time>=Ends-CircleOverlapSeconds)
        {
            bNextCircle=ChooseCircle(NextSafeAnchor);
            if(bNextCircle) NextCircleStartedAt=Ends-CircleOverlapSeconds;
            bChanged=true;
        }
        if(Time<Ends) break;
        if(!bNextCircle) {Finish(true); return;}
        SafeAnchor=NextSafeAnchor; CircleStartedAt=NextCircleStartedAt;
        bNextCircle=false; ++CircleIndex; bChanged=true;
        ZoneCrystals.RemoveAll([this](const FMCIceZoneCrystal& Crystal){return Crystal.ZoneIndex<CircleIndex;});
    }
    if(bChanged) ForceNetUpdate();
}

void AMCIceEvent::QueueZoneCrystal()
{
    TArray<int32> Eligible;
    if(!IsCircleBlocked(CircleIndex) && Now()<CircleStartedAt+CircleDuration(CircleIndex)-4) Eligible.Add(CircleIndex);
    if(bNextCircle && !IsCircleBlocked(CircleIndex+1)) Eligible.Add(CircleIndex+1);
    if(Eligible.IsEmpty()) return;
    const int32 Index=Eligible[Random.RandRange(0,Eligible.Num()-1)];
    FHitResult Floor;
    if(!AnchorFloor(Index==CircleIndex?SafeAnchor:NextSafeAnchor,Floor)) return;
    auto& Crystal=ZoneCrystals.AddDefaulted_GetRef();
    Crystal.ZoneIndex=Index; Crystal.Health=ZoneCrystalHealth;
    Crystal.Anchor=Tongue->GetActorTransform().InverseTransformPosition(Floor.ImpactPoint);
    ForceNetUpdate();
}

bool AMCIceEvent::HitObstructionWithPickaxe(AMCToothCharacter* Hero,float Damage,FVector& Point)
{
    if(!HasAuthority() || Stage!=EMCIceEventStage::Active || !IsValid(Hero) || !Hero->CanWork()
        || !Hero->Inventory || Hero->Inventory->Selected!=EMCToolSlot::Pickaxe || !FMath::IsFinite(Damage) || Damage<=0) return false;
    int32 Best=INDEX_NONE; double Distance=FMath::Square(180.f);
    for(int32 I=0;I<ZoneCrystals.Num();++I)
    {
        const auto& Crystal=ZoneCrystals[I]; FHitResult Floor;
        if(Crystal.Health<=0 || !AnchorFloor(Crystal.Anchor,Floor)) continue;
        const FVector Center=Floor.ImpactPoint+Floor.ImpactNormal*85;
        const FVector Contact=FBox(Center-FVector(65,65,85),Center+FVector(65,65,85)).GetClosestPointTo(Hero->GetActorLocation());
        const FVector Offset=Contact-Hero->GetActorLocation();
        if(Offset.SizeSquared()>Distance || FVector::DotProduct(Offset.GetSafeNormal2D(),Hero->GetActorForwardVector())<.25f) continue;
        FHitResult Obstacle; FCollisionQueryParams Query(SCENE_QUERY_STAT(MCIceCrystalContact),false,Hero); Query.AddIgnoredActor(this);
        if(GetWorld()->LineTraceSingleByChannel(Obstacle,Hero->GetActorLocation(),Contact,ECC_Visibility,Query)) continue;
        Best=I; Distance=Offset.SizeSquared(); Point=Contact;
    }
    if(Best==INDEX_NONE) return false;
    auto& Crystal=ZoneCrystals[Best]; Crystal.Health=FMath::Max(0.f,Crystal.Health-Damage);
    if(Crystal.Health<=0) {Hero->NotifyTaskFeedback(true,Point); ZoneCrystals.RemoveAt(Best);}
    RefreshZoneCrystals(); ForceNetUpdate(); return true;
}

float AMCIceEvent::FreezeAmount(const AMCToothCharacter* Hero) const
{
    for(const auto& Player:Players) if(Player.Hero==Hero) return Player.Amount;
    return 0;
}

void AMCIceEvent::UpdateFreeze(float DeltaSeconds)
{
    TArray<FMCPlayerFreeze> Updated;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        auto* Hero=*It;
        if(!Hero->GetController() || !Hero->Status || !Hero->Status->IsAlive() || Hero->IsActorBeingDestroyed()
            || Hero->SwallowedBy || Hero->IsMimicCaptured()) continue;
        auto& Player=Updated.AddDefaulted_GetRef(); Player.Hero=Hero;
        const FVector Feet=Hero->GetActorLocation()-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        Player.bSafe=IsSafePoint(Feet);
        Player.Amount=FMath::Clamp(FreezeAmount(Hero)+DeltaSeconds*(Player.bSafe?-1/FMath::Max(1.f,ThawSeconds):1/FMath::Max(1.f,FreezeSeconds)),0.f,1.f);
        if(Player.Amount>=1)
        {
            Hero->NotifyTaskFeedback(false,Hero->GetActorLocation());
            Hero->Status->Damage(Hero->Status->State.MaxHealth,FVector::UpVector);
        }
    }
    Players=MoveTemp(Updated);
    FreezeSendElapsed+=DeltaSeconds;
    if(FreezeSendElapsed>=.1f) { FreezeSendElapsed=0; PublishManualHUD(); ForceNetUpdate(); }
}

AMCToothCharacter* AMCIceEvent::ChooseTarget()
{
    TArray<AMCToothCharacter*> Candidates;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        FHitResult Floor;
        if(!It->GetController() || !It->Status || !It->Status->IsAlive() || It->SwallowedBy || It->IsMimicCaptured()) continue;
        const FVector Feet=It->GetActorLocation()-FVector(0,0,It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        if(Tongue->SurfacePoint(Feet,Floor) && FMath::Abs(Feet.Z-Floor.ImpactPoint.Z)<150) Candidates.Add(*It);
    }
    return Candidates.IsEmpty()?nullptr:Candidates[Random.RandRange(0,Candidates.Num()-1)];
}

void AMCIceEvent::QueueIcicle(AMCToothCharacter* Target)
{
    FHitResult Floor;
    if(!IsValid(Target) || !Target->Status || !Target->Status->IsAlive() || Target->SwallowedBy || Target->IsMimicCaptured()
        || !Tongue->SurfacePoint(Target->GetActorLocation(),Floor)) return;
    auto& Strike=Strikes.AddDefaulted_GetRef();
    Strike.Anchor=Tongue->GetActorTransform().InverseTransformPosition(Floor.ImpactPoint);
    Strike.ImpactAt=Now()+IcicleWarningSeconds; Strike.Id=++StrikeSerial;
    ForceNetUpdate();
}

void AMCIceEvent::UpdateIcicleSeries(double Time)
{
    if(SeriesShotsLeft==0 && Time>=NextIcicleAt)
    {
        SeriesTarget=ChooseTarget(); SeriesShotsLeft=3; NextSeriesShotAt=Time;
        NextIcicleAt=Time+IcicleIntervalSeconds;
    }
    // At most one fresh warning per frame: a hitch must not turn a sequential
    // series into three simultaneous unavoidable strikes at the same position.
    if(SeriesShotsLeft>0 && Time>=NextSeriesShotAt)
    {
        QueueIcicle(SeriesTarget.Get()); --SeriesShotsLeft;
        NextSeriesShotAt=Time+IcicleSeriesSpacingSeconds;
    }
}

void AMCIceEvent::BeginNova()
{
    auto* Target=ChooseTarget(); FHitResult Floor;
    if(!Target || !AnchorFloor(CandyAnchor,Floor)) {NextNovaAt=Now()+NovaIntervalSeconds; return;}
    const FVector WorldDirection=(Target->GetActorLocation()-Floor.ImpactPoint).GetSafeNormal2D();
    NovaDirection=Tongue->GetActorTransform().InverseTransformVectorNoScale(WorldDirection).GetSafeNormal();
    if(NovaDirection.IsNearlyZero()) NovaDirection=FVector::ForwardVector;
    bNovaWarning=true; NovaImpactAt=Now()+NovaWarningSeconds;
    NextNovaAt=NovaImpactAt+NovaIntervalSeconds; ForceNetUpdate();
}

bool AMCIceEvent::IsInsideNova(FVector WorldPoint) const
{
    FHitResult Center,Floor;
    if(!IsActive() || WorldPoint.ContainsNaN() || !AnchorFloor(CandyAnchor,Center)
        || !Tongue->SurfacePoint(WorldPoint,Floor) || FMath::Abs(WorldPoint.Z-Floor.ImpactPoint.Z)>150) return false;
    const FVector Offset=Floor.ImpactPoint-Center.ImpactPoint;
    const FVector Direction=Tongue->GetActorTransform().TransformVectorNoScale(NovaDirection).GetSafeNormal2D();
    return Offset.SizeSquared2D()<=FMath::Square(NovaRange)
        && (Offset.IsNearlyZero() || FVector::DotProduct(Offset.GetSafeNormal2D(),Direction)>=FMath::Cos(FMath::DegreesToRadians(NovaHalfAngleDegrees)));
}

void AMCIceEvent::ResolveNova()
{
    if(!HasAuthority() || !bNovaWarning) return;
    bNovaWarning=false; FHitResult Center;
    if(!AnchorFloor(CandyAnchor,Center)) return;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        auto* Hero=*It;
        if(!Hero->GetController() || !Hero->CanWork() || Hero->SwallowedBy || Hero->IsMimicCaptured()) continue;
        const FVector Feet=Hero->GetActorLocation()-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        if(!IsInsideNova(Feet)) continue;
        FHitResult Obstacle; FCollisionQueryParams Query(SCENE_QUERY_STAT(MCIceNovaContact),false,this);
        Query.AddIgnoredActor(Tongue); Query.AddIgnoredActor(Hero);
        if(GetWorld()->LineTraceSingleByChannel(Obstacle,Center.ImpactPoint+Center.ImpactNormal*100,Hero->GetActorLocation(),ECC_Visibility,Query)) continue;
        Hero->FreezeLegs(NovaFootHealth);
        if(Hero->HasFrozenLegs()) FrozenLegHeroes.AddUnique(Hero);
    }
    ForceNetUpdate();
}

void AMCIceEvent::ResolveIcicle(FMCIcicleStrike& Strike)
{
    if(!HasAuthority() || Strike.bImpacted) return;
    Strike.bImpacted=true;
    FHitResult Floor;
    if(!AnchorFloor(Strike.Anchor,Floor)) return;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        auto* Hero=*It;
        if(!Hero->GetController() || !Hero->Status || !Hero->Status->IsAlive() || Hero->SwallowedBy || Hero->IsMimicCaptured()) continue;
        const FVector Feet=Hero->GetActorLocation()-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        const float Reach=IcicleRadius+Hero->GetCapsuleComponent()->GetScaledCapsuleRadius();
        if(FVector::DistSquaredXY(Feet,Floor.ImpactPoint)>FMath::Square(Reach) || FMath::Abs(Feet.Z-Floor.ImpactPoint.Z)>260) continue;
        FHitResult Obstacle;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MCIcicleContact),false,this);
        Query.AddIgnoredActor(Tongue); Query.AddIgnoredActor(Hero);
        if(GetWorld()->LineTraceSingleByChannel(Obstacle,Floor.ImpactPoint+Floor.ImpactNormal*45,Hero->GetActorLocation(),ECC_Visibility,Query)) continue;
        FVector Direction=(Hero->GetActorLocation()-Floor.ImpactPoint).GetSafeNormal2D();
        if(Direction.IsNearlyZero()) Direction=FVector::ForwardVector;
        if(Hero->Status->Damage(IcicleDamage,Direction) && Hero->ToothPhysics)
            Hero->ToothPhysics->ApplyHit(Direction*220+Floor.ImpactNormal*90,Hero->GetActorLocation());
    }
    ForceNetUpdate();
}

FVector AMCIceEvent::CandyContactPoint(FVector From) const
{
    FVector Point;
    if(Body->GetClosestPointOnCollision(From,Point)>=0) return Point;
    return Body->Bounds.GetBox().GetClosestPointTo(From);
}

bool AMCIceEvent::HitWithPickaxe(AMCToothCharacter* Hero,float Damage)
{
    if(!HasAuthority() || Stage!=EMCIceEventStage::Active || !IsValid(Hero) || !Hero->CanWork()
        || !Hero->Inventory || Hero->Inventory->Selected!=EMCToolSlot::Pickaxe || !FMath::IsFinite(Damage) || Damage<=0) return false;
    const FVector Offset=CandyContactPoint(Hero->GetActorLocation())-Hero->GetActorLocation();
    if(Offset.SizeSquared()>FMath::Square(180.f) || FVector::DotProduct(Offset.GetSafeNormal2D(),Hero->GetActorForwardVector())<.25f
        || !Hero->CanContact(this)) return false;
    CandyHealth=FMath::Max(0.f,CandyHealth-Damage); ForceNetUpdate();
    if(CandyHealth<=0)
    {
        Hero->NotifyTaskFeedback(true,GetActorLocation());
        if(auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>()) Mode->AwardTask(Hero,EMCScoreTask::Ice);
        Finish();
    }
    return true;
}

void AMCIceEvent::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(!IsActive()) { OnRep_State(); return; }
    if(HasAuthority())
    {
        const auto* State=GetWorld()->GetGameState<AMCGameState>();
        if(!IsValid(Tongue) || Tongue->IsActorBeingDestroyed()
            || (State && (State->Phase==EMCShiftPhase::Won || State->Phase==EMCShiftPhase::Lost))) { Finish(true); return; }
        const double Time=Now();
        if(Stage==EMCIceEventStage::Arrival && Time>=StartedAt+ArrivalSeconds) { Stage=EMCIceEventStage::Active; OnRep_State(); ForceNetUpdate(); }
        if(Stage==EMCIceEventStage::Active)
        {
            if(CandyHealth<=0) { Finish(); return; }
            UpdateCircles(Time);
            if(!IsActive()) return;
            if(Time>=NextZoneCrystalAt) {QueueZoneCrystal(); NextZoneCrystalAt=Time+ZoneCrystalIntervalSeconds;}
            if(bNovaWarning && Time>=NovaImpactAt) ResolveNova();
            else if(!bNovaWarning && Time>=NextNovaAt) BeginNova();
            UpdateFreeze(DeltaSeconds);
            UpdateIcicleSeries(Time);
            for(auto& Strike:Strikes) if(!Strike.bImpacted && Time>=Strike.ImpactAt) ResolveIcicle(Strike);
            Strikes.RemoveAll([Time](const FMCIcicleStrike& Strike){ return Time>Strike.ImpactAt+1; });
        }
    }
    RefreshPresentation(DeltaSeconds);
}

void AMCIceEvent::BuildCandyFace()
{
    FIceGuideMesh Mesh;
    constexpr int32 Segments=64;
    for(int32 Side=0;Side<2;++Side)
    {
        const float Sign=Side?1.f:-1.f;
        for(int32 I=0;I<Segments;++I)
        {
            const float A=I*2*PI/Segments,B=(I+1)*2*PI/Segments;
            const FLinearColor Color=(I/4)%2?FLinearColor(.16f,.86f,.63f):FLinearColor(.97f,1.f,.98f);
            const int32 First=Mesh.Vertices.Num();
            Mesh.Vertices.Append({FVector(0,0,Sign*50.2f),FVector(FMath::Cos(A)*50,FMath::Sin(A)*50,Sign*50.2f),FVector(FMath::Cos(B)*50,FMath::Sin(B)*50,Sign*50.2f)});
            for(int32 J=0;J<3;++J)
            { Mesh.Normals.Add(FVector(0,0,Sign)); Mesh.UV.Add(FVector2D::ZeroVector); Mesh.Colors.Add(Color); Mesh.Tangents.Add(FProcMeshTangent(FVector::ForwardVector,false)); }
            if(Side) Mesh.Indices.Append({First,First+2,First+1}); else Mesh.Indices.Append({First,First+1,First+2});
        }
    }
    CandyFace->CreateMeshSection_LinearColor(0,Mesh.Vertices,Mesh.Indices,Mesh.Normals,Mesh.UV,Mesh.Colors,Mesh.Tangents,false);
}

void AMCIceEvent::RefreshFloorGuides()
{
    if(!IsActive()) { FloorGuides->ClearAllMeshSections(); return; }
    FIceGuideMesh Mesh; FHitResult Floor;
    const FTransform Transform=FloorGuides->GetComponentTransform();
    const double Time=Now();
    const float WarmPulse=.88f+.12f*FMath::Sin(float(Time)*2.4f);
    if(AnchorFloor(SafeAnchor,Floor))
        Mesh.Ring(Tongue,Transform,Floor.ImpactPoint,SafeRadius(),7,
            (IsCircleBlocked(CircleIndex)?FLinearColor(.12f,.5f,1.f):FLinearColor(1.f,.58f,.17f))*WarmPulse,
            IsCircleBlocked(CircleIndex));
    if(bNextCircle && AnchorFloor(NextSafeAnchor,Floor))
        Mesh.Ring(Tongue,Transform,Floor.ImpactPoint,NextSafeRadius(),7,
            (IsCircleBlocked(CircleIndex+1)?FLinearColor(.12f,.5f,1.f):FLinearColor(1.f,.74f,.27f))*WarmPulse,
            IsCircleBlocked(CircleIndex+1));
    for(const auto& Strike:Strikes)
    {
        if(!AnchorFloor(Strike.Anchor,Floor) || Time>Strike.ImpactAt+1) continue;
        const float Progress=FMath::Clamp(1.f-float((Strike.ImpactAt-Time)/FMath::Max(1.f,IcicleWarningSeconds)),0.f,1.f);
        const float Pulse=.82f+.18f*FMath::Sin(Progress*PI*(4+Progress*6));
        const FLinearColor Color=Strike.bImpacted?FLinearColor(.25f,.8f,1.f):FLinearColor(1.f,.12f+.18f*Progress,.08f)*Pulse;
        Mesh.Ring(Tongue,Transform,Floor.ImpactPoint,IcicleRadius,6+2*Progress,Color,!Strike.bImpacted);
        if(!Strike.bImpacted) Mesh.Ring(Tongue,Transform,Floor.ImpactPoint,FMath::Max(8.f,IcicleRadius*Progress),3,Color);
    }
    if(NovaImpactAt>0 && Time<NovaImpactAt+.8 && AnchorFloor(CandyAnchor,Floor))
    {
        const FVector Direction=Tongue->GetActorTransform().TransformVectorNoScale(NovaDirection).GetSafeNormal2D();
        const FVector Center=Floor.ImpactPoint;
        const FLinearColor Color=bNovaWarning?FLinearColor(.32f,.85f,1.f):FLinearColor(.85f,.97f,1.f);
        for(float Angle:{-NovaHalfAngleDegrees,NovaHalfAngleDegrees})
        {
            const FVector Ray=Direction.RotateAngleAxis(Angle,FVector::UpVector);
            for(int32 Step=1;Step<12;++Step)
                Mesh.Strip(Tongue,Transform,Center+Ray*(NovaRange*Step/12),Center+Ray*(NovaRange*(Step+1)/12),8,Color);
        }
        // Crossbars give the cone a visible area, rather than a single line.
        const FVector Side=FVector::CrossProduct(Direction,FVector::UpVector);
        for(int32 Step=1;Step<=8;++Step)
        {
            const float Distance=NovaRange*Step/9,Width=Distance*FMath::Tan(FMath::DegreesToRadians(NovaHalfAngleDegrees));
            const FVector Middle=Center+Direction*Distance;
            Mesh.Strip(Tongue,Transform,Middle-Side*Width,Middle+Side*Width,2,Color);
        }
    }
    FloorGuides->CreateMeshSection_LinearColor(0,Mesh.Vertices,Mesh.Indices,Mesh.Normals,Mesh.UV,Mesh.Colors,Mesh.Tangents,false);
}

void AMCIceEvent::RefreshZoneCrystals()
{
    while(ZoneCrystalMeshes.Num()<ZoneCrystals.Num())
    {
        auto* Part=NewObject<UStaticMeshComponent>(this); Part->SetupAttachment(Body);
        Part->SetStaticMesh(ConeMesh); Part->SetMaterial(0,IcicleMaterial);
        Part->SetCollisionProfileName(TEXT("NoCollision"));
        Part->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
        Part->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Part->SetGenerateOverlapEvents(false); Part->SetCastShadow(true);
        Part->RegisterComponent(); AddInstanceComponent(Part); ZoneCrystalMeshes.Add(Part);
    }
    for(int32 I=0;I<ZoneCrystalMeshes.Num();++I)
    {
        FHitResult Floor; auto* Part=ZoneCrystalMeshes[I].Get();
        const bool bVisible=IsActive() && ZoneCrystals.IsValidIndex(I) && ZoneCrystals[I].Health>0 && AnchorFloor(ZoneCrystals[I].Anchor,Floor);
        Part->SetVisibility(bVisible && !(IsValid(EventVFX) && EventVFX->HasCrystalVisuals()));
        Part->SetCollisionEnabled(bVisible?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
        if(!bVisible) continue;
        Part->SetWorldLocationAndRotation(Floor.ImpactPoint+Floor.ImpactNormal*85,Floor.ImpactNormal.Rotation()+FRotator(-90,0,0));
        Part->SetWorldScale3D(FVector(1.3f,1.3f,1.7f));
    }
}

void AMCIceEvent::RefreshNovaWind()
{
    if(IsValid(EventVFX) && EventVFX->HasWindVisuals())
    {
        if(NovaWind->GetNumSections()>0) NovaWind->ClearAllMeshSections();
        return;
    }
    const double Time=Now(); FHitResult Floor;
    if(NovaImpactAt<=0 || Time>NovaImpactAt+.8 || !AnchorFloor(CandyAnchor,Floor))
    {if(NovaWind->GetNumSections()>0) NovaWind->ClearAllMeshSections(); return;}
    FIceGuideMesh Mesh;
    const FTransform Transform=NovaWind->GetComponentTransform();
    const FVector Center=Floor.ImpactPoint;
    const FVector Direction=Tongue->GetActorTransform().TransformVectorNoScale(NovaDirection).GetSafeNormal2D();
    const float Age=float(Time-NovaImpactAt+NovaWarningSeconds);
    for(int32 Ribbon=0;Ribbon<24;++Ribbon)
    {
        const float Across=(Ribbon%8)/7.f*2-1;
        const FVector Ray=Direction.RotateAngleAxis(Across*NovaHalfAngleDegrees*.92f,FVector::UpVector);
        const float Travel=FMath::Frac(Age*(bNovaWarning?.55f:1.2f)+Ribbon*.618f);
        const float Distance=MintRadius+Travel*FMath::Max(1.f,NovaRange-MintRadius-220);
        const FVector A=Center+Ray*Distance,B=A+Ray*180;
        Mesh.Strip(Tongue,Transform,A,B,bNovaWarning?3.f:8.f,
            FLinearColor(.65f+.3f*Travel,.87f+.1f*Travel,1.f),30+(Ribbon/8)*65);
    }
    if(Mesh.Indices.IsEmpty()) {if(NovaWind->GetNumSections()>0) NovaWind->ClearAllMeshSections();}
    else NovaWind->CreateMeshSection_LinearColor(0,Mesh.Vertices,Mesh.Indices,Mesh.Normals,Mesh.UV,Mesh.Colors,Mesh.Tangents,false);
}

void AMCIceEvent::RefreshFreezeBars()
{
    for(int32 I=FreezeBars.Num()-1;I>=0;--I)
    {
        const auto* Hero=BarHeroes[I].Get();
        if(!Hero || !Hero->Status || !Hero->Status->IsAlive() || !Players.ContainsByPredicate([Hero](const FMCPlayerFreeze& Player){return Player.Hero==Hero;}))
        { if(IsValid(FreezeBars[I])) FreezeBars[I]->DestroyComponent(); FreezeBars.RemoveAt(I); BarHeroes.RemoveAt(I); }
    }
    for(const auto& Player:Players)
    {
        auto* Hero=Player.Hero.Get();
        if(!IsValid(Hero) || !Hero->Status || !Hero->Status->IsAlive()) continue;
        int32 Index=BarHeroes.IndexOfByPredicate([Hero](const TWeakObjectPtr<AMCToothCharacter>& Item){return Item.Get()==Hero;});
        if(Index==INDEX_NONE)
        {
            auto* Bar=NewObject<UWidgetComponent>(this);
            Bar->SetupAttachment(Hero->GetRootComponent()); Bar->SetWidgetSpace(EWidgetSpace::Screen);
            Bar->SetDrawSize(FVector2D(220,52)); Bar->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Bar->SetPivot(FVector2D(.5f,1.f));
            Bar->SetRelativeLocation(FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+170));
            Bar->SetWidgetClass(UMCFreezeBarWidget::StaticClass()); Bar->RegisterComponent(); AddInstanceComponent(Bar);
            Index=FreezeBars.Add(Bar); BarHeroes.Add(Hero); Bar->InitWidget();
        }
        if(auto* Widget=Cast<UMCFreezeBarWidget>(FreezeBars[Index]->GetUserWidgetObject())) Widget->SetFreeze(Player.Amount,Player.bSafe);
    }
}

void AMCIceEvent::RefreshIceCoatings()
{
    // This is local presentation of the existing replicated freeze meter.
    // A leader-pose shell shares the final animated/ragdoll pose and leaves all
    // body materials (including the player's dirt and face) in place.
    for(int32 I=IceCoatings.Num()-1;I>=0;--I)
    {
        auto* Part=IceCoatings[I].Get();
        auto* Source=IsValid(Part)?Cast<USkeletalMeshComponent>(Part->GetAttachParent()):nullptr;
        auto* Hero=Source?Cast<AMCToothCharacter>(Source->GetOwner()):nullptr;
        if(!IsValid(Hero) || Hero->IsActorBeingDestroyed() || !Hero->Status || !Hero->Status->IsAlive()
            || Source!=Hero->GetMesh() || !Source->GetSkeletalMeshAsset() || FreezeAmount(Hero)<=.002f)
        {
            if(IsValid(Hero)) RemoveTickPrerequisiteActor(Hero);
            if(IsValid(Part)) Part->DestroyComponent();
            IceCoatings.RemoveAt(I);
        }
    }
    if(!PlayerIceMaterial) return;
    for(const auto& Player:Players)
    {
        auto* Hero=Player.Hero.Get();
        if(!IsValid(Hero) || Hero->IsActorBeingDestroyed() || !Hero->Status || !Hero->Status->IsAlive()
            || !FMath::IsFinite(Player.Amount) || Player.Amount<=.002f) continue;
        auto* Source=Hero->GetMesh();
        if(!Source || !Source->GetSkeletalMeshAsset()) continue;
        auto* Found=IceCoatings.FindByPredicate([Source](const TObjectPtr<USkeletalMeshComponent>& Part)
            { return IsValid(Part.Get()) && Part->GetAttachParent()==Source; });
        USkeletalMeshComponent* Part=Found?Found->Get():nullptr;
        if(!Part)
        {
            Part=NewObject<USkeletalMeshComponent>(this);
            Part->ComponentTags.Add(TEXT("MC_PlayerIceCoating"));
            Part->SetupAttachment(Source);
            Part->SetSkeletalMeshAsset(Source->GetSkeletalMeshAsset());
            Part->SetLeaderPoseComponent(Source,true);
            Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Part->SetGenerateOverlapEvents(false); Part->SetCastShadow(false);
            Part->bReceivesDecals=false;
            Part->PrimaryComponentTick.bCanEverTick=false;
            AddTickPrerequisiteActor(Hero);
            Part->RegisterComponent(); AddInstanceComponent(Part); IceCoatings.Add(Part);
        }
        if(Part->GetSkeletalMeshAsset()!=Source->GetSkeletalMeshAsset())
            Part->SetSkeletalMeshAsset(Source->GetSkeletalMeshAsset());
        Part->SetVisibility(Source->IsVisible() && !Source->bHiddenInGame && !Hero->IsHidden());
        for(int32 Slot=0;Slot<Source->GetNumMaterials();++Slot)
        {
            auto* Material=Cast<UMaterialInstanceDynamic>(Part->GetMaterial(Slot));
            if(!Material || !Material->IsChildOf(PlayerIceMaterial))
            {
                Material=UMaterialInstanceDynamic::Create(PlayerIceMaterial,Part);
                Part->SetMaterial(Slot,Material);
            }
            Material->SetScalarParameterValue(TEXT("IceAmount"),FMath::Clamp(Player.Amount,0.f,1.f));
            // Leader pose already carries bones and morphs. Shader deformation
            // must also match the body, after its locomotion update this frame.
            static const FName ShapeParameters[]={TEXT("BodyStretch"),TEXT("Damage"),TEXT("DamageChipDepth")};
            const auto* SourceMaterial=Source->GetMaterial(Slot);
            for(FName Parameter:ShapeParameters)
            {
                float Value=0.f;
                if(SourceMaterial) SourceMaterial->GetScalarParameterValue(FMaterialParameterInfo(Parameter),Value);
                Material->SetScalarParameterValue(Parameter,Value);
            }
        }
    }
}

void AMCIceEvent::RefreshFrostSurface()
{
    if(!IsActive() || !IsValid(Tongue)) { FrostSurface->ClearAllMeshSections(); return; }
    const FProcMeshSection* SourceSection=Tongue->Surface->GetProcMeshSection(0);
    const auto& SourceIndices=Tongue->TriangleIndices();
    if(!SourceSection || SourceSection->ProcVertexBuffer.IsEmpty() || SourceIndices.IsEmpty())
    { FrostSurface->ClearAllMeshSections(); return; }
    AddTickPrerequisiteActor(Tongue);

    auto* Material=Cast<UMaterialInstanceDynamic>(FrostSurface->GetMaterial(0));
    if(!Material)
    {
        // Loading also supports existing editor instances after Live Coding;
        // the constructor holds the saved material reference for cooked games.
        auto* Parent=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/Cold/Frost/M_TongueFrost.M_TongueFrost"));
        if(!Parent) return;
        Material=FrostSurface->CreateDynamicMaterialInstance(0,Parent);
    }
    if(!Material) return;
    FHitResult SafeFloor,NextFloor;
    const bool bSafe=!IsCircleBlocked(CircleIndex) && AnchorFloor(SafeAnchor,SafeFloor);
    const bool bNext=bNextCircle && !IsCircleBlocked(CircleIndex+1) && AnchorFloor(NextSafeAnchor,NextFloor);
    Material->SetScalarParameterValue(TEXT("IceAmount"),FrostAmount());
    Material->SetScalarParameterValue(TEXT("Warm Enabled"),bSafe?1.f:0.f);
    Material->SetScalarParameterValue(TEXT("Warm Radius"),SafeRadius());
    Material->SetVectorParameterValue(TEXT("Warm Center"),FLinearColor(bSafe?SafeFloor.ImpactPoint:FVector::ZeroVector));
    Material->SetScalarParameterValue(TEXT("Next Warm Enabled"),bNext?1.f:0.f);
    Material->SetScalarParameterValue(TEXT("Next Warm Radius"),NextSafeRadius());
    Material->SetVectorParameterValue(TEXT("Next Warm Center"),FLinearColor(bNext?NextFloor.ImpactPoint:FVector::ZeroVector));

    // Copy the current surface pose, normals and authored UVs so the frost
    // follows breathing/pressure without sliding across the tongue.
    // The material fades warmth per pixel; triangle-sized cutouts are unnecessary.
    FIceGuideMesh Mesh;
    const int32 Count=SourceSection->ProcVertexBuffer.Num();
    Mesh.Vertices.Reserve(Count); Mesh.Normals.Reserve(Count);
    Mesh.UV.Reserve(Count); Mesh.Colors.Reserve(Count); Mesh.Tangents.Reserve(Count);
    const FTransform Transform=FrostSurface->GetComponentTransform();
    const FTransform TongueTransform=Tongue->Surface->GetComponentTransform();
    for(const FProcMeshVertex& Vertex:SourceSection->ProcVertexBuffer)
    {
        const FVector Normal=TongueTransform.TransformVectorNoScale(Vertex.Normal).GetSafeNormal();
        const FVector Point=TongueTransform.TransformPosition(Vertex.Position)+Normal*2.5f;
        Mesh.Vertices.Add(Transform.InverseTransformPosition(Point));
        Mesh.Normals.Add(Transform.InverseTransformVectorNoScale(Normal).GetSafeNormal());
        Mesh.UV.Add(Vertex.UV0); Mesh.Colors.Add(FLinearColor::White);
        const FVector Tangent=TongueTransform.TransformVectorNoScale(Vertex.Tangent.TangentX);
        Mesh.Tangents.Add(FProcMeshTangent(Transform.InverseTransformVectorNoScale(Tangent).GetSafeNormal(),Vertex.Tangent.bFlipTangentY));
    }
    const FProcMeshSection* Existing=FrostSurface->GetProcMeshSection(0);
    if(Existing && Existing->ProcVertexBuffer.Num()==Count && Existing->ProcIndexBuffer.Num()==SourceIndices.Num())
        FrostSurface->UpdateMeshSection_LinearColor(0,Mesh.Vertices,Mesh.Normals,Mesh.UV,Mesh.Colors,Mesh.Tangents);
    else
        FrostSurface->CreateMeshSection_LinearColor(0,Mesh.Vertices,SourceIndices,Mesh.Normals,Mesh.UV,Mesh.Colors,Mesh.Tangents,false);
}

void AMCIceEvent::RefreshPresentation(float DeltaSeconds)
{
    SetClimate();
    FHitResult Floor;
    const bool bCandyFloor=AnchorFloor(CandyAnchor,Floor);
    if(bCandyFloor)
    {
        const float Arrival=FMath::Clamp(float((Now()-StartedAt)/FMath::Max(1.f,ArrivalSeconds)),0.f,1.f);
        const float Height=240+FMath::Square(1-Arrival)*600;
        SetActorLocationAndRotation(Floor.ImpactPoint+Floor.ImpactNormal*Height,Floor.ImpactNormal.Rotation()+FRotator(-90,0,0));
        SetActorScale3D(FVector(MintRadius/50,MintRadius/50,4.8f));
    }
    Body->SetCollisionEnabled(bCandyFloor && Stage==EMCIceEventStage::Active?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
    RefreshZoneCrystals();
    if(GetNetMode()==NM_DedicatedServer) return;
    const bool bVisible=IsActive(); Body->SetVisibility(bVisible); CandyFace->SetVisibility(false);
    if(bVisible)
    {
        EnsurePresentationVFX();
        if(IsValid(EventVFX)) EventVFX->UpdateFromEvent(*this,DeltaSeconds,Now());
        Body->SetVisibility(!(IsValid(EventVFX) && EventVFX->HasCrystalVisuals()));
    }
    RefreshFrostSurface();
    GuideElapsed+=DeltaSeconds;
    if(GuideElapsed>=.1f) { GuideElapsed=0; RefreshFloorGuides(); }
    while(IcicleMeshes.Num()<Strikes.Num())
    {
        auto* Part=NewObject<UStaticMeshComponent>(this); Part->SetupAttachment(Body);
        Part->SetStaticMesh(ConeMesh); Part->SetMaterial(0,IcicleMaterial); Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetCastShadow(false); Part->RegisterComponent(); AddInstanceComponent(Part); IcicleMeshes.Add(Part);
    }
    const double Time=Now();
    for(int32 I=0;I<IcicleMeshes.Num();++I)
    {
        const bool bShow=Strikes.IsValidIndex(I) && AnchorFloor(Strikes[I].Anchor,Floor)
            && Time>=Strikes[I].ImpactAt-.55 && Time<Strikes[I].ImpactAt+.18f;
        UStaticMeshComponent* Part=IcicleMeshes[I]; Part->SetVisibility(bShow);
        if(!bShow) continue;
        const float Fall=FMath::Clamp(1-float((Strikes[I].ImpactAt-Time)/.45),0.f,1.f);
        const float Height=220+(1-Fall*Fall)*620;
        Part->SetWorldLocationAndRotation(Floor.ImpactPoint+Floor.ImpactNormal*Height,FRotator(180,0,0));
        Part->SetWorldScale3D(FVector(1.1f,1.1f,4.4f));
    }
    RefreshFreezeBars();
    RefreshIceCoatings();
    RefreshNovaWind();
}

void AMCIceEvent::SetClimate()
{
    if(GetNetMode()==NM_DedicatedServer || !Climate) return;
    float Amount=IsActorBeingDestroyed()?0:FrostAmount();
    for(TActorIterator<AMCColdColaEvent> It(GetWorld());It;++It)
        if(!It->IsActorBeingDestroyed()) Amount=FMath::Max(Amount,It->FrostAmount());
    for(TActorIterator<AMCIceEvent> It(GetWorld());It;++It)
        if(*It!=this && !It->IsActorBeingDestroyed()) Amount=FMath::Max(Amount,It->FrostAmount());
    if(auto* Instance=GetWorld()->GetParameterCollectionInstance(Climate)) Instance->SetScalarParameterValue(TEXT("ColdAmount"),Amount);
}

void AMCIceEvent::PublishManualHUD()
{
    auto* State=GetWorld()->GetGameState<AMCGameState>();
    if(!HasAuthority() || !State || !State->bDevManualEvents || !ActorHasTag(TEXT("MC_DevKeyEvent"))) return;
    auto& Status=State->DirectorState;
    const FString Title=IsComplete()?TEXT("ЛЕДЯНОЙ КРИСТАЛЛ РАЗБИТ"):bFailed?TEXT("ЗИМА НЕ ЗАПУСТИЛАСЬ"):TEXT("ЗИМА БЛИЗКО");
    const FString Instruction=IsComplete()?TEXT("Лёд растаял. Выбери следующее событие в F3."):
        Stage==EMCIceEventStage::Arrival?TEXT("Падает ледяной кристалл. Найди тёплую зону у края; приготовь кирку (слот 2)."):
        TEXT("Разбей центральный кристалл. Перебегай между тёплыми зонами; разбивай мешающие согреванию кристаллы и лёд на ногах. Уклоняйся от трёх сосулек и конуса бури.");
    const int32 Left=IsComplete()?0:FMath::CeilToInt(CandyHealth),Total=FMath::CeilToInt(MaxCandyHealth);
    if(Status.bEnabled && Status.CurrentTitle==Title && Status.Instruction==Instruction && State->TasksLeft==Left && State->TasksTotal==Total) return;
    Status.bEnabled=true; Status.CurrentTitle=Title; Status.Instruction=Instruction; Status.NextTitle.Empty();
    Status.SpawnedFood=Total; Status.FinishedFood=FMath::Max(0,Total-Left);
    Status.CompletionProgress=Total>0?1-float(Left)/Total:0;
    State->TasksLeft=Left; State->TasksTotal=Total; State->ForceNetUpdate();
}

void AMCIceEvent::ClearGameplay()
{
    if(IsValid(SlipperyFloor)) SlipperyFloor->Destroy(); SlipperyFloor=nullptr;
    for(const auto& Hero:FrozenLegHeroes) if(Hero.IsValid()) Hero->ClearFrozenLegs();
    for(const auto& Player:Players) if(IsValid(Player.Hero)) Player.Hero->ClearFrozenLegs();
    FrozenLegHeroes.Reset();
    Players.Reset(); Strikes.Reset(); ZoneCrystals.Reset(); bNextCircle=false; bNovaWarning=false; NovaImpactAt=0;
    SeriesTarget.Reset(); SeriesShotsLeft=0; NextIcicleAt=NextSeriesShotAt=NextZoneCrystalAt=NextNovaAt=0;
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AMCIceEvent::ClearPresentation()
{
    if(IsValid(EventVFX))
    {
        // The reliable completion cue is issued before state/actor removal. A
        // state-only late join never replays a historical shatter.
        if(!bCompletionVFXSignaled) {EventVFX->Destroy(); EventVFX=nullptr;}
    }
    if(IsValid(Tongue)) RemoveTickPrerequisiteActor(Tongue);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetVisibility(false); CandyFace->SetVisibility(false); FloorGuides->ClearAllMeshSections(); FrostSurface->ClearAllMeshSections(); NovaWind->ClearAllMeshSections();
    for(UStaticMeshComponent* Part:IcicleMeshes) if(IsValid(Part)) Part->DestroyComponent(); IcicleMeshes.Reset();
    for(UStaticMeshComponent* Part:ZoneCrystalMeshes) if(IsValid(Part)) Part->DestroyComponent(); ZoneCrystalMeshes.Reset();
    for(UWidgetComponent* Bar:FreezeBars) if(IsValid(Bar)) Bar->DestroyComponent(); FreezeBars.Reset(); BarHeroes.Reset();
    for(USkeletalMeshComponent* Part:IceCoatings) if(IsValid(Part))
    {
        if(auto* Source=Part->GetAttachParent())
            if(auto* Hero=Source->GetOwner()) RemoveTickPrerequisiteActor(Hero);
        Part->DestroyComponent();
    }
    IceCoatings.Reset();
    SetClimate();
}

void AMCIceEvent::OnRep_State()
{
    if(Stage==EMCIceEventStage::Arrival && bCompletionVFXSignaled)
    {
        // Start also supports reuse of this actor after successful completion.
        // Release the former local cue before presentation of the new run.
        bCompletionVFXSignaled=false;
        if(IsValid(EventVFX)) EventVFX->Destroy();
        EventVFX=nullptr;
    }
    if(Stage==EMCIceEventStage::Idle) bCompletionVFXSignaled=false;
    SetActorTickEnabled(IsActive());
    if(!IsActive()) ClearPresentation();
}

void AMCIceEvent::EnsurePresentationVFX()
{
    if(GetNetMode()==NM_DedicatedServer || IsValid(EventVFX) || !GetWorld() || GetWorld()->bIsTearingDown) return;
    FActorSpawnParameters Params;
    Params.ObjectFlags|=RF_Transient;
    Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    EventVFX=GetWorld()->SpawnActor<AMCIceEventVFX>(FVector::ZeroVector,FRotator::ZeroRotator,Params);
}

void AMCIceEvent::Multicast_FinishVFX_Implementation(FVector_NetQuantize CoreFloorPoint)
{
    if(bCompletionVFXSignaled) return;
    bCompletionVFXSignaled=true;
    if(GetNetMode()==NM_DedicatedServer || !GetWorld() || GetWorld()->bIsTearingDown) return;
    EnsurePresentationVFX();
    if(IsValid(EventVFX)) EventVFX->FinishPresentation(CoreFloorPoint);
}

void AMCIceEvent::Multicast_CancelVFX_Implementation()
{
    bCompletionVFXSignaled=false;
    if(IsValid(EventVFX)) EventVFX->Destroy();
    EventVFX=nullptr;
}

void AMCIceEvent::Finish(bool bFailure)
{
    if(!HasAuthority()) return;
    if(!bFailure)
    {
        FHitResult Floor;
        const FVector Point=AnchorFloor(CandyAnchor,Floor)?Floor.ImpactPoint:GetActorLocation()-FVector(0,0,240);
        Multicast_FinishVFX(Point);
    }
    bFailed=bFailure; Stage=EMCIceEventStage::Complete;
    ClearGameplay(); OnRep_State(); PublishManualHUD(); ForceNetUpdate();
}

void AMCIceEvent::Stop()
{
    if(!HasAuthority()) return;
    // Cancel is reliable too: a Stop immediately followed by Destroy cannot
    // leave a former success cue playing merely because Idle never replicated.
    Multicast_CancelVFX();
    Stage=EMCIceEventStage::Idle; ClearGameplay(); OnRep_State(); ForceNetUpdate();
}

void AMCIceEvent::EndPlay(const EEndPlayReason::Type Reason)
{
    // A successful event may be removed by its director immediately. Its local
    // one-shot shatter already has a short lifespan; cancellation keeps ownership
    // so ClearPresentation kills every loop and retained pooled burst instead.
    if(Reason==EEndPlayReason::Destroyed && bCompletionVFXSignaled && IsValid(EventVFX)) EventVFX=nullptr;
    else bCompletionVFXSignaled=false;
    // Tear down the local climate contribution even on streaming removal on a client.
    Stage=EMCIceEventStage::Idle;
    if(HasAuthority()) ClearGameplay();
    ClearPresentation(); Super::EndPlay(Reason);
}

void AMCIceEvent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCIceEvent,Stage); DOREPLIFETIME(AMCIceEvent,bFailed);
    DOREPLIFETIME(AMCIceEvent,CandyHealth); DOREPLIFETIME(AMCIceEvent,MaxCandyHealth);
    DOREPLIFETIME(AMCIceEvent,ArrivalSeconds); DOREPLIFETIME(AMCIceEvent,FreezeSeconds); DOREPLIFETIME(AMCIceEvent,ThawSeconds);
    DOREPLIFETIME(AMCIceEvent,CircleSeconds); DOREPLIFETIME(AMCIceEvent,CircleStepSeconds); DOREPLIFETIME(AMCIceEvent,MinCircleSeconds);
    DOREPLIFETIME(AMCIceEvent,CircleOverlapSeconds);
    DOREPLIFETIME(AMCIceEvent,CircleRadius); DOREPLIFETIME(AMCIceEvent,MinCircleRadius);
    DOREPLIFETIME(AMCIceEvent,IcicleWarningSeconds); DOREPLIFETIME(AMCIceEvent,IcicleIntervalSeconds);
    DOREPLIFETIME(AMCIceEvent,IcicleSeriesSpacingSeconds);
    DOREPLIFETIME(AMCIceEvent,IcicleRadius); DOREPLIFETIME(AMCIceEvent,IcicleDamage);
    DOREPLIFETIME(AMCIceEvent,ZoneCrystalIntervalSeconds); DOREPLIFETIME(AMCIceEvent,ZoneCrystalHealth);
    DOREPLIFETIME(AMCIceEvent,NovaIntervalSeconds); DOREPLIFETIME(AMCIceEvent,NovaWarningSeconds);
    DOREPLIFETIME(AMCIceEvent,NovaRange); DOREPLIFETIME(AMCIceEvent,NovaHalfAngleDegrees); DOREPLIFETIME(AMCIceEvent,NovaFootHealth);
    DOREPLIFETIME(AMCIceEvent,Tongue); DOREPLIFETIME(AMCIceEvent,CandyAnchor);
    DOREPLIFETIME(AMCIceEvent,SafeAnchor); DOREPLIFETIME(AMCIceEvent,NextSafeAnchor); DOREPLIFETIME(AMCIceEvent,bNextCircle);
    DOREPLIFETIME(AMCIceEvent,StartedAt); DOREPLIFETIME(AMCIceEvent,CircleStartedAt); DOREPLIFETIME(AMCIceEvent,CircleIndex);
    DOREPLIFETIME(AMCIceEvent,NextCircleStartedAt); DOREPLIFETIME(AMCIceEvent,ZoneCrystals);
    DOREPLIFETIME(AMCIceEvent,bNovaWarning); DOREPLIFETIME(AMCIceEvent,NovaImpactAt); DOREPLIFETIME(AMCIceEvent,NovaDirection);
    DOREPLIFETIME(AMCIceEvent,Players); DOREPLIFETIME(AMCIceEvent,Strikes);
}
