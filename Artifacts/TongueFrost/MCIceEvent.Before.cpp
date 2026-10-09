#include "MCIceEvent.h"
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
constexpr float CircleTravel=520.f;

struct FIceGuideMesh
{
    TArray<FVector> Vertices,Normals;
    TArray<int32> Indices;
    TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;

    void Ring(AMCTongue* Tongue,const FTransform& Transform,FVector Center,float Radius,float Width,FLinearColor Color)
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
            if(I>0)
            {
                const int32 A=First+(I-1)*2;
                Indices.Append({A,A+3,A+1,A,A+2,A+3});
            }
        }
    }
};
}

AMCIceEvent::AMCIceEvent()
{
    bReplicates=true; bAlwaysRelevant=true;
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.bStartWithTickEnabled=false;
    SetNetUpdateFrequency(10); SetMinNetUpdateFrequency(5);
    Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MintCandy")); SetRootComponent(Body);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Colors(TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Ice(TEXT("/Game/Gameplay/Cold/M_StylizedIce.M_StylizedIce"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Coating(TEXT("/Game/Gameplay/Cold/M_PlayerIceCoating.M_PlayerIceCoating"));
    static ConstructorHelpers::FObjectFinder<UMaterialParameterCollection> MouthClimate(TEXT("/Game/Gameplay/Cold/MPC_MouthClimate.MPC_MouthClimate"));
    Body->SetStaticMesh(Cylinder.Object); ConeMesh=Cone.Object; GuideMaterial=Colors.Object; IcicleMaterial=Ice.Object; Climate=MouthClimate.Object;
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
    FrostSurface->SetCastShadow(false); FrostSurface->SetMaterial(0,GuideMaterial);
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
    CircleSeconds=FMath::IsFinite(CircleSeconds)?FMath::Clamp(CircleSeconds,6.f,60.f):14.f;
    CircleOverlapSeconds=FMath::IsFinite(CircleOverlapSeconds)?FMath::Clamp(CircleOverlapSeconds,1.f,CircleSeconds*.5f):4.f;
    CircleRadius=FMath::IsFinite(CircleRadius)?FMath::Clamp(CircleRadius,100.f,450.f):330.f;
    MinCircleRadius=FMath::IsFinite(MinCircleRadius)?FMath::Clamp(MinCircleRadius,80.f,CircleRadius):200.f;
    IcicleWarningSeconds=FMath::IsFinite(IcicleWarningSeconds)?FMath::Clamp(IcicleWarningSeconds,1.f,5.f):2.2f;
    IcicleIntervalSeconds=FMath::IsFinite(IcicleIntervalSeconds)?FMath::Clamp(IcicleIntervalSeconds,3.f,20.f):5.5f;
    IcicleRadius=FMath::IsFinite(IcicleRadius)?FMath::Clamp(IcicleRadius,50.f,220.f):155.f;
    IcicleDamage=FMath::IsFinite(IcicleDamage)?FMath::Clamp(IcicleDamage,0.f,100.f):35.f;
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

float AMCIceEvent::SafeRadius() const
{
    const float Age=FMath::Clamp(float((Now()-CircleStartedAt)/FMath::Max(1.f,CircleSeconds)),0.f,1.f);
    return FMath::Lerp(CircleRadius,MinCircleRadius,Age);
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
    return Inside(SafeAnchor,SafeRadius()) || (bNextCircle && Inside(NextSafeAnchor,CircleRadius));
}

bool AMCIceEvent::ChooseCircle(FVector& Anchor,bool bFirst)
{
    if(!IsValid(Tongue)) return false;
    FHitResult Floor;
    if(bFirst)
    {
        for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
            if(It->GetController() && It->Status && It->Status->IsAlive()
                && Tongue->InteriorSurfacePoint(It->GetActorLocation(),CircleRadius,Floor))
            { Anchor=Tongue->GetActorTransform().InverseTransformPosition(Floor.ImpactPoint); return true; }
    }
    FHitResult Current,Candy;
    if(!AnchorFloor(bFirst?CandyAnchor:SafeAnchor,Current) || !AnchorFloor(CandyAnchor,Candy)) return false;
    // Each second transfer moves back toward the mint for an attack window.
    const float DistanceToCandy=float(FVector::DistXY(Current.ImpactPoint,Candy.ImpactPoint));
    const FVector Origin=!bFirst && CircleIndex%2 && DistanceToCandy<=700?Candy.ImpactPoint:Current.ImpactPoint;
    for(int32 Try=0;Try<32;++Try)
    {
        const float Angle=Random.FRandRange(-PI,PI),Distance=Random.FRandRange(180.f,CircleTravel);
        const FVector Candidate=Try==0 && DistanceToCandy>700
            ?Current.ImpactPoint+(Candy.ImpactPoint-Current.ImpactPoint).GetSafeNormal2D()*420
            :Origin+FVector(FMath::Cos(Angle)*Distance,FMath::Sin(Angle)*Distance,0);
        if(!Tongue->InteriorSurfacePoint(Candidate,CircleRadius,Floor)) continue;
        const float NextDistance=float(FVector::DistXY(Candy.ImpactPoint,Floor.ImpactPoint));
        if(FVector::DistSquaredXY(Current.ImpactPoint,Floor.ImpactPoint)>FMath::Square(CircleTravel)
            || NextDistance>FMath::Max(700.f,DistanceToCandy-80)) continue;
        Anchor=Tongue->GetActorTransform().InverseTransformPosition(Floor.ImpactPoint); return true;
    }
    // A small or obstructed arena keeps a reachable warm patch instead of failing a transfer.
    Anchor=bFirst?CandyAnchor:SafeAnchor;
    return AnchorFloor(Anchor,Floor);
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

void AMCIceEvent::QueueIcicle()
{
    TArray<AMCToothCharacter*> Candidates;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        FHitResult Floor;
        if(!It->GetController() || !It->Status || !It->Status->IsAlive() || It->SwallowedBy || It->IsMimicCaptured()) continue;
        const FVector Feet=It->GetActorLocation()-FVector(0,0,It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        if(Tongue->SurfacePoint(Feet,Floor) && FMath::Abs(Feet.Z-Floor.ImpactPoint.Z)<150) Candidates.Add(*It);
    }
    if(Candidates.IsEmpty() || Strikes.Num()>=4) return;
    // Prefer groups sheltering together. Ties choose a seeded random player.
    AMCToothCharacter* Target=nullptr; int32 BestCount=0,Ties=0;
    for(auto* Candidate:Candidates)
    {
        int32 Count=0;
        for(auto* Other:Candidates)
            if(FVector::DistSquaredXY(Candidate->GetActorLocation(),Other->GetActorLocation())<FMath::Square(IcicleRadius)) ++Count;
        if(Count>BestCount) { Target=Candidate; BestCount=Count; Ties=1; }
        else if(Count==BestCount && Random.RandRange(1,++Ties)==1) Target=Candidate;
    }
    FHitResult Floor;
    if(!Target || !Tongue->SurfacePoint(Target->GetActorLocation(),Floor)) return;
    auto& Strike=Strikes.AddDefaulted_GetRef();
    Strike.Anchor=Tongue->GetActorTransform().InverseTransformPosition(Floor.ImpactPoint);
    Strike.ImpactAt=Now()+IcicleWarningSeconds; Strike.Id=++StrikeSerial;
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
            if(!bNextCircle && Time>=CircleStartedAt+CircleSeconds-CircleOverlapSeconds)
            { bNextCircle=ChooseCircle(NextSafeAnchor); ForceNetUpdate(); }
            if(Time>=CircleStartedAt+CircleSeconds)
            {
                if(bNextCircle) SafeAnchor=NextSafeAnchor;
                bNextCircle=false; CircleStartedAt=Time; ++CircleIndex; ForceNetUpdate();
            }
            UpdateFreeze(DeltaSeconds);
            if(Time>=NextIcicleAt) { QueueIcicle(); NextIcicleAt=Time+IcicleIntervalSeconds; }
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
    if(AnchorFloor(SafeAnchor,Floor))
        Mesh.Ring(Tongue,Transform,Floor.ImpactPoint,SafeRadius(),7,FLinearColor(.22f,1.f,.54f));
    if(bNextCircle && AnchorFloor(NextSafeAnchor,Floor))
        Mesh.Ring(Tongue,Transform,Floor.ImpactPoint,CircleRadius,5,FLinearColor(1.f,.86f,.25f));
    const double Time=Now();
    for(const auto& Strike:Strikes)
    {
        if(!AnchorFloor(Strike.Anchor,Floor) || Time>Strike.ImpactAt+1) continue;
        const float Progress=FMath::Clamp(1.f-float((Strike.ImpactAt-Time)/FMath::Max(1.f,IcicleWarningSeconds)),0.f,1.f);
        const FLinearColor Color=Strike.bImpacted?FLinearColor(.25f,.8f,1.f):FLinearColor(1.f,.12f,.08f);
        Mesh.Ring(Tongue,Transform,Floor.ImpactPoint,IcicleRadius,6,Color);
        if(!Strike.bImpacted) Mesh.Ring(Tongue,Transform,Floor.ImpactPoint,FMath::Max(8.f,IcicleRadius*Progress),3,Color);
    }
    FloorGuides->CreateMeshSection_LinearColor(0,Mesh.Vertices,Mesh.Indices,Mesh.Normals,Mesh.UV,Mesh.Colors,Mesh.Tangents,false);
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
    // Copy the rendered tongue, with no extra collision or permanent material edits.
    // Native vertex colors keep the prototype readable with any saved pressure material.
    const auto& WorldVertices=Tongue->CurrentWorldVertices();
    const auto& SourceIndices=Tongue->TriangleIndices();
    if(WorldVertices.IsEmpty() || SourceIndices.IsEmpty()) return;
    FIceGuideMesh Mesh;
    Mesh.Vertices.Reserve(WorldVertices.Num()); Mesh.Normals.Reserve(WorldVertices.Num());
    Mesh.UV.Reserve(WorldVertices.Num()); Mesh.Colors.Reserve(WorldVertices.Num()); Mesh.Tangents.Reserve(WorldVertices.Num());
    const FTransform Transform=FrostSurface->GetComponentTransform();
    for(int32 I=0;I<WorldVertices.Num();++I)
    {
        const FVector Point=WorldVertices[I];
        const float Grain=((uint32(I)*2654435761u)>>24)/255.f;
        Mesh.Vertices.Add(Transform.InverseTransformPosition(Point+FVector(0,0,2.5f)));
        Mesh.Normals.Add(FVector::UpVector); Mesh.UV.Add(FVector2D(Point.X/400,Point.Y/400));
        Mesh.Colors.Add(FMath::Lerp(FLinearColor(.30f,.62f,.83f),FLinearColor(.78f,.95f,1.f),Grain));
        Mesh.Tangents.Add(FProcMeshTangent(FVector::ForwardVector,false));
    }
    FHitResult SafeFloor,NextFloor;
    const bool bSafe=AnchorFloor(SafeAnchor,SafeFloor),bNext=bNextCircle && AnchorFloor(NextSafeAnchor,NextFloor);
    const float SafeSquared=FMath::Square(SafeRadius()),NextSquared=FMath::Square(CircleRadius),Frost=FrostAmount();
    Mesh.Indices.Reserve(SourceIndices.Num());
    for(int32 I=0;I+2<SourceIndices.Num();I+=3)
    {
        const int32 A=SourceIndices[I],B=SourceIndices[I+1],C=SourceIndices[I+2];
        if(!WorldVertices.IsValidIndex(A) || !WorldVertices.IsValidIndex(B) || !WorldVertices.IsValidIndex(C)) continue;
        const FVector Center=(WorldVertices[A]+WorldVertices[B]+WorldVertices[C])/3;
        if((bSafe && FVector::DistSquaredXY(Center,SafeFloor.ImpactPoint)<=SafeSquared)
            || (bNext && FVector::DistSquaredXY(Center,NextFloor.ImpactPoint)<=NextSquared)) continue;
        const float Growth=((uint32(I/3)*2246822519u)>>24)/255.f;
        if(Growth>Frost) continue;
        Mesh.Indices.Append({A,B,C});
    }
    if(Mesh.Indices.IsEmpty()) { FrostSurface->ClearAllMeshSections(); return; }
    FrostSurface->CreateMeshSection_LinearColor(0,Mesh.Vertices,Mesh.Indices,Mesh.Normals,Mesh.UV,Mesh.Colors,Mesh.Tangents,false);
}

void AMCIceEvent::RefreshPresentation(float DeltaSeconds)
{
    SetClimate();
    FHitResult Floor;
    const bool bCandyFloor=AnchorFloor(CandyAnchor,Floor);
    if(bCandyFloor)
    {
        const float Arrival=FMath::Clamp(float((Now()-StartedAt)/FMath::Max(1.f,ArrivalSeconds)),0.f,1.f);
        const float Height=MintRadius+FMath::Square(1-Arrival)*600;
        SetActorLocationAndRotation(Floor.ImpactPoint+Floor.ImpactNormal*Height,FRotator(90,0,0));
        SetActorScale3D(FVector(MintRadius/50,MintRadius/50,.75f));
    }
    Body->SetCollisionEnabled(bCandyFloor && Stage==EMCIceEventStage::Active?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
    if(GetNetMode()==NM_DedicatedServer) return;
    const bool bVisible=IsActive(); Body->SetVisibility(bVisible); CandyFace->SetVisibility(bVisible);
    GuideElapsed+=DeltaSeconds;
    if(GuideElapsed>=.1f) { GuideElapsed=0; RefreshFrostSurface(); RefreshFloorGuides(); }
    while(IcicleMeshes.Num()<Strikes.Num())
    {
        auto* Part=NewObject<UStaticMeshComponent>(this); Part->SetupAttachment(Body);
        Part->SetStaticMesh(ConeMesh); Part->SetMaterial(0,IcicleMaterial); Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetCastShadow(false); Part->RegisterComponent(); AddInstanceComponent(Part); IcicleMeshes.Add(Part);
    }
    const double Time=Now();
    for(int32 I=0;I<IcicleMeshes.Num();++I)
    {
        const bool bShow=Strikes.IsValidIndex(I) && AnchorFloor(Strikes[I].Anchor,Floor) && Time<Strikes[I].ImpactAt+.7f;
        UStaticMeshComponent* Part=IcicleMeshes[I]; Part->SetVisibility(bShow);
        if(!bShow) continue;
        const float Fall=FMath::Clamp(1-float((Strikes[I].ImpactAt-Time)/.45),0.f,1.f);
        const float Height=220+(1-Fall*Fall)*620;
        Part->SetWorldLocationAndRotation(Floor.ImpactPoint+Floor.ImpactNormal*Height,FRotator(180,0,0));
        Part->SetWorldScale3D(FVector(1.1f,1.1f,4.4f));
    }
    RefreshFreezeBars();
    RefreshIceCoatings();
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
    const FString Title=IsComplete()?TEXT("ЛЕДЕНЕЦ РАЗБИТ"):bFailed?TEXT("ЗИМА НЕ ЗАПУСТИЛАСЬ"):TEXT("ЗИМА БЛИЗКО");
    const FString Instruction=IsComplete()?TEXT("Лёд растаял. Выбери следующее событие в F3."):
        Stage==EMCIceEventStage::Arrival?TEXT("Падает мятный леденец. Найди зелёный круг; приготовь кирку (слот 2)."):
        TEXT("Разбей леденец киркой (слот 2). Зелёный и жёлтый круги отогревают; выбегай из красной отметки сосульки.");
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
    Players.Reset(); Strikes.Reset(); bNextCircle=false; Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AMCIceEvent::ClearPresentation()
{
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetVisibility(false); CandyFace->SetVisibility(false); FloorGuides->ClearAllMeshSections(); FrostSurface->ClearAllMeshSections();
    for(UStaticMeshComponent* Part:IcicleMeshes) if(IsValid(Part)) Part->DestroyComponent(); IcicleMeshes.Reset();
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
    SetActorTickEnabled(IsActive());
    if(!IsActive()) ClearPresentation();
}

void AMCIceEvent::Finish(bool bFailure)
{
    if(!HasAuthority()) return;
    bFailed=bFailure; Stage=EMCIceEventStage::Complete;
    ClearGameplay(); OnRep_State(); PublishManualHUD(); ForceNetUpdate();
}

void AMCIceEvent::Stop()
{
    if(!HasAuthority()) return;
    Stage=EMCIceEventStage::Idle; ClearGameplay(); OnRep_State(); ForceNetUpdate();
}

void AMCIceEvent::EndPlay(const EEndPlayReason::Type Reason)
{
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
    DOREPLIFETIME(AMCIceEvent,CircleSeconds); DOREPLIFETIME(AMCIceEvent,CircleOverlapSeconds);
    DOREPLIFETIME(AMCIceEvent,CircleRadius); DOREPLIFETIME(AMCIceEvent,MinCircleRadius);
    DOREPLIFETIME(AMCIceEvent,IcicleWarningSeconds); DOREPLIFETIME(AMCIceEvent,IcicleIntervalSeconds);
    DOREPLIFETIME(AMCIceEvent,IcicleRadius); DOREPLIFETIME(AMCIceEvent,IcicleDamage);
    DOREPLIFETIME(AMCIceEvent,Tongue); DOREPLIFETIME(AMCIceEvent,CandyAnchor);
    DOREPLIFETIME(AMCIceEvent,SafeAnchor); DOREPLIFETIME(AMCIceEvent,NextSafeAnchor); DOREPLIFETIME(AMCIceEvent,bNextCircle);
    DOREPLIFETIME(AMCIceEvent,StartedAt); DOREPLIFETIME(AMCIceEvent,CircleStartedAt); DOREPLIFETIME(AMCIceEvent,CircleIndex);
    DOREPLIFETIME(AMCIceEvent,Players); DOREPLIFETIME(AMCIceEvent,Strikes);
}
